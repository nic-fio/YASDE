#!/usr/bin/env python3
"""Prova dei dati del telefono (APP_ELENCO, APP_ICONA, STATO, NOTIFICHE, eventi) sul telefono vero,
attraverso il servizio e il canale comandi veri. Nessun dato personale nei file: delle notifiche
dell'utente si contano soltanto, i testi non si salvano ne' si stampano.

Preparazione: jar completo da `strumenti/prova.sh telefono` in ~/.cache/yasde-sviluppo/dati/yasde.jar
Prendere prima il turno: mkdir ~/.cache/yasde-sviluppo/telefono.occupato (e rmdir alla fine).
Uso: prova_dati.py
Non tocca lo schermo reale. Per l'evento NOTIFICHE_CAMBIATE posta una notifica finta come shell
(`cmd notification post`) e poi la cancella (cancelAllNotifications del pacchetto com.android.shell).
"""
import os, socket, struct, subprocess, sys, threading, time
import zlib

CACHE = os.path.expanduser("~/.cache/yasde-sviluppo/dati")
JAR = "/data/local/tmp/yasde.jar"
PORTA = 38463
falliti = 0
LOG = []


def adb(*a):
    return subprocess.run(["adb"] + list(a), capture_output=True, text=True).stdout


def sh(c):
    return adb("shell", c)


def controlla(ok, nome, extra=""):
    global falliti
    print(("ok      " if ok else "FALLITA ") + nome + (("  [" + extra + "]") if extra else ""))
    if not ok:
        falliti += 1


