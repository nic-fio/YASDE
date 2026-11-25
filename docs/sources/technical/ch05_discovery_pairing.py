from build import c, note, p, rif, seq, steps, table, ul, warn

S1 = p("Discovery is " + c("src/scoperta") + ": a multicast-DNS (DNS-SD) client written from scratch, with no Avahi and no library. It looks for the two services Android announces while Wireless debugging is on. "
       "It depends only on " + c("src/comune") + ".", lead=True) + \
    table(["Service", "Gives", "Exists"], [
        [c("_adb-tls-connect._tcp.local"), "the port to connect to (" + c("abbinamento = false") + ")", "while wireless debugging is on; the port changes every time it is turned on"],
        [c("_adb-tls-pairing._tcp.local"), "the pairing port (" + c("abbinamento = true") + ")", "only while the phone shows the pairing-code screen"],
    ], "«TAB» — The two mDNS services") + \
    p("<b>API.</b> " + c("ph_scoperta_cerca(duration_ms, found, max, error)") + " asks on every active interface, collects answers for the given time and fills " + c("PhTelefonoTrovato") + " (instance name such as " + c("adb-R5CT0000000-AbCdEf") + ", address text, port, pairing flag), without duplicates. It returns the count; zero is not an error. "
      + c("ph_scoperta_leggi_risposta") + " and " + c("ph_scoperta_domanda") + " are the pure parts, testable without a network; a malformed or hostile packet must never read outside the buffer and returns 0.") + \
    steps([
        "<b>Sockets.</b> IPv4 only for sending: one socket per interface that is up, running, not loopback and multicast-capable (at most 16, one per distinct address), bound to that interface with " + c("IP_MULTICAST_IF") + ", TTL 255, multicast loop off. Replies sent from a port other than 5353 come back unicast to that socket.",
        "<b>Listening.</b> One extra socket on UDP 5353 joined to " + c("224.0.0.251") + ", shared with other programs (Avahi may already own the port). If the port is not available the search goes on with unicast replies only.",
        "<b>Questions.</b> The first question has the QU bit (unicast answer wanted). Questions repeat at 0, 250, 1000, 2000, 3000, 4500, 6000 and 8000 ms from the start and then every 2 s, because a phone with Wi-Fi in power saving answers late. From the second question on, follow-up questions are added: SRV for an instance seen in a PTR without SRV, and A for an SRV target without an address.",
        "<b>Filtering.</b> A packet whose source is outside the subnet of every interface is discarded (multicast is not routed, so an outside sender is not a real neighbour). At most 64 packets are read per socket per turn, and the time limit is checked inside the loop.",
        "<b>Combining.</b> PTR, SRV and A/AAAA records may arrive in different packets and are joined only at the end; the list is recomputed from scratch at every turn so that a TTL 0 “goodbye” removes an entry already found. Name compression is followed for at most 16 pointers; the collection holds at most 64 SRV, 128 address and 64 PTR records.",
    ]) + \
    p("The link asks for 1500 ms per search (" + rif("Link states and timings") + "). The hand tool " + c("src/scoperta/cerca.c") + " searches for 5 seconds and prints a trace of every packet; it is how a silent phone is diagnosed.") + \
    note("A phone that does not announce itself is not a YASDE fault: a real case was solved by turning Wireless debugging off and on again. YASDE cannot tell this from “no phone in range”.")

PAIR = seq([("PC", "ph_abbinamento_esegui", "navy"), ("phone", "pairing service", "dark")], [
    (0, 1, "TCP to the pairing port"),
    ("sep", "TLS 1.3, certificate of the PC"),
    ("nota", 0, "export 64 bytes (label adb-label) from this TLS session"),
    ("nota", 0, "password = code + exported bytes"),
    (0, 1, "type 0: SPAKE2 message (32 bytes)"),
    (1, 0, "type 0: SPAKE2 message (32 bytes)", True),
    ("nota", 0, "shared key (64 bytes) → HKDF-SHA256 → AES-128 key"),
    (0, 1, "type 1: AES-GCM(info with the PC public key)"),
    (1, 0, "type 1: AES-GCM(info of the phone)", True),
    ("nota", 0, "decrypts and verifies the tag: pairing done"),
], "«FIG» — Pairing with the six-digit code")

