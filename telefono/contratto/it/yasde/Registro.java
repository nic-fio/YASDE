package it.yasde;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Punto d'incontro tra il servizio (cartella base) e i pezzi (video, input, ...):
 * i pezzi si registrano qui, il servizio legge da qui. Così le due cartelle si
 * compilano una senza l'altra. Tutto thread-safe.
 */
public final class Registro {
    /** Chi manda gli eventi al PC: lo imposta il servizio. */
    public interface Eventi {
        void manda(int tipo, byte[] corpo);
    }

    public static final class Intervallo {
        public final int da, a;
        public final GestoreComandi gestore;

        Intervallo(int da, int a, GestoreComandi gestore) {
            this.da = da;
            this.a = a;
            this.gestore = gestore;
        }
    }

    private static final Map<String, GestoreCanale> canali = new ConcurrentHashMap<>();
    private static final List<Intervallo> comandi = new ArrayList<>();
    private static final List<Runnable> chiusure = new ArrayList<>();
    private static volatile Eventi eventi;
    private static volatile Guardiano guardiano;

    private Registro() {}

    public static void registraCanale(String prefisso, GestoreCanale g) {
        canali.put(prefisso, g);
    }

    public static GestoreCanale canale(String prefisso) {
        return canali.get(prefisso);
    }

    public static void registraComandi(int daTipo, int aTipo, GestoreComandi g) {
        synchronized (comandi) {
            comandi.add(new Intervallo(daTipo, aTipo, g));
        }
    }

    /** Il gestore per un tipo di messaggio, o null. */
    public static GestoreComandi comandi(int tipo) {
        synchronized (comandi) {
            for (Intervallo i : comandi) {
                if (tipo >= i.da && tipo <= i.a) return i.gestore;
            }
        }
        return null;
    }

    /**
     * Registra ciò che un pezzo deve fare alla chiusura ordinata del servizio
     * (per esempio chiudere le sessioni video). Il servizio le esegue prima dei
     * ripristini del guardiano. Non sostituisce il guardiano: se il servizio
     * muore di colpo queste non girano.
     */
    public static void registraChiusura(Runnable r) {
        synchronized (chiusure) {
            chiusure.add(r);
        }
    }

    /** Lo chiama il servizio, una volta, alla chiusura. Un pezzo che fallisce non ferma gli altri. */
    public static void eseguiChiusure() {
        List<Runnable> copia;
        synchronized (chiusure) {
            copia = new ArrayList<>(chiusure);
            chiusure.clear();
        }
        for (Runnable r : copia) {
            try {
                r.run();
            } catch (Throwable t) {
                System.out.println("chiusura di un pezzo fallita: " + t);
            }
        }
    }

    public static void impostaEventi(Eventi e) {
        eventi = e;
    }

    /** Manda un evento al PC; senza canale comandi aperto non fa nulla. */
    public static void mandaEvento(int tipo, byte[] corpo) {
        Eventi e = eventi;
        if (e != null) e.manda(tipo, corpo);
    }

    public static void impostaGuardiano(Guardiano g) {
        guardiano = g;
    }

    /** Può essere null se il servizio non è partito (prove isolate di un pezzo). */
    public static Guardiano guardiano() {
        return guardiano;
    }

    // ---- Segni di vita del PC (23 nov sera; docs/protocollo.md §2.1 e §2.9) ----

    /** Senza segni di vita del PC per questo tempo il telefono smette di tenersi sveglio. */
    public static final long PC_ASSENTE_MS = 30L * 60L * 1000L;

    private static volatile long ultimoSegno; // 0 = nessun segno ancora (nessun SALUTO)
    private static final List<Runnable> ascoltatoriVita = new ArrayList<>();

    /**
     * Lo chiama il servizio per OGNI messaggio ricevuto dal PC sul canale comandi
     * (SALUTO compreso), prima di smistarlo. oraMs = SystemClock.elapsedRealtime()
     * (passata da fuori: così il registro gira anche nelle prove su JVM). Avvisa
     * gli ascoltatori sul thread del chiamante: ogni ascoltatore torna subito
     * (niente attese, niente lavoro lungo: al massimo sveglia un suo thread);
     * un ascoltatore che lancia non ferma gli altri.
     */
    public static void segnoDiVita(long oraMs) {
        ultimoSegno = oraMs > 0 ? oraMs : 1;
        List<Runnable> copia;
        synchronized (ascoltatoriVita) {
            copia = new ArrayList<>(ascoltatoriVita);
        }
        for (Runnable r : copia) {
            try {
                r.run();
            } catch (Throwable t) {
                System.out.println("ascoltatore dei segni di vita fallito: " + t.getClass().getSimpleName());
            }
        }
    }

    /** Ora (elapsedRealtime, ms) dell'ultimo segno di vita del PC; 0 se nessuno ancora. */
    public static long ultimoSegnoDiVita() {
        return ultimoSegno;
    }

    /** Chiamato a ogni segno di vita (vedi segnoDiVita per che cosa puo' fare). */
    // Attenzione per chi si registra: l'ascoltatore puo' essere chiamato in
    // contemporanea alla chiusura del pezzo, o dopo (non c'e' modo di toglierlo), e
    // gira sul thread del canale comandi: non deve fare lavoro ne' comandi esterni,
    // solo svegliare un thread del pezzo.
    public static void registraSegnoDiVita(Runnable r) {
        synchronized (ascoltatoriVita) {
            ascoltatoriVita.add(r);
        }
    }
}
