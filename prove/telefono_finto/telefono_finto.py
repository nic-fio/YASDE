#!/usr/bin/env python3
"""Telefono finto: si comporta come il demone ADB di un telefono con il debug
wireless acceso, per provare i pezzi del PC senza telefono vero.

Uso: telefono_finto.py --cartella DIR [--porta N] [--non-abbinato]
Stampa "PORTA <n>" su stdout e resta in ascolto su 127.0.0.1.
Solo libreria standard (piu' il comando `openssl` per il certificato)."""

import argparse
import ctypes
import ctypes.util
import errno
import os
import posixpath
import queue
import select
import shutil
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading

# Comandi del protocollo ADB (quattro lettere, little endian).
def _id(s):
    return struct.unpack("<I", s)[0]

A_CNXN, A_STLS, A_OPEN, A_OKAY, A_CLSE, A_WRTE = (
    _id(b"CNXN"), _id(b"STLS"), _id(b"OPEN"), _id(b"OKAY"), _id(b"CLSE"), _id(b"WRTE"))

VERSIONE = 0x01000001        # senza checksum dei dati
VERSIONE_STLS = 0x01000000
MAX_DATI = 1024 * 1024
FUNZIONI = "shell_v2,stat_v2,ls_v2,sendrecv_v2,fixed_push_mkdir"
INTESTAZIONE = struct.Struct("<6I")

# Shell v2: un byte di tipo e quattro di lunghezza.
SH_STDIN, SH_STDOUT, SH_STDERR, SH_EXIT, SH_CLOSE_STDIN, SH_WINDOW = 0, 1, 2, 3, 4, 5

SYNC_DATA_MAX = 64 * 1024


def log(*a):
    print(*a, file=sys.stderr, flush=True)


# ---------------------------------------------------------------- certificato

def crea_contesto_tls():
    """Contesto TLS 1.3 lato server con un certificato fresco. Chiede il
    certificato del client ma non lo verifica (come fa adbd: l'abbinamento si
    controlla dopo, sull'impronta)."""
    cartella = tempfile.mkdtemp(prefix="telefono_finto_")
    chiave, cert = os.path.join(cartella, "k.pem"), os.path.join(cartella, "c.pem")
    subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
         "-nodes", "-keyout", chiave, "-out", cert, "-subj", "/CN=telefono-finto", "-days", "2"],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_3
    ctx.load_cert_chain(cert, chiave)
    shutil.rmtree(cartella, ignore_errors=True)
    ctx.verify_mode = ssl.CERT_OPTIONAL   # chiede il certificato
    if not _accetta_ogni_certificato(ctx):
        # Senza il trucco il certificato non si puo' chiedere senza verificarlo.
        ctx.verify_mode = ssl.CERT_NONE
        log("avviso: certificato del client non richiesto (libssl non raggiungibile)")
    return ctx


_CALLBACK_TIPO = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_void_p)
_callback_accetta = _CALLBACK_TIPO(lambda ok, store: 1)


def _accetta_ogni_certificato(ctx):
    """Il modulo ssl non permette di chiedere un certificato senza verificarlo:
    si installa su libssl una funzione di verifica che dice sempre si'.
    Il puntatore all'SSL_CTX sta subito dopo l'intestazione dell'oggetto Python;
    si controlla che sia quello giusto prima di usarlo."""
    try:
        nome = ssl._ssl.__file__  # solo per essere sicuri che _ssl sia caricato
        lib = None
        for candidato in (ctypes.util.find_library("ssl"), "libssl.so.3", "libssl.so.1.1"):
            if candidato:
                try:
                    lib = ctypes.CDLL(candidato)
                    break
                except OSError:
                    continue
        if lib is None or nome is None:
            return False
        ptr = ctypes.c_void_p.from_address(id(ctx) + 2 * ctypes.sizeof(ctypes.c_void_p)).value
        if not ptr:
            return False
        lib.SSL_CTX_get_verify_mode.argtypes = [ctypes.c_void_p]
        lib.SSL_CTX_get_verify_mode.restype = ctypes.c_int
        if lib.SSL_CTX_get_verify_mode(ptr) != 1:  # SSL_VERIFY_PEER
            return False
        lib.SSL_CTX_set_verify.argtypes = [ctypes.c_void_p, ctypes.c_int, _CALLBACK_TIPO]
        lib.SSL_CTX_set_verify.restype = None
        lib.SSL_CTX_set_verify(ptr, 1, _callback_accetta)
        return True
    except Exception:  # noqa: BLE001  qualsiasi problema = ripiego
        return False


