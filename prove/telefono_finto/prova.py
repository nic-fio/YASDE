#!/usr/bin/env python3
"""Prova del telefono finto con un piccolo client ADB: TLS vero, due canali
insieme, un comando, un file mandato e riletto, un socket Unix, un PC non
abbinato. Esce con 0 se tutto va bene."""

import os
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time

QUI = os.path.dirname(os.path.abspath(__file__))
H = struct.Struct("<6I")


def _id(s):
    return struct.unpack("<I", s)[0]


CNXN, STLS, OPEN, OKAY, CLSE, WRTE = (_id(x) for x in (b"CNXN", b"STLS", b"OPEN", b"OKAY", b"CLSE", b"WRTE"))


def controlla(cond, msg):
    if not cond:
        raise AssertionError(msg)


class Telefono:
    """Il processo telefono_finto.py."""

    def __init__(self, cartella, *extra):
        self.p = subprocess.Popen(
            [sys.executable, os.path.join(QUI, "telefono_finto.py"), "--cartella", cartella, *extra],
            stdout=subprocess.PIPE, text=True)
        riga = self.p.stdout.readline()
        controlla(riga.startswith("PORTA "), "il telefono finto non ha stampato la porta: %r" % riga)
        self.porta = int(riga.split()[1])

    def ferma(self):
        self.p.kill()
        self.p.wait()
        self.p.stdout.close()


class Cliente:
    """Client sincrono: legge un messaggio alla volta e tiene i dati per canale."""

    def __init__(self, porta, abbinato=True):
        s = socket.create_connection(("127.0.0.1", porta), timeout=15)
        self.raw = s
        self.manda_raw(CNXN, 0x01000001, 256 * 1024, b"host::prova\0")
        cmd, a0, _, _ = self.leggi_raw()
        controlla(cmd == STLS, "era atteso STLS")
        self.manda_raw(STLS, 0x01000000, 0)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        ctx.minimum_version = ssl.TLSVersion.TLSv1_3
        self.cert_dir = tempfile.mkdtemp(prefix="prova_telefono_")
        k, c = os.path.join(self.cert_dir, "k.pem"), os.path.join(self.cert_dir, "c.pem")
        subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
                        "-nodes", "-keyout", k, "-out", c, "-subj", "/CN=pc-prova", "-days", "2"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        ctx.load_cert_chain(c, k)
        self.s = ctx.wrap_socket(s)
        self.canali = {}    # id locale (nostro) -> dict(dati, ok, chiuso, remoto)
        self.prossimo = 1
        if abbinato:
            cmd, _, a1, dati = self.leggi()
            controlla(cmd == CNXN, "dopo il TLS era atteso CNXN")
            self.max_dati = a1
            self.identita = dati.rstrip(b"\0").decode()

    def manda_raw(self, cmd, a0, a1, dati=b""):
        self.raw.sendall(H.pack(cmd, a0, a1, len(dati), 0, cmd ^ 0xFFFFFFFF) + dati)

    def leggi_raw(self):
        return self._leggi_da(self.raw)

    @staticmethod
    def _esatto(sock, n):
        out = bytearray()
        while len(out) < n:
            b = sock.recv(n - len(out))
            if not b:
                raise ConnectionError("chiusa")
            out += b
        return bytes(out)

    def _leggi_da(self, sock):
        cmd, a0, a1, n, _, magic = H.unpack(self._esatto(sock, H.size))
        controlla(magic == cmd ^ 0xFFFFFFFF, "magic sbagliato")
        return cmd, a0, a1, self._esatto(sock, n) if n else b""

    def leggi(self):
        return self._leggi_da(self.s)

    def manda(self, cmd, a0, a1, dati=b""):
        self.s.sendall(H.pack(cmd, a0, a1, len(dati), 0, cmd ^ 0xFFFFFFFF) + dati)

    def _pompa(self):
        """Elabora un messaggio: i WRTE si confermano subito, come fa adb."""
        cmd, a0, a1, dati = self.leggi()
        c = self.canali.get(a1)
        if c is None:
            return
        if cmd == OKAY:
            c["remoto"] = a0
            c["ok"] += 1
        elif cmd == WRTE:
            c["dati"] += dati
            self.manda(OKAY, a1, a0)
        elif cmd == CLSE:
            c["chiuso"] = True
            c["remoto"] = c["remoto"] or a0

    def apri(self, servizio):
        """Manda OPEN e torna l'id del canale; usare attendi_aperto() dopo."""
        i = self.prossimo
        self.prossimo += 1
        self.canali[i] = {"dati": bytearray(), "ok": 0, "chiuso": False, "remoto": 0, "aperto": False}
        self.manda(OPEN, i, 0, servizio.encode() + b"\0")
        return i

    def attendi_aperto(self, i):
        c = self.canali[i]
        while not c["ok"] and not c["chiuso"]:
            self._pompa()
        c["ok"] -= 1
        c["aperto"] = not c["chiuso"]
        return c["aperto"]

    def scrivi(self, i, dati):
        c = self.canali[i]
        self.manda(WRTE, i, c["remoto"], dati)
        while not c["ok"] and not c["chiuso"]:
            self._pompa()
        c["ok"] -= 1

    def attendi(self, i, condizione):
        c = self.canali[i]
        while not condizione(c) and not c["chiuso"]:
            self._pompa()

    def chiudi_canale(self, i):
        c = self.canali[i]
        self.manda(CLSE, i, c["remoto"])
        self.attendi(i, lambda c: False)

    def fine(self):
        try:
            self.s.close()
        except OSError:
            pass


