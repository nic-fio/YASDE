#!/bin/sh
# Avvia sul tablet l'app vera (programma "yasde") presa dal server, con il
# telefono vero, in una cartella di prova separata dalla configurazione
# dell'utente: ~/.cache/yasde-sviluppo/prova-vera/ (config, cache, chiave
# di sviluppo gia' abbinata). Prende il turno del telefono e lo rilascia
# all.uscita. L.eseguibile completo (tutti i pezzi) lo mette chi coordina in
# /media/YASDE/lavoro/app/ sul server; prima va lanciato anche
# strumenti/prova.sh telefono.
# Uso: strumenti/app-sul-tablet.sh [opzioni del programma]
# L'uscita del programma va in ~/.cache/yasde-sviluppo/prova-vera/app.log
set -e
S="${YASDE_SERVER:?imposta YASDE_SERVER con il nome o l'indirizzo del server di compilazione}"
D="$HOME/.cache/yasde-sviluppo/prova-vera"
# YASDE_DA_ZERO=1: cartella di prova a parte e SENZA la chiave di sviluppo,
# come un'installazione nuova: serve a provare la prima connessione e
# l'abbinamento dall'app. La cartella normale non viene toccata.
[ "${YASDE_DA_ZERO:-0}" = 1 ] && D="$HOME/.cache/yasde-sviluppo/prova-da-zero"
TURNO="$HOME/.cache/yasde-sviluppo/telefono.occupato"
mkdir -p "$D/config/YASDE" "$D/cache"
if ! mkdir "$TURNO" 2>/dev/null; then
    echo "Il telefono e' occupato da una prova in corso: riprova tra un minuto."
    exit 1
fi
# Il turno si rilascia sempre, anche se il programma cade o viene interrotto.
trap 'rmdir "$TURNO" 2>/dev/null || true' EXIT INT TERM
scp -q -o BatchMode=yes "$S:/media/YASDE/lavoro/app/yasde" "$D/yasde"
scp -q -o BatchMode=yes "$S:/media/YASDE/lavoro/telefono/telefono/yasde.jar" "$D/yasde.jar"
if [ "${YASDE_DA_ZERO:-0}" != 1 ] && [ ! -f "$D/config/YASDE/chiave.pem" ]; then
    cp "$HOME/.cache/yasde-sviluppo/chiave-prova.pem" "$D/config/YASDE/chiave.pem"
    chmod 600 "$D/config/YASDE/chiave.pem"
fi
cd "$D"
# L'app gira sotto gdb (se c'e'): se cade, nel registro resta la traccia di
# dove e' caduta, per ogni thread. Senza gdb parte da sola.
export XDG_CONFIG_HOME="$D/config" XDG_CACHE_HOME="$D/cache"
export GSK_RENDERER="${GSK_RENDERER:-vulkan}"
# Nel registro restano anche le righe di GTK sul passaggio diretto del video
# allo schermo ("Attaching ... GdkDmabufTexture" = avviene davvero).
export GDK_DEBUG="${GDK_DEBUG:-offload}"
export LD_LIBRARY_PATH="$HOME/.cache/yasde-sviluppo/insieme${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if command -v gdb >/dev/null 2>&1 && [ "${YASDE_SENZA_GDB:-0}" != 1 ]; then
    gdb -q -batch -ex "handle SIGPIPE nostop noprint pass" -ex run -ex "echo \n--- traccia ---\n" \
        -ex bt -ex "echo \n--- tutti i thread ---\n" -ex "thread apply all bt 14" \
        --args ./yasde "$@" > "$D/app.log" 2>&1 || echo "uscita di gdb: $?" >> "$D/app.log"
else
    ./yasde "$@" > "$D/app.log" 2>&1 || echo "uscita del programma: $?" >> "$D/app.log"
fi
