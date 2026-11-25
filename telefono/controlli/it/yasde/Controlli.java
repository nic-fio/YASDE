package it.yasde;

import java.io.DataInputStream;
import java.io.IOException;
import java.util.Arrays;

/**
 * Interruttori rapidi e «trova il telefono» (docs/protocollo.md §2.10, comandi 0x80-0x8f).
 *
 * - Suoneria e non disturbare cambiati dal widget sono comandi espliciti dell'utente: effetto permanente,
 *   NESSUN ripristino (eccezione documentata nel protocollo).
 * - La torcia accesa da YASDE si spegne alla chiusura ordinata; se il servizio muore di colpo la spegne
 *   il sistema (misurato sul telefono di prova: la torcia di setTorchMode è legata al processo).
 * - «Trova il telefono» (classe Trova) cambia volume, modo e non disturbare SOLO per il tempo dello squillo,
 *   con i ripristini nel guardiano.
 * - Un comando che il telefono rifiuta (permesso mancante, comando assente) risponde ERRORE e da allora il
 *   suo bit in «comandi possibili» è 0. Mai un valore inventato: ciò che non si legge è 0xff.
 *
 * CONTROLLI_CAMBIATI (0x8e) parte solo dopo la prima CONTROLLI_STATO: un thread controlla lo stato al più ogni
 * `periodoMs` (anche per i cambi fatti a mano sul telefono) e subito dopo ogni comando e a fine squillo
 * (`avvisa`); manda l'evento solo se qualcosa è cambiato. Gli eventi partono fuori da ogni lock.
 *
 * Chiusura: `chiudi` ferma «trova» (valori rimessi), aspetta i comandi in corso, spegne la torcia nostra;
 * dopo il suo ritorno nessun evento è in corso e nessuno arriverà; i comandi rispondono errore.
 */
public final class Controlli {
    private static final int CONTROLLI_STATO = 0x80;
    private static final int SUONERIA_IMPOSTA = 0x81;
    private static final int NON_DISTURBARE_IMPOSTA = 0x82;
    private static final int TORCIA_IMPOSTA = 0x83;
    private static final int TROVA_AVVIA = 0x84;
    private static final int TROVA_FERMA = 0x85;
    private static final int CONTROLLI_CAMBIATI = 0x8e;

    /**
     * «Trova il telefono» e' SPENTO nel jar di produzione (decisione dopo le prove sul telefono di prova):
     * 1) `AudioTrack` non si crea dal processo shell («Cannot create AudioTrack», misurato);
     * 2) il passaggio al modo «suona» fa alzare da Android il volume salvato da 0 a 1 senza ritorno: chi tiene
     *    il volume a 0 in vibra lo perde (misurato: volume_ring_speaker ed earpiece da 0 a 1).
     * Con la costante falsa il bit «trova possibile» e' sempre 0, TROVA_AVVIA risponde ERRORE senza toccare nulla
     * (niente guardiano, modo, Non disturbare, volume, suono) e TROVA_FERMA risponde OK senza fare nulla. La classe
     * Trova resta nel sorgente ma non e' raggiungibile. Non e' `final` solo perche' le prove JVM la forzano.
     */
    static volatile boolean TROVA_ABILITATO = false;

    static final int BIT_SUONERIA = 1;
    static final int BIT_NON_DISTURBARE = 2;
    static final int BIT_TORCIA = 4;
    static final int BIT_TROVA = 8;

    private static final int NON_DISPONIBILE = 0xff;
    private static final int TROVA_MIN_S = 5;
    private static final int TROVA_MAX_S = 120;
    private static final long ATTESA_CHIUSURA_MS = 10000;
    private static final byte[] VUOTO = new byte[0];

    private static boolean registrato; // protetto da Controlli.class

    private final Telefono t;
    private final Trova trova;
    private final Registro.Eventi eventi;
    private final long periodoMs;

    private final Object serratura = new Object();
    // tutti protetti da `serratura`
    private boolean chiuso;
    private int uso; // comandi e controlli in corso: la chiusura li aspetta
    private boolean disponibilitaCalcolata;
    private int comandi;
    private boolean svegliato;
    private Thread notificatore;
    private byte[] ultimo; // ultimo stato mandato o risposto
    private boolean torciaNostra;

