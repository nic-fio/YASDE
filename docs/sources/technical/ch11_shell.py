from build import arrow, box, c, fig, note, p, rif, table, text, ui, ul, warn, zone

S1 = p("The shell is everything the user sees: the single GTK 4 window, the desktop inside it, and the windows of the "
       "phone's apps drawn as inner windows. It lives entirely under " + c("src/shell/") + ". GTK appears nowhere else in "
       "the project, and the shell never touches the phone, the audio or the disk directly: it goes through one narrow "
       "door, the services (" + rif("The services and their backends") + ").", lead=True) + \
    p("Two Italian words recur in the code. " + c("ossatura") + " is the skeleton: the piece that owns the program, the "
      "main window and the real services. " + c("pezzo") + " (piece) is the unit of work: one piece is one folder, with "
      "its own " + c("meson.build") + ", its own tests and, in the shell, its own style sheet and translation file.") + \
    table(["Piece (test name)", "Folder", "What it does"], [
        [c("shell-ossatura"), c("src/shell/ossatura/"), "The " + c("GtkApplication") + " " + c("it.yasde.YASDE") + ", the main window, the "
         "real services, the lock and connection veil, the PC/phone clipboard link, the ordered shutdown"],
        [c("shell-superficie"), c("src/shell/superficie/"), "The widget that shows one video stream without copies (DMA-BUF) "
         "or from memory"],
        [c("shell-finestra-app"), c("src/shell/finestra-app/"), "The inner windows of the apps: title bar, buttons, input translation, "
         "pause, screenshot and recording, Compatibility"],
        [c("shell-barra"), c("src/shell/barra/"), "The bar at the bottom (48 px)"],
        [c("shell-launcher"), c("src/shell/launcher/"), "The app launcher with search"],
        [c("shell-desktop"), c("src/shell/desktop/"), "Wallpaper, icons, widgets, menus and the two dialogs"],
        [c("shell-trasferimento"), c("src/shell/trasferimento/"), "Send and receive panels, folder navigator, APK confirmation"],
        [c("shell-sistema"), c("src/shell/sistema/"), "Notifications, toasts, phone status, volume"],
        [c("shell-connessione"), c("src/shell/connessione/"), "First connection and pairing screen"],
        [c("shell-finto"), c("src/shell/finto/") + " + " + c("src/shell/comune/"), "The fake backend, and the common piece (delivery, context, "
         "services wrapper, style)"],
    ], "«TAB» — The pieces of the shell. The test name is what " + c("strumenti/prova.sh") + " takes")

LAYERS = fig(
    zone(20, 14, 860, 250, "One " + "GtkApplicationWindow" + " (the host window): the root holds the layers, bottom to top")
    + box(40, 46, 190, 44, "PH_STRATO_DESKTOP", "wallpaper, icons, widgets", "blue")
    + box(250, 46, 190, 44, "PH_STRATO_FINESTRE", "inner app windows", "blue")
    + box(460, 46, 190, 44, "PH_STRATO_BARRA", "the 48 px bar", "blue")
    + box(670, 46, 190, 44, "PH_STRATO_PANNELLI", "popups, launcher, dialogs", "blue")
    + box(40, 110, 190, 44, "PH_STRATO_TOAST", "pop-up notices", "navy")
    + box(250, 110, 190, 44, "PH_STRATO_RILASCIO", "drop-to-send box", "navy")
    + box(460, 110, 190, 44, "PH_STRATO_VELO", "veil / first connection", "navy")
    + box(670, 110, 190, 44, "PH_STRATO_TITOLO_HOST", "6 px strip, full screen", "navy")
    + text(450, 186, "Each layer is a full-area overlay: a child with a non-FILL alignment takes only its own size,", 11.5)
    + text(450, 204, "and the rest of the area stays clickable for the layers below.", 11.5)
    + text(450, 232, "Order of installation: desktop, windows, system, launcher, transfer, bar (the bar is last).", 11.5),
    900, 280, "«FIG» — The layers of the shell, from the bottom up", "Structure of the shell")

