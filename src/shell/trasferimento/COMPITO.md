# COMPITO — shell-trasferimento (F-09: invia, scarica, selettore di cartelle, trascinamento di file)

Leggi prima: `CLAUDE.md`, `src/shell/CONTRATTI.md`, `src/shell/shell.h`, `servizi.h`, `pezzi.h`. Specifica §5.9 (tutta: 5.9.1–5.9.4), §5.10, §5.11; mockup righe 286–403 (pannelli e riquadro di rilascio), metodi `toggleUp`, `toggleDown`, `toggleUpPick`, `upEnter`, `upUp`, `upUseDir`, `upNewDir`, `queueXfers`, `addUploads`, `onDragOver`, `onDragLeave`, `onDrop`, `normPath`, `searchFS`, `dlGoKey`, `upGoKey`, `toggleDlPick`, `pcEnter`, `pcUp`, `pcUse`, `pcNew`, `dlEnter`, `dlUp`, `dlToggle`, `dlStart`, `xferTick` (solo l'avviso finale), `fmtSize`, e in `renderVals` `xrow`, `upDests`, `dlRows`, `upDirRows`, `dlEmptyText`, `upEmptyText`, `dlLabel`.

## Che cosa fa

`ph_trasferimento_installa(sh)` crea i due pannelli (nascosti, `PH_STRATO_PANNELLI`), il riquadro di rilascio (`PH_STRATO_RILASCIO`), un `GtkDropTarget` per `GDK_TYPE_FILE_LIST` sulla radice (`ph_shell_radice`), e registra `PhShellGestoreTrasferimento` (`apri(verso)` → `ph_shell_popup_alterna` su `PH_POPUP_INVIO` / `PH_POPUP_RICEZIONE`).

Pannelli: `ph-pannello`, a 200 px dal bordo destro e 56 dal basso, larghi 380 (invio alto al massimo 560, ricezione 580), ruolo accessibile dialogo con etichetta «Invia file al telefono» / «Scarica file dal telefono». Intestazione: titolo 14 px 600 («Invia al telefono» / «Scarica dal telefono») e sotto 12 px #9aa4b5 con 2 px sopra («Dal PC al telefono» / «Dal telefono al PC»).

### Invia al telefono (F-09a)
- Zona (se non si sta scegliendo la cartella): bordo 2 px tratteggiato `rgba(255,255,255,.28)`, raggio 12, imbottitura 16, contenuto centrato con 10 px: «Trascina i file in qualsiasi punto della finestra» (13 px), «oppure» (12 px #9aa4b5), pulsante «Scegli i file dal PC» che apre il selettore del sistema (`GtkFileDialog`, più file). NON c'è «Prova con file di esempio».
- «Cartella di destinazione sul telefono» (`ph-sezione`); riga con fondo .06, raggio 8, imbottitura 6 8: percorso intero (12 px, tagliato con i puntini) e pulsante `ph-ghost ph-bordato` «Cambia…» (diventa «Annulla» mentre si sceglie).
- Gruppo «Cartelle usate spesso» (ruolo gruppo; solo se non si sta scegliendo): «Download», «Documenti», «Immagini», «Musica» → `/storage/emulated/0/Download`, `/Documents`, `/Pictures`, `/Music`; raggio 6, imbottitura 5 12, 12 px 600; quella attuale: accento con testo #0b1220 (`aria-pressed`), le altre fondo .1.
- Destinazione iniziale: `cartella_invio` del profilo, altrimenti `/storage/emulated/0/Download`; ogni cambio si salva nel profilo.
- Accodare: `ph_srv_tr_accoda(s, PH_SRV_INVIO, file, cartella)` per ogni file normale (le cartelle si saltano); errore → `ph_shell_avviso` con la frase.

### Selettore di cartelle del telefono (F-09c, §5.9.4) — dentro il pannello di invio
Riquadro bordo .14, raggio 10, imbottitura 8, colonna con 6 px:
- riga: «Su» (`ph-ghost ph-bordato`, etichetta «Cartella superiore», opacità .4 e inattivo a «/») e campo del percorso (`ph-campo`, etichetta «Percorso sul telefono (Invio per andare)»): Invio normalizza (spazi ai lati tolti, «/sdcard» = `/storage/emulated/0`, «/» finali tolti, vuoto = «/») e va alla cartella se esiste (`ph_srv_tel_elenca`); se non esiste avviso «Cartella non trovata» con il testo digitato;
- campo di ricerca (`ph-campo`, segnaposto «Cerca una cartella in tutto il telefono», etichetta «Cerca una cartella nel telefono»): `ph_srv_tel_cerca(s, "/", testo, true, 30, …)` — solo cartelle, in tutto il telefono; a ogni tasto si annulla la ricerca precedente (`GCancellable`) e se ne avvia una nuova dopo una breve pausa (~250 ms); i risultati mostrano il percorso intero (`percorso_schermo`) e un clic ci va;
- elenco delle sottocartelle (alto al massimo 170, scorre): righe `ph-riga` con «>» (16 px, #9aa4b5) e il nome; etichetta «Apri la cartella {nome}»; clic → entra; file e nascosti (nome che inizia col punto) non si mostrano; vuoto: «Nessun risultato» (ricerca), «Accesso non consentito dal sistema» (`PH_SRV_ESITO_NON_CONSENTITO`), «Nessuna sottocartella»; 12 px #9aa4b5;
- in fondo: «Nuova cartella» (`ph-ghost ph-bordato`: crea «Nuova cartella», o «Nuova cartella 2», «3», … se il nome c'è già, con `ph_srv_tel_crea_cartella`, ed entra) e «Usa questa cartella» (`ph-accento`, si allarga: la cartella attuale diventa la destinazione e il selettore si chiude).
- Parte dalla destinazione attuale; si risale fino a «/» (`/`, `/storage`, `/storage/emulated`, `/storage/emulated/0`); percorsi sempre interi.
- Elenchi lunghi: a pagine di 200 voci con «Mostra altri {numero}» in fondo (§5.9).

### Scarica dal telefono (F-09b)
(Se non si sta scegliendo la cartella del PC:)
- riga «Su» + percorso digitabile (come sopra; partenza `/storage/emulated/0`);
- ricerca «Cerca file o cartelle in tutto il telefono» (etichetta «Cerca nel telefono»): `ph_srv_tel_cerca(s, "/", testo, false, 30, …)`;
- elenco (alto al massimo 230): cartelle con «>» (clic entra, etichetta «Apri la cartella {nome}», a destra «Cartella» o, cercando, il percorso della cartella che la contiene); file con casella 16×16 raggio 4 (scelta: fondo e bordo 2 px accento; altrimenti bordo 2 px `rgba(255,255,255,.4)`), etichetta «Seleziona {nome}» / «Deseleziona {nome}», a destra la dimensione (cercando: «{cartella} · {dimensione}»), 11 px #9aa4b5; nascosti esclusi; vuoto: «Nessun risultato», «Accesso non consentito dal sistema», «Cartella vuota». Le selezioni (percorsi interi) RESTANO cambiando cartella.
- pulsante largo (13 px 600, imbottitura 9 12, raggio 8): «Seleziona i file da scaricare» inattivo (fondo `rgba(255,255,255,.25)`) senza selezione, altrimenti «Scarica {numero} file» con l'accento → `ph_srv_tr_accoda(s, PH_SRV_RICEZIONE, percorso, cartella_pc)` per ogni file; poi la selezione si svuota.
- «Cartella di destinazione sul PC»; riga «PC: {percorso}» + «Cambia…»/«Annulla». Destinazione iniziale: `cartella_ricezione` del profilo, poi la preferenza `cartella_ricevuti`, poi la cartella Scaricati dell'utente (`g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD)`, o la home). Ogni cambio si salva nel profilo.
- Selettore di cartelle del PC (come quello del telefono, senza ricerca): «Su» (fino a «/»), «PC: {percorso}» (12 px #cbd5e1), sottocartelle del PC lette con GIO in modo asincrono (`g_file_enumerate_children_async`, mai sul thread GTK in modo bloccante; nascoste escluse), «Nessuna sottocartella», «Nuova cartella» (`g_file_make_directory_async`, nomi come sopra), «Usa questa cartella». Partenza: la destinazione attuale (la home se non esiste).

### Elenco dei trasferimenti (comune ai due pannelli)
- Da `ph_srv_trasferimenti` (`PH_SRV_EV_TRASFERIMENTI`), solo quelli del verso del pannello, il più recente in alto (alto al massimo 150 invio / 120 ricezione, scorre); voce `ph-voce` (imbottitura 8 10, 12 px): nome 600 tagliato + stato a destra #cbd5e1 («{percento}%» per in corso; «Completato»; «In attesa», «Annullato», «Non riuscito» — questi tre non sono nel mockup, testi proposti); barra 4 px raggio 2 (`ph-avanzamento`) riempita d'accento alla percentuale; sotto «{dimensione} · {destinazione}» #9aa4b5 (FALLITO: la frase d'errore al posto della destinazione).
- Dimensioni come il mockup (`fmtSize`): da 1 MiB «{numero} MB» con una cifra decimale (virgola in italiano, punto in inglese), da 1 KiB «{numero} KB» arrotondato, altrimenti «{numero} B».
- Alla fine di ogni trasferimento (passaggio a COMPLETATO, confrontando con la fotografia precedente per id): `ph_shell_avviso` «Inviato al telefono» / «Scaricato sul PC» con il nome; più completati nello stesso aggiornamento: «{nome} e altri {numero}». Nessuna altra azione (A11 aperto).
- Coda ferma (`ph_srv_coda_ferma`): riparte da sola al ritorno della connessione; nessun comando.

### Trascinamento di file sulla finestra (F-09a)
- Entrando con dei file: riquadro in `PH_STRATO_RILASCIO` a 8 px da sinistra, alto e destra e 56 px dal basso, bordo 3 px tratteggiato `rgba(255,255,255,.7)`, raggio 16, fondo `rgba(14,21,31,.55)`, testo centrato «Rilascia per inviare al telefono» 18 px 600 bianco; non riceve clic (`can_target` falso). Uscendo sparisce.
- Al rilascio: accoda tutti i file normali verso la destinazione attuale e apre il pannello di invio. Le app trascinate dal launcher NON attivano il riquadro (tipo diverso).

## Che cosa NON fa
- Installazione di APK trascinati (5.9.1): manca il contratto nei servizi (riportato da chi coordina) → un .apk si invia come un file normale.
- Annullamento per voce o per tutto: è una [PROPOSTA] della specifica non nel mockup → non si mostra (i servizi lo offrono già: `ph_srv_tr_annulla`, pronto quando si decide).
- Luoghi rapidi (Recenti, Camera, WhatsApp, …): «si possono conservare», non sono nel mockup → no.
- Nessun accesso diretto al telefono o ad ADB: solo servizi.

## Dipende da
`shell.h`, `servizi.h`, `config/config.h` (campi del profilo), `config/lingua.h`. GIO per le cartelle del PC.

## Criteri di accettazione
1. Misure, testi e stati come sopra in entrambi i pannelli; uno solo aperto alla volta.
2. Selettore del telefono: si risale fino a «/»; `/data` mostra «Accesso non consentito dal sistema»; «/sdcard/Download» + Invio va a `/storage/emulated/0/Download`; «/nonc'è» + Invio → avviso «Cartella non trovata»; la ricerca di «pippo» trova `/storage/emulated/0/pippo`; «Nuova cartella» due volte crea «Nuova cartella» e «Nuova cartella 2».
3. Scarica: selezione che resta cambiando cartella; il pulsante cambia testo e stato; accodare svuota la selezione.
4. Trascinando file dal gestore file del PC sulla finestra compare il riquadro; al rilascio partono nella coda e si apre il pannello.
5. Avviso a fine trasferimento con il nome giusto; puntino della barra coerente (lo legge la barra dai servizi).
6. Mai blocchi del thread GTK (cartelle del PC asincrone).
7. Prova di logica (`prova_trasferimento.c`, senza schermo): normalizzazione dei percorsi, «Su» fino a «/», nome libero per «Nuova cartella», formato delle dimensioni (italiano e inglese), testo dello stato di una voce, rilevazione dei completati tra due fotografie, filtro dei nascosti, paginazione a 200.
8. `strumenti/prova.sh shell-trasferimento` → `uscita 0`.

## Come si prova
- `strumenti/prova.sh shell-trasferimento`.
- Demo: `strumenti/demo-sul-tablet.sh trasferimento`. Nel `demo.c` aggiungi due pulsanti in basso a destra (in `PH_STRATO_BARRA`) che chiamano il tuo `apri(PH_SRV_INVIO / PH_SRV_RICEZIONE)`; il telefono finto ha l'albero del mockup (con «pippo», `/data` non consentita) e fa avanzare i trasferimenti ogni 300 ms senza scrivere nulla sul PC.
