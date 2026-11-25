#!/bin/sh
# Pubblica il sito di YASDE sul server web: rigenera sito/pubblico e ne copia il
# contenuto nella cartella del sito, al posto di quello che c'e'.
#
#   YASDE_SITO_SERVER=<utente>@<server> sito/pubblica.sh
#
# Il server non si scrive nei file (repository pubblico): si passa a ogni lancio.
# La cartella di destinazione e' fissa, /srv/www/yasde.nicfio.it, e deve esistere
# gia' (la crea chi prepara il server, insieme al blocco del sito nella
# configurazione del server web: vedi sito/LEGGIMI.md). Lo script non tocca
# nient'altro sul server.
#
# Come copia: manda le pagine in una cartella temporanea nella home dell'utente
# sul server; solo se sono arrivate intere svuota la cartella del sito e ce le
# sposta. Se la connessione cade a meta' il sito pubblicato resta com'era.
# I file del programma non si pubblicano qui: il sito rimanda alla release su GitHub.
set -eu

S="${YASDE_SITO_SERVER:?imposta YASDE_SITO_SERVER con <utente>@<server> del server web}"
# Solo lettere, cifre, @ . _ - : un valore che comincia con «-» verrebbe letto da ssh come opzione.
case "$S" in
-* | *[!A-Za-z0-9@._-]* | '')
    echo "pubblica.sh: YASDE_SITO_SERVER non valido (atteso <utente>@<server>)" >&2
    exit 1
    ;;
esac
DEST=/srv/www/yasde.nicfio.it
TMP=.yasde-sito-in-arrivo # relativa alla home sul server

cd "$(dirname "$0")/.."

# Si pubblica sempre una copia appena generata, con indirizzo e contatto predefiniti:
# variabili rimaste impostate nella shell non devono cambiarla.
unset YASDE_SITO_USCITA YASDE_SITO_INDIRIZZO YASDE_SITO_CONTATTO
python3 sito/costruisci.py

P=sito/pubblico
[ -f "$P/index.html" ] || { echo "pubblica.sh: manca $P/index.html" >&2; exit 1; }
[ -f "$P/.uscita-sito-yasde" ] || { echo "pubblica.sh: $P non e' stata generata da costruisci.py" >&2; exit 1; }
# Niente collegamenti simbolici sul server web: potrebbero puntare fuori dal sito.
if [ -n "$(find "$P" -type l | head -n 1)" ]; then
    echo "pubblica.sh: in $P ci sono collegamenti simbolici: non pubblico" >&2
    exit 1
fi
# Il contrassegno della cartella generata serve solo qui: non si pubblica.
N=$(find "$P" -type f ! -name .uscita-sito-yasde | wc -l)

# Sul server: DEST e TMP sono costanti scritte qui sopra, mai valori arrivati da fuori.
# Ordine pensato perche' il sito non resti mai vuoto: prima si copiano le pagine nuove
# sopra le vecchie, poi si toglie cio' che nella copia nuova non c'e' piu'. Se qualcosa
# cade a meta', il sito e' vecchio, nuovo o un misto dei due.
tar -C "$P" --exclude=./.uscita-sito-yasde -c . | ssh -o BatchMode=yes -- "$S" "
    set -eu
    case \"\$HOME\" in '' | / | /srv/www | /srv/www/*) echo 'HOME non adatta sul server'; exit 3 ;; esac
    [ -L $DEST ] && { echo 'sul server $DEST e un collegamento simbolico: non pubblico'; exit 3; }
    [ -d $DEST ] || { echo 'sul server manca $DEST: va preparato prima'; exit 3; }
    [ -w $DEST ] || { echo 'sul server $DEST non e scrivibile da questo utente'; exit 3; }
    rm -rf \"\$HOME/$TMP\"
    mkdir \"\$HOME/$TMP\"
    tar -x -m --no-same-owner -C \"\$HOME/$TMP\"
    [ -f \"\$HOME/$TMP/index.html\" ] || { echo 'copia incompleta: il sito resta come prima'; exit 4; }
    [ \"\$(find \"\$HOME/$TMP\" -type f | wc -l)\" = $N ] || { echo 'numero di file diverso: il sito resta come prima'; exit 4; }
    chmod -R a+rX \"\$HOME/$TMP\"
    cp -a \"\$HOME/$TMP/.\" $DEST/
    cd $DEST
    for f in * .[!.]*; do
        [ -e \"\$f\" ] || [ -L \"\$f\" ] || continue
        [ -e \"\$HOME/$TMP/\$f\" ] || rm -rf -- \"\$f\"
    done
    cd /
    rm -rf \"\$HOME/$TMP\"
    chmod -R a+rX $DEST
    echo \"pubblicati \$(find $DEST -type f | wc -l) file in $DEST\"
"
