from build import arrow, box, c, fig, flow, note, p, path, rif, seq, steps, table, text, ul, warn, zone

S1 = p("A window of a phone app sends the user's mouse and keyboard to the phone as input events. The PC side is " + c("ph_input_*") + " in " + c("src/componente/componente.c") + " and the stream code in " + c("src/shell/ossatura/sv_flussi.c") + "; the phone side is " + c("telefono/video") + ": " + c("Input") + " (the channel), " + c("Iniettore") + " (the injection), " + c("Gesti") + " (what a touch or pinch means) and " + c("Pizzico") + " (the pinch geometry).", lead=True) + \
    p("<b>On the PC.</b> " + c("sv_flusso_input") + " accepts an event only if the stream is active and not paused and, for touch and wheel, the video size it was computed on equals the stream's current size (the phone would discard it anyway). It then switches the panel off again if the phone is “in hand” (" + rif("The phone panel: off when connected") + "). Mouse moves and wheel events are dropped when more than " + c("CODA_INPUT_MAX") + " (512) jobs are waiting in the input pool, so a phone that cannot keep up does not accumulate movements; the closing phases of a gesture always pass. The window code (" + c("src/shell/finestra-app/finestra_app.c") + ") feeds it from one drag gesture on the windows container that accepts every button; " + c("gesto_inizia_pulsante") + " maps the GDK button with " + c("ph_fa_pulsante_tocco") + " to the bit of " + c("pulsanti") + " (left 1, right 2, middle 4; back, forward and others start nothing). The left button keeps everything it had: title bar and corner start a window move or resize, content sends a touch. Right and middle count only on the content of an app window (the same discards as the left: pause, lost-connection veil and buttons inside it, black bands around the image, a size that changed); on the title bar, the corner handle, the window buttons or outside the windows nothing happens, not even a change of the active window. " + c("TOCCO") + " down and moves carry the bit of the button that started the gesture (" + c("tocco_pulsanti") + "), up and cancel carry 0; the field travels unchanged through " + c("ph_superficie_tocco") + ", " + c("ph_srv_flusso_tocco") + " and " + c("sv_flusso_input") + " to the message. One button at a time: pressing a different button while a gesture is open makes GTK reset the controller (" + c("gtk_gesture_single_handle_event") + "), so the toolkit sends a cancel and the gesture in progress is cancelled (the touch is cancelled on the phone; a window move or resize is cancelled); the second button starts nothing. " + c("gesto_inizia_pulsante") + " also refuses a press while a gesture is open, as a defence. Every way of losing the gesture (cancel by the toolkit, window closed, pause, pinch starting, window minimised) sends a cancel for the touch that is down, so no down is left without its up or cancel. Text that is plain ASCII goes as " + c("TESTO") + "; text with other characters goes through the clipboard as a paste (" + c("APPUNTI_INCOLLA") + ", " + rif("The shared clipboard") + "). The send writes on the " + c("input") + " channel under a lock held during the write: the known exception to the rule of no lock during a network write, because ADB forbids two writers on one channel.") + \
    p("<b>On the phone.</b> " + c("Input") + " reads the channel without a buffer and puts each message in a bounded queue of 256 closures; when the queue is full the reader waits, so no event is lost. One thread executes them. Each event carries the session id and, for touch, wheel and pinch, the video size used for the coordinates; if it differs from the session's current size the event is discarded (specification 4.5). A touch-down during a pause, resize or other transition is dropped. At the end of the channel (PC gone or error) every open gesture of every session is cancelled: no finger stays down.") + \
    table(["Message", "Injected as"], [
        [c("TOCCO"), "a " + c("MotionEvent") + " on the session's display: down, move, up or cancel. The left button is a finger (source touchscreen). If the " + c("pulsanti") + " field has the right or the middle button when the gesture starts (the right wins if both), the whole gesture is a real mouse: source mouse, tool type mouse, " + c("ACTION_DOWN") + " with " + c("BUTTON_SECONDARY") + " or " + c("BUTTON_TERTIARY") + " followed by " + c("ACTION_BUTTON_PRESS") + ", moves with the button held, then " + c("ACTION_BUTTON_RELEASE") + " and up (or cancel) with no button; " + c("setActionButton") + " is hidden, so it is called by reflection and, if missing, the two BUTTON events are skipped. The button is decided at the press and remembered until the release; unknown bits are ignored. A cancelled mouse gesture (another session pressing, pause, resize, end of channel) sends only " + c("ACTION_CANCEL") + " with no button and no " + c("ACTION_BUTTON_RELEASE") + " (a release would count as a mouse-up and open the link). For the mirror the coordinates are scaled to the real screen"],
        [c("ROTELLA"), c("ACTION_SCROLL") + " with source mouse; " + c("AXIS_HSCROLL") + " and " + c("AXIS_VSCROLL") + " are the 120ths divided by 120"],
        [c("TASTO"), "a " + c("KeyEvent") + " with the code and meta state given, from the virtual keyboard map, on the session's display"],
        [c("TESTO"), "each character mapped by the virtual keyboard map to key events; a character the map cannot produce is skipped and logged as a code point"],
        [c("INDIETRO"), "Back down then up"],
        [c("PIZZICO"), "see the next section"],
    ], "«TAB» — How the phone injects each message") + \
    p("Every injected event carries the id of the session's virtual display (" + c("schermoInput") + ": 0, the real screen, for the mirror).")

