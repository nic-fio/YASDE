package it.yasde;

import java.util.ArrayList;
import java.util.List;

/**
 * Logica (senza Android, provabile sulla JVM) di VIDEO_PAGINA: ogni pagina apre un task nuovo, e i
 * task che c'erano PRIMA del lancio si tolgono, cosi' le pagine non si accumulano. Si tolgono solo
 * quelli letti prima del lancio (mai altri) e solo dopo aver visto il task nuovo sullo schermo:
 * lo schermo non resta mai vuoto, quindi la guardia «app chiusa da sola» non puo' scattare.
 */
final class PaginaTask {
    /** Le operazioni sullo schermo virtuale (le fornisce Sessione). */
    interface Schermo {
        List<Integer> task();

        boolean togli(int idTask);

        void attendi() throws InterruptedException;

        /** True se il tempo totale concesso a VIDEO_PAGINA (lancio compreso) e' finito. */
        boolean scaduto();

        void riga(String testo);
    }

    private PaginaTask() {}

    /**
     * Quali task togliere, dati gli id letti prima del lancio e quelli di adesso: nessuno se non e'
     * comparso almeno un task nuovo (lancio non visibile ancora); altrimenti i vecchi ancora presenti.
     */
    static List<Integer> daTogliere(List<Integer> prima, List<Integer> dopo) {
        List<Integer> out = new ArrayList<Integer>();
        boolean nuovo = false;
        for (int t : dopo) {
            if (!prima.contains(t)) nuovo = true;
        }
        if (!nuovo) return out;
        for (int t : prima) {
            if (dopo.contains(t) && !out.contains(t)) out.add(t);
        }
        return out;
    }

    /**
     * Dopo un lancio riuscito: aspetta (al massimo `maxAttese` attese) che il task nuovo compaia, poi
     * toglie i vecchi. Se non compare non toglie nulla. Ritorna quanti ne ha tolti.
     */
    static int esegui(Schermo s, List<Integer> prima, int maxAttese) throws InterruptedException {
        for (int i = 0; ; i++) {
            List<Integer> via = daTogliere(prima, s.task());
            if (!via.isEmpty()) {
                for (int t : via) {
                    boolean ok = s.togli(t);
                    s.riga("pagina: task " + t + (ok ? " tolto" : " NON tolto"));
                }
                return via.size();
            }
            if (i >= maxAttese || s.scaduto()) return 0;
            s.attendi();
        }
    }
}
