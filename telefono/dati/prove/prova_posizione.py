#!/usr/bin/env python3
"""Prova di POSIZIONE (0x90) sul telefono vero. NON stampa ne' salva coordinate ne' nomi di localita':
solo «posizione presente si/no», eta' in minuti, esito e tempi.
Preparazione: jar da `strumenti/prova.sh telefono` in ~/.cache/yasde-sviluppo/dati/yasde.jar.
Regole di CLAUDE.md («Prove sul telefono»): turno preso e tolto solo da questo script; controllo iniziale in
sola lettura (se c'e' gia' un it.yasde o un yasde-custode-*, esce); jar con nome proprio; si
uccide solo il proprio pid; ogni comando adb ha una scadenza; pulizia in finally.
"""
import os, socket, struct, subprocess, sys, threading, time, secrets

CACHE = os.path.expanduser("~/.cache/yasde-sviluppo/dati")
TURNO = os.path.expanduser("~/.cache/yasde-sviluppo/telefono.occupato")
NOME = "yasde-posizione-%s.jar" % secrets.token_hex(4)
JAR = "/data/local/tmp/" + NOME
PORTA = 38471
falliti = 0


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


def leggi(b):
    """(esito, eta' in minuti o None, fornitore, localita' presente, ricerca avviata, formato intero) senza esporre le coordinate."""
    esito = b[0]
    lat, lon, eta, forn = struct.unpack(">iiIB", b[1:14])
    n = struct.unpack(">H", b[14:16])[0]
    intero = len(b) == 17 + n and b[16 + n] in (0, 1) and n <= 127
    if esito == 0:
        assert abs(lat) <= 90000 and abs(lon) <= 180000, "coordinate fuori limite"
        assert lat % 5 == 0 and lon % 5 == 0, "coordinate non multiple di 5 millesimi (arrotondamento a 0,005)"
    return esito, (None if eta == 0xffffffff else eta // 60), forn, n > 0, b[16 + n] if intero else None, intero


def prova(pid_propri):
    adb("push", CACHE + "/yasde.jar", JAR)
    adb("forward", "tcp:%d" % PORTA, "localabstract:yasde")
    p = subprocess.Popen(["adb", "shell", "CLASSPATH=%s app_process / it.yasde.Servizio" % JAR],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    pid_propri.append(p)
    log = []
    t0 = time.time()
    while True:
        r = p.stdout.readline()
        if not r or time.time() - t0 > 30:
            raise SystemExit("servizio non partito")
        log.append(r.strip())
        if r.strip() == "PRONTO":
            break
    threading.Thread(target=lambda: [log.append(x.strip()) for x in p.stdout], daemon=True).start()
    s = socket.create_connection(("127.0.0.1", PORTA), timeout=30)
    s.sendall(b"comandi\n")
    chiedi(s, 1, struct.pack(">H", 2))
    print("API mancanti nel log:", [x for x in log if "MANCANTE" in x])
    print("posizione del telefono accesa:", adb("shell", "cmd location is-location-enabled").strip())

    def descrivi(r, ms):
        esito, eta, forn, loc, avv, intero = r
        return "esito %d, posizione presente %s, eta' %s min, fornitore %d, localita' presente %s, ricerca avviata %s, %.1f ms" % (
            esito, "si" if esito == 0 else "no", eta, forn, "si" if loc else "no", avv, ms)

    t, b, ms = chiedi(s, 0x90, b"\x00")
    r0 = leggi(b) if t == 2 and len(b) >= 17 else None
    controlla(r0 is not None and r0[5] and r0[4] == 0, "POSIZIONE senza richiesta -> OK subito, ricerca avviata 0", descrivi(r0, ms) if r0 else "")
    controlla(ms < 500, "POSIZIONE senza richiesta: risposta immediata", "%.1f ms" % ms)
    t, b, ms = chiedi(s, 0x90, b"\x01")
    r1 = leggi(b) if t == 2 and len(b) >= 17 else None
    controlla(r1 is not None and r1[5], "POSIZIONE con richiesta permessa -> OK subito", descrivi(r1, ms) if r1 else "")
    controlla(ms < 500, "POSIZIONE con richiesta permessa: risposta immediata anche se parte la ricerca", "%.1f ms" % ms)
    print("   attendo 15 s per la seconda lettura (ricerca/nome in sottofondo)")
    time.sleep(15)
    t, b, ms = chiedi(s, 0x90, b"\x00")
    r2 = leggi(b) if t == 2 and len(b) >= 17 else None
    controlla(r2 is not None and r2[5], "POSIZIONE dopo 15 s -> OK", descrivi(r2, ms) if r2 else "")
    controlla(ms < 500, "POSIZIONE dopo 15 s: risposta immediata", "%.1f ms" % ms)
    if r0 and r2 and r0[0] == 0 and r2[0] == 0:
        print("   nome della localita': prima %s, dopo %s" % ("presente" if r0[3] else "assente", "presente" if r2[3] else "assente"))
    t, b, ms = chiedi(s, 0x90, b"\x02")
    controlla(t == 3, "POSIZIONE richiesta=2 -> ERRORE", b[2:].decode("utf-8", "replace"))
    t, b, ms = chiedi(s, 0x90, b"")
    controlla(t == 3, "POSIZIONE senza corpo -> ERRORE")
    t, b, ms = chiedi(s, 4)
    controlla(t == 2, "CHIUDI -> OK")
    print("righe di log: %d (nessuna deve contenere coordinate: controllo solo il numero)" % len(log))


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
        adb("forward", "--remove", "tcp:%d" % PORTA)
        # solo il proprio jar (il servizio lo cancella da solo alla chiusura)
        adb("shell", "rm -f /data/local/tmp/%s" % NOME)
        os.rmdir(TURNO)
    print("FALLITE: %d" % falliti)
    sys.exit(1 if falliti else 0)


principale()
