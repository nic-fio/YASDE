from build import arrow, box, c, fig, flow, note, p, rif, seq, steps, table, text, ul, warn

S1 = p("The ADB client is " + c("src/adb") + ": a client for the Android Debug Bridge protocol written from scratch, with no ADB library and no "
       "use of the " + c("adb") + " tool. It opens one TLS connection to the phone's wireless-debugging port and multiplexes any number of channels on it. "
       "It knows nothing about video, audio or files; its public header is " + c("src/adb/adb.h") + ".", lead=True) + \
    table(["File", "Role"], [
        [c("adb.h"), "the contract: " + c("PhChiave") + ", " + c("PhAdb") + ", " + c("PhCanale") + ", " + c("ph_shell_esegui")],
        [c("chiave.c"), "RSA key file, self-signed certificate, public key in Android format"],
        [c("connessione.c"), "TCP, STLS, TLS 1.3, reader thread, channels and flow control"],
        [c("pacchetto.c") + ", " + c("interno.h"), "the 24-byte packet header and its checksum"],
        [c("shell.c"), "shell v2 commands"],
        [c("finto_telefono.c"), "a fake phone used by tests"],
    ], "«TAB» — Files of " + c("src/adb")) + \
    p("The concurrency model is binding: one reader thread per " + c("PhAdb") + " reads every packet from the socket and routes it to its channel; writes on the socket are serialised by a mutex so that a packet is never split by another; every channel has its own incoming queue; "
      + c("ph_canale_leggi") + " blocks (with a timeout) only the thread that reads that channel. On one channel one reader and one writer may work together, two readers or two writers may not. "
      "If the connection dies, every read and write in progress returns an error at once, no thread stays blocked, and " + c("ph_adb_viva") + " becomes false.")

HAND = seq([("PC", "ph_adb_collega", "navy"), ("phone", "adbd", "dark")], [
    (0, 1, "TCP connect (non-blocking, with deadline)"),
    (0, 1, "CNXN  version 0x01000001, max 1 MiB, features"),
    (1, 0, "STLS  (or AUTH: not paired)", True),
    (0, 1, "STLS"),
    ("sep", "TLS 1.3 handshake, client certificate of the PC"),
    (1, 0, "CNXN  banner, max data, features", True),
    ("nota", 0, "PhAdb alive; reader thread started"),
], "«FIG» — The connection handshake")

S2 = p("The connection is opened by " + c("ph_adb_collega(host, port, key, timeout, not_paired, error)") + ". One deadline covers the whole procedure: TCP connect, STLS exchange, TLS handshake and the phone's greeting.", lead=True) + HAND + \
    steps([
        "<b>TCP.</b> A non-blocking socket tried on every address returned by " + c("getaddrinfo") + ". After connecting: " + c("TCP_NODELAY") + ", " + c("SO_KEEPALIVE") + " with idle 5 s, interval 2 s, 3 probes, and " + c("TCP_USER_TIMEOUT") + " 10000 ms: a phone that leaves the Wi-Fi without closing is detected in about 10 s instead of hours. " + c("SIGPIPE") + " is ignored for the whole process (once, only if nobody chose a behaviour), because OpenSSL writes with " + c("write()") + ".",
        "<b>CNXN in clear.</b> Version " + c("0x01000001") + " (checksum no longer verified: integrity is given by TLS), maximum data 1 MiB, banner " + c("host::features=shell_v2,cmd,delayed_ack;") + ". The final semicolon is required: adbd reads the banner as a C++ string including the terminator, and without it the last feature would be " + c("delayed_ack") + " followed by a NUL byte and would not be recognised.",
        "<b>STLS.</b> If the phone answers " + c("AUTH") + " the PC is not paired (" + c("*non_abbinato = true") + "); any other answer is an error. The PC answers " + c("STLS") + " and starts TLS.",
        "<b>TLS 1.3 only</b>, with the PC certificate. The phone certificate is not verified (it is self-signed, as for the standard tool): what matters is that the phone recognises the PC, and it decides by the PC key. The handshake failing with a certificate alert, or the phone closing during or just after the handshake, is reported as “not paired”; any other error is a network error.",
        "<b>Greeting.</b> Inside TLS the phone sends its CNXN: its maximum data (capped at 1 MiB, and refused if below 256) and the banner (which contains the model). " + c("delayed_ack") + " is active only if both sides declared it.",
    ]) + \
    p("The banner is available as " + c("ph_adb_presentazione") + ". " + c("ph_adb_chiudi") + " marks the connection closed, shuts the socket down to wake the reader, joins it and releases; the file descriptor and the SSL object stay until the last channel is closed, so that a channel finishing an operation never finds a recycled descriptor.")

