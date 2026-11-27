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
	armv7) QEMU=qemu-system-arm; PKG=qemu-system-arm ;;
	riscv64) QEMU=qemu-system-riscv64; PKG=qemu-system-riscv64 ;;
	*) echo "architettura non ancora supportata: $ARCH" >&2; exit 1 ;;
esac
apk add -q --no-cache $PKG qemu-hw-display-virtio-vga qemu-hw-display-virtio-gpu qemu-hw-display-virtio-gpu-pci imagemagick socat mtools >/dev/null

ACC="-accel tcg"
[ -c /dev/kvm ] && ACC="-accel kvm"
USB="-device qemu-xhci -device usb-kbd"
case $ARCH in
	aarch64)
		CPU=max; [ -c /dev/kvm ] && CPU=host
		MACCHINA="-M virt -cpu $CPU -bios /usr/share/qemu/edk2-aarch64-code.fd $USB"; VIDEO=virtio-gpu-pci ;;
	armv7)
		MACCHINA="-M virt,highmem=off -cpu cortex-a15 -bios /usr/share/qemu/edk2-arm-code.fd $USB"; VIDEO=virtio-gpu-pci ;;
	riscv64)
		if [ "${FIRMWARE:-uboot}" = edk2 ]; then
			# opensbi (predefinito) passa a edk2 nella flash, edk2 avvia grub; edk2 vuole anche la flash
			# delle variabili, scrivibile. si ferma a BdsDxe prima di grub, per questo di solito si usa u-boot
			cp /usr/share/qemu/edk2-riscv-vars.fd /tmp/variabili.fd
			MACCHINA="-M virt -drive if=pflash,unit=0,format=raw,readonly=on,file=/usr/share/qemu/edk2-riscv-code.fd -drive if=pflash,unit=1,format=raw,file=/tmp/variabili.fd $USB"
		else
			# opensbi (predefinito) passa a u-boot in modo s, u-boot trova EFI/BOOT/BOOTRISCV64.EFI e avvia grub.
			# u-boot per qemu riscv64 c'e' solo nel pacchetto riscv64 di alpine: si scarica quello, con le sue chiavi
			apk fetch -q --arch riscv64 --keys-dir /usr/share/apk/keys/riscv64 -o /tmp u-boot-qemu
			tar xzf /tmp/u-boot-qemu-*.apk -C /tmp usr/share/u-boot/qemu-riscv64_smode/u-boot.bin 2>/dev/null
			MACCHINA="-M virt -kernel /tmp/usr/share/u-boot/qemu-riscv64_smode/u-boot.bin -device virtio-rng-pci $USB"
		fi
		VIDEO=virtio-gpu-pci ;;
	*)
		# FIRMWARE=uefi prova l'avvio uefi dei pc (edk2 di qemu), senza quello il bios
		MACCHINA=""
		if [ "${FIRMWARE:-}" = uefi ]; then
			# edk2 va passato come flash (con -bios qemu non lo carica), con la flash delle variabili scrivibile
			E=x86_64; [ "$ARCH" = x86 ] && E=i386
			cp /usr/share/qemu/edk2-i386-vars.fd /tmp/variabili.fd
			MACCHINA="-drive if=pflash,unit=0,format=raw,readonly=on,file=/usr/share/qemu/edk2-$E-code.fd -drive if=pflash,unit=1,format=raw,file=/tmp/variabili.fd"
		fi
		VIDEO=virtio-vga ;;
esac
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
# tasti a richiesta: "MISURA tasti a b ret" sulla seriale li manda alla tastiera, uno alla volta (nomi di sendkey)
tasti() { for k in $1; do echo "sendkey $k" | socat - UNIX-CONNECT:/tmp/monitor >/dev/null; sleep 0.3; done; }
fatte=0; premuti=0; t=0
while [ "$t" -lt "$ATTESA" ]; do
	sleep 1; t=$((t + 1))
	gruppi=$(grep -a "MISURA tasti " /out/avvio-$ARCH$SIGLA.log 2>/dev/null | sed 's/.*MISURA tasti //' | tr -dc 'a-z0-9 \n-')
	n=$(printf '%s\n' "$gruppi" | grep -c . || true)
	while [ "$premuti" -lt "$n" ]; do premuti=$((premuti + 1)); tasti "$(printf '%s\n' "$gruppi" | sed -n "${premuti}p")"; done
	richieste=$(grep -a "MISURA scatta " /out/avvio-$ARCH$SIGLA.log 2>/dev/null | sed 's/.*MISURA scatta //' | tr -dc 'a-z0-9-\n')
	n=$(printf '%s\n' "$richieste" | grep -c . || true)
	while [ "$fatte" -lt "$n" ]; do fatte=$((fatte + 1)); foto "$(printf '%s\n' "$richieste" | sed -n "${fatte}p")"; done
done
echo "screendump /tmp/schermo.ppm" | socat - UNIX-CONNECT:/tmp/monitor >/dev/null
sleep 2
magick /tmp/schermo.ppm /out/schermo-$ARCH$SIGLA.png
kill $PID; wait $PID 2>/dev/null || true
# i rapporti sull'hardware che resurgo ha scritto nella partizione AVVIO (la prima, a 1 MiB)
rm -rf "/out/rapporti-$ARCH$SIGLA"
mcopy -s -n -i /tmp/disco.img@@1M ::/rapporti "/out/rapporti-$ARCH$SIGLA" 2>/dev/null || true
echo "fatto: /out/schermo-$ARCH$SIGLA.png"
