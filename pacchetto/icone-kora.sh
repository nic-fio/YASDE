#!/bin/sh
# Passo "icone Kora" della costruzione: gira dentro il container, lo chiama
# dentro.sh ma si puo' lanciare anche da solo (vedi LEGGIMI.md).
# Uso: icone-kora.sh [costruisci|verifica]      (senza argomento: costruisci + verifica)
# Variabili (con valori di default, per la prova da soli le si cambiano):
#   AD       AppDir dove mettere i file            (/lavoro/AppDir)
#   TABELLA  tabella pacchetto -> icona Kora       (/lavoro/sorgente/icone-kora.txt)
#   KORA     copia locale del tema, fuori dal repository e dal lavoro che
#            prova.sh cancella (/terzi/kora = /media/YASDE/terzi/kora)
#   TMPL     cartella di lavoro temporanea         (/lavoro/tmp)
set -eu

AD="${AD:-/lavoro/AppDir}"
TABELLA="${TABELLA:-/lavoro/sorgente/icone-kora.txt}"
KORA="${KORA:-/terzi/kora}"
TMPL="${TMPL:-/lavoro/tmp}"

# Versione FISSATA del tema (ultimo commit del ramo master al 24 nov 2026,
# commit del 19 nov 2026). Cambiarla e' una decisione: la tabella si verifica
# contro questo commit.
KORA_URL=https://github.com/bikass/kora.git
KORA_COMMIT=ff8ee3ce96b7a7a27fb9db8a2c872840bb05a96e
KORA_DATA=2026-11-19

DEST="$AD/usr/share/yasde/icone-kora"
LIC="$AD/usr/share/doc/licenses"
# Stessi limiti del programma che legge la tabella (se lui rifiuterebbe una riga,
# la costruzione si ferma): pacchetto 1-127 caratteri fra lettere, cifre, . _ ;
# nome-kora 1-95 caratteri fra lettere, cifre, . _ + - , non inizia con '.' e non
# contiene "..".
RE_NOME='^[A-Za-z0-9._+-]{1,95}$'
RE_PKG='^[A-Za-z0-9._]{1,127}$'

errore() { echo "ERRORE (icone Kora): $*" >&2; exit 1; }

# Nomi distinti della tabella, uno per riga. Si ferma se una riga e' malformata:
# il programma tollererebbe l'errore, ma un pacchetto con una tabella rotta
# sarebbe un pacchetto senza icone in silenzio.
nomi_tabella() { # file destinazione (nomi distinti)
    [ -f "$1" ] || errore "manca la tabella $1"
    n=0
    while IFS= read -r riga || [ -n "$riga" ]; do
        n=$((n + 1))
        case "$riga" in ''|'#'*) continue ;; esac
        pkg=${riga%% *}; nome=${riga#* }
        # Un solo spazio fra i due campi, niente altro sulla riga.
        [ "$pkg" != "$riga" ] || errore "$1 riga $n: manca il nome dell'icona"
        printf '%s\n' "$pkg" | grep -Eq "$RE_PKG" || errore "$1 riga $n: nome di pacchetto non valido"
        printf '%s\n' "$nome" | grep -Eq "$RE_NOME" || errore "$1 riga $n: nome di icona non valido: $nome"
        case "$nome" in .*|*..*) errore "$1 riga $n: nome di icona non valido (inizia con . o contiene ..): $nome" ;; esac
        printf '%s\n' "$nome"
    done < "$1" > "$2.grezzo"
    # Il ciclo non e' in una pipe: un errore dentro di lui ferma davvero lo script.
    sort -u "$2.grezzo" > "$2"
}

procura_tema() {
    mkdir -p "$(dirname "$KORA")"
    if [ -d "$KORA" ]; then
        # Una copia gia' presente si usa solo se e' al commit giusto; non la si
        # cancella ne' la si sposta (potrebbe servire ad altri).
        [ -d "$KORA/.git" ] || errore "$KORA esiste ma non e' una copia git di Kora: toglila a mano o sistemala"
        HEAD_ATTUALE=$(git -c safe.directory='*' -C "$KORA" rev-parse HEAD 2>/dev/null) || errore "non riesco a leggere il commit di $KORA"
        [ "$HEAD_ATTUALE" = "$KORA_COMMIT" ] || errore "$KORA e' al commit $HEAD_ATTUALE, serve $KORA_COMMIT: aggiornala (git fetch + checkout) o toglila"
        # File modificati o aggiunti a mano: la copia non e' piu' quel commit.
        MOD=$(git -c safe.directory='*' -C "$KORA" status --porcelain 2>/dev/null) || errore "git status fallito in $KORA"
        [ -z "$MOD" ] || errore "$KORA ha file modificati o in piu' rispetto al commit $KORA_COMMIT (git status): ripulisci la copia"
        return 0
    fi
    echo "scarico Kora al commit $KORA_COMMIT ($KORA_DATA)"
    # Cartella temporanea propria (due costruzioni insieme non si pestano i piedi,
    # e in ogni caso il blocco gira sotto flock) e mv -T: mai "dentro" $KORA.
    P=$(mktemp -d "$KORA.parziale.XXXXXX") || errore "non riesco a creare la cartella temporanea accanto a $KORA"
    git init -q "$P"
    ( cd "$P" \
        && git remote add origin "$KORA_URL" \
        && timeout 600 git fetch -q --depth 1 origin "$KORA_COMMIT" \
        && git -c advice.detachedHead=false checkout -q FETCH_HEAD ) \
        || { rm -rf "$P"; errore "scaricamento di Kora ($KORA_URL, commit $KORA_COMMIT) non riuscito"; }
    [ "$(git -c safe.directory='*' -C "$P" rev-parse HEAD)" = "$KORA_COMMIT" ] || { rm -rf "$P"; errore "il commit scaricato non e' $KORA_COMMIT"; }
    [ ! -e "$KORA" ] || { rm -rf "$P"; errore "$KORA e' comparsa nel frattempo"; }
    mv -T "$P" "$KORA" || { rm -rf "$P"; errore "non riesco a mettere Kora in $KORA"; }
}

