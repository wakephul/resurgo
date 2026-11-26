# Tappa 5: aggiornamenti A/B firmati

Prove del 26/11/2026 in QEMU con KVM sul server .51, a 64 bit. Il disco a due sistemi parte anche a 32 bit e su ARM (aarch64 provato sul Mac). Gli scenari di aggiornamento li ho girati solo su x86_64.

## Come funziona

Il disco ha quattro partizioni:

| | contenuto |
| --- | --- |
| 1 AVVIO | FAT: GRUB, la sua configurazione e `grubenv`, che dice quale sistema avviare |
| 2 sistemaA | ext4 in sola lettura, con il suo kernel |
| 3 sistemaB | l'altra versione |
| 4 dati | `/var`, con dentro le case degli utenti (`/home` punta a `/var/home`) |

Per prima cosa avevo pensato a squashfs per i sistemi. L'initramfs di Alpine però trova la radice solo per `UUID=` o `LABEL=`, e squashfs non ha né l'uno né l'altro. Allora i sistemi sono in ext4, con etichetta `sistemaA` e `sistemaB`. L'aggiornamento resta compresso (zstd): 72 MB a 32 bit, 90 MB a 64 bit, 97 MB su ARM.

Un aggiornamento è una cartella con `sistema.img.zst`, `manifesto` (versione, architettura, dimensione, sha256) e `manifesto.firma`. `resurgo-aggiorna` fa questi passi in quest'ordine:

1. controlla la firma con la chiave pubblica dentro il sistema, poi architettura e versione (dev'essere più nuova, e non fra quelle che già non partivano);
2. scrive nella partizione che non è in uso, senza mai toccare l'altra;
3. rilegge quello che ha scritto e lo confronta con lo sha256 del manifesto;
4. solo alla fine, con un'unica scrittura di `grubenv`, dice a GRUB di provare la versione nuova con tre tentativi.

Se manca la corrente prima del punto 4, al riavvio parte la versione di sempre.

GRUB toglie un tentativo a ogni avvio di una versione in prova. Il servizio `resurgo-avvio` la conferma quando parte il desktop. Se il desktop non arriva in tempo (3 minuti, 60 secondi nelle prove), riavvia. Finiti i tentativi, GRUB torna alla versione di prima. Lì `resurgo-avvio` scrive l'avviso per l'utente e segna la versione che non partiva, perché non venga reinstallata.

## Risultati

| Scenario | Cosa succede | Esito |
| --- | --- | --- |
| corrente staccata 1 s dopo l'inizio della scrittura | QEMU ucciso a metà scrittura, poi riavviato | riparte la versione 1 dal sistema A, `grubenv` intatto |
| aggiornamento alla versione 2 | scritta nel sistema B, riavvio | parte la 2 "in prova", il desktop arriva, viene confermata |
| versione 3 guasta (senza compositore) | scritta nel sistema A, riavvio | tre avvii della 3, ognuno con "il desktop non è arrivato in 60 secondi, riavvio"; al quarto GRUB torna alla 2, che scrive "La versione 3 non partiva: sono tornato alla versione 2" e non la reinstalla |

Il criterio della tappa è soddisfatto: un aggiornamento interrotto staccando la corrente lascia il sistema avviabile.

In un primo giro, dopo il ritorno alla versione 2, il computer si è riavviato una volta in più. Nel secondo giro, con i log salvati, non è successo. Non so cosa l'abbia causato: lo segno come anomalia vista una volta.

## Problemi trovati per strada

- **Il pacchetto `grub` di Alpine,** installato nella radice, prova a configurare il disco su cui gira e fallisce nel container. Lo installo senza i suoi script: del sistema serve solo `grub-editenv`.
- **`grub-bios-setup` vuole un disco vero.** Ho scritto a mano quello che fa lui: `boot.img` nei primi 440 byte dell'MBR, `core.img` dal settore 1 e il numero dei suoi settori.
- **Con la radice in sola lettura** `mkdir` fuori da `/var`, `/tmp` e `/run` fallisce in silenzio. Lo script di prova cercava di montare in `/mnt`.
- **Su ARM, nel GRUB standalone,** non va cambiato `prefix`: i moduli stanno dentro l'eseguibile.
- **`tune2fs`** in Alpine sta in `e2fsprogs-extra`.

## Cosa manca

- dm-verity, cioè il controllo del sistema a ogni avvio e non solo quando si installa;
- scaricare solo la parte che cambia;
- una chiave di firma separata da quella del repository, tenuta fuori linea;
- le prove degli scenari a 32 bit e su ARM;
- lo scaricamento dell'aggiornamento da internet: per ora arriva da un disco collegato.
