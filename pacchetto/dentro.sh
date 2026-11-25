#!/bin/sh
# Gira dentro il container (lo lancia costruisci.sh). Cartella corrente: /lavoro
# (= /media/YASDE/lavoro/pacchetto sul server). Montati in sola lettura:
#   /veloce   il lavoro di "strumenti/prova.sh veloce" (eseguibile ottimizzato)
#   /telefono il lavoro di "strumenti/prova.sh telefono" (jar)
set -eu

VERSIONE=2.1
NOME="YASDE-$VERSIONE-x86_64.AppImage"
EXE=/veloce/build-veloce/src/shell/ossatura/yasde
JAR=/telefono/telefono/yasde.jar
SRC=/lavoro/sorgente
AD=/lavoro/AppDir
CACHE=/lavoro/cache
TMPL=/lavoro/tmp
export TMPDIR=$TMPL

# Strumenti: versione fissata e somma di controllo (SHA-256 dichiarata da
# GitHub per le risorse di quelle release). Niente codice di terzi nel
# repository: si scaricano qui, nel lavoro sul server.
TOOL_URL=https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
TOOL_SHA=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
RUNTIME_URL=https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64
RUNTIME_SHA=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d

# Sorgenti del runtime dell'AppImage (type2-runtime, parte dell'AppImage): il tag
# esatto e le due librerie che il suo script di build scarica (letto in
# scripts/common/install-dependencies.sh del tag 20251108: libfuse 3.15.0 con la
# patch patches/libfuse/mount.c.diff, che sta nel tag, e squashfuse 0.5.2; le
# somme di queste due sono le stesse che quello script verifica). Il runtime e'
# MIT, collegato staticamente a libfuse3 (LGPL-2.1) e squashfuse (BSD-2); zstd,
# zlib, mimalloc e musl vengono da pacchetti Alpine 3.21 di cui non si puo'
# ricavare la versione esatta: vedi LEGGIMI.md. La somma del tar del tag e'
# quella osservata al primo scarico da github.com via https.
T2R_URL=https://github.com/AppImage/type2-runtime/archive/refs/tags/20251108.tar.gz
T2R_SHA=4c4f6df4647c9f01f871d7edd3716d8aeeffda9d22ffbebe3fccd95a6ab52c95
FUSE3_URL=https://github.com/libfuse/libfuse/releases/download/fuse-3.15.0/fuse-3.15.0.tar.xz
FUSE3_SHA=70589cfd5e1cff7ccd6ac91c86c01be340b227285c5e200baa284e401eea2ca0
SQFUSE_URL=https://github.com/vasi/squashfuse/archive/0.5.2.tar.gz
SQFUSE_SHA=db0238c5981dabbd80ee09ae15387f390091668ca060a7bc38047912491443d3
# Archivio di Debian "fotografato" per i sorgenti che l'archivio corrente non ha
# piu' (versioni sostituite da un aggiornamento): usato solo dentro la corsa.
SNAPSHOT=20261123T000000Z

# FFmpeg: sorgente ufficiale, stessa serie (7.1) di quella contro cui e' stato
# compilato l'eseguibile, quindi stesse soname. La somma e' stata verificata il
# 23 nov 2026 contro una fonte indipendente da ffmpeg.org: il campo
# Checksums-Sha256 dell'indice Sources di Debian trixie, firmato dall'archivio e
# controllato da apt (`apt-cache showsrc ffmpeg`, file ffmpeg_7.1.5.orig.tar.xz).
FF_VER=7.1.5
FF_URL=https://ffmpeg.org/releases/ffmpeg-$FF_VER.tar.xz
FF_SHA=de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f
FF=$CACHE/ffmpeg-$FF_VER-installato
# Solo cio' che il codice usa (src/video, src/audio): decodificatore e parser
# H.264 con VA-API e Vulkan (il ripiego software e' il decodificatore stesso),
# codificatore PNG (cattura), muxer mp4 (registrazione; il contenitore va su un
# avio scritto dal programma, quindi nessun protocollo), swscale. Niente GPL,
# niente nonfree, niente librerie esterne oltre a zlib, libva, libdrm, Vulkan.
FF_OPZ="--disable-gpl --disable-nonfree --disable-everything --disable-autodetect \
 --disable-programs --disable-doc --disable-debug --disable-static --enable-shared --enable-pic \
 --disable-avdevice --disable-avfilter --disable-swresample --disable-postproc --disable-network \
 --enable-avcodec --enable-avformat --enable-avutil --enable-swscale \
 --enable-decoder=h264 --enable-parser=h264 --enable-encoder=png --enable-muxer=mp4 \
 --enable-zlib --enable-vaapi --enable-libdrm --enable-vulkan \
 --enable-hwaccel=h264_vaapi --enable-hwaccel=h264_vulkan"

# libtiff e cairo: le versioni di Debian tirano dentro librerie GPL-2+ senza
# eccezione (libjbig, liblzo2), incompatibili con la licenza del progetto. Si
# ricompilano dai sorgenti Debian (orig + patch di sicurezza di Debian, estratti
# con dpkg-source) senza jbig, lerc, lzo: stesse soname di Debian. Cairo si
# sostituisce per intero (cairo, cairo-gobject, script-interpreter), cosi' nel
# processo c'e' una sola versione di cairo. Sorgenti presi con `apt-get source`
# (apt controlla il .dsc e gli archivi contro l'indice Sources firmato) e in piu'
# la somma dell'orig fissata qui, letta da quell'indice il 23 nov 2026
# (`apt-cache showsrc tiff cairo`).
TIFF_DEB=4.7.0-3+deb13u3
TIFF_ORIG=tiff_4.7.0.orig.tar.bz2
TIFF_SHA=2b8b5c4e79cb2a4651796602e45d6179b7f18bc283feab417b8e881ef7e81862
TIFF=$CACHE/tiff-$TIFF_DEB-installato
TIFF_OPZ="-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_LIBDIR=lib -Djbig=OFF -Dlerc=OFF -Dlzma=OFF -Dzstd=OFF -Dwebp=OFF -Dlibdeflate=OFF -Dcxx=OFF -Dtiff-tools=OFF -Dtiff-tests=OFF -Dtiff-contrib=OFF -Dtiff-docs=OFF -Dtiff-opengl=OFF"
CAIRO_DEB=1.18.4-1
CAIRO_ORIG=cairo_1.18.4.orig.tar.xz
CAIRO_SHA=445ed8208a6e4823de1226a74ca319d3600e83f6369f99b14265006599c32ccb
CAIRO=$CACHE/cairo-$CAIRO_DEB-installato
CAIRO_OPZ="--buildtype=release --auto-features=disabled --wrap-mode=nodownload -Dlibdir=lib -Dxlib=enabled -Dxcb=enabled -Dfreetype=enabled -Dfontconfig=enabled -Dpng=enabled -Dzlib=enabled -Dglib=enabled -Dtee=enabled -Dlzo=disabled -Dspectre=disabled -Dsymbol-lookup=disabled -Dtests=disabled -Dgtk_doc=false"
SRCDEB=$CACHE/sorgenti-debian
COSTR=/lavoro/costruzione

