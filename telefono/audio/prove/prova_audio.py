#!/usr/bin/env python3
"""Prova integrata della cattura audio sul telefono vero, attraverso il servizio e il canale `audio`.
Nessun dato personale nei file. Uso: prova_audio.py
Preparazione: jar completo da `strumenti/prova.sh telefono` in ~/.cache/yasde-sviluppo/audio/yasde-completo.jar
e il jar di prova (ProvaAudio, fuori dal jar di produzione) da telefono/audio/yasde-prova-audio.jar dello
stesso lavoro sul server, in ~/.cache/yasde-sviluppo/audio/yasde-prova-audio.jar
Richiede adb, ffmpeg, bin_a_ogg.py (stessa cartella). Il telefono deve essere sbloccato; prendere prima il turno
(mkdir ~/.cache/yasde-sviluppo/telefono.occupato). Lo script prende il turno (o lo
eredita con YASDE_TURNO_MIO=1), si ferma se il telefono non e' Awake o se c'e' gia' un servizio YASDE
o una regola di cattura attiva, usa come volume di prova quello trovato (a 6 solo se e' gia' al massimo) e a fine
prova, anche su SIGINT/SIGTERM, rimette ESATTAMENTE il valore trovato. Nessun suono (tono solo con
YASDE_CON_TONO=1).
Prove: 1 cattura 10 s + chiusura canale; 2 kill -9 del servizio; 3 volume cambiato dall'utente + kill -9
(il valore dell'utente vince); 4 CHIUDI del servizio durante la cattura; 5 chiusura e riapertura immediata
del canale 10 volte di fila; 7 CHIUDI mentre un secondo canale e' in attesa; 6 il PC non legge per 10 s e poi chiude (nessun thread rimasto).
"""
import os, re, signal, socket, struct, subprocess, sys, threading, time

CACHE = os.path.expanduser("~/.cache/yasde-sviluppo/audio")
JARLOCALE = CACHE + "/yasde-completo.jar"
JAR = "/data/local/tmp/yasde.jar"
JARTONO = "/data/local/tmp/prova-audio.jar:/data/local/tmp/prova-audio-extra.jar"
JARTONO_FILE = "/data/local/tmp/prova-audio.jar"
JAREXTRA = "/data/local/tmp/prova-audio-extra.jar"
PORTA = 38463
QUI = os.path.dirname(os.path.abspath(__file__))
VOLUME_PROVA = None  # deciso all'avvio: il volume trovato se non e' al massimo, altrimenti 6
VOLUME_UTENTE = None  # un altro valore, per fingere che l'utente cambi volume
falliti = 0
LOG = []


def adb(*a):
    return subprocess.run(["adb"] + list(a), capture_output=True, text=True).stdout


def sh(c):
    return adb("shell", c)


def controlla(ok, nome, extra=""):
    global falliti
    print(("ok      " if ok else "FALLITA ") + nome + (("  [" + extra + "]") if extra else ""))
    if not ok:
        falliti += 1


def volume():
    m = re.search(r"volume is (\d+) in range \[0\.\.(\d+)\]", sh("cmd media_session volume --stream 3 --get 2>&1"))
    return int(m.group(1)), int(m.group(2))


def imposta_volume(v):
    sh("cmd media_session volume --stream 3 --set %d" % v)
    assert volume()[0] == v


def volumi():
    """Volume STREAM_MUSIC per dispositivo di uscita, dalla riga «Current:» di dumpsys audio: {nome: valore}."""
    blocco = sh("dumpsys audio").split("- STREAM_MUSIC:", 1)[1]
    riga = [r for r in blocco.splitlines() if r.strip().startswith("Current:")][0]
    out = {}
    for m in re.finditer(r"\d+ \(([a-z_0-9]+)\): (\d+)", riga):
        out[m.group(1)] = int(m.group(2))
    return out


def regola_attiva():
    # una regola di cattura appare in `dumpsys audio` come "AudioMix, reg:" e in media.audio_policy come "mixp"
    a = "AudioMix, reg:" in sh("dumpsys audio")
    b = "mixp" in sh("dumpsys media.audio_policy")
    return a and b


