package it.yasde;

/**
 * Pagine di un'app che il PC puo' chiedere (docs/protocollo.md §2.2). L'elenco e' fisso: il PC
 * manda un numero, mai un intent; qui il numero diventa un'azione standard di Android. Parte
 * pura (nessuna API di Android), provata sulla JVM.
 */
final class Pagine {
    static final int NESSUNA = 0, BATTERIA = 1, MEMORIA = 2;

    private Pagine() {}

    /**
     * Azione dell'intent per la pagina; null per NESSUNA (avvio normale). Una pagina fuori
     * elenco lancia IllegalArgumentException: il chiamante la rifiuta prima di toccare nulla.
     */
    static String azione(int pagina) {
        switch (pagina) {
            case NESSUNA:
                return null;
            case BATTERIA:
                return "android.intent.action.POWER_USAGE_SUMMARY";
            case MEMORIA:
                return "android.settings.INTERNAL_STORAGE_SETTINGS";
            default:
                throw new IllegalArgumentException("pagina non valida");
        }
    }

    /** Come azione(), ma per VIDEO_PAGINA, dove la pagina 0 non ha senso: errore anche per lei. */
    static String azioneRichiesta(int pagina) {
        String a = azione(pagina);
        if (a == null) throw new IllegalArgumentException("pagina non valida");
        return a;
    }
}
