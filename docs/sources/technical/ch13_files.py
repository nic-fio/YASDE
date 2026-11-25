from build import arrow, box, c, fig, note, p, rif, table, text, ui, ul, warn, zone

S1 = p("Files move over the ADB connection with the " + c("sync:") + " service (chapter 4). Three pieces share the work, from the bottom up: "
       + c("src/file/") + " (copy and listing, no queue), " + c("src/trasferimenti/") + " (the queue and the APK installer, no graphics) and "
       + c("src/shell/trasferimento/") + " (panels, navigator, drag and drop). The shell reaches them only through " + c("ph_srv_tr_*") + " and "
       + c("ph_srv_apk_installa") + " of the services.", lead=True) + \
    table(["Function (" + c("src/file/file.h") + ")", "Behavior"], [
        [c("ph_file_invia"), "PC to phone. The remote file is created or replaced (choosing a free name is the caller's job); the local modification time is kept; "
         "if cancelled or failed midway the partial file on the phone is removed."],
        [c("ph_file_ricevi"), "Phone to PC. Writes " + c("<local>.parziale") + " created exclusively (if it exists: " + c("PH_FILE_ESISTE") + ", nothing touched) and renames at the end; "
         "never replaces a user's file, except an empty regular file the caller placed as a name reservation. The file gets the phone's modification time. "
         "Cancelled or failed: the partial file is deleted."],
        [c("ph_file_elenca"), "A folder of the phone without " + c(".") + " and " + c("..") + ", sorted folders first, then files, by name ignoring case. Hidden files are included; whoever displays filters them."],
        [c("ph_file_info"), "One path of the phone."],
    ], "«TAB» — The copy and listing functions") + \
    p("Results are " + c("PhFileEsito") + ": " + c("PH_FILE_RIUSCITO") + ", " + c("PH_FILE_ERRORE") + ", " + c("PH_FILE_ANNULLATO") + ", " + c("PH_FILE_NON_TROVATO") + ", "
      + c("PH_FILE_NON_CONSENTITO") + " (Android denies access) and " + c("PH_FILE_ESISTE") + ". Every operation opens its own " + c("sync:") + " channel, so several can run at once from "
      "different threads. A transfer that sees no data for " + c("PH_FILE_ATTESA_MS") + " (30 s) fails; reads proceed in slices of " + c("PH_FILE_FETTA_MS") + " (200 ms) so that "
      "cancellation is noticed quickly.")

QUEUE = fig(
    box(20, 40, 150, 54, "Panel / drop", "GTK thread", "navy")
    + box(210, 40, 170, 54, "ph_srv_tr_accoda", "services, GTK thread", "blue")
    + box(420, 40, 170, 54, "ph_tr_accoda", "queue (one worker)", "blue")
    + box(630, 40, 240, 54, "ph_file_invia / ph_file_ricevi", "sync: channel to the phone", "dark")
    + arrow(172, 67, 208, 67) + arrow(382, 67, 418, 67) + arrow(592, 67, 628, 67)
    + box(420, 130, 170, 54, "PhTrAvviso", "from the worker thread", "green")
    + box(210, 130, 170, 54, "Snapshot + event", "PH_SRV_EV_TRASFERIMENTI", "green")
    + box(20, 130, 150, 54, "Panels update", "rows in place", "navy")
    + arrow(505, 96, 505, 128) + arrow(418, 157, 382, 157) + arrow(208, 157, 172, 157),
    900, 200, "«FIG» — The path of a transfer, from the panel to the phone and back as an event")

