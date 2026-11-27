#!/usr/bin/env python3
"""Prova sul telefono vero del «telefono sveglio» (TieniSveglio + NascostePannello.prendiSveglio + AssenzaPc): per tutto
il tempo in cui il servizio e' vivo (dal SALUTO) il telefono non si addormenta ne' si blocca per inattivita'; alla
chiusura, alla morte (kill -9) o all'assenza del PC il wake lock sparisce. Nessun dato personale nei file.

AVVERTENZA PER L'UTENTE (la stampa anche all'avvio):
  - NON toccare il telefono durante la prova e non usarlo: serve il telefono LIBERO, sveglio e sbloccato.
  - Durata: gruppo 1 = timeout di spegnimento dell'utente + 20 s, piu' 3-4 minuti; gruppi 2 e 3 circa 2 minuti.
  - Lo schermo restera' acceso oltre il solito; il pannello si spegne e si riaccende un paio di volte.
  - ALLA FINE DI OGNI GRUPPO IL TELEFONO SARA' BLOCCATO (o spento): e' il comportamento giusto, perche' il wake lock
    viene rilasciato e Android, se l'ultimo tocco e' piu' vecchio del tempo di spegnimento, spegne e blocca SUBITO
    (il wake lock e' preso senza ON_AFTER_RELEASE, scelta voluta). Con il wireless debugging di Samsung il blocco fa
    cadere il collegamento: lo script lo dice e elenca i file che potrebbero essere rimasti. Poi sbloccare a mano.

Opzione `prova_sveglio.py 1 --b-lunga`: fa la prova B anche con timeout dell'utente lungo (anche 30 minuti): stampa durata (timeout + 20 s)
e ora prevista di fine, NON cambia mai l'impostazione del telefono, e durante B manda PRESENZA (0x05) ogni 4 minuti come il PC vero
(senza, a 30:00 scatterebbe l'assenza del PC e B proverebbe la cosa sbagliata). Senza l'opzione, con timeout > 150 s, B e' saltata.
Uscite: 0 tutto ok, 1 prove fallite, 3 non si parte, 4 collegamento adb caduto o inaffidabile (verifiche non fatte), 5 telefono non piu'
sveglio e sbloccato (fermata), 6 prova B non valida (telefono toccato durante l'attesa o tocco illeggibile: da ripetere).
SIGHUP e SIGTERM fanno uscire lo script in modo pulito (pulizia e rilascio del turno); un secondo Ctrl+C durante la pulizia puo'
lasciare il turno preso.

Si lancia UN GRUPPO alla volta (`prova_sveglio.py 1`, poi `2`, poi `3`, sbloccando il telefono tra uno e l'altro):
dopo un rilascio che addormenta il telefono non si puo' proseguire (mai risvegliare o sbloccare dallo script).
  Gruppo 1: A wake lock presente dopo il SALUTO; C pannello spento/riacceso con il lock che resta; B nessun input
            oltre il timeout dell'utente (schermo ON, sveglio, non bloccato, lock preso: il caso segnalato); D CHIUDI
            (per ULTIMA: il rilascio addormenta il telefono) con lock sparito e impostazioni identiche.
  Gruppo 2: F assenza del PC con limite accorciato a 20 s (solo prove): evento motivo 6, pannello acceso, lock
            sparito, impostazioni di partenza; se il telefono e' ancora sveglio e sbloccato, un messaggio fa tornare
            il lock e il timeout al massimo.
  Gruppo 3: E kill -9 del servizio: lock sparito da solo, guardiano ripulito, impostazioni identiche.
Ordine e perche': B va prima di ogni rilascio (dopo B ogni rilascio addormenta); D, F ed E sono ognuna in un gruppo a
parte come ULTIMO atto, perche' dopo il rilascio il telefono puo' addormentarsi e bloccarsi e il collegamento cadere.

Preparazione (come prova_pannello.py): jar completo (contratto + base + video + pannello) in
~/.cache/yasde-sviluppo/pannello/yasde-completo.jar e jar di prova (ProvaPannello, fuori dal jar di
produzione) in ~/.cache/yasde-sviluppo/pannello/yasde-prova-pannello.jar, entrambi presi dal server con
`strumenti/prova.sh telefono-pannello`. Richiede adb.

Regole rispettate: turno del telefono preso con mkdir e tolto SOLO se la nostra mkdir e' riuscita; prima di toccare
qualcosa, in sola lettura, esce se c'e' gia' un it.yasde o un yasde-custode-*, o se il telefono non e'
sveglio, con lo schermo acceso e sbloccato (stato di blocco illeggibile = non si parte); PRIMA DI OGNI PROVA lo stato
«sveglio e sbloccato» si ricontrolla e, se non lo e', lo script si ferma con un messaggio dopo la pulizia possibile;
non scrive MAI impostazioni dell'utente (timeout e frequenza si leggono e si confrontano; le scrive solo il servizio di
prova con il suo guardiano, che e' cio' che si prova); niente suoni; ogni comando adb ha una scadenza; la pulizia gira
in `finally`, uccide solo il pid annotato e toglie solo i file annotati; se il collegamento adb cade lo dice e
elenca i nomi dei file possibili resti, senza ritentare all'infinito.
NON provati: blocco a mano col tasto, chiamata in arrivo, risparmio energetico, notte intera.
"""
import os, queue, re, signal, struct, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
GRUPPO = sys.argv[1] if len(sys.argv) > 1 else ""
B_LUNGA = "--b-lunga" in sys.argv[2:]  # solo gruppo 1: la prova B anche con timeout lungo (anche 30 minuti); non cambia nessuna impostazione
sys.argv = [sys.argv[0]]  # prova_pannello legge sys.argv: qui non servono altri argomenti
import prova_pannello as pp  # noqa: E402  (le sue funzioni di appoggio; la sua prova() NON parte)