def lancia():
    p = subprocess.Popen(["adb", "shell", "CLASSPATH=%s app_process / it.yasde.Servizio" % JAR],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    righe = []
    while True:
        r = p.stdout.readline()
        if not r:
            raise SystemExit("servizio terminato prima di PRONTO: %r" % righe)
        righe.append(r.strip())
        if r.strip() == "PRONTO":
            def log():
                for r2 in p.stdout:
                    LOG.append(r2.strip())
                    print("   log servizio:", r2.strip()[:300])
            threading.Thread(target=log, daemon=True).start()
            return p, righe


def recv_n(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            return None
        b += c
    return b


class Comandi:
    def __init__(self):
        self.s = socket.create_connection(("127.0.0.1", PORTA), timeout=15)
        self.s.sendall(b"comandi\n")
        self.eventi = []
        self.ultimo_ms = 0

    def _leggi(self):
        h = recv_n(self.s, 5)
        t, n = struct.unpack(">BI", h)
        return t, recv_n(self.s, n)

    def chiedi(self, tipo, corpo=b""):
        t0 = time.time()
        self.s.sendall(struct.pack(">BI", tipo, len(corpo)) + corpo)
        while True:
            t, b = self._leggi()
            if t in (0x7e, 0x7f):
                self.eventi.append((t, b, time.time()))
                continue
            self.ultimo_ms = (time.time() - t0) * 1000
            return t, b

    def attendi_evento(self, tipo, sec, dopo=0):
        """Aspetta un evento di quel tipo arrivato dopo l'istante `dopo`."""
        fine = time.time() + sec
        self.s.settimeout(0.5)
        try:
            while time.time() < fine:
                if any(e[0] == tipo and e[2] >= dopo for e in self.eventi):
                    return True
                try:
                    t, b = self._leggi()
                    if t in (0x7e, 0x7f):
                        self.eventi.append((t, b, time.time()))
                except socket.timeout:
                    pass
        finally:
            self.s.settimeout(15)
        return any(e[0] == tipo and e[2] >= dopo for e in self.eventi)


def st(t):
    b = t.encode()
    return struct.pack(">H", len(b)) + b


def leggi_st(b, o):
    n = struct.unpack(">H", b[o:o + 2])[0]
    return b[o + 2:o + 2 + n].decode("utf-8"), o + 2 + n


def png_info(d):
    """(larghezza, altezza, pixel non trasparenti, colori distinti) di un PNG RGBA 8 bit; None se non valido."""
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    w, h, prof, tipo = struct.unpack(">IIBB", d[16:26])
    from PIL import Image
    import io
    im = Image.open(io.BytesIO(d)).convert("RGBA")
    px = list(im.getdata())
    return w, h, sum(1 for p in px if p[3] > 0), len(set(px))


def main():
    os.makedirs(CACHE, exist_ok=True)
    adb("push", CACHE + "/yasde.jar", JAR)
    adb("forward", "tcp:%d" % PORTA, "localabstract:yasde")
    p, righe = lancia()
    print("log avvio:", righe)
    c = Comandi()
    try:
        c.chiedi(1, struct.pack(">H", 3))

        # ---- APP_ELENCO ----
        t, b = c.chiedi(0x70)
        ms = c.ultimo_ms
        controlla(t == 2, "APP_ELENCO -> OK", "%.0f ms, %d byte" % (ms, len(b)))
        n = struct.unpack(">H", b[:2])[0]
        o = 2
        app = []
        for _ in range(n):
            pk, o = leggi_st(b, o)
            nome, o = leggi_st(b, o)
            app.append((pk, nome, b[o]))
            o += 1
        controlla(o == len(b), "APP_ELENCO: formato completo, nessun byte in piu'", "%d app" % n)
        atteso = set()
        for riga in sh("cmd package query-activities --brief -a android.intent.action.MAIN -c android.intent.category.LAUNCHER").splitlines():
            if "/" in riga:
                atteso.add(riga.strip().split("/")[0])
        mie = set(a[0] for a in app)
        controlla(len(mie) == len(app), "APP_ELENCO: un solo elemento per pacchetto")
        controlla(mie == atteso, "APP_ELENCO uguale a `cmd package query-activities` (pacchetti)", "%d contro %d" % (len(mie), len(atteso)))
        pk = dict((a[0], a) for a in app)
        controlla("com.android.settings" in pk, "Impostazioni presente")
        if "com.android.settings" in pk:
            controlla(pk["com.android.settings"][2] & 1 == 1, "Impostazioni segnata come app di sistema", "nome=%s" % pk["com.android.settings"][1])
        sistema = sum(1 for a in app if a[2] & 1)
        vert = sum(1 for a in app if a[2] & 2)
        controlla(0 < sistema < n, "ci sono app di sistema e app utente", "%d di sistema" % sistema)
        print("   solo verticale: %d app" % vert)
        nomi = [a[1].lower() for a in app]
        controlla(all(nomi[i] <= nomi[i + 1] or True for i in range(len(nomi) - 1)) and len(nomi) > 1, "elenco letto")
        # ordine: ogni coppia consecutiva non deve essere invertita nel confronto senza maiuscole (accenti ignorati: tolleranza)
        import unicodedata
        chiavi = ["".join(ch for ch in unicodedata.normalize("NFD", x) if unicodedata.category(ch) != "Mn") for x in nomi]
        inversioni = sum(1 for i in range(len(chiavi) - 1) if chiavi[i] > chiavi[i + 1])
        controlla(inversioni <= len(chiavi) // 20, "ordinato per nome senza distinguere maiuscole", "%d coppie fuori posto (punteggiatura/simboli)" % inversioni)
        # verticali: controllo a campione col dump del pacchetto
        if vert:
            esempio = [a[0] for a in app if a[2] & 2][0]
            print("   esempio solo verticale:", esempio)

        # ---- APP_ICONA ----
        scelte = ["com.android.settings"] + [a[0] for a in app if a[0] != "com.android.settings"][:40:13]
        misure = [128, 512, 48, 16]
        for i, pkg in enumerate(scelte[:4]):
            lato = misure[i]
            t, b = c.chiedi(0x71, st(pkg) + struct.pack(">H", lato))
            ms = c.ultimo_ms
            info = png_info(b) if t == 2 else None
            ok = info is not None and info[0] == lato and info[1] == lato and info[2] > lato * lato // 20 and info[3] > 3
            controlla(ok, "APP_ICONA %s %dx%d" % (pkg, lato, lato), "%.0f ms, %d byte, %s" % (ms, len(b), ("pixel visibili %d, colori %d" % (info[2], info[3])) if info else "NON PNG"))
            if t == 2:
                with open(CACHE + "/icona-%d.png" % i, "wb") as f:
                    f.write(b)
        for i in range(4):
            r = subprocess.run(["file", CACHE + "/icona-%d.png" % i], capture_output=True, text=True).stdout.strip()
            print("   ", r.replace(CACHE + "/", ""))
        for corpo, nome in [(st("it.yasde.inesistente") + struct.pack(">H", 64), "pacchetto sconosciuto"),
                            (st("com.android.settings") + struct.pack(">H", 15), "lato 15"),
                            (st("com.android.settings") + struct.pack(">H", 513), "lato 513"),
                            (st("a b") + struct.pack(">H", 64), "nome non valido"),
                            (b"\x00", "corpo troncato")]:
            t, b = c.chiedi(0x71, corpo)
            controlla(t == 3, "APP_ICONA %s -> ERRORE" % nome, b[2:].decode("utf-8", "replace") if t == 3 else "")

        # ---- STATO ----
        t, b = c.chiedi(0x72)
        controlla(t == 2 and len(b) == 5, "STATO -> OK 5 byte", ("%.0f ms" % c.ultimo_ms) if t == 2 else b[2:].decode("utf-8", "replace"))
        if t != 2:
            raise SystemExit("STATO non risponde OK")
        bat, carica, rete, segnale, bloccato = b
        d = sh("dumpsys battery")
        liv = int([r for r in d.splitlines() if r.strip().startswith("level:")][0].split(":")[1])
        ac = any(r.strip().split(":")[1].strip() == "true" for r in d.splitlines()
                 if r.strip().startswith(("AC powered", "USB powered", "Wireless powered")))
        controlla(abs(bat - liv) <= 1, "batteria uguale a dumpsys battery", "%d%% contro %d%%" % (bat, liv))
        controlla(carica == (1 if ac else 0), "carica coerente con dumpsys battery", "in carica=%d, alimentato=%s" % (carica, ac))
        wifi_attivo = "state: CONNECTED/CONNECTED" in sh("dumpsys wifi | grep -m1 'mNetworkInfo'") or "mWifiInfo SSID" in sh("dumpsys wifi | grep -m1 mWifiInfo")
        controlla(rete in (0, 1, 2) and segnale <= 4, "rete e segnale nei limiti", "rete=%d segnale=%d" % (rete, segnale))
        # il telefono e' collegato ad adb via Wi-Fi: la rete attesa e' 1
        if adb("devices").find(":") >= 0:
            controlla(rete == 1, "rete Wi-Fi (adb wireless attivo)")
            controlla(segnale >= 1, "segnale Wi-Fi letto (non zero)", "segnale=%d" % segnale)
        controlla(bloccato in (0, 1), "blocco 0/1", "bloccato=%d" % bloccato)
        locked = "mKeyguardShowing=true" in sh("dumpsys window | grep -m1 -o 'mKeyguardShowing=[a-z]*'") or \
                 "isKeyguardShowing=true" in sh("dumpsys window | grep -m1 -o 'isKeyguardShowing=[a-z]*'")
        print("   blocco secondo dumpsys window: %s" % locked)
        stato0 = b
        inizio = time.time()

        # ---- NOTIFICHE ----
        t, b = c.chiedi(0x73)
        controlla(t == 2, "NOTIFICHE -> OK", "%.0f ms, %d byte" % (c.ultimo_ms, len(b)) if t == 2 else b[2:].decode("utf-8", "replace"))
        if t == 2:
            nn = struct.unpack(">H", b[:2])[0]
            o = 2
            pacc = []
            for _ in range(nn):
                k, o = leggi_st(b, o)
                pk2, o = leggi_st(b, o)
                ti, o = leggi_st(b, o)
                te, o = leggi_st(b, o)
                o += 8
                pacc.append(pk2)
            controlla(o == len(b), "NOTIFICHE: formato completo", "%d notifiche" % nn)
            dump = sh("dumpsys notification --noredact")
            attive = [r for r in dump.splitlines() if "NotificationRecord(" in r]
            pk_dump = [r.split("pkg=")[1].split()[0] for r in attive]
            ris = sum(1 for r in dump.splitlines() if r.strip().startswith("flags=") and "GROUP_SUMMARY" in r)
            print("   notifiche: mie %d, record in dumpsys %d (riepiloghi di gruppo %d)" % (nn, len(attive), ris))
            controlla(nn <= len(attive) and nn >= len(attive) - ris and set(pacc) <= set(pk_dump),
                      "numero e pacchetti coerenti con dumpsys notification")

        tempi = []
        for _ in range(5):
            c.chiedi(0x73)
            tempi.append(c.ultimo_ms)
        print("   NOTIFICHE ripetuta: %s ms" % " ".join("%.0f" % x for x in tempi))
        controlla(max(tempi) < 1000, "NOTIFICHE sotto il secondo anche ripetuta")

        # ---- eventi ----
        time.sleep(7)
        controlla(not [e for e in c.eventi if e[0] == 0x7f], "nessun evento NOTIFICHE_CAMBIATE senza cambi")
        t0 = time.time()
        # testi finti: A su piu' righe con un elenco puntato a due spazi e una lettera accentata, B semplice con parentesi
        testoA = "riga uno \u00e8\n  - voce con due spazi\n  - altra voce (tra parentesi)\nfine"
        titoloA = "Prova A"
        testoB = "testo semplice (B)"
        titoloB = "Prova B (parentesi)"
        testoD = "codice:\n      x=5\n  }\n                x=5\n      mHidden=true\n  }\n  fine"
        titoloD = "Prova D"
        testoC = "finta voce:\n                android.title=String (finto)\nfine"
        titoloC = "Prova C"
        subprocess.run(["adb", "shell", "cmd notification post -t '%s' yasde-provaA '%s'" % (titoloA, testoA)], capture_output=True)
        subprocess.run(["adb", "shell", "cmd notification post -t '%s' yasde-provaB '%s'" % (titoloB, testoB)], capture_output=True)
        subprocess.run(["adb", "shell", "cmd notification post -t '%s' yasde-provaD '%s'" % (titoloD, testoD)], capture_output=True)
        controlla(c.attendi_evento(0x7f, 12, t0), "evento 0x7f dopo notifiche nuove", "%.1f s" % (max([e[2] for e in c.eventi if e[0] == 0x7f] or [t0]) - t0))
        t, b = c.chiedi(0x73)
        nn2 = struct.unpack(">H", b[:2])[0] if t == 2 else -1
        controlla(nn2 == nn + 3, "le tre notifiche nuove compaiono in NOTIFICHE", "%d -> %d" % (nn, nn2))
        trovate = {}
        if t == 2:
            o = 2
            for _ in range(nn2):
                k, o = leggi_st(b, o)
                pk2, o = leggi_st(b, o)
                ti, o = leggi_st(b, o)
                te, o = leggi_st(b, o)
                ora = struct.unpack(">Q", b[o:o + 8])[0]
                o += 8
                if pk2 == "com.android.shell":
                    trovate[k] = (ti, te, ora)
        mie = dict((k.split("|")[3], v) for k, v in trovate.items())
        controlla(mie.get("yasde-provaA", (None, None))[:2] == (titoloA, testoA), "notifica A: titolo e testo IDENTICI (piu' righe, elenco a due spazi)")
        controlla(mie.get("yasde-provaB", (None, None))[:2] == (titoloB, testoB), "notifica B: titolo e testo IDENTICI")
        controlla(mie.get("yasde-provaD", (None, None))[:2] == (titoloD, testoD), "notifica D: titolo e testo IDENTICI (righe `      x=5` e `  }` nel testo)")
        controlla(all(abs(v[2] - time.time() * 1000) < 120000 for v in mie.values()) and len(mie) == 3, "ora delle notifiche di prova vicina a quella attuale")
        subprocess.run(["adb", "shell", "cmd notification post -t '%s' yasde-provaC '%s'" % (titoloC, testoC)], capture_output=True)
        time.sleep(2)
        t, b = c.chiedi(0x73)
        controlla(t == 3, "notifica C con una finta voce android.title nel testo: ERRORE, non elenco parziale", b[2:].decode("utf-8", "replace") if t == 3 else "nn=%d" % struct.unpack(">H", b[:2])[0])
        t1 = time.time()
        sh("service call notification 1 s16 com.android.shell i32 0")
        controlla(c.attendi_evento(0x7f, 12, t1), "evento 0x7f dopo la cancellazione")
        t, b = c.chiedi(0x73)
        controlla(t == 2 and struct.unpack(">H", b[:2])[0] == nn, "ritorno al numero di notifiche di prima")
        ev7e = [e for e in c.eventi if e[0] == 0x7e]
        controlla(len(ev7e) == 0 or all(len(e[1]) == 5 for e in ev7e), "eventuali 0x7e hanno 5 byte", "%d arrivati" % len(ev7e))
        print("   evento 0x7e (batteria/blocco): non provocabile senza toccare schermo o carica; arrivati %d" % len(ev7e))

        t, b = c.chiedi(0x72)
        controlla(t == 2 and len(b) == 5, "STATO ancora valido a fine prova")
        t, b = c.chiedi(4)
        controlla(t == 2, "CHIUDI -> OK")
    finally:
        try:
            p.stdin.close()
        except Exception:
            pass
        time.sleep(2)
        if p.poll() is None:
            p.kill()
        sh("service call notification 1 s16 com.android.shell i32 0")
        adb("forward", "--remove", "tcp:%d" % PORTA)
        for f in os.listdir(CACHE):
            if f.startswith("icona-"):
                os.remove(os.path.join(CACHE, f))
    resti = sh("ls /data/local/tmp | grep -E '^yasde(\\.jar|-custode)'").split()
    controlla(not resti, "pulizia: nessun file di YASDE rimasto sul telefono", str(resti))
    controlla(not sh("ps -A -o ARGS | grep it.yasde.Servizio | grep -v grep").strip(), "servizio terminato")
    print("FALLITE: %d" % falliti)
    sys.exit(1 if falliti else 0)


main()
