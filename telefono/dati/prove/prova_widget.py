#!/usr/bin/env python3
"""Prova di MEMORIA_DETTAGLIO (0x76) e CALENDARIO (0x77) sul telefono vero, col jar completo.
Riporta SOLO conteggi e tempi: i titoli degli eventi sono dati personali e non si stampano ne' si salvano.
Preparazione: jar da `strumenti/prova.sh telefono` in ~/.cache/yasde-sviluppo/dati/yasde.jar; turno del telefono preso.
"""
import os, socket, struct, subprocess, sys, threading, time

CACHE = os.path.expanduser("~/.cache/yasde-sviluppo/dati")
JAR = "/data/local/tmp/yasde.jar"
PORTA = 38465
falliti = 0


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


def main():
    adb("push", CACHE + "/yasde.jar", JAR)
    adb("forward", "tcp:%d" % PORTA, "localabstract:yasde")
    p = subprocess.Popen(["adb", "shell", "CLASSPATH=%s app_process / it.yasde.Servizio" % JAR],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log = []
    while True:
        r = p.stdout.readline()
        if not r:
            raise SystemExit("servizio terminato prima di PRONTO")
        log.append(r.strip())
        if r.strip() == "PRONTO":
            break
    threading.Thread(target=lambda: [log.append(x.strip()) for x in p.stdout], daemon=True).start()
    try:
        s = socket.create_connection(("127.0.0.1", PORTA), timeout=15)
        s.sendall(b"comandi\n")
        chiedi(s, 1, struct.pack(">H", 3))
        print("API mancanti nel log:", [x for x in log if "MANCANTE" in x])
        t, b, ms = chiedi(s, 0x76)
        controlla(t == 2 and len(b) == 32, "MEMORIA_DETTAGLIO -> OK 32 byte", "%.0f ms" % ms)
        if t == 2:
            v = struct.unpack(">4Q", b)
            controlla(all(x != 0xFFFFFFFFFFFFFFFF for x in v) and v[3] > 0, "quattro valori leggibili", "immagini %d MB, video %d MB, audio %d MB, app %d MB" % tuple(x // 10**6 for x in v))
        for massimo in (1, 4, 16):
            t, b, ms = chiedi(s, 0x77, bytes([massimo]))
            n = struct.unpack(">H", b[:2])[0] if t == 2 else -1
            ok = t == 2 and n <= massimo
            if ok:
                o = 2
                for _ in range(n):
                    ln = struct.unpack(">H", b[o:o + 2])[0]
                    o += 2 + ln + 8 + 8 + 1
                ok = o == len(b)
            controlla(ok, "CALENDARIO massimo %d -> OK, formato completo" % massimo, "%d eventi, %.0f ms" % (n, ms) if t == 2 else b[2:].decode("utf-8", "replace"))
        for massimo in (0, 17, 255):
            t, b, ms = chiedi(s, 0x77, bytes([massimo]))
            controlla(t == 3, "CALENDARIO massimo %d -> ERRORE" % massimo, b[2:].decode("utf-8", "replace"))
        t, b, ms = chiedi(s, 0x77)
        controlla(t == 3, "CALENDARIO senza corpo -> ERRORE")
        t, b, ms = chiedi(s, 0x74, b"")
        controlla(t == 3 or t == 2, "0x74 (archivio) ancora gestito da un altro pezzo, non da dati")
        t, b, ms = chiedi(s, 0x72)
        controlla(t == 2 and len(b) == 5, "STATO ancora valido")
        t, b, ms = chiedi(s, 4)
        controlla(t == 2, "CHIUDI -> OK")
        print("righe di log che contengono 'calend':", sum(1 for x in log if "calend" in x.lower()))
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
    print("FALLITE: %d" % falliti)
    sys.exit(1 if falliti else 0)


main()
