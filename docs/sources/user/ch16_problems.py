# Manuale utente di YASDE: capitolo 16.

from build import c, key, note, p, rif, table, tip, ui, ul, warn


S1 = p("Find the symptom, read the cause, apply the fix.", lead=True) + table(["Symptom", "Cause", "What to do"], [
    ["Nothing happens when you start the file", "The file is not executable, or the system lacks FUSE (the program " + c("fusermount3") + " or " + c("fusermount") + ").", "Make it executable (" + rif("Starting YASDE") + "). Install the fuse3 or fuse package of your distribution."],
    ["The program closes at once", "A system library is missing: Vulkan, VA-API or PipeWire (the program needs all three to start).", "Install the Vulkan, libva and PipeWire packages of your distribution (" + rif("What you need") + ")."],
], "«TAB» — Starting YASDE")

S2 = table(["Symptom", "Cause", "What to do"], [
    [ui("Looking for phones on the network…") + " does not go away", "The phone does not announce itself on the network.", "Turn Wireless debugging off and on, check the Wi-Fi network, press " + ui("Search again") + " (" + rif("Stuck on Looking for phones on the network") + ")."],
    [ui("Pairing failed. Check the code and try again."), "Wrong or expired code.", "Reopen " + ui("Pair device with pairing code") + " on the phone and type the new code."],
    ["The code field is shown but pairing never ends", "YASDE does not find the pairing service.", "Type the address and port in " + ui("The phone does not show up? Type the address and port") + " (" + rif("If the phone does not show up") + ")."],
    [ui("Looking for the phone") + " after a drop", "The phone lost the Wi-Fi network or Wireless debugging turned off.", "Turn it back on; press " + ui("Reconnect now") + " or " + ui("Search again") + "."],
    [ui("Phone locked"), "The phone is locked.", "Unlock it: the windows return where they were."],
    [ui("There are several phones on the network: I am using the first one found."), "More than one phone is announcing itself.", "YASDE uses one phone at a time; switch Wireless debugging off on the others."],
    ], "«TAB» — Connection problems")

S3 = table(["Symptom", "Cause", "What to do"], [
    ["An app looks too small inside its window", "The app uses the density of the phone's real screen.", "Tick " + ui("Compatibility") + " in the window's " + ui("More commands") + " menu (" + rif("Size and the Compatibility option") + ")."],
    ["A window cannot be resized or maximized", "It is a portrait-only app, the phone mirror, or the window is recording.", "This is intended (" + rif("Windows that cannot be resized") + ")."],
    ["The title bars and panels are not translucent", "The computer has no graphics acceleration, so YASDE draws with the processor and keeps the title bars, the taskbar and the panels almost opaque.", "Nothing to fix: everything else works the same."],
    ["Text on the panels or on the taskbar is hard to read", "The wallpaper is very dark or very bright, and the panels and the taskbar are translucent glass.", "Choose a less extreme wallpaper (" + rif("The wallpaper") + ")."],
    [ui("I cannot open the app"), "The app's picture could not be started.", "Try again from the launcher."],
    [ui("The app stream stopped: the window was closed."), "The app's picture stream ended, and YASDE closed the window.", "Open the app again from the launcher."],
    [ui("Hardware video decoding unavailable") + " (the card adds: " + ui("Video uses the CPU: apps are less smooth. Install VA-API or Vulkan video drivers.") + ")", "No video driver of the graphics card (VA-API or Vulkan) could be used, or the graphics card cannot decode or show this video, so YASDE decodes the video with the processor. The card stays on screen for about nine seconds and appears at most once each time the phone connects, if the video falls back to the processor.", "Apps work but look less smooth. Check that the VA-API or Vulkan video drivers of your graphics card are installed, then restart YASDE."],
    [ui("This app protects its screen: the content cannot be shown."), "The app forbids capturing its screen (banking apps, for example).", "Nothing can be done in YASDE: use the app on the phone."],
    ["The " + ui("Compatibility") + " option cannot be ticked", "Recording is in progress, or " + ui("Too many apps in compatibility mode.") + ".", "Stop the recording; untick it for apps you no longer need."],
    [ui("Taskbar full") + " or " + ui("Desktop full"), "16 pinned apps, or no free cell for the icon.", "Unpin or remove something first."],
    [ui("Wallpaper not changed"), "The picture is too large or cannot be read.", "Choose another picture."],
], "«TAB» — Problems with windows and apps")

S4 = table(["Symptom", "Cause", "What to do"], [
    ["No sound", "The sound starts about five seconds after the first app opens; or YASDE's volume is at 0 or muted.", "Wait; check the " + ui("Volume") + " popup (" + rif("The volume control") + ")."],
    ["The phone's volume is at maximum", "YASDE raises it while it captures sound.", "It goes back when YASDE closes (" + rif("What happens to the phone's volume") + ")."],
    ["The phone's screen is dark", "YASDE turned the panel off on purpose.", "Touch the phone, or use " + ui("Physical panel") + " (" + rif("The physical panel") + ")."],
    ["The phone's screen came on by itself", "You touched it, a call arrived, or the phone was unlocked: the “in hand” rule.", "It turns off again at your next input from the PC."],
    [ui("The phone did not turn the physical panel off"), "The phone is locked, in a call, or refuses.", "Try again later."],
    [ui("Screenshot not saved") + " or " + ui("I cannot save the screenshot"), "No picture was ready yet, or the folder cannot be written.", "Wait a moment and try again; check the " + c("Pictures/YASDE") + " folder."],
    [ui("Recording interrupted"), "The window was minimized or hidden, or the connection dropped.", "Start a new recording; the file made so far is kept."],
    [ui("The phone's location is turned off."), "The weather widget needs the phone's position.", "Turn the phone's location on, or choose a city with " + ui("Location…") + "."],
    [ui("YASDE is not allowed to use the phone's location."), "The phone has not allowed it.", "Choose a city with " + ui("Location…") + "."],
    [ui("Weather forecast not available right now."), "The weather service does not answer, or there is no Internet.", "Try again later."],
    [ui("The phone's security check did not approve the app."), "The phone considers the app unsafe.", "Install it only if you trust where it comes from. YASDE cannot bypass the phone's check (" + rif("Installing an app from an APK") + ")."],
    ["Files YASDE left on the phone", "The connection fell before YASDE could clean up.", "They are removed at the next start of YASDE."],
], "«TAB» — Problems with sound, screen, files and weather")

CHAPTER = ("Problems and solutions", [
    ("Problems when starting", S1),
    ("Connection problems", S2),
    ("Problems with windows and apps", S3),
    ("Sound, screen, files and weather", S4),
])
