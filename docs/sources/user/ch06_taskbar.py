# Manuale utente di YASDE: capitolo 6.

from build import c, note, p, rif, shot, steps, table, tip, ui, ul


S1 = p("The taskbar is the strip at the bottom of the YASDE window. On the left are the launcher and the apps; on the "
       "right the file buttons and the system area.", lead=True) + table(["Item", "What it is"], [
    [ui("App launcher"), "Opens the launcher (" + rif("The launcher") + ")."],
    ["App icons", "First the pinned apps, then the other open apps. A line under the icon shows the state: highlighted for the active window, plain for an open one, an outline for a minimized one."],
    [ui("Send files to the phone"), "Opens the send panel (" + rif("Sending files to the phone") + "). A dot appears while a transfer is running."],
    [ui("Download files from the phone"), "Opens the download panel (" + rif("Downloading files from the phone") + "). It shows the same dot."],
    [ui("Volume: 100%"), "The volume popup (" + rif("The volume control") + "). The label follows the level, or says " + ui("Volume: muted") + "."],
    [ui("Phone status"), "Wi-Fi and battery icons with the percentage. Opens the phone popup (" + rif("The phone popup") + ")."],
    [ui("Notifications"), "A bell with the number of notifications. Opens the notifications panel (" + rif("The notifications panel") + ")."],
    ["Clock", "The time of the PC, as HH:MM. Click it for the calendar (see below)."],
], "«TAB» — The taskbar, from left to right") + \
    p("The taskbar is dark glass; the launcher, the panels, the menus, the dialogs and the widgets are light glass and let you glimpse, blurred, what is behind them. "
      "On computers without graphics acceleration they have a plain, almost opaque background. Nothing changes in how they work.") + \
    shot("desktop.png", "«FIG» — The taskbar at the bottom of the desktop")

S2 = p("A click on an app's icon does what you expect:", lead=True) + table(["Click on…", "Result"], [
    ["A pinned app that is not open", "Opens the app."],
    ["An open app that is behind", "Brings its window to the front."],
    ["The active window", "Minimizes it."],
    ["A minimized window", "Restores it."],
], "«TAB» — Clicking an app on the taskbar") + \
    p("Right-click an app's icon for a menu:") + ul([
        ui("Close app") + " (only if the app is open): closes the window and removes the app from the phone's recent apps.",
        ui("Pin to taskbar") + " or " + ui("Unpin from taskbar") + ".",
    ]) + \
    p("A pinned app stays on the taskbar even when it is closed, and shows the name the phone gives it. Pins are kept for each phone, "
      "in the order you pinned them, up to 16. A seventeenth gives " + ui("Taskbar full") + ": " + ui("Unpin an app to make room") + ".")

S3 = p("Click the clock to open a calendar of the current month. The title gives the date, the arrows " + ui("Previous month")
       + " and " + ui("Next month") + " move by a month, " + ui("Previous year") + " and " + ui("Next year") + " by a year. "
       "It uses the PC's date and shows no events. Click the clock again, press Esc or click elsewhere to close it.", lead=True) + \
    note("for events read from the phone use the " + ui("Month calendar") + " widget (" + rif("The catalog") + ").", "Events.")

S4 = p("Click " + ui("App launcher") + " to see all the phone's apps in a grid. The search field has the focus at once: type "
       "part of a name and the grid is filtered. " + ui("No apps found") + " appears when nothing matches.", lead=True) + \
    p("The launcher is a panel in the upper middle of the YASDE window, not a full-screen cover: about 60% of the window's width (never narrower than 480 or wider than 960 pixels), just below the top edge, and as tall as its content, up to about three quarters of the space above the taskbar. "
      "The search field stays at the top and the grid scrolls. Clicking outside the panel closes it.") + \
    shot("launcher.png", "«FIG» — The launcher with the search field") + table(["Action", "Result"], [
        ["Click an app", "Opens it and closes the launcher."],
        ["Enter in the search field", "Opens the first app found (the field says " + ui("Search apps (Enter opens the first)") + ")."],
        ["Drag an app onto the desktop", "Adds an icon there (" + rif("Icons") + ")."],
        ["Right-click an app", "Menu: " + ui("Open") + ", " + ui("Add to desktop") + " or " + ui("Remove from desktop") + ", "
         + ui("Pin to taskbar") + " or " + ui("Unpin from taskbar") + "."],
        ["Esc", "Closes the menu if it is open, otherwise the launcher."],
    ], "«TAB» — What you can do in the launcher")

CHAPTER = ("The taskbar and the launcher", [
    ("The taskbar", S1),
    ("Working with apps on the taskbar", S2),
    ("The clock and the calendar", S3),
    ("The launcher", S4),
])
