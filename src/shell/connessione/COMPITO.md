# COMPITO — shell-connessione (prima connessione)

Leggi prima: `CLAUDE.md` (anche «Difetti già visti»), `src/shell/CONTRATTI.md` (§15 e le Decisioni di chi coordina), `connessione.h` (il contratto da realizzare), `src/shell/servizi.h` (in fondo: «Prima connessione»; stati del collegamento all'inizio), `src/shell/shell.h` (`ph_shell_connessione_da_mostrare`, strati, accessibilità). Sotto i servizi: `src/collegamento/collegamento.h` (stati CERCO, NON_ABBINATO, `ph_col_abbina`), `src/scoperta/scoperta.h`, `src/abbinamento/abbinamento.h` — solo da leggere: tu parli SOLO con i servizi. Specifica §1.5 (ADB wireless), §2.4 («abbinamento con codice»), §3.3 (stati), §4.8; `PIANO.md` («Non iniziati: prima connessione»). Il mockup NON ha questa schermata: aspetto e testi qui sotto sono una proposta che il committente deve ancora vedere (le foto servono a quello).

## Che cosa fa
`ph_connessione_nuova(sh)` crea la schermata (a tutta area, opaca) che l'ossatura mette in `PH_STRATO_VELO` e mostra/nasconde con `ph_shell_connessione_da_mostrare`. La schermata si aggiorna da sola a `PH_SRV_EV_COLLEGAMENTO`.

### Aspetto (proposta, finché il committente non decide)
- Fondo: lo sfondo predefinito del mockup (sfumatura 135°, `#0e2638` → `#1a625f`); al centro una carta come i dialoghi del mockup: classe `ph-dialogo` (fondo `rgba(14,21,31,.98)`, bordo `rgba(255,255,255,.16)`, raggio 16, imbottitura 20, ombra), larga 480 px (al massimo la radice − 48), colonna con 12 px.
- In alto il titolo dello stato (`ph-titolo-dialogo`, 16 px 600), sotto il testo (13 px, `#cbd5e1`, a capo, interlinea ~1,4), poi i contenuti dello stato, in fondo i pulsanti (`ph-accento` per l'azione principale, `ph-ghost ph-bordato` per le altre).
- Attesa: `GtkSpinner` 16 px accanto alla riga «Cerco…» o «Abbinamento in corso…» (12 px `#9aa4b5`).
- Copre anche la barra (non c'è nulla da fare senza telefono). La barra del titolo della finestra principale resta (è dell'ossatura).

### Che cosa mostra in ogni stato (dati da `ph_srv_collegamento`)
1. **CERCO, `telefoni_noti == 0`** (mai collegato un telefono): titolo «Collega il telefono»; testo in passi numerati:
   «1. Sul telefono apri Impostazioni › Opzioni sviluppatore e attiva Debug wireless.» «2. Telefono e PC devono essere sulla stessa rete Wi-Fi.» «3. Quando il telefono compare qui ti chiederà il codice di abbinamento.»; riga di attesa con lo spinner «Cerco telefoni in rete…»; pulsante «Cerca di nuovo» (`ph-ghost ph-bordato`) → `ph_srv_riconnetti_ora`.
2. **CERCO, `telefoni_noti > 0`** (telefono già abbinato ma non raggiungibile): titolo «Cerco il telefono»; il `motivo` del collegamento (già una frase per l'utente) e sotto «Controlla che il telefono sia acceso, sulla stessa rete Wi-Fi e con Debug wireless attivo.»; spinner; «Cerca di nuovo». Se `scelta_arbitraria`: riga in più «Ci sono più telefoni in rete: uso il primo trovato.» (12 px `#9aa4b5`).
3. **NON_ABBINATO** (il telefono è in rete ma non riconosce questo PC): titolo «Abbina il telefono»; testo «Sul telefono: Opzioni sviluppatore › Debug wireless › Associa dispositivo con codice. Scrivi qui il codice a sei cifre che compare.»; campo del codice (`ph-campo`, 24 px, cifre a spaziatura fissa, centrato, largo ~200 px, solo cifre, al massimo 6; etichetta accessibile «Codice di abbinamento»; Invio = «Abbina»); pulsante «Abbina» (`ph-accento`, attivo solo con `ph_srv_codice_valido`) → `ph_srv_abbina(s, codice, NULL, 0, annulla, …)`; durante l'attesa (`ph_srv_abbinamento_in_corso`) campo e pulsante inattivi, spinner con «Abbinamento in corso…»; esito FALLITO/SCADUTA: la frase dell'esito (o il `motivo` del collegamento) in rosso chiaro `#fca5a5` 12 px sotto il campo, campo svuotato e di nuovo attivo; RIFIUTATA: la stessa cosa con la frase «Riprova tra un momento.»; RIUSCITO: «Abbinato: collego il telefono…» con lo spinner (lo stato passa a CERCO e poi COLLEGATO, e l'ossatura toglie la schermata). Sotto, un espansore chiuso «Il telefono non compare? Scrivi indirizzo e porta» con un campo «Indirizzo IP e porta» (come li mostra il telefono sotto il codice, es. `192.168.1.20:37099`): se compilato, «Abbina» usa `ph_srv_abbina(s, codice, host, porta, …)` (separare host e porta sull'ultimo «:»; IPv6 tra parentesi quadre; porta 1–65535; non valido = pulsante inattivo). In fondo «Cerca di nuovo» (`ph_srv_riconnetti_ora`: riprova senza abbinare).
4. **PERSO** (solo se mai collegato in questa esecuzione): titolo «Connessione persa» (testo già tradotto), il `motivo`, «Cerca di nuovo».
5. **COLLEGATO, BLOCCATO, CHIUSO**: nulla da mostrare (l'ossatura la nasconde); se resta visibile per un istante, mostra l'ultimo contenuto senza pulsanti attivi.

Testi: «Connessione persa» e «Riconnetti ora» sono già in `data/en/shell-comune.txt`; gli altri testi qui sopra sono nuovi: aggiungili con la traduzione in `data/en/shell-connessione.txt` (dopo aver controllato con grep che non esistano già).

### Comportamento
- Una sola richiesta di abbinamento alla volta (lo garantisce già il pezzo comune: la seconda è RIFIUTATA); un `GCancellable` per la richiesta in corso, annullato nel dispose e quando la schermata viene nascosta.
- Il codice non si conserva: si legge dal campo al clic, si passa a `ph_srv_abbina` (che lo copia) e il campo si svuota subito dopo l'esito. Mai il codice né l'indirizzo nei log o su disco; niente `g_print`/`fprintf` di quei campi neanche per prova.
- Tastiera: il fuoco va al campo del codice quando entra lo stato NON_ABBINATO; Tab tra campo, «Abbina», espansore, «Cerca di nuovo»; Esc non fa nulla qui (non è un popup).
- Accessibilità: etichette accessibili su campo e pulsanti; la riga d'errore con ruolo `GTK_ACCESSIBLE_ROLE_ALERT` (annunciata).

## Che cosa NON fa
- Nessuna scoperta propria, nessun elenco dei telefoni in rete (i nomi mDNS contengono il numero di serie e il collegamento sceglie da solo: specifica 1.5 «un telefono attivo alla volta»), nessuna scelta tra più telefoni.
- Nessun accesso a `telefoni.toml`, alla chiave, ad ADB.
- Non decide quando apparire (ossatura) e non tocca il velo.
- Niente cavo USB (ripiego non in perimetro della shell).

## Dipende da
`connessione.h`, `servizi.h`, `shell.h`, `config/lingua.h`.

## Criteri di accettazione
1. Ogni stato come descritto, con i testi giusti; la schermata copre il desktop e la barra.
2. Con il telefono finto (`demo-connessione`, parte non abbinato): codice `999999` → errore e campo svuotato; `000000` → la frase «Apri sul telefono…»; `123456` → «Abbinato: collego il telefono…» e poi la schermata sparisce (il demo la nasconde come farà l'ossatura).
3. `--stato cerca-nuovo`: passi numerati e spinner; dalla finestrella dei comandi «Telefono visibile in rete» → si passa a «Abbina il telefono». `--stato cerca-noto`: «Cerco il telefono» con il motivo; «Telefono visibile in rete» → la schermata sparisce.
4. «Abbina» inattivo finché il codice non ha 6 cifre; indirizzo manuale non valido → inattivo.
5. Nessun codice né indirizzo nei log del demo (`grep` sull'uscita di `foto-demo.sh`).
6. Chiudere il demo durante «Abbinamento in corso…» non lascia funzioni di ritorno in giro (AddressSanitizer pulito).
7. Prova di logica (`prova_connessione.c`, senza schermo): scelta di titolo/testi/pulsanti per ogni stato (dati `PhSrvCollegamento` finti), filtro del campo del codice (solo cifre, massimo 6), lettura di «indirizzo:porta» (IPv4, `[IPv6]:porta`, porta fuori intervallo, spazi), testo dell'errore secondo l'esito.
8. Foto con `strumenti/foto-demo.sh connessione` per i tre stati principali (`-- --stato non-abbinato`, `-- --stato cerca-nuovo`, `-- --stato cerca-noto`), da guardare e descrivere nel rapporto (sono quelle che vedrà il committente).
9. `strumenti/prova.sh shell-connessione` → `uscita 0` (anche la compilazione `-O3`).

## Come si prova
- `strumenti/prova.sh shell-connessione`.
- Demo: `strumenti/demo-sul-tablet.sh connessione` o, senza disturbare lo schermo, `strumenti/foto-demo.sh connessione`. Opzioni del demo: `--stato non-abbinato` (predefinito nel demo di questo pezzo), `cerca-noto`, `cerca-nuovo`, `collegato`; `--comandi` per la finestrella (pulsante «Telefono visibile in rete»). Per le foto di stati che richiedono un'azione (errore del codice, abbinamento in corso) aggiungi al tuo `demo.c` una sequenza a tempo (per esempio `--mostra errore`).
