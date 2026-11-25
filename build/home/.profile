# su tty1 parte direttamente il desktop
if [ "$(tty)" = /dev/tty1 ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
	export XDG_RUNTIME_DIR=/tmp/$(id -u)-runtime
	mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
	# disegno col processore: funziona su qualsiasi scheda grafica
	export WLR_RENDERER=pixman
	exec labwc >/tmp/labwc.log 2>&1
fi