S2 = p("Ctrl + wheel is a zoom. The PC sends the gesture as steps and the phone makes two fingers: this lets the apps' gesture detectors see a continuous movement. The shell turns the wheel into " + c("ph_input_pizzico") + " calls; the argument checks (phase unknown, x or y outside the video, zero size) fail with -1 without sending anything and the step is clamped to −1200..1200.", lead=True) + \
    table(["Rule", "Value"], [
        ["Fingers", "on a horizontal line through the centre (x, y), symmetric"],
        ["Initial distance", "25% of the shorter side of the virtual display"],
        ["Per 120 of step", "the distance is multiplied by 1.25 (−120 divides it); steps of the gesture are summed"],
        ["Distance limits", "between 5% and 90% of the shorter side"],
        ["Near an edge", "the centre moves inward just enough that both fingers stay inside [0, w−1] × [0, h−1]; the gesture goes on from there with the same shift"],
        ["Long steps", "a step beyond ±30 is split into several " + c("ACTION_MOVE") + " (at most 30 per move, one every 8 ms)"],
        ["Phases", "start puts both fingers down (and applies its step); continue moves; end lifts both (" + c("ACTION_POINTER_UP") + " then " + c("ACTION_UP") + "); cancel sends " + c("ACTION_CANCEL") + ". Continue or end without an open start is discarded; a new start closes the open gesture first"],
        ["Always closed", "after 400 ms with no step the phone ends the gesture by itself; session close, pause and size change cancel it"],
        ["With a touch in progress", "the last arrival wins: a pinch start cancels the touch finger first; a touch down closes an open pinch (cancel); a touch move or up whose finger is no longer down is discarded"],
        ["Paused", "the pinch is discarded and an open one is cancelled"],
    ], "«TAB» — Pinch rules (" + c("Pizzico") + ", " + c("Gesti") + ")") + \
    p("On the PC side the stream records the last pinch centre so that a pause, resize or close sends a " + c("PH_PIZZICO_ANNULLA") + " and no finger stays down; what counts is what the phone really received, updated after each successful send, so a lost “up” is still cancelled and a cancel is not sent for a down that never left. An old jar answers an unknown type as it does for any unknown input type, with no visible error.")

