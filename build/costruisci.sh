#!/bin/sh
# costruisce l'immagine minima di resurgo (tappa 1) per un'architettura.
# gira dentro un container alpine:3.24 come root, senza montare niente:
#   docker run --rm -v "$PWD":/resurgo -v "$PWD/out":/out alpine:3.24 /resurgo/build/costruisci.sh x86_64
# x86 e x86_64 si costruiscono su un host x86_64, aarch64 su un host arm (per esempio il mac).
# esce con /out/resurgo-<arch>.img
set -eu

ARCH=${1:?"architettura: x86, x86_64 o aarch64"}
VERSIONE=v3.24
MIRROR=https://dl-cdn.alpinelinux.org/alpine
QUI=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/out}
LAVORO=/tmp/resurgo-$ARCH
R=$LAVORO/radice
UTENTE=resurgo

case $ARCH in
	x86|x86_64) AVVIO=syslinux; SERIALE=ttyS0; ATTREZZI=syslinux ;;
	aarch64) AVVIO=uefi; SERIALE=ttyAMA0; ATTREZZI="grub grub-efi" ;;
	*) echo "architettura non supportata: $ARCH" >&2; exit 1 ;;
esac
if [ "$ARCH" = aarch64 ] && [ "$(uname -m)" != aarch64 ]; then
	echo "aarch64 va costruita su un host arm, senza emulazione" >&2; exit 1
fi

# attrezzi per fare l'immagine (girano sull'host del container, non nella radice)
apk add -q --no-cache e2fsprogs dosfstools mtools sfdisk $ATTREZZI >/dev/null

