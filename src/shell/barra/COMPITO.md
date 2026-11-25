# COMPITO — shell-barra (F-03)

Leggi prima: `CLAUDE.md`, `src/shell/CONTRATTI.md`, gli header `src/shell/shell.h`, `servizi.h`, `pezzi.h`. Specifica §5.3 (F-03), §5.11; mockup `Main.dc.html` righe 192–227 (barra) e `renderVals` → `taskItems`.

## Che cosa fa

`ph_barra_installa(sh)` crea la barra in basso e la aggiunge a `PH_STRATO_BARRA` (`halign FILL`, `valign END`). È installata per ultima (pezzi.h).

Aspetto (mockup):
- Contenitore: altezza 48 px, fondo `rgba(15,23,32,.95)` (`--ph-barra`), bordo superiore 1 px `rgba(255,255,255,.1)`, imbottitura 0 8 px, spazio 4 px tra gli elementi, allineati al centro in verticale.
- Pulsanti: classe `ph-tb` di `base.css` (alti 40, minimo 40 di larghezza, raggio 8, sopra `rgba(255,255,255,.14)`).

Da sinistra a destra (§5.3):
1. **Launcher**: icona `PH_ICONA_LAUNCHER`, etichetta accessibile «Launcher delle app», `aria-expanded` = popup `PH_POPUP_LAUNCHER` aperto (`ph_shell_espanso`, aggiornato su `PH_SHELL_EV_POPUP`). Clic → `ph_shell_launcher_alterna`.
2. **Separatore**: classe `ph-separatore` (1×24, margini 0 4 px).
3. **App aperte**: una voce per finestra interna, nell'ordine di `ph_shell_finestre` (rilette a ogni `PH_SHELL_EV_FINESTRE`):
   - quadratino 26×26 raggio 7: l'icona `ph_srv_icona` (riletta a `PH_SRV_EV_ICONA`) oppure `ph_shell_segnaposto_nuovo(pacchetto, titolo, 26, 7)` (iniziali 11 px 600); per lo specchio: segnaposto «Tl» colore #475569 (mockup: `isPhone`);
   - indicatore sotto: 16×3 raggio 2, a 2 px dal fondo, centrato: attiva → accento; aperta → #9aa4b5; minimizzata → trasparente con bordo 1 px #9aa4b5;
   - voce attiva: fondo `rgba(255,255,255,.14)`; minimizzata: opacità .65;
   - etichetta accessibile: «{nome} (aperta)» o «{nome} (in pausa)»; suggerimento: il nome, o «{nome} · in pausa, stream sospeso» se minimizzata (testi già in `data/en/shell-comune.txt`);
   - clic → `ph_shell_clic_barra(sh, id)` (la logica minimizza/ripristina/porta davanti è di finestra-app).
4. **Spazio flessibile**.
5. **Area di sistema**, in ordine:
   - **Invia file al telefono**: `PH_ICONA_INVIA`, etichetta «Invia file al telefono», suggerimento «Invia file al telefono (PC verso telefono)», `aria-expanded` = `PH_POPUP_INVIO` aperto; clic → `ph_shell_trasferimento_apri(sh, PH_SRV_INVIO)`; **puntino** 7×7 tondo color accento in alto a destra (6 px dal bordo alto e destro del pulsante) se `ph_srv_trasferimenti_attivi(s, PH_SRV_INVIO)` (riletto a `PH_SRV_EV_TRASFERIMENTI`);
   - **Scarica file dal telefono**: come sopra con `PH_ICONA_SCARICA`, «Scarica file dal telefono», «Scarica file dal telefono (telefono verso PC)», `PH_POPUP_RICEZIONE`, `PH_SRV_RICEZIONE`;
   - separatore;
   - **Volume**, **Stato del telefono**, **Notifiche**: i tre pulsanti li dà il pezzo sistema con `ph_shell_sistema_pulsante(sh, PH_POPUP_VOLUME / PH_POPUP_TELEFONO / PH_POPUP_NOTIFICHE)`; la barra li colloca e basta (NULL = posto vuoto);
   - **Orologio**: «HH:MM» (ore e minuti a due cifre, ora locale del PC), 12 px peso 500, colore #e6eaf2, imbottitura 0 16 px 0 10 px; si aggiorna allo scattare di ogni minuto (non con un intervallo fisso di 30 s). Non è un pulsante.

## Che cosa NON fa

- Non apre popup propri: launcher, trasferimento e sistema aprono i loro.
- Non decide nulla sulle finestre (solo `ph_shell_clic_barra`).
- Niente app «preferite» fisse: il mockup ne mostra (`fav`), la specifica dice «una voce per finestra» e non c'è persistenza per i preferiti → punto aperto per chi coordina; per ora solo le finestre aperte.
- Niente scorciatoie da tastiera proprie.