S3 = p("Text copied on the phone is copied on the PC and the reverse. Only text: images and files are ignored. The phone side is " + c("telefono/appunti") + " (" + c("Appunti") + ", " + c("Regole") + ", " + c("NascosteAppunti") + "), the PC side is " + c("src/dati/appunti.c") + ", on the channel " + c("appunti") + ".", lead=True) + \
    table(["Type", "Name", "Direction", "Body"], [
        [c("0x58"), c("APPUNTI_TESTO"), "T→PC", "the text just copied on the phone"],
        [c("0x59"), c("APPUNTI_IMPOSTA"), "PC→T", "the text to put in the phone clipboard"],
        [c("0x5a"), c("APPUNTI_INCOLLA"), "PC→T", c("u32") + " session id, then the text; clipboard plus “paste” to the app of that session"],
    ], "«TAB» — Clipboard messages (text is a " + c("u32") + " length plus UTF-8, not a 16-bit " + c("stringa") + ")") + \
    p("<b>Never passed</b> (in either direction, silently):") + \
    ul([
        "copies marked sensitive (" + c("ClipDescription.EXTRA_IS_SENSITIVE") + ");",
        "texts above 200000 bytes (" + c("MAX_TESTO") + ", also " + c("PH_DATI_APPUNTI_MAX") + " on the PC): the message is ignored;",
        "copies that come from a password manager or an authenticator: the criterion is the list of known packages in " + c("Regole") + " (a name stands for itself and its sub-packages). The Google password manager copies from Play Services, so it cannot be filtered by package and only the standard sensitive flag applies;",
        "non-text content.",
    ]) + \
    p("<b>No echo.</b> The phone sends " + c("APPUNTI_TESTO") + " when its clipboard changes but not for what it has just received from the PC: an echo is the copy YASDE itself put (label " + c("yasde") + ", source " + c("com.android.shell") + " when the system tells it) with the same text as the last one from the PC. A real copy on the phone with the same text has another label and passes. The system notifies twice for one copy; the same timestamp and text count once, and an uncertain source is re-evaluated at the next notice. When the channel closes the phone's clipboard stays as it is.") + \
    p("<b>On the PC</b> a thread reads messages for the whole session (150 ms slices); a text is cleaned by " + c("ph_dati_pulisci") + " (invalid UTF-8 becomes U+FFFD, NUL bytes are removed, and the length inside the message must match the bytes present) and handed to the notice; it is discarded if the cleaning grew it above the limit. When the reader ends by itself the notice is called once with a NULL text. " + c("APPUNTI_INCOLLA") + " serves the text the virtual keyboard cannot produce (accents, emoji): afterwards the phone's clipboard keeps that text.") + \
    note("Texts never go to a log, on either side.")

