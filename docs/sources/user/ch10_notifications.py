# Manuale utente di YASDE: capitolo 10.

from build import c, note, p, rif, shot, steps, table, tip, ui, ul, warn


S1 = p("The " + ui("Notifications") + " button on the taskbar is a bell with the number of notifications you have not hidden. "
       "Click it to open the panel " + ui("Notifications") + ".", lead=True) + \
    shot("notifications.png", "«FIG» — The notifications panel") + ul([
        "Each entry shows the app's icon, the title, the text, and the app's name with the time.",
        "Click an entry to open its app; the notification is hidden at the same time.",
        ui("Hide") + " hides one notification and " + ui("Hide all") + " hides them all. When none is left the panel says " + ui("No notifications") + ".",
        "Hiding works only in YASDE: it does not remove the notification from the phone. A hidden notification that the app updates appears again.",
        "If the phone cannot give its notifications you see " + ui("I cannot read the phone's notifications.") + ".",
    ])

S2 = p("When a new notification arrives a card appears at the bottom right for a few seconds. Click it to open the app. "
       "Short messages from YASDE itself (for example " + ui("Widget added") + " or " + ui("Screenshot saved") + ") use the same card.", lead=True) + ul([
    "One message of this kind warns about video: " + ui("Hardware video decoding unavailable") + " means that YASDE is decoding the video with the processor, so apps look less smooth. It stays on screen longer than the others (about nine seconds) and appears at most once each time the phone connects, if the video falls back to the processor (" + rif("Problems with windows and apps") + ").",
    "Only notifications that arrive while YASDE is connected give a card: the ones already on the phone at connection, or after a reconnection, do not.",
    "If the YASDE window is minimized or not in front, a new notification is also sent to your PC's own notification system, so you see it even without looking at YASDE. This can be switched off by hand (" + rif("Settings you can change by hand") + ").",
    "YASDE keeps titles and texts of notifications only in memory, never on disk and never in logs. The PC's own notification system, which receives them with the default setting, may keep a history of them.",
]) + shot("decoding-warning.png", "«FIG» — The warning about video decoding with the processor")

S3 = p("The button " + ui("Phone status") + " shows the Wi-Fi and battery icons with the battery percentage. Click it for the popup.", lead=True) + \
    shot("phone-popup.png", "«FIG» — The phone popup with the physical panel switch") + table(["Part", "What it shows or does"], [
        [ui("Phone"), "A line such as " + ui("Android 14 · Battery 80% · Wi-Fi connected") + ". The network can also be " + ui("Mobile network") + " or " + ui("No network") + "."],
        [ui("Show the phone screen"), "Opens or closes the window that mirrors the phone's real screen. The pill shows " + ui("Visible") + " or " + ui("Hidden") + " (" + rif("The phone screen mirror") + ")."],
        [ui("Physical panel"), "Turns the phone's real screen panel on or off. The note says " + ui("Turns off by itself while you use the PC") + ". The pill shows " + ui("On") + " or " + ui("Off") + " and follows the real state of the phone (" + rif("The physical panel") + ")."],
    ], "«TAB» — The phone popup")

CHAPTER = ("Notifications and phone status", [
    ("The notifications panel", S1),
    ("Pop-up cards and PC notifications", S2),
    ("The phone popup", S3),
])
