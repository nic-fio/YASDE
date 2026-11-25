package it.yasde;

import java.io.DataInputStream;
import java.io.IOException;
import java.util.List;
import java.util.concurrent.LinkedBlockingQueue;

import android.os.SystemClock;

/**
 * Pannello fisico e regola del «telefono in mano» (docs/protocollo.md §2.9, comandi 0x60-0x6f).
 *
 * Spegnere agisce solo sul compositore (SurfaceControl.setDisplayPowerMode): Android crede lo schermo
 * acceso, le app sugli schermi virtuali continuano a girare. Il pannello si riaccende DA SOLO (e parte
 * PANNELLO_EVENTO) quando l'utente tocca lo schermo reale o preme un tasto fisico (InputFisico), quando
 * il telefono si sblocca o si risveglia, quando arriva o c'e' una chiamata (controllo periodico ogni
 * secondo, solo chiamate leggere ai servizi di sistema: niente dumpsys).
 *
 * Frequenza del compositore (workaround di §2.9, generalizzato): a OGNI spegnimento si misura la frequenza
 * attiva del compositore e la si scrive nel log; SOLO se e' sotto la frequenza voluta (la minore tra la massima
 * del display a parita' di risoluzione e il tetto peak_refresh_rate dell'utente; misurata, mai data per
 * scontata) si porta min_refresh_rate a quella frequenza, con il ripristino (ordine 410) nel guardiano,
 * rimesso alla riaccensione se ha ancora il nostro valore. Misurato sul telefono di prova (120 Hz): a
 * pannello spento il compositore scende a 60 Hz e un'app che scorre produce al massimo ~57 fotogrammi al
 * secondo (contro 85 a pannello acceso); con la frequenza minima a 120 tornano ~80. Non scende mai a 24 Hz.
 *
 * Timeout dello schermo: portato al massimo alla prima richiesta di spegnimento e ripristinato (solo
 * se ha ancora il nostro valore) quando il PC chiede di accendere o alla chiusura; il guardiano fa lo
 * stesso se il servizio muore. Con il pannello gestito da noi il timeout deve restare al massimo anche
 * nei periodi «in mano», altrimenti le app sugli schermi virtuali smettono di ricevere input.
 *
 * Telefono sveglio (TieniSveglio): dal momento in cui il servizio parte e fino alla chiusura un wake lock tiene lo
 * schermo di Android acceso, a prescindere dal pannello e dal timeout dell'utente: niente sospensione e niente
 * blocco per inattivita'. Se l'utente blocca il telefono di sua mano il sistema lo addormenta comunque (un wake
 * lock di schermo non vale a telefono addormentato) e vale la gestione del blocco che c'e'. Il lock si prende dal SALUTO
 * (primo segno di vita), si rilascia mentre il telefono e' bloccato e si riprende allo sblocco; alla morte del servizio lo
 * rilascia il sistema.
 *
 * Assenza del PC (AssenzaPc, protocollo §2.9): dopo PC_ASSENTE_MS senza segni di vita il telefono torna quello di tutti i
 * giorni (pannello riacceso se l'avevamo spento noi, frequenza e timeout dell'utente, wake lock rilasciato, evento
 * motivo 6); al primo segno di vita dopo l'assenza si riprende il wake lock e il timeout al massimo, il pannello resta
 * acceso finche' il PC non manda PANNELLO_IMPOSTA 0.
 */
public final class Pannello {
    private static final int PANNELLO_IMPOSTA = 0x60;
    private static final int PANNELLO_STATO = 0x61;
    private static final int PANNELLO_EVENTO = 0x6e;

    private static final int MOTIVO_UTENTE = 1;
    private static final int MOTIVO_CHIAMATA = 2;
    private static final int MOTIVO_FINE_CHIAMATA = 3;
    private static final int MOTIVO_PC = 4;
    private static final int MOTIVO_CHIUSURA = 5;
    private static final int MOTIVO_ASSENTE = 6; // il PC non da' segni di vita da PC_ASSENTE_MS

    private static final String NOME_PANNELLO = "pannello-acceso";
    private static final String NOME_TIMEOUT = "pannello-timeout";
    private static final String NOME_FREQUENZA = "pannello-frequenza";
    private static final int ORDINE_PANNELLO = 400;
    private static final int ORDINE_FREQUENZA = 410;
    private static final int ORDINE_TIMEOUT = 420;
    private static final long PERIODO_MS = 1000;
    private static final String MINIMO_FREQUENZA = "min_refresh_rate";
    private static final String NON_AL_SICURO = "impossibile mettere al sicuro il ripristino";

