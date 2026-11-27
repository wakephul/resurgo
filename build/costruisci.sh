#!/bin/sh
# costruisce resurgo per un'architettura: il disco completo da installare e l'aggiornamento firmato.
#   docker run --rm -v "$PWD":/resurgo -v "$PWD/out":/out -v "$PWD/out/repo/resurgo":/repo:ro \
#     -v "$HOME/.resurgo-chiavi":/chiavi:ro alpine:3.24 /resurgo/build/costruisci.sh x86_64
# x86 e x86_64 si costruiscono su un host x86_64, aarch64 su un host arm (per esempio il mac).
#
# il disco ha quattro partizioni:
#   1 AVVIO      fat: grub, la sua configurazione e grubenv (quale sistema avviare, se in prova)
#   2 sistemaA   ext4 in sola lettura: il sistema con il suo kernel
#   3 sistemaB   ext4 in sola lettura: l'altra versione (vuota finche' non arriva un aggiornamento)
#   4 dati       ext4: /var, con dentro le case degli utenti (/home punta a /var/home)
#
# esce con:
#   /out/resurgo-<arch>[-prova][-nome].img          disco completo
#   /out/aggiornamento-<arch>-<versione>/           sistema.img.zst, manifesto, manifesto.firma
#
# variabili: RESURGO_VERSIONE (numero, default 1), MANTICE=0, PROVA=nome, DNS=ricorsivo, SPAZIO_MB,
#            ROTTA=1 (versione volutamente guasta, senza desktop: serve a provare il ritorno indietro)
set -eu

ARCH=${1:?"architettura: x86, x86_64 o aarch64"}
VERSIONE_ALPINE=v3.24
MIRROR=https://dl-cdn.alpinelinux.org/alpine
QUI=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/out}
LAVORO=/tmp/resurgo-$ARCH
R=$LAVORO/radice
UTENTE=resurgo
RESURGO_VERSIONE=${RESURGO_VERSIONE:-1}

# KERNEL: la variante del kernel. sui pc lts, con i driver per l'hardware vero (dischi, schede video, wi-fi);
# KERNEL=virt fa un'immagine solo per macchine virtuali, piu' piccola. riscv64 non ha linux-virt in alpine.
# aarch64 e armv7 per ora restano su virt: per le schede vere (raspberry pi) servira' il loro kernel
case $ARCH in
	x86|x86_64) KERNEL=${KERNEL:-lts} ;;
	riscv64) KERNEL=lts ;;
	*) KERNEL=${KERNEL:-virt} ;;
esac
case $ARCH in
	# sui pc grub per il bios nel buco dopo l'mbr e, nella stessa partizione AVVIO, grub per uefi:
	# la stessa chiavetta parte su un pc del 2006 e su uno del 2015
	x86_64) AVVIO=bios; SERIALE=ttyS0; ATTREZZI="grub grub-bios grub-efi"; GRUB_EFI=x86_64-efi; FILE_EFI=BOOTX64.EFI ;;
	x86) AVVIO=bios; SERIALE=ttyS0; ATTREZZI="grub grub-bios grub-efi"; GRUB_EFI=i386-efi; FILE_EFI=BOOTIA32.EFI ;;
	aarch64) AVVIO=uefi; SERIALE=ttyAMA0; ATTREZZI="grub grub-efi"; GRUB_EFI=arm64-efi; FILE_EFI=BOOTAA64.EFI ;;
	armv7) AVVIO=uefi; SERIALE=ttyAMA0; ATTREZZI="grub grub-efi"; GRUB_EFI=arm-efi; FILE_EFI=BOOTARM.EFI ;;
	riscv64) AVVIO=uefi; SERIALE=ttyS0; ATTREZZI="grub grub-efi"; GRUB_EFI=riscv64-efi; FILE_EFI=BOOTRISCV64.EFI ;;
	*) echo "architettura non supportata: $ARCH" >&2; exit 1 ;;
