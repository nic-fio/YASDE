from build import c, note, p, rif, steps, table, ul, warn

S1 = p("Four common extensions, with the files each one touches. The examples follow the path of an existing feature: the Next alarm widget (message " + c("0x7c") + ", " + c("SVEGLIA") + ", the Italian word for alarm clock) "
       "goes through every layer. Its type " + c("0x7c") + " is taken: the whole range " + c("0x70") + "–" + c("0x7f") + " is fully assigned, so a <b>new</b> message of the same kind needs a free number, for example one of " + c("0x91") + "–" + c("0x9f") + " (already registered to " + c("Dati") + ") or " + c("0x86") + "–" + c("0x8d") + " (the " + c("Controlli") + " range). Contract files (" + c("docs/protocollo.md") + ", " + c("src/*/*.h") + ", " + c("telefono/contratto/") + ", " + c("src/shell/servizi.h") + ", " + c("src/shell/servizi_backend.h") + ", the root " + c("meson.build") + ") are changed by whoever "
       "coordinates the project, not by the piece's author (chapter 18): in a team, a piece's author reports what is missing.", lead=True)

S2 = p("Take a request that returns data, like the existing " + c("SVEGLIA") + " (0x7c) or " + c("BATTERIA") + " (0x7d), which you can read as models; a new one takes a free number such as 0x91. The message types are in ranges, one per area, and each range is served by one phone class.", lead=True) + \
    table(["Range", "Area", "Phone class (folder)"], [
        ["0x01–0x0f", "Generic: hello, OK, error, close, presence", c("Servizio") + " (" + c("telefono/base/") + ")"],
        ["0x40–0x4f", "Video", c("Video") + " (" + c("telefono/video/") + ")"],
        ["0x50–0x5f", "Input (and clipboard 0x58–0x5a)", c("Input") + ", " + c("Appunti")],
        ["0x60–0x6f", "Physical panel", c("Pannello") + " (" + c("telefono/pannello/") + ")"],
        ["0x70–0x7f", "Phone data (0x74–0x75 in " + c("telefono/archivio/") + ")", c("Dati") + " (" + c("telefono/dati/") + "), " + c("Archivio")],
        ["0x80–0x8f", "Quick switches", c("Controlli") + " (" + c("telefono/controlli/") + ")"],
        ["0x90–0x9f", "Approximate position", c("Dati")],
    ], "«TAB» — Message ranges of " + c("docs/protocollo.md")) + steps([
    "<b>Specify it</b> in " + c("docs/protocollo.md") + ": the type number inside the right range (§2), the direction, the request body and the OK body. Numbers are big endian, a string is a 2-byte length plus UTF-8, a message is a type byte, a 4-byte length (at most 1 MiB) and the body. "
    "Say what the phone touches, and its restore if it changes anything (the guardian rule).",
    "<b>Phone side.</b> Add the constant and the " + c("case") + " in the class that owns the range (" + c("telefono/dati/it/yasde/Dati.java") + ": the existing " + c("SVEGLIA = 0x7c") + " is the model), the encoding of the OK body in the helper class (" + c("CampiDati.java") + ") and the reading of the data in "
    "the class of hidden or platform calls (" + c("NascosteDati.java") + "). If the number is outside the ranges already registered with " + c("Registro.registraComandi") + ", register the new range in the class's " + c("registra") + " method. A thrown exception becomes an " + c("ERRORE") + " with its message. Source level is 8: no lambdas.",
    "<b>A new phone class</b> (a new area) goes in a new folder under " + c("telefono/") + " with a " + c("prove.sh") + ", and its name is added to the array " + c("PEZZI") + " in " + c("telefono/base/it/yasde/Servizio.java") + ", which registers each class by reflection at startup; "
    "also add it to the default list in " + c("telefono/costruisci.sh") + ".",
    "<b>PC side, the piece</b> (" + c("src/dati/") + "): add " + c("PH_DT_<NAME>") + " to the enum in " + c("src/dati/interno.h") + ", the API and its structure in " + c("src/dati/dati.h") + ", and the function in " + c("src/dati/nuovi.c") + " built on " + c("ph_dati_chiedi") + ". "
    "Read the answer with the bounds-checked reader, accept only a body of exactly the expected length, and return -1 with a fixed message otherwise: the body is hostile data. Add a free function for any allocated field.",
    "<b>Tests on both sides.</b> PC: the fake component (" + c("src/dati/prova/finto.c") + ") answers the new type, and " + c("src/dati/prova/prova_dati.c") + " proves the good answer and every refusal. Phone: a JVM test class (a " + c(".java.txt") + " file in the piece's test folder) and, for the real phone, a script in " + c("telefono/dati/prove/")
    + " (chapter 16). Verify that each test fails if the code is removed.",
    "<b>The services</b> (if the shell needs it): " + c("ph_srv_tel_<name>") + " and its callback type in " + c("src/shell/servizi.h") + ", the entry " + c("tel_<name>") + " in " + c("PhServiziBackend") + " (" + c("servizi_backend.h") + "), the wrapper that makes a job in " + c("src/shell/comune/servizi.c") + ", the function the backend calls to deliver the result, " + c("ph_servizi_lavoro_<name>") + " (declared in " + c("servizi_backend.h") + ", defined in " + c("servizi.c") + ", called for the alarm by " + c("sv_extra.c") + "), the real implementation in the "
    "skeleton (" + c("sv_tel_sveglia") + " in " + c("src/shell/ossatura/sv_extra.c") + ", registered in the table in " + c("servizi_veri.c") + " and declared in " + c("sv_interno.h") + "), and the fake one in " + c("src/shell/finto/finto.c") + " (and a command in its small window if the state must change).",
    "<b>Document</b> the change in the manual chapter that describes it, and in " + c("src/shell/CONTRATTI.md") + " if it changes a rule.",
])