    private static final Object serratura = new Object();
    private static boolean spento; // il pannello e' spento da noi
    private static boolean inMano;
    private static boolean inChiamata;
    private static volatile boolean chiuso; // scritto sotto la serratura; letto anche senza (avviaControllo)
    private static boolean timeoutMesso;
    private static String timeoutOriginale; // null = non lo abbiamo cambiato (era gia' al massimo)
    private static boolean timeoutAssente; // l'impostazione non esisteva (si ripristina cancellandola)
    private static boolean frequenzaMessa;
    private static String frequenzaOriginale; // null = non esisteva
    private static String frequenzaNostra; // il valore che abbiamo scritto (per esempio "120.0")
    private static boolean frequenzaInScrittura; // il 410 e' registrato ma applicaFrequenza non ha finito di scrivere
    private static int riaccensioni; // contatore: chi ha lavorato fuori dalla serratura sa se nel frattempo c'e' stata una riaccensione
    private static InputFisico ascolto;
    private static long limiteMs = Registro.PC_ASSENTE_MS; // l'unica fonte del limite dei 30 minuti: per AssenzaPc e per TieniSveglio
    private static boolean registrato;
    private static AssenzaPc assenza;
    private static final java.util.concurrent.atomic.AtomicBoolean controlloAvviato = new java.util.concurrent.atomic.AtomicBoolean();
    private static TieniSveglio tieniSveglio; // il wake lock che tiene il telefono sbloccato per tutta la vita del servizio
    private static Thread controllo;
    private static int accensionePendente; // motivo di una riaccensione da fare, se il tentativo e' fallito
    private static String ultimoErrore = "";

    /** Eventi verso il PC: si preparano sotto la serratura e li manda un thread a parte (un PC che non legge non la blocca). */
    private static final LinkedBlockingQueue<byte[]> eventi = new LinkedBlockingQueue<>();

    private Pannello() {}

    /**
     * SOLO per le prove sul telefono (la chiama ProvaPannello, che non sta nel jar di produzione): accorcia il limite
     * dell'assenza del PC. Puo' solo ACCORCIARE (mai oltre Registro.PC_ASSENTE_MS), rifiuta valori <= 0 e vale solo
     * prima di registra(). Nessuna proprieta' di sistema, variabile d'ambiente o argomento letti qui.
     */
    static synchronized void limitePerProve(long ms) {
        if (ms <= 0) throw new IllegalArgumentException("limite non valido: " + ms);
        if (registrato) throw new IllegalStateException("il limite si cambia solo prima di registra()");
        limiteMs = Math.min(ms, Registro.PC_ASSENTE_MS);
    }

    /** Il limite in uso (per le prove). */
    static synchronized long limiteAttuale() {
        return limiteMs;
    }

    /** Da qui il limite non si cambia piu' (primo passo di registra(); separato per poterlo provare su JVM). */
    static synchronized void segnaRegistrato() {
        registrato = true;
    }

    public static void registra() {
        segnaRegistrato();
        List<String> mancano = NascostePannello.verifica();
        if (mancano.isEmpty()) {
            System.out.println("[pannello] API di Android necessarie: tutte presenti");
        } else {
            for (String m : mancano) System.out.println("[pannello] API MANCANTE: " + m);
        }
        String input = InputFisico.verifica();
        if (input == null) {
            System.out.println("[pannello] rilevazione dell'utente (tocco e tasti fisici) disponibile");
        } else {
            System.out.println("[pannello] rilevazione dell'utente NON disponibile, il pannello non si spegne: " + input);
        }
        Thread mittente = new Thread("yasde-pannello-eventi") {
            @Override
            public void run() {
                try {
                    while (true) {
                        byte[] e = eventi.take();
                        try {
                            Registro.mandaEvento(PANNELLO_EVENTO, e);
                        } catch (Throwable t) {
                            System.out.println("[pannello] evento non mandato: " + t); // il thread non muore
                        }
                    }
                } catch (InterruptedException e) {
                    // fine
                }
            }
        };
        mittente.setDaemon(true);
        mittente.start();
        // Per tutto il tempo in cui YASDE e' collegata il telefono non si addormenta ne' si blocca per
        // inattivita' (decisione del committente, 23 nov 2026). Un wake lock, non il timeout dello schermo:
        // lo rilascia il sistema se il servizio muore, quindi non serve nessun ripristino nel guardiano.
        tieniSveglio = new TieniSveglio(new TieniSveglio.Presa() {
            @Override
            public void prendi() {
                NascostePannello.prendiSveglio();
            }

            @Override
            public void rilascia() {
                NascostePannello.rilasciaSveglio();
            }
        }, 5000, 2000, limiteAttuale(), TieniSveglio.MONOTONO, TieniSveglio.SCADENZA_STATO_MS, 1000);
        assenza = new AssenzaPc(new AssenzaPc.Azioni() {
            @Override
            public boolean riaccendi() {
                return !spento || tentaAccendi(MOTIVO_ASSENTE);
            }

            @Override
            public boolean ripristinaFrequenza() {
                ripristinaFrequenzaLocked();
                return !frequenzaMessa || frequenzaInScrittura; // in scrittura la annulla applicaFrequenza, a riaccensione avvenuta
            }

            @Override
            public boolean ripristinaTimeout() {
                ripristinaTimeoutLocked();
                if (timeoutMesso) {
                    // il wake lock si rilascia comunque alla scadenza: se il timeout dell'utente non si riesce a rimettere
                    // resta al massimo finche' un giro non riesce (si ritenta ogni secondo)
                    logCambiato("PC assente: timeout dello schermo dell'utente NON rimesso, ritento ogni secondo");
                }
                return !timeoutMesso;
            }

            @Override
            public void eventoAssente() {
                evento(!spento, MOTIVO_ASSENTE);
            }

            @Override
            public boolean riprendi() {
                return riprendiDaAssenzaLocked();
            }
        }, limiteAttuale());
        // Il primo segno di vita (il SALUTO) fa partire il controllo; l'ascoltatore non fa altro (gira sul thread
        // che riceve i messaggi del PC: niente serratura, niente chiamate al sistema).
        Registro.registraSegnoDiVita(new Runnable() {
            @Override
            public void run() {
                avviaControllo();
            }
        });
        Registro.registraChiusura(new Runnable() {
            @Override
            public void run() {
                chiudi();
            }
        });
        Registro.registraComandi(0x60, 0x6f, new GestoreComandi() {
            @Override
            public byte[] gestisci(int tipo, DataInputStream corpo) throws Exception {
                return comando(tipo, corpo);
            }
        });
    }