S2 = p("The root of the shell (" + c("ph_shell_radice") + ") is a stack of layers (" + c("PhShellStrato") + " in "
       + c("src/shell/shell.h") + "). Inside a layer the last child added is on top. For absolute positions (windows, "
       "icons, context menus) a piece uses " + c("ph_shell_libero_nuovo()") + ", a container that lets clicks through "
       "wherever it has no child.", lead=True) + LAYERS + \
    p("Each piece offers a single entry point, " + c("ph_<piece>_installa(PhShell *)") + ", declared in " + c("src/shell/pezzi.h") +
      ". It is called once, from the GTK thread, after " + c("ph_shell_nuova") + ": it builds the widgets, adds them to the "
      "layers, loads the piece's CSS, registers the handlers it serves and starts listening to the services. "
      + c("ph_ossatura_installa_pezzi") + " (in " + c("src/shell/ossatura/ossatura.c") + ") installs only the pieces present in "
      "the build (the header generated at configuration time, with one flag per piece).") + \
    p("Pieces serve each other through " + c("PhShell") + " handlers, never by including each other's headers: windows "
      "(open an app, list the windows, bar click, mirror), desktop (icons), notices (" + c("ph_shell_avviso") + "), launcher, transfer, "
      "system (the three bar buttons and the hidden-notification set) and pinned apps. Events travel the other way with "
      + c("ph_shell_ascolta") + ": " + c("PH_SHELL_EV_FINESTRE") + ", " + c("PH_SHELL_EV_POPUP") + ", " + c("PH_SHELL_EV_HOST") + ", "
      + c("PH_SHELL_EV_AREA") + ", " + c("PH_SHELL_EV_DESKTOP") + ", " + c("PH_SHELL_EV_NASCOSTE") + ", " + c("PH_SHELL_EV_FISSATE") + ".") + \
    p("Popups follow one rule: only one is open at a time (" + c("PhShellPopup") + "). Opening one closes the open one; "
            "a transparent veil under the popups, bar included, turns the first click outside into a close that does not "
            "reach what is below. Escape closes the open popup; with none open it goes to the active inner window, which "
            "translates it to the phone's Back when the preference " + c("esc_indietro") + " is on (the default). F11 toggles "
            "full screen and never reaches the phone.")

S3 = p("The shell is one GTK thread plus worker threads. The rule is written at the top of " + c("src/shell/servizi.h")
       + " and in " + c("src/shell/CONTRATTI.md") + ", and every piece obeys it.", lead=True) + ul([
    "<b>One GTK thread.</b> All widgets, " + c("PhShell") + " and every " + c("ph_srv_*") + " call live there.",
    "<b>The GTK thread never blocks:</b> no network, no large file reads, no joins of threads that may hang. Requests "
    "to the phone start on backend threads and return with a callback.",
    "<b>The only road from another thread to the GTK thread is " + c("ph_shell_consegna(fn, dati, libera)") + ".</b> It is "
    "always asynchronous, even when called from the GTK thread (the function runs in a later iteration of the main loop, "
    "never inside the call), it keeps the order of deliveries from one thread, and it frees the data afterwards. "
    "Scattered " + c("g_idle_add") + " or " + c("g_main_context_invoke") + " calls are forbidden in pieces.",
    "<b>Every callback and every event arrives on the GTK thread.</b> A " + c("GCancellable") + " cancelled from the GTK "
    "thread guarantees that the callback will not arrive any more, even if the result is already queued.",
    "<b>Snapshots.</b> State (connection, phone, apps, notifications, transfers, volume) is read with calls that return the "
    "last snapshot. A returned pointer is valid until the GTK thread returns to the main loop; anything needed later "
    "is copied.",
    "<b>Video:</b> one reader thread per stream, owned by the surface, calls " + c("ph_srv_flusso_leggi") + " (reception and "
    "decoding happen there). Textures are built on the GTK thread.",
])

S3b = table(["Pool (in " + c("struct Sv") + ")", "Threads", "Work"], [
    [c("p_lavori"), "3", "Folder listings, searches, thumbnails, file reads"],
    [c("p_stato"), "1", "Apps, notifications, panel (in order)"],
    [c("p_icone"), "1", "App icons, one at a time"],
    [c("p_scrittura"), "1", "Saving profile and preferences (in order)"],
    [c("p_flussi"), "1", "Opening, pausing, resizing and closing video streams"],
    [c("p_input"), "1", "Input toward the phone (in order)"],
    [c("p_installa"), "1", "APK installation and uninstallation, one at a time"],
], "«TAB» — The worker pools of the real services (" + c("src/shell/ossatura/servizi_veri.c") + ", " + c("src/shell/ossatura/sv_interno.h") + ")") + \
    p("Besides the pools the real services run a manager thread (" + c("ph-gestore") + "): it is the only one that takes a "
      "borrowed session from the connection (chapter 3); pool threads only \"use\" the session through a counter, and the manager "
      "waits for the counter before it hands the session back.")

