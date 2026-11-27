#!/usr/bin/env python3
"""Prova di CHIAMATE (0x91), RETE (0x92), MEDIA (0x93) e di MEDIA_COMANDO fuori elenco (0x94) sul telefono vero.
NON stampa ne' salva nomi, numeri, titoli o reti: per le stringhe solo la lunghezza. NON manda comandi media validi (niente suono).
Preparazione: jar da `strumenti/prova.sh telefono` in ~/.cache/yasde-sviluppo/telefono-dati/yasde.jar.
Regole di CLAUDE.md («Prove sul telefono»): turno preso e tolto solo da questo script; controllo iniziale in
sola lettura (se c'e' gia' un it.yasde o un yasde-custode-*, esce); jar con nome proprio; si
uccide solo il proprio pid; ogni comando adb ha una scadenza; pulizia in finally.
"""
import os, queue, socket, struct, subprocess, sys, threading, time, secrets

CACHE = os.path.expanduser("~/.cache/yasde-sviluppo/telefono-dati")
TURNO = os.path.expanduser("~/.cache/yasde-sviluppo/telefono.occupato")
NOME = "yasde-telefonia-%s.jar" % secrets.token_hex(4)
JAR = "/data/local/tmp/" + NOME
PORTA = 38473
falliti = 0
PID_REMOTI = []


def pid_remoti():
    """Pid sul telefono del servizio avviato da questo script: la riga di `sh` che porta il nome unico del NOSTRO jar e i suoi figli."""
    righe = [r.split(None, 2) for r in adb("shell", "ps -A -o PID,PPID,ARGS").splitlines()[1:]]
    padri = {r[0] for r in righe if len(r) == 3 and NOME in r[2] and "ps -A" not in r[2] and "grep" not in r[2]}
    figli = {r[0] for r in righe if len(r) >= 2 and r[1] in padri}
    return sorted(padri | figli)


def adb(*a, timeout=20):
    try:
        return subprocess.run(["adb"] + list(a), capture_output=True, text=True, timeout=timeout).stdout
    except subprocess.TimeoutExpired:
        return ""


def controlla(ok, nome, extra=""):
    global falliti
    print(("ok      " if ok else "FALLITA ") + nome + (("  [" + extra + "]") if extra else ""))
    if not ok:
        falliti += 1


