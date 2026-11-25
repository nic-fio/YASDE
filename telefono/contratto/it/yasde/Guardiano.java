package it.yasde;

/** Il guardiano che ripristina il telefono (docs/protocollo.md §3). Thread-safe. */
public interface Guardiano {
    /**
     * Registra il comando shell che annulla una modifica. Ordine più basso = eseguito prima.
     * Ritorna true solo se il ripristino è davvero al sicuro (l'elenco che il
     * guardiano eseguirà è stato scritto): con false chi chiama NON deve fare
     * la modifica, perché se il servizio morisse nessuno la annullerebbe.
     */
    boolean imposta(String nome, int ordine, String comandoShell);

    /** Toglie un ripristino che non serve più (la modifica è già stata annullata). */
    void togli(String nome);
}
