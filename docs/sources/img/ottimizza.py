"""Copia le foto grezze in docs/sources/img/ ricomprimendole SENZA perdita (PNG
ottimizzato): restano sotto ~200 kB, e finiscono incorporate nell'HTML dei manuali. Uso: ottimizza.py <cartella-grezze> <cartella-destinazione> <nome>..."""
import os
import sys

from PIL import Image

grezze, dest, *nomi = sys.argv[1:]
for n in nomi:
    src = os.path.join(grezze, n + ".png")
    if not os.path.exists(src):
        print("manca", src)
        continue
    im = Image.open(src).convert("RGB")
    out = os.path.join(dest, n + ".png")
    im.save(out, optimize=True)
    print("%s: %dx%d, %d kB" % (n, im.width, im.height, os.path.getsize(out) // 1024))
