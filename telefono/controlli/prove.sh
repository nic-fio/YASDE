#!/bin/sh
# Prove del pezzo controlli (le lancia telefono/costruisci.sh, cwd = telefono/, dopo aver costruito il jar):
# 1. prove su JVM (ProvaControlliJvm): comandi di shell del guardiano eseguiti davvero con un `cmd` finto,
#    «trova il telefono» (ogni ramo), stato, comandi, eventi e chiusura con un telefono e un guardiano finti;
# 2. jar di prova per il telefono vero (prove/prova_controlli.py): contratto + base + controlli + la classe
#    di prova ProvaControlliTelefono (che NON sta nel jar di produzione). Cosi' si prova il pezzo anche prima
#    che `it.yasde.Controlli` sia nell'elenco `Servizio.PEZZI`: la classe di prova lo registra da se'.
set -e
cd "$(dirname "$0")/.."
ANDROID=/android-sdk/platforms/android-35/android.jar
rm -rf classi-prove-controlli && mkdir -p classi-prove-controlli/src classi-prove-controlli/jvm classi-prove-controlli/tel classi-prove-controlli/tutto
cp controlli/prove/ProvaControlliJvm.java.txt classi-prove-controlli/src/ProvaControlliJvm.java
javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove-controlli/jvm classi-prove-controlli/src/ProvaControlliJvm.java
java -classpath classi:classi-prove-controlli/jvm it.yasde.ProvaControlliJvm
cp controlli/prove/ProvaControlliTelefono.java.txt classi-prove-controlli/src/ProvaControlliTelefono.java
javac -Xlint:all -Xlint:-options -Werror -source 8 -target 8 -bootclasspath "$ANDROID" -classpath "$ANDROID" \
    -d classi-prove-controlli/tutto $(find contratto base controlli -name '*.java') classi-prove-controlli/src/ProvaControlliTelefono.java
/android-sdk/build-tools/35.0.0/d8 --min-api 34 --lib "$ANDROID" --output controlli/yasde-prova-controlli.jar \
    $(find classi-prove-controlli/tutto -name '*.class')
rm -rf classi-prove-controlli
