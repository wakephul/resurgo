#!/bin/sh
# compila un pacchetto di build/pacchetti con abuild e lo mette nel repository locale di resurgo.
# gira dentro un container alpine:3.24 come root:
#   docker run --rm -v "$PWD":/resurgo -v "$PWD/out":/out -v "$HOME/.resurgo-chiavi":/chiavi alpine:3.24 /resurgo/build/compila-pacchetto.sh mesa
# per x86 si usa l'immagine i386/alpine:3.24 sullo stesso host x86_64 (gira nativa, niente emulazione).
# la chiave privata sta fuori dal repository, in /chiavi; se manca la crea.
# esce con /out/repo/resurgo/<arch>/*.apk e l'indice firmato.
set -eu

PACCHETTO=${1:?"nome del pacchetto"}
QUI=$(cd "$(dirname "$0")" && pwd)

apk update -q >/dev/null && apk add -q alpine-sdk sudo >/dev/null
adduser -D costruttore 2>/dev/null || true
addgroup costruttore abuild
echo "costruttore ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/costruttore

# chiave del repository: la stessa per tutte le architetture
mkdir -p /chiavi
if [ ! -f /chiavi/resurgo.rsa ]; then
	openssl genrsa -out /chiavi/resurgo.rsa 4096 2>/dev/null
	openssl rsa -in /chiavi/resurgo.rsa -pubout -out /chiavi/resurgo.rsa.pub 2>/dev/null
fi
cp /chiavi/resurgo.rsa.pub /etc/apk/keys/
mkdir -p /home/costruttore/.abuild
cp /chiavi/resurgo.rsa /chiavi/resurgo.rsa.pub /home/costruttore/.abuild/
echo 'PACKAGER_PRIVKEY="/home/costruttore/.abuild/resurgo.rsa"' > /home/costruttore/.abuild/abuild.conf
echo "JOBS=$(nproc)" >> /home/costruttore/.abuild/abuild.conf

rm -rf /home/costruttore/resurgo
mkdir -p /home/costruttore/resurgo /out/repo
cp -r "$QUI/pacchetti/$PACCHETTO" /home/costruttore/resurgo/
# i programmi nostri (favilla, ...) hanno il sorgente nella radice del progetto
if [ -d "$QUI/../$PACCHETTO" ]; then cp "$QUI/../$PACCHETTO"/* /home/costruttore/resurgo/$PACCHETTO/; fi
chown -R costruttore /home/costruttore /out/repo

LOG=/out/abuild-$PACCHETTO-$(apk --print-arch).log
if ! su costruttore -c "cd /home/costruttore/resurgo/$PACCHETTO && abuild checksum && REPODEST=/out/repo abuild -r" > "$LOG" 2>&1; then
	echo "abuild fallito, guarda $LOG" >&2
	grep -E "ERROR|error:" "$LOG" | tail -20 >&2
	exit 1
fi
cp /chiavi/resurgo.rsa.pub /out/repo/resurgo/
ls -la /out/repo/resurgo/*/
