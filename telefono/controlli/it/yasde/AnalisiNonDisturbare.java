package it.yasde;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Legge l'output di `dumpsys notification --zen` e dice se il Non disturbare attivo e' MANUALE e SENZA
 * SCADENZA (solo allora «trova il telefono» puo' spegnerlo e rimetterlo con `cmd notification set_dnd`, che
 * crea sempre un Non disturbare manuale senza scadenza).
 *
 * Formato letto sul telefono di prova (Android 14+, Non disturbare spento): `mInterruptionFilter=1`,
 * `mZenMode=ZEN_MODE_OFF`, poi `mConfig=ZenModeConfig[user=0 ...` con `automaticRules={` e righe
 * `ZenRule[id=...,state=STATE_TRUE,enabled=FALSE,...,zenMode=...,conditionId=condition://...` e
 * `manualRule=ZenRule[id=MANUAL_RULE,state=STATE_FALSE,enabled=TRUE,...,zenMode=...,conditionId=,pkg=android,...`;
 * il blocco compare due volte. I booleani sono MAIUSCOLI (qui si confronta senza distinguere), il conditionId
 * della regola manuale senza scadenza e' VUOTO, una regola automatica DISATTIVATA puo' avere STATE_TRUE.
 * NON visti sul telefono: lo stato «manuale acceso» e «a tempo» (conto alla rovescia: conditionId
 * `condition://android/countdown/...`, trattato come scadenza).
 *
 * Visto a Non disturbare acceso a mano: la regola manuale ha enabled=TRUE, state=STATE_TRUE, conditionId vuoto.
 * «Manuale senza scadenza» = `mZenMode` (in testa) non spento E, nella configurazione corrente, UNA sola
 * `manualRule=ZenRule[` con enabled=TRUE, zenMode non spento e conditionId vuoto E nessun'altra regola
 * insieme enabled=TRUE e state=STATE_TRUE.
 * Qualunque altra cosa, o un testo non riconosciuto, e' «non so» = false.
 */
final class AnalisiNonDisturbare {
    private AnalisiNonDisturbare() {}

    private static final Pattern ZEN_MODE = Pattern.compile("mzenmode=(zen_mode_[a-z_]+)");
    private static final Pattern ZEN_MODE_REGOLA = Pattern.compile("zenmode=(zen_mode_[a-z_]+)");
    // «enabled=» come campo vero della regola: preceduto da «[» o «,» (non la fine di un altro nome di campo)
    private static final Pattern ABILITATA = Pattern.compile("[\\[,]enabled=true");
    private static final Pattern VERA = Pattern.compile("[\\[,]state=state_true");
    private static final Pattern CONDIZIONE = Pattern.compile("conditionid=([^,\\]\\n]*)([,\\]\\n])");

    private static final Pattern FINE_BLOCCO = Pattern.compile("\\n\\s*(msuppressedeffects|zen log)");

    /**
     * Si guarda SOLO la configurazione corrente: il blocco che segue `mConfig=ZenModeConfig[` (non `mConfigs[u=..]`
     * e non la cronologia dei cambi, che nello stesso dump contiene decine di regole passate accese e spente)
     * fino alla prima riga `mSuppressedEffects` o «Zen Log»; se manca quella, fino a `deletedRules=` (se
     * questo precede la regola manuale non la si trova e si rifiuta). Senza inizio o fine: «non so».
     */
    static boolean manualeSenzaScadenza(String dump) {
        if (dump == null || dump.isEmpty()) return false;
        String t = dump.toLowerCase(Locale.ROOT);
        int inizio = t.indexOf("mconfig=zenmodeconfig[");
        if (inizio < 0) return false;
        int fine = -1;
        Matcher f = FINE_BLOCCO.matcher(t);
        if (f.find(inizio)) fine = f.start();
        if (fine < 0) {
            int d = t.indexOf("deletedrules=", inizio);
            if (d >= 0) fine = d;
        }
        if (fine < 0) return false;
        Matcher z = ZEN_MODE.matcher(t);
        if (!z.find() || z.start() > inizio || z.group(1).equals("zen_mode_off")) return false;
        String blocco = t.substring(inizio, fine);
        // le posizioni di ogni «zenrule[»: il pezzo di ciascuna arriva alla riga o alla regola dopo
        List<Integer> inizi = new ArrayList<>();
        int i = blocco.indexOf("zenrule[");
        while (i >= 0) {
            inizi.add(i);
            i = blocco.indexOf("zenrule[", i + 1);
        }
        int manuali = 0;
        for (int k = 0; k < inizi.size(); k++) {
            int da = inizi.get(k);
            int a = k + 1 < inizi.size() ? inizi.get(k + 1) : blocco.length();
            int riga = blocco.indexOf('\n', da);
            if (riga >= 0 && riga < a) a = riga;
            String r = blocco.substring(da, a);
            boolean manuale = da >= 11 && blocco.startsWith("manualrule=", da - 11);
            if (manuale) {
                manuali++;
                if (!ABILITATA.matcher(r).find()) return false;
                // la regola manuale esiste sempre (anche a Non disturbare spento, con state=STATE_FALSE): conta solo se attiva
                if (!VERA.matcher(r).find()) return false;
                Matcher m = ZEN_MODE_REGOLA.matcher(r);
                if (!m.find() || m.group(1).equals("zen_mode_off")) return false;
                Matcher c = CONDIZIONE.matcher(r);
                if (!c.find() || !c.group(1).isEmpty()) return false; // vuoto = senza scadenza; testo troncato o altro = non so
            } else if (ABILITATA.matcher(r).find() && r.contains("state=state_true")) {
                return false; // regola automatica attiva (la manuale accesa ha lo stesso aspetto ma e' esclusa sopra)
            }
        }
        return manuali == 1;
    }
}
