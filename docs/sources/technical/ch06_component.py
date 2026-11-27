from build import arrow, box, c, fig, flow, note, p, path, rif, seq, steps, table, text, ul, warn, zone

S1 = p("The PC side of the phone component is " + c("src/componente") + ". It copies the jar to the phone, launches the service, opens the protocol channels, greets the service and then offers the rest of the PC two things: requests answered with " + c("OK") + " or " + c("ERRORE") + " (and events that arrive at any time), and the video and input functions. "
       "It uses only " + c("adb.h") + " and " + c("file.h") + " and never decodes video: it hands packets to the layer above.", lead=True) + \
    table(["File", "Role"], [
        [c("componente.h"), "the contract: message codec, " + c("PhComponente") + ", video and input functions, presence"],
        [c("componente.c"), "start, close, command thread, requests, input"],
        [c("video.c"), "video sessions: open, read packets, resize, key frame, pause, close"],
        [c("messaggi.c") + ", " + c("lettura.c"), "message writer and bounded reader; resumable reads"],
        [c("interno.h"), "internal constants and the " + c("PhComponente") + " structure"],
    ], "«TAB» — Files of " + c("src/componente"))

START = seq([("PC", "ph_componente_avvia", "navy"), ("adbd", "ADB channels", "blue"), ("service", "on the phone", "dark")], [
    (0, 1, "sync: SEND yasde-XXXXXXXX.jar"),
    (0, 1, "launch app_process with the jar"),
    (1, 2, "process started (uid 2000)", True),
    ("nota", 2, "socket, leftovers cleaned, guardian started"),
    (2, 0, "stdout line: ready", True),
    (0, 1, "abstract socket, then the command line"),
    (0, 1, "abstract socket, then the input line"),
    (0, 2, "SALUTO (version 2)"),
    (2, 0, "OK: version, API, screen, density, model", True),
], "«FIG» — Starting the service")