# Strumento di conversione: solo per la costruzione, non entra nel pacchetto.
serve_strumenti() {
    MANC=""
    for p in librsvg2-bin; do
        dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "install ok installed" || MANC="$MANC $p"
    done
    if [ -n "$MANC" ]; then
        echo "mancano nell'immagine del container, installati per questa corsa:$MANC"
        mkdir -p "$TMPL"
        { apt-get install -y --no-install-recommends $MANC >"$TMPL/apt-kora.txt" 2>&1 \
            || { apt-get update >"$TMPL/apt-kora-upd.txt" 2>&1 && apt-get install -y --no-install-recommends $MANC >"$TMPL/apt-kora.txt" 2>&1; }; } \
            || { tail -n 5 "$TMPL/apt-kora.txt"; errore "non riesco a installare:$MANC"; }
    fi
    command -v rsvg-convert >/dev/null 2>&1 || errore "manca rsvg-convert"
}

# Larghezza e altezza di un PNG dall'intestazione (byte 16-23), tipo di colore (byte 25).
misura_png() { # file -> "LxA tipo"
    set -- $(od -An -tu1 -j16 -N10 "$1")
    echo "$(( $1 * 16777216 + $2 * 65536 + $3 * 256 + $4 ))x$(( $5 * 16777216 + $6 * 65536 + $7 * 256 + $8 )) ${10}"
}

