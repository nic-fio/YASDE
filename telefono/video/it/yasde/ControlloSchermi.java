package it.yasde;

import java.util.ArrayList;
import java.util.List;

/**
 * Un solo controllore per tutte le sessioni con un'app: un thread, un dump delle finestre al
 * secondo (ogni dump tiene il lucchetto del WindowManager), analizzato una volta per tutti gli
 * schermi aperti. Parte alla prima sessione registrata e finisce quando non ce ne sono piu'.
 * Il rilevamento dell'app chiusa (evento 1) e' separato e non ne dipende.
 */
final class ControlloSchermi {
    private static final List<Sessione> sessioni = new ArrayList<>();
    private static Thread thread;
    private static boolean nonRiconosciutoSegnalato, lentoSegnalato;

    private ControlloSchermi() {}

    static void registra(Sessione s) {
        synchronized (sessioni) {
            sessioni.add(s);
            if (thread == null) {
                thread = new Thread("yasde-schermi-protetti") {
                    @Override
                    public void run() {
                        ciclo();
                    }
                };
                thread.setDaemon(true);
                thread.start();
            }
        }
    }

    static void togli(Sessione s) {
        synchronized (sessioni) {
            sessioni.remove(s);
        }
    }

    private static List<Sessione> copia() {
        synchronized (sessioni) {
            return new ArrayList<>(sessioni);
        }
    }

    private static void ciclo() {
        try {
            while (true) {
                Thread.sleep(1000);
                synchronized (sessioni) {
                    if (sessioni.isEmpty()) {
                        thread = null;
                        return;
                    }
                }
                List<Sessione> lista = copia();
                long t0 = System.currentTimeMillis();
                AnalisiFinestre.Esito e = AnalisiFinestre.analizza(NascosteVideo.dumpFinestre());
                long ms = System.currentTimeMillis() - t0;
                if (ms > 3000 && !lentoSegnalato) {
                    lentoSegnalato = true;
                    System.out.println("[video] dump delle finestre lento: " + ms + " ms");
                }
                if (e.nonRiconosciute > 0 && !nonRiconosciutoSegnalato) {
                    nonRiconosciutoSegnalato = true;
                    System.out.println("[video] campo fl= delle finestre non riconosciuto (" + e.nonRiconosciute
                            + " finestre): schermo protetto non rilevabile con questo formato");
                }
                for (Sessione s : lista) s.statoProtezione(e.protetto(s.idSchermoApp()));
            }
        } catch (InterruptedException e) {
            // fine
        } catch (Throwable e) {
            System.out.println("[video] controllo schermo protetto non disponibile: " + e);
        }
        // Qualunque sia l'uscita, il controllore puo' ripartire alla prossima registrazione.
        synchronized (sessioni) {
            thread = null;
        }
    }
}
