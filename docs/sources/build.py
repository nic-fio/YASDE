"""Generatore dei manuali di YASDE, nello stile comune dei manuali dei progetti del committente.

Produce due file HTML autosufficienti (nessun file esterno), in inglese:
  docs/Technical Manual.html   dai capitoli in technical/chNN_*.py
  docs/User Manual.html        dai capitoli in user/chNN_*.py

Ogni capitolo espone CHAPTER = (titolo, [(titolo_sezione, html), ...]).
I segnaposto «FIG» e «TAB» nelle didascalie diventano «Figure N.M» e
«Table N.M», numerati per capitolo; rif("Titolo di una sezione") diventa il
collegamento a quella sezione; shot("file.png", didascalia) inserisce una
schermata di img/ come figura. Lo stile è style.css e lo script (ricerca nella
barra laterale, pulsante Copy sui blocchi di comandi) è manual.js: sono il
canone comune, identico byte per byte, e non si modificano qui; le sole
aggiunte di YASDE stanno in EXTRA_CSS, dopo.
La mappa dei file e la tabella dei numeri si contano dai sorgenti a ogni generazione.

    python3 docs/sources/build.py              rigenera i due manuali
    python3 docs/sources/build.py --controlla  controlla che siano allineati al codice
    ... --prova CARTELLA   (con o senza --controlla) usa capitoli in CARTELLA/{user,technical},
                           schermate in CARTELLA/img e scrive in CARTELLA/out: per provare
                           il generatore senza toccare docs/

Servono le librerie pygments e Pillow (pacchetti python3-pygments e python3-pil).
"""
import html
import importlib.util
import math
import os
import pathlib
import re
import sys
import tempfile

# Niente __pycache__ sotto docs/: una copia compilata di un modulo potrebbe conservare testo vecchio.
sys.dont_write_bytecode = True

# Niente __pycache__ accanto ai capitoli: una copia vecchia (stessa data, a un secondo) darebbe un manuale vecchio.
sys.dont_write_bytecode = True

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
DATE = "November 2026"
VERSION = re.search(r"^project\([^)]*version:\s*'([^']+)'", (ROOT / "meson.build").read_text(), re.M | re.S).group(1)
# Licenza del progetto (file LICENSE nella radice): compare nel piè di pagina dei manuali. Se questa costante
# fosse vuota i manuali non potrebbero citare nessuna licenza (lo controlla --controlla).
LICENZA = "MIT with Commons Clause"
NOME = "YASDE"
# Dati pubblici del progetto, voluti dal titolare: sono gli UNICI indirizzi e contatti ammessi nei manuali.
SITO_HOST = "yasde.nicfio.it"
SITO_URL = "https://" + SITO_HOST + "/"
CONTATTO = "yasde@nicfio.it"
# Oltre al sito del progetto, solo i due indirizzi dei crediti che la licenza dei dati meteo richiede (capitolo del meteo).
HOST_AMMESSI = {SITO_HOST, "open-meteo.com", "www.geonames.org"}
SOTTOTITOLO = "YASDE — Yet Another Samsung Dex Emulator"
AVVISO = ("Independent project, not affiliated with, endorsed by or sponsored by Samsung. "
          "Samsung and Samsung DeX are trademarks of Samsung Electronics Co., Ltd.")

# Cartelle dei capitoli e delle schermate: cambiano solo con --prova. Passano per una variabile
# d'ambiente perché i capitoli fanno «from build import *»: ne caricano una copia del modulo.
_PROVA = os.environ.get("YASDE_MANUALI_PROVA")
CAPITOLI = pathlib.Path(_PROVA) if _PROVA else HERE
IMG = (pathlib.Path(_PROVA) if _PROVA else HERE) / "img"
USCITA = pathlib.Path(_PROVA) / "out" if _PROVA else ROOT / "docs"

MANUALS = {
    "tecnico": dict(dir="technical", file="Technical Manual.html", title="Technical Manual — YASDE",
                    h1="Technical Manual"),
    "utente": dict(dir="user", file="User Manual.html", title="User Manual — YASDE", h1="User Manual"),
}