rm -rf "$LAVORO"
mkdir -p "$R/etc/apk/keys" "$OUT"
cp /usr/share/apk/keys/*.pub "$R/etc/apk/keys/"
printf '%s\n' "$MIRROR/$VERSIONE/main" "$MIRROR/$VERSIONE/community" > "$R/etc/apk/repositories"

# repository nostro (mesa senza llvm): se c'e', va per primo e i suoi pacchetti vincono per versione
REPO=${REPO:-/repo}
if [ -f "$REPO/$ARCH/APKINDEX.tar.gz" ]; then
	cp "$REPO/resurgo.rsa.pub" "$R/etc/apk/keys/"
	sed -i "1i $REPO" "$R/etc/apk/repositories"
else
	echo "attenzione: manca $REPO/$ARCH, uso mesa di alpine (con llvm)" >&2
fi

# pacchetti: base, kernel per macchine virtuali, desktop minimo
apk --root "$R" --arch "$ARCH" add --initdb --no-cache -q \
	alpine-base linux-virt eudev seatd labwc foot font-dejavu unbound ca-certificates \
	$(cat "$QUI/pacchetti.txt" 2>/dev/null || true)

# mantice regola il sistema all'avvio; MANTICE=0 lo toglie (serve per i confronti)
MANTICE=${MANTICE:-1}
if [ "$MANTICE" = 1 ]; then
	apk --root "$R" --arch "$ARCH" add --no-cache -q mantice
fi

# immagine di prova: PROVA=nome aggiunge il browser e lo script build/prova/misura-nome, che parte da solo
PROVA=${PROVA:-}
DNS=${DNS:-cifrato}
if [ -n "$PROVA" ]; then
	apk --root "$R" --arch "$ARCH" add --no-cache -q favilla
fi

# il repository locale non esiste sulla macchina installata
sed -i "\|^$REPO\$|d" "$R/etc/apk/repositories"
if ls "$R"/usr/lib/libLLVM* >/dev/null 2>&1; then echo "attenzione: llvm e' finito nell'immagine" >&2; fi

# servizi: link diretti invece di rc-update, cosi' non serve eseguire nulla nella radice
servizio() { mkdir -p "$R/etc/runlevels/$1"; ln -sf "/etc/init.d/$2" "$R/etc/runlevels/$1/$2"; }
for s in devfs dmesg udev udev-trigger udev-settle; do servizio sysinit $s; done
for s in modules sysctl hostname bootmisc syslog networking; do servizio boot $s; done
if [ "$MANTICE" = 1 ]; then servizio boot mantice; fi
for s in seatd unbound; do servizio default $s; done
for s in mount-ro killprocs savecache; do servizio shutdown $s; done

echo resurgo > "$R/etc/hostname"
printf '127.0.0.1\tresurgo localhost\n::1\t\tresurgo localhost\n' > "$R/etc/hosts"
# dns: unbound locale, cifrato verso quad9 (dns-over-tls), con i domini di pubblicita' e tracciamento bloccati.
# dhcp non deve sovrascrivere resolv.conf
mkdir -p "$R/etc/udhcpc"
echo 'RESOLV_CONF="no"' > "$R/etc/udhcpc/udhcpc.conf"
echo "nameserver 127.0.0.1" > "$R/etc/resolv.conf"
{
	printf 'server:\n\tinterface: 127.0.0.1\n\taccess-control: 127.0.0.0/8 allow\n'
	printf '\thide-identity: yes\n\thide-version: yes\n\tqname-minimisation: yes\n'
	printf '\ttls-cert-bundle: /etc/ssl/certs/ca-certificates.crt\n'
	# ponytail: dnssec ancora spento (serve l'ancora di fiducia scrivibile), da aggiungere col sistema in sola lettura
	printf '\tmodule-config: "iterator"\n'
	if [ -f "$R/usr/share/favilla/dns-blocco.conf" ]; then printf '\tinclude: /usr/share/favilla/dns-blocco.conf\n'; fi
	# DNS=ricorsivo: unbound chiede da solo ai server radice, in chiaro. serve sulle reti che bloccano il dns
	# cifrato (quella del .51 chiude sia la 853 sia il dns su https). ponytail: la scelta automatica tra le due
	# modalita' la fara' mantice; per ora si decide quando si costruisce l'immagine.
	if [ "$DNS" = cifrato ]; then
		printf 'forward-zone:\n\tname: "."\n\tforward-tls-upstream: yes\n'
		printf '\tforward-addr: 9.9.9.9@853#dns.quad9.net\n\tforward-addr: 149.112.112.112@853#dns.quad9.net\n'
	fi
} > "$R/etc/unbound/unbound.conf"

# rete via cavo con dhcp (il wi-fi arrivera' con iwd)
mkdir -p "$R/etc/network"
printf 'auto lo\niface lo inet loopback\n\nauto eth0\niface eth0 inet dhcp\n' > "$R/etc/network/interfaces"

# utente con accesso automatico su tty1, console seriale per il debug
echo "$UTENTE:x:1000:1000:Resurgo:/home/$UTENTE:/bin/sh" >> "$R/etc/passwd"
echo "$UTENTE:x:1000:" >> "$R/etc/group"
echo "$UTENTE:!::0:::::" >> "$R/etc/shadow"
awk -F: -v OFS=: -v u="$UTENTE" '$1 ~ /^(video|input|audio|seat)$/ { $4 = ($4 == "" ? u : $4 "," u) } 1' \
	"$R/etc/group" > "$LAVORO/group" && cat "$LAVORO/group" > "$R/etc/group"
sed -i 's|^tty1::.*|tty1::respawn:/bin/login -f '"$UTENTE"'|' "$R/etc/inittab"
grep -q "^$SERIALE" "$R/etc/inittab" || echo "$SERIALE::respawn:/sbin/getty -L 115200 $SERIALE vt100" >> "$R/etc/inittab"

cp -r "$QUI/home/." "$R/home/$UTENTE/"
if [ -n "$PROVA" ]; then
	echo "/dev/$SERIALE" > "$R/etc/resurgo-seriale"
	# niente login sulla seriale: getty si riprenderebbe il dispositivo e le misure non arriverebbero
	sed -i "/^$SERIALE::/d" "$R/etc/inittab"
	install -m755 "$QUI/prova/misura-$PROVA" "$R/usr/local/bin/misura"
	install -m644 "$QUI/prova/prova-js.html" "$R/usr/share/favilla/prova-js.html"
	install -m755 "$QUI/prova/seriale.start" "$R/etc/local.d/seriale.start"
	servizio default local
	echo "misura &" >> "$R/home/$UTENTE/.config/labwc/autostart"
fi
chown -R 1000:1000 "$R/home/$UTENTE"

# kernel e initramfs vanno nella partizione di avvio
UUID=$(cat /proc/sys/kernel/random/uuid)
mkdir -p "$LAVORO/avvio"
mv "$R/boot/vmlinuz-virt" "$R/boot/initramfs-virt" "$LAVORO/avvio/"
printf 'UUID=%s\t/\text4\tdefaults,noatime\t0 1\nLABEL=AVVIO\t/boot\tvfat\tdefaults\t0 2\n' "$UUID" > "$R/etc/fstab"

# psi=1: il kernel di alpine ha la pressione sulle risorse spenta di default, a mantice e alle misure serve
RIGA="root=UUID=$UUID rootfstype=ext4 modules=ext4 console=$SERIALE,115200 console=tty1 quiet psi=1"

# partizione di avvio fat, scritta con mtools senza montarla
MB_AVVIO=32
mkfs.vfat -n AVVIO -C "$LAVORO/avvio.img" $((MB_AVVIO * 1024)) >/dev/null
if [ "$AVVIO" = syslinux ]; then
	printf 'DEFAULT resurgo\nPROMPT 0\nTIMEOUT 0\nLABEL resurgo\n  LINUX /vmlinuz-virt\n  INITRD /initramfs-virt\n  APPEND %s\n' "$RIGA" > "$LAVORO/avvio/syslinux.cfg"
	mcopy -i "$LAVORO/avvio.img" "$LAVORO/avvio/"* ::/
	syslinux --install "$LAVORO/avvio.img"
else
	# uefi: grub con la configurazione dentro, cerca la partizione che contiene il kernel
	printf 'set timeout=0\ninsmod part_gpt\ninsmod fat\nsearch --no-floppy --file --set=root /vmlinuz-virt\nlinux /vmlinuz-virt %s\ninitrd /initramfs-virt\nboot\n' "$RIGA" > "$LAVORO/grub.cfg"
	grub-mkstandalone -O arm64-efi -o "$LAVORO/BOOTAA64.EFI" "boot/grub/grub.cfg=$LAVORO/grub.cfg"
	mcopy -i "$LAVORO/avvio.img" "$LAVORO/avvio/"* ::/
	mmd -i "$LAVORO/avvio.img" ::/EFI ::/EFI/BOOT
	mcopy -i "$LAVORO/avvio.img" "$LAVORO/BOOTAA64.EFI" ::/EFI/BOOT/
fi

# partizione di sistema: contenuto + 10% di margine (diventera' squashfs in sola lettura)
MB_RADICE=$(( $(du -sm "$R" | cut -f1) * 110 / 100 + 24 ))
mkfs.ext4 -q -L resurgo -U "$UUID" -d "$R" "$LAVORO/radice.img" "${MB_RADICE}M"

# disco: 1 MB di spazio iniziale, avvio, sistema
IMG="$OUT/resurgo-$ARCH${PROVA:+-$PROVA}${NOME:+-$NOME}.img"
rm -f "$IMG"
# con gpt la copia di riserva della tabella sta in fondo al disco: serve 1 MB libero anche li'
CODA=0; [ "$AVVIO" = uefi ] && CODA=1
truncate -s $((1 + MB_AVVIO + MB_RADICE + CODA))M "$IMG"
if [ "$AVVIO" = syslinux ]; then
	printf 'label: dos\nstart=1MiB, size=%sMiB, type=c, bootable\nstart=%sMiB, type=83\n' $MB_AVVIO $((1 + MB_AVVIO)) | sfdisk -q "$IMG"
else
	printf 'label: gpt\nstart=1MiB, size=%sMiB, type=U\nstart=%sMiB, size=%sMiB, type=L\n' $MB_AVVIO $((1 + MB_AVVIO)) $MB_RADICE | sfdisk -q "$IMG"
fi
dd if="$LAVORO/avvio.img" of="$IMG" bs=1M seek=1 conv=notrunc status=none
dd if="$LAVORO/radice.img" of="$IMG" bs=1M seek=$((1 + MB_AVVIO)) conv=notrunc status=none
if [ "$AVVIO" = syslinux ]; then
	dd if=/usr/share/syslinux/mbr.bin of="$IMG" bs=440 count=1 conv=notrunc status=none
fi

sha256sum "$IMG" | sed "s|$OUT/||" > "$IMG.sha256"
echo "$ARCH: $(du -m "$IMG" | cut -f1) MB, sistema ${MB_RADICE} MB"
