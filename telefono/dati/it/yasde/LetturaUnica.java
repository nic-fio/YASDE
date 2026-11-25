package it.yasde;

/**
 * Esegue una lettura che puo' bloccarsi (provider, servizio di sistema) su un thread a parte, con una
 * scadenza, e AL PIU' UNA ALLA VOLTA: se la lettura precedente e' ancora viva (e' scaduta ma non e'
 * ancora tornata) non ne parte un'altra, cosi' i thread bloccati non si accumulano. Parte pura (solo
 * java.*), provata sulla JVM.
 */
final class LetturaUnica<T> {
    interface Lavoro<T> {
        T esegui() throws Exception;
    }

    /** Una lettura precedente e' ancora in corso. */
    static final class Occupata extends IllegalStateException {
        private static final long serialVersionUID = 1L;

        Occupata(String frase) {
            super(frase);
        }
    }

    /** La lettura non e' finita entro la scadenza. */
    static final class Scaduta extends IllegalStateException {
        private static final long serialVersionUID = 1L;

        Scaduta(String frase) {
            super(frase);
        }
    }

    /** La lettura e' fallita: la causa e' l'eccezione del lavoro. */
    static final class Fallita extends RuntimeException {
        private static final long serialVersionUID = 1L;

        Fallita(Throwable causa) {
            super(causa);
        }
    }

    private final String nome, fraseOccupata, fraseScaduta;
    private Thread filo;

    LetturaUnica(String nome, String fraseOccupata, String fraseScaduta) {
        this.nome = nome;
        this.fraseOccupata = fraseOccupata;
        this.fraseScaduta = fraseScaduta;
    }

    @SuppressWarnings("unchecked")
    T esegui(final Lavoro<T> lavoro, long scadenzaMs) throws InterruptedException {
        final Object[] esito = new Object[2];   // [0] valore, [1] errore
        final Thread t;
        synchronized (this) {
            if (filo != null && filo.isAlive()) throw new Occupata(fraseOccupata);
            t = new Thread(nome) {
                @Override
                public void run() {
                    try {
                        esito[0] = lavoro.esegui();
                    } catch (Throwable e) {
                        esito[1] = e;
                    }
                }
            };
            t.setDaemon(true);
            filo = t;
            t.start();
        }
        t.join(scadenzaMs);   // fuori dal lock: un'altra richiesta riceve subito Occupata
        if (t.isAlive()) throw new Scaduta(fraseScaduta);
        if (esito[1] != null) throw new Fallita((Throwable) esito[1]);
        return (T) esito[0];
    }
}