    /**
     * Punto d'ingresso del guardiano: `CLASSPATH=<jar> app_process / it.yasde.Pannello accendi`
     * riaccende il pannello anche se il servizio e' morto. Fa il minimo indispensabile: due chiamate
     * dirette ai servizi di sistema, niente contesto. Non accende se Android e' a riposo (lo schermo
     * l'ha spento il sistema, per esempio con il tasto di accensione): lo risveglia l'utente.
     */
    public static void main(String[] args) {
        if (args.length != 1 || !args[0].equals("accendi")) {
            System.out.println("uso: Pannello accendi");
            System.exit(2);
        }
        try {
            boolean sveglio = true;
            try {
                sveglio = NascostePannello.interattivo();
            } catch (Throwable t) {
                System.out.println("stato di Android illeggibile (" + t + "): accendo comunque");
            }
            if (sveglio) NascostePannello.impostaPannello(true);
            System.out.println(sveglio ? "pannello acceso" : "Android a riposo: pannello lasciato al sistema");
            System.exit(0);
        } catch (Throwable t) {
            System.out.println("pannello non acceso: " + t);
            System.exit(1);
        }
    }

    // ---- Comandi ----

    private static byte[] comando(int tipo, DataInputStream corpo) throws Exception {
        if (tipo == PANNELLO_IMPOSTA) {
            int v = corpo.readUnsignedByte();
            if (v > 1) throw new IllegalArgumentException("valore non valido: 0 spegne, 1 accende");
            if (v == 0) spegni();
            else accendiPerRichiesta();
            return new byte[0];
        }
        if (tipo == PANNELLO_STATO) {
            synchronized (serratura) {
                avviaControlloLocked();
                return new byte[] {(byte) (spento ? 0 : 1), (byte) (inMano ? 1 : 0)};
            }
        }
        throw new IllegalArgumentException("tipo sconosciuto");
    }

    private static String comandoAccendi() {
        String cp = System.getenv("CLASSPATH");
        if (cp == null || cp.isEmpty()) cp = System.getProperty("java.class.path", "");
        // il guardiano la racchiude tra apici: niente caratteri strani nel percorso del jar
        if (!cp.matches("[A-Za-z0-9_./:-]+")) throw new IllegalStateException("percorso del jar non utilizzabile dal guardiano");
        return "CLASSPATH=" + cp + " app_process / it.yasde.Pannello accendi";
    }

