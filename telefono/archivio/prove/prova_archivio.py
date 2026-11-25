#!/usr/bin/env python3
"""Prova di archivio (miniature, memoria, ricerca) sul telefono vero, col servizio vero.
Nessun dato personale nei file: dei file dell'utente si stampano solo conteggi e tempi.
Preparazione: jar completo da `strumenti/prova.sh telefono` in
~/.cache/yasde-sviluppo/archivio/yasde.jar. Richiede adb, ffmpeg, Pillow.
Prendere il turno del telefono prima (mkdir ~/.cache/yasde-sviluppo/telefono.occupato).
Uso: prova_archivio.py
I file di prova stanno in /sdcard/Download/yasde-prova-archivio/ e sono cancellati alla fine.
"""
import io, os, shutil, socket, struct, subprocess, sys, tempfile, threading, time

CACHE = os.path.expanduser("~/.cache/yasde-sviluppo/archivio")
JAR = "/data/local/tmp/yasde.jar"
PORTA = 38463
DIR = "/sdcard/Download/yasde-prova-archivio"
falliti = 0


def adb(*a):
    return subprocess.run(["adb"] + list(a), capture_output=True, text=True).stdout


def sh(c):
    return adb("shell", c)


def controlla(ok, nome, extra=""):
    global falliti
    print(("ok      " if ok else "FALLITA ") + nome + (("  [" + extra + "]") if extra else ""))
    if not ok:
        falliti += 1


def lancia():
    p = subprocess.Popen(["adb", "shell", "CLASSPATH=%s app_process / it.yasde.Servizio" % JAR],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    while True:
        r = p.stdout.readline()
        if not r:
            raise SystemExit("servizio terminato prima di PRONTO")
        if r.strip() == "PRONTO":
            def log():
                for r2 in p.stdout:
                    print("   log servizio:", r2.strip()[:200])
            threading.Thread(target=log, daemon=True).start()
            return p


def apri(riga):
    s = socket.create_connection(("127.0.0.1", PORTA), timeout=60)
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


def leggi(s):
    h = recv_n(s, 5)
    if h is None:
        return None
    t, n = struct.unpack(">BI", h)
    return t, recv_n(s, n) if n else b""


def msg(s, tipo, corpo=b""):
    s.sendall(struct.pack(">BI", tipo, len(corpo)) + corpo)


def st(t):
    b = t.encode("utf-8")
    return struct.pack(">H", len(b)) + b


def chiedi(c, tipo, corpo=b""):
    msg(c, tipo, corpo)
    return leggi(c)


def pid_servizio():
    r = sh("ps -A -o PID,ARGS | grep -E '^ *[0-9]+ app_process .*it.yasde.Servizio' | grep -v grep").split()
    return r[0] if r else None


def thread_servizio(pid):
    return int(sh("ls /proc/%s/task | wc -l" % pid).strip() or 0)


def prepara_file():
    from PIL import Image
    loc = tempfile.mkdtemp(prefix="archivio-", dir=os.path.expanduser("~/.cache/yasde-sviluppo"))
    os.makedirs(loc + "/cartella è foto")
    # foto 400x200: sinistra in alto rossa; orientamento EXIF 6 => mostrata 200x400, rosso in alto a destra
    im = Image.new("RGB", (400, 200), (0, 0, 255))
    for x in range(100):
        for y in range(100):
            im.putpixel((x, y), (255, 0, 0))
    ex = Image.Exif()
    ex[0x0112] = 6
    im.save(loc + "/foto ruotata è.jpg", exif=ex, quality=92)
    Image.new("RGBA", (300, 300), (0, 200, 0, 255)).save(loc + "/immagine città.png")
    open(loc + "/non immagine.txt", "w").write("solo testo\n" * 50)
    open(loc + "/cartella è foto/più foto.txt", "w").write("x")
    os.makedirs(loc + "/sotto/sotto2")
    open(loc + "/sotto/sotto2/profondo-prova-archivio.txt", "w").write("x")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc=size=320x240:rate=15:duration=2",
                    "-pix_fmt", "yuv420p", "-c:v", "libx264", loc + "/video prova.mp4"], check=True)
    return loc


def decodifica(b):
    from PIL import Image
    return Image.open(io.BytesIO(b))


