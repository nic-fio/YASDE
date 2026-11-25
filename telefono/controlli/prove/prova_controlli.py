#!/usr/bin/env python3
"""Prova integrata del pezzo «controlli» sul telefono vero, attraverso il servizio e il canale comandi.
Nessun dato personale nei file. Uso: prova_controlli.py

Preparazione: il jar di prova (contratto + base + controlli + ProvaControlliTelefono; lo costruisce
`strumenti/prova.sh telefono-controlli`, sul server in
lavoro/telefono-controlli/telefono/controlli/yasde-prova-controlli.jar) copiato in
~/.cache/yasde-sviluppo/controlli/yasde-prova-controlli.jar. Richiede adb.

PRUDENZA (il telefono e' di una persona):
- NESSUN SUONO UDIBILE. «Trova il telefono» si prova per intero (modifiche, guardiano, ripristini, suoneria vera
  fatta partire) ma con due opzioni di SOLA PROVA dichiarate qui: volume della suoneria forzato a 1 invece del
  massimo (VOLUME_TROVA) e suoneria con il volume del lettore a 0 e senza vibrazione (ProvaControlliTelefono
  servizio <volume> le imposta). Il suono vero a volume alto NON e' provato.
- torcia accesa al massimo circa 1 secondo (misurato e stampato);
- turno del telefono: mkdir ~/.cache/yasde-sviluppo/telefono.occupato; se esiste lo script ESCE; toglie
  solo il proprio; telefono Awake e collegato o si esce (mai risvegliare/sbloccare/ricollegare);
- modo della suoneria, non disturbare e volume della suoneria si leggono PRIMA e si rimettono ESATTAMENTE
  (in `finally`, anche se una prova fallisce); a fine prova si stampa il confronto.
"""
import os, queue, re, signal, socket, struct, subprocess, sys, threading, time

CACHE = os.path.expanduser("~/.cache/yasde-sviluppo/controlli")
JARLOCALE = CACHE + "/yasde-prova-controlli.jar"
JAR = "/data/local/tmp/pn-controlli.jar"
TURNO = os.path.expanduser("~/.cache/yasde-sviluppo/telefono.occupato")
PORTA = 38472
VOLUME_TROVA = 1  # opzione di sola prova: volume della suoneria durante «trova» (invece del massimo)
CLASSE = "it.yasde.ProvaControlliTelefono"
falliti = 0
LOG = []
turno_mio = False
toccato = False  # vero solo da quando lancia() ha messo qualcosa sul telefono


def adb(*a, **k):
    """Un comando adb con scadenza (30 s): se scade o adb manca torna "" (le letture diventano LetturaFallita)."""
    k.setdefault("timeout", 30)
    try:
        return subprocess.run(["adb"] + list(a), capture_output=True, text=True, **k).stdout
    except (subprocess.TimeoutExpired, OSError):
        return ""


def sh(c):
    return adb("shell", c)


def controlla(ok, nome, extra=""):
    global falliti
    print(("ok      " if ok else "FALLITA ") + nome + (("  [" + extra + "]") if extra else ""), flush=True)
    if not ok:
        falliti += 1


def aspetta(cond, sec, passo=0.1):
    fine = time.time() + sec
    while time.time() < fine:
        if cond():
            return True
        time.sleep(passo)
    return cond()


# ---- valori del telefono (sola lettura, shell) ----

class LetturaFallita(Exception):
    """Il telefono non risponde (adb caduto o valore illeggibile): mai un valore inventato."""


def leggi_int(comando):
    r = sh(comando).strip().split()
    if not r or not re.fullmatch(r"-?\d+", r[0]):
        raise LetturaFallita(comando)
    return int(r[0])


def modo():
    """Valore di mode_ringer: 0 silenzioso, 1 vibra, 2 suona."""
    return leggi_int("cmd settings get global mode_ringer")


def zen():
    return leggi_int("cmd settings get global zen_mode")