S2 = p("Pairing is " + c("src/abbinamento") + ". " + c("ph_abbinamento_esegui(host, port, code, key, timeout_ms, error)") + " makes the phone register the PC's public key. host and port are those of the pairing service, not of the connection. The code is the one the phone shows under Developer options, Wireless debugging, Pair device with pairing code.", lead=True) + PAIR + \
    steps([
        "<b>Transport.</b> TCP with a deadline, then TLS 1.3 with the PC certificate; the phone certificate is not verified (the guarantee comes from the code). All reads and writes have a socket timeout equal to " + c("timeout_ms") + " (10000 if zero or negative). " + c("SIGPIPE") + " is ignored, because pairing happens before any connection.",
        "<b>Binding to the connection.</b> 64 bytes are exported from the TLS session with " + c("SSL_export_keying_material") + " (label " + c("adb-label") + " including its terminating zero, no context). The SPAKE2 password is the code followed by these bytes, so a man in the middle cannot reuse the exchange on another connection. A code of 1 to 32 characters is accepted at this level.",
        "<b>SPAKE2.</b> On edwards25519, in the variant used by Android (BoringSSL's, with its quirks reproduced on purpose: without them the phone refuses). The PC is " + c("alice") + " with the name " + c("adb pair client") + ", the phone is " + c("bob") + " (" + c("adb pair server") + "); both names enter the final digest with their zero terminator (16 bytes each). The points M and N are fixed, generated from the seeds " + c("edwards25519 point generation seed (M)") + " and " + c("(N)") + ", and a test regenerates them. The arithmetic uses OpenSSL BIGNUMs: correct but not constant time, which is accepted because the code is valid once, for a few seconds, on the home network.",
        "<b>Key.</b> HKDF-SHA256 over the 64-byte SPAKE2 key with info " + c("adb pairing_auth aes-128-gcm key") + " (no terminating zero) gives a 16-byte AES key.",
        "<b>Information exchange.</b> Each side sends one packet of 8192 bytes plus the 16-byte AES-GCM tag, with nonce counter 0 (8 bytes little endian plus four zeros). The PC's plaintext is the type byte 0 (ADB public key) followed by the key text of " + c("ph_chiave_pubblica_adb") + "; the rest is zeros.",
        "<b>Verification.</b> A wrong code is detected only by the failure to decrypt the phone's packet (or by the phone closing): this is the single confirmation.",
    ]) + \
    table(["Field", "Size", "Value"], [
        ["Version", "1 byte", "1"],
        ["Type", "1 byte", "0 = SPAKE2 message, 1 = information"],
        ["Length", "4 bytes big endian", "at most " + c("2 * 8192") + "; checked before reading"],
        ["Body", "length", "32 bytes (SPAKE2) or 8192 + 16 bytes (information)"],
    ], "«TAB» — Pairing packet") + \
    p("Secrets are wiped with " + c("OPENSSL_cleanse") + " (password, shared key, AES key, plaintext). Failures return a specific sentence: phone silent on that port; phone closed the pairing (code expired or screen closed); wrong protocol version; unexpected or oversized answer; “wrong or expired code”.")

S3 = p("From the user's side pairing is a state of the link, not a separate program. When " + c("ph_adb_collega") + " reports that no candidate knows the PC key, the link enters " + c("NON_ABBINATO") + " and the shell shows the connection window.", lead=True) + \
    steps([
        "The window asks for the six-digit code. The field accepts only digits, at most 6, even when pasted: spaces and other characters are filtered out and the buffer is wiped (" + c("src/shell/connessione") + ").",
        "The shell registers the request before calling " + c("ph_col_abbina(code)") + " (or " + c("ph_col_abbina_a(host, port, code)") + " when an address is typed) and starts a timer. The outcome is not a return value: it arrives in a link notice whose field " + c("esito_abbinamento") + " is " + c("RIUSCITO") + " or " + c("FALLITO") + ", exactly once per accepted request. With no outcome in " + c("PH_SRV_ABB_LIMITE_S") + " (60) seconds the request counts as expired.",
        "The link thread looks for the pairing service for 1500 ms: the one at the address of the phone that answered “not paired”, otherwise the only one present; with more than one and no match, or none, the notice says to open the pairing-code screen on the phone and try again.",
        "It loads or creates the key and calls " + c("ph_abbinamento_esegui") + " with a 15000 ms limit. Success moves the state to " + c("CERCO") + " (“pairing done: connecting”); failure stays in " + c("NON_ABBINATO") + " with a sentence of the link's own: user-facing messages never contain addresses or the library's text.",
        "The code is wiped with " + c("explicit_bzero") + " right after use and never reaches a log or a file. If a close arrives during pairing no notice concludes the request; only " + c("CHIUSO") + " arrives.",
    ]) + \
    p(c("ph_col_riconnetti_ora") + " in " + c("NON_ABBINATO") + " retries the connection without pairing; it is refused (-1) while a pairing is accepted or running.") + \
    warn("Wrong code, pasted code with spaces, authorisation revoked from the phone, and wireless debugging switched off and on while the app is open were not yet tried on a real phone at the time of writing.")

CHAPTER = ("Discovery and pairing", [
    ("Discovery with mDNS", S1),
    ("Pairing with the six-digit code", S2),
    ("Pairing as seen by the link and the shell", S3),
])
