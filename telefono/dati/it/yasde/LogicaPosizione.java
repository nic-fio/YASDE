package it.yasde;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.List;

/**
 * Parte pura di POSIZIONE (0x90, docs/protocollo.md §2.11): scelta della posizione piu' recente,
 * arrotondamento a 0,005 gradi (circa 500 m; millesimi interi multipli di 5: la posizione precisa non lascia mai il telefono),
 * eta', regola delle 6 ore e codifica dell'OK. Nessuna classe di Android: si prova sulla JVM.
 * RISERVATEZZA: niente qui scrive coordinate o localita' in un log o in un messaggio d'errore.
 */
final class LogicaPosizione {
    static final int ESITO_NOTA = 0, ESITO_SPENTA = 1, ESITO_NEGATO = 2, ESITO_NESSUNA = 3;
    static final int FORNITORE_RETE = 0, FORNITORE_GPS = 1, FORNITORE_FUSO = 2, FORNITORE_PASSIVO = 3;
    static final long ETA_IGNOTA = 0xffffffffL;
    /** Una posizione piu' vecchia di cosi' non basta: se il PC lo permette se ne chiede una nuova. */
    static final long SEI_ORE_MS = 6L * 3600000L;
    static final int MAX_LOCALITA = 127;

    private LogicaPosizione() {}

    /** Una posizione nota di un fornitore. etaMs < 0 = eta' ignota. Le coordinate sono in gradi, esatte. */
    static final class Candidata {
        final double lat, lon;
        final long etaMs;
        final int fornitore;

        Candidata(double lat, double lon, long etaMs, int fornitore) {
            this.lat = lat;
            this.lon = lon;
            this.etaMs = etaMs;
            this.fornitore = fornitore;
        }
    }

    /**
     * Gradi -> MILLESIMI di grado interi arrotondati al multiplo piu' vicino di 0,005 gradi (circa 555 m
     * nord-sud), a meta' lontano da zero: il risultato e' sempre un multiplo di 5 (0,0025 -> 5;
     * -0,0025 -> -5; 45,4567 -> 45455). Limiti: +-90000 (latitudine) e +-180000 (longitudine).
     */
    static int millesimi5(double gradi) {
        double passi = Math.abs(gradi) * 1000.0 / 5.0;   // numero di passi da 0,005 gradi
        long r = (long) Math.floor(passi + 0.5) * 5L;
        return (int) (gradi < 0 ? -r : r);
    }

    static boolean valida(double lat, double lon) {
        return !Double.isNaN(lat) && !Double.isNaN(lon) && !Double.isInfinite(lat) && !Double.isInfinite(lon)
                && Math.abs(lat) <= 90.0 && Math.abs(lon) <= 180.0;
    }

    /**
     * La piu' recente (eta' minore; l'eta' ignota perde su quella nota). A parita' vale l'ordine della
     * lista. Le coordinate non valide (NaN, fuori dai limiti) si scartano. Null se non c'e' nulla.
     */
    static Candidata scegli(List<Candidata> candidate) {
        Candidata migliore = null;
        for (Candidata c : candidate) {
            if (c == null || !valida(c.lat, c.lon)) continue;
            if (migliore == null || meglio(c, migliore)) migliore = c;
        }
        return migliore;
    }

    private static boolean meglio(Candidata c, Candidata m) {
        if (c.etaMs < 0) return false;
        if (m.etaMs < 0) return true;
        return c.etaMs < m.etaMs;
    }

    /** Regola della richiesta singola: il PC la permette E manca una posizione o e' ignota o ha piu' di 6 ore. */
    static boolean serveRichiesta(boolean permessa, Candidata scelta) {
        if (!permessa) return false;
        if (scelta == null || scelta.etaMs < 0) return true;
        return scelta.etaMs > SEI_ORE_MS;
    }

    /** Freno sul telefono: al piu' una richiesta nuova ogni 10 minuti, qualunque cosa chieda il PC. */
    static final long INTERVALLO_RICHIESTE_MS = 10L * 60000L;

    /** Ricorda quando e' partita l'ultima richiesta nuova; l'orologio (monotono, in ms) lo passa il chiamante. */
    static final class Freno {
        private long ultima;
        private boolean avuta;

        /**
         * Si puo' chiedere? Si' se non ce n'e' state, se sono passati almeno 10 minuti esatti, o se
         * l'orologio e' tornato indietro (il segno lasciato non vale piu': meglio una richiesta in piu'
         * che un widget che non si aggiorna mai piu'; il freno riparte dalla prossima richiesta).
         */
        synchronized boolean puoiChiedere(long ora) {
            if (!avuta || ora < ultima) return true;
            return ora - ultima >= INTERVALLO_RICHIESTE_MS;
        }

