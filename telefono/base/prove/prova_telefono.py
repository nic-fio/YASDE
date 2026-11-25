#!/usr/bin/env python3
"""Prove del componente base sul telefono vero (via adb). Nessun dato personale nei file.

Preparazione (una volta): jar principale da `strumenti/prova.sh telefono-base`, copiato in
~/.cache/yasde-sviluppo/base/yasde.jar. Jar del pezzo finto (guardiano), da compilare
sul server nel container con android.jar e le sorgenti di telefono/contratto, dopo aver copiato
InputProva.java.txt come it/yasde/Input.java, e poi d8 -> yasde-prova-base.jar
nella stessa cartella di cache.

Uso: prova_telefono.py saluto | uccisione | shell | blocco | bloccokill | scrittura | avvio | vuoto | resti
  saluto     SALUTO, tipo sconosciuto, versione errata, secondo canale, CHIUDI
  uccisione  kill -9 del servizio: il guardiano deve ripristinare e cancellare il jar
  blocco     un ripristino che si blocca (sh -c 'sleep 100000 & wait', un nipote che timeout non
             raggiunge) prima di uno normale: dopo CHIUDI il servizio rinuncia dopo 20 s e il
             guardiano deve eseguire comunque tutto
  bloccokill come blocco, ma con kill -9 del servizio (lavora il guardiano)
  scrittura  scrittura dell'elenco simulata fallita (-Dyasde.scritturaFallita=true): imposta
             ritorna false e dopo kill -9 non si esegue nulla
  avvio      kill -9 del servizio a meta' dell'avvio del guardiano: nessun file deve restare
  vuoto      elenco vuoto e kill -9 del servizio: nessun file deve restare
  resti      resti di una sessione morta (file FINTI creati dalla prova: uno script di guardiano con un
             ripristino, un jar, un accessorio): il servizio che parte li ripulisce dopo aver eseguito il
             ripristino; un file con altro nome non si tocca
  shell      chiusura brusca della shell del PC: stesso risultato

Regole (CLAUDE.md): turno del telefono (mkdir telefono.occupato, o YASDE_TURNO_MIO=1 se lo ha
preso il chiamante e la cartella esiste); prima di qualunque scrittura, in sola lettura, esce se
sul telefono c'e' gia' un it.yasde, un yasde-custode-* o uno sleep 77777; usa jar con nomi
propri (prova-base-*); annota i pid che lancia e in pulizia uccide e cancella SOLO quelli; ogni
comando adb ha una scadenza; la pulizia gira in un finally.
"""
import os, re, select, socket, struct, subprocess, sys, time

TURNO = os.path.expanduser("~/.cache/yasde-sviluppo/telefono.occupato")
CACHE = os.path.expanduser("~/.cache/yasde-sviluppo/base")
DIR = "/data/local/tmp/"
JAR = DIR + "prova-base-servizio.jar"
JARP = DIR + "prova-base-pezzo.jar"
SEGNAPOSTO = DIR + "prova-base-ripristino"
BLOCCA = DIR + "prova-base-blocca"
PORTA = 38457

FIGLI = []        # processi adb lanciati dalla prova: tutti uccisi alla fine
PIDS = []         # pid dei servizi lanciati dalla prova (e dai quali derivano i file del guardiano)
ESTRANEO = []     # pid dello sleep 77777 lanciato dalla prova
SESSIONI = []     # sessioni dei miei guardiani e delle mie esecuzioni dell'elenco (id = pid del capo)
falliti = 0
# File finti della prova "resti": nomi della forma dei resti veri ma con pid e cifre inventati; la prova
# esce prima di scrivere se sul telefono c'e' gia' qualcosa che il servizio potrebbe scambiare per resto.
RESTO_SCRIPT = DIR + "yasde-custode-999999.sh"
RESTO_PID = RESTO_SCRIPT + ".pid"
RESTO_JAR = DIR + "yasde-deadbeef.jar"
ESTRANEO_FILE = DIR + "prova-base-estraneo.txt"


