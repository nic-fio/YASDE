#!/bin/sh
# Rifa le schermate del manuale utente con il telefono FINTO (nessun telefono vero,
# nessun dato personale). Apre i demo della shell in uno schermo INVISIBILE
# (compositore "cage" senza monitor: non disturba lo schermo di chi lavora) e li
# fotografa con "grim". Poi crea la copia ottimizzata (Pillow) in docs/sources/img/.
#
# Uso:  docs/sources/img/fai-schermate.sh [nome ...]      (senza nomi: tutte)
#   nomi: connect connect-search desktop launcher window windows-snapped volume notifications
#         phone-popup transfer transfer-list widgets window-menu decoding-warning desktop-widgets
# Prima va compilato "veloce" sul server (strumenti/prova.sh veloce): i demo si
# prendono da /media/YASDE/lavoro/veloce/build-veloce.
# Icone delle app: quelle VERE del programma. Si estrae dall'AppImage 2.3 costruita sul
# server (/media/YASDE/lavoro/pacchetto/YASDE-2.3-x86_64.AppImage) la sola cartella
# usr/share/yasde/icone-kora (tabella.txt + PNG del tema Kora, GPL-3), in
# ~/.cache/yasde-sviluppo/schermate/appimage/ (mai nel repository), e la si passa
# cosi' com'e' al programma con YASDE_ICONE_KORA, come fa AppRun. Le app del telefono
# dimostrativo (opzione --app-note di demo-ossatura) sono app note con pacchetto e nome
# veri della tabella: i dati (notifiche, chiamate, rete, meteo, note) restano inventati.
# Le foto grezze restano in ~/.cache/yasde-sviluppo/schermate/grezze/.
set -e
QUI="$(cd "$(dirname "$0")" && pwd)"
D="$HOME/.cache/yasde-sviluppo/schermate"
G="$D/grezze"
SRV="${YASDE_SERVER:?imposta YASDE_SERVER con il nome o l'indirizzo del server di compilazione}"
B=/media/YASDE/lavoro/veloce/build-veloce/src/shell
mkdir -p "$G" "$D/bin"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

# App del telefono dimostrativo (src/shell/finto/finto.c, APP_NOTE) che le schermate usano:
# ognuna deve avere nella tabella vera la sua icona e il suo PNG nell'AppImage.
APP_USATE="com.whatsapp com.android.chrome com.google.android.keep com.google.android.apps.photos"

# Estrae dall'AppImage la cartella delle icone e controlla tabella e PNG.
prepara_icone() {
    A="$D/appimage"; rm -rf "$A"; mkdir -p "$A"
    scp -q -o BatchMode=yes "$SRV:/media/YASDE/lavoro/pacchetto/YASDE-2.3-x86_64.AppImage" "$A/YASDE.AppImage" \
        || { echo "AppImage 2.3 non trovata sul server"; exit 1; }
    chmod +x "$A/YASDE.AppImage"
    # Estrae nella cartella dell'AppImage (squashfs-root), non altrove.
    ( cd "$A" && ./YASDE.AppImage --appimage-extract 'usr/share/yasde/icone-kora/*' > "$A/estrazione.log" 2>&1 ) \
        || { echo "estrazione fallita: $A/estrazione.log"; exit 1; }
    K="$A/squashfs-root/usr/share/yasde/icone-kora"
    [ -f "$K/tabella.txt" ] || { echo "manca tabella.txt nell'AppImage"; exit 1; }
    for p in $APP_USATE; do
        n=$(grep -E "^$p " "$K/tabella.txt" | head -1 | cut -d' ' -f2)
        printf '%s\n' "$n" | grep -Eq '^[A-Za-z0-9][A-Za-z0-9._-]*$' && ! printf '%s' "$n" | grep -q '\.\.' \
            || { echo "$p: non c'e' nella tabella dell'AppImage"; exit 1; }
        [ -f "$K/$n.png" ] || { echo "$p: manca il PNG $n.png nell'AppImage"; exit 1; }
    done
    export YASDE_ICONE_KORA="$K"
}

scarica() { # nome-demo percorso-sul-server
    scp -q -o BatchMode=yes "$SRV:$B/$2" "$D/bin/$1.nuovo" && mv -f "$D/bin/$1.nuovo" "$D/bin/$1"
}

