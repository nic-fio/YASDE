# Manuale utente di YASDE: capitolo 17.

from build import dl, p


S1 = p("Words used in this manual.", lead=True) + dl([
    ("ADB", "Android Debug Bridge, the connection method Android offers to computers. YASDE speaks it itself: no Android tools are needed on the PC."),
    ("APK", "The file of an Android app."),
    ("AppImage", "A single file that runs on Linux without installing anything."),
    ("Back", "Android's “go back” action: the " + "Back button of a window and the Esc key."),
    ("Compatibility", "A per-app option that makes apps drawn too small use the real density of the phone's screen."),
    ("Density", "How many dots Android draws in an inch. It decides how large an app looks. YASDE sets it for each window."),
    ("Developer options", "A hidden section of the phone's Settings that you unlock by tapping Build number seven times."),
    ("Guardian", "A small helper on the phone that puts things back if YASDE or the connection dies."),
    ("In hand", "The state of the phone when you touch it, unlock it or use it for a call: its screen is on and YASDE does not turn it off until you work from the PC again."),
    ("Launcher", "The grid with all the apps of the phone."),
    ("Mirror", "The window titled Phone screen that shows the real screen of the phone."),
    ("Open-Meteo", "The free weather service used by the Weather widget."),
    ("Pairing code", "The six digits shown by the phone to authorize the PC."),
    ("Physical panel", "The light-emitting part of the phone's screen. YASDE can turn it off while the phone stays awake."),
    ("Pin", "To keep an app's icon on the taskbar even when it is closed."),
    ("Snapping", "Dragging a window to an edge of the desktop so that it fills a half or the whole area."),
    ("Taskbar", "The bar at the bottom with the launcher, the open apps and the system buttons."),
    ("Veil", "A cover over the desktop that says the phone is locked or the connection was lost."),
    ("Virtual screen", "A screen that exists only in memory. The apps in YASDE windows run on virtual screens, not on the phone's display."),
    ("Wake lock", "An Android request to keep the phone awake; it is released by Android when the program that asked dies."),
    ("Widget", "A small card on the desktop that shows data from the phone or the PC."),
    ("Wireless debugging", "The Android setting that lets the PC connect over Wi-Fi."),
])

CHAPTER = ("Glossary", [
    ("Glossary of terms", S1),
])