ETICHETTA = "yasde-sveglio"
falliti = 0
NON_VALIDA = False  # B fatta ma il telefono e' stato toccato: ne' ok ne' fallita, da ripetere (uscita 6)
CADUTO = False  # vero se il collegamento adb e' caduto: una prova con verifiche non fatte non puo' uscire 0
FASE = "avvio"  # l'ultima prova iniziata (per dire quali verifiche non sono state fatte)
RESTI = [pp.JAR, pp.JARP, pp.CHIAMATA, pp.EXTRA, pp.GUASTO, pp.FREQ, "/data/local/tmp/pn-rete.pid"]  # i nostri file sul telefono


class AdbCaduto(Exception):
    """Un comando adb e' scaduto o ha dato errore di collegamento: una lettura vuota NON vale come «niente trovato»."""


def adb(*a):
    try:
        r = subprocess.run(["adb"] + list(a), capture_output=True, text=True, timeout=15)
    except subprocess.TimeoutExpired:
        raise AdbCaduto("comando adb scaduto: adb " + " ".join(a)[:80])
    if r.returncode == 255 or r.stderr.lstrip().startswith("error:"):
        raise AdbCaduto("errore di collegamento adb (%d): %s" % (r.returncode, r.stderr.strip()[:120]))
    return r.stdout


pp.adb = adb  # le funzioni di prova_pannello usano questo: cosi' ogni comando adb ha la scadenza


def adb_vivo():
    """Il collegamento c'e' ancora? (sul Samsung il blocco fa cadere il wireless debugging)"""
    try:
        r = subprocess.run(["adb", "get-state"], capture_output=True, text=True, timeout=5)
        return r.stdout.strip() == "device"
    except subprocess.TimeoutExpired:
        return False


def controlla(ok, nome, extra=""):
    global falliti
    print(("ok      " if ok else "FALLITA ") + nome + (("  [" + extra + "]") if extra else ""), flush=True)
    if not ok:
        falliti += 1


