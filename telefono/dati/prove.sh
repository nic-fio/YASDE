#!/bin/sh
# Prova su JVM della parte pura dei dati del telefono (la lancia telefono/costruisci.sh, cwd = telefono/).
set -e
cd "$(dirname "$0")/.."
rm -rf classi-prove-dati && mkdir classi-prove-dati
cp dati/prove/ProvaDati.java.txt classi-prove-dati/ProvaDati.java
javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove-dati classi-prove-dati/ProvaDati.java
java -classpath classi:classi-prove-dati it.yasde.ProvaDati
rm -rf classi-prove-dati
