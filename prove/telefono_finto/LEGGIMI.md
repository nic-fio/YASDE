# Telefono finto

Strumento di prova (non entra nell'app): si comporta come il demone ADB di un telefono con il debug wireless acceso, cosi' i pezzi del PC si provano senza telefono vero.

## Come si lancia

    python3 telefono_finto.py --cartella DIR [--porta N] [--non-abbinato]

Ascolta su 127.0.0.1 (porta 0 = scelta dal sistema), stampa su stdout `PORTA <n>` e resta in ascolto. Servono Python 3 (solo libreria standard, modulo `ssl`) e il comando `openssl` per il certificato. Il log va su stderr.

`prova.py` e' la prova di se' stesso (client Python: TLS, due canali insieme, un comando, un file nei due sensi, un socket Unix, un PC non abbinato). Su server: `strumenti/prova.sh scoperta`.

## Cosa simula

- Handshake: CNXN, STLS, TLS 1.3 lato server (chiede il certificato del client, non lo verifica), poi il CNXN del telefono (versione 0x01000001, senza checksum, funzioni `shell_v2,stat_v2,ls_v2,sendrecv_v2,fixed_push_mkdir`).
- Piu' canali insieme (OPEN/OKAY/WRTE/CLSE); ogni WRTE verso il client aspetta il suo OKAY prima del successivo.
- `shell,v2,raw:<comando>` e `shell,v2:<comando>`: `sh -c` dentro DIR, stdout/stderr/codice con il protocollo shell v2 (stdin e chiusura dello stdin compresi).
- `sync:` con STAT, STA2, LST2, LIST, LIS2, SEND, SND2, RECV, RCV2. I percorsi del telefono stanno sotto DIR (`/sdcard/x` e' `DIR/sdcard/x`); `..` e collegamenti simbolici non portano mai fuori da DIR.
- Dopo ogni `FAIL` del sync il canale si chiude, come fa adbd; durante un SEND prima si scartano i dati fino al `DONE` e poi si chiude.
- `localabstract:<nome>`: inoltra al socket Unix `DIR/socket/<nome>` se esiste, altrimenti rifiuta l'OPEN.
- `--non-abbinato`: chiude subito dopo il TLS, come un telefono che non conosce il PC.

## Cosa non simula

- Nessuna compressione nel sync (nessuna funzione `sendrecv_v2_*` annunciata), niente `DRY_RUN`, niente collegamenti simbolici in SEND.
- Nessun abbinamento con codice, nessun servizio `shell:` v1, `exec:`, `forward`, `reverse`, `cmd`, `abb`, `tcpip:`.
- I comandi shell vedono DIR come directory corrente ma non rimappano i percorsi assoluti (`/sdcard` dentro uno script e' quello del sistema vero).
- Nessuna pseudo-console (`pty`), nessun ridimensionamento finestra, nessun ACK ritardato.
- I socket Unix devono stare in `DIR/socket/` (nomi senza `/`).