FLOW = fig(
    box(20, 50, 150, 54, "Worker thread", "pool or stream reader", "dark")
    + box(210, 50, 160, 54, "ph_servizi_pubblica_*", "backend side, any thread", "blue")
    + box(410, 50, 170, 54, "ph_shell_consegna", "idle source, default priority", "navy")
    + box(620, 50, 130, 54, "GTK thread", "swap the snapshot", "blue")
    + box(780, 50, 100, 54, "Listeners", "ph_srv_ascolta", "green")
    + arrow(172, 77, 208, 77) + arrow(372, 77, 408, 77) + arrow(582, 77, 618, 77) + arrow(752, 77, 778, 77)
    + text(450, 140, "The snapshot is replaced only on the GTK thread; listeners then re-read it.", 12)
    + text(450, 162, "A request goes the other way: ph_srv_* on the GTK thread, queued into a pool, result back through the same road.", 12),
    900, 180, "«FIG» — How a result goes from a worker thread to the GTK thread")

S4 = p("Every piece reaches the phone, the audio, the transfers and the persistence only through " + c("PhServizi") + " ("
       + c("src/shell/servizi.h") + "). Two implementations stand behind it: the real one, written by the skeleton on top of "
       "the lower pieces, and the fake one (" + rif("The fake backend") + "), so any piece can be run and photographed without "
       "a phone.", lead=True) + FLOW + \
    p("The common piece (" + c("src/shell/comune/servizi.c") + ") wraps a backend table (" + c("PhServiziBackend") + " in "
      + c("src/shell/servizi_backend.h") + ") and enforces the thread rules for both: events on the GTK thread, snapshots, "
      "the icon cache, cancellation, deferred saving of the profile and the life of video streams. App icons (" + c("ph_srv_icona") + ", GTK thread only): "
      "if the package has a row in the Kora table (" + c("tabella.txt") + ", lines " + c("<package> <kora-name>") + ") and the file " + c("<kora-name>.png") + " loads, that texture is returned and the phone is not asked at all "
      "(a PNG of another size is accepted, up to 1024 px a side and 2 MiB: whoever shows it scales it); otherwise the phone's icon, or none yet (the request starts by itself and " + c("PH_SRV_EV_ICONA") + " arrives), and the initials square if it never comes. "
      "The folder is the one in the environment variable " + c("YASDE_ICONE_KORA") + " if set and not empty, else " + c("$APPDIR/usr/share/yasde/icone-kora") + " (inside the AppImage), else none. The table is read once, at first use "
      "(at most 256 KiB and 2000 lines; malformed lines are skipped; package up to 127 characters, name up to 95 and valid; a package twice: the first wins); each PNG is loaded at first use and kept; a PNG that fails to load is not tried again. "
      "Missing or damaged files mean the phone's icon, with no error on screen; only a missing or unreadable table writes one decision line in the log. An app that changes its icon on the phone keeps showing the Kora one. A backend call is "
      "answered with exactly one final " + c("ph_servizi_lavoro_*") + " call per job (searches: zero or more "
      + c("ph_servizi_lavoro_trovati") + " and one " + c("ph_servizi_lavoro_cerca_fine") + "). After the final call the job "
      "is not touched again.") + \
    table(["Event (" + c("PhSrvEvento") + ")", "Meaning"], [
        [c("PH_SRV_EV_COLLEGAMENTO"), "Connection state, including the lock"],
        [c("PH_SRV_EV_TELEFONO"), "Battery, network, physical panel"],
        [c("PH_SRV_EV_APP") + ", " + c("PH_SRV_EV_ICONA"), "App list; an icon arrived (argument: package)"],
        [c("PH_SRV_EV_NOTIFICHE"), "Notifications changed"],
        [c("PH_SRV_EV_TRASFERIMENTI"), "Transfer queue changed"],
        [c("PH_SRV_EV_VOLUME") + ", " + c("PH_SRV_EV_APPUNTI"), "Volume; text arrived from the phone's clipboard"],
        [c("PH_SRV_EV_PROFILO"), "The profile of another phone was loaded: re-read it"],
        [c("PH_SRV_EV_CONTROLLI") + ", " + c("PH_SRV_EV_METEO"), "Quick switches; weather forecast"],
    ], "«TAB» — Events of the services") + \
    p("Strings given to the services are copied before the call returns. Strings coming from the phone are already safe to "
      "show (valid UTF-8, no control characters); the raw " + c("percorso") + " and " + c("nome") + " fields only serve to "
      "navigate and request, and only the " + c("_schermo") + " versions are displayed. Structures of the contracts are "
      "always initialized by field name, never by position, because contracts grow by adding fields at the end.") + \
    note("the shell never receives the phone's coordinates, serial number, addresses or the mDNS instance names; "
         "it gets only forecasts and, if the phone knows it, the name of the place.", "Privacy at the door.")