def volume_vivo():
    """Volume letto adesso con media_session: a suoneria silenziata da' SEMPRE 0, quindi NON e' il volume dell'utente."""
    m = re.search(r"volume is (\d+)", sh("cmd media_session volume --stream 2 --get 2>&1"))
    return int(m.group(1)) if m else -1


def salvati():
    """I volumi della suoneria SALVATI (non risentono del silenziamento): una riga per ogni `volume_ring*`."""
    return tuple(sorted(r.strip() for r in sh("cmd settings list system").splitlines() if r.startswith("volume_ring")))


def valori():
    return (modo(), zen(), salvati())


NOME_MODO = {0: "SILENT", 1: "VIBRATE", 2: "NORMAL"}
NOME_ZEN = {0: "all", 1: "priority", 2: "none", 3: "alarms"}


def rimetti(v):
    """Rimette modo e non disturbare. NON scrive MAI un volume: i volumi sono dell'utente (solo `trova` li cambia, e li rimette lui)."""
    m, z, _ = v
    sh("cmd audio set-ringer-mode %s" % NOME_MODO[m])
    sh("cmd notification set_dnd %s" % NOME_ZEN[z])


def custode():
    return sh("cat /data/local/tmp/yasde-custode-*.sh 2>/dev/null")


def leggi_con_fine(comando):
    """Esegue una lettura e pretende la riga di controllo finale: un adb scaduto o caduto da' "" e NON deve
    passare per «nessun risultato». Senza la riga: LetturaFallita."""
    o = sh(comando + "; echo FINE-LETTURA")
    if "FINE-LETTURA" not in o:
        raise LetturaFallita(comando)
    return o.replace("FINE-LETTURA", "")


def file_custode():
    return leggi_con_fine("ls /data/local/tmp | grep '^yasde-custode'").split()


def attendi_guardiano(sec):
    """Dopo un kill -9: aspetta che il servizio non ci sia piu' e che il guardiano abbia finito (cancella i suoi
    file `yasde-custode-*` SOLO dopo aver rimesso i valori). Rimettere prima farebbe trovare al guardiano
    «ancora il nostro valore» e lo farebbe riscrivere sopra quelli dell'utente. Vero se e' finito entro `sec`."""
    def finito():
        try:
            return pid_servizio() is None and not file_custode()
        except LetturaFallita:
            return False  # non si sa: NON e' finito

    return aspetta(finito, sec, 0.5)


def voci_trova():
    return [r for r in custode().splitlines() if "set_dnd" in r or "set-ringer-mode" in r or "volume --stream 2 --set" in r]


# ---- servizio e canale comandi ----

