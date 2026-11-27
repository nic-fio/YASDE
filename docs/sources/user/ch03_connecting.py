# Manuale utente di YASDE: capitolo 3.

from build import c, flow, note, p, rif, shot, steps, table, tip, ui, ul, warn


S1 = p("The first connection is done once for each phone, without a cable, in a few minutes. YASDE needs two "
       "things on the phone: the Android " + ui("Developer options") + " and the " + ui("Wireless debugging")
       + " switch. After that the phone connects by itself every time.", lead=True) + ul([
    "The phone: Android 14 or later, charged and unlocked.",
    "The PC and the phone on the same Wi-Fi network (not a guest network that keeps devices apart, and not mobile data).",
    "YASDE running: at the first start it shows the " + ui("Connect your phone") + " screen by itself.",
]) + table(["Step", "On the phone"], [
    ["Unlock Developer options", "Open " + ui("Settings") + " › About phone and tap " + ui("Build number")
     + " seven times (on some brands it is under Software information). The phone asks for your PIN. If the Developer options "
     "are already visible in Settings, skip this."],
    ["Turn on Wireless debugging", "Open " + ui("Settings") + " › " + ui("Developer options") + " and turn on "
     + ui("Wireless debugging") + ". Tap " + ui("Allow") + " if the phone asks about the Wi-Fi network."],
], "«TAB» — What to prepare on the phone") + \
    note("the Android names depend on the brand. The search field in the phone's Settings finds them quickly: type "
         + c("build number") + " or " + c("wireless debugging") + ".", "Where things are.")

S2 = p("Until a phone is paired, the whole YASDE window is the connection screen. Its content follows what YASDE sees "
       "on the network.", lead=True) + shot("connect-search.png", "«FIG» — The connection screen while it looks for a phone") + table(
    ["Title", "When you see it", "What it shows"], [
        [ui("Connect your phone"), "No phone has been paired yet.",
         "Three numbered steps: " + ui("1. On the phone open Settings › Developer options and turn on Wireless debugging.")
         + " " + ui("2. The phone and the PC must be on the same Wi-Fi network.") + " "
         + ui("3. When the phone appears here it will ask you for the pairing code.") + " Under them "
         + ui("Looking for phones on the network…") + " with a spinning circle, and the button " + ui("Search again") + "."],
        [ui("Pair the phone"), "A phone is on the network but does not know this PC.",
         "The field " + ui("Pairing code") + ", the button " + ui("Pair") + " and the expander " + ui("The phone does not show up? Type the address and port") + "."],
        [ui("Looking for the phone"), "A phone was paired before but is not found now.",
         ui("Check that the phone is on, on the same Wi-Fi network and with Wireless debugging turned on.")
         + " and the reason YASDE gives. If several phones are found: " + ui("There are several phones on the network: I am using the first one found.")],
    ], "«TAB» — The states of the connection screen") + \
    p("Above the title the card always shows the name YASDE, “Yet Another Samsung Dex Emulator” and the line “Independent project, not affiliated with Samsung”.") + \
    p("YASDE works with one phone at a time. It finds phones by listening on the local network, so you never type an "
      "address, except in the special case described in " + rif("If the phone does not show up") + ".")

S3 = flow([
    ("Same network", "PC and phone on Wi-Fi", "soft"),
    ("Developer options", "7 taps on Build number", "blue"),
    ("Wireless debugging", "turned on", "blue"),
    ("Pairing code", "6 digits from the phone", "navy"),
    ("Connected", "next times: automatic", "green"),
], "«FIG» — The first connection, from the network to the connected phone", width=960) + steps([
    "Make sure the phone is on the same Wi-Fi network and that " + ui("Wireless debugging") + " is on (" + rif("Before you start") + ").",
    "On the phone open " + ui("Developer options") + " › " + ui("Wireless debugging") + " and tap "
    + ui("Pair device with pairing code") + ". The phone shows a six-digit code and keeps the pairing screen open.",
    "In a moment the YASDE screen changes to " + ui("Pair the phone") + ": it says "
    + ui("On the phone: Developer options › Wireless debugging › Pair device with pairing code. Type here the six-digit code that appears.")
    + " and shows the field " + ui("Pairing code") + ".",
    "Type the six digits (anything that is not a digit is ignored, so a code pasted with spaces works) and press "
    + ui("Pair") + ".",
    "The screen shows " + ui("Pairing in progress…") + " and then " + ui("Paired: connecting the phone…") +
    ". When the connection is made the screen disappears and your desktop appears.",
]) + shot("connect.png", "«FIG» — The pairing screen, ready for the six-digit code") + shot("desktop-widgets.png", "«FIG» — The desktop after the connection (with a few icons and widgets added)") + tip(
    "keep the pairing screen on the phone open until YASDE has finished: the code is valid only while that screen is shown.",
    "Do not close it.") + note("YASDE never saves the pairing code, on disk or in logs. "
                              "It saves the key of the PC, so the phone recognises it next time, and the phone's last address, "
                              "used to reconnect (never written in the logs).", "The code is not kept.")

