from build import arrow, box, c, fig, flow, note, p, path, rif, seq, steps, table, text, ul, warn

AVVIO = flow([
    ("AppRun", "paths, loaders", "dark"),
    ("main", "ph_ossatura_esegui", "blue"),
    ("GtkApplication", "it.yasde.YASDE", "blue"),
    ("services", "ph_col_avvia", "light"),
    ("window", "desktop appears", "navy"),
], "«FIG» — From the AppImage to the window")

S1 = p("The AppImage runs " + c("pacchetto/AppRun") + ", a POSIX shell script that prepares the environment and then replaces itself "
       "(" + c("exec") + ") with " + c("usr/bin/yasde") + ". The program is " + c("src/shell/ossatura/main.c") + ", which only calls " + c("ph_ossatura_esegui") + ".", lead=True) + AVVIO + \
    steps([
        c("AppRun") + " takes " + c("APPDIR") + " from its own location (never from the environment, which could point to another package), "
        "puts the package libraries first in " + c("LD_LIBRARY_PATH") + ", adds the package schemas to " + c("XDG_DATA_DIRS") + ", and points " +
        c("GIO_MODULE_DIR") + " to an empty folder so that no system GIO module of another GLib version is loaded.",
        "The image-loader cache for gdk-pixbuf is generated from a template with the mount point of the package (" + c("~/.cache/YASDE/yasde-pixbuf-<pid>.cache") +
        "); files of dead processes (or older than a day) are removed. If the folder is not writable or the path has unsafe characters the program starts without these loaders (WebP does not open; PNG and JPEG stay).",
        c("ph_ossatura_esegui") + " first loads the preferences; an unreadable file is set aside and defaults are used.",
        c("ph_lingua_inizializza") + " always selects English: the " + c("lingua") + " preference is read and ignored.",
        "If the user chose a fixed shell scale, " + c("GDK_DPI_SCALE") + " is set from it before GTK starts (GTK4 has no fractional widget scale; it is applied to fonts).",
        "The " + c("GtkApplication") + " has the id " + c("it.yasde.YASDE") + " and is unique per session. In " + c("startup") + " (primary instance only) SIGTERM and SIGINT are routed to " + c("g_application_quit") + ", temporary files older than 3600 s are removed and orphan background images are cleaned.",
        "In " + c("activate") + " the services are built (" + c("ph_ossatura_servizi_veri") + ": the " + c("yasde.jar") + " is looked up next to the executable; the key path is " + c("chiave.pem") + " in the config folder), then the shell and the window are created and the pieces installed. A second launch only presents the existing window.",
    ])

LCOLL = seq([("link thread", "keeps one phone", "navy"), ("scoperta", "mDNS", "light"), ("adb", "TLS client", "blue"),
             ("componente", "PC side", "blue"), ("phone", "the service", "dark")], [
    (0, 2, "ph_adb_collega(last address)"),
    ("sep", "no answer or no address: search the network"),
    (0, 1, "ph_scoperta_cerca(1500 ms)"),
    (1, 0, "candidates", True),
    (0, 2, "ph_adb_collega(candidate, 8000 ms)"),
    (0, 2, "getprop ro.serialno (serial)"),
    (0, 3, "ph_componente_avvia(15000 ms)"),
    (3, 2, "sync: copy yasde-XXXXXXXX.jar"),
    (3, 4, "launch app_process (service)"),
    (4, 3, "PRONTO (stdout line)", True),
    (3, 4, "command and input channels; greeting"),
    (4, 3, "OK: API, screen, model", True),
    (0, 0, "ph_dati_apri, ph_dati_stato"),
    ("nota", 0, "connected (or locked): notice to the shell"),
], "«FIG» — From searching to connected", width=900)