def analizza_shell(dati):
    """Smonta i pacchetti shell v2: torna (stdout, stderr, codice o None)."""
    out, err, codice, i = b"", b"", None, 0
    while i + 5 <= len(dati):
        t, n = struct.unpack_from("<BI", dati, i)
        corpo = bytes(dati[i + 5:i + 5 + n])
        i += 5 + n
        if t == 1:
            out += corpo
        elif t == 2:
            err += corpo
        elif t == 3:
            codice = corpo[0]
    return out, err, codice


def prova_canali_e_shell(c):
    # Due canali aperti insieme: il primo e' lento, il secondo finisce prima.
    a = c.apri("shell,v2,raw:sleep 0.5; echo uno; echo errore-uno >&2")
    b = c.apri("shell,v2,raw:echo due; exit 3")
    controlla(c.attendi_aperto(a), "canale A non aperto")
    controlla(c.attendi_aperto(b), "canale B non aperto")
    c.attendi(b, lambda x: False)
    controlla(not c.canali[a]["chiuso"], "A doveva essere ancora vivo quando B ha finito")
    c.attendi(a, lambda x: False)
    out, err, codice = analizza_shell(c.canali[b]["dati"])
    controlla((out, err, codice) == (b"due\n", b"", 3), "canale B: %r" % ((out, err, codice),))
    out, err, codice = analizza_shell(c.canali[a]["dati"])
    controlla((out, err, codice) == (b"uno\n", b"errore-uno\n", 0), "canale A: %r" % ((out, err, codice),))

    # stdin: si manda del testo e si chiude lo stdin.
    s = c.apri("shell,v2,raw:cat")
    controlla(c.attendi_aperto(s), "canale cat non aperto")
    c.scrivi(s, struct.pack("<BI", 0, 5) + b"ciao\n")
    c.scrivi(s, struct.pack("<BI", 4, 0))
    c.attendi(s, lambda x: False)
    out, _, codice = analizza_shell(c.canali[s]["dati"])
    controlla(out == b"ciao\n" and codice == 0, "cat: %r %r" % (out, codice))

    # Il comando gira dentro la cartella del telefono.
    p = c.apri("shell,v2:pwd")
    c.attendi_aperto(p)
    c.attendi(p, lambda x: False)
    out, _, _ = analizza_shell(c.canali[p]["dati"])
    controlla(out.strip().decode().endswith(os.path.basename(c.cartella)), "pwd: %r" % out)


def sync_comando(c, i, cmd, percorso):
    c.scrivi(i, cmd + struct.pack("<I", len(percorso)) + percorso.encode())


