# COMPITO — telefono/base

## Aggiunta del 23 nov sera — segni di vita del PC (protocollo §2.1)
- `Protocollo`: costante `PRESENZA = 0x05`. `Servizio`: risponde `OK` vuoto a PRESENZA (come a SALUTO, gestita dal servizio stesso).
- Per OGNI messaggio letto dal canale `comandi` (SALUTO compreso, tipi sconosciuti compresi), PRIMA di smistarlo: `Registro.segnoDiVita(SystemClock.elapsedRealtime())`.
- Prove JVM: ogni tipo di messaggio aggiorna il segno di vita (con un orologio passato da fuori), gli ascoltatori sono chiamati, un ascoltatore che lancia non ferma il canale; PRESENZA → OK vuoto.