        synchronized void registra(long ora) {
            ultima = ora;
            avuta = true;
        }
    }

    /**
     * Va fatta una richiesta nuova? La regola del protocollo (serveRichiesta) E il freno del telefono:
     * dentro i 10 minuti si risponde con l'ultima posizione nota, come se il PC non l'avesse permessa.
     */
    static boolean richiestaConsentita(boolean permessa, Candidata scelta, Freno freno, long ora) {
        return serveRichiesta(permessa, scelta) && freno.puoiChiedere(ora);
    }

    /** Eta' in secondi per il messaggio (u32): 0xffffffff se ignota, mai negativa, mai oltre u32. */
    static long etaSecondi(long etaMs) {
        if (etaMs < 0) return ETA_IGNOTA;
        return Math.min(etaMs / 1000L, ETA_IGNOTA - 1);
    }

    /** L'esito da mandare: gia' arrotondato, nessuna coordinata precisa. */
    static final class Risultato {
        final int esito, latMill, lonMill, fornitore;
        final long etaSec;
        final String localita;

        private Risultato(int esito, int latMill, int lonMill, long etaSec, int fornitore, String localita) {
            this.esito = esito;
            this.latMill = latMill;
            this.lonMill = lonMill;
            this.etaSec = etaSec;
            this.fornitore = fornitore;
            this.localita = localita == null ? "" : localita;
        }

        static Risultato senzaPosizione(int esito) {
            return new Risultato(esito, 0, 0, ETA_IGNOTA, FORNITORE_RETE, "");
        }

        /** Dalla candidata scelta: arrotonda ora; la localita' (se c'e') riguarda le coordinate arrotondate. */
        static Risultato nota(Candidata c, String localita) {
            return new Risultato(ESITO_NOTA, millesimi5(c.lat), millesimi5(c.lon), etaSecondi(c.etaMs), c.fornitore, localita);
        }
    }

    /** Risposta immediata di POSIZIONE: l'esito e se un lavoro in sottofondo e' stato avviato. */
    static final class Risposta {
        final Risultato risultato;
        final boolean ricercaAvviata;

        Risposta(Risultato risultato, boolean ricercaAvviata) {
            this.risultato = risultato;
            this.ricercaAvviata = ricercaAvviata;
        }
    }

    /**
     * u8 esito, i32 lat, i32 lon (millesimi di grado, multipli di 5; 0 se esito != 0), u32 eta', u8 fornitore, stringa localita'
     * (2 byte di lunghezza + 0-127 byte UTF-8), u8 ricerca avviata: 17 + n byte.
     */
    static byte[] codifica(Risposta risposta) throws IOException {
        Risultato r = risposta.risultato;
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        DataOutputStream o = new DataOutputStream(b);
        boolean nota = r.esito == ESITO_NOTA;
        o.writeByte(r.esito);
        o.writeInt(nota ? r.latMill : 0);
        o.writeInt(nota ? r.lonMill : 0);
        o.writeInt((int) (nota ? r.etaSec : ETA_IGNOTA));
        o.writeByte(nota ? r.fornitore : FORNITORE_RETE);
        CampiDati.scriviStringa(o, nota ? r.localita : "", MAX_LOCALITA);
        o.writeByte(risposta.ricercaAvviata ? 1 : 0);
        return b.toByteArray();
    }

    /** Toglie NUL e gli altri caratteri di controllo: un NUL farebbe rifiutare al PC l'intera risposta. */
    static String senzaControllo(String n) {
        StringBuilder b = new StringBuilder(n.length());
        for (int i = 0; i < n.length(); i++) {
            char c = n.charAt(i);
            if (!Character.isISOControl(c)) b.append(c);
        }
        return b.toString();
    }

    /** Chi sa dire il nome di una localita' dalle coordinate (in millesimi di grado, arrotondati a 0,005); puo' lanciare o tardare. */
    interface Cercatore {
        /** Il nome, o null/vuoto se non lo conosce. */
        String cerca(int latMill, int lonMill) throws Exception;
    }

    /**
     * Il nome della localita' per le coordinate GIA' arrotondate, con scadenza. Qualunque problema (nome
     * assente, Geocoder lento oltre il limite, eccezione, lettura precedente ancora in corso) da' il nome
     * vuoto: mai un errore, e nessun testo dell'eccezione (potrebbe contenere coordinate).
     */
    static String nomeLocalita(LetturaUnica<String> lettura, final Cercatore cercatore, final int latMill, final int lonMill, long scadenzaMs) {
        try {
            String n = lettura.esegui(new LetturaUnica.Lavoro<String>() {
                @Override
                public String esegui() throws Exception {
                    return cercatore.cerca(latMill, lonMill);
                }
            }, scadenzaMs);
            return n == null ? "" : CampiDati.tronca(senzaControllo(n).trim(), MAX_LOCALITA);
        } catch (LetturaUnica.Occupata | LetturaUnica.Scaduta | LetturaUnica.Fallita e) {
            return "";
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return "";
        }
    }

