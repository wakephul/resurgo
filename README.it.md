<p align="center"><img src="brand/logo/resurgo-readme.svg" width="96" alt="Logo di Resurgo: una r minuscola rossa con il braccio che sale"></p>

# Resurgo

Resurgo è un sistema operativo libero per far tornare utilizzabili i computer vecchi, anche di 15 o 20 anni fa. È basato su Linux (Alpine), gira sui PC a 32 e a 64 bit e sulle schede ARM, e lo voglio sicuro e privato già appena installato, senza dover toccare impostazioni.

In English: [README.md](README.md)

Il progetto è appena partito e ancora non c'è niente da scaricare. Qui sotto c'è quello che voglio fare.

## Perché si chiama così

Resurgo in latino vuol dire "risorgo". Viene dal motto della fenice, *Post fata resurgo*: dopo la morte risorgo. La fenice rinasce dalle sue ceneri, e io vorrei fare lo stesso con i computer che tutti danno per morti e che invece funzionano ancora. Il logo è una r con il braccio alzato.

## Come lo voglio

Si adatta da solo al computer su cui lo metti. All'avvio guarda quanta RAM c'è, che processore, che disco e se le ventole si possono comandare, poi si regola: se la memoria è poca la comprime e tiene meno schede aperte nel browser, se il processore scalda gli abbassa la frequenza, se il disco è meccanico smette di indicizzare. Il programma che lo fa si chiama Mantice, e alla fine ti mostra cosa ha trovato e cosa ha cambiato, con la possibilità di annullare ogni cosa.

Ha un browser suo, Favilla, che gira anche sui processori a 32 bit e blocca pubblicità e tracciatori prima ancora di caricarli. La pubblicità viene bloccata anche per tutto il resto del sistema, a livello di DNS.

Il disco è cifrato, il sistema è in sola lettura e gli aggiornamenti arrivano in blocco: se uno va storto, al riavvio torna la versione di prima. Non c'è telemetria e non serve nessun account.

L'interfaccia è moderna ma senza animazioni e senza trasparenze, così va veloce anche con le schede grafiche di quegli anni.

## Su cosa gira

| Processore | Esempi |
| --- | --- |
| x86 a 32 bit | Pentium 4, Pentium M, Core Duo, gli Atom dei netbook |
| x86 a 64 bit | Core 2 Duo, Athlon 64 e tutto quello venuto dopo |
| ARM a 32 e 64 bit | Raspberry Pi dal 2 in poi |
| RISC-V | schede VisionFive e Milk-V, in prova |

Servono almeno 512 MB di RAM (con 1 GB si sta comodi) e 4 GB di disco. Non funziona sui processori senza SSE2 (Pentium III, Athlon XP) e sui Mac PowerPC, perché lì un browser moderno non si riesce a compilare.

## Come si installa

L'ho pensato perché ci riesca chiunque, anche chi non ha mai installato un sistema operativo.

Quando sarà pronto, si installerà così.

1. Scarichi un file solo, `resurgo-pc.iso`. Va bene per tutti i PC, sia a 32 che a 64 bit: all'avvio Resurgo capisce da solo quale versione usare.
2. Lo metti su una chiavetta USB qualsiasi, basta 1 GB, con [Raspberry Pi Imager](https://www.raspberrypi.com/software/), che è gratis e va su Windows, Mac e Linux. Il dispositivo lo salti, in fondo all'elenco dei sistemi scegli l'opzione per usare un file tuo, poi il file e poi la chiavetta. Il file sta sotto i 700 MB, quindi se il computer è così vecchio da non partire da USB lo puoi anche masterizzare su un CD.
3. Attacchi la chiavetta al computer vecchio, lo accendi e premi il tasto del menu di avvio (sono in tabella qui sotto).
4. Resurgo parte dalla chiavetta e lo puoi provare senza toccare niente di quello che c'è sul disco.
5. Se ti piace, clicchi "Installa". Le domande sono tre: lingua, disco e nome con password. La password serve sia per entrare sia per la cifratura del disco, quindi è una sola.

Se sul disco c'è Windows, prima di cancellare qualcosa Resurgo ti chiede se vuoi copiare documenti, foto e desktop su un'altra chiavetta o su un disco esterno.

| Marca | Tasto per il menu di avvio |
| --- | --- |
| Dell | F12 |
| HP | Esc, poi F9 |
| Lenovo | F12 (sugli IdeaPad il tastino Novo) |
| Acer | F12, a volte va prima attivato nel BIOS |
| Asus | Esc oppure F8 |
| Toshiba | F12 |
| Fujitsu | F12 |
| MSI | F11 |
| Sony Vaio | F11 oppure il tasto ASSIST |
| Mac Intel | tieni premuto Alt (Option); sui Mac dal 2018 con chip T2 prima va permesso l'avvio da USB in Utility Sicurezza Avvio |

Se la tua marca non c'è, guarda la primissima schermata all'accensione: di solito il tasto è scritto in basso.

Più avanti ci sarà anche un'app per Mac che fa partire Resurgo dentro una macchina virtuale, senza chiavette.

## A che punto siamo

Fatte, in macchina virtuale (i numeri sono in `docs/`):

1. Immagine minima che parte su x86, x86_64 e aarch64, sotto i 300 MB senza browser
2. Favilla: a 32 bit JavaScript non ha il JIT, una scheda costa da 40 a 140 MB
3. Mantice: con sei siti a 512 MB carica tutto, senza si blocca per minuti
4. Aggiornamenti A/B firmati: con la corrente staccata a metà riparte la versione di prima
5. Pannello, menu, impostazioni, benvenuto e tema, provati a 1024×600
6. Ventole, temperatura e batteria in Mantice; armv7 parte fino al desktop

Mancano:

- le prove su computer veri, dove servono soprattutto per ventole e temperatura
- RISC-V su una scheda vera (in QEMU parte, con U-Boot)
- la versione 0.1 da scaricare

## Come dare una mano

Il modo più utile adesso è dirmi su che computer vorresti provarlo. Apri una issue con marca, modello, RAM e anno, se lo sai. Mi serve per scegliere le macchine di prova e le ventole da supportare per prime.

## Licenza

Il codice è GPL-3.0 o successiva. Logo, immagini e documentazione sono CC BY-SA 4.0.
