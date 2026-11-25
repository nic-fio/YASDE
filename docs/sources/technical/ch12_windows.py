from build import arrow, box, c, diamond, fig, note, p, rif, table, text, ui, ul, warn, zone

S1 = p("The rules of the inner windows are pure logic in " + c("src/finestre/") + ": no graphics, no phone, no internal clock "
       "(the time arrives as a parameter, in milliseconds, non-decreasing), one thread. The shell piece "
       + c("src/shell/finestra-app/") + " only draws what these rules decide and feeds them events. The header "
       + c("src/finestre/finestre.h") + " is the contract; the tests are in " + c("src/finestre/prova_finestre.c") + ".", lead=True) + \
    p("Coordinates are the shell's logical units (physical pixels divided by the shell scale), with the origin at the top-left "
      "corner of the usable area, which is the root minus the 48 px bar (" + c("PH_FIN_BARRA_H") + "). Pointers are " + c("double") + ", "
      "rectangles are integers. Return values follow the project convention: 0 or -1 with a " + c("PhErrore") + "; a pointer or id of 0 is an error. "
      "At most " + c("PH_FIN_MAX") + " (64) windows are open at once.") + \
    table(["Constant", "Value", "Meaning"], [
        [c("PH_FIN_TITOLO_H"), "50", "Window title bar"],
        [c("PH_FIN_CROM_H"), "70", "Title bar plus technical foot (" + c("PH_FIN_PIE_H") + ", 20 px); taken from the content only when the foot is on, otherwise only the 50 px of the title"],
        [c("PH_FIN_PULS"), "30", "Round title buttons, centred vertically in the title bar"],
        [c("PH_FIN_PULS_DIST") + ", " + c("PH_FIN_PULS_MARG_DX") + ", " + c("PH_FIN_PULS_MARG_SX"), "4, 14, 22", "Gap between two buttons; margin from the right edge to the right-hand group; margin from the left edge to Back"],
        [c("PH_FIN_ANGOLO"), "18", "Resize handle, bottom right"],
        [c("PH_FIN_MIN_W") + " × " + c("PH_FIN_MIN_H"), "288 × 180", "Minimum size when resizing (288 = 22 + 7 buttons of 30 + 6 gaps of 4 + 14, plus 18 px: the seven buttons fit without overlapping, the app name has no room)"],
        [c("PH_FIN_VISIBILE_MIN"), "96", "Pixels of a window that must stay inside the area horizontally"],
        [c("PH_FIN_AGG_LATO") + ", " + c("PH_FIN_AGG_ALTO"), "14, 10", "Snap distance from the side edges and from the top"],
        [c("PH_FIN_ASSESTA_MS"), "300", "Settle time without size changes before the phone is asked"],
        [c("PH_FIN_DENS_PASSO"), "20", "Step of the automatic effective density, in dpi"],
        [c("PH_FIN_FLUSSO_MIN") + ", " + c("PH_FIN_FLUSSO_TETTO"), "64, 2560", "Smallest side and default cap of the stream"],
        [c("PH_FIN_DENSITA_RIF"), "160", "Reference dpi of a shell at scale 1"],
        [c("PH_FIN_DENSITA_MIN") + " – " + c("PH_FIN_DENSITA_MAX"), "50 – 2000", "Accepted densities"],
        [c("PH_FIN_MARGINE_AREA"), "16", "Margin left per side when a default size must shrink to fit"],
    ], "«TAB» — Constants of " + c("src/finestre/finestre.h"))

