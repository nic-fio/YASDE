from build import arrow, box, c, fig, note, p, rif, table, term, tree, ui, ul, warn, zone

S1 = p("The PC side is built with Meson. The root " + c("meson.build") + " declares the project " + c("yasde") + " version 2.1.0 in C with the default options " + c("c_std=c17") + ", " + c("warning_level=3") + ", "
       + c("werror=true") + " and " + c("buildtype=debugoptimized") + ", adds " + c("-D_GNU_SOURCE") + ", and then includes the pieces.", lead=True) + \
    table(["Option (" + c("meson_options.txt") + ")", "Type, default", "Meaning"], [
        [c("pezzo"), "string, " + c("tutto"), "Builds one piece (for example " + c("adb") + ", " + c("config") + ", " + c("shell-desktop") + ") or everything. With one piece the others do not exist and its tests use fakes."],
    ], "«TAB» — The only project option") + ul([
    "<b>Pieces.</b> The list in the root file: " + c("adb") + ", " + c("file") + ", " + c("scoperta") + ", " + c("abbinamento") + ", " + c("componente") + ", " + c("video") + ", " + c("audio") + ", " + c("dati") + ", " + c("finestre") + ", "
    + c("config") + ", " + c("trasferimenti") + ", " + c("collegamento") + ", " + c("meteo") + ". Each lives in " + c("src/<piece>/") + ", has its own " + c("meson.build") + " and defines " + c("<piece>_dep") + " (a dependency with its static library). "
    "Common code (" + c("src/comune/") + ": " + c("PhErrore") + ", buffers, the neutral frame descriptor) is always built.",
    "<b>With " + c("pezzo=tutto") + "</b> only the pieces listed in " + c("pezzi_pronti") + " (and the shell pieces in " + c("shell_pronti") + ") enter: a piece still being written cannot break the whole build; the coordinator adds a piece to the list when it "
    "is approved. A shell piece named " + c("shell-<piece>") + " builds with " + c("config") + " and " + c("finestre") + " (pure logic already approved), and the skeleton with all the pieces below.",
    "<b>The shell</b> (" + c("src/shell/meson.build") + ") needs GTK 4 at least 4.14 (" + c("gtk4") + "), builds the common piece and the fake backend always, and the shell pieces present. The skeleton's real services are built only if all the lower pieces "
    "are there (the flag " + c("PH_OSSATURA_SERVIZI_VERI") + "). The program is the executable " + c("yasde") + " built from " + c("src/shell/ossatura/main.c") + ". A header generated at configuration time holds one flag per piece.",
    "<b>Libraries.</b> OpenSSL 3 (ADB TLS and weather), FFmpeg's " + c("libavcodec") + ", " + c("libavutil") + ", " + c("libavformat") + ", " + c("libswscale") + " and Vulkan (the " + c("video") + " piece: decoding, PNG and MP4 capture), Opus and PipeWire (" + c("audio") + "), "
    "GTK 4 (the shell), gdk-pixbuf (the desktop piece, wallpaper decoding), and threads. FFmpeg stays closed inside " + c("src/video/") + " and GTK inside " + c("src/shell/") + ".",
    "<b>Resources.</b> Each shell piece compiles its CSS with " + c("glib-compile-resources") + " (" + c("--manual-register") + ", source files such as " + c("barra.gresource.xml") + ") as " + c("/it/yasde/shell/<piece>.css") + "; the generated string is long, so resources "
    "are compiled apart with " + c("-Wno-overlength-strings") + ". Translations and CSS are inside the executable: the program reads no data file at run time.",
    "<b>Translations.</b> A generator built first (" + c("genera_traduzioni") + ") turns the " + c("data/en/*.txt") + " files into a C source at build time (chapter 14).",
    "<b>Phone component.</b> Not in Meson: " + c("telefono/costruisci.sh") + " compiles the Java with " + c("javac") + " and D8 into " + c("telefono/yasde.jar") + " (chapter 16). The AppImage carries the jar next to the executable, which looks for it in its own folder.",
])

S2 = p("Builds do not run on the machine where the sources are edited. They run on a build server, inside a build container, started by " + c("strumenti/prova.sh") + " (chapter 16), which copies the sources there. The container's recipe is "
       + c("strumenti/Containerfile") + ": a Debian 13 image with the compilers (GCC and Clang), Meson, Ninja, " + c("pkg-config") + ", Valgrind, GDB, the JDK, OpenSSL, Vulkan with its validation layers and compiler for shaders, FFmpeg's development "
       "libraries, VA-API and DRM, SDL3, Opus, PipeWire, GTK 4 and the broadway virtual screen, and an Italian locale. The Android SDK (platform 35, build-tools 35.0.0) is mounted read-only into the container; "
       "a container is new for every run.", lead=True) + ul([
    "A build server path and the container's name are development equipment and are not part of the program; nothing in the executable or the package depends on them.",
    "Builds go in " + c("build-<piece>") + " folders next to the sources on the server: " + c("build-<piece>-veloce") + " is the optimized variant of a shell piece (the demos for the tablet), " + c("build-veloce") + " is the optimized whole.",
    "Big files never go in " + c("/tmp") + ".",
])