S3 = p("The key and the certificate live in " + c("chiave.c") + ". The key file is " + c("chiave.pem") + " in " + c("~/.config/YASDE") + ", an RSA 2048-bit private key in PKCS#8 PEM (the format of the standard tool's " + c("adbkey") + ", so that in development an already paired phone can be reused).", lead=True) + \
    table(["Item", "Value"], [
        ["Creation", c("ph_chiave_carica_o_crea") + ": if the file is missing the key is generated and saved with permissions 0600, creating the folder with 0700"],
        ["Certificate", "X.509 v3, serial 1, subject and issuer " + c("CN=yasde") + ", valid from one day ago for ten years, signed with SHA-256; used for TLS and for pairing"],
        ["Public key for the phone", c("ph_chiave_pubblica_adb") + ": Android's " + c("RSAPublicKey") + " structure (length in words, " + c("n0inv") + ", modulus, " + c("rr") + " = (2^2048)^2 mod n, exponent; little endian; 524 bytes) in base64, followed by a space and " + c("user@host") + ", NUL-terminated"],
        ["OpenSSL API", "only EVP and BN calls of OpenSSL 3; the deprecated low-level RSA functions are not used"],
    ], "«TAB» — The PC key and certificate") + \
    note(c("n0inv") + " and " + c("rr") + " are what adbd needs for Montgomery multiplication; this is why the key cannot be sent as a plain public key.")

S4 = p("A channel is one ADB service opened on the connection: " + c("ph_canale_apri(adb, service, timeout, error)") + ". Services used by YASDE: " + c("shell,v2,raw:<command>") + ", " + c("shell:<command>") + ", " + c("sync:") + " and " + c("localabstract:yasde") + ".", lead=True) + \
    table(["Function", "Behaviour"], [
        [c("ph_canale_leggi"), "reads 1 to n bytes; returns the count, " + c("PH_CANALE_CHIUSO") + " (0: the other side closed and the queue is empty), " + c("PH_CANALE_ERRORE") + " (-1, error filled) or " + c("PH_CANALE_TEMPO") + " (-2: timeout, not an error); a negative timeout waits without limit"],
        [c("ph_canale_leggi_tutto"), "exactly n bytes or failure; closing before n bytes is an error"],
        [c("ph_canale_scrivi"), "all n bytes, split in ADB packets, or failure"],
        [c("ph_canale_chiudi"), "closes and frees; wakes a read in progress in another thread"],
    ], "«TAB» — Channel functions") + \
    p("Flow control exists in two forms, chosen at connection time.") + \
    table(["Mode", "Sending", "Receiving"], [
        [c("delayed_ack") + " (both sides declare it)", "a window per channel: the phone's OKAY carries the number of bytes by which the window grows; the PC never sends beyond it, and several writes may be in flight",
         "the PC grants the phone a 4 MiB window per channel (" + c("FINESTRA_RICEZIONE") + "); each OKAY widens it by what the reader really consumed, sent after 1 MiB of consumed data (" + c("ACK_SOGLIA") + "); a phone sending beyond its window violates the flow control and the connection drops"],
        ["classic", "after a WRTE the OKAY is awaited before the next WRTE on the same channel", "the OKAY is sent when data is queued, but withheld while the queue exceeds twice the maximum packet (" + c("SOGLIA_CODA") + "), and resumes when the reader has emptied it"],
    ], "«TAB» — Flow control") + \
    p("The effect is that a slow reader brakes the phone instead of filling the PC's memory. Waiting for an OKAY or writing to the socket has a hard limit of 30000 ms (" + c("ATTESA_SCRITTURA_MS") + "): a phone that stops answering does not block a thread for ever. A late reply to an open that had timed out is answered with CLSE so that the service is closed on the phone.") + \
    warn("Packets coming from the phone are treated as hostile. The header is decoded with " + c("ph_pacchetto_decodifica") + ", which refuses a magic that is not the complement of the command and a length above " + c("PH_ADB_MAX_DATI") + " (1 MiB); an OKAY with a malformed payload in " + c("delayed_ack") + " mode drops the connection.")

