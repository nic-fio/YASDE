#!/bin/sh
# Prova su JVM di confronto nomi e percorrenza (la lancia telefono/costruisci.sh, cwd = telefono/).
set -e
cd "$(dirname "$0")/.."
rm -rf classi-prove-archivio && mkdir -p classi-prove-archivio/it/yasde
cp archivio/prove/ProvaArchivio.java.txt classi-prove-archivio/it/yasde/ProvaArchivio.java
javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove-archivio classi-prove-archivio/it/yasde/ProvaArchivio.java
java -classpath classi:classi-prove-archivio it.yasde.ProvaArchivio
rm -rf classi-prove-archivio