def info(frase):
    print("info    " + frase, flush=True)


def wake_lock():
    """Righe del nostro wake lock nella sola sezione «Wake Locks: size=» di `dumpsys power` (non lo storico)."""
    trovate = []
    dentro = False
    for r in pp.sh("dumpsys power").splitlines():
        if re.match(r"^\s*Wake Locks: size=", r):
            dentro = True
            continue
        if dentro:
            if not r.strip() or not r.startswith("  "):
                break  # fine della sezione (riga vuota o intestazione successiva)
            if ETICHETTA in r and "SCREEN" in r:
                trovate.append(r.strip())
    return trovate


def wakefulness():
    m = re.search(r"mWakefulness=(\w+)", pp.sh("dumpsys power | grep -m1 mWakefulness="))
    return m.group(1) if m else "?"


def bloccato():
    """true/false dal criterio del sistema; None se non si riesce a leggerlo."""
    m = re.search(r"(?:isStatusBarKeyguard|showing)=(true|false)", pp.sh("dumpsys window policy | grep -E 'showing=|isStatusBarKeyguard='"))
    return None if not m else m.group(1) == "true"


def sveglio_e_sbloccato():
    """Sola lettura. None se va bene, altrimenti la frase del motivo."""
    if not adb_vivo():
        return "collegamento adb caduto"
    if wakefulness() != "Awake" or pp.stato_android() != "ON":
        return "il telefono non e' sveglio con lo schermo acceso (%s / %s)" % (wakefulness(), pp.stato_android())
    if bloccato() is not False:
        return "il telefono e' bloccato o lo stato di blocco non si legge (%s)" % bloccato()
    return None


def pronto_a_partire():
    """Sola lettura. Ritorna la frase del motivo per cui NON si parte, o None."""
    if pp.sh("ps -A -o PID,ARGS | grep -E 'it\\.yasde' | grep -v grep").strip():
        return "sul telefono c'e' gia' un it.yasde (un'altra prova o YASDE vero)"
    if pp.sh("ls /data/local/tmp 2>/dev/null | grep -E '^yasde-custode-'").strip():
        return "sul telefono c'e' gia' un file yasde-custode-*"
    return sveglio_e_sbloccato()


class Fermata(Exception):
    """La prova si ferma con un messaggio chiaro (telefono non piu' sveglio e sbloccato, collegamento caduto)."""


def prima_di(nome):
    """Ricontrollo in sola lettura PRIMA di ogni prova: se il telefono non e' sveglio e sbloccato ci si ferma."""
    global FASE
    FASE = nome
    m = sveglio_e_sbloccato()
    if m:
        raise Fermata("prima della prova %s: %s. Mi fermo senza risvegliare ne' sbloccare nulla: le prove seguenti NON sono state fatte" % (nome, m))


def rete_avvia(secondi):
    """Rete di sicurezza: dopo `secondi` riaccende comunque il pannello (stesso punto d'ingresso del guardiano)."""
    pp.sh('nohup setsid sh -c "sleep %d; CLASSPATH=%s app_process / it.yasde.Pannello accendi" '
          '>/dev/null 2>&1 & echo $! > /data/local/tmp/pn-rete.pid' % (secondi, pp.JAR))


def pulizia_prima_del_rilascio():
    """Tutto cio' che non serve piu', PRIMA del passo che puo' addormentare il telefono (poi il collegamento puo' cadere).
    Resta solo il jar del servizio (lo toglie il guardiano); il jar di prova si toglie ora: il processo l'ha gia' caricato."""
    pp.rete_ferma()
    pp.sh("rm -f %s %s %s %s %s" % (pp.CHIAMATA, pp.EXTRA, pp.GUASTO, pp.FREQ, pp.JARP))