# foto <nome> <secondi> <demo> [opzioni del demo]
foto() {
    N="$1"; T="$2"; DEMO="$3"; shift 3
    exec 9> "$D/.lucchetto"; flock 9
    PRIMA=$(ls "$XDG_RUNTIME_DIR" | grep -E '^wayland-[0-9]+$' | sort)
    # Configurazione e cache di prova, separate da quelle dell'utente.
    rm -rf "$D/home"; mkdir -p "$D/home/config" "$D/home/cache" "$D/home/data"
    # Disegna la scheda grafica (gles2 + Vulkan), come sul PC di chi usa l'app: solo cosi'
    # si vede la sfocatura dietro il titolo delle finestre (con il disegno via processore
    # l'app la spegne). FOTO_PROCESSORE=1 torna al disegno via processore (pixman + cairo).
    if [ -n "$FOTO_PROCESSORE" ]; then WR=pixman; GR=cairo; else WR=gles2; GR=vulkan; fi
    WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=$WR WAYLAND_DISPLAY= \
      setsid timeout $((T + 8)) cage -- env GSK_RENDERER=$GR XDG_CONFIG_HOME="$D/home/config" \
      XDG_CACHE_HOME="$D/home/cache" XDG_DATA_HOME="$D/home/data" \
      "$D/bin/$DEMO" "$@" > "$G/$N.log" 2>&1 &
    S=""
    for i in 1 2 3 4 5 6 7 8 9 10; do
        sleep 0.5
        for w in $(ls "$XDG_RUNTIME_DIR" | grep -E '^wayland-[0-9]+$' | sort); do
            echo "$PRIMA" | grep -qx "$w" || S="$w"
        done
        [ -n "$S" ] && break
    done
    [ -n "$S" ] || { echo "schermo invisibile non partito: $G/$N.log"; return 1; }
    sleep "$T"
    WAYLAND_DISPLAY="$S" grim "$G/$N.png" && echo "foto: $G/$N.png"
    wait 2>/dev/null || true
    flock -u 9
}

# Selettori dei pulsanti (vedi src/shell/ossatura/demo.c): i tre pulsanti di sistema
# della barra sono, in ordine, volume, stato del telefono, notifiche.
SIS="dato=ph-sistema-pulsante"

fai() {
    case "$1" in
    connect)   foto connect 4 demo-connessione --stato non-abbinato ;;
    connect-search) foto connect-search 3 demo-connessione --stato cerca-nuovo ;;
    transfer-list) foto transfer-list 7 demo-ossatura --app-note \
                   --passi "invia:holiday-photos.zip,clic:tip=Send files to the phone (PC to phone)" ;;
    desktop)   foto desktop 4 demo-ossatura --app-note ;;
    launcher)  foto launcher 6 demo-ossatura --app-note --passi "clic:tip=App launcher" ;;
    window)    foto window 6 demo-ossatura --app-note --passi "app:com.google.android.keep" ;;
    windows-snapped) foto windows-snapped 9 demo-ossatura --app-note \
                   --passi "app:com.google.android.keep,app:com.android.chrome,hook:aggancia-sinistra:0,hook:aggancia-destra:1" ;;
    volume)    foto volume 6 demo-ossatura --app-note --passi "clic:$SIS#0" ;;
    phone-popup) foto phone-popup 6 demo-ossatura --app-note --passi "clic:$SIS#1" ;;
    notifications) foto notifications 6 demo-ossatura --app-note --passi "clic:$SIS#2" ;;
    transfer)  foto transfer 6 demo-ossatura --app-note --passi "clic:tip=Send files to the phone (PC to phone)" ;;
    desktop-widgets) foto desktop-widgets 9 demo-ossatura --app-note --passi "vetrina" ;;
    widgets)   foto widgets 8 demo-ossatura --app-note --passi "destro:600:300,clic:testo=Add widget…" ;;
    window-menu) foto window-menu 7 demo-ossatura --app-note --passi "app:com.google.android.keep,clic:css=ph-finestra-app-puls#3" ;;
    decoding-warning) foto decoding-warning 6 demo-ossatura --app-note --passi "decodifica-sw" ;;
    *) echo "schermata sconosciuta: $1"; return 1 ;;
    esac
}

prepara_icone
scarica demo-ossatura ossatura/demo-ossatura
scarica demo-connessione connessione/demo-connessione
TUTTE="connect connect-search desktop launcher window windows-snapped volume notifications phone-popup transfer transfer-list widgets window-menu decoding-warning desktop-widgets"
for n in ${*:-$TUTTE}; do fai "$n"; done

# Copie ottimizzate: stesso nome, in docs/sources/img/ (le grezze restano nella cache).
python3 -I "$QUI/ottimizza.py" "$G" "$QUI" ${*:-$TUTTE}