def adb(*a, scadenza=40):
    try:
        return subprocess.run(["adb"] + list(a), capture_output=True, text=True, timeout=scadenza).stdout
    except subprocess.TimeoutExpired:
        print("adb: scadenza superata:", a[:2])
        return ""


def sh(cmd):
    return adb("shell", cmd)


def attendi(condizione, scadenza, passo=0.2):
    """Aspetta a intervalli che condizione() sia vera, al massimo `scadenza` secondi."""
    fine = time.time() + scadenza
    while True:
        if condizione():
            return True
        if time.time() >= fine:
            return False
        time.sleep(passo)


def controlla(ok, nome):
    global falliti
    print(("ok      " if ok else "FALLITA ") + nome)
    if not ok:
        falliti += 1


def processi():
    """Elenco (pid, ppid, argomenti) di tutti i processi del telefono."""
    r = []
    for riga in sh("ps -A -o PID,PPID,ARGS").splitlines()[1:]:
        m = re.match(r"\s*(\d+)\s+(\d+)\s+(.*)", riga)
        if m:
            r.append((int(m.group(1)), int(m.group(2)), m.group(3)))
    return r


def sola_lettura_libero():
    """Vero se sul telefono non c'e' nulla di YASDE ne' di altre prove (sola lettura)."""
    if any("it.yasde" in a and not a.startswith("sh -c ps") for _, _, a in processi()):
        return False
    # il servizio che parte cancella i resti di forma nostra: se ce ne sono (anche del YASDE vero)
    # la prova non parte, altrimenti li toccherebbe lei
    if re.search(r"yasde-custode-|yasde-[0-9a-f]{8}\.jar", sh("ls " + DIR)):
        return False
    if any(p == 999999 for p, _, _ in processi()):
        return False
    return not any(a.startswith("sleep 77777") for _, _, a in processi())


def pid_servizio():
    """Il MIO servizio: il figlio della shell che ha il mio jar nel CLASSPATH. Lo annota."""
    ps = processi()
    for pid, _, a in ps:
        if a.startswith("sh -c CLASSPATH=" + JAR):
            for p2, pp2, a2 in ps:
                if pp2 == pid and a2.startswith("app_process") and "it.yasde.Servizio" in a2:
                    if p2 not in PIDS:
                        PIDS.append(p2)
                    return p2
    return None


def file_guardiano(pid):
    return [DIR + "yasde-custode-%d.sh" % pid + x for x in ("", ".tmp", ".pid")]


def annota_sessione(sid):
    if sid not in SESSIONI:
        SESSIONI.append(sid)


def guardiano_pid(pid):
    """Pid del guardiano del servizio `pid` (la sua riga di comando porta il nome del suo elenco).
    Se lo vede, annota la sua sessione (il guardiano e' capo di una sessione propria)."""
    for p, _, a in processi():
        if a.startswith("sh -c ELENCO") and "yasde-custode-%d.sh" % pid in a:
            annota_sessione(p)
            return p
    return None


def annota():
    """Annota le sessioni dei miei guardiani e quella dell'esecuzione dell'elenco fatta dal servizio
    (il suo pid sta in yasde-custode-<pid>.sh.pid finche' il file esiste). Sempre True."""
    for pid in PIDS:
        guardiano_pid(pid)
        v = sh("cat %s" % file_guardiano(pid)[2]).strip()
        if v.isdigit():
            annota_sessione(int(v))
    return True


def uccidi_sessioni():
    """Uccide i processi rimasti nelle SOLE sessioni annotate (ci restano anche gli orfani di un
    ripristino scaduto, adottati da init). Prima di uccidere ricontrolla le righe di comando: i
    membri devono essere shell, timeout o sleep, altrimenti l'id e' stato riusato da altri e non
    si tocca nulla. Mai per nome."""
    for sid in SESSIONI:
        membri = [int(x) for x in sh("pgrep -s %d" % sid).split() if x.isdigit()]
        if not membri:
            continue
        righe = dict((p, a) for p, _, a in processi())
        presenti = [m for m in membri if m in righe]
        if presenti and all(righe[m].split(" ")[0] in ("sh", "timeout", "sleep") for m in presenti):
            sh("kill -9 " + " ".join(str(m) for m in presenti))
        elif presenti:
            print("sessione %d non mia (righe diverse dalle attese): non toccata" % sid)


