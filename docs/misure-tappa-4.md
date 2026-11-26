# Tappa 4: pronti per i computer veri

26/11/2026. Questa tappa si chiude solo quando Resurgo avrà girato su qualche computer vero, e quelli non li ho. Intanto ho preparato le immagini per PC perché ci possano partire, e un modo per sapere com'è andata anche senza esserci.

Le prove qui sotto sono ancora in QEMU, sul .51.

## Il kernel con i driver veri

Fino alla tappa 7 le immagini per PC avevano `linux-virt`, il kernel di Alpine per le macchine virtuali: niente driver per le schede video, il Wi-Fi e buona parte dei dischi veri. Ora hanno `linux-lts`. `KERNEL=virt` fa ancora l'immagine solo per macchine virtuali, più piccola.

`linux-lts` ha 143 MB di moduli. Tolgo quelli che su un PC vecchio non servono: InfiniBand, webcam e schede TV, Xen e simili, le schede AMD dal 2014 in poi (`amdgpu`) e le Intel Xe, i filesystem da server (btrfs, xfs, ceph...). Del firmware tengo solo quello per le schede di quegli anni:

- Radeon, cioè le schede ATI fino al 2013;
- Realtek, per il cavo e per il Wi-Fi;
- Broadcom;
- Atheros USB;
- il Wi-Fi Intel fino alla serie 8000, cioè fino al 2015 circa.

Le Intel vecchie per la grafica non vogliono firmware.

L'initramfs porta solo i driver per arrivare al disco: SATA e IDE, USB, NVMe, schede SD. Con i driver video dentro, come vuole Alpine di base, pesava 172 MB.

| | x86_64 | x86 |
| --- | --- | --- |
| sistema prima (linux-virt) | 257 MB | 239 MB |
| sistema ora (linux-lts) | 496 MB | 420 MB |
| aggiornamento compresso | 228 MB | 168 MB |
| immagine da scrivere, compressa | 241 MB | 177 MB |

Il sistema sta ancora nelle partizioni da 1 GB. Sopra il limite dei 500 MB però ci va già vicino. Il prossimo da togliere sono i moduli di rete e audio da server. Prima voglio vedere dai rapporti cosa serve davvero.

## BIOS e UEFI sulla stessa chiavetta

La partizione AVVIO ora contiene anche GRUB per UEFI: `BOOTX64.EFI` per x86_64, `BOOTIA32.EFI` per x86. Quest'ultimo è per i PC a 32 bit con UEFI, i primi Atom e i Mac del 2006. Il GRUB per il BIOS resta dov'era, dopo l'MBR.

La tabella delle partizioni resta MBR e la partizione AVVIO resta di tipo FAT32 normale (`c`), non ESP (`ef`). I firmware UEFI la leggono lo stesso, come fanno con le chiavette preparate con Rufus. In più Windows le dà una lettera, e questo serve per leggere il rapporto.

| | BIOS | UEFI |
| --- | --- | --- |
| x86_64 | parte fino al desktop | parte fino al desktop |
| x86 | parte fino al desktop | parte fino al desktop (edk2 a 32 bit) |

Secure Boot non è supportato: va spento.

Ho rifatto anche le prove della tappa 5 su x86_64 col kernel nuovo: corrente staccata durante l'aggiornamento, aggiornamento riuscito, versione guasta con ritorno alla precedente. Riescono tutte e tre.

## Il rapporto sull'hardware

A ogni avvio, dopo due minuti, `resurgo-rapporto` scrive un file di testo nella partizione AVVIO, in `rapporti/`. Contiene:

- modello del computer e BIOS;
- processore e memoria;
- dispositivi PCI e USB, con i loro driver;
- dischi;
- schermi;
- rete;
- sensori, ventole e batteria;
- cosa ha fatto Mantice;
- la pressione su processore, memoria e disco (PSI);
- i servizi;
- gli errori e il firmware mancante nel registro del kernel.

Non contiene numeri di serie, indirizzi MAC o nomi utente, così si può allegare a una issue. Un esempio, preso in QEMU, è in `rapporto-esempio-qemu.txt`.

`prova-qemu.sh` ora copia i rapporti fuori dal disco virtuale, in `out/rapporti-ARCH`. Con `FIRMWARE=uefi` prova l'avvio UEFI dei PC.

## Come si prova

Tutto il procedimento è in `prova-su-pc-vero.md`: scrivere la chiavetta, scegliere da dove partire, cosa guardare, dove trovare il rapporto.

## Cosa manca

- le prove vere, su più computer possibile e di anni diversi;
- il Wi-Fi: le schede vengono riconosciute, ma manca il programma per collegarsi (iwd);
- il kernel per le schede ARM vere (Raspberry Pi);
- Favilla nelle immagini normali.
