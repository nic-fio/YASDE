# Manuale utente di YASDE: capitolo 12.

from build import c, key, note, p, rif, table, tip, ui, ul, warn


S1 = p("To take a screenshot of an app, press " + ui("Screenshot") + " in its title bar.", lead=True) + ul([
    "The picture is saved as a PNG in " + c("Pictures/YASDE") + " (your Pictures folder; if you have none, the folder is made in your home folder) and is also copied to the PC clipboard, so you can paste it right away.",
    "A message says " + ui("Screenshot saved") + " with the folder: " + ui("Pictures/YASDE and copied to the clipboard") + ".",
    "To copy only, without a file, press " + key("Ctrl", "Shift", "C") + " (" + ui("Copy screenshot") + "): you get " + ui("Screenshot copied to the clipboard") + ".",
    "The file is named after the app, the date and the time, for example " + c("Browser 2026-11-24 10.30.05.png") + "; if that name exists, " + c(" (2)") + ", " + c(" (3)") + "… are added. YASDE never overwrites a file.",
]) + note("some apps protect their screen. For them the window shows " + ui("This app protects its screen: the content cannot be shown.") + " and a screenshot cannot be taken.", "Protected apps.")

S2 = p("Press " + ui("Record the screen") + " in the title bar to start recording the window's app, and press it again to stop. "
       + ui("REC") + " shows next to the app's name while recording.", lead=True) + ul([
    "The recording is an MP4 file in the same folder as the screenshots, named like them. The video is saved as the phone sends it, without being re-encoded.",
    "It records the screen only, without sound.",
    "At the end you get " + ui("Recording saved") + " with the folder.",
    "It stops by itself, with " + ui("Recording interrupted") + ", if the window is minimized, if the YASDE window is hidden, if you close the window or if the connection is lost (" + ui("Recording interrupted: connection lost") + "). What was recorded up to then is kept.",
    "While a window records it cannot be resized, maximized, snapped or rotated, and " + ui("Compatibility") + " is inactive: the picture size must not change.",
    "If a recording is already running in that window and you ask for another, you get " + ui("Recording is already in progress.") + ".",
])

CHAPTER = ("Screenshots and recordings", [
    ("Screenshots", S1),
    ("Recordings", S2),
])