# Dopo il canone (style.css), solo le regole per elementi che esistono soltanto nei manuali di YASDE.
EXTRA_CSS = """
/* Solo per YASDE: sottotitolo e avviso in copertina, schermate, terminali (titolo, prompt, righe del programma, errori), comandi dentro le procedure,
   albero delle cartelle, pastiglie di stato, glossario, righe di gruppo nelle tabelle */
.cover-sub{margin-top:14px;font-size:17px;font-weight:600;color:#003a90}
.cover-note{margin-top:8px;max-width:760px;font-size:12.5px;line-height:1.5;color:#5a6f93}
.fig.shot{text-align:center}
.fig.shot img{display:block;max-width:100%;height:auto;margin:0 auto;border:1px solid #cbd5e1;border-radius:8px;background:#fff}
.fig.shot .cap{margin-top:8px}
.steps .code-w{margin:10px 0 4px}
.code .pr{color:#93c5fd} .code .cmd{color:#fff;font-weight:700} .code .am{color:#fdba74;font-weight:600}
.code .er{color:#fca5a5} .code .dim{color:#94a3b8}
.code-t{display:block;font-family:Outfit,Segoe UI,Arial,sans-serif;font-size:11px;font-weight:700;letter-spacing:.12em;text-transform:uppercase;color:#64748b;margin:16px 0 -8px}
.tree{background:transparent;border:0;padding:4px 8px;font-family:ui-monospace,Menlo,Consolas,monospace;font-size:13px;line-height:1.6;color:#0f172a;overflow-x:auto;margin:0;text-align:left}
.tree .d{color:#0050C0;font-weight:700} .tree .m{color:#64748b}
.pill{display:inline-block;font-size:12px;font-weight:700;border-radius:999px;padding:.1em .65em;white-space:nowrap}
.p-ok{background:#dcfce7;color:#166534} .p-wait{background:#dbeafe;color:#1e40af} .p-snooze{background:#fef3c7;color:#92400e}
.p-off{background:#e2e8f0;color:#475569} .p-info{background:#f1f5f9;color:#334155;border:1px solid #cbd5e1}
.gloss{margin:12px 0} .gloss dt{font-weight:700;color:#003a90;font-size:15px;margin-top:12px} .gloss dd{margin:2px 0 0;color:#334155;font-size:14px}
.tbl-group td{background:#eef2f7 !important;font-weight:700;color:#003a90}
"""

# ── Testo ───────────────────────────────────────────────────────────────
def esc(t):
    return html.escape(t, quote=False)


def c(t):
    return f"<code>{esc(t)}</code>"


def ui(label):
    """Etichetta esatta di una finestra o di un pulsante. Le etichette lunghe (messaggi interi) vanno
    a capo, per non uscire dalle tabelle."""
    stile = ' style="white-space:normal"' if len(label) > 40 else ""
    return f'<span class="ui"{stile}>{esc(label)}</span>'


def key(*keys):
    return "+".join(f"<kbd>{esc(k)}</kbd>" for k in keys)


def pill(text, kind):
    return f'<span class="pill p-{kind}">{esc(text)}</span>'


def p(t, lead=False):
    return f'<p class="lead">{t}</p>' if lead else f"<p>{t}</p>"


def h4(t):
    return f"<h4>{t}</h4>"


def ul(items):
    return '<ul class="ul">' + "".join(f"<li>{i}</li>" for i in items) + "</ul>"


def ol(items):
    return '<ol class="ol">' + "".join(f"<li>{i}</li>" for i in items) + "</ol>"


def steps(items):
    return '<ol class="steps">' + "".join(f"<li>{i}</li>" for i in items) + "</ol>"


def note(t, title="Note."):
    return f'<div class="callout c-info"><div class="callout-i">i</div><div><b>{title}</b> {t}</div></div>'


def warn(t, title="Warning."):
    return f'<div class="callout c-warn"><div class="callout-i">!</div><div><b>{title}</b> {t}</div></div>'


def tip(t, title="Tip."):
    return f'<div class="callout c-ok"><div class="callout-i">+</div><div><b>{title}</b> {t}</div></div>'


def rif(titolo):
    """Collegamento a una sezione del manuale, per titolo; lo risolve number()."""
    return f"«RIF:{titolo}»"


def table(head, rows, cap, tid=None):
    """rows: liste di celle; una stringa sola fa da riga di gruppo su tutta la larghezza."""
    th = "".join(f"<th>{h}</th>" for h in head)
    tr = []
    for r in rows:
        if isinstance(r, str):
            tr.append(f'<tr class="tbl-group"><td colspan="{len(head)}">{r}</td></tr>')
        else:
            tr.append("<tr>" + "".join(f"<td>{x}</td>" for x in r) + "</tr>")
    idattr = f' id="{tid}"' if tid else ""
    return (f'<figure class="tbl-wrap"><table class="tbl"{idattr}><thead><tr>{th}</tr></thead>'
            f'<tbody>{"".join(tr)}</tbody></table><figcaption class="cap">{cap}</figcaption></figure>')


def dl(pairs, cls="deflist"):
    return f'<dl class="{cls}">' + "".join(f"<dt>{a}</dt><dd>{b}</dd>" for a, b in pairs) + "</dl>"


def code(text, lang="bash", title=""):
    from pygments import lex
    from pygments.lexers import (BashLexer, CLexer, JavaLexer, PythonLexer, TextLexer)
    from pygments.token import Comment, Keyword, String
    lx = {"bash": BashLexer, "c": CLexer, "java": JavaLexer, "python": PythonLexer,
          "text": TextLexer}[lang]()
    out = []
    for tok, val in lex(text.strip("\n"), lx):
        e = html.escape(val, quote=True)
        cls = "cm" if tok in Comment else "st" if tok in String else "k" if tok in Keyword else None
        if cls and val.strip():
            e = "\n".join(f'<span class="{cls}">{x}</span>' if x else x for x in e.split("\n"))
        out.append(e)
    head = f'<span class="code-t">{esc(title)}</span>' if title else ""
    return head + '<pre class="code"><code>' + "".join(out).rstrip("\n") + "</code></pre>"


PROMPT = re.compile(r"^([$#](?: |$))(.*)$")


