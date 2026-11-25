# Manuale utente di YASDE: capitolo 8.

from build import c, key, note, p, rif, table, tip, ui, ul, warn


S1 = p("In an app window the mouse acts as one finger on the phone's screen.", lead=True) + table(["You do", "The app gets"], [
    ["Left click", "A tap."],
    ["Left button held and moved", "A swipe or drag."],
    ["Right click", "A right mouse click, as with a mouse on the phone: in Chrome, on a link, it opens the context menu."],
    ["Middle click", "A middle mouse click: in Chrome, on a link, it opens the link in a new tab."],
    ["Mouse wheel or touchpad scroll", "A scroll."],
    [key("Ctrl") + " + wheel up / down", "A zoom in / out at the pointer, like pinching with two fingers. It is meant for apps that do not react to the plain wheel: maps, photos, documents."],
    ["Pinch on a touchpad", "The same zoom."],
], "«TAB» — The mouse in an app window") + \
    note("the right and middle buttons reach the app only when you press them on the app's own content. A click in the title bar, on a window border or on a window button works on the window, never on the app. Only one button counts at a time: pressing a second button while one is held cancels what you were doing (a touch is cancelled on the phone; a window move or resize is cancelled) and the second button does nothing. The back and forward buttons of the mouse are not sent. A right or middle click also brings the window to the front, like a left click.", "Other buttons.")

S2 = p("The window that has the focus receives the keys. Letters, digits, accents and symbols are sent as text, so capitals and "
       "your keyboard layout come out right. Enter, Tab, Backspace, Delete, arrows, Home, End, Page Up, Page Down and F1 to F12 are sent as keys, "
       "and shortcuts with Ctrl or Alt (such as Ctrl+C and Ctrl+V) are passed to the app.", lead=True) + \
    table(["Keys", "What they do"], [
        [key("Esc"), "Back, as the " + ui("Back") + " button, unless " + c("esc_indietro") + " is false (" + rif("Settings you can change by hand") + "). If a menu or panel is open, Esc closes that first."],
        [key("F11"), "Full screen for the YASDE window (and back)."],
        [key("Ctrl", "R"), "Rotate the active window."],
        [key("Ctrl", "W"), "Close the active app."],
        [key("Ctrl", "Shift", "C"), "Copy a screenshot of the active window to the clipboard."],
    ], "«TAB» — Keys that YASDE keeps for itself") + \
    p("These keys never reach the phone (Esc only while it acts as Back). In full screen the title bar is hidden: move the pointer to the top edge and a bar with "
      + ui("Exit full screen") + " slides down.")

S3 = p("The PC and the phone share their clipboard, for text only.", lead=True) + ul([
    "Copy text on the PC and paste it in an app, or the reverse: the text travels by itself, with no extra step.",
    "Pictures and files are not shared this way (use " + rif("Sending files to the phone") + ").",
    "Texts longer than 200,000 bytes are ignored.",
    "A copy that a password manager marks as sensitive never passes between the two, in either direction.",
    "YASDE never writes the copied text in its logs.",
]) + tip("a screenshot copied with " + key("Ctrl", "Shift", "C") + " goes to the PC clipboard as a picture and is not sent to the phone.", "Screenshots.")

CHAPTER = ("Mouse, keyboard and clipboard", [
    ("Mouse and touchpad", S1),
    ("Keyboard", S2),
    ("Shared clipboard", S3),
])