S2 = p("Order, snapping and the gestures follow the mockup unless a comment in the header says " + c("SCELTA") + " (choice).", lead=True) + ul([
    "<b>Opening</b> (" + c("ph_fin_apri") + "): if the app already has a window it is restored, brought to the front and made active "
    "(no new window). Otherwise a cascade places it at x = 70 + n·40, y = 30 + n·30 with n the number of open windows (the mirror excluded) "
    "modulo 6, then brings it inside the area. The mirror opens at x = 640, y = 20 with no cascade. With the preference " + c("riapri_dove_eri") + " (reopen where you were) on, position and size come from the remembered rectangle of the app.",
    "<b>Depth.</b> Windows are kept in depth order, minimized ones included (" + c("ph_fin_ordine") + "). Closing leaves no window active; "
    "minimizing an active window leaves none active either.",
    "<b>Dragging</b> (title bar): the window follows the pointer. At the bottom it may go past the area until only the title bar is visible "
    "(" + c("y") + " is limited to the area height minus the title height); at the top never, so the title stays grabbable; horizontally at "
    "least 96 px stay visible. A click without movement neither detaches nor snaps.",
    "<b>Snapping</b> (resizable windows only): pointer within 14 px of the left or right edge gives the left or right half, within 10 px of the "
    "top gives maximize; " + c("ph_fin_gesto_anteprima") + " returns the rectangle for the preview. A snapped window follows the area; on the first "
    "movement of a drag it detaches and recovers its previous size, keeping the pointer at the same horizontal proportion of the title.",
    "<b>Resizing</b> (corner handle): size = original + displacement, at least 288×180, at most up to the right edge and the bar. No request goes "
    "to the phone during the gesture.",
    "<b>Maximize / restore:</b> toggles, and restore returns to the arrangement before (also from snapped). Rotate (Ctrl+R) swaps the "
    "width and height of the content, within the area.",
    "<b>Minimize</b> is a separate flag from the arrangement: restoring returns to what it was. A minimized window's stream is paused "
    "(" + c("ph_fa_pausa_voluta") + "): the shell also pauses every stream when the host window is not visible, and before pausing any open touch is closed.",
    "<b>Bar click</b> (" + c("ph_fin_clic_barra") + "): in front goes minimized, minimized goes restored, otherwise it comes to the front.",
    "<b>When the area shrinks</b> (host window, full screen, scale: " + c("ph_fin_imposta_host") + "): each normal window remembers the size it was given by "
    "the user (the one at opening, or the last resize, rotation or return to resizable) and its effective size is that one, reduced only as much "
    "as needed to fit; so a window shrunk by a small area grows back when the area grows. A window that was fully inside is brought fully inside; "
    "one the user had pushed out is moved only as much as keeps the title bar and 96 px visible.",
    "<b>Registration in progress</b> (" + c("ph_fin_imposta_registrazione") + "): resizing, unsnapping, maximize, snap and rotate are refused and nothing is asked of the "
    "phone until it ends (moving is still allowed), because the size of a recording cannot change.",
])

