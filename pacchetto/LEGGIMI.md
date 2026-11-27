# Pacchetto AppImage di YASDE 2.3

Un solo file, `YASDE-2.3-x86_64.AppImage` (Linux x86_64), piu' l'archivio dei
sorgenti `YASDE-2.3-sorgenti.tar` da pubblicare nella stessa pagina di
rilascio. Nessun file `.desktop` viene installato sul PC (specifica 11.5):
quello dentro il pacchetto serve solo ad `appimagetool` e dichiara
`X-AppImage-Integrate=false`.

**Documenti pubblici solo in inglese** (decisione del 24 nov 2026): quanto finisce
nel pacchetto e un utente puo' leggere e' scritto in inglese da `dentro.sh`
(`README.txt`, `SOURCE-OFFER.txt`, `LIST.txt`, `SYSTEM-REQUIREMENTS.txt`,
`open-meteo.txt`, `type2-runtime/NOTE.txt`, messaggi di `AppRun`, `Comment` del
`.desktop`), con la dicitura «not affiliated with Samsung». Questo `LEGGIMI.md`
non entra nel pacchetto ed e' interno. Il nome del progetto e' YASDE: `dentro.sh`
ha un controllo che FALLISCE se compare il nome vietato (la parola `phone`+`stra`, qualunque maiuscola) nei nomi
dei file, nei testi nostri, nelle stringhe dell'eseguibile o del jar: nessuna
eccezione.

## Come si costruisce
Dal tablet: `pacchetto/costruisci.sh` (esce con lo stesso codice dell'ultima riga
`uscita N` del log). Lavora sul server, nel container `yasde-sviluppo`; log in
`/media/YASDE/log/pacchetto.log`; risultati in
`/media/YASDE/lavoro/pacchetto/` (AppImage e archivio dei sorgenti). Prima
servono compilati `strumenti/prova.sh veloce` (eseguibile) e
`strumenti/prova.sh telefono` (jar).

Scaricato nel lavoro sul server (mai nel repository), solo via https, con versione
fissata e somma verificata prima dell'uso:
- `appimagetool` 1.9.1 e runtime `type2-runtime` 20251108: somme dichiarate da GitHub;
- FFmpeg 7.1.5: tar da ffmpeg.org; la somma fissata e' stata confrontata il 23 nov
  2026 con il campo Checksums-Sha256 dell'indice Sources di Debian trixie
  (firmato, letto da apt): coincidono;
- libtiff 4.7.0-3+deb13u3 e cairo 1.18.4-1: sorgenti Debian presi con
  `apt-get source` (apt li controlla contro l'indice firmato) e in piu' la somma
  dell'orig fissata nello script, letta dallo stesso indice.
- sorgenti del runtime: tar del tag `type2-runtime` 20251108 (somma osservata al
  primo scarico da github.com; gli archivi `archive/refs/tags` di GitHub non
  garantiscono byte identici nel tempo: se la somma cambia lo script si ferma),
  `libfuse` 3.15.0 e `squashfuse` 0.5.2: le versioni e le somme di questi ultimi
  due sono quelle che `scripts/common/install-dependencies.sh` del tag verifica, e
  lo script le ricontrolla in quel file.
Dal container servono `deb-src` (aggiunti per la corsa) e la rete. I sorgenti
Debian gia' scaricati restano in cache (una cartella per pacchetto e versione,
riverificata con il suo `SHA256SUMS`) e non si riscaricano; se una versione non e'
piu' nell'archivio corrente si ripiega su `snapshot.debian.org` datato
20261123 (solo dentro la corsa, `check-valid-until=no`); se manca anche li' la
costruzione si ferma.

## Licenze
- **FFmpeg**: quello di Debian e' GPL e non e' incluso. Si compila uno solo LGPL
  2.1+ (`--disable-gpl --disable-nonfree --disable-everything`) con soltanto cio'
  che il codice usa (decodificatore e parser H.264 con VA-API e Vulkan, encoder
  PNG, muxer mp4, swscale, zlib). Lo script controlla «LGPL version 2.1 or later»
  nel configure e nella libreria.
- **libtiff e cairo**: quelli di Debian portavano dentro `libjbig` e `liblzo2`
  (GPL-2+ senza eccezione). Si ricompilano dai sorgenti Debian (con le patch di
  sicurezza di Debian) senza jbig, lerc, lzma, zstd, webp, deflate (libtiff) e senza
  lzo (cairo): stesse soname (`libtiff.so.6`, `libcairo.so.2`,
  `libcairo-gobject.so.2`, `libcairo-script-interpreter.so.2`). Cairo e'
  sostituito per intero, cosi' nel processo c'e' una sola versione. Lo script
  fallisce se queste librerie non vengono risolte dalle versioni compilate qui, se
  `libjbig`, `liblzo2` o `libLerc` finiscono nel pacchetto, o se compaiono tra i
  NEEDED di un file qualsiasi del pacchetto.
