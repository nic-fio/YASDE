package it.yasde;

/**
 * Tutto ciò che il pezzo «controlli» chiede al telefono, in un punto solo: così la logica (Controlli, Trova)
 * si prova sulla JVM con un finto, e solo TelefonoAndroid conosce Android. Ogni metodo che non riesce lancia
 * un'eccezione: mai un valore inventato.
 *
 * Valori: modo della suoneria 0 suona, 1 vibra, 2 silenzioso (come il protocollo); filtro del non disturbare
 * come Android (1 tutto passa = spento, 2 solo priorità, 3 nessuna interruzione, 4 solo sveglie).
 */
interface Telefono {
    /** Il telefono rifiuta il comando (permesso mancante, comando assente): il suo bit sparisce da «comandi possibili». */
    final class NonPermesso extends Exception {
        private static final long serialVersionUID = 1L;

        NonPermesso(String messaggio) {
            super(messaggio);
        }
    }

    int modoSuoneria() throws Exception;

    void impostaModoSuoneria(int modo) throws Exception;

    int filtroNonDisturbare() throws Exception;

    void impostaFiltroNonDisturbare(int filtro) throws Exception;

    int volumeSuoneria() throws Exception;

    int volumeSuoneriaMassimo() throws Exception;

    void impostaVolumeSuoneria(int volume) throws Exception;

    /** Il volume che «trova il telefono» mette durante lo squillo: di norma il massimo (solo la prova sul telefono lo abbassa). */
    int volumeTrova(int massimo);

    /** 0 spenta, 1 accesa; eccezione se lo stato non è noto. */
    int torcia() throws Exception;

    void impostaTorcia(boolean accesa) throws Exception;

    /** Chiede se nel frattempo e' arrivato l'ordine di fermare (lo implementa chi ha lanciato «trova»). */
    interface Fermata {
        boolean richiesta();
    }

    /**
     * Fa partire suoneria in ciclo e vibrazione; se non riesce non lascia nulla in corso. Subito prima di far
     * partire il suono controlla `fermata`: se l'arresto e' gia' arrivato non suona e lancia.
     */
    void avviaSuono(Fermata fermata) throws Exception;

    /** Ferma suono e vibrazione; non lancia mai, si può chiamare a vuoto. */
    void fermaSuono();

    /**
     * Il Non disturbare attivo e' manuale e senza scadenza (spegnerlo e rimetterlo con `set_dnd` lo lascia
     * identico). false se viene da una regola automatica o ha una scadenza; eccezione se non si riesce a
     * stabilirlo: chi chiama tratta «non so» come false.
     */
    boolean nonDisturbareManualeSenzaScadenza() throws Exception;

    /** Esiste il comando `cmd notification set_dnd` (dalla sua guida, senza eseguire nulla che cambi). */
    boolean haComandoNonDisturbare();

    /** C'è una fotocamera con flash e la torcia di questo telefono si spegne da sola con il processo. */
    boolean haTorcia();

    /** Esistono TUTTI i comandi di ripristino che il guardiano userebbe (volume, modo della suoneria, non disturbare). */
    boolean haComandiRipristino();
}