def proteggi(c):
    """Se il collegamento adb cade, un errore del canale comandi (OSError, nessuna risposta) diventa AdbCaduto (uscita 4), non un traceback."""
    originale = c.chiedi

    def chiedi(tipo, corpo=b"", sec=10):
        try:
            return originale(tipo, corpo, sec)
        except (OSError, queue.Empty):
            if not adb_vivo():
                raise AdbCaduto("canale comandi caduto insieme al collegamento adb")
            raise

    c.chiedi = chiedi
    return c


def attivita_utente():
    """mLastUserActivityTime assoluto da `dumpsys power` (il numero prima della parentesi), o None se non si legge."""
    m = re.search(r"mLastUserActivityTime=(\d+)", pp.sh("dumpsys power | grep -m1 'mLastUserActivityTime='"))
    return int(m.group(1)) if m else None


def avvia_servizio(limite_ms=None):
    pp.push_jar()
    pp.sh("rm -f %s %s %s %s" % (pp.CHIAMATA, pp.EXTRA, pp.GUASTO, pp.FREQ))
    servizio = pp.lancia(limite_ms)
    pid = pp.pid_servizio()
    c = proteggi(pp.Comandi())
    t, b = c.chiedi(1, struct.pack(">H", 3))  # il SALUTO e' il primo segno di vita: senza, il lock non si prende
    return servizio, pid, c, t == 2


def elenca_resti(rimaste):
    """Collegamento caduto: dice cosa non e' stato verificato e quali dei MIEI file potrebbero essere rimasti."""
    print("ATTENZIONE: il collegamento adb e' caduto o inaffidabile (di solito il blocco del telefono fa cadere il wireless debugging).")
    print("Le verifiche rimaste (%s) NON sono state fatte. File che potrebbero essere rimasti sul telefono (solo i miei):" % rimaste)
    for f in RESTI:
        print("   " + f)
    print("Dopo aver sbloccato il telefono e riattivato adb: controllare quei nomi (il jar del servizio lo toglie il guardiano).")


def verifiche_finali_possibili(gruppo_rimasto):
    """Dopo l'ultimo atto: se il collegamento e' caduto lo dice, segna CADUTO (uscita 4) e ritorna False."""
    global CADUTO
    if adb_vivo():
        return True
    CADUTO = True
    elenca_resti(gruppo_rimasto)
    return False