S2 = p(c("ph_componente_avvia(adb, jar, event_fn, user, info, timeout_ms, error)") + " does the following in order. " + c("timeout_ms") + " (0 means 10000) applies to the launch, the wait for " + c("PRONTO") + ", opening the channels and the greeting; afterwards a request has 15000 ms (" + c("PH_TIMEOUT_RICHIESTA_MS") + ", because opening an app can take a while).", lead=True) + START + \
    steps([
        "<b>Name.</b> " + c("/data/local/tmp/yasde-<8 hex digits>.jar") + " with the digits read from " + c("/dev/urandom") + ". The name changes at every start so that the guardian of a service that has just died, which deletes its own jar, cannot delete the next one.",
        "<b>Copy</b> with " + c("ph_file_invia") + " (" + rif("File transfer with sync") + "). The “jar copied” flag is set before the copy: if anything fails from here on, the jar is removed with " + c("rm -f") + " (3000 ms limit, result ignored).",
        "<b>Launch</b> with the legacy " + c("shell:") + " service: " + c("CLASSPATH=<jar> app_process / it.yasde.Servizio") + ". The channel stays open for the whole life of the service; if it closes, the service ends.",
        "<b>Wait for PRONTO</b>: the output is read line by line (lines of at most 160 characters are kept, a longer unterminated chunk beyond 4096 bytes is dropped); earlier log lines are discarded but the last one is remembered for the error message (“the service ended before PRONTO; last line: ...”).",
        "<b>Channels.</b> Each protocol channel is an ADB channel " + c("localabstract:yasde") + " followed by one ASCII line naming its type. The component opens " + c("comandi") + " and " + c("input") + " itself; the rest of the PC opens others through " + c("ph_componente_canale") + " (" + c("audio") + ", " + c("cerca") + ", " + c("appunti") + "), which refuses the three names owned by the component (" + c("comandi") + ", " + c("input") + ", " + c("video:") + "...) and any line longer than 60 characters.",
        "<b>Threads</b> are started: command reader and shell-log reader; then " + c("SALUTO") + " is sent; then the presence thread.",
        "<b>Greeting.</b> " + c("SALUTO") + " carries the protocol version (2). The reply gives the version, the Android API level, the real screen size and density, and the model (" + c("PhTelefonoInfo") + "). A different version is an error.",
    ]) + \
    table(["Constant", "Value", "Meaning"], [
        [c("PH_PROTOCOLLO_VERSIONE"), "2", "protocol version"],
        [c("PH_MSG_MAX_CORPO"), "1 MiB", "largest message body; a larger declared length ends the command thread"],
        [c("PH_VIDEO_MAX_PACCHETTO"), "8 MiB", "largest video packet"],
        [c("PH_FETTA_MS"), "50", "every blocking read is cut in slices of this size, so a close never waits long"],
        [c("PH_TIMEOUT_PREDEFINITO_MS"), "10000", "start-up default"],
        [c("PH_TIMEOUT_RICHIESTA_MS"), "15000", "one request"],
        [c("PH_PRESENZA_INTERVALLO_S"), "300", "idle time after which " + c("PRESENZA") + " is sent"],
    ], "«TAB» — Constants of the component") + \
    p("<b>Requests.</b> The command channel serves one request at a time: " + c("ph_componente_richiedi_coda") + " takes the turn (a flag, not a mutex, because the wait is timed and the death of the component wakes the queue), writes the message and waits for the " + c("OK") + " or " + c("ERRORE") + " delivered by the reader thread. An " + c("ERRORE") + " becomes a " + c("PhErrore") + " with the phone's sentence; the sentence " + c("tipo sconosciuto") + " is turned into “the service on the phone does not know request 0x..”. "
      "If the phone does not answer within the limit the channel is no longer aligned (a late answer would be taken for the next one), so the component is declared dead. An answer with no request waiting is discarded.") + \
    p("<b>Events</b> (phone to PC, any time, even between a request and its answer) are told apart by type. Video events (" + c("0x48") + ") go to the " + c("PhEvento") + " given at start; others go to listeners registered with " + c("ph_componente_ascolta") + " by type range (at most 8, a range overlapping another without being equal is an error, a NULL function removes). The listener table lock stays held during the call, so removing a listener waits for a call in progress; a listener must return fast and must not call request functions.") + \
    p("<b>Presence.</b> The thread " + c("thread_presenza") + " sends " + c("PRESENZA") + " when no request has started for " + c("presenza_ms") + " (300 s). It uses the boot clock (" + c("CLOCK_BOOTTIME") + ", which advances while the PC sleeps, so at wake-up the message goes at once) and waits in slices of at most 30 s. A failure writes one generic line on stderr and is retried at the next expiry. See " + rif("Signs of life and PC absence") + ".")

