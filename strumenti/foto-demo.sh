#!/bin/sh
# Apre il demo di un pezzo della shell in uno schermo INVISIBILE sul tablet
# (compositore "cage" senza monitor: non disturba lo schermo dell'utente) e ne
# salva una o piu' fotografie PNG da guardare. Serve a verificare a occhio un
# pezzo senza aspettare l'utente.
# Uso: strumenti/foto-demo.sh <pezzo> [secondi tra le foto, es. "2 4 6"] [-- opzioni del demo]
#      es.  strumenti/foto-demo.sh barra
#           strumenti/foto-demo.sh launcher "2 5" -- --prova
# Prima va lanciato strumenti/prova.sh shell-<pezzo> (il demo si prende dal server).
# Le foto finiscono in ~/.cache/yasde-sviluppo/foto/<pezzo>-<n>.png;
# l'uscita del demo in ~/.cache/yasde-sviluppo/foto/<pezzo>.log.
set -e
P="${1:?manca il pezzo (es. barra)}"; shift
P="${P#shell-}"
TEMPI="2"
if [ $# -gt 0 ] && [ "$1" != "--" ]; then TEMPI="$1"; shift; fi
[ "${1:-}" = "--" ] && shift
S="${YASDE_SERVER:?imposta YASDE_SERVER con il nome o l'indirizzo del server di compilazione}"
SORG="/media/YASDE/lavoro/shell-$P/build-shell-$P-veloce/src/shell/$P/demo-$P"
D="$HOME/.cache/yasde-sviluppo/shell-$P"
F="$HOME/.cache/yasde-sviluppo/foto"
mkdir -p "$D" "$F"
# Una fotografia alla volta: lo schermo invisibile e' unico.
exec 9> "$F/.lucchetto"
flock 9
if ! scp -q -o BatchMode=yes "$S:$SORG" "$D/demo-$P" 2>/dev/null; then
    echo "Demo non trovato sul server: lancia prima strumenti/prova.sh shell-$P"; exit 1
fi
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
PRIMA=$(ls "$XDG_RUNTIME_DIR" | grep -E '^wayland-[0-9]+$' | sort)
ULTIMO=0; for t in $TEMPI; do ULTIMO=$t; done
rm -f "$F/$P"-*.png
WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman WAYLAND_DISPLAY= \
    setsid timeout $((ULTIMO + 6)) cage -- env GSK_RENDERER="${GSK_RENDERER:-cairo}" \
    LD_LIBRARY_PATH="$HOME/.cache/yasde-sviluppo/insieme" "$D/demo-$P" "$@" > "$F/$P.log" 2>&1 &
sleep 1
SCHERMO=""
for i in 1 2 3 4 5 6; do
    for w in $(ls "$XDG_RUNTIME_DIR" | grep -E '^wayland-[0-9]+$' | sort); do
        echo "$PRIMA" | grep -qx "$w" || SCHERMO="$w"
    done
    [ -n "$SCHERMO" ] && break
    sleep 0.5
done
if [ -z "$SCHERMO" ]; then echo "schermo invisibile non partito: vedi $F/$P.log"; exit 1; fi
N=0; FATTO=1
for t in $TEMPI; do
    sleep $((t - FATTO > 0 ? t - FATTO : 0)); FATTO=$t; N=$((N + 1))
    if WAYLAND_DISPLAY="$SCHERMO" grim "$F/$P-$N.png" 2>/dev/null; then echo "foto: $F/$P-$N.png"; else echo "foto $N non riuscita (il demo e' gia' uscito? vedi $F/$P.log)"; fi
done
wait 2>/dev/null || true
echo "uscita del demo in $F/$P.log:"; tail -n 8 "$F/$P.log" | cut -c1-200