CONTENT = fig(
    zone(20, 14, 860, 250, "AppDir of the AppImage")
    + box(40, 48, 190, 52, "AppRun", "own folder, LD_LIBRARY_PATH, GSettings", "navy")
    + box(250, 48, 190, 52, "usr/bin/yasde + yasde.jar", "program and phone component", "blue")
    + box(460, 48, 190, 52, "usr/lib/", "GTK 4, GLib, Pango, Opus, OpenSSL…", "blue")
    + box(670, 48, 190, 52, "FFmpeg, libtiff, cairo", "rebuilt here, no GPL parts", "green")
    + box(40, 124, 190, 52, "gdk-pixbuf loaders", "webp loader + cache template", "dark")
    + box(250, 124, 190, 52, "usr/lib/gio-vuoto", "empty: no GIO modules", "dark")
    + box(460, 124, 190, 52, "glib-2.0/schemas", "GTK GSettings, compiled", "dark")
    + box(670, 124, 190, 52, "usr/share/doc/ (legal)", "copyright, source offer, lists", "navy")
    + box(40, 198, 400, 46, "Left to the host system", "glibc, Vulkan, VA-API, DRM, X11, XCB, Wayland, fontconfig, PipeWire", "dark")
    + box(460, 198, 400, 46, "Not included", "full icon themes (only the Kora icons of the table), other image loaders, GIO modules, GTK themes and engines", "dark"),
    900, 284, "«FIG» — What is inside the AppImage and what is left to the system")

S3 = p("The program ships as one file, " + c("YASDE-2.1-x86_64.AppImage") + " (Linux x86_64), plus a source archive, " + c("YASDE-2.1-sorgenti.tar") + ", published on the same release page. "
       "The scripts are " + c("pacchetto/costruisci.sh") + " (starts on the development machine and sends the work to the build server over SSH, to run in the build container) and " + c("pacchetto/dentro.sh") + " (runs inside; mounted read-only: the optimized program from " + c("prova.sh veloce")
       + " and the jar from " + c("prova.sh telefono") + "). The log ends with " + c("uscita N") + ", and the script exits with the same N. A lock prevents two builds at once; a cache of downloaded sources and of the already built FFmpeg "
       "survives between runs, the rest is redone from zero. The script checks its folder variables (" + c("PACCHETTO_LAVORO") + ", " + c("PACCHETTO_VELOCE") + ", " + c("PACCHETTO_TELEFONO") + ", " + c("PACCHETTO_LOG") + ") against a strict pattern "
       "before touching the server, and refuses ones that contain " + c("..") + " or overlap.", lead=True) + CONTENT + ul([
    "<b>The launcher</b> (" + c("pacchetto/AppRun") + ") always uses its own folder (an inherited " + c("APPDIR") + " is ignored), puts the package's libraries first in " + c("LD_LIBRARY_PATH") + ", the package's GSettings schemas first in "
    + c("XDG_DATA_DIRS") + ", points " + c("GIO_MODULE_DIR") + " to an empty folder (no system GIO modules, which are tied to another GLib), and then " + c("exec") + "s " + c("usr/bin/yasde") + ".",
    "<b>Image loaders.</b> The loaders cache template has a placeholder path; " + c("AppRun") + " resolves it at every start into " + c("${XDG_CACHE_HOME:-$HOME/.cache}/YASDE/yasde-pixbuf-<pid>.cache") + ", the only file the package writes on the PC, "
    "in a folder the specification allows. If the package path contains " + c("\\") + ", " + c("\"") + ", " + c("&") + ", " + c("|") + " or a newline, or the folder is not writable, it gives up with a warning and webp does not open (png and jpeg are inside the library). "
    "Files of past runs are removed at start: those whose pid is dead and those older than 24 hours.",
    "<b>No integration.</b> No " + c(".desktop") + " file is installed on the PC; the one inside the package serves only " + c("appimagetool") + " and declares " + c("X-AppImage-Integrate=false") + ".",
    "<b>Public documents</b> in the package are only in English (" + c("README.txt") + ", " + c("SOURCE-OFFER.txt") + ", " + c("LIST.txt") + ", " + c("SYSTEM-REQUIREMENTS.txt") + ", " + c("open-meteo.txt") + ", " + c("type2-runtime/NOTE.txt") + "), with the line that the "
    "project is not affiliated with Samsung.",
    "<b>Size.</b> About 18 MB for the AppImage and about 45 MB for the AppDir; the source archive is about 295 MB.",
])

