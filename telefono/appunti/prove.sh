#!/bin/sh
# Prove del pezzo appunti (la lancia telefono/costruisci.sh, cwd = telefono/):
# 1) regole pure su JVM; 2) jar di prova per il telefono (ProvaAppunti, FUORI dal jar di produzione):
#    CLASSPATH=yasde.jar:yasde-prova-appunti.jar app_process / it.yasde.ProvaAppunti ...
set -e
cd "$(dirname "$0")/.."
ANDROID=/android-sdk/platforms/android-35/android.jar
rm -rf classi-prove-appunti && mkdir -p classi-prove-appunti/src classi-prove-appunti/classi
cp appunti/prove/ProvaRegole.java.txt classi-prove-appunti/src/ProvaRegole.java
javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove-appunti/classi classi-prove-appunti/src/ProvaRegole.java
java -classpath classi:classi-prove-appunti/classi it.yasde.ProvaRegole
cp appunti/prove/ProvaAppunti.java.txt classi-prove-appunti/src/ProvaAppunti.java
javac -Xlint:all -Xlint:-options -Werror -source 8 -target 8 -bootclasspath "$ANDROID" \
    -classpath "$ANDROID:classi" -d classi-prove-appunti/classi classi-prove-appunti/src/ProvaAppunti.java
/android-sdk/build-tools/35.0.0/d8 --min-api 34 --lib "$ANDROID" --output appunti/yasde-prova-appunti.jar \
    classi-prove-appunti/classi/it/yasde/ProvaAppunti*.class
rm -rf classi-prove-appunti
