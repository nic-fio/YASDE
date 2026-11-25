package it.yasde;

/** Scelta della frequenza voluta a pannello spento: parte pura, senza Android, cosi' si prova sulla JVM. */
final class Frequenze {
    private Frequenze() {}

    /**
     * La massima tra i modi con la STESSA risoluzione del modo attivo (`attivo` = indice nell'elenco): su un
     * display dove la frequenza piu' alta esiste solo a una risoluzione minore, scrivere quella come minimo
     * potrebbe far cambiare risoluzione ad Android.
     */
    static float massimaStessaRisoluzione(int attivo, int[] larghezza, int[] altezza, float[] hz) {
        float massima = 0;
        for (int i = 0; i < hz.length; i++) {
            if (larghezza[i] == larghezza[attivo] && altezza[i] == altezza[attivo] && hz[i] > massima) massima = hz[i];
        }
        return massima;
    }

    /**
     * Frequenza voluta: la minore tra la massima e il tetto scelto dall'utente (`peak_refresh_rate`, solo se
     * numerico e maggiore di 0; «null» o altro = nessun tetto).
     */
    static float voluta(float massima, String tettoUtente) {
        if (tettoUtente != null && tettoUtente.matches("[0-9]{1,4}(\\.[0-9]{1,4})?")) {
            float tetto = Float.parseFloat(tettoUtente);
            if (tetto > 0 && tetto < massima) return tetto;
        }
        return massima;
    }
}
