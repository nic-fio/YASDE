# Manuale utente di YASDE: capitolo 15.

from build import c, note, p, rif, table, tip, ui, ul, warn


S1 = p("YASDE installs nothing on the phone and needs no root. Everything below is temporary and is undone when YASDE closes, except where the table says otherwise; if the connection is cut, at the latest at the next start of YASDE.", lead=True) + \
    table(["What", "Why", "How it is undone"], [
        ["A small program run from the files " + c("/data/local/tmp/yasde-<code>.jar") + " and a helper script " + c("yasde-custode-<number>.sh") + " in the phone's temporary folder", "It reads the screen, sound, notifications and so on for the PC.", "Both files are removed when YASDE closes."],
        ["The physical panel is turned off", "So the phone stays dark while you work on the PC.", "Turned on when you touch the phone, at a call, with the switch, when YASDE closes, or after 30 minutes without the PC."],
        ["The screen timeout is set to its maximum", "So the apps keep receiving input while the panel is off.", "Your value is put back, if the setting still has YASDE's value."],
        ["The minimum screen refresh rate may be raised (only on some phones)", "So video stays smooth with the panel off.", "Your value is put back."],
        ["The media volume is raised to the maximum", "So apps start their sound (" + rif("What happens to the phone's volume") + ").", "Your value is put back, unless you changed it meanwhile."],
        ["The phone is kept awake and unlocked", "So the desktop does not lock on inactivity.", "Android releases it by itself when YASDE stops."],
        ["A temporary copy of an APK in " + c("yasde-apk-<code>.apk"), "Only while an app is being installed.", "Removed afterwards, or at the next connection."],
        ["The clipboard text copied from the PC", "Shared clipboard.", "It stays on the clipboard like any copied text."],
        ["The quick switches and the ringer/flashlight", "Only when you click the " + ui("Quick switches") + " widget.", "They are your actions: YASDE does not undo them."],
    ], "«TAB» — What YASDE changes on the phone")

S2 = p("Each change to a phone setting (panel, screen timeout, refresh rate, volume) is first registered with a small guardian "
       "program on the phone, then made. If YASDE or the connection fails, the guardian restores the phone.", lead=True) + ul([
    "If Wireless debugging is turned off and on while YASDE is running, Android may stop the guardian too, and the restore waits. "
    "At the next start YASDE carries out any pending restore and removes the leftovers.",
    "If YASDE cannot make a restore safe, it does not make the change.",
]) + warn("If you changed the screen timeout or the volume yourself while YASDE was running, YASDE leaves your new value.", "Your changes win.")

S3 = p("The only connection to the Internet is the weather widget, and only while a " + ui("Weather") + " widget is on the desktop. The rest "
       "of the traffic between PC and phone goes over your local network, but see the note on the place name below.", lead=True) + table(["When", "To whom", "What is sent"], [
    ["While a weather widget is on the desktop", c("api.open-meteo.com"), "The position of the phone rounded to the nearest 0.005 degrees (about 500 meters), or the coordinates of the city you chose."],
    ["When you press " + ui("Search") + " in the location dialog", c("geocoding-api.open-meteo.com"), "The text you typed and a language code."],
], "«TAB» — What leaves the PC") + ul([
    "The rounding is done on the phone: the precise position never leaves it. The name of the place is worked out on the phone by Android's own geocoder from the rounded coordinates; on many phones that is an online service of Google or of the phone's maker, which YASDE does not control.",
    "Both services also see the PC's Internet address, as any website does.",
    "The position is not saved on disk and not written in logs. The only place saved is a city you chose by hand (name and rounded coordinates, in the phone's file).",
    "There is no consent dialog: adding the widget is the choice. Remove it and nothing is sent. The widget shows “Weather data by Open-Meteo.com”, a free service for non-commercial use.",
    "Pairing and everything else between PC and phone travels on the local Wi-Fi network in an encrypted connection.",
])

S4 = ul([
    "The pairing code is never saved. The phone's last address is saved in " + c("telefoni.toml") + " to reconnect, and is never written in logs.",
    "YASDE keeps notification titles and texts, and copied text, only in memory. With the default setting, though, a new notification is also handed to the PC's own notification system when YASDE is not in front, and that system may keep a history of it.",
    "Copies marked sensitive by a password manager are never passed between PC and phone.",
    "On the PC disk YASDE keeps the phone's serial number (in a file name and in " + c("telefoni.toml") + "), its name, its last address, your wallpaper picture and your layout.",
])

S5 = p("YASDE is an independent project, not affiliated with, endorsed by or sponsored by Samsung. Samsung and Samsung DeX "
       "are trademarks of Samsung Electronics Co., Ltd.")

CHAPTER = ("Privacy and what changes on the phone", [
    ("What YASDE changes on the phone", S1),
    ("Restoring the phone", S2),
    ("What leaves the PC: the weather", S3),
    ("What YASDE keeps", S4),
    ("Trademarks", S5),
])
