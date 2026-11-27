#!/usr/bin/env python3
"""Genera sito/pubblico/ dai sorgenti di sito/ e dai file del progetto.
Rilanciabile: la cartella di uscita viene rifatta da zero. Si ferma con un messaggio
chiaro se manca un file di partenza.

Variabili d'ambiente (tutte facoltative):
  YASDE_SITO_INDIRIZZO  indirizzo canonico del sito, con la barra finale (default: INDIRIZZO_PREDEFINITO; vuoto: nessun canonico)
  YASDE_SITO_CONTATTO   email di contatto del progetto (default: CONTATTO_PREDEFINITO; vuoto: nessun contatto nella pagina)
  YASDE_SITO_USCITA     cartella di uscita (default sito/pubblico): per le prove (vedi scegli_uscita per i limiti)
"""
import base64
import html
import io
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Dati pubblici del progetto, decisi dal titolare: valgono se le variabili d'ambiente non ci sono. Per generare una
# copia senza l'uno o senza l'altro basta impostare la variabile a vuoto (es. YASDE_SITO_CONTATTO= python3 ...).
INDIRIZZO_PREDEFINITO = "https://yasde.nicfio.it/"
CONTATTO_PREDEFINITO = "yasde@nicfio.it"

SITO = Path(__file__).resolve().parent
RADICE = SITO.parent
PREDEFINITA = SITO / "pubblico"
USCITA = PREDEFINITA  # fissata in main() dopo i controlli
CONTRASSEGNO = ".uscita-sito-yasde"  # file vuoto scritto in ogni cartella generata: dice «qui si può svuotare»
IMG = RADICE / "docs" / "sources" / "img"
TIPI = {".png": "image/png", ".webp": "image/webp"}
# Immagini incorporate nella pagina come data URI
IMMAGINI = {}
# Le schermate si incorporano come WebP (qualita' 84): stesse dimensioni, un quarto del peso del PNG dei manuali.
for _n in ("desktop-widgets.png", "window.png", "widgets.png", "launcher.png", "windows-snapped.png",
           "window-menu.png", "notifications.png", "phone-popup.png", "transfer.png"):
    IMMAGINI[_n] = IMG / _n
QUALITA_WEBP = 84


def fine(msg):
    sys.exit("costruisci.py: " + msg)


def scegli_uscita(valore, predefinita=None):
    """Cartella di uscita, controllata PRIMA di cancellare qualunque cosa. Lo script svuota la cartella,
    quindi una diversa da sito/pubblico si accetta solo se non esiste, è vuota o porta il contrassegno
    scritto dallo script stesso. Sono sempre rifiutate: radice del filesystem, home, radice del progetto,
    cartelle che la contengono, cartelle dentro il progetto (salvo sito/pubblico), tutto ciò che non è
    una cartella vera (file, collegamenti simbolici): vale anche per quella predefinita.
    `predefinita` serve solo alle prove (di norma è sito/pubblico)."""
    predefinita = predefinita or PREDEFINITA
    grezzo = Path(valore).expanduser() if valore else predefinita
    p = grezzo.resolve()
    pred = predefinita.resolve()
    if grezzo.is_symlink() or (p.exists() and not p.is_dir()):
        fine("cartella di uscita rifiutata: «%s» non è una cartella vera (file o collegamento simbolico)" % grezzo)
    if not valore:
        return predefinita
    if p == pred:
        return pred
    radice = RADICE.resolve()
    if p == Path(p.anchor) or p == Path.home().resolve() or p == radice or radice.is_relative_to(p):
        fine("YASDE_SITO_USCITA rifiutata: «%s» è la radice del filesystem, la home o contiene il progetto" % valore)
    if p.is_relative_to(radice):
        fine("YASDE_SITO_USCITA rifiutata: «%s» è dentro il progetto (l'unica ammessa è sito/pubblico)" % valore)
    if p.exists() and any(p.iterdir()) and not (p / CONTRASSEGNO).is_file():
        fine("YASDE_SITO_USCITA rifiutata: «%s» non è vuota e non è stata generata da questo script" % valore)
    return p