    private static void spegni() throws Exception {
        synchronized (serratura) {
            if (chiuso) throw new IllegalStateException("servizio in chiusura");
            avviaControlloLocked();
            if (spento) return;
            Guardiano g = Registro.guardiano();
            if (g == null) throw new IllegalStateException("guardiano assente: il pannello non si spegne senza ripristino");
            if (NascostePannello.inChiamata()) throw new IllegalStateException("chiamata in corso: il pannello resta acceso");
            if (!NascostePannello.interattivo() || NascostePannello.bloccato()) {
                throw new IllegalStateException("telefono bloccato o con lo schermo spento: il pannello non si gestisce");
            }
            // 1. cio' che serve per accorgersi dell'utente: senza, il pannello non si spegne
            InputFisico f = InputFisico.avvia(new InputFisico.Ascoltatore() {
                @Override
                public void attivita(String che) {
                    System.out.println("[pannello] " + che);
                    riaccendiDaSolo(MOTIVO_UTENTE);
                }
            });
            try {
                preparaESpegniLocked(g);
            } catch (RuntimeException | IOException e) {
                f.ferma();
                throw e;
            }
            ascolto = f;
            spento = true;
            inMano = false;
            accensionePendente = 0;
            evento(false, MOTIVO_PC);
            misuraFrequenza();
        }
    }

    /**
     * Prima si registrano nel guardiano TUTTI i ripristini (timeout e pannello): se uno non e' al sicuro
     * non si cambia nulla. Poi si porta il timeout al massimo e si spegne il pannello.
     */
    private static void preparaESpegniLocked(Guardiano g) throws IOException {
        // prima di registrare qualsiasi cosa: se il comando del guardiano o il token del display non si
        // ottengono non resta registrato nessun ripristino per una modifica mai fatta
        String accendi = comandoAccendi();
        NascostePannello.verificaPannello();
        String chiave = ImpostazioniPannello.TIMEOUT_SCHERMO;
        String massimo = Integer.toString(ImpostazioniPannello.TIMEOUT_MASSIMO);
        boolean cambiaTimeout = false;
        String originale = null;
        boolean assente = false;
        if (!timeoutMesso) {
            originale = ImpostazioniPannello.leggi(chiave);
            if (originale.equals(massimo)) {
                // gia' al massimo (scelta dell'utente): niente da cambiare e niente da ripristinare
                timeoutMesso = true;
                timeoutOriginale = null;
            } else {
                assente = originale.equals("null");
                if (!assente && !originale.matches("[0-9]{1,10}")) {
                    throw new IOException("timeout dello schermo illeggibile: " + originale);
                }
                if (!g.imposta(NOME_TIMEOUT, ORDINE_TIMEOUT,
                        ImpostazioniPannello.comandoRipristino(chiave, massimo, assente ? null : originale))) {
                    throw new IllegalStateException(NON_AL_SICURO);
                }
                cambiaTimeout = true;
            }
        }
        if (!g.imposta(NOME_PANNELLO, ORDINE_PANNELLO, accendi)) {
            if (cambiaTimeout) g.togli(NOME_TIMEOUT);
            throw new IllegalStateException(NON_AL_SICURO);
        }
        if (cambiaTimeout) {
            timeoutMesso = true; // da qui il ripristino e' dovuto anche se la verifica della scrittura fallisse
            timeoutOriginale = originale;
            timeoutAssente = assente;
            try {
                ImpostazioniPannello.scrivi(chiave, massimo);
            } catch (IOException e) {
                g.togli(NOME_PANNELLO);
                throw e;
            }
        }
        try {
            NascostePannello.impostaPannello(false);
        } catch (RuntimeException e) {
            // spegnimento fallito: si prova a riaccendere e il ripristino si toglie solo se riesce
            try {
                NascostePannello.impostaPannello(true);
                g.togli(NOME_PANNELLO);
            } catch (RuntimeException e2) {
                System.out.println("[pannello] dopo lo spegnimento fallito il pannello non si riaccende (" + e2
                    + "): il ripristino resta nel guardiano");
            }
            throw e;
        }
    }

    /** PANNELLO_IMPOSTA 1: il PC rilascia il pannello (e il timeout dello schermo). */
    private static void accendiPerRichiesta() throws Exception {
        synchronized (serratura) {
            if (chiuso) throw new IllegalStateException("servizio in chiusura");
            avviaControlloLocked();
            if (spento) {
                if (!tentaAccendi(MOTIVO_PC)) {
                    throw new IllegalStateException("riaccensione non riuscita: si ritenta da sola ogni secondo");
                }
            } else {
                inMano = false;
                ripristinaTimeoutLocked();
            }
        }
    }

    /** Dal thread di input o dal controllo: l'utente ha preso il telefono. Non lancia mai. */
    private static void riaccendiDaSolo(int motivo) {
        synchronized (serratura) {
            if (!spento || chiuso) return;
            try {
                tentaAccendi(motivo);
            } catch (Throwable t) {
                accensionePendente = motivo;
                System.out.println("[pannello] riaccensione fallita, ritento: " + t);
            }
        }
    }

