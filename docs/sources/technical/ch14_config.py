from build import c, code, note, p, rif, table, term, ui, ul, warn

S1 = p("The piece " + c("src/config/") + " is pure logic: no graphics, no phone. It holds the folders, the file formats, the safe writing and the language "
       "tables. On the PC's disk YASDE writes only in two folders, following XDG, and never anything on the phone.", lead=True) + \
    table(["Folder", "Content"], [
        [c("~/.config/YASDE/") + " (or " + c("$XDG_CONFIG_HOME/YASDE") + ")", c("preferenze.toml") + " (preferences), " + c("telefoni.toml") + " (known phones), "
         + c("telefono-<serial>.toml") + " (one profile per phone), " + c("profilo-<serial>.toml") + " (measurements per phone; not written by the program today, see " + rif("The measurements file") + "), " + c("chiave.pem") + " (the PC's ADB key)"],
        [c("~/.cache/YASDE/") + " (or " + c("$XDG_CACHE_HOME/YASDE") + ")", "Wallpaper images " + c("sfondo-<n>.png") + " and, from the AppImage launcher, the image loader file (chapter 17)"],
    ], "«TAB» — The two folders on the PC") + ul([
    "An XDG variable that is empty or holds a non-absolute path is ignored, as XDG itself says. Folders are created with mode 0700; if the folder exists it must belong to the user "
    "(otherwise writing is refused), and if it is the user's but writable by group or others those permissions are removed.",
    "<b>Safe writing</b> (" + c("ph_cfg_scrivi_file") + "): a temporary file in the same folder (mode 0600, created exclusively), " + c("fsync") + ", " + c("rename") + " onto the final name, "
    + c("fsync") + " of the folder. An interruption leaves either the old file or the new one. A killed program may leave a " + c(".name.XXXXXX") + " temporary; startup removes those "
    "older than an hour (" + c("ph_cfg_pulisci_temporanei") + ", a value under 60 s counts as 60).",
    "<b>Known limits.</b> If a configuration file is a symbolic link, the rename replaces it with a regular file (the target is untouched). With two instances at once the last write wins, "
    "never a half-written file. A configuration file larger than " + c("PH_CFG_MAX_FILE") + " (1 MiB) is an error.",
    "<b>The serial number is hostile data</b> (it comes from the phone): in a file name it enters only cleaned (" + c("ph_cfg_seriale_pulito") + "): " + c("[A-Za-z0-9_-]") + " kept, every other byte becomes "
    + c("_") + ", at most 63 characters, and if anything was changed or cut a dash and an 8-digit FNV-1a hash of the original are appended, so " + c("AB:CD") + " and " + c("AB.CD") + " never share a file. "
    "Inside " + c("telefoni.toml") + " it stays as it is, because there it is a value and never a path.",
])

