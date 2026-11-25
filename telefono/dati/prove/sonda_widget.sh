#!/bin/sh
# Compila la sonda dei widget sul server (container) e la copia in ~/.cache/yasde-sviluppo/dati/sonda.jar.
# Poi, col turno del telefono preso:
#   adb push ~/.cache/yasde-sviluppo/dati/sonda.jar /data/local/tmp/sonda-widget.jar
#   adb shell CLASSPATH=/data/local/tmp/sonda-widget.jar app_process / it.yasde.SondaWidget
#   adb shell rm /data/local/tmp/sonda-widget.jar
set -e
S="${YASDE_SERVER:?imposta YASDE_SERVER con il nome o l'indirizzo del server di compilazione}"
D=/media/YASDE/lavoro/sonda-widget
POD="podman --root /media/YASDE/containers --runroot /media/YASDE/containers-run --cgroup-manager=cgroupfs"
cd "$(dirname "$0")"
ssh -o BatchMode=yes $S "rm -rf $D && mkdir -p $D/src/it/yasde" 2>/dev/null
scp -q SondaWidget.java.txt $S:$D/src/it/yasde/SondaWidget.java
ssh -o BatchMode=yes $S "$POD run --rm -v $D:/lavoro -v /media/YASDE/android-sdk:/android-sdk:ro yasde-sviluppo sh -c 'cd /lavoro && A=/android-sdk/platforms/android-35/android.jar && mkdir -p classi && javac -Xlint:-options -source 8 -target 8 -bootclasspath \$A -classpath \$A -d classi src/it/yasde/SondaWidget.java && /android-sdk/build-tools/35.0.0/d8 --min-api 34 --lib \$A --output sonda.jar \$(find classi -name \"*.class\")'" 2>&1 | grep -v storage
mkdir -p ~/.cache/yasde-sviluppo/dati
scp -q $S:$D/sonda.jar ~/.cache/yasde-sviluppo/dati/sonda.jar
ls -l ~/.cache/yasde-sviluppo/dati/sonda.jar