    /**
     * Riaccende il pannello. A riaccensione riuscita toglie il ripristino dal guardiano, ferma l'ascolto,
     * ripristina frequenza (e, se chiesto dal PC, timeout) e prepara l'evento: ritorna true. Se fallisce
     * ricorda il motivo in accensionePendente (il controllo periodico ritenta ogni secondo, per primo) e
     * ritorna false: non lancia. Se Android e' a riposo il pannello e' gia' spento dal sistema e non si forza.
     */
    private static boolean tentaAccendi(int motivo) {
        boolean sveglio = true;
        try {
            sveglio = NascostePannello.interattivo();
        } catch (RuntimeException e) {
            System.out.println("[pannello] stato di Android illeggibile: accendo comunque: " + e);
        }
        try {
            if (sveglio) NascostePannello.impostaPannello(true);
        } catch (RuntimeException e) {
            accensionePendente = motivo;
            logCambiato("riaccensione fallita (motivo " + motivo + "), ritento: " + e);
            return false;
        }
        Guardiano g = Registro.guardiano();
        if (g != null) g.togli(NOME_PANNELLO);
        if (ascolto != null) ascolto.ferma();
        ascolto = null;
        spento = false;
        accensionePendente = 0;
        riaccensioni++;
        ripristinaFrequenzaLocked();
        if (motivo == MOTIVO_PC) {
            inMano = false;
            ripristinaTimeoutLocked();
        } else if (motivo == MOTIVO_ASSENTE) {
            inMano = false; // timeout ed evento li gestisce AssenzaPc, passo per passo
        } else {
            inMano = true;
        }
        if (motivo != MOTIVO_ASSENTE) evento(sveglio, motivo);
        return true;
    }

    /** Scrive nel log solo se il messaggio cambia (il controllo gira ogni secondo). */
    private static void logCambiato(String m) {
        if (!m.equals(ultimoErrore)) {
            ultimoErrore = m;
            System.out.println("[pannello] " + m);
        }
    }

    // ---- Timeout dello schermo ----

    /** Rimette il timeout dell'utente solo se ha ancora il nostro valore; il guardiano si toglie a cosa fatta. */
    private static void ripristinaTimeoutLocked() {
        if (!timeoutMesso) return;
        Guardiano g = Registro.guardiano();
        try {
            if (timeoutOriginale != null) {
                String chiave = ImpostazioniPannello.TIMEOUT_SCHERMO;
                String massimo = Integer.toString(ImpostazioniPannello.TIMEOUT_MASSIMO);
                if (ImpostazioniPannello.leggi(chiave).equals(massimo)) {
                    if (timeoutAssente) ImpostazioniPannello.rimuovi(chiave);
                    else ImpostazioniPannello.scrivi(chiave, timeoutOriginale);
                } // altrimenti l'utente l'ha cambiato: vince lui
                if (g != null) g.togli(NOME_TIMEOUT);
            }
            timeoutMesso = false;
            timeoutOriginale = null;
        } catch (IOException e) {
            // il ripristino resta nel guardiano, che lo rifa' alla chiusura del servizio
            System.out.println("[pannello] timeout non ripristinato ora: " + e);
        }
    }

    // ---- Frequenza del compositore ----

    /**
     * Un secondo dopo lo spegnimento (il tempo che il compositore cambi modo) misura la frequenza attiva e la
     * scrive nel log con la massima del display (modi con la stessa risoluzione del modo attivo) e il tetto scelto
     * dall'utente (peak_refresh_rate). Il workaround scatta SOLO se la attiva e' sotto la frequenza voluta (la
     * minore tra massima e tetto): la condizione si misura a ogni spegnimento su ogni telefono, non si da' per
     * scontata; un tetto dell'utente a 60 Hz e' una sua scelta, non il sintomo.
     */
    private static void misuraFrequenza() {
        Thread t = new Thread("yasde-pannello-frequenza") {
            @Override
            public void run() {
                try {
                    Thread.sleep(1000);
                } catch (InterruptedException e) {
                    return;
                }
                float[] hz;
                try {
                    hz = NascostePannello.frequenze();
                } catch (RuntimeException e) {
                    System.out.println("[pannello] frequenza del compositore non misurabile: " + e);
                    return;
                }
                String tetto = "null";
                try {
                    tetto = ImpostazioniPannello.leggi("peak_refresh_rate");
                } catch (IOException e) {
                    // senza conoscere il tetto dell'utente il rimedio non si applica: vince l'utente
                    System.out.println("[pannello] tetto di frequenza dell'utente illeggibile, rimedio non applicato: " + e);
                    return;
                }
                float voluta = Frequenze.voluta(hz[1], tetto);
                System.out.println("[pannello] frequenza del compositore a pannello spento: " + hz[0] + " Hz (massima del display "
                    + hz[1] + " Hz, tetto dell'utente " + tetto + ", voluta " + voluta + " Hz)");
                if (hz[0] < voluta - 0.5f) applicaFrequenza(Math.round(voluta) + ".0");
            }
        };
        t.setDaemon(true);
        t.start();
    }

