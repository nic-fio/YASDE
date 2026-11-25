# YASDE 2.1 — Specifica per Claude Code

> Documento di partenza per la progettazione e lo sviluppo di **YASDE 2.1**: un'app (AppImage) la cui unica finestra, ridimensionabile, è un **desktop environment emulato** nel quale le app di *qualunque* telefono Android (anche senza uscita video) girano come finestre interne.
> Autore delle specifiche: il committente (non scrive codice, dà le specifiche e giudica i risultati). Questo documento nasce da una lunga discussione di funzionalità e da un mockup HTML interattivo iterato più volte.
> Lingua del progetto: **italiano** (nomi, commenti, messaggi, documenti). Questo file è in italiano per coerenza con le convenzioni di lavoro (sezione 11).

**Indice**

0. Come usare questo documento · 1. Visione e prodotto · 2. Principi e decisioni · 3. Base tecnica: flussi, encoder, audio e guardiani · 4. Architettura target · 5. Funzioni della shell (F-01 … F-11) · 6. Riferimento visivo: il mockup · 7. Il componente sul telefono e il protocollo · 8. Prestazioni: obiettivi e metodo · 9. Rischi e punti aperti · 10. Piano per fasi · 11. Convenzioni di lavoro · 12. Appendici

---

## 0. Come usare questo documento

**Stato:** discussione di funzionalità conclusa per il primo ciclo. **Non esiste ancora né un piano né codice.** Questo documento fissa *che cosa* costruire, *perché*, e *dove sono i rischi*. Il *come* dettagliato va ricavato dallo studio e dalle misure della Fase 0 (sezione 10).

**Regole di lettura per Claude Code**

1. Leggi prima, nell'ordine: sezioni 1 (visione), 2 (principi e decisioni), 3 (base tecnica), 4 (architettura), 5 (funzioni).
2. Le frasi marcate **[DECISO]** sono decisioni del committente: non riproporle come alternative. Le frasi marcate **[PROPOSTA]** sono ipotesi di lavoro da confermare con le misure. Le frasi marcate **[APERTO]** sono punti da decidere o verificare, elencati anche nella sezione 9.
3. Ciò che è scritto come "mockup" si riferisce al mockup HTML interattivo allegato (`Main.dc.html`, sezione 6): è il riferimento visivo e di comportamento, **non** codice da portare.
4. Prima di proporre un'alternativa a una scelta di architettura, controlla `notes/user-decisions.md` e `notes/issue-log.md`: una parte delle alternative è già stata scartata (esempi: Flatpak, icone nel menu di sistema, temi scuri come stile, più telefoni attivi contemporaneamente).
5. **Misura prima di scrivere.** È un principio del progetto: studio, poi misure sul telefono reale, poi codice un pezzo alla volta. La Fase 0 produce solo studi e misure (in `notes/`), nessun codice di prodotto.

**Cosa NON fare**

- Non iniziare a scrivere la shell prima della Fase 0.
- Non copiare codice di terzi (scrcpy e AOSP si leggono come documentazione, mai copiati).
- Non installare nulla sul telefono né richiedere root.
- Non legare una soluzione a un produttore di telefoni (vedi 2.3).

---

## 1. Visione e prodotto

### 1.1 Che cos'è

YASDE 2.1 è un'app per PC (Linux, AppImage) con **una sola finestra principale ridimensionabile che rappresenta un desktop**, in stile Samsung DeX / AndroidDex. Sul desktop virtuale ci sono:

- uno **sfondo** personalizzabile,
- **icone di app** e **widget**,
- una **barra** in basso con launcher, finestre aperte, e un'area di sistema (trasferimento file, volume, stato del telefono, notifiche, orologio),
- le **finestre delle app Android**, che restano *dentro* il desktop emulato.

L'utente può mettere la finestra principale a **schermo intero** per avere l'impressione di trovarsi davanti a un vero desktop.

### 1.2 Differenza da DeX e AndroidDex

- **DeX** richiede un telefono Samsung con uscita video (o wireless). YASDE dà una modalità desktop a **qualunque** smartphone Android, **anche senza uscita video**: è la motivazione che il committente considera importante e "nascosta" del progetto.
- **AndroidDex** viene giudicato dal committente poco ottimizzato. YASDE deve stare "un paio di gradini sopra": accelerazione hardware, flussi gestiti con cura, fluidità misurata.
- **Funzione assente in DeX**: il **trasferimento di file PC↔telefono** in entrambe le direzioni (sezione 5.9). Nella shell è una funzione di primo piano, con due pulsanti dedicati nella barra.

### 1.3 Nome

**[DECISO]** Il nome è **YASDE 2.1**. Nel mockup il nome compare nella barra del titolo. L'organizzazione del repository (nuovo repo, branch o cartella nuova) **non è ancora decisa**: **[APERTO]**, da proporre al committente in Fase 0.

### 1.4 Perimetro iniziale

- Piattaforma PC: **Linux x86_64, AppImage**. **[APERTO]** Windows non è nel perimetro finché il committente non lo decide.
- Telefono: **Android 14 e successivi**. Una soglia minima fissa elimina una serie di problemi di compatibilità; non estenderla senza una misura che lo giustifichi.
- Un telefono attivo alla volta.
- Connessione: ADB wireless con il client ADB scritto da zero in YASDE (cavo solo come procedura di ripiego).

---

## 2. Principi e decisioni

### 2.1 I due principi

**[DECISO]** Per il committente due principi, che non si escludono, rendono il progetto solido:

1. **Funzionalità.** Il prodotto deve fare tutto ciò che fa un desktop in stile DeX, più il trasferimento file.
2. **Prestazioni**, intese come **reattività e fluidità senza scatti (nessuno stuttering)**.

La **gestione energetica** del telefono è importante ma è considerata un'**ottimizzazione successiva, non un requisito**. Nel codice vanno però previsti i **punti di aggancio** (hook) per poterla fare dopo senza riscrivere (per esempio: possibilità di sospendere un flusso, di abbassare il frame rate di una finestra, di spegnere il pannello).

### 2.2 Universalità misurata

**[DECISO]** L'obiettivo è funzionare su *qualunque* Android 14+, ma l'universalità va **misurata, non dichiarata**:

- una **tabella dei telefoni testati** (dove l'unico telefono a disposizione del committente è un Samsung S23+, quindi la tabella parte con una riga sola e cresce con le segnalazioni degli utenti),
- un **profilo dell'encoder per telefono** (valori che funzionano, tentativi di ripiego, comportamento osservato),
- segnalazioni degli utenti come fonte di dati.

### 2.3 Restare agganciati ad Android, non ai produttori

**[DECISO]** "L'importante è restare agganciati ad Android, non ai vendor." Conseguenze operative:

- Si usano solo API e comportamenti di **AOSP/Android**. Sul telefono l'accelerazione hardware è **MediaCodec**; non si assume Vulkan Video sui telefoni e Mesa non esiste sui telefoni.
- Un workaround per un produttore è ammesso **solo se innescato da un sintomo osservato** (non dal nome del produttore), **isolato** in un punto preciso del codice, documentato in `notes/`.
- Esempio: il workaround Samsung per la frequenza di aggiornamento a 24 Hz con il pannello spento (vedi 4.7): è innescato da una misura (stato di SurfaceFlinger) e isolato.

### 2.4 Principi di progetto

Principi scritti prima del codice e da seguire ovunque:

| Principio | Significato |
|---|---|
| Niente sul telefono | Nessuna app installata. Il componente è copiato in `/data/local/tmp` a ogni connessione e cancellato alla fine. |
| Il telefono torna com'era | Ogni impostazione modificata ha un *guardiano* che la ripristina, anche se il PC sparisce o il servizio muore di colpo. |
| ADB nostro | Nessun `adb` da installare: TLS, abbinamento con codice, mDNS e canali multipli sono scritti nel progetto. |
| Codice nostro | Il componente sul telefono è scritto da zero; nessun codice di terzi da cui dipendere. |
| Misura prima di scrivere | Studio, misure sul telefono reale, poi codice un pezzo alla volta. Le misure restano in `notes/`. |
| Un file, nessuna traccia | Un solo AppImage; sul PC solo `~/.config/YASDE` e `~/.cache/YASDE`. |

Questi vincoli valgono per **ogni funzione**: tutto ciò che una funzione modifica sul telefono deve avere la sua azione inversa registrata presso il guardiano (vedi la procedura in 7.2).

### 2.5 Registro delle decisioni di funzionalità (tutte confermate dal committente)

| # | Decisione | Note |
|---|---|---|
| D1 | Forma: un AppImage con **una sola finestra principale ridimensionabile = il desktop** | |
| D2 | Opzione **schermo intero** della finestra principale; **un solo controllo**, in alto | Prima era duplicato; tenere solo quello nella barra del titolo |
| D3 | Controlli finestra del PC **standard Linux** (minimizza, ingrandisci, chiudi) | Nel mockup non ci sono scorciatoie di dimensione S/M/L |
| D4 | Finestre delle app con i controlli **Android**: indietro, **screenshot**, **registra lo schermo**, minimizza, ingrandisci, chiudi | Lo screenshot e la registrazione della finestra sono apprezzati dal committente |
| D5 | Launcher come **griglia di app** (come in DeX), non come elenco | |
| D6 | Strategia di rendering delle finestre: **un flusso per app**, composto dalla shell (4.2) | La strategia con un solo display freeform è scartata |
| D7 | Il flusso della finestra viene **scalato in GPU durante il trascinamento** e **riconfigurato a fine movimento** | |
| D8 | **Tre manopole indipendenti**: scala della shell, densità delle app, qualità del flusso (4.4) | |
| D9 | Ciclo di vita: **minimizza** = l'app resta aperta, flusso in pausa; **chiudi** = app tolta dai recenti; "riapri dove eri" opzionale (4.6) | |
| D10 | **Schermo reale del telefono** come **toggle nell'area di sistema** (non una finestra fissa) che apre una finestra con lo schermo reale; mostrarlo non accende il display fisico (4.7) | In DeX non esiste una finestra dello schermo reale |
| D11 | **Blocco**: se il telefono si blocca, si blocca anche la shell (come DeX) (4.8) | |
| D12 | **Widget della shell** (non Android veri) nella prima versione (5.2.3) | |
| D13 | **Icone sul desktop**: modello DeX, **solo app**; si aggiungono dal launcher (trascinamento o menu contestuale), si spostano, si rimuovono (5.2.2) | |
| D14 | **Sfondo personalizzabile in 3 modalità**: colore solido, colore sfumato, immagine scelta dalla **galleria del telefono** (5.2.1) | |
| D15 | **Volume nell'area di sistema** della barra, come DeX (5.8) | |
| D16 | **Widget** aggiunti dal menu del clic destro sul desktop, voce "Aggiungi widget…" (come DeX), insieme a "Cambia sfondo…" | |
| D17 | **Trasferimento file** con due pulsanti nella barra: freccia su = PC→telefono, freccia giù = telefono→PC (5.9) | Funzione chiave, assente in DeX |
| D18 | Sul telefono il file può trovarsi in **qualunque cartella**, non solo in quelle preimpostate; dalla selezione delle cartelle si deve poter risalire fino a **"/"** (5.9.4) | Correzione fatta dopo due iterazioni del mockup |
| D19 | Nome del prodotto: **YASDE 2.1** | |
| D20 | Supporto minimo **Android 14+** | |

---

## 3. Base tecnica: flussi, encoder, audio e guardiani

Elementi tecnici che condizionano tutto il progetto.

### 3.1 Flussi video e dimensioni

- **Un display virtuale per finestra**: ogni finestra app è un `VirtualDisplay` della dimensione della finestra, con l'app lanciata sopra; una finestra mostra anche lo **specchio** dello schermo reale (cap 1920 px per lato). Un flusso H.264 per sessione (un canale `video:<id>`).
- **Encoder**: primo encoder hardware (non alias) per il tipo, **8 Mbit/s, 60 fps dichiarati, keyframe ogni 10 s**, `prepend-sps-pps-to-idr-frames`; ripiego in ordine (hardware senza prepend, poi encoder di default Android). Il **keyframe a richiesta** (`VIDEO_CHIAVE`, `REQUEST_SYNC_FRAME`) costa circa **0,1 s**. Sugli encoder Qualcomm che non emettono nulla su schermo fermo c'è il **ridisegno forzato** (stacca e riattacca la Surface).
- **Ridimensionamento**: la finestra comunica la nuova dimensione; il telefono ridimensiona il display (`VirtualDisplay.resize`) se basta, altrimenti la sessione viene ricreata (se la scala si discosta oltre il 15% con il tetto di 2560 px); un evento di input calcolato sulla dimensione vecchia viene **scartato**.
- **Dimensioni in pixel**: dimensione della finestra in punti × densità del telefono, entro **2560 per lato**, multipli di 8; il display ha la **stessa densità** del telefono (alcune app, per esempio Facebook, disegnano certi elementi con la densità dello schermo reale). Per le app solo-verticali in una finestra larga il display è una colonna a forma di telefono.

### 3.2 Audio, pannello e guardiani

- **Audio**: **un solo canale**, cattura loopback con `AudioPolicy` (`ROUTE_FLAG_LOOP_BACK`), Opus 128 kbit/s, 48 kHz stereo, pacchetti da 20 ms, timestamp dal conteggio dei campioni; riproduzione con margine adattivo e sincronia audio/video. **Regola da non perdere**: la cattura audio parte *dopo* lo specchio e **5 s dopo** che lo specchio è aperto (con lo specchio a interruttore di 4.7 il punto è aperto: vedi A18); con l'ordine diverso i reel di Facebook nelle finestre andavano in sofferenza (misurato; meccanismo interno di Android non compreso). Non cambiare l'ordine senza rifare quei test.
- **Pannello fisico**: regola del "telefono in mano": il pannello si spegne mentre le app sono usate dal PC, il telefono resta sveglio e sbloccato; si riaccende quando l'utente lo prende. Dettagli in 4.7.
- **Guardiani**: due processi shell (di connessione e di servizio) che ripristinano timeout schermo, volume media, pannello, `min_refresh_rate`, file temporanei. Il timeout schermo è portato al massimo durante l'uso (senza, le app sui display virtuali non ricevono input); il volume media è portato al massimo (con volume 0 alcune app, per esempio Facebook, non avviano l'audio dei reel).

### 3.3 Primo collegamento, dati sul PC, stati della connessione e punti da tenere d'occhio

- **Primo collegamento**: procedura guidata di preparazione e abbinamento con codice; scoperta via mDNS; cavo USB come ripiego.
- **Dati sul PC**: `~/.config/YASDE` (`telefoni.toml`, `preferenze.toml`) e `~/.cache/YASDE`.
- **Stati della connessione**: `Cerco`, `Collegato`, `Bloccato`, `Perso`, `Chiuso`. Con la connessione persa la finestra mostra l'ultima immagine sfocata con "Riconnetti ora"; la sessione riparte da sola al ritorno.
- **Punti da tenere d'occhio**: il volume originale dell'utente va salvato e ripristinato correttamente, non come 15 (A17); da evitare: un margine audio adattivo che si restringe solo sul silenzio; `adbd` rifiuta ogni canale con *delayed ack* annunciato (da indagare); memoria del servizio ~145 MB (A16); nessun avviso spurio alla prima lettura delle notifiche (A20).

---

## 4. Architettura target

### 4.1 Livelli

```
 PC Linux (Rust)                                          Telefono Android 14+ (Java)
┌────────────────────────────────────────────┐          ┌───────────────────────────────────┐
│ SHELL                                      │          │ Servizio (app_process, uid 2000)  │
│  finestra host ▸ desktop ▸ barra ▸ launcher│          │  canale comandi (messaggi)        │
│  compositore: texture per finestra, GPU    │          │  Video: 1 sessione per finestra   │
│  widget · sfondo · icone · menu contestuali│          │   (display virtuale + MediaCodec) │
├────────────────────────────────────────────┤          │  Specchio schermo reale (toggle)  │
│ Pezzi lato PC                              │◄────────►│  Audio (1 canale, loopback, Opus) │
│  video · audio · input                     │  ADB su  │  Input (injectInputEvent)         │
│  appunti · azioni · trasferimento file     │  Wi-Fi,  │  Appunti · Pannello · Miniature   │
├────────────────────────────────────────────┤  una     │  Guardiani (custode)              │
│ Collegamento + client ADB                  │  sola    └───────────────────────────────────┘
│  TLS, mDNS, canali, guardiano connessione  │  conn.
└────────────────────────────────────────────┘
```

Regola: **ogni livello chiama solo quelli sotto**. La shell non parla con ADB; il client ADB non sa nulla di video o audio.