S5 = p("The skeleton is " + c("src/shell/ossatura/") + ". " + c("main.c") + " only calls " + c("ph_ossatura_esegui") + ".",
       lead=True) + ul([
    "<b>Before any thread:</b> "
    "preferences (" + c("ph_cfg_preferenze_carica") + "; result -1 means defaults and never save), language "
    "(" + c("ph_lingua_inizializza") + "), and the font scale: with " + c("scala_automatica") + " off, the value is applied "
    "through " + c("GDK_DPI_SCALE") + " before " + c("gtk_init") + ", because GTK 4 has no fractional widget scale.",
    "<b>Single instance:</b> the application id is " + c("it.yasde.YASDE") + "; a second launch brings the existing window to "
    "the front (" + c("activate") + ") and opens no second connection.",
    "<b>At startup</b> (main instance only): handlers for " + c("SIGTERM") + " and " + c("SIGINT") + " that call "
    + c("g_application_quit") + ", removal of stale temporary files (" + c("ph_cfg_pulisci_temporanei(3600)") + ") and of old "
    "wallpaper images (" + c("ph_cfg_pulisci_sfondi") + ").",
    "<b>On activation:</b> real services (" + c("ph_ossatura_servizi_veri") + "), " + c("ph_shell_nuova") + ", the host "
    "window (" + c("ph_finestra_nuova") + "), the pieces, and the clipboard link (" + c("ph_appunti_installa") + ").",
    "<b>The host window</b> is a resizable " + c("GtkApplicationWindow") + " titled " + ui("YASDE") + " with a header bar that carries the full-screen button, default size 980×584, "
    "minimum 640×420, with the shell root as its content. F11 is caught on the window before any inner window sees it. "
    "In full screen a 6 px strip at the top (" + c("PH_STRATO_TITOLO_HOST") + ") expands to a title row on hover.",
    "<b>Host visibility:</b> " + c("ph_host_visibile_da_stato") + " reads the toplevel state: minimized or suspended means "
    "not visible. The skeleton tells the shell with " + c("ph_shell_imposta_host") + " and every video stream goes to pause.",
]) + p("What covers the shell is decided by " + c("ph_ossatura_vista") + " (in " + c("src/shell/ossatura/logica.c") + "): "
       "the first-connection screen wins when " + c("ph_shell_connessione_da_mostrare") + " says so, otherwise the veil, "
       "otherwise nothing. Never both.") + \
    table(["Connection state", "Shown", "Content"], [
        [c("PH_SRV_NON_ABBINATO"), "Connection screen, always", "Pairing code (" + rif("The pieces of the shell") + ")"],
        [c("PH_SRV_CERCO") + ", " + c("PH_SRV_PERSO"), "Connection screen only if no phone was ever connected in this run; "
         "otherwise the veil", "Veil: title and the connection's own reason; " + ui("Reconnect now") + " when the connection is lost"],
        [c("PH_SRV_BLOCCATO"), "Veil", "Lock icon, " + ui("Phone locked") + " and a fixed sentence"],
        [c("PH_SRV_COLLEGATO") + ", " + c("PH_SRV_CHIUSO"), "Nothing", "—"],
    ], "«TAB» — What covers the shell, by connection state") + \
    p("A phone whose screen was turned off by the system or by the power button (" + c("schermo_spento") + ") gets the lock veil too, "
      "and its streams stop.") + \
    p("<b>Ordered shutdown</b> (" + c("ph_ossatura_chiudi") + ", used by the program and by " + c("demo-ossatura") + " so the demo "
      "tests the real sequence): (1) the window is hidden and destroyed; (2) " + c("ph_shell_libera") + " destroys the root itself, "
      "so the " + c("destroy") + " handlers of the pieces run once, with " + c("PhShell") + " and the services still alive, and "
      "streams are closed; (3) only then " + c("ph_servizi_libera") + " closes the connection, and the guardians restore the phone. "
      "After each step the pending main-loop work is drained. A second defence, the registry of live objects in "
      + c("src/shell/comune/ascoltatori.c") + ", makes " + c("ph_srv_smetti") + " and " + c("ph_shell_smetti") + " harmless after "
      "release. The rule for pieces: in the root's " + c("destroy") + " stop listening with all the " + c("*_smetti") + " "
      "functions and keep your own reference to the widgets as long as listeners exist.")

