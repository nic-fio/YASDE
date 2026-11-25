#!/usr/bin/env python3
"""Tasto destro e centrale come veri pulsanti del mouse (TOCCO con `pulsanti` 2 e 4): si guarda in `dumpsys input`
che durante il gesto ci sia un puntatore sullo schermo e che dopo su, annulla (altra sessione) e fine del canale
non ne resti nessuno. DA VERIFICARE A OCCHIO (non automatico): che nel video non compaia il puntatore di Android.
LIMITE: dumpsys input espone solo il tipo di strumento dei puntatori che toccano (FINGER/MOUSE), non action,
source, actionButton ne' buttonState degli eventi (RecentQueue dice solo "MotionEvent"). Quindi la prova NON vede
DOWN con buttonState, BUTTON_PRESS/RELEASE ne' la differenza tra UP e CANCEL: quella verifica e' a schermo (menu
sul link e link in nuova scheda in Chrome; centrale annullato che non apre il link).
Uso: prova_mouse.py <pacchetto-app>   (un'app qualsiasi con attivita' di avvio, senza dati personali)
Turno e telefono Awake come da CLAUDE.md. Nessun dato personale nei file."""
import os, re, signal, struct, sys, time

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prova_video as P
import prova_gesti as G

P.CACHE = os.path.expanduser("~/.cache/yasde-sviluppo/telefono-video")
P.falliti = 0
JARV = "/data/local/tmp/prova-video-%d.jar" % os.getpid()
JARB = "/data/local/tmp/prova-base-%d.jar" % os.getpid()
P.JAR = JARV + ":" + JARB


def _termina(*a):
    raise SystemExit("terminato")


signal.signal(signal.SIGTERM, _termina)


def tocco(ci, k, az, pulsanti, x=360, y=640):
    P.input_msg(ci, 0x50, struct.pack(">IHHBHHB", k, 720, 1280, az, x, y, pulsanti))


def tipo(did):
    """Tipi di strumento (FINGER/MOUSE) dei puntatori che toccano lo schermo `did`, da `touchingPointers` di dumpsys input."""
    out = P.sh("dumpsys input")
    i = out.find("TouchStatesByDisplay")
    m = re.search(r"LogicalDisplayId %s:\n(.*?)(?=\n    LogicalDisplayId|\n  [A-Za-z]|\Z)" % did, out[i:], re.S)
    if not m:
        return set()
    return set(re.findall(r"touchingPointers=\[Pointer\(id=\d+, (\w+)\)", m.group(1)))


def attendi_tipo(did, voluto, sec=2.0):
    fine = time.time() + sec
    while time.time() < fine:
        if tipo(did) == voluto:
            return True
        time.sleep(0.15)
    return tipo(did) == voluto


def prova(app):
    # Il jar del solo pezzo video non ha Servizio (sta in telefono-base): CLASSPATH con i due jar, nomi propri della prova.
    P.adb("push", P.CACHE + "/yasde.jar", JARV)
    P.adb("push", P.CACHE + "/base.jar", JARB)
    p, _ = P.lancia()
    P.adb("forward", "tcp:%d" % P.PORTA, "localabstract:yasde")
    c = P.Comandi()
    c.chiedi(1, struct.pack(">H", 1))
    did = {}
    pre = G.schermi()
    for k in (1, 2):
        t, b = P.apri_video(c, k, 720, 1280, 450, app)
        time.sleep(2.5)
        nuovi = G.schermi() - pre - set(did.values())
        if t == 2 and len(nuovi) == 1:
            did[k] = nuovi.pop()
    P.controlla(len(did) == 2, "due sessioni su due schermi virtuali")
    if len(did) != 2:
        return
    A, B = did[1], did[2]
    ci = P.apri(b"input\n")
    # destro e centrale: giu' (pulsanti = bit), muovi, su (pulsanti 0)
    for nome, bit in (("destro", 2), ("centrale", 4), ("destro+centrale", 6)):
        tocco(ci, 1, 0, bit)
        P.controlla(G.attendi(A, 1), "%s giu': 1 puntatore su A" % nome)
        P.controlla(attendi_tipo(A, {"MOUSE"}), "%s giu': strumento MOUSE" % nome, str(tipo(A)))
        tocco(ci, 1, 1, bit, 380, 660)
        P.controlla(G.attendi(A, 1) and attendi_tipo(A, {"MOUSE"}), "%s muovi: ancora MOUSE" % nome, str(tipo(A)))
        tocco(ci, 1, 2, 0, 380, 660)
        P.controlla(G.attendi(A, 0), "%s su (pulsanti 0): nessun puntatore" % nome, "A=%d" % G.toccanti(A))
    # sinistro invariato
    tocco(ci, 1, 0, 1)
    P.controlla(G.attendi(A, 1), "sinistro giu': 1 puntatore")
    P.controlla(attendi_tipo(A, {"FINGER"}), "sinistro giu': strumento FINGER", str(tipo(A)))
    tocco(ci, 1, 2, 0)
    P.controlla(G.attendi(A, 0), "sinistro su: nessun puntatore")
    # bit sconosciuti: si ignorano (dito)
    tocco(ci, 1, 0, 0x78)
    P.controlla(G.attendi(A, 1) and attendi_tipo(A, {"FINGER"}), "bit sconosciuti: gesto come un dito (FINGER)", str(tipo(A)))
    tocco(ci, 1, 2, 0)
    G.attendi(A, 0)
    # destro giu' su A, giu' su B: A annullato, nessun pulsante rimasto
    tocco(ci, 1, 0, 2)
    P.controlla(G.attendi(A, 1) and attendi_tipo(A, {"MOUSE"}), "destro su A: MOUSE", str(tipo(A)))
    tocco(ci, 2, 0, 1)
    P.controlla(G.attendi(A, 0) and G.attendi(B, 1), "destro su A poi dito su B: A annullato", "A=%d B=%d" % (G.toccanti(A), G.toccanti(B)))
    tocco(ci, 2, 2, 0)
    G.attendi(B, 0)
    # fine del canale con un destro giu'
    tocco(ci, 1, 0, 2)
    G.attendi(A, 1)
    ci.close()
    P.controlla(G.attendi(A, 0, 3.0), "fine del canale con il destro giu': nessun puntatore", "A=%d" % G.toccanti(A))
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
            P.JAR = JARV  # pulisci() toglie solo questo; l'altro jar subito dopo
            P.pulisci()
            P.sh("rm -f %s %s" % (JARV, JARB))
            for g in os.listdir(P.CACHE):
                if g.endswith((".h264", ".raw", ".png")):
                    os.remove(os.path.join(P.CACHE, g))
            P.rilascia_turno()
    print("falliti:", P.falliti)
    sys.exit(1 if P.falliti else 0)
