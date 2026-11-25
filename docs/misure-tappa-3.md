# Misure della tappa 3: Mantice, prima versione

Prova del 25/11/2026, a 32 bit (i686), in QEMU con KVM sul server .51, 2 processori. Ogni configurazione è stata provata una volta sola, quindi i numeri sono indicativi.

La prova (`PROVA=mantice`, script `build/prova/misura-mantice`) apre Wikipedia, Il Post e Repubblica insieme in Favilla. Dopo 90 secondi, con le tre schede aperte, lancia in una finestra nuova la pagina di calcolo JavaScript della tappa 2. Misuro così la reattività:
- **calcolo JS:** quanto ci mette quel calcolo;
- **PSI:** quanto tempo i processi restano fermi ad aspettare memoria, con `psi=1` all'avvio;
- **processi uccisi:** quelli chiusi dal kernel per mancanza di memoria.

## Cosa fa Mantice in questa versione

Lo trovi nel rapporto `mantice mostra`, che a 512 MB dice:

- memoria compressa (zram, lz4) grande quanto la RAM, swappiness 150;
- earlyoom sempre acceso (dal suo servizio, non da Mantice: OpenRC lo spegneva passando alla fase normale);
- disco meccanico: accesso con BFQ;
- desktop disegnato col processore;
- Favilla con al massimo 2 schede sveglie (sotto 1,2 GB; 6 fino a 2,5 GB; nessun limite sopra);
- cifratura consigliata: Adiantum, perché la CPU virtuale non ha AES-NI;
- frequenza del processore non regolabile in macchina virtuale.

## Sei siti a 512 MB: qui Mantice fa la differenza

Prova del 26/11, con Wikipedia, Il Post, Repubblica, Corriere, ANSA e un'altra voce di Wikipedia aperti insieme. Favilla chiude i processi di WebKit quando esce, quindi la fase a sei schede parte pulita.

| 512 MB | con Mantice | senza Mantice |
| --- | --- | --- |
| siti caricati in 5 minuti | 6 su 6 (4 addormentati) | 3 su 6 |
| memoria del browser | 214 MB | 372 MB |
| tempo fermo per memoria, tutta la prova (PSI some) | 3,6 s | 263 s |
| tempo bloccato del tutto (PSI full) | 2,1 s | 165 s |
| swap compresso usato | 30 MB | nessuno swap |
| processi uccisi | 0 | 0 |

Senza Mantice il computer resta bloccato per quasi tre minuti su cinque e metà delle pagine non arriva. Con Mantice carica tutto e si ferma per tre secondi e mezzo.

Un primo giro (25/11 notte) aveva dato lo stesso quadro: 6 su 6 e 4,2 s fermi con Mantice, 2 su 6 e 273 s senza. In quel giro però i processi di WebKit della fase a tre schede restavano vivi durante quella a sei, e penalizzavano il caso senza Mantice. Per questo l'ho rifatto.

## Tre siti

| | 512 MB con Mantice | 512 MB senza | 1 GB con Mantice | 1 GB senza |
| --- | --- | --- | --- | --- |
| memoria del browser con 3 siti | 222 MB | 298 MB | 328 MB | 427 MB |
| schede addormentate | Il Post | nessuna | Il Post | nessuna |
| calcolo JS con 3 schede aperte | 1183 ms | 1156 ms | 1227 ms | 1128 ms |
| tempo fermo per memoria (PSI some) | 1,4 s | 1,7 s | 0 | 0 |
| tempo bloccato del tutto (PSI full) | 1,0 s | 1,0 s | 0 | 0 |
| swap compresso usato | 29 MB | nessuno swap | 0 | nessuno swap |
| processi uccisi | 0 | 0 | 0 | 0 |

A riposo, senza schede, lo stesso calcolo JS ci mette circa 1.140 ms.

## Cosa vuol dire

Il criterio della tappa è soddisfatto: con 1 GB e tre schede aperte il sistema non aspetta mai la memoria e il calcolo JavaScript va come a riposo. Con tre pagine però 1 GB basta anche senza Mantice. A 512 MB e tre siti Mantice abbassa la memoria del browser di un quarto, ma il sistema regge in tutti e due i casi. La differenza vera si vede con sei siti a 512 MB, nella tabella sopra.

## Problemi trovati per strada

- **Disco pieno.** Le immagini di prova avevano il 10% di spazio libero (circa 50 MB) e la cache di WebKit lo riempiva. La scrittura dei risultati falliva senza errori, e Favilla sembrava uscire senza dire niente. Adesso le immagini di prova hanno 512 MB liberi in più (`SPAZIO_MB`). Varrà anche per il sistema vero: con la radice in sola lettura, cache e dati dovranno stare in una partizione con spazio.
- **Processi rimasti.** I processi di WebKit restavano in memoria dopo la chiusura di Favilla: ora Favilla li chiude prima di uscire.
- abuild usa `src/` come cartella di lavoro e la svuota: il sorgente di Mantice ora sta in `sorgente/`, e il manifesto Rust ha `[workspace]` perché cargo non risalga le cartelle.
- Alla prima scheda Favilla creava la pagina due volte (il quaderno sceglie subito la scheda appena aggiunta). Corretto.
- Due Favilla aperti insieme sullo stesso profilo si danno fastidio (database HSTS condiviso). Nella prova il secondo usa un profilo suo; nel sistema vero andrà evitato con un'istanza sola.
- Il kernel di Alpine ha PSI spento: ora l'immagine parte con `psi=1`.

## Cosa manca a Mantice

Il controllo della temperatura con la frequenza, le ventole, le batterie, lo schermo piccolo e l'accelerazione grafica sui driver provati. Per le ventole e la frequenza serve hardware vero: in macchina virtuale non ci sono.
