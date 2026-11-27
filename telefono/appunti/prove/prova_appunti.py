#!/usr/bin/env python3
"""Prova integrata degli appunti sul telefono vero, attraverso il servizio e il canale `appunti`.
Nessun dato personale nei file. Uso: prova_appunti.py   (prende e rilascia da solo il turno del telefono).

Preparazione, in ~/.cache/yasde-sviluppo/appunti/: yasde-completo.jar (da `strumenti/prova.sh telefono`) e
yasde-prova-appunti.jar (ProvaAppunti, fuori dal jar di produzione: telefono/appunti/ del lavoro sul server).

Gli appunti attuali del telefono si leggono all'inizio e si tengono SOLO in memoria; a fine prova si rimettono
(se non erano testo, si lasciano vuoti e lo si dice). Il loro contenuto non viene mai stampato.
Gli appunti si cambiano "dal telefono" con un secondo processo shell (ProvaAppunti) e si leggono con un altro
processo ancora: e' il mezzo indipendente dal servizio. L'incolla si verifica con `uiautomator dump --display`.
"""
import base64, os, queue, re, socket, struct, subprocess, sys, threading, time
import xml.etree.ElementTree as ET

CACHE = os.path.expanduser("~/.cache/yasde-sviluppo/appunti")
JAR = "/data/local/tmp/ap-completo.jar"
JARP = "/data/local/tmp/ap-prova.jar"
UI = "/data/local/tmp/ap-ui.xml"
PORTA = 38471
falliti = 0
log_servizio = []


def adb(*a):
    return subprocess.run(["adb"] + list(a), capture_output=True, text=True).stdout


def sh(c):
    return adb("shell", c)


def controlla(ok, nome, extra=""):
    global falliti
    print(("ok      " if ok else "FALLITA ") + nome + (("  [" + extra + "]") if extra else ""))
    if not ok:
        falliti += 1


def prova_cmd(*args, stdin=None):
    cp = "CLASSPATH=%s:%s app_process / it.yasde.ProvaAppunti" % (JAR, JARP)
    return subprocess.run(["adb", "shell", cp + " " + " ".join(args) + " 2>&1"], input=stdin,
                          capture_output=True, text=True).stdout


def b64(t):
    return base64.b64encode(t.encode("utf-8")).decode()


def tel_leggi():
    """(stato, testo, sensibile): stato VUOTO, NONTESTO o TESTO."""
    o = prova_cmd("leggi").split("\n")
    r = o[0].strip()
    sens = any(x.strip() == "SENSIBILE 1" for x in o)
    if r.startswith("TESTO "):
        return "TESTO", base64.b64decode(r[6:]).decode("utf-8"), sens
    if r.startswith("NONTESTO"):
        return "NONTESTO", None, sens
    if r == "VUOTO":
        return "VUOTO", None, sens
    raise SystemExit("lettura appunti fallita: %s" % r[:80])


def tel_imposta(t, sens=False):
    # il testo passa da stdin (un argomento di 270 KB non entra in adb shell)
    return "IMPOSTATO" in prova_cmd("imposta", "-", "sens" if sens else "", stdin=b64(t))


def tel_ripeti(n):
    return "IMPOSTATO" in prova_cmd("ripeti", str(n))