    Controlli(Telefono t, Guardiano g, Registro.Eventi eventi, long periodoMs, int tentativi, long pausaMs) {
        this.t = t;
        this.eventi = eventi;
        this.periodoMs = periodoMs;
        this.trova = new Trova(t, g, new Runnable() {
            @Override
            public void run() {
                avvisa();
            }
        }, tentativi, pausaMs, 60000);
    }

    /** Lo chiama il servizio (per riflessione) all'avvio. */
    public static void registra() {
        synchronized (Controlli.class) {
            if (registrato) return; // una seconda registrazione (per esempio dalla prova e da Servizio.PEZZI) non fa nulla
            registrato = true;
        }
        java.util.List<String> mancanti = NascosteControlli.verifica();
        System.out.println("[controlli] registrato; API nascoste: " + (mancanti.isEmpty() ? "tutte presenti" : "mancanti " + mancanti));
        final Controlli c = new Controlli(new TelefonoAndroid(), new Guardiano() {
            @Override
            public boolean imposta(String nome, int ordine, String comandoShell) {
                Guardiano g = Registro.guardiano();
                return g != null && g.imposta(nome, ordine, comandoShell);
            }

            @Override
            public void togli(String nome) {
                Guardiano g = Registro.guardiano();
                if (g != null) g.togli(nome);
            }
        }, new Registro.Eventi() {
            @Override
            public void manda(int tipo, byte[] corpo) {
                Registro.mandaEvento(tipo, corpo);
            }
        }, 2000, 10, 1000);
        Registro.registraChiusura(new Runnable() {
            @Override
            public void run() {
                c.chiudi();
            }
        });
        Registro.registraComandi(0x80, 0x8f, new GestoreComandi() {
            @Override
            public byte[] gestisci(int tipo, DataInputStream corpo) throws Exception {
                return c.comando(tipo, corpo);
            }
        });
    }

    // ---- codifica ----

    /** Il corpo di CONTROLLI_STATO e di CONTROLLI_CAMBIATI. Valori sconosciuti: passare 0xff. */
    static byte[] codifica(int suoneria, int nonDisturbare, int torcia, boolean trova, int comandi) {
        return new byte[] {(byte) suoneria, (byte) nonDisturbare, (byte) torcia, (byte) (trova ? 1 : 0), (byte) comandi};
    }

    /** La durata di «trova» fuori intervallo si limita (5-120 s). */
    static int limitaSecondi(int s) {
        return Math.max(TROVA_MIN_S, Math.min(TROVA_MAX_S, s));
    }

    // ---- comandi ----

    byte[] comando(int tipo, DataInputStream corpo) throws Exception {
        if (!entra()) throw new IllegalStateException("servizio in chiusura");
        try {
            return esegui(tipo, corpo);
        } finally {
            esci();
        }
    }

