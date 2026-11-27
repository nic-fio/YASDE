# Manuale utente di YASDE: capitolo 5.

from build import c, note, p, rif, shot, steps, table, tip, ui, ul, warn


S1 = p("Widgets are cards on the desktop. YASDE draws them on the PC with data it reads from the phone. They "
       "are not Android widgets: only the ones in the catalog below exist. Like the taskbar they are dark glass, with white text; "
       "the menus and dialogs stay light.", lead=True) + \
    shot("widgets.png", "«FIG» — The Add widget dialog") + steps([
        "Right-click an empty part of the desktop and choose " + ui("Add widget…") + ".",
        "In the dialog " + ui("Add widget") + " press " + ui("Add") + " on the card you want. The message " + ui("Widget added") + " confirms it. You can add several; close the dialog with " + ui("Done") + ".",
        "A new widget goes to the first free place, starting from the top of the right edge, with a gap of at least 12 pixels from the others. Drag it where you want it: it snaps to a grid of 8 pixels.",
        "Right-click a widget to change it (see below).",
    ]) + ul([
        "<b>Shape.</b> Most widgets are rounded tiles with very soft corners. The analog clock (" + ui("Classic analog") + " and " + ui("Minimal analog") + ") and " + ui("Battery details") + " are round; " + ui("Next alarm") + " and " + ui("Quick switches") + " are capsules (the ends are half circles).",
        "<b>Size.</b> There is no size menu. Move the pointer over a widget: a small corner appears at its bottom right. Drag it to make the widget larger or smaller. The size changes in steps of 8 pixels and the top left corner stays where it is. Each widget has a smallest and a largest size, and it cannot go outside the desktop. Round widgets stay round; a capsule stays at most 128 pixels tall.",
        "<b>From the keyboard.</b> Press Tab until the corner has the focus, then use the arrow keys: each press changes the size by 8 pixels.",
        "<b>More space, more content.</b> A larger widget shows more: for example the storage legend, more notifications or the next days of the weather. What does not fit is left out or cut off.",
        "The right-click menu of a widget has " + ui("Style") + " (for the widgets that have styles), "
        + ui("City") + " (for the dual time zone clock), " + ui("Location…") + " (for the weather) and " + ui("Remove widget") + ".",
        "When you change the style, the size is kept if the new style has the same shape; if the shape changes (for example from the digital clock to an analog one) the widget goes back to a standard size for the new style.",
        "Size, style and position are saved for each phone.",
    ]) + tip("a click on some widgets opens an <b>app</b> of the phone in a YASDE window (see the catalog). It opens the app itself, never a precise page or a conversation. If the phone does not have that app, the click does nothing; a notification is the exception: YASDE always asks the phone to open the app that sent it.")

S2 = table(["Widget", "What it shows", "Notes"], [
    [ui("Clock"), "Time and date.", "Six styles, see below."],
    [ui("Calendar"), "Today: the day number in large letters, then the events of the day, each with a colored bar and its time. The one going on now is marked " + ui("NOW") + ".", "Read-only. Click an event to open the phone's Calendar app, when YASDE knows which app it is. When the widget is narrow the date is written in one line at the top instead."],
    [ui("Phone status"), "Battery level with a bar; larger sizes add " + ui("Network") + ", " + ui("Physical panel") + " and " + ui("Screen") + " (locked or unlocked).", "Values the phone does not give are left empty, never invented."],
    [ui("Recent notifications"), "The latest notifications of the phone, without opening the panel: for each one the icon of the app (its initials in a colored circle if the icon is not known), the title, the text and the time.", "A notification hidden in YASDE does not count (" + rif("The notifications panel") + "). The larger the widget, the more notifications. Click one to open the app that sent it."],
    [ui("Phone storage"), "The <b>free</b> space in large letters, with the total (" + ui("free of") + " …), a bar divided into colored slices and a legend.", "The slices are images, videos, audio, apps and " + ui("Other") + " (what is used and not counted in the first four). A category the phone cannot report is left out. The bar needs enough height and the legend needs room for all its entries, otherwise it is left out. Click to open the phone's Settings app, when the phone has it."],
    [ui("Weather"), "Current conditions and forecast, with the place name.", "Six styles. It uses the Internet: " + rif("The weather widget") + "."],
    [ui("Month calendar"), "The month with a dot on the days that have events; click a day to see its events.", "Three styles: " + ui("Month") + ", " + ui("Week") + ", " + ui("Agenda") + ". Clicking the title or an event opens the phone's Calendar app, when YASDE knows which app it is."],
    [ui("Next alarm"), "A capsule with an amber circle, the time of the phone's next alarm in large letters, and under it the day and how long is left (for example " + ui("Tomorrow · in 7 hours") + ").", ui("No alarm") + " when there is none. Click to open the alarm app, when the phone says which one it is and has it."],
    [ui("Battery details"), "A round widget: a ring with the percentage in the middle and the charging state under it (" + ui("Charging") + ", " + ui("Discharging") + "…). With more room it adds the temperature, the health and the voltage.", "The ring is green; it is coral when the level is 20% or less and the phone is not charging. Values the phone does not give are left out. Click to open the phone's Settings app, when the phone has it."],
    [ui("Quick switches"), "A capsule with three round buttons: " + ui("Ringer") + ", " + ui("Do not disturb") + " and " + ui("Flashlight") + ". With enough room the name and the state are written under each button.", "Each click changes the phone: the ringer goes from " + ui("Rings") + " to " + ui("Vibrates") + " to " + ui("Silent") + " and back. A full button means on (for the ringer: rings), only an outline means that the ringer vibrates, an empty button means off (the ringer is silent). A switch the phone does not allow is inactive."],
    [ui("Quick notes"), "A notepad: the title " + ui("Quick notes") + " at the top and under it a text field of several lines, where the text wraps by itself.", "Starts at 250×180 (smallest 200×130, largest 480×640) and is resized from its corner like the others. Drag the widget by the title: from the text field it does not move, because there the mouse selects text. The wheel scrolls the text, right-click opens the widget menu, and copy and paste are on the keyboard shortcuts. There is one text for each phone: several Quick notes widgets show the same text, and when you change phone you see the text of that phone. It is saved while you type (within about a second), before you change phone and when YASDE closes. The limit is 2047 bytes of UTF-8, about 2000 characters, fewer with accented letters or emoji; beyond it nothing more is accepted, not even by pasting. New lines and tabs are kept, other control characters are dropped. The text stays on the PC, in the phone's profile: it is not sent to the phone and does not go in the logs."],
    [ui("Frequent apps"), "The apps you open most often.", "Counted by YASDE for each phone; " + ui("No app opened yet") + " until you open some."],
    [ui("Recent transfers"), "The latest files sent or received in this session.", "Click a received file to open it on the PC, or a sent file to open its folder."],
], "«TAB» — The widget catalog") + \
    note("two widgets are not in the catalog: Find my phone and Now playing. They are not available in this version.", "Not available.")