def cerca(radice, testo, solo_cartelle=0, massimo=100, chiudi_dopo=None):
    """Ritorna (risultati, motivo, secondi). Con chiudi_dopo chiude il canale dopo quei secondi."""
    s = apri(b"cerca\n")
    msg(s, 0x78, st(radice) + st(testo) + struct.pack(">BH", solo_cartelle, massimo))
    t0 = time.time()
    ris, motivo = [], None
    while True:
        if chiudi_dopo is not None and time.time() - t0 > chiudi_dopo:
            s.close()
            return ris, None, time.time() - t0
        s.settimeout(0.2 if chiudi_dopo is not None else 60)
        try:
            m = leggi(s)
        except socket.timeout:
            continue
        if m is None:
            break
        t, c = m
        if t == 0x79:
            n = struct.unpack(">H", c[:2])[0]
            ris.append((c[2:2 + n].decode("utf-8"), c[2 + n]))
        elif t == 0x7a:
            motivo = c[0]
            break
    s.close()
    return ris, motivo, time.time() - t0


def main():
    loc = prepara_file()
    sh("rm -rf '%s'" % DIR)
    adb("push", loc, DIR)
    shutil.rmtree(loc)
    adb("push", CACHE + "/yasde.jar", JAR)
    p = lancia()
    try:
        adb("forward", "tcp:%d" % PORTA, "localabstract:yasde")
        c = apri(b"comandi\n")
        t, _ = chiedi(c, 0x01, struct.pack(">H", 1))
        controlla(t == 0x02, "SALUTO")
        pid = pid_servizio()
        base_thread = thread_servizio(pid)

        # miniature
        t, b = chiedi(c, 0x74, st(DIR + "/foto ruotata è.jpg") + struct.pack(">H", 100))
        ok = t == 0x02 and b[:2] == b"\xff\xd8"
        controlla(ok, "miniatura foto: JPEG")
        if ok:
            im = decodifica(b).convert("RGB")
            controlla(im.size == (50, 100), "foto raddrizzata e ridotta: 50x100", str(im.size))
            r, g, bl = im.getpixel((im.size[0] - 5, 5))
            controlla(r > 200 and g < 60 and bl < 60, "rosso in alto a destra (raddrizzata)", str((r, g, bl)))
            r, g, bl = im.getpixel((5, im.size[1] - 5))
            controlla(bl > 200 and r < 60, "blu in basso a sinistra", str((r, g, bl)))
        t, b = chiedi(c, 0x74, st(DIR + "/foto ruotata è.jpg") + struct.pack(">H", 512))
        controlla(t == 0x02 and decodifica(b).size == (200, 400), "foto piccola non ingrandita (200x400)")
        t, b = chiedi(c, 0x74, st(DIR + "/immagine città.png") + struct.pack(">H", 64))
        ok = t == 0x02 and decodifica(b).size == (64, 64)
        controlla(ok, "miniatura PNG 64x64")
        if ok:
            px = decodifica(b).convert("RGB").getpixel((30, 30))
            controlla(px[1] > 150 and px[0] < 60, "PNG: verde", str(px))
        t, b = chiedi(c, 0x74, st(DIR + "/video prova.mp4") + struct.pack(">H", 128))
        ok = t == 0x02 and b[:2] == b"\xff\xd8"
        controlla(ok, "miniatura video: JPEG")
        if ok:
            controlla(decodifica(b).size == (128, 96), "video: fotogramma 128x96", str(decodifica(b).size))
        for nome, percorso in (("inesistente", DIR + "/non c'e.jpg"), ("non immagine", DIR + "/non immagine.txt"),
                               ("cartella", DIR), ("percorso relativo", "foto.jpg")):
            t, b = chiedi(c, 0x74, st(percorso) + struct.pack(">H", 100))
            frase = b[2:2 + struct.unpack(">H", b[:2])[0]].decode("utf-8") if t == 0x03 else ""
            controlla(t == 0x03 and frase, "ERRORE per " + nome, frase)
        t, b = chiedi(c, 0x74, st(DIR + "/immagine città.png") + struct.pack(">H", 10))
        controlla(t == 0x03, "lato fuori limite: ERRORE")
        t, b = chiedi(c, 0x74, st("/data/local/tmp") + b"\x00")
        controlla(t == 0x03, "corpo troncato: ERRORE senza cadute")

        # memoria
        t, b = chiedi(c, 0x75)
        tot, lib = struct.unpack(">QQ", b)
        df = sh("df -k /storage/emulated/0 | tail -1").split()
        # df: Filesystem 1K-blocks Used Available ...
        dtot, dlib = int(df[1]) * 1024, int(df[3]) * 1024
        controlla(t == 0x02 and abs(tot - dtot) < dtot * 0.01, "memoria: totale come df", "%d MB / df %d MB" % (tot >> 20, dtot >> 20))
        controlla(abs(lib - dlib) < 200 << 20, "memoria: libero come df", "%d MB / df %d MB" % (lib >> 20, dlib >> 20))

        # ricerca mirata sui file di prova
        ris, mot, sec = cerca(DIR, "CITTA")
        controlla(len(ris) == 1 and mot == 0 and ris[0][0].endswith("immagine città.png"), "ricerca senza accenti/maiuscole", "%d risultati" % len(ris))
        ris, mot, sec = cerca(DIR, "foto")
        controlla(len(ris) == 3 and mot == 0, "ricerca 'foto': foto, cartella, file nella cartella", "%d" % len(ris))
        controlla(ris[0][0].count("/") <= ris[-1][0].count("/"), "ordine in ampiezza")
        ris, mot, sec = cerca(DIR, "foto", 1)
        controlla(len(ris) == 1 and ris[0][1] == 1, "solo cartelle")
        ris, mot, sec = cerca(DIR, "foto", 0, 1)
        controlla(len(ris) == 1 and mot == 1, "massimo 1 -> motivo 1")
        ris, mot, sec = cerca("/non/esiste", "x")
        controlla(mot == 2 and not ris, "radice inesistente: FINE motivo 2")
        ris, mot, sec = cerca("/data", "x")
        controlla(mot == 2 and len(ris) == 0, "/data protetta come partenza: FINE motivo 2", "%.2f s" % sec)

        # ricerche ampie: solo conteggi e tempi
        ris, mot, sec = cerca("/storage/emulated/0", "e", 0, 5000)
        print("   ampia 'e' max 5000: %d risultati, motivo %s, %.2f s" % (len(ris), mot, sec))
        controlla(mot in (0, 1), "ricerca ampia terminata")
        ris, mot, sec = cerca("/storage/emulated/0", "zzqxjk-nonesiste", 0, 5000)
        print("   ampia senza risultati: %d risultati, motivo %s, %.2f s" % (len(ris), mot, sec))
        controlla(mot in (0, 1) and sec < 40, "ricerca completa entro il tetto di tempo", "%.2f s" % sec)
        ris, mot, sec = cerca("/", "profondo-prova-archivio", 0, 5000)
        print("   da '/' file profondo: %d risultati, motivo %s, %.2f s" % (len(ris), mot, sec))
        controlla(len(ris) == 1 and ris[0][0].endswith("/sotto/sotto2/profondo-prova-archivio.txt"), "da / trova il file in sotto/sotto2", "%.2f s" % sec)
        ris, mot, sec = cerca("/", "zzqxjk-nonesiste", 0, 5000)
        print("   da '/' senza risultati: motivo %s, %.2f s" % (mot, sec))
        controlla(mot in (0, 1), "ricerca da / terminata")
        for brutta in ("//", "/./", "/storage/.."):
            ris, mot, sec = cerca(brutta, "profondo-prova-archivio", 0, 5000)
            controlla(len(ris) == 1 and ris[0][0].startswith("/sdcard") is False and "//" not in ris[0][0] and "/./" not in ris[0][0] and mot == 0 and sec < 5,
                      "partenza '%s': percorsi puliti, tempo come da /" % brutta, "%.2f s, %d, motivo %s" % (sec, len(ris), mot))
        ris, mot, sec = cerca("/proc", "self", 0, 5)
        controlla(mot in (0, 1) and len(ris) >= 1, "/proc come partenza esplicita: si cerca", "%d risultati" % len(ris))

        # interruzione: chiudo il canale dopo 0.1 s (la ricerca da / dura ~0.4 s)
        for i in range(3):
            ris, mot, sec = cerca("/", "zzqxjk-nonesiste", 0, 5000, chiudi_dopo=0.1)
            t0 = time.time()
            n = thread_servizio(pid)
            while n > base_thread and time.time() - t0 < 5:
                n = thread_servizio(pid)
            print("   interruzione %d: thread %d (base %d), dopo %.2f s dal controllo" % (i, n, base_thread, time.time() - t0))
            controlla(n <= base_thread, "dopo la chiusura i thread tornano alla base")
        # il servizio risponde ancora
        t, _ = chiedi(c, 0x75)
        controlla(t == 0x02, "servizio vivo dopo le interruzioni")

        # chiusura ordinata
        chiedi(c, 0x04)
        c.close()
    finally:
        try:
            p.stdin.close()
            p.wait(timeout=10)
        except Exception:
            p.kill()
        sh("rm -rf '%s'" % DIR)
        adb("forward", "--remove", "tcp:%d" % PORTA)
        sh("rm -f /data/local/tmp/yasde.jar")
    resti = sh("ls /sdcard/Download | grep yasde-prova-archivio; ls /data/local/tmp | grep -E '^yasde(\\.jar|-custode)'").strip()
    controlla(not resti, "pulizia completa", resti)
    print("FALLITE: %d" % falliti)
    sys.exit(1 if falliti else 0)


main()
