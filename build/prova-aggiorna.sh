#!/bin/sh
# prova della tappa 5 in qemu: corrente staccata a meta' aggiornamento, aggiornamento riuscito, versione guasta.
# gira in un container alpine:3.24 con /dev/kvm e /out montati. serve:
#   /out/resurgo-<arch>-aggiorna.img                 disco con la versione 1
#   /out/aggiornamento-<arch>-2/ e -3/                versione buona e versione guasta (ROTTA=1)
set -u
ARCH=${1:?architettura}
apk add -q --no-cache qemu-system-x86_64 qemu-system-i386 qemu-hw-display-virtio-vga qemu-hw-display-virtio-gpu e2fsprogs >/dev/null 2>&1
QEMU=qemu-system-x86_64; [ "$ARCH" = x86 ] && QEMU=qemu-system-i386
ACC="-accel tcg"; [ -c /dev/kvm ] && ACC="-accel kvm"
# i log restano in /out/prova-aggiorna, per capire cosa e' successo dopo
L=/out/prova-aggiorna; mkdir -p $L
SCENARI=${SCENARI:-123}
esito() { echo "ESITO $*"; }

# dischi d'aggiornamento: ext4 con dentro sistema.img.zst, manifesto, firma
for v in 2 3; do
	mkfs.ext4 -q -F -L aggiornamento -d /out/aggiornamento-$ARCH-$v $L/agg$v.img 400M
done

PID=""
avvia() { # disco [aggiornamento] [log]
	extra=""; [ -n "${2:-}" ] && extra="-drive file=$2,format=raw,if=virtio,readonly=on"
	: > "$3"
	$QEMU $ACC -m 1024 -smp 2 -drive file=$1,format=raw,if=virtio $extra -device virtio-vga -display none \
		-nic user,model=virtio-net-pci -serial file:$3 &
	PID=$!
}
spegni() { kill -9 "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""; }
aspetta() { # log modello secondi -> 0 se trovato
	n=0; while [ $n -lt "$3" ]; do grep -a -q "$2" "$1" && return 0; sleep 1; n=$((n + 1)); done; return 1
}
conta() { grep -a -c "$2" "$1"; }

case $SCENARI in *1*) ;; *) echo "== 1. saltato"; : ;; esac
if echo "$SCENARI" | grep -q 1; then
echo "== 1. corrente staccata durante la scrittura"
cp /out/resurgo-$ARCH-aggiorna.img $L/d1.img
avvia $L/d1.img $L/agg2.img $L/s1a.log
if aspetta $L/s1a.log "scrittura iniziata" 300; then
	sleep 1; spegni
	esito "1: corrente staccata 1 s dopo l'inizio della scrittura"
	avvia $L/d1.img "" $L/s1b.log
	aspetta $L/s1b.log "MISURA avvio" 180 && grep -a "MISURA avvio" $L/s1b.log | head -1
	aspetta $L/s1b.log "versione 1 confermata\|MISURA avvio versione=1" 5
	if grep -a -q "MISURA avvio versione=1 slot=A" $L/s1b.log; then esito "1: RIUSCITA, riparte la versione 1"; else esito "1: FALLITA"; fi
	spegni
else
	esito "1: la scrittura non e' mai iniziata"; tail -5 $L/s1a.log; spegni
fi

fi

echo "== 2. aggiornamento completo alla versione 2"
cp /out/resurgo-$ARCH-aggiorna.img $L/d2.img
avvia $L/d2.img $L/agg2.img $L/s2.log
aspetta $L/s2.log "MISURA aggiornato" 600 && grep -a "MISURA aggiornato" $L/s2.log
aspetta $L/s2.log "MISURA avvio versione=2" 300 && grep -a "MISURA avvio versione=2" $L/s2.log | head -1
aspetta $L/s2.log "versione 2 confermata" 200
if grep -a -q "versione 2 confermata" $L/s2.log; then esito "2: RIUSCITA, la versione 2 parte ed e' confermata"; else esito "2: FALLITA"; grep -a MISURA $L/s2.log | tail -5; fi
spegni

echo "== 3. versione 3 guasta: tre tentativi, poi ritorno alla 2"
avvia $L/d2.img $L/agg3.img $L/s3.log
aspetta $L/s3.log "MISURA aggiornato" 600 && grep -a "MISURA aggiornato" $L/s3.log
aspetta $L/s3.log "tornato indietro" 900
sleep 30
echo "avvii della versione 3: $(conta $L/s3.log 'MISURA avvio versione=3')"
grep -a "MISURA avvio" $L/s3.log | cut -c1-220
grep -a -E "tornato indietro|non partiva" $L/s3.log | cut -c1-200
ultimo=$(grep -a "MISURA avvio versione=" $L/s3.log | tail -1)
if grep -a -q "tornato indietro" $L/s3.log && echo "$ultimo" | grep -q "versione=2"; then esito "3: RIUSCITA, dopo tre avvii della 3 e' tornata la 2"; else esito "3: FALLITA"; fi
spegni
