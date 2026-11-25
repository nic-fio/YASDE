#!/usr/bin/env python3
"""Prova di CALENDARIO_INTERVALLO (0x7b), SVEGLIA (0x7c) e BATTERIA (0x7d) sul telefono vero.
Riporta SOLO conteggi, tempi e valori numerici di batteria: titoli e pacchetti dell'utente non si stampano.
Preparazione: jar da `strumenti/prova.sh telefono` in ~/.cache/yasde-sviluppo/dati/yasde.jar.
Il turno del telefono lo prende e lo toglie questo script (solo se la mkdir e' riuscita qui);
se il telefono non e' collegato o non e' Awake, esce senza ricollegarlo ne' svegliarlo.
"""
import os, socket, struct, subprocess, sys, threading, time

CACHE = os.path.expanduser("~/.cache/yasde-sviluppo/dati")
TURNO = os.path.expanduser("~/.cache/yasde-sviluppo/telefono.occupato")
JAR = "/data/local/tmp/yasde.jar"
PORTA = 38469
falliti = 0
GIORNO = 86400000


def adb(*a):
    return subprocess.run(["adb"] + list(a), capture_output=True, text=True).stdout


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
            return None
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


def stringa(b, o):
    n = struct.unpack(">H", b[o:o + 2])[0]
    return b[o + 2:o + 2 + n].decode("utf-8", "replace"), o + 2 + n


def prova():
    adb("push", CACHE + "/yasde.jar", JAR)
    adb("forward", "tcp:%d" % PORTA, "localabstract:yasde")
    p = subprocess.Popen(["adb", "shell", "CLASSPATH=%s app_process / it.yasde.Servizio" % JAR],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log = []
    try:
        while True:
            r = p.stdout.readline()
            if not r:
                raise SystemExit("servizio terminato prima di PRONTO")
            log.append(r.strip())
            if r.strip() == "PRONTO":
                break
        threading.Thread(target=lambda: [log.append(x.strip()) for x in p.stdout], daemon=True).start()
        s = socket.create_connection(("127.0.0.1", PORTA), timeout=15)
        s.sendall(b"comandi\n")
        chiedi(s, 1, struct.pack(">H", 1))
        print("API mancanti nel log:", [x for x in log if "MANCANTE" in x])
        ora = int(time.time() * 1000)

        def intervallo(da, a, mx):
            return chiedi(s, 0x7b, struct.pack(">QQH", da, a, mx))

        for giorni in (31, 62):
            t, b, ms = intervallo(ora, ora + giorni * GIORNO, 500)
            ok = t == 2
            n = -1
            if ok:
                tr = b[0]
                pk, o = stringa(b, 1)
                n = struct.unpack(">H", b[o:o + 2])[0]
                o += 2
                for _ in range(n):
                    ln = struct.unpack(">H", b[o:o + 2])[0]
                    o += 2 + ln + 8 + 8 + 1
                ok = o == len(b)
                extra = "%d eventi, troncato=%d, app calendario %s, %.0f ms" % (n, tr, "presente" if pk else "assente", ms)
            else:
                extra = b[2:].decode("utf-8", "replace")
            controlla(ok, "CALENDARIO_INTERVALLO %d giorni -> OK, formato completo" % giorni, extra)
        t, b, ms = intervallo(ora, ora + 31 * GIORNO, 1)
        controlla(t == 2 and b[0] in (0, 1), "massimo 1: risponde", "%.0f ms" % ms)
        for nome, (da, a, mx) in {"intervallo vuoto": (ora, ora, 10), "rovesciato": (ora + 1, ora, 10), "oltre 62 giorni": (ora, ora + 62 * GIORNO + 1, 10),
                                  "massimo 0": (ora, ora + GIORNO, 0), "massimo 501": (ora, ora + GIORNO, 501)}.items():
            t, b, ms = intervallo(da, a, mx)
            controlla(t == 3, "CALENDARIO_INTERVALLO %s -> ERRORE" % nome, b[2:].decode("utf-8", "replace"))
        t, b, ms = chiedi(s, 0x7b, b"\x00")
        controlla(t == 3, "CALENDARIO_INTERVALLO corpo troncato -> ERRORE")

        t, b, ms = chiedi(s, 0x7c)
        if t == 2:
            ce = b[0]
            tm = struct.unpack(">Q", b[1:9])[0]
            pk, o = stringa(b, 9)
            controlla(o == len(b) and ce in (0, 1) and (ce == 0) == (tm == 0), "SVEGLIA -> OK", "c'e'=%d, nel futuro=%s, pacchetto %s, %.0f ms" % (ce, tm > ora if ce else "-", "noto" if pk else "vuoto", ms))
        else:
            controlla(False, "SVEGLIA -> OK", b[2:].decode("utf-8", "replace"))
        dump = adb("shell", "dumpsys alarm | grep -ci 'next alarm clock'").strip()
        print("   dumpsys alarm: righe «next alarm clock» =", dump)

        t, b, ms = chiedi(s, 0x7d)
        if t == 2 and len(b) == 16:
            liv, stato, alim, temp, salute, tens, minc, mina = struct.unpack(">BBBhBHii", b)
            d = adb("shell", "dumpsys battery")
            dl = int([r for r in d.splitlines() if r.strip().startswith("level:")][0].split(":")[1])
            controlla(abs(liv - dl) <= 1, "BATTERIA livello uguale a dumpsys battery", "%d%% contro %d%%" % (liv, dl))
            controlla(mina == -1, "autonomia stimata -1 (nessuna API pubblica)")
            print("   stato %d, alimentazione %d, temperatura %s, salute %d, tensione %s mV, minuti alla carica %d, %.0f ms" % (
                stato, alim, "non nota" if temp == 0x7fff else "%.1f C" % (temp / 10), salute, tens or "non nota", minc, ms))
            print("   percorso usato: %s" % ("dump di ripiego" if any("batteria: ripiego sul dump" in x for x in log) else "intento persistente (o ripiego silenzioso)"))
        else:
            controlla(False, "BATTERIA -> OK 16 byte", b[2:].decode("utf-8", "replace") if t == 3 else "lunghezza %d" % len(b))
        t, b, ms = chiedi(s, 4)
        controlla(t == 2, "CHIUDI -> OK")
        print("righe di log con 'calend':", sum(1 for x in log if "calend" in x.lower()))
    finally:
        try:
            p.stdin.close()
        except Exception:
            pass
        time.sleep(2)
        if p.poll() is None:
            p.kill()
        adb("forward", "--remove", "tcp:%d" % PORTA)
    resti = adb("shell", "ls /data/local/tmp | grep -E '^yasde(\\.jar|-custode)'").split()
    controlla(not resti, "pulizia: nessun file di YASDE rimasto", str(resti))


def principale():
    try:
        os.mkdir(TURNO)
    except FileExistsError:
        raise SystemExit("telefono occupato: un altro ha il turno, riprovare piu' tardi")
    try:
        if not any(r.endswith("\tdevice") for r in adb("devices").splitlines()):
            raise SystemExit("telefono non collegato: mi fermo (non lo ricollego)")
        if "mWakefulness=Awake" not in adb("shell", "dumpsys power"):
            raise SystemExit("telefono non Awake: mi fermo (non lo risveglio)")
        prova()
    finally:
        os.rmdir(TURNO)
    print("FALLITE: %d" % falliti)
    sys.exit(1 if falliti else 0)


principale()
