#!/bin/sh
# Rifa le schermate del manuale utente con il telefono FINTO (nessun telefono vero,
# nessun dato personale). Apre i demo della shell in uno schermo INVISIBILE
# (compositore "cage" senza monitor: non disturba lo schermo di chi lavora) e li
# fotografa con "grim". Poi crea la copia ottimizzata (Pillow) in docs/sources/img/.
#
# Uso:  docs/sources/img/fai-schermate.sh [nome ...]      (senza nomi: tutte)
#   nomi: connect connect-search desktop launcher window windows-snapped volume notifications
#         phone-popup transfer transfer-list widgets window-menu decoding-warning
# Prima va compilato "veloce" sul server (strumenti/prova.sh veloce): i demo si
# prendono da /media/YASDE/lavoro/veloce/build-veloce.
# Le foto grezze restano in ~/.cache/yasde-sviluppo/schermate/grezze/.
set -e
QUI="$(cd "$(dirname "$0")" && pwd)"
D="$HOME/.cache/yasde-sviluppo/schermate"
G="$D/grezze"
SRV="${YASDE_SERVER:?imposta YASDE_SERVER con il nome o l'indirizzo del server di compilazione}"
B=/media/YASDE/lavoro/veloce/build-veloce/src/shell
mkdir -p "$G" "$D/bin"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

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
    transfer-list) foto transfer-list 7 demo-ossatura --app-neutra \
                   --passi "invia:holiday-photos.zip,clic:tip=Send files to the phone (PC to phone)" ;;
    desktop)   foto desktop 4 demo-ossatura --app-neutra ;;
    launcher)  foto launcher 6 demo-ossatura --app-neutra --passi "clic:tip=App launcher" ;;
    window)    foto window 6 demo-ossatura --app-neutra --passi "app:it.finto.note" ;;
    windows-snapped) foto windows-snapped 9 demo-ossatura --app-neutra \
                   --passi "app:it.finto.note,app:it.finto.browser,hook:aggancia-sinistra:0,hook:aggancia-destra:1" ;;
    volume)    foto volume 6 demo-ossatura --app-neutra --passi "clic:$SIS#0" ;;
    phone-popup) foto phone-popup 6 demo-ossatura --app-neutra --passi "clic:$SIS#1" ;;
    notifications) foto notifications 6 demo-ossatura --app-neutra --passi "clic:$SIS#2" ;;
    transfer)  foto transfer 6 demo-ossatura --app-neutra --passi "clic:tip=Send files to the phone (PC to phone)" ;;
    widgets)   foto widgets 8 demo-ossatura --app-neutra --passi "destro:600:300,clic:testo=Add widget…" ;;
    window-menu) foto window-menu 7 demo-ossatura --app-neutra --passi "app:it.finto.note,clic:css=ph-finestra-app-puls#3" ;;
    decoding-warning) foto decoding-warning 6 demo-ossatura --app-neutra --passi "decodifica-sw" ;;
    *) echo "schermata sconosciuta: $1"; return 1 ;;
    esac
}

scarica demo-ossatura ossatura/demo-ossatura
scarica demo-connessione connessione/demo-connessione
TUTTE="connect connect-search desktop launcher window windows-snapped volume notifications phone-popup transfer transfer-list widgets window-menu decoding-warning"
for n in ${*:-$TUTTE}; do fai "$n"; done

# Copie ottimizzate: stesso nome, in docs/sources/img/ (le grezze restano nella cache).
python3 -I "$QUI/ottimizza.py" "$G" "$QUI" ${*:-$TUTTE}