- **Controllo GPL**: lo script FALLISCE se un pacchetto incluso ha un file
  `copyright` che cita una GPL (non LGPL), o non e' in formato leggibile a macchina,
  e non e' nell'elenco di eccezioni motivate dentro `dentro.sh` (venti casi:
  doppia licenza o GPL solo su parti non distribuite, ognuno col suo motivo).
- **Offerta del sorgente**: `usr/share/doc/licenses/SOURCE-OFFER.txt` dice che i
  sorgenti sono pubblicati accanto all'AppImage nella stessa pagina di rilascio,
  nell'archivio `YASDE-2.3-sorgenti.tar` (con `SHA256SUMS`), e elenca versioni e
  origini esatte. L'archivio contiene i sorgenti Debian (`apt-get source`) di ogni
  libreria Debian inclusa, quelli di libtiff e cairo, e il tar di FFmpeg.
  **Va pubblicato insieme all'AppImage.**
- Dentro il pacchetto, per ogni libreria, il suo `copyright`; i testi delle licenze
  comuni che i `copyright` citano (`/usr/share/common-licenses/...`: LGPL, GPL,
  Apache, MPL, CC0...) sono in `common-texts/` (lo script fallisce se uno manca);
  per FFmpeg, libtiff e cairo i testi dai loro sorgenti.
- **Runtime dell'AppImage** (`type2-runtime`, MIT, collegato staticamente a
  libfuse3 LGPL-2.1 e squashfuse BSD-2): i sorgenti del tag, di libfuse 3.15.0 e di
  squashfuse 0.5.2 sono nell'archivio dei sorgenti; licenze e note in
  `usr/share/doc/licenses/type2-runtime/`; citati nell'offerta del sorgente.
  Per zstd, zlib, mimalloc e musl incorporati nel runtime: avvisi riprodotti (musl
  e mimalloc in `type2-runtime/copyright-*`, zstd e zlib nei `copyright` gia' nel
  pacchetto; di zstd si sceglie BSD-3); provenienza Alpine 3.21; versione esatta
  non ricavabile, irrilevante per licenze che chiedono solo l'avviso.

## Icone delle app (tema Kora, GPL-3)
Le icone di alcune app Android vengono dal tema Kora (`github.com/bikass/kora`,
autore bikass, GPL-3), come FILE SEPARATI in `usr/share/yasde/icone-kora/`: mai nel
programma e mai nel repository. Passo isolato in `pacchetto/icone-kora.sh`, chiamato da
`dentro.sh` (`costruisci`, prima dei testi finali) e di nuovo a fine controlli
(`verifica`).
- **Versione fissata**: commit `ff8ee3ce96b7a7a27fb9db8a2c872840bb05a96e` (19 nov 2026),
  costanti in cima a `icone-kora.sh`. Copia locale in `/media/YASDE/terzi/kora`
  (montata nel container come `/terzi`, fuori dal repository e dal lavoro che
  `prova.sh` cancella): si scarica con `git fetch --depth 1` di quel solo commit; se
  esiste gia' al commit giusto si riusa, se e' a un altro commit la costruzione si
  ferma (non la cancella). Scaricamento fallito = costruzione ferma, mai pacchetto
  senza icone in silenzio. Nel repository del tema il tema vero sta in `kora/`.