Modello di thread: **thread principale GTK** per tutta l'interfaccia, **runtime tokio** per connessione/ADB/componente/audio/video/input, **GStreamer** per decodifica e riproduzione. Comunicano solo con canali (watch, mpsc, Notify). Un widget non lascia mai il thread GTK. La shell deve rispettare questa regola.

### 4.2 Strategia di rendering: un flusso per app, composto dalla shell

**[DECISO]** Ogni finestra interna è **una sessione video** (un display virtuale + un encoder hardware + un canale `video:<id>`). La **shell compone** nella propria finestra i flussi decodificati, come un compositore.

Alternativa **scartata**: un unico display virtuale grande con la modalità finestre libere (freeform) di Android, con un solo flusso per tutto il desktop. Motivi:

- ogni app è l'**unico contenuto del proprio display**: resize, orientamento, schermo protetto, ancoraggio dell'input e chiusura si gestiscono per finestra;
- il freeform di Android dipende dal produttore e dalle impostazioni del sistema (contrario al principio 2.3);
- un solo flusso grande costringerebbe a codificare anche le parti vuote del desktop e legherebbe tutte le finestre alla stessa qualità.

Conseguenza da misurare in Fase 0: **più finestre = più encoder in parallelo sul telefono** (4.12).

### 4.3 La shell lato PC: compositore e rendering

**[DECISO]** L'appoggio a **Vulkan/Mesa** risolve, secondo il committente, "tutti i problemi di performance" del lato PC; il punto debole che resta è l'**HiDPI** (4.4).

**[APERTO]** *Come* comporre molti flussi dentro una sola finestra GTK4 è la prima decisione tecnica della Fase 0. La soluzione semplice è una pipeline per finestra (`appsrc ! h264parse ! decodebin ! videoconvert ! gtk4paintablesink`); con la shell servono più flussi nella stessa superficie. Strade da valutare con prototipi e misure (non con opinioni):

1. widget GTK4 per finestra interna con `GtkPicture`/paintable dentro un contenitore proprio (semplice; da misurare il costo di composizione con N flussi e con trascinamento);
2. **un'unica superficie di rendering GPU** (`GtkGLArea` o rendering Vulkan) dove ogni flusso è una texture, con **importazione DMA-BUF a copia zero** dal decoder (VA-API su Intel/AMD, NVDEC su NVIDIA) e disegno di sfondo, finestre, bordi e ombre a carico della shell;
3. combinazioni (rendering proprio per le finestre, widget GTK per barra e menu).

Criteri di scelta: costo CPU/GPU con 1/3/5 flussi, regolarità dei frame durante trascinamento e ridimensionamento, latenza input→immagine, stabilità con HiDPI e scala frazionaria, gestione di ombre e ritagli, accessibilità minima dei controlli della shell. La scelta va scritta in `notes/user-decisions.md` con le misure.

Tecniche che si possono adottare, scritte da zero (**idee, non codice di terzi**): importazione DMA-BUF a copia zero, conversione colore con compute shader, controllo dei byte del flusso ("confessione" dell'encoder: lo SPS dichiarato deve coincidere con ciò che esce), contratto di qualità scritto (4.12).

### 4.4 Le tre manopole indipendenti (HiDPI e densità)

**[DECISO]** Le seguenti tre grandezze sono **indipendenti**:

| Manopola | Che cosa regola | Default | Dove si vede |
|---|---|---|---|
| **Scala della shell** | Dimensione dell'interfaccia del desktop (barra, icone, controlli) sul PC | Automatica dal sistema | Aspetto del desktop |
| **Densità delle app** | `dpi` del display virtuale: quanto "grandi" appaiono i contenuti delle app Android | **La densità del telefono** | Contenuto delle finestre |
| **Qualità del flusso** | Risoluzione e bitrate del flusso, con **tetto di risoluzione** (2560 px per lato) | Tetto di 2560 px, 8 Mbit/s (3.1) | Nitidezza |

Regole:

- Il display virtuale ha per default la densità del telefono (alcune app disegnano certi elementi con la densità dello schermo reale; con densità diverse verrebbero enormi). Regolarla è un'opzione per l'utente.
- La dimensione in pixel del flusso = dimensione della finestra interna (in pixel fisici della shell) × (densità app / densità shell), entro il tetto e in multipli di 8.
- Con scale frazionarie del PC va evitato l'effetto "sfocato": il testo della shell si disegna alla scala vera; i flussi si scalano in GPU solo in transitorio (4.5).

### 4.5 Trascinamento e ridimensionamento delle finestre

**[DECISO]** Durante il **trascinamento** e il **ridimensionamento** di una finestra interna:

1. la shell **scala la texture** del flusso in GPU (nessuna richiesta al telefono per ogni frame);
2. a **movimento finito** ("assestato": attesa di circa 300 ms senza cambi di misura) la shell chiede al telefono la **nuova dimensione** (`VIDEO_RIDIMENSIONA`, o ricreazione della sessione se la scala cambia troppo);
3. gli eventi di input calcolati sulla dimensione vecchia vengono scartati.

Aggancio ai bordi, per le app ridimensionabili: bordo sinistro → metà sinistra, bordo destro → metà destra, bordo alto → massimizza. Le app **solo-verticali** hanno finestra fissa 9:16, non si ingrandiscono e, a finestra massimizzata/schermo intero, stanno in una colonna a forma di telefono.

### 4.6 Ciclo di vita delle finestre

**[DECISO]**

| Azione | Effetto |
|---|---|
| **Minimizza** | La finestra sparisce dal desktop; **l'app resta aperta sul telefono**; il flusso va **in pausa**. Alla riapertura riparte con un keyframe a richiesta (~0,1 s). |
| **Chiudi** | La finestra si chiude e l'app viene **tolta dai recenti**. Se l'app stava controllando una riproduzione (YouTube, Facebook) viene messa in pausa. |
| **Riapri dove eri** (opzionale) | Alla riapertura dell'app la finestra torna dove era (posizione e dimensione). |

**[APERTO]** *Come* mettere in pausa un flusso. Opzioni da misurare sul telefono reale in Fase 0:

- A) chiudere la sessione **senza** togliere il task (l'app resta tra i recenti) e riaprirla alla ripresa (costo di ricreazione display+encoder);
- B) un **nuovo messaggio** di pausa/ripresa che sospende l'encoder senza distruggere il display (candidato da verificare: `MediaCodec.setParameters` con il parametro di sospensione degli encoder video, quando l'encoder lo supporta; altrimenti smettere di leggere/inviare i pacchetti e richiedere un keyframe alla ripresa).

Criterio: costo di ripresa percepito (tempo da clic a immagine fresca) e carico sul telefono mentre la finestra è in pausa. Qui sta anche il principale **hook energetico** (2.1).

### 4.7 Schermo reale del telefono e pannello fisico

**[DECISO]** In DeX non esiste una finestra dello schermo reale. In YASDE è un **toggle/controllo nell'area di sistema** (popup dello stato del telefono) che apre una **finestra del desktop con lo specchio** dello schermo reale (sessione specchio, tetto 1920 px). Poiché il **pannello fisico si spegne da solo** con la regola del "telefono in mano", **mostrare lo schermo reale non accende il display fisico**: era l'obiettivo dichiarato.

Regole del pannello:

- il pannello si spegne con `SurfaceControl.setDisplayPowerMode` (agisce solo sul compositore: Android crede lo schermo acceso);
- spegnimento a *telefono in mano*: un tocco/tasto/clic dal PC o l'inattività per il timeout scelto dall'utente lo rimettono in stato "spento"; sblocco manuale, chiamata in arrivo, fine chiamata, chiusura dell'ultima sessione lo accendono;
- con pannello spento va portato `min_refresh_rate` a 60 prima di spegnere (workaround Samsung innescato dalla misura di `dumpsys SurfaceFlinger`);
- lo **specchio conta come sessione**: con lo specchio aperto, chiudere l'ultima finestra non riaccende il pannello;
- la regola dei **no riavvii durante una chiamata**.

Nel mockup il popup del telefono ha: toggle "mostra schermo del telefono", toggle "pannello fisico", pulsante "simula blocco".

### 4.8 Blocco del telefono

