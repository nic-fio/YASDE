from build import c, note, p, rif, table, ul, warn

S1 = p("The protocol between the PC and the component is described in " + c("docs/protocollo.md") + ", which is the contract between " + c("src/componente") + " (PC, C) and " + c("telefono/") + " (phone, Java). Whoever finds it wrong or incomplete does not change it alone: the problem is reported to whoever coordinates. "
       "This chapter reorganises that document for reading; the version is " + c("3") + " (" + c("PH_PROTOCOLLO_VERSIONE") + ", " + c("Protocollo.VERSIONE") + ").", lead=True) + \
    p("All the channels are ADB channels of the service " + c("localabstract:yasde") + ". Immediately after opening one, the PC writes a line of ASCII text ended by a newline that names the channel type. The service accepts lines of at most 64 characters (5 s to arrive).") + \
    table(["Line", "Channel", "How many", "Direction"], [
        [c("comandi"), "requests and answers, events", "one", "both"],
        [c("video:<id>"), "H.264 packets of the session <id> (decimal)", "one per session", "phone to PC"],
        [c("input"), "mouse and keyboard events", "one", "PC to phone"],
        [c("audio"), "Opus packets", "one (a second is closed)", "phone to PC"],
        [c("cerca"), "search of files and folders", "one per search", "both"],
        [c("appunti"), "shared clipboard", "one", "both"],
    ], "«TAB» — Channel types") + \
    p("An unknown type makes the service close the channel. The service runs as uid 2000 and, at the end (command channel closed, or shell closed), closes every session, runs the restores and deletes the jar.")

S2 = p("On " + c("comandi") + " and " + c("input") + " every message has the same shape. All numbers are big endian; a " + c("stringa") + " is a 2-byte length followed by UTF-8 with no terminator.", lead=True) + \
    table(["Field", "Size", "Notes"], [
        ["type", "1 byte", "see the tables below"],
        ["length", "4 bytes", "body length only; at most 1 MiB (" + c("PH_MSG_MAX_CORPO") + ", " + c("Protocollo.MAX_CORPO") + ")"],
        ["body", "length bytes", "depends on the type"],
    ], "«TAB» — Message header") + \
    p("On " + c("comandi") + " the PC sends one request at a time and waits for the answer, " + c("OK") + " (possibly with a body) or " + c("ERRORE") + ". Events (phone to PC) may arrive at any moment, even between a request and its answer, and are recognised by their type. Two channels have a different layout: " + c("video:<id>") + " (" + rif("Video: receiving and decoding") + ") and " + c("audio") + " (" + rif("Audio: capture and transport") + "). The clipboard messages carry the text as a 32-bit length plus UTF-8, not as a " + c("stringa") + ".") + \
    table(["Writer", "Reader"], [
        [c("ph_msg_inizia") + ", " + c("ph_msg_u8") + ", " + c("ph_msg_u16") + ", " + c("ph_msg_i16") + ", " + c("ph_msg_u32") + ", " + c("ph_msg_stringa") + ", " + c("ph_msg_finisci") + " (writes the provisional length, then corrects it)",
         c("PhLettore") + " with " + c("ph_leggi_u8") + ".." + c("ph_leggi_u64") + " and " + c("ph_leggi_stringa") + ": a cursor that never leaves the buffer; after a short body " + c("rotto") + " stays true and reads return 0"],
        [c("Protocollo.scrivi") + ", " + c("Protocollo.scriviStringa") + ", " + c("Protocollo.corpoErrore"), c("Protocollo.leggi") + ": refuses a length above the maximum"],
    ], "«TAB» — The codecs on the two sides") + \
    note("Any message the phone receives from the PC on " + c("comandi") + " counts as a sign of life (" + rif("Signs of life and PC absence") + ").")