- **Tabella**: `data/icone-kora.txt` (`<pacchetto> <nome-kora>`); `costruisci.sh` la fa
  viaggiare nello stesso tar. Riga malformata, tabella vuota, nome mancante nel tema o
  collegamento che esce da `kora/` = costruzione ferma. I nomi distinti si convertono
  in PNG 128x128 con sfondo trasparente (`rsvg-convert` da `librsvg2-bin`, installato nel
  container per la corsa, NON nel pacchetto; un SVG non quadrato e' centrato, non deformato).
  Molti nomi sono collegamenti ad altri file: si seguono dentro il tema e si copia il file vero.
- **Dentro il pacchetto**: `usr/share/yasde/icone-kora/` con `tabella.txt` (copia esatta),
  `<nome>.png`, `sorgenti/<nome>.svg` (file veri, nessun collegamento); in
  `usr/share/doc/licenses/`: `kora-icon-theme-GPL-3.txt` (LICENSE del tema) e
  `kora-icon-theme.txt` (crediti, commit, che cosa e' stato fatto, marchi di terzi);
  citato in `LIST.txt`, `README.txt` e `SOURCE-OFFER.txt`. `AppRun` non cambia: il
  programma trova la cartella da `$APPDIR/usr/share/yasde/icone-kora`.
- **Da solo**: sul server, nel container, `AD=<AppDir> TABELLA=<tabella> TMPL=<tmp>
  sh icone-kora.sh [costruisci|verifica]` (con `-v /media/YASDE/terzi:/terzi`).
- Dubbio aperto: la licenza di YASDE non e' decisa; la GPL-3 vale solo per questi file
  separati, ma nessuno di noi e' un legale.

## Collegamenti a Internet (meteo)
L'app si collega a `api.open-meteo.com` (previsioni) e a
`geocoding-api.open-meteo.com` (ricerca di una localita' scritta dall'utente) SOLO
mentre un widget Meteo e' sul desktop; non c'e' un dialogo di consenso: aggiungere
il widget e' la scelta (decisione esplicita del committente, annotata in
`PIANO.md`). Esce: la posizione del telefono arrotondata al multiplo piu' vicino di 0,005 gradi
(circa 500 m), o le coordinate della localita' scelta a mano, oppure il testo
cercato; i server vedono inoltre l'indirizzo IP del PC. Attribuzioni e licenze dei dati (Open-Meteo, CC BY 4.0; luoghi GeoNames,
CC BY 4.0) in `usr/share/doc/licenses/open-meteo.txt`.
Il collegamento TLS usa l'OpenSSL del pacchetto (nessuna autorita' di certificazione
inclusa): cerca quelle del sistema ospite (percorsi predefiniti di OpenSSL piu' i
file noti delle distribuzioni: `/etc/ssl/certs/ca-certificates.crt`,
`/etc/pki/tls/certs/ca-bundle.crt`, `/etc/ssl/ca-bundle.pem`, `/etc/ssl/cert.pem`).
Lo script prova, con la `libcrypto` del pacchetto, che la stessa sequenza carichi le
autorita' del container. Il collegamento TLS vero a `api.open-meteo.com` e' stato
provato a mano dall'AppImage (handshake e verifica del certificato, senza inviare
nulla); nessuna richiesta di previsioni e' stata ancora provata.

## Che cosa c'e' dentro
- `usr/bin/yasde` e `usr/bin/yasde.jar`: il programma cerca il jar nella
  sua stessa cartella. Non legge file di dati a runtime (traduzioni e CSS sono
  compilati nell'eseguibile), quindi non c'e' una cartella `data`.
- `usr/lib/`: le librerie da cui dipende l'eseguibile, senza scendere in quelle
  richieste al sistema: GTK 4 con Pango, GLib, HarfBuzz, GDK-Pixbuf, ecc.; FFmpeg,
  libtiff e cairo compilati qui; Opus; OpenSSL; `libatomic` (GPL-3 con
  eccezione GCC Runtime Library); le librerie del caricatore webp. Il resto e' del
  container (Debian 13, GTK 4.18).
- `usr/lib/gdk-pixbuf-2.0/`: caricatore webp di gdk-pixbuf (png e jpeg sono
  dentro la libreria) e un modello del file dei caricatori con un segnaposto per il
  percorso. `AppRun` lo risolve a ogni avvio in
  `${XDG_CACHE_HOME:-$HOME/.cache}/YASDE/yasde-pixbuf-<pid>.cache` (l'unico
  file scritto sul PC dal pacchetto, in una cartella ammessa dalla specifica).
  Se il percorso del pacchetto contiene `\`, `"`, `&`, `|` o un a capo, oppure la
  cartella non e' scrivibile, `AppRun` rinuncia con un avviso (webp non si apre).
  Limite della pulizia: i file piu' vecchi di 24 ore si tolgono all'avvio
  successivo; un'istanza viva da piu' di 24 ore che non avesse ancora caricato
  immagini potrebbe perdere il suo file se se ne avvia un'altra.
  Lo script verifica che png, jpeg e webp si salvino e si rileggano con questi
  caricatori e che senza di essi la prova fallisca.
- `usr/lib/gio-vuoto/`: cartella vuota; `GIO_MODULE_DIR` punta li' (nessun modulo
  GIO del sistema, legato a un'altra GLib).
- `usr/share/glib-2.0/schemas/`: schemi GSettings di GTK 4, compilati.
- `usr/share/doc/licenses/`: offerta del sorgente, `LIST.txt`,
  `SYSTEM-REQUIREMENTS.txt` (misurato dallo script), `copyright` di ogni pacchetto.
- `AppRun` (usa sempre la propria cartella, ignora un `APPDIR` ereditato),
  `yasde.desktop`, `yasde.svg` (icona provvisoria).

Non ci sono: temi di icone completi (solo le icone Kora della tabella, vedi sopra), altri
caricatori di immagini, moduli GIO, temi e motori GTK.

## Che cosa resta al sistema, e perche'
Lo script stampa e scrive in `SYSTEM-REQUIREMENTS.txt` l'elenco dei NEEDED reali
non inclusi; ogni NEEDED di ogni file del pacchetto e' incluso o in questo elenco,
e `ldd` non da «not found». Ultima misura:
- glibc (`libc`, `libm`, `ld-linux`): legata al sistema.
- Grafica: `libvulkan` (loader e driver Vulkan), `libva` e `libva-drm` (driver
  VA-API), `libdrm`. libdrm resta al sistema perche' i driver Mesa la caricano nello
  stesso processo e vogliono quella del sistema: due copie sarebbero un rischio
  senza vantaggio.
- Server grafico: `libX11` e le `libX*`, `libxcb*`, `libwayland-client`,
  `libwayland-egl`.
- `libfontconfig`: configurazione dei caratteri del PC.
- `libpipewire-0.3`: i suoi plugin si cercano in percorsi del sistema e devono
  essere della stessa versione del demone in esecuzione.
Incluse perche' neutre e non legate ai driver: `libselinux`, `libz`, `libzstd`,
`libpng`, `libjpeg`, ecc. `libstdc++` e `libgcc_s` non servono piu' (niente C++
nel pacchetto dopo l'uscita di libLerc: misurato dallo script).

## Requisiti minimi del sistema ospite (misurati dallo script)
- glibc **2.38 o piu' recente** (`GLIBC_2.38` e' il simbolo piu' recente richiesto
  dai file del pacchetto). Non basta glibc 2.36 (Debian 12). Nessun requisito su
  libstdc++ (lo script misura anche `GLIBCXX`/`CXXABI`: nessuno richiesto).