S3 = p("The format is a subset of TOML written from scratch (" + c("src/config/toml.c") + ", " + c("toml.h") + "): tables " + c("[name]") + ", repeated tables " + c("[[name]]") + " for lists, "
       "quoted strings (valid UTF-8 on one line), 64-bit integers, finite decimals, booleans, and comments with " + c("#") + ". Not supported: arrays, inline tables, multi-line strings, dates, "
       "hexadecimal integers. Limits: file " + c("PH_TOML_MAX_FILE") + " 4 MiB, line 8192 bytes, 1024 tables, 2048 entries per table.", lead=True) + \
    table(["Table.key", "Type and range", "Default"], [
        "<b>" + c("preferenze.toml") + "</b>",
        [c("[generale] lingua"), c("auto") + " | " + c("it") + " | " + c("en") + ": read and <b>ignored</b> (the app is English only)", c("auto")],
        [c("[generale] esc_indietro"), "true or false: Esc means Back on the phone", "true"],
        [c("[generale] avvisi"), "true or false: pop-up notices", "true"],
        [c("[generale] cartella_ricevuti"), "path; the starting receive folder, the phone's own receive folder wins when it exists", "empty"],
        [c("[generale] widget_nascosti"), "true or false: hide all widgets", "false"],
        [c("[generale] riapri_dove_eri"), "true or false: reopen windows where they were", "false"],
        [c("[shell] scala_automatica"), "true or false: shell scale from the system", "true"],
        [c("[shell] scala"), "0.5 to 4.0, used when the automatic scale is off", "1.0"],
        "<b>" + c("telefoni.toml") + "</b>: one " + c("[[telefono]]") + " per phone",
        [c("seriale") + ", " + c("nome") + ", " + c("indirizzo") + ", " + c("visto"), "serial (required, no duplicates), model name, last address as " + c("host:port") + " (IPv6 as " + c("[host]:port") + "), last time seen in Unix seconds", "—"],
        "<b>" + c("telefono-<serial>.toml") + "</b>: the profile of one phone",
        [c("[sfondo] tipo"), c("solido") + " | " + c("sfumato") + " | " + c("immagine"), c("sfumato")],
        [c("[sfondo] colore_solido"), "color " + c("#RRGGBB"), c("#1f2430")],
        [c("[sfondo] colore_da") + ", " + c("colore_a"), "gradient colors", c("#0e2638") + ", " + c("#1a625f")],
        [c("[sfondo] direzione"), c("diagonale") + " (135°) | " + c("verticale") + " (180°) | " + c("orizzontale") + " (90°) | " + c("diagonale-inversa") + " (45°)", c("diagonale")],
        [c("[sfondo] immagine"), "a file name in the cache (valid name: no " + c("/") + ", not starting with a dot, only " + c("[A-Za-z0-9._-]") + ", 1 to 127)", "empty"],
        [c("[sfondo] adattamento"), c("riempi") + " | " + c("adatta") + " | " + c("allunga") + " | " + c("centra"), c("riempi")],
        [c("[[icona]]") + " " + c("app") + ", " + c("colonna") + ", " + c("riga"), "desktop icon: package, 0 to 255 each; one icon per app, at most " + c("PH_CFG_MAX_ICONE") + " (256)", "—"],
        [c("[[widget]]") + " " + c("tipo"), c("orologio") + ", " + c("stato") + ", " + c("notifiche") + ", " + c("memoria") + ", " + c("calendario") + ", " + c("note") + ", " + c("meteo") + ", " + c("riproduzione") + ", "
         + c("calendario-mese") + ", " + c("sveglia") + ", " + c("batteria") + ", " + c("interruttori") + ", " + c("trova") + ", " + c("app-frequenti") + ", " + c("trasferimenti") + "; an unknown type drops the entry; at most " + c("PH_CFG_MAX_WIDGET") + " (32)", "—"],
        [c("[[widget]]") + " " + c("destra") + ", " + c("alto"), "pixels from the right and top edge, 0 to 16384", "16"],
        [c("[[widget]]") + " " + c("dimensione"), c("piccolo") + " | " + c("medio") + " | " + c("grande") + ": the old three-size level; it is still written (older versions read it) and it is the size of a profile that has no free size", c("medio")],
        [c("[[widget]]") + " " + c("larghezza") + ", " + c("altezza"), "free size in pixels: 0 or 48 to 2048 (another value, or a wrong type, becomes 0 without failing the load); written only when not 0. With 0 the size is the one equivalent to " + c("dimensione") + ". The desktop then holds the size inside the limits of the type", "0, 0"],
        [c("[[widget]]") + " " + c("stile") + ", " + c("opzione"), "graphic variant 0 to 15; style-dependent value 0 to 255", "0, 0"],
        [c("[audio] volume"), "0 to 100 (the mute is not saved)", "100"],
        [c("[cartelle] invio") + ", " + c("ricezione"), "last send folder (on the phone) and receive folder (on the PC), empty if none", "empty"],
        [c("[app] densita"), "0 (the phone's own) or 72 to 960 dpi: a fixed density chosen by the user", "0"],
        [c("[[fissata]]") + " " + c("app"), "pinned apps of the bar, in order, no duplicates, at most " + c("PH_CFG_MAX_FISSATE") + " (16)", "none"],
        [c("[[apertura]]") + " " + c("app") + ", " + c("volte") + ", " + c("ultima"), "opening counts for Frequent apps: package, 1 to " + c("PH_CFG_MAX_VOLTE") + ", last time in Unix seconds; at most 64 (the least used goes first)", "none"],
        [c("[meteo]") + " " + c("nome") + ", " + c("dettaglio") + ", " + c("lat_centesimi") + ", " + c("lon_centesimi"), "the city chosen by hand: name, detail (region, country), degrees × 100 as integers (−9000..9000, −18000..18000); the section is written only if a city is chosen", "none"],
        [c("[note]") + " " + c("testo"), "the Quick notes text, one per phone, typed by the user on the PC (never from the phone, never sent to it); the key is written only when the text is not empty. Valid UTF-8; new lines and tabs are allowed, no other ASCII control character (bytes below 0x20, or 0x7F); U+0080 to U+009F are not filtered. On reading: a " + c("\\r") + " is removed; a control character that is not allowed and arrives as an escape (" + c("\\b") + ", " + c("\\f") + ", " + c("\\u00XX") + ") makes the text empty; a value of another type makes it empty; a text longer than the field (" + c("PH_CFG_LUN_NOTE") + " = 2048 bytes with the terminator, so 2047 of text) is cut at a character boundary. On writing: a text with a control character that is not allowed is not written (the table is skipped, so it reads back empty), a valid one reads back identical. Invalid UTF-8 or a control character written raw in the file is a syntax error of the whole file, as for any string. The text is the user's data: never in a log or an error message", "empty"],
        [c("[[compatibilita]]") + " " + c("app"), "apps with the " + ui("Compatibility") + " switch on, at most " + c("PH_CFG_MAX_COMPAT") + " (128)", "none"],
        [c("[[finestra]]") + " " + c("app") + ", " + c("x") + ", " + c("y") + ", " + c("larghezza") + ", " + c("altezza"), "remembered window rectangle (one per app, at most 64); x and y −32768..32767, sizes 1..16384", "none"],
    ], "«TAB» — The keys of the three configuration files") + \
    p("<b>Defaults of a new phone.</b> With no file (or a file set aside) the desktop has three widgets, Clock, Phone status and Recent notifications, with no saved free size (so the size equivalent to medium), at 16 px from the right "
      "edge and 16, 120 and 240 px from the top in the profile. On the screen, while the profile is exactly this one, the desktop shows them one under the other from 16 px with " + c("PH_DSK_DISTANZA_WIDGET") + " (12 px) between them, "
      "according to their real heights (" + c("ph_dsk_predefiniti_alti") + "); the profile is rewritten only when the user moves or changes something. A valid file always has the last word: without any " + c("[[widget]]") + " there are no widgets.")

