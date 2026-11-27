from build import c, file_map, numeri, p, rif, tree

T = "Tests"


def grp(titolo, base, voci):
    """Un gruppo della mappa: base è la cartella, voci = [(nome file, ruolo)]."""
    return (titolo, [((base + "/" if base else "") + n, r) for n, r in voci])


GRUPPI = [
    grp("Root and generator", "", [("meson.build", "Root build file: project options, the list of pieces, the pieces ready for «tutto»")]),
    grp("Manuals", "docs/sources", [("build.py", "Generates the two manuals: functions for text, tables and SVG figures, numbering, source map counting, checks")]),
    grp("Common code", "src/comune", [
        ("buffer.c", "Growing byte buffer"), ("buffer.h", "Contract of the buffer"),
        ("errore.c", "PhErrore: writing and prefixing messages"), ("errore.h", "Error convention of the whole project"),
        ("fotogramma.h", "The neutral video frame (DMA-BUF or memory), pure C"), ("prova_comune.c", T + " of the common code")]),
    grp("ADB client", "src/adb", [
        ("adb.h", "Contract: connection, channels, shell, flow control"), ("chiave.c", "The PC's RSA key, self-signed certificate for TLS, ADB public key format"),
        ("connessione.c", "ADB connection: TCP, CNXN/STLS, TLS 1.3, reader thread, channels, flow control"), ("interno.h", "Internal: protocol packets"),
        ("pacchetto.c", "Packet header reading and writing"), ("shell.c", "The " + c("shell,v2") + " service: packets, output, exit code"),
        ("finto_telefono.c", "Fake ADB daemon on localhost for tests"), ("finto_telefono.h", "Contract of the fake daemon"),
        ("prova_canali.c", T + ": channels, flow control, delayed acknowledgement"), ("prova_chiave.c", T + ": key and certificate"),
        ("prova_pacchetti.c", T + ": packet format"), ("prova_velocita.c", "Throughput measurement"), ("prova_vera.c", "Check against a real phone")]),
    grp("Discovery and pairing", "src/scoperta", [
        ("scoperta.h", "Contract of the mDNS (DNS-SD) discovery, written from scratch"), ("scoperta.c", "mDNS queries and answers"),
        ("scoperta_interno.h", "Internal parts for tests"), ("cerca.c", "Hand tool: lists the phones found in 5 s"), ("prova_scoperta.c", T + " of the discovery")]),
    grp("Pairing", "src/abbinamento", [
        ("abbinamento.h", "Contract: pairing with the six-digit code"), ("abbinamento.c", "Pairing sequence: TLS, SPAKE2, key exchange"),
        ("spake2.h", "SPAKE2 on edwards25519, Android's variant"), ("spake2.c", "SPAKE2 implementation"), ("prova_spake2.c", T + " of SPAKE2")]),
    grp("Connection", "src/collegamento", [
        ("collegamento.h", "Contract: states, borrowed sessions, concurrency rules"), ("collegamento.c", "State machine with its own thread: search, connect, component, guardian, retry"),
        ("prova/finto.c", "Fake lower pieces for the connection tests"), ("prova/finto.h", "Contract of those fakes"), ("prova/lingua.c", "Fake language functions for the tests"),
        ("prova/prova_collegamento.c", T + " of the state machine, with the thread-sanitizer variant")]),
    grp("Files", "src/file", [
        ("file.h", "Contract: copy and listing with " + c("sync:")), ("file.c", "Send, receive, list, stat"), ("finto_canale.c", "Fake channel for tests"), ("finto_canale.h", "Contract of the fake channel"),
        ("prova_file.c", T + " of the file layer"), ("prova_vera.c", "Check against a real phone")]),
    grp("Transfers and installation", "src/trasferimenti", [
        ("trasferimenti.h", "Contract of the queue and the free-name rule"), ("trasferimenti.c", "The transfer queue and its worker thread"),
        ("installa.h", "Contract of install and uninstall"), ("installa.c", "APK recognition, copy, " + c("pm") + ", cleanup, analysis of " + c("pm") + " output"),
        ("prova/finto.c", "Fake lower pieces for the queue tests"), ("prova/finto.h", "Contract of the fake"), ("prova/finto_installa.c", "Fake phone for the install tests"),
        ("prova/finto_installa.h", "Contract of that fake"), ("prova/prova_installa.c", T + " of install and uninstall"), ("prova/prova_trasferimenti.c", T + " of the queue")]),
    grp("The component, PC side", "src/componente", [
        ("componente.h", "Contract: start of the service, command channel, video sessions, input"), ("componente.c", "Startup, command channel, events, heartbeat"),
        ("interno.h", "Internal details"), ("lettura.c", "Resumable reading from a channel"), ("messaggi.c", "Pure part of the protocol: writing and reading messages"),
        ("video.c", "Video sessions: open, read H.264 packets, resize, keyframe, close"), ("prova/finto.c", "Fake component (a phone-side simulator in C)"),
        ("prova/finto.h", "Contract of the fake"), ("prova/prova_componente.c", T + " of the component"), ("prova/prova_messaggi.c", T + " of the message format")]),
    grp("Phone data", "src/dati", [
        ("dati.h", "Contract: apps, icons, state, notifications, storage, calendar, alarm, battery, switches, position, calls, network, media, search, clipboard"),
        ("interno.h", "Internal: message types"), ("richieste.c", "Requests 0x70–0x75 and events 0x7e–0x7f"), ("nuovi.c", "Month calendar, alarm, battery, quick switches, position, calls, network, media and the media command (0x91–0x94)"),
        ("cerca.c", "File search on the " + c("cerca") + " channel"), ("appunti.c", "Shared clipboard channel"), ("canale.c", "Message reading from a channel, safe against a hostile phone"),
        ("utf8.c", "Cleaning of strings that come from the phone"), ("prova/finto.c", "Fake component for the data tests"), ("prova/finto.h", "Contract of the fake"), ("prova/prova_dati.c", T + " of the data layer")]),
    grp("Video", "src/video", [
        ("decodifica.h", "Contract of H.264 decoding (FFmpeg, hardware accelerated)"), ("decodifica.c", "Decoder with VA-API or Vulkan and a software fallback"),
        ("esporta.h", "Contract: decoded frame to the neutral frame"), ("esporta.c", "DMA-BUF export without copies, or memory"), ("esporta_interno.h", "Internal parts of the export"),
        ("colore.c", "Color choices and checks on shared planes"), ("colore_interno.h", "Internal color parts"), ("cattura.h", "Contract: screenshot (PNG) and recording (MP4)"),
        ("cattura_interno.h", "Internal parts of capture"), ("cattura_nome.c", "File names of captures, created without overwriting"), ("cattura_png.c", "Frame to RGB to PNG"),
        ("cattura_mp4.c", "Fragmented MP4 recording without recoding"), ("misura_decodifica.c", "Measurement tool for decoding"), ("prova/aiuti.h", "Test helpers"),
        ("prova/prova_cattura_mp4.c", T + " of MP4 recording"), ("prova/prova_cattura_ostili.c", T + " with hostile input for capture"), ("prova/prova_cattura_png.c", T + " of PNG capture"),
        ("prova/prova_esporta.c", T + " of the frame export"), ("prova/prova_ordine_via.c", T + " of the automatic order of the decoding vias")]),
    grp("Audio", "src/audio", [
        ("audio.h", "Contract: receive Opus packets, decode and play with PipeWire"), ("audio.c", "The player: channel reading, decoding, queue, output"),
        ("pacchetti.c", "Reading of audio packets (time, length, data)"), ("decodifica.c", "Opus decoding, 48 kHz stereo"), ("flusso.c", "Packet to queue: decoding and gap recognition"),
        ("coda.c", "Decoded audio queue with an adaptive margin"), ("uscita_pipewire.c", "PipeWire playback stream"), ("suona.c", "Hand tool: plays a stream or a 440 Hz tone"),
        ("interno.h", "Internal parts"), ("prova/aiuti.h", "Test helpers"), ("prova/finto_canale.c", "Fake audio channel"), ("prova/finto_canale.h", "Contract of the fake"),
        ("prova/prova_audio.c", T + " of the player"), ("prova/prova_coda.c", T + " of the queue"), ("prova/prova_decodifica.c", T + " of decoding"), ("prova/prova_pacchetti.c", T + " of packet reading")]),
    grp("Window rules", "src/finestre", [
        ("finestre.h", "Contract: order, snapping, gestures, stream size, density, input translation"), ("finestre.c", "The rules (pure logic)"), ("prova_finestre.c", T + " of the rules")]),
    grp("Configuration and language", "src/config", [
        ("config.h", "Contract: folders, files, preferences, phone profile"), ("config.c", "Reading and writing of the three configuration files, cleanup"),
        ("config_interno.h", "Internal parts"), ("percorsi.c", "XDG folders, safe write"), ("toml.h", "Contract of the TOML subset"),
        ("toml.c", "Reader and writer of the TOML subset"), ("profilo.h", "Contract of the measurements file"), ("profilo.c", "Reading and writing of the measurements file"),
        ("lingua.h", "Contract: " + c("ph_t") + " and placeholders"), ("lingua.c", "Active language and lookup"), ("lingua_testo.c", "Placeholder composition and comparison"),
        ("lingua_tabella.h", "Shape of the generated table"), ("genera_traduzioni.c", "Build-time generator of the English table"), ("tabella_finta.c", "Fake table for the language tests"),
        ("prova_config.c", T + " of configuration"), ("prova_lingua.c", "Scan of every " + c("ph_t") + " call in the sources"), ("prova_lingua_funzioni.c", T + " of the language functions"),
        ("prova_profilo.c", T + " of the measurements file")]),
    grp("Weather", "src/meteo", [
        ("meteo.h", "Contract: forecasts, place search, rules of privacy and frequency"), ("meteo.c", "Requests, cache, frequency rules, request paths"),
        ("analisi.c", "JSON reader for the forecast and the place search"), ("http.c", "Minimal HTTP/1.1 client"), ("tls.c", "HTTPS with OpenSSL: name resolution, connection, certificate check"),
        ("tls.h", "Contract of the TLS layer"), ("interno.h", "Internal parts"), ("prova/prova_meteo.c", T + " of the weather piece"), ("prova/prova_tls.c", T + " of TLS"),
        ("prova/tls_finto.c", "Fake TLS server for tests"), ("prova/tls_finto.h", "Contract of the fake server")]),
    grp("Shell contracts", "src/shell", [
        ("servizi.h", "THE contract: the only door to the phone, audio, transfers, persistence"), ("servizi_backend.h", "How to write a backend of the services"),
        ("shell.h", "The shell context: delivery, layers, popups, handlers, style, icons, glass surfaces"), ("superficie.h", "Contract of the video surface widget"), ("prova_css.h", "Common detector of hand-written colours in the pieces' CSS, used by their tests"),
        ("pezzi.h", "Entry points of the pieces"), ("fotogramma.h", "Includes the neutral frame")]),
    grp("Shell: common piece", "src/shell/comune", [
        ("consegna.c", "Delivery to the GTK thread, monotonic clock, decision log"), ("shell.c", "The shell context: layers, popups, handlers"), ("servizi.c", "Wrapper of a backend: events, snapshots, jobs, cancel"),
        ("stile.c", "Loading and checking of CSS, icons"), ("vetro.c", "Glass surfaces: registry, blur behind them"), ("prova_vetro.c", T + " of the glass"), ("ascoltatori.c", "Listener lists and the registry of live objects"), ("interno.h", "Internal parts"), ("prova_comune.c", T + " of the common piece")]),
    grp("Shell: skeleton", "src/shell/ossatura", [
        ("main.c", "The program's " + c("main")), ("ossatura.c", "Application, startup, ordered shutdown"), ("ossatura.h", "Internal parts"), ("finestra.c", "The host window, full screen, host state"),
        ("velo.c", "Lock and connection veil"), ("appunti.c", "PC and phone clipboard link"), ("logica.c", "Pure logic: veil content, what covers the shell, host visibility, stream state machine"),
        ("logica.h", "Contract of that logic"), ("servizi_veri.c", "Core of the real services: connection, manager thread, pools, profile, shutdown"), ("sv_interno.h", "Internal model of the real services"),
        ("sv_dati.c", "Phone data, state, panel"), ("sv_extra.c", "Calendar, alarm, battery, switches, position"), ("sv_sondaggi.c", "Recent calls, network and media: one thread that reads only what is observed, at fixed rhythms"), ("sv_file.c", "Folder listing, search, folders, thumbnails, reading"),
        ("sv_flussi.c", "Video streams: open, pause, resize, input, screenshot, recording"), ("sv_audio.c", "Audio and volume"), ("sv_tr.c", "The transfer queue's session life"),
        ("sv_abbina.c", "Pairing from the shell"), ("sv_meteo.c", "Weather observation and place search"), ("demo.c", "Demo of the whole shell over the fake backend"),
        ("finti_sotto.c", "Fake lower pieces that sleep like the network"), ("finti_sotto.h", "Contract of those fakes"), ("prova_ossatura.c", T + " of the skeleton's logic"),
        ("prova_servizi_veri.c", T + " of the real services over fakes"), ("prova_sondaggi_veri.c", T + " of those readings over fake lower pieces, no phone"), ("prova_meteo_veri.c", T + " of the real weather side"),
        ("prova_decodifica_avviso.c", T + " of the on-screen warning about CPU video decoding")]),
    grp("Shell: video surface", "src/shell/superficie", [
        ("superficie.c", "The widget: reader thread, textures, blur, protected screen"), ("logica.c", "Frame swap, rhythm, format maps, exact rectangle"), ("logica.h", "Contract of that logic"),
        ("demo.c", "Demo with three surfaces"), ("prova_superficie.c", T + " of the surface's logic")]),
    grp("Shell: inner windows", "src/shell/finestra-app", [
        ("finestra_app.c", "Inner windows: title bar, buttons, input, pause, capture, Compatibility"), ("logica.c", "Keys, short names, pinch state machine, translation of points"), ("logica.h", "Contract of that logic"),
        ("finestra_app_demo.h", "Demo hooks"), ("demo.c", "Demo of the windows"), ("prova_finestra_app.c", T + " of the windows' logic")]),
    grp("Shell: bar", "src/shell/barra", [
        ("barra.c", "The bar widget"), ("logica.c", "Pure logic of the bar"), ("logica.h", "Contract of that logic"), ("demo.c", "Demo of the bar"), ("prova_barra.c", T + " of the bar's logic")]),
    grp("Shell: launcher", "src/shell/launcher", [
        ("launcher.c", "The launcher widget"), ("launcher_interno.h", "Internal parts"), ("logica.c", "Search filter"), ("logica.h", "Contract of that logic"), ("demo.c", "Demo of the launcher"),
        ("prova_demo.c", "Helper of the demo test"), ("prova_demo.h", "Contract of that helper"), ("prova_launcher.c", T + " of the launcher's logic")]),
    grp("Shell: desktop", "src/shell/desktop", [
        ("desktop.c", "The desktop: layers, menus, drag and drop"), ("sfondo.c", "Wallpaper drawing and loading"), ("immagine.c", "Wallpaper image decoding and reduction"), ("icone.c", "Icon grid"),
        ("dialoghi.c", "The wallpaper and widget dialogs"), ("widget.c", "Widget catalog and cards"), ("wx.c", "Table of the newer widget types"), ("wx_calendario.c", "Month calendar widget"),
        ("wx_controlli.c", "Quick switches widget"), ("wx_elenchi.c", "Frequent apps and recent transfers widgets"), ("wx_meteo.c", "Weather widget"), ("wx_note.c", "Quick notes widget (one text buffer shared by all of them)"), ("wx_nuovi.c", "Recent calls, Network and Now playing widgets"), ("wx_telefono.c", "Alarm and battery widgets"),
        ("orologio.c", "Clock widget"), ("meteo_disegno.c", "Weather icons and drawings"), ("meteo_luogo.c", "Place search dialog of the weather"), ("logica.c", "Pure logic of the desktop"),
        ("logica.h", "Contract of that logic"), ("logica_wx.c", "Pure logic of the newer widgets"), ("logica_wx.h", "Contract of that logic"), ("logica_meteo.c", "Pure logic of the weather widget"),
        ("logica_meteo.h", "Contract of that logic"), ("logica_note.c", "Pure logic of the Quick notes widget (what text is accepted, how much room is left)"), ("logica_note.h", "Contract of that logic"), ("logica_nuovi.c", "Pure logic of the Recent calls, Network and Now playing widgets"), ("logica_nuovi.h", "Contract of that logic"), ("prova_nuovi.c", T + " of that logic, without a screen"), ("demo_nuovi.c", "Screen demo of the three new widgets"), ("demo_vetrina.c", "Demo only (built into demo-ossatura, never in the program): the fixed arrangement of eleven widgets with invented data, used for the showcase screenshot"), ("prova_note.c", T + " of that logic, without a screen"), ("demo_note.c", "Screen demo of Quick notes"), ("componenti.c", "Drawing parts shared by the widgets (ring, sliced bar, curve, rows, large number)"), ("logica_comp.c", "Pure geometry of those parts"), ("logica_comp.h", "Contract of that logic"),
        ("logica_vecchi.c", "Pure logic of storage, notifications, day calendar and status widgets"), ("logica_vecchi.h", "Contract of that logic"), ("logica_telefono.c", "Pure logic of the battery and alarm widgets"), ("logica_telefono.h", "Contract of that logic"),
        ("logica_controlli.c", "Pure logic of the quick switches widget"), ("logica_controlli.h", "Contract of that logic"), ("interno.h", "Internal parts"), ("demo.c", "Demo of the desktop"), ("demo_hook.c", "Demo commands for the widgets"), ("demo_controlli.c", "Screen demo of the quick switches"), ("demo_meteo.c", "Screen demo of the weather"), ("demo_senzafondo.c", "Screen demo of the “No background” style of the clock and the weather"),
        ("prova_desktop.c", T + " of the desktop's logic"), ("prova_vecchi.c", T + " of the storage, notifications, calendar and status logic"), ("prova_telefono.c", T + " of the battery and alarm logic"), ("prova_controlli.c", T + " of the quick switches logic"), ("prova_meteo_wx.c", T + " of the weather sizes")]),
    grp("Shell: system area", "src/shell/sistema", [
        ("sistema.c", "Installation of the system piece"), ("notifiche.c", "Notification panel"), ("toast.c", "Toasts and desktop notices"), ("telefono.c", "Phone status popup"), ("volume.c", "Volume popup"),
        ("pulsanti.c", "The three bar buttons"), ("logica.c", "Pure logic of the system piece"), ("logica.h", "Contract of that logic"), ("interno.h", "Internal parts"), ("demo.c", "Demo of the system piece"),
        ("prova_sistema.c", T + " of the system piece's logic")]),
    grp("Shell: transfer", "src/shell/trasferimento", [
        ("trasferimento.c", "The send and receive panels, drop target"), ("navigatore.c", "Folder and file navigator"), ("navigatore.h", "Contract of the navigator"), ("elenco.c", "Queue rows"), ("elenco.h", "Contract of the rows"),
        ("percorsi.c", "Pure logic: paths, free names, sizes, pages, completed transfers"), ("percorsi.h", "Contract of that logic"), ("apk.c", "APK installation confirmation"), ("apk.h", "Contract of the confirmation"),
        ("interno.h", "Internal parts"), ("demo.c", "Demo of the transfer piece"), ("prova.c", "Test helper"), ("prova.h", "Contract of that helper"), ("prova_trasferimento.c", T + " of the transfer piece's logic")]),
    grp("Shell: first connection", "src/shell/connessione", [
        ("connessione.c", "The first connection and pairing screen"), ("connessione.h", "Contract of the screen"), ("logica_connessione.c", "Pure logic: what to show in each state"),
        ("logica_connessione.h", "Contract of that logic"), ("demo.c", "Demo of the screen"), ("prova_connessione.c", T + " of the screen's logic")]),
    grp("Shell: fake backend", "src/shell/finto", [
        ("finto.c", "The fake phone: connection, apps, notifications, folders, transfers, widgets"), ("finto.h", "Contract of the fake and of the demo program"), ("interno.h", "Internal parts"),
        ("comandi.c", "The small window of fake-phone commands"), ("flusso_finto.c", "Synthetic video streams"), ("superficie_finta.c", "Fake surface for tests"), ("demo_finto.c", "Demo of the fake backend"),
        ("prova_finto.c", T + " of the fake backend")]),
    grp("Packaging", "pacchetto", [
        ("AppRun", "AppImage launcher: libraries, schemas, loaders"), ("costruisci.sh", "Runs the packaging on the build server"), ("icone-kora.sh", "Packaging step: Kora icons as PNG, sources, GPL-3 text and credits"), ("dentro.sh", "Packaging inside the container: tools, FFmpeg, libtiff, cairo, checks, AppImage"),
        ("yasde.desktop", "Desktop entry used only by " + c("appimagetool")), ("prova_pixbuf.c", "Packaging test: image loaders"), ("prova_tls.c", "Packaging test: certificate authorities with the package's OpenSSL"),
        ("prova_tls_rete.c", "Packaging test: TLS handshake over the network")]),
    grp("Development tools", "strumenti", [
        ("prova.sh", "Builds and tests a piece on the build server"), ("dentro.sh", "The same, inside the container"), ("Containerfile", "Recipe of the build container"), ("prepara-server.sh", "Sets up an empty build server: container image and Android SDK"), ("sdk.sh", "Downloads the Android SDK; run by prepara-server.sh"), ("meson.build", "Builds the tools below"),
        ("yasde-prova.c", "Command line client for the lower pieces"), ("finestra-prova.c", "Bare window for one app (SDL3)"), ("misura-flussi.c", "Measures several video streams in parallel"),
        ("prototipo-gtk.c", "GTK prototype that measured the frame path"), ("demo-sul-tablet.sh", "Opens a piece's demo on the tablet"), ("foto-demo.sh", "Opens a demo on an invisible screen and photographs it"),
        ("app-sul-tablet.sh", "Opens the real program on the tablet"), ("finestra-sul-tablet.sh", "Opens the bare window on the tablet")]),
    grp("Phone: build", "telefono", [("costruisci.sh", "Compiles the component's jar and runs the JVM tests")]),
    grp("Phone: contract", "telefono/contratto/it/yasde", [
        ("Registro.java", "Meeting point between the service and the pieces"), ("GestoreCanale.java", "Handler of a channel"), ("GestoreComandi.java", "Handler of a range of commands"), ("Guardiano.java", "The restore guardian's interface")]),
    grp("Phone: base", "telefono/base", [
        ("it/yasde/Servizio.java", "The service: " + c("app_process") + " entry, socket, channel dispatch"), ("it/yasde/Protocollo.java", "Message types and reading/writing"), ("it/yasde/Smistatore.java", "Dispatch of the command channel"),
        ("it/yasde/Custode.java", "The guardian: a separate shell process that restores the phone"), ("it/yasde/Resti.java", "Leftovers of old sessions: finding and removing"), ("it/yasde/Pari.java", "Who may connect to the service socket"),
        ("it/yasde/Nascoste.java", "Single point for hidden APIs and their startup check"), ("prove.sh", "JVM tests"), ("prove/prova_telefono.py", "Real-phone test")]),
    grp("Phone: video and input", "telefono/video", [
        ("it/yasde/Video.java", "Video channel and commands 0x40–0x4f"), ("it/yasde/Sessione.java", "A session: virtual screen, encoder, the app on it"), ("it/yasde/Pagine.java", "Fixed list of app pages: number to standard action"), ("it/yasde/PaginaTask.java", "Pure part of the page request: which earlier tasks to remove after a page is open"), ("it/yasde/Codificatore.java", "Hardware H.264 encoder and its reader thread"),
        ("it/yasde/Inoltro.java", "Rules of what leaves on the video channel"), ("it/yasde/Input.java", "Input channel"), ("it/yasde/Iniettore.java", "Builds and injects input events"), ("it/yasde/Gesti.java", "State of gestures in progress"),
        ("it/yasde/Pizzico.java", "Geometry and state machine of the pinch"), ("it/yasde/Specchio.java", "Real screen measures for the mirror"), ("it/yasde/ControlloSchermi.java", "Protected-screen watch"),
        ("it/yasde/AnalisiFinestre.java", "Reads the window dump for protected windows"), ("it/yasde/LiberaTask.java", "Close with task removal"), ("it/yasde/Barriera.java", "Barrier on an execution queue"),
        ("it/yasde/Binario.java", "Protocol string reading"), ("it/yasde/NascosteVideo.java", "Hidden APIs of video and input"), ("it/yasde/ProvaVideo.java", "Isolated test of a session"), ("prove.sh", "JVM tests"),
        ("prove/prova_gesti.py", "Real-phone test: gestures"), ("prove/prova_mouse.py", "Real-phone test: right and middle mouse buttons"),("prove/prova_pausa.py", "Real-phone test: pause"), ("prove/prova_pausa_prod.py", "Real-phone test: pause in production"), ("prove/prova_rotella.py", "Real-phone test: wheel"),
        ("prove/prova_schermi.py", "Real-phone test: screens"), ("prove/prova_video.py", "Real-phone test: video"), ("prove/prova_zoom.py", "Real-phone test: zoom")]),
    grp("Phone: audio", "telefono/audio", [
        ("it/yasde/Audio.java", "Audio channel"), ("it/yasde/Cattura.java", "One capture: loopback, Opus"), ("it/yasde/NascosteAudio.java", "Hidden APIs of audio"), ("it/yasde/Volume.java", "Media volume through a shell command"),
        ("prove.sh", "JVM tests"), ("prove/prova_audio.py", "Real-phone test"), ("prove/bin_a_ogg.py", "Converts captured audio packets to Ogg Opus")]),
    grp("Phone: data", "telefono/dati", [
        ("it/yasde/Dati.java", "Commands 0x70–0x7d, 0x90–0x94 and events"), ("it/yasde/CampiDati.java", "Pure part: fields, encoding, limits"), ("it/yasde/CampiTelefonia.java", "Pure part of calls, network and media (0x91–0x94): encoding, string cuts, mapping of Android values"), ("it/yasde/LettoriShell.java", "Pure readers of " + c("dumpsys media_session") + ", " + c("cmd wifi status") + " and " + c("dumpsys telephony.registry") + ": the fallback roads"), ("it/yasde/NascosteTelefonia.java", "What calls, network and media ask Android (read-only), and the three media commands"), ("it/yasde/NascosteDati.java", "What the data layer asks Android"),
        ("it/yasde/AnalisiBatteria.java", "Parses " + c("dumpsys battery")), ("it/yasde/AnalisiNotifiche.java", "Parses notifications from the dump"), ("it/yasde/LetturaUnica.java", "A blocking read on its own thread with a deadline"),
        ("it/yasde/LogicaPosizione.java", "Pure part of the position: recency, rounding to 0.005°"), ("prove.sh", "JVM tests"), ("prove/prova_dati.py", "Real-phone test"), ("prove/prova_posizione.py", "Real-phone test: position"),
        ("prove/prova_widget.py", "Real-phone test: widget data"), ("prove/prova_widget2.py", "Real-phone test: newer widget data"), ("prove/prova_telefonia.py", "Real-phone test: calls, network and media (no valid media command is sent)"), ("prove/conta_verticali.py", "Counts portrait-only apps"),
        ("prove/sonda_riproduzione.sh", "Probe: playback information"), ("prove/sonda_widget.sh", "Probe: widget data")]),
    grp("Phone: archive, clipboard", "telefono", [
        ("archivio/it/yasde/Archivio.java", "Thumbnails and storage"), ("archivio/it/yasde/Cerca.java", "Search channel"), ("archivio/it/yasde/Percorrenza.java", "Breadth-first walk of a folder"),
        ("archivio/it/yasde/ConfrontoNomi.java", "Name comparison for search"), ("archivio/it/yasde/BinarioArchivio.java", "Protocol reading for this piece"), ("archivio/prove.sh", "JVM tests"), ("archivio/prove/prova_archivio.py", "Real-phone test"),
        ("appunti/it/yasde/Appunti.java", "Clipboard channel"), ("appunti/it/yasde/Regole.java", "Pure part: message format, never-pass rules, echo suppression"), ("appunti/it/yasde/NascosteAppunti.java", "Hidden APIs of the clipboard"),
        ("appunti/prove.sh", "JVM tests"), ("appunti/prove/prova_appunti.py", "Real-phone test")]),
    grp("Phone: panel and switches", "telefono", [
        ("pannello/it/yasde/Pannello.java", "Physical panel commands"), ("pannello/it/yasde/TieniSveglio.java", "Keeps the phone awake while the PC is alive"), ("pannello/it/yasde/AssenzaPc.java", "Rule of the PC's absence"),
        ("pannello/it/yasde/InputFisico.java", "Notices a touch or key on the real screen"), ("pannello/it/yasde/EventiEvdev.java", "Reads kernel input events"), ("pannello/it/yasde/Frequenze.java", "Rate chosen with the panel off"),
        ("pannello/it/yasde/ImpostazioniPannello.java", "System settings through a shell command"), ("pannello/it/yasde/ShellPannello.java", "Shell commands with a real deadline"), ("pannello/it/yasde/NascostePannello.java", "Hidden APIs of the panel"),
        ("pannello/prove.sh", "JVM tests"), ("pannello/prove/prova_pannello.py", "Real-phone test"), ("pannello/prove/prova_sveglio.py", "Real-phone test: staying awake"),
        ("controlli/it/yasde/Controlli.java", "Quick switches and find-my-phone commands 0x80–0x8f"), ("controlli/it/yasde/Trova.java", "Find my phone (switched off)"), ("controlli/it/yasde/Telefono.java", "What the switches ask of the phone"),
        ("controlli/it/yasde/TelefonoAndroid.java", "The real phone for the switches"), ("controlli/it/yasde/ComandiShell.java", "Restore lines for the guardian"), ("controlli/it/yasde/ShellControlli.java", "Shell commands with a deadline"),
        ("controlli/it/yasde/AnalisiNonDisturbare.java", "Parses the Do Not Disturb state"), ("controlli/it/yasde/NascosteControlli.java", "The one hidden API of this piece"), ("controlli/prove.sh", "JVM tests"),
        ("controlli/prove/prova_controlli.py", "Real-phone test")]),
]

