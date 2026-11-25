from build import c, note, p, rif, seq, steps, table, ul, warn

S1 = p("The phone captures everything it plays and sends it as Opus; the PC plays it. On the phone the code is " + c("telefono/audio") + ": " + c("Audio") + " (the channel), " + c("Cattura") + " (one capture), " + c("Volume") + " (media volume) and " + c("NascosteAudio") + " (the hidden audio-policy APIs).", lead=True) + \
    steps([
        "<b>Opening the channel starts the capture, closing it stops it.</b> One audio channel at a time: a second one waits up to 2000 ms for the first to finish closing and is then refused (closed). If the service is closing, no capture starts.",
        "<b>Loopback rule.</b> " + c("NascosteAudio") + " registers an " + c("AudioPolicy") + " with a mixing rule that matches by usage: unknown, media, alarm, notification, notification event, accessibility, navigation guidance, sonification, game and assistant. While the rule is active the phone stays silent and the sounds come out of the PC; when it is removed the phone plays again by itself. The recorder is built by hand (" + c("NascosteAudio.creaRegistratore") + ") with a context that presents itself as " + c("com.android.shell") + ": the library helper creates it without a context, so the package would be " + c("android") + " and the system refuses uid 2000.",
        "<b>Format.</b> " + c("AudioRecord") + " 48 kHz stereo 16 bit, read in blocks of 960 frames (20 ms), encoded with a " + c("MediaCodec") + " Opus encoder at 128 kbit/s.",
        "<b>Time.</b> The time of each packet is the number of samples captured so far divided by 48000, not the clock, and a lost PCM block is added to it, so a gap always shows as a jump of 20 ms or more.",
        "<b>Queue.</b> Packets wait in a queue of 250 (5 s) toward the PC; beyond it the oldest is dropped and the jump is visible in the time.",
    ]) + \
    table(["Field", "Size", "Notes"], [
        ["time", "8 bytes", "microseconds: samples captured so far / 48000 (not the clock)"],
        ["length", "2 bytes", "at most 4000 on the PC side; more means a broken channel"],
        ["data", "length", "one Opus packet"],
    ], "«TAB» — Audio packet (10-byte header)")

S2 = p("The media volume must be at the maximum during capture: with volume 0 some apps do not start their audio. The change follows the rule that governs every change to the phone: the undo is registered first, the change is verified, the user's value wins.", lead=True) + \
    steps([
        "Read the current volume and the maximum (" + c("cmd media_session volume --stream 3 --get") + ", parsed from " + c("volume is N in range [a..b]") + ").",
        "If it is already at the maximum, do nothing. Otherwise register the entry " + c("audio-volume") + " (order 50) with " + c("Guardiano.imposta") + ". If it returns false, or there is no guardian, the volume is not touched and the capture goes on.",
        "Set the maximum and re-read it: a different value is an error and the volume is restored.",
        "At close: read the volume; only if it is still the maximum YASDE set, put the original back (retried once if the re-read does not match) and remove the guardian entry after the check succeeded. Otherwise it is left and logged as changed by the user. A failed check leaves the entry for the guardian.",
    ]) + \
    p("The volume the user moves in the shell is a gain applied on the PC and does not touch the phone's volume. All commands run with a real timeout: the output is read on a separate thread so a blocked command cannot block the caller.")