S4 = p("A damaged file must never crash the program or destroy data.", lead=True) + ul([
    "<b>Syntax error</b> (unclosed quote, huge number, null byte, invalid UTF-8, line too long, a text that is not UTF-8 in the file…): the load function returns -1 with a message "
    + c("riga N: …") + " (the message text is in Italian, like all internal messages) and the structure holds the defaults, never half a structure.",
    "<b>Unknown tables and keys</b> are ignored and not rewritten at the next save. A value of the wrong type, out of range, too long or with an unknown name returns that one field to its "
    "default, without an error; a list entry with invalid required fields (or a duplicate) is dropped.",
    "<b>Missing file:</b> defaults, return 0 (first run).",
    "<b>Unreadable file:</b> the " + c("*_carica") + " functions rename it to " + c("<name>.illeggibile") + " (then " + c(".illeggibile.1") + " … " + c(".9") + "; with all taken the oldest is replaced), return the defaults "
    "and return 1; the next save therefore destroys nothing. Results: 0 read or missing, 1 set aside, -1 a real error (path, permissions, failed rename). <b>After -1 nothing must be saved</b>, because the file was "
    "neither read nor set aside; the skeleton then uses defaults and never saves the preferences.",
    "The " + c("*_da_testo") + " functions do not touch the disk and return -1 with the line.",
    "<b>Wallpaper cleanup</b> (" + c("ph_cfg_pulisci_sfondi") + ", once at startup, before threads): reads every " + c("telefono-*.toml") + " and removes from the cache the files named exactly "
    + c("sfondo-<1-10 digits>.png") + " that are named by no profile. If even one profile cannot be read nothing is deleted (-2); only regular files of the user are touched, never folders or symlinks; a file "
    "modified less than 10 minutes ago stays (a choice in progress). Whoever saves a wallpaper writes the image and saves the profile that names it immediately after.",
    "<b>The city of the weather widget</b> is the one position datum allowed on disk, because the user chose it by hand. A name that is empty or too long, a detail that is too long, or coordinates out of "
    "range mean no city, without failing the load.",
])

S5 = p("A second per-phone file, " + c("profilo-<serial>.toml") + ", is handled by " + c("src/config/profilo.c") + " (" + c("profilo.h") + "): what was measured or observed on a phone, for choosing values next "
       "time and for a published table of tested phones. It never holds the serial number or an address; the model is the commercial name (" + c("Build.MODEL") + ") and never the name the user gave the device.", lead=True) + \
    table(["Table.key", "Content"], [
        [c("[profilo] versione") + ", " + c("modello") + ", " + c("api"), "format version (" + c("PH_PROF_VERSIONE") + " = 1), model, API level"],
        [c("[encoder] scelto") + ", " + c("ripiego") + ", " + c("repeat_previous_frame_after_ignorato"), "the encoder chosen, the fallback used if any, whether forced redraw is needed"],
        [c("[misure] max_flussi"), "streams in parallel without degradation, 1 to 64"],
        [c("[[sintomo]]") + ", " + c("[[workaround]]"), c("nome") + " (" + c("[A-Za-z0-9_.-]") + ", 1 to 31), " + c("nota") + " (at most 128, never goes into the table), " + c("data")],
    ], "«TAB» — Keys of the measurements file") + \
    p("A key that is absent means \"not measured\": no value is invented. A file of a future version (" + c("versione") + " greater than the program's) is not touched and not renamed: load returns -1 with an "
      "empty profile, and the program must not save. Model and encoder strings accept only " + c("[A-Za-z0-9 ._+()/,-]") + " (no colon, so no web addresses are formed).") + \
    warn("in this version no code outside " + c("src/config/") + " calls " + c("ph_profilo_*") + ": the module and its tests exist, but the file is not yet written or read by the program.", "Not connected.")

