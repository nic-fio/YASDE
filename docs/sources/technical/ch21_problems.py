from build import c, note, p, rif, table, ui, ul, warn

S1 = p("Only real problems, taken from the open points of the project plan and from the code. Each one says what is known, what is not, and where to look. "
       "None of them blocks the program.", lead=True) + table(["Problem", "State", "Where"], [
    ["Direct scan-out (offload) of the video not confirmed", "Open: to understand before a release", c("src/shell/superficie/superficie.c")],
    ["1:1 image at a fractional scale (125%, 150%)", "Function exists, not wired into the windows", c("src/shell/finestra-app/finestra_app.c")],
    ["Blur behind the glass surfaces costs drawing time; off unless the renderer is GL, NGL or Vulkan", "Known: with four video windows the median draw time goes from 5.1 to 7.1 ms with the blur on the windows only and to 8.5 ms with the launcher open above them (95th percentile 7.2 → 10.0 → 12.8 ms; measured once, CPU about +50%); with any other renderer (for example CPU drawing, " + c("GskCairoRenderer") + ") the surfaces have an almost opaque fill. Readability: over a very dark or very bright wallpaper the secondary text on the 58% veil loses contrast", c("src/shell/comune/vetro.c") + ", " + c("src/shell/finestra-app/finestra_app.c")],
    ["Relative " + c("XDG_CACHE_HOME") + " in the AppImage launcher", "Known, not blocking", c("pacchetto/AppRun")],
    ["In-memory cache on some systems", "Known: the wallpaper is lost at every reboot there", c("src/config/percorsi.c")],
    ["Switched-off features: Find my phone, Quick notes, Now playing", "Deliberate", c("src/shell/desktop/widget.c")],
    ["Restores lost when the wireless debugging restarts", "Open point, mitigated", c("telefono/base/")],
    ["Never tried live", "A list of things proven only by tests", "chapter 16"],
    ["Phone panel with no idle time; audio and video not in sync; Vulkan decoding copies frames", "Open: to decide", "Functions that are off"],
    ["Documents that disagree with the code", "A list", "this chapter"],
], "«TAB» — The known problems at a glance")

S2 = p("On one machine with an Intel GPU (GTK 4.18.6, a real screen, with " + c("GDK_DEBUG=offload") + ", even with " + c("force-offload") + ", with the Vulkan and the GL renderers) GTK never writes the line \"Attaching … GdkDmabufTexture\": the direct pass of the images to the "
       "screen does <b>not</b> appear active, neither in the surface demo nor in the prototype measured at the beginning (" + c("strumenti/prototipo-gtk.c") + ": same result).", lead=True) + ul([
    "The textures are DMA-BUF (zero refusals): the video reaches the graphics card without copies in memory; only the last step is missing.",
    "It does not depend on our widget tree (the prototype behaves the same). What is not known: whether it is the machine or the compositor, or a rule of GTK that does not say its reason.",
    "A hint for the real windows: rounded corners with clipping (" + c("finestra-app.css") + ", " + c("GTK_OVERFLOW_HIDDEN") + ") would prevent it anyway.",
    "Not urgent: the fluidity measured today is already reached without this step.",
])

S3 = p("Windows are sized so that the image is 1:1 with the physical pixels. At a fractional shell scale the widget's size is rounded to whole logical pixels (1000 physical pixels at 1.5 is 667 logical, which is 1000.5 physical), so the image is not exactly 1:1 and "
       "is resampled. Scales 1 and 2 are not affected.", lead=True) + ul([
    "The surface has the answer: " + c("ph_superficie_misura_esatta") + " (" + c("src/shell/superficie/superficie.h") + ") draws the image in a rectangle of non-integer size inside its allocation (" + c("ph_sup_rettangolo_esatto") + " in " + c("src/shell/superficie/logica.c") + "), and "
    + c("ph_fa_origine_pixel_intero") + " computes an origin on a whole physical pixel.",
    "Only the surface demo calls " + c("ph_superficie_misura_esatta") + " today: the inner windows of the real program do not. The windows must also be tried on a small screen and at scales 125% and 150% before a release.",
])