S4 = p("While the apps are used from the PC the phone panel (its real screen) stays off, while the phone stays awake and unlocked; when the user takes the phone in hand the panel turns on again. The code is " + c("telefono/pannello") + " (" + c("Pannello") + ", " + c("TieniSveglio") + ", " + c("AssenzaPc") + ", " + c("InputFisico") + ", " + c("Frequenze") + ", " + c("ImpostazioniPannello") + ", " + c("NascostePannello") + ") and, on the PC, " + c("src/shell/ossatura/sv_dati.c") + ".", lead=True) + \
    table(["Type", "Name", "Body"], [
        [c("0x60"), c("PANNELLO_IMPOSTA"), c("u8") + " 0 = off, 1 = on (any other value is an error)"],
        [c("0x61"), c("PANNELLO_STATO"), "empty; answer " + c("u8") + " on, " + c("u8") + " in hand"],
        [c("0x6e"), c("PANNELLO_EVENTO"), "event: " + c("u8") + " on, " + c("u8") + " reason"],
    ], "«TAB» — Panel messages") + \
    table(["Reason", "Name in code", "Meaning", "In hand on the PC"], [
        [c("1"), c("MOTIVO_UTENTE"), "the user touched the real screen or a physical key, unlocked or woke the phone", "yes"],
        [c("2"), c("MOTIVO_CHIAMATA"), "incoming or ongoing call", "yes"],
        [c("3"), c("MOTIVO_FINE_CHIAMATA"), "call ended", "yes"],
        [c("4"), c("MOTIVO_PC"), "asked by the PC", "no"],
        [c("5"), c("MOTIVO_CHIUSURA"), "restore at close", "no"],
        [c("6"), c("MOTIVO_ASSENTE"), "PC absent for 30 minutes", "yes: the panel stays on until the first PC input turns it off"],
    ], "«TAB» — Panel event reasons (" + c("ph_ossatura_pannello_da_evento") + ")") + \
    p("<b>How off works.</b> " + c("PANNELLO_IMPOSTA 0") + " acts only on the compositor (" + c("SurfaceControl.setDisplayPowerMode") + ", a hidden API): Android still believes the screen is on, so the apps on the virtual displays go on running and receiving input. It is refused with " + c("ERRORE") + " during a call, when the phone is locked or its screen is off, and when the guardian is missing.") + \
    steps([
        "Open the listener on the physical devices (" + c("InputFisico") + ": " + c("getevent -pl") + " once, devices chosen by capability, touchscreen with " + c("ABS_MT_POSITION_X") + " and " + c("INPUT_PROP_DIRECT") + ", keys by common physical keys; the shell may read " + c("/dev/input") + " because it is in the group " + c("input") + "). Without it the panel is not turned off. Injected events never reach " + c("/dev/input") + ", so they cannot count as the user touching the phone.",
        "Register the restore entries first, then change: the screen timeout (" + c("pannello-timeout") + ", order 420) if it is not already the maximum, then the panel (" + c("pannello-acceso") + ", order 400). If any registration returns false nothing is changed; if the later step fails, the earlier ones are undone as far as they can.",
        "Raise the screen timeout to the maximum " + c("2147483647") + " ms (" + c("screen_off_timeout") + " through " + c("cmd settings") + ", verified by re-reading): without it the apps on virtual displays stop receiving input. It stays at the maximum even during the “in hand” periods, and is restored (only if still ours) when the PC asks to turn on or at close.",
        "Turn the panel off, send the event (reason 4), then measure the refresh rate.",
    ]) + \
    p("<b>It comes back by itself, and the event goes out,</b> when the user touches the real screen or presses a physical key, unlocks the phone, or a call arrives or is in progress. From then the phone is “in hand” and stays on until the PC sends " + c("PANNELLO_IMPOSTA 0") + " again. If the turn-on fails, the reason is remembered and the control loop retries it every second, before anything else. If Android is asleep the panel is not forced (the system turned it off; the user wakes it).") + \
    p("<b>The control loop.</b> One thread, every 1000 ms (" + c("PERIODO_MS") + "), reads the call state, whether the screen is interactive and whether the phone is locked, each in its own try so one failing read does not stop the others, then evaluates the PC absence, then the wake-lock guard, then reports changes: a call starting turns the panel on (or marks the phone in hand), the end of a call is reported, a lock or sleep with the panel off turns it on, and an unlock or wake with the panel on is reported as reason 1. It starts at the first sign of life, and an exception in a round never kills it.")

PANFLOW = fig(
    box(40, 40, 170, 54, "On", "panel on, not in hand", "light")
    + box(365, 40, 170, 54, "Off", "turned off by YASDE", "dark")
    + box(690, 40, 170, 54, "In hand", "on, user is using it", "amber")
    + arrow(212, 60, 363, 60) + text(288, 52, "connected / first PC input", 11)
    + arrow(539, 58, 688, 58) + text(613, 50, "touch, unlock, call", 11)
    + arrow(688, 80, 539, 80) + text(613, 98, "first input from the PC", 11)
    + arrow(363, 82, 212, 82, "#475569", True) + text(288, 100, "PC asks on / drop / close", 11),
    900, 130, "«FIG» — The panel states as seen from the PC")

