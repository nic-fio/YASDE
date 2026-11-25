#!/usr/bin/env python3
"""Prova integrata di video e input sul telefono vero, attraverso il servizio e i canali veri.
Nessun dato personale nei file. Uso: prova_video.py [pacchetto2]
Preparazione: jar completo da `strumenti/prova.sh telefono` in ~/.cache/yasde-sviluppo/video/yasde.jar
Richiede adb e ffmpeg. Il telefono deve essere sbloccato.
"""
import os, signal, socket, struct, subprocess, sys, threading, time

CACHE = os.path.expanduser("~/.cache/yasde-sviluppo/video")
JAR = "/data/local/tmp/prova-video-%d.jar" % os.getpid()  # nome proprio della prova (mai yasde.jar)
PORTA = 38461
P1 = "com.android.settings"
P2 = sys.argv[1] if len(sys.argv) > 1 else "com.samsung.android.app.contacts"
falliti = 0
LOG = []


def _termina(*a):
    raise SystemExit("terminato")  # cosi' i blocchi finally (pulizia, turno) girano sempre


signal.signal(signal.SIGTERM, _termina)


PID = None  # pid del servizio lanciato da questa prova (ferma solo quello)
PS_SERVIZI = "ps -A -o PID,ARGS | grep -E '^ *[0-9]+ app_process .*it.yasde.Servizio' | grep -v grep"


def adb(*a):
    try:
        return subprocess.run(["adb"] + list(a), capture_output=True, text=True, timeout=40).stdout
    except subprocess.TimeoutExpired:
        return ""


def sh(c):
    return adb("shell", c)


def controlla(ok, nome, extra=""):
    global falliti
    print(("ok      " if ok else "FALLITA ") + nome + (("  [" + extra + "]") if extra else ""))
    if not ok:
        falliti += 1


def verifica_libero():
    """In sola lettura, PRIMA di qualunque push: se c'e' gia' un it.yasde o un guardiano, si esce."""
    processi = sh("ps -A -o PID,ARGS | grep -E 'it[.]yasde' | grep -v grep").strip()
    custodi = sh("ls /data/local/tmp | grep -E '^yasde-custode-'").strip()
    if processi or custodi:
        sys.exit("sul telefono c'e' gia' un servizio o un guardiano YASDE (altra prova o YASDE vero): mi fermo")


def _pid_servizi():
    return set(int(r.split()[0]) for r in sh(PS_SERVIZI).splitlines() if r.split() and r.split()[0].isdigit())


def pulisci():
    """Ferma solo il servizio lanciato da questa prova e cancella solo i file che ha creato."""
    global PID
    if PID:
        sh("kill -9 %d" % PID)
        sh("rm -f /data/local/tmp/yasde-custode-%d.sh" % PID)
    sh("rm -f %s" % JAR)
    adb("forward", "--remove", "tcp:%d" % PORTA)
    PID = None


def lancia():
    global PID
    prima = _pid_servizi()
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
            nuovi = _pid_servizi() - prima
            PID = max(nuovi) if nuovi else None
            return p, righe


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
    def __init__(self):
        self.s = apri(b"comandi\n")
        self.eventi = []

    def chiedi(self, tipo, corpo=b""):
        self.s.sendall(struct.pack(">BI", tipo, len(corpo)) + corpo)
        while True:
            h = recv_n(self.s, 5)
            t, n = struct.unpack(">BI", h)
            b = recv_n(self.s, n)
            if t == 0x48:
                self.eventi.append(struct.unpack(">IBHH", b))
                continue
            return t, b

    def attendi_evento(self, che, sec):
        fine = time.time() + sec
        self.s.settimeout(0.5)
        try:
            while time.time() < fine:
                if any(e[1] == che for e in self.eventi):
                    return True
                try:
                    h = recv_n(self.s, 5)
                    t, n = struct.unpack(">BI", h)
                    b = recv_n(self.s, n)
                    if t == 0x48:
                        self.eventi.append(struct.unpack(">IBHH", b))
                except socket.timeout:
                    pass
        finally:
            self.s.settimeout(10)
        return any(e[1] == che for e in self.eventi)


def st(t):
    b = t.encode()
    return struct.pack(">H", len(b)) + b


