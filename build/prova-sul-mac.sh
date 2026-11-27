#!/bin/sh
# prova resurgo in una finestra su un mac con apple silicon, senza installare niente: compila
# build/mac/resurgo-vm.swift (serve swiftc, che c'e' con xcode o con gli strumenti da riga di comando)
# e gli fa avviare una copia del disco aarch64. x86 e x86_64 su questi mac non si virtualizzano.
#   build/prova-sul-mac.sh           riparte dal disco dell'ultima volta (out/mac/disco.img)
#   build/prova-sul-mac.sh da-capo   rifa il disco dall'immagine costruita
# variabili: IMMAGINE (default out/resurgo-aarch64.img), MEMORIA_MB (default 1024; 512 come un pc vecchio)
set -eu

QUI=$(cd "$(dirname "$0")" && pwd)
OUT=$(cd "$QUI/.." && pwd)/out
M=$OUT/mac
IMMAGINE=${IMMAGINE:-$OUT/resurgo-aarch64.img}
mkdir -p "$M"

if [ "$(uname -m)" != arm64 ]; then echo "serve un mac con apple silicon" >&2; exit 1; fi

if [ ! -x "$M/resurgo-vm" ] || [ "$QUI/mac/resurgo-vm.swift" -nt "$M/resurgo-vm" ]; then
	swiftc -O -o "$M/resurgo-vm" "$QUI/mac/resurgo-vm.swift"
fi
if ! codesign -d --entitlements - "$M/resurgo-vm" 2>/dev/null | grep -q virtualization; then
	# senza questo permesso macOS non lascia usare Virtualization.framework
	cat > "$M/permessi.plist" <<-FINE
	<?xml version="1.0" encoding="UTF-8"?>
	<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
	<plist version="1.0"><dict><key>com.apple.security.virtualization</key><true/></dict></plist>
	FINE
	codesign -s - -f --entitlements "$M/permessi.plist" "$M/resurgo-vm" 2>/dev/null
fi

if [ "${1:-}" = da-capo ] || [ ! -f "$M/disco.img" ]; then
	if [ ! -f "$IMMAGINE" ]; then
		echo "manca $IMMAGINE: va costruita prima (vedi build/costruisci.sh)" >&2
		exit 1
	fi
	rm -f "$M/disco.img" "$M/disco.img.efi"
	# su apfs cp -c fa una copia istantanea che non occupa spazio finche' non cambia
	cp -c "$IMMAGINE" "$M/disco.img" 2>/dev/null || cp "$IMMAGINE" "$M/disco.img"
elif [ "$IMMAGINE" -nt "$M/disco.img" ]; then
	echo "c'e' un'immagine piu' nuova del disco: per usarla, build/prova-sul-mac.sh da-capo"
fi

exec "$M/resurgo-vm" "$M/disco.img" "${MEMORIA_MB:-1024}"
