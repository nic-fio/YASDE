# COMPITO — shell-launcher (F-04)

Leggi prima: `CLAUDE.md`, `src/shell/CONTRATTI.md`, `src/shell/shell.h`, `servizi.h`, `pezzi.h`. Specifica §5.4 (F-04), §5.2.2 (trascinamento verso il desktop), §5.2.4 (menu dell'icona), §5.11; mockup righe 233–246 (launcher), 412–427 (menu contestuale), metodi `openApp`, `onQueryKey`, `onAppCtx`, `startIconDrag`, `ctxAddDesk`, `ctxRemoveDesk`, `ctxOpenAppNow`, `toggleLauncher`, `closePopups`, e in `renderVals` `gridCols`, `gridItems`, `noResults`.

## Che cosa fa

`ph_launcher_installa(sh)` crea il launcher (nascosto) in `PH_STRATO_PANNELLI` e registra il gestore `PhShellGestoreLauncher` (`alterna` → `ph_shell_popup_alterna(sh, PH_POPUP_LAUNCHER, …)`).

Aspetto (mockup):
- Riquadro CENTRATO (fase 4 di 2.1, sostituisce i 24 px dai bordi): larghezza 60 % dell'area, fra 480 e 960 px (area piu' stretta di 528: a 24 px dai bordi); alto secondo il contenuto, al massimo il 76 % dell'area sopra la barra (48 px), a distanza dall'alto pari al 6 % dell'altezza (minimo 24); oltre il massimo scorre la griglia, il campo resta fisso; il clic nell'area scoperta lo chiude (velo dei popup della shell). Misure in `logica.h` (`PH_LAUNCHER_*`, `ph_launcher_misure`). Fondo `rgba(14,21,31,.96)` (`--ph-launcher`), bordo 1 px `rgba(255,255,255,.14)`, raggio 18, imbottitura 20 24 px, ombra `0 18px 50px rgba(0,0,0,.55)`, colonna con 18 px tra campo e griglia.
- Campo di ricerca centrato, largo quanto il riquadro ma al massimo 420 px: classi `ph-campo ph-ricerca` (fondo .08, bordo .18, testo bianco, raggio 20, imbottitura 10 16, 13 px), segnaposto «Cerca app (Invio apre la prima)», etichetta accessibile «Cerca app». Il fuoco va al campo all'apertura.
- Griglia scorrevole in verticale: colonne = ⌊larghezza interna del riquadro / 112⌋, almeno 1, griglia centrata (colonne larghe 112 px), spazio 28 px tra le righe e 8 px tra le colonne, imbottitura 4 4 12 px. Ogni app è un pulsante (classe del mockup `.ico`: raggio 10, imbottitura 6 4, sopra `rgba(255,255,255,.14)`) con: icona 60×60 raggio 16 (`ph_srv_icona`, classe `ph-launcher-icona-vera`, SENZA ombra: un'icona vera ha la sua forma e l'ombra rettangolare farebbe un alone quadrato; oppure `ph_shell_segnaposto_nuovo(pacchetto, nome, 60, 16)`, con ombra `0 4px 12px rgba(0,0,0,.35)`), nome 12 px #e6eaf2 centrato; etichetta accessibile = nome.
- Elenco: `ph_srv_app` (riletto a `PH_SRV_EV_APP`, icone a `PH_SRV_EV_ICONA`), nell'ordine del telefono, filtrato: il nome contiene il testo cercato, senza distinguere maiuscole (confronto con `g_utf8_casefold`). Nessun risultato → «Nessuna app trovata» (13 px #cbd5e1, centrato su tutta la riga). Elenco vuoto perché le app non sono ancora arrivate → griglia vuota (nessun testo inventato).