# Librerie richieste al sistema ospite (nome senza ".so"): NON si includono.
#  - glibc e le sue parti, runtime del compilatore: legati al sistema;
#  - driver e interfacce grafiche (Vulkan, VA-API, DRM, GL/EGL/GBM): devono
#    combaciare con la GPU e il driver del PC. libdrm in particolare: i driver
#    Mesa (libdrm_amdgpu ecc.) la caricano nello stesso processo e vogliono
#    quella del sistema, due copie diverse sarebbero un rischio senza guadagno;
#  - X11, XCB, Wayland: parlano col server grafico del PC;
#  - fontconfig: legge la configurazione dei caratteri del PC;
#  - PipeWire: i suoi plugin si cercano in percorsi del sistema e vanno alla
#    stessa versione del demone del PC.
# libselinux, libOpenCL, libvpl e libvdpau NON sono qui: libselinux (neutra,
# serve a libmount) si include; le altre non sono piu' tra i NEEDED.
ESCLUSE='^(ld-linux.*|libc|libm|libdl|libpthread|librt|libutil|libresolv|libanl|libnsl|libnss_.*|libgcc_s|libstdc\+\+|libvulkan|libva|libva-.*|libdrm|libGL.*|libEGL.*|libgbm|libX11|libX11-xcb|libXext|libXi|libXcursor|libXdamage|libXfixes|libXrandr|libXinerama|libXrender|libXau|libXdmcp|libxcb.*|libwayland-.*|libfontconfig|libpipewire-0\.3)$'

nome_base() { basename "$1" | sed 's/\.so.*//'; }
escluso() { nome_base "$1" | grep -Eq "$ESCLUSE"; }
errore() { echo "ERRORE: $*" >&2; exit 1; }
needed() { readelf -d "$1" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'; }
# Versione piu' alta richiesta di un tipo di simbolo (GLIBC, GLIBCXX, CXXABI) da tutti i file.
massimo() { for f in "$@"; do readelf --dyn-syms -W "$f" | grep -o "$TIPO""_[0-9][0-9.]*" || true; done | sort -uV | tail -n 1; }

[ -x "$EXE" ] || errore "manca l'eseguibile $EXE (lancia prima strumenti/prova.sh veloce)"
[ -f "$JAR" ] || errore "manca il jar $JAR (lancia prima strumenti/prova.sh telefono)"
[ -f "$SRC/AppRun" ] || errore "mancano i sorgenti del pacchetto in $SRC"
umask 022

rm -rf "$AD" "$TMPL" /lavoro/*.AppImage /lavoro/*-sorgenti.tar /lavoro/ffbuild "$COSTR"
mkdir -p "$AD/usr/bin" "$AD/usr/lib" "$AD/usr/share/glib-2.0/schemas" "$AD/usr/share/doc/licenses" "$CACHE" "$TMPL"

scarica() { # url somma file
    if [ -f "$3" ] && echo "$2  $3" | sha256sum -c - >/dev/null 2>&1; then return 0; fi
    rm -f "$3" "$3.part"
    curl --proto '=https' --proto-redir '=https' -fsSL --retry 2 -m 300 -o "$3.part" "$1" || { echo "ERRORE: download non riuscito: $1" >&2; exit 3; }
    echo "$2  $3.part" | sha256sum -c - || errore "somma di controllo diversa per $1"
    mv "$3.part" "$3"
}

serve_apt() { # pacchetti che servono e che l'immagine del container non ha
    MANCANTI=""
    for p in "$@"; do
        dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "install ok installed" || MANCANTI="$MANCANTI $p"
    done
    if [ -n "$MANCANTI" ]; then
        echo "mancano nell'immagine del container, installati per questa corsa:$MANCANTI"
        apt-get install -y --no-install-recommends $MANCANTI >"$TMPL/apt.txt" 2>&1 || { tail -n 5 "$TMPL/apt.txt"; errore "non riesco a installare:$MANCANTI"; }
    fi
}
# Scarica i sorgenti Debian (nome=versione ...), ognuno nella sua cartella di
# $SRCDEB, che resta tra una corsa e l'altra: quelli gia' scaricati si
# riverificano con il loro SHA256SUMS e non si riscaricano (cosi' un
# aggiornamento di Debian non rompe la costruzione). Per cio' che l'archivio
# corrente non ha piu' si ripiega su snapshot.debian.org datato. Se neanche li'
# c'e', ci si ferma.
dir_sorgente() { echo "$SRCDEB/$(echo "$1" | tr '=:' '__')"; }
sorgenti_debian() {
    mkdir -p "$SRCDEB"
    for item in "$@"; do
        d=$(dir_sorgente "$item")
        if [ -f "$d/.ok" ]; then
            ( cd "$d" && sha256sum -c SHA256SUMS >/dev/null 2>&1 ) || errore "sorgenti gia' scaricati in $d non corrispondono piu' a SHA256SUMS (rovinati?)"
            continue
        fi
        rm -rf "$d"; mkdir -p "$d"
        if ! ( cd "$d" && apt-get source --download-only "$item" ) >"$TMPL/aptsrc.txt" 2>&1; then
            echo "$item non e' piu' nell'archivio corrente: provo snapshot.debian.org ($SNAPSHOT)"
            rm -f "$d"/*
            cat > /etc/apt/sources.list.d/sorgenti-snapshot.list <<EOF
deb-src [check-valid-until=no] https://snapshot.debian.org/archive/debian/$SNAPSHOT/ trixie main
deb-src [check-valid-until=no] https://snapshot.debian.org/archive/debian-security/$SNAPSHOT/ trixie-security main
EOF
            apt-get update >"$TMPL/aptupd2.txt" 2>&1 || true
            ( cd "$d" && apt-get source --download-only "$item" ) >"$TMPL/aptsrc.txt" 2>&1 || { tail -n 8 "$TMPL/aptsrc.txt"; errore "apt-get source non riuscito per $item (ne' archivio corrente ne' snapshot)"; }
        fi
        ( cd "$d" && ls | grep -v '^SHA256SUMS$' | xargs sha256sum > SHA256SUMS ) && touch "$d/.ok"
    done
}

echo "== container: apt e sorgenti Debian"
# Sorgenti Debian per apt-get source (solo dentro questo container usa e getta).
cat > /etc/apt/sources.list.d/sorgenti-pacchetto.list <<'EOF'
deb-src http://deb.debian.org/debian trixie main
deb-src http://deb.debian.org/debian trixie-updates main
deb-src http://deb.debian.org/debian-security trixie-security main
EOF
apt-get update >"$TMPL/aptupd.txt" 2>&1 || { tail -n 5 "$TMPL/aptupd.txt"; errore "apt-get update fallito"; }
# file: richiesto da appimagetool; webp-pixbuf-loader: caricatore webp di gdk-pixbuf.
serve_apt file webp-pixbuf-loader
sorgenti_debian "tiff=$TIFF_DEB" "cairo=$CAIRO_DEB"
echo "$TIFF_SHA  $(dir_sorgente "tiff=$TIFF_DEB")/$TIFF_ORIG" | sha256sum -c - || errore "somma di $TIFF_ORIG diversa da quella fissata"
echo "$CAIRO_SHA  $(dir_sorgente "cairo=$CAIRO_DEB")/$CAIRO_ORIG" | sha256sum -c - || errore "somma di $CAIRO_ORIG diversa da quella fissata"

echo "== FFmpeg $FF_VER (LGPL, minimo)"
STAMPA="$FF_VER $FF_SHA $FF_OPZ"
if [ ! -f "$FF/.configurazione" ] || [ "$(cat "$FF/.configurazione")" != "$STAMPA" ]; then
    rm -rf "$FF"
    # nasm: assembler per la velocita' della decodifica software di FFmpeg.
    serve_apt nasm
    scarica "$FF_URL" "$FF_SHA" "$CACHE/ffmpeg-$FF_VER.tar.xz"
    mkdir /lavoro/ffbuild
    tar -xJf "$CACHE/ffmpeg-$FF_VER.tar.xz" -C /lavoro/ffbuild
    ( cd "/lavoro/ffbuild/ffmpeg-$FF_VER" \
        && ./configure --prefix="$FF" $FF_OPZ > "$TMPL/ff-configure.txt" 2>&1 \
        || { tail -n 20 "$TMPL/ff-configure.txt"; exit 1; } ) || errore "configure di FFmpeg fallito"
    grep -q "License: LGPL version 2.1 or later" "$TMPL/ff-configure.txt" || errore "FFmpeg non risulta LGPL 2.1+ (vedi configure)"
    ( cd "/lavoro/ffbuild/ffmpeg-$FF_VER" && make -j"$(nproc)" > "$TMPL/ff-make.txt" 2>&1 && make install >> "$TMPL/ff-make.txt" 2>&1 \
        || { tail -n 20 "$TMPL/ff-make.txt"; exit 1; } ) || errore "compilazione di FFmpeg fallita"
    mkdir -p "$FF/licenza"
    cp "/lavoro/ffbuild/ffmpeg-$FF_VER/COPYING.LGPLv2.1" "/lavoro/ffbuild/ffmpeg-$FF_VER/COPYING.LGPLv3" \
       "/lavoro/ffbuild/ffmpeg-$FF_VER/LICENSE.md" "$FF/licenza/"
    cp "$TMPL/ff-configure.txt" "$FF/licenza/configure.txt"
    echo "$STAMPA" > "$FF/.configurazione"
    rm -rf /lavoro/ffbuild
else
    echo "gia' compilato in $FF (stessa versione e stesse opzioni)"
fi
for L in libavcodec.so.61 libavutil.so.59 libavformat.so.61 libswscale.so.8; do
    [ -e "$FF/lib/$L" ] || errore "FFmpeg compilato non ha $L (l'eseguibile la chiede: ABI diversa)"
done
grep -q "LGPL version 2.1 or later" "$FF/lib/libavcodec.so.61" || errore "libavcodec compilata non dichiara LGPL 2.1+"

echo "== libtiff $TIFF_DEB (senza jbig, lerc, lzma, zstd, webp, deflate)"
STAMPA="$TIFF_DEB $TIFF_SHA $TIFF_OPZ"
if [ ! -f "$TIFF/.configurazione" ] || [ "$(cat "$TIFF/.configurazione")" != "$STAMPA" ]; then
    rm -rf "$TIFF" "$COSTR"; mkdir -p "$COSTR"
    serve_apt cmake
    ( cd "$COSTR" && dpkg-source -x "$(dir_sorgente "tiff=$TIFF_DEB")/tiff_$TIFF_DEB.dsc" ) >"$TMPL/tiff-x.txt" 2>&1 || { tail -n 5 "$TMPL/tiff-x.txt"; errore "dpkg-source di tiff fallito"; }
    T=$(ls -d "$COSTR"/tiff-*/ | head -n 1)
    ( cmake -S "$T" -B "$T/build-pacchetto" $TIFF_OPZ -DCMAKE_INSTALL_PREFIX="$TIFF" >"$TMPL/tiff-b.txt" 2>&1 \
        && cmake --build "$T/build-pacchetto" -j"$(nproc)" >>"$TMPL/tiff-b.txt" 2>&1 \
        && cmake --install "$T/build-pacchetto" >>"$TMPL/tiff-b.txt" 2>&1 \
        || { tail -n 20 "$TMPL/tiff-b.txt"; exit 1; } ) || errore "compilazione di libtiff fallita"
    mkdir -p "$TIFF/licenza"; cp "$T/LICENSE.md" "$TIFF/licenza/"
    echo "$STAMPA" > "$TIFF/.configurazione"
    rm -rf "$COSTR"
