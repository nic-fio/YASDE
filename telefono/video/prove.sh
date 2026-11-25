#!/bin/sh
# Prova su JVM dell'analizzatore delle finestre (la lancia telefono/costruisci.sh, cwd = telefono/).
set -e
cd "$(dirname "$0")/.."
rm -rf classi-prove-video && mkdir classi-prove-video
cp video/prove/ProvaAnalisi.java.txt classi-prove-video/ProvaAnalisi.java
javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove-video classi-prove-video/ProvaAnalisi.java
java -classpath classi:classi-prove-video it.yasde.ProvaAnalisi
cp video/prove/ProvaInoltro.java.txt classi-prove-video/ProvaInoltro.java
javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove-video classi-prove-video/ProvaInoltro.java
java -classpath classi:classi-prove-video it.yasde.ProvaInoltro
cp video/prove/ProvaLiberaTask.java.txt classi-prove-video/ProvaLiberaTask.java
javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove-video classi-prove-video/ProvaLiberaTask.java
java -classpath classi:classi-prove-video it.yasde.ProvaLiberaTask
cp video/prove/ProvaPizzico.java.txt classi-prove-video/ProvaPizzico.java
javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove-video classi-prove-video/ProvaPizzico.java
java -classpath classi:classi-prove-video it.yasde.ProvaPizzico
cp video/prove/ProvaGesti.java.txt classi-prove-video/ProvaGesti.java
javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove-video classi-prove-video/ProvaGesti.java
java -classpath classi:classi-prove-video it.yasde.ProvaGesti
cp video/prove/ProvaBarriera.java.txt classi-prove-video/ProvaBarriera.java
javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove-video classi-prove-video/ProvaBarriera.java
java -classpath classi:classi-prove-video it.yasde.ProvaBarriera
rm -rf classi-prove-video

# Misura della pausa (Fase 0): classe di prova in un jar a parte, fuori da yasde.jar.
ANDROID=/android-sdk/platforms/android-35/android.jar
rm -rf classi-pausa && mkdir classi-pausa
cp video/prove/ProvaPausa.java.txt classi-pausa/ProvaPausa.java
cp video/prove/ProvaRotella.java.txt classi-pausa/ProvaRotella.java
javac -Xlint:all -Xlint:-options -Werror -source 8 -target 8 -bootclasspath "$ANDROID" \
    -classpath "$ANDROID:classi" -d classi-pausa classi-pausa/ProvaPausa.java classi-pausa/ProvaRotella.java
/android-sdk/build-tools/35.0.0/d8 --min-api 34 --lib "$ANDROID" --output prova-pausa.jar classi-pausa/it/yasde/ProvaPausa*.class classi-pausa/it/yasde/ProvaRotella*.class
rm -rf classi-pausa