S3 = p("A window's input goes to the phone in the video's own coordinates. " + c("ph_fin_trova") + " finds the topmost non-minimized window "
       "under a point and returns its zone: " + c("PH_FIN_ZONA_TITOLO") + ", " + c("PH_FIN_ZONA_PULSANTE") + " (with the button), "
       + c("PH_FIN_ZONA_CONTENUTO") + ", " + c("PH_FIN_ZONA_ANGOLO") + " or none.", lead=True) + ul([
    "For the content the point is converted to the video: " + c("vx = x · video_w / drawn width") + ", always inside " + c("0 ≤ vx < video_w") + " "
    "and truncated by the injector. The phone also receives " + c("video_w") + " and " + c("video_h") + ", and drops the event if they no longer "
    "match its display.",
    "At rest the image keeps the video's proportions, with bands at the sides when the area has other proportions. The shell therefore applies the same "
    "formula to the image rectangle (" + c("ph_fa_traduci_immagine") + " with " + c("ph_superficie_rettangolo_immagine") + "): a point in a band is not input; "
    "a touch already begun is clamped into the image, because a touch that started in a window stays its own until release.",
    "In automatic density the image is drawn 1:1 in the top-left corner of the content (" + c("PhFinInfo.utile_w") + " × " + c("utile_h") + " physical pixels, "
    "multiples of 8); the margin to the right and below (up to 7 physical pixels) is filled with the window background and a point in it is content with "
    + c("valido") + " false. The origin must also fall on a whole physical pixel (" + c("ph_fa_origine_pixel_intero") + ").",
    "Wheel: one notch is 120; a touchpad sends fractions that are accumulated without losing the remainder (" + c("ph_fa_passi_rotella") + "). The conversion "
    "from GTK's direction to Android's is done once, by the caller.",
    "<b>Ctrl + wheel is zoom.</b> The window turns it into a simulated pinch at the pointer position (a protocol message with steps in 120ths; chapter 10). "
    "The normal wheel is not sent in that case. The gesture opens at the first step, sends one step per event, and ends " + c("PH_FA_PIZZICO_FINE_MS") + " "
    "(250 ms) after the last step, when Ctrl is released, when the pointer leaves, on focus loss, and before any touch. The phone always closes the gesture "
    "(by itself after 400 ms without steps). Touchpad pinch gestures (" + c("GtkGestureZoom") + ") are also translated (a ratio of 1.25 per 120 steps), and a "
    "touchscreen pinch cancels the first finger's touch when the zoom starts; during a window move or resize zoom is refused.",
    "<b>Reserved keys</b> (" + c("ph_fa_tasto_riservato") + "): Ctrl+R, Ctrl+W, Ctrl+Shift+C and F11 never go to the phone. Printable characters without Ctrl and Alt "
    "are sent as text (so capitals, accents and foreign layouts arrive right); everything else is an Android key code (" + c("ph_fa_tasto_android") + ").",
])

NODES = (
    box(30, 52, 200, 54, "Content in physical px", "w × h of the inner window", "dark")
    + diamond(380, 79, 190, 66, "Fixed density?")
    + box(540, 20, 330, 50, "Fixed: stream = content × density / 160", "pixels reduced above the cap, density kept", "navy")
    + box(540, 92, 330, 54, "Automatic: stream = content, 1:1", "density = 160 × shell scale (steps of 20), never above the phone's", "blue")
    + box(540, 168, 330, 54, "Narrower than the phone (w < Wt)", "density lowered so the app sees Wt points", "green")
    + arrow(232, 79, 284, 79)
    + arrow(475, 70, 538, 46, label="yes", lx=496, ly=52)
    + arrow(475, 90, 538, 118, label="no", lx=496, ly=116)
    + arrow(705, 148, 705, 166)
)
FIG = fig(NODES, 900, 238, "«FIG» — How the stream size and the density of an app window are chosen (" + "automatic rule on the right" + ")")