def repo_github():
    """Indirizzo del repository ricavato dal remote 'origin': mai scritto a mano."""
    try:
        url = subprocess.run(["git", "-C", str(RADICE), "remote", "get-url", "origin"],
                             capture_output=True, text=True, check=True, timeout=20).stdout.strip()
    except (subprocess.SubprocessError, OSError):
        fine("impossibile leggere il remote 'origin' di git")
    m = re.match(r"^(?:https://github\.com/|git@github\.com:)([^/]+/[^/]+?)(?:\.git)?/?$", url)
    if not m:
        fine("il remote 'origin' non è un indirizzo GitHub riconoscibile")
    return "https://github.com/" + m.group(1)


def versione():
    """Versione corrente, dalla stessa riga di meson.build che usa il generatore dei manuali."""
    f = RADICE / "meson.build"
    if not f.is_file():
        fine("manca " + str(f))
    m = re.search(r"^project\([^)]*version:\s*'([^']+)'", f.read_text(encoding="utf-8"), re.M | re.S)
    if not m:
        fine("versione non trovata in " + str(f))
    # meson dice 2.3.0; il prodotto e i nomi dei file usano «2.3»
    return ".".join(m.group(1).split(".")[:2])


def data_uri(nome):
    f = IMMAGINI[nome]
    if not f.is_file():
        fine("file di partenza mancante: " + str(f))
    try:
        from PIL import Image
    except ImportError:
        fine("serve la libreria Pillow (PIL) per preparare le schermate: python3 -m pip install pillow")
    buf = io.BytesIO()
    Image.open(f).convert("RGB").save(buf, "WEBP", quality=QUALITA_WEBP, method=6)
    return "data:%s;base64,%s" % (TIPI[".webp"], base64.b64encode(buf.getvalue()).decode())


def immagine(nome):
    if nome not in IMMAGINI:
        fine("immagine «%s» non elencata in IMMAGINI (costruisci.py)" % nome)
    return data_uri(nome)


def blocchi_condizionali(testo, valori):
    """<!--IF:NOME-->...<!--/IF--> resta solo se la variabile NOME non è vuota;
    <!--IFNOT:NOME-->...<!--/IF--> solo se è vuota."""
    def sost(m):
        pieno = bool(valori.get(m.group(2)))
        return m.group(3) if pieno == (m.group(1) == "IF") else ""
    return re.sub(r"<!--(IF|IFNOT):(\w+)-->\n?(.*?)<!--/IF-->\n?", sost, testo, flags=re.S)


# Marchio: icona del committente (solo sito/logo.png) + scritta YASDE accanto. Senza icona resta la sola scritta.
# L'icona non si incorpora com'è: con PIL se ne ricavano le misure che servono (doppia densità), con LANCZOS.
MARCHIO_DEFS = ('''<svg width="0" height="0" style="position:absolute" aria-hidden="true"><defs>
<linearGradient id="marchio-sfumatura" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#33d6ff"/><stop offset=".5" stop-color="#2a8cff"/><stop offset="1" stop-color="#1a6dff"/></linearGradient>
<symbol id="marchio-scritta" viewBox="0 0 900 360"><text x="0" y="290" font-family="system-ui,-apple-system,'Segoe UI',Roboto,'Noto Sans','DejaVu Sans',sans-serif" font-weight="800" font-size="300" textLength="880" lengthAdjust="spacingAndGlyphs"><tspan fill="currentColor">YAS</tspan><tspan fill="url(#marchio-sfumatura)">DE</tspan></text></symbol>
</defs></svg>''')
MARCHIO_TESTO = '<svg viewBox="0 0 900 360" role="img" aria-label="YASDE"><use href="#marchio-scritta"/></svg>'
LIMITE_LOGO = 5 * 1024 * 1024  # byte
# Altezze mostrate (px CSS) del logo: barra, piede, testata; si incorpora il doppio (schermi ad alta densità)
ALTEZZE = {"barra": 40, "piede": 32, "grande": 160}


