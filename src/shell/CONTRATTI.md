# Contratti della shell (YASDE 2.3)

Questo documento, gli header di contratto e il proprio `COMPITO.md` bastano a chi scrive un pezzo della shell. Fonte di verità per funzioni, aspetto e misure: `yasde-specification.md` (capitoli 4, 5, 6.2, 8) e `mockup/project/Main.dc.html`. Niente funzioni oltre a quelle. Architettura decisa: `PIANO.md`, «Proposta di architettura della shell» (GTK4 in C, disegno Vulkan, video senza copie, un thread per flusso, un solo thread grafico).

## 1. Pezzi, cartelle, proprietà

| Pezzo (`strumenti/prova.sh …`) | Cartella | Che cosa |
|---|---|---|
| `shell-ossatura` | `src/shell/ossatura/` | applicazione GTK, finestra principale F-01, servizi veri, velo §4.8, host minimizzato §4.14, `base.css` |
| `shell-superficie` | `src/shell/superficie/` | widget che mostra un flusso (`superficie.h`) |
| `shell-finestra-app` | `src/shell/finestra-app/` | finestre interne F-05 |
| `shell-barra` | `src/shell/barra/` | barra F-03 |
| `shell-launcher` | `src/shell/launcher/` | launcher F-04 |
| `shell-desktop` | `src/shell/desktop/` | desktop F-02 |
| `shell-trasferimento` | `src/shell/trasferimento/` | trasferimento file F-09 |
| `shell-sistema` | `src/shell/sistema/` | notifiche e toast F-06, stato F-07, volume F-08 |
| `shell-connessione` | `src/shell/connessione/` | prima connessione: attesa del telefono, abbinamento con il codice (§15) |
| `shell-finto` | `src/shell/finto/` + `src/shell/comune/` | backend finto e pezzo comune (di chi coordina) |

Ogni pezzo tocca SOLO la sua cartella e il suo file di traduzioni `data/en/shell-<pezzo>.txt` (§7). Non si modificano: `src/shell/*.h`, `src/shell/comune/`, `src/shell/finto/`, `src/shell/meson.build`, `src/shell/CONTRATTI.md`, gli header degli altri pezzi del progetto (`src/*/*.h`), il `meson.build` di radice, `strumenti/`. Se un contratto è sbagliato o incompleto: scriverlo nel rapporto finale e proseguire sul resto (nel frattempo, nel proprio demo, un finto nella propria cartella).

Header di contratto (tutti in `src/shell/`):

