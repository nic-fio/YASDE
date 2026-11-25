#!/usr/bin/env python3
"""Prova di VIDEO_PAUSA / VIDEO_RIPRENDI (0x44/0x45) sul telefono vero, col servizio e i canali veri.
Prende il turno del telefono (un solo turno, tetto di tempo), lo rilascia sempre. Nessun dato personale.
Jar completo da `strumenti/prova.sh telefono` in ~/.cache/yasde-sviluppo/video/yasde.jar"""
import os, signal, struct, subprocess, sys, time

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prova_video as P

TURNO = os.path.expanduser("~/.cache/yasde-sviluppo/telefono.occupato")
LIMITE = 170
P.falliti = 0


def _termina(*a):
    raise SystemExit("terminato")


signal.signal(signal.SIGTERM, _termina)  # il blocco finally rilascia sempre il turno


def conta(fl):
    return len(fl.pacchetti)


def prova():
    inizio = time.time()
    P.adb("push", P.CACHE + "/yasde.jar", P.JAR)
    p, _ = P.lancia()
    P.adb("forward", "tcp:%d" % P.PORTA, "localabstract:yasde")
    c = P.Comandi()
    c.chiedi(1, struct.pack(">H", 1))
    ID = struct.pack(">I", 1)

    # --- app che si muove: Orologio, cronometro avviato
    t, b = P.apri_video(c, 1, 720, 1280, 450, "com.sec.android.app.clockpackage")
    P.controlla(t == 2, "sessione con l'Orologio")
    fl = P.FlussoVideo(1, os.path.join(P.CACHE, "p1.h264"))
    ci = P.apri(b"input\n")
    time.sleep(2.5)
    for (x, y) in ((438, 1176), (570, 984)):
        P.tocco(ci, 1, 720, 1280, 0, x, y)
        time.sleep(0.06)
        P.tocco(ci, 1, 720, 1280, 2, x, y)
        time.sleep(0.8)
    time.sleep(1.5)
    n0 = conta(fl)
    t, b = c.chiedi(0x44, ID)
    n_ok = conta(fl)
    P.controlla(t == 2, "VIDEO_PAUSA")
    time.sleep(0.3)
    n1 = conta(fl)
    P.controlla(n1 - n_ok <= 6, "dopo l'OK a VIDEO_PAUSA al massimo i pacchetti gia' in viaggio", "%d" % (n1 - n_ok))
    time.sleep(3)
    P.controlla(conta(fl) == n1, "nessun pacchetto per 3 s in pausa", "%d dopo la pausa, %d dopo 3 s" % (n1 - n0, conta(fl) - n1))
    t, b = c.chiedi(0x44, ID)
    P.controlla(t == 2, "pausa doppia: OK senza effetto")
    t, b = c.chiedi(0x43, ID)
    P.controlla(t == 3, "VIDEO_CHIAVE in pausa -> ERRORE", b[2:].decode())
    t, b = c.chiedi(0x42, struct.pack(">IHHH", 1, 640, 1136, 450))
    P.controlla(t == 3, "VIDEO_RIDIMENSIONA in pausa -> ERRORE", b[2:].decode())
    pre = conta(fl)
    t0 = time.time()
    t, b = c.chiedi(0x45, ID)
    ok = t == 2
    ms_ok = (time.time() - t0) * 1000
    ch = None
    while time.time() - t0 < 3:
        if len(fl.pacchetti) > pre:
            ch = fl.pacchetti[pre:]
            break
        time.sleep(0.002)
    ms = (ch[0][6] - t0) * 1000 if ch else -1
    time.sleep(0.5)
    ch = fl.pacchetti[pre:]
    primi = [(x[0], x[1] & 1) for x in ch[:3]]
    P.controlla(ok and ms_ok < 100, "VIDEO_RIPRENDI risponde OK subito", "%.0f ms" % ms_ok)
    chiave_ms = next(((x[6] - t0) * 1000 for x in ch if x[0] == 2 and x[1] & 1), -1)
    P.controlla(0 < chiave_ms < 250, "app in movimento: fotogramma chiave entro 250 ms dalla ripresa", "%.0f ms" % chiave_ms)
    prim = next((x for x in ch), None)
    P.controlla(prim is not None and (prim[0] == 1 or (prim[0] == 2 and prim[1] & 1)) and
                all(x[0] == 1 or x[1] & 1 for x in ch[:1]),
                "il primo pacchetto dopo la ripresa e' CONFIGURAZIONE o fotogramma chiave", str(primi))
    t, b = c.chiedi(0x45, ID)
    P.controlla(t == 2, "ripresa doppia: OK senza effetto")
    # cicli ripetuti: dopo l'OK a RIPRENDI nessun fotogramma non chiave prima della prima chiave
    cattivi, tempi = 0, []
    for ciclo in range(6):
        c.chiedi(0x44, ID)
        time.sleep(0.5)  # i pacchetti gia' scritti prima della pausa arrivano al PC
        pre = conta(fl)
        time.sleep(1.0)
        if ciclo == 0:
            out = P.adb("shell", "top -b -n 2 -d 1 -m 15").split("\nTasks:")[-1].splitlines()
            print("   (info) top in pausa:", " | ".join(" ".join(l.split()[1:2] + l.split()[8:9] + l.split()[11:12]) for l in out
                                                      if "app_process" in l or "surfaceflinger" in l))
        t0 = time.time()
        c.chiedi(0x45, ID)
        time.sleep(1.2)
        nuovi = fl.pacchetti[pre:]
        prima_chiave = next((i for i, x in enumerate(nuovi) if x[0] == 2 and x[1] & 1), None)
        if prima_chiave is None or any(x[0] == 2 and not x[1] & 1 for x in nuovi[:prima_chiave]):
            cattivi += 1
        elif nuovi:
            tempi.append((nuovi[prima_chiave][6] - t0) * 1000)
    P.controlla(cattivi == 0 and len(tempi) == 6, "6 pause/riprese: mai un fotogramma non chiave prima della prima chiave",
                "chiavi dopo %s ms" % ["%.0f" % x for x in tempi])
    # il cronometro dell'Orologio non va lasciato acceso: si ferma e si azzera
    for (x, y) in ((570, 984), (150, 984)):
        P.tocco(ci, 1, 720, 1280, 0, x, y)
        time.sleep(0.06)
        P.tocco(ci, 1, 720, 1280, 2, x, y)
        time.sleep(0.8)
    fl.chiudi()
    t, b = c.chiedi(0x41, struct.pack(">IB", 1, 1))
    P.controlla(t == 2, "VIDEO_CHIUDI")
    ci.close()

    # --- app ferma: Impostazioni (statica); input in pausa con effetto visibile dopo la ripresa
    t, b = P.apri_video(c, 2, 720, 1280, 450, "com.android.settings")
    fl2 = P.FlussoVideo(2, os.path.join(P.CACHE, "p2.h264"))
    ci = P.apri(b"input\n")
    time.sleep(3.5)
    ID2 = struct.pack(">I", 2)
    prima = fl2.fotogrammi()
    t, b = c.chiedi(0x44, ID2)
    time.sleep(0.5)
    pre = conta(fl2)
    punto_prima = fl2.fotogrammi()
    P.rotella(ci, 2, 720, 1280, 360, 640, 0, -360)  # scorrimento in pausa
    time.sleep(2)
    P.controlla(conta(fl2) == pre, "app ferma: nessun pacchetto in pausa nemmeno con input")
    t0 = time.time()
    t, b = c.chiedi(0x45, ID2)
    chiave2 = -1
    while time.time() - t0 < 3 and chiave2 < 0:
        for x in fl2.pacchetti[pre:]:
            if x[0] == 2 and x[1] & 1:
                chiave2 = (x[6] - t0) * 1000
                break
        time.sleep(0.002)
    P.controlla(t == 2 and 0 < chiave2 < 600, "app ferma: fotogramma chiave dopo la ripresa (ridisegno forzato)", "%.0f ms" % chiave2)
    time.sleep(1.5)
    n_dopo = fl2.fotogrammi()
    P.rotella(ci, 2, 720, 1280, 360, 640, 0, -360)  # controllo informativo: lo stesso scorrimento a ripresa avvenuta
    time.sleep(1.5)
    fl2.chiudi()
    fr = P.immagini(os.path.join(P.CACHE, "p2.h264"), os.path.join(P.CACHE, "tmp.raw"))
    d_pausa = P.diff(fr[punto_prima - 1], fr[n_dopo - 1])
    d_dopo = P.diff(fr[n_dopo - 1], fr[-1])
    print("   (info) differenza dopo l'input in pausa %.2f, dopo un input a ripresa avvenuta %.2f" % (d_pausa, d_dopo))
    # Misurato: a superficie staccata Android non consegna l'input all'app (finestra non visibile):
    # l'input in pausa e' accettato ma non ha effetto; dopo la ripresa funziona.
    P.controlla(d_pausa < 1.0 and d_dopo > 3.0, "input in pausa senza effetto, dopo la ripresa funziona (comportamento di Android)",
                "in pausa %.2f, dopo la ripresa %.2f" % (d_pausa, d_dopo))
    # --- casi di §2.2 (precisazioni): id sconosciuto, pausa prima che il canale sia aperto
    t, b = c.chiedi(0x44, struct.pack(">I", 999))
    P.controlla(t == 3, "VIDEO_PAUSA con id sconosciuto -> ERRORE")
    t, b = c.chiedi(0x45, struct.pack(">I", 999))
    P.controlla(t == 3, "VIDEO_RIPRENDI con id sconosciuto -> ERRORE")
    t, b = P.apri_video(c, 3, 720, 1280, 450, "com.sec.android.app.clockpackage")
    ID3 = struct.pack(">I", 3)
    t, b = c.chiedi(0x44, ID3)
    P.controlla(t == 2, "VIDEO_PAUSA prima che il canale video sia aperto -> OK")
    fl3 = P.FlussoVideo(3, os.path.join(P.CACHE, "p3.h264"))
    time.sleep(1.5)
    P.controlla(conta(fl3) <= 1, "canale aperto durante la pausa: nessun fotogramma", "%d pacchetti" % conta(fl3))
    t, b = c.chiedi(0x45, ID3)
    time.sleep(1.5)
    pk = fl3.pacchetti
    kk = [(x[0], x[1] & 1) for x in pk if x[0] == 2]
    P.controlla(t == 2 and kk and kk[0][1] == 1, "ripresa col canale aperto: primo fotogramma e' una chiave", str([(x[0], x[1] & 1) for x in pk[:3]]))
    fl3.chiudi()
    c.chiedi(0x41, struct.pack(">IB", 3, 1))
    t, b = c.chiedi(0x44, ID3)
    P.controlla(t == 3, "VIDEO_PAUSA su sessione chiusa -> ERRORE")

    # --- chiudi in pausa
    t, b = c.chiedi(0x44, ID2)
    t, b = c.chiedi(0x41, struct.pack(">IB", 2, 1))
    P.controlla(t == 2, "VIDEO_CHIUDI in pausa")
    t, b = c.chiedi(0x45, ID2)
    P.controlla(t == 3, "ripresa dopo la chiusura -> ERRORE (sessione sconosciuta)")
    ci.close()
    t, b = c.chiedi(4)
    time.sleep(3)
    P.controlla(P.sh("dumpsys display | grep -c 'yasde-'").strip() == "0", "nessuno schermo virtuale rimasto")
    P.controlla(time.time() - inizio < LIMITE, "turno sotto il tetto", "%.0f s" % (time.time() - inizio))


if __name__ == "__main__":
    mio = False
    try:
        mio = P.avvia_turno()
        prova()
    finally:
        if mio:
            P.pulisci()
            for f in os.listdir(P.CACHE):
                if f.endswith((".h264", ".raw")):
                    os.remove(os.path.join(P.CACHE, f))
            P.rilascia_turno()
    print("falliti:", P.falliti)
    sys.exit(1 if P.falliti else 0)