def lancia():
    p = subprocess.Popen(["adb", "shell", "CLASSPATH=%s app_process / it.yasde.Servizio" % JAR],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    while True:
        r = p.stdout.readline()
        if not r:
            raise SystemExit("servizio terminato prima di PRONTO: %r" % log_servizio)
        log_servizio.append(r.strip())
        if r.strip() == "PRONTO":
            break

    def leggi_log():
        for r in p.stdout:
            log_servizio.append(r.strip())

    threading.Thread(target=leggi_log, daemon=True).start()
    return p


def apri(riga):
    adb("forward", "tcp:%d" % PORTA, "localabstract:yasde")
    s = socket.create_connection(("127.0.0.1", PORTA), timeout=5)
    s.sendall(riga)
    return s


def msg(s, tipo, corpo=b""):
    s.sendall(struct.pack(">BI", tipo, len(corpo)) + corpo)


def testo_corpo(t):
    b = t.encode("utf-8")
    return struct.pack(">I", len(b)) + b


def leggi_n(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            return None
        b += c
    return b


def leggi_msg(s):
    h = leggi_n(s, 5)
    if h is None:
        return None
    t, n = struct.unpack(">BI", h)
    return t, leggi_n(s, n) if n else b""


class Canale:
    """Il canale appunti con un thread che mette in coda i messaggi che arrivano dal telefono."""

    def __init__(self):
        self.s = apri(b"appunti\n")
        self.s.settimeout(None)
        self.coda = queue.Queue()
        self.chiuso = threading.Event()
        threading.Thread(target=self._legge, daemon=True).start()

    def _legge(self):
        try:
            while True:
                m = leggi_msg(self.s)
                if m is None:
                    break
                self.coda.put(m)
        except OSError:
            pass
        self.chiuso.set()

    def prossimo(self, attesa):
        try:
            return self.coda.get(timeout=attesa)
        except queue.Empty:
            return None

    def testo(self, attesa):
        m = self.prossimo(attesa)
        if m is None:
            return None
        t, corpo = m
        if t != 0x58:
            return "tipo inatteso %d" % t
        n = struct.unpack(">I", corpo[:4])[0]
        return corpo[4:4 + n].decode("utf-8") if n == len(corpo) - 4 else "lunghezza incoerente"

    def chiudi(self):
        try:
            self.s.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.s.close()


def comando(s, tipo, corpo=b""):
    msg(s, tipo, corpo)
    while True:
        m = leggi_msg(s)
        if m is None:
            return None
        if m[0] in (0x02, 0x03):
            return m


def pid_servizio():
    return sh("ps -A -o PID,ARGS | grep -E '^ *[0-9]+ app_process .*it.yasde.Servizio'").split()[:1]


def thread_appunti(pid):
    n = sh("cat /proc/%s/task/*/comm 2>/dev/null | grep -c yasde-appun" % pid).strip()
    return int(n) if n.isdigit() else 0


def dump_ui(display):
    sh("rm -f %s" % UI)
    sh("uiautomator dump --display %d %s >/dev/null 2>&1" % (display, UI))
    x = sh("cat %s" % UI)
    try:
        return ET.fromstring(x[x.index("<hierarchy"):])
    except ValueError:
        return None


def centro(nodo):
    m = re.match(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]", nodo.get("bounds", ""))
    a = [int(v) for v in m.groups()]
    return (a[0] + a[2]) // 2, (a[1] + a[3]) // 2


# testi che questa prova mette negli appunti: se all'avvio ne trova uno, e' un residuo e vale "vuoto"
TESTI_PROVA = {"prova test", "testo X", "dopo la riapertura", "ancora vivo", "seconda copia", "altro", "dal pc uno",
               "normale dopo sensibile", "segreto da password", "prima del troppo lungo", "sessione inesistente",
               "Perch\u00e9 \u00e8 cos\u00ec \u20ac \U0001f600", "dal pc uno",
               "Perch\u00e9 \u00e8 cos\u00ec: \u00e0\u00f2\u00f9 \U0001f600 \u4e2d\u6587",
               "Copiato sul telefono: citt\u00e0, \u20ac, \U0001f4cb"}

TURNO = os.path.expanduser("~/.cache/yasde-sviluppo/telefono.occupato")


def main():
    # il turno del telefono si tiene solo per la durata di questa esecuzione (rilasciato anche se la prova fallisce)
    try:
        os.mkdir(TURNO)
    except FileExistsError:
        raise SystemExit("turno del telefono occupato: riprovare piu' tardi")
    try:
        main_turno()
    finally:
        os.rmdir(TURNO)


def stato_appunti():
    st, t, _ = tel_leggi()
    return "vuoti" if st == "VUOTO" else "non vuoti, %d caratteri" % len(t or "") if st == "TESTO" else "non vuoti (non testo)"


def svuota_e_verifica(stato):
    out = prova_cmd("svuota")
    visto = stato_appunti()
    print("appunti iniziali (%s): svuotati (%s); riletti: %s" % (stato, "SVUOTATO" in out, visto))
    if visto != "vuoti":
        print("ATTENZIONE: appunti non vuoti dopo lo svuotamento")


def verifica():
    """Solo controllo: stampa lo stato; svuota solo se il testo e' uno dei testi di prova."""
    adb("push", CACHE + "/yasde-completo.jar", JAR)
    adb("push", CACHE + "/yasde-prova-appunti.jar", JARP)
    try:
        st, t, _ = tel_leggi()
        print("appunti del telefono:", stato_appunti())
        if st == "TESTO" and (t in TESTI_PROVA or set(t) <= set("ab")):
            svuota_e_verifica("residuo di prova")
        elif st != "VUOTO":
            print("testo NON riconosciuto come di prova: non toccato")
    finally:
        sh("rm -f %s %s" % (JAR, JARP))


def main_turno():
    if len(sys.argv) > 1 and sys.argv[1] == "verifica":
        return verifica()
    adb("push", CACHE + "/yasde-completo.jar", JAR)
    adb("push", CACHE + "/yasde-prova-appunti.jar", JARP)
    # PRIMA di tutto: gli appunti dell'utente, solo in memoria
    orig = tel_leggi()
    if orig[0] == "TESTO" and (orig[1] in TESTI_PROVA or set(orig[1]) <= set("ab")):
        # residuo di una prova precedente: l'utente aveva gli appunti vuoti
        orig = ("VUOTO", None, False)
    print("appunti iniziali del telefono: %s, %d caratteri (contenuto non mostrato)" % (orig[0], len(orig[1] or "")))
    p = lancia()
    try:
        print("log avvio:", [r for r in log_servizio if "appunti" in r])
        prova(p, orig, len(sys.argv) > 1 and sys.argv[1] == "incolla")
    finally:
        # ripristino degli appunti dell'utente
        stato, testo, sens = orig
        # la chiusura del servizio cancella il suo jar (e fa fuori chi lo usa): si rimettono i jar di prova
        adb("push", CACHE + "/yasde-completo.jar", JAR)
        adb("push", CACHE + "/yasde-prova-appunti.jar", JARP)
        if stato == "TESTO":
            ok = False
            for _ in range(3):
                time.sleep(1)
                try:
                    a = tel_imposta(testo, sens)
                    b = tel_leggi()
                    ok = a and b[1] == testo
                    print("   ripristino: imposta=%s stato=%s uguale=%s (%d caratteri)" % (a, b[0], b[1] == testo, len(testo)))
                except SystemExit as e:
                    print("   ripristino: lettura fallita (%s)" % e)
                if ok:
                    break
            print("appunti iniziali rimessi: %s" % ("si" if ok else "NO"))
        else:
            svuota_e_verifica(stato)
        try:
            p.stdin.close()
        except OSError:
            pass
        p.terminate()
        sh("rm -f %s %s %s /data/local/tmp/yasde-*.jar" % (UI, JAR, JARP))
        adb("forward", "--remove-all")
    print("falliti:", falliti)
    sys.exit(1 if falliti else 0)


def prova(p, orig, solo_incolla=False):
    c0 = apri(b"comandi\n")
    r = comando(c0, 1, struct.pack(">H", 3))
    controlla(r and r[0] == 2, "SALUTO -> OK")

    # --- canale appunti: PC -> telefono ---
    ch = Canale()
    time.sleep(0.5)
    if solo_incolla:
        prova_incolla(c0, ch)
        ch.chiudi()
        comando(c0, 4)
        time.sleep(2)
        return
    accenti = "Perché è così: àòù \U0001f600 中文"
    msg(ch.s, 0x59, testo_corpo(accenti))
    time.sleep(1)
    st, t, _ = tel_leggi()
    controlla(st == "TESTO" and t == accenti, "IMPOSTA con accenti ed emoji: appunti del telefono identici (lettura da altro processo)")
    controlla(ch.testo(2) is None, "nessuna eco di cio' che ha mandato il PC")

    # il PC imposta X, poi una copia vera di X fatta da un ALTRO processo sul telefono: deve arrivare al PC
    msg(ch.s, 0x59, testo_corpo("testo X"))
    time.sleep(1)
    controlla(ch.testo(1.5) is None, "il PC imposta X: nessuna eco")
    tel_imposta("testo X")
    controlla(ch.testo(5) == "testo X", "copia di X fatta da un altro processo dopo che il PC ha impostato X: arriva al PC")

    # --- telefono -> PC ---
    dalTel = "Copiato sul telefono: città, €, \U0001f4cb"
    tel_imposta(dalTel)
    r = ch.testo(5)
    controlla(r == dalTel, "copia fatta dal telefono -> APPUNTI_TESTO identico", "" if r == dalTel else "arrivato: %r" % (r and r[:20]))
    # un secondo cambio dopo l'eco: passa
    tel_imposta("seconda copia")
    controlla(ch.testo(5) == "seconda copia", "una seconda copia passa")
    # lo stesso testo del PC di nuovo, poi diverso, poi quello del PC copiato dal telefono: passa
    msg(ch.s, 0x59, testo_corpo("dal pc uno"))
    time.sleep(1)
    controlla(ch.testo(1.5) is None, "eco soppressa anche per un secondo testo del PC")
    tel_imposta("altro")
    ch.testo(5)
    tel_imposta("dal pc uno")
    controlla(ch.testo(5) == "dal pc uno", "dopo una copia diversa, il testo gia' mandato dal PC passa se copiato sul telefono")

    # --- sensibile: non arriva nulla ---
    tel_imposta("segreto da password", sens=True)
    st, t, sens = tel_leggi()
    controlla(sens, "copia marcata sensibile: la marca c'e' negli appunti")
    controlla(ch.testo(3) is None, "copia sensibile: non arriva nulla")
    # dopo una sensibile, una normale passa
    tel_imposta("normale dopo sensibile")
    controlla(ch.testo(5) == "normale dopo sensibile", "dopo una copia sensibile una normale passa")

    # --- limiti 200.000 byte ---
    tel_ripeti(200001)
    controlla(ch.testo(4) is None, "telefono->PC: 200001 byte ignorati")
    tel_ripeti(200000)
    r = ch.testo(8)
    controlla(r == "a" * 200000, "telefono->PC: 200000 byte passano")
    msg(ch.s, 0x59, testo_corpo("prima del troppo lungo"))
    time.sleep(1)
    ch.testo(1)
    msg(ch.s, 0x59, testo_corpo("a" * 200001))
    time.sleep(2)
    st, t, _ = tel_leggi()
    controlla(t == "prima del troppo lungo", "PC->telefono: 200001 byte ignorati (appunti invariati)")
    msg(ch.s, 0x59, testo_corpo("b" * 200000))
    time.sleep(2)
    st, t, _ = tel_leggi()
    controlla(t == "b" * 200000, "PC->telefono: 200000 byte accettati")
    # messaggio malformato / tipo sconosciuto: ignorati, il canale resta aperto
    msg(ch.s, 0x59, b"\x00\x00\x00\x09ab")
    msg(ch.s, 0x77, b"xx")
    msg(ch.s, 0x5a, b"\x00")
    time.sleep(1)
    msg(ch.s, 0x59, testo_corpo("ancora vivo"))
    time.sleep(1)
    controlla(tel_leggi()[1] == "ancora vivo" and not ch.chiuso.is_set(), "messaggi malformati ignorati, canale vivo")

    # --- secondo canale rifiutato ---
    ch2 = Canale()
    t0 = time.time()
    ch2.chiuso.wait(6)
    controlla(ch2.chiuso.is_set(), "secondo canale appunti rifiutato (chiuso dopo %.1f s)" % (time.time() - t0))

    # --- INCOLLA in un campo di testo su uno schermo virtuale ---
    prova_incolla(c0, ch)

    # --- chiusura del canale: thread finiti, riapertura ---
    pid = pid_servizio()[0]
    n1 = thread_appunti(pid)
    ch.chiudi()
    time.sleep(2)
    n2 = thread_appunti(pid)
    controlla(n1 >= 1 and n2 == 0, "chiusura del canale: thread del canale finiti (prima %d, dopo %d)" % (n1, n2))
    ch = Canale()
    time.sleep(0.5)
    tel_imposta("dopo la riapertura")
    controlla(ch.testo(5) == "dopo la riapertura", "riapertura del canale: copia dal telefono arriva")
    # lunghezza oltre 1 MiB: canale chiuso
    msg(ch.s, 0x59, b"")  # corpo vuoto: ignorato
    ch.s.sendall(struct.pack(">BI", 0x59, (1 << 20) + 1))
    controlla(ch.chiuso.wait(4), "lunghezza oltre 1 MiB: canale chiuso")
    time.sleep(1)
    controlla(thread_appunti(pid) == 0, "dopo la chiusura per errore nessun thread rimasto")
    ch = Canale()
    time.sleep(0.5)
    # --- chiusura del servizio con il canale aperto ---
    r = comando(c0, 4)
    controlla(r and r[0] == 2, "CHIUDI -> OK")
    time.sleep(3)
    controlla(ch.chiuso.is_set(), "canale chiuso dalla chiusura del servizio")
    controlla(not pid_servizio(), "servizio terminato, nessun processo rimasto")
    righe = [r for r in log_servizio if "appunti" in r]
    print("log del servizio sugli appunti:")
    for r in righe:
        print("   ", r)
    controlla(not any("ERRORE" in r for r in righe if "non inviata" not in r and "rifiutato" not in r and "ignorato" not in r and "sessione video 99" not in r),
              "nessun ERRORE inatteso nel log")


def prova_incolla(c0, ch):
    # apre una sessione video con Impostazioni (app di sistema con un campo di ricerca)
    corpo = struct.pack(">IHHHIB", 1, 720, 1280, 300, 2000000, 15) + struct.pack(">H", 20) + b"com.android.settings" + b"\x00"  # pagina 0 (protocollo versione 2)
    r = comando(c0, 0x40, corpo)
    controlla(r and r[0] == 2, "VIDEO_APRI di Impostazioni su uno schermo virtuale", "" if r and r[0] == 2 else repr(r))
    if not r or r[0] != 2:
        return
    v = apri(b"video:1\n")
    v.settimeout(None)
    threading.Thread(target=lambda: [None for _ in iter(lambda: v.recv(65536), b"")], daemon=True).start()
    time.sleep(4)
    m = re.search(r'"yasde-1", displayId (\d+)', sh("dumpsys display"))
    if not m:
        controlla(False, "id dello schermo virtuale trovato")
        return
    disp = int(m.group(1))
    # tocca il campo di ricerca
    albero = dump_ui(disp)
    cerca = None
    if albero is not None:
        for n in albero.iter("node"):
            if "search" in n.get("resource-id", "").lower() and n.get("clickable") == "true":
                cerca = n
                break
    controlla(cerca is not None, "campo di ricerca di Impostazioni trovato nello schermo virtuale")
    if cerca is not None:
        x, y = centro(cerca)
        sh("input -d %d tap %d %d" % (disp, x, y))
        time.sleep(2)
        albero = dump_ui(disp)
        campo = None
        # nelle Impostazioni di questo telefono il campo compare dopo un secondo tocco sulla barra in alto
        sh("input -d %d tap 360 64" % disp)
        time.sleep(1)
        albero = dump_ui(disp)
        for n in albero.iter("node") if albero is not None else []:
            if n.get("class", "").endswith(("EditText", "AutoCompleteTextView")):
                campo = n
        controlla(campo is not None, "un campo di testo e' comparso")
        if campo is not None:
            testo = "Perché è così € \U0001f600"
            id_sess = struct.pack(">I", 1)
            msg(ch.s, 0x5a, id_sess + testo_corpo(testo))
            time.sleep(2)
            albero = dump_ui(disp)
            visto = [n.get("text", "") for n in albero.iter("node") if n.get("class", "").endswith(("EditText", "AutoCompleteTextView"))] if albero is not None else []
            controlla(any(testo in t or "Perché è così" in t for t in visto), "INCOLLA: il testo con accenti e' comparso nel campo", "campo: %r" % (visto,))
            controlla(tel_leggi()[1] == testo, "dopo INCOLLA gli appunti del telefono restano con quel testo")
            controlla(ch.testo(2) is None, "nessuna eco dopo INCOLLA")
        # id sessione inesistente: solo una riga di log, il canale resta
        msg(ch.s, 0x5a, struct.pack(">I", 99) + testo_corpo("sessione inesistente"))
        time.sleep(1)
        controlla(tel_leggi()[1] != "sessione inesistente", "INCOLLA su sessione inesistente: appunti non toccati")
    sh("input -d %d keyevent 4" % disp)
    r = comando(c0, 0x41, struct.pack(">IB", 1, 1))
    controlla(r and r[0] == 2, "VIDEO_CHIUDI")
    v.close()


if __name__ == "__main__":
    main()