S4 = p("Everything downloaded is fetched into the build server's work area (never into the repository), only over HTTPS, with a fixed version and a checksum verified before use.", lead=True) + table(["Item", "Version", "How it is verified"], [
    [c("appimagetool"), "1.9.1", "SHA-256 as declared by the release"],
    ["AppImage runtime (" + c("type2-runtime") + ")", "20251108", "SHA-256 as declared by the release; the tag's source tarball has the checksum seen at the first download (if it changes, the script stops)"],
    ["FFmpeg", "7.1.5", "Tarball from ffmpeg.org; the fixed checksum was compared with the signed Debian sources index (" + c("Checksums-Sha256") + ")"],
    ["libtiff, cairo", "Debian trixie sources", c("apt-get source") + " (apt checks against the signed index) plus the orig checksum fixed in the script; if a version is gone from the current archive, a dated snapshot is used (inside the run only)"],
    [c("libfuse") + " 3.15.0 and " + c("squashfuse") + " 0.5.2", "—", "Versions and sums are those the runtime's own script verifies; the script rechecks them in that file"],
], "«TAB» — Downloaded tools and how they are checked") + \
    p("The sources of every included Debian library are saved in the source archive (with a " + c("SHA256SUMS") + " file), together with those of libtiff and cairo and the FFmpeg tarball.")

