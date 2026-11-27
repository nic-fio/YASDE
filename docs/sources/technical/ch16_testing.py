from build import c, note, p, rif, table, term, ul, warn

S1 = p("Tests live with the code. One piece is one folder and has its own " + c("meson.build") + ", which defines " + c("<piece>_dep") + " and its " + c("test(...)") + " entries; the tests of a piece are in "
       "its folder (" + c("prova_*.c") + ", or a " + c("prova/") + " subfolder). What a piece depends on is replaced in its tests by a fake written in its own folder, so with " + c("-Dpezzo=<name>") + " the other "
       "pieces are not even built.", lead=True) + ul([
    "<b>Build flags.</b> C17, warnings as errors (" + c("warning_level=3") + ", " + c("werror=true") + " in the root " + c("meson.build") + "), tests compiled with AddressSanitizer and UndefinedBehaviorSanitizer "
    "(" + c("-Db_sanitize=address,undefined") + ").",
    "<b>Thread sanitizer.</b> It cannot be mixed with AddressSanitizer, so the pieces whose correctness is about threads rebuild their sources in a second variant with " + c("b_sanitize=thread") + " (" + c("-fsanitize=thread") + "): "
    "ADB, the component, data, transfers, the connection (" + c("collegamento-finto-tsan") + ") and weather, each in its own " + c("meson.build") + ".",
    "<b>Fakes.</b> There are several, each for a different layer: " + c("src/adb/finto_telefono.c") + " (a fake ADB daemon in C), " + c("prove/telefono_finto/telefono_finto.py") + " (a fake ADB daemon in Python with TLS, "
    "channels, " + c("shell,v2") + " and " + c("sync:") + "; it simulates neither pairing, nor " + c("exec:") + ", nor compression), " + c("src/componente/prova/finto.c") + " (a fake component), "
    + c("src/dati/prova/finto.c") + ", " + c("src/audio/prova/finto_canale.c") + ", " + c("src/file/finto_canale.c") + ", " + c("src/trasferimenti/prova/finto.c") + " and the fake TLS side of the weather piece.",
    "<b>Fake lower pieces for the real services.</b> " + c("src/shell/ossatura/finti_sotto.c") + " replaces connection, data, component and the rest, with fakes that \"sleep like the network\": the tests check that no call blocks the GTK "
    "thread, that borrowed sessions are given back, and that closing with requests in flight is clean.",
    "<b>Rules the tests must obey</b> (from " + c("CLAUDE.md") + "): a test must fail if the code it proves is removed (verify it); never sleep to \"give time\" to another thread, wait for a signal with a deadline; "
    "every new error branch has its test; an intermittent failure is never ignored nor worked around by widening a tolerance: it is captured and explained.",
])

S2 = p("All builds and tests run on a build server, inside the build container, never on the machine where the sources are edited and never with large files in " + c("/tmp") + ". The one entry point is "
       + c("strumenti/prova.sh <piece>") + ". The script packs the tree, sends it over SSH to the server, runs the container under a per-piece lock (two agents asking for the same piece queue up instead of deleting each "
       "other's work), writes the whole log on the server, prints the last 25 lines, and the last line is always " + c("uscita N") + " (N is the exit code). If a run fails, the log "
       "and the test details are kept with the time in the name, so an intermittent failure is not lost on the next run.", lead=True) + \
    table(["Command", "What it builds and runs"], [
        [c("prova.sh adb|file|scoperta|abbinamento|componente|video|audio|dati|finestre|config|trasferimenti|collegamento|meteo"), "One C piece with its tests, with AddressSanitizer and UBSan"],
        [c("prova.sh shell-<piece>"), "One shell piece (" + c("shell-finto") + ", " + c("shell-ossatura") + ", " + c("shell-superficie") + ", " + c("shell-finestra-app") + ", " + c("shell-barra") + ", "
         + c("shell-launcher") + ", " + c("shell-desktop") + ", " + c("shell-trasferimento") + ", " + c("shell-sistema") + ", " + c("shell-connessione") + "): runs only the " + c("shell") + " suite, then rebuilds the same piece with "
         + c("-O3") + " and no checks (the warnings that optimization shows, and the demos for the tablet)"],
        [c("prova.sh tutto"), "All pieces that are finished and approved (" + c("pezzi_pronti") + " and " + c("shell_pronti") + " in the root " + c("meson.build") + "), with every test. Only when the work is complete: the others compile your tree"],
        [c("prova.sh veloce"), "Everything together, optimized (" + c("-Dbuildtype=release") + "), without memory checks and without tests: for speed measurements and for the executables to try on the tablet. A C piece is finished only if "
         "this also passes, because optimization shows warnings that the test build does not"],
        [c("prova.sh telefono") + ", " + c("prova.sh telefono-<piece>"), "The phone component's jar (all pieces or one), with the JVM tests of each folder"],
    ], "«TAB» — The commands of " + c("strumenti/prova.sh")) + \
    term("""
$ strumenti/prova.sh config
$ strumenti/prova.sh shell-desktop
$ strumenti/prova.sh veloce
""", "Examples") + \
    p("Inside the container " + c("strumenti/dentro.sh") + " does the work. For the shell it also starts a virtual screen, " + c("gtk4-broadwayd") + " on display " + c(":5") + ", waiting up to 10 s for its socket instead of pausing at random. "
      "The default container image has it; with an image that lacks it the tests that need a screen end with exit code 77 and are reported as skipped. The image recipe is " + c("strumenti/Containerfile") + " (a Debian base with the build tools, "
      "Vulkan, FFmpeg and VA-API headers, SDL3, Opus, PipeWire, GTK 4, an Italian locale to prove that numbers are read and written the same under any system language, and the broadway tools).")

