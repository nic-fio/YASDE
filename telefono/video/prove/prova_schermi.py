#!/usr/bin/env python3
"""Prova che chiudere un flusso non lasci schermi virtuali nel gestore delle finestre: conta le voci
"Display: mDisplayId=N" di `dumpsys window displays` prima e dopo aprire/chiudere sessioni (una prima app,
con e senza togli_task, e una seconda app). Non tocca le voci gia' presenti.
Uso: prova_schermi.py [pacchetto1 [pacchetto2]]  (pacchetto1 meglio se ha attivita' incorporate nello stesso
task; senza argomenti si scelgono le prime due app di sistema con attivita' di avvio, senza stamparne il nome)
Turno del telefono preso con mkdir (solo se riesce), telefono sveglio altrimenti si esce. Nessun dato personale."""
import os, re, signal, struct, subprocess, sys, time

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prova_video as P

TURNO = os.path.expanduser("~/.cache/yasde-sviluppo/telefono.occupato")
P.falliti = 0


def _termina(*a):
    raise SystemExit("terminato")


signal.signal(signal.SIGTERM, _termina)  # il finally rilascia il turno solo se e' mio


def voci():
    out = P.sh("dumpsys window displays | grep -c 'Display: mDisplayId='").strip()
    return int(out) if out.isdigit() else -1


def rimossi():
    out = P.sh("dumpsys activity activities | grep -c 'mIsRemovalRequested=true'").strip()
    return int(out) if out.isdigit() else -1


def scegli_app():
    sistema = set(l.split(":", 1)[1] for l in P.sh("pm list packages -s").split() if ":" in l)
    trovate = []
    for riga in P.sh("cmd package query-activities --brief -a android.intent.action.MAIN -c android.intent.category.LAUNCHER").splitlines():
        m = re.match(r"\s*([A-Za-z0-9_.]+)/", riga)
        if m and m.group(1) in sistema and m.group(1) not in trovate:
            trovate.append(m.group(1))
    return trovate


def prova(giri, kill_app, app_a, app_b):
    P.adb("push", P.CACHE + "/yasde.jar", P.JAR)
    p, _ = P.lancia()
    P.adb("forward", "tcp:%d" % P.PORTA, "localabstract:yasde")
    c = P.Comandi()
    c.chiedi(1, struct.pack(">H", 3))
    base, base_rim = voci(), rimossi()
    print("   (info) voci prima: %d, task con rimozione richiesta prima: %d" % (base, base_rim))
    k = 1
    for pk, togli in giri:
        t, b = P.apri_video(c, k, 720, 1280, 450, pk)
        P.controlla(t == 2, "apri l'app %d" % (k))
        time.sleep(3)
        t0 = time.time()
        t, b = c.chiedi(0x41, struct.pack(">IB", k, togli))
        P.controlla(t == 2 and time.time() - t0 < 5, "chiudi l'app %d togli=%d" % (k, togli), "%.0f ms" % ((time.time() - t0) * 1000))
        time.sleep(2)
        P.controlla(voci() == base, "dopo la chiusura le voci tornano a %d" % base, "ora %d" % voci())
        k += 1
    P.controlla(rimossi() == base_rim, "nessun nuovo task 'rimozione richiesta' agganciato", "prima %d, dopo %d" % (base_rim, rimossi()))
    # morte brusca del componente con una sessione aperta (seconda app)
    t, b = P.apri_video(c, k, 720, 1280, 450, kill_app)
    time.sleep(3)
    if P.PID:
        P.sh("kill -9 %d" % P.PID)
    time.sleep(5)
    P.controlla(voci() == base, "dopo kill -9 con una sessione aperta (seconda app) le voci tornano a %d" % base, "ora %d" % voci())
    P.controlla(rimossi() == base_rim, "dopo kill -9 nessun task 'rimozione richiesta' in piu'", "ora %d" % rimossi())
    # CHIUDI del servizio con tre sessioni aperte: le chiusure vanno in parallelo, entro i 3 s concessi
    P.adb("push", P.CACHE + "/yasde.jar", P.JAR)
    p2, _ = P.lancia()
    c = P.Comandi()
    c.chiedi(1, struct.pack(">H", 3))
    for k2, app_k in ((51, app_a), (52, app_b), (53, app_a)):
        t, b = P.apri_video(c, k2, 720, 1280, 450, app_k)
        P.controlla(t == 2, "apri la sessione %d prima del CHIUDI" % k2)
    time.sleep(2.5)
    t0 = time.time()
    t, b = c.chiedi(4)
    for _ in range(60):
        if p2.poll() is not None:
            break
        time.sleep(0.1)
    P.controlla(t == 2 and p2.poll() is not None and time.time() - t0 < 3.5, "CHIUDI con tre sessioni: servizio terminato entro 3 s",
                "%.1f s" % (time.time() - t0))
    time.sleep(2)
    P.controlla(voci() == base, "dopo il CHIUDI le voci tornano a %d" % base, "ora %d" % voci())


if __name__ == "__main__":
    mio = False
    try:
        mio = P.avvia_turno()
        app = sys.argv[1:3] if len(sys.argv) > 1 else scegli_app()
        if len(app) == 1:
            app = app * 2
        if len(app) < 2:
            sys.exit("nessuna app con attivita' di avvio trovata")
        prova([(app[0], 1), (app[0], 0), (app[1], 1)], app[1], app[0], app[1])
    finally:
        if mio:
            P.pulisci()
            for g in os.listdir(P.CACHE):
                if g.endswith((".h264", ".raw", ".png")):
                    os.remove(os.path.join(P.CACHE, g))
            P.rilascia_turno()
    print("falliti:", P.falliti)
    sys.exit(1 if P.falliti else 0)
