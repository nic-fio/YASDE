package it.yasde;

/**
 * Tiene il telefono sveglio (e quindi sbloccato per inattivita') finche' YASDE e' collegata e viva. La parte di
 * Android sta dietro l'interfaccia Presa (in produzione un wake lock dello schermo, vedi NascostePannello),
 * cosi' la logica si prova su JVM.
 *
 * Perche' un wake lock e non il timeout dello schermo: il wake lock appartiene al processo, quindi se il
 * servizio muore (anche kill -9) il sistema lo rilascia da solo e il telefono riprende a spegnersi e
 * bloccarsi con i suoi tempi; non resta nessuna impostazione dell'utente cambiata da rimettere. Per questo
 * non passa dal guardiano: non c'e' nulla che possa sopravvivere a noi.
 *
 * La guardia (tieni): il wake lock si tiene SOLO se (a) il canale comandi e' aperto e salutato, (b) il telefono
 * NON e' bloccato (dopo un blocco col tasto il lock terrebbe accesa per sempre la schermata di blocco: si
 * rilascia, e allo sblocco si riprende) e (c) l'ultimo segno di vita del PC e' piu' giovane di limiteMs (Registro.PC_ASSENTE_MS, o meno nelle prove sul telefono)
 * (un PC caduto senza chiudere non tiene il telefono sbloccato per sempre). Ogni dubbio va nel verso sicuro
 * (rilasciare): stato del blocco illeggibile, orologio che torna indietro, segno di vita «dal futuro».
 * Il tempo e' un orologio monotono passato da fuori (ms). Lo stato voluto SCADE da solo: se aggiorna() non arriva
 * da scadenzaStatoMs (misurato con un orologio proprio, monotono) il thread lo tratta come «non tenere» e rilascia,
 * cosi' un controllo periodico morto o fermo non lascia il telefono sbloccato oltre il limite.
 *
 * Chi chiama aggiorna() (di norma il controllo periodico del pannello, ogni secondo) non aspetta mai: la
 * chiamata si limita a scrivere lo stato voluto. Prendere e rilasciare lo fa un thread a parte, fuori da ogni
 * lock, ritentando ogni ritentoMs se la chiamata di sistema fallisce. Dopo il ritorno di chiudi() il wake lock
 * e' rilasciato; se una chiamata in corso non finisce entro attesaMs la completa (e, se serve, rilascia) il suo
 * thread appena torna, e comunque il sistema alla morte del processo.
 *
 * Il wake lock si prende SENZA ON_AFTER_RELEASE (decisione del progettista, il verso piu' sicuro): quando viene
 * rilasciato (chiusura, morte, blocco, assenza del PC da 30 minuti) e l'utente non tocca il telefono da piu' del suo
 * tempo di spegnimento, Android NON riparte da zero: spegne lo schermo e blocca SUBITO.
 */
final class TieniSveglio {
    /** Orologio monotono in ms, passato da fuori (nelle prove e' finto). */
    interface Orologio {
        long ms();
    }

    /** L'orologio vero: System.nanoTime, monotono. */
    static final Orologio MONOTONO = new Orologio() {
        @Override
        public long ms() {
            return System.nanoTime() / 1000000L;
        }
    };

    /**
     * Lo stato «tieni» vale cosi' a lungo dopo l'ultima aggiorna(): se il controllo si ferma, il lock si rilascia da solo.
     * 30 s e non 5: il giro del controllo e i comandi del PC lavorano sotto la stessa serratura del pannello e lanciano
     * `cmd settings` con 10 s di scadenza ciascuno (fino a 20 s): con 5 s un telefono sotto sforzo farebbe cadere il lock
     * e il telefono si addormenterebbe a meta' sessione. Tetto di sicurezza se il controllo muore: 30 minuti + 30 s.
     */
    static final long SCADENZA_STATO_MS = 30000;

    /** Le due chiamate di sistema. Lanciano un'eccezione se falliscono. Rilascia deve reggere di essere chiamata a vuoto. */
    interface Presa {
        void prendi();

        void rilascia();
    }

    private final Presa presa;
    private final long ritentoMs;
    private final long attesaMs;
    private final long limiteMs; // senza segni di vita del PC oltre questo tempo non si tiene (l'unica fonte e' Pannello)
    private final Orologio orologio; // proprio, monotono: misura da quanto non arriva aggiorna()
    private final long scadenzaStatoMs;
    private final long periodoMs; // ogni quanto il thread rivaluta anche senza chiamate
    private final Object serratura = new Object();
    private boolean avviato;
    private boolean chiuso;
    private boolean voluto; // la guardia dice di tenere il lock
    private boolean preso; // il lock risulta preso (ultima chiamata riuscita)
    private boolean inPresa; // il thread e' dentro presa.prendi() o presa.rilascia()
    private boolean rinunciato; // chiudi() non ha aspettato una chiamata in corso: completa il thread
    private boolean haAggiorna;
    private long ultimaAggiorna; // ora (orologio proprio) dell'ultima aggiorna()
    private boolean haAdesso;
    private long ultimoAdesso;
    private Thread filo;
    private String ultimoErrore = "";

    TieniSveglio(Presa presa, long ritentoMs, long attesaMs, long limiteMs, Orologio orologio, long scadenzaStatoMs,
            long periodoMs) {
        this.presa = presa;
        this.ritentoMs = ritentoMs;
        this.attesaMs = attesaMs;
        this.limiteMs = limiteMs;
        this.orologio = orologio;
        this.scadenzaStatoMs = scadenzaStatoMs;
        this.periodoMs = periodoMs;
    }