def resti():
    """Cio' che la prova ha creato e deve sparire: il mio jar e i file dei miei guardiani."""
    annota()
    nomi = sh("ls " + DIR).split()
    mio = [os.path.basename(JAR)] + [os.path.basename(f) for p in PIDS for f in file_guardiano(p)]
    return [n for n in nomi if n in mio]


def uccidi_pid(pid, contiene):
    """kill -9 di un pid solo se la sua riga di comando e' ancora quella attesa (niente pid riusati)."""
    if any(p == pid and contiene in a for p, _, a in processi()):
        sh("kill -9 %d" % pid)


def uccidi_servizio():
    pid = pid_servizio()
    if pid:
        uccidi_pid(pid, "it.yasde.Servizio")
    return pid is not None


def sleep_estraneo_vivo():
    return any(p in ESTRANEO and a.startswith("sleep 77777") for p, _, a in processi())


def uccidi_estraneo():
    for p in ESTRANEO:
        uccidi_pid(p, "sleep 77777")


def leggi_riga(p, scadenza):
    """Una riga dallo stdout non bufferizzato di p, con scadenza; None se finisce o scade."""
    fine = time.time() + scadenza
    buf = b""
    while time.time() < fine:
        if select.select([p.stdout], [], [], 0.5)[0]:
            c = os.read(p.stdout.fileno(), 1)
            if not c:
                return None
            if c == b"\n":
                return buf.decode(errors="replace")
            buf += c
    return None