S6 = p("Short descriptions of the pieces; the real behavior is in each folder's code and in its " + c("COMPITO.md") + ".", lead=True) + \
    table(["Piece", "Behavior verified in the code"], [
        ["<b>Bar</b>", "48 px at the bottom. Left to right: launcher button, separator, one entry per open inner window (icon or initials "
         "placeholder, indicator under it: active, open, minimized), then the system area: send, receive, volume, phone status, "
         "notifications (the last three are created by the system piece and only placed here). Clicking an entry calls "
         + c("ph_shell_clic_barra") + ". A right click opens the bar menu (" + c("PH_POPUP_MENU_BARRA") + "): close the app, pin or unpin. "
         "Pinned apps are per phone, at most " + c("PH_CFG_MAX_FISSATE") + " (16) and stay on the bar when closed. The clock opens the calendar popup."],
        ["<b>Launcher</b>", "Grid of the phone's apps with a search field (a case-insensitive match; Enter opens the first result). The panel is centered "
         "horizontally, 60% of the area wide (at least 480, at most 960 px; if the area is narrower than 528 px it stays 24 px from the edges), its top at 6% of the "
         "height (at least 24 px) and its height that of the content, up to 76% of the height above the bar; the search field stays at the top and the grid scrolls "
         "vertically. Icons are 60 px, columns 112 px wide (as many as fit the inner width), 28 px between rows; a click outside the panel closes it (the shell's popup veil). "
         "The measures are in " + c("PH_LAUNCHER_*") + " and " + c("ph_launcher_misure") + " (" + c("src/shell/launcher/logica.h") + "), applied by " + c("misure_aggiorna") + " in " + c("launcher.c") + "; "
         "this replaces the measures of the specification. Known limit: when the window is resized with the launcher open, the old measures hold for one frame. Click opens, "
         "drag starts a drag to the desktop (the launcher closes when the drag starts), right click offers open and add to or "
         "remove from the desktop, plus pin."],
        ["<b>Desktop</b>", "Wallpaper (solid, gradient or an image taken from the phone and saved in the cache), icon grid with cells of 92×96 "
         "starting at 16,16, and widgets. The catalog in " + c("src/shell/desktop/widget.c") + " has Clock, Calendar, Phone status, "
         "Recent notifications, Phone storage, Weather, Month calendar, Next alarm, Battery in detail, Quick switches, Frequent apps and "
         "Recent transfers; each card has a shape (tile with a corner radius of 28 px, circle or capsule, " + c("ph_dsk_forma") + ") and a free size set from a handle at its bottom right corner, in steps of " + c("PH_DSK_PASSO") + " (8 px), between a minimum and a maximum of the type (" + c("ph_dsk_misura_limiti") + "). "
         "Find-my-phone is compiled out (" + c("PH_DSK_TROVA_OFFERTO") + " is 0), and the types "
         + c("note") + " and " + c("riproduzione") + " exist in the profile format but are not in the catalog."],
        ["<b>Inner window</b>", "Title bar of 50 px with Back on the left, then the name, and on the right the REC marker (just before screenshot), screenshot, "
         "record, more (⋮), minimize, maximize, close, as round buttons of 30 px that show only their symbol until the pointer passes over them "
         "(Close turns coral); in very narrow windows (such as the minimum, 288 px; the room for the name is the width minus 270 px) the name has no room and is not shown. Light frame, corner radius 24 px (0 when maximized or snapped, with no shadow and no border); border and highlight line are drawn by a "
         "child (" + c("ph-finestra-app-filo") + ") drawn over the whole frame without moving the other children, so the children stay where " + c("finestre.h") + " puts them. "
         "A window that is not active keeps the same fill, with name and symbols dimmed and a lighter shadow. "
         "<b>Glass.</b> GTK has no backdrop filter, so behind the title bar only (50 px) the frame, in its own snapshot, redraws what lies under the window, "
         "blurred (Gaussian, deviation 26 px) and more saturated (1.6): the widgets drawn before it, that is the desktop layer and the windows below. "
         "Each is taken through a " + c("GtkWidgetPaintable") + ", which reuses the drawing already made and signals when it changes; for the other windows the "
         "paintables of their children are used, not the frame's, so blurs are not nested. On top comes the CSS background of the title, a white veil at 58% "
         "(42% when the window is not active). The rest of the window is covered by the opaque content and is not blurred. The mode is decided when the frame is realized, by the one rule of the whole shell (" + c("ph_shell_vetro_decidi") + ", see Glass surfaces below); "
         "if it is off, or the frame is not inside the shell's root, the frame keeps its almost opaque fill (94%) and the title bar is plain. The automatic tests of this glass are " + c("shell-finestra-app-vetro") + " and " + c("shell-finestra-app-vetro-menu") + ". Colours, radius and shadows are the "
         + c("--ph-vetro-*") + " variables of " + c("src/shell/ossatura/base.css") + ", used by " + c("finestra-app.css") + "; resize handle of 18 px; shortcuts Ctrl+R rotate, Ctrl+W close app, "
         "Ctrl+Shift+C copy screenshot. Rules for geometry and density live in " + c("src/finestre/") + " (chapter 12)."],
        ["<b>Video surface</b>", "See the next section."],
        ["<b>System</b>", "Notification panel (hiding applies only inside the shell; an updated notification becomes visible again), toasts "
         "(5.2 s for a phone notification, 3.2 s for an information notice), a desktop notice through " + c("GNotification") + " when the "
         "host window is not visible, the phone popup with the mirror switch and the physical panel switch, and the volume popup."],
        ["<b>Transfer</b>", "Send and receive panels with queues, the folder navigator and the APK confirmation (chapter 13)."],
        ["<b>Connection</b>", "Card over the default gradient: instructions and a search spinner when no phone was ever paired; "
         "a code field and a pair button when the phone does not know the PC, an expander for typing an address and port by hand, and a " + ui("Search again") + " button. The code is read at click, passed to " + c("ph_srv_abbina")
         + ", and the field is emptied after every attempt. Pairing code, addresses, ports and serial numbers never reach disk or logs."],
    ], "«TAB» — The pieces of the shell, in short")

