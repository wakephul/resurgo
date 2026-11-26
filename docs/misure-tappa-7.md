# Tappa 7: ventole, batterie, armv7 e RISC-V

26/11/2026. Ventole e batterie sono provate su un `/sys` finto, perché in macchina virtuale non ci sono. armv7 e RISC-V sono provati in QEMU sul Mac, in emulazione completa.

## Mantice: ventole, temperatura, batteria

Il servizio `mantice-sorveglia` gira ogni 5 secondi:

- **Ventole.** Comanda solo i driver conosciuti: `pwm1` di it87, nct6775 e dell-smm, i livelli 1-7 del ThinkPad (con `thinkpad_acpi fan_control=1`, che l'immagine mette in `/etc/modprobe.d`) e applesmc dei Mac Intel. La curva sta al 30% fino a 50 °C e sale fino al 100% a 75 °C. Un sensore che legge zero o 130 °C e oltre (succede con gli ingressi scollegati) si scarta. Sopra gli 80 °C, o se non resta nessuna lettura credibile, le ventole tornano al BIOS. Come valore del BIOS vale solo un automatico: se all'avvio una ventola era già in manuale, si rimette l'automatico. Allo stop del servizio `mantice ripristina` rimette al BIOS solo quello che la sorveglianza ha toccato in questo avvio: se un'area era spenta, non la tocca.
- **Frequenza.** Sopra gli 80 °C il massimo del processore scende di un decimo dell'intervallo a ogni giro, mai sotto il 30%. Sotto i 70 °C risale.
- **Batteria.** All'avvio la carica si ferma all'80% e riparte sotto il 75%, dove il kernel lo permette (`charge_control_end_threshold`).

Se non c'è niente da sorvegliare, come in macchina virtuale, il servizio non parte e lo dice (`mantice utile`).

Prove automatiche (`cargo test`), con un `/sys` finto creato da `MANTICE_RADICE`:

| Situazione | Risultato |
| --- | --- |
| 45 °C | it87 in manuale al 30% (pwm 77), ThinkPad a livello 2 |
| 62,5 °C | it87 al 65% (pwm 166) |
| 85 °C | ventole al BIOS (valore originale, "level auto"), frequenza da 2000 a 1880 MHz |
| 85 °C per 20 giri | frequenza ferma a 1160 MHz, il 30% dell'intervallo |
| 60 °C | frequenza risale a 1280 MHz |
| un sensore a 150 °C, un altro a 60 °C | conta il 60 |
| solo letture impossibili | ventole al BIOS |
| ripristina senza sorveglianza in questo avvio | non scrive niente |
| ripristina dopo la sorveglianza | ventole al BIOS, frequenza al massimo |
| ventola già in manuale all'avvio | l'originale da rimettere è l'automatico |
| sensore sparito | ventole al BIOS |
| driver sconosciuto (finto "pippo") | non viene toccato |
| batteria | fine carica 80, inizio 75 |

Su un computer vero non è ancora provato.

## Il benvenuto e i permessi

Il benvenuto ora mostra anche Batteria e Ventole. Le regolazioni si spengono con `doas mantice disattiva AREA` e `riattiva AREA`. In `/etc/doas.d` c'è una regola per ogni comando esatto, e Mantice accetta solo le sei aree conosciute. Il file delle regolazioni spente resta di root. Nella macchina virtuale ho provato che il giro funziona e che un comando di Mantice non previsto viene rifiutato.

## armv7 e RISC-V

| | armv7 | riscv64 |
| --- | --- | --- |
| dove si costruisce | container armv7 emulato da Docker sul Mac | container riscv64 emulato da Docker sul Mac; Mantice compilato a parte, nativo sul Mac |
| avvio | GRUB UEFI (arm-efi), QEMU virt con edk2 | GRUB UEFI (riscv64-efi), QEMU virt con OpenSBI ed edk2 |
| sistema | 429 MB | 1.344 MB, in partizioni da 2 GB |
| aggiornamento compresso | 129 MB | 881 MB |
| in QEMU | parte fino al desktop, con Mantice e il benvenuto | non parte: il firmware UEFI (edk2) si ferma prima di GRUB, nello stesso punto sul Mac e sul .51, anche con la flash delle variabili. Da riprovare con U-Boot al posto di edk2 |

Su queste due architetture uso il Mesa di Alpine, con LLVM: il nostro Mesa è compilato solo per x86, x86_64 e aarch64. RISC-V pesa così tanto perché Alpine non ha `linux-virt` per riscv64. Si usa `linux-lts`, con i moduli di tutto l'hardware.

## Problemi trovati

- **L'etichetta di Docker cambia sotto i piedi.** Dopo un `docker run --platform linux/riscv64 alpine:3.24`, l'etichetta locale `alpine:3.24` resta all'immagine riscv64, e i container successivi senza piattaforma girano emulati. Ora la piattaforma si passa sempre.
- **Rust non compila sotto emulazione su RISC-V.** L'allocatore Scudo di Alpine va in crash. Mantice per riscv64 si compila sul Mac (`cargo build --target riscv64gc-unknown-linux-musl`, statico) e il pacchetto prende l'eseguibile pronto.
- **I comandi di Mantice per il benvenuto.** La prima versione cambiava proprietario e permessi del file come root, e la revisione di sicurezza l'ha segnalato. In più non poteva funzionare: il benvenuto doveva scrivere in una cartella di root. Ora passa da doas.

## Cosa manca

- ventole e temperatura su hardware vero;
- il nostro Mesa per armv7 e riscv64;
- un kernel più piccolo per RISC-V;
- le prove di aggiornamento su queste due architetture.