def lancia_adb(opzioni, con_prova):
    cp = JAR + (":" + JARP if con_prova else "")
    p = subprocess.Popen(["adb", "shell", "CLASSPATH=%s app_process %s/ it.yasde.Servizio" % (cp, opzioni)],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    FIGLI.append(p)
    return p


def lancia(con_prova, opzioni=""):
    p = lancia_adb(opzioni, con_prova)
    righe = []
    while True:
        r = leggi_riga(p, 30)
        if r is None:
            raise SystemExit("servizio terminato o muto prima di PRONTO: %r" % righe)
        righe.append(r.strip())
        if r.strip() == "PRONTO":
            pid_servizio()
            return p, righe


def apri(riga=b"comandi\n"):
    adb("forward", "tcp:%d" % PORTA, "localabstract:yasde")
    s = socket.create_connection(("127.0.0.1", PORTA), timeout=5)
    s.settimeout(5)
    s.sendall(riga)
    return s


def msg(s, tipo, corpo=b""):
    s.sendall(struct.pack(">BI", tipo, len(corpo)) + corpo)


def leggi(s):
    h = b""
    while len(h) < 5:
        c = s.recv(5 - len(h))
        if not c:
            return None
        h += c
    t, n = struct.unpack(">BI", h)
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            return None
        b += c
    return t, b


def prova_saluto():
    p, righe = lancia(False)
    print("log avvio:", righe)
    s = apri()
    msg(s, 1, struct.pack(">H", 1))
    t, b = leggi(s)
    ver, api, w, h, dpi = struct.unpack(">5H", b[:10])
    n = struct.unpack(">H", b[10:12])[0]
    controlla(n > 0 and len(b) == 12 + n, "modello presente nel SALUTO")
    # il modello del telefono non si stampa (dato personale)
    print("saluto: versione", ver, "api", api, "misure e densita' presenti:", w > 0 and h > 0 and dpi > 0)
    controlla(t == 2 and ver == 1 and w > 0 and h > 0 and dpi > 0, "SALUTO -> OK con misure")
    msg(s, 0x7e)
    t, b = leggi(s)
    controlla(t == 3 and b[2:] == b"tipo sconosciuto", "tipo sconosciuto -> ERRORE")
    msg(s, 1, struct.pack(">H", 9))
    t, b = leggi(s)
    controlla(t == 3, "versione errata -> ERRORE")
    s2 = apri()
    controlla(s2.recv(1) == b"", "secondo canale comandi chiuso")
    s3 = apri(b"boh\n")
    controlla(s3.recv(1) == b"", "canale sconosciuto chiuso")
    s4 = apri(b"x" * 100 + b"\n")
    controlla(s4.recv(1) == b"", "riga troppo lunga chiusa")
    msg(s, 4)
    t, b = leggi(s)
    controlla(t == 2, "CHIUDI -> OK")
    controlla(attendi(lambda: p.poll() is not None, 10), "servizio terminato")
    controlla(pid_servizio() is None, "nessun processo rimasto")
    controlla(not resti(), "jar e file del guardiano spariti")


def prova_morte(come, scrittura_fallita=False):
    p, righe = lancia(True, "-Dyasde.scritturaFallita=true " if scrittura_fallita else "")
    print("log avvio:", righe)
    pid = PIDS[-1] if PIDS else None
    controlla(pid is not None and attendi(lambda: guardiano_pid(pid) is not None, 10), "guardiano attivo")
    esiti = [r for r in righe if r.startswith("prova imposta")]
    print("esiti imposta:", esiti)
    atteso = "false" if scrittura_fallita else "true"
    controlla(esiti == ["prova imposta secondo=" + atteso, "prova imposta primo=" + atteso],
              "imposta ritorna %s" % atteso)
    print("file durante:", resti())
    if come == "uccisione":
        uccidi_servizio()
    else:
        p.kill()  # il client adb muore: la shell del PC si chiude di colpo
    controlla(attendi(lambda: pid_servizio() is None, 10), "servizio morto")
    attendi(lambda: not resti(), 20)  # il guardiano lavora dopo la morte del servizio
    ris = sh("cat %s" % SEGNAPOSTO).split()
    if scrittura_fallita:
        controlla(ris == [], "voci non registrate: nessun ripristino eseguito (visto: %s)" % ris)
    else:
        controlla(ris == ["a", "b"], "ripristini eseguiti in ordine (visto: %s)" % ris)
    controlla(not resti(), "jar e file del guardiano spariti (rimasto: %s)" % resti())


def prova_avvio():
    """Servizio ucciso nel mezzo dell'avvio del guardiano (finestra allargata a 3 s da
    -Dyasde.ritardoCustode, tra i due passi: guardiano e file dell'elenco). Si uccide appena
    il primo passo e' visibile, quale che sia l'ordine dei passi: SOLO il processo del guardiano
    o il suo file elenco (non il jar, che c'e' gia' prima dell'avvio)."""
    lancia_adb("-Dyasde.ritardoCustode=3000 ", False)
    controlla(attendi(lambda: pid_servizio() is not None, 10, 0.05), "servizio partito")
    pid = PIDS[-1] if PIDS else None
    if pid is None:
        return
    def primo_passo():
        return guardiano_pid(pid) is not None or os.path.basename(file_guardiano(pid)[0]) in sh("ls " + DIR).split()
    controlla(attendi(primo_passo, 10, 0.05), "primo passo del guardiano visibile (processo o file elenco)")
    t0 = time.time()
    uccidi_servizio()
    controlla(time.time() - t0 < 2.5, "ucciso dentro la finestra di 3 s")
    controlla(attendi(lambda: pid_servizio() is None, 10), "servizio morto")
    attendi(lambda: not resti(), 20)
    controlla(not resti(), "servizio ucciso a meta' avvio: nessun file resta (rimasto: %s)" % resti())
    controlla(guardiano_pid(pid) is None, "nessun guardiano rimasto")


def prova_vuoto():
    """Elenco vuoto (nessun pezzo registra nulla) e servizio ucciso: nessun file deve restare."""
    p, righe = lancia(False)
    pid = PIDS[-1] if PIDS else None
    controlla(pid is not None and attendi(lambda: guardiano_pid(pid) is not None, 10), "guardiano attivo")
    controlla(bool(resti()), "file del guardiano presenti durante")
    uccidi_servizio()
    attendi(lambda: not resti(), 20)
    controlla(not resti(), "elenco vuoto + kill -9: nessun file resta (rimasto: %s)" % resti())


def prova_resti():
    """Un servizio che parte trova resti di una sessione morta (file finti, pid 999999 inesistente): esegue
    il ripristino pendente UNA volta, cancella script, accessorio e jar vecchio, non tocca altri nomi."""
    sh("rm -f " + SEGNAPOSTO)
    contenuto = ("# ripristini di YASDE\necho $$ > \"$0.pid\"\n"
                 "T=; command -v timeout >/dev/null 2>&1 && T='timeout 20'\n"
                 "# prova-resti\n$T sh -c 'echo ripristinato >> %s'\n" % SEGNAPOSTO)
    locale = os.path.join(CACHE, "resto-finto.sh")
    with open(locale, "w") as fh:
        fh.write(contenuto)
    try:
        adb("push", locale, RESTO_SCRIPT)
    finally:
        os.remove(locale)
    sh("echo 1 > %s; echo x > %s; echo x > %s" % (RESTO_PID, RESTO_JAR, ESTRANEO_FILE))
    controlla(all(os.path.basename(f) in sh("ls " + DIR).split() for f in (RESTO_SCRIPT, RESTO_PID, RESTO_JAR, ESTRANEO_FILE)),
              "resti finti creati")
    p, righe = lancia(False)
    print("log avvio:", [r for r in righe if r.startswith("resti:")])
    nomi = sh("ls " + DIR).split()
    for f in (RESTO_SCRIPT, RESTO_PID, RESTO_JAR, RESTO_SCRIPT + ".tentativi"):
        controlla(os.path.basename(f) not in nomi, "resto tolto: " + os.path.basename(f))
    controlla(os.path.basename(ESTRANEO_FILE) in nomi, "file con altro nome non toccato")
    ris = sh("cat " + SEGNAPOSTO).split()
    controlla(ris == ["ripristinato"], "ripristino pendente eseguito una volta (visto: %s)" % ris)


def prova_blocco(uccidi):
    """Voce bloccata con un nipote che timeout non raggiunge. uccidi=False: chiusura in ordine
    (CHIUDI); True: kill -9 del servizio, lavora il guardiano. Uno sleep estraneo (mio, col suo
    pid annotato) non deve morire finche' non lo uccido io."""
    sh("touch " + BLOCCA)
    e = subprocess.Popen(["adb", "shell", "sleep 77777"])
    FIGLI.append(e)
    def trova_estraneo():
        for p, _, a in processi():
            if a.startswith("sleep 77777") and p not in ESTRANEO:
                ESTRANEO.append(p)
                return True
        return False
    controlla(attendi(trova_estraneo, 10), "sleep estraneo partito")
    p, righe = lancia(True)
    pid = PIDS[-1] if PIDS else None
    if uccidi:
        controlla(pid is not None and attendi(lambda: guardiano_pid(pid) is not None, 10), "guardiano attivo")
        uccidi_servizio()
    else:
        s = apri()
        msg(s, 4)
        controlla(leggi(s)[0] == 2, "CHIUDI -> OK")
        t0 = time.time()
        attendi(lambda: annota() and p.poll() is not None, 40, 0.5)
        controlla(p.poll() is not None, "servizio terminato (dopo %.0f s)" % (time.time() - t0))
        controlla(bool(resti()), "il servizio non ha cancellato elenco e jar")
    attendi(lambda: not resti(), 80, 2)
    ris = sh("cat %s" % SEGNAPOSTO).split()
    controlla(ris[-3:] == ["a", "b", "n"], "il ripristino normale e' stato eseguito (visto: %s)" % ris)
    controlla(not resti(), "jar e file del guardiano spariti (rimasto: %s)" % resti())
    controlla(pid_servizio() is None and pid is not None and guardiano_pid(pid) is None,
              "nessun servizio ne' guardiano rimasto")
    controlla(not any(a.startswith("sleep 100000") for _, _, a in processi()),
              "nessuno sleep dei ripristini rimasto")
    controlla(sleep_estraneo_vivo(), "lo sleep estraneo e' sopravvissuto")
    uccidi_estraneo()
    controlla(attendi(lambda: e.poll() is not None, 10), "sleep estraneo ucciso da me alla fine")


def pulizia():
    """Gira sempre. Uccide e cancella SOLO cio' che la prova ha lanciato o scritto (pid e nomi annotati).
    Ordine (conta): 1) si annota; 2) si uccide il MIO servizio, cosi' non puo' piu' avviare ripristini
    (se prima si chiudessero i client adb, vedrebbe lo stdin chiuso e partirebbe la chiusura ordinata,
    con una sessione nuova che non faremmo in tempo ad annotare); 3) si annota di nuovo, comprese le
    esecuzioni dell'elenco fatte dal servizio; 4) solo dopo: sessioni, guardiano, client adb, file."""
    annota()
    for pid in PIDS:
        uccidi_pid(pid, "it.yasde.Servizio")
    annota()
    ps = processi()
    for pid in PIDS:
        esatta = "sh " + DIR + "yasde-custode-%d.sh" % pid
        for p, _, a in ps:
            if a == esatta:
                annota_sessione(p)
    uccidi_sessioni()
    for pid in PIDS:
        g = guardiano_pid(pid)
        if g:
            uccidi_pid(g, "yasde-custode-%d.sh" % pid)
    for f in FIGLI:
        try:
            f.kill()
        except OSError:
            pass
    uccidi_estraneo()
    files = [JAR, JARP, SEGNAPOSTO, BLOCCA, RESTO_SCRIPT, RESTO_PID, RESTO_SCRIPT + ".tmp", RESTO_SCRIPT + ".tentativi", RESTO_JAR, ESTRANEO_FILE] + [f for p in PIDS for f in file_guardiano(p)]
    sh("rm -f " + " ".join(files))
    adb("forward", "--remove", "tcp:%d" % PORTA)


def esegui(modo):
    adb("push", CACHE + "/yasde.jar", JAR, scadenza=60)
    if modo != "saluto":
        adb("push", CACHE + "/yasde-prova-base.jar", JARP, scadenza=60)
    sh("rm -f " + SEGNAPOSTO)
    if modo == "saluto":
        prova_saluto()
    elif modo in ("blocco", "bloccokill"):
        prova_blocco(modo == "bloccokill")
    elif modo == "vuoto":
        prova_vuoto()
    elif modo == "resti":
        prova_resti()
    elif modo == "avvio":
        prova_avvio()
    elif modo == "scrittura":
        prova_morte("uccisione", True)
    else:
        prova_morte(modo)


if __name__ == "__main__":
    modo = sys.argv[1]
    turno_mio = False
    if os.environ.get("YASDE_TURNO_MIO") == "1":
        # il turno l'ha preso il chiamante: deve esistere davvero
        if not os.path.isdir(TURNO):
            raise SystemExit("YASDE_TURNO_MIO=1 ma il turno non esiste: mi fermo")
    else:
        try:
            os.mkdir(TURNO)
            turno_mio = True
        except OSError:
            raise SystemExit("telefono occupato (turno preso da un altro): mi fermo")
    rc = 1
    try:
        if adb("get-state").strip() != "device":
            raise SystemExit("telefono non collegato: mi fermo")
        if "Awake" not in sh("dumpsys power | grep mWakefulness="):
            raise SystemExit("telefono non Awake: mi fermo (mai risvegliarlo)")
        if not sola_lettura_libero():
            raise SystemExit("sul telefono c'e' gia' un it.yasde, un yasde-custode-* o uno sleep 77777: mi fermo")
        try:
            esegui(modo)
        finally:
            pulizia()
        print("falliti:", falliti)
        rc = 1 if falliti else 0
    finally:
        if turno_mio:
            os.rmdir(TURNO)
    sys.exit(rc)