esac
# le architetture non x86 vanno costruite in un container della stessa architettura (aarch64 nativo sul mac,
# armv7 e riscv64 con l'emulazione di docker sul mac: mai emulazione registrata sui server)
case $ARCH in
	aarch64|armv7|riscv64)
		if [ "$(apk --print-arch)" != "$ARCH" ]; then echo "$ARCH va costruita in un container $ARCH" >&2; exit 1; fi ;;
esac

# attrezzi per fare l'immagine (girano sull'host del container, non nella radice)
apk add -q --no-cache e2fsprogs e2fsprogs-extra dosfstools mtools sfdisk zstd openssl kmod $ATTREZZI >/dev/null

rm -rf "$LAVORO"
mkdir -p "$R/etc/apk/keys" "$OUT"
# una build fallita non deve lasciare in giro il disco vecchio con lo stesso nome
rm -f "$OUT/resurgo-$ARCH${PROVA:+-$PROVA}${NOME:+-$NOME}.img"
cp /usr/share/apk/keys/*.pub "$R/etc/apk/keys/"
printf '%s\n' "$MIRROR/$VERSIONE_ALPINE/main" "$MIRROR/$VERSIONE_ALPINE/community" > "$R/etc/apk/repositories"

# repository nostro (mesa senza llvm, mantice, favilla): va per primo e i suoi pacchetti vincono per versione
REPO=${REPO:-/repo}
if [ -f "$REPO/$ARCH/APKINDEX.tar.gz" ]; then
	cp "$REPO/resurgo.rsa.pub" "$R/etc/apk/keys/"
	sed -i "1i $REPO" "$R/etc/apk/repositories"
else
	echo "attenzione: manca $REPO/$ARCH, uso mesa di alpine (con llvm)" >&2
fi

# l'initramfs porta solo i driver per arrivare al disco: sata e ide, usb (la chiavetta), nvme, schede sd,
# virtio. i driver video (kms) si caricano dopo, dal sistema: dentro l'initramfs lo gonfiavano a 170 MB
mkdir -p "$R/etc/mkinitfs"
echo 'features="ata base ext4 mmc nvme scsi usb virtio"' > "$R/etc/mkinitfs/mkinitfs.conf"

# pacchetti: base, kernel, desktop minimo, e gli attrezzi per gli aggiornamenti
apk --root "$R" --arch "$ARCH" add --initdb --no-cache -q \
	alpine-base linux-$KERNEL eudev seatd labwc foot font-dejavu unbound ca-certificates \
	iwd dbus wireless-regdb \
	zstd openssl e2fsprogs e2fsprogs-extra \
	$(cat "$QUI/pacchetti.txt" 2>/dev/null || true)
# kernel lts: via i moduli che su un pc vecchio non servono, e il firmware solo per le schede di quegli anni
if [ "$KERNEL" = lts ]; then
	K=$(ls "$R/lib/modules")
	M="$R/lib/modules/$K/kernel"
	# infiniband, webcam e tv, macchine virtuali altrui, schede amd dal 2014 (amdgpu) e intel xe, filesystem da server,
	# schede di rete e dischi da server (10-100 Gbit, fibre channel, target scsi)
	rm -rf "$M/drivers/infiniband" "$M/drivers/media" "$M/drivers/xen" "$M/drivers/vfio" "$M/drivers/vdpa" \
		"$M/drivers/vhost" "$M/drivers/iio" "$M/drivers/gpu/drm/amd" "$M/drivers/gpu/drm/xe" \
		"$M/fs/btrfs" "$M/fs/xfs" "$M/fs/ocfs2" "$M/fs/gfs2" "$M/fs/ceph" "$M/fs/jfs" "$M/fs/nilfs2" "$M/fs/orangefs" \
		"$M/fs/dlm" "$M/fs/bcachefs" "$M/fs/erofs" "$M/net/ceph" "$M/net/rds" "$M/net/sunrpc/xprtrdma" \
		"$M/drivers/net/ethernet/mellanox" "$M/drivers/net/ethernet/qlogic" "$M/drivers/net/ethernet/chelsio" \
		"$M/drivers/net/ethernet/sfc" "$M/drivers/scsi/qla2xxx" "$M/drivers/scsi/lpfc" "$M/drivers/target"
	depmod -b "$R" "$K"
	if [ "$ARCH" = x86 ] || [ "$ARCH" = x86_64 ]; then
		# radeon (ati fino al 2013), realtek per cavo e wi-fi, broadcom, atheros usb, wi-fi intel fino al 2015.
		# niente amdgpu, nvidia, i915 (le intel vecchie non vogliono firmware): sono le schede nuove
		apk --root "$R" --arch "$ARCH" add --no-cache -q linux-firmware-radeon linux-firmware-rtl_nic \
			linux-firmware-rtlwifi linux-firmware-brcm linux-firmware-ath9k_htc linux-firmware-intel
		F="$R/lib/firmware/intel"
		find "$F" -mindepth 1 -maxdepth 1 ! -name iwlwifi -exec rm -rf {} +
		find "$F/iwlwifi" -type f | grep -v -E '/iwlwifi-(100|105|135|1000|2000|2030|3160|3945|4965|5000|5150|6000|6000g2a|6000g2b|6050|7260|7265|7265D|8000C|8265)-' | xargs rm -f
		# i collegamenti rimasti senza file (busybox find non ha -xtype)
		find "$R/lib/firmware" -type l | while read -r l; do [ -e "$l" ] || rm -f "$l"; done
	fi
fi

# di grub nel sistema serve solo grub-editenv: senza i suoi script, che proverebbero a configurare il disco del container
apk --root "$R" --arch "$ARCH" add --no-cache --no-scripts -q grub

# la shell (pannello, menu, impostazioni, benvenuto, tema): dal repository nostro
apk --root "$R" --arch "$ARCH" add --no-cache -q resurgo-shell

# mantice regola il sistema all'avvio; MANTICE=0 lo toglie (serve per i confronti)
MANTICE=${MANTICE:-1}
if [ "$MANTICE" = 1 ]; then
	apk --root "$R" --arch "$ARCH" add --no-cache -q mantice
fi

# immagine di prova: PROVA=nome aggiunge lo script build/prova/misura-nome, che parte da solo col desktop,
# e build/prova/nome.start se c'e' (parte come root all'avvio). per le prove del browser aggiunge favilla
PROVA=${PROVA:-}
DNS=${DNS:-cifrato}
if [ -n "$PROVA" ] && [ "$PROVA" != aggiorna ]; then
	apk --root "$R" --arch "$ARCH" add --no-cache -q favilla
fi

# il repository locale non esiste sulla macchina installata
sed -i "\|^$REPO\$|d" "$R/etc/apk/repositories"
if ls "$R"/usr/lib/libLLVM* >/dev/null 2>&1; then echo "attenzione: llvm e' finito nell'immagine" >&2; fi

# servizi: link diretti invece di rc-update, cosi' non serve eseguire nulla nella radice
servizio() { mkdir -p "$R/etc/runlevels/$1"; ln -sf "/etc/init.d/$2" "$R/etc/runlevels/$1/$2"; }
for s in devfs dmesg udev udev-trigger udev-settle; do servizio sysinit $s; done
for s in root localmount modules sysctl hostname bootmisc syslog networking; do servizio boot $s; done
if [ "$MANTICE" = 1 ]; then servizio boot mantice; servizio default earlyoom; servizio default mantice-sorveglia; fi
for s in dbus iwd seatd unbound resurgo-avvio resurgo-rapporto; do servizio default $s; done
for s in mount-ro killprocs; do servizio shutdown $s; done

echo resurgo > "$R/etc/hostname"
printf '127.0.0.1\tresurgo localhost\n::1\t\tresurgo localhost\n' > "$R/etc/hosts"
echo "$RESURGO_VERSIONE" > "$R/etc/resurgo-versione"

# dns: unbound locale, cifrato verso quad9 (dns-over-tls), con i domini di pubblicita' e tracciamento bloccati.
# dhcp non deve sovrascrivere resolv.conf
mkdir -p "$R/etc/udhcpc"
echo 'RESOLV_CONF="no"' > "$R/etc/udhcpc/udhcpc.conf"
echo "nameserver 127.0.0.1" > "$R/etc/resolv.conf"
{
	printf 'server:\n\tinterface: 127.0.0.1\n\taccess-control: 127.0.0.0/8 allow\n'
	printf '\thide-identity: yes\n\thide-version: yes\n\tqname-minimisation: yes\n'
	printf '\ttls-cert-bundle: /etc/ssl/certs/ca-certificates.crt\n'
	# ponytail: dnssec ancora spento (serve l'ancora di fiducia scrivibile, in /var), da aggiungere
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

# rete via cavo con dhcp
mkdir -p "$R/etc/network"
printf 'auto lo\niface lo inet loopback\n\nauto eth0\niface eth0 inet dhcp\n' > "$R/etc/network/interfaces"
# wi-fi con iwd: fa da solo anche il dhcp, il dns resta a unbound (resolv.conf e' nel sistema in sola lettura).
# le reti salvate finiscono in /var/lib/iwd, nella partizione dati. l'utente entra in iwd dal gruppo netdev.
# ogni rete vede un indirizzo mac suo, non quello della scheda. e iwd non scrive nel registro di sistema:
# ci metterebbe nome e bssid di ogni rete a cui ci si collega, cioe' dove e' stato il computer
printf '[General]\nEnableNetworkConfiguration=true\nAddressRandomization=network\n\n[Network]\nNameResolvingService=none\n' > "$R/etc/iwd/main.conf"
echo 'error_logger=""' > "$R/etc/conf.d/iwd"

# aggiornamenti: lo strumento, il servizio che conferma l'avvio, la chiave pubblica per le firme
install -Dm755 "$QUI/sistema/resurgo-aggiorna" "$R/usr/sbin/resurgo-aggiorna"
install -Dm755 "$QUI/sistema/resurgo-avvio.initd" "$R/etc/init.d/resurgo-avvio"
install -Dm755 "$QUI/sistema/resurgo-rapporto" "$R/usr/sbin/resurgo-rapporto"
install -Dm755 "$QUI/sistema/resurgo-rapporto.initd" "$R/etc/init.d/resurgo-rapporto"
install -Dm644 /chiavi/resurgo.rsa.pub "$R/usr/share/resurgo/chiave-aggiornamenti.pub"

# utente con accesso automatico su tty1, console seriale per il debug
echo "$UTENTE:x:1000:1000:Resurgo:/home/$UTENTE:/bin/sh" >> "$R/etc/passwd"
echo "$UTENTE:x:1000:" >> "$R/etc/group"
echo "$UTENTE:!::0:::::" >> "$R/etc/shadow"
awk -F: -v OFS=: -v u="$UTENTE" '$1 ~ /^(video|input|audio|seat|netdev)$/ { $4 = ($4 == "" ? u : $4 "," u) } 1' \
	"$R/etc/group" > "$LAVORO/group" && cat "$LAVORO/group" > "$R/etc/group"
sed -i 's|^tty1::.*|tty1::respawn:/bin/login -f '"$UTENTE"'|' "$R/etc/inittab"
grep -q "^$SERIALE" "$R/etc/inittab" || echo "$SERIALE::respawn:/sbin/getty -L 115200 $SERIALE vt100" >> "$R/etc/inittab"

mkdir -p "$R/home/$UTENTE"
cp -r "$QUI/home/." "$R/home/$UTENTE/"
if [ -n "$PROVA" ]; then
	echo "/dev/$SERIALE" > "$R/etc/resurgo-seriale"
	# niente login sulla seriale: getty si riprenderebbe il dispositivo e le misure non arriverebbero
	sed -i "/^$SERIALE::/d" "$R/etc/inittab"
	install -m755 "$QUI/prova/seriale.start" "$R/etc/local.d/seriale.start"
	if [ -f "$QUI/prova/$PROVA.start" ]; then install -m755 "$QUI/prova/$PROVA.start" "$R/etc/local.d/$PROVA.start"; fi
	if [ -f "$QUI/prova/misura-$PROVA" ]; then
		install -m755 "$QUI/prova/misura-$PROVA" "$R/usr/local/bin/misura"
		# l'autostart della casa sostituisce quello di sistema: lo copio e ci aggiungo la misura
		mkdir -p "$R/home/$UTENTE/.config/labwc"
		cp "$R/etc/xdg/labwc/autostart" "$R/home/$UTENTE/.config/labwc/autostart"
		echo "misura &" >> "$R/home/$UTENTE/.config/labwc/autostart"
	fi
	if [ -d "$R/usr/share/favilla" ]; then install -m644 "$QUI/prova/prova-js.html" "$R/usr/share/favilla/prova-js.html"; fi
	servizio default local
	echo "${ATTESA_DESKTOP:-60}" > "$R/etc/resurgo-attesa-desktop"
fi
# versione guasta per le prove: senza compositore il desktop non arriva mai
if [ "${ROTTA:-0}" = 1 ]; then rm -f "$R/usr/bin/labwc"; fi
chown -R 1000:1000 "$R/home/$UTENTE"

# radice in sola lettura: /var e le case stanno nella partizione dati, /tmp in memoria
mkdir -p "$LAVORO/dati"
mv "$R/home" "$LAVORO/dati/home"
cp -a "$R/var/." "$LAVORO/dati/"
rm -rf "$R/var" && mkdir -p "$R/var" "$R/avvio"
ln -s var/home "$R/home"
cat > "$R/etc/fstab" <<EOF
/dev/root	/	ext4	ro,noatime	0 0
LABEL=dati	/var	ext4	rw,noatime	0 2
LABEL=AVVIO	/avvio	vfat	rw,noatime,umask=0077	0 2
tmpfs	/tmp	tmpfs	nosuid,nodev,mode=1777	0 0
EOF

# --- l'aggiornamento: sistema in ext4 (etichetta provvisoria, resurgo-aggiorna la cambia), compresso e firmato
MB_SISTEMA=$(( $(du -sm "$R" | cut -f1) * 115 / 100 + 32 ))
# riscv64 ha il kernel lts con tutti i moduli e mesa con llvm: serve una partizione piu' grande
SLOT_MB=${SLOT_MB:-$([ "$ARCH" = riscv64 ] && echo 2048 || echo 1024)}
if [ "$MB_SISTEMA" -gt "$SLOT_MB" ]; then echo "il sistema ($MB_SISTEMA MB) non sta nella partizione ($SLOT_MB MB)" >&2; exit 1; fi
mkfs.ext4 -q -L sistema -O ^has_journal -d "$R" "$LAVORO/sistema.img" "${MB_SISTEMA}M"
AGG="$OUT/aggiornamento-$ARCH-$RESURGO_VERSIONE"
rm -rf "$AGG" && mkdir -p "$AGG"
zstd -q -19 -T0 "$LAVORO/sistema.img" -o "$AGG/sistema.img.zst"
{
	echo "versione=$RESURGO_VERSIONE"
	echo "arch=$ARCH"
	echo "byte=$(stat -c %s "$LAVORO/sistema.img")"
	echo "sha256=$(sha256sum "$LAVORO/sistema.img" | cut -d' ' -f1)"
} > "$AGG/manifesto"
openssl dgst -sha256 -sign /chiavi/resurgo.rsa -out "$AGG/manifesto.firma" "$AGG/manifesto"

# --- il disco completo
MB_AVVIO=32
DATI_MB=$(( $(du -sm "$LAVORO/dati" | cut -f1) + 256 + ${SPAZIO_MB:-$([ -n "$PROVA" ] && echo 512 || echo 0)} ))
mkfs.ext4 -q -L dati -d "$LAVORO/dati" "$LAVORO/dati.img" "${DATI_MB}M"

# configurazione di grub, uguale per tutte le architetture. grubenv dice quale sistema avviare.
# un sistema nuovo parte "in prova" con tre tentativi: se non viene confermato (servizio resurgo-avvio),
# al quarto avvio grub torna alla versione di prima e lo segna in "tornato"
cat > "$LAVORO/grub.cfg" <<'EOF'
set timeout=0
insmod part_msdos
insmod part_gpt
insmod fat
insmod ext2
search --no-floppy --label AVVIO --set=avvio
set ambiente=($avvio)/grub/grubenv
load_env -f $ambiente
if [ -z "$attivo" ]; then set attivo=A; fi
if [ "$prova" = "1" ]; then
	if [ "$tentativi" = "3" ]; then set tentativi=2
	elif [ "$tentativi" = "2" ]; then set tentativi=1
	elif [ "$tentativi" = "1" ]; then set tentativi=0
	else
		set attivo=$precedente
		set prova=0
		set tornato=1
	fi
	save_env -f $ambiente attivo prova tentativi tornato
fi
search --no-floppy --label sistema$attivo --set=root
linux /boot/vmlinuz-KERNEL root=LABEL=sistema$attivo rootfstype=ext4 ro modules=ext4 resurgo.slot=$attivo panic=10 RIGA_SERIALE quiet psi=1
initrd /boot/initramfs-KERNEL
boot
EOF
sed -i -e "s|RIGA_SERIALE|console=$SERIALE,115200 console=tty1|" -e "s|KERNEL|$KERNEL|g" "$LAVORO/grub.cfg"
grub-editenv "$LAVORO/grubenv" create
grub-editenv "$LAVORO/grubenv" set attivo=A prova=0

mkfs.vfat -n AVVIO -C "$LAVORO/avvio.img" $((MB_AVVIO * 1024)) >/dev/null
mmd -i "$LAVORO/avvio.img" ::/grub
mcopy -i "$LAVORO/avvio.img" "$LAVORO/grub.cfg" "$LAVORO/grubenv" ::/grub/

# sistema A: la stessa immagine dell'aggiornamento, con l'etichetta giusta
cp "$LAVORO/sistema.img" "$LAVORO/sistemaA.img"
tune2fs -L sistemaA "$LAVORO/sistemaA.img" >/dev/null

IMG="$OUT/resurgo-$ARCH${PROVA:+-$PROVA}${NOME:+-$NOME}.img"
P2=$((1 + MB_AVVIO)); P3=$((P2 + SLOT_MB)); P4=$((P3 + SLOT_MB))
truncate -s $((P4 + DATI_MB + 1))M "$IMG"
if [ "$AVVIO" = bios ]; then
	printf 'label: dos\nstart=1MiB, size=%sMiB, type=c, bootable\nstart=%sMiB, size=%sMiB, type=83\nstart=%sMiB, size=%sMiB, type=83\nstart=%sMiB, size=%sMiB, type=83\n' \
		$MB_AVVIO $P2 $SLOT_MB $P3 $SLOT_MB $P4 $DATI_MB | sfdisk -q "$IMG"
else
	printf 'label: gpt\nstart=1MiB, size=%sMiB, type=U\nstart=%sMiB, size=%sMiB, type=L\nstart=%sMiB, size=%sMiB, type=L\nstart=%sMiB, size=%sMiB, type=L\n' \
		$MB_AVVIO $P2 $SLOT_MB $P3 $SLOT_MB $P4 $DATI_MB | sfdisk -q "$IMG"
fi
dd if="$LAVORO/sistemaA.img" of="$IMG" bs=1M seek=$P2 conv=notrunc status=none
dd if="$LAVORO/dati.img" of="$IMG" bs=1M seek=$P4 conv=notrunc status=none

# grub: sul pc nel buco dopo l'mbr (grub-bios-setup lavora sul file, senza montare niente),
# su arm come applicazione efi nella partizione di avvio. tutti e due leggono /grub/grub.cfg da AVVIO
if [ "$AVVIO" = bios ]; then
	MODULI="biosdisk part_msdos part_gpt fat ext2 search search_label search_fs_file configfile loadenv normal linux echo test"
	mkdir -p "$LAVORO/grub-i386"
	cp /usr/lib/grub/i386-pc/* "$LAVORO/grub-i386/"
	printf 'search --no-floppy --label AVVIO --set=root\nset prefix=($root)/grub\nconfigfile ($root)/grub/grub.cfg\n' > "$LAVORO/primo.cfg"
	grub-mkimage -O i386-pc -d "$LAVORO/grub-i386" -o "$LAVORO/grub-i386/core.img" -c "$LAVORO/primo.cfg" -p /grub $MODULI
	dd if="$LAVORO/avvio.img" of="$IMG" bs=1M seek=1 conv=notrunc status=none
	# quello che fa grub-bios-setup, a mano (lui vuole un disco vero, qui c'e' un file):
	# core.img dal settore 1, nel buco prima della prima partizione; boot.img nei primi 440 byte dell'mbr,
	# cosi' la tabella delle partizioni resta. boot.img punta gia' al settore 1 e core.img al 2:
	# resta da scrivere quanti settori seguono, nei due byte a 508 del primo settore di core.img
	SETTORI=$(( ($(stat -c %s "$LAVORO/grub-i386/core.img") + 511) / 512 ))
	[ "$SETTORI" -lt 2000 ] || { echo "core.img troppo grande: $SETTORI settori" >&2; exit 1; }
	dd if="$LAVORO/grub-i386/core.img" of="$IMG" bs=512 seek=1 conv=notrunc status=none
	RESTO=$((SETTORI - 1))
	printf "$(printf '\\%03o\\%03o' $((RESTO % 256)) $((RESTO / 256)))" | dd of="$IMG" bs=1 seek=$((512 + 508)) conv=notrunc status=none
	dd if="$LAVORO/grub-i386/boot.img" of="$IMG" bs=440 count=1 conv=notrunc status=none
fi
if [ -n "${GRUB_EFI:-}" ]; then
	# la partizione AVVIO e' fat32 normale (tipo c, non ef): i firmware uefi la leggono lo stesso, come per
	# le chiavette fatte con rufus, e windows le da' una lettera, cosi' il rapporto sull'hardware si apre
	# standalone: i moduli per gpt e fat vanno caricati prima di cercare la partizione, e prefix resta quello
	# interno (memdisk), dove stanno tutti i moduli
	printf 'insmod part_gpt\ninsmod part_msdos\ninsmod fat\nsearch --no-floppy --label AVVIO --set=root\nconfigfile ($root)/grub/grub.cfg\n' > "$LAVORO/primo.cfg"
	grub-mkstandalone -O $GRUB_EFI -o "$LAVORO/$FILE_EFI" "boot/grub/grub.cfg=$LAVORO/primo.cfg"
	mmd -i "$LAVORO/avvio.img" ::/EFI ::/EFI/BOOT
	mcopy -i "$LAVORO/avvio.img" "$LAVORO/$FILE_EFI" ::/EFI/BOOT/
	dd if="$LAVORO/avvio.img" of="$IMG" bs=1M seek=1 conv=notrunc status=none
fi

sha256sum "$IMG" | sed "s|$OUT/||" > "$IMG.sha256"
echo "$ARCH v$RESURGO_VERSIONE: disco $(du -m "$IMG" | cut -f1) MB (sistema $MB_SISTEMA MB in partizioni da $SLOT_MB), aggiornamento $(du -m "$AGG/sistema.img.zst" | cut -f1) MB"
