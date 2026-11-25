#!/bin/sh
# Apre sul tablet il programma dimostrativo di un pezzo della shell, sopra il
# telefono finto (niente telefono vero: non serve il turno). Il demo si prende
# dal server, dalla compilazione -O3 senza controlli di memoria che
# strumenti/prova.sh shell-<pezzo> fa dopo le prove; copia locale in
# ~/.cache/yasde-sviluppo/shell-<pezzo>/ (mai in /tmp).
# Uso: strumenti/demo-sul-tablet.sh <pezzo> [opzioni del demo]
#   pezzo:   finto ossatura superficie finestra-app barra launcher desktop
#            trasferimento sistema   (anche con il prefisso "shell-")
#   opzioni: --misura LxA (predefinita 1280x800), --dmabuf (fotogrammi finti
#            in DMA-BUF), --senza-comandi (niente finestrella dei comandi)
# Tasti nel demo: F5 blocca/sblocca, F6 connessione persa 5 s, F7 notifica
# nuova, F8 l'ultima app si chiude da sola. Chiudi la finestra per finire.
set -e
P="${1:?manca il pezzo (es. barra)}"
shift
P="${P#shell-}"
S="${YASDE_SERVER:?imposta YASDE_SERVER con il nome o l'indirizzo del server di compilazione}"
SORG="/media/YASDE/lavoro/shell-$P/build-shell-$P-veloce/src/shell/$P/demo-$P"
D="$HOME/.cache/yasde-sviluppo/shell-$P"
mkdir -p "$D"
if ! scp -q -o BatchMode=yes "$S:$SORG" "$D/demo-$P" 2>/dev/null; then
    echo "Demo non trovato sul server: lancia prima strumenti/prova.sh shell-$P"
    exit 1
fi
COMANDI=--comandi
OPZIONI=""
for a in "$@"; do
    if [ "$a" = "--senza-comandi" ]; then COMANDI=""; else OPZIONI="$OPZIONI $a"; fi
done
# Disegno con Vulkan come nella proposta di architettura (PIANO.md). Le
# librerie che il tablet non ha (se servono) stanno nella cartella "insieme".
export GSK_RENDERER="${GSK_RENDERER:-vulkan}"
export LD_LIBRARY_PATH="$HOME/.cache/yasde-sviluppo/insieme${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# shellcheck disable=SC2086
exec "$D/demo-$P" $COMANDI $OPZIONI
