# Manuale utente di YASDE: capitolo 4.

from build import c, note, p, rif, shot, steps, table, tip, ui, ul


S1 = p("Once the phone is connected, the YASDE window is a desktop: a wallpaper, app icons and widgets in the middle, "
       "the taskbar at the bottom. App windows open on top of it.", lead=True) + \
    shot("desktop-widgets.png", "«FIG» — A desktop with four icons, widgets and the taskbar") + ul([
        "The desktop is kept for each phone: wallpaper, icons and widgets are saved automatically and come back at the next start.",
        "A new phone starts with a teal gradient wallpaper, no icons and three widgets on the right: " + ui("Clock")
        + ", " + ui("Phone status") + " and " + ui("Recent notifications") + ".",
        "The window can be made smaller or larger, down to 640 × 420, and put in full screen (" + rif("Keyboard") + ").",
    ])

S2 = p("Right-click an empty part of the desktop to open its menu:", lead=True) + table(["Item", "What it does"], [
    [ui("Change wallpaper…"), "Opens the wallpaper dialog (" + rif("The wallpaper") + ")."],
    [ui("Add widget…"), "Opens the widget catalog (" + rif("Adding and arranging widgets") + ")."],
], "«TAB» — The desktop menu")

S3 = p("The dialog " + ui("Desktop wallpaper") + " has a preview at the top and three tabs under " + ui("Wallpaper type")
       + ". Every change is applied at once; close the dialog with " + ui("Done") + ".", lead=True) + table(["Tab", "Options"], [
    [ui("Solid color"), "A palette of 48 ready-made colors (a row of greys and blacks and five rows of tints) and " + ui("Other color…") + " for any color."],
    [ui("Gradient"), "Two colors, " + ui("From") + " and " + ui("To") + ", each from the same 48-color palette or " + ui("Other color…") + ", and the " + ui("Gradient direction") + ": "
     + ui("Diagonal") + ", " + ui("Vertical") + ", " + ui("Horizontal") + ", " + ui("Reverse diagonal") + "."],
    [ui("Image from the phone"), "Pictures taken from the phone. " + ui("Recent") + " shows up to 40 of the newest "
     "pictures of the phone's Camera and Screenshots folders; " + ui("Phone folders") + " lets you browse the folders and "
     "pick any picture. Then choose how it fits: " + ui("Fill") + ", " + ui("Fit") + ", " + ui("Stretch") + " or " + ui("Center") + "."],
], "«TAB» — The wallpaper types") + \
    note("the chosen picture is copied to the PC (in " + c("~/.cache/YASDE") + "), so the wallpaper stays even when the "
         "phone is disconnected. Pictures in JPEG, PNG and WebP are accepted; a picture that is too large or cannot be "
         "read gives " + ui("Wallpaper not changed") + ".", "A copy on the PC.")

S4 = p("Icons are shortcuts to the phone's apps. They sit on a grid and show the app's name.", lead=True) + ul([
    "To add one, open the launcher (" + rif("The launcher") + ") and drag an app onto the desktop, or right-click it "
    "there and choose " + ui("Add to desktop") + ". The message " + ui("Added to the desktop") + " confirms it.",
    "For the best-known apps YASDE uses the icons of the Kora theme; the other apps show the phone's own icon, so the style may be mixed. An app that changes its icon on the phone still shows the Kora one.",
    "Click an icon to open the app. Drag it to move it to another free cell: it never opens by accident while dragging.",
    "Right-click an icon for " + ui("Open") + " and " + ui("Remove from desktop") + ". Removing only deletes the shortcut: the app stays on the phone.",
    "When there is no free cell you get " + ui("Desktop full") + ": " + ui("Remove an icon to make room") + ".",
]) + tip("widgets are on the right side of the desktop, so icons use only the space that remains. If the window becomes "
         "too small for a cell, the icon moves to the nearest free one on screen.", "Space.") + \
    note("the Kora icon theme is the work of bikass (github.com/bikass/kora) and is distributed under the GPL-3. It comes with YASDE as separate files, with the full GPL-3 text and its credits in the documentation folder of the package.", "Credits.")

CHAPTER = ("The desktop", [
    ("What you see", S1),
    ("The desktop menu", S2),
    ("The wallpaper", S3),
    ("Icons", S4),
])
