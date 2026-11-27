# Manuale utente di YASDE: capitolo 2.

from build import c, note, p, rif, steps, table, tip, ul, warn, ui


S1 = p("YASDE is a single file for Linux PCs with an x86-64 processor. There is nothing to install: you download "
       "the file, make it executable and start it. The file is about 20 MB and already contains GTK 4 and the video "
       "libraries it needs.", lead=True) + \
    table(["Needed", "Details"], [
        ["Operating system", "Linux, 64-bit x86, with glibc 2.38 or newer, and a graphical session (X11 or Wayland)."],
        ["Graphics", "A Vulkan driver for your graphics card. Without " + c("libvulkan") + " the program does not start. "
         "Video decoding uses VA-API when the card offers it, so " + c("libva") + " and " + c("libva-drm") + " must be present."],
        ["Sound", "PipeWire: its library " + c("libpipewire-0.3") + " must be present (the program does not start without it) "
         "and the PipeWire service must be running to hear sound."],
        ["System libraries", "libdrm, fontconfig, and the usual X11, XCB and Wayland client libraries; any recent desktop has them."],
        ["Starting the AppImage", "The AppImage carries its own FUSE 3 library, but the system must provide the program " + c("fusermount3") + " or " + c("fusermount") + " (package fuse3 or fuse) with FUSE enabled."],
        ["Phone", "Android 14 or later, with " + ui("Wireless debugging") + " (see " + rif("Before you start") + ")."],
        ["Network", "The PC and the phone on the same Wi-Fi network."],
    ], "«TAB» — What the PC needs") + \
    note("the list comes from what the package asks of the system. YASDE has not been tried on every Linux "
         "distribution, so a system older than the one described may need more.", "Other systems.")

S2 = steps([
    "Download " + c("YASDE-2.3-x86_64.AppImage") + " and put it in any folder you like.",
    "Make it executable: in your file manager open the file's properties and allow running it as a program, or type "
    + c("chmod +x YASDE-2.3-x86_64.AppImage") + " in a terminal.",
    "Double-click the file, or run it from a terminal. A single YASDE window opens.",
    "At the first start the window shows " + ui("Connect your phone") + ": go on with " + rif("The Connect your phone screen") + ".",
]) + tip("the window can be switched to full screen with the " + ui("Full screen") + " button in its title bar or with the "
         "F11 key (" + rif("Keyboard") + ").", "Full screen.")

S3 = p("YASDE does not touch the rest of your system.", lead=True) + ul([
    "It adds no menu entry, no launcher and no icon: the file runs from where you put it.",
    "Apart from the files you ask for (screenshots, recordings and downloads, " + rif("Screenshots") + " and " + rif("Downloading files from the phone") + "), it writes on the PC only in two folders: " + c("~/.config/YASDE") + " for settings and the phone's key, and "
    + c("~/.cache/YASDE") + " for the wallpaper image and a small image-loader file (" + rif("Where YASDE keeps its data") + ").",
    "On the phone it installs nothing (" + rif("What YASDE changes on the phone") + ").",
])

S4 = p("To update, replace the file with the new one. To remove YASDE completely, delete the file and the two "
       "folders " + c("~/.config/YASDE") + " and " + c("~/.cache/YASDE") + ". The phone needs no cleaning: whatever YASDE "
       "put on it is removed when YASDE closes. To be sure, close YASDE normally with the phone connected before deleting it. If the last session was cut short (for example the Wi-Fi dropped), start YASDE and close it once: it cleans up what was left.") + \
    warn("deleting " + c("~/.config/YASDE") + " also deletes the key that pairs the PC with the phone, together with your "
         "wallpaper, icons and widgets. You will have to pair the phone again.", "Settings are lost.")

CHAPTER = ("Installation", [
    ("What you need", S1),
    ("Starting YASDE", S2),
    ("What YASDE adds to your system", S3),
    ("Updating and removing", S4),
])