S3 = p("A widget is drawn by the desktop piece (" + c("src/shell/desktop/") + ") and shares the look of the others: same card, three sizes, translucent background.", lead=True) + steps([
    "<b>The type in the profile.</b> Add a value at the <b>end</b> of " + c("PhCfgWidgetTipo") + " in " + c("src/config/config.h") + " (values saved before stay valid) and its name in the " + c("NOMI_WIDGET") + " table in " + c("src/config/config.c") + ", in the same position. "
    "Extend the config tests (reading, writing, the unknown-type rule).",
    "<b>The catalog.</b> Add an entry to " + c("PH_DSK_CATALOGO") + " in " + c("src/shell/desktop/widget.c") + ": the type, the name and the description, both inside " + c("PH_T_") + ". A widget that is not offered yet is left out with a compile-time switch, as " + c("PH_DSK_TROVA_OFFERTO") + " does for the find-my-phone widget.",
    "<b>The drawing and data.</b> Add a table of type " + c("PhDskWxVt") + " (" + c("src/shell/desktop/interno.h") + ") in a " + c("wx_*.c") + " file (start, stop, update, how often to ask) and map the type to it in " + c("ph_dsk_wx_vt") + " in " + c("src/shell/desktop/wx.c") + ". Ask the phone only while the widget is on the desktop, "
    "using " + c("ph_dsk_wx_puo_chiedere") + " (for example the next alarm every 5 minutes, the battery every minute, the calendar every 15 minutes), and never put titles, file names or other personal data on disk or in logs.",
    "<b>The texts.</b> Every new text is in " + c("ph_t") + " or " + c("PH_T_") + ", and its translation is added to " + c("data/en/shell-desktop.txt") + " in the same change, after checking with " + c("grep") + " that the key does not exist in any file of " + c("data/en/") + " (a duplicate key between two files stops the build).",
    "<b>The CSS</b> in " + c("src/shell/desktop/desktop.css") + ", classes " + c("ph-desktop-*") + ". <b>The fake</b>: give " + c("src/shell/finto/") + " the new data and, in " + c("src/shell/desktop/demo_hook.c") + ", the commands to show the empty, error and \"not available\" states.",
    "<b>Tests.</b> Pure logic in " + c("logica_wx.c") + " and " + c("src/shell/desktop/prova_desktop.c") + "; photograph the demo (" + c("strumenti/foto-demo.sh") + ") and compare it with the mockup.",
])

S4 = p("An interface entry is any new text: a label, a hint, a message. The program is English only, so each Italian key in the code needs its English value, or the build's test fails.", lead=True) + steps([
    "Write the text in the code with the Italian literal in " + c("ph_t(\"…\")") + " (or " + c("PH_T_(\"…\")") + " for a literal placed in a table, translated later with " + c("ph_tv") + "). The argument is always a literal. Placeholders are " + c("{name}") + ", composed with " + c("ph_lingua_componi") + ", never with printf.",
    "Add the line " + c("\"italian\" = \"english\"") + " to the piece's file " + c("data/en/shell-<piece>.txt") + " (create it for a new piece: the list of files is read at configuration, so run " + c("meson setup --reconfigure") + "). Same placeholders in both. Check with " + c("grep") + " for an existing key: the shell's mockup texts are all already in "
    + c("data/en/shell-comune.txt") + " and must be reused as they are.",
    "Run " + c("strumenti/prova.sh <piece>") + " and the " + c("lingua") + " test: it fails on a missing translation, different placeholders or a non-literal argument. Phone texts are not translated and are not written in the table.",
    "Accessibility: the label goes through " + c("ph_shell_etichetta") + " with the text and the hint. Do not write example " + c("ph_t") + " calls in comments, because the scan reads comments too.",
])

S5 = p("A new piece is one folder, and one agent.", lead=True) + steps([
    "Create " + c("src/<piece>/") + " with a " + c("meson.build") + " that defines " + c("<piece>_dep") + " (a dependency with its static library) and its " + c("test(...)") + " entries, plus the header that is the piece's contract.",
    "Add the name to the list " + c("pezzi") + " in the root " + c("meson.build") + " and, only when the reviewer approves it, to " + c("pezzi_pronti") + ". (A shell piece is added to the " + c("shell_pezzi") + " list in " + c("src/shell/meson.build") + " and to " + c("shell_pronti") + " in the root; its entry point is declared in "
    + c("src/shell/pezzi.h") + " and called by " + c("ph_ossatura_installa_pezzi") + ", with a flag in the header generated at configuration time.)",
    "In its tests replace what the piece depends on with a fake in its own folder, so " + c("-Dpezzo=<piece>") + " builds without the others.",
    "Write the tests: logic without screen, every error branch, and for threads the concurrency tests (chapter 16). Close in parallel, callbacks, waits and midway failures follow the rules in " + rif("Rules born from reviews") + ".",
    "Run " + c("strumenti/prova.sh <piece>") + " and, when it is finished, " + c("strumenti/prova.sh veloce") + ".",
    "Add the piece to the source map of the manual (chapter 20) and to the glossary if it brings new terms; the generator stops if a source file is missing from the map.",
])

CHAPTER = ("Extending YASDE", [
    ("Before you start", S1),
    ("Adding a protocol message on both sides", S2),
    ("Adding a widget", S3),
    ("Adding an interface entry with its translation", S4),
    ("Adding a piece", S5),
])
