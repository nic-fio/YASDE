from build import c, dl, p, rif


def v(titolo):
    return " See " + rif(titolo) + "."


EN = [
    ("AppImage", "The single-file package of YASDE (" + c("YASDE-2.2-x86_64.AppImage") + ") with the program, the phone component and the libraries." + v("The AppImage")),
    ("AppRun", "The AppImage's launcher script: sets libraries, schemas and image loaders, then starts the program." + v("The AppImage")),
    ("Automatic density", "The rule that gives every app window a stream equal to its physical pixels and a density of 160 times the shell scale." + v("The density rule")),
    ("Backend", "A realization of the services: the real one in the skeleton, or the fake one." + v("The services and their backends")),
    ("Broadway", "GTK's virtual screen (" + c("gtk4-broadwayd") + "), used by the screen tests in the build container." + v("Running the tests")),
    ("Compatibility", "The per-app window switch that gives the app the phone's real density." + v("Compatibility")),
    ("Component", "The Java code that runs on the phone, started with " + c("app_process") + " (chapter 6)."),
    ("Contract", "A header or document that fixes how two pieces talk (for example " + c("servizi.h") + " or the protocol document). A piece does not change a contract by itself." + v("Principles of the project")),
    ("Delivery", "Passing a function to the GTK thread with " + c("ph_shell_consegna") + "." + v("Threads and delivery to the GTK thread")),
    ("Decoding path (via)", "The way an H.264 stream is decoded: " + c("PH_VIA_VAAPI") + ", " + c("PH_VIA_VULKAN") + " or " + c("PH_VIA_SOFTWARE") + "." + v("How the decoding path is chosen and when it falls back")),

    ("DMA-BUF", "A buffer shared with the graphics card, which lets a decoded frame reach the screen without a copy in memory." + v("The video surface")),
    ("DRM PRIME", "The FFmpeg pixel format " + c("AV_PIX_FMT_DRM_PRIME") + " that describes a frame as DMA-BUF file descriptors; a VA-API frame is mapped to it without copying." + v("Hardware acceleration end to end")),

    ("Fake backend", "A simulated phone in memory used by demos and tests." + v("The fake backend")),
    ("Guardian", "The phone-side shell process that restores the phone's settings if the service dies (chapter 6)."),
    ("Host window", "The one real GTK window of the program, which contains the whole shell." + v("The skeleton")),
    ("Inner window", "The window of a phone app drawn inside the host window." + v("The window rules")),
    ("Layer", "One level of the shell's stack: desktop, windows, bar, panels, toasts, drop, veil, host title." + v("Layers, popups and piece handlers")),
    ("Modifier (DMA-BUF)", "A 64-bit code that tells how the planes of a DMA-BUF are laid out in memory (tiling, compression); it travels with the fourcc and the file descriptors from " + c("ph_esporta") + " to GTK." + v("Hardware acceleration end to end")),

    ("Mirror", "The inner window that shows the phone's real screen." + v("The mirror window")),
    ("Pairing", "Authorizing the PC on the phone with the six-digit code (chapter 5)."),
    ("Offload (scan-out)", "Handing a DMA-BUF texture straight to the display hardware instead of drawing it with the renderer, requested with " + c("GtkGraphicsOffload") + "; GTK decides, and it was not seen active on the one machine tried (an Intel GPU, GTK 4.18.6)." + v("Requirements and limits of hardware acceleration")),

    ("Piece", "One folder with its build file, its contract and its tests." + v("Tests of the PC pieces")),
    ("Popup", "A panel, menu or dialog of the shell; only one is open at a time." + v("Layers, popups and piece handlers")),
    ("Pool", "A group of worker threads of the real services with one role." + v("Threads and delivery to the GTK thread")),
    ("Profile", "The per-phone file " + c("telefono-<serial>.toml") + " (desktop, folders, pinned apps…)." + v("The file formats and their keys")),
    ("VA-API", "The Linux interface (libva) to the video decoding hardware of a GPU; the first choice of the decoding order, and the only one that gives the frame to the shell without copies." + v("How the decoding path is chosen and when it falls back")),
    ("Vulkan Video", "The Vulkan extension " + c("VK_KHR_video_decode_h264") + " for decoding on the GPU; the second choice, used only if offered as shipped, and it copies every frame to memory." + v("How the decoding path is chosen and when it falls back")),

    ("Snapshot", "The last state received for something (connection, apps, notifications…), replaced only on the GTK thread." + v("The services and their backends")),
    ("Surface", "The widget that shows one video stream." + v("The video surface")),
    ("Veil", "The full-area cover for the lock and the lost connection." + v("The skeleton")),
    ("Widget (desktop)", "A card on the desktop (clock, weather…), drawn by the desktop piece (not a GTK widget)." + v("Adding a widget")),
]

