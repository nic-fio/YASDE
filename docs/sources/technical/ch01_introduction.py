from build import CONTATTO, SITO_URL, c, note, p, rif, table, ul, warn

S1 = p("YASDE (Yet Another Samsung Dex Emulator) is a Linux program with a single window that is an emulated desktop: the "
       "apps of an Android 14+ phone run as windows inside it. This manual describes how it is built, for a developer who "
       "must change the code. It documents the code as it is today; where the code and the older design documents "
       "disagree, the code wins and the difference is listed in " + rif("Known deviations from the design documents") + ".", lead=True) + \
    p("The product has two halves. On the PC a program written in C17 (built with Meson) talks to the phone over wireless "
      "ADB, decodes the video the phone sends, plays its audio, and draws the desktop with GTK4. On the phone a small Java "
      "component, started with " + c("app_process") + ", creates virtual displays, encodes them to H.264, captures audio, "
      "injects input and answers questions about the phone. Nothing is installed on the phone and nothing needs root.") + \
    note("YASDE is an independent project, not affiliated with Samsung. “DeX” is a trademark of its owner and is used only to "
         "say which behaviour the desktop imitates.") + \
    p("The project's site is " + c(SITO_URL) + ". Questions and reports: " + c(CONTATTO) + ".")

S2 = p("Four rules shape every design choice in the code. They come from the project instructions and are enforced by "
       "review, not by the compiler.", lead=True) + \
    table(["Principle", "What it means in the code"], [
        ["Nothing installed on the phone",
         "The component is a jar copied to " + c("/data/local/tmp/yasde-<8 hex digits>.jar") + " at every start and deleted at "
         "the end. No APK, no package, no service registered with Android."],
        ["No root", "The component runs as the shell user (uid 2000), exactly like any command typed in " + c("adb shell") +
         ". It refuses connections from any other uid (" + c("Pari") + ", see " + rif("Who may talk to the component") + ")."],
        ["Plain Android APIs only",
         "Public APIs first. Hidden APIs are reached by reflection and gathered in one class per folder (" + c("Nascoste") +
         ", " + c("NascosteVideo") + ", " + c("NascosteAudio") + ", " + c("NascostePannello") + " ...), each with a check at start-up "
         "that names the ones missing on this phone. No vendor-specific code: a workaround is triggered by a measured symptom, "
         "never by the manufacturer's name."],
        ["Every change has its undo",
         "Whatever the component changes on the phone (screen timeout, media volume, panel power, minimum refresh rate) registers "
         "its undo command with the guardian " + c("Custode") + " before the change is made (" + rif("The guardian and the restore list") + ")."],
    ], "«TAB» — The four principles")

S3 = p("What is needed to build and run YASDE, as declared by the " + c("meson.build") + " files.", lead=True) + \
    table(["Where", "Requirement"], [
        ["PC, language and build", "C17, warnings as errors, Meson; tests run under AddressSanitizer. Version 2.1.0 (" + c("meson.build") + " at the root)."],
        ["PC, libraries", "OpenSSL (TLS 1.3, RSA, AES-GCM, HKDF, SPAKE2 arithmetic), FFmpeg (" + c("libavcodec") + ", " + c("libavformat") +
         ", " + c("libavutil") + ", " + c("libswscale") + "), Vulkan headers and loader (to detect hardware decoding), Opus, PipeWire (" +
         c("libpipewire-0.3") + "), GTK 4.14 or newer for the shell, pthreads."],
        ["PC, hardware", "A GPU with VA-API H.264 decoding (first choice) or Vulkan Video; the software decoder is the last resort."],
        ["Phone", "Android 14 or newer, wireless debugging turned on and the PC paired with it (" + rif("Pairing with the six-digit code") + ")."],
        ["Phone jar", "Java source level 8 against " + c("android.jar") + " (no lambdas, no method references), converted with " + c("d8") + " at " +
         c("--min-api 34") + " by " + c("telefono/costruisci.sh") + "."],
    ], "«TAB» — Requirements")