def lancia():
    global toccato
    toccato = True
    LOG.clear()
    adb("push", JARLOCALE, JAR)  # il guardiano cancella il jar alla fine: a ogni avvio si ricopia
    p = subprocess.Popen(["adb", "shell", "CLASSPATH=%s app_process / %s servizio %d" % (JAR, CLASSE, VOLUME_TROVA)],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    coda = queue.Queue()

    def lettore():
        for r2 in p.stdout:
            LOG.append(r2.strip())
            coda.put(r2.strip())
        coda.put(None)

    threading.Thread(target=lettore, daemon=True).start()
    while True:
        try:
            r = coda.get(timeout=30)
        except queue.Empty:
            p.kill()
            raise SystemExit("il servizio non e' arrivato a PRONTO entro 30 s: %r" % LOG)
        if r is None:
            raise SystemExit("servizio terminato prima di PRONTO: %r" % LOG)
        if r == "PRONTO":
            return p


def recv_n(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            return None
        b += c
    return b


class Comandi:
    def __init__(self):
        adb("forward", "tcp:%d" % PORTA, "localabstract:yasde")
        self.s = socket.create_connection(("127.0.0.1", PORTA), timeout=10)
        self.s.sendall(b"comandi\n")
        self.s.settimeout(None)
        self.risp = queue.Queue()
        self.eventi = queue.Queue()
        threading.Thread(target=self.ciclo, daemon=True).start()

    def ciclo(self):
        try:
            while True:
                h = recv_n(self.s, 5)
                if h is None:
                    return
                t, n = struct.unpack(">BI", h)
                b = recv_n(self.s, n)
                if b is None:
                    return
                (self.risp if t in (2, 3) else self.eventi).put((t, b))
        except OSError:
            return

    def chiedi(self, tipo, corpo=b"", sec=40):
        self.s.sendall(struct.pack(">BI", tipo, len(corpo)) + corpo)
        return self.risp.get(timeout=sec)

    def evento(self, sec, cond=lambda b: True):
        """Aspetta un evento 0x8e che soddisfa cond; ritorna il corpo o None."""
        fine = time.time() + sec
        while time.time() < fine:
            try:
                t, b = self.eventi.get(timeout=max(0.05, fine - time.time()))
            except queue.Empty:
                return None
            if t == 0x8e and cond(b):
                return b
        return None

    def svuota(self):
        while not self.eventi.empty():
            self.eventi.get()

    def stato(self):
        t, b = self.chiedi(0x80)
        return tuple(b) if t == 2 else None


def prepara_partenza():
    """Porta il telefono allo stato da cui parte «trova»: vibra e non disturbare solo priorita' (mai un volume scritto)."""
    sh("cmd audio set-ringer-mode VIBRATE")
    sh("cmd notification set_dnd priority")


def pid_servizio():
    r = leggi_con_fine("ps -A -o PID,ARGS | grep -E '^ *[0-9]+ app_process .*%s servizio' | grep -v grep" % CLASSE).split()
    return r[0] if r else None


def torcia_processo():
    return sh("ps -A -o PID,ARGS | grep -E '^ *[0-9]+ app_process .*%s torcia-su' | grep -v grep" % CLASSE).split()


def fuori(m):  # modo di Android -> modo del protocollo
    return {2: 0, 1: 1, 0: 2}[m]


# ---- prove ----

def prova(base):
    global falliti
    m0, z0, v0 = base
    p = lancia()
    try:
        c = Comandi()
        controlla(any("[controlli] registrato" in r for r in LOG), "servizio partito con il pezzo registrato")
        # 1. stato
        s = c.stato()
        print("stato iniziale (suoneria, nd, torcia, trova, bit): %r" % (s,))
        controlla(s is not None and len(s) == 5, "CONTROLLI_STATO: 5 byte")
        controlla(s[0] == fuori(m0) and s[1] == (0 if z0 == 0 else 1) and s[2] in (0, 0xff) and s[3] == 0,
                  "stato = valori letti dalla shell (modo %d, zen %d)" % (m0, z0))
        bit = s[4]
        print("comandi possibili: 0b%s" % format(bit, "04b"))
        controlla(bit == 15, "tutti e 4 i comandi possibili su questo telefono")
        torcia_ok = (bit & 4) != 0 and s[2] == 0

        # 5. «trova il telefono»: stato di partenza provato = vibra, non disturbare solo priorita', volume diverso dal bersaglio
        prepara_partenza()
        time.sleep(0.5)
        s0 = valori()
        print("partenza di «trova»: %r (modo, zen, volume)" % (s0,))
        c.svuota()
        t, b = c.chiedi(0x84, struct.pack(">H", 30))
        controlla(t == 2, "TROVA_AVVIA risponde OK", "" if t == 2 else b.decode(errors="replace"))
        if t != 2:
            print("  log del servizio: %r" % LOG[-12:])
        if t == 2:
            ora = valori()
            controlla(ora[0] == 2 and ora[1] == 0 and volume_vivo() == VOLUME_TROVA,
                      "durante lo squillo: suona, non disturbare spento, volume di prova", repr((ora[:2], volume_vivo())))
            controlla(c.stato()[3] == 1, "stato: trova in corso")
            voci = voci_trova()
            controlla(len(voci) == 3, "guardiano: 3 ripristini registrati", "%d" % len(voci))
            controlla(c.evento(3, lambda x: x[3] == 1) is not None, "evento 0x8e all'avvio di trova")
            rip = sh("dumpsys audio | grep -i -c 'USAGE_NOTIFICATION_RINGTONE'").strip()
            print("  (righe di dumpsys audio con USAGE_NOTIFICATION_RINGTONE: %s)" % rip)
            t, b = c.chiedi(0x85)
            controlla(t == 2, "TROVA_FERMA risponde OK")
            controlla(aspetta(lambda: valori() == s0, 5), "fermato: modo, non disturbare e volume come prima", repr(valori()))
            controlla(not voci_trova(), "guardiano: ripristini tolti dopo aver rimesso i valori")
            controlla(c.evento(3, lambda x: x[3] == 0) is not None, "evento 0x8e alla fine di trova")
        # arresto automatico (il minimo e' 5 s: la richiesta di 1 s viene limitata)
        c.svuota()
        t0 = time.time()
        t, b = c.chiedi(0x84, struct.pack(">H", 1))
        controlla(t == 2, "TROVA_AVVIA 1 s (limitato a 5 s)")
        ev = c.evento(10, lambda x: x[3] == 0)
        durata = time.time() - t0
        controlla(ev is not None and 4 <= durata <= 8, "arresto automatico al limite", "%.1f s" % durata)
        controlla(aspetta(lambda: valori() == s0, 5) and not voci_trova(), "arresto automatico: valori rimessi, guardiano vuoto", repr(valori()))

        rimetti(base)  # le prove seguenti partono dai valori dell'utente
        time.sleep(0.5)
        # 2. suoneria avanti e indietro, come comandi dell'utente (nessun ripristino nel guardiano)
        # dal modo «suona» si passa SOLO se l'utente ci era (cambierebbe il volume udibile): altrimenti solo vibra e
        # silenzioso; «suona» alla suoneria vera e' coperto dalle prove JVM e da «trova» (volume 1, suono muto)
        for mp in ((0, 1, 2) if m0 == 2 else (1, 2)) + (fuori(m0),):
            c.svuota()
            t, b = c.chiedi(0x81, bytes([mp]))
            controlla(t == 2, "SUONERIA_IMPOSTA %d risponde OK" % mp, "" if t == 2 else b.decode(errors="replace"))
            if t == 2:
                controlla(aspetta(lambda: modo() == {0: 2, 1: 1, 2: 0}[mp], 3), "suoneria %d applicata" % mp)
                ev = c.evento(4, lambda x: x[0] == mp)
                controlla(ev is not None, "evento 0x8e dopo il cambio di suoneria %d" % mp)
        controlla(not [r for r in custode().splitlines() if "set-ringer-mode" in r], "suoneria: nessun ripristino nel guardiano")
        # il cambio fatto a mano (dalla shell, senza passare da noi) viene visto
        c.svuota()
        sh("cmd audio set-ringer-mode %s" % NOME_MODO[0 if m0 != 0 else 1])
        nuovo = fuori(0 if m0 != 0 else 1)
        controlla(c.evento(6, lambda x: x[0] == nuovo) is not None, "cambio fatto fuori da YASDE: arriva l'evento 0x8e (entro il periodo)")
        sh("cmd audio set-ringer-mode %s" % NOME_MODO[m0])

        # 3. non disturbare
        precedente = 0 if z0 == 0 else 1
        prima_nd = precedente
        for dv in (1, 0, precedente):
            c.svuota()
            t, b = c.chiedi(0x82, bytes([dv]))
            controlla(t == 2, "NON_DISTURBARE_IMPOSTA %d risponde OK" % dv, "" if t == 2 else b.decode(errors="replace"))
            if t == 2:
                controlla(aspetta(lambda: (zen() != 0) == (dv == 1), 3), "non disturbare %d applicato" % dv)
                if dv != prima_nd:  # se il valore non cambia non c'e' nessun evento da aspettare
                    controlla(c.evento(4, lambda x: x[1] == dv) is not None, "evento 0x8e dopo non disturbare %d" % dv)
                prima_nd = dv
        controlla(not [r for r in custode().splitlines() if "set_dnd" in r], "non disturbare: nessun ripristino nel guardiano")

        # 4. torcia (al massimo circa 1 secondo)
        if torcia_ok:
            c.svuota()
            t0 = time.time()
            t, b = c.chiedi(0x83, b"\x01")
            acc = c.stato()
            t1, b1 = c.chiedi(0x83, b"\x00")
            durata = time.time() - t0
            controlla(t == 2 and acc is not None and acc[2] == 1, "torcia accesa e letta nello stato")
            controlla(t1 == 2 and c.stato()[2] == 0, "torcia spenta")
            controlla(durata < 1.5, "torcia accesa per meno di 1,5 s", "%.2f s" % durata)
            controlla(c.evento(3, lambda x: x[2] == 1) is not None, "evento 0x8e per la torcia")
            # kill -9 di un processo che l'ha accesa: la spegne il sistema?
            tp = subprocess.Popen(["adb", "shell", "CLASSPATH=%s app_process / %s torcia-su" % (JAR, CLASSE)],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            try:
                prima = queue.Queue()
                threading.Thread(target=lambda: prima.put(tp.stdout.readline().strip()), daemon=True).start()
                try:
                    riga = prima.get(timeout=20)
                except queue.Empty:
                    riga = "(nessuna risposta in 20 s)"
                t0 = time.time()
                if riga == "acceso":
                    sh("pkill -9 -f '%s torcia-su'" % CLASSE)
                    spenta = aspetta(lambda: c.stato()[2] == 0, 1.2, 0.05)
                    dopo = time.time() - t0
                    if not spenta:
                        sh("CLASSPATH=%s app_process / %s torcia-spegni" % (JAR, CLASSE))
                    controlla(spenta, "kill -9 del processo che ha acceso la torcia: la spegne il sistema",
                              "%.2f s dopo l'uccisione" % dopo if spenta else "RESTA ACCESA: la torcia va dichiarata non disponibile")
                else:
                    controlla(False, "processo torcia-su partito", riga)
            finally:
                try:
                    tp.stdin.close()
                except OSError:
                    pass
                tp.kill()
                sh("CLASSPATH=%s app_process / %s torcia-spegni >/dev/null 2>&1" % (JAR, CLASSE))
        else:
            print("torcia: NON PROVATA (non disponibile o gia' accesa)")

        # 6. kill -9 del servizio durante «trova»: il guardiano rimette tutto
        prepara_partenza()
        time.sleep(0.5)
        s0 = valori()
        t, b = c.chiedi(0x84, struct.pack(">H", 60))
        controlla(t == 2 and modo() == 2 and zen() == 0 and volume_vivo() == VOLUME_TROVA, "trova riavviato per la prova di kill -9", repr((t, modo(), zen())))
        pid = pid_servizio()
        controlla(pid is not None, "pid del servizio trovato")
        if pid:
            sh("kill -9 %s" % pid)
            ok = aspetta(lambda: valori() == s0, 25, 0.5)
            controlla(ok, "kill -9 durante trova: il guardiano rimette modo, non disturbare e volume", repr(valori()))
            controlla(attendi_guardiano(40), "guardiano finito (servizio e file yasde-custode-* spariti)")
    finally:
        try:
            p.stdin.close()
        except OSError:
            pass
        try:
            p.wait(timeout=15)
        except subprocess.TimeoutExpired:
            p.kill()
        adb("forward", "--remove", "tcp:%d" % PORTA)


def giro_trova(base, nome, modo_cmd):
    """Secondo giro di «trova» con partenza da vibra/SILENZIOSO (preparata SOLO con comandi di modo e di non
    disturbare, mai con un volume): fermata dal PC e kill -9, con confronto finale di modo, non disturbare e volumi salvati."""
    print("--- giro di trova con partenza %s ---" % nome)
    p = lancia()
    try:
        c = Comandi()
        sh("cmd audio set-ringer-mode %s" % modo_cmd)
        sh("cmd notification set_dnd priority")
        time.sleep(0.5)
        s0 = valori()
        print("partenza (%s): modo %d, zen %d" % (nome, s0[0], s0[1]))
        controlla(s0[0] == {"SILENT": 0, "VIBRATE": 1}[modo_cmd] and s0[1] == 1 and s0[2] == base[2], "partenza %s preparata, volumi salvati intatti" % nome)
        c.stato()
        t, b = c.chiedi(0x84, struct.pack(">H", 30))
        controlla(t == 2, "[%s] TROVA_AVVIA risponde OK" % nome, "" if t == 2 else b.decode(errors="replace"))
        if t == 2:
            controlla(modo() == 2 and zen() == 0 and volume_vivo() == VOLUME_TROVA,
                      "[%s] durante lo squillo: suona, non disturbare spento, volume di prova" % nome, repr((modo(), zen(), volume_vivo())))
            t, b = c.chiedi(0x85)
            controlla(t == 2 and aspetta(lambda: valori() == s0, 5), "[%s] fermato dal PC: modo, non disturbare e volumi salvati come prima" % nome, repr(valori()[:2]))
            controlla(not voci_trova(), "[%s] guardiano: ripristini tolti" % nome)
        t, b = c.chiedi(0x84, struct.pack(">H", 60))
        pid = pid_servizio()
        controlla(t == 2 and pid is not None, "[%s] trova riavviato per kill -9" % nome)
        if pid:
            sh("kill -9 %s" % pid)
            controlla(aspetta(lambda: valori() == s0, 25, 0.5), "[%s] kill -9 durante trova: il guardiano rimette modo, non disturbare e volumi" % nome, repr(valori()[:2]))
            controlla(attendi_guardiano(40), "[%s] guardiano finito (servizio e file yasde-custode-* spariti)" % nome)
    finally:
        try:
            p.stdin.close()
        except OSError:
            pass
        try:
            p.wait(timeout=15)
        except subprocess.TimeoutExpired:
            p.kill()
        adb("forward", "--remove", "tcp:%d" % PORTA)
        # prima il guardiano deve aver finito; se non ha finito non si riscrive nulla qui: ci pensa il ripristino finale
        if attendi_guardiano(40):
            ripristina(base)
        else:
            controlla(False, "[%s] il guardiano non ha finito entro 40 s: niente ripristino qui, si ripete alla fine" % nome)


def ripristina(base):
    """Rimette modo e non disturbare e VERIFICA, ogni passo con il suo try: un adb che cade non interrompe niente.
    Ritorna True se il telefono e' com'era."""
    for _ in range(3):
        try:
            rimetti(base)
        except Exception as e:
            print("ripristino: errore (%s)" % e)
        try:
            v = valori()
            if v[0] == base[0] and v[1] == base[1]:
                return True
        except Exception as e:
            print("ripristino: lettura non riuscita (%s)" % e)
        time.sleep(0.5)
    return False


def uscita_su_segnale(numero, frame):
    raise SystemExit(128 + numero)  # passa dai `finally`: il telefono viene rimesso e il turno tolto


def principale():
    global turno_mio, falliti
    signal.signal(signal.SIGTERM, uscita_su_segnale)
    signal.signal(signal.SIGHUP, uscita_su_segnale)
    try:
        os.mkdir(TURNO)
        turno_mio = True
    except FileExistsError:
        print("telefono occupato (turno preso da un altro): esco senza toccare nulla")
        sys.exit(3)
    try:
        corpo_principale()
    finally:
        # il turno si toglie SEMPRE (solo il proprio), qualunque cosa sia successa
        if turno_mio:
            try:
                os.rmdir(TURNO)
            except OSError as e:
                print("ATTENZIONE: turno non tolto (%s): toglierlo a mano" % e)
    print("prove fallite: %d" % falliti)
    sys.exit(1 if falliti else 0)


def corpo_principale():
    global falliti
    base = None
    try:
        if not os.path.exists(JARLOCALE):
            raise SystemExit("manca il jar di prova: " + JARLOCALE)
        d = adb("devices")
        if not re.search(r"\tdevice\b", d):
            raise SystemExit("nessun telefono collegato (non si ricollega nulla)")
        if "mWakefulness=Awake" not in sh("dumpsys power | grep -m1 mWakefulness="):
            raise SystemExit("telefono non Awake: non si risveglia, esco")
        # sola lettura: se c'e' gia' un YASDE (vero o di un'altra prova) non si tocca nulla
        gia = sh("ps -A -o PID,ARGS | grep -E 'it[.]yasde' | grep -v grep").strip()
        vecchi = sh("ls /data/local/tmp | grep '^yasde-custode-'").strip()
        if gia or vecchi or file_custode():
            raise SystemExit("sul telefono c'e' gia' un processo it.yasde o un file yasde-custode-*: esco senza toccare nulla")
        # sola lettura, prima di ogni modifica: niente chiamata in corso; Non disturbare spento (altrimenti il ripristino
        # `set_dnd` lo convertirebbe in manuale senza scadenza se era programmato o a tempo)
        # tutto il dump: il telefono ha piu' schede, ogni riga «mCallState=N» deve valere 0 (e ce ne deve essere almeno una)
        stati = re.findall(r"(?m)^\s*mCallState=(\d+)\s*$", leggi_con_fine("dumpsys telephony.registry"))
        if not stati or any(x != "0" for x in stati):
            raise SystemExit("chiamata in corso o stato delle chiamate illeggibile: esco senza toccare nulla")
        if zen() != 0:
            raise SystemExit("Non disturbare acceso: esco senza toccare nulla")
        base = valori()
        print("valori letti prima: modo mode_ringer %d, zen_mode %d, %d righe volume_ring salvate" % (base[0], base[1], len(base[2])))
        if not base[2]:
            base = None
            raise SystemExit("volumi salvati illeggibili: esco senza toccare nulla")
        prova(base)
        if attendi_guardiano(40):
            ripristina(base)
            giro_trova(base, "SILENZIOSO", "SILENT")
        else:
            controlla(False, "il guardiano non ha finito entro 40 s: secondo giro saltato, si ripete il ripristino alla fine")
    finally:
        # da qui il ripristino non si interrompe: un secondo segnale non conta
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal.signal(signal.SIGHUP, signal.SIG_IGN)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        if base is not None:
            try:
                # un guardiano ancora al lavoro (dopo un kill -9) deve finire PRIMA di qualunque nostra scrittura
                if toccato and not attendi_guardiano(40):
                    print("ATTENZIONE: guardiano ancora presente dopo 40 s: confronto finale da ripetere a mano")
                    falliti += 1
            except Exception as e:
                print("attesa del guardiano non riuscita (%s)" % e)
            if not ripristina(base):
                falliti += 1
                print("TELEFONO NON RIMESSO: modo %s, Non disturbare %s da rimettere a mano" % (NOME_MODO[base[0]], NOME_ZEN[base[1]]))
            else:
                try:
                    dopo = valori()
                    controlla(True, "FINE: modo e non disturbare rimessi ESATTAMENTE", repr(dopo[:2]))
                    # riga per riga: ogni volume salvato dell'utente deve essere identico a prima
                    controlla(dopo[2] == base[2], "FINE: tutti i volume_ring* salvati identici a prima",
                              "" if dopo[2] == base[2] else "DIFFERENZE: prima %r, dopo %r" % (
                                  sorted(set(base[2]) - set(dopo[2])), sorted(set(dopo[2]) - set(base[2]))))
                except Exception as e:
                    falliti += 1
                    print("confronto finale non riuscito (%s): controllare il telefono a mano" % e)
        if toccato:
            try:
                sh("CLASSPATH=%s app_process / %s torcia-spegni >/dev/null 2>&1" % (JAR, CLASSE))
                sh("rm -f %s" % JAR)
                aspetta(lambda: not file_custode(), 15, 0.5)
                resti = sh("ls /data/local/tmp | grep -E '^(pn-controlli|yasde-custode)'").split()
                controlla(not resti, "pulizia: nessun file nostro rimasto sul telefono", repr(resti))
            except Exception as e:
                falliti += 1
                print("pulizia non riuscita (%s): controllare /data/local/tmp a mano" % e)


if __name__ == "__main__":
    principale()