def apri_video(c, id, w, h, dpi, pacchetto, bps=8000000):
    return c.chiedi(0x40, struct.pack(">IHHHIB", id, w, h, dpi, bps, 60) + st(pacchetto))


class FlussoVideo:
    """Legge il canale video:<id> e salva i dati H.264 in un file."""

    def __init__(self, id, file):
        self.s = apri(b"video:%d\n" % id)
        self.pacchetti = []  # (genere, segni, w, h, tempo, lunghezza, istante)
        self.f = open(file, "wb")
        self.t = threading.Thread(target=self.ciclo, daemon=True)
        self.t.start()

    def ciclo(self):
        try:
            while True:
                h = recv_n(self.s, 18)
                if h is None:
                    return
                g, sg, w, a, us, n = struct.unpack(">BBHHQI", h)
                d = recv_n(self.s, n)
                if d is None:
                    return
                self.pacchetti.append((g, sg, w, a, us, n, time.time()))
                self.f.write(d)
                self.f.flush()
        except OSError:
            return

    def fotogrammi(self):
        return sum(1 for p in self.pacchetti if p[0] == 2)

    def chiudi(self):
        self.s.close()
        self.f.close()


def input_msg(s, tipo, corpo):
    s.sendall(struct.pack(">BI", tipo, len(corpo)) + corpo)


def tocco(s, id, w, h, az, x, y):
    input_msg(s, 0x50, struct.pack(">IHHBHHB", id, w, h, az, x, y, 1))


def rotella(s, id, w, h, x, y, oriz, vert):
    input_msg(s, 0x51, struct.pack(">IHHHHhh", id, w, h, x, y, oriz, vert))


def immagini(file, dest):
    """Decodifica tutti i fotogrammi in grigio 64x36: lista di bytes."""
    r = subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", file, "-vf", "scale=64:36", "-pix_fmt", "gray",
                        "-f", "rawvideo", dest], capture_output=True)
    d = open(dest, "rb").read()
    os.remove(dest)
    n = 64 * 36
    return [d[i:i + n] for i in range(0, len(d) - n + 1, n)]


