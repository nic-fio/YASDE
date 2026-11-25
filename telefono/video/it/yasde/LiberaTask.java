package it.yasde;

import java.util.List;

/**
 * Logica (senza Android, provabile sulla JVM) della chiusura con "Chiudi": si tolgono tutti i task
 * dello schermo, poi si aspetta (con un tetto) che lo schermo si svuoti. Mai Indietro.
 */
final class LiberaTask {
    /** Le operazioni sullo schermo virtuale (le fornisce Sessione). */
    interface Schermo {
        List<Integer> task();

        boolean togli(int idTask);

        void attendi() throws InterruptedException;

        void riga(String testo);
    }

    private LiberaTask() {}

    /** Ritorna quanti task ha tolto. Aspetta al massimo `maxAttese` volte, fermandosi appena lo schermo e' vuoto. */
    static int esegui(Schermo s, int maxAttese) throws InterruptedException {
        int tolti = 0;
        for (int t : s.task()) {
            boolean ok = s.togli(t);
            s.riga("task " + t + (ok ? " tolto" : " NON tolto"));
            tolti++;
        }
        for (int i = 0; i < maxAttese && !s.task().isEmpty(); i++) s.attendi();
        return tolti;
    }
}