S4 = p("If the pairing does not go through, YASDE says why under the field:", lead=True) + table(["Message", "What to do"], [
    [ui("Pairing failed. Check the code and try again."), "The code was wrong or expired. On the phone close and reopen "
     + ui("Pair device with pairing code") + " and type the new code."],
    [ui("The phone did not answer in time. Try again."), "Check the Wi-Fi network and try again."],
    [ui("on the phone open the \"Pair device with pairing code\" screen and try again"),
     "The phone was no longer waiting for a code. Open that screen again on the phone."],
    [ui("Try again in a moment."), "Another pairing may be running: wait a few seconds."],
    [ui("This version cannot pair the phone. Try \"Search again\"."), "Press " + ui("Search again") + "."],
], "«TAB» — Pairing messages") + \
    p("The phone does not appear? Turn " + ui("Wireless debugging") + " off and on again on the phone and press "
      + ui("Search again") + ". See also " + rif("Stuck on Looking for phones on the network") + ".")

S5 = p("When the " + ui("Pairing code") + " field is shown but pairing keeps failing, YASDE may not be seeing the phone's "
       "pairing service. Open " + ui("The phone does not show up? Type the address and port") + " under the button and type "
       "the " + ui("IP address and port") + " that the phone shows under the pairing code (for example " + c("192.0.2.10:37123")
       + "), then press " + ui("Pair") + ". The button is inactive while the address is not valid.", lead=True) + \
    note("this field appears only on the pairing screen. If you are still on " + ui("Connect your phone") + " the phone "
         "is not announcing itself on the network: turn Wireless debugging off and on.", "Only for pairing.")

S6 = p("After the first pairing there is nothing more to do. When YASDE starts it looks for the phone on the network "
       "and connects by itself, and your desktop comes back as you left it.", lead=True) + ul([
    "The phone must be on, on the same Wi-Fi network, with " + ui("Wireless debugging") + " turned on.",
    "Android sometimes turns Wireless debugging off by itself, for example when you change Wi-Fi network. Turn it on again: "
    "no new code is needed.",
    "If the phone is locked when YASDE connects, the screen says " + ui("Phone locked") + " until you unlock it (" +
    rif("A locked phone and a lost connection") + ").",
    "If the phone no longer recognizes this PC (for example you removed it in Developer options), YASDE should show "
    + ui("Pair the phone") + " again, and you repeat the pairing. This case has not been tried live.",
]) + tip("many phones let you add " + ui("Wireless debugging") + " to the Quick Settings tiles: it turns back on with one tap.",
         "Quick turn-on.")

S7 = p("The most frequent problem at the first connection is that the screen stays on " + ui("Looking for phones on the network…")
       + ". YASDE listens for the phone's announcement on the network; if the phone does not send it, YASDE cannot find it. "
       "Try, in this order:", lead=True) + steps([
    "On the phone turn " + ui("Wireless debugging") + " off and on again. This solved the problem in testing.",
    "Check that phone and PC are on the same Wi-Fi network and that the router does not keep Wi-Fi devices apart (some "
    "guest networks do).",
    "Press " + ui("Search again") + ".",
])

CHAPTER = ("Connecting the phone", [
    ("Before you start", S1),
    ("The Connect your phone screen", S2),
    ("Pairing step by step", S3),
    ("Pairing messages", S4),
    ("If the phone does not show up", S5),
    ("Stuck on Looking for phones on the network", S7),
    ("Later connections", S6),
])