**[DECISO]** Come DeX: **se il telefono si blocca, si blocca anche la shell**. Si usa lo stato `Bloccato` (3.3): la shell mostra un velo sul desktop con il motivo; con connessione persa o componente guasto il velo offre "Riconnetti ora". Su telefoni Samsung il blocco fa cadere il debug wireless: la connessione si riapre allo sblocco e le finestre tornano dove erano (comportamento da garantire per le finestre interne e per il layout del desktop).

### 4.9 Audio

- **Un solo canale audio** (loopback, Opus 128 kbit/s), con tutti i suoni del telefono riprodotti dagli altoparlanti del PC; il telefono resta muto mentre la policy è attiva e torna a suonare da solo alla rimozione.
- **Volume** nella barra (5.8): **guadagno lato PC** dell'audio di YASDE; **non modifica il volume del telefono**. Include il muto. Nota: durante l'uso il volume media del telefono è portato al massimo e poi ripristinato (guardiano); il controllo dell'utente è quindi solo lato PC.
- **[APERTO]** Con più finestre video e **un solo audio**, la sincronia A/V (ogni frame è mostrato quando suona l'audio dello stesso istante del telefono) va definita per più flussi: la sincronia va applicata alla finestra "media" in primo piano (4.12), mentre le altre non hanno bisogno di sincronia stretta. Da progettare in Fase 0.

### 4.10 Input

Input: messaggi `0x50–0x5f`, un thread di iniezione, `setDisplayId` sempre, mouse = un dito, rotella = `ACTION_SCROLL`, tastiera come `KeyEvent` virtuali; testo non ASCII con "incolla". La **traduzione delle coordinate**: l'evento nasce nelle coordinate della **shell**; la shell lo traduce nelle coordinate del **contenuto della finestra interna** (togliendo barra del titolo, bordi, scala) e poi in coordinate del display (`x × larghezza display / larghezza usata dal PC`). Eventi su dimensione diversa da quella dichiarata dal video vengono scartati. Il focus è della finestra attiva della shell; i tasti riservati alla shell (per esempio quelli per schermo intero) non vanno al telefono.

### 4.11 Appunti

Appunti: bidirezionale, solo testo; non passano copie marcate sensibili, testi >200.000 byte, né testi da password manager.

### 4.12 Più flussi: budget, modalità e qualità

**Contratto di qualità**: **degradare nel tempo, non nello spazio** — mai peggiorare la risoluzione per scelta del programma; piuttosto, **scartare frame**; la frequenza cala solo **su misura**, e la misura va scritta nel log accanto al numero che l'ha causata.

**[PROPOSTA]** Due modalità per finestra:

- **interattiva**: priorità alla latenza input→immagine (poco o nessun buffer di riproduzione);
- **media**: priorità alla fluidità (piccolo buffer di riproduzione, sincronia A/V attiva se è la finestra in primo piano).

**[APERTO]** Misure di capacità sul telefono (con il S23+): quanti encoder hardware in parallelo regge senza degradare gli altri, bitrate complessivo su Wi-Fi con 1/3/5 flussi a 8 Mbit/s ciascuno, eventuali limiti di istanze di `MediaCodec`, comportamento termico in sessioni lunghe. Da qui discende un **tetto di finestre attive** o un **degrado ordinato** (per esempio ridurre il frame rate delle finestre non in primo piano, mai la risoluzione per scelta propria). Il risultato entra nel **profilo encoder per telefono** (2.2).

### 4.13 Universalità: profilo per telefono

Un file di profilo (nella cache o nei dati sul PC, `telefoni.toml` o un file nuovo) che registra per ogni telefono: encoder scelto e ripiego usato, `repeat-previous-frame-after` ignorato o no (e quindi se serve il ridisegno forzato), massimi misurati (flussi paralleli), sintomi osservati e workaround attivati. Alimenta la **tabella dei telefoni testati** pubblicata con il progetto.

### 4.14 Energia (solo hook)

Hook da prevedere, **senza implementare l'ottimizzazione**: pausa di un flusso (4.6), tetto di frame rate per finestra, soglie di bitrate per finestra, spegnimento pannello, stato "schermo del PC non visibile" (finestra host minimizzata → tutti i flussi in pausa). Ogni decisione automatica va nel log con la causa.

---

## 5. Funzioni della shell (specifica funzionale)

Ogni funzione ha un identificatore **F-xx** per la tracciabilità (test, commit, documentazione).

### 5.1 F-01 · Finestra principale (host)

- Un AppImage, **una sola finestra principale**, **ridimensionabile**, con i **controlli standard del window manager Linux**: minimizza, ingrandisci/ripristina, chiudi. Nel mockup sono disegnati nella barra del titolo (stile libadwaita); nel prodotto sono quelli del sistema/GTK.
- **Schermo intero**: un **solo** pulsante, **nella barra del titolo**. In schermo intero la barra del titolo si riduce a una **striscia sottile (6 px)** che, passando il mouse sul **bordo alto**, si espande (36 px nel mockup) e permette di uscire. Il pulsante cambia etichetta ("Schermo intero" / "Esci da schermo intero").
- Dimensioni del mockup (riferimento): finestra host 980×584 di partenza; schermo intero 1280×800; ingrandita 1280×764.
- Se la finestra host viene **chiusa**: la connessione si chiude e il telefono viene ripristinato (guardiani); il mockup mostra, per illustrazione, una schermata "YASDE 2.1 è chiuso — Riapri".
- Un secondo avvio **non** apre una seconda connessione: porta in primo piano l'istanza esistente.
- **Criteri di accettazione**: ridimensionamento fluido (senza scatti) con N finestre interne aperte; schermo intero entra/esce senza ricreare le sessioni; la striscia in alto si espande in meno di 150 ms al passaggio del mouse.

### 5.2 F-02 · Desktop

#### 5.2.1 F-02a · Sfondo

**[DECISO]** Tre modalità, scelte da una finestra di dialogo aperta dal **clic destro sul desktop → "Cambia sfondo…"**:

| Modalità | Contenuto |
|---|---|
| **Colore solido** | Selettore colore + tinte rapide |
| **Colore sfumato** | Due colori (Da / A) + direzione (diagonale, verticale, orizzontale, diagonale inversa) |
| **Immagine dalla galleria del telefono** | Griglia di anteprime "Recenti" della galleria del telefono + adattamento: **Riempi**, **Adatta**, **Allunga**, **Centra** |

- Il dialogo ha un'anteprima in alto.
- L'immagine scelta viene **copiata sul PC** (in `~/.config/YASDE` o `~/.cache/YASDE`) e **ridimensionata** alla risoluzione utile: lo sfondo resta anche **a telefono scollegato** e **non pesa né sul telefono né sullo stream**.
- Le anteprime vengono dal telefono con il meccanismo delle **miniature** (gruppi di 40, 192 px; foto ridimensionate da `BitmapFactory` e raddrizzate con EXIF; video con un fotogramma da `MediaMetadataRetriever`). Il comando helper `sfondo [larghezza]` dà lo sfondo corrente del telefono come PNG (utile come voce "usa lo sfondo del telefono", opzionale).
- **[APERTO]** Permessi: leggere la galleria con il helper senza root e senza app installata. L'helper deve elencare e miniaturizzare file da `/sdcard/DCIM/Camera`, `Screenshots`, ecc. con `sync:`: va verificato che lo stesso valga su tutte le versioni 14+ e per le cartelle di terze parti.
- Persistenza: tipo di sfondo, colori, direzione, immagine scelta e adattamento, per telefono.

#### 5.2.2 F-02b · Icone sul desktop (modello DeX)

**[DECISO]** Solo **app** (non file). Comportamento:

- **Aggiungere**: (a) **trascinare** un'app dal launcher sul desktop: appena inizia il trascinamento il launcher si chiude, compare un'icona fantasma che segue il mouse e un **riquadro tratteggiato** sulla cella di destinazione; (b) **clic destro** sull'icona nel launcher → **"Aggiungi al desktop"** (va nel primo posto libero). Se un'app è già sul desktop, il trascinamento la sposta.
- **Spostare**: trascinare l'icona; si aggancia a una **griglia** (nel mockup celle di 92×96 px a partire da 16,16; area utile a sinistra dei widget). Se la cella è occupata, va nella libera più vicina.
- **Rimuovere**: clic destro → "Rimuovi dal desktop": toglie **solo il collegamento**, non l'app dal telefono.
- **Aprire**: un clic (nel mockup, un solo clic) apre o porta in primo piano l'app.
- Menu contestuale dell'icona: **Apri**, **Aggiungi/Rimuovi dal desktop**.
- Persistenza: posizione (colonna, riga) per ogni app, per telefono.
- Icone: vengono dal comando helper `app [lato]` (con le icone PNG in base64). Il mockup usa segnaposto con le iniziali.
- **Criteri di accettazione**: il trascinamento dal launcher non fa scattare i flussi attivi; spostare un'icona non apre l'app.

#### 5.2.3 F-02c · Widget della shell

**[DECISO]** Nella prima versione i widget sono **widget propri della shell**, disegnati dalla shell sul PC con dati letti dal telefono. Non sono i widget Android veri (vedi 9).

- **Aggiunta**: clic destro sul desktop → **"Aggiungi widget…"** (come DeX), che apre un catalogo; "Aggiungi" mette il widget nel primo spazio libero.
- **Posizionamento**: trascinamento libero con aggancio a passi di 8 px.
- **Dimensioni**: tre (**Piccolo**, **Medio**, **Grande**) dal **clic destro sul widget**. Più grande = più informazioni. Larghezze nel mockup: 200 / 250 / 320 px; altezza automatica.
- **Rimozione**: clic destro → "Rimuovi widget".
- Un interruttore globale può nascondere tutti i widget.

Catalogo del mockup e **fattibilità della sorgente dati** (da verificare in Fase 0; ✓ = fattibile con le fonti note o banale, ? = da studiare):

| Widget | Piccolo / Medio / Grande | Sorgente dati | Fattibilità |
|---|---|---|---|
| Orologio | ora / + data / + secondo fuso | PC | ✓ |
| Stato del telefono | batteria / + rete / + pannello e schermo | Notifiche del telefono (batteria e rete da `dumpsys`), stato del pannello e del blocco dallo stato della connessione (3.3) | ✓ |
| Notifiche recenti | 1 / 2 / 4 voci | Notifiche del telefono | ✓ |
| Memoria del telefono | usato / + suddivisione | `df`/`StatFs` via shell | ? semplice, da misurare |
| Calendario | 1 / 2 / 4 eventi | Provider di calendario di Android | ? accessibile dal shell? |
| Note rapide | ultime note | Non esiste un'app note standard in AOSP | ? sorgente da definire (può essere locale al PC) |
| Meteo | adesso / + 2 giorni / + 3 giorni | Nessuna API Android standard | ? sorgente da definire (probabile servizio web, con il consenso dell'utente) |
| In riproduzione | brano / + avanzamento / + prossimo | `MediaSession` del telefono (via shell) | ? da verificare; l'audio resta quello di YASDE |

Un widget la cui sorgente non è fattibile in modo agnostico **non entra nella prima versione**: meglio sei widget solidi che otto incerti.

Persistenza: elenco dei widget con tipo, posizione (distanza dal bordo destro e dall'alto) e dimensione, per telefono.

#### 5.2.4 F-02d · Menu contestuali del desktop

- **Sul desktop vuoto**: "Cambia sfondo…", "Aggiungi widget…".
- **Su un'icona** (desktop o launcher): "Apri", "Aggiungi al desktop" / "Rimuovi dal desktop".
- **Su un widget**: sezione "Dimensione" (Piccolo / Medio / Grande, con l'attuale segnata), "Rimuovi widget".

### 5.3 F-03 · Barra (in basso, 48 px)

Da sinistra a destra:

1. **Pulsante del launcher** (griglia 2×2).
2. Separatore.
3. **App aperte** (una voce per finestra): icona con indicatore sotto; clic = se è in primo piano la minimizza, se è minimizzata la ripristina, altrimenti la porta davanti.
4. Spazio flessibile.
5. **Area di sistema**, in ordine:
   - **Invia file al telefono** (freccia su) — F-09a,
   - **Scarica file dal telefono** (freccia giù) — F-09b,
   - separatore,
   - **Volume** — F-08,
   - **Stato del telefono** (rete, batteria, percentuale) — F-07,
   - **Notifiche** (campanella con **contatore**) — F-06,
   - **Orologio**.

I pulsanti di trasferimento mostrano un **puntino colorato** se ci sono trasferimenti in corso. Tutti i pulsanti hanno `aria-label` e suggerimento.

### 5.4 F-04 · Launcher (griglia di app)

**[DECISO]** Overlay a **griglia** di app (come in DeX), non un elenco. Bordi interni a 24 px (24 px sopra, 60 px in basso per lasciare la barra).

- Campo di **ricerca** in alto al centro; **Invio apre la prima app trovata**; "Nessuna app trovata" se vuoto.
- Colonne calcolate dalla larghezza.
- Ogni app: icona + nome. **Clic** apre l'app; **trascinamento** la mette sul desktop; **clic destro** apre il menu contestuale (5.2.4).
- L'elenco viene dal comando helper `app` ed è valido solo se termina con la riga `fine`: una lista interrotta da una caduta non si usa e si rilegge alla riconnessione.
- Altre voci del menu di un'app, dove hanno senso: Apri / Porta in primo piano, Chiudi app, Preferiti, Info app, Disinstalla (solo app utente, con conferma).

### 5.5 F-05 · Finestre delle app

- **Barra del titolo**: **Indietro**, **Screenshot**, **Registra lo schermo**, **Minimizza**, **Ingrandisci/Ripristina**, **Chiudi**; il nome dell'app al centro; indicatore **REC** durante la registrazione.
  - Screenshot: salva un PNG con data e ora nella cartella `/YASDE` (percorso esatto da definire) e lo copia negli appunti; **Copia screenshot** (Ctrl+Maiusc+C).
  - Registrazione: MP4 in `/YASDE`; durante la registrazione la dimensione non cambia.
  - Altri comandi nel menu "altri comandi" (⋮): **Ruota** (Ctrl+R; nessun effetto se massimizzata o in registrazione), **Chiudi app** (Ctrl+W).
- **Trascinamento** dal titolo; **ridimensionamento** dall'angolo in basso a destra; **aggancio ai bordi** (4.5).
- **Z-order** e **focus**: un clic su una finestra la porta davanti e la rende attiva; la barra mostra quale è attiva.
- App **solo-verticali**: finestra 9:16 fissa, senza ingrandimento e senza bordi trascinabili; a schermo massimo, colonna a forma di telefono.
- Piè di finestra diagnostico (facoltativo, utile in sviluppo): dimensione del flusso (× densità, entro 2560), codec e bitrate.
- **Schermo protetto** (`FLAG_SECURE`): messaggio al posto dell'immagine nera (non si aggira la protezione).
- **Connessione persa / componente guasto**: ultima immagine sfocata con "Riconnetti ora" e "Chiudi"; riparte da sola.
- **Criteri di accettazione**: aprire, spostare, ridimensionare, minimizzare e chiudere finestre non provoca finestre nere né desincronizzazioni; l'input va sempre nel punto giusto dopo un ridimensionamento.

### 5.6 F-06 · Notifiche

- **Campanella** nella barra con **contatore**. Il pannello (a destra, sopra la barra) elenca le notifiche del telefono con: titolo, testo, app, ora, pulsante **Nascondi**, e **Nascondi tutte**. Nascondere agisce **solo nella shell** (resta sul telefono) e la notifica **riappare se viene aggiornata**.
- **Toast** in basso a destra per le notifiche nuove; un clic apre l'app e nasconde la notifica.
- Il widget "Notifiche recenti" (5.2.3) usa la stessa sorgente.
- Restano disponibili le notifiche di sistema via D-Bus (opzione dell'utente "avviso a comparsa").
- Nota: possibili avvisi spuri alla prima lettura delle notifiche dopo l'apertura della shell: da verificare e correggere (A20).

### 5.7 F-07 · Stato del telefono e popup

- Pulsante con icona di rete, batteria e percentuale.
- Popup con: **mostra lo schermo del telefono** (apre la finestra con lo specchio, 4.7), **pannello fisico acceso/spento**, e pulsante **simula blocco** (solo nel mockup; nel prodotto lo stato è reale).

### 5.8 F-08 · Volume

**[DECISO]** Nell'area di sistema della barra, come DeX.

- Pulsante con icona di altoparlante (diversa se mutato o a 0); un clic apre un popup con **cursore 0–100**, pulsante **muto**, percentuale e nota: "Regola l'audio di YASDE 2.1 sul PC. Il volume del telefono non cambia."
- **Semantica**: guadagno lato PC dell'audio di YASDE. Muto non ferma la cattura (si smette di riprodurre). Persistenza: ultimo livello.
- **[PROPOSTA]** Estensione: scorciatoie da tastiera per volume/muto quando la shell ha il focus.

### 5.9 F-09 · Trasferimento file PC ↔ telefono

**[DECISO]** È la funzione che il committente considera "la vera chicca" dell'app. È **parte della shell** e **non ha il limite delle cartelle preimpostate**.

Parti coinvolte: copia verso il telefono, elenco delle cartelle e ricezione (`sync:`); luoghi, Recenti, miniature e selezione; installazione e disinstallazione di APK. Comportamenti: **nome libero senza sovrascrivere**, **data di modifica del telefono** al file ricevuto, **notifica al media scanner** perché il file compaia in Galleria/File, nascosti i file che iniziano con il punto, elenchi a pagine di 200 voci ("Mostra altri N").

#### 5.9.1 F-09a · Invia al telefono (freccia su, PC → telefono)

Pannello sopra la barra con:

- **Zona di trascinamento**: "Trascina i file in qualsiasi punto della finestra" — il trascinamento di file dal PC su **tutta la finestra della shell** apre il pannello e li mette in coda, mostrando sul desktop un riquadro "Rilascia per inviare al telefono". Più file insieme.
- Pulsante "scegli file" (selettore del sistema).
- **Cartella di destinazione sul telefono** (campo con il percorso corrente + pulsante "Cambia…" che apre il selettore di cartelle, 5.9.4). Tasti rapidi per le cartelle usate più spesso (Download, Documenti, Immagini, Musica).
- Elenco dei trasferimenti con **nome, stato (percentuale / Completato), barra di avanzamento, dimensione e destinazione completa**.
- Installazione di **APK** trascinato: confermata dall'utente (`pm install -r`, con spiegazioni dei rifiuti in parole semplici).

#### 5.9.2 F-09b · Scarica dal telefono (freccia giù, telefono → PC)

Pannello sopra la barra con:

- **Percorso** del telefono, **modificabile**: si digita e Invio va alla cartella; se non esiste: "Cartella non trovata".
- Pulsante **"Su"** per risalire.
- **Ricerca** in tutto il telefono (file e cartelle); i risultati mostrano il percorso; un clic su una cartella ci entra, su un file lo seleziona.
- **Elenco** della cartella con **selezione multipla** (casella per i file, freccia per le cartelle); le selezioni **restano cambiando cartella**.
- Pulsante **"Scarica N file"**; disattivato (e con etichetta "Seleziona i file da scaricare") se non c'è nulla di selezionato.
- **Cartella di destinazione sul PC**: percorso corrente + "Cambia…" che apre il selettore di cartelle del PC (come 5.9.4).
- Elenco dei trasferimenti come nell'invio.
- **Luoghi rapidi** possibili: Recenti (ultimi 7 giorni di Camera, Screenshot, Download, Documenti, WhatsApp, per gruppi Oggi/Ieri/Questa settimana), Camera, Screenshot, Download, WhatsApp, Documenti, Memoria del telefono, Schede SD (da `/storage`).

#### 5.9.3 Trasferimenti: comportamento comune

- **Coda**: più file in sequenza. **[PROPOSTA]** Annullamento per singolo file e per tutto.
- Alla fine di ogni trasferimento: **avviso** ("Inviato al telefono" / "Scaricato sul PC", con il nome del file). **[APERTO]** Azione successiva: "Apri cartella" (con il file evidenziato quando è uno solo), apertura del file, o solo avviso.
- Preferenze: ultima cartella usata per invio e ricezione, per telefono. La cartella di invio è **qualunque cartella** (non una tra sei preimpostate).

#### 5.9.4 Selettore di cartelle (telefono e PC)

**[DECISO]** Richiesta esplicita, dopo due iterazioni: dal selettore di cartelle si deve poter **tornare indietro fino a "/"** sia sul **telefono** sia sul **PC**, e raggiungere **qualunque cartella** (esempio fatto dal committente: una cartella "pippo" sul telefono).

- **Telefono**: radice vera di Android (`/`) → `storage` → `emulated` → `0` (memoria interna). La memoria interna è `/storage/emulated/0` (percorso mostrato per intero; `/sdcard/…` si accetta come scorciatoia).
- **PC**: radice `/`, con la home dell'utente come punto di partenza.
- **Controlli**: percorso digitabile con Invio; pulsante "Su"; elenco delle sottocartelle; **ricerca** (telefono: in tutto il telefono, solo cartelle nel selettore di destinazione); **"Nuova cartella"**; **"Usa questa cartella"**.
- **Cartelle protette**: con il helper senza root (uid 2000, non è root) alcune cartelle non sono leggibili (per esempio `/data` o parti di `Android/data` con le restrizioni di Android recente). In quel caso la cartella mostra **"Accesso non consentito dal sistema"**, non un elenco vuoto. **[APERTO]** Verificare in Fase 0 quali cartelle risultano leggibili dal helper e come cambia da una versione di Android a un'altra; la regola resta "misurare, non assumere".
- Ricerca sul telefono: i file si leggono con `sync:` cartella per cartella. Una ricerca su tutto il telefono richiede un elenco ricorsivo: **[APERTO]** valutare un comando helper dedicato (`cerca`), come `miniature`, oppure `find` via shell, con un limite di risultati (nel mockup 30) e interruzione se l'utente digita altro.
- Percorsi sempre mostrati **per intero** (non solo "Telefono/…").

### 5.10 F-10 · Persistenza e configurazione

Tutto sul PC, in `~/.config/YASDE` (e cache in `~/.cache/YASDE`), niente sul telefono. **[PROPOSTA]** Un file per telefono (per numero di serie, sul modello di `telefoni.toml`) con: sfondo, icone sul desktop (posizioni), widget (tipo, posizione, dimensione), volume, ultime cartelle di invio/ricezione, densità app, stato delle finestre (per "riapri dove eri"). Preferenze generali in `preferenze.toml` (tasto Esc come indietro, avvisi, lingua, cartella ricevuti, …).

### 5.11 F-11 · Lingua e accessibilità

- Interfaccia **italiano/inglese**: testi in italiano nel codice; traduzioni inglesi nelle tabelle `data/en/`; macro `t!`; un test verifica che ogni testo abbia la traduzione con gli stessi segnaposto.
- Controlli con etichette accessibili; tutto ciò che è cliccabile è un vero controllo (non un `div` generico); contrasto del testo sufficiente anche per le etichette piccole.
- Tema: la shell disegna il proprio tema scuro (il desktop è disegnato dalla shell); la finestra host e i dialoghi seguono il tema del sistema dove possibile.
---

## 6. Riferimento visivo: il mockup

Il mockup è un file HTML interattivo (`Main.dc.html`, allegato a questo documento) creato in un canvas di progettazione. È il riferimento per **aspetto e comportamento**, non codice da portare in GTK. Dati finti: app, notifiche, calendario, cartelle del telefono e i trasferimenti sono simulati; nessun telefono è collegato.

### 6.1 Come aprirlo

Il file è un sorgente del canvas (`<x-dc>` con script `DCLogic`): non si apre direttamente in un browser come pagina normale. Va usato come **documento di riferimento da leggere**: contiene tutta la logica di stato (finestre, trascinamenti, menu contestuali, trasferimenti simulati) in un unico file, ed è la fonte più precisa dei comportamenti descritti nella sezione 5.

### 6.2 Misure e convenzioni del mockup (riferimento)

| Elemento | Valore |
|---|---|
| Finestra host (di partenza) | 980×584; schermo intero 1280×800; ingrandita 1280×764 |
| Barra del titolo host | ~36 px; in schermo intero striscia di 6 px che si espande a 36 px |
| Barra in basso | 48 px |
| Griglia icone desktop | celle 92×96 px, origine 16,16; icona 48×48 px (raggio 12 px) |
| Icone del launcher | 60×60 px |
| Widget | larghezze 200 / 250 / 320 px; passo di posizionamento 8 px; angoli 14 px |
| Pannello notifiche | 340 px di larghezza |
| Pannelli di trasferimento | 380 px di larghezza |
| Finestre app (esempio) | note 400×290, messaggi 296×470 (verticale 9:16), generica 380×420 |
| Accento | `#2dd4bf` (variabile) |
| Sfondo predefinito | sfumatura 135°, `#0e2638` → `#1a625f` |

### 6.3 Elementi non da imitare alla lettera

Gli stati "host minimizzato" (pillola in basso) e "host chiuso" (schermata Riapri) del mockup **simulano** comportamenti del window manager del PC: nel prodotto sono quelli del sistema. I segnaposto (iniziali al posto delle icone, gradienti al posto delle foto) vanno sostituiti da icone e miniature reali.

---

## 7. Il componente sul telefono e il protocollo

Procedura per estendere: **un messaggio nuovo tocca due lati** (classe Java e modulo Rust); si sceglie il numero nell'intervallo giusto (video `0x40–0x4f`, input/appunti `0x50–0x5f`; per una nuova parte, un nuovo intervallo di 16); si ricostruisce il jar con `android/helper/build.sh`; lato PC si aggiunge la costante in `componente::tipo` (o `input::tipo`); si aggiunge il nome al test `nessun_tipo_usato_da_due_moduli`; test di codifica con byte attesi; descrizione nel capitolo del manuale. **Un jar vecchio risponde `ERRORE tipo sconosciuto`**: il PC deve saperlo gestire. `PROTOCOLLO` (valore iniziale 1) cambia **solo** se un messaggio esistente cambia significato.

### 7.1 Messaggi e comandi da aggiungere (da confermare in Fase 0)

| # | Cambiamento | Tipo | Note |
|---|---|---|---|
| P1 | Pausa/ripresa di una sessione video (4.6) | messaggio nuovo nell'intervallo video, o uso di `VIDEO_CHIUDI`+`VIDEO_APRI` | Misurare prima |
| P2 | Densità per sessione (`dpi` diverso dalla densità del telefono) | previsto nel messaggio `VIDEO_APRI` (`larghezza altezza dpi`) | Nessun messaggio nuovo |
| P3 | Elenco cartelle con stato di accesso (lettura consentita o no) | uso di `sync:` + eventuale comando helper | Per "Accesso non consentito" |
| P4 | Ricerca ricorsiva sul telefono | comando helper `cerca` (breve, stampa e termina) o `find` via shell | Limite di risultati; interrompibile |
| P5 | Miniature per la galleria dello sfondo | `miniature` | Solo un uso nuovo |
| P6 | Dati dei widget (memoria, calendario, riproduzione) | comandi helper brevi nuovi, o `dumpsys`/`content` via shell | Una sorgente per volta, solo se agnostica |
| P7 | Sfondo del telefono come immagine | comando helper `sfondo [larghezza]` | Facoltativo |
| P8 | Accodamento e annullamento dei trasferimenti | lato PC (coda) + annullamento del `sync:` in corso | |

### 7.2 Vincoli per ogni cambiamento

- **Guardiano**: tutto ciò che si cambia sul telefono ha la sua azione inversa registrata (`Servizio.custode().imposta("nome", ordine, "comando")`, `togli("nome")`); ordini assegnati: pannello 400, refresh rate 410, task 500; si prova con `kill -9` sul servizio (`YASDE-prova shell 'kill -9 …'`).
- **Il valore dell'utente vince**: se l'azione cambia un'impostazione dell'utente, la si ripristina solo se ha ancora il valore di YASDE.
- **Un nuovo canale** (se serve un flusso proprio): gestore `static void gestisci(LocalSocket s, String tipo)` + voce in `Servizio.TIPI`; gira su un proprio thread, cattura tutti gli errori (anche gli `Error` delle API nascoste) e chiude con `shutdownInput/Output` prima di `close`.
- **API nascoste**: restano dietro `Nascoste` (riflessione) con `Autotest` all'avvio; qualsiasi API usata in più va dichiarata lì.

---

## 8. Prestazioni: obiettivi e metodo di misura

### 8.1 Obiettivi (**[PROPOSTA]**, da confermare dopo le prime misure)

Il principio è "reattività e fluidità senza scatti". Gli obiettivi vanno **numerici e misurati**, non dichiarati. Metriche per ogni scenario:

| Metrica | Come si misura | Obiettivo da fissare |
|---|---|---|
| Latenza input→immagine | Clic/tocco dal PC fino alla prima modifica visibile nella finestra | da fissare con le prime misure, poi non peggiorare |
| Regolarità dei frame | Intervalli tra frame presentati: mediana, p95, p99 | p95 vicino al periodo nominale; nessun intervallo > 2× in condizioni normali |
| Frame persi | Per flusso e per finestra | Zero in uso normale; ogni perdita registrata con la causa |
| Fluidità del trascinamento | Frame della shell durante spostamento/ridimensionamento di finestre | Nessuno scatto visibile con 1, 3 e 5 flussi |
| Carico PC | CPU e GPU della shell con 1/3/5 flussi | Documentato; margine per l'utente |
| Carico telefono | Encoder in parallelo, temperatura, batteria | Documentato per telefono (profilo) |
| Banda Wi-Fi | Bitrate complessivo con N flussi | Documentato |
| Sincronia A/V | Con una finestra media in primo piano | **[APERTO]** valore da fissare con le prime misure (12.3, domanda 5) |

Principio dal contratto di qualità: **degradare nel tempo, non nello spazio**; la risoluzione non scende per scelta del programma; si scartano frame e la frequenza cala solo su misura.

### 8.2 Metodo

- Gli strumenti di misura sono **strumenti di prova** (come `YASDE-prova` e le prove numerate `notes/connection-tests.md §NN`): le misure si numerano, restano in `notes/`, e ogni decisione di architettura le cita.
- Le misure sul telefono reale (S23+) vanno separate da quelle di laboratorio.
- Etichettare ogni numero come **misurato ora**, **registrato** (da una campagna precedente) o **codificato** (costante del sorgente); nessuna cifra derivata o estrapolata.

---

## 9. Rischi e punti aperti

| # | Punto | Stato | Come si chiude |
|---|---|---|---|
| A1 | Strada di composizione dei flussi nella finestra GTK (4.3) | **Aperto** | Prototipi e misure, Fase 0 |
| A2 | Pausa/ripresa dei flussi (4.6) | **Aperto** | Misura A vs B, Fase 0 |
| A3 | Quanti encoder paralleli regge il telefono, con quale banda (4.12) | **Aperto** | Benchmark 1/3/5 flussi, Fase 0 |
| A4 | Sincronia A/V con più flussi e un solo audio (4.9) | **Aperto** | Progetto in Fase 0/2 |
| A5 | HiDPI e scale frazionarie (4.4) | **Aperto** | Prototipi su più scale; punto indicato dal committente come delicato |
| A6 | Accesso del helper alla galleria e alle cartelle (5.2.1, 5.9.4) | **Aperto** | Test su Android 14+ con il S23+; "misurare, non assumere" |
| A7 | Cartelle protette (es. `/data`, `Android/data`) | **Aperto** | Mostrare "Accesso non consentito dal sistema" e documentare |
| A8 | Ricerca ricorsiva sul telefono (5.9.4) | **Aperto** | Comando helper o `find`; limite e interruzione |
| A9 | Sorgenti dati dei widget (5.2.3): calendario, note, meteo, riproduzione | **Aperto** | Verificare uno per uno; fuori dalla V1 quelli non agnostici |
| A10 | Widget Android veri | **Aperto, fuori dalla V1** | Ospitarli sul telefono e trasmetterli come finestra; dubbio sui privilegi del helper senza privilegi di sistema |
| A11 | Azione dopo un download completato (5.9.3) | **Aperto** | Decisione del committente |
| A12 | Organizzazione del repository (nuovo repo / branch / cartella) | **Aperto** | Proposta in Fase 0 |
| A13 | Windows | **Fuori perimetro** finché non deciso | Decisione del committente |
| A14 | Compatibilità delle API nascoste tra versioni di Android 14+ | Rischio continuo | `Autotest` all'avvio e profilo per telefono |
| A15 | Solo un telefono di prova (S23+) | Vincolo | Tabella dei telefoni testati + segnalazioni degli utenti |
| A16 | Memoria del servizio (~145 MB, costo di avvio ART) e più sessioni | Da misurare | Fase 0 |
| A17 | Il volume originale dell'utente non deve restare salvato come 15 | Rischio | Verificare il valore salvato e ripristinato; rivalutare |
| A18 | Ordine di avvio dell'audio (dopo lo specchio e 5 s) con la shell | **Rischio** | **Non cambiare** senza rifare le prove sull'ordine di avvio (3.2) |
| A19 | Blocco su Samsung che fa cadere il debug wireless | Noto | Comportamento da preservare e testare con le finestre interne |
| A20 | Nessun avviso spurio alla prima lettura delle notifiche | Probabile problema | Verificare e correggere |

---

## 10. Piano per fasi

> Non esiste un piano approvato: questa è la **struttura proposta** per ordinare il lavoro. **Ogni fase termina con codice costruito, testato e documentato** (11.2) e con il committente che giudica il risultato.

### Fase 0 — Studio e misure (nessun codice di prodotto)

Obiettivo: chiudere A1, A2, A3, A4, A5, A6, A8, A12 con numeri.

- **Benchmark di composizione**: prototipo minimo (finestra + N finestre interne con flussi reali o registrati) per le strade di 4.3; misure con 1/3/5 flussi, regolarità frame, carico CPU/GPU, trascinamento e ridimensionamento.
- **Benchmark sul telefono**: 1/3/5 sessioni video in parallelo (bitrate, frame, temperatura, comportamento dell'encoder, istanze di `MediaCodec`), con il S23+.
- **Pausa/ripresa**: misura delle opzioni A e B.
- **Permessi del helper**: galleria, elenco di cartelle, cartelle protette, ricerca.
- **HiDPI**: prova su scale 100/125/150/200%.
- Output: `notes/` aggiornate, proposta di architettura con decisioni e misure; proposta su organizzazione del repository.

**Uscita**: documento di architettura approvato dal committente.

### Fase 1 — Shell minima

Finestra host (F-01), un solo flusso in una finestra interna, compositore scelto in Fase 0, input con traduzione coordinate (4.10), trascinamento con texture scalata e riconfigurazione a fine movimento (4.5), connessione e stati (3.3).

**Accettazione**: una app aperta, spostata, ridimensionata, senza scatti; input preciso dopo il ridimensionamento.

### Fase 2 — Multi-finestra, barra, launcher

Più finestre con z-order e focus, barra con app aperte (F-03), launcher a griglia (F-04), ciclo di vita (4.6), aggancio ai bordi, finestre solo-verticali, schermo protetto, connessione persa.

**Accettazione**: benchmark con 3 e 5 finestre entro gli obiettivi concordati in Fase 0.

### Fase 3 — Desktop

Sfondo in 3 modalità (F-02a), icone (F-02b) con trascinamento dal launcher e menu contestuali, widget (F-02c) dalla prima lista fattibile, persistenza (F-10).

### Fase 4 — Trasferimento file

Invio e ricezione (F-09), selettore di cartelle fino a `/`, ricerca, percorso digitabile, coda, avviso, trascinamento sulla finestra.

### Fase 5 — Area di sistema

Volume (F-08), notifiche (F-06), stato del telefono e schermo reale (F-07), blocco (4.8), pannello "in mano" integrato con la shell.

### Fase 6 — Universalità e rilascio

Profilo encoder per telefono, tabella dei telefoni testati, packaging AppImage, manuale tecnico e utente aggiornati, pagina del progetto.

---

## 11. Convenzioni di lavoro

### 11.1 Lingua e stile

- **Italiano** ovunque: nomi, commenti, messaggi del programma, README/SPECIFICATION/notes. I **due manuali** (tecnico e utente) sono in **inglese**.
- Parole semplici, frasi brevi. I commenti spiegano il **perché** e puntano alla fonte (per esempio `SPECIFICATION §7.3`, `notes/component.md`).
- Rust: `anyhow` per gli errori, con contesto che dice che cosa si stava facendo; **niente `unwrap`** dove un telefono diverso potrebbe rispondere altro.
- Java: una classe per pezzo; riflessione solo in `Nascoste` e nei pezzi che la usano; errori catturati nel thread che li produce.
- Codice di terzi: **mai copiato**. scrcpy e AOSP si leggono come documentazione.

### 11.2 Prima di ogni commit

Ogni commit lascia il progetto **costruito, testato e documentato**:

```
cargo build && cargo test && cargo clippy --all-targets
```

- Nessun avviso di clippy.
- Se hai cambiato il componente, il **jar ricostruito va nello stesso commit**.
- Se hai cambiato un comportamento, aggiorna il capitolo che lo descrive in `docs/sources/technical/` (e, se l'utente lo vede, in `docs/sources/user/`) e rigenera i manuali (`python3 docs/sources/build.py`).
- Se hai aggiunto un file sorgente, dagli una riga nella mappa (`ch17_map.py`): `cargo test` la richiede.

### 11.3 Dati personali

Il repository è **pubblico**: niente nomi di persone, numeri di serie, nomi di reti Wi-Fi, indirizzi, screenshot reali non sfocati. I test usano valori finti (per esempio `R5CT0000000`); i file prodotti dai test (`YASDE-prova.*`) sono ignorati da git.

### 11.4 Registrare le decisioni

Il perché del progetto vive in `notes/`, un file per argomento: `next-session.md` (da dove ripartire; si legge all'inizio di ogni sessione), `user-decisions.md` (decisioni e perché, incluse le alternative scartate), `issue-log.md` (problema → causa → soluzione → stato), `connection-tests.md` (misure numerate §), `component.md`, `adb.md`, `api-android.md`, `study/`, `interface.md`. **Prima di proporre un'alternativa, controlla che non sia già stata scartata.**

### 11.5 Licenza e distribuzione

MIT con la condizione «Commons Clause» (decisione del committente: chiunque può usare, modificare e ridistribuire YASDE, ma non venderlo; file `LICENSE`); sorgente pubblico (source-available); un solo AppImage; nessun file `.desktop` installato.

---

## 12. Appendici

### 12.1 Glossario essenziale

- **Shell**: il desktop emulato (finestra host + desktop + barra + launcher + widget + compositore).
- **Finestra host**: la finestra del PC che contiene tutto.
- **Finestra interna**: la finestra di un'app Android dentro la shell.
- **Display virtuale** (`VirtualDisplay`): schermo virtuale sul telefono con la sola app della finestra.
- **Specchio**: copia dello schermo reale del telefono (cap 1920 px per lato).
- **Servizio / componente / helper**: il jar Java sul telefono; servizio = processo di lunga durata, uno per connessione; helper = comandi brevi che stampano e terminano.
- **Guardiano** (*custode*): processo shell che ripristina il telefono quando il processo a cui è legato termina.
- **Telefono in mano**: stato in cui il pannello fisico non si spegne dopo che l'utente lo ha preso/sbloccato.
- **Manopole**: scala della shell, densità app, qualità del flusso (4.4).
- **Modalità interattiva / media**: priorità a latenza / a fluidità (4.12).

### 12.2 Checklist di tracciabilità funzioni → sezioni

| ID | Funzione | Sezione | Fase |
|---|---|---|---|
| F-01 | Finestra principale, schermo intero | 5.1 | 1 |
| F-02a | Sfondo (solido, sfumato, immagine dal telefono) | 5.2.1 | 3 |
| F-02b | Icone sul desktop (modello DeX) | 5.2.2 | 3 |
| F-02c | Widget della shell | 5.2.3 | 3 |
| F-02d | Menu contestuali | 5.2.4 | 3 |
| F-03 | Barra | 5.3 | 2 |
| F-04 | Launcher a griglia | 5.4 | 2 |
| F-05 | Finestre delle app | 5.5 | 1–2 |
| F-06 | Notifiche | 5.6 | 5 |
| F-07 | Stato del telefono, schermo reale | 5.7 / 4.7 | 5 |
| F-08 | Volume | 5.8 | 5 |
| F-09a | Invia al telefono | 5.9.1 | 4 |
| F-09b | Scarica dal telefono | 5.9.2 | 4 |
| F-09c | Selettore di cartelle fino a `/` | 5.9.4 | 4 |
| F-10 | Persistenza | 5.10 | 3–5 |
| F-11 | Lingua e accessibilità | 5.11 | tutte |

### 12.3 Domande da fare al committente prima di chiudere la Fase 0

1. Organizzazione del repository: nuovo repo, branch o cartella nuova?
2. Azione dopo un download completato: solo avviso, "Apri cartella", apertura del file?
3. Windows: dentro o fuori dal perimetro della prima versione?
4. Widget: confermare l'elenco della prima versione dopo la verifica delle sorgenti dati (A9).
5. Obiettivi numerici di prestazioni (8.1) dopo le prime misure.
6. Se ci siano telefoni di prova aggiuntivi (anche in prestito) per la tabella di universalità.
