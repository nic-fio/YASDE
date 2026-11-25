# COMPITO — shell-finestra-app (F-05: finestre interne delle app)

Leggi prima: `CLAUDE.md`, `src/shell/CONTRATTI.md`, `src/shell/shell.h`, `servizi.h`, `superficie.h`, `pezzi.h`, e TUTTO `src/finestre/finestre.h` (le regole: ordine, aggancio, gesti, misura del flusso, assestamento, traduzione dell'input, «riapri dove eri» sono già scritte e provate lì: non si riscrivono). Specifica §4.4, §4.5, §4.6, §4.10, §4.14, §5.5, §5.3 punto 3, §4.7 (specchio); mockup righe 91–190 (finestra), metodi `openApp`, `focus`, `minimize`, `toggleMax`, `closeWin`, `toggleRec`, `taskClick`, `startDrag`, `onDeskMove`/`onDeskUp` (parte finestre), `togglePhoneScreen`, e in `renderVals` `wins`.

Decisioni già prese dal committente: piè di finestra diagnostico NASCOSTO (`ph_fin_imposta_pie(false)`, il predefinito); menu «⋮» nel titolo (`PH_FIN_PULS_ALTRI`). In sospeso (non decidere): l'effetto di «Ingrandisci» sulle app solo verticali — usare quello di finestre.h (colonna a forma di telefono) tenendolo isolato in un punto.

## Che cosa fa

`ph_finestra_app_installa(sh)` crea il contenitore delle finestre (`ph_shell_libero_nuovo()` in `PH_STRATO_FINESTRE`), un `PhFinDesktop` (`ph_fin_crea`), e registra `PhShellGestoreFinestre`. Emette `PH_SHELL_EV_FINESTRE` a ogni cambio (apertura, chiusura, minimizza, ripristina, attiva).

### Desktop delle finestre (finestre.h)
- Area: la misura della radice (`PH_SHELL_EV_AREA`) in pixel fisici e la scala della shell (`gdk_surface_get_scale` della superficie della finestra host, frazionaria; 1 se non ancora realizzata) → `ph_fin_imposta_host`. Le coordinate di finestre.h sono unità logiche con origine in alto a sinistra dell'area utile (sopra la barra di 48): sono i px di GTK del contenitore.
- Qualità (4.4): `ph_fin_imposta_qualita(d, densita_app, 160, 2560)` dove densita_app = `densita_app` del profilo se > 0, altrimenti la densità del telefono (`PhSrvCollegamento.densita`; prima del collegamento 420 come segnaposto da sostituire appena arriva). Si rifà a `PH_SRV_EV_PROFILO` e `PH_SRV_EV_COLLEGAMENTO`.
- Il tempo per finestre.h: `ph_shell_ora_ms()`.
- `ph_fin_avanza` a ogni fotogramma (`gtk_widget_add_tick_callback` sul contenitore): per ogni richiesta → `ph_superficie_ridimensiona(sup, w, h)` (4.5: a movimento assestato, 300 ms).
- Nome app per finestre.h: `PH_FIN_APP_MAX` è 48 byte ma un pacchetto Android può essere più lungo (fino a `PH_CFG_LUN_APP`): usare come chiave per finestre.h un identificatore corto proprio (per esempio «a<numero>», da una tabella pacchetto ↔ chiave tenuta qui) e convertire nei due sensi anche per «riapri dove eri». Lo specchio usa la chiave «phone» (è quella che finestre.h tratta come specchio per la posizione iniziale).

### Aprire una finestra (gestore `apri_app`)
- Se l'app ha già una finestra: la ripristina se minimizzata, la porta davanti, la rende attiva (`ph_fin_apri` lo fa e torna `nuova = false`).
- Altrimenti: `PhFinSpec`: app solo verticale (`PhSrvApp.solo_verticale`) → {296, 470, solo_verticale}; altrimenti {380, 420}; specchio → {280, 500, solo_verticale, specchio} (misure del mockup; se l'elenco delle app non è ancora arrivato: {380, 420} e il nome = pacchetto, aggiornato all'arrivo). `ph_fin_apri`, poi `ph_fin_misura_flusso` → `ph_superficie_nuova(sh, {pacchetto, w, h, densità 0}, …)` per il contenuto. Errore (troppe finestre, flusso rifiutato) → `ph_shell_avviso` con la frase e nessuna finestra.
- Lo specchio (`specchio(true)`, chiesto dal popup del telefono): finestra «Schermo del telefono» con `pacchetto` NULL; `specchio(false)` la chiude; `specchio_aperto` dice se c'è.

### Aspetto della finestra (mockup)
- Cornice: fondo bianco, raggio 10, contenuto ritagliato, ombra `0 14px 40px rgba(0,0,0,.5)`; massimizzata o agganciata: raggio 0.
- Titolo alto 32 (`PH_FIN_TITOLO_H`): attiva fondo #2a3344 e testo #ffffff, inattiva #171c26 e #9aa4b5; nome 12 px 500, una riga, tagliato con i puntini, imbottitura 0 6.
- Pulsanti 28×28 raggio 6, colore #d6dbe6, sopra `rgba(255,255,255,.16)`; «Chiudi» sopra #c2410c con testo bianco. Ordine e posizione ESATTI di finestre.h (`PhFinPulsante`): a sinistra «Indietro» (`PH_ICONA_INDIETRO`), poi nome e indicatore «REC» (#fca5a5, 11 px 600, imbottitura 0 6, solo durante la registrazione), a destra «Screenshot» (`PH_ICONA_SCREENSHOT`), «Registra lo schermo» (`PH_ICONA_REGISTRA`), «Altri comandi» (`PH_ICONA_ALTRI`), «Riduci a icona» (`PH_ICONA_RIDUCI_FINESTRA`), «Ingrandisci o ripristina» (`PH_ICONA_INGRANDISCI_FINESTRA`), «Chiudi» (`PH_ICONA_CHIUDI_FINESTRA`). Sono `GtkButton` veri con quelle etichette accessibili (testi del mockup).
- Contenuto: la superficie su TUTTO il rettangolo `ph_fin_contenuto` (stesa senza bande: la traduzione dell'input lo presuppone).
- Maniglia di ridimensionamento: 18×18 nell'angolo in basso a destra (`PH_FIN_ANGOLO`), cursore `nwse-resize`, solo per le ridimensionabili non massimizzate.
- Nessun piè di finestra.

### Gesti e input (4.5, 4.10)
- Un gestore di puntatore sul contenitore: `ph_fin_trova(x, y)` dice finestra e zona.
  - Pressione su una finestra qualunque → `ph_fin_porta_davanti` + `ph_shell_libero_in_cima` (ordine dei widget = `ph_fin_ordine`) + avviso.
  - `ZONA_TITOLO` (fuori dai pulsanti) → `ph_fin_gesto_inizia(SPOSTA)`; `ZONA_ANGOLO` → `RIDIMENSIONA`; movimenti → `ph_fin_gesto_muovi` e riposizionamento del widget (`ph_shell_libero_sposta` con `info.rect`): il video lo scala la GPU, nessuna richiesta al telefono durante il gesto; rilascio → `ph_fin_gesto_finisci` (aggancio ai bordi per le ridimensionabili: 14 px sinistra/destra = metà, 10 px in alto = massimizza); uscita dalla radice o perdita del puntatore → `ph_fin_gesto_annulla`. L'anteprima dell'aggancio NON si disegna (non è nel mockup; finestre.h la offre solo come interrogazione).
  - `ZONA_CONTENUTO` → input al telefono, se `colpo.valido`: coordinate del video = `(int)vx, (int)vy` troncate, con `colpo.video_w/h`; mouse = un dito: pressione `PH_SRV_TOCCO_GIU`, trascinamento `MUOVI`, rilascio `SU`, perdita del puntatore `ANNULLA` (`ph_superficie_tocco`); rotella → `ph_superficie_rotella` (passi della rotella: ±1 per scatto, scorrimento fluido proporzionale); un evento iniziato nel contenuto resta a quella finestra fino al rilascio.
- `PH_SUP_EV_MISURA` → `ph_fin_dichiara_video` (la misura vera per l'input; gli eventi su una misura vecchia li scarta il telefono).
- Tastiera: la finestra attiva ha il fuoco. Tasti → `ph_superficie_tasto` con i codici Android (`KeyEvent.KEYCODE_*`: lettere, cifre, Invio 66, Canc indietro 67, Tab 61, Esc 111, frecce 19–22, spazio 62, Canc 112, Inizio 122, Fine 123, PagSu 92, PagGiù 93, F1–F12 131–142) e i modificatori (`META_SHIFT_ON` 1, `META_ALT_ON` 2, `META_CTRL_ON` 0x1000); il testo composto (metodi di input, caratteri non ASCII) con `GtkIMContext` → `ph_superficie_testo`. Riservati alla shell (non vanno al telefono): Ctrl+R (Ruota), Ctrl+W (Chiudi app), Ctrl+Maiusc+C (Copia screenshot), il tasto dello schermo intero (lo decide l'ossatura). **Esc**: con la preferenza `esc_indietro` vera → `ph_superficie_indietro`; altrimenti tasto Esc al telefono (un popup aperto lo chiude prima il gestore dei popup).

### Pulsanti e ciclo di vita (4.6)
- **Indietro** → `ph_superficie_indietro`.
- **Riduci a icona** → `ph_fin_minimizza`, widget nascosto, `ph_superficie_pausa(true)` (l'app resta viva).
- **Ripristino** (barra o `apri_app`) → visibile, `ph_superficie_pausa(false)` (fotogramma chiave in ~0,1 s).
- **Ingrandisci o ripristina** → `ph_fin_massimizza_ripristina`.
- **Chiudi** → `ph_superficie_chiudi(true)` (app tolta dai recenti), widget tolto, `ph_fin_chiudi`.
- **Clic sulla barra** (gestore `clic_barra(id)`) → `ph_fin_clic_barra` e ciò che dice (minimizzata / ripristinata / davanti) con pausa e ripresa.
- **Finestra host non visibile** (`PH_SHELL_EV_HOST`, `ph_shell_host_visibile` falso, 4.14) → tutte le superfici in pausa, e `ph_shell_decisione("flussi in pausa", "finestra host non visibile")`; tornando visibile riprendono SOLO quelle non minimizzate (altra decisione nel registro).
- `PH_SUP_EV_APP_CHIUSA` (l'app si è chiusa da sola) → la finestra si chiude (`ph_superficie_chiudi(false)`). `PH_SUP_EV_CHIUDI` («Chiudi» sul velo di connessione persa) → come Chiudi. `PH_SUP_EV_ERRORE` → avviso con la frase e chiusura.
- **Screenshot** → `ph_superficie_fotografa`; in un thread (`GTask`) PNG in `<cartella Immagini dell'utente>/YASDE/Screenshot_AAAAMMGG_HHMMSS.png` (cartella creata se manca; Immagini = `g_get_user_special_dir(G_USER_DIRECTORY_PICTURES)`, altrimenti la home), immagine negli appunti del PC (`gdk_clipboard_set_texture`), poi avviso «Screenshot salvato» / «Immagini/YASDE e copiato negli appunti» (mockup). Senza immagine: niente. **Copia screenshot** (Ctrl+Maiusc+C): solo negli appunti.
- **Registra lo schermo** → alterna lo stato di registrazione: `ph_fin_imposta_registrazione` (durante la registrazione la misura non cambia) e l'indicatore REC. Il file MP4 NON si produce ancora: manca il contratto (né i servizi né src/video lo offrono; riportato da chi coordina). Tenere il punto in cui si avvierà/fermerà la registrazione vera in una funzione sola.
- **Altri comandi (⋮)** → menu `PH_POPUP_ALTRI_COMANDI` (stile `ph-menu`, voci `ph-riga`) sotto il pulsante: «Ruota» (Ctrl+R; `ph_fin_ruota`, se rifiutata nessun effetto), «Chiudi app» (Ctrl+W; come Chiudi).
- **Riapri dove eri** (preferenza `riapri_dove_eri`): `ph_fin_riapri_dove_eri`; ricordi importati dal profilo (`finestre[]`) a `PH_SRV_EV_PROFILO` (`ph_fin_importa`) ed esportati nel profilo (`ph_fin_esporta` + `ph_srv_profilo_salva`) a ogni chiusura e a movimento assestato.

### Connessione e blocco
- Telefono bloccato: niente da fare (il velo dell'ossatura copre tutto; le finestre restano dove sono).
- Connessione persa: le superfici mostrano l'ultima immagine sfocata con «Riconnetti ora» e «Chiudi» e ripartono da sole (`PH_SRV_FL_RIPRESO`); le finestre restano aperte.

## Che cosa NON fa
- Non disegna né decodifica video (superficie); non parla con il telefono se non tramite la superficie.
- Non scrive nuove regole di geometria: quelle sono di finestre.h (se una manca o è sbagliata: rapporto).
- Nessun piè di finestra, nessuna anteprima dell'aggancio, nessuna scorciatoia non elencata.

## Dipende da
`shell.h`, `servizi.h`, `superficie.h`, `finestre/finestre.h`, `config/config.h`, `config/lingua.h`. Con `-Dpezzo=shell-finestra-app` la superficie è quella finta del backend finto (stesso contratto): funziona davvero (fotogrammi sintetici con il mirino sull'ultimo tocco).

## Criteri di accettazione
1. Aspetto e ordine dei pulsanti come sopra; finestra attiva/inattiva con i colori giusti.
2. Aprire, spostare, ridimensionare, minimizzare, ripristinare, massimizzare, agganciare, chiudere: nessuna finestra nera, nessun blocco; durante il gesto nessuna richiesta di ridimensionamento al telefono finto (stderr), dopo 300 ms fermi una sola.
3. **L'input va nel punto giusto dopo un ridimensionamento** (criterio 5.5): nel demo il mirino rosso del telefono finto compare esattamente sotto il puntatore, anche subito dopo un ridimensionamento e dopo l'arrivo della misura nuova; un tocco calcolato sulla misura vecchia il finto lo scrive come «scartato».
4. Minimizza = pausa (stderr del finto: «in pausa»), ripristino = ripresa; host non visibile = tutte in pausa, con le righe nel registro delle decisioni.
5. Esc → «indietro» al telefono finto (con la preferenza predefinita); Ctrl+R / Ctrl+W / Ctrl+Maiusc+C non arrivano al telefono.
6. Screenshot salvato nella cartella giusta e negli appunti, con l'avviso; il thread GTK non si blocca.
7. Con 3 e con 5 finestre il trascinamento resta fluido (nessun lavoro pesante sul thread GTK).
8. Prova di logica (`prova_finestra_app.c`, senza schermo): tabella dei tasti GDK → Android e dei modificatori, tabella pacchetto ↔ chiave corta (anche pacchetti più lunghi di 47 byte e conversione dei ricordi), scelta della `PhFinSpec`, nome del file dello screenshot, decisioni di pausa/ripresa al cambiare di host visibile e minimizzate.
9. `strumenti/prova.sh shell-finestra-app` → `uscita 0`.

## Come si prova
- `strumenti/prova.sh shell-finestra-app`.
- Demo: `strumenti/demo-sul-tablet.sh finestra-app` (apre Note e Messaggi all'arrivo delle app; F9 apre l'app successiva; F8 fa chiudere da sola l'ultima; F6 simula la caduta). Nel `demo.c` puoi aggiungere i tasti per minimizzare/ripristinare (al posto della barra) e per simulare la finestra host nascosta (`ph_shell_imposta_host(sh, false, false)`).

## Nota del 23 nov (richieste del committente, PIANO.md punto 4)
Aggiungi al tuo `PhShellGestoreFinestre` la voce nuova in coda `chiudi(io, id)` (shell.h): stesso effetto del pulsante Chiudi della finestra (`ph_superficie_chiudi(sup, true)`, app tolta dai recenti, widget tolto, `ph_fin_chiudi`, avviso `PH_SHELL_EV_FINESTRE`); id sconosciuto: nulla. La chiama la barra dal menu del clic destro («Chiudi app»). Vale anche per lo specchio (lo chiude, senza recenti da togliere).

## Nota del 23 nov (PIANO.md «Finestre grandi: modalità tablet»)
Nelle finestre grandi la densità dello schermo virtuale si abbassa da sola: la calcola `src/finestre/finestre.h`, che espone la densità EFFETTIVA accanto alla misura del flusso (in estensione da un altro programmatore: usa la funzione che esporrà, non ricalcolarla). All'apertura passa misura E densità effettiva nella `PhSrvFlussoRichiesta` (`densita`, 0 solo se coincide con quella del telefono); a ogni ridimensionamento assestato (`ph_fin_avanza`) chiama `ph_superficie_ridimensiona_densita(sup, w, h, densita_effettiva)` al posto di `ph_superficie_ridimensiona`. Un cambio di SOLA densità (stessa misura) conta come ridimensionamento e va mandato. Nel demo il telefono finto disegna in giallo, accanto alla misura, la densità ricevuta: controllalo con `strumenti/foto-demo.sh finestra-app` ingrandendo una finestra.

## Nota del 23 nov pomeriggio — widget «App frequenti»
A ogni finestra NUOVA di un'app (non quando la riporti davanti, non per lo specchio) conta un'apertura nel profilo del telefono con la funzione di config `ph_cfg_telefono_conta_apertura(ph_srv_profilo(s), pacchetto, ora, …)` (in arrivo, `src/config/COMPITO.md`), poi `ph_srv_profilo_salva`. Mai il pacchetto nei log.

## Nota del 23 nov — Ctrl + rotella = zoom (CONTRATTI.md §17, protocollo §2.4)
Sul contenuto di una finestra, con Ctrl premuto, la rotella (e lo scorrimento del touchpad) NON va come `ph_superficie_rotella`: diventa un pizzico con `ph_superficie_pizzico` nel punto del puntatore (tradotto nelle coordinate del video come per il tocco; nelle bande niente). Verso: rotella su = ingrandisci = passo positivo; uno scatto = 120, il touchpad proporzionale (stesso accumulo dei resti della rotella). Gesto: INIZIO al primo passo (con quel passo), CONTINUA per i successivi, FINE 250 ms dopo l'ultimo passo, al rilascio di Ctrl, all'uscita del puntatore, alla perdita del fuoco, alla minimizzazione e prima di qualunque tocco (pressione del mouse). Il gesto di pizzico del touchpad (`GtkGestureZoom`) manda lo stesso messaggio (passo = 120 · log(scala/scala precedente)/log(1,25), arrotondato con resto), se non complica: altrimenti scrivilo nel rapporto. Ctrl + rotella non arriva mai al telefono come rotella. Prove di logica: la macchina del gesto (inizio/continua/fine, scadenza di 250 ms con il tempo passato come parametro, chiusura prima di un tocco) e la conversione dei passi.

## Nota del 23 nov sera — voce «Compatibilità» (CONTRATTI.md §19)
- Menu «⋮» (`PH_POPUP_ALTRI_COMANDI`): dopo «Ruota», prima di «Chiudi app», voce a spunta «Compatibilità» con sotto, più piccola e in grigio (`#9aa4b5`), «Per le app che si vedono troppo piccole». Disattivata durante la registrazione; nessuna voce per lo specchio.
- All'apertura di una finestra NUOVA (dopo `ph_fin_apri`, prima di leggere misura e densità per l'apertura del flusso): se `ph_cfg_telefono_in_compatibilita(ph_srv_profilo(srv), pacchetto)`, chiama `ph_fin_imposta_compatibilita(d, id, true, …)`.
- Al clic: `ph_cfg_telefono_imposta_compatibilita` sul profilo (se torna -1: avviso con la frase e nulla cambia), `ph_srv_profilo_salva`, poi `ph_fin_imposta_compatibilita`; la richiesta che arriva da `ph_fin_avanza` si manda come ogni ridimensionamento con `ph_superficie_ridimensiona_densita` (misura E densità). Niente chiusura della sessione né della finestra: si resta sulla stessa app.
- La superficie disegna l'immagine ridotta (non 1:1) come per la densità fissa: `utile_w/h` = 0. Controlla che il disegno e la conversione delle coordinate del puntatore funzionino con un flusso più grande del contenuto.
- **Da verificare sul telefono** (nel rapporto, con il turno del telefono): un'app che usa la densità dello schermo vero si vede intera dopo il cambio a caldo, senza riaprire; la spunta all'apertura successiva; spegnere la voce riporta la regola automatica (l'app può restare disegnata male finché non si riapre: è il comportamento di partenza). Se il cambio a caldo non basta, fermati e riferiscilo: la riapertura della sessione si decide dopo.
- Prove di logica: voce presente/assente/disattivata, ordine nel menu, effetto sul profilo e su `ph_fin_imposta_compatibilita`, errore di elenco pieno.
