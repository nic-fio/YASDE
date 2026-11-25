#!/bin/sh
# Costruisce il jar di prova per il telefono (la lancia telefono/costruisci.sh, cwd = telefono/):
# solo ProvaAudio, che NON sta nel jar di produzione. Si usa insieme al jar principale:
# CLASSPATH=yasde.jar:yasde-prova-audio.jar app_process / it.yasde.ProvaAudio
set -e
cd "$(dirname "$0")/.."
ANDROID=/android-sdk/platforms/android-35/android.jar
rm -rf classi-prova-audio && mkdir -p classi-prova-audio/src classi-prova-audio/classi
cp audio/prove/ProvaAudio.java.txt classi-prova-audio/src/ProvaAudio.java
javac -Xlint:all -Xlint:-options -Werror -source 8 -target 8 -bootclasspath "$ANDROID" \
    -classpath "$ANDROID:classi" -d classi-prova-audio/classi classi-prova-audio/src/ProvaAudio.java
/android-sdk/build-tools/35.0.0/d8 --min-api 34 --lib "$ANDROID" --output audio/yasde-prova-audio.jar \
    $(find classi-prova-audio/classi -name '*.class')
rm -rf classi-prova-audio
