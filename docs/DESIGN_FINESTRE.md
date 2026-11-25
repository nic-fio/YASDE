# Stile delle finestre: "Vetro liquido" con controlli "Solo glifi"

Questo file è la specifica per disegnare le finestre del progetto. È indipendente dal toolkit grafico: i valori sono in pixel logici (prima della scala HiDPI) e in colori esadecimali o RGBA. Il blocco CSS in fondo è la fonte di verità: se un valore nel testo e quello nel CSS non coincidono, vale il CSS.

## Istruzioni per Claude Code

1. Prima di scrivere codice, individua nel repository come vengono disegnate oggi le finestre (toolkit, linguaggio, punto in cui si disegnano titlebar e bordo). Dimmi in poche righe quale file o modulo vuoi modificare.
2. Implementa lo stile descritto qui sotto in un solo punto centrale (un modulo o file "tema"), con i valori come costanti con nome. Non spargere numeri magici nel codice.
3. L'app è un'AppImage e deve funzionare su qualunque distribuzione e desktop. Segui la sezione "Compatibilità tra distribuzioni": lavora in due fasi, prima i livelli 0 e 1, poi (solo dopo che te lo confermo) il livello 2 con la sfocatura.
4. Alla fine confronta il risultato con la checklist in fondo. Se puoi, fai uno screenshot di una finestra su uno sfondo colorato e controlla i punti uno per uno.

## Aspetto generale

Una finestra di vetro smerigliato chiaro: pannello bianco traslucido che sfoca ciò che sta dietro, angoli molto arrotondati, un filo di luce sul bordo superiore e un'ombra morbida e lunga. Nessuna riga di separazione tra titlebar e contenuto. Il titolo è a sinistra, i tre controlli a destra, senza forma a riposo: si vedono solo i simboli.

## Finestra

| Elemento | Valore |
|---|---|
| Raggio degli angoli | 24 px |
| Riempimento | bianco al 58% di opacità, `rgba(255,255,255,0.58)` |
| Sfocatura dello sfondo | gaussiana con deviazione 26 px, più saturazione 1,6 |
| Bordo | 1 px dentro il riquadro, `rgba(255,255,255,0.70)` |
| Luce interna superiore | linea da 1 px lungo il bordo alto, che segue la curvatura degli angoli, `rgba(255,255,255,0.80)` |
| Ombra esterna | scostamento verticale 30 px, sfocatura 60 px, diffusione -20 px, `rgba(8,24,40,0.60)` |
| Contenuto | tagliato lungo gli angoli arrotondati |
| Colore del testo | `#10202E` (principale), `#2F4558` (secondario) |

Ordine di disegno: ombra esterna, sfocatura dello sfondo (limitata al rettangolo arrotondato), riempimento, bordo, luce interna, contenuto.

Nota sull'ombra: "diffusione -20 px" significa che l'ombra è più piccola della finestra di 20 px per lato, quindi si vede soprattutto sotto. Se il toolkit non supporta la diffusione negativa, disegna il rettangolo dell'ombra rimpicciolito di 20 px per lato e spostato in basso di 30 px, poi sfocalo di 60 px.

## Titlebar

- Altezza 50 px, senza linea di separazione dal contenuto.
- Margini interni: 22 px a sinistra, 14 px a destra.
- Titolo: Inter (o il carattere di sistema) peso 600, 13 px, colore `#10202E`, allineato a sinistra, centrato in verticale.
- Controlli: raggruppati a destra, centrati in verticale.
- Il resto della titlebar (tutto tranne i controlli) serve per trascinare la finestra. Doppio clic sul titolo: ingrandisci o ripristina.

## Controlli "Solo glifi"

Ordine da sinistra a destra: **minimizza, ingrandisci, chiudi**. Distanza tra i pulsanti 4 px.

Ogni pulsante: cerchio da 30 px, sfondo trasparente a riposo, simbolo centrato nel cerchio, colore `#10202E`.

Simboli, tutti con tratto da 1,5 px, estremità dritte, colore pieno `#10202E`:

| Controllo | Simbolo |
|---|---|
| Minimizza | linea orizzontale lunga 10 px |
| Ingrandisci | quadrato 10 × 10 px con bordo da 1,5 px, vuoto dentro, angoli vivi |
| Chiudi | due linee lunghe 13 px, ruotate di +45° e -45°, incrociate al centro |

Stati (transizione 0,15 s su sfondo, colore e scala):

| Stato | Sfondo del cerchio | Simbolo | Altro |
|---|---|---|---|
| Riposo | nessuno | `#10202E` | |
| Hover | `rgba(255,255,255,0.75)` | `#10202E` | luce interna 1 px bianca in alto, ombra `0 1px 3px rgba(8,24,40,0.20)` |
| Premuto | `rgba(255,255,255,0.40)` | `#10202E` | scala 0,92 |
| Chiudi, hover | `#E8604C` | `#FFFFFF` | stessa luce e ombra dell'hover normale |
| Chiudi, premuto | `#C94A38` | `#FFFFFF` | scala 0,92 |
| Focus da tastiera | | | anello da 2 px `#10202E`, distanziato di 2 px dal cerchio |

Il simbolo non cambia dimensione quando il pulsante viene premuto: si scala tutto il pulsante (cerchio e simbolo insieme).

Area cliccabile: almeno 30 × 30 px, centrata sul cerchio. Se è semplice, estendila in verticale a tutta l'altezza della titlebar (50 px) senza cambiare ciò che si vede.

## Parti non ancora disegnate (proposte da confermare con me)

Queste voci non fanno parte del design approvato. Implementale solo dopo avermi mostrato il risultato:

- **Finestra ingrandita:** raggio 0, niente ombra, niente bordo laterale. Il pulsante "ingrandisci" diventa "ripristina": due quadrati da 8 px con bordo da 1,5 px, il secondo spostato di 2 px in alto a destra e con il bordo interrotto dove il primo lo copre.
- **Finestra non attiva:** riempimento a `rgba(255,255,255,0.42)`, ombra più leggera, simboli a `rgba(16,32,46,0.45)`. I controlli restano cliccabili e tornano pieni all'hover.
- **Finestra senza ingrandimento o senza minimizzazione:** il pulsante resta al suo posto ma con simbolo al 35% di opacità e nessun effetto hover.

## Compatibilità tra distribuzioni (AppImage)

L'app deve funzionare su qualunque distribuzione e desktop, quindi nessun elemento essenziale può dipendere da funzioni presenti solo su alcuni sistemi. L'aspetto si adatta a tre livelli, scelti all'avvio in base a ciò che il sistema offre davvero:

| Livello | Quando | Aspetto |
|---|---|---|
| 0, Base | niente compositing (per esempio X11 senza compositore) | finestra opaca `#E9EEF1`, angoli quadrati o da 6 px, niente ombra trasparente; stessi controlli e stessa titlebar |
| 1, Vetro senza sfocatura | compositing disponibile, sfocatura no | riempimento quasi opaco `rgba(233,238,241,0.94)`; angoli 24 px, bordo, luce, ombra e controlli come da specifica |
| 2, Vetro completo | compositing e sfocatura disponibili (su Wayland, protocollo `ext-background-effect-v1` con capacità "blur" annunciata dal compositor) | riempimento `rgba(255,255,255,0.58)`, sfocatura 26 px e saturazione 1,6, come da specifica |

Regole:

- Il livello 1 è il riferimento: deve essere completo e bello da solo. Il livello 2 è un miglioramento facoltativo.
- Controlla le capacità a runtime, mai dal nome della distribuzione o del desktop. Se la richiesta di sfocatura non è disponibile o fallisce, scendi di livello senza errori e senza avvisi per l'utente.
- Il testo deve restare leggibile a ogni livello: non usare mai il riempimento al 58% senza la sfocatura.
- Tutto ciò che serve all'aspetto sta dentro l'AppImage: icone SVG dei controlli, CSS ed eventuale carattere (Inter). Non affidarti al tema icone o ai caratteri della distribuzione; come ripiego usa il carattere di sistema.
- Usa nel CSS solo funzioni supportate dalla versione minima di GTK che scegliamo. Dimmi quale versione minima serve e se conviene includere GTK nell'AppImage o usare quello del sistema, con i pro e i contro.
- Fase 1: livelli 0 e 1. Fase 2: livello 2, solo dopo la mia conferma.
- Prove: GNOME, KDE, un desktop leggero su X11 (per esempio XFCE) e un compositor wlroots. Per ognuno annota quale livello è stato ottenuto e allega uno screenshot.