S2 = p("Everything between “no phone” and “connected” happens in " + c("tentativo") + " and " + c("prova_candidato") + " (" +
       c("src/collegamento/collegamento.c") + ") on the link thread. The thread starts in " + c("CERCO") + " (searching) as soon as " + c("ph_col_avvia") + " returns.", lead=True) + LCOLL + \
    steps([
        "<b>Last known address.</b> From " + c("telefoni.toml") + " (the config piece): the phone expected, or the most recently seen one. It is tried first because the port of wireless debugging changes every time the option is turned on, but the address often still works.",
        "<b>Discovery.</b> " + c("ph_scoperta_cerca") + " for 1500 ms; pairing services are ignored here. Known phones come first (heaviest weight: the most recently seen), then the others. If several are found and none is known, the first is taken and the notice says so (" + c("scelta_arbitraria") + ").",
        "<b>ADB connection</b> with " + c("ph_adb_collega") + " (8000 ms). If the phone does not recognise the key the outcome is " + c("T_NON_ABBINATO") + ": if no candidate connects, the state becomes " + c("NON_ABBINATO") + " and the link waits for the pairing code (" + rif("Pairing with the six-digit code") + ").",
        "<b>Serial number.</b> " + c("getprop ro.serialno") + "; if empty or " + c("unknown") + ", " + c("aid-<android_id>") + " (from " + c("settings get secure android_id") + "), and as a last resort " + c("modello-<model>") + ". After the first connection only the same serial is accepted: another phone at the same address is discarded.",
        "<b>Component start</b> (" + rif("Starting the component on the phone") + ") with a 15000 ms limit for copy, launch, " + c("PRONTO") + " and greeting.",
        "<b>Data</b>: " + c("ph_dati_apri") + " registers on the events " + c("0x7e") + "-" + c("0x7f") + " (the link is the only one that does: there is one listener per component); then the locked state is read once.",
        "<b>Record.</b> " + c("telefoni.toml") + " is updated (serial, name, address as " + c("host:port") + " or " + c("[host]:port") + " for IPv6, last seen); a failure to save is never fatal. The session is published and the state becomes " + c("COLLEGATO") + ", or " + c("BLOCCATO") + " if the screen is locked.",
    ])

S3 = p("The link is a state machine with its own thread and no graphics. Notices are delivered one at a time from that thread, in order, "
       "and repeated notices are not sent twice.", lead=True) + \
    table(["State", "Meaning", "Leaves when"], [
        [c("CERCO"), "searching, connecting", "connected; or no candidate answers (waits " + c("pausa_cerca_ms") + " and tries again); or the key is rejected"],
        [c("COLLEGATO"), "ADB alive, component started, greeting done", "the screen locks (" + c("BLOCCATO") + "), or the connection or component dies (" + c("PERSO") + ")"],
        [c("BLOCCATO"), "connected but the phone screen is locked: the shell shows a veil", "unlock (" + c("COLLEGATO") + "), or drop (" + c("PERSO") + ")"],
        [c("PERSO"), "lost: retries by itself with growing waits", "a retry succeeds; " + c("ph_col_riconnetti_ora") + " skips the wait"],
        [c("NON_ABBINATO"), "the phone does not know this PC's key", c("ph_col_abbina") + " (pairing), or " + c("ph_col_riconnetti_ora") + " to retry without pairing"],
        [c("CHIUSO"), "close requested; everything released in order", "final state"],
    ], "«TAB» — Link states") + \
    table(["Option", "Default", "Used for"], [
        [c("durata_scoperta_ms"), "1500", "one network search"],
        [c("timeout_collega_ms"), "8000", c("ph_adb_collega") + " (the whole TCP, STLS, TLS procedure)"],
        [c("timeout_componente_ms"), "15000", c("ph_componente_avvia")],
        [c("timeout_abbinamento_ms"), "15000", c("ph_abbinamento_esegui")],
        [c("pausa_cerca_ms"), "1000", "between two searches in " + c("CERCO")],
        [c("riprova_iniziale_ms"), "1000", "first wait in " + c("PERSO") + "; doubles every time"],
        [c("riprova_massimo_ms"), "30000", "ceiling of that wait"],
        [c("riprova_dopo_blocco_ms"), "3000", "ceiling after a drop that happened from " + c("BLOCCATO") + ": wireless debugging is reopened on unlock"],
        [c("controllo_ms"), "500", "period of the liveness and lock check"],
        [c("attesa_prestiti_ms"), "3000", "how long a drop waits for loans to be returned"],
    ], "«TAB» — Link timings (" + c("PhColOpzioni") + "; 0 means default)") + \
    p("While " + c("COLLEGATO") + " or " + c("BLOCCATO") + " the thread checks every " + c("controllo_ms") + " that " + c("ph_adb_viva") + " and " + c("ph_componente_vivo") +
      " are still true and follows the lock flag, which comes from the " + c("0x7e") + " events and from the first state read. Some phones turn wireless debugging off when they lock: “locked, lost, unlocked, connected” then happens without any intervention.")