S3 = p("Styles change how a widget looks without changing the data. Choose one from the right-click menu, under "
       + ui("Style") + ".", lead=True) + table(["Widget", "Styles"], [
    [ui("Clock"), ui("Digital") + ", " + ui("Classic analog") + " (with a seconds hand), " + ui("Minimal analog") + ", "
     + ui("Flip tiles") + ", " + ui("In words") + " and " + ui("Dual time zone") + ". The last one adds a second city, chosen under "
     + ui("City") + ": " + ui("London") + ", " + ui("New York") + ", " + ui("Los Angeles") + ", " + ui("Moscow") + ", "
     + ui("Dubai") + ", " + ui("Beijing") + ", " + ui("Tokyo") + " or " + ui("Sydney") + "."],
    [ui("Month calendar"), ui("Month") + ", " + ui("Week") + ", " + ui("Agenda") + "."],
    [ui("Weather"), ui("Now") + " (icon, temperature, condition, high and low; it grows with the widget: see below), " + ui("Hours") + " (the next hours), "
     + ui("Week") + " (the next days), " + ui("Compact") + " (one line), " + ui("Today") + " (feels like, wind, rain, humidity) and "
     + ui("Curve") + " (the temperature over the next hours)."],
], "«TAB» — Widget styles") + p("The " + ui("Now") + " style of the weather grows with the widget. At the smallest size it shows the icon and the temperature. "
    "With more width it adds the place, the condition and the high and low. With more height it adds the curve of the temperature over the next hours, and with still more height the next days, each with a bar from the low to the high. "
    "The line " + ui("Weather data by Open-Meteo.com") + " is always at the bottom, at every size.")

S4 = p("The " + ui("Weather") + " widget is the only part of YASDE that uses the Internet, and only while a weather widget is on the desktop.", lead=True) + ul([
    "The place comes from the phone's position. YASDE asks the phone, which gives a position rounded to about 500 meters; the precise position never leaves the phone. The name of the place is worked out by Android on the phone from that rounded position; on many phones that uses an online service of Google or of the phone's maker.",
    "If the phone does not give a position, the widget says so and offers " + ui("Choose a location…") + ". The same dialog is under " + ui("Location…") + " in the widget menu. Type a city in " + ui("City name") + " and press " + ui("Search") + " (or Enter): the search is sent only when you press it. Pick a result, or " + ui("None") + " to forget the city. A city chosen by hand is used only when the phone gives no position.",
    "The widget always shows " + ui("Weather data by Open-Meteo.com") + " and, in the search dialog, " + ui("Places from GeoNames, via Open-Meteo.com") + ".",
    "While data is being fetched you see " + ui("Updating…") + "; if the service does not answer you see " + ui("Forecast not available") + " and the words “last data at” followed by a time when older data exists.",
]) + warn("adding the widget is the choice: there is no consent dialog. Remove the widget and YASDE stops contacting the weather service. Details are in " + rif("What leaves the PC: the weather") + ".", "Internet.")

CHAPTER = ("Widgets", [
    ("Adding and arranging widgets", S1),
    ("The catalog", S2),
    ("Styles", S3),
    ("The weather widget", S4),
])
