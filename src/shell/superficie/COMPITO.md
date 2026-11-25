# COMPITO — shell-superficie (il widget che mostra un flusso video)

Leggi prima: `CLAUDE.md`, `src/shell/CONTRATTI.md` (§2 thread), `src/shell/superficie.h` (IL contratto da realizzare), `servizi.h` (flussi), `fotogramma.h`, `shell.h`. Specifica §4.2, §4.3, §4.5, §4.12, §5.5 (schermo protetto, connessione persa), §8.1; `PIANO.md` «Proposta di architettura della shell»; il prototipo misurato `strumenti/prototipo-gtk.c` (da leggere come riferimento: `texture_da_drm`, `GtkGraphicsOffload`, conteggi; NON da copiare: lì FFmpeg è incluso, qui no). Riferimento semplice e funzionante dello stesso contratto: `src/shell/finto/superficie_finta.c` (è il minimo: la superficie vera fa di più, vedi sotto).

## Che cosa fa

Realizza TUTTE le funzioni di `superficie.h`. Un widget per flusso:

- **Apertura**: `ph_superficie_nuova` apre il flusso (`ph_srv_flusso_apri`) e avvia UN thread di lettura che cicla su `ph_srv_flusso_leggi(f, &fr, ~100 ms)` (lì avviene ricezione e decodifica: mai sul thread GTK). `-1` = fine del thread.
- **Consegna**: il thread tiene al massimo UN fotogramma in attesa (il nuovo sostituisce e rilascia quello non ancora mostrato: si scartano fotogrammi, mai risoluzione — 4.12) e fa al massimo UNA `ph_shell_consegna` in volo. Sul thread GTK si costruisce la texture e la si mette nel `GtkPicture`.
- **Senza copie**: `PH_FOTO_DMABUF` → `GdkDmabufTextureBuilder` (display, larghezza, altezza, fourcc, modificatore, piani, fd/offset/stride; per i formati YUV lo spazio dei colori da `PhFotoColore` con `gdk_dmabuf_texture_builder_set_color_state` e i parametri CICP: BT.709 limitato = primari 1, trasferimento 1, matrice 1, gamma ristretta; PREDEFINITO = non impostare). La funzione di rilascio della texture chiama `ph_fotogramma_rilascia`. Se la costruzione fallisce: come il prototipo, non rilasciare due volte (al peggio si perde un fotogramma), `ph_srv_flusso_preferisci_memoria(f)` e `ph_shell_decisione("flusso in memoria invece che DMA-BUF", "GTK ha rifiutato il DMA-BUF (fourcc %.4s, modificatore 0x%llx)")`. Prima di costruire, se il formato non è in `gdk_display_get_dmabuf_formats` scriverlo una volta nel registro (il prototipo lo fa).
- **Memoria** (`PH_FOTO_MEMORIA`) → `gdk_memory_texture_new` su un `GBytes` che avvolge i dati senza copiarli (`g_bytes_new_with_free_func` con il rilascio del fotogramma); formati: `PH_FOTO_B8G8R8A8_PREMOLT` → `GDK_MEMORY_B8G8R8A8_PREMULTIPLIED`, `PH_FOTO_R8G8B8A8` → `GDK_MEMORY_R8G8B8A8`, `PH_FOTO_R8G8B8` → `GDK_MEMORY_R8G8B8`.
- **Controllo del fotogramma** prima di usarlo: misure 1–8192, piani 1–4 con fd ≥ 0, passo ≥ larghezza × byte per pixel, dati non NULL; un fotogramma non valido si rilascia e si scarta (con una riga nel registro, una volta).
- **Composizione** (prototipo, strada 1 di 4.3): `GtkPicture` con `GTK_CONTENT_FIT_FILL` e `can_shrink`, dentro un `GtkGraphicsOffload` con fondo nero (`gtk_graphics_offload_set_black_background`); il widget STENDE l'immagine su tutta la sua area (nessuna banda: finestra-app traduce l'input così). Durante trascinamento e ridimensionamento la scala la fa la GPU: nessun lavoro della superficie oltre a ridisegnare.
- **Misura dichiarata**: `PH_SRV_FL_APERTO`/`MISURA` → memorizzata (`ph_superficie_misura_video`) e avviso `PH_SUP_EV_MISURA` a chi ha creato la superficie.
- **Schermo protetto** (`PH_SRV_FL_PROTETTO`, 5.5): al posto dell'immagine nera un messaggio centrato «Questa app protegge il suo schermo: il contenuto non si puo' mostrare.» (testo proposto, già tradotto; fondo #111827, testo #e6eaf2 13 px, a capo automatico) finché il flusso dice protetto. Non si aggira la protezione.
- **Connessione persa** (`PH_SRV_FL_PERSO`, 5.5): l'ultima immagine resta, sfocata (`gtk_snapshot_push_blur`, raggio ~12 px) e scurita, con sopra al centro «Riconnetti ora» (`ph-accento`; → `ph_srv_riconnetti_ora`) e «Chiudi» (`ph-ghost`; → avviso `PH_SUP_EV_CHIUDI`). `PH_SRV_FL_RIPRESO` → tutto torna normale da solo (la sessione è ripartita dietro: stesso `PhSrvFlusso`).
- **App chiusa da sola** (`PH_SRV_FL_APP_CHIUSA`) → `PH_SUP_EV_APP_CHIUSA`. **Errore** (`PH_SRV_FL_ERRORE`) → `PH_SUP_EV_ERRORE`.
- **Pausa/ripresa**: `ph_srv_flusso_pausa/riprendi`; in pausa resta l'ultima immagine (non nero).
- **Ridimensiona**: `ph_srv_flusso_ridimensiona` (la misura vera torna con `MISURA`).
- **Input**: le funzioni `ph_superficie_tocco/rotella/tasto/testo/indietro` passano ai servizi così come sono (la traduzione è di finestra-app).
- **Fotografia**: `ph_superficie_fotografa` = riferimento all'ultima texture mostrata.
- **Chiusura**: `ph_superficie_chiudi` e il dispose del widget: `ph_srv_flusso_interrompi`, attesa della fine del thread, `ph_srv_flusso_chiudi`, rilascio del fotogramma in attesa. Dopo, nessun avviso. Idempotente.
- **Conteggi** per le misure (8.1): ricevuti, mostrati, scartati, rifiuti DMA-BUF; con la variabile d'ambiente `PH_SUPERFICIE_STAT=1` una riga ogni 5 s su stderr per flusso (come il prototipo). Nessun numero inventato: solo contati.

## Che cosa NON fa
- Non include FFmpeg, non decodifica da sé (lo fa il servizio dentro `ph_srv_flusso_leggi`).
- Non traduce coordinate, non decide pause (lo fa finestra-app), non sincronizza con l'audio (A4 aperto, non in quest'ondata).
- Non abbassa mai la risoluzione per scelta propria.

