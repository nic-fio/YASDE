# COMPITO — telefono-controlli (classe `Controlli`, tipi 0x80–0x8f)

Leggi: `CLAUDE.md` (anche «Difetti già visti», regole del telefono e del guardiano), `docs/protocollo.md` §2.10 (IL contratto) e §3, `telefono/contratto/` (`Registro`, `GestoreComandi`, `Guardiano`), come esempio di pezzo con guardiano `telefono/pannello/`, e `telefono/audio/it/yasde/NascosteAudio.java` (usi catturati dall'audio: la suoneria di «trova» NON deve esserlo). Java 8, solo API di Android puro, uid shell, niente root, niente installato.

## Che cosa fa
- `Controlli.registra()`: `Registro.registraComandi(0x80, 0x8f, …)`, chiusura con `Registro.registraChiusura` (ferma «trova» e rimette i valori; spegne la torcia se l'ha accesa YASDE).
- 0x80 CONTROLLI_STATO: stato di suoneria (`AudioManager.getRingerMode`), non disturbare (`NotificationManager.getCurrentInterruptionFilter`: ALL = spento, altro = acceso), torcia (callback `CameraManager.TorchCallback`), «trova» in corso, e i bit dei comandi possibili, calcolati alla prima richiesta: suoneria (prova di `setRingerMode` non necessaria: SecurityException al primo uso = bit a 0 da allora), non disturbare (esiste `cmd notification set_dnd`: la guida di `cmd notification` lo elenca), torcia (c'è una fotocamera con flash e `setTorchMode` non ha dato SecurityException), trova (esistono TUTTI i comandi di ripristino: `cmd media_session volume`, `cmd audio set-ringer-mode`, `cmd notification set_dnd`, verificati dalle guide, senza eseguire nulla che cambi).
- 0x81/0x82/0x83: comandi dell'utente. Suoneria e non disturbare: effetto voluto e permanente, NESSUN ripristino (eccezione documentata nel protocollo). Torcia: accesa da YASDE → spenta alla chiusura ordinata; se il servizio muore il sistema la spegne (da verificare: vedi sotto; se non è così la torcia deve risultare «non disponibile»).
- 0x84 TROVA_AVVIA: per ogni modifica temporanea PRIMA `Guardiano.imposta` (420 volume suoneria, 421 modo suoneria, 422 non disturbare; comando che rimette il valore dell'utente solo se c'è ancora il nostro), poi la modifica; `imposta` false → nessuna modifica e `ERRORE`. Suoneria predefinita (`RingtoneManager.getActualDefaultRingtoneUri(TYPE_RINGTONE)`, se assente quella di sveglia) in ciclo con `AudioAttributes.USAGE_NOTIFICATION_RINGTONE`, più vibrazione; fermata al limite (5–120 s) o con 0x85; poi valori rimessi e ripristini tolti SOLO dopo averli rimessi. Un'azione di ripristino fallita si ritenta (regola del telefono), non si abbandona.
- 0x8e CONTROLLI_CAMBIATI: dopo la prima 0x80, a ogni cambio (anche fatto a mano sul telefono; controllo al più ogni 2 s se non c'è un avviso puro), e alla fine di «trova».
- Mai nei log valori dell'utente oltre ai nomi dei modi.

## Da verificare sul telefono (sola lettura; li lancia chi coordina) prima di scrivere
- `adb shell cmd notification help | grep -i dnd` (c'è `set_dnd`?)
- `adb shell cmd audio help | grep -i ringer` (c'è `set-ringer-mode`?)
- `adb shell cmd media_session help | grep -i volume` (c'è `volume --stream`?)
- `adb shell dumpsys package com.android.shell | grep -E "MODIFY_AUDIO_SETTINGS|ACCESS_NOTIFICATION_POLICY|MANAGE_NOTIFICATIONS|CAMERA|STATUS_BAR"` (permessi dell'uid shell)
- `adb shell dumpsys media.camera | grep -i -E "torch|flash"` (fotocamere con flash; dopo una prova della torcia: lo stato dopo `kill -9` del servizio)

## Prove
- Prove sulla JVM (`prove.sh`, file `.java.txt`) della codifica di 0x80/0x8e e della macchina di «trova» (ordine: guardiano prima, valori rimessi, ripristini tolti dopo; `imposta` false → niente modifica; limite di tempo) con finti di AudioManager e Guardiano.
- Prova sul telefono (script in `prove/`, con il turno): stato, suoneria avanti e indietro (rimessa com'era a fine prova), torcia accesa/spenta, «trova» 5 s; `kill -9` del servizio durante «trova» → volume, modo e non disturbare tornano come erano.

## Per chi coordina
Da aggiungere fuori da questa cartella (non sono del pezzo): `it.yasde.Controlli` in `Servizio.PEZZI` (`telefono/base`) e `controlli` nell'elenco predefinito di `telefono/costruisci.sh`.

## Lavoro futuro: «trova il telefono» (SPENTO nel jar di produzione, `Controlli.TROVA_ABILITATO = false`)
Scoperte sul telefono di prova (prove mute del 23 nov):
1. `AudioTrack` non si crea dal processo shell («Cannot create AudioTrack»); prima `MediaPlayer`/`Ringtone` facevano morire il processo con una eccezione di sicurezza dei loro thread interni (pacchetto «android» per l'uid 2000).
2. Il passaggio al modo «suona» fa alzare da Android il volume salvato da 0 a 1 senza ritorno: con il volume salvato a 0 in vibra (`volume_ring_speaker`, `volume_ring_earpiece`) tornare a vibra lascia 1. Il volume letto dopo il cambio di modo risulta già 1 e non si può rimettere a 0 col solo ripristino «volume».
Opzioni per i volumi salvati a 0:
- leggere PRIMA del cambio di modo i `volume_ring*` salvati (`cmd settings list system`) e rimetterli dopo (da verificare che Android li accetti);
- non passare mai da «suona» se il volume salvato è 0 e dichiarare «trova» non disponibile in quel caso;
- ripristinare con un comando che riporti davvero a 0 (da verificare).
Da aggiungere al finto JVM (`prove/ProvaControlliJvm.java.txt`): la regola «al passaggio a suona un volume salvato 0 diventa 1», con prove da vibra e da silenzioso con volume salvato 0. Per il suono serve inoltre un modo di produrlo che funzioni dall'uid shell. Altri punti noti: guardiano che lascia il file dopo `kill -9` (verificare con `base`), formato di `dumpsys notification --zen` per Non disturbare a tempo e regola automatica attiva (mai visti).