def term(text, title=""):
    """Una sessione di terminale: prompt e comandi, righe del programma, errori, commenti."""
    lines = []
    for line in text.strip("\n").split("\n"):
        m = PROMPT.match(line)
        if m:
            cmd, _, comment = m.group(2).partition("   #")
            lines.append(f'<span class="pr">{esc(m.group(1))}</span><span class="cmd">{esc(cmd)}</span>'
                         + (f'<span class="dim">   #{esc(comment)}</span>' if comment else ""))
        elif line.startswith("errore") or line.startswith("Error"):
            lines.append(f'<span class="er">{esc(line)}</span>')
        elif line.startswith("["):
            lines.append(f'<span class="am">{esc(line)}</span>')
        else:
            lines.append(esc(line))
    head = f'<span class="code-t">{esc(title)}</span>' if title else ""
    return head + '<pre class="code"><code>' + "\n".join(lines) + "</code></pre>"


def tree(lines, cap):
    """Albero di cartelle: righe 'percorso  # commento'."""
    out = []
    for line in lines:
        path, _, comment = line.partition("  #")
        m = re.match(r"^([\s│├└─]*)(.*)$", path)
        pre, name = m.group(1), m.group(2)
        cls = "d" if name.rstrip().endswith("/") else ""
        out.append(esc(pre) + (f'<span class="{cls}">{esc(name)}</span>' if cls else esc(name))
                   + (f'<span class="m">  #{esc(comment)}</span>' if comment else ""))
    return f'<figure class="fig"><pre class="tree">' + "\n".join(out) + f'</pre><figcaption class="cap">{cap}</figcaption></figure>'


# ── Figure SVG ───────────────────────────────
COL = {"dark": "#475569", "blue": "#0050C0", "light": "#3b82f6", "navy": "#003a90", "soft": "#eef2f7",
       "green": "#16a34a", "amber": "#d97706", "grey": "#94a3b8", "white": "#ffffff"}
FONT = 'font-family="Outfit,Segoe UI,Arial,sans-serif"'


def box(x, y, w, h, title, sub="", color="blue", size=13):
    fill = COL[color]
    light = color in ("soft", "white")
    fg = "#334155" if light else "#fff"
    stroke = ' stroke="#cbd5e1"' if light else ""
    cy = y + h / 2
    s = f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="9" fill="{fill}"{stroke}/>'
    if sub:
        s += (f'<text x="{x + w / 2}" y="{cy - 7}" text-anchor="middle" dominant-baseline="middle" '
              f'font-size="{size}" font-weight="700" fill="{fg}">{esc(title)}</text>'
              f'<text x="{x + w / 2}" y="{cy + 12}" text-anchor="middle" font-size="10.5" fill="{fg}" opacity="0.88">{esc(sub)}</text>')
    else:
        s += (f'<text x="{x + w / 2}" y="{cy}" text-anchor="middle" dominant-baseline="middle" '
              f'font-size="{size}" font-weight="700" fill="{fg}">{esc(title)}</text>')
    return s


def diamond(cx, cy, w, h, text, color="amber"):
    pts = f"{cx},{cy - h / 2} {cx + w / 2},{cy} {cx},{cy + h / 2} {cx - w / 2},{cy}"
    return (f'<polygon points="{pts}" fill="{COL[color]}"/>'
            f'<text x="{cx}" y="{cy}" text-anchor="middle" dominant-baseline="middle" font-size="12.5" '
            f'font-weight="700" fill="#fff">{esc(text)}</text>')


def _head(x2, y2, ang, color):
    a1 = (x2 - 8 * math.cos(ang) + 5 * math.sin(ang), y2 - 8 * math.sin(ang) - 5 * math.cos(ang))
    a2 = (x2 - 8 * math.cos(ang) - 5 * math.sin(ang), y2 - 8 * math.sin(ang) + 5 * math.cos(ang))
    return f'<path d="M{a1[0]:.1f} {a1[1]:.1f} L{x2} {y2} L{a2[0]:.1f} {a2[1]:.1f} Z" fill="{color}"/>'


def arrow(x1, y1, x2, y2, color="#0050C0", dash=False, label="", lx=None, ly=None):
    ang = math.atan2(y2 - y1, x2 - x1)
    d = ' stroke-dasharray="5 4"' if dash else ""
    s = (f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="2.2"{d}/>'
         + _head(x2, y2, ang, color))
    if label:
        s += text((x1 + x2) / 2 if lx is None else lx, (y1 + y2) / 2 - 7 if ly is None else ly, label, 11, "#334155")
    return s


def path(points, color="#0050C0", dash=False, label="", lx=0, ly=0):
    """Spezzata con freccia finale: points = [(x, y), ...]."""
    d = " ".join(("M" if i == 0 else "L") + f"{x} {y}" for i, (x, y) in enumerate(points))
    dd = ' stroke-dasharray="5 4"' if dash else ""
    (xa, ya), (xb, yb) = points[-2], points[-1]
    s = (f'<path d="{d}" fill="none" stroke="{color}" stroke-width="2.2"{dd}/>'
         + _head(xb, yb, math.atan2(yb - ya, xb - xa), color))
    if label:
        s += text(lx, ly, label, 11, "#334155")
    return s