S3 = p("Types by range. Direction: PC→T is a request or command from the PC to the phone, T→PC is an answer or event.", lead=True) + \
    table(["Type", "Name", "Direction", "Body"], [
        "General (0x01–0x0f), on " + c("comandi"),
        [c("0x01"), c("SALUTO"), "PC→T", c("u16") + " protocol version (3). Answer " + c("OK") + ": " + c("u16") + " version, " + c("u16") + " API level, " + c("u16") + " width, " + c("u16") + " height, " + c("u16") + " dpi, " + c("stringa") + " model. A different version is " + c("ERRORE")],
        [c("0x02"), c("OK"), "T→PC", "depends on the request; empty if not said"],
        [c("0x03"), c("ERRORE"), "T→PC", c("stringa") + " a sentence in Italian (shown to the user only after translation or kept to the log); unknown type: " + c("tipo sconosciuto")],
        [c("0x04"), c("CHIUDI"), "PC→T", "empty; the service answers " + c("OK") + " and then restores everything and ends"],
        [c("0x05"), c("PRESENZA"), "PC→T", "empty; answer " + c("OK") + " empty. “The PC is alive”"],
        "Video (0x40–0x4f), on " + c("comandi"),
        [c("0x40"), c("VIDEO_APRI"), "PC→T", c("u32") + " id, " + c("u16") + " width, " + c("u16") + " height, " + c("u16") + " dpi, " + c("u32") + " bits per second, " + c("u8") + " fps, " + c("stringa") + " app package (empty = mirror of the real screen), " + c("u8") + " page (version 2, at the end; 0 = none)"],
        [c("0x41"), c("VIDEO_CHIUDI"), "PC→T", c("u32") + " id, " + c("u8") + " remove_task (1 = remove the app from recents)"],
        [c("0x42"), c("VIDEO_RIDIMENSIONA"), "PC→T", c("u32") + " id, " + c("u16") + " width, " + c("u16") + " height, " + c("u16") + " dpi. Answer " + c("OK") + ": " + c("u16") + " width, " + c("u16") + " height actually applied"],
        [c("0x43"), c("VIDEO_CHIAVE"), "PC→T", c("u32") + " id: ask for a key frame now"],
        [c("0x44"), c("VIDEO_PAUSA"), "PC→T", c("u32") + " id"],
        [c("0x45"), c("VIDEO_RIPRENDI"), "PC→T", c("u32") + " id"],
        [c("0x46"), c("VIDEO_PAGINA"), "PC→T", c("u32") + " id, " + c("u8") + " page (not 0): start a page of the session's app (version 2). Answer " + c("OK") + " with an empty body"],
        [c("0x48"), c("VIDEO_EVENTO"), "T→PC event", c("u32") + " id, " + c("u8") + " what, " + c("u16") + " a, " + c("u16") + " b"],
        "Input (0x50–0x55), on " + c("input") + ", PC to phone only, no answer",
        [c("0x50"), c("TOCCO"), "PC→T", c("u32") + " id, " + c("u16") + " width, " + c("u16") + " height, " + c("u8") + " action (0 down, 1 move, 2 up, 3 cancel), " + c("u16") + " x, " + c("u16") + " y, " + c("u8") + " buttons (bit 0 left, 1 right, 2 middle; sent by the PC, not read by the phone in this version)"],
        [c("0x51"), c("ROTELLA"), "PC→T", c("u32") + " id, " + c("u16") + " width, " + c("u16") + " height, " + c("u16") + " x, " + c("u16") + " y, " + c("i16") + " horizontal, " + c("i16") + " vertical (in 120ths of a notch)"],
        [c("0x52"), c("TASTO"), "PC→T", c("u32") + " id, " + c("u8") + " action (0 down, 1 up), " + c("u32") + " Android key code (" + c("KeyEvent.KEYCODE_*") + "), " + c("u32") + " modifier state (" + c("KeyEvent.META_*") + ")"],
        [c("0x53"), c("TESTO"), "PC→T", c("u32") + " id, " + c("stringa") + " text to insert"],
        [c("0x54"), c("INDIETRO"), "PC→T", c("u32") + " id: the Back key (down and up)"],
        [c("0x55"), c("PIZZICO"), "PC→T", c("u32") + " id, " + c("u16") + " width, " + c("u16") + " height, " + c("u8") + " phase (0 start, 1 continue, 2 end, 3 cancel), " + c("u16") + " x, " + c("u16") + " y, " + c("i16") + " step (120ths of a notch, positive = zoom in, −1200..1200)"],
        "Clipboard (0x58–0x5a), on " + c("appunti"),
        [c("0x58"), c("APPUNTI_TESTO"), "T→PC", "the text just copied on the phone"],
        [c("0x59"), c("APPUNTI_IMPOSTA"), "PC→T", "the text to put in the phone's clipboard"],
        [c("0x5a"), c("APPUNTI_INCOLLA"), "PC→T", c("u32") + " video session id, then the text; puts it in the clipboard and sends “paste” to the app of that session"],
        "Panel (0x60–0x6f), on " + c("comandi"),
        [c("0x60"), c("PANNELLO_IMPOSTA"), "PC→T", c("u8") + " 0 = off, 1 = on"],
        [c("0x61"), c("PANNELLO_STATO"), "PC→T", "empty. Answer " + c("OK") + ": " + c("u8") + " on, " + c("u8") + " in hand"],
        [c("0x6e"), c("PANNELLO_EVENTO"), "T→PC event", c("u8") + " on, " + c("u8") + " reason (1 user, 2 call, 3 call ended, 4 asked by PC, 5 restore at close, 6 PC absent 30 min)"],
        "Phone data (0x70–0x7f), on " + c("comandi") + " unless noted",
        [c("0x70"), c("APP_ELENCO"), "PC→T", "empty. Answer: " + c("u16") + " count, then per app " + c("stringa") + " package, " + c("stringa") + " shown name, " + c("u8") + " flags (bit 0 system app, bit 1 portrait only)"],
        [c("0x71"), c("APP_ICONA"), "PC→T", c("stringa") + " package, " + c("u16") + " side in pixels (16–512). Answer: the bytes of a square PNG"],
        [c("0x72"), c("STATO"), "PC→T", "empty. Answer: " + c("u8") + " battery 0–100, " + c("u8") + " charging, " + c("u8") + " network (0 none, 1 Wi-Fi, 2 cellular), " + c("u8") + " signal 0–4, " + c("u8") + " screen locked"],
        [c("0x73"), c("NOTIFICHE"), "PC→T", "empty. Answer: " + c("u16") + " count, then per notification " + c("stringa") + " key, package, title, text, " + c("u64") + " time (ms since 1970)"],
        [c("0x74"), c("MINIATURA"), "PC→T", c("stringa") + " file path, " + c("u16") + " maximum side (32–512). Answer: the bytes of a JPEG"],
        [c("0x75"), c("MEMORIA"), "PC→T", "empty. Answer: " + c("u64") + " total bytes, " + c("u64") + " free bytes of internal storage"],
        [c("0x76"), c("MEMORIA_DETTAGLIO"), "PC→T", "empty. Answer: " + c("u64") + " bytes of images, video, audio, apps (code and data, cache included); unreadable = " + c("0xFFFFFFFFFFFFFFFF")],
        [c("0x77"), c("CALENDARIO"), "PC→T", c("u8") + " maximum (1–16). Answer: " + c("u16") + " count, per event " + c("stringa") + " title, " + c("u64") + " start, " + c("u64") + " end, " + c("u8") + " all-day"],
        [c("0x78"), c("CERCA"), "PC→T", "on " + c("cerca") + ": " + c("stringa") + " start folder, " + c("stringa") + " text, " + c("u8") + " folders only, " + c("u16") + " maximum results"],
        [c("0x79"), c("TROVATO"), "T→PC", "on " + c("cerca") + ": " + c("stringa") + " full path, " + c("u8") + " is a folder"],
        [c("0x7a"), c("CERCA_FINE"), "T→PC", "on " + c("cerca") + ": " + c("u8") + " reason (0 finished, 1 maximum reached, 2 error)"],
        [c("0x7b"), c("CALENDARIO_INTERVALLO"), "PC→T", c("u64") + " from, " + c("u64") + " to (at most 62 days), " + c("u16") + " maximum (1–500). Answer: " + c("u8") + " truncated, " + c("stringa") + " package of the default calendar app, then count and events as in " + c("CALENDARIO")],
        [c("0x7c"), c("SVEGLIA"), "PC→T", "empty. Answer: " + c("u8") + " there is one, " + c("u64") + " time, " + c("stringa") + " package that set it"],
        [c("0x7d"), c("BATTERIA"), "PC→T", "empty. Answer: " + c("u8") + " level, " + c("u8") + " status, " + c("u8") + " power source bits, " + c("i16") + " temperature in tenths of °C (" + c("0x7fff") + " unknown), " + c("u8") + " health, " + c("u16") + " millivolts, " + c("i32") + " minutes to full, " + c("i32") + " minutes of estimated autonomy (−1 unknown)"],
        [c("0x7e"), c("STATO_CAMBIATO"), "T→PC event", "as the answer of " + c("STATO")],
        [c("0x7f"), c("NOTIFICHE_CAMBIATE"), "T→PC event", "empty: the PC asks " + c("NOTIFICHE") + " again"],
        "Quick controls (0x80–0x8f), on " + c("comandi"),
        [c("0x80"), c("CONTROLLI_STATO"), "PC→T", "empty. Answer: " + c("u8") + " ringer (0 ring, 1 vibrate, 2 silent, 0xff n/a), " + c("u8") + " Do Not Disturb, " + c("u8") + " torch, " + c("u8") + " find in progress, " + c("u8") + " possible commands (bit 0 ringer, 1 DND, 2 torch, 3 find)"],
        [c("0x81"), c("SUONERIA_IMPOSTA"), "PC→T", c("u8") + " mode"],
        [c("0x82"), c("NON_DISTURBARE_IMPOSTA"), "PC→T", c("u8") + " 0 off, 1 on (priority only)"],
        [c("0x83"), c("TORCIA_IMPOSTA"), "PC→T", c("u8") + " 0 off, 1 on"],
        [c("0x84"), c("TROVA_AVVIA"), "PC→T", c("u16") + " maximum seconds; in this version always " + c("ERRORE") + " without touching anything"],
        [c("0x85"), c("TROVA_FERMA"), "PC→T", "empty; in this version " + c("OK") + " doing nothing"],
        [c("0x8e"), c("CONTROLLI_CAMBIATI"), "T→PC event", "as the answer of " + c("CONTROLLI_STATO")],
        "Approximate position (0x90–0x9f), on " + c("comandi"),
        [c("0x90"), c("POSIZIONE"), "PC→T", c("u8") + " search allowed. Answer: " + c("u8") + " outcome (0 known, 1 location off, 2 permission denied, 3 none known), " + c("i32") + " latitude and " + c("i32") + " longitude in thousandths of a degree, always multiples of 5, " + c("u32") + " age in seconds (" + c("0xffffffff") + " unknown), " + c("u8") + " provider (0 network, 1 gps, 2 fused, 3 passive), " + c("stringa") + " place name, " + c("u8") + " search started. The body is exactly 17 + n bytes"],
        "Calls, network and media (0x91–0x94, version 3), on " + c("comandi"),
        [c("0x91"), c("CHIAMATE"), "PC→T", c("u8") + " maximum (1–20). Answer " + c("OK") + ": " + c("u8") + " outcome (0 ok, 1 not available, 2 permission denied), " + c("u8") + " count n (0 unless outcome is 0, never above the maximum), then n times " + c("u8") + " kind (0 other, 1 incoming, 2 outgoing, 3 missed, 4 declined), " + c("u64") + " time (ms since 1970), " + c("u32") + " duration in seconds, " + c("stringa") + " name (empty if unknown), " + c("stringa") + " number. Newest first"],
        [c("0x92"), c("RETE"), "PC→T", "empty. Answer: " + c("u8") + " outcome, " + c("u8") + " Wi-Fi (0 off, 1 on, 2 connected), " + c("u8") + " Wi-Fi level (0–4), " + c("stringa") + " network name (empty if unknown or hidden), " + c("u8") + " mobile (0 absent, 1 no service, 2 in service), " + c("u8") + " mobile level (0–4), " + c("u8") + " generation (0 unknown, 2, 3, 4, 5), " + c("stringa") + " operator, " + c("u8") + " mobile data and " + c("u8") + " airplane mode (each 0 no, 1 yes, 2 unknown)"],
        [c("0x93"), c("MEDIA"), "PC→T", "empty. Answer: " + c("u8") + " outcome, " + c("u8") + " state (0 no session, 1 paused, 2 playing), " + c("stringa") + " title, " + c("stringa") + " artist, " + c("stringa") + " package, " + c("u8") + " possible commands (bit 0 play/pause, bit 1 next, bit 2 previous)"],
        [c("0x94"), c("MEDIA_COMANDO"), "PC→T", c("u8") + " command (1 play/pause, 2 next, 3 previous). Answer " + c("OK") + " with an empty body; " + c("ERRORE") + " if the command is outside the list, there is no session or the session does not allow it, touching nothing. No other command exists"],
    ], "«TAB» — All message types") + \
    p("<b>Pages (version 2).</b> The page byte is a number from a fixed list: " + c("0") + " none (normal launch), " + c("1") + " battery (action " + c("android.intent.action.POWER_USAGE_SUMMARY") + "), " + c("2") + " storage (action " + c("android.settings.INTERNAL_STORAGE_SETTINGS") + "). <b>Safety rule: the PC never sends a free intent</b> (no action, no component, no data): it sends only the number and the phone turns it into the standard action of the list; any other value is an " + c("ERRORE") + ". " + c("VIDEO_APRI") + " with a page other than 0 looks for the activity answering that action inside the given package only, and launches it on the virtual display; if the package has none it launches the app normally and still answers " + c("OK") + ". A page outside the list is refused before any display or encoder exists. With the mirror (empty package) the page is ignored. " + c("VIDEO_PAGINA") + " launches the page on the virtual display of that session, inside the session's package, even if the app is already open; it answers " + c("ERRORE") + ", touching nothing, if the session is unknown or closed, is the mirror, the page is 0 or outside the list, or no activity of the package answers. After the page is open it removes the tasks that were on the session's virtual display BEFORE the launch (read just before it), so pages do not pile up (see chapter 8). It changes no phone setting, so there is nothing to restore.") + \
    p("<b>Calls, network and media (version 3).</b> The three readings are read-only and the PC asks for them only while the matching widget is on the desktop. For each of them the outcome byte is an " + c("OK") + " answer, not an " + c("ERRORE") + ", so the widget can say why it shows nothing: 0 ok, 1 not available (service missing, or the provider does not answer in 3 seconds), 2 permission denied to the shell user. With an outcome other than 0 every other field is zero or empty, and the PC refuses an answer where it is not. The values of mobile data and airplane mode are 0, 1 or 2 (unknown): a value the phone cannot read is 2, never an invented one; a value outside the list makes the whole reading fail. Strings are cut on the phone at a character boundary to at most 127 bytes (name), 47 (number), 99 (network), 63 (operator), 159 (title), 127 (artist) and 255 (package); the PC refuses (the reading fails) a longer string or one that is not UTF-8. Sources on the phone: the call log provider (read like the calendar, newest first, with a limit); the Wi-Fi, connectivity and telephony managers and the global settings " + c("airplane_mode_on") + " and " + c("mobile_data") + "; the media session manager (metadata, playback state, transport controls; with several sessions the one that plays, otherwise the first active). The possible commands come from the actions the playback state declares, and " + c("MEDIA_COMANDO") + " refuses a command whose bit is not set. <b>Fallback roads</b> are used only when the direct API fails, because the shell user is not always allowed to call it: " + c("dumpsys media_session") + " for the media (a cautious reader: if the text is not understood the outcome is 1; if the actions cannot be read, all three commands are offered) with " + c("cmd media_session dispatch") + " or " + c("input keyevent") + " for the commands, used only if that reading found a session; " + c("cmd wifi status") + " for the network name when the SSID is hidden (no location permission); " + c("dumpsys telephony.registry") + " for the mobile part. Each fallback command has a limit of 2 seconds and its output is capped at 2 MiB; each whole reading has a limit of 6 seconds, and if the previous one is still running the outcome is 1. Names, numbers, networks and titles are personal data: never in a log, never on disk, on the phone or on the PC. A version 2 jar does not know these types, but the greeting with version 3 refuses it first.") + \
    note("Type " + c("0x47") + " is reserved (freeing the encoder in long pauses: +0.07 s at resume; added only if a measurement asks). The sizes of video and audio packets are given in their chapters. Details of video, input, clipboard and panel are in chapters 8, 9 and 10; those of files, notifications and weather in chapters 11, 13 and 15.")

