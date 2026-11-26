#!/bin/sh
# avvia un'immagine in qemu senza schermo, aspetta e salva una foto dello schermo.
# gira dentro un container alpine:3.24 (con --device /dev/kvm se c'e'):
#   docker run --rm --device /dev/kvm -v "$PWD/out":/out alpine:3.24 /resurgo/build/prova-qemu.sh x86_64
# lascia in /out: avvio-<arch>.log (console seriale) e schermo-<arch>.png
set -eu

ARCH=${1:?"architettura"}
ATTESA=${ATTESA:-60}
IMG=/out/resurgo-$ARCH${PROVA:+-$PROVA}${NOME:+-$NOME}.img
# sigla dei file di uscita: piu' prove della stessa architettura possono girare insieme
SIGLA=${NOME:+-$NOME}${RAM:+-$RAM}${GIRO:+-$GIRO}
case $ARCH in
	x86) QEMU=qemu-system-i386; PKG=qemu-system-i386 ;;
	x86_64) QEMU=qemu-system-x86_64; PKG=qemu-system-x86_64 ;;
	aarch64) QEMU=qemu-system-aarch64; PKG=qemu-system-aarch64 ;;
	*) echo "architettura non ancora supportata: $ARCH" >&2; exit 1 ;;
esac
apk add -q --no-cache $PKG qemu-hw-display-virtio-vga qemu-hw-display-virtio-gpu qemu-hw-display-virtio-gpu-pci imagemagick socat >/dev/null

ACC="-accel tcg"
[ -c /dev/kvm ] && ACC="-accel kvm"
if [ "$ARCH" = aarch64 ]; then
	CPU=max; [ -c /dev/kvm ] && CPU=host
	MACCHINA="-M virt -cpu $CPU -bios /usr/share/qemu/edk2-aarch64-code.fd -device qemu-xhci -device usb-kbd"
	VIDEO=virtio-gpu-pci
else
	MACCHINA=""
	VIDEO=virtio-vga
fi
# RISOLUZIONE=1024x600 per provare gli schermi dei netbook
if [ -n "${RISOLUZIONE:-}" ]; then VIDEO="$VIDEO,xres=${RISOLUZIONE%x*},yres=${RISOLUZIONE#*x}"; fi
cp "$IMG" /tmp/disco.img
$QEMU $ACC $MACCHINA -m ${RAM:-1024} -smp 2 \
	-drive file=/tmp/disco.img,format=raw,if=virtio \
	-device $VIDEO -display none \
	-nic user,model=virtio-net-pci \
	-serial file:/out/avvio-$ARCH$SIGLA.log \
	-monitor unix:/tmp/monitor,server,nowait &
PID=$!
# foto a richiesta: ogni riga "MISURA scatta NOME" sulla seriale fa una foto schermo-ARCH-NOME.png
foto() { echo "screendump /tmp/$1.ppm" | socat - UNIX-CONNECT:/tmp/monitor >/dev/null; sleep 1; magick /tmp/$1.ppm "/out/schermo-$ARCH$SIGLA-$1.png"; }
fatte=0; t=0
while [ "$t" -lt "$ATTESA" ]; do
	sleep 1; t=$((t + 1))
	richieste=$(grep -a "MISURA scatta " /out/avvio-$ARCH$SIGLA.log 2>/dev/null | sed 's/.*MISURA scatta //' | tr -dc 'a-z0-9-\n')
	n=$(printf '%s\n' "$richieste" | grep -c . || true)
	while [ "$fatte" -lt "$n" ]; do fatte=$((fatte + 1)); foto "$(printf '%s\n' "$richieste" | sed -n "${fatte}p")"; done
done
echo "screendump /tmp/schermo.ppm" | socat - UNIX-CONNECT:/tmp/monitor >/dev/null
sleep 2
magick /tmp/schermo.ppm /out/schermo-$ARCH$SIGLA.png
kill $PID
echo "fatto: /out/schermo-$ARCH$SIGLA.png"