def text(x, y, t, size=12, color="#334155", weight="400", anchor="middle", halo=True):
    """Etichetta; il contorno chiaro la stacca dalle linee che attraversa."""
    h = ' stroke="#f8fafc" stroke-width="4" stroke-linejoin="round" paint-order="stroke"' if halo else ""
    return (f'<text x="{x}" y="{y}" text-anchor="{anchor}" font-size="{size}" font-weight="{weight}" '
            f'fill="{color}"{h}>{esc(t)}</text>')


def zone(x, y, w, h, title, color="#e8eef7"):
    return (f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="12" fill="{color}" stroke="#d5deea"/>'
            + text(x + 14, y + 20, title, 11.5, "#003a90", "700", "start", False))


def fig(body, width, height, cap, title=""):
    """Figura SVG; come nei manuali di IR la didascalia è dentro il disegno, in basso."""
    t = text(20, 28, title, 12, "#003a90", "700", "start") if title else ""
    didascalia = html.unescape(re.sub(r"<[^>]+>", "", cap))
    height += 30
    c = text(width / 2, height - 16, didascalia, 12, "#475569", "400", "middle", False)
    return (f'<figure class="fig"><svg viewBox="0 0 {width} {height}" xmlns="http://www.w3.org/2000/svg" {FONT} '
            f'role="img"><rect width="{width}" height="{height}" fill="#f8fafc" rx="12"/>{t}{body}{c}</svg></figure>')


def shot(nome_file, cap):
    """Schermata PNG di img/<nome_file> come figura numerata, incorporata nella pagina (data URI)."""
    import base64
    f = IMG / nome_file
    if not f.is_file():
        raise SystemExit(f"shot(«{nome_file}»): la schermata manca: {f}")
    dati = base64.b64encode(f.read_bytes()).decode()
    alt = html.escape(html.unescape(re.sub(r"<[^>]+>", "", cap)), quote=True)
    return (f'<figure class="fig shot"><img src="data:image/png;base64,{dati}" alt="{alt}" loading="lazy">'
            f'<figcaption class="cap">{cap}</figcaption></figure>')


def flow(nodes, cap, title="", width=900):
    """Flusso orizzontale: nodes = [(titolo, sottotitolo, colore)]."""
    n = len(nodes)
    gap = 36
    w = (width - 40 - gap * (n - 1)) / n
    top = 50 if title else 26
    h = 64
    s = []
    for i, (t, sub, col) in enumerate(nodes):
        x = 20 + i * (w + gap)
        s.append(box(x, top, w, h, t, sub, col))
        if i < n - 1:
            s.append(arrow(x + w + 2, top + h / 2, x + w + gap - 2, top + h / 2))
    return fig("".join(s), width, top + h + 26, cap, title)


def seq(actors, events, cap, title="", width=900):
    """Diagramma di sequenza.
    actors = [(nome, sottotitolo, colore)];
    events = (da, a, testo[, tratteggio]) per un messaggio, ("nota", i, testo) per una nota,
             ("sep", testo) per una separazione con testo."""
    n = len(actors)
    top = 50 if title else 22
    colw = (width - 40) / n
    xs = [20 + colw * i + colw / 2 for i in range(n)]
    bw, bh = min(colw - 24, 190), 50
    y = top + bh + 34
    body = []
    for ev in events:
        if ev[0] == "nota":
            _, i, t = ev
            tw = max(len(t) * 6.6 + 24, 90)
            body.append(f'<rect x="{xs[i] - tw / 2}" y="{y - 15}" width="{tw}" height="26" rx="6" fill="#fff7ed" stroke="#fdba74"/>')
            body.append(text(xs[i], y + 3, t, 11, "#9a3412", "600", "middle", False))
            y += 40
        elif ev[0] == "sep":
            body.append(f'<line x1="24" y1="{y - 4}" x2="{width - 24}" y2="{y - 4}" stroke="#cbd5e1" stroke-dasharray="2 4"/>')
            tw = len(ev[1]) * 6.4 + 22
            body.append(f'<rect x="{width / 2 - tw / 2}" y="{y - 15}" width="{tw}" height="22" rx="11" fill="#f8fafc" stroke="#cbd5e1"/>')
            body.append(text(width / 2, y + 0, ev[1], 11, "#475569", "600", "middle", False))
            y += 36
        else:
            a, b, t = ev[0], ev[1], ev[2]
            dash = len(ev) > 3 and ev[3]
            color = "#475569" if dash else "#0050C0"
            if a == b:
                x = xs[a]
                body.append(f'<path d="M{x} {y - 8} h34 v18 h-30" fill="none" stroke="{color}" stroke-width="2"/>'
                            + _head(x + 2, y + 10, math.pi, color))
                body.append(text(x + 42, y + 5, t, 11, "#334155", "400", "start"))
                y += 42
            else:
                x1, x2 = xs[a], xs[b]
                off = 5 if x2 > x1 else -5
                body.append(arrow(x1 + off, y, x2 - off, y, color, dash))
                body.append(text((x1 + x2) / 2, y - 8, t, 11, "#334155"))
                y += 42
    height = y + 10
    head = []
    for i, (name, sub, color) in enumerate(actors):
        head.append(f'<line x1="{xs[i]}" y1="{top + bh}" x2="{xs[i]}" y2="{height - 14}" stroke="#94a3b8" stroke-width="1.5" stroke-dasharray="4 4"/>')
        head.append(box(xs[i] - bw / 2, top, bw, bh, name, sub, color))
    return fig("".join(head + body), width, height, cap, title)