FIG_TREE = tree([
    "yasde/",
    "├── meson.build  # root build file",
    "├── meson_options.txt  # the pezzo option",
    "├── CLAUDE.md  # project rules (Italian)",
    "├── PIANO.md  # work plan (Italian)",
    "├── data/en/  # English translation tables",
    "├── docs/  # protocol and the manuals' sources",
    "│   └── sources/  # build.py, style, script, technical/ and user/ chapters",
    "├── mockup/  # the interface mockup",
    "├── pacchetto/  # AppImage packaging",
    "├── prove/telefono_finto/  # the fake ADB daemon in Python",
    "├── src/",
    "│   ├── comune/  # errors, buffers, neutral frame",
    "│   ├── adb/  # ADB client",
    "│   ├── scoperta/  # mDNS discovery",
    "│   ├── abbinamento/  # pairing with the code",
    "│   ├── collegamento/  # connection state machine",
    "│   ├── componente/  # PC side of the phone component",
    "│   ├── dati/  # phone data, search, clipboard",
    "│   ├── file/  # copy and listing",
    "│   ├── trasferimenti/  # queue and APK installer",
    "│   ├── video/  # decoding, capture",
    "│   ├── audio/  # Opus and PipeWire",
    "│   ├── finestre/  # window rules",
    "│   ├── config/  # folders, files, language",
    "│   ├── meteo/  # forecasts",
    "│   └── shell/  # GTK 4 interface",
    "│       ├── comune/ ossatura/ finto/  # common piece, skeleton, fake backend",
    "│       └── superficie/ finestra-app/ barra/ launcher/ desktop/ sistema/ trasferimento/ connessione/",
    "├── strumenti/  # build and demo tools",
    "└── telefono/  # the Java component and its tests",
    "    ├── contratto/ base/ video/ audio/ dati/ archivio/ appunti/ pannello/ controlli/",
    "    └── costruisci.sh",
], "«TAB» — The folders of the project")