def gruppo1(stato):
    """A, C, B, D. Ritorna (servizio, pid, originali)."""
    originali = stato["originali"]
    prima_di("A")
    servizio, pid, c, salutato = avvia_servizio()
    stato["servizio"], stato["pid"], stato["c"] = servizio, pid, c
    controlla(not any("API MANCANTE" in r and "sveglio" in r for r in pp.LOG), "A API del telefono sveglio presenti",
              "; ".join(r for r in pp.LOG if "sveglio" in r)[:200])
    controlla(salutato, "A SALUTO (da qui il wake lock si prende: primo segno di vita)")
    pp.aspetta(lambda: len(wake_lock()) == 1, 10)
    w = wake_lock()
    controlla(len(w) == 1 and "uid=2000" in w[0].replace(" ", ""), "A wake lock dello schermo «%s» preso, uid 2000" % ETICHETTA, "; ".join(w)[:200])
    controlla(len(w) == 1 and "ACQ" in w[0] and "CAUSES_WAKEUP" not in w[0], "A il wake lock non sveglia lo schermo")

    prima_di("C")
    t, b = pp.imposta_pannello(c, 0)
    controlla(t == 2, "C PANNELLO_IMPOSTA 0", b[2:].decode("utf-8", "replace"))
    controlla(pp.potenza() == "OFF" and pp.stato_android() == "ON", "C pannello spento dal compositore, Android lo crede acceso")
    controlla(len(wake_lock()) == 1, "C wake lock ancora preso a pannello spento")
    t, b = pp.imposta_pannello(c, 1)
    controlla(t == 2, "C PANNELLO_IMPOSTA 1")
    controlla(pp.aspetta(lambda: pp.potenza() == "ON", 5), "C pannello riacceso")
    controlla(pp.timeout() == originali[0], "C timeout dell'utente rimesso", pp.timeout())
    controlla(len(wake_lock()) == 1, "C wake lock ancora preso dopo la riaccensione (il telefono resta sveglio anche col timeout dell'utente)")

    prima_di("B")
    o = originali[0]
    max_ok = o.isdigit() and int(o) != int(pp.MASSIMO)
    if max_ok and (int(o) <= 150000 or B_LUNGA):
        attesa = int(o) / 1000 + 20
        print("B attendo %.0f s senza alcun input (timeout dell'utente %s ms): fine prevista alle %s. NON toccare il telefono..."
              % (attesa, o, time.strftime("%H:%M:%S", time.localtime(time.time() + attesa))), flush=True)
        attivita0 = attivita_utente()  # l'ultimo tocco dell'utente all'inizio: se alla fine e' cambiato, B non dimostra nulla
        fine = time.time() + attesa
        prossima_presenza = time.time() + 240  # come il PC vero: PRESENZA (0x05) almeno ogni 5 minuti (qui 4), altrimenti a 30:00 scatta l'assenza
        guasti = []
        while time.time() < fine:
            time.sleep(5)
            if time.time() >= prossima_presenza:
                t, b = c.chiedi(5)
                if t != 2:
                    guasti.append("PRESENZA senza OK (%s)" % t)
                prossima_presenza = time.time() + 240
            if pp.stato_android() != "ON" or wakefulness() != "Awake" or bloccato() is not False or not wake_lock():
                guasti.append("%s/%s/blocco=%s/lock=%d" % (pp.stato_android(), wakefulness(), bloccato(), len(wake_lock())))
        attivita1 = attivita_utente()
        if attivita0 is None or attivita1 is None:
            global NON_VALIDA
            NON_VALIDA = True
            print("B NON VALIDA: mLastUserActivityTime non si legge da `dumpsys power`: non si sa se il telefono e' stato toccato. B da ripetere.")
        elif attivita0 != attivita1:
            NON_VALIDA = True
            print("B NON VALIDA: telefono toccato durante l'attesa (mLastUserActivityTime cambiato), B da ripetere. Non e' ne' ok ne' fallita.")
        else:
            controlla(not guasti, "B oltre il timeout dell'utente: schermo di Android acceso, sveglio, sbloccato, wake lock preso (nessun tocco)",
                      "; ".join(guasti[:3]))
        assente = False
        while not c.eventi.empty():
            t, b = c.eventi.get()
            if t == 0x6e and len(b) >= 2 and b[1] == 6:
                assente = True
        controlla(not assente, "B nessun evento di PC assente (le PRESENZA bastano)")
        controlla(pp.timeout() == o, "B il timeout dell'utente non e' stato toccato", pp.timeout())
    else:
        info("B saltata: timeout dell'utente %s troppo lungo (o gia' al massimo) per l'attesa; con `prova_sveglio.py 1 --b-lunga` "
             "si fa lo stesso (durata = timeout + 20 s; il telefono non viene modificato)" % o)

    # D, per ULTIMA: il rilascio addormenta il telefono se l'ultimo tocco e' piu' vecchio del timeout (atteso)
    prima_di("D")
    pulizia_prima_del_rilascio()
    stato["pulito"] = True
    t, b = c.chiedi(4)
    controlla(t == 2, "D CHIUDI")
    if not verifiche_finali_possibili("D: lock sparito, servizio terminato, impostazioni identiche"):
        return
    controlla(pp.aspetta(lambda: not wake_lock(), 10), "D wake lock rilasciato alla chiusura", "; ".join(wake_lock())[:200])
    controlla(pp.aspetta(lambda: pp.pid_servizio() is None, 10), "D servizio terminato")
    if pp.pid_servizio() is None:
        stato["pid"] = None  # finito: quel numero potrebbe essere riusato da Android, non si uccide piu'
    controlla((pp.timeout(), pp.minimo()) == originali, "D timeout e frequenza identici a prima", "%s / %s" % (pp.timeout(), pp.minimo()))
    w = wakefulness()
    if w == "Awake":
        info("D dopo il rilascio il telefono e' rimasto sveglio: l'ultimo tocco e' piu' recente del timeout dell'utente")
    else:
        info("D dopo il rilascio il telefono si e' addormentato (%s): ATTESO, l'ultimo tocco e' piu' vecchio del timeout" % w)