IT = [
    ("abbinamento", "Pairing: the piece " + c("src/abbinamento/") + " and the code-based pairing."),
    ("avvisi", "Notices: pop-up messages; also the preference " + c("avvisi") + "."),
    ("barra", "The bar at the bottom of the host window (" + c("src/shell/barra/") + ")."),
    ("cartella_invio, cartella_ricezione", "Send folder and receive folder, in the profile."),
    ("cattura", "Capture: screenshot and recording (" + c("src/video/cattura.h") + ")."),
    ("collegamento", "Connection: the state machine that keeps the relationship with one phone (" + c("src/collegamento/") + ")."),
    ("componente", "The component on the phone, and its PC side (" + c("src/componente/") + ")."),
    ("coda", "Queue (transfers; audio)."),
    ("consegna", "Delivery: " + c("ph_shell_consegna") + "."),
    ("custode", "Guardian: the phone-side restore process (" + c("Custode") + ")."),
    ("dati", "Data: " + c("src/dati/") + ", the phone's apps, state, notifications, storage…"),
    ("fissata", "Pinned: an app fixed on the bar (" + c("[[fissata]]") + ")."),
    ("finestra, finestre", "Window, windows (" + c("src/finestre/") + ")."),
    ("finto", "Fake (as in the fake backend, " + c("src/shell/finto/") + ")."),
    ("flusso", "Stream: a video stream, or the stream size of a window."),
    ("immagine", "Image."),
    ("lingua", "Language."),
    ("meteo", "Weather."),
    ("ossatura", "Skeleton: the main window and program piece (" + c("src/shell/ossatura/") + ")."),
    ("pannello", "Panel: the phone's physical screen panel (" + c("telefono/pannello/") + ")."),
    ("pezzo", "Piece: the unit of work, and the build option " + c("pezzo") + "."),
    ("prova, prove", "Test, tests (" + c("prova_*.c") + ", " + c("strumenti/prova.sh") + ")."),
    ("ricezione", "Receive: phone to PC file transfer."),
    ("scoperta", "Discovery: finding phones with mDNS (" + c("src/scoperta/") + ")."),
    ("servizi", "Services: the shell's only door (" + c("src/shell/servizi.h") + ")."),
    ("sfondo", "Wallpaper."),
    ("sveglia", "Alarm clock."),
    ("superficie", "Surface (the video surface)."),
    ("tutto", "All: the value of the build option " + c("pezzo") + " that builds every approved piece."),
    ("veloce", "Fast: the optimized whole build, without checks, for measurements."),
    ("velo", "Veil."),
]

S1 = p("Terms used in the manual, in alphabetical order.", lead=True) + dl([("<b>" + a + "</b>", b) for a, b in EN])
S2 = p("The code is Italian: identifiers, files, configuration keys and comments keep Italian names, and the manual quotes them as they are. These are the recurring ones with their translation.", lead=True) + dl([("<b>" + a + "</b>", b) for a, b in IT])

CHAPTER = ("Glossary", [
    ("English terms", S1),
    ("Italian names in the code", S2),
])