    /** Lo stato voluto, ma solo se l'ultima aggiorna() e' recente (altrimenti «non tenere»: verso sicuro). Sotto la serratura. */
    private boolean votoEffettivoLocked() {
        if (!voluto || !haAggiorna) return false;
        long ora = orologio.ms();
        return ora >= ultimaAggiorna && ora - ultimaAggiorna < scadenzaStatoMs;
    }

    /**
     * La guardia, pura. bloccato: true/false, oppure null se non si e' riusciti a leggerlo (vale «non tenere»).
     * Esattamente LIMITE_MS dall'ultimo segno di vita e' gia' troppo; un segno piu' recente dell'ora e' un
     * orologio incoerente: non si tiene.
     */
    static boolean tieni(boolean canaleSalutato, Boolean bloccato, long adesso, long ultimoSegno, long limiteMs) {
        if (!canaleSalutato) return false;
        if (bloccato == null || bloccato.booleanValue()) return false;
        if (adesso < ultimoSegno) return false;
        return adesso - ultimoSegno < limiteMs;
    }

    /**
     * Valuta la guardia e scrive lo stato voluto; non aspetta mai e non chiama il sistema (si puo' chiamare
     * sotto i lock di chi controlla). Un'ora piu' bassa di quella della chiamata precedente (orologio che
     * torna indietro) vale «non tenere» per quella chiamata. Parte il thread alla prima chiamata.
     */
    void aggiorna(boolean canaleSalutato, Boolean bloccato, long adesso, long ultimoSegno) {
        synchronized (serratura) {
            if (chiuso) return;
            boolean indietro = haAdesso && adesso < ultimoAdesso;
            haAdesso = true;
            ultimoAdesso = adesso;
            voluto = !indietro && tieni(canaleSalutato, bloccato, adesso, ultimoSegno, limiteMs);
            haAggiorna = true;
            ultimaAggiorna = orologio.ms();
            avviaLocked();
            serratura.notifyAll();
        }
    }

    /** Parte il thread (una sola volta). Dopo chiudi() non fa nulla. */
    void avvia() {
        synchronized (serratura) {
            avviaLocked();
        }
    }

    private void avviaLocked() {
        if (avviato || chiuso) return;
        avviato = true;
        filo = new Thread("yasde-sveglio") {
            @Override
            public void run() {
                ciclo();
            }
        };
        filo.setDaemon(true);
        filo.start();
    }

    private void ciclo() {
        while (true) {
            boolean prendere;
            synchronized (serratura) {
                while (!chiuso && votoEffettivoLocked() == preso) {
                    try {
                        serratura.wait(periodoMs); // l'attesa ha sempre un limite; la condizione (anche la scadenza) si rilegge
                    } catch (InterruptedException e) {
                        return;
                    }
                }
                if (chiuso) return;
                prendere = votoEffettivoLocked();
                if (!prendere && voluto && preso) {
                    // la guardia dice «tieni» ma l'ultima aggiorna() e' troppo vecchia (o l'orologio proprio e' tornato indietro)
                    System.out.println("[pannello] telefono sveglio: nessun aggiornamento del controllo da oltre "
                        + scadenzaStatoMs + " ms, rilascio il wake lock");
                }
                inPresa = true;
            }
            String errore = null;
            try {
                if (prendere) presa.prendi();
                else presa.rilascia();
            } catch (Throwable t) { // anche un Error: il thread non deve morire lasciando il telefono senza tentativo
                errore = t.toString();
            }
            boolean rilasciaIo = false;
            synchronized (serratura) {
                inPresa = false;
                if (errore == null) preso = prendere;
                serratura.notifyAll(); // sveglia chiudi(), se aspetta
                if (chiuso) {
                    rilasciaIo = rinunciato && errore == null && prendere;
                } else if (errore != null) {
                    if (!errore.equals(ultimoErrore)) {
                        ultimoErrore = errore;
                        System.out.println("[pannello] telefono sveglio: " + (prendere ? "presa" : "rilascio")
                            + " non riuscito, ritento: " + errore);
                    }
                    try {
                        serratura.wait(ritentoMs);
                    } catch (InterruptedException e) {
                        return;
                    }
                    continue;
                } else {
                    continue; // riuscito: si rivaluta lo stato voluto (puo' essere cambiato nel frattempo)
                }
            }
            // chiuso mentre la chiamata era in corso e chiudi() non ha aspettato: il rilascio e' nostro, fuori dalla serratura
            if (rilasciaIo) rilascia();
            return;
        }
    }

    /** Rilascia, senza mai lanciare. */
    private void rilascia() {
        try {
            presa.rilascia();
        } catch (Throwable t) {
            // il sistema lo rilascia comunque alla morte del processo; qui basta dirlo
            System.out.println("[pannello] telefono sveglio: rilascio non riuscito: " + t);
        }
    }

    /** Chiusura ordinata, idempotente: ferma tutto e rilascia. Non lancia. */
    void chiudi() {
        boolean daRilasciare;
        synchronized (serratura) {
            if (chiuso) return;
            chiuso = true;
            serratura.notifyAll(); // sveglia il thread
            long limite = System.currentTimeMillis() + attesaMs;
            while (inPresa && System.currentTimeMillis() < limite) {
                try {
                    serratura.wait(Math.max(1, limite - System.currentTimeMillis()));
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
            if (inPresa) rinunciato = true; // la chiamata in corso la completa il suo thread appena finisce
            daRilasciare = avviato;
        }
        // fuori dalla serratura: e' una chiamata di sistema. A vuoto e' innocua.
        if (daRilasciare) rilascia();
    }

    /** Solo per le prove: aspetta che il thread sia finito. */
    boolean attendiFine(long ms) throws InterruptedException {
        Thread f;
        synchronized (serratura) {
            f = filo;
        }
        if (f == null) return true;
        f.join(ms);
        return !f.isAlive();
    }
}
