package it.yasde;

/**
 * Chi puo' collegarsi al socket del servizio (parte pura, senza Android, cosi'
 * si prova sulla JVM).
 *
 * Il socket astratto del servizio e' raggiungibile, in linea di principio, da
 * qualunque processo del telefono: senza un controllo, un'app qualsiasi
 * potrebbe prendersi il canale dei comandi prima del PC o aprire il canale
 * dell'input e mandare tocchi. Il PC arriva sempre attraverso adb, cioe' da un
 * processo che gira come utente "shell" (2000), o come root (0) sui telefoni di
 * sviluppo: si accetta solo quello. Ogni altro utente (le app hanno uid da
 * 10000 in su) viene rifiutato subito dopo l'accept, prima di leggere un solo
 * byte.
 */
public final class Pari {
    /** Utente "shell" di Android: quello di adbd e dei comandi lanciati da adb. */
    public static final int UID_SHELL = 2000;
    /** Root: adbd gira cosi' solo sui telefoni di sviluppo. */
    public static final int UID_ROOT = 0;

    private Pari() {
    }

    /** true solo se chi si collega e' adb (shell) o root. */
    public static boolean ammesso(int uid) {
        return uid == UID_SHELL || uid == UID_ROOT;
    }
}