S7 = p("Each video stream is shown by one " + c("GtkPicture") + " inside a " + c("GtkGraphicsOffload") + " with a black background "
       "(" + c("src/shell/superficie/superficie.c") + "). The surface owns its " + c("PhSrvFlusso") + ", opens it, and runs one reader "
       "thread that loops on " + c("ph_srv_flusso_leggi") + " with a 100 ms timeout.", lead=True) + ul([
    "<b>Hand-over.</b> The reader keeps at most one frame waiting: a new frame replaces the old one and releases it, and at most "
    "one " + c("ph_shell_consegna") + " is in flight. Frames are dropped, never resolution (" + c("ph_sup_scambio_metti") + ").",
    "<b>Without copies.</b> A " + c("PH_FOTO_DMABUF") + " frame becomes a " + c("GdkDmabufTextureBuilder") + " texture (fd, offsets, strides, "
    "modifier; for YUV formats the color state from " + c("PhFotoColore") + ", BT.709 or BT.601, limited or full range). A "
    + c("PH_FOTO_MEMORIA") + " frame becomes " + c("gdk_memory_texture_new") + " over a " + c("GBytes") + " that wraps the data. "
    "Frames are checked first (sizes 1 to 8192, 1 to " + c("PH_FOTO_PIANI_MAX") + " planes, valid descriptors): an invalid one is released and discarded.",
    "<b>Fallbacks are recorded.</b> If GTK refuses a DMA-BUF, the stream goes to memory and a line is written with "
    + c("ph_shell_decisione") + ": " + c("[decisione] HH:MM:SS.mmm <what> - causa: <cause>") + " on stderr, never with personal data.",
    "<b>Under load.</b> The surface of the foreground window never gives up by itself; the others show one frame in two, then one in four, "
    "until the queue is empty (" + c("ph_superficie_primo_piano") + "). Resolution is never lowered.",
    "<b>States shown by the surface itself:</b> the protected-screen message, and, when the connection is lost, the last image blurred "
    "with " + ui("Reconnect now") + " and " + ui("Close") + ".",
    "<b>Closing:</b> " + c("ph_srv_flusso_interrompi") + ", wait for the reader thread (at most the read timeout), then "
    + c("ph_srv_flusso_chiudi") + " on the GTK thread; idempotent, and no callback arrives afterwards.",
    "<b>Counters:</b> with the environment variable " + c("PH_SUPERFICIE_STAT") + " set to 1, one line every 5 s on stderr per stream.",
]) + warn("the GTK 4.18 test on one machine never printed an \"Attaching … GdkDmabufTexture\" message, so the last step, "
          "direct scan-out of the frames, is not confirmed (see " + rif("The known problems at a glance") + ").", "Not confirmed.")