# ── Sorgenti: mappa dei file e numeri ────────────────────────────────────
ESTENSIONI_SCRIPT = {".sh", ".py", ".desktop", ".build", ""}


def sorgenti():
    """I file che il manuale descrive, con le loro righe: C (src/), Java e script del telefono
    (telefono/), strumenti/, pacchetto/ (senza immagini e documenti) e il generatore dei manuali."""
    file = []
    file += sorted(p for p in (ROOT / "src").rglob("*") if p.suffix in (".c", ".h"))
    file += sorted((ROOT / "telefono").rglob("*.java"))
    file += sorted(p for p in (ROOT / "telefono").rglob("*") if p.suffix in (".sh", ".py"))
    for cartella in ("strumenti", "pacchetto"):
        file += sorted(p for p in (ROOT / cartella).rglob("*")
                       if p.is_file() and p.suffix in ESTENSIONI_SCRIPT | {".c"})
    file += [ROOT / "meson.build", HERE / "build.py"]
    return {p.relative_to(ROOT).as_posix(): p.read_bytes().count(b"\n") for p in dict.fromkeys(file)
            if p.is_file() and "__pycache__" not in p.parts}


def righe(n):
    return f"{n:,}"


def file_map(groups, cap):
    """Mappa dei file: groups = [(gruppo, [(percorso, ruolo)])]. Fallisce se un
    sorgente manca dalla mappa o se la mappa cita un file che non c'è più."""
    tutti = sorgenti()
    citati = [f for _, rows in groups for f, _ in rows]
    mancano = sorted(set(tutti) - set(citati))
    in_piu = sorted(set(citati) - set(tutti))
    if mancano or in_piu:
        raise SystemExit("mappa dei file del manuale da aggiornare (il capitolo con la mappa dei sorgenti):"
                         + "".join(f"\n  manca {f}" for f in mancano)
                         + "".join(f"\n  non esiste più {f}" for f in in_piu))
    out = []
    for gruppo, rows in groups:
        out.append(gruppo)
        for f, role in rows:
            out.append([c(f), righe(tutti[f]), role])
    return table(["File", "Lines", "Role"], out, cap, "mappa-file")


def numeri(parti, cap):
    """Tabella «il progetto in numeri»: parti = [(nome, dove, contenuto, regola)];
    ogni file va nella prima parte la cui regola lo accetta."""
    conti = [0] * len(parti)
    for f, n in sorgenti().items():
        for i, (_, _, _, regola) in enumerate(parti):
            if regola(f):
                conti[i] += n
                break
    rows = [[nome, dove, righe(conti[i]), cont] for i, (nome, dove, cont, _) in enumerate(parti)]
    rows.append(["<b>Total</b>", "", f"<b>{righe(sum(conti))}</b>", ""])
    return table(["Part", "Where", "Lines", "Contents"], rows, cap)


def conta(estensione):
    """Righe di tutti i file di un tipo (per la copertina del primo capitolo)."""
    return sum(n for f, n in sorgenti().items() if f.endswith(estensione))


# ── Copertina ──────────────────────────────────────────────────────────
def logo():
    """Il logo di YASDE (pacchetto/yasde.svg), incorporato nella pagina come data URI: nessun file esterno."""
    import base64
    dati = base64.b64encode((ROOT / "pacchetto" / "yasde.svg").read_bytes()).decode()
    return f'<div class="cover-logo"><img src="data:image/svg+xml;base64,{dati}" alt="YASDE"></div>'


SEARCH = ('<div class="srch" id="srch" hidden><label class="toc-head" for="srch-q">Search</label><input type="search" '
          'id="srch-q" class="srch-q" placeholder="A word, an option…  ( / )" autocomplete="off" spellcheck="false">'
          '<div class="srch-info" id="srch-info" aria-live="polite"></div><ol class="srch-res" id="srch-res"></ol></div>')


# ── Assemblaggio ─────────────────────────────────────────────────────────
def load_chapters(kind):
    chapters = []
    sys.path.insert(0, str(HERE))
    for f in sorted((CAPITOLI / MANUALS[kind]["dir"]).glob("ch[0-9][0-9]_*.py")):
        spec = importlib.util.spec_from_file_location(f"{kind}_{f.stem}", f)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        chapters.append(mod.CHAPTER)
    return chapters


def number(doc, sezioni):
    out, counters, cur = [], {}, 0
    for part in re.split(r'(<section class="chapter" id="ch\d+">)', doc):
        m = re.match(r'<section class="chapter" id="ch(\d+)">', part)
        if m:
            cur = int(m.group(1))
            counters = {"FIG": 0, "TAB": 0}
            out.append(part)
            continue

        def num(mm):
            k = mm.group(1)
            counters[k] += 1
            return f'{"Figure" if k == "FIG" else "Table"} {cur}.{counters[k]}'
        out.append(re.sub(r"«(FIG|TAB)»", num, part) if counters else part)

    def collega(mm):
        titolo = mm.group(1)
        if titolo not in sezioni:
            raise SystemExit(f"rif(«{titolo}»): nessuna sezione ha questo titolo")
        sid, numero = sezioni[titolo]
        return f'<a href="#{sid}">{numero} “{esc(titolo)}”</a>'
    return re.sub(r"«RIF:([^»]+)»", collega, "".join(out))


