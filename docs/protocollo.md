# Protocollo PC ↔ componente sul telefono (versione 1)

Contratto tra `src/componente/` (C, PC) e `telefono/` (Java). Chi lo trova sbagliato o incompleto **non lo cambia da solo**: lo segnala a chi coordina.

## 1. Avvio

1. Il PC copia il jar in `/data/local/tmp/yasde-<8 cifre esadecimali casuali>.jar` (servizio `sync:`). Il nome cambia a ogni avvio: così il guardiano di un servizio appena morto, che cancella il proprio jar, non può cancellare quello dell'avvio successivo.
2. Il PC apre un canale shell e lancia:
   `CLASSPATH=/data/local/tmp/yasde-<casuale>.jar app_process / it.yasde.Servizio`
   Il canale resta aperto per tutta la vita del servizio: su stdout il servizio scrive una riga `PRONTO` quando ascolta, poi solo righe di log. Se il canale shell si chiude, il servizio termina.
3. Il servizio ascolta su un `LocalServerSocket` astratto di nome `yasde`.
4. Il PC apre un canale ADB `localabstract:yasde` per ogni **canale del protocollo**. Appena aperto, il PC scrive una riga di testo ASCII terminata da `\n` che dice il tipo di canale:

| Riga | Canale | Quanti |
|---|---|---|
| `comandi` | richieste e risposte, eventi | uno solo |
| `video:<id>` | pacchetti video della sessione `<id>` (decimale) | uno per sessione |
| `input` | eventi di mouse e tastiera | uno solo |
| `audio` | pacchetti audio (§2.5) | uno solo |
| `cerca` | ricerca di file e cartelle (§2.7) | uno per ricerca |
| `appunti` | appunti condivisi (§2.8) | uno solo |

Tipo sconosciuto: il servizio chiude il canale.

Il servizio gira come uid 2000 (shell), senza root, senza nulla di installato. Alla fine (canale `comandi` chiuso, o shell chiusa) chiude tutte le sessioni, fa i ripristini e cancella il jar.

## 2. Messaggi

Su `comandi` e `input` ogni messaggio è:

```
tipo      1 byte
lunghezza 4 byte, big endian (solo il corpo; massimo 1 MiB)
corpo     lunghezza byte
```

Tutti i numeri sono big endian. `stringa` = 2 byte di lunghezza + UTF-8 senza terminatore.

Sul canale `comandi` il PC manda **una richiesta alla volta** e aspetta la risposta: `OK` (con eventuale corpo) oppure `ERRORE`. Gli **eventi** (da telefono a PC) possono arrivare in qualunque momento, anche tra una richiesta e la sua risposta: si riconoscono dal tipo.

### 2.1 Generali (0x01–0x0f)

| Tipo | Nome | Verso | Corpo |
|---|---|---|---|
| 0x01 | SALUTO | PC→T | `u16` versione del protocollo (1) |
| 0x02 | OK | T→PC | dipende dalla richiesta (vuoto se non detto) |
| 0x03 | ERRORE | T→PC | `stringa` frase in italiano. Tipo sconosciuto: `tipo sconosciuto` |
| 0x04 | CHIUDI | PC→T | vuoto: il servizio ripristina tutto e termina (risponde OK prima) |
| 0x05 | PRESENZA | PC→T | vuoto; risposta `OK` vuota. «Il PC è vivo» (23 nov sera) |

Risposta `OK` a SALUTO: `u16` versione, `u16` livello API Android, `u16` larghezza schermo, `u16` altezza schermo, `u16` densità (dpi), `stringa` modello.
Versione diversa da quella del servizio: `ERRORE`.

**Segni di vita del PC** (decisione del committente del 23 nov sera: «telefono mai in blocco finché YASDE è attiva», con il tetto di 30 minuti):
- QUALUNQUE messaggio che il telefono riceve dal PC sul canale `comandi` (SALUTO compreso, ogni richiesta, PRESENZA) è un segno di vita. Il servizio annota l'ora (orologio monotono, `SystemClock.elapsedRealtime`) PRIMA di smistarlo.
- **PRESENZA ogni 5 minuti** (`PRESENZA_INTERVALLO` = 300 s): il PC la manda quando da 5 minuti non ha mandato nulla sul canale `comandi`; serve solo quando non c'è altro traffico. Come ogni richiesta, aspetta l'`OK` con il limite solito; un fallimento non è grave (si riprova alla scadenza successiva; se il collegamento è caduto, lo dice già il resto).
- **Assenza del PC: 30 minuti** (`PC_ASSENTE` = 1800 s, cioè sei PRESENZA mancate): senza segni di vita per 30 minuti il telefono smette di tenersi sveglio (§2.9, «Assenza del PC»). NON chiude il servizio né le sessioni: al primo segno di vita successivo riprende tutto da solo.

### 2.2 Video (0x40–0x4f)