def recv_n(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            raise OSError("canale chiuso")
        b += c
    return b


def chiedi(s, tipo, corpo=b""):
    t0 = time.time()
    s.sendall(struct.pack(">BI", tipo, len(corpo)) + corpo)
    while True:
        t, n = struct.unpack(">BI", recv_n(s, 5))
        b = recv_n(s, n)
        if t in (0x7e, 0x7f):
            continue
        return t, b, (time.time() - t0) * 1000


class Corpo:
    """Lettore con controllo di fine: ogni lettura oltre il corpo e' un errore di formato."""
    def __init__(self, b):
        self.b, self.p = b, 0

    def n(self, fmt):
        z = struct.calcsize(">" + fmt)
        if self.p + z > len(self.b):
            raise ValueError("corpo troppo corto")
        v = struct.unpack(">" + fmt, self.b[self.p:self.p + z])[0]
        self.p += z
        return v

    def s(self, massimo):
        n = self.n("H")
        if n > massimo or self.p + n > len(self.b):
            raise ValueError("stringa troppo lunga")
        raw = self.b[self.p:self.p + n]
        self.p += n
        raw.decode("utf-8")
        return n   # SOLO la lunghezza

    def fine(self):
        return self.p == len(self.b)


def chiamate(b):
    c = Corpo(b)
    esito, n = c.n("B"), c.n("B")
    assert esito <= 2 and n <= 20 and (esito == 0 or n == 0), "esito/n"
    voci = []
    for _ in range(n):
        tipo, ora, dur = c.n("B"), c.n("Q"), c.n("I")
        assert tipo <= 4, "tipo"
        voci.append((tipo, ora > 0, dur, c.s(127), c.s(47)))
    assert c.fine(), "coda"
    return esito, n, voci


def rete(b):
    c = Corpo(b)
    r = dict(esito=c.n("B"), wifi=c.n("B"), wl=c.n("B"), wnome=c.s(99), mob=c.n("B"), ml=c.n("B"), gen=c.n("B"), op=c.s(63), dm=c.n("B"), aereo=c.n("B"))
    assert c.fine() and r["esito"] <= 2 and r["wifi"] <= 2 and r["wl"] <= 4 and r["mob"] <= 2 and r["ml"] <= 4
    assert r["gen"] in (0, 2, 3, 4, 5) and r["dm"] <= 2 and r["aereo"] <= 2, "campo fuori elenco"
    if r["esito"] != 0:
        assert all(v == 0 for k, v in r.items() if k != "esito"), "esito != 0 ma resto non zero"
    return r


def media(b):
    c = Corpo(b)
    r = dict(esito=c.n("B"), stato=c.n("B"), titolo=c.s(159), artista=c.s(127), pacchetto=c.s(255), comandi=c.n("B"))
    assert c.fine() and r["esito"] <= 2 and r["stato"] <= 2 and r["comandi"] <= 7, "campo fuori elenco"
    if r["esito"] != 0 or r["stato"] == 0:
        assert all(v == 0 for k, v in r.items() if k != "esito"), "resto non zero"
    return r


def prova(pid_propri):
    adb("push", CACHE + "/yasde.jar", JAR)
    adb("forward", "tcp:%d" % PORTA, "localabstract:yasde")
    p = subprocess.Popen(["adb", "shell", "CLASSPATH=%s app_process / it.yasde.Servizio" % JAR],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    pid_propri.append(p)
    log = []
    coda = queue.Queue()

    def lettore():
        for riga in p.stdout:
            coda.put(riga.strip())
        coda.put(None)

    threading.Thread(target=lettore, daemon=True).start()
    t0 = time.time()
    while True:
        try:
            r = coda.get(timeout=max(0.1, 30 - (time.time() - t0)))
        except queue.Empty:
            raise SystemExit("servizio non partito (scadenza di 30 s)")
        if r is None:
            raise SystemExit("servizio non partito")
        log.append(r)
        if r == "PRONTO":
            break
    PID_REMOTI.extend(pid_remoti())
    print("pid remoti del servizio annotati:", len(PID_REMOTI))
    def raccogli():
        while True:
            x = coda.get()
            if x is None:
                return
            log.append(x)
    threading.Thread(target=raccogli, daemon=True).start()
    s = socket.create_connection(("127.0.0.1", PORTA), timeout=30)
    s.sendall(b"comandi\n")
    t, b, ms = chiedi(s, 1, struct.pack(">H", 3))
    controlla(t == 2 and struct.unpack(">H", b[:2])[0] == 3, "SALUTO versione 3 -> OK")

    t, b, ms = chiedi(s, 0x91, b"\x14")
    try:
        es, n, voci = chiamate(b) if t == 2 else (None, None, [])
        controlla(t == 2, "CHIAMATE max 20 -> formato valido", "esito %s, n %s, %.0f ms" % (es, n, ms))
        for i, v in enumerate(voci):
            print("   voce %d: tipo %d, ora presente %s, durata %d s, lunghezza nome %d, lunghezza numero %d" % ((i,) + v))
    except Exception as e:
        controlla(False, "CHIAMATE formato", str(e))
    for max_ in (0, 21):
        t, b, ms = chiedi(s, 0x91, bytes([max_]))
        controlla(t == 3, "CHIAMATE max %d -> ERRORE" % max_)

    t, b, ms = chiedi(s, 0x92)
    try:
        r = rete(b) if t == 2 else None
        controlla(r is not None, "RETE -> formato valido", "%.0f ms" % ms)
        if r:
            print("   rete:", ", ".join("%s=%s" % kv for kv in r.items()), "(nome rete e operatore: solo lunghezza)")
    except Exception as e:
        controlla(False, "RETE formato", str(e))

    t, b, ms = chiedi(s, 0x93)
    try:
        r = media(b) if t == 2 else None
        controlla(r is not None, "MEDIA -> formato valido", "%.0f ms" % ms)
        if r:
            print("   media:", ", ".join("%s=%s" % kv for kv in r.items()), "(titolo, artista, pacchetto: solo lunghezza)")
    except Exception as e:
        controlla(False, "MEDIA formato", str(e))

    t, b, ms = chiedi(s, 0x94, b"\x09")
    controlla(t == 3, "MEDIA_COMANDO 9 (fuori elenco) -> ERRORE")
    t, b, ms = chiedi(s, 0x94, b"")
    controlla(t == 3, "MEDIA_COMANDO senza corpo -> ERRORE")
    t, b, ms = chiedi(s, 4)
    controlla(t == 2, "CHIUDI -> OK")
    # il log del servizio contiene solo nomi di classi di eccezione, mai dati
    for x in log:
        if "[dati]" in x and ("non leggibil" in x or "MANCANTE" in x):
            print("   log:", x)


def principale():
    try:
        os.mkdir(TURNO)
    except FileExistsError:
        raise SystemExit("telefono occupato: un altro ha il turno, riprovare piu' tardi")
    pid_propri = []
    try:
        if not any(r.endswith("\tdevice") for r in adb("devices").splitlines()):
            raise SystemExit("telefono non collegato: mi fermo (non lo ricollego)")
        if "mWakefulness=Awake" not in adb("shell", "dumpsys power"):
            raise SystemExit("telefono non Awake: mi fermo (non lo risveglio)")
        # sola lettura: c'e' gia' qualcosa di YASDE?
        if adb("shell", "ps -A -o ARGS | grep -cE '[i]t[.]yasde'").strip() not in ("", "0") or \
                adb("shell", "ls /data/local/tmp | grep -cE '^yasde-custode-'").strip() not in ("", "0"):
            raise SystemExit("sul telefono c'e' gia' un it.yasde o un yasde-custode: esco senza toccare nulla")
        prova(pid_propri)
    finally:
        for p in pid_propri:
            try:
                p.stdin.close()
            except Exception:
                pass
            try:
                p.wait(timeout=10)
            except Exception:
                p.kill()
        # uccide SOLO i pid annotati all'avvio (il servizio e la sua shell), mai altri it.yasde
        for pid in PID_REMOTI:
            adb("shell", "kill %s" % pid, timeout=10)
        adb("forward", "--remove", "tcp:%d" % PORTA)
        # solo il proprio jar (il servizio lo cancella da solo alla chiusura)
        adb("shell", "rm -f /data/local/tmp/%s" % NOME)
        os.rmdir(TURNO)
    print("FALLITE: %d" % falliti)
    sys.exit(1 if falliti else 0)


principale()
