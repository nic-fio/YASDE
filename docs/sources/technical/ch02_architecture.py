from build import arrow, box, c, fig, note, p, rif, table, text, ul, zone

ARCH = fig(
    zone(10, 40, 880, 70, "GUI process (GTK4 shell)", "#eef2f7")
    + box(30, 62, 250, 40, "shell", "src/shell/*: desktop, windows, bar, launcher", "navy", 12)
    + box(300, 62, 260, 40, "ossatura (skeleton)", "real services: sv_*.c", "blue", 12)
    + box(580, 62, 290, 40, "finestre, config, trasferimenti, meteo", "logic around the shell", "light", 12)
    + zone(10, 125, 880, 120, "PC engine (C, no GUI)", "#e8eef7")
    + box(30, 150, 160, 40, "link", "state machine", "blue", 12)
    + box(210, 150, 160, 40, "componente", "protocol, video, input", "blue", 12)
    + box(390, 150, 120, 40, "dati", "apps, state, search", "light", 12)
    + box(530, 150, 120, 40, "audio", "Opus + PipeWire", "light", 12)
    + box(670, 150, 100, 40, "video", "FFmpeg", "light", 12)
    + box(790, 150, 90, 40, "file", "sync:", "light", 12)
    + box(30, 198, 150, 38, "scoperta", "mDNS", "dark", 12)
    + box(200, 198, 170, 38, "abbinamento", "SPAKE2 pairing", "dark", 12)
    + box(390, 198, 490, 38, "adb: TLS client, channels, shell v2", "OpenSSL, one reader thread per connection", "navy", 12)
    + zone(10, 260, 880, 90, "Phone (Java, app_process, uid 2000)", "#f1f5f9")
    + box(30, 284, 200, 50, "base", "service, guardian, registry", "dark", 12)
    + box(250, 284, 200, 50, "video, input", "virtual displays, MediaCodec", "dark", 12)
    + box(470, 284, 200, 50, "audio, dati, archivio", "capture, data, files, clipboard", "dark", 12)
    + box(690, 284, 190, 50, "pannello, controlli", "panel, wake lock, switches", "dark", 12)
    + arrow(450, 112, 450, 148) + arrow(450, 250, 450, 282, "#475569", True),
    900, 360, "«FIG» — The pieces and what sits on top of what (arrows: use; dashed: Wi-Fi, ADB over TLS)")

S1 = p("YASDE is one Linux process on the PC plus one short-lived Java process on the phone. They talk over a single ADB "
       "connection (TCP, TLS 1.3) opened by the PC; every logical channel of the protocol is an ADB channel multiplexed on it.", lead=True) + ARCH + \
    p("The PC side is divided into pieces, one folder each under " + c("src/") + ", and the build (" + c("meson.build") + " at the root, one "
      + c("meson.build") + " per piece defining " + c("<piece>_dep") + ") lets a piece be compiled alone with " + c("-Dpezzo=<name>") +
      ": whatever a piece depends on is then replaced by a fake written inside its own folder. The phone side is one folder per "
      "piece under " + c("telefono/") + " compiled together into " + c("yasde.jar") + ".")

S2 = p("Each PC piece depends only on the ones listed here. The shell is the only code that sees all of them, and it never "
       "uses a lower piece directly: it goes through the service layer described in chapter 11.", lead=True) + \
    table(["Piece", "Folder", "Uses", "What it provides"], [
        ["Common", c("src/comune"), "-", c("PhBuffer"), ", " + c("PhErrore") + ", " + c("PhFotogramma") + " (the frame handed to the shell)"],
        ["ADB client", c("src/adb"), "common, OpenSSL", "RSA key and certificate, TLS connection, channels, shell v2 (" + rif("The ADB client: transport") + ")"],
        ["Discovery", c("src/scoperta"), "common", "mDNS search for connect and pairing services (" + rif("Discovery with mDNS") + ")"],
        ["Pairing", c("src/abbinamento"), "adb (key), common", "pairing with the six-digit code (" + rif("Pairing with the six-digit code") + ")"],
        ["Files", c("src/file"), "adb", "copy and listing through " + c("sync:") + " (" + rif("File transfer with sync") + ")"],
        ["Component", c("src/componente"), "adb, file", "starts the service, command channel, video sessions, input, presence (" + rif("Starting the component on the phone") + ")"],
        ["Data", c("src/dati"), "component", "apps, icons, state, notifications, search, clipboard"],
        ["Video", c("src/video"), "common, FFmpeg, Vulkan", "H.264 decoding, export to " + c("PhFotogramma") + ", PNG and MP4 capture (" + rif("Video: receiving and decoding") + "; the hardware path stage by stage: " + rif("Hardware acceleration end to end") + ")"],
        ["Audio", c("src/audio"), "adb, Opus, PipeWire", "decoding, jitter queue, playback, gain (" + rif("Audio: playback on the PC") + ")"],
        ["Link", c("src/collegamento"), "scoperta, adb, abbinamento, componente, dati, config", "the state machine that keeps one phone connected (" + rif("Link states and timings") + ")"],
        ["Config", c("src/config"), "common", "files in " + c("~/.config/YASDE") + ", language table, phone profile"],
        ["Windows, transfers, weather", c("src/finestre") + ", " + c("src/trasferimenti") + ", " + c("src/meteo"), "common, config, file, adb", "see chapters 12, 13 and 15"],
        ["Shell", c("src/shell"), "all of the above", "GTK4 desktop; " + c("src/shell/ossatura") + " wires the real services"],
    ], "«TAB» — PC pieces and their dependencies") + \
    p("The phone side follows the same idea. " + c("telefono/contratto") + " holds the shared interfaces (" + c("Registro") + ", " +
      c("GestoreCanale") + ", " + c("GestoreComandi") + ", " + c("Guardiano") + "); " + c("telefono/base") + " holds the service; every other folder "
      "(" + c("video") + ", " + c("audio") + ", " + c("dati") + ", " + c("archivio") + ", " + c("appunti") + ", " + c("pannello") + ", " + c("controlli") +
      ") registers itself through " + c("Registro") + " and never names another folder: " + c("base") + " and " + c("video") + " talk only through " + c("Registro") + ".")

