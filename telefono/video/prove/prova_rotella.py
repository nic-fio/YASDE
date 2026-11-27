#!/usr/bin/env python3
"""Indagine sulla rotella (varianti dell'evento ACTION_SCROLL) in un'app che mostra una mappa, su uno schermo VIRTUALE.
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
JB = "/data/local/tmp/prova-rotella-%d.jar" % os.getpid()


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
    c.chiedi(1, struct.pack(">H", 3))
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

    P.adb("push", P.CACHE + "/prova-pausa.jar", JB)
    for v in ("A", "B", "C", "D", "A"):
        def inietta(v=v):
            out = P.sh("CLASSPATH=%s:%s app_process / it.yasde.ProvaRotella %s %s 360 640" % (P.JAR, JB, did, v))
            if out.strip():
                print("   uscita:", out.strip()[:300])
        d = misura(fl, f, inietta, 2.5)
        print("   (info) rotella variante %s: cambio immagine %.2f" % (v, d))
    P.sh("rm -f " + JB)
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
            P.sh("rm -f " + JB)
            P.pulisci()
            for g in os.listdir(P.CACHE):
                if g.endswith((".h264", ".raw", ".png")):
                    os.remove(os.path.join(P.CACHE, g))
            P.rilascia_turno()
    print("falliti:", P.falliti)