    private byte[] esegui(int tipo, DataInputStream corpo) throws Exception {
        switch (tipo) {
            case CONTROLLI_STATO: {
                disponibilita();
                byte[] ora = leggiStato();
                synchronized (serratura) {
                    ultimo = ora;
                    if (notificatore == null && !chiuso) {
                        notificatore = new Thread("yasde-controlli") {
                            @Override
                            public void run() {
                                ciclo();
                            }
                        };
                        notificatore.setDaemon(true);
                        notificatore.start();
                    }
                }
                return ora;
            }
            case SUONERIA_IMPOSTA: {
                int v = corpo.readUnsignedByte();
                if (v > 2) throw new IllegalArgumentException("modo non valido: 0 suona, 1 vibra, 2 silenzioso");
                richiedi(BIT_SUONERIA, "la suoneria");
                try {
                    t.impostaModoSuoneria(v);
                } catch (Telefono.NonPermesso e) {
                    rifiutato(BIT_SUONERIA, "la suoneria");
                }
                if (t.modoSuoneria() != v) throw new IOException("il telefono non ha cambiato il modo della suoneria");
                avvisa();
                return VUOTO;
            }
            case NON_DISTURBARE_IMPOSTA: {
                int v = corpo.readUnsignedByte();
                if (v > 1) throw new IllegalArgumentException("valore non valido: 0 spento, 1 acceso");
                richiedi(BIT_NON_DISTURBARE, "non disturbare");
                int filtro = v == 1 ? 2 : 1; // acceso = solo priorità, come il riquadro rapido
                try {
                    t.impostaFiltroNonDisturbare(filtro);
                } catch (Telefono.NonPermesso e) {
                    rifiutato(BIT_NON_DISTURBARE, "non disturbare");
                }
                if ((t.filtroNonDisturbare() == 1 ? 0 : 1) != v) throw new IOException("il telefono non ha cambiato non disturbare");
                avvisa();
                return VUOTO;
            }
            case TORCIA_IMPOSTA: {
                int v = corpo.readUnsignedByte();
                if (v > 1) throw new IllegalArgumentException("valore non valido: 0 spenta, 1 accesa");
                richiedi(BIT_TORCIA, "la torcia");
                boolean eraAccesa = false;
                if (v == 1) {
                    try {
                        eraAccesa = t.torcia() == 1;
                    } catch (Exception e) {
                        eraAccesa = false; // non si sa: la accendiamo noi, quindi la spegniamo noi
                    }
                    // se la teneva gia' accesa l'utente non e' nostra: la chiusura la lascia com'e'.
                    // Altrimenti prima si annota, poi si accende: se si muore a meta', la chiusura spegne comunque
                    if (!eraAccesa) {
                        synchronized (serratura) {
                            torciaNostra = true;
                        }
                    }
                }
                try {
                    t.impostaTorcia(v == 1);
                } catch (Telefono.NonPermesso e) {
                    if (v == 1 && !eraAccesa) {
                        synchronized (serratura) {
                            torciaNostra = false;
                        }
                    }
                    rifiutato(BIT_TORCIA, "la torcia");
                }
                if (v == 0) {
                    synchronized (serratura) {
                        torciaNostra = false;
                    }
                }
                avvisa();
                return VUOTO;
            }
            case TROVA_AVVIA: {
                if (!TROVA_ABILITATO) throw new IllegalStateException("«Trova il telefono» non e' disponibile su questa versione");
                int s = limitaSecondi(corpo.readUnsignedShort());
                richiedi(BIT_TROVA, "trova il telefono");
                try {
                    trova.avvia(s * 1000L);
                } catch (Telefono.NonPermesso e) {
                    rifiutato(BIT_TROVA, "trova il telefono");
                }
                avvisa();
                return VUOTO;
            }
            case TROVA_FERMA:
                if (!TROVA_ABILITATO) return VUOTO; // spento: non c'e' nulla da fermare e non si fa nulla
                trova.ferma();
                avvisa();
                return VUOTO;
            default:
                throw new IllegalArgumentException("tipo sconosciuto");
        }
    }

    private void richiedi(int bit, String che) {
        if ((disponibilita() & bit) == 0) throw new IllegalStateException("Il telefono non permette di usare " + che);
    }

    private void rifiutato(int bit, String che) {
        synchronized (serratura) {
            comandi &= ~bit;
            // trova il telefono si serve di suoneria e non disturbare: senza uno dei due non si può
            if (bit == BIT_SUONERIA || bit == BIT_NON_DISTURBARE) comandi &= ~BIT_TROVA;
        }
        throw new IllegalStateException("Il telefono non permette di usare " + che);
    }

    // ---- stato ----

    /** Quali comandi sono possibili: si calcola una volta sola (senza eseguire nulla che cambi). */
    private int disponibilita() {
        synchronized (serratura) {
            if (disponibilitaCalcolata) return comandi;
        }
        int c = 0;
        try {
            if (t.modoSuoneria() >= 0) c |= BIT_SUONERIA;
        } catch (Throwable e) {
            // non si legge: non disponibile
        }
        try {
            if (t.haComandoNonDisturbare() && t.filtroNonDisturbare() > 0) c |= BIT_NON_DISTURBARE;
        } catch (Throwable e) {
            // idem
        }
        try {
            if (t.haTorcia()) c |= BIT_TORCIA;
        } catch (Throwable e) {
            // idem
        }
        try {
            if (TROVA_ABILITATO && (c & BIT_SUONERIA) != 0 && (c & BIT_NON_DISTURBARE) != 0 && t.haComandiRipristino()) c |= BIT_TROVA;
        } catch (Throwable e) {
            // idem
        }
        synchronized (serratura) {
            if (!disponibilitaCalcolata) {
                comandi = c;
                disponibilitaCalcolata = true;
            }
            return comandi;
        }
    }