S3 = p("Every shell piece has a <b>logic test</b> without a screen (" + c("prova_<piece>.c") + ", with leak checking) and a <b>screen test</b>: the piece's demo run with " + c("--prova 2000") + " under the virtual screen with "
       + c("G_DEBUG=fatal-criticals") + ". Logic is kept apart from the widgets (pure functions in the piece's own folder, for example " + c("src/shell/superficie/logica.c") + ") precisely so it can be tested without a screen. "
       "Logic tests ignore the leaks of third-party libraries listed in " + c("src/shell/soppressioni-lsan.txt") + "; screen tests turn leak detection off because GTK leaves reachable memory at the end.", lead=True) + ul([
    "<b>Demos.</b> " + c("demo-<piece>") + " runs one piece over the fake phone; " + c("demo-ossatura") + " runs the whole shell. On the tablet: " + c("strumenti/demo-sul-tablet.sh <piece>") + " takes the optimized demo from the server "
    "(options " + c("--misura WxH") + ", " + c("--dmabuf") + ", " + c("--senza-comandi") + "); in the demo, F5 locks or unlocks, F6 drops the connection for 5 s, F7 sends a notification, F8 closes the last app.",
    "<b>Photographs.</b> " + c("strumenti/foto-demo.sh <piece>") + " opens the demo on an invisible screen of the tablet (a headless compositor, so the user's screen is not disturbed) and saves PNG photographs to compare with the mockup: "
    "sizes, colors, order, texts. The demo's own output is also read, and must hold no GTK critical warning.",
    "<b>Manual screenshots.</b> " + c("docs/sources/img/fai-schermate.sh") + " redoes them all over the fake phone (invented data only). The apps of the fake phone get generic icons of the Kora theme: the script copies from the build server the few icons named in " + c("docs/sources/img/icone-demo.txt") + " (names only; the images are not in the repository), converts them into its cache and points the demo to them with " + c("YASDE_ICONE_KORA") + ", the same way the package does. The " + c("desktop-widgets.png") + " one uses the step " + c("vetrina") + " of " + c("demo-ossatura") + ", which loads a fixed arrangement of eleven widgets (" + c("src/shell/desktop/demo_vetrina.c") + ", built only into the demo).",
    "<b>Other tools</b> in " + c("strumenti/") + ": " + c("yasde-prova.c") + " (a command line client for the lower pieces), " + c("finestra-prova.c") + " (a bare window for an app, SDL3), " + c("misura-flussi.c") + " (several video "
    "streams in parallel), " + c("prototipo-gtk.c") + " (the GTK prototype that measured the frame path), " + c("app-sul-tablet.sh") + ", " + c("finestra-sul-tablet.sh") + ". They are built only with " + c("pezzo=tutto") + " and are not part of the program.",
    "<b>Scope of " + c("tutto") + ":</b> it builds all pieces in " + c("pezzi_pronti") + " plus the shell pieces in " + c("shell_pronti") + ", so a piece still being written cannot break the build of the others; the coordinator adds a piece "
    "to those lists when it is approved.",
])

