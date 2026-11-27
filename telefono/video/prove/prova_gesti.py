#!/usr/bin/env python3
"""Gesti incrociati tra due schermi virtuali (dito del TOCCO e PIZZICO), pausa, cambio di misura e fine del
canale input con un dito giu': si guarda in `dumpsys input` quanti puntatori toccano ogni schermo.
Uso: prova_gesti.py <pacchetto-app>   (un'app qualsiasi con attivita' di avvio, senza dati personali)
Turno e telefono Awake come da CLAUDE.md. Nessun dato personale nei file."""
import os, re, signal, struct, subprocess, sys, time

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prova_video as P

TURNO = os.path.expanduser("~/.cache/yasde-sviluppo/telefono.occupato")
P.falliti = 0


def _termina(*a):
    raise SystemExit("terminato")


signal.signal(signal.SIGTERM, _termina)


def schermi():
    return set(P.sh("dumpsys window displays | grep -E 'Display: mDisplayId' | sed 's/.*mDisplayId=//;s/ .*//'").split())


def toccanti(did):
    """Quanti puntatori distinti toccano lo schermo `did` secondo `dumpsys input`."""
    out = P.sh("dumpsys input")
    i = out.find("TouchStatesByDisplay")
    if i < 0:
        return -1
    blocco = out[i:]
    m = re.search(r"LogicalDisplayId %s:\n(.*?)(?=\n    LogicalDisplayId|\n  [A-Za-z]|\Z)" % did, blocco, re.S)
    if not m:
        return 0
    return len(set(re.findall(r"Pointer\(id=(\d+)", m.group(1))))


def attendi(did, voluto, sec=2.0):
    fine = time.time() + sec
    while time.time() < fine:
        if toccanti(did) == voluto:
            return True
        time.sleep(0.15)
    return toccanti(did) == voluto


def prova(app):
    P.adb("push", P.CACHE + "/yasde.jar", P.JAR)
    p, _ = P.lancia()
    P.adb("forward", "tcp:%d" % P.PORTA, "localabstract:yasde")
    c = P.Comandi()
    c.chiedi(1, struct.pack(">H", 3))
    did = {}
    pre = schermi()
    for k in (1, 2):
        t, b = P.apri_video(c, k, 720, 1280, 450, app)
        time.sleep(2.5)
        nuovi = schermi() - pre - set(did.values())
        if t == 2 and len(nuovi) == 1:
            did[k] = nuovi.pop()
    P.controlla(len(did) == 2, "due sessioni su due schermi virtuali")
    if len(did) != 2:
        return
    A, B = did[1], did[2]
    ci = P.apri(b"input\n")

    def dito(k, az, x=360, y=640):
        P.tocco(ci, k, 720, 1280, az, x, y)

    def pz(k, fase, passo=0, x=360, y=640):
        P.input_msg(ci, 0x55, struct.pack(">IHHBHHh", k, 720, 1280, fase, x, y, passo))

    def tutti_su():
        for k in (1, 2):
            dito(k, 2)
            pz(k, 3)
        time.sleep(0.5)

    # 1. dito su A, pizzico su B: il dito di A si annulla sul suo schermo
    dito(1, 0)
    P.controlla(attendi(A, 1), "dito giu' su A: 1 puntatore su A")
    pz(2, 0)
    P.controlla(attendi(A, 0) and attendi(B, 2), "pizzico su B con dito giu' su A: A senza puntatori, B con 2", "A=%d B=%d" % (toccanti(A), toccanti(B)))
    pz(2, 2)
    P.controlla(attendi(B, 0), "fine del pizzico su B: nessun puntatore")
    # 2. dito su A, dito su B: A annullato
    dito(1, 0)
    attendi(A, 1)
    dito(2, 0)
    P.controlla(attendi(A, 0) and attendi(B, 1), "dito giu' su B con dito giu' su A: A annullato, B con 1", "A=%d B=%d" % (toccanti(A), toccanti(B)))
    dito(1, 2)  # il su di A va scartato (non e' piu' suo il dito)
    time.sleep(0.4)
    P.controlla(toccanti(B) == 1, "il su di A non tocca il dito di B")
    dito(2, 2)
    P.controlla(attendi(B, 0), "su di B: nessun puntatore")
    # 3. pizzico su A, dito su B: il pizzico di A si annulla
    pz(1, 0)
    attendi(A, 2)
    dito(2, 0)
    P.controlla(attendi(A, 0) and attendi(B, 1), "dito giu' su B con pizzico aperto su A: pizzico annullato, B con 1", "A=%d B=%d" % (toccanti(A), toccanti(B)))
    tutti_su()
    # 4. pausa di A con un dito giu': la pausa lo annulla PRIMA di staccare la superficie
    dito(1, 0)
    attendi(A, 1)
    t, b = c.chiedi(0x44, struct.pack(">I", 1))
    P.controlla(t == 2 and toccanti(A) == 0, "pausa di A con dito giu': nessun puntatore dopo la pausa", "A=%d" % toccanti(A))
    c.chiedi(0x45, struct.pack(">I", 1))
    time.sleep(0.5)
    # 5. pausa di A con pizzico aperto
    pz(1, 0)
    attendi(A, 2)
    t, b = c.chiedi(0x44, struct.pack(">I", 1))
    P.controlla(t == 2 and toccanti(A) == 0, "pausa di A con pizzico aperto: nessun puntatore", "A=%d" % toccanti(A))
    c.chiedi(0x45, struct.pack(">I", 1))
    time.sleep(0.5)
    # 6. cambio di misura con dito giu'
    dito(1, 0)
    attendi(A, 1)
    t, b = c.chiedi(0x42, struct.pack(">IHHH", 1, 640, 1136, 450))
    P.controlla(t == 2 and attendi(A, 0), "ridimensionamento con dito giu': nessun puntatore", "A=%d" % toccanti(A))
    # 7. pizzico senza fine: si chiude da solo
    pz(2, 0)
    attendi(B, 2)
    P.controlla(attendi(B, 0, 2.0), "pizzico senza fine: chiuso da solo", "B=%d" % toccanti(B))
    # 8. fine del canale input con un dito giu' e un pizzico aperto su schermi diversi
    dito(1, 0, 320, 568)
    attendi(A, 1)
    ci.close()
    P.controlla(attendi(A, 0, 3.0), "fine del canale input con un dito giu': nessun puntatore", "A=%d" % toccanti(A))
    c.chiedi(0x41, struct.pack(">IB", 1, 1))
    c.chiedi(0x41, struct.pack(">IB", 2, 1))
    c.chiedi(4)
    time.sleep(2)


if __name__ == "__main__":
    mio = False
    try:
        if not re.fullmatch(r"[A-Za-z0-9_.]+", sys.argv[1] if len(sys.argv) > 1 else ""):
            sys.exit("uso: nome del pacchetto con soli caratteri [A-Za-z0-9_.]")
        mio = P.avvia_turno()
        prova(sys.argv[1])
    finally:
        if mio:
            P.pulisci()
            for g in os.listdir(P.CACHE):
                if g.endswith((".h264", ".raw", ".png")):
                    os.remove(os.path.join(P.CACHE, g))
            P.rilascia_turno()
    print("falliti:", P.falliti)
    sys.exit(1 if P.falliti else 0)
