#!/usr/bin/env python3
"""Conta le app dell'elenco (APP_ELENCO) con il segno «solo verticale»: solo numeri, nessun nome di app.
Il turno del telefono lo prende lo script stesso (mkdir ...telefono.occupato): se e' gia' preso esce subito, e alla fine toglie solo il proprio.
Jar da ~/.cache/yasde-sviluppo/dati/yasde.jar."""
import os, socket, struct, subprocess, sys, threading, time

CACHE = os.path.expanduser("~/.cache/yasde-sviluppo/dati")
JAR = "/data/local/tmp/yasde.jar"
PORTA = 38467


def adb(*a):
    return subprocess.run(["adb"] + list(a), capture_output=True, text=True).stdout


def recv_n(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            raise SystemExit("canale chiuso")
        b += c
    return b


def chiedi(s, tipo, corpo=b""):
    s.sendall(struct.pack(">BI", tipo, len(corpo)) + corpo)
    while True:
        t, n = struct.unpack(">BI", recv_n(s, 5))
        b = recv_n(s, n)
        if t not in (0x7e, 0x7f):
            return t, b


def principale():
    # Il turno del telefono lo prende questo script; lo toglie solo se la mkdir e' riuscita qui.
    turno = os.path.expanduser("~/.cache/yasde-sviluppo/telefono.occupato")
    try:
        os.mkdir(turno)
    except FileExistsError:
        raise SystemExit("telefono occupato: un altro ha il turno, riprovare piu' tardi")
    try:
        if not any(r.endswith("\tdevice") for r in adb("devices").splitlines()):
            raise SystemExit("telefono non collegato: mi fermo (non lo ricollego)")
        if "mWakefulness=Awake" not in adb("shell", "dumpsys power"):
            raise SystemExit("telefono non Awake: mi fermo (non lo risveglio)")
        conta()
    finally:
        os.rmdir(turno)


def conta():
    adb("push", CACHE + "/yasde.jar", JAR)
    adb("forward", "tcp:%d" % PORTA, "localabstract:yasde")
    p = subprocess.Popen(["adb", "shell", "CLASSPATH=%s app_process / it.yasde.Servizio" % JAR],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        while True:
            r = p.stdout.readline()
            if not r:
                raise SystemExit("servizio terminato prima di PRONTO")
            if r.strip() == "PRONTO":
                break
        threading.Thread(target=lambda: [None for _ in p.stdout], daemon=True).start()
        s = socket.create_connection(("127.0.0.1", PORTA), timeout=15)
        s.sendall(b"comandi\n")
        chiedi(s, 1, struct.pack(">H", 2))
        t, b = chiedi(s, 0x70)
        if t != 2:
            raise SystemExit("APP_ELENCO non OK")
        n = struct.unpack(">H", b[:2])[0]
        o = 2
        vert = sistema = 0
        for _ in range(n):
            for _ in range(2):
                ln = struct.unpack(">H", b[o:o + 2])[0]
                o += 2 + ln
            vert += (b[o] >> 1) & 1
            sistema += b[o] & 1
            o += 1
        print("app %d, di sistema %d, solo verticali %d" % (n, sistema, vert))
        chiedi(s, 4)
    finally:
        try:
            p.stdin.close()
        except Exception:
            pass
        import time
        time.sleep(2)
        if p.poll() is None:
            p.kill()
        adb("forward", "--remove", "tcp:%d" % PORTA)


principale()