S2 = p("The queue (" + c("src/trasferimenti/trasferimenti.h") + ") has one worker thread and does one transfer at a time, in order. Its functions may be called from any "
       "thread; only " + c("ph_tr_chiudi") + " must be called once, with no other thread using the queue.", lead=True) + QUEUE + ul([
    "<b>Adding</b> (" + c("ph_tr_accoda") + "): does not block and does not talk to the phone. Send: the origin is a regular file of the PC and the destination folder is any "
    "absolute path on the phone (" + c("/sdcard") + " means " + c("/storage/emulated/0") + "; the phone creates the folder if missing). Receive: the origin is a file of the phone and "
    "the PC folder must exist. For N dropped files the call is made N times.",
    "<b>States</b> (" + c("PhTrStato") + "): waiting, in progress, completed, failed (the error is the sentence to show), cancelled. Ids start at 1, grow and are never reused.",
    "<b>Free name rule</b> (never overwrite): if " + c("folder/name") + " exists, " + c("name (1).ext") + ", " + c("name (2).ext") + " … until one is free. The extension is from the last dot, only "
    "if the dot is not the first character, at least one and at most 8 visible characters follow; so " + c(".bashrc") + " and " + c("Makefile") + " have none. A compression extension after "
    + c(".tar") + " makes a double extension (" + c("a.tar.gz") + " becomes " + c("a (1).tar.gz") + "). The number goes before the extension; a name over 255 bytes is shortened "
    "before the number without splitting a UTF-8 character.",
    "<b>On the PC</b> the name is reserved at start with an exclusive creation (" + c("O_EXCL") + ") of an empty file that the received file replaces. A name is free only if both the "
    "destination and " + c("<dest>.parziale") + " are (so a name has at most 255 − 9 bytes); if one appears in the meantime " + c("ph_file_ricevi") + " answers "
    + c("PH_FILE_ESISTE") + " and the queue retries with the next number, up to 20 times. On the phone the comparison ignores case, and two entries never choose the same "
    "destination even if neither has started; every entry shows a provisional name that at start can only go up in number.",
    "<b>Cancel</b> (" + c("ph_tr_annulla") + ", " + c("ph_tr_annulla_tutto") + "): a waiting entry becomes cancelled at once and will not start; one in progress stops copying, "
    "the partial file disappears, and it becomes cancelled when the copy stops (if the copy had already finished it may be completed instead: look at the final state).",
    "<b>Notices</b> (" + c("PhTrAvviso") + "): always from the worker thread, at every state change and, while copying, at most about 10 per second per entry; the final state of "
    "every entry always arrives. What the caller does (add, cancel a waiting entry, empty) generates no notice. A notice with a null entry means the queue stopped.",
    "<b>Lost connection:</b> the entry in progress fails with its sentence, the others stay waiting, and the queue stops (" + c("ph_tr_coda_ferma") + " true before the notice). "
    "It stays stopped until the caller calls " + c("ph_tr_riprendi") + " or " + c("ph_tr_svuota") + ". In the real services, a new session resumes the waiting entries "
    "and records it with " + c("ph_shell_decisione") + " (" + c("sv_tr_sessione_nuova") + " in " + c("src/shell/ossatura/sv_tr.c") + ").",
    "<b>Media scanner:</b> after a <b>sent</b> file (not after a received one) the phone is told that a new file exists with a fixed command built by " + c("ph_tr_comando_scanner") + ": the path is "
    "percent-encoded, then put between single quotes, so no character of a name can leave the quotes.",
    "<b>Shutdown:</b> " + c("ph_tr_chiudi") + " cancels the transfer in progress (the partial file disappears), drops the remaining entries, and after it returns the connection can be closed.",
])

S3 = p("The shell piece (" + c("src/shell/trasferimento/") + ") has two panels, send and receive, opened by the bar buttons (" + c("ph_shell_trasferimento_apri") + "). A dot on a bar button "
       "shows that a queue of that direction has work.", lead=True) + ul([
    "<b>Send.</b> A drop zone, a button to choose files from the PC, and the destination folder on the phone with a change button. "
    "A " + c("GtkDropTarget") + " of " + c("GDK_TYPE_FILE_LIST") + " on the root accepts files dropped anywhere in the window (the " + c("PH_STRATO_RILASCIO") + " layer shows the "
    "drop box); non-local files are skipped, and folders and unreadable files are not sent, with a notice.",
    "<b>Receive.</b> The phone's folder listing with multiple selection that stays when changing folder, a search of files and folders, and the destination folder on the PC.",
    "<b>Destinations.</b> Send folder: the profile's " + c("cartella_invio") + ", otherwise " + c("/storage/emulated/0/Download") + ". Receive folder: the first valid of the profile's "
    + c("cartella_ricezione") + ", the preference " + c("cartella_ricevuti") + ", the user's Download folder, the home folder, and finally " + c("/") + ". The last chosen folder is saved in the profile.",
    "<b>Queue rows.</b> At most " + c("PH_TRF_ELENCO_MAX") + " (12) rows per direction: in progress first, then waiting in queue order, then the latest finished; if the active ones do not fit, "
    "a line says how many more are waiting. Each row has a progress bar, a cross to cancel that entry and a button that cancels the whole queue of that direction. Rows update in place so "
    "scrolling and focus stay.",
    "<b>After a transfer</b> only a notice appears; no panel opens.",
    "<b>Recent transfers widget</b> (desktop): the last completed ones of the session, kept in memory by the common piece; filenames are never written to disk or logs.",
])

