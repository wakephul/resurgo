# Misure della tappa 3: Mantice, prima versione

Prova del 25/11/2026, a 32 bit (i686), in QEMU con KVM sul server .51, 2 processori. Ogni configurazione è stata provata una volta sola, quindi i numeri sono indicativi.

La prova (`PROVA=mantice`, script `build/prova/misura-mantice`) apre Wikipedia, Il Post e Repubblica insieme in Favilla. Dopo 90 secondi, con le tre schede aperte, lancia in una finestra nuova la pagina di calcolo JavaScript della tappa 2. Misuro così la reattività:
- **calcolo JS:** quanto ci mette quel calcolo;
- **PSI:** quanto tempo i processi restano fermi ad aspettare memoria, con `psi=1` all'avvio;
- **processi uccisi:** quelli chiusi dal kernel per mancanza di memoria.

## Cosa fa Mantice in questa versione

Lo trovi nel rapporto `mantice mostra`, che a 512 MB dice:

- memoria compressa (zram, lz4) grande quanto la RAM, swappiness 150, earlyoom acceso;
- disco meccanico: accesso con BFQ;
- desktop disegnato col processore;
- Favilla con al massimo 2 schede sveglie (sotto 1,2 GB; 6 fino a 2,5 GB; nessun limite sopra);
- cifratura consigliata: Adiantum, perché la CPU virtuale non ha AES-NI;
- frequenza del processore non regolabile in macchina virtuale.

## Risultati

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

Il criterio della tappa è soddisfatto: con 1 GB e tre schede aperte il sistema non aspetta mai la memoria e il calcolo JavaScript va come a riposo. Lo è però anche senza Mantice. Con queste tre pagine 1 GB basta comunque.

A 512 MB Mantice abbassa la memoria del browser di un quarto (da 298 a 222 MB) addormentando una scheda, e mette a disposizione 490 MB di swap compresso. Anche senza Mantice, però, non è morto nessun processo, e il tempo fermo per memoria è simile (1,0 s bloccato in tutti e due i casi). Questa prova è troppo leggera per mostrare dove Mantice fa la differenza: servono più schede, o pagine più pesanti, a 512 MB. È la prova da aggiungere.

## Problemi trovati per strada

- abuild usa `src/` come cartella di lavoro e la svuota: il sorgente di Mantice ora sta in `sorgente/`, e il manifesto Rust ha `[workspace]` perché cargo non risalga le cartelle.
- Alla prima scheda Favilla creava la pagina due volte (il quaderno sceglie subito la scheda appena aggiunta). Corretto.
- Due Favilla aperti insieme sullo stesso profilo si danno fastidio (database HSTS condiviso). Nella prova il secondo usa un profilo suo; nel sistema vero andrà evitato con un'istanza sola.
- Il kernel di Alpine ha PSI spento: ora l'immagine parte con `psi=1`.

## Cosa manca a Mantice

Il controllo della temperatura con la frequenza, le ventole, le batterie, lo schermo piccolo e l'accelerazione grafica sui driver provati. Per le ventole e la frequenza serve hardware vero: in macchina virtuale non ci sono.
