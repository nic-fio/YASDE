# Manuale utente di YASDE: capitolo 11.

from build import c, note, p, rif, shot, steps, table, tip, ui, ul, warn


S1 = p("There are three ways to send files from the PC to the phone, all in the panel " + ui("Send to the phone")
       + " (" + ui("From the PC to the phone") + "):", lead=True) + \
    shot("transfer.png", "«FIG» — The send panel") + steps([
        "Click " + ui("Send files to the phone") + " on the taskbar, or just drag files from your file manager over the YASDE window: "
        "a frame appears saying " + ui("Drop to send to the phone") + ", and the files are queued when you release them.",
        "Or press " + ui("Choose files from the PC") + " to pick files in the system's file dialog.",
        "Check the destination under " + ui("Destination folder on the phone") + ". The starting folder is Download, or the last one you used. "
        "The buttons under " + ui("Frequently used folders") + " jump to " + ui("Download") + ", " + ui("Documents") + ", " + ui("Pictures") + " and " + ui("Music") + ".",
    ]) + p("To pick another folder press " + ui("Change…") + ". You get a folder browser for the phone:") + ul([
        ui("Up") + " goes to the parent folder; the path field takes a path (Enter goes there; the phone's “/sdcard” is understood).",
        "The search field " + ui("Search for a folder on the whole phone") + " finds folders by name.",
        ui("New folder") + " creates one in the current folder; " + ui("Use this folder") + " makes it the destination.",
        "Hidden folders are not listed, and some places say " + ui("Access not allowed by the system") + ".",
    ]) + note("only regular files are sent, not folders: if you drop a folder you get " + ui("Some items were not sent") + ".", "Folders.")

S2 = p("Click " + ui("Download files from the phone") + " for the panel " + ui("Download from the phone") + " (" + ui("From the phone to the PC") + ").", lead=True) + steps([
    "Browse the phone with " + ui("Up") + ", the path field or the search field (" + ui("Search files or folders on the whole phone") + "). It starts at the phone's main storage.",
    "Tick the files with their checkboxes. Your selection stays while you change folder.",
    "Check " + ui("Destination folder on the PC") + " (starting from your Downloads folder, or the last one used; " + ui("Change…") + " opens a folder browser of the PC).",
    "Press the large button, which says " + ui("Select the files to download") + " until you tick something, and then “Download” followed by the number of files.",
])

S3 = shot("transfer-list.png", "«FIG» — A file being sent: name, percentage, cancel button, bar and destination") + p("Both panels list their transfers, newest first, with the percentage and a bar. States are " + ui("Waiting") + ", "
       + ui("Completed") + ", " + ui("Cancelled") + " and " + ui("Failed") + ".", lead=True) + ul([
    "Each entry has a cancel button; " + ui("Cancel all transfers") + " stops the whole queue.",
    "A message " + ui("Sent to the phone") + " or " + ui("Downloaded to the PC") + " appears when a transfer ends.",
    "While a transfer is running the taskbar button shows a dot.",
    "If the connection drops, the waiting transfers restart by themselves when it comes back.",
    "The widget " + ui("Recent transfers") + " shows the latest ones on the desktop.",
])

S4 = p("Drop an " + c(".apk") + " file on the YASDE window and YASDE offers to install it instead of copying it.", lead=True) + steps([
    "A dialog asks " + ui("Install the app {name} on the phone?".replace("{name}", "NAME")) + " and explains that an app already on the phone is replaced and keeps its data.",
    "Press " + ui("Install the app") + " (or " + ui("Cancel") + ").",
    "The panel shows " + ui("Installing on the phone") + ", and at the end " + ui("App installed on the phone.") + ".",
]) + p("If it fails YASDE says why. The most common messages:") + table(["Message", "Meaning"], [
    [ui("The installed version is signed by a different author: to install this one the existing one must be uninstalled first (the app's data would be lost)."), "Uninstall the old app on the phone first."],
    [ui("The phone already has a newer version of this app: it cannot go back to an older one."), "Downgrades are refused."],
    [ui("There is not enough free space on the phone: free some memory and try again."), "Free some storage."],
    [ui("This app is split into several files: it cannot be installed from a single APK."), "Split apps (bundles) cannot be installed."],
    [ui("This app needs a newer version of Android than the phone has."), "The app is too recent for this phone."],
    [ui("The phone's security check did not approve the app."), "The phone refused it."],
], "«TAB» — Some installation messages") + \
    note("the temporary copy of the APK that YASDE puts on the phone is removed afterwards; if that fails it is removed at the next connection.", "Temporary file.")

CHAPTER = ("Files", [
    ("Sending files to the phone", S1),
    ("Downloading files from the phone", S2),
    ("The transfer list", S3),
    ("Installing an app from an APK", S4),
])
