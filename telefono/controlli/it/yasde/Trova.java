package it.yasde;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * «Trova il telefono» (docs/protocollo.md §2.10): fa squillare la suoneria a volume udibile anche se il
 * telefono è in silenzioso, finché il PC non ferma o scade il limite.
 *
 * Cambia SOLO per il tempo dello squillo: non disturbare (se acceso, lo spegne), modo della suoneria
 * (lo mette su «suona») e volume della suoneria. Per ognuno, PRIMA della modifica, il suo ripristino nel
 * guardiano (`Guardiano.imposta`: se torna false la modifica non si fa e tutto ciò che era già stato fatto
 * si annulla); il ripristino si toglie dal guardiano SOLO dopo aver rimesso davvero il valore. Ordini del
 * guardiano 420 volume, 421 modo, 422 non disturbare: il volume si rimette per primo.
 *
 * Una sola sessione alla volta, con un thread suo che fa TUTTO (preparazione, attesa, ripristino): nessun
 * lock è tenuto mentre si parla con il telefono. Qualunque cosa muoia (anche con Throwable) il thread
 * rimette i valori nel `finally`. Un ripristino che non riesce si ritenta (`tentativi`, `pausaMs`); se non
 * riesce mai resta registrato nel guardiano e lo eseguirà lui se il servizio muore.
 *
 * Quando la sessione finisce (comunque) chiama `alFine`, fuori da ogni lock: può chiamare `avvia`/`ferma`
 * solo dopo che la sessione è già staccata (cioè qui dentro è possibile ripartire).
 */
final class Trova {
    static final String NOME_VOLUME = "trova-volume";
    static final String NOME_MODO = "trova-modo";
    static final String NOME_NON_DISTURBARE = "trova-non-disturbare";
    static final int ORDINE_VOLUME = 420;
    static final int ORDINE_MODO = 421;
    static final int ORDINE_NON_DISTURBARE = 422;

    private static final String NON_DISTURBARE_AUTOMATICO =
        "Il Non disturbare e' attivo da una regola automatica o a tempo: spegnilo prima di cercare il telefono";
    private static final String NON_DISTURBARE_INCERTO =
        "Non riesco a stabilire da dove viene il Non disturbare attivo: spegnilo prima di cercare il telefono";
    private static final String NON_AL_SICURO = "impossibile mettere al sicuro il ripristino: il telefono non si tocca";

    private static final int IN_AVVIO = 0;
    private static final int ATTIVA = 1;
    private static final int FINENDO = 2;

    /** Un valore del telefono cambiato per lo squillo, col modo di rimetterlo. */
    private abstract static class Voce {
        final String nome;
        final int ordine;
        final String comandoGuardiano;
        final int originale;
        final int nostro;

        Voce(String nome, int ordine, String comandoGuardiano, int originale, int nostro) {
            this.nome = nome;
            this.ordine = ordine;
            this.comandoGuardiano = comandoGuardiano;
            this.originale = originale;
            this.nostro = nostro;
        }

        abstract int leggi() throws Exception;

        abstract void scrivi(int valore) throws Exception;

        /**
         * Rimette il valore dell'utente solo se c'è ancora il nostro (se l'utente l'ha già cambiato o è già
         * com'era, non c'è nulla da fare); poi rilegge: se non è tornato com'era è un errore (si ritenta).
         */
        void rimetti() throws Exception {
            if (leggi() != nostro) return;
            scrivi(originale);
            if (leggi() != originale) throw new IOException("valore non rimesso: " + nome);
        }
    }

    private final class Sessione implements Runnable {
        final CountDownLatch pronto = new CountDownLatch(1);
        final long durataMs;
        Thread thread;
        // protetti da `s`
        int stato = IN_AVVIO;
        long limiteNs;
        boolean fermare;
        // scritto prima di pronto.countDown()
        Throwable erroreAvvio;

        Sessione(long durataMs) {
            this.durataMs = durataMs;
            this.limiteNs = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(durataMs);
        }