def giocatori_2000():
    """Le righe dei player dell'uid 2000 (il tono di prova) in stato started: dicono su quale dispositivo suonano."""
    out = []
    for r in sh("dumpsys audio").splitlines():
        if "AudioPlaybackConfiguration" in r and "u/pid:2000/" in r and "state:started" in r:
            m = re.search(r"deviceIds:\[[^\]]*\]", r)
            out.append(m.group(0) if m else "?")
    return out


def prepara_jar():
    adb("push", JARLOCALE, JAR)
    adb("push", JARLOCALE, JARTONO_FILE)
    adb("push", CACHE + "/yasde-prova-audio.jar", JAREXTRA)


def lancia():
    prepara_jar()
    LOG.clear()
    p = subprocess.Popen(["adb", "shell", "CLASSPATH=%s app_process / it.yasde.Servizio" % JAR],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    while True:
        r = p.stdout.readline()
        if not r:
            raise SystemExit("servizio terminato prima di PRONTO: %r" % LOG)
        LOG.append(r.strip())
        if r.strip() == "PRONTO":
            def log():
                for r2 in p.stdout:
                    LOG.append(r2.strip())
            threading.Thread(target=log, daemon=True).start()
            return p


def apri(riga):
    s = socket.create_connection(("127.0.0.1", PORTA), timeout=10)
    s.sendall(riga)
    return s


def recv_n(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            return None
        b += c
    return b


def messaggio(s, tipo, corpo=b""):
    s.sendall(struct.pack(">BI", tipo, len(corpo)) + corpo)
    h = recv_n(s, 5)
    t, n = struct.unpack(">BI", h)
    return t, recv_n(s, n)


def pid_servizio():
    r = sh("ps -A -o PID,ARGS | grep -E '^ *[0-9]+ app_process .*it.yasde.Servizio'").split()
    return r[0] if r else None


class Lettore:
    """Legge i pacchetti del canale audio su un thread, annotando l'istante di arrivo."""

    def __init__(self, s):
        self.s = s
        self.pacchetti = []  # (arrivo, tempo, dati)
        self.fine = False
        self.t = threading.Thread(target=self.corri, daemon=True)
        self.t.start()

    def corri(self):
        try:
            while True:
                h = recv_n(self.s, 10)
                if h is None:
                    break
                tempo, n = struct.unpack(">QH", h)
                d = recv_n(self.s, n)
                if d is None:
                    break
                self.pacchetti.append((time.time(), tempo, d))
        except OSError:
            pass
        self.fine = True


def chiudi(s):
    # shutdown prima di close: con un altro thread bloccato in recv la close da sola non chiude la connessione
    try:
        s.shutdown(socket.SHUT_RDWR)
    except OSError:
        pass
    s.close()


def avvia_tono(secondi):
    return subprocess.Popen(["adb", "shell", "CLASSPATH=%s app_process / it.yasde.ProvaAudio tono %d 440"
                             % (JARTONO, secondi)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def riepilogo_audio():
    return [r for r in LOG if r.startswith("[audio]")]


def prova1(V0):
    print("--- 1: cattura 10 s, canale audio, chiusura")
    p = lancia()
    print("   log avvio:", [r for r in LOG if "audio" in r.lower()])
    c = apri(b"comandi\n")
    t, _ = messaggio(c, 1, struct.pack(">H", 3))
    controlla(t == 2, "SALUTO")
    controlla(not regola_attiva(), "prima: nessuna regola attiva")
    controlla(volume()[0] == VOLUME_PROVA, "prima: volume %d" % VOLUME_PROVA)
    s = apri(b"audio\n")
    inizio = time.time()
    lett = Lettore(s)
    time.sleep(1.5)
    controlla(regola_attiva(), "durante: regola attiva (dumpsys)")
    controlla(volume()[0] == volume()[1], "durante: volume media al massimo", str(volume()))
    # un secondo canale audio viene chiuso, il primo continua
    s2 = apri(b"audio\n")
    s2.settimeout(3)
    try:
        chiuso = s2.recv(1) == b""
    except OSError:
        chiuso = False
    controlla(chiuso, "secondo canale audio chiuso")
    senza_tono = os.environ.get("YASDE_CON_TONO") != "1"  # nessun suono udibile, salvo richiesta esplicita
    if not senza_tono:
        controlla(regola_attiva(), "il tono parte solo con la regola attiva (altrimenti si sentirebbe)")
        if not regola_attiva():
            raise SystemExit("regola assente: niente tono, mi fermo")
    tono = avvia_tono(6) if not senza_tono else subprocess.Popen(["true"])
    time.sleep(3)
    giocatori_prima = giocatori_2000()
    print("   tono durante la cattura, dispositivo:", giocatori_prima)
    while time.time() - inizio < 10:
        time.sleep(0.2)
    n_prima = len(lett.pacchetti)
    chiudi(s)
    tono.wait(timeout=10)
    time.sleep(2.5)
    controlla(not regola_attiva(), "dopo la chiusura: regola tolta (dumpsys)")
    controlla(volume()[0] == VOLUME_PROVA, "dopo la chiusura: volume ripristinato", str(volume()))
    # Niente tono dopo la chiusura per sentire che il telefono torna a suonare: sarebbe udibile. Basta che la
    # regola sia sparita da dumpsys (controllato sopra).
    pk = lett.pacchetti
    tempi = [x[1] for x in pk]
    controlla(len(pk) > 0 and tempi[0] == 0, "primo pacchetto con tempo 0")
    salti = [i for i in range(1, len(tempi)) if tempi[i] - tempi[i - 1] != 20000]
    controlla(not salti, "tempi sempre a passi di 20 ms", "pacchetti=%d, salti=%d" % (len(pk), len(salti)))
    attesi = 10 * 50
    controlla(abs(len(pk) - attesi) <= attesi * 0.1, "circa %d pacchetti in 10 s" % attesi, str(len(pk)))
    toc_ok = all(len(d) >= 1 and (d[0] >> 3) == 31 and (d[0] & 3) == 0 for _, _, d in pk)
    controlla(toc_ok, "ogni pacchetto e' Opus CELT 20 ms con un solo fotogramma (nessuna intestazione)")
    controlla(all(len(d) >= 2 for _, _, d in pk), "nessun pacchetto vuoto o di una sola cifra")
    arrivi = [pk[i][0] - pk[i - 1][0] for i in range(1, len(pk))]
    print("   intervallo massimo tra arrivi: %.0f ms (media %.1f ms)" % (max(arrivi) * 1000, sum(arrivi) * 1000 / len(arrivi)))
    controlla(max(arrivi) < 0.25, "nessun buco di arrivo oltre 250 ms")
    f = CACHE + "/servizio-audio.bin"
    with open(f, "wb") as o:
        for _, t, d in pk:
            o.write(struct.pack(">QH", t, len(d)) + d)
    r = subprocess.run([sys.executable, QUI + "/bin_a_ogg.py", f, CACHE + "/servizio-audio.opus", "440"],
                       capture_output=True, text=True).stdout
    print("   " + r.replace("\n", "\n   "))
    if not senza_tono:
        controlla("TONO ATTESO 440 Hz: SI" in r, "decodifica: tono da 440 Hz presente")
    print("   log:", riepilogo_audio())
    messaggio(c, 4)
    p.wait(timeout=20)


def prova_kill(utente):
    print("--- %s: kill -9 del servizio durante la cattura" % ("3 (volume cambiato dall'utente)" if utente else "2"))
    p = lancia()
    c = apri(b"comandi\n")
    messaggio(c, 1, struct.pack(">H", 3))
    s = apri(b"audio\n")
    lett = Lettore(s)
    time.sleep(2)
    controlla(regola_attiva(), "durante: regola attiva")
    controlla(volume()[0] == volume()[1], "durante: volume al massimo")
    if utente:
        imposta_volume(VOLUME_UTENTE)
    pid = pid_servizio()
    controlla(pid is not None, "pid del servizio trovato")
    sh("kill -9 %s" % pid)
    time.sleep(4)
    controlla(pid_servizio() is None, "servizio morto")
    controlla(not regola_attiva(), "dopo il kill: regola sparita")
    atteso = VOLUME_UTENTE if utente else VOLUME_PROVA
    controlla(volume()[0] == atteso, "dopo il kill: volume %d (%s)" % (atteso,
              "vince l'utente" if utente else "ripristinato dal guardiano"), str(volume()))
    resti = sh("ls /data/local/tmp | grep -E '^yasde(\\.jar|-custode)'").split()
    controlla(not resti, "nessun file del servizio rimasto", str(resti))
    try:
        p.kill()
    except OSError:
        pass
    imposta_volume(VOLUME_PROVA)


def prova_chiudi():
    print("--- 4: CHIUDI del servizio durante la cattura")
    p = lancia()
    c = apri(b"comandi\n")
    messaggio(c, 1, struct.pack(">H", 3))
    s = apri(b"audio\n")
    lett = Lettore(s)
    time.sleep(2)
    controlla(regola_attiva() and volume()[0] == volume()[1], "durante: regola attiva e volume al massimo")
    t, _ = messaggio(c, 4)
    controlla(t == 2, "CHIUDI -> OK")
    try:
        p.wait(timeout=20)
    except subprocess.TimeoutExpired:
        controlla(False, "servizio terminato")
    time.sleep(1)
    controlla(not regola_attiva(), "dopo CHIUDI: regola sparita")
    controlla(volume()[0] == VOLUME_PROVA, "dopo CHIUDI: volume ripristinato", str(volume()))
    controlla(lett.fine, "canale audio chiuso dal servizio")
    print("   log:", riepilogo_audio())




def prova_volumi_per_dispositivo(kill):
    print("--- 8%s: volume per dispositivo prima/durante/dopo (%s)" % ("b" if kill else "a",
          "kill -9" if kill else "chiusura del canale"))
    prima = volumi()
    print("   prima:", prima)
    p = lancia()
    c = apri(b"comandi\n")
    messaggio(c, 1, struct.pack(">H", 3))
    s = apri(b"audio\n")
    lett = Lettore(s)
    time.sleep(2)
    durante = volumi()
    print("   durante:", durante)
    controlla(durante.get("speaker") == 15, "durante: altoparlante al massimo", str(durante.get("speaker")))
    if kill:
        sh("kill -9 %s" % pid_servizio())
        time.sleep(4)
    else:
        chiudi(s)
        time.sleep(3)
    dopo = volumi()
    print("   dopo:", dopo)
    diversi = dict((k, (prima.get(k), dopo.get(k))) for k in set(prima) | set(dopo)
                   if k != "remote_submix" and prima.get(k) != dopo.get(k))
    controlla(not diversi, "dopo = prima per ogni dispositivo", str(diversi))
    if not kill:
        messaggio(c, 4)
        p.wait(timeout=20)
    else:
        try:
            p.kill()
        except OSError:
            pass


def numero_regole():
    tot = 0
    for m in re.finditer(r"(\d+) AudioMix, reg:", sh("dumpsys audio")):
        tot += int(m.group(1))
    return tot


def guardiano_ha_voce():
    return int(sh("cat /data/local/tmp/yasde-custode-*.sh 2>/dev/null | grep -c audio-volume").strip() or 0) > 0


def nomi_thread():
    pid = pid_servizio()
    return sh("cat /proc/%s/task/*/comm" % pid).split("\n")


def thread_servizio():
    return len(nomi_thread()) - 1


def scalda():
    s = apri(b"audio\n")
    time.sleep(2)
    chiudi(s)
    time.sleep(3)


def nuovi_thread(prima, dopo):
    """Thread comparsi per nome, esclusi quelli del binder e del suo pool (nascono su richiesta e non calano)
    Il confronto parte da un servizio gia' "scaldato" da una cattura (il looper della policy, i thread del codec e
    «process reaper» nascono alla prima volta e restano: qui conta che non crescano).
    Nota: il nome visto in /proc e' troncato a 15 caratteri, «yasde-audio» sono looper, cattura e invio."""
    ignora = re.compile(r"^(Binder:|HwBinder:|binder:|Signal Catcher)")
    p = [n for n in prima if not ignora.match(n)]
    d = [n for n in dopo if n and not ignora.match(n)]
    for n in p:
        if n in d:
            d.remove(n)
    return d


def prova_riaperture():
    print("--- 5: chiusura e riapertura immediata del canale, 10 volte")
    p = lancia()
    c = apri(b"comandi\n")
    messaggio(c, 1, struct.pack(">H", 3))
    scalda()
    th0 = thread_servizio()
    n0 = nomi_thread()
    massimo_regole = 0
    pacchetti = []
    for i in range(10):
        s = apri(b"audio\n")
        lett = Lettore(s)
        t0 = time.time()
        while len(lett.pacchetti) < 10 and time.time() - t0 < 5:
            time.sleep(0.05)
        pacchetti.append(len(lett.pacchetti))
        massimo_regole = max(massimo_regole, numero_regole())
        chiudi(s)
    controlla(min(pacchetti) >= 10, "ogni riapertura riceve pacchetti", str(pacchetti))
    controlla(massimo_regole <= 1, "mai piu' di una regola loopback insieme", str(massimo_regole))
    time.sleep(3)
    controlla(numero_regole() == 0, "alla fine: nessuna regola loopback", str(numero_regole()))
    controlla(volume()[0] == VOLUME_PROVA, "alla fine: volume ripristinato", str(volume()))
    controlla(not guardiano_ha_voce(), "alla fine: nessuna voce audio nel guardiano")
    th1 = thread_servizio()
    extra = nuovi_thread(n0, nomi_thread())
    controlla(not extra, "nessun thread nostro rimasto (esclusa la riserva del binder)", "prima %d, dopo %d, nuovi: %s" % (th0, th1, extra))
    print("   log:", [r for r in LOG if "rifiutato" in r or "NON" in r or "vivo" in r or r.startswith("    ")][:30])
    messaggio(c, 4)
    p.wait(timeout=20)


def prova_pc_non_legge():
    print("--- 6: il PC non legge per 10 s, poi chiude")
    p = lancia()
    c = apri(b"comandi\n")
    messaggio(c, 1, struct.pack(">H", 3))
    scalda()
    th0 = thread_servizio()
    n0 = nomi_thread()
    s = apri(b"audio\n")
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    time.sleep(10)
    controlla(regola_attiva(), "durante: regola attiva")
    th_durante = thread_servizio()
    chiudi(s)
    time.sleep(3)
    controlla(numero_regole() == 0, "dopo: nessuna regola loopback")
    controlla(volume()[0] == VOLUME_PROVA, "dopo: volume ripristinato", str(volume()))
    controlla(not guardiano_ha_voce(), "dopo: nessuna voce audio nel guardiano")
    th1 = thread_servizio()
    extra = nuovi_thread(n0, nomi_thread())
    controlla(not extra and "yasde-audio-invio" not in nomi_thread(), "nessun thread di invio rimasto",
              "prima %d, durante %d, dopo %d, nuovi: %s" % (th0, th_durante, th1, extra))
    messaggio(c, 4)
    p.wait(timeout=20)


def prova_chiudi_con_attesa():
    print("--- 7: CHIUDI del servizio mentre un secondo canale audio e' in attesa")
    p = lancia()
    c = apri(b"comandi\n")
    messaggio(c, 1, struct.pack(">H", 3))
    s = apri(b"audio\n")
    lett = Lettore(s)
    time.sleep(1.5)
    s2 = apri(b"audio\n")  # resta in attesa fino a 2 s
    time.sleep(0.4)
    t, _ = messaggio(c, 4)
    controlla(t == 2, "CHIUDI -> OK")
    try:
        p.wait(timeout=20)
    except subprocess.TimeoutExpired:
        controlla(False, "servizio terminato")
    time.sleep(3)
    controlla(numero_regole() == 0 and not regola_attiva(), "nessuna regola loopback")
    controlla(volume()[0] == VOLUME_PROVA, "volume ripristinato e non rialzato", str(volume()))
    s2.settimeout(3)
    try:
        controlla(s2.recv(1) == b"", "il secondo canale e' chiuso")
    except OSError:
        controlla(False, "il secondo canale e' chiuso")
    controlla(not guardiano_ha_voce(), "nessuna voce audio nel guardiano")
    print("   log:", [r for r in LOG if r.startswith("[audio]")])


TURNO = os.path.expanduser("~/.cache/yasde-sviluppo/telefono.occupato")


class Interrotto(BaseException):
    pass


def segnale(n, f):
    raise Interrotto()


def main():
    # Turno del telefono: preso qui (e tolto solo se preso qui), oppure dal chiamante con YASDE_TURNO_MIO=1.
    turno_mio = False
    if os.environ.get("YASDE_TURNO_MIO") != "1":
        try:
            os.mkdir(TURNO)
            turno_mio = True
        except OSError:
            raise SystemExit("telefono occupato (turno preso da un altro): mi fermo")
    try:
        if "Awake" not in sh("dumpsys power | grep mWakefulness="):
            raise SystemExit("telefono non Awake: mi fermo (mai risvegliarlo)")
        esegui()
    finally:
        if turno_mio:
            os.rmdir(TURNO)


def esegui():
    global VOLUME_PROVA, VOLUME_UTENTE
    # PRIMA di qualunque modifica: il volume trovato, e il telefono non deve essere in uso da un'altra sessione.
    V0, massimo = volume()
    print("volume media trovato: %d (massimo %d)" % (V0, massimo))
    vecchio = sh("ps -A -o PID,ARGS | grep -E 'it[.]yasde' | grep -v grep; "
                 "ls /data/local/tmp | grep -E '^yasde-custode-'").strip()
    if pid_servizio() is not None or vecchio or regola_attiva():
        raise SystemExit("telefono in uso da un'altra sessione (servizio YASDE vivo o regola di cattura "
                         "attiva): non tocco nulla")
    # Le prove di ripristino hanno senso solo con volume sotto il massimo: si usa il valore trovato stesso
    # (nessuna modifica); solo se e' gia' al massimo si abbassa a 6 per la durata della prova.
    VOLUME_PROVA = V0 if V0 < massimo else 6
    VOLUME_UTENTE = VOLUME_PROVA + 2 if VOLUME_PROVA + 2 < massimo else VOLUME_PROVA - 2
    signal.signal(signal.SIGINT, segnale)
    signal.signal(signal.SIGTERM, segnale)
    try:
        adb("forward", "tcp:%d" % PORTA, "localabstract:yasde")
        tutte = [("1", lambda: prova1(V0)), ("2", lambda: prova_kill(False)), ("3", lambda: prova_kill(True)),
                 ("4", prova_chiudi), ("5", prova_riaperture), ("6", prova_pc_non_legge),
                 ("7", prova_chiudi_con_attesa),
                 ("8a", lambda: prova_volumi_per_dispositivo(False)), ("8b", lambda: prova_volumi_per_dispositivo(True))]
        for nome, f in tutte:
            if len(sys.argv) < 2 or nome in sys.argv[1:]:
                if not nome.startswith("8") and volume()[0] != VOLUME_PROVA:
                    imposta_volume(VOLUME_PROVA)
                f()
    finally:
        # Stato finale: ESATTAMENTE il volume trovato, qualunque cosa sia successa (anche SIGINT/SIGTERM).
        for _ in range(3):
            sh("cmd media_session volume --stream 3 --set %d" % V0)
            if volume()[0] == V0:
                break
        sh("rm -f %s %s %s" % (JAR, JARTONO_FILE, JAREXTRA))
        adb("forward", "--remove", "tcp:%d" % PORTA)
        print("volume finale:", volume()[0], "(trovato %d)" % V0)
    print("FALLITI:", falliti)
    sys.exit(1 if falliti else 0)


main()
