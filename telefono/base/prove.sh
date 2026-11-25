#!/bin/sh
# Prove di base sulla JVM del container (le lancia telefono/costruisci.sh, cwd = telefono/).
# Le classi di prova stanno in classi-prove, fuori dal jar.
set -e
cd "$(dirname "$0")/.."
rm -rf classi-prove && mkdir classi-prove
for p in ProvaProtocollo ProvaSmistatore ProvaPari ProvaResti; do
    cp base/prove/$p.java.txt classi-prove/$p.java
    javac -Xlint:all -Xlint:-options -Werror -classpath classi -d classi-prove classi-prove/$p.java
    java -classpath classi:classi-prove $p
done
rm -rf classi-prove