S3 = p("On the PC the audio piece is " + c("src/audio") + ". The public header is " + c("audio.h") + "; the parts are tested alone, without an audio device. Audio has no hardware acceleration on the PC: the Opus packets are decoded by libopus on the CPU; on the phone the Opus encoder is the one Android returns for " + c("createEncoderByType") + ", and the code does not say whether it is hardware or software.", lead=True) + \
    table(["File", "Role"], [
        [c("pacchetti.c"), "reads packets from the channel (resumable reads: a timeout in the middle of a packet keeps the state)"],
        [c("decodifica.c"), "Opus 48 kHz stereo to float, with concealment of missing packets"],
        [c("flusso.c"), "decode plus detection of lost and late packets"],
        [c("coda.c"), "queue with adaptive margin and lag recovery"],
        [c("uscita_pipewire.c"), "the PipeWire output behind a small interface (" + c("PhUscita") + "); tests use a fake one"],
        [c("audio.c"), "the player that puts the parts together"],
        [c("suona.c"), "a hand tool that plays a channel"],
    ], "«TAB» — Files of " + c("src/audio")) + \
    p("<b>Life.</b> " + c("ph_audio_crea(channel, notice, ctx, error)") + " takes the channel (it closes it even if creation fails), with volume 100 and not muted. " + c("ph_audio_avvia") + " opens the PipeWire output (application name " + c("YASDE") + ", 48 kHz stereo float, requested node latency 1024/48000, 3 s to start) and starts the reading thread; once only. " + c("ph_audio_ferma") + " stops reading and output; " + c("ph_audio_distruggi") + " also closes the channel and frees. The notice " + c("PhAudioAvviso") + " is called once, from the reading thread or PipeWire's, when the stream ends (" + c("PH_AUDIO_FINITO") + ": the phone closed it) or breaks (" + c("PH_AUDIO_ERRORE") + "); it is not called when the stop was requested, and it must not call stop or destroy.") + \
    p("<b>Packets in.</b> Each packet is decoded; a jump in the time field larger than 10 ms (half a packet; less is noise) means lost packets and up to 10 of them (200 ms) are concealed with the decoder's own concealment, no more sound is invented beyond that; a packet that arrives out of order or twice is dropped and counted; 25 late packets in a row mean the phone restarted its time and the stream is re-synchronised; 100 undecodable packets in a row make the flow unrecoverable.") + \
    p("<b>The queue.</b> Between the arriving packet and the sample leaving there is a margin that absorbs network delay. It is computed from the data: for each packet the lateness (arrival time minus the time field; the two clocks are not comparable but their difference is) is measured against the most punctual of the last 10 s; the maximum lateness in that window plus a fixed 40 ms is the ideal margin. It rises at once and falls by 8 ms per second. Constants: capacity 2 s; initial target 60 ms; limits 40 and 600 ms.") + \
    ul([
        "Every half second the queue checks how much audio is really there. If it is well above the margin (more than 40 ms) it plays slightly faster, if well below slightly slower, rereading samples with linear interpolation by at most 2%: it is heard neither as a click nor as a pitch change. It stops when within 8 ms of the margin.",
        "A huge excess (after a stall and a burst) beyond 250 ms is cut at once to 40 ms, with a 5 ms (240 frames) cross-fade.",
        "An empty queue during playback is an underrun: the sound is faded out, the queue goes back to “filling” and restarts only with the full margin. After a hole the margin climbs again by 20 ms.",
        "A stall is ignored for the estimate for at most 5 s: beyond that it is the clock that changed.",
    ]) + \
    p(c("ph_audio_statistiche") + " reports received, lost, late, invalid packets, underruns, discarded frames, the margin and the target, and PipeWire diagnostics (device delay, buffered and queued frames).")

S4 = p("<b>Gain.</b> " + c("ph_audio_volume(percent)") + " (0–100, clamped) and " + c("ph_audio_muto") + " set a gain applied on the PC to the samples, with a cubic curve " + c("(v/100)^3") + " like the sliders of PipeWire and PulseAudio: 0 is silence, 100 is gain 1. A ramp of 10 ms avoids clicks. Muting does not stop reading: packets are received and decoded all the time, only the gain drops. The volume is persisted by the caller (the config piece, in the phone profile), not here. The default of the shell slider is 100%.", lead=True) + \
    p("<b>When audio starts.</b> " + c("sv_audio_programma") + " (" + c("src/shell/ossatura/sv_audio.c") + ") schedules the start " + c("SV_AUDIO_RITARDO_MS") + " = 5000 ms after the first video stream of the session opens; the manager thread " + c("ph-gestore") + " does the actual open. Opening goes through " + c("ph_componente_canale(c, \"audio\")") + ". If the open fails or the stream ends, a new attempt is scheduled after 2, 4, 8 and 16 s (" + c("SV_AUDIO_TENTATIVI") + " = 4 per session); decisions go to the decision log. On session teardown the audio is destroyed and the counters reset.") + \
    p("<b>Synchronisation.</b> The audio clock is exposed: " + c("ph_audio_tempo_telefono_us") + " gives the phone time (the " + c("time") + " field) of the sample leaving the speakers now, estimated with the latency PipeWire declares (delay plus buffered plus queued frames), or -1 if unknown. The two phone clocks (video presentation time and audio sample count) are not comparable with each other. As the code stands, no caller in the shell uses this function: video frames are shown as they arrive and there is no active audio-video synchronisation yet. The piece to measure is listed among the open points.") + \
    warn("If you add synchronisation, convert the audio time to the video time line first; " + c("cattura.h") + " says the same for recordings, and the shell would have to do the conversion.")

CHAPTER = ("Audio", [
    ("Audio: capture and transport", S1),
    ("Media volume on the phone", S2),
    ("Audio: playback on the PC", S3),
    ("Gain, start-up and synchronisation", S4),
])