def prova_sync(c, cartella):
    i = c.apri("sync:")
    controlla(c.attendi_aperto(i), "sync non aperto")
    contenuto = os.urandom(200 * 1024 + 123)   # piu' blocchi da 64 KB

    # SEND v1, con i dati spezzati in blocchi.
    sync_comando(c, i, b"SEND", "/sdcard/prova/x.bin,33188")
    for k in range(0, len(contenuto), 64 * 1024):
        blocco = contenuto[k:k + 64 * 1024]
        c.scrivi(i, b"DATA" + struct.pack("<I", len(blocco)) + blocco)
    c.scrivi(i, b"DONE" + struct.pack("<I", 1700000000))
    c.attendi(i, lambda x: len(x["dati"]) >= 8)
    controlla(bytes(c.canali[i]["dati"][:4]) == b"OKAY", "SEND: %r" % bytes(c.canali[i]["dati"][:12]))
    c.canali[i]["dati"].clear()
    file = os.path.join(cartella, "sdcard", "prova", "x.bin")
    controlla(os.path.exists(file) and open(file, "rb").read() == contenuto, "il file non e' arrivato uguale")
    controlla(int(os.stat(file).st_mtime) == 1700000000, "mtime non impostato")

    # STAT v1 e v2.
    sync_comando(c, i, b"STAT", "/sdcard/prova/x.bin")
    c.attendi(i, lambda x: len(x["dati"]) >= 16)
    _, modo, dim, mt = struct.unpack("<4sIII", bytes(c.canali[i]["dati"][:16]))
    controlla(dim == len(contenuto) and mt == 1700000000 and modo & 0o100000, "STAT v1")
    c.canali[i]["dati"].clear()
    sync_comando(c, i, b"STA2", "/sdcard/prova/x.bin")
    c.attendi(i, lambda x: len(x["dati"]) >= 72)
    v = struct.unpack("<4sIQQIIIIQqqq", bytes(c.canali[i]["dati"][:72]))
    controlla(v[1] == 0 and v[8] == len(contenuto), "STA2")
    c.canali[i]["dati"].clear()
    sync_comando(c, i, b"STA2", "/sdcard/non-esiste")
    c.attendi(i, lambda x: len(x["dati"]) >= 72)
    controlla(struct.unpack_from("<I", c.canali[i]["dati"], 4)[0] != 0, "STA2 su file mancante senza errore")
    c.canali[i]["dati"].clear()

    # RECV: stesso contenuto.
    sync_comando(c, i, b"RECV", "/sdcard/prova/x.bin")
    ricevuto = bytearray()
    while True:
        c.attendi(i, lambda x: len(x["dati"]) >= 8)
        d = c.canali[i]["dati"]
        tipo, n = struct.unpack_from("<4sI", d)
        if tipo == b"DONE":
            del d[:8]
            break
        controlla(tipo == b"DATA", "RECV: %r" % tipo)
        c.attendi(i, lambda x: len(x["dati"]) >= 8 + n)
        ricevuto += d[8:8 + n]
        del d[:8 + n]
    controlla(bytes(ricevuto) == contenuto, "RECV: contenuto diverso")

    # LIST.
    sync_comando(c, i, b"LIST", "/sdcard/prova")
    c.attendi(i, lambda x: bytes(x["dati"]).endswith(b"DONE" + bytes(16)))
    controlla(b"x.bin" in bytes(c.canali[i]["dati"]), "LIST senza x.bin")
    c.canali[i]["dati"].clear()

    # LIS2: ogni voce e' un sync_dent_v2 di 76 byte piu' il nome; il DONE e' un
    # sync_dent_v2 vuoto di 76 byte esatti.
    sync_comando(c, i, b"LIS2", "/sdcard/prova")
    c.attendi(i, lambda x: bytes(x["dati"]).endswith(b"DONE" + bytes(72)))
    d = bytes(c.canali[i]["dati"])
    c.canali[i]["dati"].clear()
    controlla(d.startswith(b"DNT2"), "LIS2: manca DNT2")
    nome_len = struct.unpack_from("<I", d, 72)[0]
    controlla(d[76:76 + nome_len] == b"x.bin", "LIS2: nome %r" % d[76:76 + nome_len])
    controlla(len(d) == 76 + nome_len + 76 and d[-76:] == b"DONE" + bytes(72), "LIS2: DONE non e' di 76 byte")

    # Un FAIL chiude il canale (come adbd): RECV di un file inesistente.
    f = c.apri("sync:")
    controlla(c.attendi_aperto(f), "sync non aperto")
    sync_comando(c, f, b"RECV", "/sdcard/non-esiste.bin")
    c.attendi(f, lambda x: False)
    controlla(bytes(c.canali[f]["dati"][:4]) == b"FAIL" and c.canali[f]["chiuso"], "FAIL senza chiusura del canale")

    # Mai fuori dalla cartella: "/../" resta dentro.
    sync_comando(c, i, b"SEND", "/../../fuori.txt,33188")
    c.scrivi(i, b"DATA" + struct.pack("<I", 3) + b"abc")
    c.scrivi(i, b"DONE" + struct.pack("<I", 0))
    c.attendi(i, lambda x: len(x["dati"]) >= 8)
    c.canali[i]["dati"].clear()
    controlla(os.path.exists(os.path.join(cartella, "fuori.txt")), "SEND con .. non e' rimasto in DIR")
    controlla(not os.path.exists(os.path.join(os.path.dirname(cartella), "fuori.txt")),
              "un file e' uscito dalla cartella")
    c.chiudi_canale(i)