    /**
     * Stato in memoria di POSIZIONE (mai su disco): freno di 10 minuti, un solo lavoro in sottofondo
     * alla volta, la posizione trovata dall'ultima ricerca e il nome della localita' con le coordinate
     * arrotondate a cui si riferisce. Thread-safe; l'orologio (monotono, ms) lo passa il chiamante.
     */
    static final class StatoPosizione {
        static final int NIENTE = 0, SOLO_NOME = 1, CERCA = 2;

        // Freni separati: un «solo nome» (Geocoder assente o senza risultato) non consuma il freno delle ricerche di rete.
        private final Freno frenoRicerca = new Freno();
        private final Freno frenoNome = new Freno();
        private boolean inCorso, chiuso;
        private Candidata trovata;
        private long trovataIn;
        private boolean haNome;
        private int nomeLat, nomeLon;
        private String nome = "";

        /**
         * Decide se avviare il lavoro in sottofondo: NIENTE, SOLO_NOME (la posizione nota non ha ancora il
         * nome) o CERCA (ricerca di rete, poi il nome). Serve il permesso del PC, nessun lavoro in corso,
         * il servizio non chiuso e il freno di 10 minuti (uno per le ricerche, uno per i «solo nome»; parte
         * quando il lavoro parte, anche se poi fallisce). `cercaPossibile` = il fornitore di rete e' disponibile.
         */
        synchronized int tentaAvvio(boolean permessa, Candidata scelta, boolean cercaPossibile, boolean nomeAbilitato, boolean nomeNoto, long ora) {
            if (chiuso || inCorso || !permessa) return NIENTE;
            int che = NIENTE;
            if (cercaPossibile && serveRichiesta(true, scelta) && frenoRicerca.puoiChiedere(ora)) {
                che = CERCA;
                frenoRicerca.registra(ora);
            } else if (nomeAbilitato && scelta != null && !nomeNoto && frenoNome.puoiChiedere(ora)) {
                che = SOLO_NOME;
                frenoNome.registra(ora);
            }
            if (che == NIENTE) return NIENTE;
            inCorso = true;
            return che;
        }

        /**
         * Esegue il lavoro avviato da tentaAvvio: se il servizio e' stato chiuso nel frattempo (tra
         * tentaAvvio e l'avvio del thread) non fa nulla; in ogni caso libera lo stato, anche per un Throwable.
         */
        void eseguiLavoro(Runnable lavoro) {
            try {
                if (chiuso()) return;
                lavoro.run();
            } finally {
                finito();
            }
        }

        /** Il lavoro e' finito (bene, male o per scadenza): se ne puo' avviare un altro, col freno. */
        synchronized void finito() {
            inCorso = false;
        }

        /** Chiusura del servizio: niente piu' lavori, i risultati in ritardo si ignorano. */
        synchronized void chiudi() {
            chiuso = true;
        }

        synchronized boolean chiuso() {
            return chiuso;
        }

        synchronized boolean inCorso() {
            return inCorso;
        }

        /** Tiene la posizione GIA' ARROTONDATA (0,005 gradi): quella precisa non resta in memoria nemmeno sul telefono. */
        synchronized void memorizza(Candidata c, long ora) {
            if (chiuso || c == null) return;
            trovata = new Candidata(millesimi5(c.lat) / 1000.0, millesimi5(c.lon) / 1000.0, c.etaMs, c.fornitore);
            trovataIn = ora;
        }

        /** La posizione trovata dall'ultima ricerca, con l'eta' aggiornata a `ora`; null se non c'e'. */
        synchronized Candidata memorizzata(long ora) {
            if (trovata == null) return null;
            long eta = trovata.etaMs < 0 ? -1 : trovata.etaMs + Math.max(0, ora - trovataIn);
            return new Candidata(trovata.lat, trovata.lon, eta, trovata.fornitore);
        }

        synchronized void impostaNome(int latMill, int lonMill, String n) {
            if (chiuso) return;
            haNome = n != null && !n.isEmpty();
            nomeLat = latMill;
            nomeLon = lonMill;
            nome = haNome ? n : "";
        }

        /** Il nome vale solo per le stesse coordinate arrotondate cui e' stato calcolato. */
        synchronized String nomePer(int latMill, int lonMill) {
            return haNome && nomeLat == latMill && nomeLon == lonMill ? nome : "";
        }
    }
}
