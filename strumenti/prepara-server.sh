#!/bin/sh
# Prepara da zero il server di compilazione. Si lancia UNA volta, dalla macchina
# dove stanno i sorgenti (non dal server), quando il server e' nuovo o e' stato
# svuotato:
#
#   YASDE_SERVER=<nome o indirizzo del server> strumenti/prepara-server.sh
#
# Sul server servono solo podman e l'accesso SSH senza password; git non serve.
# Fa tre cose, tutte sotto /media/YASDE:
#   1. manda al server la ricetta del container (Containerfile) e sdk.sh;
#   2. costruisce l'immagine localhost/yasde-sviluppo:schermo (alcuni minuti,
#      scarica i pacchetti di Debian);
#   3. scarica l'SDK di Android in /media/YASDE/android-sdk con sdk.sh.
# Poi strumenti/prova.sh e pacchetto/costruisci.sh funzionano; cache del
# pacchetto e icone di terzi le riscaricano da soli.
# Stampa le ultime righe; i registri interi restano sul server in
# /media/YASDE/log/immagine.log e sdk.log; l'ultima riga e' sempre "uscita N"
# e lo script esce con lo stesso N (255 se la connessione al server fallisce).
set -e
S="${YASDE_SERVER:?imposta YASDE_SERVER con il nome o l'indirizzo del server di compilazione}"
B=/media/YASDE
POD="podman --root $B/containers --runroot $B/containers-run --cgroup-manager=cgroupfs"
I=localhost/yasde-sviluppo:schermo
cd "$(dirname "$0")"
N=0
tar -c Containerfile sdk.sh | ssh -o BatchMode=yes "$S" "
    command -v podman >/dev/null || { echo 'sul server manca podman'; echo 'uscita 3'; exit 3; }
    mkdir -p $B/costruzione $B/android-sdk $B/log $B/lavoro &&
    tar -x -m -C $B/costruzione &&
    $POD build -t $I -f $B/costruzione/Containerfile $B/costruzione > $B/log/immagine.log 2>&1 &&
    $POD run --rm -v $B/android-sdk:/android-sdk -v $B/costruzione:/s:ro $I sh /s/sdk.sh > $B/log/sdk.log 2>&1
    N=\$?
    tail -n 2 $B/log/immagine.log 2>/dev/null | cut -c1-160
    tail -n 2 $B/log/sdk.log 2>/dev/null | cut -c1-160
    echo \"uscita \$N\"
    exit \$N" || N=$?
exit $N
