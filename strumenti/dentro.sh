#!/bin/sh
# Gira dentro il container (lo lancia prova.sh). Cartella corrente: /lavoro.
set -e
P="$1"

# Schermo virtuale broadway per le prove con schermo. Il container e' nuovo a
# ogni corsa, quindi l'unico socket broadway che compare e' il nostro: si
# aspetta che esista (al massimo 10 s) invece di fare una pausa a occhio.
BW=""
avvia_broadway() {
    if command -v gtk4-broadwayd >/dev/null 2>&1; then
        gtk4-broadwayd :5 >/dev/null 2>&1 &
        BW=$!
        export BROADWAY_DISPLAY=:5
        N=0
        while [ $N -lt 100 ] && ! ls "${XDG_RUNTIME_DIR:-$HOME/.cache}"/broadway*.socket >/dev/null 2>&1; do
            sleep 0.1
            N=$((N + 1))
        done
        if [ $N -ge 100 ]; then
            echo "broadway: il socket non e' comparso entro 10 s: le prove con schermo saranno saltate"
        fi
    else
        echo "gtk4-broadwayd assente: le prove con schermo saranno saltate"
    fi
}
ferma_broadway() {
    [ -n "$BW" ] && kill "$BW" 2>/dev/null
    return 0
}
case "$P" in
    telefono) exec sh telefono/costruisci.sh ;;
    telefono-*) exec sh telefono/costruisci.sh "${P#telefono-}" ;;
esac
if [ "$P" = veloce ]; then
    # Tutto insieme, ottimizzato e senza controlli di memoria: serve per le
    # misure di velocita' e per gli eseguibili da provare sul tablet.
    [ -d build-veloce ] || meson setup build-veloce -Dpezzo=tutto -Dbuildtype=release
    exec meson compile -C build-veloce
fi
if [ "${P#shell-}" != "$P" ]; then
    # Pezzi della shell (src/shell/CONTRATTI.md): si provano solo le prove della
    # suite "shell" (gli altri pezzi compilati insieme hanno gia' le loro), con
    # uno schermo virtuale broadway se nel container c'e' gtk4-broadwayd
    # (altrimenti le prove con schermo risultano saltate). Poi la stessa
    # compilazione con -O3 senza controlli di memoria (come "veloce"): fa
    # emergere gli avvisi dell'ottimizzazione e da' i demo per il tablet
    # (strumenti/demo-sul-tablet.sh).
    B="build-$P"
    if [ -d "$B" ]; then
        meson setup "$B" -Dpezzo="$P" -Db_sanitize=address,undefined -Db_lundef=false --reconfigure >/dev/null
    else
        meson setup "$B" -Dpezzo="$P" -Db_sanitize=address,undefined -Db_lundef=false
    fi
    meson compile -C "$B"
    avvia_broadway
    ESITO=0
    meson test -C "$B" --suite shell --print-errorlogs --timeout-multiplier 3 || ESITO=1
    ferma_broadway
    V="build-$P-veloce"
    [ -d "$V" ] || meson setup "$V" -Dpezzo="$P" -Dbuildtype=release
    meson compile -C "$V" || ESITO=1
    exit $ESITO
fi
B="build-$P"
if [ -d "$B" ]; then
    meson setup "$B" -Dpezzo="$P" -Db_sanitize=address,undefined -Db_lundef=false --reconfigure >/dev/null
else
    meson setup "$B" -Dpezzo="$P" -Db_sanitize=address,undefined -Db_lundef=false
fi
meson compile -C "$B"
# Con "tutto" ci sono anche le prove con schermo della shell: serve broadway.
[ "$P" = tutto ] && avvia_broadway
ESITO=0
meson test -C "$B" --print-errorlogs --timeout-multiplier 3 || ESITO=$?
ferma_broadway
exit $ESITO
