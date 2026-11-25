#!/bin/sh
# Lancia sul tablet la finestra di prova dell'ondata 1 (un'app del telefono
# in una finestra spoglia). Usa gli eseguibili gia' compilati sul server e
# copiati in ~/.cache/yasde-sviluppo/insieme (li aggiorna chi coordina).
# Uso: strumenti/finestra-sul-tablet.sh [PACCHETTO] [LARGHEZZAxALTEZZA] [audio]
#      (terzo argomento "audio": con l'audio; tasti su/giu = volume, M = muto)
#      es.: strumenti/finestra-sul-tablet.sh com.android.chrome 1280x720
# Esc = indietro; chiudi la finestra per finire.
D="$HOME/.cache/yasde-sviluppo/insieme"
L="$HOME/.cache/yasde-sviluppo/telefono.occupato"
APP="${1:-com.android.settings}"
MISURA="${2:-1280x720}"
OPZIONI=""
[ "$3" = "audio" ] && OPZIONI="--audio"
# Il telefono di prova e' uno solo: si prende il turno.
if ! mkdir "$L" 2>/dev/null; then
    echo "Il telefono e' occupato da una prova in corso: riprova tra un minuto."
    exit 1
fi
trap 'rmdir "$L" 2>/dev/null' EXIT INT TERM
cd "$D" || exit 1
LD_LIBRARY_PATH="$D" ./finestra-prova --chiave "$HOME/.cache/yasde-sviluppo/chiave-prova.pem" \
    $OPZIONI yasde.jar "$APP" "$MISURA"