- **PipeWire: senza `libpipewire-0.3.so.0` il programma NON si avvia** (NEEDED
  dell'eseguibile). Per l'audio serve anche il demone in esecuzione.
- **libva: senza `libva.so.2` e `libva-drm.so.2` il programma NON si avvia**
  (NEEDED di FFmpeg). Per la decodifica hardware serve inoltre il driver VA-API
  della GPU.
- **Vulkan: senza `libvulkan.so.1` il programma NON si avvia**; serve un driver
  Vulkan per la GPU. Serve anche `libdrm.so.2`.
- Librerie X11, XCB e Wayland client, `libfontconfig`.
- FUSE 2 (`libfuse.so.2`) per far partire l'AppImage, oppure
  `--appimage-extract-and-run`: non verificato su sistemi reali.

## Non verificato
- Avvio con interfaccia, Vulkan/VA-API/PipeWire reali, decodifica video con FFmpeg
  minimo, registrazione mp4 e cattura PNG con le librerie incluse; apertura di
  immagini TIFF (libtiff e' incluso solo perche' GTK lo richiede; senza jbig, lerc,
  lzma, zstd, webp: i TIFF compressi in quei modi non si aprono).
- L'AppImage su un'altra distribuzione o con un'altra glibc.
- Rischio di `libz`/`libzstd` incluse rispetto ai driver Mesa del sistema: i driver
  caricati nello stesso processo usano la copia del pacchetto (LD_LIBRARY_PATH);
  non verificato che sia compatibile con tutti.
- Il controllo GPL e' meccanico (righe `License:` dei `copyright`) piu' le
  eccezioni scritte a mano; le eccezioni sono state motivate leggendo i `copyright`
  ma non i sorgenti. Sul runtime: la ricostruzione dai sorgenti
  dell'archivio non e' stata provata.
- Funzioni di libtiff e cairo ricompilate: provate solo che le soname si
  risolvono e che `--help` risponde anche con `LD_BIND_NOW=1`.

## Dimensione
AppImage circa 18 MB (AppDir circa 45 MB); archivio dei sorgenti circa 295 MB.

## Pacchetti del container usati dallo script
`file` (richiesto da `appimagetool`), `webp-pixbuf-loader` (caricatore webp);
solo quando si ricompilano: `nasm` (FFmpeg) e `cmake` (libtiff). Se mancano li
installa con apt per quella corsa; da aggiungere all'immagine
`yasde-sviluppo`.
