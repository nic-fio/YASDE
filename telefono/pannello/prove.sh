#!/bin/sh
# Prove del pannello (le lancia telefono/costruisci.sh, cwd = telefono/, dopo aver costruito il jar):
# 1. prove su JVM delle parti pure: lettore di eventi di input (ProvaEventi), scelta della frequenza (ProvaFrequenze), logica del telefono sveglio (ProvaSveglio), assenza del PC (ProvaAssenza);
# 2. costruzione del jar di prova per il telefono: solo ProvaPannello, che NON sta nel jar di produzione.
#    Si usa insieme al jar completo (vedi prove/prova_pannello.py).
set -e
cd "$(dirname "$0")/.."
ANDROID=/android-sdk/platforms/android-35/android.jar
rm -rf classi-prove-pannello && mkdir -p classi-prove-pannello/src classi-prove-pannello/jvm classi-prove-pannello/tel
cp pannello/prove/ProvaEventi.java.txt classi-prove-pannello/src/ProvaEventi.java
javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove-pannello/jvm classi-prove-pannello/src/ProvaEventi.java
java -classpath classi:classi-prove-pannello/jvm it.yasde.ProvaEventi
cp pannello/prove/ProvaFrequenze.java.txt classi-prove-pannello/src/ProvaFrequenze.java
javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove-pannello/jvm classi-prove-pannello/src/ProvaFrequenze.java
java -classpath classi:classi-prove-pannello/jvm it.yasde.ProvaFrequenze
cp pannello/prove/ProvaSveglio.java.txt classi-prove-pannello/src/ProvaSveglio.java
javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove-pannello/jvm classi-prove-pannello/src/ProvaSveglio.java
java -classpath classi:classi-prove-pannello/jvm it.yasde.ProvaSveglio
cp pannello/prove/ProvaAssenza.java.txt classi-prove-pannello/src/ProvaAssenza.java
javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove-pannello/jvm classi-prove-pannello/src/ProvaAssenza.java
java -classpath classi:classi-prove-pannello/jvm it.yasde.ProvaAssenza
cp pannello/prove/ProvaPannello.java.txt classi-prove-pannello/src/ProvaPannello.java
javac -Xlint:all -Xlint:-options -Werror -source 8 -target 8 -bootclasspath "$ANDROID" \
    -classpath "$ANDROID:classi" -d classi-prove-pannello/tel classi-prove-pannello/src/ProvaPannello.java
/android-sdk/build-tools/35.0.0/d8 --min-api 34 --lib "$ANDROID" --output pannello/yasde-prova-pannello.jar \
    $(find classi-prove-pannello/tel -name '*.class')
rm -rf classi-prove-pannello
