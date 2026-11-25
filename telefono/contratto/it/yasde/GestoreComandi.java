package it.yasde;

import java.io.DataInputStream;

/** Gestisce le richieste di un intervallo di tipi sul canale comandi. */
public interface GestoreComandi {
    /** Ritorna il corpo dell'OK (può essere vuoto). Un'eccezione diventa ERRORE con il suo messaggio. */
    byte[] gestisci(int tipo, DataInputStream corpo) throws Exception;
}
