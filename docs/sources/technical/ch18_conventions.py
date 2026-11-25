from build import c, note, p, rif, table, ul, warn

S1 = p("The project's rules come from " + c("CLAUDE.md") + " and from the contracts (" + c("src/*/*.h") + ", " + c("docs/protocollo.md") + ", " + c("src/shell/CONTRATTI.md") + "). "
       "They were born from reviews, and a reviewer checks them first.", lead=True) + ul([
    "<b>Everything is new code.</b> No third-party code is copied; Android's source is read as documentation only.",
    "<b>PC side:</b> C, Meson, Vulkan. <b>Phone side:</b> minimal Java launched with " + c("app_process") + "; only pure Android APIs, no dependency on a maker.",
    "<b>Nothing installed on the phone, no root.</b> Every change made to the phone has its restore.",
    "<b>Only what the specification and the mockup say.</b> No features beyond them.",
    "<b>A piece is one folder, one agent.</b> Whoever writes a piece touches only its folder. Contract headers, " + c("docs/protocollo.md") + ", " + c("telefono/contratto/") + ", the root " + c("meson.build") + " and " + c("strumenti/") + " are not modified by a piece: "
    "a wrong or incomplete contract is reported and the rest goes on.",
    "<b>No personal data in the repository</b> (it is public): no serial numbers, addresses, network names or person names in files; fake values in tests (for example " + c("R5CT0000000") + ").",
    "<b>Language:</b> names, comments, messages and documents are Italian in the code; the manuals and the public documents in the package are English (" + rif("Code language and user language") + ").",
])

S2 = p("The source is C17, compiled with warnings as errors, and tests run with AddressSanitizer and UndefinedBehaviorSanitizer (chapter 16). A piece is finished only if " + c("strumenti/prova.sh veloce") + " (everything together with " + c("-O3") + ") also passes, "
       "because optimization shows warnings the test build does not.", lead=True) + ul([
    "<b>Names.</b> Public functions are prefixed " + c("ph_") + " and the piece (" + c("ph_fin_*") + " for windows, " + c("ph_cfg_*") + " for configuration, " + c("ph_tr_*") + " for the queue, " + c("ph_srv_*") + " for the shell's services). Types are " + c("Ph…")
    + " and constants " + c("PH_…") + ". Shell classes are prefixed " + c("ph-<piece>-") + ".",
    "<b>Errors.</b> Every return value is checked. A function that can fail takes a " + c("PhErrore *") + " (which may be NULL; it holds a sentence of up to 256 bytes saying what the function was doing) "
    "(" + c("ph_errore") + " writes it, " + c("ph_errore_contesto") + " prepends a context). The return convention is the same across the project: <code>int</code> 0 for success and -1 for an error; a pointer is NULL on error. "
    "No function calls " + c("exit") + " or " + c("abort") + " for an error that depends on the phone or the network.",
    "<b>Hostile data.</b> Nothing that comes from the phone or the network may make the program read or write outside a buffer. Indexes into arrays that arrive through the services are checked against the length given; no fixed-size buffer is filled without a limit "
    "(" + c("g_strlcpy") + ", " + c("snprintf") + "). Text shown is already safe (valid UTF-8, no control characters).",
    "<b>Contracts grow at the end.</b> Structures of the contracts (handlers, requests, data) are always initialized by field name (" + c("{.io = x, .apri_app = f}") + "), never by position, because adding a field at the end would make a positional initializer an error (" + c("missing-field-initializers") + ") for everyone.",
    "<b>Includes.</b> No piece includes the internal header of another piece. GTK appears only under " + c("src/shell/") + ", FFmpeg only in " + c("src/video/") + ". The shell's pieces include none of " + c("collegamento.h") + ", " + c("dati.h") + ", " + c("componente.h") + ", " + c("decodifica.h") + ", "
    + c("audio.h") + ", " + c("trasferimenti.h") + ", " + c("file.h") + ", " + c("adb.h") + "; only the skeleton does, inside the real services.",
    "<b>Comments</b> explain why, in Italian. Contracts say what a callback may call and what a function touched when it returns an error (usually nothing).",
    "<b>No commits by pieces.</b> Whoever writes a piece does not run " + c("git commit"),
])

