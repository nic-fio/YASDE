# Manuale utente di YASDE: capitolo 5.

from build import c, note, p, rif, shot, steps, table, tip, ui, ul, warn


S1 = p("Widgets are small cards on the desktop. YASDE draws them on the PC with data it reads from the phone. They "
       "are not Android widgets: only the ones in the catalog below exist.", lead=True) + \
    shot("widgets.png", "«FIG» — The Add widget dialog") + steps([
        "Right-click an empty part of the desktop and choose " + ui("Add widget…") + ".",
        "In the dialog " + ui("Add widget") + " press " + ui("Add") + " on the card you want. The message " + ui("Widget added") + " confirms it. You can add several; close the dialog with " + ui("Done") + ".",
        "Drag the widget where you want it: it snaps to a grid of 8 pixels.",
        "Right-click a widget to change it (see below).",
    ]) + ul([
        "Each widget has three sizes: " + ui("Small") + ", " + ui("Medium") + " and " + ui("Large") + ". Larger sizes show more details.",
        "The right-click menu of a widget has " + ui("Size") + ", " + ui("Style") + " (for the widgets that have styles), "
        + ui("City") + " (for the dual time zone clock), " + ui("Location…") + " (for the weather) and " + ui("Remove widget") + ".",
        "Size, style and position are saved for each phone.",
    ])

S2 = table(["Widget", "What it shows", "Notes"], [
    [ui("Clock"), "Time and date.", "Six styles, see below."],
    [ui("Calendar"), "Today's upcoming events, read from the phone.", "Read-only."],
    [ui("Phone status"), "Battery level with a bar; larger sizes add " + ui("Network") + ", " + ui("Physical panel") + " and " + ui("Screen") + " (locked or unlocked).", "Values the phone does not give are left empty, never invented."],
    [ui("Recent notifications"), "The latest notifications of the phone, without opening the panel.", "A notification hidden in YASDE does not count (" + rif("The notifications panel") + ")."],
    [ui("Phone storage"), "Used space, the total space and a bar.", "At medium size it adds one line, at large size up to four, with the space taken by images, videos, audio and apps; a category the phone cannot report is left out."],
    [ui("Weather"), "Current conditions and forecast, with the place name.", "Six styles. It uses the Internet: " + rif("The weather widget") + "."],
    [ui("Month calendar"), "The month with a dot on the days that have events; click a day to see its events.", "Three styles: " + ui("Month") + ", " + ui("Week") + ", " + ui("Agenda") + ". Clicking the title or an event opens the phone's Calendar app, when YASDE knows which app it is."],
    [ui("Next alarm"), "Day and time of the phone's next alarm.", ui("No alarm") + " when there is none."],
    [ui("Battery details"), "Level and charging state; larger sizes add temperature, health and voltage.", "Values come from the phone."],
    [ui("Quick switches"), "Three switches: " + ui("Ringer") + ", " + ui("Do not disturb") + " and " + ui("Flashlight") + ".", "Each click changes the phone: the ringer goes from " + ui("Rings") + " to " + ui("Vibrates") + " to " + ui("Silent") + " and back. A switch the phone does not allow is inactive."],
    [ui("Frequent apps"), "The apps you open most often.", "Counted by YASDE for each phone; " + ui("No app opened yet") + " until you open some."],
    [ui("Recent transfers"), "The latest files sent or received in this session.", "Click a received file to open it on the PC, or a sent file to open its folder."],
], "«TAB» — The widget catalog") + \
    note("three widgets are not in the catalog: Find my phone, Quick notes and Now playing. They are not available in this version.", "Not available.")

S3 = p("Styles change how a widget looks without changing the data. Choose one from the right-click menu, under "
       + ui("Style") + ".", lead=True) + table(["Widget", "Styles"], [
    [ui("Clock"), ui("Digital") + ", " + ui("Classic analog") + " (with a seconds hand), " + ui("Minimal analog") + ", "
     + ui("Flip tiles") + ", " + ui("In words") + " and " + ui("Dual time zone") + ". The last one adds a second city, chosen under "
     + ui("City") + ": " + ui("London") + ", " + ui("New York") + ", " + ui("Los Angeles") + ", " + ui("Moscow") + ", "
     + ui("Dubai") + ", " + ui("Beijing") + ", " + ui("Tokyo") + " or " + ui("Sydney") + "."],
    [ui("Month calendar"), ui("Month") + ", " + ui("Week") + ", " + ui("Agenda") + "."],
    [ui("Weather"), ui("Now") + " (icon, temperature, condition, high and low), " + ui("Hours") + " (the next hours), "
     + ui("Week") + " (the next days), " + ui("Compact") + " (one line), " + ui("Today") + " (feels like, wind, rain, humidity) and "
     + ui("Curve") + " (the temperature over the next hours)."],
], "«TAB» — Widget styles")

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