S4 = p("The link's sessions live only while the connection does. Whoever wants to use the ADB connection, the component or the data "
       "object takes a loan and gives it back; the link never destroys an object under the feet of a borrower.", lead=True) + \
    ul([
        c("ph_col_presta") + " returns a loan only in " + c("COLLEGATO") + " or " + c("BLOCCATO") + "; " + c("ph_col_prestito_adb") + ", " + c("ph_col_prestito_componente") + " and " + c("ph_col_prestito_dati") + " are valid until " + c("ph_col_restituisci") + ".",
        "On a drop (or on close) the link first stops granting loans, calls the " + c("in_perdita") + " notice (“about to lose the connection: give back”), then waits up to " + c("attesa_prestiti_ms") + " for them.",
        "If a loan is not returned in time the session becomes an orphan: its memory stays valid, calls fail quickly because the connection is dead, and the last to return destroys it in order (data, component, connection; up to about 2 s). Meanwhile the link goes on with a new session.",
        "Everything opened with the borrowed objects (channels from " + c("ph_componente_canale") + ", searches and clipboard of " + c("dati") + ") must be closed before the loan is returned. Video streams may be closed afterwards: the component closes them anyway, but they must still be passed to " + c("ph_video_chiudi") + ".",
        "A loan must never be returned from inside a component or data callback (it could wait for that very callback): the same rule forbids calling " + c("ph_col_chiudi") + " from a notice.",
    ])

S5 = p("When the link reports " + c("COLLEGATO") + " or " + c("BLOCCATO") + ", " + c("col_avviso") + " (in " + c("src/shell/ossatura/servizi_veri.c") + ") posts a message to the manager thread " + c("ph-gestore") +
       ", which builds the shell-side session.", lead=True) + \
    steps([
        "Weather: the phone's serial is told to the weather service; the phone profile (window layout, desktop, preferences per phone) is loaded.",
        "<b>Data</b> (" + c("sv_dati_sessione_nuova") + "): the state is read; the listener for panel events " + c("0x6e") + " is registered and " + c("PANNELLO_STATO") + " is asked; if the panel is on and the phone is not “in hand” it is switched off, queued on the state pool because it can take seconds (" + rif("The phone panel: off when connected") + "); the clipboard channel is opened; the app list and the notifications are requested; the quick-controls state is read.",
        "Transfers: the transfer queue gets the new session.",
        "Streams: every window already open (after a drop) is reopened (" + rif("Video: sessions and recovery") + ").",
        "Weather position: there is a phone now, so the position is the phone's.",
        "<b>Audio</b> is not started here: the first stream of the session schedules it " + c("SV_AUDIO_RITARDO_MS") + " (5000 ms) later, and a failed start is retried after 2, 4, 8 and 16 s (" + rif("Audio: playback on the PC") + ").",
    ])

S6 = p("There are two ways out: the user closes YASDE (orderly), or the connection or the service dies (drop). Both end with the phone as it was, "
       "the first by the component, the second by the guardian.", lead=True) + \
    p("<b>Orderly close.</b> The window is hidden and destroyed (pieces stop listening), then " + c("ph_shell_libera") + " closes the streams, then the services are freed: " +
      c("ph_col_chiudi") + " raises " + c("in_perdita") + ", the manager closes the session and returns the loan, and the component is closed. " +
      c("ph_componente_chiudi") + " first forbids any new " + c("PRESENZA") + ", sends " + c("CHIUDI") + " and waits at most 2000 ms for the answer (the wait for the turn included), then joins its threads and closes the channels. The shell channel goes last: closing it is the safety net that stops the service. SIGTERM and SIGINT take the same path.") + \
    p("<b>On the phone</b> the " + c("Servizio") + " answers " + c("OK") + " and then: closes the command socket and the listening socket; runs the pieces' close actions on a separate thread for at most 3 s; asks the guardian to run the restore list (20 s limit per run); deletes its files; prints " + c("servizio terminato") + " and exits.") + \
    p("<b>Drop.</b> If the connection dies (" + c("ph_adb_viva") + " becomes false within about 10 s thanks to TCP keepalive and " + c("TCP_USER_TIMEOUT") + "), or the service process ends, the link moves to " + c("PERSO") + ", gives back the session as above and retries with waits of 1, 2, 4 ... up to 30 s. On the phone the service sees its command channel or stdin close and shuts down by itself; if it was killed (" + c("kill -9") + ", or " + c("adbd") + " restarted), the guardian reads end-of-file on its pipe and runs the restore list. The streams of the open windows become " + c("PERSO") + " and are reopened by themselves when a new session exists; a running recording is stopped.") + \
    warn("A service killed together with its guardian (restarting " + c("adbd") + " kills the whole process group) leaves the jar and the restore script behind. The next service cleans them at start-up (" + rif("Cleaning up leftovers at start-up") + ").")

CHAPTER = ("Startup and session lifecycle", [
    ("Startup: from the AppImage to the window", S1),
    ("Connecting: from search to a live session", S2),
    ("Link states and timings", S3),
    ("Loans: using the session objects", S4),
    ("Building the session on the shell side", S5),
    ("Orderly close, drop and reconnection", S6),
])