- `servizi.h` — l'UNICA porta verso il telefono, l'audio, i trasferimenti e la persistenza. Nessun pezzo include `collegamento.h`, `dati.h`, `componente.h`, `decodifica.h`, `audio.h`, `trasferimenti.h`, `file.h`, `adb.h` (solo l'ossatura, nei servizi veri).
- `servizi_backend.h` — come si scrive una realizzazione dei servizi: lo includono SOLO `finto/` e `ossatura/`.
- `fotogramma.h` — il fotogramma neutro (DMA-BUF o memoria), C puro.
- `shell.h` — il contesto `PhShell`: consegna al thread GTK, strati, popup, avvisi della shell, gestori (funzioni che un pezzo serve agli altri), stile, icone del mockup, trascinamento delle app, accessibilità, registro delle decisioni.
- `superficie.h` — il widget del flusso (lo usa finestra-app).
- `pezzi.h` — i punti d'ingresso `ph_<pezzo>_installa(PhShell *)`.
- `finto/finto.h` — il backend finto e il programma dimostrativo (per demo e prove).

Si possono usare anche, perché sono pura logica già approvata: `config/config.h` (tipi della persistenza), `config/lingua.h` (`ph_t`), `finestre/finestre.h` (solo finestra-app), `comune/errore.h`, `comune/buffer.h`.

Regole fisse: **le strutture dei contratti (gestori, richieste, dati) si inizializzano SEMPRE con i nomi dei campi** (`{.io = x, .apri_app = f}`), mai per posizione: i contratti crescono aggiungendo campi in coda e un inizializzatore per posizione diventa un errore (`missing-field-initializers`) per tutti; nessun pezzo include l'header interno di un altro pezzo; GTK compare solo sotto `src/shell/`; FFmpeg solo in `src/video/` (nemmeno l'ossatura lo include: vedi il rapporto sull'esportazione dei fotogrammi).

## 2. Thread

1. **Un solo thread GTK.** Tutti i widget, `PhShell`, i servizi (`ph_srv_*`) e src/finestre si usano solo lì.
2. **Il thread GTK non blocca mai**: niente letture di file grandi, niente rete, niente `g_thread_join` di un thread che può restare fermo, niente attese di lucchetti tenuti a lungo. Le richieste al telefono passano dai servizi, che rispondono con una funzione di ritorno.
3. **Una sola via da un altro thread al thread GTK: `ph_shell_consegna(fn, dati, libera)`** (`shell.h`). È sempre asincrona (anche se chiamata dal thread GTK: `fn` gira a un giro successivo del ciclo, mai dentro la chiamata), rispetta l'ordine delle consegne dello stesso thread, e libera i dati dopo. Chi consegna un puntatore a un widget tiene un `g_object_ref` fino alla consegna. Chi manda molto (i fotogrammi) accorpa: al massimo una consegna in volo per flusso, vince l'ultimo dato. Vietati `g_idle_add`, `g_main_context_invoke` e simili sparsi nel codice dei pezzi: si usa `ph_shell_consegna`.
4. **Servizi**: ogni avviso e ogni funzione di ritorno arriva sul thread GTK (lo garantisce `src/shell/comune/servizi.c`). Un `GCancellable` annullato dal thread GTK garantisce che la funzione di ritorno non arrivi più. Le fotografie dello stato (puntatori restituiti da `ph_srv_app`, `ph_srv_notifiche`, …) valgono finché il thread GTK non torna al ciclo principale: ciò che serve dopo si copia.
5. **Flussi video**: un thread di lettura per flusso, della superficie, che chiama `ph_srv_flusso_leggi` (lì avviene la ricezione e la decodifica) e consegna i fotogrammi al thread GTK. La texture (`GdkDmabufTextureBuilder` / `gdk_memory_texture_new`) si costruisce sul thread GTK, come nel prototipo misurato. Chiusura: `ph_srv_flusso_interrompi` (qualunque thread), attesa della fine del thread di lettura (torna entro il timeout di lettura), poi `ph_srv_flusso_chiudi` dal thread GTK.
6. **Vita**: ogni pezzo libera le sue cose quando la radice della shell viene distrutta (ordine garantito in `shell.h`: la radice si stacca dal `PhShell` — `ph_shell_radice_viva` diventa falso —, poi nel suo dispose i figli degli strati vengono staccati PRIMA del segnale `destroy`: chi nel `destroy` tocca i propri widget ne tiene un riferimento proprio finché esistono i suoi ascolti; per sapere se la shell intera sta chiudendo si usa `ph_shell_radice_viva`, non i dati interni della radice). Le stringhe date ai servizi sono copiate prima del ritorno (regola 5 di `servizi.h`). Si usa il segnale `destroy` della radice o il dispose dei propri widget; una superficie chiude il suo flusso anche nel proprio dispose. I demo (`ph_finto_demo`) chiudono così, poi liberano i servizi.

## 3. Strati (dal basso in alto)

La radice (`ph_shell_radice`) contiene gli strati in quest'ordine; dentro lo stesso strato l'ultimo aggiunto sta sopra. Un figlio di uno strato riceve tutta l'area e si colloca con `halign`/`valign` e margini (come in un `GtkOverlay`); con allineamento diverso da `FILL` occupa solo la sua misura e il resto resta cliccabile per gli strati sotto. Per posizioni assolute (finestre, icone, menu contestuali) si usa `ph_shell_libero_nuovo()`, che è trasparente ai clic fuori dai suoi figli.

| Strato | z del mockup | Di chi | Contenuto |
|---|---|---|---|
| `PH_STRATO_DESKTOP` | auto | desktop | sfondo, icone (griglia 92×96 da 16,16), widget |
| `PH_STRATO_FINESTRE` | 11… | finestra-app | finestre interne; l'area utile esclude la barra (48 px) |
| `PH_STRATO_BARRA` | 4000 | barra | barra in basso, 48 px |
| `PH_STRATO_PANNELLI` | 4500–5100 | comune + launcher, sistema, trasferimento, desktop, finestra-app | velo-clic dei popup (comune), launcher, pannelli, popup, menu, dialoghi |
| `PH_STRATO_TOAST` | 5200 | sistema | avvisi a comparsa |
| `PH_STRATO_RILASCIO` | 5400 | trasferimento | riquadro «Rilascia per inviare al telefono» |
| `PH_STRATO_VELO` | 9000 | ossatura | velo di blocco / connessione |
| `PH_STRATO_TITOLO_HOST` | 9500 | ossatura | striscia di 6 px in schermo intero |

Il fantasma dell'icona trascinata (mockup z 6000) è l'icona di trascinamento di GTK (`gtk_drag_source_set_icon`): nessuno strato.

## 4. Popup e pannelli

- **Uno solo aperto alla volta** (mockup: ogni `toggle*` chiude gli altri; `closePopups`). Sono popup: launcher, pannelli notifiche / telefono / volume / invio / ricezione, menu contestuali (desktop, icona, widget, app del launcher), dialoghi «Sfondo del desktop» e «Aggiungi widget», menu «Altri comandi» (⋮) di una finestra; dal 23 nov anche il menu del clic destro su un'icona della barra (`PH_POPUP_MENU_BARRA`) e il calendario dell'orologio (`PH_POPUP_CALENDARIO`), con le stesse regole (uno solo aperto, clic fuori, Esc).
- Il pezzo crea il suo widget una volta, lo aggiunge nascosto a `PH_STRATO_PANNELLI` e lo apre con `ph_shell_popup_apri` / `ph_shell_popup_alterna` (i pulsanti della barra usano alterna). Aprirne uno chiude quello aperto (con la sua funzione di chiusura).
- **Clic fuori**: con un popup aperto un velo trasparente copre l'area sotto i popup, barra compresa (mockup: z 4500 sopra la barra a z 4000): il clic chiude il popup e NON passa sotto. Quindi con un popup aperto il primo clic su un altro pulsante della barra chiude soltanto (comportamento del mockup).
- I toast (5200) stanno sopra il velo dei popup: restano cliccabili.
- Il velo di blocco chiude il popup aperto (mockup `toggleLock`).
- `PH_SHELL_EV_POPUP` avvisa quando cambia il popup aperto (la barra aggiorna `aria-expanded`).

## 5. Tastiera

- **Esc**: il mockup non lo gestisce. Il contratto (proposta da confermare, vedi rapporto): con un popup aperto Esc lo chiude (lo fa il pezzo comune, in cattura sulla radice); altrimenti va alla finestra interna attiva, che con la preferenza `esc_indietro` (predefinita vera, specifica 5.10) lo traduce in «Indietro» del telefono, altrimenti lo manda come tasto.
- Il focus della tastiera è della finestra interna attiva (4.10); i tasti riservati alla shell non vanno al telefono: scorciatoie della finestra (Ctrl+R Ruota, Ctrl+W Chiudi app, Ctrl+Maiusc+C Copia screenshot, 5.5) e lo schermo intero (tasto da decidere: vedi rapporto). Nei demo F5–F10 sono dei comandi del telefono finto.

## 6. Stile

- **Un solo CSS di base**: `src/shell/ossatura/base.css`, di proprietà dell'ossatura (solo lei lo modifica), compilato nelle risorse comuni e caricato da `ph_shell_nuova`. Contiene le variabili `--ph-*` con i colori del mockup e le classi comuni: `ph-pannello`, `ph-dialogo`, `ph-menu`, `ph-tb` (pulsanti della barra), `ph-riga` (.lrow), `ph-pbtn`, `ph-ghost` (+ `ph-bordato`), `ph-accento`, `ph-pillola` (+ `ph-acceso`), `ph-separatore`, `ph-campo` (+ `ph-ricerca`), `ph-voce`, `ph-sezione`, `ph-titolo-pannello`, `ph-titolo-dialogo`, `ph-testo-2`, `ph-testo-3`, `ph-piccolo`, `ph-minimo`, `ph-avanzamento`.
- **Vetro (2.1, fase 5)**: due classi di superficie in `base.css`, `ph-vetro-chiaro` (drawer, pannelli, menu, dialoghi, avvisi, widget, scheda di connessione: velo `rgba(255,255,255,0.58)` con la sfocatura dietro, `rgba(233,238,241,0.94)` senza; bordo e filo di luce come `inset box-shadow`, ombra lunga) e `ph-vetro-scuro` (la barra: `rgba(8,24,40,0.42)` con la sfocatura, `--ph-barra` 0,95 senza). Si mettono SOLO con `ph_shell_vetro_registra` (mai a mano: la sfocatura la disegna il C e la radice prende `ph-vetro-attivo`). Vale la superficie PIÙ VICINA: le variabili si ereditano, quindi una scura dentro una chiara rimette i valori scuri e viceversa. `ph-pannello`, `ph-dialogo`, `ph-menu` restano classi di forma (raggio, imbottitura); il loro fondo scuro vale solo finché il widget non è registrato come vetro.
- **Variabili che cambiano dentro `ph-vetro-chiaro`** (scuro → chiaro): `--ph-testo` `#e6eaf2` → `#10202E`; `--ph-testo-2` → `#2F4558`; `--ph-testo-3` → `#3A4D5F`; `--ph-accento` `#2dd4bf` → `#1F7A8C` (solo come FONDO); `--ph-su-accento` `#0b1220` → `#ffffff`; `--ph-bordo`, `--ph-bordo-dialogo` → `rgba(16,32,46,0.12)`; `--ph-bordo-forte` → `rgba(16,32,46,0.2)` (bordi interni scuri: i bianchi non si vedono su un fondo già chiaro); `--ph-separatore` → `rgba(16,32,46,0.14)`; `--ph-sopra`, `--ph-sopra-riga`, `--ph-sopra-ghost` → bianco 0,75; `--ph-voce` → bianco 0,45; `--ph-campo` → bianco 0,7; `--ph-pillola-spenta` → `rgba(16,32,46,0.2)`; `--ph-chiudi-sopra` → `#E8604C`; `--ph-rec` → `#C0392B` (solo per l'indicatore REC); `--ph-ombra-pannello/-dialogo/-menu` → `--ph-vetro-ombra`; `--ph-pannello`, `--ph-launcher`, `--ph-dialogo`, `--ph-widget` → `rgba(233,238,241,0.94)`. Variabili NUOVE (scuro / chiaro): `--ph-fuoco` (fuoco da tastiera, `#ffffff` / `#10202E`), `--ph-testo-campo` (testo nei campi, `#ffffff` / `#10202E`), `--ph-bordo-ghost` (bordo di `ph-ghost.ph-bordato`, bianco 0,2 / `rgba(16,32,46,0.2)`), `--ph-accento-spento` (fondo di `ph-accento` disabilitato, bianco 0,25 / `rgba(16,32,46,0.12)`), `--ph-su-accento-spento` (testo sopra quel fondo, `#0b1220` / `#3A4D5F`), `--ph-avanzamento-fondo` (binario di `ph-avanzamento` e dei cursori, bianco 0,16 / `rgba(16,32,46,0.14)`), `--ph-errore` (testo d'errore, `#fca5a5` / `#8B1A12`), `--ph-avviso` (testo di avviso, `#fcd34d` / `#5F3D00`), `--ph-accento-testo` (accento come TESTO o collegamento, `#2dd4bf` / `#0E4F5C`). Solo nella radice/scuro: `--ph-vetro-scuro-luce` (filo di luce in alto, bianco 0,1). Sulla superficie scura (`ph-vetro-scuro`) la regola definisce anche `--ph-barra-sfuma` (colore pieno del fondo della barra: `rgba(15,23,32,0.95)` senza sfocatura, `rgba(8,24,40,0.42)` con `ph-vetro-attivo`) e `--ph-barra-sfuma-0` (lo stesso con alfa 0), per i gradienti laterali del pezzo `barra`. Restano uguali (sempre scure o assolute): `--ph-bianco` (bianco vero, usato solo dal velo di blocco), `--ph-barra`, `--ph-titolo-*`, `--ph-fondo-host`, `--ph-velo`, `--ph-ombra-finestra`, tutte le `--ph-vetro-*`.
- **Regole comuni dentro le superfici di vetro** (`base.css`): `scale trough` (binario `--ph-avanzamento-fondo`), `scale highlight` e `scale slider` (`--ph-accento`), `checkbutton check:checked` e `switch:checked` (`--ph-accento`, segno `--ph-su-accento`), selezione del testo (`--ph-accento` / `--ph-su-accento`) e `placeholder` (`--ph-testo-3`). Il cursore del volume (`sistema.css`) ha già regole sue più specifiche per `highlight` e `slider`.
- **REGOLA PER I PEZZI**: (a) nei CSS dei pezzi nessun colore di contenuto scritto a mano (testo, bordi, hover, fondi di voci e campi, accento): solo variabili `--ph-*`, perché lo stesso widget può stare su vetro chiaro o scuro. Eccezione: un colore che è un DATO (colore di un'icona, di una categoria, di un'app) si lascia con un commento `/* colore-dato: motivo */` sulla STESSA riga; la prova del pezzo lo controlla con il rilevatore comune `src/shell/prova_css.h`, che segnala ogni colore scritto a mano senza quel commento. Se serve un colore che non c'è, chiedilo all'ossatura. (b) `ph-pannello`, `ph-dialogo`, `ph-menu` sono classi di FORMA: raggio e imbottitura; l'ombra e il bordo esterno li dà il vetro (il loro `border: 1px` diventa trasparente, la misura non cambia). (c) Su un widget registrato come vetro il pezzo NON imposta `background`, `border` (se non trasparente) né `box-shadow`. (d) Accento e rosso NON si usano come colore di testo: per il testo `--ph-accento-testo`, `--ph-errore`, `--ph-avviso` (`--ph-accento` è solo per fondi, `--ph-rec` solo per l'indicatore REC). (e) Limite noto: con il velo al 58 % sopra uno sfondo molto scuro o molto chiaro i tre livelli di testo si avvicinano (sul grigio medio `#949494` solo `--ph-testo` resta a contrasto pieno, ~5,5:1; gli altri ~3:1): le informazioni importanti usano `--ph-testo`.
- **Ogni pezzo ha il suo CSS** `<pezzo>.css`, compilato come risorsa `/it/yasde/shell/<pezzo>.css` e caricato nel proprio `_installa` con `ph_shell_stile_carica` (così il demo del pezzo da solo ha il suo stile; l'ossatura non deve caricarlo). Solo classi con prefisso `ph-<pezzo>-`; colori e misure dal mockup, usando le variabili `--ph-*` dove esistono. La prova di logica controlla che il CSS si legga senza errori.
- CSS di GTK, non del web: niente `gap`, `max-width`, `position`; le misure in px del mockup si danno con `min-width`/`min-height`, margini, `gtk_widget_set_size_request` o la disposizione nel codice.
- Icone: solo quelle del mockup, con `ph_shell_icona_nuova(&PH_ICONA_…)` (tracciati SVG del mockup disegnati nel colore del testo). Segnaposto con le iniziali al posto di un'icona mancante: `ph_shell_segnaposto_nuovo`.
- Tema: la shell disegna il proprio tema (5.11): dalla 2.1 barra di vetro scuro e drawer, pannelli, menu, dialoghi, avvisi e widget di vetro chiaro (vedi «Vetro» sopra); restano scuri il velo di blocco e il riquadro «Rilascia per inviare». La finestra host e i dialoghi del sistema seguono il tema del sistema.

## 7. Testi e lingua

- Ogni testo per l'utente si scrive in italiano con `ph_t("…")` (`config/lingua.h`): argomento sempre letterale; per le tabelle `PH_T_("…")` e poi `ph_tv(tabella[i])`. Segnaposto `{nome}` con `ph_lingua_componi`, mai printf su un testo tradotto.
- Traduzioni inglesi in `data/en/`. **Tutti i testi del mockup sono già tradotti in `data/en/shell-comune.txt`** (di chi coordina): usarli identici. Un testo nuovo va nel file del proprio pezzo `data/en/shell-<pezzo>.txt` (da creare), **dopo aver controllato con grep che non esista già in nessun file di `data/en/`**: una chiave doppia tra due file ferma la compilazione di tutti. Un testo senza traduzione fa fallire il test `lingua` di `tutto` anche se il pezzo non è ancora approvato (la scansione legge tutto `src/`): stringa e traduzione si aggiungono insieme.
- La scansione legge anche i commenti dei `.c`/`.h`: non scrivere chiamate a ph_t di esempio nei commenti.
- I testi del telefono (nomi delle app, notifiche, frasi d'errore dei servizi) arrivano già pronti e non si traducono.

## 8. Accessibilità (5.11)

- Tutto ciò che è cliccabile è un vero controllo (`GtkButton`, `GtkToggleButton`, `GtkEntry`, `GtkScale`, …), raggiungibile con Tab, con il fuoco visibile (2 px bianco, in `base.css`).
- Etichetta accessibile = `aria-label` del mockup, suggerimento = `title` del mockup: `ph_shell_etichetta(w, etichetta, suggerimento)`. `aria-expanded` → `ph_shell_espanso`, `aria-pressed` → `ph_shell_premuto`, `role="dialog"` → `GTK_ACCESSIBLE_ROLE_DIALOG`, gruppi (`role="group"`) → `GTK_ACCESSIBLE_ROLE_GROUP` con etichetta.
- Contrasto: i colori del mockup (testo #e6eaf2/#cbd5e1/#9aa4b5 su fondi scuri) già vanno bene; non schiarire i fondi.

## 9. Registro delle decisioni automatiche (4.12, 4.14)

Ogni decisione presa dal programma da solo si scrive con `ph_shell_decisione(che_cosa, causa, …)`: per esempio «flussi in pausa» / «finestra host minimizzata», «flusso in memoria invece che DMA-BUF» / «GTK ha rifiutato il DMA-BUF (fourcc …)», «fotogramma scartato» con il numero che l'ha causato. Una riga su stderr con l'ora; mai dati personali (seriali, indirizzi, nomi di file dell'utente). Le cose che chiede l'utente non sono decisioni automatiche.

## 10. Persistenza (5.10)

`ph_srv_profilo()` (file del telefono, `PhCfgTelefono`) e `ph_srv_preferenze()` (`PhCfgPreferenze`) si modificano sul thread GTK, poi `ph_srv_profilo_salva` / `ph_srv_preferenze_salva` (salvataggio differito, su un thread del backend). Al collegamento di un telefono diverso arriva `PH_SRV_EV_PROFILO`: rileggere. Chi scrive quali campi:

| Campo | Pezzo |
|---|---|
| `sfondo`, `icone`, `widget` | desktop |
| `volume` (il muto non si salva) | sistema (l'ossatura lo applica all'audio al caricamento del profilo) |
| `cartella_invio`, `cartella_ricezione` | trasferimento |
| `densita_app`, `finestre` («riapri dove eri») | finestra-app |
| preferenze `lingua`, `scala_automatica`, `scala` | ossatura (lettura) |
| preferenze `esc_indietro`, `riapri_dove_eri` | finestra-app (lettura) |
| preferenze `widget_nascosti` | desktop (lettura) |
| preferenze `avvisi`, `cartella_ricevuti` | sistema / trasferimento (lettura) |

Nessun pezzo ha un'interfaccia per cambiare le preferenze: non c'è nel mockup.

## 11. Chi chiama chi (gestori di `shell.h`)

| Gestore (chi lo serve) | Chiamate | Chi chiama |
|---|---|---|
| finestre (finestra-app) | `ph_shell_apri_app`, `ph_shell_finestre`, `ph_shell_clic_barra`, `ph_shell_specchio`, `ph_shell_specchio_aperto` | launcher, desktop, barra, sistema (toast, notifiche, popup telefono) |
| desktop | `ph_shell_sul_desktop`, `ph_shell_aggiungi_al_desktop`, `ph_shell_rimuovi_dal_desktop` | launcher (menu contestuale) |
| avvisi (sistema) | `ph_shell_avviso(titolo, testo)` | tutti (toast di informazione del mockup: «Inviato al telefono», «Screenshot salvato», «Widget aggiunto», «Cartella non trovata», …) |
| launcher | `ph_shell_launcher_alterna` | barra |
| trasferimento | `ph_shell_trasferimento_apri(verso)` | barra |
| finestre: chiusura (23 nov) | `ph_shell_chiudi_finestra(id)` (come il pulsante Chiudi: app tolta dai recenti) | barra (menu del clic destro) |
| fissate (barra, 23 nov) | `ph_shell_app_fissata(pacchetto)`, `ph_shell_fissa_app(pacchetto, fissa)`; l'elenco è nel profilo del telefono e lo tocca solo la barra, che emette `PH_SHELL_EV_FISSATE` | launcher (menu contestuale), barra |
| sistema | `ph_shell_sistema_pulsante(tipo)` (i pulsanti volume, stato del telefono, notifiche: li crea e aggiorna sistema, li colloca la barra), `ph_shell_notifica_nascosta(chiave, ora)` | barra, desktop (widget «Notifiche recenti») |

Avvisi della shell (`ph_shell_ascolta`): `PH_SHELL_EV_FINESTRE` (lo emette finestra-app a ogni cambio di finestre/attiva/minimizzata), `PH_SHELL_EV_DESKTOP` (desktop, a ogni cambio delle icone), `PH_SHELL_EV_NASCOSTE` (sistema, quando cambiano le notifiche nascoste), `PH_SHELL_EV_POPUP` (comune), `PH_SHELL_EV_HOST` (ossatura con `ph_shell_imposta_host`), `PH_SHELL_EV_AREA` (comune, misura della radice).

Trascinamento di un'app (launcher → desktop, desktop → desktop): contenuto `PH_TIPO_APP_TRASCINATA` (`ph_shell_app_trascinata_contenuto`); il desktop ha il `GtkDropTarget`. Il trascinamento di file dal PC sulla finestra è del pezzo trasferimento (`GtkDropTarget` di `GDK_TYPE_FILE_LIST` sulla radice).

Ordine di installazione (pezzi.h): desktop, finestra-app, sistema, launcher, trasferimento, barra — la barra per ultima perché colloca i pulsanti che le danno gli altri.

Senza gestore registrato (demo di un pezzo da solo) le chiamate non fanno nulla e lo scrivono su stderr. Nel proprio demo un pezzo può registrare gestori finti, scritti nella sua cartella.

## 12. Compilare, provare, guardare

- `strumenti/prova.sh shell-<pezzo>` (sul server): compila con i controlli di memoria, esegue le prove della suite `shell`, poi ricompila lo stesso pezzo con `-O3` senza controlli (gli avvisi dell'ottimizzazione, come `veloce`). Ultima riga `uscita N`. Con `-Dpezzo=shell-<pezzo>` ci sono anche `comune`, `config`, `finestre`, il pezzo comune della shell e il finto (l'ossatura anche tutti i pezzi sotto); gli altri pezzi della shell NO.
- Ogni pezzo ha: una **prova di logica** senza schermo (`prova_<pezzo>.c`, con il controllo delle perdite di memoria), e una **prova con schermo** = il demo con `--prova 2000`, che gira se nel container c'è `gtk4-broadwayd` (oggi no: risulta «saltata», exit 77) con `G_DEBUG=fatal-criticals`. La logica va separata dai widget (funzioni pure nella propria cartella) proprio per poterla provare senza schermo.
- **Demo**: `demo-<pezzo>`, il pezzo da solo sopra il telefono finto (`ph_finto_demo`). Sul tablet: `strumenti/demo-sul-tablet.sh <pezzo>` (prende la versione `-O3` dal server; apre anche la finestrella dei comandi del telefono finto). Il telefono vero non serve.
- `demo-ossatura` è la shell intera con i pezzi presenti: con `pezzo=tutto` quelli approvati (`shell_pronti` nel `meson.build` di radice, li aggiunge chi coordina dopo l'approvazione).

## 13. Dati dal telefono e dati personali

- I servizi consegnano testi già sicuri da mostrare (UTF-8 valido, senza caratteri di controllo) e immagini già decodificate con un tetto alla misura. I campi `percorso`/`nome` grezzi servono a navigare e a richiedere: si mostrano solo le versioni `_schermo`.
- Ogni indice in un array che arriva dai servizi si controlla contro la lunghezza data; nessun buffer di misura fissa riempito senza limite (`g_strlcpy`, `snprintf`).
- Mai seriali, indirizzi, nomi di reti o di persone nei file, nei log o nei test (valori finti: quelli del telefono finto).

## 14. Che cosa non fare

- Niente funzioni che non siano nella specifica o nel mockup (i pulsanti «Simula…» e «Prova con file di esempio» del mockup NON si fanno: nel prodotto lo stato è reale).
- Niente `GtkStyleContext` per widget, niente CSS in linea nel codice (tranne i colori calcolati: sfondo scelto dall'utente, colore dei segnaposto).
- Niente thread che toccano widget; niente lavoro pesante nel thread GTK.
- Niente `git commit`.

## Decisioni di chi coordina (23 nov 2026) — prevalgono sui COMPITO.md dove differiscono

- **Esc**: chiude il popup o il pannello aperto; se non ce n'è, va al telefono come «Indietro» secondo la preferenza `esc_indietro`. **F11** = schermo intero (oltre al pulsante nel titolo).
- **Barra**: le finestre aperte (specifica 5.3) più le app fissate dall'utente (aggiunta del 23 nov: vedi la tabella «fissate» sopra, `ph_barra_ordina`).
- **Widget del catalogo iniziale** (deciso dal committente): Orologio, Stato del telefono, Notifiche recenti, Memoria, Calendario. Note rapide e In riproduzione non compaiono nel catalogo; il Meteo e altri widget sono stati aggiunti dopo (l'elenco vero è `PH_DSK_CATALOGO` in `src/shell/desktop/widget.c`). I dati di Memoria (con suddivisione: immagini, video, audio, app) e Calendario esistono ora in `src/dati/dati.h` (`ph_dati_memoria_dettaglio`, `ph_dati_calendario`); se `servizi.h` non li espone ancora, chi scrive `desktop` lo scrive nel rapporto e usa i dati del finto. Il Calendario si chiede al telefono SOLO se il widget è sul desktop; i titoli degli eventi non si salvano su disco e non finiscono in nessun log. Eventi «tutto il giorno»: inizio e fine sono a mezzanotte UTC, si mostrano come giorni senza conversione di fuso.
- **Trasferimenti**: annullamento per singolo file con una piccola «×» sulla voce, e «annulla tutto». Dopo un trasferimento: solo l'avviso. Un `.apk` trascinato NON si invia come file: chiede conferma e si installa con `src/trasferimenti/installa.h` (su un thread di lavoro; la conferma della disinstallazione deve dire che si perdono anche i dati dell'app).
- **Screenshot e registrazione**: contratto in `src/video/cattura.h` (in completamento). Finestra-app prepara pulsanti, stato e indicatore REC; l'aggancio a `cattura.h` lo fa quando il contratto è approvato.
- **Pausa**: minimizzare una finestra mette il flusso in pausa (`ph_video_pausa`/`ph_video_riprendi` in `src/componente/componente.h`); prima della pausa si chiude ogni tocco in corso; in pausa niente input, niente ridimensionamento (prima si riprende).
- **Immagine mai deformata a riposo**: durante trascinamento/ridimensionamento la texture si scala; a movimento assestato si chiede la nuova misura al telefono; tra una misura e l'altra l'immagine mantiene le PROPORZIONI (bande ai lati, mai stirata).
- **Schermo del telefono spento dal sistema o tasto di accensione premuto**: i flussi si fermano; la shell lo dice con il velo, come per il blocco.
- **«Ingrandisci» sulle app solo verticali**: colonna a forma di telefono al centro (specifica 4.5).
- **Testi non presenti nel mockup** (velo, schermo protetto, stati dei trasferimenti): quelli proposti in `data/en/shell-comune.txt` valgono finché il committente non li cambia.
- **Guardare il proprio pezzo**: `strumenti/foto-demo.sh <pezzo> ["secondi delle foto"] [-- opzioni del demo]` apre il demo in uno schermo INVISIBILE sul tablet (non disturba l'utente) e salva le fotografie in `~/.cache/yasde-sviluppo/foto/<pezzo>-<n>.png`, da aprire e GUARDARE (prima va lanciato `strumenti/prova.sh shell-<pezzo>`). È obbligatorio prima di consegnare: confronta la foto con il mockup (misure, colori, ordine, testi) e correggi ciò che non torna; nel rapporto dì che cosa hai visto. Lo schermo invisibile non ha mouse né tastiera: per mostrare pannelli, menu, stati diversi dai al demo opzioni che li aprono all'avvio o una sequenza automatica a tempo (per esempio `--mostra pannello`), e fotografa agli istanti giusti. Anche l'uscita del demo va letta (`foto/<pezzo>.log`): nessun avviso critico di GTK. Le prove automatiche con schermo nel container restano «saltate» (manca `gtk4-broadwayd`). Non aprire finestre sullo schermo vero dell'utente.
- **Regole comuni**: `CLAUDE.md`, compresa la sezione «Difetti già visti: non ripeterli». Il telefono non si usa.

## 15. Prima connessione (pezzo `shell-connessione`, 23 nov)

- **Che cos'è**: la schermata al posto del desktop quando non c'è ancora un telefono collegato in questa esecuzione o quando il telefono non riconosce il PC (serve il codice di abbinamento). Contratto: `src/shell/connessione/connessione.h` (`ph_connessione_nuova`), più le aggiunte in fondo a `servizi.h` (`ph_srv_abbina`, `ph_srv_codice_valido`, `ph_srv_abbinamento_in_corso`, campi `telefoni_trovati`, `scelta_arbitraria`, `telefoni_noti` in coda a `PhSrvCollegamento`) e in `shell.h` (`ph_shell_connessione_da_mostrare`).
- **Chi decide quando**: l'ossatura, con la regola unica `ph_shell_connessione_da_mostrare(stato, collegato_in_sessione)`: NON_ABBINATO sempre; CERCO e PERSO solo se in questa esecuzione non c'è mai stato un collegamento (dopo, vale il velo di §4.8); mai in COLLEGATO, BLOCCATO, CHIUSO. La schermata sta in `PH_STRATO_VELO`, al posto del velo (mai entrambi); copre anche la barra; mostrarla chiude il popup aperto.
- **Da dove arrivano i dati**: solo da `ph_srv_collegamento` (stato, motivo già tradotto, telefoni trovati/noti) e dall'esito di `ph_srv_abbina`. Nessuna lettura di `telefoni.toml`, nessuna scoperta propria: le fa il collegamento sotto i servizi. I nomi delle istanze mDNS (contengono il numero di serie) non arrivano mai alla shell.
- **Comandi**: «Cerca di nuovo» = `ph_srv_riconnetti_ora` (in CERCO ripete la ricerca, in NON_ABBINATO riprova senza abbinare); «Abbina» = `ph_srv_abbina(codice, NULL, 0, …)`; con indirizzo e porta scritti dall'utente `ph_srv_abbina(codice, host, porta, …)`.
- **Riservatezza**: codice di abbinamento, indirizzi, porte, nomi di reti e numeri di serie MAI su disco né in nessun log (nemmeno nei messaggi d'errore); il campo del codice si svuota dopo ogni tentativo; nei test valori finti (codici del telefono finto: 123456 giusto, 000000 schermata del codice non aperta, altro sbagliato).
- **Aspetto**: il mockup non ha questa schermata; finché il committente non decide valgono la carta dei dialoghi del mockup (`ph-dialogo`) sullo sfondo predefinito e i testi proposti in `connessione/COMPITO.md`.

## Ordine di distruzione e ascolti dopo la liberazione (24 nov 2026, dopo il SIGSEGV alla chiusura)
Causa vista con gdb: la chiusura con il pulsante della finestra avviene dentro un segnale (`close-request`); la finestra resta viva fino alla fine dell'emissione, quindi la radice della shell moriva («destroy») in un giro SUCCESSIVO del ciclo principale, quando `PhShell` e servizi erano gia' liberati, e i `radice_distrutta` dei pezzi chiamavano `ph_srv_smetti`/`ph_shell_smetti` su memoria liberata.
Regole:
- `ph_shell_libera` distrugge la radice da se' (la toglie dalla finestra, poi dispose): quando torna il «destroy» della radice e' GIA' avvenuto, una volta sola, con `PhShell` e servizi ancora vivi. Poi si liberano i servizi (`ph_servizi_libera`). Mai il contrario.
- Seconda difesa: `ph_srv_smetti` e `ph_shell_smetti` consultano un registro degli oggetti vivi (`comune/ascoltatori.c`): dopo la liberazione non toccano nulla. Resta comunque una regola per i pezzi: nel «destroy» della radice smettere di ascoltare con TUTTE le funzioni `*_smetti` (mai accedere a un `PhShell`/`PhServizi` per altra via), e tenere un riferimento proprio ai widget finche' esistono gli ascolti.

## 16. Widget nuovi (23 nov pomeriggio, PIANO.md «Richieste … pomeriggio»)

Sette widget, disegnati dal pezzo `desktop` come gli altri (stesso aspetto, tre misure). Da dove arrivano i dati:

| Widget | Dati | Contratto |
|---|---|---|
| Calendario del mese | telefono, 0x7b CALENDARIO_INTERVALLO | `ph_srv_tel_calendario_intervallo` |
| Prossima sveglia | telefono, 0x7c SVEGLIA | `ph_srv_tel_sveglia` |
| Batteria in dettaglio | telefono, 0x7d BATTERIA | `ph_srv_tel_batteria` |
| Interruttori rapidi | telefono, 0x80–0x83, evento 0x8e | `ph_srv_controlli`, `ph_srv_controlli_osserva`, `ph_srv_controllo_imposta`, `PH_SRV_EV_CONTROLLI` |
| Trova il telefono | NON offerto in questa versione (decisione del committente; protocollo §2.10): il widget non entra nel catalogo; `ph_srv_trova_*` restano nel contratto ma il telefono risponde sempre «non disponibile» | — |
| App frequenti | solo PC: aperture contate da finestra-app nel profilo del telefono (campo nuovo di config) | `ph_srv_profilo` |
| Trasferimenti recenti | solo PC: ultimi completati della sessione, tenuti dal pezzo comune | `ph_srv_trasferimenti_recenti` |

Regole:
- **Riservatezza**: titoli degli eventi, nomi e percorsi di file, nomi delle app aperte: mai nei log; titoli e nomi di file mai su disco (i recenti stanno solo in memoria; il conteggio delle aperture salva solo pacchetti e numeri). Il calendario si chiede solo se un widget Calendario è sul desktop.
- **Modifiche al telefono** (protocollo §2.10): suoneria e non disturbare cambiati dal widget sono comandi espliciti dell'utente con effetto permanente (nessun ripristino); la torcia accesa da YASDE si spegne alla chiusura; «trova il telefono» non è disponibile in questa versione.
- **Non disponibile**: ogni comando che il telefono non permette arriva come «non disponibile» (`puo_*` falso o valore `PH_SRV_NON_DISPONIBILE`): il widget lo dice e il controllo resta inattivo; mai un valore inventato.
- **Frequenza**: i dati del telefono si chiedono all'aggiunta del widget, al collegamento e poi al più: calendario ogni 15 minuti e a ogni cambio di mese mostrato, sveglia ogni 5 minuti, batteria ogni minuto; gli interruttori arrivano da soli (osservati) mentre il widget c'è.

### Aggiunta al §16 (23 nov 2026): giorni degli eventi nel calendario del mese
Regole per chi disegna il widget (misurato: il telefono vero e il finto si comportano allo stesso modo).
- **Richiesta.** Il mese si chiede da mezzanotte LOCALE del primo giorno a mezzanotte locale del primo giorno del mese dopo (in millisecondi; mai più di 62 giorni).
- **Eventi «tutto il giorno».** Inizio e fine sono a mezzanotte UTC: i giorni si ricavano leggendo inizio e fine in UTC (`g_date_time_new_from_unix_utc`), MAI con l'ora locale. L'evento occupa i giorni da quello di inizio incluso a quello di fine ESCLUSO (domenica 00:00 UTC → lunedì 00:00 UTC = solo la domenica). Ai bordi del mese può arrivarne qualcuno in più: se nessuno dei suoi giorni cade nel mese mostrato, si scarta.
- **Eventi con orario.** Si convertono nel fuso locale. Un evento che finisce esattamente a mezzanotte locale non conta nel giorno dopo.

## 17. Ctrl + rotella = zoom (pizzico, 23 nov)

Decisione del committente: Ctrl + rotella su/giù fa zoom avanti/indietro nelle app (mappe, foto, documenti), come un pizzico a due dita nel punto del puntatore. Protocollo: 0x55 PIZZICO (§2.4 di `docs/protocollo.md`): il PC manda PASSI (centro nel video, passo in 120esimi come la rotella, fasi inizio/continua/fine/annulla), il TELEFONO genera le due dita e chiude sempre il gesto (anche da solo dopo 400 ms senza passi).

| Chi | Che cosa |
|---|---|
| finestra-app | riconosce Ctrl + rotella (anche lo scorrimento del touchpad con Ctrl, e il gesto di pizzico del touchpad con `GtkGestureZoom`) sul contenuto di una finestra; in quel caso NON manda la rotella normale; traduce il punto come per TOCCO; apre il gesto al primo passo, manda un passo per evento, lo chiude (FINE) 250 ms dopo l'ultimo passo, al rilascio di Ctrl, all'uscita del puntatore dalla finestra, alla perdita del fuoco e prima di qualunque TOCCO |
| superficie | `ph_superficie_pizzico` inoltra a `ph_srv_flusso_pizzico` (niente in pausa) |
| servizi / backend | `ph_srv_flusso_pizzico` controlla e passa a `flusso_pizzico` (voce facoltativa) |
| ossatura (`sv_flussi.c`) | `flusso_pizzico` → `ph_input_pizzico` sul thread dell'input |
| componente | `ph_input_pizzico` codifica 0x55 |
| telefono-video | genera le due dita (`Iniettore`) |

Verso: Ctrl + rotella su (allontana) = ingrandisci = passo positivo; uno scatto = 120; touchpad: proporzionale.

## 18. Meteo (23 nov sera) — CAMBIO DI COMPORTAMENTO: il primo collegamento a Internet

Fino a oggi YASDE non si collega MAI a Internet (solo al telefono, in rete locale). Il widget Meteo introduce l'unica eccezione: richieste HTTPS a Open-Meteo (`api.open-meteo.com/v1/forecast`, gratuito, senza chiave, uso non commerciale, dati CC BY 4.0), fatte dal pezzo `src/meteo/` (contratto `meteo.h`). Regole:
- **Nessun consenso a parte** (decisione del committente del 23 nov notte): aggiungere un widget Meteo basta. Nessun dialogo, nessuna preferenza di consenso. Finché nessun widget Meteo è sul desktop non parte NESSUN collegamento (né previsioni né ricerca dei luoghi). Il comportamento (che cosa esce, verso chi, quando) va descritto nel MANUALE dell'utente.
- **Quando**: solo mentre almeno un widget Meteo è sul desktop (`ph_srv_meteo_osserva`); al massimo una richiesta ogni 30 minuti per la stessa posizione, cache solo in memoria, dopo un fallimento nuovi tentativi a 5, 15, 30 minuti; mai raffiche.
- **Che cosa esce**: solo latitudine e longitudine arrotondate a 0,005° (circa 500 m, decisione del committente del 23 nov notte: le arrotonda il TELEFONO prima di tutto, la posizione precisa non lo lascia mai; sul filo e in `PhDatiPosizione`/`ph_meteo_chiedi` in MILLESIMI di grado, multipli di 5; la città scelta a mano resta in centesimi in `PhMeteoLuogo` e nel profilo e si passa a `ph_meteo_chiedi` × 10) e i parametri fissi della richiesta. Nessun identificativo, User-Agent generico.
- **Riservatezza**: la posizione del telefono e il suo nome di località mai su disco né nei log, né sul telefono né sul PC; la shell non riceve mai le coordinate del telefono (solo le previsioni e, se il telefono lo sa, il nome della località). Unica eccezione su disco: la città scelta A MANO dall'utente (nome, dettaglio e coordinate arrotondate a 2 decimali) nel profilo, perché è una sua scelta; mai nei log nemmeno quella, né il testo cercato.
- **Attribuzione** obbligatoria: «Dati meteo di Open-Meteo.com» con il collegamento a `https://open-meteo.com/`, sempre visibile nel widget, in ogni misura e stile.
- **Non disponibile**: servizio che non risponde → il widget dice «non disponibile» con l'ora dell'ultimo dato (se c'è); posizione del telefono spenta o negata → lo dice.
- Catena: desktop → `ph_srv_meteo_osserva` → backend (ossatura: `ph_dati_posizione` 0x90, poi `ph_meteo_chiedi`) → `ph_servizi_pubblica_meteo_fonte` → `PH_SRV_EV_METEO` → desktop.
- **Città scritta a mano** (23 nov notte): dal menu del clic destro del widget, voce «Località…»: un dialogo con un campo di testo, il pulsante «Cerca» e l'elenco dei risultati (nome + regione, paese) da cui scegliere; in più «Nessuna» per togliere la città scelta. La ricerca parte SOLO con «Cerca» o Invio, mai a ogni tasto, usa la geocodifica di Open-Meteo (`geocoding-api.open-meteo.com/v1/search`, stesso fornitore, stessi termini, stesse regole di tempi, dimensioni e frequenza: `meteo.h`, `ph_meteo_cerca`; servizi: `ph_srv_meteo_cerca`) ed esce solo il testo scritto. Sotto l'elenco, sempre, «Luoghi da GeoNames, tramite Open-Meteo.com» (`PH_METEO_GEO_ATTRIBUZIONE`, collegamento a `PH_METEO_GEO_SITO`: i luoghi sono dati GeoNames CC BY 4.0). Una città per profilo (cioè per telefono), salvata nel profilo (campi `meteo_luogo_*` di config) e comunicata al backend con `ph_srv_meteo_localita`.
- **Precedenza**: la posizione del telefono vince sempre; la città scelta vale SOLO quando il telefono non dà una posizione (spenta, permesso negato, nessuna posizione nota, telefono non collegato). Niente altro (nessun «usa sempre questa località»). `ph_srv_meteo_fonte` dice da dove vengono i dati.
- **Nome della località nel widget**: dal Geocoder del telefono quando la posizione è del telefono; dal risultato scelto quando la città è a mano. Titolo «Meteo — <località>»; senza nome, «Meteo».

## 19. Voce «Compatibilità» delle finestre di app (23 nov sera, PIANO.md)

Per le app che si disegnano rimpicciolite in un angolo (causa accertata: usano la densità dello schermo VERO del telefono, mentre finestra, attività e processo vedono quella dello schermo virtuale). Scelta dell'utente, per app; nessun riconoscimento automatico, nessun elenco di app nel codice.
- **Dove**: menu «⋮» della finestra (`PH_POPUP_ALTRI_COMANDI`), dopo «Ruota» e prima di «Chiudi app»: voce a spunta «Compatibilità», con sotto una riga di spiegazione più piccola e grigia: «Per le app che si vedono troppo piccole». Disattivata (grigia) durante una registrazione; assente nello specchio.
- **Stato**: per pacchetto, nel profilo del telefono (config: elenco `compatibilita`), scritto SOLO quando l'utente cambia la voce. All'apertura successiva dell'app la voce è già attiva.
- **Effetto** (`finestre.h`, `ph_fin_imposta_compatibilita`): lo schermo virtuale di quella finestra ha la densità VERA del telefono e il flusso è scalato di conseguenza (punti della finestra × densità/160, mai meno dei punti di larghezza del telefono, multipli di 8; oltre il tetto si riducono i pixel, mai la densità). Il contenuto visto dall'utente resta della stessa grandezza; l'immagine NON è 1:1 (la riduce la GPU): va bene così.
- **Al cambio**: la finestra resta aperta e l'app resta lì; si chiede al telefono la nuova misura e la nuova densità con un ridimensionamento (VIDEO_RIDIMENSIONA porta già i dpi). Nessuna riapertura della sessione. Da verificare sul telefono con l'app che ha il difetto (vedi nota di finestra-app); se il cambio a caldo non bastasse, il rimedio è chiudere e riaprire la sessione (deciso dopo la prova, non ora).
- **Protocollo e telefono: NULLA da cambiare** (VIDEO_APRI e VIDEO_RIDIMENSIONA portano già la densità; il telefono la applica allo schermo virtuale). Ossatura e collegamento: nulla (inoltrano già misura e densità).
