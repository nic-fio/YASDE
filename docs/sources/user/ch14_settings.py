# Manuale utente di YASDE: capitolo 14.

from build import c, code, note, p, rif, table, tip, ui, ul, warn


S1 = p("YASDE has no settings window. What you arrange (wallpaper, icons, widgets, pinned apps, volume, folders) is saved "
       "by itself, for each phone, and comes back at the next start. A few options exist only as lines in a file you can edit by hand.", lead=True) + \
    table(["What is saved", "Where you set it"], [
        ["Wallpaper type, colors, picture and fit", rif("The wallpaper")],
        ["Icons and their place on the grid", rif("Icons")],
        ["Widgets: kind, size, style, place, the weather city", rif("Adding and arranging widgets")],
        ["Pinned apps and their order", rif("Working with apps on the taskbar")],
        ["Volume level (not the mute)", rif("The volume control")],
        ["Last folders used to send and to receive files", rif("The transfer list")],
        ["Apps with " + ui("Compatibility") + " on", rif("Size and the Compatibility option")],
        ["How often each app was opened (for the " + ui("Frequent apps") + " widget)", "Counted by YASDE; only the app's identifier and the numbers are saved"],
    ], "«TAB» — What YASDE remembers for each phone")

S2 = p("Besides the files you ask for (screenshots and recordings in " + c("Pictures/YASDE") + ", downloads in your Downloads folder or the one you chose), YASDE writes on the PC only in two folders, which follow the XDG rules (if you set " + c("XDG_CONFIG_HOME") + " or "
       + c("XDG_CACHE_HOME") + " to an absolute path, those are used instead of " + c("~/.config") + " and " + c("~/.cache") + ").", lead=True) + \
    table(["File or folder", "Content"], [
        [c("~/.config/YASDE/preferenze.toml"), "General options (next section)."],
        [c("~/.config/YASDE/telefoni.toml"), "The phones YASDE knows: serial number, name, last address and when it was last seen. Used to reconnect."],
        [c("~/.config/YASDE/telefono-R5CT0000000.toml"), "Everything about one phone, in a file named after its serial number (the number shown here is made up)."],
        [c("~/.config/YASDE/chiave.pem"), "The PC's private key, which makes the phone trust this PC. Keep it private."],
        [c("~/.cache/YASDE"), "The wallpaper picture, as " + c("sfondo-<number>.png") + ", and a small image-loader file named " + c("yasde-pixbuf-<number>.cache") + ", which is renewed at each start."],
    ], "«TAB» — The files of YASDE") + ul([
        "Files are written safely: if the PC loses power during a save, you keep either the old file or the new one, never half of one. The folder is private to you.",
        "A file that cannot be read is not erased: it is renamed with the ending " + c(".illeggibile") + " and YASDE starts with default values.",
        "Wallpaper pictures that no phone file uses any more are removed from the cache at start.",
    ]) + note("on some systems " + c("~/.cache") + " is in memory and is emptied at each restart. Then a picture used as wallpaper is lost, and the wallpaper must be chosen again.", "Cache in memory.")

S3 = p("Close YASDE before editing a file: it saves its own copy when it closes and would overwrite yours. The files are plain text with lines such as " + c("name = value") + " under headings in square brackets. The names are in Italian.", lead=True) + \
    code("[generale]\nesc_indietro = true\navvisi = true\ncartella_ricevuti = \"\"\nwidget_nascosti = false\nriapri_dove_eri = false\n\n[shell]\nscala_automatica = true\nscala = 1.0",
         "text", "preferenze.toml: the options and their default values") + table(["Line", "Effect"], [
    [c("esc_indietro"), "true: the Esc key is “Back” in an app window."],
    [c("avvisi"), "true: new notifications are also sent to the PC's notification system when the YASDE window is not in front."],
    [c("cartella_ricevuti"), "Starting folder for downloaded files. Empty: your Downloads folder. A folder chosen in the download panel for a phone wins."],
    [c("widget_nascosti"), "true: no widget is shown on the desktop."],
    [c("riapri_dove_eri"), "true: windows reopen at their last place and size. Default false."],
    [c("scala_automatica"), "true: YASDE follows the PC's display scale. With false, " + c("scala") + " (between 0.5 and 4.0) is used."],
    [c("densita") + " under " + c("[app]") + " in a phone file", "0 (default): automatic, as in " + rif("Size and the Compatibility option") + ". A value between 72 and 960 forces one fixed density for every app of that phone; an invalid value goes back to 0."],
], "«TAB» — Options you can change by hand") + \
    p("The line " + c("lingua") + " is read and ignored: the program is always in English.")

CHAPTER = ("Settings and where YASDE keeps its data", [
    ("What YASDE remembers", S1),
    ("Where YASDE keeps its data", S2),
    ("Settings you can change by hand", S3),
])