S4 = p("Every app window follows one rule, like Samsung DeX: the stream is the content of the window in physical pixels (1:1), and the "
       "density is 160 times the shell scale, so one dp is about one logical pixel of the shell. The rule is in " + c("misura_di_ex") + " in "
       + c("src/finestre/finestre.c") + " and is explained in the header comment of " + c("finestre.h") + ".", lead=True) + FIG + ul([
    "<b>Stream.</b> Under the cap the stream is exactly the usable content (multiples of 8, minimum 64). Above the cap " + c("PH_FIN_FLUSSO_TETTO") + " "
    "(2560 per side; the larger side decides, both sides are scaled by the same factor) the pixels are reduced in proportion, never the density: "
    "<code>flusso = content · density_app / 160</code> is only the old fixed-density rule.",
    "<b>Density.</b> " + c("ceil20(max(50, 160 · k))") + " with k the shell scale (or less when above the cap), never above the phone's own density. "
    "Steps of 20 dpi (" + c("PH_FIN_DENS_PASSO") + "). Nothing depends on a threshold: " + c("PhFinInfo.tablet") + " (the short side of the content is at least "
    + c("PH_FIN_TABLET_LATO") + " = 600 logical px) is information only and changes no calculation.",
    "<b>Minimum width.</b> An app never sees fewer points of width than the phone's screen has (" + c("Wt") + ", for example 384 for 1080 px at 450 dpi). "
    "With f = min(1, w / Wt), under Wt the density is the integer " + c("floor(flux_w · 160 / Wt)") + " (rounded down, so the points seen, truncated as Android does, "
    "stay at least Wt): 384 gives 160 dpi, 288 gives 120, 240 gives 100. " + c("PhFinInfo.fattore") + " and " + c("punti_w") + ", " + c("punti_h") + " report it.",
    "<b>Fixed density.</b> If the profile has " + c("densita_app") + " greater than 0 the user's value is used, fixed (" + c("ph_fa_densita_automatica") + " is false and "
    + c("ph_fin_imposta_densita_automatica(false)") + "): the flux is " + c("content · density / 160") + " and pixels shrink above the cap. The profile accepts 72–960 dpi or 0.",
    "<b>The default size</b> of a phone-shaped window and of the mirror is the phone's screen in points (" + c("px · 160 / density") + ": 1080×2340 at 450 dpi is 384×832, "
    "one point is one logical pixel), always portrait for phone-shaped and generic windows, reduced in proportion to fit the area (margin 16 per side, content never "
    "narrower than min(288, area)) and snapped to multiples of 8 physical pixels. Without the phone's screen the fallbacks are " + c("PH_FIN_TEL_W") + " × " + c("PH_FIN_TEL_H")
    + " (296 × 490) for phone-shaped windows and the mirror, and " + c("PH_FIN_GENERICA_W") + " × " + c("PH_FIN_GENERICA_H") + " (384 × 442) for generic windows.",
    "<b>When the phone is asked.</b> " + c("ph_fin_avanza") + " runs on every frame (a tick callback) and returns the windows whose stream size or density has been "
    "unchanged for 300 ms and differs from the last request: the shell sends it as a resize with density (" + c("ph_superficie_ridimensiona_densita") + "). "
    "During the gesture, minimized, or while recording, nothing is asked.",
])

S5 = p("An app that cannot be resized stays as it is: the phone says so (" + c("PhSrvApp.solo_verticale") + ": the launch activity declares a portrait "
       "orientation and is not resizable), and its window keeps a phone shape.", lead=True) + ul([
    "No resize from the edges (" + c("puo_ridimensionare") + " false), no maximize (" + c("puo_ingrandire") + "), no snapping (" + c("puo_agganciare") + "); the commands show as "
    "disabled, and calling the function anyway returns -1 without changing anything. It can still be moved, minimized, restored and closed. Rotate is refused too.",
    "The information may arrive after the window opens (" + c("ph_fin_imposta_solo_verticale") + "): an enlarged, snapped or wide window then returns to the phone shape in "
    "the same place; the stream changes size once, with the request at settled movement. On a minimized window the shape changes at once and the request leaves 300 ms "
    "after restore; while recording, the shape changes but nothing goes to the phone until the end.",
    "The window sizes are recomputed (reduced in proportion) when the area shrinks, never stretched.",
])

S6 = p("Some apps draw themselves small, gathered in a corner, because they use the density of the phone's real screen while the window, the activity and the "
       "process see that of the virtual screen. The user decides, per app, with the " + ui("Compatibility") + " switch.", lead=True) + ul([
    "<b>Where:</b> the ⋮ menu of the window (" + c("PH_POPUP_ALTRI_COMANDI") + "), after " + ui("Rotate") + " and before " + ui("Close app") + ", as a check item with "
    "a smaller grey line under it (" + ui("For apps that look too small") + "). Disabled during a recording and absent in the mirror.",
    "<b>State:</b> per package, in the phone's profile (the " + c("[[compatibilita]]") + " list, at most " + c("PH_CFG_MAX_COMPAT") + " = 128), written only when the "
    "user changes the switch. The next time the app opens it is already on.",
    "<b>Effect</b> (" + c("ph_fin_imposta_compatibilita") + "): the window uses the fixed-density rule with the phone's real density D, whatever the global rule says. "
    "With f = min(1, w / Wt) the stream is " + c("(w / f, h / f) · D / 160") + " in multiples of 8, minimum 64. Above the cap the pixels are reduced in proportion and "
    "the density stays D (the app sees fewer points, and the surface enlarges the image): the density is never lowered, since the difference of density is what shrinks "
    "these apps. The image is not 1:1: the GPU scales it, and that is expected. A 384×832-point window at 450 dpi asks for about 1080×2340, the phone's screen.",
    "<b>On change:</b> the window stays open, the new size and density are asked with a resize (the next " + c("ph_fin_avanza") + " answers at once, without the settle wait). "
    "No session is reopened. It was confirmed live that the app adapts immediately.",
    "<b>Refused</b> (-1, nothing changes): the mirror, an unknown id, and a recording in progress.",
])