def gruppo2(stato):
    """F: assenza del PC con il limite accorciato a 20 s (solo prove: puo' solo accorciare i 30 minuti)."""
    originali = stato["originali"]
    prima_di("F")
    servizio, pid, c, salutato = avvia_servizio(20000)
    stato["servizio"], stato["pid"], stato["c"] = servizio, pid, c
    controlla(pid is not None, "F servizio avviato con limite di 20 s", str(pid))
    controlla(salutato, "F SALUTO")
    controlla(pp.aspetta(lambda: len(wake_lock()) == 1, 10), "F wake lock preso")
    t, b = pp.imposta_pannello(c, 0)
    controlla(t == 2 and pp.potenza() == "OFF", "F pannello spento, timeout al massimo", pp.timeout())
    c.svuota()
    # la rete di sicurezza (rete_avvia) resta attiva durante il silenzio: e' giusto, protegge il pannello spento se qualcosa va storto;
    # si toglie solo in pulizia_prima_del_rilascio, a pannello gia' riacceso
    print("F silenzio (nessun messaggio) per 20 s e un po' di piu': NON toccare il telefono...", flush=True)
    ev = None
    fine = time.time() + 45
    while time.time() < fine and ev != bytes([1, 6]):
        ev = c.evento(0x6e, 2)
    controlla(ev == bytes([1, 6]), "F dopo il silenzio arriva PANNELLO_EVENTO acceso, motivo 6 (PC assente)", repr(ev))
    # da qui il wake lock e' (o sta per essere) rilasciato: il telefono puo' addormentarsi e il collegamento cadere
    if not verifiche_finali_possibili("F: pannello acceso, lock sparito, impostazioni di partenza, ripresa"):
        return
    controlla(pp.aspetta(lambda: pp.potenza() == "ON", 5), "F pannello acceso")
    controlla(pp.aspetta(lambda: not wake_lock(), 10), "F wake lock rilasciato", "; ".join(wake_lock())[:200])
    controlla((pp.timeout(), pp.minimo()) == originali, "F impostazioni di partenza rimesse", "%s / %s" % (pp.timeout(), pp.minimo()))
    motivo = sveglio_e_sbloccato()
    if motivo:
        info("F ripresa non verificabile: dopo il rilascio il telefono si e' addormentato/bloccato (ATTESO, l'ultimo tocco e' piu' vecchio "
             "del timeout): " + motivo)
    else:
        t, b = c.chiedi(0x61)  # un messaggio qualunque del PC e' un segno di vita
        controlla(t == 2, "F messaggio dal PC")
        controlla(pp.aspetta(lambda: len(wake_lock()) == 1, 10), "F il wake lock torna al primo segno di vita")
        controlla(pp.aspetta(lambda: pp.timeout() == pp.MASSIMO, 10), "F timeout di nuovo al massimo (ripresa)", pp.timeout())
        controlla(pp.potenza() == "ON", "F il pannello resta acceso finche' il PC non lo spegne")
        pulizia_prima_del_rilascio()
        stato["pulito"] = True
        t, b = c.chiedi(4)
        controlla(t == 2, "F CHIUDI")
        if not verifiche_finali_possibili("F: servizio terminato, impostazioni"):
            return
        controlla(pp.aspetta(lambda: pp.pid_servizio() is None, 10), "F servizio terminato")
        if pp.pid_servizio() is None:
            stato["pid"] = None
        controlla((pp.timeout(), pp.minimo()) == originali, "F impostazioni di partenza dopo la chiusura", "%s / %s" % (pp.timeout(), pp.minimo()))