else
    echo "gia' compilato in $TIFF"
fi
[ -e "$TIFF/lib/libtiff.so.6" ] || errore "libtiff compilata non ha libtiff.so.6"

echo "== cairo $CAIRO_DEB (senza lzo)"
STAMPA="$CAIRO_DEB $CAIRO_SHA $CAIRO_OPZ"
if [ ! -f "$CAIRO/.configurazione" ] || [ "$(cat "$CAIRO/.configurazione")" != "$STAMPA" ]; then
    rm -rf "$CAIRO" "$COSTR"; mkdir -p "$COSTR"
    ( cd "$COSTR" && dpkg-source -x "$(dir_sorgente "cairo=$CAIRO_DEB")/cairo_$CAIRO_DEB.dsc" ) >"$TMPL/cairo-x.txt" 2>&1 || { tail -n 5 "$TMPL/cairo-x.txt"; errore "dpkg-source di cairo fallito"; }
    C=$(ls -d "$COSTR"/cairo-*/ | head -n 1)
    ( meson setup "$C/build-pacchetto" "$C" $CAIRO_OPZ --prefix="$CAIRO" >"$TMPL/cairo-b.txt" 2>&1 \
        && meson compile -C "$C/build-pacchetto" >>"$TMPL/cairo-b.txt" 2>&1 \
        && meson install -C "$C/build-pacchetto" >>"$TMPL/cairo-b.txt" 2>&1 \
        || { tail -n 20 "$TMPL/cairo-b.txt"; exit 1; } ) || errore "compilazione di cairo fallita"
    mkdir -p "$CAIRO/licenza"; cp "$C/COPYING" "$C/COPYING-LGPL-2.1" "$C/COPYING-MPL-1.1" "$CAIRO/licenza/"
    echo "$STAMPA" > "$CAIRO/.configurazione"
    rm -rf "$COSTR"
else
    echo "gia' compilato in $CAIRO"
fi
for L in libcairo.so.2 libcairo-gobject.so.2 libcairo-script-interpreter.so.2; do
    [ -e "$CAIRO/lib/$L" ] || errore "cairo compilata non ha $L"