    /**
     * Porta min_refresh_rate alla frequenza massima del display con il ripristino (ordine 410) registrato prima, se l'utente non ha gia' di
     * piu'. I comandi di shell girano FUORI dalla serratura (un tocco dell'utente non deve aspettarli); se
     * nel frattempo c'e' stata una riaccensione, quello che si e' scritto si annulla subito.
     */
    private static void applicaFrequenza(String nostra) {
        String attuale;
        int mie;
        try {
            attuale = ImpostazioniPannello.leggi(MINIMO_FREQUENZA);
            boolean assente = attuale.equals("null");
            if (!assente && !attuale.matches("[0-9.]{1,12}")) throw new IOException("frequenza minima illeggibile: " + attuale);
            if (!assente && Float.parseFloat(attuale) >= Float.parseFloat(nostra)) {
                System.out.println("[pannello] frequenza minima gia' " + attuale + ": niente da cambiare");
                return;
            }
            String originale = assente ? null : attuale;
            synchronized (serratura) {
                if (!spento || chiuso || frequenzaMessa) return;
                Guardiano g = Registro.guardiano();
                if (g == null || !g.imposta(NOME_FREQUENZA, ORDINE_FREQUENZA,
                        ImpostazioniPannello.comandoRipristino(MINIMO_FREQUENZA, nostra, originale))) {
                    System.out.println("[pannello] ripristino della frequenza non al sicuro: non la cambio");
                    return;
                }
                frequenzaMessa = true;
                frequenzaOriginale = originale;
                frequenzaNostra = nostra;
                frequenzaInScrittura = true; // finche' e' attivo il ripristino non toglie il 410 (lo fa questo metodo)
                mie = riaccensioni;
            }
            try {
                ImpostazioniPannello.scrivi(MINIMO_FREQUENZA, nostra);
            } catch (IOException e) {
                synchronized (serratura) {
                    frequenzaInScrittura = false; // il 410 resta: se il valore e' cambiato lo rimette chi ripristina
                    serratura.notifyAll();
                }
                throw e;
            }
            System.out.println("[pannello] a pannello spento il compositore e' sotto la frequenza massima: frequenza minima portata a " + nostra);
            boolean superata;
            synchronized (serratura) {
                frequenzaInScrittura = false;
                serratura.notifyAll(); // sveglia chiudi(), se aspetta
                superata = !spento || chiuso || riaccensioni != mie;
                if (!superata) return;
                frequenzaMessa = false; // la annulliamo noi qui sotto
                frequenzaOriginale = null;
            }
            // riaccensione avvenuta mentre scrivevamo: la riaccensione non ha potuto annullare un valore non
            // ancora scritto, quindi si annulla adesso (solo se ha ancora il nostro valore)
            if (ImpostazioniPannello.leggi(MINIMO_FREQUENZA).equals(nostra)) {
                if (originale == null) ImpostazioniPannello.rimuovi(MINIMO_FREQUENZA);
                else ImpostazioniPannello.scrivi(MINIMO_FREQUENZA, originale);
            }
            Guardiano g = Registro.guardiano();
            if (g != null) g.togli(NOME_FREQUENZA);
        } catch (IOException | RuntimeException e) {
            System.out.println("[pannello] frequenza minima non cambiata o non annullata: " + e);
        }
    }

    /** Rimette la frequenza minima dell'utente solo se ha ancora il nostro valore. */
    private static void ripristinaFrequenzaLocked() {
        if (!frequenzaMessa) return;
        Guardiano g = Registro.guardiano();
        try {
            if (ImpostazioniPannello.leggi(MINIMO_FREQUENZA).equals(frequenzaNostra)) {
                if (frequenzaOriginale == null) ImpostazioniPannello.rimuovi(MINIMO_FREQUENZA);
                else ImpostazioniPannello.scrivi(MINIMO_FREQUENZA, frequenzaOriginale);
            } // altrimenti l'utente l'ha cambiata (o la scrittura non e' ancora avvenuta): vince lui
            if (frequenzaInScrittura) {
                // applicaFrequenza sta ancora scrivendo: un valore non ancora nostro non vuol dire "cambiato
                // dall'utente". Il 410 resta registrato (e' sempre sicuro: agisce solo se il valore e' ancora il
                // nostro); lo toglie applicaFrequenza, ad annullamento fatto.
                return;
            }
            if (g != null) g.togli(NOME_FREQUENZA);
            frequenzaMessa = false;
            frequenzaOriginale = null;
        } catch (IOException e) {
            System.out.println("[pannello] frequenza minima non ripristinata ora: " + e);
        }
    }

    // ---- Controllo periodico ----

    private static void avviaControlloLocked() {
        avviaControllo();
    }

