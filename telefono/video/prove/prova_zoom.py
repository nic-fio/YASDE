#!/usr/bin/env python3
"""Indagine sullo zoom (doppio tocco e rotella) in un'app che mostra una mappa, su uno schermo VIRTUALE.
Uso: prova_zoom.py <pacchetto-app-mappa> [varianti...]
Apre una sessione, avvia sulla sua schermata l'intento geo (mondo intero, nessuna posizione dell'utente) e
misura quanto cambia l'immagine dopo ogni azione. Le immagini restano in ~/.cache/yasde-sviluppo/video/
e si cancellano a fine prova. Turno e telefono Awake come da CLAUDE.md. Nessun dato personale nei file."""
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


def misura(fl, f, azione, attesa=1.5):
    """Esegue l'azione e ritorna di quanto cambia l'immagine (media dei livelli di grigio, 0..255)."""
    time.sleep(1.0)
    prima = fl.fotogrammi()
    azione()
    time.sleep(attesa)
    dopo = fl.fotogrammi()
    fl.f.flush()
    fr = P.immagini(f, os.path.join(P.CACHE, "tmp.raw"))
    if prima < 1 or dopo > len(fr):
        return -1.0
    return P.diff(fr[prima - 1], fr[min(dopo, len(fr)) - 1])


def prova(app):
    P.adb("push", P.CACHE + "/yasde.jar", P.JAR)
    p, _ = P.lancia()
    P.adb("forward", "tcp:%d" % P.PORTA, "localabstract:yasde")
    c = P.Comandi()
    c.chiedi(1, struct.pack(">H", 2))
    pre = schermi()
    t, b = P.apri_video(c, 1, 720, 1280, 450, app)
    P.controlla(t == 2, "sessione aperta con l'app")
    time.sleep(2)
    nuovi = schermi() - pre
    if len(nuovi) != 1:
        print("schermo virtuale non individuato:", nuovi)
        return
    did = nuovi.pop()
    # riporta l'app sulla vista del mondo (nessuna posizione dell'utente)
    out = P.sh("am start-activity --display %s -a android.intent.action.VIEW -d 'geo:0,0?z=2' -p %s -f 0x18000000" % (did, app))
    P.controlla("Error" not in out, "intento geo avviato sullo schermo virtuale")
    f = os.path.join(P.CACHE, "zoom.h264")
    fl = P.FlussoVideo(1, f)
    ci = P.apri(b"input\n")
    time.sleep(7)

    def doppio(giu_su_ms=40, tra_ms=120):
        for _ in range(2):
            P.tocco(ci, 1, 720, 1280, 0, 360, 640)
            time.sleep(giu_su_ms / 1000.0)
            P.tocco(ci, 1, 720, 1280, 2, 360, 640)
            time.sleep(tra_ms / 1000.0)

    d1 = misura(fl, f, lambda: doppio(), 2.5)
    print("   (info) doppio clic: cambio immagine %.2f" % d1)
    d2 = misura(fl, f, lambda: [P.rotella(ci, 1, 720, 1280, 360, 640, 0, 120) or time.sleep(0.15) for _ in range(5)], 2.5)
    print("   (info) rotella verticale +5 scatti: cambio immagine %.2f" % d2)
    d3 = misura(fl, f, lambda: [P.rotella(ci, 1, 720, 1280, 360, 640, 0, -120) or time.sleep(0.15) for _ in range(5)], 2.5)
    print("   (info) rotella verticale -5 scatti: cambio immagine %.2f" % d3)
    # riferimento: un trascinamento a un dito (deve spostare la mappa)
    def trascina():
        P.tocco(ci, 1, 720, 1280, 0, 360, 900)
        for i in range(1, 11):
            time.sleep(0.016)
            P.tocco(ci, 1, 720, 1280, 1, 360, 900 - i * 30)
        P.tocco(ci, 1, 720, 1280, 2, 360, 600)
    d4 = misura(fl, f, trascina, 2.0)
    print("   (info) trascinamento a un dito (riferimento): cambio immagine %.2f" % d4)
    def pizzico(fase, passo, x=360, y=640):
        P.input_msg(ci, 0x55, struct.pack(">IHHBHHh", 1, 720, 1280, fase, x, y, passo))

    def gesto(passo, n=10):
        pizzico(0, 0)
        for _ in range(n):
            time.sleep(0.03)
            pizzico(1, passo)
        time.sleep(0.03)
        pizzico(2, 0)

    d5 = misura(fl, f, lambda: gesto(120), 2.5)
    print("   (info) pizzico avanti (10 passi da +120): cambio immagine %.2f" % d5)
    P.controlla(d5 > 3.0, "il pizzico avanti cambia l'immagine (zoom)", "%.2f" % d5)
    d6 = misura(fl, f, lambda: gesto(-120), 2.5)
    print("   (info) pizzico indietro (10 passi da -120): cambio immagine %.2f" % d6)
    P.controlla(d6 > 3.0, "il pizzico indietro cambia l'immagine (zoom)", "%.2f" % d6)
    # inizio senza fine: si chiude da solo a 400 ms; subito dopo un trascinamento a un dito deve funzionare
    pizzico(0, 0)
    time.sleep(1.2)
    d7 = misura(fl, f, trascina, 2.0)
    print("   (info) trascinamento dopo un pizzico senza fine: cambio immagine %.2f" % d7)
    # (l'esito non e' un'asserzione: vicino al massimo allontanamento la mappa non si sposta; che nessun dito
    # resti giu' lo verifica prova_gesti.py contando i puntatori in dumpsys input)
    # pizzico con misure sbagliate: scartato
    P.input_msg(ci, 0x55, struct.pack(">IHHBHHh", 1, 700, 1280, 0, 360, 640, 0))
    P.input_msg(ci, 0x55, struct.pack(">IHHBHHh", 1, 720, 1280, 9, 360, 640, 0))
    time.sleep(0.5)
    fl.chiudi()
    c.chiedi(0x41, struct.pack(">IB", 1, 1))
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
