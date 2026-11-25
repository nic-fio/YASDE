#!/bin/sh
# Costruisce il jar del componente. Gira nel container (strumenti/prova.sh).
# Uso: sh telefono/costruisci.sh [cartelle...]   (default: tutte)
# Il contratto (telefono/contratto) c'e' sempre. Risultato: telefono/yasde.jar
set -e
cd "$(dirname "$0")"
SDK=/android-sdk
ANDROID=$SDK/platforms/android-35/android.jar
[ $# -eq 0 ] && set -- base video audio dati archivio appunti pannello controlli
rm -rf classi yasde.jar && mkdir classi
CARTELLE=""
for c in "$@"; do [ -d "$c" ] && CARTELLE="$CARTELLE $c"; done
FILE=$(find contratto $CARTELLE -name '*.java' 2>/dev/null)
javac -Xlint:all -Xlint:-options -Werror -source 8 -target 8 \
    -bootclasspath "$ANDROID" -classpath "$ANDROID" -d classi $FILE
$SDK/build-tools/35.0.0/d8 --min-api 34 --lib "$ANDROID" --output yasde.jar $(find classi -name '*.class')
ls -l yasde.jar
# Prove che girano sulla JVM del container (senza Android): ogni cartella puo'
# avere uno script prove.sh.
for c in "$@"; do
    [ -f "$c/prove.sh" ] && sh "$c/prove.sh"
done
echo "jar costruito"