def _e_prova(f):
    n = f.rsplit("/", 1)[-1]
    return ("/prova/" in f or "/prove/" in f or n.startswith("prova") or n.startswith("demo") or n.startswith("finto")
            or n.endswith("_demo.h") or "finti_sotto" in n or "tabella_finta" in n or "tls_finto" in n or "finto_" in n or n == "aiuti.h"
            or n == "prove.sh" or n.startswith("sonda") or n.startswith("conta_") or n.startswith("bin_a_"))


PARTI = [
    ("Shell, program", "<code>src/shell/</code> (not tests, demos, fake)", "The GTK 4 interface",
     lambda f: f.startswith("src/shell/") and not _e_prova(f) and "/finto/" not in f),
    ("Shell, fake backend, demos and tests", "<code>src/shell/</code>", "Fake phone, demos, logic and screen tests",
     lambda f: f.startswith("src/shell/")),
    ("PC pieces", "<code>src/*/</code>", "ADB, discovery, pairing, connection, component, data, files, transfers, video, audio, windows, configuration, weather",
     lambda f: f.startswith("src/") and not _e_prova(f)),
    ("PC piece tests and fakes", "<code>src/*/</code>", "Tests and fakes of the pieces above", lambda f: f.startswith("src/")),
    ("Phone component", "<code>telefono/*/it/</code>", "Java source of the component", lambda f: f.endswith(".java")),
    ("Phone tests and probes", "<code>telefono/</code>", "Python and shell tests", lambda f: f.startswith("telefono/") and not f.endswith("costruisci.sh")),
    ("Packaging", "<code>pacchetto/</code>", "AppImage scripts and checks", lambda f: f.startswith("pacchetto/")),
    ("Tools and build", "root, <code>strumenti/</code>, <code>telefono/costruisci.sh</code>", "Meson root, build scripts, development tools", lambda f: True),
]

S1 = p("The folders of the project, one piece per folder.", lead=True) + FIG_TREE
S2 = p("Every source file with its line count, recounted each time the manual is generated. The map covers the C sources and headers under " + c("src/") + ", the Java, shell and Python files of the phone component, "
       "the scripts and C tools of " + c("strumenti/") + " and " + c("pacchetto/") + ", the root " + c("meson.build") + " and the manual generator. If a file is missing from the map, or the map lists one that no longer exists, generation stops. "
       "The per-piece " + c("meson.build") + " files, the " + c("COMPITO.md") + " task files, CSS, resource and translation files are not counted.", lead=True) + file_map(GRUPPI, "«TAB» — The project's files")
S3 = p("The same files, grouped by what they are.", lead=True) + numeri(PARTI, "«TAB» — The project in numbers")

CHAPTER = ("Source map", [
    ("The folders", S1),
    ("The project's files", S2),
    ("The project in numbers", S3),
])