def prova_socket_unix(c, cartella):
    percorso = os.path.join(cartella, "socket")
    os.makedirs(percorso, exist_ok=True)
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    old = os.getcwd()
    os.chdir(cartella)
    try:
        srv.bind("socket/eco")
    finally:
        os.chdir(old)
    srv.listen(1)

    def eco():
        conn, _ = srv.accept()
        while True:
            b = conn.recv(4096)
            if not b:
                break
            conn.sendall(b.upper())
        conn.close()

    threading.Thread(target=eco, daemon=True).start()
    i = c.apri("localabstract:eco")
    controlla(c.attendi_aperto(i), "localabstract:eco non aperto")
    c.scrivi(i, b"ping")
    c.attendi(i, lambda x: len(x["dati"]) >= 4)
    controlla(bytes(c.canali[i]["dati"]) == b"PING", "eco: %r" % bytes(c.canali[i]["dati"]))
    c.chiudi_canale(i)
    j = c.apri("localabstract:non-esiste")
    controlla(not c.attendi_aperto(j), "localabstract inesistente doveva essere rifiutato")
    srv.close()


def prova_non_abbinato(cartella):
    t = Telefono(cartella, "--non-abbinato")
    try:
        c = Cliente(t.porta, abbinato=False)
        c.s.settimeout(5)
        try:
            dati = c.s.recv(100)
        except (ssl.SSLError, ConnectionError):
            dati = b""
        controlla(dati == b"", "PC non abbinato: la connessione doveva chiudersi")
        c.fine()
    finally:
        t.ferma()


def main():
    with tempfile.TemporaryDirectory(prefix="telefono_finto_prova_") as tmp:
        cartella = os.path.join(tmp, "tel")
        os.makedirs(cartella)
        t = Telefono(cartella)
        try:
            c = Cliente(t.porta)
            c.cartella = cartella
            controlla("shell_v2" in c.identita, "identita' senza shell_v2: %r" % c.identita)
            prova_canali_e_shell(c)
            prova_sync(c, cartella)
            prova_socket_unix(c, cartella)
            c.fine()
        finally:
            t.ferma()
        prova_non_abbinato(os.path.join(tmp, "tel2"))
    print("telefono finto: tutte le prove riuscite")


if __name__ == "__main__":
    t0 = time.time()
    try:
        main()
    except Exception as e:  # noqa: BLE001
        import traceback
        traceback.print_exc()
        print("telefono finto: FALLITA:", repr(e), file=sys.stderr)
        sys.exit(1)