done
if readelf -d "$CAIRO"/lib/*.so.2 | grep -q "liblzo2"; then errore "cairo compilata usa ancora lzo"; fi
if readelf -d "$TIFF/lib/libtiff.so.6" | grep -Eq "libjbig|libLerc"; then errore "libtiff compilata usa ancora jbig o lerc"; fi

echo "== eseguibile e jar"
cp "$EXE" "$AD/usr/bin/yasde"
# Il programma cerca il jar accanto a se' (/proc/self/exe): stessa cartella.
cp "$JAR" "$AD/usr/bin/yasde.jar"
# Il programma non legge file di dati a runtime: traduzioni e CSS sono dentro
# l'eseguibile (data/en e' compilata con meson), quindi niente "data/" da copiare.

echo "== librerie"
LOADER_SRC=/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-webp.so
[ -f "$LOADER_SRC" ] || errore "manca il caricatore webp di gdk-pixbuf ($LOADER_SRC)"
# Mappa soname -> percorso, come le risolve il container con le librerie
# compilate qui (FFmpeg, libtiff, cairo) al posto di quelle di Debian.
LDP="$FF/lib:$TIFF/lib:$CAIRO/lib"
: > "$TMPL/mappa.txt"
for f in "$EXE" "$LOADER_SRC"; do
    if LD_LIBRARY_PATH="$LDP" ldd "$f" | grep -q "not found"; then errore "$f ha dipendenze non risolte nel container"; fi
    LD_LIBRARY_PATH="$LDP" ldd "$f" | awk '$2 == "=>" && $3 ~ /^\// { print $1, $3 }' >> "$TMPL/mappa.txt"
done
for X in "libavcodec.so.61 $FF" "libavutil.so.59 $FF" "libavformat.so.61 $FF" "libswscale.so.8 $FF" "libtiff.so.6 $TIFF" \
         "libcairo.so.2 $CAIRO" "libcairo-gobject.so.2 $CAIRO" "libcairo-script-interpreter.so.2 $CAIRO"; do
    L=${X% *}; DIR=${X#* }
    awk -v n="$L" -v d="$DIR/lib/" '$1 == n && index($2, d) == 1 { ok = 1 } END { exit !ok }' "$TMPL/mappa.txt" || errore "$L non viene dalle librerie compilate qui"
done
# Chiusura transitiva dei NEEDED, senza scendere dentro le librerie escluse.
: > "$TMPL/fatte.txt"
{ needed "$EXE"; needed "$LOADER_SRC"; } > "$TMPL/coda.txt"
while [ -s "$TMPL/coda.txt" ]; do
    L=$(head -n 1 "$TMPL/coda.txt")
    sed -i 1d "$TMPL/coda.txt"
    grep -qxF "$L" "$TMPL/fatte.txt" && continue
    echo "$L" >> "$TMPL/fatte.txt"
    escluso "$L" && continue
    P=$(awk -v n="$L" '$1 == n { print $2; exit }' "$TMPL/mappa.txt")
    [ -n "$P" ] || errore "non so dove sta $L"
    cp -L "$P" "$AD/usr/lib/$L"
    needed "$AD/usr/lib/$L" >> "$TMPL/coda.txt"
done
LD_PIX=usr/lib/gdk-pixbuf-2.0/2.10.0/loaders
mkdir -p "$AD/$LD_PIX" "$AD/usr/lib/gio-vuoto"
cp "$LOADER_SRC" "$AD/$LD_PIX/"
strip --strip-unneeded "$AD"/usr/lib/*.so* "$AD/$LD_PIX"/*.so 2>/dev/null || echo "strip: qualche libreria non strippata (non grave)"
echo "librerie incluse: $(ls "$AD/usr/lib" | grep -c '\.so')"

echo "== caricatori di immagini (gdk-pixbuf)"
# Modello del file dei caricatori: percorsi con segnaposto, risolti da AppRun.
LD_LIBRARY_PATH="$AD/usr/lib" GDK_PIXBUF_MODULEDIR="$AD/$LD_PIX" \
    /usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/gdk-pixbuf-query-loaders "$AD/$LD_PIX"/libpixbufloader-webp.so \
    | sed "s|$AD|@APPDIR@|g" > "$AD/usr/lib/gdk-pixbuf-2.0/loaders.cache.modello"
grep -q "image/webp" "$AD/usr/lib/gdk-pixbuf-2.0/loaders.cache.modello" || errore "il caricatore webp non si e' registrato"
grep -q "@APPDIR@/$LD_PIX/libpixbufloader-webp.so" "$AD/usr/lib/gdk-pixbuf-2.0/loaders.cache.modello" || errore "percorso del caricatore webp non convertito"

echo "== schemi GSettings di GTK"
for f in /usr/share/glib-2.0/schemas/*.gschema.xml; do
    cp "$f" "$AD/usr/share/glib-2.0/schemas/"
done
glib-compile-schemas "$AD/usr/share/glib-2.0/schemas"

echo "== AppRun, .desktop, icona"
cp "$SRC/AppRun" "$AD/AppRun"; chmod 755 "$AD/AppRun"
cp "$SRC/yasde.desktop" "$AD/yasde.desktop"
cp "$SRC/yasde.svg" "$AD/yasde.svg"
cp "$SRC/yasde.svg" "$AD/.DirIcon"
chmod 755 "$AD/usr/bin/yasde"

echo "== controllo dipendenze"
FILES="$AD/usr/bin/yasde $(ls "$AD"/usr/lib/*.so*) $AD/$LD_PIX/libpixbufloader-webp.so"
ERR=0
: > "$TMPL/richieste.txt"
for f in $FILES; do
    for L in $(needed "$f"); do
        if [ -e "$AD/usr/lib/$L" ]; then continue; fi
        if escluso "$L"; then echo "$L" >> "$TMPL/richieste.txt"; continue; fi
        echo "dipendenza ne' inclusa ne' richiesta al sistema: $L (in $(basename "$f"))"; ERR=1
    done
done
for f in $FILES; do
    if LD_LIBRARY_PATH="$AD/usr/lib" ldd "$f" 2>&1 | grep -q "not found"; then
        echo "ldd: dipendenza non risolta in $(basename "$f")"; ERR=1
    fi
done
[ $ERR -eq 0 ] || errore "dipendenze non a posto"
sort -u "$TMPL/richieste.txt" -o "$TMPL/richieste.txt"
echo "dipendenze a posto: ogni NEEDED e' nel pacchetto o richiesto al sistema; ldd senza 'not found'"
echo "richieste al sistema (NEEDED reali): $(tr '\n' ' ' < "$TMPL/richieste.txt")"

for L in libjbig.so.0 liblzo2.so.2 libLerc.so.4; do
    [ ! -e "$AD/usr/lib/$L" ] || errore "$L e' finita nel pacchetto"
done
for f in $FILES; do
    if needed "$f" | grep -Eq '^(libjbig|liblzo2|libLerc)'; then errore "$(basename "$f") ha ancora jbig, lzo o lerc tra i NEEDED"; fi
done
echo "libjbig, liblzo2 e libLerc: assenti dal pacchetto e da ogni NEEDED"

echo "== versioni minime richieste al sistema"
TIPO=GLIBC;   GLIBC_MAX=$(massimo $FILES)
TIPO=GLIBCXX; GLIBCXX_MAX=$(massimo $FILES)
TIPO=CXXABI;  CXXABI_MAX=$(massimo $FILES)
CON_CXX=""
for f in $FILES; do needed "$f" | grep -q '^libstdc++' && CON_CXX="$CON_CXX $(basename "$f")"; done
{
    echo "Required from the host system (measured by dentro.sh on the package files)"
    echo
    echo "Libraries not included, among the real NEEDED entries:"
    sed 's/^/  /' "$TMPL/richieste.txt"
    echo
    echo "Highest symbol versions required: ${GLIBC_MAX:-none} ${GLIBCXX_MAX:-none} ${CXXABI_MAX:-none}"
    echo "Package files that use the system libstdc++:${CON_CXX:- none}"
} > "$AD/usr/share/doc/licenses/SYSTEM-REQUIREMENTS.txt"
cat "$AD/usr/share/doc/licenses/SYSTEM-REQUIREMENTS.txt"

echo "== licenze e offerta del sorgente"
# Per ogni libreria (e per gli schemi) il pacchetto Debian che la contiene e il
# suo file "copyright". FFmpeg compilato qui: i testi LGPL del suo sorgente.
: > "$TMPL/pacchetti.txt"
for f in "$AD"/usr/lib/*.so* "$AD/$LD_PIX/libpixbufloader-webp.so"; do
    O=$(awk -v n="$(basename "$f")" '$1 == n { print $2; exit }' "$TMPL/mappa.txt")
    [ -n "$O" ] || O="$LOADER_SRC"
    case "$O" in "$FF"/*|"$TIFF"/*|"$CAIRO"/*) continue ;; esac
    P=$(dpkg -S "$(realpath "$O")" 2>/dev/null | head -n 1 | cut -d: -f1 || true)
    [ -n "$P" ] || errore "pacchetto sconosciuto per $f"
    echo "$P" >> "$TMPL/pacchetti.txt"
done
dpkg -S /usr/share/glib-2.0/schemas/org.gtk.gtk4.Settings.FileChooser.gschema.xml | head -n 1 | cut -d: -f1 >> "$TMPL/pacchetti.txt"
sort -u "$TMPL/pacchetti.txt" -o "$TMPL/pacchetti.txt"
LIC="$AD/usr/share/doc/licenses"
: > "$LIC/LIST.txt"
: > "$TMPL/sorgenti.txt"
while read -r P; do
    [ -f "/usr/share/doc/$P/copyright" ] || errore "manca /usr/share/doc/$P/copyright"
    mkdir -p "$LIC/$P"
    cp -L "/usr/share/doc/$P/copyright" "$LIC/$P/copyright"
    V=$(dpkg-query -W -f='${Version}' "$P")
    echo "$P $V" >> "$LIC/LIST.txt"
    echo "$P $V  <-  Debian source: $(dpkg-query -W -f='${source:Package} ${source:Version}' "$P")" >> "$TMPL/sorgenti.txt"
done < "$TMPL/pacchetti.txt"
mkdir -p "$LIC/ffmpeg-$FF_VER"
cp "$FF/licenza/COPYING.LGPLv2.1" "$FF/licenza/COPYING.LGPLv3" "$FF/licenza/LICENSE.md" "$LIC/ffmpeg-$FF_VER/"
echo "ffmpeg $FF_VER (built for this package, LGPL 2.1 or later)" >> "$LIC/LIST.txt"
mkdir -p "$LIC/tiff-$TIFF_DEB" "$LIC/cairo-$CAIRO_DEB"
cp "$TIFF/licenza/LICENSE.md" "$LIC/tiff-$TIFF_DEB/"
cp "$CAIRO/licenza/"COPYING* "$LIC/cairo-$CAIRO_DEB/"
echo "tiff $TIFF_DEB (built for this package from the Debian sources, libtiff license)" >> "$LIC/LIST.txt"
echo "cairo $CAIRO_DEB (built for this package from the Debian sources, LGPL 2.1 or MPL 1.1)" >> "$LIC/LIST.txt"
# Testi delle licenze comuni citati dai copyright (/usr/share/common-licenses/X).
mkdir -p "$LIC/common-texts"
grep -rhoE 'common-licenses/[A-Za-z0-9._+-]+' "$LIC" --include=copyright | sort -u | sed 's,common-licenses/,,; s/[.]*$//' | sort -u > "$TMPL/comuni.txt"
while read -r X; do
    [ -f "/usr/share/common-licenses/$X" ] || errore "il testo di licenza comune $X, citato da un copyright, manca nel container"
    cp -L "/usr/share/common-licenses/$X" "$LIC/common-texts/$X"
done < "$TMPL/comuni.txt"
echo "testi di licenza comuni copiati: $(wc -l < "$TMPL/comuni.txt")"
# Runtime dell'AppImage: sorgenti con somma fissata e licenze.
scarica "$T2R_URL" "$T2R_SHA" "$CACHE/type2-runtime-20251108.tar.gz"
scarica "$FUSE3_URL" "$FUSE3_SHA" "$CACHE/fuse-3.15.0.tar.xz"
scarica "$SQFUSE_URL" "$SQFUSE_SHA" "$CACHE/squashfuse-0.5.2.tar.gz"
RT="$LIC/type2-runtime"; mkdir -p "$RT" "$TMPL/rt"
tar -xzf "$CACHE/type2-runtime-20251108.tar.gz" -C "$TMPL/rt" type2-runtime-20251108/LICENSE type2-runtime-20251108/scripts/common/install-dependencies.sh
tar -xJf "$CACHE/fuse-3.15.0.tar.xz" -C "$TMPL/rt" fuse-3.15.0/LICENSE fuse-3.15.0/LGPL2.txt fuse-3.15.0/GPL2.txt
tar -xzf "$CACHE/squashfuse-0.5.2.tar.gz" -C "$TMPL/rt" squashfuse-0.5.2/LICENSE
# Le versioni e le somme qui sotto devono essere quelle che lo script di build del runtime usa.
grep -q "fuse-3.15.0.tar.xz" "$TMPL/rt/type2-runtime-20251108/scripts/common/install-dependencies.sh" \
    && grep -q "$FUSE3_SHA" "$TMPL/rt/type2-runtime-20251108/scripts/common/install-dependencies.sh" \
    && grep -q "$SQFUSE_SHA" "$TMPL/rt/type2-runtime-20251108/scripts/common/install-dependencies.sh" \
    || errore "lo script di build del runtime non usa le versioni e le somme attese di libfuse e squashfuse"
cp "$TMPL/rt/type2-runtime-20251108/LICENSE" "$RT/LICENSE-type2-runtime-MIT"
cp "$TMPL/rt/fuse-3.15.0/LGPL2.txt" "$RT/LGPL2.txt-libfuse3-LGPL-2.1"
cp "$TMPL/rt/fuse-3.15.0/LICENSE" "$RT/LICENSE-libfuse3-note"
cp "$TMPL/rt/fuse-3.15.0/GPL2.txt" "$RT/GPL2.txt-libfuse3-programmi"
cp "$TMPL/rt/squashfuse-0.5.2/LICENSE" "$RT/LICENSE-squashfuse-BSD-2"
# Avvisi di copyright di musl e mimalloc (MIT, incorporati nel runtime): dai
# pacchetti Debian omonimi (la versione esatta incorporata e' irrilevante per
# licenze che chiedono solo di riprodurre l'avviso).
mkdir -p "$TMPL/rtdeb"
( cd "$TMPL/rtdeb" && apt-get download musl libmimalloc3 ) >"$TMPL/rtdeb.txt" 2>&1 || { tail -n 5 "$TMPL/rtdeb.txt"; errore "apt-get download di musl e libmimalloc3 non riuscito"; }
for P in musl libmimalloc3; do
    dpkg-deb -x "$TMPL/rtdeb/${P}_"*.deb "$TMPL/rtdeb/x-$P" || errore "dpkg-deb non riesce ad aprire $P"
    [ -f "$TMPL/rtdeb/x-$P/usr/share/doc/$P/copyright" ] || errore "manca il copyright di $P"
    cp "$TMPL/rtdeb/x-$P/usr/share/doc/$P/copyright" "$RT/copyright-$P"
done
rm -rf "$TMPL/rtdeb"
[ -f "$LIC/libzstd1/copyright" ] && [ -f "$LIC/zlib1g/copyright" ] || errore "mancano i copyright di libzstd1 o zlib1g, citati da NOTE.txt"
cat > "$RT/NOTE.txt" <<TESTO
The AppImage file contains the type2-runtime (tag 20251108), MIT license,
statically linked to:
  - libfuse3 3.15.0 (library: LGPL-2.1; the libfuse programs are GPL-2 and are
    not linked) with the patch patches/libfuse/mount.c.diff of the tag;
  - squashfuse 0.5.2 (BSD-2);
  - zstd, zlib, mimalloc, musl (permissive licenses), from Alpine 3.21 packages.
    Notices reproduced; origin Alpine 3.21; the exact version cannot be
    determined and is irrelevant for licenses that only ask for the notice. The
    notices are:
      * musl (MIT): copyright-musl, next to this file;
      * mimalloc (MIT): copyright-libmimalloc3, next to this file;
      * zstd: ../libzstd1/copyright; for zstd the BSD-3-clause license is chosen
        (zstd is dual-licensed BSD-3-clause or GPL-2);
      * zlib (zlib license): ../zlib1g/copyright.
    The files copyright-musl and copyright-libmimalloc3 are those of the Debian
    packages of the same name.
The sources of the tag, of libfuse 3.15.0 and of squashfuse 0.5.2 are in
YASDE-$VERSIONE-sorgenti.tar, next to the AppImage; with the scripts/ folder of
the tag the runtime can be rebuilt (and, once relinked, swapped into the
AppImage).
TESTO
rm -rf "$TMPL/rt"
# Icone delle app dal tema Kora (GPL-3): file separati, con sorgenti SVG, licenza e
# crediti. Il passo sta in icone-kora.sh (si puo' lanciare anche da solo).
echo "== icone Kora"
AD="$AD" TABELLA="$SRC/icone-kora.txt" KORA=/terzi/kora TMPL="$TMPL" sh "$SRC/icone-kora.sh" costruisci
echo "kora-icon-theme (selected icons, GPL-3; PNG conversions and SVG sources, see kora-icon-theme.txt)" >> "$LIC/LIST.txt"
cat > "$LIC/open-meteo.txt" <<'TESTO'
Weather data by Open-Meteo.com (https://open-meteo.com/).
Data license: CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/).
The service is used under the Open-Meteo terms of use for non-commercial use.
The data is shown only in the Weather widget, with the credit "Weather data by
Open-Meteo.com".

Place search uses the Open-Meteo geocoding service
(geocoding-api.open-meteo.com), which is based on GeoNames
(https://www.geonames.org/), license CC BY 4.0
(https://creativecommons.org/licenses/by/4.0/).

The app connects to api.open-meteo.com (forecasts) and to
geocoding-api.open-meteo.com (search for a place typed by the user) ONLY while
a Weather widget is on the desktop. What is sent: the phone's position rounded
to the nearest multiple of 0.005 degrees (about 500 m), or the coordinates of
the place chosen by hand, or the text searched for; the servers also see the
PC's IP address. There is no consent dialog: adding the widget is the choice.
TESTO
{
    cat <<TESTO
YASDE $VERSIONE

YASDE ("Yet Another Samsung Dex Emulator") shows the apps of an Android phone
as windows on a Linux desktop.

YASDE is an independent project, not affiliated with, endorsed by or sponsored by Samsung. Samsung and Samsung DeX are trademarks of Samsung Electronics Co., Ltd.

Where to find things in this package (usr/share/doc/licenses/):
  SOURCE-OFFER.txt         written offer of the source code of the free libraries
  LIST.txt                 libraries included, with versions
  SYSTEM-REQUIREMENTS.txt  libraries taken from the host system (not included)
  open-meteo.txt           credits and terms for the Weather widget data
  kora-icon-theme.txt      credits for the app icons (Kora, GPL-3); license text in
                           kora-icon-theme-GPL-3.txt; the icons are in usr/share/yasde/icone-kora/
  <package>/copyright      license notices of each included library
  common-texts/            full texts of the common licenses cited by the copyright files
  type2-runtime/           licenses of the AppImage runtime

Network: YASDE connects to api.open-meteo.com and geocoding-api.open-meteo.com
only while a Weather widget is on the desktop (see open-meteo.txt).
TESTO
} > "$LIC/README.txt"
{
    cat <<TESTO
YASDE is an independent project, not affiliated with, endorsed by or sponsored by Samsung. Samsung and Samsung DeX are trademarks of Samsung Electronics Co., Ltd.

WRITTEN OFFER OF THE SOURCE CODE

This package (AppImage) includes free libraries distributed under licenses
that ask for the source code to be obtainable (LGPL, GPL with exception and the
like), among them GTK, GLib, Pango, Cairo, HarfBuzz and FFmpeg. The license text
of each one is in the folder usr/share/doc/licenses/ of the package.

The sources of the included libraries are published next to this AppImage file,
on the same release page, in the archive YASDE-$VERSIONE-sorgenti.tar (with the
SHA-256 sums in SHA256SUMS). The archive contains the exact sources of the
versions listed below.

FFmpeg $FF_VER
  Original source: $FF_URL (in the archive: ffmpeg-$FF_VER.tar.xz)
  SHA-256 of the source: $FF_SHA
  Built for this package by pacchetto/dentro.sh, with these configure options
  (LGPL only, no GPL and no nonfree parts):
    $FF_OPZ
  Resulting libraries: libavcodec, libavformat, libavutil, libswscale.

libtiff $TIFF_DEB and cairo $CAIRO_DEB
  Debian sources (orig + Debian patches), in the archive. Built for this package
  by pacchetto/dentro.sh, without jbig, lerc, lzma, zstd, webp, deflate
  (libtiff) and without lzo (cairo), because those libraries are GPL.
    libtiff: $TIFF_OPZ
    cairo:   $CAIRO_OPZ

AppImage runtime (type2-runtime, tag 20251108)
  Source: $T2R_URL (in the archive: type2-runtime-20251108.tar.gz)
  Statically linked to libfuse3 3.15.0 ($FUSE3_URL, in the archive) and
  squashfuse 0.5.2 ($SQFUSE_URL, in the archive, as squashfuse-0.5.2.tar.gz);
  licenses in usr/share/doc/licenses/type2-runtime/. zstd, zlib, mimalloc and
  musl come from Alpine 3.21 packages (permissive licenses, versions not given).

Kora icon theme (selected icons)
  Author bikass, https://github.com/bikass/kora, GPL-3. The SVG files used are
  included in the AppImage, as plain files, in usr/share/yasde/icone-kora/sorgenti/
  (they are the source of the PNG files next to them); credits and license text
  in usr/share/doc/licenses/kora-icon-theme.txt and kora-icon-theme-GPL-3.txt.

All the other included libraries are binaries of the Debian package indicated
(Debian 13 "trixie", amd64). For each one: binary package, version, source
package and version. The Debian sources are also in the archive; they can also
be found on the official archive (https://deb.debian.org/debian, section
"main"; old versions on https://snapshot.debian.org) with
"apt-get source <source>=<version>".

TESTO
    sed 's/^/  /' "$TMPL/sorgenti.txt"
} > "$LIC/SOURCE-OFFER.txt"
echo "pacchetti con licenza: $(wc -l < "$TMPL/pacchetti.txt") (piu' FFmpeg, libtiff e cairo compilati qui)"

echo "== licenze GPL"
# Un pacchetto il cui file copyright cita una licenza GPL (non LGPL) ci entra
# solo se e' in questo elenco di eccezioni motivate; altrimenti la costruzione
# FALLISCE. Formato: pacchetto|motivo.
cat > "$TMPL/gpl-ok.txt" <<'EOF'
libblkid1|GPL-2+ solo per i programmi e gli script di util-linux, non distribuiti; la libreria e' LGPL-2.1+
libmount1|come libblkid1: GPL-2+ solo per i programmi di util-linux; la libreria e' LGPL-2.1+
libbz2-1.0|GPL-2 solo per i file di packaging Debian; la libreria ha licenza BSD
libdatrie1|GPL-2+ solo per packaging/programmi accessori; la libreria e' LGPL-2.1+
libffi8|GPL solo per script di build e test; la libreria ha licenza MIT
libfreetype6|doppia licenza FTL o GPL-2+: si usa FTL
libgdk-pixbuf-2.0-0|GPL-2+ solo per test e miniature (tests, thumbnailer), non distribuiti; la libreria e' LGPL-2+
libglib2.0-0t64|GPL-2+ solo per script di generazione e strumenti di sviluppo (gen-unicode-tables.pl, glib-gettextize), non distribuiti; la libreria e' LGPL-2.1+
libgraphite2-3|licenza multipla (LGPL-2.1+, MPL-1.1, GPL-2+): si usa LGPL-2.1+
libgtk-4-1|GPL-3+ solo per debian/tests/run-with-display, non distribuito; la libreria e' LGPL-2.1+
libgtk-4-common|come libgtk-4-1 (stesso file copyright)
libharfbuzz0b|GPL-3+ solo per macro e script di build (autoconf); la libreria ha licenza MIT
libharfbuzz-subset0|come libharfbuzz0b (stesso sorgente)
libpng16-16t64|GPL-2+ solo per contrib/ e packaging; la libreria ha licenza libpng
libselinux1|GPL-2 solo per utils/avcstat.c e packaging; la libreria e' di pubblico dominio
libssl3t64|GPL-1+/Artistic solo per script perl di OpenSSL; la libreria e' Apache-2.0
libthai0|GPL-2+ solo per packaging; la libreria e' LGPL-2.1+
libzstd1|doppia licenza BSD-3-clause o GPL-2: si usa BSD-3-clause
libpixman-1-0|copyright non in formato DEP5; testo MIT, nessuna GPL citata
libxkbcommon0|copyright non in formato DEP5; testo MIT, nessuna GPL citata
libatomic1|copyright non in formato DEP5; libatomic e' GPL-3 con l'eccezione GCC Runtime Library (il codice che la usa resta libero dalla GPL)
EOF
GPLERR=0
while read -r P; do
    # Senza righe "License:" (copyright non in formato DEP5) non si puo' giudicare
    # a macchina: serve comunque l'eccezione motivata.
    if ! grep -q '^License: ' "$LIC/$P/copyright" || grep -E '^License: ' "$LIC/$P/copyright" | grep -Eq '(^|[^L])GPL'; then
        if grep -q "^$P|" "$TMPL/gpl-ok.txt"; then
            echo "   ammesso: $P ($(grep "^$P|" "$TMPL/gpl-ok.txt" | cut -d'|' -f2))"
        else
            echo "   GPL NON MOTIVATA: $P"; GPLERR=1
        fi
    fi
done < "$TMPL/pacchetti.txt"
[ $GPLERR -eq 0 ] || errore "nel pacchetto c'e' un pacchetto con licenza GPL non presente tra le eccezioni motivate"

echo "== controllo del nome: nessuna parola vietata nel pacchetto"
# La parola vietata (scritta spezzata, cosi' nemmeno questo file la contiene) non
# deve comparire da nessuna parte, con qualunque maiuscola: nessuna eccezione.
VIETATA="phone""stra"
VIETATA_I="Phone""stra"   # iniziale maiuscola e tutta maiuscola, scritte a mano: /bin/sh non ha ${x^}
VIETATA_M="PHONE""STRA"
# Si controllano i NOMI di tutti i file, il CONTENUTO di TUTTI i file dell'AppDir
# (binari compresi: eseguibile, librerie, testi, icone; nessuna eccezione per i
# testi di terzi) e il contenuto del jar, che e' compresso e quindi si apre prima.
viola() { grep -i "$VIETATA" || true; }
# cerca_in: elenca i file sotto la cartella $1 con la parola dentro (-a: anche i
# binari). Esce 0 se ne trova, 1 se non ne trova, 2 se la ricerca stessa fallisce.
cerca_in() { grep -rail -e "$VIETATA" -- "$1"; }
# Prova del controllo: ogni maiuscola e ogni posizione devono farlo scattare e un
# nome corretto no. Se questa prova non regge, il controllo non vale nulla.
for forma in "foo $VIETATA_I bar" "$VIETATA_M" "$VIETATA" "${VIETATA_I}2" "x it.$VIETATA.Servizio" "(yasde|$VIETATA)-x"; do
    [ -n "$(printf '%s\n' "$forma" | viola)" ] || errore "controllo del nome: non scatta su: $forma"
done
[ -z "$(printf '%s\n' 'yasde-sviluppo YASDE' | viola)" ] || errore "controllo del nome: scatta su un nome corretto"
# Prova della ricerca sull'AppDir: una cartella finta con un file qualunque, anche
# binario, con la parola dentro deve far scattare; senza la parola no; una cartella
# inesistente e' un errore (mai un "pulito" a vuoto).
PN="$TMPL/prova-nome"
rm -rf "$PN"; mkdir -p "$PN/pulita/sotto" "$PN/sporca/sotto"
printf 'yasde\n' > "$PN/pulita/sotto/a.txt"; printf '\000\001yasde\000' > "$PN/pulita/sotto/b.bin"
printf 'yasde\n' > "$PN/sporca/sotto/a.txt"; printf '\000\001x%s\000y\n' "$VIETATA_M" > "$PN/sporca/sotto/b.bin"
RC=0; cerca_in "$PN/sporca" > "$PN/esito.txt" || RC=$?
[ $RC -eq 0 ] && grep -q "sotto/b.bin" "$PN/esito.txt" || errore "controllo del nome: la ricerca non trova la parola in un file binario"
RC=0; cerca_in "$PN/pulita" > "$PN/esito.txt" || RC=$?
[ $RC -eq 1 ] && [ ! -s "$PN/esito.txt" ] || errore "controllo del nome: la ricerca scatta su una cartella pulita"
RC=0; cerca_in "$PN/non-esiste" > /dev/null 2>&1 || RC=$?
[ $RC -eq 2 ] || errore "controllo del nome: la ricerca su una cartella inesistente non e' un errore"
rm -rf "$PN"
[ -d "$AD/usr" ] || errore "controllo del nome: manca $AD/usr"
NOMEERR=0
if find "$AD" | grep -i "$VIETATA"; then echo "parola vietata in un nome di file (vedi sopra)"; NOMEERR=1; fi
RC=0; cerca_in "$AD" > "$TMPL/nome-file.txt" || RC=$?
[ $RC -le 1 ] || errore "controllo del nome: la ricerca sull'AppDir e' fallita"
if [ $RC -eq 0 ]; then cat "$TMPL/nome-file.txt"; echo "parola vietata nel contenuto dei file elencati sopra"; NOMEERR=1; fi
command -v strings >/dev/null 2>&1 || errore "controllo del nome: manca strings"
mkdir -p "$TMPL/jar"
command -v unzip >/dev/null 2>&1 || errore "controllo del nome: manca unzip per aprire il jar"
unzip -q -o "$AD/usr/bin/yasde.jar" -d "$TMPL/jar" || errore "controllo del nome: il jar non si apre"
if ( cd "$TMPL/jar" && find . | grep -i "$VIETATA" ); then echo "parola vietata in un nome di file del jar"; NOMEERR=1; fi
: > "$TMPL/strings-jar.txt"
find "$TMPL/jar" -type f > "$TMPL/file-jar.txt"
while IFS= read -r f; do strings -a "$f" >> "$TMPL/strings-jar.txt" || errore "controllo del nome: strings sul jar e' fallito"; done < "$TMPL/file-jar.txt"
[ -s "$TMPL/strings-jar.txt" ] || errore "controllo del nome: strings sul jar non ha prodotto nulla"
V=$(viola < "$TMPL/strings-jar.txt")
if [ -n "$V" ]; then printf '%s\n' "$V"; echo "parola vietata nel jar"; NOMEERR=1; fi
rm -rf "$TMPL/jar"
[ $NOMEERR -eq 0 ] || errore "la parola vietata compare nel pacchetto (vedi sopra)"
echo "nessuna parola vietata nel pacchetto"

# Controllo finale delle icone Kora (cartella, tabella, PNG 128x128, SVG, licenze).
AD="$AD" TMPL="$TMPL" sh "$SRC/icone-kora.sh" verifica

echo "== prova di AppRun (senza interfaccia)"
timeout 30 "$AD/AppRun" --help > "$TMPL/help.txt" 2>&1 || errore "AppRun --help e' fallito: $(head -n 5 "$TMPL/help.txt")"
grep -q -i "uso\|usage" "$TMPL/help.txt" || errore "AppRun --help: risposta inattesa"
echo "AppRun --help ok"
# Con il binding immediato si risolvono subito tutti i simboli (e le versioni dei
# simboli) di tutte le librerie: le librerie ricompilate qui devono bastare a GTK.
LD_BIND_NOW=1 timeout 30 "$AD/AppRun" --help > "$TMPL/help-now.txt" 2>&1 || errore "AppRun --help con binding immediato fallito: $(head -n 5 "$TMPL/help-now.txt")"
echo "AppRun --help con LD_BIND_NOW=1 ok"

echo "== prova png, jpeg, webp con i caricatori del pacchetto"
gcc -Wall -Werror -o "$TMPL/prova_pixbuf" "$SRC/prova_pixbuf.c" $(pkg-config --cflags --libs gdk-pixbuf-2.0)
sed "s|@APPDIR@|$AD|g" "$AD/usr/lib/gdk-pixbuf-2.0/loaders.cache.modello" > "$TMPL/loaders.cache"
LD_LIBRARY_PATH="$AD/usr/lib" GDK_PIXBUF_MODULE_FILE="$TMPL/loaders.cache" "$TMPL/prova_pixbuf" "$TMPL" > "$TMPL/pixbuf.txt" 2>&1 \
    || { cat "$TMPL/pixbuf.txt"; errore "prova dei caricatori di immagini fallita"; }
cat "$TMPL/pixbuf.txt"
[ "$(grep -c 'salvato e riletto' "$TMPL/pixbuf.txt")" -eq 3 ] || errore "non tutti i formati risultano caricabili"
# Senza il caricatore webp la prova deve fallire (la prova prova davvero qualcosa).
if LD_LIBRARY_PATH="$AD/usr/lib" GDK_PIXBUF_MODULE_FILE=/dev/null "$TMPL/prova_pixbuf" "$TMPL" >/dev/null 2>&1; then
    errore "la prova dei caricatori passa anche senza il caricatore webp: non prova nulla"
fi

echo "== prova della ricerca dei certificati con l'OpenSSL del pacchetto"
gcc -Wall -Werror -o "$TMPL/prova_tls" "$SRC/prova_tls.c" $(pkg-config --cflags --libs openssl)
LD_LIBRARY_PATH="$AD/usr/lib" "$TMPL/prova_tls" > "$TMPL/tls.txt" 2>&1 || { cat "$TMPL/tls.txt"; errore "la ricerca dei certificati con l'OpenSSL del pacchetto non trova autorita'"; }
cat "$TMPL/tls.txt"
grep -q "libcrypto: $AD/usr/lib/" "$TMPL/tls.txt" || errore "la prova non ha usato la libcrypto del pacchetto"

echo "== archivio dei sorgenti"
# Sorgenti esatti (stesse versioni dell'elenco): quelli Debian con apt-get source
# (se non e' possibile, la costruzione si ferma), FFmpeg dal tar scaricato.
# Si pubblica accanto all'AppImage (vedi SOURCE-OFFER.txt).
LISTA=$(while read -r P; do dpkg-query -W -f='${source:Package}=${source:Version}\n' "$P"; done < "$TMPL/pacchetti.txt" | sort -u)
sorgenti_debian $LISTA "tiff=$TIFF_DEB" "cairo=$CAIRO_DEB"
# Cartella di preparazione: solo i file di questa corsa (la cartella dei sorgenti
# scaricati in cache puo' contenerne di corse precedenti).
ST="$TMPL/YASDE-$VERSIONE-sorgenti"
mkdir -p "$ST"
for item in $LISTA "tiff=$TIFF_DEB" "cairo=$CAIRO_DEB"; do
    d=$(dir_sorgente "$item")
    for f in "$d"/*; do
        case "$f" in */SHA256SUMS) continue ;; esac
        cp -f "$f" "$ST/"
    done
