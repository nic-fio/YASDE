package it.yasde;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;

/**
 * Smista un messaggio del canale comandi (docs/protocollo.md §2.1). Parte senza classi Android
 * (l'ora e il saluto arrivano da fuori), così si prova sulla JVM.
 */
public final class Smistatore {
    /** Orologio monotono in ms (sul telefono SystemClock.elapsedRealtime). */
    public interface Orologio {
        long ora();
    }

    /** Costruisce il corpo dell'OK a SALUTO (versione, misure, modello: dipende da Android). */
    public interface Saluto {
        byte[] rispondi(byte[] corpo) throws Exception;
    }

    private final Orologio orologio;
    private final Saluto saluto;

    public Smistatore(Orologio orologio, Saluto saluto) {
        this.orologio = orologio;
        this.saluto = saluto;
    }

    /**
     * Ritorna il corpo dell'OK; un'eccezione diventa ERRORE (la frase è il messaggio).
     * Per OGNI messaggio, anche sconosciuto o malformato, PRIMA di smistarlo si annota il segno
     * di vita del PC: Registro.segnoDiVita non tiene lock e non lascia uscire eccezioni degli ascoltatori.
     */
    public byte[] tratta(Protocollo.Messaggio m) throws Exception {
        Registro.segnoDiVita(orologio.ora());
        if (m.tipo == Protocollo.SALUTO) return saluto.rispondi(m.corpo);
        if (m.tipo == Protocollo.PRESENZA) {
            if (m.corpo.length != 0) throw new IllegalArgumentException("PRESENZA non vuole corpo");
            return new byte[0];
        }
        if (m.tipo == Protocollo.CHIUDI) return new byte[0];
        GestoreComandi g = Registro.comandi(m.tipo);
        if (g == null) throw new IllegalArgumentException("tipo sconosciuto");
        return g.gestisci(m.tipo, new DataInputStream(new ByteArrayInputStream(m.corpo)));
    }
}