def gruppo3(stato):
    """E: kill -9 del servizio."""
    originali = stato["originali"]
    prima_di("E")
    servizio, pid, c, salutato = avvia_servizio()
    stato["servizio"], stato["pid"], stato["c"] = servizio, pid, c
    controlla(pid is not None, "E servizio avviato", str(pid))
    controlla(salutato, "E SALUTO")
    controlla(pp.aspetta(lambda: len(wake_lock()) == 1, 10), "E wake lock preso")
    pulizia_prima_del_rilascio()
    stato["pulito"] = True
    if pid:
        pp.sh("kill -9 %s" % pid)
    stato["pid"] = None
    if not verifiche_finali_possibili("E: lock sparito da solo, guardiano ripulito, impostazioni identiche"):
        return
    controlla(pp.aspetta(lambda: not wake_lock(), 15), "E dopo kill -9 il wake lock sparisce da solo", "; ".join(wake_lock())[:200])
    controlla(pp.aspetta(lambda: not pp.custode(), 20), "E il guardiano ha finito e ripulito", pp.custode()[:200])
    controlla((pp.timeout(), pp.minimo()) == originali, "E timeout e frequenza identici a prima", "%s / %s" % (pp.timeout(), pp.minimo()))
    w = wakefulness()
    info("E dopo il rilascio il telefono e' %s (%s)" % (w, "tocco recente" if w == "Awake" else "atteso: tocco piu' vecchio del timeout"))


