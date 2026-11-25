#!/usr/bin/env python3
"""converte liste in formato adblock (easylist e simili) nelle regole json del blocco contenuti di webkit.

uso: converti.py [--domini domini.txt] lista1.txt lista2.txt ... > blocco.json
con --domini le regole che bloccano un dominio intero (||esempio.com^, senza condizioni) vanno in un elenco
a parte per il dns di sistema, che le gestisce molto meglio, e non finiscono nel json di webkit.
le regole che webkit non sa esprimere vengono saltate; il conteggio finisce su stderr.
"""
import json
import re
import sys

# tipi di risorsa adblock -> tipi webkit
TIPI = {
    "script": "script", "image": "image", "stylesheet": "style-sheet", "font": "font",
    "media": "media", "subdocument": "document", "xmlhttprequest": "fetch", "ping": "ping",
    "websocket": "websocket", "popup": "popup", "other": "raw", "object": "raw",
}
# opzioni che cambiano il significato della regola in modi che webkit non sa fare: regola saltata
NON_GESTITE = {"redirect", "redirect-rule", "removeparam", "csp", "rewrite", "replace", "header",
               "permissions", "urltransform", "badfilter", "generichide", "elemhide", "genericblock",
               "document", "match-case", "denyallow", "to", "method", "1p", "strict1p", "strict3p"}


def url_filter(modello):
    """da modello adblock a espressione regolare nel sottoinsieme che webkit accetta."""
    inizio = fine = ""
    if modello.startswith("||"):
        modello = modello[2:]
        inizio = r"^[a-z][a-z0-9.+-]*://([^/]*\.)?"
    elif modello.startswith("|"):
        modello = modello[1:]
        inizio = "^"
    if modello.endswith("|"):
        modello = modello[:-1]
        fine = "$"
    pezzi = []
    for c in modello:
        if c == "*":
            pezzi.append(".*")
        elif c == "^":
            pezzi.append("[^a-zA-Z0-9_.%-]")
        elif c in r".+?()[]{}\$|":
            pezzi.append("\\" + c)
        else:
            pezzi.append(c)
    corpo = "".join(pezzi)
    # il separatore finale puo' anche essere la fine dell'indirizzo
    if corpo.endswith("[^a-zA-Z0-9_.%-]"):
        corpo = corpo[: -len("[^a-zA-Z0-9_.%-]")] + "([^a-zA-Z0-9_.%-].*)?"
        fine = fine or "$"
    if not corpo.strip(".*"):
        return None
    return inizio + corpo + fine


def domini(valore):
    si, no = [], []
    for d in valore.split("|"):
        d = d.strip().lower()
        if not d or "*" in d or d.startswith("/"):
            continue
        (no if d.startswith("~") else si).append("*" + d.lstrip("~"))
    return si, no


def regola_rete(riga):
    eccezione = riga.startswith("@@")
    if eccezione:
        riga = riga[2:]
    if riga.startswith("/") and riga.endswith("/"):
        return None  # espressioni regolari adblock: troppo diverse da quelle di webkit
    modello, _, opzioni = riga.rpartition("$") if "$" in riga else (riga, "", "")
    trigger = {}
    tipi, terzi = [], None
    for o in filter(None, opzioni.split(",")):
        nome, _, valore = o.partition("=")
        nome = nome.lower()
        negata = nome.startswith("~")
        nome = nome.lstrip("~")
        if nome in NON_GESTITE:
            return None
        if nome in ("third-party", "3p"):
            terzi = not negata
        elif nome == "domain":
            si, no = domini(valore)
            if si and no:
                return None
            if si:
                trigger["if-domain"] = si
            elif no:
                trigger["unless-domain"] = no
        elif nome in TIPI:
            if negata:
                return None  # "tutto tranne questo tipo": webkit non lo sa dire
            tipi.append(TIPI[nome])
        elif nome in ("important", "all"):
            continue
        else:
            return None
    f = url_filter(modello)
    if not f or not f.isascii():
        return None  # webkit accetta solo indirizzi ascii
    trigger["url-filter"] = f
    if tipi:
        trigger["resource-type"] = sorted(set(tipi))
    if terzi is not None:
        trigger["load-type"] = ["third-party" if terzi else "first-party"]
    return {"trigger": trigger, "action": {"type": "ignore-previous-rules" if eccezione else "block"}}


def regola_estetica(riga):
    dominio, _, selettore = riga.partition("##")
    if not selettore or "#@#" in riga or "#?#" in riga or "#$#" in riga or ":-abp-" in selettore or "+js(" in selettore:
        return None
    if any(x in selettore for x in (":has(", ":has-text(", ":upward(", ":xpath(", ":matches-", ":remove(", ":style(")):
        return None
    trigger = {"url-filter": ".*"}
    if dominio:
        si, no = domini(dominio.replace(",", "|"))
        if si and no:
            return None
        if si:
            trigger["if-domain"] = si
        elif no:
            trigger["unless-domain"] = no
    return {"trigger": trigger, "action": {"type": "css-display-none", "selector": selettore}}


SOLO_DOMINIO = re.compile(r"^\|\|([a-z0-9.-]+\.[a-z0-9-]+)\^(\$(all|important)(,(all|important))*)?$")
ECCEZIONE_DOMINIO = re.compile(r"^@@\|\|([a-z0-9.-]+\.[a-z0-9-]+)\^(\$(all|important)(,(all|important))*)?$")


def main():
    argomenti = sys.argv[1:]
    file_domini = None
    if argomenti[:1] == ["--domini"]:
        file_domini, argomenti = argomenti[1], argomenti[2:]
    blocchi, eccezioni, estetiche, saltate = [], [], [], 0
    generiche = []
    domini_dns, domini_salvi = set(), set()
    for percorso in argomenti:
        for riga in open(percorso, encoding="utf-8", errors="replace"):
            riga = riga.strip()
            if not riga or riga.startswith(("!", "[")):
                continue
            if "##" in riga or "#@#" in riga or "#?#" in riga or "#$#" in riga:
                r = regola_estetica(riga)
                if r is None:
                    saltate += 1
                elif list(r["trigger"]) == ["url-filter"]:
                    generiche.append(r["action"]["selector"])
                else:
                    estetiche.append(r)
                continue
            if file_domini:
                m = SOLO_DOMINIO.match(riga)
                if m:
                    domini_dns.add(m.group(1))
                    continue
                m = ECCEZIONE_DOMINIO.match(riga)
                if m:
                    domini_salvi.add(m.group(1))
            r = regola_rete(riga)
            if r is None:
                saltate += 1
            elif r["action"]["type"] == "block":
                blocchi.append(r)
            else:
                eccezioni.append(r)
    # i selettori generici si raggruppano: una regola ogni 500 selettori, molto piu' veloce da compilare
    for i in range(0, len(generiche), 500):
        estetiche.insert(0, {"trigger": {"url-filter": ".*"},
                             "action": {"type": "css-display-none", "selector": ", ".join(generiche[i:i + 500])}})
    # le eccezioni devono venire dopo i blocchi che annullano
    regole = estetiche + blocchi + eccezioni
    print(f"blocchi {len(blocchi)}, eccezioni {len(eccezioni)}, estetiche {len(estetiche)} "
          f"({len(generiche)} selettori generici), saltate {saltate}", file=sys.stderr)
    if file_domini:
        domini_dns -= domini_salvi
        with open(file_domini, "w") as f:
            f.write("\n".join(sorted(domini_dns)) + "\n")
        print(f"domini al dns {len(domini_dns)}", file=sys.stderr)
    json.dump(regole, sys.stdout, separators=(",", ":"))


if __name__ == "__main__":
    main()