S5 = p("The package must contain nothing that forces the combined work under GPL terms, and offer the sources of what it contains. The build script fails when any of the following is not true.", lead=True) + ul([
    "<b>FFmpeg is rebuilt</b>, LGPL 2.1 or later: " + c("--disable-gpl --disable-nonfree --disable-everything --disable-autodetect --disable-programs") + ", shared, with only what the code uses: the H.264 decoder and parser with VA-API and Vulkan "
    "(software fallback is the decoder itself), the PNG encoder, the MP4 muxer (the container goes to an I/O context written by the program, so no protocols), and swscale. The script checks for \"LGPL version 2.1 or later\" in the configure output and in the library, "
    "and that every soname the executable asks for exists.",
    "<b>libtiff and cairo are rebuilt</b> from Debian's sources, with Debian's security patches, because the distribution's builds pull in " + c("libjbig") + " and " + c("liblzo2") + " (GPL-2+ without exception). libtiff without jbig, lerc, lzma, zstd, webp and deflate; cairo without lzo. "
    "The sonames stay the same (" + c("libtiff.so.6") + ", " + c("libcairo.so.2") + " and its siblings); cairo is replaced entirely, so the process has one version. The script fails if these libraries are not resolved from the versions built here, "
    "if " + c("libjbig") + ", " + c("liblzo2") + " or " + c("libLerc") + " end up in the package, or appear among the NEEDED entries of any file in it.",
    "<b>GPL check.</b> The script fails if an included package has a " + c("copyright") + " file that cites a GPL (not LGPL), or is not machine-readable, and is not in the list of reasoned exceptions inside " + c("dentro.sh") + " (21 cases at the time of writing: dual terms, or GPL only on parts not distributed, each with its reason). "
    "The check is mechanical (the machine-readable header lines of those files) plus those hand-written exceptions.",
    "<b>Source offer.</b> " + c("SOURCE-OFFER.txt") + " (in the package's documentation folder) states that the sources are published next to the AppImage, in the source archive, and lists exact versions and origins. <b>The archive must be published together with the AppImage.</b>",
    "<b>Legal texts inside.</b> Each library's " + c("copyright") + "; the texts of common legal texts that those files cite (from the distribution's common texts folder) in " + c("common-texts/") + " (the script fails if one is missing); for FFmpeg, libtiff and cairo the texts from their sources.",
    "<b>The AppImage runtime</b> (MIT, statically linked to libfuse3 LGPL-2.1 and squashfuse BSD-2): the sources of the tag, libfuse and squashfuse are in the archive; legal texts and notes are in " + c("type2-runtime/") + " in the package documentation folder" + ". "
    "For zstd, zlib, mimalloc and musl embedded in the runtime the notices are reproduced; the exact versions cannot be derived, which is irrelevant for terms that ask only for the notice.",
    "<b>Kora icons</b> (GPL-3, separate files). The step " + c("pacchetto/icone-kora.sh") + " (called by " + c("dentro.sh") + " before the final texts, and again as " + c("verifica") + " at the end of the checks; it can also run alone) "
    "fetches the Kora theme by " + c("bikass") + " (" + c("github.com/bikass/kora") + ") at a fixed commit (" + c("ff8ee3ce") + "…, 19 Nov 2026) into a copy outside the repository, reused only if it is at that commit and unmodified; a failed download stops the build, never a package without icons. "
    "Only the icons named in " + c("data/icone-kora.txt") + " (104 rows, " + c("<Android package> <icon name>") + ") are converted to 128×128 PNG with a transparent background (" + c("rsvg-convert") + ", not part of the package) into " + c("usr/share/yasde/icone-kora/") + ", "
    "with an exact copy of the table as " + c("tabella.txt") + " and the SVG sources in " + c("sorgenti/") + " (real files, no links). A malformed row, an empty table, a name missing from the theme or a link leaving the theme stops the build, with the same limits as the program. "
    "In the package's documentation folder (" + c("usr/share/doc/") + ") go the GPL-3 text of the theme and a credits file (commit, what was done, third-party marks), cited in " + c("LIST.txt") + ", " + c("README.txt") + " and " + c("SOURCE-OFFER.txt") + ". "
    "The final check verifies the files: table, a PNG and an SVG for each name, PNG size, GPL-3 text and credits. Nothing of Kora is in the repository. The terms of YASDE itself are still to be decided; the GPL-3 applies only to these separate files.",
    "<b>Weather.</b> " + c("open-meteo.txt") + " (in the same documentation folder) carries the credits and terms of Open-Meteo and GeoNames data (CC BY 4.0). No certificate authority is included in the package: the program looks for the host system's (chapter 15), and the script proves, with the package's " + c("libcrypto") + ", "
    "that the same sequence loads the authorities of the container.",
    "<b>Naming.</b> The project name is YASDE. The script fails if a forbidden project name appears, in any capitalization, in file names, in our texts, in the strings of the executable or of the jar, with no exception, " + c("AppRun") + " included. The script first proves the check itself: a phrase with the forbidden name "
    "in any capitalization must trigger it.",
    "<b>Dependencies.</b> Every NEEDED of every file in the package is either included or in the list of what is left to the system; " + c("ldd") + " reports no \"not found\". Required: glibc 2.38 or newer, PipeWire, libva, Vulkan and libdrm, X11 and Wayland client libraries, fontconfig, and the kernel's FUSE with " + c("fusermount3") + " or " + c("fusermount") + " "
    "(or " + c("--appimage-extract-and-run") + "; the runtime has libfuse3 linked statically). Without " + c("libpipewire-0.3.so.0") + ", " + c("libva.so.2") + " and " + c("libva-drm.so.2") + " or " + c("libvulkan.so.1") + " the program does not start. The script writes into " + c("SYSTEM-REQUIREMENTS.txt") + " only the NEEDED libraries not included and the highest symbol versions required.",
    "<b>Self tests.</b> " + c("AppRun --help") + " must answer, also with " + c("LD_BIND_NOW=1") + "; png, jpeg and webp are saved and re-read with the "
    "bundled loaders, and the same test must fail without the webp loader (otherwise it proves nothing); certificate loading is proved as above.",
])

S6 = warn("what the build script cannot prove: the startup with the interface, real Vulkan, VA-API and PipeWire, hardware decoding with the minimal FFmpeg, MP4 recording and PNG capture with the bundled libraries, opening of TIFF images "
          "(libtiff is included only because GTK needs it; TIFFs compressed in the removed ways do not open), the AppImage on another distribution or glibc, the rebuilt runtime from the archive's sources, the compatibility of the bundled "
          + c("libz") + " and " + c("libzstd") + " with every Mesa driver loaded in the same process, and that libtiff and cairo behave correctly (only that their sonames resolve and that " + c("--help") + " answers). "
          "The weather TLS connection to the real server was tried once by hand from the AppImage.", "Not verified.") + \
    p("Packages the script installs when missing, for that run only: " + c("file") + " (required by " + c("appimagetool") + "), the webp pixbuf loader, and when rebuilding " + c("nasm") + " and " + c("cmake") + ". They belong in the container image.")

CHAPTER = ("Building and packaging", [
    ("Meson and the project options", S1),
    ("The build container", S2),
    ("The AppImage", S3),
    ("Downloaded tools", S4),
    ("Legal texts and the checks that fail the build", S5),
    ("What the build does not prove", S6),
])