    /** Parte il controllo periodico (una sola volta); si puo' chiamare anche senza la serratura. */
    private static void avviaControllo() {
        if (chiuso || !controlloAvviato.compareAndSet(false, true)) return;
        controllo = new Thread("yasde-pannello-controllo") {
            @Override
            public void run() {
                ciclo();
            }
        };
        controllo.setDaemon(true);
        controllo.start();
    }

    private static void ciclo() {
        boolean primo = true;
        boolean svegliPrima = true;
        boolean bloccatoPrima = false;
        try {
            while (true) {
                try {
                    synchronized (serratura) {
                        if (chiuso) return;
                        try {
                            serratura.wait(PERIODO_MS);
                        } catch (InterruptedException e) {
                            return;
                        }
                        if (chiuso) return;
                        // 1. il ritentativo di una riaccensione fallita viene PER PRIMO, in un try a se'
                        if (accensionePendente != 0 && spento) {
                            try {
                                tentaAccendi(accensionePendente);
                            } catch (Throwable t) {
                                logCambiato("ritentativo di riaccensione fallito: " + t);
                            }
                        }
                        // 2. ogni lettura nel proprio try: una che lancia non ferma le altre
                        Boolean chiamata = null;
                        boolean sveglio = svegliPrima;
                        boolean bloccato = bloccatoPrima;
                        Boolean bloccatoNoto = null; // null = non letto: la guardia del telefono sveglio lo tratta come bloccato
                        try {
                            chiamata = NascostePannello.inChiamata();
                        } catch (Throwable t) {
                            logCambiato("stato della chiamata illeggibile: " + t);
                        }
                        try {
                            sveglio = NascostePannello.interattivo();
                        } catch (Throwable t) {
                            logCambiato("stato dello schermo illeggibile: " + t);
                        }
                        try {
                            bloccato = NascostePannello.bloccato();
                            bloccatoNoto = Boolean.valueOf(bloccato);
                        } catch (Throwable t) {
                            logCambiato("stato del blocco illeggibile: " + t);
                        }
                        // assenza del PC prima (ripristini), poi la guardia del wake lock (che rilascia con la stessa scadenza).
                        // Prima il segno di vita, poi l'ora (AssenzaPc.leggi spiega perche'); lo stesso orologio di Registro.segnoDiVita.
                        AssenzaPc.Lettura l = AssenzaPc.leggi(new AssenzaPc.Fonte() {
                            @Override
                            public long ultimoSegno() {
                                return Registro.ultimoSegnoDiVita();
                            }

                            @Override
                            public long ora() {
                                return SystemClock.elapsedRealtime();
                            }
                        });
                        long ora = l.ora;
                        long ultimoSegno = l.ultimoSegno;
                        try {
                            assenza.valuta(ora, ultimoSegno);
                        } catch (Throwable t) {
                            logCambiato("controllo dell'assenza del PC fallito: " + t);
                        }
                        try {
                            tieniSveglio.aggiorna(ultimoSegno != 0, bloccatoNoto, ora, ultimoSegno);
                        } catch (Throwable t) {
                            logCambiato("guardia del telefono sveglio fallita: " + t);
                        }
                        try {
                            if (chiamata != null && chiamata != inChiamata) {
                                inChiamata = chiamata;
                                if (chiamata) {
                                    if (spento) {
                                        tentaAccendi(MOTIVO_CHIAMATA); // se fallisce resta pendente: si ritenta
                                    } else {
                                        inMano = true;
                                        evento(true, MOTIVO_CHIAMATA);
                                    }
                                } else {
                                    evento(!spento, MOTIVO_FINE_CHIAMATA);
                                }
                            }
                            if (spento && (!sveglio || bloccato)) {
                                // il sistema ha preso il controllo (blocco o riposo): non si forza il pannello a dormire
                                tentaAccendi(MOTIVO_UTENTE);
                            } else if (!primo && ((bloccatoPrima && !bloccato) || (!svegliPrima && sveglio))) {
                                inMano = true;
                                evento(true, MOTIVO_UTENTE); // sblocco o risveglio a pannello gia' acceso
                            }
                        } catch (Throwable t) {
                            logCambiato("controllo fallito: " + t);
                        }
                        primo = false;
                        svegliPrima = sveglio;
                        bloccatoPrima = bloccato;
                    }
                } catch (Throwable t) {
                    logCambiato("giro del controllo fallito: " + t); // il controllo non muore in silenzio
                }
            }
        } finally {
            uscitaDelControllo();
        }
    }