S5 = p(c("ph_shell_esegui(adb, command, out, err, &code, timeout, error)") + " runs a command with the shell v2 protocol and waits for the end. It opens a channel with service " + c("shell,v2,raw:<command>") + ", reads frames of one id byte plus a 32-bit little-endian length plus data, and closes the channel.", lead=True) + \
    table(["Id", "Meaning"], [
        [c("1"), "stdout: appended to " + c("out") + " (may be NULL)"],
        [c("2"), "stderr: appended to " + c("err") + " (may be NULL)"],
        [c("3"), "exit code, one byte; ends the command"],
    ], "«TAB» — Shell v2 frame ids used by the PC") + \
    p("It returns " + c("-1") + " if the command could not run or the time ran out; a non-zero exit code is not an error of the function. A frame longer than 16 MiB is refused. Read in slices of 4096 bytes. The component itself is started with the older " + c("shell:") + " service because its output is a plain line stream (" + rif("Starting the component on the phone") + ").")

S6 = p("The copy of the component jar and every file transfer use the ADB " + c("sync:") + " service, in " + c("src/file/file.c") + ". Requests are four letters plus a 32-bit little-endian length (the protocol is documented in AOSP's SYNC.TXT; the code here is new). Each operation opens its own " + c("sync:") + " channel, so several can run together from different threads.", lead=True) + \
    table(["Operation", "Request", "Notes"], [
        [c("ph_file_invia"), c("SEND") + " + DATA blocks + " + c("DONE"), "PC to phone; creates or overwrites; keeps the local modification time; a partial remote file is removed on cancel or failure; mode 0100644"],
        [c("ph_file_ricevi"), c("RECV") + ", with a prior " + c("STA2"), "phone to PC; writes " + c("<local>.parziale") + " created exclusively, renames at the end without replacing an existing file, gives the phone's modification time; never overwrites a user file"],
        [c("ph_file_elenca"), c("LIS2") + " (entries " + c("DNT2") + ")", "folder listing without " + c("." ) + " and " + c("..") + "; sorted: folders first, then files, by name ignoring case; hidden names are included (the shell filters)"],
        [c("ph_file_info"), c("STA2"), "information on a path"],
        ["remove remote", c("shell:rm -f -- '<path>'"), "sync cannot delete; the path is quoted with " + c("'\\''") + " for embedded quotes"],
    ], "«TAB» — " + c("sync:") + " operations") + \
    table(["Constant", "Value", "Meaning"], [
        [c("BLOCCO_MAX"), "65536", "largest DATA block (protocol limit)"],
        [c("BLOCCHI_PER_SCRITTURA"), "16", "blocks gathered in one write (cancellation is checked about every MiB)"],
        [c("PH_FILE_ATTESA_MS"), "30000", "silence after which the phone is declared stuck"],
        [c("PH_FILE_FETTA_MS"), "200", "read slice, so the cancel flag is checked often"],
        [c("PERCORSO_MAX"), "1024", "longest path in a sync request"],
        [c("VOCI_MAX") + ", " + c("NOMI_BYTE_MAX"), "200000; 64 MiB", "safety caps on entries and on the bytes of their names"],
    ], "«TAB» — Limits of " + c("src/file")) + \
    p("Outcomes are " + c("PhFileEsito") + ": " + c("PH_FILE_RIUSCITO") + ", " + c("PH_FILE_ERRORE") + ", " + c("PH_FILE_ANNULLATO") + " (the cancel flag became true), " + c("PH_FILE_NON_TROVATO") + ", " + c("PH_FILE_NON_CONSENTITO") + " (Android denies access) and " + c("PH_FILE_ESISTE") + " (the destination or its " + c(".parziale") + " already exists on the PC: nothing was touched). A " + c("FAIL") + " from the phone is plain text and is classified by its words.")

CHAPTER = ("The ADB client", [
    ("The ADB client: transport", S1),
    ("Connection and TLS handshake", S2),
    ("The PC key and certificate", S3),
    ("Channels and flow control", S4),
    ("Shell commands", S5),
    ("File transfer with sync", S6),
])
