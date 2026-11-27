# COMPITO — shell-sistema (F-06 notifiche, F-07 stato del telefono, F-08 volume; avvisi a comparsa)

Leggi prima: `CLAUDE.md`, `src/shell/CONTRATTI.md`, `src/shell/shell.h`, `servizi.h`, `pezzi.h`. Specifica §5.6, §5.7, §4.7, §5.8, §4.9, §5.11; mockup righe 213–225 (pulsanti nella barra), 248–284 (pannello notifiche, popup telefono, popup volume), 504–509 (toast), metodi `addNotif`, `showInfo`, `toastClick`, `hideNotif`, `hideAll`, `openNotif`, `togglePhoneScreen`, `togglePanel`, `onVol`, `toggleMute`, e in `renderVals` `volLabel`, `volText`, `mutePill*`, `notifItems`, `screenPill*`, `panelPill*`.

## Che cosa fa

`ph_sistema_installa(sh)` crea il pannello notifiche, il popup telefono, il popup volume (nascosti, in `PH_STRATO_PANNELLI`) e lo spazio dei toast (`PH_STRATO_TOAST`); registra `PhShellGestoreAvvisi` (toast di informazione) e `PhShellGestoreSistema` (i tre pulsanti per la barra, «nascosta»). Tutti i pannelli: classe `ph-pannello` (fondo `rgba(20,27,38,.98)`, bordo .14, raggio 14, imbottitura 14, ombra), titolo `ph-titolo-pannello`.

### Pulsanti per la barra (`pulsante(tipo)`)
Pulsanti `ph-tb` creati a richiesta (la barra li colloca); il sistema li aggiorna e al clic alterna il suo popup (`ph_shell_popup_alterna`); `aria-expanded` segue il popup (`PH_SHELL_EV_POPUP`).
- `PH_POPUP_VOLUME`: icona `PH_ICONA_VOLUME`, oppure `PH_ICONA_VOLUME_MUTO` se muto o volume 0; etichetta «Volume: muto» o «Volume: {percento}%».
- `PH_POPUP_TELEFONO`: icona della rete `PH_ICONA_WIFI` (il mockup non ha un'icona per la rete cellulare né per «nessuna rete»: usare la Wi-Fi con opacità .4 senza rete e la stessa Wi-Fi con rete cellulare; punto aperto riportato da chi coordina), icona batteria 22×20 con il riempimento interno proporzionale alla carica (il rettangolo pieno largo 11·carica/100 px a partire da x=4, nel sistema di coordinate dell'icona del mockup), testo «{percento}%»; etichetta «Stato del telefono». Stato non noto: niente testo e batteria vuota.
- `PH_POPUP_NOTIFICHE`: `PH_ICONA_CAMPANELLA` e, se ci sono notifiche visibili, il contatore: minimo 16×16, raggio 8, 11 px 600, testo #0b1220 su accento, imbottitura 0 4; etichetta «Notifiche».