def diff(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def resti(pid):
    """Il jar e il file del guardiano di questo servizio (altri file yasde-* non sono nostri)."""
    nome = "yasde-custode-%s.sh" % (pid[0] if pid else "0")
    return [f for f in sh("ls /data/local/tmp").split() if f in (os.path.basename(JAR), nome)]


def prova():
    os.makedirs(CACHE, exist_ok=True)
    adb("push", CACHE + "/yasde.jar", JAR)
    p, righe = lancia()
    print("log avvio servizio:", [r for r in righe if "video" in r.lower() or "input" in r.lower()])
    adb("forward", "tcp:%d" % PORTA, "localabstract:yasde")
    c = Comandi()
    # SALUTO
    t, b = c.chiedi(1, struct.pack(">H", 1))
    ver, api, w, h, dpi = struct.unpack(">5H", b[:10])
    controlla(t == 2 and ver == 1, "SALUTO", "api %d, %dx%d, %d dpi" % (api, w, h, dpi))
    # VIDEO_APRI
    t0 = time.time()
    t, b = apri_video(c, 1, 1280, 720, dpi, P1)
    controlla(t == 2, "VIDEO_APRI Impostazioni 1280x720", "%.0f ms" % ((time.time() - t0) * 1000))
    t, b = apri_video(c, 1, 1280, 720, dpi, P1)
    controlla(t == 3, "id duplicato -> ERRORE", b[2:].decode())
    t, b = apri_video(c, 9, 1281, 720, dpi, P1)
    controlla(t == 3, "misure non multiple di 8 -> ERRORE", b[2:].decode())
    t, b = apri_video(c, 9, 640, 480, dpi, "it.inesistente.app")
    controlla(t == 3, "app inesistente -> ERRORE", b[2:].decode())
    print("schermi virtuali ora:", sh("dumpsys display | grep -c 'yasde-'").strip())
    f1 = os.path.join(CACHE, "s1.h264")
    v1 = FlussoVideo(1, f1)
    ini = time.time()
    time.sleep(3)
    pk = v1.pacchetti
    controlla(len(pk) > 2 and pk[0][0] == 1 and pk[1][0] == 2 and pk[1][1] & 1,
              "canale video: prima CONFIGURAZIONE poi fotogramma chiave",
              "primi: %s" % [(x[0], x[1], x[2], x[3]) for x in pk[:3]])
    controlla(all(x[2] == 1280 and x[3] == 720 for x in pk), "misure nei pacchetti 1280x720")
    controlla(pk[0][6] - ini < 1.0, "prima configurazione entro 1 s", "%.0f ms" % ((pk[0][6] - ini) * 1000))
    ts = [x[4] for x in pk if x[0] == 2]
    controlla(all(b2 >= a2 for a2, b2 in zip(ts, ts[1:])), "tempi crescenti", "%d fotogrammi" % len(ts))

    ci = apri(b"input\n")
    cp = []  # checkpoint: indice del fotogramma

    def punto(nome):
        time.sleep(1.2)
        cp.append((nome, v1.fotogrammi()))

    time.sleep(3)
    punto("A inizio")
    rotella(ci, 1, 1280, 720, 640, 360, 0, 360)
    punto("B su (verticale +)")
    rotella(ci, 1, 1280, 720, 640, 360, 0, -360)
    punto("C giu (verticale -)")
    rotella(ci, 1, 1280, 720, 640, 360, 0, 360)
    punto("D su (verticale +)")
    tocco(ci, 1, 1280, 720, 0, 640, 360)
    time.sleep(0.06)
    tocco(ci, 1, 1280, 720, 2, 640, 360)
    punto("E dopo tocco")
    tocco(ci, 1, 1000, 720, 0, 640, 360)
    tocco(ci, 1, 1000, 720, 2, 640, 360)
    punto("F tocco con misure sbagliate")
    input_msg(ci, 0x54, struct.pack(">I", 1))
    punto("G dopo INDIETRO")
    # VIDEO_CHIAVE
    n0 = sum(1 for x in v1.pacchetti if x[1] & 1)
    tt = time.time()
    t, b = c.chiedi(0x43, struct.pack(">I", 1))
    time.sleep(1)
    n1 = sum(1 for x in v1.pacchetti if x[1] & 1)
    controlla(t == 2 and n1 > n0, "VIDEO_CHIAVE produce un fotogramma chiave", "%d -> %d" % (n0, n1))
    # RIDIMENSIONA
    tt = time.time()
    t, b = c.chiedi(0x42, struct.pack(">IHHH", 1, 960, 536, dpi))
    r = struct.unpack(">HH", b) if t == 2 else None
    controlla(r == (960, 536), "VIDEO_RIDIMENSIONA risponde con le misure effettive", "%.0f ms" % ((time.time() - tt) * 1000))
    time.sleep(1.5)
    dopo = [x for x in v1.pacchetti if x[2] == 960]
    controlla(len(dopo) > 2 and dopo[0][0] == 1 and dopo[1][0] == 2 and dopo[1][1] & 1,
              "dopo il ridimensionamento: CONFIGURAZIONE poi chiave a 960x536")
    t, b = c.chiedi(0x42, struct.pack(">IHHH", 77, 960, 536, dpi))
    controlla(t == 3, "ridimensiona sessione sconosciuta -> ERRORE")
    # evento con misure vecchie dopo ridimensionamento: scartato
    # (verificato sopra con misure sbagliate; qui un tocco con le misure nuove funziona)
    # analisi immagini della sessione 1
    v1.chiudi()
    fr = immagini(f1, os.path.join(CACHE, "tmp.raw"))
    pr = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v", "-show_entries", "stream=width,height,profile",
                         "-of", "csv=p=0", f1], capture_output=True, text=True).stdout.strip()
    print("ffprobe:", pr, "| fotogrammi decodificati:", len(fr), "| pacchetti-fotogramma:", v1.fotogrammi())
    def im(n):
        k = dict(cp)
        idx = [v for (nm, v) in cp if nm.startswith(n)][0]
        return fr[min(idx, len(fr)) - 1]
    A, B, C, D, E, F, G = [im(x) for x in "ABCDEFG"]
    controlla(diff(A, B) < 1.0, "rotella verticale + all'inizio della pagina: nessun movimento", "%.2f" % diff(A, B))
    controlla(diff(A, C) > 2.0, "rotella verticale - muove il contenuto", "%.2f" % diff(A, C))
    controlla(diff(C, D) > 2.0 and diff(A, D) < diff(A, C) / 2,
              "rotella verticale + riporta verso l'inizio della pagina", "A-D %.2f, A-C %.2f" % (diff(A, D), diff(A, C)))
    controlla(diff(D, E) > 3.0, "TOCCO apre una voce", "%.2f" % diff(D, E))
    controlla(diff(E, F) < 1.0, "TOCCO con misure sbagliate scartato", "%.2f" % diff(E, F))
    controlla(diff(F, G) > 3.0, "INDIETRO torna alla pagina precedente", "%.2f" % diff(F, G))

    # chiusura e riapertura del canale video della stessa sessione (ora a 960x536)
    v1r = FlussoVideo(1, os.path.join(CACHE, "s1r.h264"))
    time.sleep(2.5)
    pr = v1r.pacchetti
    controlla(len(pr) >= 2 and pr[0][0] == 1 and pr[1][0] == 2 and pr[1][1] & 1 and pr[0][2] == 960,
              "canale riaperto: CONFIGURAZIONE poi fotogramma chiave (960x536)", str([(x[0], x[1], x[2]) for x in pr[:3]]))
    v1r.chiudi()
    time.sleep(0.5)
    v1r = FlussoVideo(1, os.path.join(CACHE, "s1r2.h264"))
    time.sleep(2.5)
    pr = v1r.pacchetti
    controlla(len(pr) >= 2 and pr[0][0] == 1 and pr[1][0] == 2 and pr[1][1] & 1, "canale riaperto una seconda volta: stesso inizio")
    v1r.chiudi()

    # due sessioni insieme
    t, b = apri_video(c, 2, 720, 1280 if False else 1280, dpi, P2)
    controlla(t == 2, "seconda sessione (%s)" % P2, b[2:].decode() if t == 3 else "")
    if t == 2:
        v2 = FlussoVideo(2, os.path.join(CACHE, "s2.h264"))
        f1b = FlussoVideo(1, os.path.join(CACHE, "s1b.h264"))
        time.sleep(3)
        controlla(v2.fotogrammi() > 5 and f1b.fotogrammi() > 5, "due flussi insieme",
                  "s1 %d, s2 %d fotogrammi" % (f1b.fotogrammi(), v2.fotogrammi()))
        print("schermi virtuali:", sh("dumpsys display | grep -c 'yasde-'").strip())
        # app chiusa
        sh("am force-stop %s" % P2)
        controlla(c.attendi_evento(1, 6), "VIDEO_EVENTO 1 (app chiusa)", str(c.eventi))
        t, b = c.chiedi(0x41, struct.pack(">IB", 2, 1))
        controlla(t == 2, "VIDEO_CHIUDI sessione 2")
        v2.chiudi()
        f1b.chiudi()
    # (a) VIDEO_RIDIMENSIONA e VIDEO_CHIUDI mentre il PC NON legge il canale video
    t, b = apri_video(c, 10, 1280, 720, dpi, P1, bps=30000000)
    controlla(t == 2, "sessione 10 per la prova del canale non letto")
    muto = apri(b"video:10\n")  # collegato ma mai letto
    ci2 = apri(b"input\n")
    for i in range(60):  # molto movimento: il flusso riempie i buffer
        rotella(ci2, 10, 1280, 720, 640, 360, 0, 360 if i % 2 else -360)
        time.sleep(0.15)
    tt = time.time()
    t, b = c.chiedi(0x43, struct.pack(">I", 10))
    controlla(t == 2 and time.time() - tt < 2, "VIDEO_CHIAVE a canale non letto", "%.0f ms" % ((time.time() - tt) * 1000))
    tt = time.time()
    t, b = c.chiedi(0x42, struct.pack(">IHHH", 10, 960, 536, dpi))
    controlla(t == 2 and time.time() - tt < 2, "VIDEO_RIDIMENSIONA a canale non letto", "%.0f ms" % ((time.time() - tt) * 1000))
    for i in range(30):
        rotella(ci2, 10, 960, 536, 480, 268, 0, 360 if i % 2 else -360)
        time.sleep(0.1)
    tt = time.time()
    t, b = c.chiedi(0x41, struct.pack(">IB", 10, 1))
    controlla(t == 2 and time.time() - tt < 2, "VIDEO_CHIUDI a canale non letto", "%.0f ms" % ((time.time() - tt) * 1000))
    muto.close()
    ci2.close()
    t, b = c.chiedi(0x43, struct.pack(">I", 10))
    controlla(t == 3, "canale comandi ancora vivo dopo la chiusura (sessione 10 non esiste piu')")

    # (a2) il PC smette di leggere il canale, poi ne apre un secondo per la stessa sessione
    t, b = apri_video(c, 11, 1280, 720, dpi, P1, bps=30000000)
    muto = apri(b"video:11\n")
    ci2 = apri(b"input\n")
    for i in range(60):
        rotella(ci2, 11, 1280, 720, 640, 360, 0, 360 if i % 2 else -360)
        time.sleep(0.15)
    nuovo = FlussoVideo(11, os.path.join(CACHE, "s11.h264"))
    time.sleep(2)
    pn = nuovo.pacchetti
    controlla(len(pn) >= 2 and pn[0][0] == 1 and pn[1][0] == 2 and pn[1][1] & 1,
              "secondo canale dopo uno non letto: CONFIGURAZIONE e chiave entro 2 s", str([(x[0], x[1]) for x in pn[:3]]))
    nuovo.chiudi()
    muto.close()
    ci2.close()
    t, b = c.chiedi(0x41, struct.pack(">IB", 11, 1))
    controlla(t == 2, "chiusa la sessione 11")

    # (b) altre app: solo quelle con un'attivita' di avvio (ogni avvio ha --display dello schermo virtuale)
    candidati = ["com.sec.android.app.clockpackage", "com.samsung.android.calendar", "com.sec.android.app.myfiles",
                 "com.sec.android.gallery3d", "com.samsung.android.app.notes", "org.mozilla.geckoview_example",
                 "com.samsung.android.app.contacts"]
    inst = sh("pm list packages").split()
    nuove = []
    ids = []
    k = 20
    for pk in candidati:
        if "package:" + pk not in inst:
            print("   non installata:", pk)
            continue
        if len(nuove) >= 6:
            break
        t, b = apri_video(c, k, 720, 1280, dpi, pk)
        if t == 2:
            nuove.append((k, pk, FlussoVideo(k, os.path.join(CACHE, "a%d.h264" % k))))
            controlla(True, "app %s avviata" % pk)
        else:
            controlla(False, "app %s" % pk, b[2:].decode()[:120])
        k += 1
    time.sleep(4)
    for (kk, pk, fl) in nuove:
        controlla(fl.fotogrammi() >= 3 and fl.pacchetti[0][0] == 1, "flusso di %s" % pk, "%d fotogrammi" % fl.fotogrammi())
    # (c) rotella orizzontale: provata sul calendario (vista a mese) se c'e'
    for (kk, pk, fl) in nuove:
        if pk == "com.samsung.android.calendar":
            ci3 = apri(b"input\n")
            n0 = fl.fotogrammi()
            time.sleep(1.5)
            a0 = fl.fotogrammi()
            rotella(ci3, kk, 720, 1280, 360, 640, 360, 0)
            time.sleep(1.5)
            a1 = fl.fotogrammi()
            rotella(ci3, kk, 720, 1280, 360, 640, -360, 0)
            time.sleep(1.5)
            a2 = fl.fotogrammi()
            fl.chiudi()
            fr = immagini(os.path.join(CACHE, "a%d.h264" % kk), os.path.join(CACHE, "tmp2.raw"))
            im0, im1, im2 = (fr[min(x, len(fr)) - 1] for x in (a0, a1, a2))
            controlla(diff(im0, im1) > 2.0 or diff(im1, im2) > 2.0, "ROTELLA orizzontale cambia la schermata del calendario",
                      "%.2f / %.2f" % (diff(im0, im1), diff(im1, im2)))
            ci3.close()
    for (kk, pk, fl) in nuove:
        fl.chiudi()
        t, b = c.chiedi(0x41, struct.pack(">IB", kk, 1))
        controlla(t == 2, "chiusa %s con togli_task" % pk)
    time.sleep(1)

    # (d) schermo protetto (evento 2): schermata di conferma del blocco schermo di Impostazioni
    # (usa FLAG_SECURE); non si inserisce nulla. L'avvio e' sullo schermo virtuale.
    def schermi():
        return set(sh("dumpsys window displays | grep -E 'Display: mDisplayId' | sed 's/.*mDisplayId=//;s/ .*//'").split())
    prima_s = schermi()
    t, b = apri_video(c, 30, 720, 1280, dpi, P1)
    time.sleep(1.5)
    nuovo_s = schermi() - prima_s
    controlla(t == 2 and len(nuovo_s) == 1, "sessione 30 per la prova dello schermo protetto", str(nuovo_s))
    if len(nuovo_s) == 1:
        did = nuovo_s.pop()
        c.eventi = [e for e in c.eventi if e[0] != 30]
        out = sh("am start-activity --display %s -a android.app.action.SET_NEW_PASSWORD -f 0x18000000" % did)
        controlla("Error" not in out, "schermata protetta avviata sullo schermo virtuale", out.strip()[:80])
        ok = c.attendi_evento(2, 6)
        ev = [e for e in c.eventi if e[0] == 30 and e[1] == 2]
        controlla(ok and ev and ev[-1][2] == 1, "VIDEO_EVENTO 2 a=1 (schermo protetto acceso)", str(ev))
        # Si copre la schermata protetta con Impostazioni (sempre sullo schermo virtuale).
        sh("am start-activity --display %s -n com.android.settings/.Settings -f 0x18000000" % did)
        c.attendi_evento(2, 1)
        fine = time.time() + 6
        while time.time() < fine and not [e for e in c.eventi if e[0] == 30 and e[1] == 2 and e[2] == 0]:
            c.attendi_evento(99, 1)
        ev = [e for e in c.eventi if e[0] == 30 and e[1] == 2]
        controlla(len(ev) >= 2 and ev[-1][2] == 0, "VIDEO_EVENTO 2 a=0 (schermo non piu' protetto)", str(ev))
    c.chiedi(0x41, struct.pack(">IB", 30, 1))

    # (e) schermo protetto con 3 sessioni aperte insieme: l'evento 2 arriva solo per quelle protette
    prima_s = schermi()
    dis = {}
    for k in (40, 41, 42):
        t, b = apri_video(c, k, 720, 1280, dpi, P1)
        time.sleep(1.2)
        nuovi = schermi() - prima_s - set(dis.values())
        if t == 2 and len(nuovi) == 1:
            dis[k] = nuovi.pop()
    controlla(len(dis) == 3, "tre sessioni insieme per la prova dello schermo protetto", str(dis))
    if len(dis) == 3:
        c.eventi = []
        for k in (40, 42):
            sh("am start-activity --display %s -a android.app.action.SET_NEW_PASSWORD -f 0x18000000" % dis[k])
        fine = time.time() + 8
        while time.time() < fine and len([e for e in c.eventi if e[1] == 2 and e[2] == 1]) < 2:
            c.attendi_evento(99, 1)
        acc = sorted(e[0] for e in c.eventi if e[1] == 2 and e[2] == 1)
        controlla(acc == [40, 42], "evento 2 acceso solo per le sessioni 40 e 42", str(acc))
        for k in (40, 42):
            sh("am start-activity --display %s -n com.android.settings/.Settings -f 0x18000000" % dis[k])
        fine = time.time() + 8
        while time.time() < fine and len([e for e in c.eventi if e[1] == 2 and e[2] == 0]) < 2:
            c.attendi_evento(99, 1)
        spe = sorted(e[0] for e in c.eventi if e[1] == 2 and e[2] == 0)
        controlla(spe == [40, 42], "evento 2 spento per le sessioni 40 e 42", str(spe))
        tutti = [e for e in c.eventi if e[1] == 2]
        controlla(len(tutti) == 4, "evento 2 solo ai cambi (4 in tutto)", str(tutti))
    for k in dis:
        c.chiedi(0x41, struct.pack(">IB", k, 1))

    # togli_task
    def recenti(pk):
        r = sh("dumpsys activity recents | grep -E 'Recent #' | grep 'A=[0-9]*:%s'" % pk)
        return sorted(x.split("#")[2].split()[0] for x in r.splitlines())
    t, b = c.chiedi(0x41, struct.pack(">IB", 1, 1))
    time.sleep(1)
    tolto = [l for l in LOG if "[video] task" in l and "tolto" in l and "NON" not in l]
    controlla(t == 2 and bool(tolto), "VIDEO_CHIUDI togli_task=1: removeTask riuscito per il task dello schermo",
              "%s; recenti con Impostazioni dopo: %s" % (tolto[-1:] , recenti(P1)))
    t, b = c.chiedi(0x41, struct.pack(">IB", 1, 1))
    controlla(t == 3, "chiudere due volte -> ERRORE")
    # CHIUDI
    pid1 = [str(PID)] if PID else []
    t, b = c.chiedi(4)
    for _ in range(20):
        if p.poll() is not None:
            break
        time.sleep(0.5)
    time.sleep(1)
    pid = sh("ps -p %s -o PID | grep -v PID" % pid1[0]).strip() if pid1 else ""
    controlla(t == 2 and p.poll() is not None and not pid, "CHIUDI: servizio terminato")
    controlla(sh("dumpsys display | grep -c 'yasde-'").strip() == "0", "nessuno schermo virtuale")
    controlla(not resti(pid1), "jar e file del guardiano di questo servizio spariti", str(resti(pid1)))

    # kill -9 con una sessione aperta (il servizio ha cancellato il jar: lo si rimette)
    adb("push", CACHE + "/yasde.jar", JAR)
    p, righe = lancia()
    c = Comandi()
    c.chiedi(1, struct.pack(">H", 1))
    t, b = apri_video(c, 5, 1280, 720, dpi, "com.sec.android.app.clockpackage")  # Impostazioni lascerebbe uno schermo nel gestore finestre
    time.sleep(1)
    n_prima = sh("dumpsys display | grep -c 'yasde-'").strip()
    pid = [str(PID)] if PID else []
    sh("kill -9 %s" % pid[0])
    time.sleep(3)
    controlla(n_prima != "0", "(prima del kill) schermo virtuale presente", n_prima)
    controlla(sh("dumpsys display | grep -c 'yasde-'").strip() == "0", "kill -9: schermo virtuale sparito")
    controlla(not resti(pid), "kill -9: jar e file del guardiano cancellati", str(resti(pid)))
    sh("am force-stop %s >/dev/null 2>&1; true" % "it.inesistente")