        @Override
        public void run() {
            List<Voce> fatte = new ArrayList<>();
            boolean suono = false;
            try {
                try {
                    prepara(fatte);
                    controllaFermare();
                    t.avviaSuono(new Telefono.Fermata() {
                        @Override
                        public boolean richiesta() {
                            synchronized (s) {
                                return fermare;
                            }
                        }
                    });
                    suono = true;
                } catch (Throwable e) {
                    // fallimento a metà: si annulla ciò che era già stato fatto PRIMA di dire ERRORE
                    annulla(fatte);
                    erroreAvvio = e;
                    synchronized (s) {
                        stato = FINENDO;
                        if (corrente == this) corrente = null; // chi riceve l'errore può subito riprovare
                    }
                    pronto.countDown();
                    return;
                }
                synchronized (s) {
                    stato = ATTIVA;
                }
                pronto.countDown();
                aspetta();
            } catch (Throwable e) {
                System.out.println("[controlli] trova il telefono interrotto: " + e);
            } finally {
                synchronized (s) {
                    stato = FINENDO;
                }
                if (suono) {
                    try {
                        t.fermaSuono();
                    } catch (Throwable e) {
                        System.out.println("[controlli] suono non fermato: " + e);
                    }
                }
                annulla(fatte);
                synchronized (s) {
                    if (corrente == this) corrente = null;
                }
                pronto.countDown(); // se non era ancora partito (Throwable in preparazione prima dei latch)
                try {
                    alFine.run();
                } catch (Throwable e) {
                    System.out.println("[controlli] avviso di fine non riuscito: " + e);
                }
            }
        }

        private void aspetta() throws InterruptedException {
            synchronized (s) {
                while (!fermare) {
                    long resto = limiteNs - System.nanoTime();
                    if (resto <= 0) return;
                    TimeUnit.NANOSECONDS.timedWait(s, resto);
                }
            }
        }

        private void controllaFermare() {
            synchronized (s) {
                if (fermare) throw new IllegalStateException("ricerca fermata prima di suonare");
            }
        }

        private void applica(List<Voce> fatte, Voce v) throws Exception {
            controllaFermare();
            // il ripristino PRIMA della modifica; se il guardiano non lo prende, nessuna modifica
            if (!g.imposta(v.nome, v.ordine, v.comandoGuardiano)) throw new IllegalStateException(NON_AL_SICURO);
            fatte.add(v);
            controllaFermare(); // se nel frattempo e' arrivato un stop non si scrive: annulla() rimette cio' che c'e'
            v.scrivi(v.nostro);
        }

        private void prepara(List<Voce> fatte) throws Exception {
            int filtro = t.filtroNonDisturbare();
            int modo = t.modoSuoneria();
            if (filtro != 1) {
                // spegnerlo e poi rimetterlo con `set_dnd` lo trasforma in un Non disturbare manuale senza
                // scadenza: se viene da una regola automatica o ha una scadenza (o non si sa) non si tocca nulla
                boolean semplice;
                try {
                    semplice = t.nonDisturbareManualeSenzaScadenza();
                } catch (Exception e) {
                    throw new IOException(NON_DISTURBARE_INCERTO);
                }
                if (!semplice) throw new IllegalStateException(NON_DISTURBARE_AUTOMATICO);
                applica(fatte, new Voce(NOME_NON_DISTURBARE, ORDINE_NON_DISTURBARE, ComandiShell.ripristinaNonDisturbare(filtro), filtro, 1) {
                    @Override
                    int leggi() throws Exception {
                        return t.filtroNonDisturbare();
                    }

                    @Override
                    void scrivi(int v) throws Exception {
                        t.impostaFiltroNonDisturbare(v);
                    }
                });
            }
            if (modo != 0) {
                applica(fatte, new Voce(NOME_MODO, ORDINE_MODO, ComandiShell.ripristinaModo(modo, 0), modo, 0) {
                    @Override
                    int leggi() throws Exception {
                        return t.modoSuoneria();
                    }

                    @Override
                    void scrivi(int v) throws Exception {
                        t.impostaModoSuoneria(v);
                    }
                });
            }
            // Il volume si legge SOLO ORA, con il modo gia' su «suona»: con la suoneria silenziata Android
            // risponde 0 qualunque sia il volume dell'utente, e rimettere quello 0 gli cancellerebbe il volume
            // (e in modo «suona» un volume 0 fa passare Android a «vibra»).
            int volume = t.volumeSuoneria();
            int massimo = t.volumeSuoneriaMassimo();
            int bersaglio = t.volumeTrova(massimo);
            if (volume != bersaglio) {
                applica(fatte, new Voce(NOME_VOLUME, ORDINE_VOLUME, ComandiShell.ripristinaVolume(volume, bersaglio), volume, bersaglio) {
                    @Override
                    int leggi() throws Exception {
                        return t.volumeSuoneria();
                    }

                    @Override
                    void scrivi(int v) throws Exception {
                        t.impostaVolumeSuoneria(v);
                    }
                });
            }
        }
    }

    private final Telefono t;
    private final Guardiano g;
    private final Runnable alFine;
    private final int tentativi;
    private final long pausaMs;
    private final long attesaMs;

    private final Object s = new Object();
    private Sessione corrente; // protetto da s
    private boolean chiusa; // protetto da s
    private int attese; // avvii in attesa dell'esito di una sessione in preparazione; protetto da s

