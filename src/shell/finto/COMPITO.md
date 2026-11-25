# COMPITO — aggiunte al backend finto (23 nov pomeriggio, widget nuovi)

Il finto (questa cartella) deve realizzare le voci nuove di `servizi_backend.h` («Widget del 23 nov pomeriggio») così che il desktop si provi senza telefono:
- `tel_calendario_intervallo`: gli eventi finti del mockup ripetuti nei giorni feriali dell'intervallo (5 al giorno) più un «tutto il giorno» ogni domenica (mezzanotte UTC); pacchetto calendario `it.finto.calendario`; con il comando «Calendario illeggibile» → ERRORE.
- `tel_sveglia`: domani alle 07:30 (pacchetto `it.finto.orologio`); comando per «nessuna sveglia».
- `tel_batteria`: dal livello finto (78 che cala), in scarica, 31,5 °C, salute buona, 4100 mV, carica −1, autonomia 540 minuti; con «in carica» (comando nuovo): stato in carica, rete, carica completa tra 45 minuti, autonomia −1.
- `controlli_osserva`/`controllo_imposta`/`trova_avvia`/`trova_ferma` + `ph_servizi_pubblica_controlli`: stato in memoria (suona, non disturbare spento, torcia spenta); «trova» dura i secondi chiesti poi si ferma da solo (pubblica `trova_in_corso`); comandi della finestrella: «Torcia non disponibile» (puo_torcia falso), «Rifiuta i controlli» (ERRORE con una frase).
- Prove in `prova_finto.c` per ognuno (esiti, ERRORE, avvisi dei controlli solo mentre osservati, fine automatica di «trova»).

## Pizzico (23 nov)
Già fatto da chi ha scritto i contratti: `finto_flusso_pizzico` (scrive su stderr, conta i passi accettati: `ph_finto_pizzichi`), voce `flusso_pizzico` nella tabella, `ph_superficie_pizzico` nella superficie finta, prova in `prova_finto.c`. Se vuoi, nel disegno sintetico mostra un indicatore del pizzico (per esempio due puntini attorno al centro); non è richiesto.

## Meteo (23 nov sera)
Realizza `meteo_osserva` nel finto: senza nessuna rete, dopo 300 ms pubblica una previsione finta fissa e plausibile (adesso 14,5 °C poco nuvoloso di giorno, 48 ore e 7 giorni con codici vari: sereno, nuvoloso, pioggia, neve, temporale, nebbia; località «Città finta»), poi la ripubblica ogni 30 minuti; comandi nella finestrella: «Meteo non disponibile» (NON_DISPONIBILE con i dati vecchi e l'ora), «Posizione spenta» (SENZA_POSIZIONE con la frase), «Meteo lento» (resta IN_ATTESA 10 s). Con `false` smette subito. Prove in `prova_finto.c`: nessuna pubblicazione senza osservatori, contatore degli osservatori, stati d'errore.

## Meteo: città scelta a mano e ricerca (23 nov notte, CONTRATTI.md §18)
Nel pezzo comune ci sono già `ph_srv_meteo_cerca`, `ph_srv_meteo_localita`, `ph_srv_meteo_fonte`, `ph_servizi_pubblica_meteo_fonte`, `ph_servizi_lavoro_luoghi` (compilati ma NON provati da chi li ha scritti: le prove le fai tu qui). Nel finto:
- voce `meteo_cerca`: senza rete, dopo 300 ms risponde con un elenco fisso di luoghi finti che contengono il testo (senza distinzione di maiuscole): «Città finta» (Regione finta, Paese finto), «Città finta di sopra», «Borgo finto», … al massimo 10; testo «zzz» → 0 risultati; comando nella finestrella «Ricerca luoghi in errore» → ERRORE «Il servizio meteo non risponde.»; annullabile;
- voce `meteo_localita`: ricorda la città; con «Posizione spenta» attivo e una città scelta pubblica PRONTO con `localita` = nome della città e fonte `A_MANO` (`ph_servizi_pubblica_meteo_fonte`); senza città, SENZA_POSIZIONE come ora; con la posizione accesa fonte `TELEFONO` e località «Città finta».
- Prove in `prova_finto.c` (anche dei rami del pezzo comune): ricerca senza voce nel backend → ERRORE; testo di 1 lettera, solo spazi, più di 64 byte, lingua diversa da "it"/"en" → ERRORE senza chiamare il finto; 0 risultati → OK con v non NULL e n = 0; luoghi con nome vuoto o coordinate fuori limite scartati da `ph_servizi_lavoro_luoghi`; più di 10 → 10 validi (si scartano prima i non validi, poi si limita; al massimo 100 elementi esaminati); ricerca con il meteo non osservato → ERRORE «Aggiungi prima il widget Meteo.» senza chiamare il finto; annullata → nessuna chiamata; precedenza telefono/città con `ph_srv_meteo_fonte`; `ph_srv_meteo_localita(NULL)` torna a SENZA_POSIZIONE.

## Nota del 23 nov notte — coordinate
Nulla da cambiare: il finto usa solo `PhMeteoLuogo`, che resta in centesimi. (Le coordinate del telefono in millesimi non arrivano mai alla shell.)