def build(kind, outdir=None):
    outdir = USCITA if outdir is None else outdir
    meta = MANUALS[kind]
    css = (HERE / "style.css").read_text() + EXTRA_CSS
    js = (HERE / "manual.js").read_text()
    toc, body, sezioni = [], [], {}
    for ci, (ctitle, sections) in enumerate(load_chapters(kind), 1):
        toc.append(f'<li class="toc-ch"><a href="#ch{ci}"><span class="toc-n">{ci}</span><span class="toc-t">{esc(ctitle)}</span></a></li>')
        body.append(f'<section class="chapter" id="ch{ci}"><div class="ch-head"><span class="ch-kick">Chapter {ci}</span>'
                    f'<h2 class="h-ch">{esc(ctitle)}</h2></div>')
        sezioni[ctitle] = (f"ch{ci}", f"ch. {ci}")
        for si, (stitle, shtml) in enumerate(sections, 1):
            sid = f"ch{ci}s{si}"
            if stitle in sezioni:
                raise SystemExit(f"due sezioni si chiamano «{stitle}»: rif() non saprebbe quale scegliere")
            sezioni[stitle] = (sid, f"{ci}.{si}")
            toc.append(f'<li class="toc-se"><a href="#{sid}"><span class="toc-n">{ci}.{si}</span><span class="toc-t">{esc(stitle)}</span></a></li>')
            body.append(f'<section class="sec" id="{sid}"><h3 class="h-sec">{ci}.{si} · {esc(stitle)}</h3>{shtml}</section>')
        body.append("</section>")
    if not body:
        print(f"avviso: nessun capitolo in {CAPITOLI / meta['dir']}: il manuale ha la sola copertina", file=sys.stderr)
        body.append('<section class="chapter" id="ch0"><div class="ch-head"><h2 class="h-ch">No chapters yet</h2></div>'
                    '<section class="sec"><p>This manual has no chapters yet.</p></section></section>')
    doc = number("".join(body), sezioni)
    licenza = f" · Licence: {esc(LICENZA)}" if LICENZA else ""
    page = f"""<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>{meta["title"]}</title>
<style>{css}</style></head>
<body>
<div class="doc">
  <section class="cover cover-w">
    {logo()}
    <h1 class="cover-title">{meta["h1"]}</h1>
    <div class="cover-sub">{esc(SOTTOTITOLO)}</div>
    <div class="cover-note">{esc(AVVISO)}</div>
    <div class="cover-meta">
      <div><span>Version</span><b>{VERSION}</b></div>
      <div><span>Date</span><b>{DATE}</b></div>
    </div>
  </section>
  <div class="with-side">
    <aside class="side"><div class="side-inner">{SEARCH}<h2 class="toc-head">Contents</h2><ul class="toc-list" id="toc">{"".join(toc)}</ul></div></aside>
    <main class="main">{doc}</main>
  </div>
  <footer class="doc-foot">{esc(SOTTOTITOLO)} · {meta["h1"]} · Version {VERSION} · {DATE}{licenza}<br>{esc(AVVISO)}</footer>
</div>
<script>window.MANUAL_CODE_TERMS=[];</script>
<script>
{js}</script>
</body></html>
"""
    outfile = pathlib.Path(outdir) / meta["file"]
    outfile.write_text(page, encoding="utf-8")
    return outfile, len(page)


# ── Controlli: il manuale allineato al codice ───────────────────────────
# File citati nei manuali che non stanno nel repository (sono dell'utente o del telefono).
FILE_FUORI_DAL_REPOSITORIO = set()

# Nome vietato: non deve comparire in nessun punto dei manuali, senza eccezioni. Lo si costruisce
# a pezzi perché la parola stessa non deve esistere nemmeno in questo file.
NOME_VIETATO = "phon" + "estra"

# Tutto ciò che NON è testo da leggere: stile, script, immagini incorporate.
def _senza_incorporati(pagina):
    return re.sub(r'<style.*?</style>|<script.*?</script>|data:[\w/+.-]+;base64,[A-Za-z0-9+/=]+', " ",
                  pagina, flags=re.S)


SERIALE = re.compile(r"\b(?=[A-Z0-9]*\d)(?=[A-Z0-9]*[A-Z])[A-Z0-9]{11}\b")
SERIALE_FINTO = "R5CT0000000"
IP_PRIVATO = re.compile(r"\b(?:10\.\d{1,3}|192\.168|172\.(?:1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}\b")
# I nomi di persona da non far comparire NON si scrivono qui (repository pubblico): stanno, uno per
# riga, nel file locale `.nomi-vietati` nella radice del progetto (escluso da git) oppure, separati da
# virgole, nella variabile d'ambiente YASDE_NOMI_VIETATI. Senza nessuno dei due il controllo dei nomi
# non gira.
def _nomi_vietati():
    nomi = [n.strip() for n in os.environ.get("YASDE_NOMI_VIETATI", "").split(",") if n.strip()]
    f = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".nomi-vietati")
    if os.path.isfile(f):
        nomi += [r.strip() for r in open(f, encoding="utf-8") if r.strip() and not r.startswith("#")]
    return nomi