### Notifiche (F-06)
- Fonte: `ph_srv_notifiche` (`PH_SRV_EV_NOTIFICHE`). **Nascondere vale solo nella shell**: insieme (chiave → ora_ms) delle nascoste; una notifica nascosta che si aggiorna (ora_ms diversa) torna visibile. `nascosta(chiave, ora)` del gestore risponde da qui; a ogni cambio si emette `PH_SHELL_EV_NASCOSTE`. Le nascoste di notifiche che non esistono più si dimenticano.
- **Pannello** (`PH_POPUP_NOTIFICHE`): a 8 px dal bordo destro e 56 px dal basso, largo 340, alto al massimo 460 (scorre), colonna con 10 px: intestazione «Notifiche» e a destra «Nascondi tutte» (`ph-ghost`); «Nessuna notifica» (13 px #cbd5e1, imbottitura 8 0) se non ce ne sono visibili; se il telefono non le può leggere (`problema`) mostrare quella frase al posto dell'elenco; ogni voce (`ph-voce`, riga con 10 px): icona 30×30 raggio 8 (`ph_srv_icona` del pacchetto o segnaposto 11 px 600), poi un pulsante largo che apre (etichetta «Apri {nome}», con titolo 13 px 600, testo #cbd5e1 12 px con 2 px sopra, sotto «{app} · {ora}» 11 px #9aa4b5 con 4 px sopra, dove app = nome dell'app da `ph_srv_app_cerca` o il pacchetto, ora = HH:MM locale di `ora_ms`), poi «Nascondi» (`ph-ghost`, etichetta «Nascondi la notifica»). NON c'è «Simula una nuova notifica» (solo mockup).
- Clic sulla notifica → la nasconde, `ph_shell_apri_app(pacchetto)`, chiude il popup.
- **Toast per le notifiche nuove**: a destra 16 px, 60 px dal basso, largo 300, `PH_STRATO_TOAST`; pulsante con fondo `rgba(20,27,38,.98)`, bordo 1 px `rgba(255,255,255,.18)`, raggio 12, imbottitura 12 14, ombra `0 14px 40px rgba(0,0,0,.55)`; titolo 13 px 600, testo 12 px #cbd5e1 con 2 px sopra. Dura 5,2 s; un toast nuovo sostituisce quello presente. Clic → apre l'app e nasconde la notifica. «Nuova» = chiave mai vista da quando il telefono è collegato; la PRIMA fotografia dopo ogni collegamento è solo la base e non dà toast (così non ci sono avvisi spuri alla prima lettura, A20). Una notifica aggiornata non dà toast (riappare nel pannello).
- **Avviso a comparsa del sistema** (5.6, preferenza `avvisi`): se la preferenza è vera e la finestra host non è visibile (`ph_shell_host_visibile` falso) o non è attiva, la notifica nuova va anche alle notifiche del desktop del PC con `GNotification` tramite `g_application_get_default()` (se non c'è un'applicazione, come nei demo, niente).
- **Toast di informazione** (gestore avvisi, `ph_shell_avviso(titolo, testo)`): stesso aspetto, dura 3,2 s, il clic lo chiude soltanto.

### Stato del telefono (F-07, §4.7)
Popup `PH_POPUP_TELEFONO`: 8 px da destra, 56 dal basso, largo 300, colonna con 8 px:
- «Telefono» (titolo); riga 12 px #cbd5e1 con 4 px sotto: «Android {versione} · Batteria {percento}% · {rete}» (versione da `api`: 34 → 14, 35 → 15, 36 → 16, poi +1 per livello; rete: «Wi-Fi connesso», «Rete cellulare», «Nessuna rete»);
- `ph-pbtn` «Mostra schermo del telefono» con pillola «Visibile»/«Nascosto» (`ph-pillola`, `ph-acceso` se visibile; `aria-pressed`): alterna `ph_shell_specchio(sh, !ph_shell_specchio_aperto(sh))` e chiude il popup (mockup);
- `ph-pbtn` «Pannello fisico» con sotto «Si spegne da solo mentre usi il PC» (11 px #9aa4b5) e pillola «Acceso»/«Spento»: `ph_srv_pannello(s, !acceso)`; la pillola segue lo stato vero (`PH_SRV_EV_TELEFONO`), non il clic.
- NON c'è «Simula blocco del telefono» (solo mockup).

### Volume (F-08)
Popup `PH_POPUP_VOLUME`: a 135 px da destra, 56 dal basso, largo 250, colonna con 10 px:
- intestazione «Volume» e a destra il valore 12 px #cbd5e1 («Muto» se muto o 0, altrimenti «{percento}%»);
- cursore 0–100 (`GtkScale` orizzontale, colore accento, etichetta «Volume di YASDE 2.2»): muovendolo si toglie il muto (mockup `onVol`);
- `ph-pbtn` «Muto» con pillola «Attivo»/«Disattivo» (`aria-pressed`);
- nota 11 px #9aa4b5, interlinea 1,4: «Regola l'audio di YASDE 2.2 sul PC. Il volume del telefono non cambia.»
- Semantica: guadagno lato PC (`ph_srv_imposta_volume`, `ph_srv_imposta_muto`; lo stato vero torna con `PH_SRV_EV_VOLUME`). Il muto non ferma la cattura. Persistenza: `volume` del profilo (il muto no), salvato con `ph_srv_profilo_salva` a cursore fermo (non a ogni passo).

## Che cosa NON fa
- I pulsanti «Simula…» del mockup.
- Scorciatoie da tastiera per il volume ([PROPOSTA] della specifica, non decisa).
- Non apre finestre: lo specchio lo apre finestra-app (`ph_shell_specchio`).

## Dipende da
`shell.h`, `servizi.h`, `config/lingua.h`.

## Criteri di accettazione
1. Misure, colori, testi come sopra; contatore = notifiche visibili.
2. Nascondi / Nascondi tutte agiscono solo nella shell; una notifica nascosta e poi aggiornata (finto: «Aggiorna l'ultima notifica») torna visibile; `PH_SHELL_EV_NASCOSTE` emesso.
3. Toast: notifica nuova (F7 nel demo) → toast 5,2 s che al clic apre l'app; nessun toast per le notifiche già presenti al collegamento né dopo una caduta e ritorno (F6).
4. Popup telefono: specchio e pannello come sopra; la pillola del pannello segue lo stato del telefono finto.
5. Volume: cursore, muto, icona e testo coerenti; il profilo riceve l'ultimo livello.
6. Uno solo dei popup aperto alla volta (gestore dei popup); Esc chiude.
7. Prova di logica (`prova_sistema.c`, senza schermo): insieme delle nascoste (nascondi, aggiorna, dimentica), notifiche nuove rispetto alla base (prima fotografia senza toast), testi del volume, versione Android da API, riempimento della batteria, testo «{app} · {ora}».
8. `strumenti/prova.sh shell-sistema` → `uscita 0`.

## Come si prova
- `strumenti/prova.sh shell-sistema`.
- Demo: `strumenti/demo-sul-tablet.sh sistema`. Senza la barra i tre pulsanti non hanno un posto: nel `demo.c` crea una barra minima (una `GtkBox` in `PH_STRATO_BARRA` in basso a destra) e mettici i tre `ph_shell_sistema_pulsante`; registra un gestore finestre finto che scrive su stderr l'app aperta e tiene lo stato dello specchio. Finestrella dei comandi: notifiche nuove/aggiornate/illeggibili, batteria, caduta.