S3 = p("The service runs " + c("Servizio.main") + " in " + c("telefono/base/it/yasde/Servizio.java") + ". Every thread catches everything, including " + c("Error") + ", so a broken channel cannot bring the service down. The order of its start-up matters.", lead=True) + \
    steps([
        "Find the running jar from " + c("CLASSPATH") + "; it is accepted only if it is under " + c("/data/local/tmp/") + ", ends in " + c(".jar") + " and has no " + c("..") + " (otherwise nothing will be deleted at the end).",
        "Create the guardian object (not started).",
        "<b>Take the socket:</b> an abstract " + c("LocalServerSocket") + " named " + c("yasde") + ". From here no other service of this name is alive.",
        "<b>Clean leftovers</b> of dead sessions (" + rif("Cleaning up leftovers at start-up") + "), before starting the own guardian (its script could have the name of a dead one with the same pid).",
        "<b>Start the guardian</b> (" + rif("The guardian and the restore list") + ") and register it in " + c("Registro") + "; add a JVM shutdown hook (SIGHUP, SIGTERM) that runs the close without " + c("System.exit") + ".",
        "Check the hidden APIs (" + c("Nascoste.verifica") + ") and log the missing ones; then register the pieces (below).",
        "Start the stdin guard thread: if the shell's stdin closes, the PC is gone and the service shuts down.",
        "Print " + c("PRONTO") + " and loop on " + c("accept") + ", one daemon thread named " + c("canale") + " per connection.",
    ]) + \
    p("<b>Pieces.</b> " + c("Servizio") + " calls by reflection the static method " + c("registra()") + " of each class in " + c("PEZZI") + "; a missing class is skipped with a log line.") + \
    table(["Class", "Folder", "Registers"], [
        [c("Video"), c("telefono/video"), "channel " + c("video") + ", commands " + c("0x40") + "-" + c("0x4f") + ", close action"],
        [c("Input"), c("telefono/video"), "channel " + c("input")],
        [c("Audio"), c("telefono/audio"), "channel " + c("audio") + ", close action"],
        [c("Dati"), c("telefono/dati"), "commands " + c("0x70") + "-" + c("0x73") + ", " + c("0x76") + "-" + c("0x77") + ", " + c("0x7b") + "-" + c("0x7d") + ", " + c("0x90") + "-" + c("0x9f") + "; events " + c("0x7e") + ", " + c("0x7f") + "; close action"],
        [c("Archivio"), c("telefono/archivio"), "commands " + c("0x74") + ", " + c("0x75")],
        [c("Cerca"), c("telefono/archivio"), "channel " + c("cerca")],
        [c("Appunti"), c("telefono/appunti"), "channel " + c("appunti") + ", close action"],
        [c("Pannello"), c("telefono/pannello"), "commands " + c("0x60") + "-" + c("0x6f") + ", signs of life, close action"],
        [c("Controlli"), c("telefono/controlli"), "commands " + c("0x80") + "-" + c("0x8f") + ", close action (turns off the torch YASDE lit)"],
    ], "«TAB» — Pieces of the component") + \
    p("<b>Channels.</b> Each connection is first checked for who is on the other end (" + rif("Who may talk to the component") + "), then a line of at most 64 characters is read with a 5 s socket timeout. " + c("comandi") + " is served by the service itself and only one command channel may exist (a second is refused). Any other line is split at the first colon into prefix and argument and handed to " + c("Registro.canale") + "; an unknown prefix closes the channel.") + \
    p("<b>Command channel.</b> A loop reads a message, calls " + c("Smistatore.tratta") + " and writes " + c("OK") + " or " + c("ERRORE") + " under a lock shared with the events, so answers and events never interleave. An exception becomes " + c("ERRORE") + " with its message (an exception with no message becomes “malformed request”: a Java class name is never sent). After a successful " + c("CHIUDI") + " the loop ends and the service shuts down; if the channel closes for any reason the service shuts down as well.")

S4 = p(c("Registro") + " (in " + c("telefono/contratto") + ") is the meeting point between the service and the pieces, so the two folders compile without each other. Everything in it is thread-safe.", lead=True) + \
    table(["Method", "Used by", "Purpose"], [
        [c("registraCanale(prefix, handler)"), "pieces", "serve the channel whose first line starts with the prefix (" + c("GestoreCanale.gestisci(socket, argument)") + ")"],
        [c("registraComandi(from, to, handler)"), "pieces", "serve command types in a range (" + c("GestoreComandi.gestisci(type, body)") + " returns the body of the " + c("OK") + ")"],
        [c("registraChiusura(runnable)"), "pieces", "what to do at orderly close; run before the guardian restores; never run if the service is killed"],
        [c("mandaEvento(type, body)"), "pieces", "send an event to the PC; does nothing without an open command channel"],
        [c("guardiano()"), "pieces", "the guardian; can be null in isolated tests of one piece"],
        [c("segnoDiVita(nowMs)"), "dispatcher", "record a sign of life of the PC and notify listeners"],
        [c("registraSegnoDiVita(runnable)"), "pieces", "be notified at every sign of life, on the command thread: only wake a thread, no work"],
    ], "«TAB» — " + c("Registro")) + \
    p(c("Smistatore") + " has no Android classes (the clock and the greeting come from outside) so it can be tested on a plain JVM. For every message, even unknown or malformed, it first records the sign of life, then dispatches: " + c("SALUTO") + " to the greeting function; " + c("PRESENZA") + " answers an empty " + c("OK") + " (and refuses a non-empty body); " + c("CHIUDI") + " answers an empty " + c("OK") + "; anything else goes to the handler registered for its type, or fails with " + c("tipo sconosciuto") + ".")

