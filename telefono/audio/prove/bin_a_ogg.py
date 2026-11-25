#!/usr/bin/env python3
"""Converte un file di pacchetti audio (formato di docs/protocollo.md 2.5) in Ogg Opus e lo analizza.
Uso: bin_a_ogg.py file.bin [uscita.opus] [Hz atteso]
Stampa: pacchetti, intervalli di tempo diversi da 20 ms, durata, e con ffmpeg la frequenza dominante
(Goertzel) nei tratti con suono. Serve ffmpeg."""
import struct, subprocess, sys, math, tempfile, os


def leggi_pacchetti(dati):
    out, i = [], 0
    while i + 10 <= len(dati):
        t, n = struct.unpack(">QH", dati[i:i + 10])
        if i + 10 + n > len(dati):
            break
        out.append((t, dati[i + 10:i + 10 + n]))
        i += 10 + n
    return out, len(dati) - i


def crc_tabella():
    t = []
    for i in range(256):
        r = i << 24
        for _ in range(8):
            r = ((r << 1) ^ 0x04C11DB7) if r & 0x80000000 else (r << 1)
            r &= 0xFFFFFFFF
        t.append(r)
    return t


CRC = crc_tabella()


def crc(b):
    r = 0
    for x in b:
        r = ((r << 8) & 0xFFFFFFFF) ^ CRC[((r >> 24) & 0xFF) ^ x]
    return r


def pagina(tipo, granule, seriale, seq, pacchetti):
    segs = bytearray()
    for p in pacchetti:
        n = len(p)
        while n >= 255:
            segs.append(255)
            n -= 255
        segs.append(n)
    assert len(segs) < 256
    h = b"OggS" + struct.pack("<BBqIII", 0, tipo, granule, seriale, seq, 0) + bytes([len(segs)]) + bytes(segs)
    corpo = b"".join(pacchetti)
    c = crc(h + corpo)
    return h[:22] + struct.pack("<I", c) + h[26:] + corpo


def scrivi_ogg(pacchetti, nome):
    # un pacchetto Opus da 20 ms per pagina: semplice, e le durate dei pacchetti si ricavano dal TOC
    testa = b"OpusHead" + struct.pack("<BBHIhB", 1, 2, 312, 48000, 0, 0)
    tag = b"OpusTags" + struct.pack("<I", 7) + b"prova  " + struct.pack("<I", 0)
    seriale = 0x50484F4E
    out = pagina(2, 0, seriale, 0, [testa]) + pagina(0, 0, seriale, 1, [tag])
    gran = 312
    for k, (t, d) in enumerate(pacchetti):
        gran += 960
        ultimo = k == len(pacchetti) - 1
        out += pagina(4 if ultimo else 0, gran, seriale, 2 + k, [d])
    open(nome, "wb").write(out)


def goertzel(campioni, hz, fs=48000):
    k = 2 * math.cos(2 * math.pi * hz / fs)
    s1 = s2 = 0.0
    for x in campioni:
        s = x + k * s1 - s2
        s2, s1 = s1, s
    return (s1 * s1 + s2 * s2 - k * s1 * s2) / len(campioni)


def main():
    nome = sys.argv[1]
    pacchetti, avanzo = leggi_pacchetti(open(nome, "rb").read())
    print("pacchetti:", len(pacchetti), "byte residui:", avanzo)
    salti = [(i, pacchetti[i][0] - pacchetti[i - 1][0]) for i in range(1, len(pacchetti))
             if pacchetti[i][0] - pacchetti[i - 1][0] != 20000]
    print("intervalli diversi da 20 ms:", len(salti), salti[:5])
    print("primo tempo:", pacchetti[0][0], "durata dai tempi: %.3f s" % ((pacchetti[-1][0] + 20000) / 1e6))
    toc = [d[0] >> 3 for _, d in pacchetti]
    print("config TOC distinte:", sorted(set(toc)), "(31 = CELT a banda piena 20 ms)")
    opus = sys.argv[2] if len(sys.argv) > 2 else tempfile.mktemp(suffix=".opus")
    scrivi_ogg(pacchetti, opus)
    pcm = subprocess.run(["ffmpeg", "-v", "error", "-i", opus, "-f", "s16le", "-ac", "1", "-ar", "48000", "-"],
                         capture_output=True).stdout
    n = len(pcm) // 2
    camp = struct.unpack("<%dh" % n, pcm[:n * 2])
    print("decodificato: %d campioni = %.3f s" % (n, n / 48000))
    atteso = int(sys.argv[3]) if len(sys.argv) > 3 else 440
    blocco = 4800  # 100 ms
    righe = []
    for b in range(0, n - blocco, blocco * 5):
        seg = camp[b:b + blocco]
        rms = math.sqrt(sum(x * x for x in seg) / len(seg))
        cand = {hz: goertzel(seg, hz) for hz in range(100, 4000, 20)}
        meglio = max(cand, key=cand.get)
        righe.append((b / 48000, rms, meglio))
    for t, rms, hz in righe:
        print("  t=%.1fs rms=%6.0f  frequenza dominante ~%d Hz" % (t, rms, hz if rms > 50 else 0))
    con_suono = [hz for _, rms, hz in righe if rms > 200]
    ok = bool(con_suono) and all(abs(hz - atteso) <= 20 for hz in con_suono)
    print("TONO ATTESO %d Hz: %s" % (atteso, "SI" if ok else "NO"))
    if len(sys.argv) <= 2:
        os.unlink(opus)


main()
