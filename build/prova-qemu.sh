#!/bin/sh
# avvia un'immagine in qemu senza schermo, aspetta e salva una foto dello schermo.
# gira dentro un container alpine:3.24 (con --device /dev/kvm se c'e'):
#   docker run --rm --device /dev/kvm -v "$PWD/out":/out alpine:3.24 /resurgo/build/prova-qemu.sh x86_64
# lascia in /out: avvio-<arch>.log (console seriale) e schermo-<arch>.png
set -eu

ARCH=${1:?"architettura"}
ATTESA=${ATTESA:-60}
IMG=/out/resurgo-$ARCH.img
case $ARCH in
	x86) QEMU=qemu-system-i386; PKG=qemu-system-i386 ;;
	x86_64) QEMU=qemu-system-x86_64; PKG=qemu-system-x86_64 ;;
	*) echo "architettura non ancora supportata: $ARCH" >&2; exit 1 ;;
esac
apk add -q --no-cache $PKG qemu-hw-display-virtio-vga qemu-hw-display-virtio-gpu qemu-hw-display-virtio-gpu-pci imagemagick socat >/dev/null

ACC="-accel tcg"
[ -c /dev/kvm ] && ACC="-accel kvm"
cp "$IMG" /tmp/disco.img
$QEMU $ACC -m 1024 -smp 2 \
	-drive file=/tmp/disco.img,format=raw,if=virtio \
	-device virtio-vga -display none \
	-serial file:/out/avvio-$ARCH.log \
	-monitor unix:/tmp/monitor,server,nowait &
PID=$!
sleep "$ATTESA"
echo "screendump /tmp/schermo.ppm" | socat - UNIX-CONNECT:/tmp/monitor >/dev/null
sleep 2
magick /tmp/schermo.ppm /out/schermo-$ARCH.png
kill $PID
echo "fatto: /out/schermo-$ARCH.png"
