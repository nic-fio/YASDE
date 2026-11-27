#!/usr/bin/env python3
"""Prova integrata del pannello fisico sul telefono vero, attraverso il servizio e il canale comandi.
Nessun dato personale nei file. Uso: prova_pannello.py [1|2]   (1 = prove 1-6, 2 = prove 7-11; senza argomento tutte, circa 5 minuti)
Preparazione: jar completo (contratto + base + video + pannello) in
~/.cache/yasde-sviluppo/pannello/yasde-completo.jar e jar di prova (ProvaPannello, fuori dal jar di
produzione: lo costruisce `strumenti/prova.sh telefono-pannello`, sul server in
lavoro/telefono-pannello/telefono/pannello/yasde-prova-pannello.jar) in
~/.cache/yasde-sviluppo/pannello/yasde-prova-pannello.jar. Richiede adb. Il telefono deve essere
sbloccato e con lo schermo acceso; non si provano chiamate vere (stato finto: ProvaPannello), ne' il
blocco dello schermo.

PRUDENZA: la prova prende da sola il turno del telefono (mkdir ~/.cache/yasde-sviluppo/telefono.occupato)
e lo rilascia in ogni caso. Salva timeout dello schermo e frequenza minima e a fine prova controlla che siano
identici. PRIMA di spegnere il pannello parte sul telefono un processo separato che lo riaccende comunque dopo
90 secondi; in `finally` il pannello viene riacceso con lo stesso punto d'ingresso del guardiano.
Il timeout dello schermo della prova e' quello che l'utente ha (se fosse gia' al massimo il ripristino non si
vedrebbe, ma non c'e' nulla da ripristinare).

Prove: 1 avvio e API; 2 spegnimento con una sessione video attiva (frequenza e fotogrammi prima/dopo);
3 il tocco iniettato su schermo virtuale e il tasto iniettato non riaccendono; 4 chiamata finta;
5 tasto fisico (evdev, tastiera virtuale uhid) riaccende; 6 accensione chiesta dal PC; 7 kill -9 a pannello spento;
8 il valore dell'utente vince; 9 CHIUDI a pannello spento; 10 riaccensione che fallisce una volta e riesce al
ritentativo (da chiamata e da richiesta del PC); 11 misura della frequenza: vera (a pannello spento il
compositore scende da 120 a 60 e il rimedio porta la frequenza minima a 120: fotogrammi quasi come a pannello
acceso), finta (rimedio che scatta e si ripristina; che non scatta se la frequenza e' gia' la massima).
"""
import os, re, socket, struct, subprocess, sys, threading, time, queue

CACHE = os.path.expanduser("~/.cache/yasde-sviluppo/pannello")
JARLOCALE = CACHE + "/yasde-completo.jar"
JAR = "/data/local/tmp/pn-pannello.jar"
JARP = "/data/local/tmp/pn-pannello-prova.jar"
JARPLOCALE = CACHE + "/yasde-prova-pannello.jar"
EXTRA = "/data/local/tmp/pn-input-extra"
GUASTO = "/data/local/tmp/pn-guasto"
FREQ = "/data/local/tmp/pn-freq"
CHIAMATA = "/data/local/tmp/pn-chiamata"
TURNO = os.path.expanduser("~/.cache/yasde-sviluppo/telefono.occupato")
PORTA = 38471
TIMEOUT_PROVA = None  # il valore che l'utente ha prima della prova (si legge in prova())
MASSIMO = "2147483647"
W, H = 720, 1280
falliti = 0
LOG = []
gruppo = sys.argv[1] if len(sys.argv) > 1 else "tutto"  # 1 = prove 1-6; 2 = prove 7-11 (esecuzioni brevi)


def adb(*a):
    return subprocess.run(["adb"] + list(a), capture_output=True, text=True).stdout


def sh(c):
    return adb("shell", c)


def controlla(ok, nome, extra=""):
    global falliti
    print(("ok      " if ok else "FALLITA ") + nome + (("  [" + extra + "]") if extra else ""), flush=True)
    if not ok:
        falliti += 1