S5 = p("The PC decides when to turn the panel off; the phone only obeys and reports. The code is in " + c("sv_dati.c") + ".", lead=True) + PANFLOW + \
    ul([
        "<b>At connection</b> (decision of 24 Nov 2026): as soon as the session is up, if the panel is on and the phone is not in hand, YASDE turns it off, once, queued on the state pool because it can last seconds. If the phone refuses (locked, in a call) it is not retried; at unlock or at the end of the call the “in hand” rule takes over. If the phone is already in hand at connection the panel is not turned off. Reading the state again after a refusal is not covered by a test.",
        "<b>First input from the PC</b> with the phone in hand and the panel on: " + c("sv_pannello_spegni_se_in_mano") + " queues one turn-off (one at a time). After a refused turn-off it waits 2 s before trying again.",
        "<b>The popup switch</b> “physical panel” calls " + c("sv_pannello") + " directly.",
        "<b>Closing the last window does not turn the panel on</b> while YASDE is connected (decision of 23 Nov 2026, in place of the specification). The panel comes on only for the user (unlock on the phone, the popup switch), a call, the end of a call, and YASDE closing or dropping (restore on the phone side).",
        "<b>Turned off or on by the PC</b> means not in hand. State changes from " + c("0x6e") + " events are published as the phone state; every state read is requested after the listener is registered.",
        "There is no timer on the PC that turns the panel off again after the user's inactivity. The design documents say there is; the code does not (" + rif("Known deviations from the design documents") + ").",
    ]) + \
    warn("Turning the panel off at connection was proven on a real phone on 24 Nov 2026. Still not tried live: the panel not turning on at the close of the last window, and reason 6 after an absence.")

S6 = p("Turning the panel off makes the phone's compositor drop to a lower rate. A measured case (120 Hz test phone): the compositor went from 120 to 60 Hz with the panel off and the video fell from about 87 to 57 frames per second; with the remedy it climbed back to about 80. The remedy is a workaround started by the measured symptom on any phone, never by the manufacturer's name; it is isolated in one place and documented.", lead=True) + \
    steps([
        "After every turn-off the active compositor rate is measured and written in the log with the display's maximum.",
        "The wanted rate is the highest among the modes with the same resolution as the active one (" + c("Frequenze.massimaStessaRisoluzione") + "), capped by the user's " + c("peak_refresh_rate") + " if it is numeric and greater than 0 (" + c("Frequenze.voluta") + ").",
        "If the active rate is below the wanted one, " + c("min_refresh_rate") + " is set to the wanted rate, with the restore (" + c("pannello-frequenza") + ", order 410) registered before and applied only if the value is still ours; at turn-on it is put back. If the user already has a minimum equal or higher nothing changes.",
    ]) + \
    note("The design documents say “set min_refresh_rate to 60 before turning off”. The code does the measured version above.")

S7 = p("A wake lock keeps the phone awake and therefore protected from locking through inactivity for as long as YASDE is connected and alive. It is in " + c("TieniSveglio") + ", with the Android part behind the " + c("Presa") + " interface so the logic is tested on a plain JVM. The lock is taken from the first message of the command channel (the greeting), not at service start.", lead=True) + \
    p("Why a wake lock and not the screen timeout: the wake lock belongs to the process, so if the service dies (even with " + c("kill -9") + ") the system releases it and the phone goes back to turning off and locking on its own times. No setting stays changed and nothing needs the guardian.") + \
    table(["Condition (all must hold)", "Detail"], [
        ["Channel greeted", "the PC has sent " + c("SALUTO")],
        ["Phone not locked", "the lock would keep the lock screen lit for ever after a lock with the power key: it is released while locked and taken again at unlock; an unreadable state counts as locked"],
        ["PC alive", "last sign of life younger than 30 minutes (" + c("Registro.PC_ASSENTE_MS") + "); exactly 30 minutes is already too much; a sign newer than now is an incoherent clock and counts as no"],
    ], "«TAB» — When the wake lock is held") + \
    p("Every doubt goes to the safe side: release. The wanted state itself expires: if " + c("aggiorna") + " does not arrive within " + c("SCADENZA_STATO_MS") + " (30 s, measured with its own monotonic clock) the thread treats it as “do not hold”. 30 s and not 5 because the control round and the PC's commands work under the same lock as the panel and run " + c("cmd settings") + " with 10 s limits (up to 20 s); with 5 s a phone under load would drop the lock and fall asleep mid-session. The caller of " + c("aggiorna") + " never waits: it only writes the wanted state; a separate thread takes and releases, outside every lock, retrying every 5 s on failure.") + \
    p("The lock is taken without " + c("ON_AFTER_RELEASE") + " (the safe choice): when it is released and the user has not touched the phone for longer than their own timeout, Android does not restart the countdown from zero, it turns the screen off and locks at once.")