done
cp "$CACHE/ffmpeg-$FF_VER.tar.xz" "$CACHE/type2-runtime-20251108.tar.gz" "$CACHE/fuse-3.15.0.tar.xz" "$CACHE/squashfuse-0.5.2.tar.gz" "$ST/"
( cd "$ST" && ls | grep -v '^SHA256SUMS$' | xargs sha256sum > SHA256SUMS )
tar -cf "/lavoro/YASDE-$VERSIONE-sorgenti.tar" -C "$TMPL" "YASDE-$VERSIONE-sorgenti"
ls -l "/lavoro/YASDE-$VERSIONE-sorgenti.tar"
rm -rf "$ST"

echo "== strumenti"
scarica "$TOOL_URL" "$TOOL_SHA" "$CACHE/appimagetool-x86_64.AppImage"
scarica "$RUNTIME_URL" "$RUNTIME_SHA" "$CACHE/runtime-x86_64"
chmod +x "$CACHE/appimagetool-x86_64.AppImage"

echo "== AppImage"
ARCH=x86_64 "$CACHE/appimagetool-x86_64.AppImage" --appimage-extract-and-run --no-appstream \
    --runtime-file "$CACHE/runtime-x86_64" "$AD" "/lavoro/$NOME"
[ -f "/lavoro/$NOME" ] || errore "AppImage non prodotta"
chmod 755 "/lavoro/$NOME"
ls -l "/lavoro/$NOME"; sha256sum "/lavoro/$NOME"

echo "== prova dell'AppImage (senza interfaccia)"
APPIMAGE_EXTRACT_AND_RUN=1 timeout 60 "/lavoro/$NOME" --help > "$TMPL/help2.txt" 2>&1 || errore "AppImage --help e' fallito: $(head -n 5 "$TMPL/help2.txt")"
grep -q -i "uso\|usage" "$TMPL/help2.txt" || errore "AppImage --help: risposta inattesa"
echo "AppImage --help ok"
rm -rf "$TMPL"
