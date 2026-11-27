# Provare Resurgo su un computer vero

Questa è la tappa 4. Finora Resurgo l'ho provato solo in macchine virtuali, che però non hanno ventole, sensori di temperatura, batterie, schede video e Wi-Fi veri. Serve vedere come va su computer veri, soprattutto vecchi.

La prova non tocca il disco del computer. Resurgo parte dalla chiavetta e scrive solo lì, quindi Windows o qualunque cosa ci sia installata resta com'è.

## Cosa serve

- un computer con un processore x86 (Intel o AMD), anche a 32 bit, di qualunque anno dal 2005 circa in poi. Il processore deve avere SSE2: vanno bene Pentium 4, Pentium M, Atom e tutti quelli dopo, non vanno Pentium III e Athlon XP;
- una chiavetta USB da almeno 4 GB. Quello che c'è sopra verrà cancellato;
- l'immagine: `resurgo-x86_64.img` per i computer a 64 bit, `resurgo-x86.img` per quelli a 32 bit (Pentium 4, Pentium M, i primi Atom). Se non sai quale, prova prima la 64 bit: se il processore non la regge, si ferma subito con un messaggio.

## Scrivere la chiavetta

Se l'immagine finisce in `.img.zst` è compressa: prima va scompattata, con [7-Zip](https://www.7-zip.org) (nelle versioni recenti) su Windows o con `zstd -d resurgo-x86_64.img.zst` su Mac e Linux. Viene un file da 700-800 MB.

Il modo più semplice è con [balenaEtcher](https://etcher.balena.io) o con [Raspberry Pi Imager](https://www.raspberrypi.com/software/) ("Usa file personalizzato"). Scegli l'immagine, scegli la chiavetta, scrivi.

Da Linux o dal Mac si può anche col terminale. Attenzione a scegliere il disco giusto, perché `dd` non chiede conferma:

```sh
sudo dd if=resurgo-x86_64.img of=/dev/sdX bs=4M conv=fsync status=progress
```

Sul Mac il disco si chiama tipo `/dev/rdisk4` (lo vedi con `diskutil list`) e prima va smontato con `diskutil unmountDisk /dev/disk4`.

## Avviare dalla chiavetta

1. Infila la chiavetta e accendi il computer.
2. Premi il tasto per scegliere da dove partire. Ce n'è una tabella nel README, di solito è F12, F9, F11 o Esc.
3. Scegli la chiavetta. Sui computer con UEFI può comparire due volte, una con "UEFI" davanti: vanno bene tutte e due, e se una non parte prova l'altra.

Se compare un errore di "Secure Boot", va spento nelle impostazioni del BIOS/UEFI. Resurgo per ora non è firmato per Secure Boot.

Sui Mac Intel si tiene premuto Alt (Option) all'accensione e si sceglie "EFI Boot".

## Cosa guardare

Dopo un minuto circa, sui computer vecchi anche di più, dovrebbe comparire il desktop con la finestra di benvenuto. Lascialo acceso almeno **tre minuti**: dopo due minuti Resurgo scrive un rapporto sull'hardware nella chiavetta.

Mentre aspetti, se puoi, segnati:

- se le ventole sono più silenziose o più rumorose del solito;
- se lo schermo è alla risoluzione giusta;
- se tastiera, mouse e touchpad funzionano;
- se ci sono scritte strane prima del desktop. Una foto col telefono va benissimo;
- se il computer ha il Wi-Fi: clicca su "Nessuna rete" in basso a destra, scegli la tua rete e scrivi la password. Se la rete non compare, o non si collega, segnatelo.

Mantice comanda le ventole solo dei driver che conosce, e sopra gli 80 gradi le restituisce sempre al BIOS. Se comunque qualcosa non ti convince, spegni il computer. Nel benvenuto si può anche spegnere l'interruttore "Ventole e temperatura": vale dal prossimo avvio.

Poi spegni dal menu in basso a sinistra (la r), "Spegni".

## Il rapporto

Infila la chiavetta in un altro computer. Comparirà un'unità che si chiama AVVIO. Dentro, nella cartella `rapporti`, c'è un file di testo per ogni avvio, con la data e il nome del modello.

Nel rapporto ci sono: il modello del computer, il processore, la memoria, le schede (video, rete, Wi-Fi, audio) e i loro driver, i sensori di temperatura e le ventole, la batteria, cosa ha regolato Mantice e gli errori del kernel. Non ci sono numeri di serie, indirizzi MAC o nomi utente: lo puoi allegare tranquillamente a una issue su GitHub, insieme alle tue note.

Se il desktop non arriva mai, il rapporto non c'è. In quel caso servono la foto dello schermo e il modello del computer.

## Cosa non c'è ancora

- le reti Wi-Fi aziendali o dell'università (802.1X, eduroam): per ora solo dal terminale;
- il browser, Favilla, che nelle immagini normali non c'è ancora;
- l'installazione sul disco del computer.
