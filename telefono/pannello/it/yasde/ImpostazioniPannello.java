package it.yasde;

import java.io.IOException;

/**
 * Impostazioni di sistema del telefono (tabella «system») tramite il comando di shell di Android puro
 * `cmd settings` (stesso mezzo per il servizio e per il guardiano). Ogni scrittura si rilegge: se il
 * valore non e' quello chiesto e' un errore, mai un silenzio. Un'impostazione assente si legge «null».
 */
final class ImpostazioniPannello {
    static final String TIMEOUT_SCHERMO = "screen_off_timeout";
    /** Il massimo del timeout dello schermo (int): lo schermo non si spegne da solo. */
    static final int TIMEOUT_MASSIMO = Integer.MAX_VALUE;

    private ImpostazioniPannello() {}

    /** Valore attuale come testo, oppure «null» se l'impostazione non esiste. */
    static String leggi(String chiave) throws IOException {
        return esegui("get", chiave).trim();
    }

    /** Scrive e verifica con una rilettura. */
    static void scrivi(String chiave, String valore) throws IOException {
        esegui("put", chiave, valore);
        String letto = leggi(chiave);
        if (!letto.equals(valore)) {
            throw new IOException("impostazione " + chiave + " non scritta: voluto " + valore + ", letto " + letto);
        }
    }

    /** Cancella l'impostazione (torna «null», come se l'utente non l'avesse mai toccata). */
    static void rimuovi(String chiave) throws IOException {
        esegui("delete", chiave);
        String letto = leggi(chiave);
        if (!letto.equals("null")) throw new IOException("impostazione " + chiave + " non cancellata: letto " + letto);
    }

    /**
     * Riga di shell autonoma per il guardiano: rimette `originale` solo se l'impostazione ha ancora il
     * valore `nostro` (il valore dell'utente vince). Niente virgolette (il guardiano la racchiude tra
     * apici): `cmd settings get` stampa una sola riga, quindi il valore e' la prima parola.
     * `originale` null = l'impostazione non esisteva: si cancella. Valori solo numerici: nessun rischio
     * di iniezione nella riga.
     */
    static String comandoRipristino(String chiave, String nostro, String originale) {
        if (!nostro.matches("[0-9.]{1,12}") || (originale != null && !originale.matches("[0-9.]{1,12}"))) {
            throw new IllegalArgumentException("valore di impostazione non numerico");
        }
        String base = "cmd settings ";
        String rimetti = originale == null
            ? base + "delete system " + chiave
            : base + "put system " + chiave + " " + originale;
        return "set -- $(" + base + "get system " + chiave + " 2>&1 | tail -n 1); [ x$1 = x" + nostro + " ] && "
            + rimetti;
    }

    private static String esegui(String azione, String... resto) throws IOException {
        String[] cmd = new String[3 + 1 + resto.length];
        cmd[0] = "cmd";
        cmd[1] = "settings";
        cmd[2] = azione;
        cmd[3] = "system";
        System.arraycopy(resto, 0, cmd, 4, resto.length);
        return ShellPannello.esegui(10, cmd);
    }
}