def potenza():
    m = re.search(r"powerMode=(\w+)", sh("dumpsys SurfaceFlinger | grep -E '^    powerMode'"))
    return m.group(1) if m else "?"


def frequenza():
    m = re.search(r"activeMode=\{id=\d+.*?vsyncRate=([\d.]+) Hz", sh("dumpsys SurfaceFlinger | grep -E '    activeMode'"))
    return float(m.group(1)) if m else 0.0


def stato_android():
    m = re.search(r"Display State=(\w+)", sh("dumpsys display | grep -m1 'Display State='"))
    return m.group(1) if m else "?"


def timeout():
    return sh("cmd settings get system screen_off_timeout").strip()


def minimo():
    return sh("cmd settings get system min_refresh_rate").strip()


def imposta(chiave, v):
    if v == "null":
        sh("cmd settings delete system %s" % chiave)
    else:
        sh("cmd settings put system %s %s" % (chiave, v))


def aspetta(cond, sec, passo=0.25):
    fine = time.time() + sec
    while time.time() < fine:
        if cond():
            return True
        time.sleep(passo)
    return cond()


def custode():
    return sh("cat /data/local/tmp/yasde-custode-*.sh 2>/dev/null")


def resti():
    # solo cio' che e' nostro o del servizio di questa prova
    return sh("ls /data/local/tmp | grep -E '^(pn-|yasde-custode)'").split()


def processi():
    return sh("ps -A -o PID,ARGS | grep -E 'it.yasde|getevent|pn-pannello' | grep -v grep").strip()


def pid_servizio():
    r = sh("ps -A -o PID,ARGS | grep -E '^ *[0-9]+ app_process .*it.yasde.ProvaPannello servizio'").split()
    return r[0] if r else None


def schermo_tocco():
    """Percorso del touchscreen (ABS_MT_POSITION_X + INPUT_PROP_DIRECT) da `getevent -pl`."""
    for blocco in re.split(r"(?m)^add device ", sh("getevent -pl")):
        if "ABS_MT_POSITION_X" in blocco and "INPUT_PROP_DIRECT" in blocco:
            m = re.search(r":\s*(/dev/input/event\d+)", blocco)
            if m:
                return m.group(1)
    return None


# ---- rete di sicurezza ----

def rete_avvia():
    sh('nohup setsid sh -c "sleep 90; CLASSPATH=%s app_process / it.yasde.Pannello accendi" '
       '>/dev/null 2>&1 & echo $! > /data/local/tmp/pn-rete.pid' % JAR)


def rete_ferma():
    sh("P=$(cat /data/local/tmp/pn-rete.pid 2>/dev/null); [ -n \"$P\" ] && kill $(pgrep -P $P) $P 2>/dev/null; "
       "rm -f /data/local/tmp/pn-rete.pid")


def accendi_comunque():
    """Lo stesso punto d'ingresso del guardiano."""
    return sh("CLASSPATH=%s app_process / it.yasde.Pannello accendi" % JAR).strip()


# ---- servizio ----

def push_jar():
    adb("push", JARLOCALE, JAR)  # il guardiano cancella il jar alla fine: a ogni avvio si ricopia
    adb("push", JARPLOCALE, JARP)