_NOMI = _nomi_vietati()
NOMI_DI_PERSONA = re.compile(r"\b(?:" + "|".join(re.escape(n) for n in _NOMI) + r")\b", re.I) if _NOMI else re.compile(r"(?!)")

# Parole che in un testo inglese non compaiono: due diverse nella stessa frase la
# segnalano come italiana. Restano fuori solo codice, tasti e blocchi <pre> (nomi veri
# dei sorgenti, che sono in italiano). I manuali sono interamente in inglese: niente
# citazioni italiane, niente scritte italiane nei grafici.
PAROLE_ITALIANE = re.compile(
    r"\b(il|lo|gli|della|delle|degli|dello|nella|nelle|negli|sono|questo|questa|quando|perché|anche|però|oppure|"
    r"finché|ancora|sempre|niente|nessun|nessuna|dopo|ogni|tutti|tutte|viene|serve|deve|può|hanno|col|coi|dal|dai|"
    r"sul|sui|alla|alle|allo|che|non|una|del|con|si|è)\b", re.I)


PAROLE_ITALIANE_GRAFICI = re.compile(
    r"\b(il|lo|gli|della|delle|dello|nella|sono|questo|quando|perché|anche|ogni|che|una|è|di|telefono|collegamento|"
    r"collegato|bloccato|chiuso|perso|cassetto|finestra|servizio|comandi|errore|risposta|evento|appunti|ricevi|"
    r"invia|chiudi|apri|notifiche|stato|primo|riserva|margine|posta|lettura|codifica|spedizione|sentinella)\b", re.I)


def frasi_italiane(pagina):
    """Le frasi del testo corrente di un manuale che sembrano ancora in italiano."""
    t = re.sub(r'<style.*?</style>|<script.*?</script>|<pre.*?</pre>|<code>.*?</code>|<kbd>.*?</kbd>|<span class="ui"[^>]*>.*?</span>',
               " ", pagina, flags=re.S)
    frasi = [f.strip() for f in re.split(r"(?<=[.;:!?])\s+|\n", html.unescape(re.sub(r"<[^>]+>", "\n", t)))
             if len({w.lower() for w in PAROLE_ITALIANE.findall(f)}) >= 2]
    # Le scritte dei grafici sono brevi: basta una parola tipicamente italiana.
    for svg in re.findall(r"<svg.*?</svg>", t, flags=re.S):
        for x in re.findall(r"<text[^>]*>(.*?)</text>", svg, flags=re.S):
            x = html.unescape(x)
            if PAROLE_ITALIANE_GRAFICI.search(x):
                frasi.append("grafico: " + x)
    return frasi