    /**
     * Lo stato di tutti i valori. Un valore il cui comando non è possibile (bit a 0) è 0xff: il PC non controlla
     * la coerenza tra valore e bit, la tiene questo pezzo.
     */
    private byte[] leggiStato() {
        int c;
        synchronized (serratura) {
            c = comandi;
        }
        int suoneria = NON_DISPONIBILE;
        int nonDisturbare = NON_DISPONIBILE;
        int torcia = NON_DISPONIBILE;
        if ((c & BIT_SUONERIA) != 0) {
            try {
                suoneria = t.modoSuoneria();
            } catch (Throwable e) {
                suoneria = NON_DISPONIBILE;
            }
        }
        if ((c & BIT_NON_DISTURBARE) != 0) {
            try {
                nonDisturbare = t.filtroNonDisturbare() == 1 ? 0 : 1;
            } catch (Throwable e) {
                nonDisturbare = NON_DISPONIBILE;
            }
        }
        if ((c & BIT_TORCIA) != 0) {
            try {
                torcia = t.torcia();
            } catch (Throwable e) {
                torcia = NON_DISPONIBILE;
            }
        }
        return codifica(suoneria, nonDisturbare, torcia, trova.inCorso(), c);
    }

    // ---- avvisi ----

    /** Sveglia il controllo dello stato: lo stato è cambiato adesso (comando nostro, fine di «trova»). */
    private void avvisa() {
        synchronized (serratura) {
            svegliato = true;
            serratura.notifyAll();
        }
    }

    private void ciclo() {
        while (true) {
            synchronized (serratura) {
                if (chiuso) return;
                // si aspetta il periodo INTERO (o un avviso, o la chiusura): serratura.notifyAll() lo chiamano anche
                // i comandi che finiscono, e senza questo giro il controllo scatterebbe a ogni comando
                long limite = System.nanoTime() + periodoMs * 1000000L;
                while (!svegliato && !chiuso) {
                    long resto = limite - System.nanoTime();
                    if (resto <= 0) break;
                    try {
                        serratura.wait(Math.max(1, resto / 1000000L));
                    } catch (InterruptedException e) {
                        return;
                    }
                }
                if (chiuso) return;
                svegliato = false;
            }
            controlla();
        }
    }

    private void controlla() {
        if (!entra()) return;
        try {
            byte[] ora = leggiStato();
            boolean cambiato;
            synchronized (serratura) {
                cambiato = !Arrays.equals(ora, ultimo);
                ultimo = ora;
            }
            if (cambiato) {
                synchronized (serratura) {
                    cambiato = !chiuso; // chiusi nel frattempo: nessun evento
                }
            }
            if (cambiato) eventi.manda(CONTROLLI_CAMBIATI, ora);
        } catch (Throwable e) {
            System.out.println("[controlli] controllo dello stato: " + e); // il thread non muore
        } finally {
            esci();
        }
    }

    // ---- chiusura ----

    private boolean entra() {
        synchronized (serratura) {
            if (chiuso) return false;
            uso++;
            return true;
        }
    }

    private void esci() {
        synchronized (serratura) {
            uso--;
            serratura.notifyAll();
        }
    }

    /** Solo per le prove: il thread degli avvisi (null prima della prima CONTROLLI_STATO). */
    Thread notificatore() {
        synchronized (serratura) {
            return notificatore;
        }
    }

    /** Solo per le prove: un controllo dello stato subito (come quello periodico). */
    void controllaOra() {
        controlla();
    }

    void chiudi() {
        Thread n;
        synchronized (serratura) {
            if (chiuso) return;
            chiuso = true;
            n = notificatore;
            serratura.notifyAll();
        }
        trova.chiudi();
        long limite = System.nanoTime() + ATTESA_CHIUSURA_MS * 1000000L;
        synchronized (serratura) {
            while (uso > 0) {
                long resto = limite - System.nanoTime();
                if (resto <= 0) break;
                try {
                    serratura.wait(Math.max(1, resto / 1000000L));
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }
        if (n != null) {
            try {
                n.join(2000);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
        boolean spegni;
        synchronized (serratura) {
            spegni = torciaNostra;
        }
        if (spegni) {
            try {
                t.impostaTorcia(false);
                synchronized (serratura) {
                    torciaNostra = false;
                }
            } catch (Throwable e) {
                System.out.println("[controlli] torcia non spenta alla chiusura: " + e);
            }
        }
    }
}
