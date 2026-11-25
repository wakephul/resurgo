#!/bin/sh
# costruisce l'immagine minima di resurgo (tappa 1) per un'architettura.
# gira dentro un container alpine:3.24 come root, senza montare niente:
#   docker run --rm -v "$PWD":/resurgo -v "$PWD/out":/out alpine:3.24 /resurgo/build/costruisci.sh x86_64
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
	x86|x86_64) AVVIO=syslinux ;;
	*) echo "per ora solo x86 e x86_64 (aarch64 richiede l'emulazione arm sul server)" >&2; exit 1 ;;
esac

# attrezzi per fare l'immagine (girano sull'host del container, non nella radice)
apk add -q --no-cache e2fsprogs dosfstools mtools sfdisk syslinux >/dev/null

rm -rf "$LAVORO"
mkdir -p "$R/etc/apk/keys" "$OUT"
cp /usr/share/apk/keys/*.pub "$R/etc/apk/keys/"
printf '%s\n' "$MIRROR/$VERSIONE/main" "$MIRROR/$VERSIONE/community" > "$R/etc/apk/repositories"

# pacchetti: base, kernel per macchine virtuali, desktop minimo
apk --root "$R" --arch "$ARCH" add --initdb --no-cache -q \
	alpine-base linux-virt eudev seatd labwc foot font-dejavu \
	$(cat "$QUI/pacchetti.txt" 2>/dev/null || true)

# servizi: link diretti invece di rc-update, cosi' non serve eseguire nulla nella radice
servizio() { mkdir -p "$R/etc/runlevels/$1"; ln -sf "/etc/init.d/$2" "$R/etc/runlevels/$1/$2"; }
for s in devfs dmesg udev udev-trigger udev-settle; do servizio sysinit $s; done
for s in modules sysctl hostname bootmisc syslog; do servizio boot $s; done
for s in seatd; do servizio default $s; done
for s in mount-ro killprocs savecache; do servizio shutdown $s; done

echo resurgo > "$R/etc/hostname"
printf '127.0.0.1\tresurgo localhost\n::1\t\tresurgo localhost\n' > "$R/etc/hosts"

# utente con accesso automatico su tty1, console seriale per il debug
echo "$UTENTE:x:1000:1000:Resurgo:/home/$UTENTE:/bin/sh" >> "$R/etc/passwd"
echo "$UTENTE:x:1000:" >> "$R/etc/group"
echo "$UTENTE:!::0:::::" >> "$R/etc/shadow"
awk -F: -v OFS=: -v u="$UTENTE" '$1 ~ /^(video|input|audio|seat)$/ { $4 = ($4 == "" ? u : $4 "," u) } 1' \
	"$R/etc/group" > "$LAVORO/group" && cat "$LAVORO/group" > "$R/etc/group"
sed -i 's|^tty1::.*|tty1::respawn:/bin/login -f '"$UTENTE"'|' "$R/etc/inittab"
grep -q '^ttyS0' "$R/etc/inittab" || echo 'ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100' >> "$R/etc/inittab"

cp -r "$QUI/home/." "$R/home/$UTENTE/"
chown -R 1000:1000 "$R/home/$UTENTE"

# kernel e initramfs vanno nella partizione di avvio
UUID=$(cat /proc/sys/kernel/random/uuid)
mkdir -p "$LAVORO/avvio"
mv "$R/boot/vmlinuz-virt" "$R/boot/initramfs-virt" "$LAVORO/avvio/"
printf 'UUID=%s\t/\text4\tdefaults,noatime\t0 1\nLABEL=AVVIO\t/boot\tvfat\tdefaults\t0 2\n' "$UUID" > "$R/etc/fstab"

cat > "$LAVORO/avvio/syslinux.cfg" <<EOF
DEFAULT resurgo
PROMPT 0
TIMEOUT 0
LABEL resurgo
  LINUX /vmlinuz-virt
  INITRD /initramfs-virt
  APPEND root=UUID=$UUID rootfstype=ext4 modules=ext4 console=ttyS0,115200 console=tty1 quiet
EOF

# partizione di avvio fat, scritta con mtools senza montarla
MB_AVVIO=64
mkfs.vfat -n AVVIO -C "$LAVORO/avvio.img" $((MB_AVVIO * 1024)) >/dev/null
mcopy -i "$LAVORO/avvio.img" "$LAVORO/avvio/"* ::/
syslinux --install "$LAVORO/avvio.img"

# partizione di sistema: contenuto + 25% di margine
MB_RADICE=$(( $(du -sm "$R" | cut -f1) * 125 / 100 + 32 ))
mkfs.ext4 -q -L resurgo -U "$UUID" -d "$R" "$LAVORO/radice.img" "${MB_RADICE}M"

# disco: 1 MB di spazio iniziale, avvio, sistema
IMG="$OUT/resurgo-$ARCH.img"
rm -f "$IMG"
truncate -s $((1 + MB_AVVIO + MB_RADICE))M "$IMG"
sfdisk -q "$IMG" <<EOF
label: dos
start=1MiB, size=${MB_AVVIO}MiB, type=c, bootable
start=$((1 + MB_AVVIO))MiB, type=83
EOF
dd if="$LAVORO/avvio.img" of="$IMG" bs=1M seek=1 conv=notrunc status=none
dd if="$LAVORO/radice.img" of="$IMG" bs=1M seek=$((1 + MB_AVVIO)) conv=notrunc status=none
dd if=/usr/share/syslinux/mbr.bin of="$IMG" bs=440 count=1 conv=notrunc status=none

sha256sum "$IMG" | sed "s|$OUT/||" > "$IMG.sha256"
echo "$ARCH: $(du -m "$IMG" | cut -f1) MB, sistema ${MB_RADICE} MB"
