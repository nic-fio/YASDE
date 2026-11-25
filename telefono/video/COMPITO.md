# COMPITO — aggiunta a telefono-video: PIZZICO (0x55, 23 nov)

Leggi `docs/protocollo.md` §2.4 (riga 0x55 e il paragrafo «PIZZICO»: è il contratto) e `CLAUDE.md` («Difetti già visti»). In `Input`/`Iniettore`: nuovo tipo 0x55 sul canale `input`.
- Geometria, fasi, divisione dei passi lunghi in movimenti da ≤30 a ~8 ms, scadenza di 400 ms (un timer sul thread di iniezione; nessuna attesa senza limite), regole con TOCCO in corso (vince l'ultimo: l'altro si chiude con CANCEL o con le dita alzate), pausa e cambio di misura (annulla), valori fuori limite: come scritto nel protocollo.
- `MotionEvent.obtain` con due `PointerProperties` (tipo dito) e `PointerCoords`, sorgente `InputDevice.SOURCE_TOUCHSCREEN`, `setDisplayId` dello schermo virtuale della sessione, tempi coerenti (`downTime` del gesto, `eventTime` crescente). Le dita restano sempre dentro lo schermo.
- Alla chiusura della sessione e all'arresto del servizio un gesto aperto si chiude (nessun dito «rimasto giù»).
Prove sulla JVM: la geometria pura (distanza iniziale, moltiplicazione per 1,25 ogni 120, limiti 5–90%, spostamento del centro vicino ai bordi, divisione dei passi), la macchina delle fasi (inizio senza fine → chiusura a 400 ms; TOCCO durante un pizzico e viceversa). Sul telefono (con il turno): Maps o Foto in una finestra, un pizzico avanti e uno indietro.