S8 = p("Each piece has its CSS file, compiled into the executable as a GResource at " + c("/it/yasde/shell/<piece>.css") + " and "
       "loaded by the piece's own " + c("_installa") + " with " + c("ph_shell_stile_carica") + ". The base style, "
       + c("src/shell/ossatura/base.css") + " (compiled by the common piece as " + c("base.css") + "), is loaded by "
       + c("ph_shell_nuova") + " and holds the " + c("--ph-*") + " color variables and the shared classes (" + c("ph-pannello") + ", "
       + c("ph-dialogo") + ", " + c("ph-menu") + ", " + c("ph-tb") + ", " + c("ph-riga") + ", " + c("ph-pbtn") + ", " + c("ph-ghost") + ", "
       + c("ph-accento") + ", " + c("ph-pillola") + ", " + c("ph-campo") + ", …).", lead=True) + ul([
    "A piece uses only classes prefixed " + c("ph-<piece>-") + ". It is GTK CSS, not web CSS: no " + c("gap") + ", "
    + c("max-width") + " or " + c("position") + "; sizes come from " + c("min-width") + ", margins or code.",
    "No " + c("GtkStyleContext") + " per widget and no inline CSS, except computed colors (the user's wallpaper color, placeholder colors).",
    "Icons are the mockup's SVG paths drawn in the text color (" + c("ph_shell_icona_nuova") + ", " + c("PhShellIcona") + "), with round line caps and joins unless the field " + c("spigoli_vivi") + " is true (butt caps and mitered joins, " + c("GSK_LINE_CAP_BUTT") + " / " + c("GSK_LINE_JOIN_MITER") + "); it is true only for the seven title symbols of the inner windows, which follow " + c("docs/DESIGN_FINESTRE.md") + " and not the mockup (checked by " + c("prova_comune.c") + "); missing app icons "
    "become a colored square with initials (" + c("ph_shell_segnaposto_nuovo") + ").",
    "Every piece's logic test loads its CSS (" + c("ph_shell_stile_controlla") + ") and fails on any syntax error.",
    "The look is glass, not the mockup's dark colours: the mockup stays the reference for the layout only. Bar, launcher, panels, menus, dialogs and widgets are glass surfaces (next point); "
    "the host window and system dialogs follow the system theme.",
    "<b>Glass surfaces.</b> A piece registers a widget with " + c("ph_shell_vetro_registra(sh, w, radius, type)") + " (" + c("src/shell/shell.h") + ", section “Vetro”; implementation in " + c("src/shell/comune/vetro.c") + "). "
    "The shell gives it the CSS class " + c("ph-vetro-chiaro") + " (light) or " + c("ph-vetro-scuro") + " (dark); the classes in " + c("base.css") + " give the veil, the edge (light: border and long shadow; dark: a thin light line on top) and the text colours. When the graphics card draws,  the graphics card draws, the layered root draws behind it, "
    "inside its rounded rectangle (the same radius as its CSS), what lies in the layers below it, blurred and more saturated through " + c("GtkWidgetPaintable") + " (light: deviation 26 px, saturation 1.6; dark: 20 px, 1.4). "
    "The rule is one for the whole shell (" + c("ph_shell_vetro_per_renderer") + "): blur only with " + c("GskGLRenderer") + ", " + c("GskNglRenderer") + " or " + c("GskVulkanRenderer") + "; the environment variable " + c("YASDE_SFOCATURA") + " (0 or 1) overrides it (" + c("ph_shell_vetro_decidi") + "). "
    "When blur is on the root has the class " + c("ph-vetro-attivo") + "; without a graphics card the surfaces have an almost opaque fill, never a translucent veil without blur. "
    "Blurs are never nested: a registered surface is drawn whole in the snapshot of whoever is above it (without its own blur, with its veil, border and shadow), while a widget that draws its own blur (the frame of an inner window, marked with " + c("ph_shell_vetro_proprio") + ") is drawn from its children. "
    "Known limits (the first and the last are written in the code, the connection card one follows from the mechanism): behind a surface only what lies in the layers below it is seen, not what lies under it in the same layer (an icon under a desktop widget stays sharp; behind the connection card the blur is covered by the screen's own background); the CSS opacity of a surface does not apply to its blur.",
    "<b>Which surfaces are glass.</b> Dark: the bar and the desktop widgets (the cards register with " + c("PH_VETRO_SCURO") + " and a radius that follows their shape and size). Light: the launcher and its menu, the system popups, the notice (toast), the transfer panels, the menus and dialogs of the desktop, the calendar and the menu of the bar, the ⋮ menu of the inner window, the connection card. "
    "Not glass: the lock veil, the “Drop to send” frame (it stays a dark veil), the icons and names on the desktop, the PC window.",
    "<b>Theme.</b> Inside a light surface the " + c("--ph-*") + " variables change value: text " + c("#10202E") + ", " + c("#2F4558") + ", " + c("#3A4D5F") + " (three levels), accent " + c("#1F7A8C") + " (only as a background), and new variables for text that must stay readable on light: "
    + c("--ph-accento-testo") + " (accent as text), " + c("--ph-errore") + ", " + c("--ph-avviso") + ", plus focus, field text, ghost border and progress track; the nearest surface wins, so a dark one inside a light one restores the dark values. The full list is in " + c("base.css") + " and in " + c("src/shell/CONTRATTI.md") + ", section 6. "
    "Rule for the pieces: no hand-written content colour in their CSS, only " + c("--ph-*") + " variables; a colour that is data (an icon, a category) stays with a same-line comment " + c("colore-dato: reason") + ", and every piece's test checks it with " + c("src/shell/prova_css.h") + ". "
    "A piece never sets background, border or shadow on a registered widget.",
    "<b>Glass tests.</b> " + c("shell-comune-vetro") + " (the registry and the rule), " + c("shell-finestra-app-vetro") + " and " + c("shell-finestra-app-vetro-menu") + " (the title bar and the ⋮ menu), " + c("shell-desktop-vetro") + " (widgets, menu, dialogs), and each piece's own tests check that its surfaces carry the right class.",
    "<b>Cost of the blur.</b> Measured on 24 Nov 2026 on the development tablet, fake phone, four video windows at 60 frames/s, Vulkan renderer; median / 95th percentile of the draw time: without blur 5.1 / 7.2 ms; blur on the windows only 7.1 / 10.0 ms; with the launcher open above the windows 8.5 / 12.8 ms. No frame lost; CPU time of the program about +50%.",
    "<b>Readability limit.</b> With the 58% veil over a very dark or very bright wallpaper the secondary levels of text lose contrast (only the main text stays full), and the dark bar over a white background turns grey.",
    "Accessibility: everything clickable is a real control reachable with Tab, with a visible focus ring; labels go through "
    + c("ph_shell_etichetta") + ", " + c("ph_shell_espanso") + " and " + c("ph_shell_premuto") + ".",
]) + p("The renderer is not forced by the program: GTK chooses it. The development scripts that open demos on the tablet export "
       + c("GSK_RENDERER=vulkan") + " unless the variable is already set.")

