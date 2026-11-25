# YASDE

**Yet Another Samsung DeX Emulator** — version 2.1.

YASDE is a Linux application, shipped as a single AppImage. Its one window is an emulated
desktop: the apps of an Android 14+ phone run inside it as separate windows, with sound,
mouse, keyboard, clipboard, notifications and file transfer. Nothing is installed on the
phone and no root is needed; every setting YASDE changes on the phone is put back.

YASDE is an independent project, not affiliated with, endorsed by or sponsored by Samsung.
Samsung and Samsung DeX are trademarks of Samsung Electronics Co., Ltd.

## What is in this repository

| Path | What |
|---|---|
| `docs/User Manual.html`, `docs/Technical Manual.html` | The two manuals (download and open in a browser). See `docs/README.md`. |
| `src/` | The PC program (C17, Meson, GTK 4, Vulkan/VA-API through FFmpeg). One folder per piece. |
| `telefono/` | The phone-side component (minimal Java, started with `app_process`). |
| `data/` | Translations and the icon table. |
| `pacchetto/` | Everything that builds the AppImage and the archive of third-party sources. |
| `strumenti/` | Build and test tools: the build container recipe, the Android SDK script, the test runner. |
| `docs/sources/` | Sources of the manuals and their generator. |
| `docs/protocollo.md` | The protocol between the PC and the phone component. |
| `yasde-specification.md`, `mockup/` | The design documents the program was written from. |
| `PIANO.md` | Decisions taken, known limits, open points and current state. |
| `CLAUDE.md` | Working rules of the project. |

The program and the public documents are in English. Source comments, internal documents
and file names are in Italian.

## Starting again from this repository

Everything needed to continue the project is here; nothing lives only on a developer's
machine.

1. Clone the repository on the machine where the sources are edited.
2. Prepare a build server (any Linux machine with `podman`, reachable over SSH) and set
   the environment variable `YASDE_SERVER` to its name. The commands that build the
   container image from `strumenti/Containerfile` and download the Android SDK are at
   the top of `strumenti/sdk.sh`.
3. Build and test a piece with `strumenti/prova.sh <piece>`; `tutto` builds and tests
   everything, `veloce` builds the optimized program, `telefono` builds the phone
   component.
4. Build the AppImage with `pacchetto/costruisci.sh`. It downloads the pinned third-party
   sources by itself and also writes the archive of third-party sources that must be
   published next to the AppImage.
5. Regenerate and check the manuals with `python3 docs/sources/build.py` and
   `python3 docs/sources/build.py --controlla`.

The built AppImage and the archive of third-party sources of each version are attached to
the corresponding release of this repository.
