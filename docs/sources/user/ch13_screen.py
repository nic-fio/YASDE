# Manuale utente di YASDE: capitolo 13.

from build import c, flow, note, p, rif, shot, steps, table, tip, ui, ul, warn


FLUSSO = flow([
    ("Connected", "YASDE turns the panel off", "blue"),
    ("Panel off", "apps run on virtual screens", "navy"),
    ("You touch the phone", "or a call, or an unlock", "amber"),
    ("Panel on", "the phone is “in hand”", "green"),
    ("First input from the PC", "the panel turns off again", "navy"),
], "«FIG» — What happens to the phone's screen panel while YASDE is connected", width=960)

S1 = p("The apps you see in YASDE run on virtual screens, not on the phone's real display. So the real screen can stay off "
       "while you work on the PC. Only the light of the panel is switched off: Android believes the screen is on, "
       "and the apps keep running.", lead=True) + FLUSSO + ul([
    "Right after the connection succeeds YASDE turns the panel off, once. If the phone refuses (it is locked or in a call), it does not insist.",
    "If you take the phone in hand and touch its screen, press a button, unlock it, or a call arrives or is in progress, the panel turns on by itself and stays on while you use the phone.",
    "At the first input you give from the PC after that, the panel turns off again.",
    "If the phone was already “in hand” at the moment of the connection, the panel is not turned off.",
    "Closing the last app window does not turn the panel on again. It comes back on only if you unlock or touch the phone, at a call, when you use the switch below, or when YASDE closes.",
]) + table(["Control", "What it does"], [
    [ui("Physical panel"), "In the " + ui("Phone status") + " popup. Switch it on or off by hand. The pill follows the real state."],
    [ui("The phone did not turn the physical panel off"), "A message with the reason: " + ui("The phone is locked.") + ", " + ui("A call is in progress.") + ", "
     + ui("There is no safe way to restore it right now.") + " or " + ui("The phone does not allow it.")],
    [ui("The phone did not turn the physical panel on"), "The same kind of message when the phone refuses to turn it on."],
], "«TAB» — The panel switch and its messages") + \
    note("while the panel is off the phone's display is dark but the phone is awake. Touching the dark screen wakes it, as you expect.", "Dark, not asleep.")

S2 = p("While YASDE is connected the phone does not go to sleep and does not lock because of inactivity, even if you only "
       "watch a video on the PC. YASDE does it with an Android “keep awake” request that the phone drops by itself if YASDE dies.", lead=True) + ul([
    "If you lock the phone yourself with its power button, it locks: that is your choice and YASDE does not fight it (" + rif("A locked phone and a lost connection") + ").",
    "If the PC disappears without YASDE closing (for example the Wi-Fi drops), the phone waits 30 minutes with no sign from the PC. Then it goes back to normal: panel on, your own screen timeout, and it locks with its usual timing.",
    "When the PC comes back, the phone is meant to be kept awake again, with the panel on until YASDE turns it off at the next input. This case has not been tried live.",
    "When YASDE closes or crashes the phone returns to its normal behavior.",
])

S3 = p("Click " + ui("Show the phone screen") + " in the phone popup to open a window titled " + ui("Phone screen") + " that mirrors "
       "the real screen of the phone. Click it again to close the window.", lead=True) + \
    ul([
        "The mirror has a fixed shape, the proportion of the phone's screen. It moves, minimizes and closes like the other windows, but cannot be resized or maximized, and has no " + ui("Compatibility") + " option.",
        "Mouse and keyboard work in it as in the other windows.",
    ])

S4 = p("YASDE follows the phone's lock state.", lead=True) + table(["You see", "It means"], [
    [ui("Phone locked") , ui("As in DeX, the desktop locks with the phone. Unlock the phone to continue: the windows return where they were.")],
    [ui("Connection lost"), "The Wi-Fi or the phone dropped. YASDE keeps your windows, shows the last picture and keeps trying; " + ui("Reconnect now") + " asks it to try at once. Windows resume by themselves."],
    [ui("Looking for the phone"), "YASDE is searching the network after a drop."],
], "«TAB» — The veils over the desktop") + \
    p("The desktop is hidden behind a veil in these cases and returns when the phone is back.")

CHAPTER = ("The phone's screen", [
    ("The physical panel", S1),
    ("Phone awake and unlocked", S2),
    ("The phone screen mirror", S3),
    ("A locked phone and a lost connection", S4),
])