TURNO = os.path.expanduser("~/.cache/yasde-sviluppo/telefono.occupato")


def avvia_turno(attesa_max=900):
    """Prende il turno del telefono (mkdir) e fa i controlli in sola lettura; ritorna True solo se il turno e' mio.
    Si esce se il telefono non e' collegato o sveglio o se c'e' gia' un servizio YASDE."""
    if "device" not in adb("devices").split("List of devices attached")[-1].split():
        sys.exit("telefono non collegato: mi fermo")
    scadenza = time.time() + attesa_max
    while True:
        try:
            os.mkdir(TURNO)
            break
        except FileExistsError:
            if time.time() > scadenza:
                sys.exit("turno occupato per troppo tempo: esco")
            time.sleep(25)
    try:
        if "mWakefulness=Awake" not in sh("dumpsys power | grep mWakefulness="):
            sys.exit("telefono non sveglio: mi fermo")
        verifica_libero()
    except BaseException:
        os.rmdir(TURNO)  # il turno e' mio: lo rilascio prima di uscire
        raise
    return True


def rilascia_turno():
    os.rmdir(TURNO)


if __name__ == "__main__":
    mio = False
    try:
        mio = avvia_turno()
        prova()
    finally:
        if mio:
            pulisci()
            for f in os.listdir(CACHE):
                if f.endswith((".h264", ".raw")):
                    os.remove(os.path.join(CACHE, f))
            rilascia_turno()
    print("falliti:", falliti)
    sys.exit(1 if falliti else 0)