S4 = p("Every request that fails answers " + c("ERRORE") + " with one sentence. The PC turns it into a " + c("PhErrore") + ". The cases to know:", lead=True) + \
    table(["Case", "Result"], [
        ["Unknown type", c("ERRORE") + " “tipo sconosciuto”. The PC maps it to “old service version” (" + c("ph_componente_richiesta") + ")"],
        ["Malformed body", c("ERRORE") + " with the exception's message, or “malformed request” if it has none"],
        ["Different protocol version", c("ERRORE") + " on " + c("SALUTO")],
        ["Body longer than 1 MiB", "the PC ends the command thread and the component is dead; the phone's reader refuses it"],
        ["Answer after the PC gave up", "discarded by the PC; the PC already considers the component dead after a timeout"],
        ["Event type not registered on the PC", "ignored"],
        ["Unknown type on " + c("input"), "ignored: that channel has no answers, so there is no visible error"],
    ], "«TAB» — Errors") + \
    warn("A timed-out request kills the component on the PC side by design: an answer that came late would otherwise be taken for the answer to the next request. The link then goes to " + c("PERSO") + " and reconnects.")

CHAPTER = ("The protocol", [
    ("Channels", S1),
    ("Message format", S2),
    ("Message types", S3),
    ("Errors and malformed input", S4),
])
