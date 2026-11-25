#!/usr/bin/env python3
"""Misura della pausa dei flussi (Fase 0). Uso: prova_pausa.py <E|A|B1|B2|B3|S1|S0> <pacchetto> ["x,y;x,y" ["x,y;x,y"]]
Prende il turno del telefono (solo se la mkdir riesce), esce se il telefono non e' sveglio o c'e' gia' un servizio
YASDE, ferma solo il processo che lancia e cancella solo i file che crea. Jar: ~/.cache/yasde-sviluppo/video/
(yasde.jar e prova-pausa.jar). Nessun dato personale nei file."""
import os, re, sys, subprocess, threading, time

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prova_video as P

CACHE = P.CACHE
N = os.getpid()
J1, J2, H = ("/data/local/tmp/prova-pausa-%d-a.jar" % N, "/data/local/tmp/prova-pausa-%d-b.jar" % N,
             "/data/local/tmp/prova-pausa-%d.h264" % N)
PID_PROVA = []  # pid del processo lanciato da questa prova
LIMITE = 150  # secondi: tetto per il turno


def top():
    out = P.adb("shell", "top -b -n 2 -d 1 -m 15")
    parti = out.split("\nTasks:")
    ultima = parti[-1].splitlines()
    righe = [l for l in ultima if "app_process" in l or "surfaceflinger" in l or "PID" in l and "CPU" in l]
    print("top:\n  " + "\n  ".join(r.strip() for r in righe))


def prova(strategia, pacchetto, tocchi):
    inizio = time.time()
    P.adb("push", CACHE + "/yasde.jar", J1)
    P.adb("push", CACHE + "/prova-pausa.jar", J2)
    cmd = "PROVA_FILE=%s CLASSPATH=%s:%s app_process / it.yasde.ProvaPausa %s %s %s %s" % (
        H, J1, J2, strategia, pacchetto, " ".join("'%s'" % t for t in tocchi), "")
    p = subprocess.Popen(["adb", "shell", cmd], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    for _ in range(30):  # il pid del processo che abbiamo lanciato (prima non ce n'era nessuno)
        out = P.sh("ps -A -o PID,ARGS | grep -F 'it.yasde.ProvaPausa' | grep -v grep").split()
        if out and out[0].isdigit():
            PID_PROVA.append(int(out[0]))
            break
        time.sleep(0.1)

    def leggi():
        for r in p.stdout:
            print(r.rstrip(), flush=True)
            if r.startswith("PAUSA 1"):
                time.sleep(1.5)
                top()
    lettore = threading.Thread(target=leggi, daemon=True)
    lettore.start()
    try:
        p.wait(timeout=max(5, LIMITE - (time.time() - inizio)))
    except subprocess.TimeoutExpired:
        p.kill()
        print("TEMPO SCADUTO: esco e rilascio il turno")
    P.adb("pull", H, CACHE + "/pausa.h264")


def ferma_processo_proprio():
    for pid in PID_PROVA:
        P.sh("kill -9 %d" % pid)


if __name__ == "__main__":
    mio = False
    try:
        if len(sys.argv) < 3 or not re.fullmatch(r"[A-Za-z0-9_.]+", sys.argv[2]):
            sys.exit("uso: prova_pausa.py <strategia> <pacchetto con soli [A-Za-z0-9_.]> [tocchi]")
        for t in sys.argv[3:5]:
            if not re.fullmatch(r"[0-9,;]+", t):
                sys.exit("tocchi non validi")
        mio = P.avvia_turno()
        prova(sys.argv[1], sys.argv[2], sys.argv[3:5])
    finally:
        if mio:
            ferma_processo_proprio()
            P.sh("rm -f %s %s %s" % (J1, J2, H))
            P.rilascia_turno()