S6 = p("User-visible text in the code is written in Italian, in " + c("ph_t(\"…\")" ) + " calls, and the English comes from tables compiled into the executable. Since 24 November 2026 the app is "
       "<b>English only</b>: " + c("ph_lingua_inizializza") + " always sets English, whatever the preference in the file and whatever the PC's environment (an old " + c("it") + " or " + c("auto") + " is read and ignored).", lead=True) + ul([
    "<b>The functions</b> (" + c("src/config/lingua.h") + "): " + c("ph_t") + " (translation in the active language, never NULL, the pointer lives as long as the program; the Italian text itself when there is "
    "no translation or the language is Italian), " + c("PH_T_") + " (a marker for a literal placed in a table: it translates nothing), " + c("ph_tv") + " (translates a marked text later; not checked by the scan, "
    "only for " + c("PH_T_") + " texts), " + c("ph_lingua_componi") + " (placeholders), " + c("ph_lingua_traduzione_en") + " (for the test).",
    "<b>The argument of " + c("ph_t") + " is always a literal</b> (or adjacent literals). " + c("ph_t(variable)") + " is an error of the test.",
    "<b>Placeholders</b> are written " + c("{name}") + " (" + c("[A-Za-z_][A-Za-z0-9_]*") + "), not printf: a translation can reorder the words. A literal brace is " + c("{{") + " or " + c("}}") + ", and " + c("%") + " means nothing. "
    + c("ph_lingua_componi(buf, size, ph_t(\"…\"), \"name\", value, (const char *)NULL)") + " substitutes them (at most 16 pairs); the model is never a printf format; a NULL value is a caller error "
    "(empty string, return " + c("(size_t)-1") + "); a result that does not fit is cut on a UTF-8 character boundary.",
    "<b>The tables.</b> " + c("data/en/*.txt") + ": one line per text, " + c("\"italian\" = \"english\"") + ", a subset of TOML. Files: " + c("comune.txt") + ", " + c("config.txt") + ", " + c("collegamento.txt") + ", "
    + c("installa.txt") + ", and " + c("shell-<piece>.txt") + " for most shell pieces (there is none for the launcher or the fake backend; " + c("shell-comune.txt") + " holds all the mockup's texts). The texts of the phone (app names, notifications, error sentences "
    "of the component) arrive ready and are not translated.",
    "<b>The generator</b> (" + c("src/config/genera_traduzioni.c") + ") runs at build time under Meson, reads the files and writes a C source with the table sorted by " + c("strcmp") + " on the Italian text. "
    "A duplicate text (between files or inside one), an empty translation, different placeholders between the two languages, or a syntax error stops the build with the file and line. The list of "
    "files is read at configuration: a new file needs " + c("meson setup --reconfigure") + ". A duplicate key between two files stops the build of everything.",
    "<b>The test</b> (" + c("lingua") + ", " + c("src/config/prova_lingua.c") + ") scans every " + c("src/") + " source for " + c("ph_t(\"…\")") + " and " + c("PH_T_(\"…\")"
    ) + " calls (skipping comments, strings and characters, joining adjacent literals) and fails if a text has no English translation, if the placeholders differ, or if an argument is not a literal. Comments of "
    + c(".c") + " and " + c(".h") + " files are scanned too, so an example call in a comment must not be written.",
    "<b>The test proves itself:</b> the generator tests run with deliberately wrong files (" + c("src/config/finti/") + ") and must fail (" + c("should_fail") + "), and succeed with a good one, so the "
    "failures depend on the content. " + c("lingua_funzioni") + " tests the functions with a fake table (" + c("src/config/tabella_finta.c") + ").",
    "A text without a translation makes the " + c("lingua") + " test of " + c("tutto") + " fail even if its piece is not yet approved, because the scan reads all of " + c("src/") + ": the string and its translation are "
    "added together.",
])

CHAPTER = ("Configuration and language", [
    ("Files and folders on the PC", S1),
    ("The file formats and their keys", S3),
    ("Tolerant reading and set-aside files", S4),
    ("The measurements file", S5),
    ("Language and the English tables", S6),
])