S4 = p("The component's Java code is tested in two ways.", lead=True) + ul([
    "<b>On the container's JVM, without Android</b> (run by " + c("telefono/costruisci.sh") + " for every piece that has a " + c("prove.sh") + "): the piece's test classes are kept as " + c(".java.txt") + " files and copied to a "
    "temporary folder at test time, so they never enter the jar. For example " + c("telefono/base/prove.sh") + " runs " + c("ProvaProtocollo") + ", " + c("ProvaSmistatore") + ", " + c("ProvaPari") + " and " + c("ProvaResti") + ".",
    "<b>The build</b> compiles the contract and the pieces' sources at Java source level 8 (" + c("-source 8 -target 8") + ", so no lambdas or method references: anonymous classes) against " + c("android.jar") + " of the SDK "
    "(platform 35), with " + c("-Xlint:all -Werror") + ", and converts with D8 to " + c("telefono/yasde.jar") + " (" + c("--min-api 34") + "). The pieces are " + c("base") + ", " + c("video") + ", " + c("audio") + ", " + c("dati") + ", "
    + c("archivio") + ", " + c("appunti") + ", " + c("pannello") + ", " + c("controlli") + " plus " + c("contratto") + " (always present).",
    "<b>On the real phone:</b> each piece has Python scripts in its " + c("prove/") + " folder (for example " + c("telefono/base/prove/prova_telefono.py") + ") that push the jar through the phone's ADB and drive it. "
    "Those use the standard " + c("adb") + " tool already paired with the phone.",
])

S5 = p("There is one test phone, so real-phone tests run one at a time. The rules come from " + c("CLAUDE.md") + " and exist because earlier reviews found the opposite.", lead=True) + ul([
    "<b>The turn.</b> Before touching the phone take the turn with a directory created in the user's cache (" + c("mkdir") + " fails if it exists: wait and retry every 20 to 30 s). Remove it with " + c("rmdir") + " only if your own "
    + c("mkdir") + " succeeded in this very run (script variable \"my turn\" and a " + c("trap") + " or " + c("finally") + " that removes only one's own). If the " + c("mkdir") + " fails or times out, the script exits: never go on without the turn, "
    "never remove someone else's.",
    "<b>Do not wake or reconnect.</b> Never wake, unlock or reconnect the phone: if it is not awake and connected, stop and report it.",
    "<b>Touch only what you created.</b> Note the pids of the processes launched and the names of the files written, and clean up only those (never \"the first " + c("it.yasde") + " found\", never a wildcard on the guardian's scripts). "
    "Before touching anything, read-only: if the phone already runs a component or has a guardian file (another test, or the real program), exit.",
    "<b>Settings.</b> No setting changed without a restore; the starting values are saved and compared line by line at the end. No volume or user setting written \"to prepare\" the test. Nothing is started on the real screen. "
    "No audible sound without the user's explicit consent.",
    "<b>Every " + c("adb") + " command has a deadline;</b> the cleanup runs in a " + c("finally") + ". No serial number or address in any file: fake values (for example " + c("R5CT0000000") + ") in examples.",
    "<b>The component restores the phone</b> in every case: a thread that dies for an unexpected " + c("Throwable") + " must leave the phone as it was, and a failed action that would leave an abnormal state (screen off, volume changed) "
    "is retried, not abandoned (chapter 6).",
])

S6 = p("The tests prove logic, formats, protocol, concurrency and the fake phones. Several things are only proven live or not at all, and the manual says so rather than imply otherwise.", lead=True) + ul([
    "<b>Tried live on 24 November 2026</b> with the AppImage, on one machine and one phone: pairing from an empty installation, the panel turning off by itself at connection, the bar volume starting at 100%, and the Compatibility switch (the hot change is enough). "
    "<b>Never tried live</b> at the time of writing: every English sentence on screen, the "
    "weather (a real forecast request: only the TLS handshake was tried by hand), the pairing screen's hints when the phone does not announce itself.",
    "<b>Screen tests</b> run on a virtual broadway screen: they prove that the widgets build and respond, not how they look on a real display, with the Vulkan renderer, or under fractional scales (125%, 150%), which have still to be tried.",
    "<b>The AppImage</b> was checked for its contents, legal texts and dependencies by the build script, and its startup with the interface was tried live on one distribution with one phone. "
    "Its use on another distribution or glibc, and the parts that trial did not touch (MP4 recording and PNG capture with the bundled libraries, Vulkan decoding), were not verified (chapter 17).",
    "<b>Direct scan-out</b> of video frames (offload) is not confirmed on the one machine where it was looked for (" + rif("The known problems at a glance") + ").",
    "<b>One phone, one Android.</b> Behavior on other makers' phones and Android versions beyond the one tested is not covered.",
])

CHAPTER = ("Testing", [
    ("Tests of the PC pieces", S1),
    ("Running the tests", S2),
    ("Shell tests, demos and photographs", S3),
    ("Tests of the phone component", S4),
    ("Rules for tests on the real phone", S5),
    ("What is not covered", S6),
])
