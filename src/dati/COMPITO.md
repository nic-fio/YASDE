# COMPITO — aggiunte a dati (23 nov pomeriggio)

Realizza le dichiarazioni nuove di `dati.h` (sezioni «Widget aggiunti il 23 nov» e «Controlli rapidi»), seguendo `docs/protocollo.md` §2.6 (0x7b–0x7d) e §2.10 (0x80–0x85, evento 0x8e):
- `ph_dati_calendario_intervallo` / `_libera`, `ph_dati_sveglia` / `_libera`, `ph_dati_batteria`: richieste come le altre, risposte trattate come ostili (lunghezze e conteggi contro il buffer e i tetti, stringhe UTF-8 ripulite e troncate), argomenti validati prima di mandare.
- `ph_dati_controlli`, i cinque comandi, `ph_dati_ascolta_controlli` (ascolto di 0x8e con `ph_componente_ascolta`; `ph_dati_chiudi` lo toglie; regole degli avvisi come 0x7e/0x7f; chiusura in parallelo: «Difetti già visti»).
- Mai titoli né pacchetti nei log o nei messaggi d'errore.
Prove nel telefono finto di `src/dati/prova/` per ogni messaggio (byte attesi, risposte troncate, valori fuori intervallo, avviso 0x8e e sua rimozione con un avviso in corso).

## Aggiunta del 23 nov sera — `ph_dati_posizione` (0x90)
Realizza `ph_dati_posizione` (dati.h, «Posizione approssimativa»): risposta ostile (esito fuori elenco → SCONOSCIUTA; latitudine fuori ±9000 o longitudine fuori ±18000 centesimi → SCONOSCIUTA; località UTF-8 ripulita e troncata). Mai coordinate o località nei log o nei messaggi d'errore. Prove nel telefono finto (byte attesi, ogni esito, valori fuori intervallo).

## Nota del 23 nov notte
Il contratto di `ph_dati_posizione` non cambia: è caduto solo il «dopo il consenso» (ora: solo mentre un widget Meteo è sul desktop, lo decide la shell). La località del telefono finisce nel titolo del widget: fino a 127 byte ripuliti, mai nei log.

## Nota del 23 nov notte — POSIZIONE (0x90) cambiata
Corpo dell'OK: `u8` esito, `i32` lat, `i32` lon, `u32` età, `u8` fornitore, `stringa` località, `u8` ricerca avviata (17 + n byte: la `stringa` ha 2 byte di lunghezza). Nuovo campo `ricerca_avviata` in `PhDatiPosizione`. Valori fuori misura (esito, coordinate, fornitore, ricerca avviata non 0/1, località non UTF-8 o > 127 byte, coordinate/località non zero con esito ≠ NOTA, lunghezza sbagliata) = -1, NON più SCONOSCIUTA. La chiamata non dura più fino a 14 s. Aggiorna le prove nel telefono finto (ogni caso fuori misura = -1, il campo nuovo, il corpo vecchio di 18 + n byte = -1).

## Nota del 23 nov notte — coordinate in millesimi
`PhDatiPosizione.lat_centesimi/lon_centesimi` diventano `lat_millesimi/lon_millesimi` (dati.h): stesso posto nel corpo, `i32` in millesimi di grado. La lettura fallisce (-1) per latitudine fuori ±90000, longitudine fuori ±180000 o un valore NON multiplo di 5. Aggiorna `nuovi.c` e le prove (valori validi, estremi esatti, di 5 oltre il limite, non multipli di 5 come 45457).
