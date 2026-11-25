package it.yasde;

/**
 * Le righe di shell che il guardiano esegue per rimettere ciò che «trova il telefono» ha cambiato.
 * Ognuna rimette il valore dell'utente SOLO se l'impostazione ha ancora quello messo da noi (il valore
 * dell'utente vince, docs/protocollo.md §3). Righe autonome: niente variabili del servizio, niente apici
 * (il guardiano le racchiude già tra apici), solo numeri e parole fisse.
 */
final class ComandiShell {
    private ComandiShell() {}

    private static final String[] MODO_ANDROID = {"NORMAL", "VIBRATE", "SILENT"};
    private static final int[] VALORE_MODO = {2, 1, 0}; // valore di `mode_ringer` per i modi 0 suona, 1 vibra, 2 silenzioso
    private static final String[] FILTRO = {null, "all", "priority", "none", "alarms"};

    private static void controllaVolume(int v) {
        if (v < 0 || v > 1000) throw new IllegalArgumentException("volume fuori intervallo");
    }

    private static void controllaModo(int m) {
        if (m < 0 || m > 2) throw new IllegalArgumentException("modo della suoneria non valido");
    }

    private static void controllaFiltro(int f) {
        if (f < 1 || f > 4) throw new IllegalArgumentException("filtro del non disturbare non valido");
    }

    /** Il nome del modo per `cmd audio set-ringer-mode`. */
    static String nomeModo(int modo) {
        controllaModo(modo);
        return MODO_ANDROID[modo];
    }

    /** Il valore di `mode_ringer` (impostazioni globali) per un modo del protocollo. */
    static int valoreModo(int modo) {
        controllaModo(modo);
        return VALORE_MODO[modo];
    }

    /** L'argomento di `cmd notification set_dnd` per un filtro di Android. */
    static String argomentoFiltro(int filtro) {
        controllaFiltro(filtro);
        return FILTRO[filtro];
    }

    /** Volume della suoneria: rimesso a `originale` se è ancora `nostro` (`grep -q` sulla riga «volume is N in range»). */
    static String ripristinaVolume(int originale, int nostro) {
        controllaVolume(originale);
        controllaVolume(nostro);
        return "cmd media_session volume --stream 2 --get 2>&1 | grep -q volume.is." + nostro
            + ".in.range && cmd media_session volume --stream 2 --set " + originale;
    }

    /** Modo della suoneria: rimesso se `mode_ringer` ha ancora il valore del modo `nostro`. */
    static String ripristinaModo(int originale, int nostro) {
        controllaModo(originale);
        controllaModo(nostro);
        return "[ x$(cmd settings get global mode_ringer) = x" + VALORE_MODO[nostro]
            + " ] && cmd audio set-ringer-mode " + MODO_ANDROID[originale];
    }

    /** Non disturbare: rimesso al filtro `originale` se `zen_mode` è ancora 0 (spento, il nostro valore). */
    static String ripristinaNonDisturbare(int originale) {
        controllaFiltro(originale);
        if (originale == 1) throw new IllegalArgumentException("non c'è nulla da ripristinare: era spento");
        return "[ x$(cmd settings get global zen_mode) = x0 ] && cmd notification set_dnd " + FILTRO[originale];
    }
}
