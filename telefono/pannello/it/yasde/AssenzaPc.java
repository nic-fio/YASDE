package it.yasde;

/**
 * Regola dell'assenza del PC (docs/protocollo.md §2.9), parte pura: l'orologio (monotono, ms, lo stesso di
 * Registro.segnoDiVita) e l'ultimo segno di vita arrivano da fuori, le azioni dietro l'interfaccia Azioni.
 * Non e' thread-safe: la chiama il controllo del pannello sotto la sua serratura, una volta al secondo.
 *
 * Passati PC_ASSENTE_MS (esatti: un ms prima non basta) senza segni di vita il telefono torna quello di tutti i
 * giorni, in quest'ordine: riaccendi il pannello se l'aveva spento lui, ripristina la frequenza minima, rimetti il
 * timeout dello schermo dell'utente, poi si manda l'evento «PC assente». Ogni passo riuscito non si rifa: se uno
 * fallisce (o lancia, anche un Error) si ferma li' e si ritenta al giro dopo, senza saltare l'ordine e senza
 * mandare l'evento prima della fine. Il wake lock non e' un passo: lo rilascia la guardia di TieniSveglio con la
 * stessa scadenza, e il controllo chiama prima questa regola, poi la guardia. Un orologio che torna indietro
 * rispetto all'ultimo segno vale «scaduto» (il verso sicuro). Al primo segno di vita dopo l'assenza si
 * riprende (porta di nuovo il timeout al massimo, con il ripristino registrato prima); se la ripresa fallisce si
 * ritenta al giro dopo. Nessun segno di vita ancora (0, nessun SALUTO): non c'e' nulla da fare.
 */
final class AssenzaPc {
    interface Azioni {
        /** Passo 1: riaccende il pannello se l'ha spento YASDE. true = fatto (o non serviva). */
        boolean riaccendi();

        /** Passo 2: ripristina la frequenza minima dell'utente. true = fatto (o non serviva). */
        boolean ripristinaFrequenza();

        /** Passo 3: rimette il timeout dello schermo dell'utente (solo se ha ancora il nostro valore). true = fatto. */
        boolean ripristinaTimeout();

        /** PANNELLO_EVENTO motivo «PC assente» con lo stato vero del pannello: dopo tutti i passi, o dopo una uscita annullata a meta'. Una volta sola per volta. */
        void eventoAssente();

        /** Ripresa: timeout di nuovo al massimo (ripristino registrato prima). true = fatto. */
        boolean riprendi();
    }

    /** Le due letture del controllo: l'ultimo segno di vita e l'ora (stesso orologio monotono). */
    interface Fonte {
        long ultimoSegno();

        long ora();
    }

    /** Il risultato di leggi(). */
    static final class Lettura {
        final long ora;
        final long ultimoSegno;

        Lettura(long ora, long ultimoSegno) {
            this.ora = ora;
            this.ultimoSegno = ultimoSegno;
        }
    }

    /**
     * Legge PRIMA l'ultimo segno di vita e DOPO l'ora. L'ordine conta: un messaggio del PC e' datato prima di essere
     * scritto, quindi un segno letto prima dell'ora non puo' essere «dal futuro»; a parti invertite un messaggio
     * arrivato tra le due letture sembrerebbe piu' recente dell'ora e verrebbe trattato come orologio incoerente
     * (scaduto): l'uscita completa con il PC presente.
     */
    static Lettura leggi(Fonte f) {
        long segno = f.ultimoSegno();
        long ora = f.ora();
        return new Lettura(ora, segno);
    }

    private final Azioni azioni;
    private final long limiteMs;
    private boolean assente;
    private int passo; // passi di uscita gia' riusciti (0..3)

    AssenzaPc(Azioni azioni, long limiteMs) {
        this.azioni = azioni;
        this.limiteMs = limiteMs;
    }

    boolean assente() {
        return assente;
    }

    /** Scaduto: i 30 minuti esatti, o un segno «dal futuro» (orologio incoerente). */
    private boolean scaduto(long ora, long ultimo) {
        return ora < ultimo || ora - ultimo >= limiteMs;
    }

    void valuta(long ora, long ultimoSegno) {
        if (ultimoSegno == 0) return;
        boolean scaduto = scaduto(ora, ultimoSegno);
        if (!assente && !scaduto && passo > 0) {
            // Un segno di vita torna a meta' dell'uscita: il pannello e' gia' stato riacceso (passo 1 fatto) e il PC
            // lo crede ancora spento, il timeout puo' essere gia' dell'utente. Si fa come nella ripresa: timeout di nuovo
            // al massimo (ripristino registrato prima) e l'evento con lo stato vero; solo allora si ricomincia pulito.
            boolean fatto = false;
            try {
                fatto = azioni.riprendi();
            } catch (Throwable t) {
                System.out.println("[pannello] ripresa a meta' uscita fallita, ritento: " + t);
            }
            if (fatto) {
                passo = 0;
                try {
                    azioni.eventoAssente();
                } catch (Throwable t) {
                    System.out.println("[pannello] evento non preparato: " + t);
                }
            }
            return;
        }
        if (!assente && scaduto) {
            while (passo < 3 && esegui(passo)) passo++;
            if (passo < 3) return;
            try {
                azioni.eventoAssente();
            } catch (Throwable t) {
                System.out.println("[pannello] evento di PC assente non preparato: " + t);
            }
            assente = true;
        } else if (assente && !scaduto) {
            boolean fatto = false;
            try {
                fatto = azioni.riprendi();
            } catch (Throwable t) {
                System.out.println("[pannello] ripresa dopo l'assenza del PC fallita, ritento: " + t);
            }
            if (fatto) {
                assente = false;
                passo = 0;
            }
        }
    }

    private boolean esegui(int p) {
        try {
            if (p == 0) return azioni.riaccendi();
            if (p == 1) return azioni.ripristinaFrequenza();
            return azioni.ripristinaTimeout();
        } catch (Throwable t) { // anche un Error: il telefono resta com'era e si ritenta
            System.out.println("[pannello] PC assente, passo " + (p + 1) + " fallito, ritento: " + t);
            return false;
        }
    }
}
