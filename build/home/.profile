# su tty1 parte direttamente il desktop
if [ "$(tty)" = /dev/tty1 ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
	export XDG_RUNTIME_DIR=/tmp/$(id -u)-runtime
	mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
	# le scelte di mantice (per esempio come disegnare il desktop); senza mantice, col processore
	if [ -f /run/mantice/ambiente ]; then set -a; . /run/mantice/ambiente; set +a; fi
	export WLR_RENDERER=${WLR_RENDERER:-pixman}
	exec labwc >/tmp/labwc.log 2>&1
fi
