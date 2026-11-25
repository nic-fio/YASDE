package it.yasde;

import java.util.HashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Legge il testo di `dumpsys window windows` e dice, per ogni schermo, se una finestra visibile
 * ha FLAG_SECURE (il sistema oscura l'immagine). Funzione pura, senza Android: si prova sulla JVM.
 */
final class AnalisiFinestre {
    private static final long FLAG_SECURE = 0x2000L;
    private static final Pattern SCHERMO = Pattern.compile("mDisplayId=(\\d+)");
    // "fl=" da solo: non "pfl=" ne' "sfl=". Il valore arriva a fine riga o alla graffa che chiude.
    private static final Pattern FLAGS = Pattern.compile("(?:^|[\\s{])fl=([^\\n}]*)");
    private static final Pattern ESADECIMALE = Pattern.compile("(?:#|0[xX])?[0-9a-fA-F]+");

    /** Esito: schermi con una finestra protetta visibile, e quante finestre visibili non si sono capite. */
    static final class Esito {
        final Map<Integer, Boolean> protetti = new HashMap<>();
        int nonRiconosciute;

        boolean protetto(int schermo) {
            Boolean b = protetti.get(schermo);
            return b != null && b;
        }
    }

    private AnalisiFinestre() {}

    /** Ritorna true/false se il campo e' leggibile, null se non si riconosce il formato. */
    static Boolean protetta(String valore) {
        String[] parti = valore.trim().split("\\s+");
        if (parti.length == 0 || parti[0].isEmpty()) return null;
        String primo = parti[0];
        boolean numerico = ESADECIMALE.matcher(primo).matches()
                && (primo.startsWith("#") || primo.toLowerCase().startsWith("0x") || primo.matches(".*[0-9].*"));
        if (numerico) {
            String n = primo.startsWith("#") ? primo.substring(1)
                    : (primo.toLowerCase().startsWith("0x") ? primo.substring(2) : primo);
            try {
                return (Long.parseLong(n, 16) & FLAG_SECURE) != 0;
            } catch (NumberFormatException e) {
                return null;
            }
        }
        // Nomi separati da spazi (formato di AOSP); il campo finisce al primo "chiave=valore".
        boolean nomi = false;
        for (String p : parti) {
            if (p.indexOf('=') >= 0) break;
            if (!p.matches("[A-Z][A-Z0-9_]*")) return null;
            nomi = true;
            if (p.equals("SECURE")) return true;
        }
        return nomi ? Boolean.FALSE : null;
    }

    static Esito analizza(String dump) {
        Esito e = new Esito();
        int pos = 0;
        while (true) {
            int i = dump.indexOf("\n  Window #", pos);
            if (i < 0) return e;
            int j = dump.indexOf("\n  Window #", i + 1);
            String blocco = j < 0 ? dump.substring(i) : dump.substring(i, j);
            pos = i + 1;
            Matcher m = SCHERMO.matcher(blocco);
            if (!m.find() || !blocco.contains("isReadyForDisplay()=true")) continue;
            int schermo;
            try {
                schermo = Integer.parseInt(m.group(1));
            } catch (NumberFormatException ex) {
                continue; // id illeggibile: salta solo questa finestra
            }
            Matcher f = FLAGS.matcher(blocco);
            Boolean p = f.find() ? protetta(f.group(1)) : null;
            if (p == null) {
                e.nonRiconosciute++;
                continue;
            }
            Boolean prima = e.protetti.get(schermo);
            e.protetti.put(schermo, p || (prima != null && prima));
        }
    }
}
