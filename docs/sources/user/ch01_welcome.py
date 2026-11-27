# Manuale utente di YASDE: capitolo 1.

from build import CONTATTO, SITO_URL, VERSION, arrow, box, c, fig, note, p, rif, table, text, tip, ui, ul, zone


SCHEMA = fig(
    zone(20, 14, 360, 196, "Linux PC")
    + box(40, 46, 320, 44, "YASDE window", "desktop, taskbar, widgets", "navy")
    + box(40, 100, 155, 44, "App window", "one for each app", "blue")
    + box(205, 100, 155, 44, "App window", "mouse and keyboard", "blue")
    + box(40, 154, 320, 40, "PC speakers, mouse, keyboard and clipboard", "", "soft", 12)
    + zone(520, 14, 360, 196, "Android 14 or later phone")
    + box(540, 46, 320, 44, "The phone's apps", "keep running on the phone", "blue")
    + box(540, 100, 320, 44, "Wireless debugging", "an Android setting, turned on once", "dark")
    + box(540, 154, 320, 40, "Nothing installed", "", "soft", 12)
    + arrow(516, 70, 384, 70, "#0050C0") + text(450, 58, "picture and sound", 11, "#003a90", "700")
    + arrow(384, 124, 516, 124, "#16a34a") + text(450, 112, "clicks, keys, files", 11, "#166534", "700")
    + text(450, 176, "same Wi-Fi network", 11.5, "#334155", "700")
    + text(450, 192, "encrypted connection", 11, "#475569"),
    900, 224, "«FIG» — YASDE shows on the PC the apps that run on the phone")

S1 = p("YASDE (“Yet Another Samsung Dex Emulator”) turns one window on your Linux PC into a desktop. The apps of your "
       "Android phone open as windows inside that desktop, and you use them with mouse and keyboard. It is an "
       "independent project, not affiliated with Samsung, and it is designed for Android 14 or later phones of any make, using only standard Android. It has been tried on one phone so far.",
       lead=True) + SCHEMA + \
    p("The apps keep running on the phone, with their data and accounts. YASDE sends their picture and sound to "
      "the PC and sends your clicks and keystrokes back to the phone. With YASDE you can:") + ul([
        "keep a desktop with a wallpaper, app icons and widgets, and a taskbar with a launcher;",
        "open the phone's apps in separate windows, several at the same time;",
        "type with the PC keyboard and copy and paste text in both directions;",
        "hear the phone's app sound through the PC speakers;",
        "read the phone's notifications and see the phone's status on the PC;",
        "send files to the phone, download files from it and install an APK;",
        "take screenshots and record an app's screen;",
        "open a window that mirrors the phone's real screen.",
    ]) + \
    note("no app is installed on the phone and no root access is needed. The few things YASDE changes while it "
         "runs are put back when it closes (" + rif("What YASDE changes on the phone") + ").", "Nothing to install.") + \
    p("The program is in English only.") + \
    note("YASDE is free: anyone may use it, change it and pass it on, but may not sell it. The terms are the MIT "
         "License with the Commons Clause condition, in the file " + c("LICENSE") + " of the project. The third-party "
         "components that come in the package keep their own licences, listed in the documentation folder of the package.",
         "Licence.")

S2 = table(["If you want to…", "Read", "Chapters"], [
    ["Install YASDE and connect the phone for the first time", "Installation and Connecting the phone", "2 - 3"],
    ["Use the desktop, the apps and the files", "From The desktop to Screenshots and recordings", "4 - 12"],
    ["Understand what happens to the phone's screen", "The phone's screen", "13"],
    ["Know where YASDE keeps its data and what it changes", "Settings, Privacy", "14 - 15"],
    ["Solve a problem", "Problems and solutions", "16"],
], "«TAB» — Where to find what you need") + \
    p("To go from the downloaded file to the first open app, follow " + rif("Pairing step by step") +
      ". The " + rif("Glossary of terms") + " explains the less common words. Questions and reports: " + c(CONTATTO) +
      "; the project's site is " + c(SITO_URL) + ".")

S3 = table(["Convention", "Meaning"], [
    [ui("Add widget…"), "Text that appears in the interface: buttons, menu items, labels, messages."],
    [c("~/.config/YASDE"), "File and folder names and values, to be typed exactly as shown."],
    ["Blue <b>Note</b> box", "Useful information for understanding."],
    ["Green <b>Tip</b> box", "A simpler or safer way to do something."],
    ["Yellow <b>Warning</b> box", "A behavior that may be surprising."],
], "«TAB» — The conventions of this manual") + \
    p("In this manual “phone” is the Android phone connected to YASDE and “PC” is the Linux computer running it. "
      "Names of Android settings, for example " + ui("Wireless debugging") + ", can differ a little from one brand to "
      "another: the manual uses the most common names. The figures that are drawings show the idea. The screenshots "
      "were taken with a demonstration phone: the apps are well-known apps shown with the icons of the theme included in the program (Kora, credited in the chapter on the desktop), the data (notifications, calls, network, weather, notes) is invented and the content inside the app windows is a placeholder. App names and icons belong to their respective owners.") + \
    note("this manual describes YASDE " + VERSION + ". What it says is checked against the program: if something is "
         "not described here, YASDE does not do it.", "Version.")

CHAPTER = ("Welcome", [
    ("What YASDE is", S1),
    ("Who this manual is for", S2),
    ("Conventions", S3),
])