# ------------------------------------------------------------------ percorsi

class Sandbox:
    """Mappa i percorsi del telefono sotto DIR e non esce mai da DIR."""

    def __init__(self, cartella):
        self.radice = os.path.realpath(cartella)

    def mappa(self, percorso):
        norm = posixpath.normpath("/" + percorso.lstrip("/"))   # toglie i ".."
        reale = os.path.join(self.radice, norm.lstrip("/"))
        # Anche i collegamenti simbolici non devono portare fuori.
        controllo = os.path.realpath(reale)
        if controllo != self.radice and not controllo.startswith(self.radice + os.sep):
            raise PermissionError("percorso fuori dalla cartella")
        return reale


# ----------------------------------------------------------------- servizi

class Servizio:
    """Base: un servizio riceve dati con dati() e parla con canale.invia()."""

    def __init__(self, canale):
        self.canale = canale

    def avvia(self):
        pass

    def dati(self, b):
        pass

    def chiudi(self):
        """Il client ha chiuso il canale: ferma tutto."""


def impacchetta_shell(tipo, dati):
    return struct.pack("<BI", tipo, len(dati)) + dati


class ShellV2(Servizio):
    def __init__(self, canale, comando, sandbox):
        super().__init__(canale)
        self.comando = comando
        self.sandbox = sandbox
        self.buf = bytearray()
        self.proc = None

    def avvia(self):
        argv = ["sh", "-c", self.comando] if self.comando else ["sh"]
        self.proc = subprocess.Popen(
            argv, cwd=self.sandbox.radice, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        lettori = [
            threading.Thread(target=self._leggi, args=(self.proc.stdout, SH_STDOUT), daemon=True),
            threading.Thread(target=self._leggi, args=(self.proc.stderr, SH_STDERR), daemon=True),
        ]
        for t in lettori:
            t.start()
        threading.Thread(target=self._attendi, args=(lettori,), daemon=True).start()

    def _leggi(self, flusso, tipo):
        while True:
            b = os.read(flusso.fileno(), 32768)
            if not b:
                return
            self.canale.invia(impacchetta_shell(tipo, b))

    def _attendi(self, lettori):
        for t in lettori:
            t.join()
        codice = self.proc.wait()
        self.canale.invia(impacchetta_shell(SH_EXIT, bytes([codice & 0xFF])))
        self.canale.fine()

    def dati(self, b):
        self.buf += b
        while len(self.buf) >= 5:
            tipo, n = struct.unpack_from("<BI", self.buf)
            if len(self.buf) < 5 + n:
                return
            corpo = bytes(self.buf[5:5 + n])
            del self.buf[:5 + n]
            try:
                if tipo == SH_STDIN:
                    self.proc.stdin.write(corpo)
                    self.proc.stdin.flush()
                elif tipo == SH_CLOSE_STDIN:
                    self.proc.stdin.close()
            except (BrokenPipeError, ValueError, OSError):
                pass   # il comando non legge piu' lo stdin

    def chiudi(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.kill()


class SocketUnix(Servizio):
    """localabstract:<nome> -> socket Unix DIR/socket/<nome>."""

    def __init__(self, canale, sock):
        super().__init__(canale)
        self.sock = sock

    def avvia(self):
        threading.Thread(target=self._leggi, daemon=True).start()

    def _leggi(self):
        try:
            while True:
                b = self.sock.recv(32768)
                if not b:
                    break
                self.canale.invia(b)
        except OSError:
            pass
        self.canale.fine()

    def dati(self, b):
        try:
            self.sock.sendall(b)
        except OSError:
            self.canale.fine()

    def chiudi(self):
        try:
            self.sock.close()
        except OSError:
            pass


def _stat_v2(st, errore=0):
    return struct.pack("<IQQIIIIQqqq", errore, st.st_dev, st.st_ino, st.st_mode, st.st_nlink,
                       st.st_uid, st.st_gid, st.st_size, int(st.st_atime), int(st.st_mtime),
                       int(st.st_ctime))


class Sync(Servizio):
    """Servizio sync: STAT/LIST/SEND/RECV e le varianti v2. Le richieste
    possono arrivare spezzate su piu' WRTE, quindi c'e' una piccola macchina
    a stati sul flusso di byte."""

    def __init__(self, canale, sandbox):
        super().__init__(canale)
        self.sb = sandbox
        self.buf = bytearray()
        self.stato = "richiesta"
        self.req = None        # (id, percorso) della richiesta in corso
        self.file = None
        self.v2 = False
        self.mtime = 0

    def dati(self, b):
        self.buf += b
        while self._avanza():
            pass

    def _prendi(self, n):
        if len(self.buf) < n:
            return None
        r = bytes(self.buf[:n])
        del self.buf[:n]
        return r

    def _fail(self, messaggio, chiudi=True):
        """Manda FAIL e, come adbd, chiude il canale (salvo durante SEND, dove
        prima si scartano i dati fino a DONE: vedi lo stato "scarta")."""
        m = messaggio.encode()
        self.canale.invia(b"FAIL" + struct.pack("<I", len(m)) + m)
        if chiudi:
            self.stato = "chiuso"
            self.buf.clear()
            self.canale.fine()

    def _avanza(self):
        if self.stato == "chiuso":
            self.buf.clear()
            return False
        if self.stato == "richiesta":
            if len(self.buf) < 8:
                return False
            cmd, n = struct.unpack_from("<4sI", self.buf)
            if cmd == b"QUIT":
                del self.buf[:8]
                self.canale.fine()
                return False
            if n > 1024 or len(self.buf) < 8 + n:
                if n > 1024:
                    self._fail("richiesta troppo lunga")
                    self.buf.clear()
                return False
            percorso = bytes(self.buf[8:8 + n]).decode("utf-8", "replace")
            del self.buf[:8 + n]
            return self._richiesta(cmd, percorso)
        if self.stato == "setup_send_v2":
            r = self._prendi(12)
            if r is None:
                return False
            _, modo, flag = struct.unpack("<III", r)
            if flag != 0:
                self._fail("compressione non supportata", chiudi=False)
                self.stato = "scarta"
                return True
            return self._apri_invio(modo)
        if self.stato == "setup_recv_v2":
            r = self._prendi(8)
            if r is None:
                return False
            _, flag = struct.unpack("<II", r)
            if flag != 0:
                self._fail("compressione non supportata")
                return False   # il FAIL ha chiuso il canale
            self._invia_file(self.req[1])
            if self.stato != "chiuso":   # se il file mancava, _fail ha gia' chiuso
                self.stato = "richiesta"
            return self.stato == "richiesta"
        if self.stato == "dati_invio":
            if len(self.buf) < 8:
                return False
            cmd, n = struct.unpack_from("<4sI", self.buf)
            if cmd == b"DATA":
                if n > SYNC_DATA_MAX:
                    self._fine_invio_errore("blocco dati troppo grande")
                    return True
                if len(self.buf) < 8 + n:
                    return False
                try:
                    self.file.write(bytes(self.buf[8:8 + n]))
                except OSError as e:
                    del self.buf[:8 + n]
                    self._fine_invio_errore(e.strerror or "scrittura fallita")
                    return True
                del self.buf[:8 + n]
                return True
            if cmd == b"DONE":
                del self.buf[:8]
                mtime = n
                try:
                    self.file.close()
                    os.utime(self.req[1], (mtime, mtime))
                except OSError as e:
                    self.file = None
                    self._fail(e.strerror or "chiusura fallita")
                    return False
                self.file = None
                self.canale.invia(b"OKAY" + struct.pack("<I", 0))
                self.stato = "richiesta"
                return True
            self._fine_invio_errore("comando inatteso durante l'invio")
            return True
        if self.stato == "scarta":
            # dopo un errore si buttano i dati finche' non arriva DONE
            if len(self.buf) < 8:
                return False
            cmd, n = struct.unpack_from("<4sI", self.buf)
            if cmd == b"DATA":
                if len(self.buf) < 8 + n:
                    return False
                del self.buf[:8 + n]
                return True
            del self.buf[:8 if cmd == b"DONE" else len(self.buf)]
            self.stato = "chiuso"
            self.canale.fine()
            return False
        return False

    def _fine_invio_errore(self, messaggio):
        if self.file is not None:
            try:
                self.file.close()
            except OSError:
                pass
            self.file = None
        self._fail(messaggio, chiudi=False)
        self.stato = "scarta"

    def _percorso(self, p, chiudi=True):
        try:
            return self.sb.mappa(p)
        except PermissionError:
            self._fail("permesso negato", chiudi=chiudi)
            return None

    def _richiesta(self, cmd, percorso):
        if cmd in (b"STAT", b"STA2", b"LST2"):
            reale = self._mappa_muta(percorso)
            self._stat(cmd, reale)
            return True
        if cmd in (b"LIST", b"LIS2"):
            self._elenco(cmd, self._mappa_muta(percorso))
            return True
        if cmd == b"RECV":
            self._invia_file(percorso)
            return True
        if cmd == b"RCV2":
            self.req = (cmd, percorso)
            self.stato = "setup_recv_v2"
            return True
        if cmd == b"SEND":
            p, _, modo = percorso.rpartition(",")
            if not p:
                p, modo = percorso, "0"
            try:
                m = int(modo, 0)
            except ValueError:
                m = 0o644
            self.req = (cmd, p)
            return self._apri_invio_percorso(p, m)
        if cmd == b"SND2":
            self.req = (cmd, percorso)
            self.stato = "setup_send_v2"
            return True
        self._fail("comando sync sconosciuto")
        self.buf.clear()
        return False

    def _mappa_muta(self, percorso):
        """Percorso reale oppure None se esce da DIR (si comporta come 'non esiste')."""
        try:
            return self.sb.mappa(percorso)
        except PermissionError:
            return None

    def _stat(self, cmd, reale):
        try:
            if reale is None:
                raise FileNotFoundError
            st = os.stat(reale) if cmd == b"STA2" else os.lstat(reale)
        except OSError as e:
            if cmd == b"STAT":
                self.canale.invia(b"STAT" + struct.pack("<III", 0, 0, 0))
            else:
                self.canale.invia(cmd + struct.pack("<IQQIIIIQqqq", e.errno or errno.ENOENT,
                                                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
            return
        if cmd == b"STAT":
            self.canale.invia(b"STAT" + struct.pack("<III", st.st_mode, min(st.st_size, 0xFFFFFFFF),
                                                    int(st.st_mtime) & 0xFFFFFFFF))
        else:
            self.canale.invia(cmd + _stat_v2(st))

    def _elenco(self, cmd, reale):
        v2 = cmd == b"LIS2"
        voci = []
        try:
            if reale is not None:
                voci = sorted(os.listdir(reale))
        except OSError:
            voci = []
        for nome in voci:
            try:
                st = os.lstat(os.path.join(reale, nome))
            except OSError:
                continue
            n = nome.encode()
            if v2:
                dati = b"DNT2" + _stat_v2(st) + struct.pack("<I", len(n)) + n
            else:
                dati = b"DENT" + struct.pack("<IIII", st.st_mode, min(st.st_size, 0xFFFFFFFF),
                                             int(st.st_mtime) & 0xFFFFFFFF, len(n)) + n
            self.canale.invia(dati)
        if v2:
            # sync_dent_v2 vuoto: 76 byte in tutto (id + 68 + namelen)
            self.canale.invia(b"DONE" + bytes(72))
        else:
            self.canale.invia(b"DONE" + bytes(16))

    def _apri_invio(self, modo):
        return self._apri_invio_percorso(self.req[1], modo)

    def _apri_invio_percorso(self, p, modo):
        reale = self._percorso(p, chiudi=False)
        if reale is None:
            self.stato = "scarta"
            return True
        try:
            os.makedirs(os.path.dirname(reale), exist_ok=True)
            self.file = open(reale, "wb")
            os.chmod(reale, (modo & 0o777) or 0o644)
        except OSError as e:
            self._fail(e.strerror or "apertura fallita", chiudi=False)
            self.stato = "scarta"
            return True
        self.req = (self.req[0], reale)   # da qui in poi serve il percorso reale
        self.stato = "dati_invio"
        return True

    def _invia_file(self, percorso):
        reale = self._percorso(percorso)
        if reale is None:
            return
        try:
            f = open(reale, "rb")
        except OSError as e:
            self._fail(e.strerror or "apertura fallita")
            return
        with f:
            while True:
                blocco = f.read(SYNC_DATA_MAX)
                if not blocco:
                    break
                self.canale.invia(b"DATA" + struct.pack("<I", len(blocco)) + blocco)
        self.canale.invia(b"DONE" + struct.pack("<I", 0))

    def chiudi(self):
        if self.file is not None:
            try:
                self.file.close()
            except OSError:
                pass


# ------------------------------------------------------------------- canali

class Canale:
    """Un canale ADB aperto. Dal client i dati arrivano in `entrata` (e si
    risponde subito OKAY); verso il client ogni WRTE aspetta il suo OKAY prima
    del successivo (controllo di flusso)."""

    def __init__(self, conn, locale, remoto):
        self.conn, self.locale, self.remoto = conn, locale, remoto
        self.uscita = queue.Queue(maxsize=8)
        self.entrata = queue.Queue()
        self.ack = threading.Event()
        self.chiuso = threading.Event()   # il client ha chiuso, o noi abbiamo finito
        self.servizio = None

    def avvia(self, servizio):
        self.servizio = servizio
        threading.Thread(target=self._scrittore, daemon=True).start()
        threading.Thread(target=self._lettore, daemon=True).start()
        servizio.avvia()

    def invia(self, dati):
        while not self.chiuso.is_set():
            try:
                self.uscita.put(dati, timeout=0.5)
                return
            except queue.Full:
                continue

    def fine(self):
        """Il servizio ha finito: si chiude dopo aver mandato quello che resta."""
        while True:
            try:
                self.uscita.put(None, timeout=0.5)
                return
            except queue.Full:
                if self.chiuso.is_set():
                    return

    def _scrittore(self):
        massimo = max(1024, min(self.conn.max_dati, MAX_DATI))
        while True:
            dati = self.uscita.get()
            if dati is None:
                break
            for i in range(0, len(dati), massimo):
                if self.chiuso.is_set():
                    return
                self.ack.clear()
                self.conn.invia(A_WRTE, self.locale, self.remoto, dati[i:i + massimo])
                while not self.ack.wait(0.5):
                    if self.chiuso.is_set():
                        return
        if not self.chiuso.is_set():
            self.chiuso.set()
            self.conn.invia(A_CLSE, self.locale, self.remoto)
            self.conn.rimuovi(self)

    def _lettore(self):
        while True:
            b = self.entrata.get()
            if b is None:
                return
            try:
                self.servizio.dati(b)
            except Exception as e:  # noqa: BLE001  un servizio rotto non ferma il telefono
                log("errore nel servizio:", e)
                self.fine()

    def dal_client_chiuso(self):
        self.chiuso.set()
        self.entrata.put(None)
        try:
            self.uscita.put_nowait(None)   # sveglia lo scrittore se aspetta
        except queue.Full:
            pass
        self.servizio.chiudi()
        self.conn.rimuovi(self)
        self.conn.invia(A_CLSE, self.locale, self.remoto)


# ---------------------------------------------------------------- connessione

class Connessione:
    def __init__(self, sock, ctx, sandbox, non_abbinato):
        self.sock, self.ctx, self.sandbox = sock, ctx, sandbox
        self.non_abbinato = non_abbinato
        self.max_dati = 256 * 1024
        self.canali = {}
        self.blocco = threading.Lock()
        self.prossimo_id = 1
        self.uscita = bytearray()
        self.sveglia_r, self.sveglia_w = os.pipe()
        os.set_blocking(self.sveglia_r, False)
        os.set_blocking(self.sveglia_w, False)
        self.finita = False

    # -- scrittura, da qualunque thread: si accoda, scrive solo il thread di I/O
    def invia(self, cmd, a0, a1, dati=b""):
        # Il checksum e' zero: la versione 0x01000001 lo ignora.
        msg = INTESTAZIONE.pack(cmd, a0, a1, len(dati), 0, cmd ^ 0xFFFFFFFF) + dati
        with self.blocco:
            self.uscita += msg
        try:
            os.write(self.sveglia_w, b"x")
        except OSError:
            pass   # pipe piena: il thread e' comunque sveglio

    def rimuovi(self, canale):
        with self.blocco:
            if self.canali.get(canale.locale) is canale:
                del self.canali[canale.locale]

    # -- avvio, bloccante
    def _leggi_messaggio(self, sock):
        h = self._esatto(sock, INTESTAZIONE.size)
        cmd, a0, a1, n, _crc, magic = INTESTAZIONE.unpack(h)
        if magic != cmd ^ 0xFFFFFFFF or n > MAX_DATI:
            raise ConnectionError("intestazione non valida")
        return cmd, a0, a1, self._esatto(sock, n) if n else b""

    @staticmethod
    def _esatto(sock, n):
        out = bytearray()
        while len(out) < n:
            b = sock.recv(n - len(out))
            if not b:
                raise ConnectionError("chiusa")
            out += b
        return bytes(out)

    def esegui(self):
        try:
            self._esegui()
        except (OSError, ConnectionError, ssl.SSLError) as e:
            log("connessione terminata:", e)
        finally:
            self.finita = True
            for c in list(self.canali.values()):
                c.chiuso.set()
                c.entrata.put(None)
                if c.servizio is not None:
                    c.servizio.chiudi()
            try:
                self.sock.close()
            except OSError:
                pass
            os.close(self.sveglia_r)
            os.close(self.sveglia_w)

    def _esegui(self):
        sock = self.sock
        sock.settimeout(15)
        cmd, a0, a1, dati = self._leggi_messaggio(sock)
        if cmd != A_CNXN:
            raise ConnectionError("era atteso CNXN")
        self.max_dati = a1 or self.max_dati
        sock.sendall(INTESTAZIONE.pack(A_STLS, VERSIONE_STLS, 0, 0, 0, A_STLS ^ 0xFFFFFFFF))
        # Il client risponde con un suo STLS e poi parte il TLS; per sicurezza
        # si accetta anche l'avvio diretto del TLS (primo byte 0x16).
        iniz = b""
        while len(iniz) < 4:
            iniz = sock.recv(4, socket.MSG_PEEK)
            if not iniz:
                raise ConnectionError("chiusa prima del TLS")
        if iniz[:4] == b"STLS":
            self._esatto(sock, INTESTAZIONE.size)
        tls = self.ctx.wrap_socket(sock, server_side=True)
        self.sock = sock = tls
        if self.non_abbinato:
            log("simulo un PC non abbinato: chiudo dopo il TLS")
            return
        ident = ("device::ro.product.name=telefono_finto;ro.product.model=TelefonoFinto;"
                 "ro.product.device=finto;features=%s" % FUNZIONI).encode() + b"\0"
        self.invia(A_CNXN, VERSIONE, MAX_DATI, ident)
        sock.setblocking(False)
        self._ciclo_io()

    def _ciclo_io(self):
        sock = self.sock
        ingresso = bytearray()
        while not self.finita:
            with self.blocco:
                da_scrivere = len(self.uscita) > 0
            lettura = [sock, self.sveglia_r]
            r, w, _ = select.select(lettura, [sock] if da_scrivere else [], [], 1.0)
            if self.sveglia_r in r:
                try:
                    os.read(self.sveglia_r, 4096)
                except OSError:
                    pass
            if w:
                with self.blocco:
                    pezzo = bytes(self.uscita[:65536])
                try:
                    n = sock.send(pezzo)
                    with self.blocco:
                        del self.uscita[:n]
                except (ssl.SSLWantWriteError, ssl.SSLWantReadError, BlockingIOError):
                    pass
            while True:   # legge tutto quello che c'e', anche cio' che TLS ha gia' in pancia
                try:
                    b = sock.recv(65536)
                except (ssl.SSLWantReadError, ssl.SSLWantWriteError, BlockingIOError):
                    break
                if not b:
                    return
                ingresso += b
                if not sock.pending():
                    break
            while len(ingresso) >= INTESTAZIONE.size:
                cmd, a0, a1, n, _crc, magic = INTESTAZIONE.unpack_from(ingresso)
                if magic != cmd ^ 0xFFFFFFFF or n > MAX_DATI:
                    raise ConnectionError("intestazione non valida")
                if len(ingresso) < INTESTAZIONE.size + n:
                    break
                dati = bytes(ingresso[INTESTAZIONE.size:INTESTAZIONE.size + n])
                del ingresso[:INTESTAZIONE.size + n]
                self._messaggio(cmd, a0, a1, dati)

    # -- messaggi ricevuti
    def _messaggio(self, cmd, a0, a1, dati):
        if cmd == A_OPEN:
            self._apri(a0, dati.rstrip(b"\0").decode("utf-8", "replace"))
            return
        canale = self.canali.get(a1)
        if cmd == A_OKAY and canale is not None:
            canale.ack.set()
        elif cmd == A_WRTE and canale is not None:
            canale.entrata.put(dati)
            self.invia(A_OKAY, canale.locale, canale.remoto)
        elif cmd == A_CLSE and canale is not None:
            canale.dal_client_chiuso()
        # il resto (CNXN ripetuto, canali sconosciuti) si ignora

    def _apri(self, remoto, servizio):
        canale = None
        srv = None
        try:
            with self.blocco:
                locale = self.prossimo_id
                self.prossimo_id += 1
            canale = Canale(self, locale, remoto)
            srv = self._crea_servizio(canale, servizio)
        except OSError as e:
            log("servizio %r rifiutato: %s" % (servizio, e))
        if srv is None:
            self.invia(A_CLSE, 0, remoto)
            return
        with self.blocco:
            self.canali[locale] = canale
        self.invia(A_OKAY, locale, remoto)
        canale.avvia(srv)

    def _crea_servizio(self, canale, s):
        if s.startswith("shell,"):
            opzioni, _, comando = s.partition(":")
            parti = opzioni.split(",")[1:]
            if "v2" in parti:
                return ShellV2(canale, comando, self.sandbox)
            return None
        if s == "sync:":
            return Sync(canale, self.sandbox)
        if s.startswith("localabstract:"):
            nome = s[len("localabstract:"):]
            if not nome or "/" in nome or nome.startswith(".") or "\0" in nome:
                return None
            percorso = os.path.join(self.sandbox.radice, "socket", nome)
            if not os.path.exists(percorso):
                return None
            u = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                # Percorso relativo: i socket Unix hanno un limite di ~100 byte.
                u.connect(os.path.join("socket", nome))
            except OSError:
                u.close()
                return None
            return SocketUnix(canale, u)
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cartella", required=True, help="radice del telefono finto")
    ap.add_argument("--porta", type=int, default=0)
    ap.add_argument("--non-abbinato", action="store_true",
                    help="chiude subito dopo il TLS, come con un PC non abbinato")
    a = ap.parse_args()

    cartella = os.path.abspath(a.cartella)
    os.makedirs(cartella, exist_ok=True)
    os.chdir(cartella)   # per i socket Unix con percorso relativo
    sandbox = Sandbox(cartella)
    ctx = crea_contesto_tls()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", a.porta))
    srv.listen(8)
    print("PORTA %d" % srv.getsockname()[1], flush=True)
    while True:
        s, _ = srv.accept()
        c = Connessione(s, ctx, sandbox, a.non_abbinato)
        threading.Thread(target=c.esegui, daemon=True).start()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