costruisci() {
    command -v git >/dev/null 2>&1 || errore "manca git nel container"
    # Un solo processo alla volta controlla o scarica la copia comune del tema.
    mkdir -p "$(dirname "$KORA")"
    ( flock 9 || exit 1; procura_tema ) 9>"$KORA.lucchetto" || exit 1
    [ -f "$KORA/LICENSE" ] || errore "il tema non ha il file LICENSE"
    grep -q "GNU GENERAL PUBLIC LICENSE" "$KORA/LICENSE" && grep -q "Version 3" "$KORA/LICENSE" || errore "il LICENSE del tema non e' la GPL-3"
    serve_strumenti
    mkdir -p "$TMPL"
    nomi_tabella "$TABELLA" "$TMPL/nomi-kora.txt"
    [ -s "$TMPL/nomi-kora.txt" ] || errore "la tabella $TABELLA non contiene nessuna riga"
    # Nel repository il tema vero sta nella sottocartella kora/ (accanto: kora-pgrey,
    # LICENSE, immagini): i collegamenti devono restare dentro kora/.
    TEMA="$KORA/kora"
    [ -d "$TEMA/apps/scalable" ] || errore "manca $TEMA/apps/scalable"
    RADICE=$(realpath "$TEMA")
    rm -rf "$DEST"
    mkdir -p "$DEST/sorgenti" "$LIC"
    cp "$TABELLA" "$DEST/tabella.txt"
    N=0
    while IFS= read -r nome; do
        src="$TEMA/apps/scalable/$nome.svg"
        [ -e "$src" ] || [ -L "$src" ] || errore "l'icona \"$nome\" della tabella non esiste nel tema (kora/apps/scalable/$nome.svg)"
        # I collegamenti si seguono, ma il risultato deve restare dentro il tema.
        vero=$(realpath "$src" 2>/dev/null) || errore "il collegamento di \"$nome\" non porta a nessun file"
        case "$vero" in "$RADICE"/*) ;; *) errore "\"$nome\" punta fuori dal tema ($vero)" ;; esac
        [ -f "$vero" ] || errore "\"$nome\" non porta a un file normale ($vero)"
        # Il sorgente e' il file vero, col nome che la tabella usa.
        cp "$vero" "$DEST/sorgenti/$nome.svg"
        rsvg-convert -w 128 -h 128 --keep-aspect-ratio -f png -o "$DEST/$nome.png" "$vero" \
            || errore "conversione in PNG fallita per \"$nome\""
        # Un SVG non quadrato esce piu' basso o piu' stretto di 128: lo si rifa
        # centrato in una pagina 128x128 trasparente (senza deformarlo).
        set -- $(misura_png "$DEST/$nome.png")
        if [ "$1" != "128x128" ]; then
            larg=${1%x*}; alt=${1#*x}
            rsvg-convert -w 128 -h 128 --keep-aspect-ratio --page-width 128 --page-height 128 \
                --left $(( (128 - larg) / 2 )) --top $(( (128 - alt) / 2 )) -f png -o "$DEST/$nome.png" "$vero" \
                || errore "conversione in PNG fallita per \"$nome\" (pagina quadrata)"
        fi
        N=$((N + 1))
    done < "$TMPL/nomi-kora.txt"
    echo "icone Kora convertite: $N"

    cp "$KORA/LICENSE" "$LIC/kora-icon-theme-GPL-3.txt"
    cat > "$LIC/kora-icon-theme.txt" <<TESTO
Kora icon theme (selected icons)

Author:     bikass (GitHub user), as stated by the repository (no other
            author or copyright notice is given in the theme's README,
            LICENSE or index.theme, nor in the SVG files used)
Repository: https://github.com/bikass/kora
Commit:     $KORA_COMMIT ($KORA_DATA)
License:    GNU General Public License, version 3 (full text in
            kora-icon-theme-GPL-3.txt, next to this file)

What was done by this package: a selection of the icons of the theme (only
those named in usr/share/yasde/icone-kora/tabella.txt, the list that says which
Android app uses which icon) was converted to PNG, 128x128 pixels with a
transparent background. Command used for each icon (librsvg's rsvg-convert):
  rsvg-convert -w 128 -h 128 --keep-aspect-ratio -f png -o <name>.png <name>.svg
and, for an icon that is not square, the same command with
  --page-width 128 --page-height 128 --left <x> --top <y>
so that it is centered on a transparent 128x128 page. The script that does this
(pacchetto/icone-kora.sh, in the YASDE source repository, with pacchetto/dentro.sh
that calls it) is part of the source of the distributed files.
The original SVG files used are included, unchanged, in
usr/share/yasde/icone-kora/sorgenti/ (one file per icon; where the theme gives an
icon as a link to another file, the file linked to is included under the name
of the icon).

The icons depict trademarks and logos of their respective owners. They are
shown only to identify the corresponding apps. YASDE is not affiliated with
those owners.
TESTO
}

verifica() {
    [ -d "$DEST" ] || errore "manca la cartella $DEST"
    [ -f "$DEST/tabella.txt" ] || errore "manca tabella.txt"
    [ -d "$DEST/sorgenti" ] || errore "manca la cartella sorgenti"
    [ -f "$LIC/kora-icon-theme-GPL-3.txt" ] || errore "manca kora-icon-theme-GPL-3.txt"
    [ -f "$LIC/kora-icon-theme.txt" ] || errore "manca kora-icon-theme.txt"
    grep -q "$KORA_COMMIT" "$LIC/kora-icon-theme.txt" || errore "kora-icon-theme.txt non cita il commit"
    mkdir -p "$TMPL"
    nomi_tabella "$DEST/tabella.txt" "$TMPL/nomi-verifica.txt"
    [ -s "$TMPL/nomi-verifica.txt" ] || errore "tabella.txt vuota"
    V=0
    while IFS= read -r nome; do
        [ -f "$DEST/$nome.png" ] && [ ! -L "$DEST/$nome.png" ] || errore "manca il PNG di $nome"
        [ -f "$DEST/sorgenti/$nome.svg" ] && [ ! -L "$DEST/sorgenti/$nome.svg" ] || errore "manca l'SVG di $nome (o e' un collegamento)"
        set -- $(misura_png "$DEST/$nome.png")
        [ "$1" = "128x128" ] || errore "$nome.png misura $1, non 128x128"
        # Tipo di colore 6 = RGBA: lo sfondo trasparente e' possibile.
        [ "$2" = "6" ] || errore "$nome.png non ha il canale alfa (tipo di colore $2)"
        V=$((V + 1))
    done < "$TMPL/nomi-verifica.txt"
    # Niente collegamenti simbolici e niente file in piu' nella cartella.
    if find "$DEST" -type l | grep -q .; then errore "ci sono collegamenti simbolici in $DEST"; fi
    [ "$(find "$DEST" -maxdepth 1 -type f -name '*.png' | wc -l)" -eq "$V" ] || errore "PNG in piu' o in meno rispetto alla tabella"
    [ "$(find "$DEST/sorgenti" -type f -name '*.svg' | wc -l)" -eq "$V" ] || errore "SVG in piu' o in meno rispetto alla tabella"
    # Ne' file in piu' di altro tipo, ne' sottocartelle.
    [ "$(find "$DEST" -mindepth 1 \( -type f -o -type d \) | wc -l)" -eq $((2 * V + 2)) ] || errore "ci sono file o cartelle in piu' in $DEST"
    echo "icone Kora verificate: $V (PNG 128x128 con alfa, SVG sorgente, tabella, licenze)"
}

case "${1:-tutto}" in
    costruisci) costruisci ;;
    verifica) verifica ;;
    tutto) costruisci; verifica ;;
    *) errore "uso: icone-kora.sh [costruisci|verifica]" ;;
esac