def controlla():
    errori, pagine = [], []
    with tempfile.TemporaryDirectory() as tmp:
        for kind, meta in MANUALS.items():
            fresco, _ = build(kind, tmp)
            pagina = fresco.read_text()
            if "<style>" + (HERE / "style.css").read_text() not in pagina:
                errori.append(f"{meta['file']}: lo stile non comincia col canone comune (style.css)")
            if (HERE / "manual.js").read_text() not in pagina:
                errori.append(f"{meta['file']}: manca lo script comune (manual.js)")
            pagine.append(re.sub(r"<script.*?</script>", "", pagina, flags=re.S))
            pubblicato = USCITA / meta["file"]
            if not pubblicato.exists() or pubblicato.read_text() != pagina:
                errori.append(f"«{pubblicato.relative_to(ROOT) if pubblicato.is_relative_to(ROOT) else pubblicato}» "
                              f"non corrisponde ai sorgenti: python3 docs/sources/build.py")
            if VERSION not in pagina:
                errori.append(f"{meta['file']}: la versione {VERSION} non compare")
            if '<html lang="en">' not in pagina:
                errori.append(f"{meta['file']}: la pagina non dichiara lang=\"en\"")
            if AVVISO not in html.unescape(pagina):
                errori.append(f"{meta['file']}: manca l'avviso «Independent project…»")
            if not LICENZA and re.search(r"licen[cs]e", _senza_incorporati(pagina), re.I):
                errori.append(f"{meta['file']}: cita una licenza, ma LICENZA è vuota (non ancora decisa)")
            for frase in frasi_italiane(pagina):
                errori.append(f"{meta['file']}: testo ancora in italiano: {frase[:120]}")
            ids = set(re.findall(r'\bid="([^"]+)"', pagina))
            for a in sorted(set(re.findall(r'href="#([^"]+)"', pagina)) - ids):
                errori.append(f"{meta['file']}: collegamento interno #{a} senza destinazione")
            # Autosufficienza: nessun file esterno e nessuna rete (restano ancore e dati incorporati).
            for attr, v in re.findall(r'\b(src|href)="([^"]*)"', _senza_incorporati_tranne_attr(pagina)):
                if not (v.startswith("#") or v.startswith("data:")):
                    errori.append(f"{meta['file']}: {attr}=\"{v[:60]}\" punta fuori dalla pagina")
            testo = _senza_incorporati(pagina)
            if "«FIG»" in pagina or "«TAB»" in pagina or "«RIF:" in pagina:
                errori.append(f"{meta['file']}: segnaposto «FIG»/«TAB»/«RIF:» non risolto")
            for cls in ("fig", "tbl-wrap"):
                for blocco in re.findall(rf'<figure class="{cls}[^"]*">.*?</figure>', pagina, re.S):
                    if not re.search(r"(Figure|Table) \d+\.\d+", re.sub(r"<[^>]+>", "", blocco)):
                        errori.append(f"{meta['file']}: figura o tabella senza didascalia numerata")
            # Dati personali e nome vietato.
            for s in sorted(set(SERIALE.findall(testo)) - {SERIALE_FINTO}):
                errori.append(f"{meta['file']}: possibile numero di serie vero: {s} (ammesso solo {SERIALE_FINTO})")
            for ip in sorted(set(IP_PRIVATO.findall(testo))):
                errori.append(f"{meta['file']}: indirizzo IP privato negli esempi: {ip} (usare 192.0.2.x)")
            # Indirizzi e contatti: ammessi solo quelli del progetto (CONTATTO e SITO_HOST), tutto il resto no.
            # Le entità HTML si sciolgono prima di cercare: «&#64;» al posto di «@» non deve sfuggire.
            leggibile = html.unescape(re.sub(r"<[^>]+>", " ", testo))
            for e in sorted(set(re.findall(r"[\w.+-]+@[\w-]+(?:\.[\w-]+)+", leggibile)) - {CONTATTO}):
                errori.append(f"{meta['file']}: indirizzo email non ammesso: {e}")
            for h in sorted({x.lower() for x in re.findall(r"https?://([^/\s\"'<>)]+)", leggibile, re.I)} - HOST_AMMESSI):
                errori.append(f"{meta['file']}: indirizzo web non ammesso: {h}")
            for n in sorted(set(m.lower() for m in NOMI_DI_PERSONA.findall(testo))):
                errori.append(f"{meta['file']}: nome di persona: {n}")
            # Il nome vietato si cerca ovunque: pagina grezza (attributi, stile, script inclusi), pagina
            # con le entità risolte e contenuto decodificato dei dati base64 che sono testo (SVG).
            import base64
            fonti = [pagina, html.unescape(pagina)]
            for mime, b64 in re.findall(r'data:([\w/+.-]+);base64,([A-Za-z0-9+/=]+)', pagina):
                if mime.startswith("text/") or mime.endswith("+xml") or mime.endswith("xml"):
                    try:
                        fonti.append(base64.b64decode(b64).decode("utf-8", "replace"))
                    except ValueError:
                        errori.append(f"{meta['file']}: dati base64 {mime} non decodificabili")
            for fonte in fonti:
                for m in re.finditer(r".{0,30}" + NOME_VIETATO + r".{0,30}", fonte, re.I | re.S):
                    errori.append(f"{meta['file']}: nome vietato: …{m.group(0).strip()}…")
                    break
    # I controlli sui nomi valgono per i due manuali insieme.
    pagina = "\n".join(pagine)
    codici = [html.unescape(re.sub(r"<[^>]+>", "", x)) for x in re.findall(r"<code>(.*?)</code>", pagina, re.S)]

    # Metodi Java «Classe.metodo» di una classe nostra: il metodo deve essere nel file.
    java = {}
    for p in (ROOT / "telefono").rglob("*.java"):
        java.setdefault(p.stem, []).append(p)
    for x in codici:
        for a, b in re.findall(r"\b([A-Z]\w*)\.([a-zA-Z_]\w*)\b", x):
            if b == "java" or a not in java:
                continue
            if not any(re.search(rf"\b{b}\b", f.read_text(errors="replace")) for f in java[a]):
                errori.append(f"metodo o campo citato che non esiste: {a}.{b} ({a}.java)")

    # File citati: devono esistere nel repository.
    tutti_i_file = {p.name for p in ROOT.rglob("*")
                    if ".git" not in p.parts and "build" not in p.parts and "__pycache__" not in p.parts}
    for x in codici:
        for f in re.findall(r"\b([\w-]+\.(?:c|h|java|sh|py|build))\b", x):
            if f not in tutti_i_file and f not in FILE_FUORI_DAL_REPOSITORIO:
                errori.append(f"file citato che non esiste: {f}")
    return list(dict.fromkeys(errori))


def _senza_incorporati_tranne_attr(pagina):
    """Come _senza_incorporati, ma lascia visibili gli attributi src/href (anche data:) per controllarli."""
    return re.sub(r'<style.*?</style>|<script.*?</script>', " ", pagina, flags=re.S)


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--prova" in args:
        i = args.index("--prova")
        base = pathlib.Path(args[i + 1]).resolve()
        base.mkdir(exist_ok=True)
        (base / "out").mkdir(exist_ok=True)
        os.environ["YASDE_MANUALI_PROVA"] = str(base)
        os.execv(sys.executable, [sys.executable, str(pathlib.Path(__file__).resolve())] + args[:i] + args[i + 2:])
    if args == ["--controlla"]:
        errori = controlla()
        for e in errori:
            print("manuale:", e)
        if errori:
            sys.exit(1)
        print("manuale allineato al codice")
    else:
        for kind in MANUALS:
            f, n = build(kind)
            print("scritto", f, n, "byte")