def prova():
    global falliti
    if GRUPPO not in ("1", "2", "3"):
        raise SystemExit("uso: prova_sveglio.py 1|2|3   (vedi la descrizione in testa al file; un gruppo alla volta)")
    def esci_pulito(numero, frame):
        raise SystemExit(128 + numero)  # cosi' il finally (pulizia e rilascio del turno) gira anche con terminale chiuso o kill

    signal.signal(signal.SIGHUP, esci_pulito)
    signal.signal(signal.SIGTERM, esci_pulito)
    print("AVVERTENZA: un secondo Ctrl+C durante la pulizia puo' lasciare il turno del telefono preso "
          "(cartella ~/.cache/yasde-sviluppo/telefono.occupato): in quel caso toglierla a mano SOLO dopo aver controllato che nessuno usi il telefono.", flush=True)
    print("AVVERTENZA: non toccare il telefono durante la prova. Durata: timeout dell'utente + 20 s piu' 3-4 minuti (gruppo 1), "
          "circa 2 minuti (gruppi 2 e 3). Lo schermo restera' acceso oltre il solito, il pannello si spegne e si riaccende un paio di "
          "volte, ALLA FINE IL TELEFONO SARA' BLOCCATO (atteso). Sbloccare a mano e lanciare il gruppo successivo.", flush=True)
    for f in (pp.JARLOCALE, pp.JARPLOCALE):
        if not os.path.exists(f):
            raise SystemExit("manca " + f)
    turno_mio = False
    for _ in range(60):
        try:
            os.mkdir(pp.TURNO)
            turno_mio = True
            break
        except FileExistsError:
            time.sleep(25)
    if not turno_mio:
        raise SystemExit("turno del telefono non ottenuto: esco senza toccare nulla")
    stato = {"servizio": None, "pid": None, "c": None, "pulito": False, "originali": None}
    toccato = False  # vero solo dopo il controllo in sola lettura: se la prova esce prima, non si tocca nulla
    esito = 0
    try:
        try:
            motivo = pronto_a_partire()
        except AdbCaduto as e:
            motivo = "collegamento adb non affidabile, non si puo' controllare lo stato del telefono: %s" % e
        if motivo:
            print("NON si parte: " + motivo)
            sys.exit(3)
        toccato = True
        stato["originali"] = (pp.timeout(), pp.minimo())
        print("valori letti prima della prova (solo lettura): timeout=%s minimo=%s" % stato["originali"])
        o = stato["originali"][0]
        durata = int(o) / 1000 + 20 if o.isdigit() and int(o) != int(pp.MASSIMO) and (int(o) <= 150000 or B_LUNGA) else 0
        if GRUPPO == "1" and B_LUNGA and durata:
            print("B lunga: durata prevista %.0f s (timeout dell'utente + 20 s) piu' il resto; fine prevista alle %s circa. "
                  "Il telefono resta sotto turno per tutto questo tempo." % (durata, time.strftime("%H:%M:%S", time.localtime(time.time() + durata + 240))), flush=True)
        rete_avvia(int(durata + 240))
        {"1": gruppo1, "2": gruppo2, "3": gruppo3}[GRUPPO](stato)
    except Fermata as e:
        print("FERMATA: " + str(e))
        esito = 5
    except AdbCaduto as e:
        global CADUTO
        CADUTO = True
        print("COLLEGAMENTO ADB CADUTO durante la prova %s: %s" % (FASE, e))
        elenca_resti("dalla prova %s in poi" % FASE)
    except SystemExit:
        raise
    except BaseException:
        print("ECCEZIONE; ultime righe del log del servizio:")
        for r in pp.LOG[-25:]:
            print("   ", r[:300])
        raise
    finally:
        # pulizia: solo cio' che e' nostro (il pid annotato, i nostri file, la nostra rete di sicurezza)
        if toccato:
            try:
                if stato["servizio"] is not None and stato["servizio"].poll() is None:
                    stato["servizio"].kill()
                if adb_vivo():
                    # il pid si uccide solo se e' ancora il nostro servizio (stessa riga di comando): Android puo' riusare i numeri
                    if stato["pid"] and pp.pid_servizio() == stato["pid"]:
                        pp.sh("kill -9 %s" % stato["pid"])
                        time.sleep(3)
                    if pp.potenza() != "ON" and wakefulness() == "Awake":
                        pp.push_jar()
                        print("pannello acceso a fine prova:", pp.accendi_comunque())
                    pp.rete_ferma()
                    pp.sh("rm -f %s %s %s %s %s %s" % (pp.CHIAMATA, pp.JAR, pp.JARP, pp.EXTRA, pp.GUASTO, pp.FREQ))
                    if adb_vivo():
                        # addormentato apposta (atteso) = pannello non «ON» ma Android a riposo: non e' un difetto
                        controlla(pp.potenza() == "ON" or wakefulness() != "Awake", "FINE pannello acceso (o telefono addormentato dal sistema)")
                        controlla(not wake_lock(), "FINE nessun wake lock nostro")
                        controlla(pp.aspetta(lambda: pp.resti() == [], 20), "FINE nessun file nostro sul telefono", repr(pp.resti()))
                        controlla(pp.processi() == "", "FINE nessun processo nostro sul telefono", pp.processi())
                        adb("forward", "--remove", "tcp:%d" % pp.PORTA)
                    else:
                        verifiche_finali_possibili("pulizia finale")
                else:
                    verifiche_finali_possibili("pulizia finale")
            except AdbCaduto as e:
                CADUTO = True
                print("COLLEGAMENTO ADB CADUTO durante la pulizia finale: %s" % e)
                elenca_resti("pulizia finale")
        if turno_mio:
            try:
                os.rmdir(pp.TURNO)  # solo se la nostra mkdir e' riuscita (turno_mio)
            except OSError:
                pass
    print("falliti:", falliti)
    # Prima le prove fallite: un collegamento caduto (atteso dopo l'ultimo atto)
    # non deve coprire un difetto vero visto prima.
    sys.exit(1 if falliti else (4 if CADUTO else (esito or (6 if NON_VALIDA else 0))))


if __name__ == "__main__":
    prova()