S5 = p("The socket is abstract and in principle reachable by any process of the phone. Without a check any app could take the command channel before the PC, or open " + c("input") + " and send touches. The PC always arrives through adbd, that is as the shell user.", lead=True) + \
    p("Right after " + c("accept") + " and before reading one byte, " + c("Servizio.connessione") + " reads the peer's credentials from the kernel (not forgeable) and accepts only " + c("Pari.UID_SHELL") + " (2000) and " + c("Pari.UID_ROOT") + " (0, which is adbd on development phones). Any other uid (apps start at 10000) is closed at once. If the credentials cannot be read, the connection is refused. The refusal is logged as " + c("collegamento rifiutato (uid N non ammesso)") + " at most once per second. The uid is not personal data and is what tells you what to change if some phone's adb shows a different user.") + \
    warn("The check was proven on a real phone: the live connection of 24 Nov 2026 succeeded with it active (adb arrives as uid 2000). If YASDE cannot connect on some other phone, look for that log line first.")

FLOW = flow([
    ("register", "imposta(name, order, cmd)", "blue"),
    ("write list", "tmp file + rename", "light"),
    ("change", "only if imposta is true", "navy"),
    ("undo", "cmd, then togli(name)", "light"),
], "«FIG» — The life of one restore entry")

S6 = p("The guardian is the class " + c("Custode") + " (" + c("telefono/base/it/yasde/Custode.java") + "), the implementation of the " + c("Guardiano") + " interface. It makes sure the phone returns as it was even if the service dies abruptly. The design documents speak of two guardians (connection and service); the code has this single one.", lead=True) + FLOW + \
    steps([
        "<b>A separate process.</b> The service starts " + c("setsid sh -c <script>") + ", a shell that is leader of its own session, so it dies neither with the service nor with the PC's shell. The service keeps the write end of a pipe open: its death, however it happens (even " + c("kill -9") + "), closes the pipe and the guardian reads end-of-file.",
        "<b>The list.</b> The restore commands live in a file " + c("/data/local/tmp/yasde-custode-<pid>.sh") + ", rewritten atomically (temporary file, " + c("fsync") + ", rename on the same file system) at every " + c("imposta") + " or " + c("togli") + ". The guardian runs it as a script. It starts with " + c("echo $$ > \"$0.pid\"") + " (the id of the session, used to kill leftovers) and defines " + c("timeout 20") + " if the phone has it.",
        "<b>One shell per entry</b>, sorted by order (lowest first; equal orders keep insertion order), each in its own " + c("sh -c '...'") + " so a wrong command does not eat the next ones; each runs under " + c("timeout 20") + " when available.",
        "<b>The guardian's script</b> reads one line from the pipe. If the line is not " + c("fine") + " (end-of-file: the service died) it runs the list. Then it deletes the list and its " + c(".tmp") + " and " + c(".pid") + ", deletes the jar only if it is still the same file seen at start (inode, size, date: the PC may already have copied a new one), and as a last act kills the processes still in its own session, including children of an expired restore adopted by init.",
        "<b>Orderly close:</b> " + c("ferma()") + " runs the list itself (20 s limit; on expiry the session is killed with " + c("pgrep -s") + "), then writes " + c("fine") + " and closes the pipe; the guardian only deletes the files. If a restore does not finish in time, " + c("fine") + " is not written and nothing is deleted: closing the pipe makes the guardian redo the list.",
    ]) + \
    p("<b>The contract</b> (" + c("Guardiano") + "): " + c("imposta(name, order, shellCommand)") + " returns true only if the restore is really safe, that is the new list was written. If it returns false the caller must not make the change, because nobody would undo it. After the guardian has stopped, " + c("imposta") + " executes the command at once (20 s) and returns false. " + c("togli(name)") + " removes an entry whose change has already been undone; if the rewrite fails the disk list keeps the entry (one entry too many, conditional and harmless, is better than one too few).") + \
    p("<b>Value-wins rule.</b> A restore command changes a setting only if it still has the value YASDE wrote; if the user changed it meanwhile, it stays. Commands are self-contained shell lines: no environment variables of the service, no nested quotes.") + \
    table(["Entry", "Order", "Registered by", "Restores"], [
        [c("audio-volume"), "50", c("Cattura"), "media volume back to the original, only if still at the maximum YASDE set (" + c("cmd media_session volume --stream 3") + ")"],
        [c("pannello-acceso"), "400", c("Pannello"), "panel on: " + c("CLASSPATH=<jar> app_process / it.yasde.Pannello accendi") + ", which does nothing if Android is asleep"],
        [c("pannello-frequenza"), "410", c("Pannello"), c("min_refresh_rate") + " back to the user's value, only if still ours"],
        [c("pannello-timeout"), "420", c("Pannello"), c("screen_off_timeout") + " back to the user's value (or deleted if it did not exist), only if still the maximum"],
        [c("trova-volume") + ", " + c("trova-modo") + ", " + c("trova-non-disturbare"), "420-422", c("Trova"), "unused: “find my phone” is not available in this version"],
    ], "«TAB» — Restore entries and their order") + \
    note("Not everything has an undo on purpose. The ringer mode and Do Not Disturb set by the quick-switches widget are explicit user commands with a permanent effect, like tapping the phone's own quick tile; the torch is turned off at orderly close and, if the service dies, by the system because it is tied to the process that lit it (measured on the test phone).") + \
    note("The wake lock (" + c("TieniSveglio") + ") is not in the list for the same reason it is not needed: the system releases it when the process dies.")

