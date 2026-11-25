#!/bin/sh
# Compila e prova un pezzo sul server, dentro il container. Mai sul tablet.
# Uso: strumenti/prova.sh <pezzo>     (adb, file, scoperta, abbinamento,
#                                      componente, video, telefono, tutto;
#                                      shell-finto, shell-ossatura,
#                                      shell-superficie, shell-finestra-app,
#                                      shell-barra, shell-launcher,
#                                      shell-desktop, shell-trasferimento,
#                                      shell-sistema)
# Stampa solo le ultime righe; il log intero resta sul server in
# /media/YASDE/log/<pezzo>.log e l'ultima riga e' sempre "uscita N".
set -e
P="${1:?manca il nome del pezzo}"
S="${YASDE_SERVER:?imposta YASDE_SERVER con il nome o l'indirizzo del server di compilazione}"
D=/media/YASDE/lavoro/$P
L=/media/YASDE/log/$P.log
POD="podman --root /media/YASDE/containers --runroot /media/YASDE/containers-run --cgroup-manager=cgroupfs"
# Immagine del container: predefinita quella con gtk4-broadwayd (ricetta in
# strumenti/Containerfile, costruita da strumenti/prepara-server.sh);
# YASDE_IMMAGINE ne sceglie un'altra.
IMMAGINE="${YASDE_IMMAGINE:-localhost/yasde-sviluppo:schermo}"
cd "$(dirname "$0")/.."
# Un solo comando sul server, sotto un lucchetto per pezzo: due agenti che
# lanciano lo stesso pezzo (di solito "tutto" o "veloce") nello stesso momento
# si mettono in fila invece di cancellarsi a vicenda sorgenti e compilazione.
# Se la prova fallisce, registro e dettaglio delle prove vengono conservati con
# l'ora nel nome: un fallimento intermittente non va perso alla corsa dopo.
tar -c --exclude=.git --exclude=mockup --exclude='build*' . | ssh -o BatchMode=yes $S "mkdir -p $D && flock $D.lucchetto sh -c 'cd $D && rm -rf src telefono prove strumenti docs data meson.build meson_options.txt && tar -x -m -C $D && nice $POD run --rm --cap-add SYS_PTRACE -v $D:/lavoro -v /media/YASDE/android-sdk:/android-sdk:ro $IMMAGINE sh strumenti/dentro.sh $P > $L 2>&1 < /dev/null; U=\$?; echo \"uscita \$U\" >> $L; if [ \$U -ne 0 ]; then O=\$(date +%H%M%S); cp $L $L.fallito-\$O; cp $D/build-*/meson-logs/testlog.txt $L.fallito-\$O.prove 2>/dev/null; echo \"registro conservato: $L.fallito-\$O\"; fi; tail -n 25 $L'" 2>/dev/null