S7 = p("The mirror is the window that shows the phone's real screen (the app with an empty package, key " + c("phone") + " in " + c("finestre.h") + "). It is opened from the phone "
       "status popup with " + ui("Show the phone screen") + " and closed from the same switch.", lead=True) + ul([
    "<b>Fixed shape:</b> like a non-resizable app it cannot be resized, maximized or snapped (even if created with " + c("solo_verticale") + " false). Its proportion is that of "
    "the phone's screen as it is, landscape included. " + c("ph_fin_misura_flusso") + " returns an error for it and " + c("ph_fin_avanza") + " never names it: the size of the "
    "stream is decided by the phone.",
    "<b>Stream size:</b> " + c("ph_ossatura_misura_specchio") + " takes the phone's screen to multiples of 8 inside 64 to 1920 per side without deforming it (cap of 1920); the "
    "window first asks with " + c("SPECCHIO_FLUSSO_W") + " × " + c("SPECCHIO_FLUSSO_H") + " = 288 × 512 as a placeholder that the backend ignores for the mirror.",
    "<b>No density:</b> density is 0 and the Compatibility entry is absent. If the phone rotates while the mirror is open, the window stays as it is and the image appears with bands.",
    "<b>Closing:</b> the mirror closes without touching the recents list; closing an app window removes the app from the recents (" + c("togli_task") + ").",
])

S8 = p("What is saved, and where.", lead=True) + table(["What", "Where", "Written by"], [
    ["Remembered rectangle per app (" + c("[[finestra]]") + ", at most 64)", "Phone profile", "Windows piece, through " + c("ph_fa_ricordi_a_profilo") + "; the mirror is never saved"],
    ["Fixed density (" + c("[app] densita") + ")", "Phone profile", "Read by the windows piece; 0 means the automatic rule"],
    ["Compatibility list", "Phone profile", "Windows piece, only on user change"],
    ["Opening counts (" + c("[[apertura]]") + ")", "Phone profile", "Windows piece: one count per new window of an app, not for the mirror, a raise or a restore"],
    ["" + c("esc_indietro") + ", " + c("riapri_dove_eri"), "Preferences", "Read only; the program has no interface that changes preferences"],
], "«TAB» — Persistent data of the windows (formats in " + rif("Files and folders on the PC") + ")") + \
    note("the app names given to " + c("finestre.h") + " are at most " + c("PH_FIN_APP_MAX") + " (48) bytes, shorter than a package name. The shell keeps a table package ↔ short key "
         "(" + c("a1") + ", " + c("a2") + ", …; " + c("phone") + " for the mirror) in " + c("PhFaChiavi") + ", and converts in both directions, also for the remembered rectangles.",
         "Short keys.") + \
    warn("the comment at the top of " + c("src/finestre/finestre.h") + " (point 1 of the deviations) still says a dragged window stays above the bar; the code (" + c("src/finestre/finestre.c") + ") lets it go "
         "down until only the title bar is visible.", "Header out of date.")

CHAPTER = ("Windows and density", [
    ("The window rules", S1),
    ("Order, snapping and gestures", S2),
    ("Input translation", S3),
    ("The density rule", S4),
    ("Apps that cannot be resized", S5),
    ("Compatibility", S6),
    ("The mirror window", S7),
    ("What is remembered", S8),
])