    /**
     * Il controllo esce (chiusura, o un'uscita inattesa): se non e' una chiusura il telefono resta com'era (pannello
     * riacceso, frequenza e timeout dell'utente, come all'assenza del PC) e il controllo puo' ripartire al prossimo
     * segno di vita. Il wake lock non serve toglierlo qui: lo stato voluto scade da solo (TieniSveglio).
     */
    private static void uscitaDelControllo() {
        if (chiuso) return;
        controlloAvviato.set(false);
        synchronized (serratura) {
            try {
                if (spento) tentaAccendi(MOTIVO_ASSENTE);
                ripristinaFrequenzaLocked();
                ripristinaTimeoutLocked();
                evento(!spento, MOTIVO_ASSENTE);
            } catch (Throwable t) {
                System.out.println("[pannello] controllo fermo e telefono non rimesso com'era: " + t);
            }
        }
        System.out.println("[pannello] il controllo periodico e' uscito: telefono rimesso com'era");
    }

    /**
     * Ripresa dopo l'assenza del PC: timeout di nuovo al massimo, con il ripristino registrato nel guardiano PRIMA
     * (se non e' al sicuro non si cambia nulla). Il pannello resta com'e'. Si puo' ritentare: ritorna false se
     * qualcosa non e' riuscito. Sotto la serratura.
     */
    private static boolean riprendiDaAssenzaLocked() {
        Guardiano g = Registro.guardiano();
        if (g == null) {
            logCambiato("ripresa dopo l'assenza del PC: guardiano assente, il timeout non si cambia");
            return false;
        }
        try {
            String chiave = ImpostazioniPannello.TIMEOUT_SCHERMO;
            String massimo = Integer.toString(ImpostazioniPannello.TIMEOUT_MASSIMO);
            if (!timeoutMesso) {
                String originale = ImpostazioniPannello.leggi(chiave);
                if (originale.equals(massimo)) {
                    timeoutMesso = true; // gia' al massimo per scelta dell'utente: niente da ripristinare
                    timeoutOriginale = null;
                } else {
                    boolean assente = originale.equals("null");
                    if (!assente && !originale.matches("[0-9]{1,10}")) {
                        throw new IOException("timeout dello schermo illeggibile: " + originale);
                    }
                    if (!g.imposta(NOME_TIMEOUT, ORDINE_TIMEOUT,
                            ImpostazioniPannello.comandoRipristino(chiave, massimo, assente ? null : originale))) {
                        logCambiato("ripresa dopo l'assenza del PC: " + NON_AL_SICURO);
                        return false;
                    }
                    timeoutMesso = true; // da qui il ripristino e' dovuto anche se la scrittura fallisse
                    timeoutOriginale = originale;
                    timeoutAssente = assente;
                }
            }
            if (!ImpostazioniPannello.leggi(chiave).equals(massimo)) ImpostazioniPannello.scrivi(chiave, massimo);
            return true;
        } catch (IOException e) {
            logCambiato("ripresa dopo l'assenza del PC: timeout non portato al massimo, ritento: " + e);
            return false;
        }
    }

    // ---- Chiusura ----

    /** Chiusura ordinata: pannello acceso, impostazioni ripristinate, guardiano ripulito. */
    private static void chiudi() {
        boolean eraSpento;
        synchronized (serratura) {
            if (chiuso) return;
            chiuso = true;
            serratura.notifyAll(); // sveglia il controllo: trovera' chiuso
            eraSpento = spento;
            if (spento && !tentaAccendi(MOTIVO_CHIUSURA)) {
                // resta nel guardiano, che riaccende alla fine
                System.out.println("[pannello] pannello non riacceso alla chiusura: lo fa il guardiano");
            }
            // Se applicaFrequenza sta ancora scrivendo si aspetta (al massimo 2 s, le chiusure dei pezzi ne hanno 3):
            // altrimenti il comando di scrittura potrebbe finire dopo l'uscita del servizio. Se il limite scade il
            // 410 resta al guardiano, che lo esegue comunque (agisce solo se il valore e' ancora il nostro).
            long limite = System.currentTimeMillis() + 2000;
            while (frequenzaInScrittura && System.currentTimeMillis() < limite) {
                try {
                    serratura.wait(100);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
            ripristinaFrequenzaLocked();
            ripristinaTimeoutLocked();
            if (ascolto != null) ascolto.ferma();
            ascolto = null;
        }
        // per ultimo: prima il pannello e le impostazioni dell'utente, poi il telefono torna libero di addormentarsi
        if (tieniSveglio != null) tieniSveglio.chiudi();
        System.out.println("[pannello] chiuso" + (eraSpento ? " (pannello riacceso)" : ""));
    }

    /** Prepara l'evento (sotto la serratura); lo manda il thread degli eventi, fuori dalla serratura. */
    private static void evento(boolean acceso, int motivo) {
        eventi.offer(new byte[] {(byte) (acceso ? 1 : 0), (byte) motivo});
    }
}
