# COMPITO — aggiunte a telefono-dati (23 nov pomeriggio)

Nuovi tipi 0x7b CALENDARIO_INTERVALLO, 0x7c SVEGLIA, 0x7d BATTERIA (`docs/protocollo.md` §2.6, righe e note). Nella classe `Dati` (e `NascosteDati` se servono API nascoste): estendere `registraComandi` per coprirli.
- 0x7b: come CALENDARIO (stesso provider, stessi calendari visibili, stesse regole di riservatezza: mai titoli nei log), ma sull'intervallo [da, a) chiesto (eventi che si sovrappongono), massimo 1–500, `u8` troncato, pacchetto dell'app Calendario risolto con `ACTION_MAIN` + `CATEGORY_APP_CALENDAR` (vuoto se nessuna). Argomenti non validi → `ERRORE`.
- 0x7c: `AlarmManager.getNextAlarmClock()`; pacchetto da `getShowIntent().getCreatorPackage()`.
- 0x7d: `ACTION_BATTERY_CHANGED` (registerReceiver con ricevitore null) + `BatteryManager.computeChargeTimeRemaining()`; autonomia −1 se non c'è un'API pubblica (non stimarla).
Prove sulla JVM della codifica dei tre OK con valori finti; sul telefono (con il turno) una lettura di ciascuno.
Da verificare (sola lettura, li lancia chi coordina): `adb shell dumpsys alarm | grep -i -A2 "next alarm clock"`, `adb shell dumpsys battery`.

## Aggiunta del 23 nov sera — POSIZIONE (0x90, docs/protocollo.md §2.11)
Registra anche l'intervallo 0x90–0x9f. POSIZIONE: `getLastKnownLocation` sui fornitori abilitati (la più recente), arrotondamento al multiplo più vicino di 0,005° PRIMA di codificare (millesimi interi, multipli di 5: decisione del committente, PIANO.md), età, fornitore; con «richiesta permessa» e nessuna posizione o più vecchia di 6 ore: UNA `getCurrentLocation` sul fornitore di rete con limite 10 s (mai gps, mai aggiornamenti continui); località con `Geocoder` solo se presente, limite 2 s. Posizione spenta / permesso negato / nessuna posizione: OK con l'esito. MAI coordinate o località nei log (nemmeno nelle eccezioni: messaggi generici).
Da verificare (sola lettura, li lancia chi coordina; nessuna coordinata stampata):
`adb shell dumpsys package com.android.shell | grep -i location` · `adb shell cmd location is-location-enabled` · `adb shell dumpsys location | grep -c -i "last location"` (solo il conteggio).
Prove sulla JVM: arrotondamento (positivi, negativi, ±0,005, poli e antimeridiano), codifica dell'OK per ogni esito.

## Nota del 23 nov notte — località
Il committente vuole il nome della località nel widget: `NascosteDati.LOCALITA_ABILITATA` deve restare `true` (lo è). Nient'altro cambia nel telefono: nessun consenso da gestire qui (il PC chiede la posizione solo mentre un widget Meteo è sul desktop). La città scritta a mano è tutta sul PC.

## Nota del 23 nov notte — POSIZIONE risponde subito (protocollo §2.11)
0x90 risponde SEMPRE subito con l'ultima posizione nota (o «nessuna») e la località già in memoria, più il campo nuovo in coda `u8` ricerca avviata. Se il PC permette e serve (nessuna posizione o più vecchia di 6 ore, freno di 10 minuti, nessuna ricerca già in corso), AVVIA su un thread la `getCurrentLocation` di rete (10 s) e, alla posizione, il Geocoder (2 s) e risponde 1; risultato tenuto in memoria accanto alla posizione (mai su disco, mai nei log). La località di una posizione nota ma senza nome si calcola nello stesso lavoro in sottofondo, con le stesse condizioni. Il thread in sottofondo che muore per un `Throwable` non lascia nulla di aperto; alla chiusura del servizio si ferma. Prove JVM: risposta immediata (nessuna attesa dentro la richiesta), campo ricerca avviata 0/1 nei vari casi, freno, una sola ricerca alla volta.

## Nota del 23 nov notte — arrotondamento a 0,005° (circa 500 m)
Decisione del committente: `LogicaPosizione` arrotonda al multiplo PIÙ VICINO di 0,005° (a metà lontano da zero), non più a 0,01°; sul filo i campi `i32` diventano MILLESIMI di grado (gradi × 1000, sempre multipli di 5; limiti ±90000 e ±180000). L'arrotondamento resta il PRIMO passo: anche ciò che si tiene in memoria e ciò che va al Geocoder è già arrotondato. Rinomina `centesimi(...)` (es. `millesimi5(...)`) e aggiorna le prove JVM: 0,0024 → 0; 0,0025 → 5; −0,0025 → −5; 45,4567 → 45455; 90 → 90000; −179,999 → −180000; i valori codificati sono sempre multipli di 5. Nulla su disco né nei log.