## Dipende da
`superficie.h`, `servizi.h`, `fotogramma.h`, `shell.h`, `config/lingua.h`.

## Criteri di accettazione
1. Con il telefono finto: 3 superfici a 60 fotogrammi al secondo (il contatore nel video avanza regolare), ridimensionamento (F9 nel demo) scalato subito in GPU e flusso nuovo dopo 300 ms senza immagini nere.
2. Con `--dmabuf` (sul tablet, `/dev/udmabuf` presente) le texture sono DMA-BUF (conteggio rifiuti 0); se GTK rifiuta, passa alla memoria con UNA riga nel registro delle decisioni e continua.
3. Pausa (F10): nessun fotogramma letto (stderr del finto), ultima immagine ferma; ripresa riparte.
4. F6 (connessione persa): immagine sfocata con i due pulsanti; al ritorno torna da sola. App «Banca»: messaggio dello schermo protetto.
5. Chiusura durante la lettura e chiusura subito dopo l'apertura: nessuna perdita, nessun accesso dopo la liberazione (prova con AddressSanitizer), nessun avviso dopo `ph_superficie_chiudi`.
6. Il thread GTK non fa mai attese: la `g_thread_join` della chiusura aspetta al massimo il timeout di lettura.
7. Prova di logica (`prova_superficie.c`, senza schermo): lo «scambio» del fotogramma in attesa (il nuovo rilascia il vecchio, una sola consegna in volo) usato da due thread, controllo dei fotogrammi non validi, corrispondenza formati memoria → GDK e `PhFotoColore` → CICP. Per queste prove la logica sta in funzioni pure separate dal widget.
8. `strumenti/prova.sh shell-superficie` → `uscita 0`.

## Come si prova
- `strumenti/prova.sh shell-superficie`.
- Demo: `strumenti/demo-sul-tablet.sh superficie` (tre superfici; F9 ridimensiona, F10 pausa; `--dmabuf` per la via senza copie; `PH_SUPERFICIE_STAT=1` per i conteggi). Il telefono vero non serve: i fotogrammi veri arriveranno dai servizi veri dell'ossatura con lo stesso contratto.

## Nota del 23 nov (PIANO.md «Finestre grandi: modalità tablet»)
Realizza `ph_superficie_ridimensiona_densita(sup, larghezza, altezza, densita)` (in fondo a `superficie.h`): inoltra a `ph_srv_flusso_ridimensiona_densita(f, larghezza, altezza, densita)` (0 = densità invariata), con le stesse regole di `ph_superficie_ridimensiona` (dopo `ph_superficie_chiudi` nulla). Nient'altro: la misura e la densità vere tornano con `PH_SRV_FL_MISURA` come oggi. Realizza anche le funzioni di screenshot e registrazione aggiunte prima (`ph_superficie_screenshot`, `ph_superficie_registra`, `_ferma`, `_registrando`) se non l'hai già fatto: il riferimento è `src/shell/finto/superficie_finta.c`.

## Nota del 23 nov — pizzico (CONTRATTI.md §17)
Realizza `ph_superficie_pizzico(sup, video_w, video_h, fase, x, y, passo)` (in fondo a `superficie.h`): inoltra a `ph_srv_flusso_pizzico`; in pausa e dopo `ph_superficie_chiudi` non fa nulla. Riferimento: `src/shell/finto/superficie_finta.c`.