Comportamento:
- **Clic** su un'app → `ph_shell_apri_app` e chiude il launcher (mockup `openApp`: `launcher:false, query:''`). Il testo cercato si svuota a ogni chiusura del launcher (`closePopups`).
- **Invio** nel campo → apre la prima app trovata (se c'è).
- **Trascinamento** di un'app (`GtkDragSource` con `ph_shell_app_trascinata_contenuto(pacchetto, false)`, icona di trascinamento = l'icona 48×48 dell'app): appena il trascinamento parte il launcher si chiude (5.2.2; mockup: oltre 6 px di movimento). Il rilascio sul desktop lo gestisce il desktop. Un trascinamento non apre l'app.
- **Clic destro** su un'app → menu contestuale DENTRO il popup del launcher (mockup: il launcher resta aperto e il menu sta sopra, z 5100): stile `ph-menu` (minimo 200 px, raggio 10, imbottitura 6), voci `ph-riga`: «Apri» (apre e chiude il launcher), poi «Aggiungi al desktop» se `!ph_shell_sul_desktop`, altrimenti «Rimuovi dal desktop» (chiamano `ph_shell_aggiungi_al_desktop` / `ph_shell_rimuovi_dal_desktop`; il launcher resta aperto; l'avviso «Aggiunta al desktop» lo dà il desktop). Posizione: dove si è cliccato, spostata per restare dentro la radice (mockup: x ≤ larghezza − 210, y ≤ altezza − 150). Il menu si chiude con un clic fuori dal menu, con Esc o scegliendo una voce. NON è un `PH_POPUP_MENU` separato: aprirlo non deve chiudere il launcher.
- Tastiera: Tab tra campo e app; Esc chiude il menu se aperto, altrimenti il launcher (lo fa il gestore dei popup).

## Che cosa NON fa

- Altre voci di menu (Chiudi app, Preferiti, Info app, Disinstalla) non sono nel mockup: non si fanno (punto aperto riportato da chi coordina).
- Non sa dove va l'icona sul desktop: lo decide il desktop.
- Nessuna richiesta propria al telefono (solo `ph_srv_app` e `ph_srv_icona`).

## Dipende da

`shell.h` (popup, gestori, trascinamento, icone, accessibilità), `servizi.h`, `config/lingua.h`.

## Criteri di accettazione

1. Misure e colori come sopra; numero di colonne secondo la formula alla larghezza attuale (si ricalcola a `PH_SHELL_EV_AREA`).
2. Filtro: «mu» trova «Musica»; «xyz» mostra «Nessuna app trovata»; Invio apre la prima trovata e chiude il launcher.
3. Clic destro: le voci giuste a seconda di `ph_shell_sul_desktop`; il launcher resta aperto dopo «Aggiungi/Rimuovi».
4. Il trascinamento chiude il launcher appena parte e il contenuto è di tipo `PH_TIPO_APP_TRASCINATA`.
5. Con 2000 app (`PH_DATI_APP_MAX`) apertura e digitazione restano fluide: la griglia non ricrea tutti i widget a ogni tasto (usare `GtkGridView`/`GtkFlowBox` con filtro, o riuso dei widget).
6. Prova di logica (`prova_launcher.c`, senza schermo): filtro (maiuscole, accenti, vuoto), numero di colonne, posizione del menu ristretta all'area, scelta della prima app per Invio.
7. `strumenti/prova.sh shell-launcher` → `uscita 0`.

## Come si prova

- `strumenti/prova.sh shell-launcher`.
- Demo: `strumenti/demo-sul-tablet.sh launcher`. Nel `demo.c` apri il launcher all'avvio (chiama il tuo `alterna`) e registra un gestore desktop finto (un insieme di pacchetti in memoria) e un gestore finestre finto che scrive su stderr l'app aperta.

## Nota del 23 nov (richieste del committente, PIANO.md punto 3)
Nel menu contestuale di un'app aggiungi, dopo «Aggiungi/Rimuovi dal desktop», la voce «Fissa sulla barra» se `!ph_shell_app_fissata(sh, pacchetto)`, altrimenti «Togli dalla barra» → `ph_shell_fissa_app(sh, pacchetto, true/false)` (shell.h; testi già in `data/en/shell-comune.txt`). Il launcher resta aperto. Senza la barra (demo da solo) le chiamate non fanno nulla: nel demo registra un `PhShellGestoreFissate` finto. Se il menu è aperto quando arriva `PH_SHELL_EV_FISSATE`, aggiorna la voce.
(L'elenco delle fissate è nel profilo del telefono, `PhCfgTelefono.fissate` con le funzioni `ph_cfg_telefono_*` di config, ma lo tocca solo la barra: il launcher passa sempre da `ph_shell_app_fissata` / `ph_shell_fissa_app` e ascolta `PH_SHELL_EV_FISSATE`.)