S4 = p("One navigator (" + c("PhNav") + " in " + c("src/shell/trasferimento/navigatore.c") + ") serves three lists.", lead=True) + table(
    ["Mode (" + c("PhNavModo") + ")", "What it is"], [
        [c("PH_NAV_TEL_CARTELLE"), "Folder chooser of the phone inside the send panel: Up, a typeable path, a folder search, the list of subfolders, New folder, Use this folder"],
        [c("PH_NAV_TEL_FILE"), "Receive list: Up, path, search of files and folders, multiple selection kept across folders"],
        [c("PH_NAV_PC_CARTELLE"), "Folder chooser of the PC: like the first, without search; the path is shown and also typeable; read with GIO, asynchronously"],
    ], "«TAB» — The three modes of the navigator") + ul([
    "<b>Paths.</b> " + c("ph_trf_normalizza") + " cleans what the user types: spaces trimmed, a leading " + c("/") + " added, " + c("/sdcard") + " (any case) becomes the internal storage, " + c("//") + " and "
    + c("/.") + " removed, " + c("..") + " resolved and never above " + c("/") + ", trailing slashes removed. It rejects invalid UTF-8, control characters, and paths longer than "
    + c("PH_TRF_PERCORSO_MAX") + " (4096). For the PC, " + c("~") + " is the home folder and relative paths start from the shown folder; a missing folder falls back to home.",
    "<b>Pages.</b> A list shows " + c("PH_TRF_PAGINA") + " (200) entries and then a button with how many more; hidden entries (name starting with a dot, or any hidden part of a search result's path) are filtered.",
    "<b>Texts.</b> " + c("ph_trf_testo_sicuro") + " makes names displayable: valid UTF-8, control and invisible direction characters replaced, a length limit with an ellipsis. Sizes are shown as "
    "B, KB, MB, GB with one decimal from 1 MiB (" + c("ph_trf_dimensione") + "), computed in floating point, rounded to tenths and printed from an integer count of tenths, so the decimal separator follows the language of the app, never the language of the PC.",
    "<b>Lifecycle.</b> The navigator is reference counted because GIO requests always return, even after it is closed; the owner frees it with " + c("ph_nav_distruggi") + ", which cancels everything. "
    "A cancelled services request never calls back.",
    "The phone's folders are read by the real services with " + c("ph_file_elenca") + " on the " + c("p_lavori") + " pool; searches are run on the phone's own search channel and can be cancelled. Creating a "
    "folder uses a shell command built by " + c("ph_ossatura_comando_mkdir") + " with the path in single quotes (a quote inside becomes " + c("'\\''") + ").",
])