def lancia(limite_ms=None):
    LOG.clear()
    push_jar()
    p = subprocess.Popen(["adb", "shell", "CLASSPATH=%s:%s app_process / it.yasde.ProvaPannello servizio %s %s %s %s"
                          % (JAR, JARP, CHIAMATA, EXTRA, GUASTO, FREQ) + ((" %d" % limite_ms) if limite_ms else "")],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    # lettura con scadenza (30 s): un servizio che non dice PRONTO non deve bloccare la prova
    righe = queue.Queue()

    def leggi_righe():
        for r in p.stdout:
            righe.put(r)
        righe.put(None)

    threading.Thread(target=leggi_righe, daemon=True).start()
    fine = time.time() + 30
    while True:
        try:
            r = righe.get(timeout=max(0.1, fine - time.time()))
        except queue.Empty:
            r = False
        if r is False or r is None:
            p.kill()
            pid = pid_servizio()
            if pid:
                sh("kill -9 %s" % pid)  # solo il processo di questa prova (pattern ProvaPannello servizio)
            raise SystemExit("servizio non pronto entro 30 s o terminato prima di PRONTO: %r" % LOG)
        LOG.append(r.strip())
        if r.strip() == "PRONTO":
            def log():
                while True:
                    x = righe.get()
                    if x is None:
                        return
                    LOG.append(x.strip())
            threading.Thread(target=log, daemon=True).start()
            return p


def apri(riga):
    s = socket.create_connection(("127.0.0.1", PORTA), timeout=10)
    s.sendall(riga)
    return s


def recv_n(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            return None
        b += c
    return b


class Comandi:
    """Canale `comandi`: un thread legge tutto; risposte e eventi separati."""

    def __init__(self):
        adb("forward", "tcp:%d" % PORTA, "localabstract:yasde")
        self.s = apri(b"comandi\n")
        self.s.settimeout(None)  # il canale puo' restare muto a lungo: il thread di lettura aspetta
        self.risp = queue.Queue()
        self.eventi = queue.Queue()
        threading.Thread(target=self.ciclo, daemon=True).start()

    def ciclo(self):
        try:
            while True:
                h = recv_n(self.s, 5)
                if h is None:
                    return
                t, n = struct.unpack(">BI", h)
                b = recv_n(self.s, n)
                if b is None:
                    return
                (self.risp if t in (2, 3) else self.eventi).put((t, b))
        except OSError:
            return

    def chiedi(self, tipo, corpo=b"", sec=10):
        self.s.sendall(struct.pack(">BI", tipo, len(corpo)) + corpo)
        return self.risp.get(timeout=sec)

    def evento(self, tipo, sec):
        """Aspetta un evento del tipo dato; ritorna il corpo o None."""
        fine = time.time() + sec
        while time.time() < fine:
            try:
                t, b = self.eventi.get(timeout=max(0.05, fine - time.time()))
            except queue.Empty:
                return None
            if t == tipo:
                return b
        return None

    def svuota(self):
        while not self.eventi.empty():
            self.eventi.get()


def imposta_pannello(c, v):
    return c.chiedi(0x60, bytes([v]))


def stato_pannello(c):
    t, b = c.chiedi(0x61)
    return tuple(b) if t == 2 else None


def st(t):
    b = t.encode()
    return struct.pack(">H", len(b)) + b


class Video:
    """Una sessione video su schermo virtuale con scorrimento continuo (input sullo schermo VIRTUALE)."""

    def __init__(self, c):
        t, b = c.chiedi(0x40, struct.pack(">IHHHIB", 1, W, H, 320, 8000000, 60) + st("com.android.settings") + b"\x00")  # in coda la pagina (versione 2)
        controlla(t == 2, "VIDEO_APRI Impostazioni su schermo virtuale")
        self.v = apri(b"video:1\n")
        self.v.settimeout(None)
        self.inp = apri(b"input\n")
        self.fotogrammi = 0
        self.fermo = False
        self.pausa = False
        threading.Thread(target=self.leggi, daemon=True).start()
        threading.Thread(target=self.scorri, daemon=True).start()

    def leggi(self):
        try:
            while True:
                h = recv_n(self.v, 18)
                if h is None:
                    return
                g, sg, w, a, us, n = struct.unpack(">BBHHQI", h)
                if recv_n(self.v, n) is None:
                    return
                if g == 2:
                    self.fotogrammi += 1
        except OSError:
            return

    def scorri(self):
        d = 1
        try:
            while not self.fermo:
                if self.pausa:
                    time.sleep(0.1)
                    continue
                corpo = struct.pack(">IHHHHhh", 1, W, H, W // 2, H // 2, 0, d * 240)
                self.inp.sendall(struct.pack(">BI", 0x51, len(corpo)) + corpo)
                d = -d
                time.sleep(0.01)  # 100 eventi al secondo: con meno, sono gli eventi a limitare i fotogrammi
        except OSError:
            return

    def fps(self, sec):
        a = self.fotogrammi
        t0 = time.time()
        time.sleep(sec)
        return (self.fotogrammi - a) / (time.time() - t0)

    def chiudi(self):
        self.fermo = True
        for s in (self.v, self.inp):
            try:
                s.close()
            except OSError:
                pass


def prova():
    global falliti, TIMEOUT_PROVA
    for f in (JARLOCALE, JARPLOCALE):
        if not os.path.exists(f):
            raise SystemExit("manca " + f)
    # turno del telefono
    for _ in range(60):
        try:
            os.mkdir(TURNO)
            break
        except FileExistsError:
            time.sleep(25)
    else:
        raise SystemExit("turno del telefono non ottenuto")
    servizio = None
    video = None
    originali = (timeout(), minimo())
    TIMEOUT_PROVA = originali[0]
    print("valori salvati prima della prova: timeout=%s minimo=%s" % originali)
    try:
        push_jar()
        sh("rm -f %s %s %s %s" % (CHIAMATA, EXTRA, GUASTO, FREQ))
        controlla(potenza() == "ON", "partenza: pannello acceso")
        rete_avvia()

        if gruppo != "2":
            # 1 avvio
            servizio = lancia()
            controlla(any("[pannello] API di Android necessarie: tutte presenti" in r for r in LOG), "1 API del pannello presenti",
                      "; ".join(r for r in LOG if "pannello" in r)[:200])
            c = Comandi()
            t, b = c.chiedi(1, struct.pack(">H", 2))
            controlla(t == 2, "1 SALUTO")
            controlla(stato_pannello(c) == (1, 0), "1 STATO iniziale: acceso, non in mano")
            t, b = imposta_pannello(c, 7)
            controlla(t == 3, "1 valore non valido rifiutato", b[2:].decode("utf-8", "replace"))
            t, b = c.chiedi(0x6a)
            controlla(t == 3 and b[2:] == b"tipo sconosciuto", "1 tipo sconosciuto nel blocco 0x60-0x6f")

            # 2 spegnimento con video
            video = Video(c)
            time.sleep(3)
            fps_on = video.fps(4)
            hz_on = frequenza()
            t, b = imposta_pannello(c, 0)
            controlla(t == 2, "2 PANNELLO_IMPOSTA 0", b[2:].decode("utf-8", "replace"))
            ev = c.evento(0x6e, 3)
            controlla(ev == bytes([0, 4]), "2 evento spento, motivo 4 (chiesto dal PC)", repr(ev))
            controlla(potenza() == "OFF", "2 compositore: pannello SPENTO")
            controlla(stato_android() == "ON", "2 Android crede lo schermo ACCESO", stato_android())
            controlla(timeout() == MASSIMO, "2 timeout dello schermo al massimo", timeout())
            cu = custode()
            controlla("Pannello accendi" in cu and "pannello-timeout" in cu, "2 guardiano: ripristino del pannello e del timeout registrati")
            controlla(stato_pannello(c) == (0, 0), "2 STATO: spento, non in mano")
            time.sleep(2)
            fps_off = video.fps(4)
            hz_off = frequenza()
            controlla(fps_off > 10, "2 le app sul virtuale producono video a pannello spento",
                      "fps con pannello acceso %.1f, spento %.1f; frequenza compositore acceso %.0f Hz, spento %.0f Hz"
                      % (fps_on, fps_off, hz_on, hz_off))
            controlla(minimo() == "%.1f" % round(hz_on) and hz_off >= hz_on - 1,
                      "2 il compositore a pannello spento e' sotto la massima: il rimedio porta la frequenza minima alla massima",
                      "minimo %s, compositore %.0f Hz" % (minimo(), hz_off))
            controlla(fps_off > 0.7 * fps_on, "2 con il rimedio i fotogrammi a pannello spento sono almeno il 70% di quelli a pannello acceso",
                      "%.1f contro %.1f" % (fps_off, fps_on))
            controlla("pannello-frequenza" in custode(), "2 guardiano: ripristino della frequenza registrato (ordine 410)")
            video.pausa = True
            time.sleep(1)
            fps_fermo = video.fps(3)
            hz_fermo = frequenza()
            video.pausa = False
            controlla(hz_fermo >= 60, "2 a contenuto fermo la frequenza non scende sotto 60", "%.0f Hz, fps %.1f" % (hz_fermo, fps_fermo))
            t, b = imposta_pannello(c, 0)
            controlla(t == 2, "2 secondo spegnimento: idempotente")

            # 3 niente falsi positivi
            c.svuota()
            sh("input keyevent KEYCODE_SHIFT_LEFT")
            time.sleep(3)
            controlla(potenza() == "OFF" and c.evento(0x6e, 0.2) is None,
                      "3 tasto iniettato su display 0 e input sul virtuale (8+ s di scorrimento) NON riaccendono")

            # 4 chiamata finta
            sh("touch %s" % CHIAMATA)
            ev = c.evento(0x6e, 4)
            controlla(ev == bytes([1, 2]), "4 chiamata in arrivo: evento acceso, motivo 2", repr(ev))
            controlla(potenza() == "ON", "4 pannello riacceso da solo per la chiamata")
            controlla(stato_pannello(c) == (1, 1), "4 STATO: acceso, in mano")
            controlla("Pannello accendi" not in custode(), "4 guardiano: ripristino del pannello tolto a riaccensione riuscita")
            t, b = imposta_pannello(c, 0)
            controlla(t == 3 and "chiamata" in b[2:].decode("utf-8", "replace"), "4 spegnere durante la chiamata e' rifiutato con ERRORE",
                      b[2:].decode("utf-8", "replace"))
            controlla(potenza() == "ON", "4 e il pannello resta acceso")
            sh("rm -f %s" % CHIAMATA)
            ev = c.evento(0x6e, 4)
            controlla(ev == bytes([1, 3]), "4 fine chiamata: evento acceso, motivo 3", repr(ev))
            # chiamata che arriva a pannello spento
            t, b = imposta_pannello(c, 0)
            controlla(t == 2 and potenza() == "OFF", "4 spento di nuovo dopo la chiamata")
            c.svuota()
            sh("touch %s" % CHIAMATA)
            ev = c.evento(0x6e, 4)
            controlla(ev == bytes([1, 2]) and potenza() == "ON", "4 chiamata a pannello spento: lo riaccende", repr(ev))
            sh("rm -f %s" % CHIAMATA)
            c.evento(0x6e, 4)

            # 5 tasto fisico. I tocchi veri non si possono iniettare (la shell non scrive in /dev/input; gli input
            # di `input` non passano da li'). Si crea una tastiera virtuale (uhid) con il solo tasto F20, inutile per
            # Android, e la si aggiunge ai dispositivi ascoltati con il file di prova. Il touchscreen vero e' trovato
            # dalla stessa ricerca (provato sotto); la lettura dei suoi eventi usa lo stesso codice, provato su JVM.
            controlla(schermo_tocco() is not None, "5 touchscreen trovato tra i dispositivi di input")
            prima = set(sh("ls /dev/input").split())
            tast = subprocess.Popen(["adb", "shell", "CLASSPATH=%s:%s app_process / it.yasde.ProvaPannello tastiera" % (JAR, JARP)],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            try:
                controlla(tast.stdout.readline().strip() == "creato", "5 tastiera virtuale (uhid) creata")
                aspetta(lambda: set(sh("ls /dev/input").split()) - prima, 5)
                nuovi = sorted(set(sh("ls /dev/input").split()) - prima)
                controlla(len(nuovi) == 1, "5 un solo nuovo dispositivo di input", repr(nuovi))
                sh("echo /dev/input/%s > %s" % (nuovi[0], EXTRA))
                t, b = imposta_pannello(c, 0)
                controlla(t == 2 and potenza() == "OFF", "5 spento", b[2:].decode("utf-8", "replace"))
                c.svuota()
                tast.stdin.write("premi\n")
                tast.stdin.flush()
                ev = c.evento(0x6e, 4)
                controlla(ev == bytes([1, 1]), "5 tasto fisico: evento acceso, motivo 1", repr(ev))
                controlla(potenza() == "ON", "5 pannello riacceso da solo")
                controlla(stato_pannello(c) == (1, 1), "5 STATO: acceso, in mano")
                controlla("Pannello accendi" not in custode(), "5 guardiano: pannello tolto")
                controlla(timeout() == MASSIMO, "5 il timeout resta al massimo anche in mano", timeout())
                controlla(any("[pannello] evento di /dev/input/" + nuovi[0] in r for r in LOG), "5 il log dice quale dispositivo")
            finally:
                try:
                    tast.stdin.write("fine\n")
                    tast.stdin.flush()
                except OSError:
                    pass
                try:
                    tast.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    tast.kill()
                sh("rm -f %s" % EXTRA)
            controlla(aspetta(lambda: set(sh("ls /dev/input").split()) == prima, 5), "5 tastiera virtuale distrutta, dispositivi come prima")

            # 6 accensione chiesta dal PC
            t, b = imposta_pannello(c, 0)
            controlla(t == 2 and potenza() == "OFF", "6 spento")
            c.svuota()
            t, b = imposta_pannello(c, 1)
            controlla(t == 2 and potenza() == "ON", "6 PANNELLO_IMPOSTA 1: acceso")
            ev = c.evento(0x6e, 3)
            controlla(ev == bytes([1, 4]), "6 evento acceso, motivo 4", repr(ev))
            controlla(timeout() == TIMEOUT_PROVA, "6 timeout dell'utente ripristinato", timeout())
            controlla(minimo() == originali[1], "6 frequenza minima dell'utente ripristinata", minimo())
            cu = custode()
            controlla("Pannello accendi" not in cu and "pannello-timeout" not in cu, "6 guardiano ripulito")
            controlla(stato_pannello(c) == (1, 0), "6 STATO: acceso, non in mano")
            video.chiudi()
            c.chiedi(0x41, struct.pack(">IB", 1, 1))

        if gruppo != "1":
            if gruppo == "2":
                servizio = lancia()
                c = Comandi()
                c.chiedi(1, struct.pack(">H", 2))
            # 7 kill -9 a pannello spento
            t, b = imposta_pannello(c, 0)
            controlla(t == 2 and potenza() == "OFF" and timeout() == MASSIMO, "7 spento, timeout al massimo")
            pid = pid_servizio()
            t0 = time.time()
            sh("kill -9 %s" % pid)
            controlla(aspetta(lambda: potenza() == "ON", 15), "7 kill -9: il guardiano riaccende il pannello",
                      "%.1f s" % (time.time() - t0))
            controlla(aspetta(lambda: timeout() == TIMEOUT_PROVA, 10), "7 e ripristina il timeout", timeout())
            controlla(aspetta(lambda: minimo() == originali[1], 10), "7 e la frequenza minima", minimo())
            controlla(aspetta(lambda: not any(r.startswith("yasde-custode") for r in resti()), 10),
                      "7 il guardiano ha ripulito i suoi file")
            servizio.kill()

            # 8 il valore dell'utente vince
            servizio = lancia()
            c = Comandi()
            c.chiedi(1, struct.pack(">H", 2))
            t, b = imposta_pannello(c, 0)
            controlla(t == 2 and timeout() == MASSIMO, "8 spento, timeout al massimo")
            imposta("screen_off_timeout", "123456")  # l'utente lo cambia
            sh("kill -9 %s" % pid_servizio())
            controlla(aspetta(lambda: potenza() == "ON", 15), "8 kill -9: pannello riacceso")
            time.sleep(3)
            controlla(timeout() == "123456", "8 il timeout cambiato dall'utente NON viene toccato", timeout())
            imposta("screen_off_timeout", TIMEOUT_PROVA)
            servizio.kill()

            # 9 CHIUDI a pannello spento
            servizio = lancia()
            c = Comandi()
            c.chiedi(1, struct.pack(">H", 2))
            t, b = imposta_pannello(c, 0)
            controlla(t == 2 and potenza() == "OFF", "9 spento")
            t, b = c.chiedi(4)
            controlla(t == 2, "9 CHIUDI risponde OK")
            controlla(aspetta(lambda: potenza() == "ON", 10), "9 chiusura ordinata: pannello acceso")
            controlla(aspetta(lambda: timeout() == TIMEOUT_PROVA, 5), "9 timeout ripristinato", timeout())
            controlla(aspetta(lambda: pid_servizio() is None, 10), "9 il servizio e' terminato")
            controlla(aspetta(lambda: not any(r.startswith("yasde-custode") for r in resti()), 10),
                      "9 nessun file del guardiano rimasto")
            controlla(any("[pannello] chiuso (pannello riacceso)" in r for r in LOG), "9 il log del servizio dice che il pannello e' stato riacceso alla chiusura")
            # 10 riaccensione che fallisce una volta e riesce al ritentativo (punto di sostituzione solo di prova)
            servizio = lancia()
            c = Comandi()
            c.chiedi(1, struct.pack(">H", 2))
            t, b = imposta_pannello(c, 0)
            controlla(t == 2 and potenza() == "OFF", "10 spento")
            c.svuota()
            sh("touch %s" % CHIAMATA)  # chiamata a pannello spento ...
            sh("touch %s" % GUASTO)    # ... e la prima riaccensione fallisce
            ev = c.evento(0x6e, 6)
            controlla(ev == bytes([1, 2]) and potenza() == "ON", "10 chiamata: la riaccensione fallita viene ritentata e riesce", repr(ev))
            controlla(any("riaccensione fallita" in r for r in LOG), "10 il log dice che il primo tentativo e' fallito")
            controlla(stato_pannello(c) == (1, 1), "10 STATO: acceso, in mano (solo dopo la riuscita)")
            sh("rm -f %s" % CHIAMATA)
            c.evento(0x6e, 4)
            t, b = imposta_pannello(c, 0)
            controlla(t == 2 and potenza() == "OFF", "10 spento di nuovo")
            c.svuota()
            sh("touch %s" % GUASTO)
            t, b = imposta_pannello(c, 1)
            controlla(t == 3, "10 PANNELLO_IMPOSTA 1 con riaccensione fallita: ERRORE al PC", b[2:].decode("utf-8", "replace"))
            ev = c.evento(0x6e, 6)
            controlla(ev == bytes([1, 4]) and potenza() == "ON", "10 ... ma il ritentativo riaccende e manda l'evento (motivo 4)", repr(ev))
            controlla(timeout() == TIMEOUT_PROVA, "10 e il timeout dell'utente e' ripristinato dopo il ritentativo", timeout())

            # 11 frequenza finta: il rimedio scatta solo se la misura e' sotto la massima del display
            c.chiedi(4)  # chiude il servizio della prova 10
            aspetta(lambda: pid_servizio() is None, 10)
            servizio = lancia()
            c = Comandi()
            c.chiedi(1, struct.pack(">H", 2))
            sh("echo 24.0 90.0 > %s" % FREQ)
            t, b = imposta_pannello(c, 0)
            controlla(t == 2, "11 spento con frequenza (finta) di 24 Hz su un massimo di 90")
            controlla(aspetta(lambda: minimo() == "90.0", 6), "11 il rimedio scatta: frequenza minima alla massima (90.0)", minimo())
            cu = custode()
            controlla("min_refresh_rate" in cu and "pannello-frequenza" in cu, "11 guardiano: ripristino della frequenza registrato")
            controlla(any("frequenza del compositore a pannello spento: 24.0 Hz" in r for r in LOG), "11 la misura e' nel log")
            t, b = imposta_pannello(c, 1)
            controlla(t == 2 and minimo() == originali[1], "11 alla riaccensione la frequenza minima e' rimessa com'era", minimo())
            controlla("min_refresh_rate" not in custode(), "11 guardiano ripulito")
            t, b = imposta_pannello(c, 0)
            aspetta(lambda: minimo() == "90.0", 6)
            imposta("min_refresh_rate", "100.0")  # l'utente la cambia
            sh("kill -9 %s" % pid_servizio())
            controlla(aspetta(lambda: potenza() == "ON", 15), "11 kill -9 con rimedio attivo: pannello riacceso")
            time.sleep(3)
            controlla(minimo() == "100.0", "11 il valore dell'utente (100.0) vince sul ripristino", minimo())
            imposta("min_refresh_rate", originali[1])
            servizio.kill()
            servizio = lancia()
            c = Comandi()
            c.chiedi(1, struct.pack(">H", 2))
            sh("echo 120.0 120.0 > %s" % FREQ)
            t, b = imposta_pannello(c, 0)
            time.sleep(2.5)
            controlla(minimo() == originali[1], "11 con la frequenza gia' alla massima il rimedio NON scatta", minimo())
            t, b = c.chiedi(4)
            # tetto dell'utente a 60 Hz: 60 a pannello spento e' una sua scelta, non il sintomo
            aspetta(lambda: pid_servizio() is None, 10)
            peak_prima = sh("cmd settings get system peak_refresh_rate").strip()
            servizio = lancia()
            c = Comandi()
            c.chiedi(1, struct.pack(">H", 2))
            try:
                imposta("peak_refresh_rate", "60.0")
                sh("echo 60.0 120.0 > %s" % FREQ)
                t, b = imposta_pannello(c, 0)
                time.sleep(2.5)
                controlla(minimo() == originali[1], "11 con peak_refresh_rate a 60 il rimedio NON scatta", minimo())
                controlla(any("tetto dell'utente 60.0, voluta 60.0" in r for r in LOG), "11 il log dice tetto e frequenza voluta")
                t, b = imposta_pannello(c, 1)
            finally:
                imposta("peak_refresh_rate", peak_prima)
            t, b = c.chiedi(4)
    except BaseException:
        print("ECCEZIONE; ultime righe del log del servizio:")
        for r in LOG[-25:]:
            print("   ", r[:300])
        raise
    finally:
        if video:
            video.chiudi()
        if servizio and servizio.poll() is None:
            servizio.kill()
        pid = pid_servizio()
        if pid:
            sh("kill -9 %s" % pid)
            time.sleep(5)
        # rete di sicurezza: pannello acceso, impostazioni com'erano, niente rimasto
        push_jar()
        print("pannello acceso a fine prova:", accendi_comunque())
        rete_ferma()
        imposta("screen_off_timeout", originali[0])
        imposta("min_refresh_rate", originali[1])
        sh("rm -f %s %s %s %s %s %s" % (CHIAMATA, JAR, JARP, EXTRA, GUASTO, FREQ))
        time.sleep(2)
        dopo = (timeout(), minimo())
        controlla(potenza() == "ON", "FINE pannello acceso")
        controlla(dopo == originali, "FINE impostazioni identiche a prima", "prima %s dopo %s" % (originali, dopo))
        controlla(resti() == [], "FINE nessun file nostro sul telefono", repr(resti()))
        controlla(processi() == "", "FINE nessun processo nostro sul telefono", processi())
        adb("forward", "--remove", "tcp:%d" % PORTA)
        try:
            os.rmdir(TURNO)
        except OSError:
            pass
    print("falliti:", falliti)
    sys.exit(1 if falliti else 0)


if __name__ == "__main__":
    prova()