S3 = p("Defects found in earlier reviews, written so as not to be repeated. Each is checked by the reviewer.", lead=True) + table(["Rule", "What it means in the code"], [
    ["<b>Closing in parallel</b>", "Every public function that uses an object must tolerate the object's " + c("…_chiudi") + " called by another thread at the same moment: either it finishes first, or it returns an error; never use of freed memory. A \"is it closed?\" check "
     "made and then dropped (lock released) does not protect: a use counter that the close waits for is needed."],
    ["<b>Callbacks</b>", "After a " + c("…_chiudi") + " returns, no callback is running and none will arrive. Never call a callback while holding a lock that the receiver may want to take. The contract says what a callback may call (for example inside the queue's notice, "
     + c("ph_tr_chiudi") + " is forbidden)."],
    ["<b>Waits</b>", "No wait without a limit, except where the contract says so. No lock held during an external command, a network write or a pause."],
    ["<b>Failing midway</b>", "If an operation of several steps fails midway, what was already done is undone; when a function returns an error, the contract says what was touched (normally nothing)."],
    ["<b>Phone</b>", "The restore is registered BEFORE the change and removed only AFTER the change was really undone; if " + c("Guardiano.imposta") + " returns false the change is not made. A failed action that would leave the phone in an abnormal state (screen off, volume changed) is retried, "
     "not abandoned. A thread that dies of an unexpected " + c("Throwable") + " must leave the phone as it was."],
    ["<b>Tests</b>", "A test fails if the code it proves is removed (verified); no sleeping to give another thread time (wait for a signal with a deadline); every new error branch has a test; an intermittent failure is captured and explained, never ignored or hidden by a wider tolerance."],
    ["<b>Real phone tests</b>", "The turn: " + c("rmdir") + " only if the own " + c("mkdir") + " succeeded in this run; if it fails the script exits. Never wake, unlock or reconnect the phone. A test touches only what it created, never a wildcard, and checks beforehand, read-only, that no component or guardian file of another run is there."],
    ["<b>Whole builds</b>", c("tutto") + " and " + c("veloce") + " run only when the work is finished, never with files half written: the others compile your tree. Never " + c("rm") + " in " + c("data/") + " or in folders that are not yours."],
    ["<b>Reports</b>", "Say what you did NOT verify. Never call clean what you did not see pass."],
], "«TAB» — The rules of «Defects already seen» in " + c("CLAUDE.md"))

S4 = p("Rules specific to the GTK shell (" + c("src/shell/CONTRATTI.md") + "), in addition to the above.", lead=True) + ul([
    "One GTK thread; it never blocks; the only way from another thread is " + c("ph_shell_consegna") + " (" + rif("Threads and delivery to the GTK thread") + "). " + c("g_idle_add") + " and similar are not scattered around the pieces.",
    "Whoever hands a pointer to a widget to another thread keeps a " + c("g_object_ref") + " until delivery. Whoever sends a lot (frames) coalesces: at most one delivery in flight per stream, the last datum wins.",
    "Pieces free their things when the root is destroyed, with all the " + c("*_smetti") + " functions, and keep their own reference to the widgets as long as listeners exist (" + rif("The skeleton") + ").",
    "Every automatic decision of the program (pause, reduced rate, resumed stream, fallback from DMA-BUF to memory) is written with " + c("ph_shell_decisione") + " with its cause and the number that caused it; never personal data. Things the user asks for are not automatic decisions.",
    "No " + c("GtkStyleContext") + " per widget and no inline CSS (except computed colors); GTK CSS has no " + c("gap") + ", " + c("max-width") + " or " + c("position") + ".",
    "Everything clickable is a real control reachable with Tab.",
    "Privacy in logs and disk: no serial numbers, addresses, network names or person names; nothing about calendar event titles, file names, phone location, pairing code. The pairing code, addresses and ports are never written, not even in an error message.",
])

S5 = p("The Java side follows rules of its own (chapter 6): source level 8 (no lambdas or method references: anonymous classes), pure Android APIs, hidden ones only by reflection and gathered in one class per folder with a startup self-check that says which are missing; "
       "every channel runs on its own thread and catches everything (even " + c("Error") + "), so a broken channel does not take down the service; every change to the phone registers its restore command in the guardian (" + c("Custode") + "), a separate shell process that "
       "executes it if the service dies suddenly, and that deletes the jar; restore commands are self-contained shell lines with no environment variables of the service and no nested quotes; the user's value wins, and a setting is restored only if it still has the value YASDE put there.", lead=True)

S6 = p("Two languages live in the code and they never mix.", lead=True) + table(["Language", "Where", "Examples"], [
    ["<b>Italian</b>", "Identifiers, comments, internal messages (" + c("PhErrore") + " sentences, stderr lines), file names, folders, configuration keys, protocol names, documents in the repository (" + c("CLAUDE.md") + ", " + c("PIANO.md") + ", " + c("CONTRATTI.md") + ", " + c("COMPITO.md") + ")",
     c("ph_fin_apri") + ", " + c("telefoni.toml") + ", " + c("[[fissata]]") + ", " + c("PH_SRV_NON_ABBINATO")],
    ["<b>English</b>", "Everything the user can read: interface texts (through " + c("data/en/") + "), the public documents in the package, the two manuals", "the window titles, buttons, notices; " + c("README.txt") + ", " + c("SOURCE-OFFER.txt")],
], "«TAB» — Which language where") + ul([
    "Texts for the user are written in Italian in " + c("ph_t(\"…\")") + " and translated by a table (chapter 14); the program always runs in English. The Italian text is the key: a text without translation fails the build's test, so none can reach the user in Italian.",
    "Because the code is Italian, the manuals keep identifiers as they are and explain Italian words the first time (for example " + c("ossatura") + ", the skeleton). The check of the manuals rejects Italian sentences in running text, outside code and preformatted blocks.",
    "Texts of the phone (app names, notifications, error sentences of the component) arrive ready, in the phone's language, and are not translated.",
])

CHAPTER = ("Conventions", [
    ("Principles of the project", S1),
    ("C and the common code", S2),
    ("Rules born from reviews", S3),
    ("Rules of the shell", S4),
    ("Rules of the phone side", S5),
    ("Code language and user language", S6),
])