S5 = p("A dropped " + c(".apk") + " is not sent as a file. The shell asks for confirmation (" + c("ph_trf_apk_chiedi") + " in " + c("src/shell/trasferimento/apk.c") + ", a non-blocking "
       + c("GtkAlertDialog") + " with Cancel and " + ui("Install the app") + "), then installs it with " + c("ph_srv_apk_installa") + ", which runs " + c("ph_inst_installa") + " from "
       + c("src/trasferimenti/installa.c") + " on the " + c("p_installa") + " pool (one at a time). The install row appears at the top of the send list, with progress and a cross.", lead=True) + ul([
    "<b>Recognition</b> (" + c("ph_inst_riconosci") + "): an APK is a file whose name ends in " + c(".apk") + " (any case) and whose content is a zip with an " + c("AndroidManifest.xml") + " entry in the "
    "central directory. Only the tail and the central directory are read (at most 32 MiB); zip64 files count as not APK. Every length read from the file is checked.",
    "<b>Steps</b> (" + c("ph_inst_installa") + "): APK check, copy with " + c("ph_file_invia") + " to " + c("/data/local/tmp/yasde-apk-<8 hex digits>.apk") + " (a random name from "
    + c("/dev/urandom") + ", new at every call), " + c("pm install -r") + ", cleanup of the temporary file. Limits: " + c("pm install") + " 120 s, " + c("pm uninstall") + " 30 s, cleanup 5 s.",
    "<b>The temporary file is always deleted</b> after the copy was attempted: on success, refusal, error, cancel or lost connection. If the deletion cannot be confirmed, "
    + c("temporaneo_rimasto") + " is true and the message says it will be removed at the next connection.",
    "<b>Cleanup at connection</b> (" + c("ph_inst_pulisci") + "): the real services of the skeleton call it whenever they get a new session (" + c("src/shell/ossatura/servizi_veri.c") + "), before any install: " + c("rm -f /data/local/tmp/yasde-apk-*.apk") + " "
    "(a fixed line). It cannot match the component's own files, which are jars and " + c("yasde-custode-*") + ".",
    "<b>Refusals in plain words.</b> " + c("ph_inst_analizza") + " is a pure function that turns the output of " + c("pm") + " into a " + c("PhInstRisultato") + ": outcome, a " + c("PhInstMotivo") + " (already installed, "
    "different signature, older version, no space, invalid APK, old Android, wrong architecture, blocked, verification, test-only, cancelled on the phone, system app, not installed, too old target, split "
    "missing, other), the " + c("INSTALL_FAILED_*") + " code and a translated sentence with no PC path. " + c("pm") + " exits with 0 even when it refuses, so the exit code does not count, and "
    + c("Success") + " must be a whole line: an unrecognized answer is an error, never a success by chance.",
    "<b>The install is complete only if the outcome is</b> " + c("PH_INST_RIUSCITO") + "; the one exception is a " + c("pm") + " that does not answer in time (error, and the message says it is unknown whether it was installed).",
    "<b>Uninstall</b> (" + c("ph_inst_disinstalla") + ") runs " + c("pm uninstall <package>") + " after " + c("ph_inst_nome_pacchetto_valido") + " (1 to 255 characters of letters, digits, " + c("_") + " and " + c(".") + ", "
    "first a letter: the only check that lets a name into a command line). It removes the app's data too, and the confirmation the shell shows must say so. If " + c("pm") + " refuses a system app the message says it.",
])

S6 = p("What can be left behind, and who removes it.", lead=True) + table(["Where", "What", "Who cleans it"], [
    ["Phone, " + c("/data/local/tmp") , c("yasde-apk-<hex>.apk"), "Always right after the copy; otherwise " + c("ph_inst_pulisci") + " at the next connection"],
    ["PC, next to the destination", c("<name>.parziale") + " (receive in progress)", "Removed on cancel or failure; never overwrites a user's file"],
    ["PC, destination", "An empty reserved file", "Replaced by the received file; removed if the transfer does not start, when it is still the same file (same device and inode)"],
    ["PC, configuration and cache folders", c(".name.XXXXXX") + " temporaries of a safe write", c("ph_cfg_pulisci_temporanei(3600)") + " at startup removes those older than an hour"],
    ["PC, cache", c("sfondo-<n>.png") + " wallpaper images that are named by no profile", c("ph_cfg_pulisci_sfondi") + " at startup (see " + rif("Files and folders on the PC") + ")"],
], "«TAB» — Temporary files and their cleanup")

CHAPTER = ("Files and transfers", [
    ("The file layer", S1),
    ("The transfer queue", S2),
    ("The panels", S3),
    ("The folder navigator", S4),
    ("Installing and uninstalling APKs", S5),
    ("Temporary files", S6),
])