## Dipende da

`shell.h` (strati, popup, gestori, icone, accessibilità), `servizi.h` (icone, trasferimenti attivi), `config/lingua.h`. Nient'altro.

## Criteri di accettazione

1. Ordine, misure e colori come sopra; tutti i pulsanti hanno etichetta accessibile e (dove il mockup ha `title`) suggerimento.
2. Aprendo/chiudendo/minimizzando finestre (gestore finto nel demo) le voci si aggiornano entro un giro del ciclo, con lo stato giusto dell'indicatore.
3. Il puntino di invio/scarica compare con un trasferimento in corso o in attesa e sparisce a coda vuota o ferma.
4. `aria-expanded` dei pulsanti launcher/invio/scarica segue il popup aperto.
5. L'orologio cambia al minuto esatto (tolleranza 1 s).
6. Prova di logica (`prova_barra.c`, senza schermo) per le funzioni pure: stato dell'indicatore e testi da `PhShellFinestra` (aperta/attiva/minimizzata/specchio), millisecondi al prossimo minuto, puntino da un elenco di trasferimenti.
7. `strumenti/prova.sh shell-barra` → `uscita 0` (anche la compilazione `-O3` che segue).

## Come si prova

- `strumenti/prova.sh shell-barra` (logica + CSS; la prova con schermo risulta saltata finché nel container non c'è broadway).
- Demo: `strumenti/demo-sul-tablet.sh barra`. Nel `demo.c` (cartella tua) registra un gestore finestre finto (`ph_shell_registra_finestre`) con 3 finestre e un gestore sistema finto con tre pulsanti semplici, così da vedere tutte le voci; F5–F8 comandano il telefono finto; aggiungi un tasto per accodare un trasferimento finto (`ph_srv_tr_accoda` di un file esistente) e vedere il puntino.

## Nota del 23 nov (richieste del committente, PIANO.md «Richieste del committente dopo la prima prova», punti 3-6)
- **App fissate** (punto 3): registra `PhShellGestoreFissate` (`ph_shell_registra_fissate`, shell.h). L'elenco è nel profilo del telefono: `PhCfgTelefono.fissate[]`/`n_fissate` (massimo `PH_CFG_MAX_FISSATE` = 16) con le funzioni di config `ph_cfg_telefono_app_fissata`, `ph_cfg_telefono_fissa_app` (-1 = nome non valido o elenco pieno: avviso a comparsa), `ph_cfg_telefono_togli_app`. Lo tocchi solo tu, su `ph_srv_profilo(s)`: dopo ogni cambio `ph_srv_profilo_salva` e `ph_shell_emetti(sh, PH_SHELL_EV_FISSATE)`; a `PH_SRV_EV_PROFILO` rileggi ed emetti lo stesso avviso. Sulla barra: prima le fissate, nell'ordine dell'elenco (una fissata aperta mostra lo stato della sua finestra; una non aperta: icona senza indicatore, clic → `ph_shell_apri_app`), poi le altre finestre aperte. Il telefono finto ha nel profilo due app fissate: Messaggi e Calcolatrice.
- **Menu del clic destro** su un'icona (punto 4): popup `PH_POPUP_MENU_BARRA` (stile `ph-menu`, voci `ph-riga`, sopra l'icona): «Chiudi app» se l'app ha una finestra (`ph_shell_chiudi_finestra(sh, id)`), poi «Fissa sulla barra» / «Togli dalla barra» (testi già in `data/en/shell-comune.txt`). Esc e clic fuori lo chiudono (gestore dei popup).
- **Volume e orologio più grandi** (punto 5): coerenti con gli altri elementi della barra (stessa altezza dei pulsanti `ph-tb`, icona 20 px come le altre, testo dell'orologio leggibile alla stessa scala); il pulsante del volume è di sistema: chiedi al suo programmatore o scrivilo nel rapporto se serve un cambio là.
- **Calendario** (punto 6): l'orologio diventa un pulsante (etichetta accessibile: l'ora e la data) che alterna il popup `PH_POPUP_CALENDARIO` con un `GtkCalendar` del mese corrente (data del PC, nessun dato del telefono), sopra la barra all'altezza dell'orologio, stile `ph-pannello`.
- **Tipi di popup**: usa `PH_POPUP_MENU_BARRA` per il menu del clic destro e `PH_POPUP_CALENDARIO` per il calendario (non più `PH_POPUP_MENU`); l'avviso «app fissate cambiate» è `PH_SHELL_EV_FISSATE`.
