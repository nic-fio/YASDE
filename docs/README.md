# YASDE documentation

| File | For whom |
|---|---|
| `User Manual.html` | People who use YASDE: installation, first connection, everyday use, problems. |
| `Technical Manual.html` | People who develop it: architecture, ADB link, phone-side component, video, audio, input, shell, packaging, tests. |

Both manuals are written **in English**, like their sources in `sources/`.

Each manual is a single self-contained HTML page: style, script, logo, diagrams and
screenshots are all inside the file. They can be downloaded on their own and opened in a
browser, with no internet connection. (On GitHub the `.html` files show up as source code,
not as pages: download them to read them.) Layout: cover with logo, search box at the top
of the side bar, Copy button on command blocks.

The manuals are **generated**: do not edit the HTML by hand.

| Source | What |
|---|---|
| `sources/build.py` | The generator: helpers for text, tables, callouts, SVG figures and screenshots; file map and counts taken from the sources; checks. |
| `sources/style.css` | The style of the manuals. A few extra rules live in `EXTRA_CSS` in `build.py`. |
| `sources/manual.js` | The script of the manuals (side bar search, Copy button). |
| `sources/technical/chNN_*.py`, `sources/user/chNN_*.py` | One file per chapter: `CHAPTER = (title, [(section, html), ...])`. |
| `sources/img/*.png` | Screenshots used by the user manual (fake data only). |

```
python3 docs/sources/build.py              # regenerate the two manuals
python3 docs/sources/build.py --controlla  # check them (language, references, names, personal data)
```

Needs `python3-pygments` and `python3-pil`. With few or no chapters the generator still
works: a manual without chapters has only its cover and a notice.

## Adding a chapter

Create `sources/user/chNN_name.py` (or `sources/technical/…`); chapters are ordered by file
name. Start with `from build import *` and define `CHAPTER`. Helpers available: `esc, c, ui,
key, pill, p, h4, ul, ol, steps, note, warn, tip, rif, table, dl, code, term, tree, box,
diamond, arrow, path, text, zone, fig, flow, seq, file_map, numeri, conta, shot`, plus
`VERSION` and `DATE`. Captions use the placeholders `«FIG»` and `«TAB»`, numbered per
chapter; `rif("Section title")` links to another section by its exact title.

## Adding a screenshot

Put the PNG in `sources/img/` and use `shot("file.png", "«FIG» Caption.")` in a chapter.
The image is embedded in the page; a missing file stops the generation with an error.

## Checks

`--controlla` regenerates the manuals in a temporary folder and fails if the files on disk
differ, if a `rif()` points nowhere, if a figure or table has no numbered caption, if the
text contains Italian, if a cited file or Java method does not exist, if the page refers to
anything outside itself, if a forbidden project name appears anywhere, or if a real-looking serial
number, private IP address or personal name appears. No licence line is printed until the
`LICENZA` constant in `build.py` is set.