def uri_png(im):
    buf = io.BytesIO()
    im.save(buf, "PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


def marchio():
    """(defs, marchio barra, marchio piede, logo grande, data URI favicon): senza logo solo la scritta."""
    f = SITO / "logo.png"
    if (SITO / "logo.svg").exists():
        fine("sito/logo.svg non è ammesso: il logo si accetta solo come sito/logo.png (Pillow lo ridisegna da capo). "
             "Se il logo è un SVG, convertilo prima in PNG e togli logo.svg.")
    if not f.is_file():
        print("ATTENZIONE: logo non ancora fornito (manca sito/logo.png): al suo posto c'è la sola scritta YASDE.")
        return MARCHIO_DEFS, MARCHIO_TESTO, MARCHIO_TESTO, "", ""
    if f.stat().st_size > LIMITE_LOGO:
        fine("logo.png rifiutato: più di %d MB" % (LIMITE_LOGO // (1024 * 1024)))
    try:
        from PIL import Image
    except ImportError:
        fine("serve la libreria Pillow (PIL) per preparare il logo: python3 -m pip install pillow")
    try:
        im = Image.open(f)
        im.load()
        im = im.convert("RGBA")
    except Exception as e:
        fine("logo.png non è un'immagine valida: %s" % e)

    def img(classe, h):  # doppia densità: si incorpora il logo ridisegnato al doppio dell'altezza mostrata
        w1 = round(im.width * h / im.height)
        r = im.resize((w1 * 2, h * 2), Image.LANCZOS)
        return '<img class="%s" src="%s" alt="" width="%d" height="%d">' % (classe, uri_png(r), w1, h)
    icone = {k: img("logo-icona", ALTEZZE[k]) for k in ALTEZZE}
    lato = 64  # icona della pagina: il logo, intero, in un quadrato trasparente
    r = im.copy()
    r.thumbnail((lato, lato), Image.LANCZOS)
    q = Image.new("RGBA", (lato, lato), (0, 0, 0, 0))
    q.alpha_composite(r, ((lato - r.width) // 2, (lato - r.height) // 2))
    fav = uri_png(q)
    barra = '<span class="marchio-coppia">%s%s</span>' % (icone["barra"], MARCHIO_TESTO)
    piede = '<span class="marchio-coppia">%s%s</span>' % (icone["piede"], MARCHIO_TESTO)
    grande = icone["grande"].replace("logo-icona", "logo-grande")
    return MARCHIO_DEFS, barra, piede, grande, fav


def compila(nome, valori):
    f = SITO / nome
    if not f.is_file():
        fine("file di partenza mancante: " + str(f))
    testo = f.read_text(encoding="utf-8")
    # Parti comuni (stile, testata, piede): si inseriscono prima di tutto il resto, una sola volta, senza ricorsione
    def includi(m):
        p = SITO / m.group(1)
        if not p.is_file():
            fine("file di partenza mancante: " + str(p))
        return p.read_text(encoding="utf-8").rstrip("\n")
    for _ in range(2):  # le parti possono includere altre parti (testa.html include stile.css)
        testo = re.sub(r"\{\{INCLUDI:([^}]+)\}\}", includi, testo)
    testo = blocchi_condizionali(testo, valori)
    testo = re.sub(r"\{\{IMG:([^}]+)\}\}", lambda m: immagine(m.group(1)), testo)
    for chiave, valore in valori.items():
        testo = testo.replace("{{" + chiave + "}}", valore)
        testo = testo.replace("{{JSON:" + chiave + "}}", json.dumps(valore)[1:-1])
    resto = re.search(r"\{\{[^}]*\}\}", testo)
    if resto:
        fine("segnaposto non sostituito in %s: %s" % (nome, resto.group(0)))
    return testo


# Indirizzo e contatto finiscono in href, mailto: e JSON: si accetta solo un formato semplice
RE_INDIRIZZO = re.compile(r"^https://(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\.)+[A-Za-z]{2,}/$")
RE_CONTATTO = re.compile(r"^[A-Za-z0-9._%+-]+@(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\.)+[A-Za-z]{2,}$")


def variabile(nome, regola, esempio, predefinito):
    """Valore da variabile d'ambiente; se la variabile non esiste vale il predefinito, se è vuota (impostata a
    «vuoto» in modo esplicito) non c'è nessun valore."""
    v = os.environ.get(nome)
    v = predefinito if v is None else v.strip()
    if v and not regola.match(v):
        fine("%s non valida: «%s». Formato atteso, per esempio: %s" % (nome, v, esempio))
    return html.escape(v, quote=True)


def main():
    global USCITA
    USCITA = scegli_uscita(os.environ.get("YASDE_SITO_USCITA", "").strip())
    licenza = RADICE / "LICENSE"
    manuali = [RADICE / "docs" / "User Manual.html", RADICE / "docs" / "Technical Manual.html"]
    for f in [licenza] + manuali:
        if not f.is_file():
            fine("file di partenza mancante: " + str(f))
    v = versione()
    repo = repo_github()
    # Il sito non ospita i file del programma: gli scaricamenti sono gli allegati della release su GitHub (tag v<versione>)
    rilascio = "%s/releases/download/v%s" % (repo, v)
    appimage = "%s/YASDE-%s-x86_64.AppImage" % (rilascio, v)
    valori = {
        "VERSION": v,
        "GITHUB_REPO": repo,
        "URL_APPIMAGE": appimage,
        "URL_SORGENTI": "%s/YASDE-%s-sorgenti.tar" % (rilascio, v),
        "URL_SOMME": rilascio + "/SHA256SUMS",
        "URL_RELEASE": "%s/releases/tag/v%s" % (repo, v),
        "INDIRIZZO": variabile("YASDE_SITO_INDIRIZZO", RE_INDIRIZZO, "https://<dominio>/", INDIRIZZO_PREDEFINITO),
        "CONTATTO": variabile("YASDE_SITO_CONTATTO", RE_CONTATTO, "<nome>@<dominio>", CONTATTO_PREDEFINITO),
        "LICENZA": html.escape(licenza.read_text(encoding="utf-8").strip()),
    }
    # Campo del JSON-LD preparato qui (i blocchi condizionali non si annidano): ha senso solo con l'indirizzo del sito
    valori["DOWNLOADJSON"] = '"downloadUrl":%s,' % json.dumps(appimage)
    # Tutti i controlli e le pagine sono pronti prima di cancellare la cartella di uscita
    defs, barra, piede, grande, fav = marchio()
    valori.update({"MARCHIO_DEFS": defs, "MARCHIO": barra, "MARCHIO_PIEDE": piede, "LOGO_GRANDE": grande,
                   "LOGO": fav, "LOGO_URI": fav})
    per_pagina = {  # (base dei collegamenti alle ancore, voce di menu attuale)
        "index.html": ("", "", ""),
        "docs.html": ("index.html", ' aria-current="page"', ""),
        "licence.html": ("index.html", "", ' aria-current="page"'),
    }
    pagine = {}
    for n, (base, ac_docs, ac_lic) in per_pagina.items():
        pagine[n] = compila(n, dict(valori, BASE=base, AC_DOCS=ac_docs, AC_LICENCE=ac_lic))
    # Si svuota il contenuto senza togliere la cartella: un server di prova acceso dentro di essa continua a funzionare
    USCITA.mkdir(parents=True, exist_ok=True)
    for voce in USCITA.iterdir():
        if voce.is_dir() and not voce.is_symlink():
            shutil.rmtree(voce)
        else:
            voce.unlink()
    for n, testo in pagine.items():
        (USCITA / n).write_text(testo, encoding="utf-8")
    for f in manuali:
        shutil.copyfile(f, USCITA / f.name)
    (USCITA / CONTRASSEGNO).write_text("")
    print("sito generato in " + str(USCITA))


if __name__ == "__main__":
    main()