S4 = p("Code, comments and internal documents are written in Italian, so identifiers in this manual are Italian too. The "
       "first time a recurring name appears its meaning is given in brackets; the table lists the ones you meet most.", lead=True) + \
    table(["Name", "Meaning", "Where"], [
        [c("Custode"), "the guardian: the process that undoes the phone changes if the service dies", c("telefono/base")],
        [c("Registro"), "the registry where the pieces of the component register themselves", c("telefono/contratto")],
        [c("Pari"), "the peers: the check on who connects to the socket", c("telefono/base")],
        [c("Smistatore"), "the dispatcher of command-channel messages", c("telefono/base")],
        [c("Sessione"), "one video session (virtual display, encoder and app)", c("telefono/video")],
        [c("Pannello"), "the physical panel (phone screen) manager", c("telefono/pannello")],
        [c("TieniSveglio"), "“keep awake”: the wake lock", c("telefono/pannello")],
        [c("collegamento"), "the link: the state machine that keeps one phone connected", c("src/collegamento")],
        [c("componente"), "the component: PC side of the protocol", c("src/componente")],
        [c("scoperta"), "discovery of phones with mDNS", c("src/scoperta")],
        [c("abbinamento"), "pairing with the six-digit code", c("src/abbinamento")],
        [c("dati"), "phone data: apps, state, notifications, search, clipboard", c("src/dati")],
        [c("PhErrore"), "error record: a sentence in Italian saying what was being done", c("src/comune/errore.h")],
    ], "«TAB» — Italian names that recur") + \
    p("Conventions of the C code: every public name starts with " + c("ph_") + " or " + c("Ph") + " (the prefix is historical and "
      "was kept); functions return " + c("0") + " or " + c("-1") + " (pointers return " + c("NULL") + " on error) and fill a " + c("PhErrore") +
      "; no function calls " + c("exit()") + " for a failure that depends on the phone or the network. The full list of code "
      "conventions is in chapter 18.")

S5 = p("The table lists the places where this manual states that the code does not do what the design documents say. Each "
       "one is detailed in the chapter named in the last column.", lead=True) + \
    table(["Design document says", "Code does", "Chapter"], [
        ["The shell draws with Vulkan.", "The shell draws with GTK4. Vulkan is used only to detect and drive hardware H.264 decoding.",
         "see chapter 8"],
        ["Two guardians (connection and service).", "One guardian, " + c("Custode"), "see chapter 6"],
        ["Audio and video are synchronised.", "No synchronisation is active: video frames are shown as they arrive. " + c("ph_audio_tempo_telefono_us") + " exists but nothing outside " + c("src/audio") + " calls it.", "see chapter 9"],
        ["The protocol defines " + c("VIDEO_EVENTO") + " kind 3 (size changed).", "The phone code never sends it; sizes change only through the resize request and the new configuration packet.", "see chapter 8"],
        ["Hardware decoding hands frames to the screen without copies.", "Only with VA-API, the first automatic choice. Vulkan is the fallback where VA-API is missing: in that case " + c("ph_esporta") + " copies the frame to memory (" + c("PH_ESPORTA_RIPIEGO") + ").", "see chapter 8"],
        ["Audio capture starts 5 s after the drawer mirror opens.", "Audio starts 5 s after the first video stream of the session; there is no mirror at start-up (" + c("sv_audio.c") + ").", "see chapter 9"],
        ["The panel goes off again after the inactivity time chosen by the user.",
         "Only the first input from the PC turns it off again; there is no inactivity timer on the PC.", "see chapter 10"],
        ["The panel turns on when the last window closes.", "Closing the last window does not turn the panel on while YASDE is connected.", "see chapter 10"],
        ["The mouse is one finger.", "True for the left button only. The right and middle buttons pressed on the content of an app window "
         "reach the phone as real mouse buttons (context menu, open in a new tab), as DeX does.", "see chapter 10"],
        ["Minimum refresh rate set to 60 Hz when the panel goes off.",
         "Set to the highest rate with the same resolution (capped by the user's peak rate) only if the measured rate is lower.", "see chapter 10"],
    ], "«TAB» — Where code and design documents differ") + \
    warn("This manual was written from the sources. Behaviour that has not been tried on a real phone is said so in the text; "
         "the first launch on a real phone is the proof.")

CHAPTER = ("Introduction", [
    ("What YASDE is", S1),
    ("Principles", S2),
    ("Requirements", S3),
    ("Conventions and names", S4),
    ("Known deviations from the design documents", S5),
])