    /**
     * `tentativi` e `pausaMs`: quante volte e con che pausa si ritenta un ripristino che fallisce.
     * `attesaMs`: quanto aspettano `avvia` (la preparazione) e `ferma`/`chiudi` (il ripristino) al massimo.
     */
    Trova(Telefono t, Guardiano g, Runnable alFine, int tentativi, long pausaMs, long attesaMs) {
        this.t = t;
        this.g = g;
        this.alFine = alFine;
        this.tentativi = Math.max(1, tentativi);
        this.pausaMs = pausaMs;
        this.attesaMs = attesaMs;
    }

    boolean inCorso() {
        synchronized (s) {
            return corrente != null && corrente.stato != FINENDO;
        }
    }

    /**
     * Avvia lo squillo per al massimo `durataMs`; già in corso: riparte il limite. Torna solo quando lo
     * squillo suona (o lancia: in quel caso il telefono è com'era, tranne se un ripristino non è riuscito e
     * resta nel guardiano). Eccezioni: NonPermesso (il telefono rifiuta), le altre per qualunque altro motivo.
     */
    void avvia(long durataMs) throws Exception {
        Sessione nuova;
        Sessione attesa; // quella di cui aspettare l'esito: la nostra, o una ancora in avvio
        synchronized (s) {
            if (chiusa) throw new IllegalStateException("servizio in chiusura");
            Sessione c = corrente;
            if (c != null) {
                if (c.stato == FINENDO) throw new IllegalStateException("la ricerca precedente sta finendo: riprova tra poco");
                c.limiteNs = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(durataMs);
                s.notifyAll();
                nuova = null;
                attesa = c.stato == IN_AVVIO ? c : null;
                if (attesa != null) attese++;
            } else {
                nuova = new Sessione(durataMs);
                nuova.thread = new Thread(nuova, "yasde-trova");
                nuova.thread.setDaemon(true);
                corrente = nuova;
                nuova.thread.start();
                attesa = nuova;
                attese++;
            }
        }
        if (attesa == null) return;
        nuova = attesa;
        try {
            if (!nuova.pronto.await(attesaMs, TimeUnit.MILLISECONDS)) {
                ferma();
                throw new IOException("il telefono non risponde: ricerca annullata");
            }
        } finally {
            synchronized (s) {
                attese--;
            }
        }
        Throwable e = nuova.erroreAvvio;
        if (e == null) return;
        if (e instanceof Exception) throw (Exception) e;
        throw new IllegalStateException("errore imprevisto: " + e);
    }

    /** Solo per le prove: quanti avvii stanno aspettando l'esito di una preparazione. */
    int attese() {
        synchronized (s) {
            return attese;
        }
    }

    /** Ferma lo squillo e rimette i valori; torna a ripristino finito (o dopo `attesaMs`). OK anche se non era in corso. */
    void ferma() {
        Sessione c;
        synchronized (s) {
            c = corrente;
            if (c == null) return;
            c.fermare = true;
            s.notifyAll();
        }
        attendi(c);
    }

    /** Come `ferma`, e da qui `avvia` risponde errore. */
    void chiudi() {
        Sessione c;
        synchronized (s) {
            chiusa = true;
            c = corrente;
            if (c != null) {
                c.fermare = true;
                s.notifyAll();
            }
        }
        if (c != null) attendi(c);
    }

    private void attendi(Sessione c) {
        if (Thread.currentThread() == c.thread) return;
        try {
            c.thread.join(attesaMs);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    /** Rimette le voci in ordine inverso; ognuna si ritenta; si toglie dal guardiano solo dopo averla rimessa. */
    private void annulla(List<Voce> fatte) {
        for (int i = fatte.size() - 1; i >= 0; i--) {
            Voce v = fatte.get(i);
            boolean fatto = false;
            for (int k = 0; k < tentativi && !fatto; k++) {
                try {
                    v.rimetti();
                    fatto = true;
                } catch (Throwable e) {
                    System.out.println("[controlli] ripristino di " + v.nome + " non riuscito (" + (k + 1) + "): " + e);
                    if (k + 1 < tentativi) pausa();
                }
            }
            if (fatto) {
                try {
                    g.togli(v.nome);
                } catch (Throwable e) {
                    System.out.println("[controlli] ripristino di " + v.nome + " non tolto dal guardiano: " + e);
                }
            } else {
                System.out.println("[controlli] " + v.nome + " non rimesso: resta nel guardiano");
            }
        }
        fatte.clear();
    }

    private void pausa() {
        try {
            Thread.sleep(pausaMs);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }
}
