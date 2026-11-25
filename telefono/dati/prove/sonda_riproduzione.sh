#!/bin/sh
# Compila la sonda dei widget sul server (container) e la copia in ~/.cache/yasde-sviluppo/dati/sonda-riproduzione.jar.
# Poi, col turno del telefono preso:
#   adb push ~/.cache/yasde-sviluppo/dati/sonda-riproduzione.jar /data/local/tmp/sonda-riproduzione.jar
#   adb shell CLASSPATH=/data/local/tmp/sonda-riproduzione.jar app_process / it.yasde.SondaRiproduzione
#   adb shell rm /data/local/tmp/sonda-riproduzione.jar
set -e
S="${YASDE_SERVER:?imposta YASDE_SERVER con il nome o l'indirizzo del server di compilazione}"
D=/media/YASDE/lavoro/sonda-riproduzione
POD="podman --root /media/YASDE/containers --runroot /media/YASDE/containers-run --cgroup-manager=cgroupfs"
cd "$(dirname "$0")"
ssh -o BatchMode=yes $S "rm -rf $D && mkdir -p $D/src/it/yasde" 2>/dev/null
scp -q SondaRiproduzione.java.txt $S:$D/src/it/yasde/SondaRiproduzione.java
ssh -o BatchMode=yes $S "$POD run --rm -v $D:/lavoro -v /media/YASDE/android-sdk:/android-sdk:ro yasde-sviluppo sh -c 'cd /lavoro && A=/android-sdk/platforms/android-35/android.jar && mkdir -p classi && javac -Xlint:-options -source 8 -target 8 -bootclasspath \$A -classpath \$A -d classi src/it/yasde/SondaRiproduzione.java && /android-sdk/build-tools/35.0.0/d8 --min-api 34 --lib \$A --output sonda-riproduzione.jar \$(find classi -name \"*.class\")'" 2>&1 | grep -v storage
mkdir -p ~/.cache/yasde-sviluppo/dati
scp -q $S:$D/sonda-riproduzione.jar ~/.cache/yasde-sviluppo/dati/sonda-riproduzione.jar
ls -l ~/.cache/yasde-sviluppo/dati/sonda-riproduzione.jar