S4 = ul([
    "<b>Relative XDG.</b> With " + c("XDG_CACHE_HOME") + " relative, or a non-absolute " + c("HOME") + ", " + c("pacchetto/AppRun") + " uses the value as it is; the program does not (it ignores a relative XDG variable, as the specification says), so the two may disagree on the cache folder.",
    "<b>Memory-backed cache.</b> On the tablet used for tests " + c("~/.cache") + " lives in memory and is emptied at every reboot, so the wallpaper image saved there is lost each time (and a profile that names it then points to nothing; " + c("ph_cfg_pulisci_sfondi") + " only removes files, it does not repair the profile). "
    "To evaluate.",
    "<b>The product name and trademarks.</b> The name \"Samsung DeX\" belongs to Samsung; a non-affiliation line is present in the manuals and in the package. It is not a legal opinion.",
    "<b>Repository and project name</b> on the hosting service are still to be decided.",
    "<b>Pairing screen hint.</b> When the phone does not announce itself on the network the screen stays at " + ui("Looking for phones on the network…") + " without saying what to check (in a live test the phone had to have its wireless debugging switched off and on). Hints after a few seconds without results were proposed, and the answer is awaited.",
])

S5 = p("Some functions exist in the code and are off.", lead=True) + table(["Function", "State", "Detail"], [
    ["Find my phone", "Not offered", "The widget is not in the catalog (" + c("PH_DSK_TROVA_OFFERTO") + " is 0); on the phone the command is switched off by a constant and touches nothing (message " + c("TROVA_AVVIA") + " answers an error). Measured reasons: an audio track cannot be created from the shell process, and the switch to \"ring\" raises Android's saved ringer volume from 0 to 1 with no return. A future evolution, to take up with a test phone that is not the user's."],
    ["Quick notes", "Not offered", "The type " + c("note") + " is in the profile format but not in the catalog (it was left for after the app worked)."],
    ["Now playing, recent calls, network", "Not made", "Now playing needs a test with a real playback; the other two were to be evaluated for feasibility with pure Android, without root."],
    ["Audio in a window recording", "Not wired", "Recordings hold only video. " + c("src/video/cattura.h") + " already accepts an Opus audio track, and the contract has the " + c("con_audio") + " option, but the real services accept it and ignore it (" + c("src/shell/ossatura/sv_flussi.c") + "): the step that brings the audio packets to the recording is missing."],
    ["Breaking away from FFmpeg", "To evaluate", "FFmpeg is confined to " + c("src/video/") + " precisely so it can be replaced later."],
    ["Idle time of the physical panel", "Missing", "The idle time foreseen by the specification (4.7) and by " + c("docs/protocollo.md") + " §2.9 does not exist in the code: the panel is turned off again only at the first input from the PC with the phone \"in hand\". Either the function is missing or the protocol must be corrected: the user's decision."],
    ["Audio and video sync", "Not active", c("ph_audio_tempo_telefono_us") + " is not used by the shell: frames are shown as soon as they arrive."],
    ["Vulkan decoding copies frames", "Open: to evaluate", "The automatic order is VA-API first, then Vulkan: Vulkan decoding is used only where VA-API is missing or fails (for example a driver that offers only Vulkan Video), and there every frame is copied to memory (" + c("src/video/esporta.c") + ", " + c("PH_ESPORTA_RIPIEGO") + "): the copy-free path holds only with VA-API. "
     "It contradicts the architecture choice of never taking the copying path, and the measurements (27 frames per second with copies). To evaluate: export without copies also from Vulkan. On the machine tried VA-API is used, so it is not seen there."],
    ["Text size", "Open", "The size of texts in app windows (DeX uses 90%) is to be judged live."],
    ["Technical measurements file", "Not connected", c("src/config/profilo.c") + " has no caller outside its tests (chapter 14)."],
    ["Decoder not replaced when the device is lost in mid-stream", "To evaluate", "A decoder born with a hardware via that FFmpeg drops to software (at the first packet, or after a new configuration such as a resize) is now recreated once as software (decision line and toast). A device lost in the middle of a session is not handled in the same way: it gives only decoding errors and key-frame requests every 500 ms, the decoder is not recreated and the via is not changed. Read from the code (" + c("src/video/decodifica.c") + ", " + c("src/shell/ossatura/sv_flussi.c") + "), never run."],
], "«TAB» — Functions that are off or incomplete")