## Checklist di controllo

- [ ] Angoli a 24 px, contenuto tagliato lungo la curva.
- [ ] Si vede lo sfondo sfocato attraverso la finestra (o è attivo il fallback dichiarato).
- [ ] Filo di luce visibile lungo il bordo superiore, anche negli angoli.
- [ ] Ombra morbida visibile soprattutto sotto la finestra.
- [ ] Titolo a sinistra, 13 px, peso 600, `#10202E`.
- [ ] Tre simboli a destra, nell'ordine minimizza, ingrandisci, chiudi, senza forme a riposo.
- [ ] Hover: cerchio chiaro dietro il simbolo; su "Chiudi" cerchio corallo e simbolo bianco.
- [ ] Premuto: il pulsante si rimpicciolisce leggermente e il colore si scurisce.
- [ ] Ho navigato con Tab: l'anello di focus si vede.
- [ ] Tutti i valori stanno in un solo modulo "tema", come costanti con nome.
- [ ] I livelli 0 e 1 funzionano senza sfocatura; il livello viene scelto a runtime e scende da solo se una funzione manca.
- [ ] Icone, CSS ed eventuale carattere sono dentro l'AppImage.

## Riferimento CSS (fonte di verità)

```css
.finestra {
  width: 400px;                       /* di esempio, la larghezza reale è libera */
  border-radius: 24px;
  background: rgba(255, 255, 255, 0.58);
  -webkit-backdrop-filter: blur(26px) saturate(1.6);
  backdrop-filter: blur(26px) saturate(1.6);
  border: 1px solid rgba(255, 255, 255, 0.7);
  box-shadow:
    inset 0 1px 0 rgba(255, 255, 255, 0.8),
    0 30px 60px -20px rgba(8, 24, 40, 0.6);
  overflow: hidden;
  color: #10202E;
}

.titlebar {
  height: 50px;
  padding: 0 14px 0 22px;
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.titolo { font-weight: 600; font-size: 13px; color: #10202E; }

.controlli { display: flex; align-items: center; gap: 4px; }

.btn {
  position: relative;
  width: 30px; height: 30px;
  border: 0; padding: 0; margin: 0;
  border-radius: 50%;
  background: none;
  color: #10202E;
  cursor: pointer;
  --g: 10px;      /* lato del simbolo */
  --t: 1.5px;     /* spessore del tratto */
  transition: 0.15s;
}
.btn::before, .btn::after {
  content: ""; position: absolute; left: 50%; top: 50%;
  transform: translate(-50%, -50%);
}
/* minimizza */
.min::before { width: var(--g); height: var(--t); background: currentColor; }
.min::after  { display: none; }
/* ingrandisci */
.max::before { width: var(--g); height: var(--g); border: var(--t) solid currentColor; box-sizing: border-box; }
.max::after  { display: none; }
/* chiudi */
.cls::before, .cls::after { width: calc(var(--g) * 1.3); height: var(--t); background: currentColor; }
.cls::before { transform: translate(-50%, -50%) rotate(45deg); }
.cls::after  { transform: translate(-50%, -50%) rotate(-45deg); }

/* stati */
.btn:hover {
  background: rgba(255, 255, 255, 0.75);
  box-shadow: inset 0 1px 0 #fff, 0 1px 3px rgba(8, 24, 40, 0.2);
}
.btn.cls:hover  { background: #E8604C; color: #fff; }
.btn:active     { background: rgba(255, 255, 255, 0.4); transform: scale(0.92); }
.btn.cls:active { background: #C94A38; color: #fff; }
.btn:focus-visible { outline: 2px solid #10202E; outline-offset: 2px; }
```
