# Misure della tappa 2: Favilla a 32 e 64 bit

Misure del 25/11/2026 sul prototipo di Favilla (WebKitGTK 2.48.7 di Alpine 3.24), in QEMU con KVM sul server .51: 1 GB di RAM, 2 processori, desktop disegnato col processore. Sono numeri di una macchina virtuale su un processore moderno. Su un computer vero di quegli anni JavaScript sarà parecchio più lento, e le misure vanno rifatte lì (tappa 4).

I numeri della memoria sono dell'ultimo giro, quelli di JavaScript di tutti i giri. Le misure partono da sole nell'immagine di prova (`PROVA=favilla DNS=ricorsivo build/costruisci.sh`), con lo script `build/prova/misura-favilla`. La memoria è la PSS sommata di Favilla e dei processi di WebKit.

## JavaScript: a 32 bit il JIT non c'è

| | x86 (i686) | x86_64 |
| --- | --- | --- |
| pagina di prova, normale | 1065-1508 ms | 517-573 ms |
| stessa pagina con `JSC_useJIT=false` | 1094-1123 ms | 830-930 ms |

A 32 bit i tempi con e senza JIT sono uguali: WebKit su i686 usa solo l'interprete. Alpine lo spegne esplicitamente solo su riscv64, ma su i686 WebKit non ha più un JIT da anni. Sulla stessa CPU il 32 bit è circa 2 volte più lento del 64 bit con JIT, e circa 1,3 volte più lento del 64 bit senza JIT.

## Memoria

| | x86 (i686) | x86_64 |
| --- | --- | --- |
| browser vuoto | 179 MB | 231 MB |
| Wikipedia | 219-240 MB | 314-336 MB |
| Il Post | 308-317 MB | 353-367 MB |
| Repubblica | 282-291 MB | 344-386 MB |
| le tre insieme, solo blocco DNS | 374 MB | 548 MB |
| le tre insieme, blocco DNS e WebKit | 305 MB | 564 MB |

Il browser vuoto sta già sopra l'obiettivo di 120 MB. Una scheda in più costa da 40 MB (Wikipedia) a 140 MB (Il Post). Su un computer con 1 GB le tre pagine insieme stanno in memoria, a 32 bit restano circa 500 MB liberi, ma non c'è margine per molto altro.

## Blocco della pubblicità

Il blocco ora sta su due livelli:

- **DNS di sistema (Unbound):** 87.959 domini interi presi da EasyList, EasyPrivacy ed EasyList Italy. Unbound con questo elenco occupa 30 MB a 32 bit e 45 MB a 64 bit.
- **WebKit:** le altre 34.895 regole (percorsi, regole di terze parti, regole estetiche). Il JSON da 5 MB si compila nel pacchetto: 13,5 s a 32 bit e 8,4 s a 64 bit sul server, 22,6 MB di filtro compilato. Favilla lo carica pronto in 0,08 s.

Con tutte le regole dentro WebKit il filtro compilato pesava 65 MB e ci metteva 58 s. Sull'immagine di prova riempiva il disco e la compilazione falliva.

Tempi di caricamento con il filtro di WebKit acceso, contro il solo DNS: Il Post scende da 7,6 a 4,3 s a 32 bit e da 8,3 a 3,3 s a 64 bit. Su Repubblica e Wikipedia la differenza sta dentro il rumore di una misura singola. Servono più ripetizioni per dire di quanto aiuta davvero.

## Problemi trovati

- **Il video dentro la pagina fa cadere WebKit.** Il pacchetto di Alpine non si porta dietro i plugin di GStreamer, e senza la prima pagina con un video manda in crash il processo. Con `gst-plugins-base` e `gst-plugins-good` gli errori spariscono, ma Repubblica cadeva ancora. In Favilla il video dentro la pagina ora è spento di default (`--con-media` per accenderlo): i video andranno aperti in mpv.
- **Il DNS cifrato può essere bloccato dalla rete.** La rete del .51 chiude sia il DNS su TLS (porta 853) sia il DNS su HTTPS verso Quad9 e Cloudflare. Il ripiego è Unbound ricorsivo, che chiede ai server radice in chiaro (`DNS=ricorsivo`). Il passaggio automatico tra le due modalità lo deve fare Mantice.
- **L'immagine con il browser pesa troppo:** 542 MB a 32 bit e 597 MB a 64 bit, contro i 239 e 257 MB dell'immagine senza browser. WebKitGTK, GStreamer e le loro librerie valgono circa 300 MB.

## Cosa resta da fare

- rifare le misure su un computer vero a 32 bit;
- ridurre la memoria del browser vuoto;
- far stare WebKit dentro il limite dei 300 MB;
- ripetere le misure sul blocco per avere numeri affidabili.