S3 = p("The code is thread-based, with no event loop of its own on the PC side. The table lists the long-lived threads and "
       "who owns them.", lead=True) + \
    table(["Thread", "Owner", "Job"], [
        ["ADB reader", c("ph_adb_collega"), "reads every ADB packet from the TLS socket and routes it to its channel; one per connection"],
        ["Link thread", c("ph_col_avvia"), "search, connect, monitor every 500 ms, drop, close; delivers every notice one at a time"],
        ["Component: commands", c("ph_componente_avvia"), "reads the command channel and separates answers from events"],
        ["Component: shell", c("ph_componente_avvia"), "reads and discards the log lines of the service process"],
        ["Component: presence", c("ph_componente_avvia"), "sends " + c("PRESENZA") + " when nothing has been sent for 5 minutes"],
        ["Search, clipboard", c("src/dati"), "one thread per file search and one for the clipboard channel"],
        ["Audio reader, PipeWire", c("src/audio"), "reads and decodes Opus; PipeWire calls the fill function from its own real-time thread"],
        ["Manager " + c("ph-gestore"), c("src/shell/ossatura"), "builds the session when the link is up, tears it down on loss, starts audio after its delay"],
        ["Worker pools", c("src/shell/ossatura"), "seven " + c("GThreadPool") + "s: 3 general workers, and one thread each for state, icons, writing, streams, input, install"],
        ["Video reader", "the shell surface", "calls " + c("sv_flusso_leggi") + " in a loop: receive, decode, export, one thread per window"],
        ["GTK main thread", "GTK", "everything visible; never waits on the network"],
    ], "«TAB» — Threads") + \
    p("The rules that keep this safe are in the contract headers and are checked first by review: a public function that uses an "
      "object must survive a close called from another thread at the same moment (a use counter that close waits for); no "
      "callback runs while holding a lock its receiver might want; no wait is unbounded unless the contract says so; no lock is "
      "held during an external command or a network write. The single known exception is the input channel lock, held during a "
      "write because ADB forbids two writers on one channel.") + \
    note("Objects owned by a session (" + c("PhAdb") + ", " + c("PhComponente") + ", " + c("PhDati") + ") are never used without a loan: " + rif("Loans: using the session objects") + ".")

S4 = p("This is the path of a user action, to fix the picture before the details. A click on a window of a phone app:", lead=True) + \
    ul([
        "GTK delivers the pointer event to the window surface; the shell turns it into a " + c("PhSrvInput") + " in the video's own coordinates and calls " + c("sv_flusso_input") + ".",
        "If the stream is active and the video size matches, the phone panel is switched off if the phone is “in hand”, then the event is queued on the input pool.",
        c("ph_input_tocco") + " writes a " + c("TOCCO") + " message (type " + c("0x50") + ") on the " + c("input") + " channel, with the video size it was computed on.",
        "On the phone, " + c("Input") + " reads it, drops it if the size is stale, scales it to the virtual display and injects a " + c("MotionEvent") + " touchscreen event on that display.",
        "The app draws; the virtual display feeds the encoder; a H.264 packet travels the " + c("video:<id>") + " channel back; the reader thread decodes it with FFmpeg and hands the frame to GTK (as a DMA-BUF texture with VA-API; copied to memory first with the Vulkan via, used only where VA-API is missing).",
    ])

CHAPTER = ("Architecture", [
    ("The two halves", S1),
    ("Pieces and dependencies", S2),
    ("Threads", S3),
    ("Life of a click", S4),
])
