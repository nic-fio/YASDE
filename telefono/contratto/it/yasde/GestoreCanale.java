package it.yasde;

import android.net.LocalSocket;

/** Gestisce un canale del protocollo (docs/protocollo.md §1). Gira sul thread del canale. */
public interface GestoreCanale {
    /** argomento: ciò che segue "prefisso:" nella riga di apertura, oppure "". */
    void gestisci(LocalSocket s, String argomento) throws Exception;
}