ABS = seq([("PC", "componente", "navy"), ("Registro", "last sign of life", "blue"), ("Pannello", "control loop 1 s", "dark")], [
    (0, 1, "any message on the command channel", True),
    ("sep", "30 minutes pass with no message"),
    (2, 2, "AssenzaPc.valuta: expired"),
    (2, 2, "1 panel on; 2 refresh rate back; 3 timeout back"),
    (2, 0, "PANNELLO_EVENTO on, reason 6", True),
    ("nota", 2, "wake lock released; service and sessions stay"),
    ("sep", "the PC speaks again"),
    (0, 1, "any message", True),
    (2, 2, "wake lock again, timeout to maximum"),
], "«FIG» — PC absence and return", width=900)

S8 = p("The phone must not stay in a state “owned by YASDE” when the PC has vanished without closing. Rule: any message the phone receives from the PC on the command channel (the greeting included) is a sign of life. The service notes the time (monotonic clock, " + c("SystemClock.elapsedRealtime") + ") before dispatching it (" + c("Smistatore.tratta") + " → " + c("Registro.segnoDiVita") + ").", lead=True) + \
    table(["Constant", "Value", "Where"], [
        [c("PRESENZA_INTERVALLO"), "300 s (" + c("PH_PRESENZA_INTERVALLO_S") + ")", "PC: sends " + c("PRESENZA") + " when nothing has been sent on the command channel for 5 minutes; it is needed only when there is no other traffic"],
        [c("PC_ASSENTE"), "1800 s (" + c("Registro.PC_ASSENTE_MS") + ", " + c("PH_PC_ASSENTE_S") + ")", "phone: six missed " + c("PRESENZA") + " in a row"],
        ["Control round", "1000 ms", "phone: absence check and wake-lock guard"],
    ], "«TAB» — Presence timing") + ABS + \
    p("<b>On absence</b> (" + c("AssenzaPc") + ", exactly " + c("PC_ASSENTE_MS") + ", a clock going back counts as expired) the steps are, in order: turn the panel on if YASDE had turned it off; restore the minimum refresh rate; put the user's screen timeout back (only if still ours); then send " + c("PANNELLO_EVENTO") + " (on, reason 6). A step that fails or throws stops there and is retried at the next round, without skipping the order and without sending the event early. The wake lock is not a step: the guard releases it with the same limit. It does not close the service or the sessions. It is the safest state: the phone is exactly as every day and Android turns it off and locks it with the user's times.") + \
    p("<b>On return</b> (the first sign of life after the absence): the wake lock is taken again if the phone is not locked, and the screen timeout is raised again (the restore registered before); the panel STAYS ON until the PC sends " + c("PANNELLO_IMPOSTA 0") + ", exactly as if the phone were in hand (reason 6 counts as in hand on the PC). A failed return is retried at the next round.") + \
    p("If the wake-up of a sleeping PC delays a " + c("PRESENZA") + ", the boot clock makes the next one go out at once; a late message does not trip the 30 minutes because the window is six times the interval. The case “phone restarted by the user and the PC still running” needs no rule: the link loses the connection and starts again.")

CHAPTER = ("Input, clipboard and the phone's panel", [
    ("Input injection", S1),
    ("The pinch gesture", S2),
    ("The shared clipboard", S3),
    ("The phone panel: off when connected", S4 + S5),
    ("Refresh rate while the panel is off", S6),
    ("Keeping the phone awake", S7),
    ("Signs of life and PC absence", S8),
])
