# Manuale utente di YASDE: capitolo 7.

from build import c, note, p, rif, shot, steps, table, tip, ui, ul, warn


S1 = p("Each app opens in its own window inside the YASDE desktop. Opening an app that is already open only brings its "
       "window to the front (or restores it if it was minimized). You can have up to 64 windows.", lead=True) + \
    shot("window.png", "«FIG» — An app window with its title bar") + table(["Button", "What it does"], [
        [ui("Back"), "Android's Back action. On the left of the title bar. The Esc key does the same by default (" + rif("Keyboard") + ")."],
        ["App name", "Next to Back. While recording, " + ui("REC") + " appears on the right, just before the " + ui("Screenshot") + " button. In a very narrow window (about 300 pixels or less, such as the minimum width) there is no room and the name is not shown."],
        [ui("Screenshot"), "Takes a screenshot (" + rif("Screenshots") + ")."],
        [ui("Record the screen"), "Starts and stops a recording (" + rif("Recordings") + ")."],
        [ui("More commands"), "Menu with " + ui("Rotate") + ", " + ui("Compatibility") + " and " + ui("Close app") + "."],
        [ui("Minimize"), "Hides the window; the app stays open on the taskbar."],
        [ui("Maximize"), "Fills the area above the taskbar; the same button, " + ui("Maximize or restore") + ", restores it."],
        [ui("Close"), "Closes the window and the app. It turns coral when you point at it."],
    ], "«TAB» — The buttons of an app window") + \
    p("The buttons are round and show only their symbol until you point at them. The title bar is made of glass: it lets you glimpse, blurred, what is behind the window. "
      "On computers without graphics acceleration the title bar is light and opaque; everything else works the same.") + \
    note("new windows open slightly offset from each other. With the option “reopen where you were” (" + rif("Settings you can change by hand") + ") a window comes back at its last place and size.", "Placement.")

S2 = ul([
    "To move a window, drag its title bar. You can push it partly outside the left, right and bottom edges to put it aside, "
    "but at least 96 pixels and the title bar stay reachable; it never goes above the top edge.",
    "To resize, drag the small handle at the bottom-right corner. The minimum is 288 × 180.",
    "Click a window to bring it to the front; the active window has a darker name and symbols and a stronger shadow, while the others are dimmed.",
    ui("Rotate") + " (menu " + ui("More commands") + ", or Ctrl+R) swaps the width and height of a window. It does nothing on a maximized, snapped or fixed-shape window, or while recording.",
    "A window you minimize pauses its picture to save work; restoring it resumes at once. The app itself keeps running on the phone.",
]) + p("While you move or resize, YASDE only scales the picture. The phone is told the new size once you stop moving for about "
       "a third of a second.")

S3 = p("Drag a window by its title bar toward an edge of the desktop and release:", lead=True) + table(["Release near…", "Result"], [
    ["The left edge", "The window fills the left half."],
    ["The right edge", "The window fills the right half."],
    ["The top edge", "The window is maximized."],
], "«TAB» — Snapping windows") + \
    shot("windows-snapped.png", "«FIG» — Two windows snapped side by side") + \
    p("A snapped window follows the desktop if you resize the YASDE window. Dragging its title bar away unsnaps it and it gets its previous size back.")

S4 = p("Some apps are made only for portrait use. YASDE keeps their window in phone shape: they cannot be resized, "
       "maximized or snapped, and " + ui("Rotate") + " is inactive. You can still move, minimize and close them. The window "
       "of the phone's mirrored screen (" + rif("The phone screen mirror") + ") works the same way.", lead=True) + \
    tip("the app is shown at the size of the phone's screen, reduced if it does not fit the desktop, and never narrower than a phone "
        "would make it.", "Size.")

S5 = p("How big an app looks depends on a setting called density. YASDE chooses it for you, window by window, so that one dot "
       "of the app is about one pixel of the screen, as on Samsung DeX. A small window therefore looks like a phone; a large window "
       "gives the app more room instead of making everything bigger, so an app may switch to a tablet layout. There is no control "
       "for this in the interface.", lead=True) + \
    p("A few apps draw themselves too small. The cause is in the app: it uses the density of the phone's real screen instead of the "
      "one it is given. For these apps YASDE has the " + ui("Compatibility") + " option:") + steps([
        "Open the window of the app and click " + ui("More commands") + ".",
        "Tick " + ui("Compatibility") + " (the menu says " + ui("For apps that look too small") + ").",
        "The app redraws at the right size at once, without being reopened.",
    ]) + shot("window-menu.png", "«FIG» — The More commands menu: Rotate, Compatibility and Close app") + ul([
        "The choice is remembered for that app, for that phone, and applied each time you open it. Untick it to go back.",
        "It is not offered for the phone screen mirror and is inactive while the window is being recorded.",
        "Up to 128 apps can have it; beyond that you get " + ui("Too many apps in compatibility mode.") + ".",
    ])

CHAPTER = ("App windows", [
    ("Opening and controlling a window", S1),
    ("Moving and resizing", S2),
    ("Snapping windows", S3),
    ("Windows that cannot be resized", S4),
    ("Size and the Compatibility option", S5),
])