| Tipo | Nome | Verso | Corpo |
|---|---|---|---|
| 0x40 | VIDEO_APRI | PC→T | `u32` id, `u16` larghezza, `u16` altezza, `u16` dpi, `u32` bit al secondo, `u8` fps, `stringa` pacchetto dell'app (vuota = specchio dello schermo reale) |
| 0x41 | VIDEO_CHIUDI | PC→T | `u32` id, `u8` togli_task (1 = toglie l'app dai recenti) |
| 0x42 | VIDEO_RIDIMENSIONA | PC→T | `u32` id, `u16` larghezza, `u16` altezza, `u16` dpi |
| 0x43 | VIDEO_CHIAVE | PC→T | `u32` id: chiede subito un fotogramma chiave |
| 0x48 | VIDEO_EVENTO | T→PC (evento) | `u32` id, `u8` che cosa, `u16` a, `u16` b |

- L'`id` lo sceglie il PC, mai riusato nella stessa connessione.
- Larghezza e altezza: multipli di 8, tra 64 e 2560. Altrimenti `ERRORE`.
- VIDEO_APRI con un pacchetto: crea uno schermo virtuale di quelle misure e densità, avvia lì l'app, e codifica in H.264 con il codificatore hardware di Android (`MediaCodec`). Risponde `OK` quando la sessione esiste; il PC apre allora il canale `video:<id>`. Il codificatore parte quando il canale è aperto.
- VIDEO_RIDIMENSIONA: risposta `OK` con `u16` larghezza, `u16` altezza effettive. Dopo, il primo pacchetto con le nuove misure è una CONFIGURAZIONE. Dopo quella CONFIGURAZIONE non arrivano più pacchetti con le misure vecchie.
- VIDEO_EVENTO, `che cosa`: 1 = l'app si è chiusa da sola (la sessione resta da chiudere), 2 = schermo protetto acceso/spento (`a` = 1/0), 3 = le misure sono cambiate (`a`×`b`), 4 = errore del codificatore (la sessione è morta).

Pausa e ripresa (misura della Fase 0 sul telefono di prova: ripresa in 0,12–0,17 s, compositore del telefono a riposo durante la pausa, l'app resta viva):

| Tipo | Nome | Verso | Corpo |
|---|---|---|---|
| 0x44 | VIDEO_PAUSA | PC→T | `u32` id |
| 0x45 | VIDEO_RIPRENDI | PC→T | `u32` id |

- VIDEO_PAUSA: stacca la superficie del codificatore dallo schermo virtuale (`VirtualDisplay.setSurface(null)`). Schermo virtuale, app e codificatore restano vivi; non arrivano più fotogrammi. Risposta `OK`. In pausa: già in pausa → `OK` senza effetto; VIDEO_RIDIMENSIONA e VIDEO_CHIAVE → `ERRORE`; l'input non dà errore ma NON ha effetto (misurato: a superficie staccata Android non lo consegna all'app), quindi il PC non manda input a una sessione in pausa e, prima di metterla in pausa, chiude ogni tocco in corso (TOCCO con azione su o annulla); VIDEO_CHIUDI funziona come sempre; i VIDEO_EVENTO continuano ad arrivare.
- VIDEO_RIPRENDI: riattacca la superficie e chiede un fotogramma chiave; se entro 250 ms non arriva (app ferma), ridisegno forzato come all'apertura. Risposta `OK` subito; il primo pacchetto dopo la ripresa è un fotogramma chiave (preceduto dalla CONFIGURAZIONE se serve). Non in pausa → `OK` senza effetto.
- Le sessioni specchio (pacchetto vuoto) si mettono in pausa allo stesso modo.
- Precisazioni. Dopo l'`OK` a VIDEO_PAUSA il servizio non scrive più pacchetti sul canale: ciò che il codificatore restituisce ancora si scarta. I pacchetti scritti PRIMA della pausa e non ancora letti dal PC possono arrivare anche dopo la ripresa: «il primo è un fotogramma chiave» vale per ciò che il telefono scrive dopo l'`OK` a VIDEO_RIPRENDI. PAUSA o RIPRENDI con un id sconosciuto o di una sessione chiusa → `ERRORE`. PAUSA prima che il canale `video:<id>` sia aperto è valida: il codificatore parte alla ripresa, se il canale è aperto. Se in pausa le misure cambiano da sole (VIDEO_EVENTO 3), dopo la ripresa arriva prima la CONFIGURAZIONE con le misure nuove. Per ridimensionare una sessione in pausa il PC prima la riprende.

Tipi 0x46–0x47 riservati (liberare il codificatore nelle pause lunghe: +0,07 s alla ripresa; si aggiunge solo se una misura lo richiede).

### 2.3 Canale `video:<id>` (solo T→PC)

Ogni pacchetto:

```
genere    1 byte: 1 = CONFIGURAZIONE (SPS+PPS), 2 = FOTOGRAMMA
segni     1 byte: bit 0 = fotogramma chiave
larghezza 2 byte   misure del video a cui appartiene il pacchetto
altezza   2 byte
tempo     8 byte   microsecondi, orologio monotono del telefono (presentationTimeUs)
lunghezza 4 byte   (massimo 8 MiB)
dati      H.264 in formato Annex B (con i codici di inizio 00 00 00 01)
```

Dopo l'apertura del canale il primo pacchetto è una CONFIGURAZIONE, il secondo un fotogramma chiave. Il PC non scrive mai su questo canale; se lo chiude, il codificatore si ferma (la sessione resta finché non arriva VIDEO_CHIUDI).

### 2.4 Canale `input` (0x50–0x5f, solo PC→T, nessuna risposta)

Ogni evento porta l'id della sessione video e **le misure su cui il PC ha calcolato le coordinate**. Se non coincidono con le misure attuali della sessione, il telefono **scarta** l'evento (specifica 4.5).

| Tipo | Nome | Corpo |
|---|---|---|
| 0x50 | TOCCO | `u32` id, `u16` larghezza, `u16` altezza, `u8` azione (0 giù, 1 muovi, 2 su, 3 annulla), `u16` x, `u16` y, `u8` pulsanti (bit 0 sinistro, 1 destro, 2 centrale) |
| 0x51 | ROTELLA | `u32` id, `u16` larghezza, `u16` altezza, `u16` x, `u16` y, `i16` orizzontale, `i16` verticale (in 120esimi di scatto) |
| 0x52 | TASTO | `u32` id, `u8` azione (0 giù, 1 su), `u32` codice tasto Android (`KeyEvent.KEYCODE_*`), `u32` stato dei modificatori (`KeyEvent.META_*`) |
| 0x53 | TESTO | `u32` id, `stringa` testo da inserire |
| 0x54 | INDIETRO | `u32` id: tasto Indietro (giù e su) |
| 0x55 | PIZZICO | `u32` id, `u16` larghezza, `u16` altezza, `u8` fase (0 inizio, 1 continua, 2 fine, 3 annulla), `u16` x, `u16` y (centro del gesto, nella misura del video), `i16` passo (in 120esimi di scatto: positivo = ingrandisci, −1200…1200) |

Il mouse è un dito (specifica 4.10): TOCCO con sorgente touchscreen; la rotella è `ACTION_SCROLL`. Ogni evento iniettato porta l'id dello schermo virtuale della sessione.

**PIZZICO** (decisione del committente del 23 nov: Ctrl + rotella = zoom): il PC manda il gesto come PASSI e il TELEFONO genera due dita (`MotionEvent` con due puntatori, sorgente touchscreen, sullo schermo virtuale della sessione: `ACTION_DOWN`, `ACTION_POINTER_DOWN`, `ACTION_MOVE`, `ACTION_POINTER_UP`, `ACTION_UP`; `ACTION_CANCEL` per annulla). Regole:
- **Geometria**: le due dita stanno su una retta orizzontale per il centro (x, y), simmetriche. Distanza iniziale = 25% del lato minore dello schermo virtuale; ogni 120 di passo la moltiplica per 1,25 (−120 la divide), sommando i passi del gesto; la distanza resta tra il 5% e il 90% del lato minore. Le coordinate del PC (nella misura del video) si scalano su quelle dello schermo virtuale come per TOCCO. Se una delle dita uscirebbe dallo schermo, il centro si sposta verso l'interno quanto basta (le dita restano sempre dentro [0, larghezza−1] × [0, altezza−1]); il gesto continua da lì con lo stesso spostamento.
- **Passi lunghi**: un passo oltre ±30 si divide in più `ACTION_MOVE` (al più 30 per movimento, a ~8 ms l'uno), così i rilevatori di gesto delle app vedono un movimento continuo.
- **Fasi**: `inizio` mette giù le due dita (e applica il suo passo, se c'è); `continua` muove; `fine` alza le due dita (`ACTION_POINTER_UP` poi `ACTION_UP`); `annulla` manda `ACTION_CANCEL`. Un `continua`/`fine` senza un `inizio` aperto si scarta. Un nuovo `inizio` con un gesto aperto chiude prima quello (fine).
- **Il gesto si chiude sempre**: se per 400 ms non arriva nessun passo, il telefono lo chiude da solo (fine). Anche la chiusura della sessione, la pausa e un cambio di misura lo chiudono (annulla). Nessun dito resta mai giù.
- **Con un TOCCO in corso**: vince l'ultimo arrivato. Un `inizio` di PIZZICO con il dito del TOCCO giù manda prima `ACTION_CANCEL` per quel dito; un TOCCO giù con un pizzico aperto chiude prima il pizzico (annulla). Un TOCCO muovi/su che non ha più il suo dito giù si scarta.
- **In pausa** (VIDEO_PAUSA) il PIZZICO si scarta e un gesto aperto si chiude (annulla).
- **Valori**: misura diversa da quella attuale → scartato (come gli altri eventi); x ≥ larghezza o y ≥ altezza → scartato; passo fuori da −1200…1200 → limitato; fase sconosciuta → scartato. Un jar vecchio risponde come per un tipo sconosciuto (il canale input non ha risposte: il PC lo sa dalla versione del componente, non c'è errore visibile).

### 2.5 Canale `audio` (solo T→PC) — cartella `telefono/audio/`, classe `Audio`

Aprire il canale avvia la cattura; chiuderlo la ferma. Si cattura **tutto l'audio in riproduzione del telefono** (specifica 4.9) con una regola di instradamento «loopback» di Android (`AudioPolicy`, API nascosta): finché è attiva il telefono resta muto e i suoni escono dal PC; alla rimozione torna a suonare da solo. Un solo canale audio alla volta (il secondo viene chiuso).

Codifica: Opus, 48 kHz, stereo, 128 kbit/s, pacchetti da 20 ms (`MediaCodec`). Ogni pacchetto:

```
tempo     8 byte   microsecondi: campioni catturati finora / 48000 (non l'orologio)
lunghezza 2 byte
dati      un pacchetto Opus
```

Il volume «media» del telefono va portato al massimo durante la cattura (con volume 0 alcune app non avviano l'audio) e ripristinato dal guardiano **solo se ha ancora il valore messo da YASDE** (il valore dell'utente vince). Il volume che l'utente regola nella shell è un guadagno sul PC, non tocca il telefono.

### 2.6 Dati del telefono (0x70–0x7f, richieste sul canale `comandi`)

Due cartelle: `telefono/dati/` (classe `Dati`: tipi 0x70–0x73, 0x76–0x77 ed eventi 0x7e–0x7f) e `telefono/archivio/` (classe `Archivio`: tipi 0x74–0x75, e classe `Cerca` per il canale di §2.7).

| Tipo | Nome | Corpo della richiesta | Corpo dell'OK |
|---|---|---|---|
| 0x70 | APP_ELENCO | vuoto | `u16` numero, poi per ogni app: `stringa` pacchetto, `stringa` nome mostrato, `u8` segni (bit 0 = app di sistema, bit 1 = solo verticale) |
| 0x71 | APP_ICONA | `stringa` pacchetto, `u16` lato in pixel (16–512) | i byte di un PNG lato×lato |
| 0x72 | STATO | vuoto | `u8` batteria (0–100), `u8` in carica (0/1), `u8` rete (0 nessuna, 1 Wi-Fi, 2 cellulare), `u8` segnale (0–4), `u8` schermo bloccato (0/1) |
| 0x73 | NOTIFICHE | vuoto | `u16` numero, poi per ogni notifica: `stringa` chiave, `stringa` pacchetto, `stringa` titolo, `stringa` testo, `u64` ora (millisecondi dal 1970) |
| 0x74 | MINIATURA | `stringa` percorso del file, `u16` lato massimo (32–512) | i byte di un JPEG (foto raddrizzata secondo EXIF; per un video un suo fotogramma) |
| 0x75 | MEMORIA | vuoto | `u64` byte totali, `u64` byte liberi della memoria interna |
| 0x76 | MEMORIA_DETTAGLIO | vuoto | `u64` byte di immagini, `u64` di video, `u64` di audio, `u64` delle app (codice e dati, cache compresa); un valore non leggibile vale 0xFFFFFFFFFFFFFFFF |
| 0x77 | CALENDARIO | `u8` massimo (1–16) | `u16` numero, poi per ogni evento: `stringa` titolo, `u64` inizio, `u64` fine (millisecondi dal 1970), `u8` tutto il giorno (0/1) |
| 0x7b | CALENDARIO_INTERVALLO | `u64` da, `u64` a (millisecondi dal 1970; a − da al massimo 62 giorni), `u16` massimo (1–500) | `u8` troncato (1 = c'erano più eventi del massimo), `stringa` pacchetto dell'app Calendario predefinita (vuota se nessuna), `u16` numero, poi per ogni evento come in CALENDARIO: `stringa` titolo, `u64` inizio, `u64` fine, `u8` tutto il giorno |
| 0x7c | SVEGLIA | vuoto | `u8` c'è (0/1), `u64` ora (millisecondi dal 1970; 0 se non c'è), `stringa` pacchetto dell'app che l'ha impostata (vuota se non nota) |
| 0x7d | BATTERIA | vuoto | `u8` livello (0–100), `u8` stato (0 sconosciuto, 1 in carica, 2 in scarica, 3 non in carica, 4 piena), `u8` alimentazione (bit 0 rete, bit 1 USB, bit 2 senza fili, bit 3 base; 0 = nessuna), `i16` temperatura in decimi di °C (0x7fff = non nota), `u8` salute (0 sconosciuta, 1 buona, 2 surriscaldata, 3 guasta, 4 sovratensione, 5 troppo fredda, 6 errore), `u16` tensione in mV (0 = non nota), `i32` minuti alla carica completa (−1 = non noto), `i32` minuti di autonomia stimata (−1 = non noto) |
| 0x7e | STATO_CAMBIATO | evento T→PC | come l'OK di STATO; mandato quando cambia il blocco dello schermo, la carica, il tipo di rete, o la batteria di almeno 1 punto (il solo segnale non conta) |
| 0x7f | NOTIFICHE_CAMBIATE | evento T→PC | vuoto: il PC richiede NOTIFICHE |

- MEMORIA_DETTAGLIO e CALENDARIO servono solo ai widget della shell (specifica 5.2.3); fattibilità misurata sul telefono di prova (suddivisione in circa 60 ms, calendario in circa 13 ms). CALENDARIO: i prossimi eventi (da adesso a 30 giorni), in ordine di inizio, dal provider standard di Android letto come utente shell; sono dati personali: il PC lo chiede SOLO se l'utente ha messo il widget Calendario sul desktop, il telefono non li scrive mai in un log né altrove, e il PC non li salva su disco. Se il provider non è leggibile: `ERRORE` (il widget lo dice), mai un elenco vuoto finto. Solo gli eventi dei calendari visibili (quelli che mostra l'app Calendario). Per gli eventi «tutto il giorno» inizio e fine sono a mezzanotte UTC, non nel fuso dell'utente: il PC li mostra come giorni, senza convertirli di fuso. Le «app» di MEMORIA_DETTAGLIO sono codice + dati (i dati comprendono già la cache).
- CALENDARIO_INTERVALLO (widget «Calendario del mese»): gli eventi che si sovrappongono a [da, a), in ordine di inizio, dai soli calendari visibili, con le STESSE regole di riservatezza di CALENDARIO (chiesto solo se il widget è sul desktop; mai nei log del telefono né su disco del PC). Intervallo vuoto, rovesciato o oltre 62 giorni, massimo fuori da 1–500: `ERRORE`. L'app Calendario è quella che risponde all'intento `ACTION_MAIN` + `CATEGORY_APP_CALENDAR` (il PC la apre con un clic sul widget, come un'app qualunque). Provider non leggibile: `ERRORE`.
- SVEGLIA: `AlarmManager.getNextAlarmClock()` (API pubblica, nessun permesso); il pacchetto è quello dell'intento di presentazione della sveglia (`getShowIntent().getCreatorPackage()`), vuoto se non c'è.
- BATTERIA: dall'intento persistente `ACTION_BATTERY_CHANGED` (`registerReceiver(null, …)`) e da `BatteryManager` (`computeChargeTimeRemaining` per la carica, API pubblica); l'autonomia stimata solo se il sistema la espone senza permessi speciali, altrimenti −1 (mai stimata da noi). Il PC la richiede al più una volta al minuto mentre il widget è sul desktop.
- APP_ELENCO: solo le app con un'attività di avvio (categoria LAUNCHER), ordinate per nome. Se la risposta supererebbe 1 MiB: `ERRORE`.
- «Solo verticale» = l'attività di avvio dichiara un orientamento verticale (portrait, sensor/reverse/user portrait) e non è ridimensionabile.
- Le notifiche elencate sono quelle visibili all'utente (non i riepiloghi di gruppo che hanno figli, non quelle nascoste); l'ora è quella mostrata nella tendina (`when`), o quella di creazione se manca; il testo può essere vuoto. Se il sistema non permette di leggerle senza privilegi: `ERRORE` con la frase che lo dice (mai un elenco vuoto finto).
- Gli eventi 0x7e/0x7f partono solo dopo la prima richiesta STATO/NOTIFICHE; se non esiste un modo «puro» di essere avvisati dei cambi, il telefono controlla da solo a intervalli (lo stato non più di una volta ogni 2 s, le notifiche non più di una volta ogni 5 s, perché leggerle costa) e manda l'evento solo se qualcosa è cambiato.

### 2.7 Canale `cerca` — cartella `telefono/archivio/`, classe `Cerca`

Per non bloccare il canale comandi, ogni ricerca ha il suo canale. Appena aperto, il PC manda un messaggio (formato di §2):

| Tipo | Nome | Verso | Corpo |
|---|---|---|---|
| 0x78 | CERCA | PC→T | `stringa` cartella di partenza, `stringa` testo cercato, `u8` solo cartelle (0/1), `u16` massimo di risultati |
| 0x79 | TROVATO | T→PC | `stringa` percorso completo, `u8` è una cartella (0/1) |
| 0x7a | CERCA_FINE | T→PC | `u8` motivo: 0 finita, 1 massimo raggiunto, 2 errore |

Si cerca nei nomi (non nel contenuto), senza distinguere maiuscole, saltando i nomi che iniziano con il punto e le cartelle non leggibili. I risultati arrivano man mano. Massimo di risultati 0 = il massimo del telefono (5000). CERCA_FINE: motivo 1 anche quando il telefono si ferma per i propri tetti (numero di cartelle visitate o tempo); motivo 2 se la cartella di partenza non esiste, non è un percorso assoluto o non è leggibile (così il PC distingue «nessun risultato» da «non si può cercare qui»). I collegamenti simbolici possono comparire tra i risultati ma non vengono seguiti. Chiudere il canale interrompe subito la ricerca (specifica 5.9.4: interruzione se l'utente digita altro).

### 2.8 Canale `appunti` (nei due versi) — cartella `telefono/appunti/`, classe `Appunti`

Appunti condivisi tra PC e telefono, **solo testo** (specifica 4.11). Messaggi nel formato di §2, con il testo come `u32` lunghezza + UTF-8 (non la `stringa` a 16 bit).

| Tipo | Nome | Verso | Corpo |
|---|---|---|---|
| 0x58 | APPUNTI_TESTO | T→PC | il testo appena copiato sul telefono |
| 0x59 | APPUNTI_IMPOSTA | PC→T | il testo da mettere negli appunti del telefono |
| 0x5a | APPUNTI_INCOLLA | PC→T | `u32` id della sessione video, poi il testo (`u32` lunghezza + UTF-8, come negli altri due): lo mette negli appunti e manda «incolla» all'app di quella sessione |

- Il telefono manda APPUNTI_TESTO quando i suoi appunti cambiano, **non** per ciò che ha appena ricevuto dal PC (niente eco).
- **Non passano mai** (specifica 4.11): le copie marcate come sensibili (`ClipDescription.EXTRA_IS_SENSITIVE`), i testi oltre 200.000 byte (in entrambi i versi: il messaggio viene ignorato), e i testi che arrivano da un gestore di password (contenuto marcato sensibile o, se il sistema lo espone, pacchetto sorgente noto come gestore di password: il criterio usato va documentato nella classe).
- Contenuti non testuali (immagini, file): ignorati.
- APPUNTI_INCOLLA serve al testo che la tastiera virtuale non sa produrre (accenti, emoji; specifica 4.10 «testo non ASCII con incolla»): dopo l'incolla gli appunti del telefono restano con quel testo.
- Alla chiusura del canale gli appunti del telefono restano come sono.

### 2.9 Pannello fisico (0x60–0x6f, sul canale `comandi`) — cartella `telefono/pannello/`, classe `Pannello`

Regola del «telefono in mano» (specifica 3.3 e 4.7): mentre le app si usano dal PC il pannello del telefono resta spento, ma il telefono resta sveglio e sbloccato; quando l'utente lo prende in mano il pannello si riaccende.

| Tipo | Nome | Verso | Corpo |
|---|---|---|---|
| 0x60 | PANNELLO_IMPOSTA | PC→T | `u8` 0 = spegni, 1 = accendi |
| 0x61 | PANNELLO_STATO | PC→T | vuoto; OK con `u8` acceso (0/1), `u8` in mano (0/1) |
| 0x6e | PANNELLO_EVENTO | T→PC (evento) | `u8` acceso (0/1), `u8` motivo: 1 l'utente ha toccato/sbloccato il telefono, 2 chiamata in arrivo o in corso, 3 fine chiamata, 4 chiesto dal PC, 5 ripristino (chiusura), 6 PC assente da 30 minuti (23 nov sera) |

- Spegnere agisce solo sul compositore (`SurfaceControl.setDisplayPowerMode`, API nascosta): Android continua a credere lo schermo acceso, le app sugli schermi virtuali continuano a girare e a ricevere input.
- Il telefono riaccende DA SOLO il pannello, e manda l'evento, quando: l'utente tocca lo schermo reale o preme un tasto fisico, sblocca il telefono, arriva o è in corso una chiamata. Da quel momento è «in mano»: resta acceso finché il PC non manda di nuovo PANNELLO_IMPOSTA 0 (lo decide il PC: al primo input dal PC o dopo il tempo di inattività scelto dall'utente).
- Durante una chiamata PANNELLO_IMPOSTA 0 viene rifiutato con ERRORE.
- Dopo ogni spegnimento si misura la frequenza attiva del compositore e la si scrive nel log con la massima del display. La frequenza voluta è la massima tra i modi con la STESSA risoluzione del modo attivo, limitata dal tetto scelto dall'utente (`peak_refresh_rate`, se numerico e maggiore di 0). Se la attiva è sotto quella voluta (misurato sul telefono di prova: da 120 a 60 Hz a pannello spento, con il video che scende da circa 87 a 57 fotogrammi al secondo; con il rimedio risale a circa 80), si porta la frequenza minima a quella voluta e la si ripristina alla riaccensione. Se l'utente ha già una frequenza minima uguale o più alta non si cambia nulla. È un workaround innescato dal sintomo misurato su ogni telefono, mai dal nome del produttore (specifica 2.3), isolato in un punto e documentato.
- Ripristini registrati nel guardiano PRIMA di ogni modifica: pannello acceso (ordine 400), frequenza minima al valore dell'utente solo se ha ancora il nostro (ordine 410). Con `kill -9` del servizio il pannello DEVE riaccendersi.
- Il timeout dello schermo del telefono va portato al massimo mentre il pannello è gestito da YASDE (senza, le app sugli schermi virtuali smettono di ricevere input) e ripristinato dal guardiano solo se ha ancora il nostro valore.
- **Telefono sveglio** (23 nov sera, classe `TieniSveglio`): il wake lock di schermo si prende al SALUTO (primo messaggio del canale `comandi`), NON all'avvio del servizio; si rilascia mentre il telefono è BLOCCATO (schermata di blocco presente, `KeyguardManager.isKeyguardLocked`) e si riprende allo sblocco; si rilascia alla chiusura (anche nel ripristino del guardiano, se serve).
- **Assenza del PC** (§2.1, `Registro.PC_ASSENTE_MS`): se dall'ultimo segno di vita (`Registro.ultimoSegnoDiVita`) passano 30 minuti, il pannello fa, nell'ordine: riaccende il pannello se l'aveva spento lui e ripristina la frequenza minima (come alla chiusura, stessi ripristini del guardiano, che poi si tolgono), rimette il timeout dello schermo dell'utente (se ha ancora il nostro valore), rilascia il wake lock, manda PANNELLO_EVENTO (acceso 1, motivo 6). È lo stato più sicuro: il telefono torna esattamente quello di tutti i giorni e Android lo spegne e lo blocca con i tempi dell'utente; nessuno schermo spento «da noi» resta senza padrone. Il servizio e le sessioni restano vivi. Al primo segno di vita successivo (`Registro.registraSegnoDiVita`): riprende il wake lock (se il telefono non è bloccato) e porta di nuovo il timeout al massimo (ripristino registrato PRIMA); il pannello RESTA ACCESO finché il PC non manda PANNELLO_IMPOSTA 0 (come quando il telefono è «in mano»). Il controllo dell'assenza gira sul thread del pannello almeno una volta al minuto; nessuna attesa senza limite.

### 2.11 Posizione approssimativa (0x90–0x9f, sul canale `comandi`) — cartella `telefono/dati/`, classe `Dati`

Serve SOLO al widget Meteo (decisione del committente del 23 nov sera): il PC la chiede solo mentre almeno un widget Meteo è sul desktop (aggiungerlo vale come scelta dell'utente: nessun dialogo di consenso, decisione del 23 nov notte).

| Tipo | Nome | Corpo della richiesta | Corpo dell'OK |
|---|---|---|---|
| 0x90 | POSIZIONE | `u8` richiesta permessa (0/1) | `u8` esito (0 nota, 1 posizione del telefono spenta, 2 permesso negato all'utente shell, 3 nessuna posizione nota), `i32` latitudine e `i32` longitudine in MILLESIMI di grado, sempre multipli di 5 (latitudine −90000..90000, longitudine −180000..180000; 0 se esito ≠ 0), `u32` età in secondi (0xffffffff se ignota), `u8` fornitore (0 rete, 1 gps, 2 fusa, 3 passivo), `stringa` località (vuota se non nota), `u8` ricerca avviata (0/1) |

Corpo dell'OK: 1 + 4 + 4 + 4 + 1 + (2 + n) + 1 = **17 + n** byte (la `stringa` ha 2 byte di lunghezza, come ovunque), n = byte UTF-8 della località (0–127). Nessun altro campo: un corpo più corto o più lungo è un errore.

- **Arrotondamento sul telefono** (decisione del committente del 23 nov notte: circa 500 m): le coordinate si arrotondano al multiplo PIÙ VICINO di 0,005° (a metà, lontano da zero) PRIMA di tutto: prima di codificarle, prima di tenerle in memoria, prima di darle al Geocoder. Sono circa 555 m nord-sud e circa 415 m est-ovest a 42° di latitudine. La posizione precisa non lascia mai il telefono. Sul filo: millesimi interi = gradi × 1000, quindi sempre multipli di 5. Il PC rifiuta (lettura fallita) coordinate fuori limite o NON multiple di 5: è un controllo in più che il telefono abbia arrotondato.
- **Fonte**: `LocationManager.getLastKnownLocation` su tutti i fornitori abilitati (rete, fusa, gps, passivo), si prende la più recente. Nessun GPS acceso, nessun aggiornamento continuo.
- **Risposta SEMPRE SUBITO** (decisione del 23 nov notte): 0x90 risponde in decine di millisecondi con l'ultima posizione nota (o «nessuna posizione nota») e la località già in memoria; dentro la richiesta non si aspetta MAI né una posizione nuova né il Geocoder (il canale `comandi` serve una richiesta alla volta: aperture di app, icone, notifiche non devono restare in coda).
- **Ricerca in sottofondo a bassa potenza**: SOLO se il PC la permette (`u8` = 1) E non c'è una posizione nota o ha più di 6 ore E il freno di 10 minuti tra due ricerche è rispettato E nessuna ricerca è già in corso: il telefono AVVIA su un suo thread una sola `LocationManager.getCurrentLocation` sul fornitore di RETE (mai `gps`), con limite di 10 s, e risponde subito con `ricerca avviata` = 1 (altrimenti 0). Il risultato resta in memoria sul telefono (mai su disco) e sarà la risposta delle richieste successive; scaduta o fallita → nulla cambia. Il PC, quando riceve `ricerca avviata` = 1, richiede di nuovo dopo circa 15 s, UNA sola volta per ricerca avviata, poi torna al suo ritmo normale. Il PC permette la ricerca al più una volta ogni 6 ore e solo mentre un widget Meteo è sul desktop.
- **Località**: calcolata dallo stesso lavoro in sottofondo (anche per una posizione nota senza località ancora calcolata, con le stesse condizioni di permesso e freno), `Geocoder.getFromLocation` sulle coordinate già arrotondate, solo se `Geocoder.isPresent()`, con limite di 2 s; tenuta in memoria accanto alla posizione a cui si riferisce (mai su disco). Finché non c'è, vuota. (Su Android puro il Geocoder può mancare: non è un errore.) Il widget la mostra nel titolo («Meteo — <località>»): la funzione è ATTIVA (decisione del 23 nov notte).
- **Riservatezza**: posizione e località mai nei log del telefono né su disco (né sul telefono né sul PC: in memoria e basta).
- Posizione del telefono spenta o permesso negato: OK con l'esito che lo dice (il widget lo spiega), non `ERRORE`.
- **Lato PC**: esito, coordinate (latitudine fuori ±9000, longitudine fuori ±18000), fornitore o `ricerca avviata` fuori elenco, località non UTF-8 o oltre 127 byte, corpo di lunghezza sbagliata = la lettura FALLISCE (errore, come per le altre letture), non «nessuna posizione nota».

### 2.10 Controlli rapidi (0x80–0x8f, sul canale `comandi`) — cartella `telefono/controlli/`, classe `Controlli`

Widget «Interruttori rapidi» (richieste del committente, `PIANO.md`). «Trova il telefono» NON è disponibile in questa versione (decisione del committente): i suoi tipi restano nella tabella, vedi sotto.

| Tipo | Nome | Verso | Corpo |
|---|---|---|---|
| 0x80 | CONTROLLI_STATO | PC→T | vuoto; OK con: `u8` suoneria (0 suona, 1 vibra, 2 silenzioso, 0xff non disponibile), `u8` non disturbare (0 spento, 1 acceso, 0xff non disponibile), `u8` torcia (0 spenta, 1 accesa, 0xff non disponibile), `u8` trova in corso (0/1), `u8` comandi possibili (bit 0 suoneria, bit 1 non disturbare, bit 2 torcia, bit 3 trova il telefono) |
| 0x81 | SUONERIA_IMPOSTA | PC→T | `u8` modo (0 suona, 1 vibra, 2 silenzioso) |
| 0x82 | NON_DISTURBARE_IMPOSTA | PC→T | `u8` 0 = spento, 1 = acceso (come il riquadro rapido di Android: «solo priorità») |
| 0x83 | TORCIA_IMPOSTA | PC→T | `u8` 0 = spenta, 1 = accesa |
| 0x84 | TROVA_AVVIA | PC→T | `u16` durata massima in secondi. In questa versione: `ERRORE` («non disponibile su questa versione») senza toccare nulla |
| 0x85 | TROVA_FERMA | PC→T | vuoto. In questa versione: OK senza fare nulla |
| 0x8e | CONTROLLI_CAMBIATI | T→PC (evento) | come l'OK di CONTROLLI_STATO |

- **Modi**: suoneria con `AudioManager.getRingerMode`/`setRingerMode`; non disturbare letto dall'impostazione globale `zen_mode` (`cmd settings get global zen_mode`: 0 spento, altro acceso), perché dall'uid shell `NotificationManager` e `Settings` non si possono usare (misurato), e impostato con `cmd notification set_dnd priority` (acceso) e `cmd notification set_dnd all` (spento) — per Android `set_dnd on` vuol dire «nessuna interruzione», non «solo priorità»; torcia con `CameraManager.setTorchMode` sulla prima fotocamera con flash. Un comando che il telefono non permette (eccezione di sicurezza, comando assente) risponde `ERRORE` con una frase per l'utente e da allora il suo bit in «comandi possibili» è 0: il widget dice «non disponibile». La disponibilità si controlla alla prima CONTROLLI_STATO (per i comandi `cmd`: la loro guida, senza eseguire nulla che cambi).
- **Ripristini (regola del guardiano, §3)**:
  - suoneria e non disturbare cambiati dal widget sono COMANDI ESPLICITI DELL'UTENTE con effetto voluto e permanente (come toccare il riquadro rapido sul telefono): NESSUN ripristino, restano anche dopo la chiusura. In SUONERIA_IMPOSTA il passaggio a «suona» lo fa Android: se il volume della suoneria salvato era 0 (tipico di chi sta in vibra) Android lo porta a 1 e non lo riabbassa tornando in vibra (misurato); è l'effetto voluto di un comando dell'utente: nessun ripristino;
  - la torcia accesa da YASDE si spegne alla chiusura ordinata (`Registro.registraChiusura`) e, se il servizio muore di colpo, la spegne il sistema (la torcia di `setTorchMode` è legata al processo che l'ha accesa: DA VERIFICARE sul telefono; se non fosse così, la torcia resta «non disponibile», perché non esiste un comando di shell per spegnerla dal guardiano);
- **Trova il telefono: NON disponibile in questa versione** (decisione del committente). Il bit 3 di «comandi possibili» è sempre 0; TROVA_AVVIA risponde `ERRORE` («non disponibile su questa versione») senza toccare nulla; TROVA_FERMA risponde OK senza fare nulla; «trova in corso» è sempre 0. Cause misurate: dal processo shell `AudioTrack` non si crea; il passaggio a «suona» alza il volume salvato da 0 a 1 senza ritorno. I tipi 0x84 e 0x85 restano nella tabella.
- CONTROLLI_CAMBIATI parte solo dopo la prima CONTROLLI_STATO; se non c'è un modo puro di essere avvisati, il telefono controlla da solo non più di una volta ogni 2 s e manda l'evento solo se qualcosa è cambiato (anche per i cambi fatti a mano sul telefono).

## 3. Regole per il lato telefono

- Solo API di Android puro. Le API nascoste si usano per riflessione, raccolte in una sola classe per cartella (`Nascoste` in `base`, `NascosteVideo` in `video`), ciascuna con una prova all'avvio che dice quali mancano.
- **Guardiano** (`Custode`): ogni cosa che il servizio cambia sul telefono registra il suo comando di ripristino; un processo shell separato lo esegue se il servizio muore di colpo (`kill -9`), e cancella il jar. Il valore dell'utente vince: si ripristina solo se l'impostazione ha ancora il valore messo da YASDE.
- Ogni canale gira sul suo thread e cattura tutti gli errori (anche `Error`): un canale rotto non abbatte il servizio.

### Agganci tra le classi Java (per lavorare in parallelo)

Cartelle di sorgenti (oltre a `telefono/audio/`, `telefono/dati/` e `telefono/archivio/`, che seguono le stesse regole di `video`): `telefono/contratto/` (interfacce comuni, già scritte: `Registro`, `GestoreCanale`, `GestoreComandi`, `Guardiano`), `telefono/base/` (servizio, protocollo, guardiano) e `telefono/video/` (video e input). `base` e `video` non si nominano a vicenda: si parlano solo attraverso `Registro`.

- `Servizio` (base) all'avvio chiama per riflessione il metodo statico `registra()` di ogni classe dell'elenco `Servizio.PEZZI` (`it.yasde.Video`, `it.yasde.Input`, `it.yasde.Audio`, `it.yasde.Dati`, `it.yasde.Archivio`, `it.yasde.Cerca`, `it.yasde.Appunti`, `it.yasde.Pannello`, e dal 23 nov `it.yasde.Controlli`); una classe assente si salta con una riga di log. Poi imposta `Registro.impostaGuardiano(...)` e `Registro.impostaEventi(...)`.
- Ogni pezzo in `registra()` chiama `Registro.registraCanale("video", ...)`, `Registro.registraComandi(0x40, 0x4f, ...)` e simili; manda eventi con `Registro.mandaEvento`; registra i ripristini con `Registro.guardiano()`.
- Chiusura ordinata: ogni pezzo registra con `Registro.registraChiusura(Runnable)` ciò che deve chiudere (le sessioni video); il servizio chiama `Registro.eseguiChiusure()` prima dei ripristini del guardiano.
- Il PC tiene aperto lo stdin del canale shell che ha lanciato il servizio: se si chiude, il servizio termina.
- I comandi di ripristino del guardiano sono righe di shell autonome: niente variabili d'ambiente del servizio, niente virgolette annidate.
- Il servizio smista: riga di apertura del canale → `Registro.canale(prefisso)`; tipo di richiesta → `Registro.comandi(tipo)`.

`Protocollo` (base) contiene le costanti dei tipi e le funzioni per leggere e scrivere messaggi e stringhe.
