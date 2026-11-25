package it.yasde;

import java.text.Normalizer;
import java.util.Locale;

/** Confronto dei nomi per la ricerca: niente dipendenze da Android (provato su JVM). */
final class ConfrontoNomi {
    private ConfrontoNomi() {}

    /**
     * Forma di confronto: scomposizione Unicode, tolti i segni combinanti (accenti),
     * poi minuscolo. Cosi' "Città" e "CITTA" coincidono. Ricompone con NFC solo
     * quello che resta, per non lasciare sequenze spezzate.
     */
    static String normalizza(String s) {
        String d = Normalizer.normalize(s, Normalizer.Form.NFD);
        StringBuilder b = new StringBuilder(d.length());
        for (int i = 0; i < d.length(); i++) {
            char c = d.charAt(i);
            int t = Character.getType(c);
            if (t == Character.NON_SPACING_MARK || t == Character.COMBINING_SPACING_MARK
                    || t == Character.ENCLOSING_MARK) {
                continue;
            }
            b.append(c);
        }
        return b.toString().toLowerCase(Locale.ROOT);
    }

    /** `ago` deve essere gia' normalizzato (una volta per ricerca). */
    static boolean contiene(String nome, String ago) {
        return normalizza(nome).contains(ago);
    }
}