S7 = p("If adbd restarts (wireless debugging switched off and on), init kills every process of its group, even those with " + c("setsid") + ": service and guardian die together and the jar and the restore script stay on the phone. The next service cleans them. The rule is in " + c("Resti") + " (pure, with no Android classes: the processes are read from a folder passed in, so it is tested on a JVM) and " + c("Custode.pulisciResti") + ".", lead=True) + \
    steps([
        "<b>Names recognised by exact form only</b>: " + c("yasde-custode-<pid>.sh") + " with accessories " + c(".pid") + ", " + c(".tmp") + " and " + c(".tentativi") + ", and " + c("yasde-<8 hex>.jar")+ ".",
        "If there is nothing of ours apart from the current jar, the costly scan of the processes is skipped.",
        "<b>Scan of the processes</b> in " + c("/proc") + ": the pids of services and the paths under the folder cited by any process. If the own service is not among the processes read, the scan is not reliable and nothing is executed or deleted.",
        "<b>The plan.</b> A script is dead if its pid is not a live service (the own pid always counts as dead: its guardian has not started) and no live process cites it. A jar is old if it is not the current one and no live process cites it. Orphan accessories whose script is gone are deleted.",
        "<b>Run dead scripts first</b> (their pending restores) with a total budget " + c("4000") + " ms, of which 1000 reserved to kill the session of an expired one; only files that are regular, not links and owned by shell or the service's own user are executed. A counter in " + c(".tentativi") + ", written before running, stops retries: after " + c("MAX_TENTATIVI") + " (3) failed starts the script is no longer executed and stays on the phone with the user's restores inside.",
        "Then delete the script and accessories, then the old jars; a jar cited by a script that is left behind stays, because the script's command (" + c("app_process ... it.yasde.Pannello accendi") + ") needs it.",
    ]) + \
    p("This must finish before " + c("PRONTO") + ", which the PC waits for only a short time, hence the budget. The PC side removes its own jar when start-up fails (" + rif("Starting the component on the phone") + ").")

S8 = p("What happens depends on how the service ends.", lead=True) + \
    table(["Cause", "Service", "Phone", "PC"], [
        ["PC closes YASDE", "gets " + c("CHIUDI") + ", runs close actions and restores, deletes jar", "as before", "orderly"],
        ["PC silent (no signs of life)", "keeps running", "after 30 minutes without signs of life the panel and the wake lock are released but service and sessions stay", rif("Signs of life and PC absence")],
        ["Command channel or stdin closes", "shuts down by itself, restores", "as before", "link goes to " + c("PERSO")],
        ["SIGHUP, SIGTERM", "shutdown hook: close without exit; the guardian is the net", "as before", "link goes to " + c("PERSO")],
        [c("kill -9") + " of the service", "gone", "guardian reads end-of-file and runs the list, then removes files", "link goes to " + c("PERSO") + ", reconnects with a new jar"],
        ["adbd restarted", "service and guardian both gone", "leftovers stay until the next start", "reconnects; the new service cleans (" + rif("Cleaning up leftovers at start-up") + ")"],
    ], "«TAB» — How the service ends")

CHAPTER = ("The phone component", [
    ("Starting the component on the phone", S1 + S2),
    ("The service process", S3),
    ("The registry and the dispatcher", S4),
    ("Who may talk to the component", S5),
    ("The guardian and the restore list", S6),
    ("Cleaning up leftovers at start-up", S7),
    ("What happens when the service dies", S8),
])
