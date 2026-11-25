#!/bin/sh
# Costruisce l'AppImage sul server, dentro il container. Mai sul tablet.
# Uso: pacchetto/costruisci.sh
# Prima servono l'eseguibile (strumenti/prova.sh veloce) e il jar
# (strumenti/prova.sh telefono) gia' compilati sul server.
# Risultato: /media/YASDE/lavoro/pacchetto/YASDE-2.1-x86_64.AppImage
# Stampa solo le ultime righe; il log intero resta sul server in
# /media/YASDE/log/pacchetto.log, l'ultima riga e' sempre "uscita N" e lo
# script esce con lo stesso N (255 se la connessione al server fallisce).
set -e
S="${YASDE_SERVER:?imposta YASDE_SERVER con il nome o l'indirizzo del server di compilazione}"
# Di norma tutto in /media/YASDE/lavoro/pacchetto; per costruire da una copia
# coerente dell'albero (non dall'albero di lavoro) si danno le tre cartelle:
#   PACCHETTO_LAVORO   dove si costruisce (cache, AppDir, risultato)
#   PACCHETTO_VELOCE   albero compilato con "veloce" (contiene build-veloce/)
#   PACCHETTO_TELEFONO albero in cui "telefono" ha prodotto telefono/yasde.jar
#   PACCHETTO_LOG      file di registro
D="${PACCHETTO_LAVORO:-/media/YASDE/lavoro/pacchetto}"
VELOCE="${PACCHETTO_VELOCE:-/media/YASDE/lavoro/veloce}"
TELEFONO="${PACCHETTO_TELEFONO:-/media/YASDE/lavoro/telefono}"
L="${PACCHETTO_LOG:-/media/YASDE/log/pacchetto.log}"
# Le cartelle e il registro si controllano PRIMA di toccare il server: un valore
# sbagliato farebbe cancellare cartelle o troncare un file altrove.
RE_DIR='^/media/YASDE/lavoro/[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)*$'
RE_LOG='^/media/YASDE/log/[A-Za-z0-9._-]+\.log$'
rifiuta() { echo "ERRORE: $*" >&2; exit 2; }
valida_dir() { # nome valore
    printf '%s\n' "$2" | grep -Eq "$RE_DIR" || rifiuta "$1 non valido (deve stare sotto /media/YASDE/lavoro, solo lettere, cifre, . _ - e /)"
    case "$2" in *..*) rifiuta "$1 non valido (contiene ..)" ;; esac
    # "/." in fondo o in mezzo riporterebbe alla cartella madre degli alberi.
    case "$2" in */.|*/./*) rifiuta "$1 non valido (contiene /.)" ;; esac
}
valida_dir PACCHETTO_LAVORO "$D"
valida_dir PACCHETTO_VELOCE "$VELOCE"
valida_dir PACCHETTO_TELEFONO "$TELEFONO"
printf '%s\n' "$L" | grep -Eq "$RE_LOG" || rifiuta "PACCHETTO_LOG non valido (deve essere /media/YASDE/log/<nome>.log)"
case "$L" in *..*) rifiuta "PACCHETTO_LOG non valido (contiene ..)" ;; esac
# La cartella di costruzione non coincide con gli alberi da cui si legge, non li
# contiene e non e' contenuta in loro.
for ALTRO in "$VELOCE" "$TELEFONO"; do
    case "$D/" in "$ALTRO/"*) rifiuta "PACCHETTO_LAVORO coincide con un albero di lettura o e' dentro di lui" ;; esac
    case "$ALTRO/" in "$D/"*) rifiuta "PACCHETTO_LAVORO contiene un albero di lettura" ;; esac
done
POD="podman --root /media/YASDE/containers --runroot /media/YASDE/containers-run --cgroup-manager=cgroupfs"
cd "$(dirname "$0")"
# La tabella delle icone Kora sta in data/ (fuori da pacchetto/): viaggia nello stesso tar.
[ -f ../data/icone-kora.txt ] || rifiuta "manca data/icone-kora.txt (tabella delle icone Kora)"
# Il lucchetto evita due costruzioni insieme; la cartella "cache" (sorgenti e
# strumenti scaricati, FFmpeg gia' compilato) sopravvive tra una corsa e
# l'altra, il resto si rifa' da zero.
# I valori sono gia' validati (solo caratteri sicuri); nel comando remoto vanno
# comunque tra virgolette.
tar -c -C . . -C ../data icone-kora.txt | ssh -o BatchMode=yes $S "mkdir -p \"$D\" /media/YASDE/terzi && flock \"$D.lucchetto\" sh -c 'cd \"$D\" && rm -rf sorgente && mkdir sorgente && tar -x -m -C sorgente && nice $POD run --rm -v \"$D\":/lavoro -v \"$VELOCE\":/veloce:ro -v \"$TELEFONO\":/telefono:ro -v /media/YASDE/terzi:/terzi localhost/yasde-sviluppo:schermo sh /lavoro/sorgente/dentro.sh > \"$L\" 2>&1 < /dev/null; U=\$?; echo \"uscita \$U\" >> \"$L\"; tail -n 25 \"$L\"; exit \$U'"