S9 = p("The fake backend (" + c("src/shell/finto/") + ", " + c("finto" ) + " means fake) is a simulated phone in memory, used by every "
       "demo and by the piece tests. Nothing is written to disk and no real file is sent.", lead=True) + ul([
    "It simulates the connection (searching, then connected after 300 ms; lock, drop and return on command), a list of apps with "
    "generated icons, notifications, battery, a folder tree with thumbnails, transfers that advance every 300 ms, synthetic video "
    "streams at 60 frames per second (in memory, or in DMA-BUF with the " + c("dmabuf") + " option), volume, clipboard, profile and "
    "preferences, APK installation, screenshot (a real PNG) and simulated recording, and the data of the new widgets.",
    "It also simulates a first connection: " + c("PhFintoInizio") + " can start the phone as connected, as not paired (fake codes: "
    "123456 is right, 000000 means the code screen is not open on the phone, anything else is wrong) or as searching.",
    "Every command received (input, panel, …) is written on stderr with the prefix " + c("finto:") + ".",
    "Each piece has a demo, " + c("demo-<piece>") + ", built by its " + c("meson.build") + ". " + c("strumenti/demo-sul-tablet.sh") + " opens it "
    "on the test tablet; " + c("strumenti/foto-demo.sh") + " opens it on an invisible screen and saves PNG photographs, which "
    "is how a piece's look is compared with the mockup (" + rif("Tests of the PC pieces") + ").",
    c("demo-ossatura") + " is the whole shell with the pieces present, over the fake backend.",
])

CHAPTER = ("The shell", [
    ("Overview and pieces", S1),
    ("Layers, popups and piece handlers", S2),
    ("Threads and delivery to the GTK thread", S3 + S3b),
    ("The services and their backends", S4),
    ("The skeleton", S5),
    ("The pieces of the shell", S6),
    ("The video surface", S7),
    ("Style, resources and accessibility", S8),
    ("The fake backend", S9),
])