S6 = p("When the wireless debugging is switched off and on, the phone's init system kills the whole process group, the service and its guardian, and nothing restores. A process of ours cannot survive that without root. Leftovers seen once after real sessions: jars and guardian "
       "scripts in " + c("/data/local/tmp") + ", one with a volume restore never executed.", lead=True) + ul([
    "<b>Remedy in place:</b> the next service's startup <b>runs</b> the pending restores and removes the leftovers (" + c("telefono/base/it/yasde/Resti.java") + ", " + c("telefono/base/it/yasde/Custode.java") + "). The pending restore is run only if the setting still has the value YASDE put there.",
    "<b>Open points:</b> a guardian script of an old session with the same pid as the new service, if it was not run at that startup, is overwritten (it should be renamed); and the PC is not told about a restore that was never executed (a contract to write).",
    "<b>Open point:</b> reduce what YASDE changes durably, preferring what Android undoes by itself when the process dies (the screen timeout at its maximum, and the maximum media volume, are to be measured). The PC also sends a presence message every 5 minutes, and after 30 minutes "
    "without a sign of life the phone stops keeping itself awake (chapter 6).",
])

S7 = p("What the project's own record says has not been seen working in the real program.", lead=True) + ul([
    "Tried live on 24 November 2026 with the AppImage, on one machine and one phone: pairing from an empty installation, the panel turning off at connection, the bar volume at 100%, Compatibility. The AppImage with the interface was tried on one distribution; the others are not. "
    "Never tried live: every English sentence on screen (error sentences too), "
    "the pairing screen's three instruction lines, a wrong pairing code, a code pasted with spaces, authorization revoked from the phone, and wireless debugging switched off and on with the app open.",
    "The " + ui("Compatibility") + " switch was confirmed live with one app (Onecta by Daikin drawn at the right size in a phone-shaped window; the hot change is enough). The weather has not been tried live with a real forecast request.",
    "The AppImage on another distribution and with another glibc, and everything the build script cannot see (" + rif("What the build does not prove") + ").",
    "Behavior at fractional scales and on small screens (" + rif("The 1:1 image at fractional scales") + ").",
    "One phone only: other makers and Android versions are not covered.",
])

S8 = p("Places where the written documents and the code do not agree. The code is right.", lead=True) + table(["Where", "What it says", "What the code does"], [
    [c("src/finestre/finestre.h") + ", head comment, point 1", "A dragged window stays above the bar.", "It may go down until only the title bar is visible (" + c("src/finestre/finestre.c") + ")."],
    [c("src/shell/CONTRATTI.md") + ", decisions of 23 November", "The bar has no pinned favorites.", "Pinned apps exist: " + c("PhShellGestoreFissate") + " in " + c("src/shell/shell.h") + ", up to 16 per phone."],
    [c("meson_options.txt") + ", the " + c("pezzo") + " description", "Lists pieces up to transfers.", "The pieces are more (see the root " + c("meson.build") + ")."],
    [c("src/shell/finestra-app/COMPITO.md") + ", opening sizes", "296×470 and 380×420 from the mockup.", "The default is the phone's screen in points (" + rif("The density rule") + ")."],
    [c("data/en/shell-trasferimento.txt") + ", one key", "\"This version of YASDE cannot install apps on the phone yet.\"", "Apps are installed (" + rif("Installing and uninstalling APKs") + "); the key is a leftover, not used by any " + c("ph_t") + " call."],
    [c("docs/protocollo.md") + ", " + c("VIDEO_APRI"), "Does not cite the limits.", c("telefono/video/it/yasde/Video.java") + " imposes density 40 to 1000, bitrate and frame-rate limits."],
    [c("docs/protocollo.md") + ", " + c("VIDEO_EVENTO"), "Event 3 exists.", "The phone never sends it."],
    [c("telefono/pannello/it/yasde/Pannello.java") + ", comment at line 32", "The wake lock starts at startup.", "It starts from the greeting (" + c("SALUTO") + ")."],
    [c("src/adb/adb.h"), "Cites the " + c("adbkey") + " format.", "The file is " + c("chiave.pem") + "."],
    ["The specification", "Two guardians.", "The code has one (" + c("Custode") + ")."],
    [c("pacchetto/LEGGIMI.md") + ", line 169", "Cites " + c("libfuse.so.2") + ".", "The AppImage has libfuse3 inside and asks only for " + c("fusermount3") + " or " + c("fusermount") + "."],
    [c("src/config/config.h") + ", the example of widget types", "Lists eight types.", "The code accepts fifteen (" + rif("The file formats and their keys") + ")."],
], "«TAB» — Documents that disagree with the code")

CHAPTER = ("Known problems", [
    ("The known problems at a glance", S1),
    ("Direct scan-out of the video", S2),
    ("The 1:1 image at fractional scales", S3),
    ("Smaller issues", S4),
    ("Functions that are off", S5),
    ("Restores lost when the debugging restarts", S6),
    ("Never tried live", S7),
    ("Documents that disagree with the code", S8),
])
