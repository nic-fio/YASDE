package it.yasde;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Analisi del testo di `dumpsys notification --noredact`. Un processo shell non ha il permesso
 * ACCESS_NOTIFICATIONS (misurato sul telefono: INotificationManager risponde "Access denied"),
 * ma ha DUMP, e quel dump elenca le notifiche attive con titolo e testo presi dalle `extras`
 * standard. Parte pura (solo java.*), provata sulla JVM con testi nel formato osservato.
 *
 * Formato (AOSP, sezione "  Notification List:" a 2 spazi di rientro, record a 4 spazi):
 *   NotificationRecord(0x...: pkg=P user=... id=N tag=T ...        <- la tag puo' contenere a-capo
 *     key=...  flags=...  groupKey=...  mHidden=...  mCreationTimeMs=<ms>(<data>)   <- campi a 6 spazi
 *     notification=                                               <- blocco a 8+ spazi
 *         extras={ ... android.title=String (...)  android.text=CharSequenza (...) ... }
 * Un valore che contiene a-capo continua sulle righe seguenti; una riga senza rientro continua
 * il campo precedente (la chiave con un a-capo nella tag).
 * Voci delle extras: `chiave=Classe (valore)`, oppure `chiave=null`, oppure un array
 * (`chiave=Parcelable[] (N)` seguito dalle righe degli elementi). Ogni riga `^ {16}chiave=` apre
 * una voce; solo android.title e android.text accolgono righe di continuazione.
 * Il campo `flags=` e' letto cercando la parola GROUP_SUMMARY: se un'altra versione di Android lo
 * stampasse in forma numerica, i riepiloghi non si riconoscerebbero (comparirebbero tra le notifiche).
 * Fine dei blocchi: dentro `notification=` e `publicNotification=` una riga `      parola=` NON basta a
 * chiuderli (potrebbe essere testo libero, per esempio codice in una chat). `notification=` si chiude
 * solo con il vero `publicNotification=`; il blocco pubblico solo con un campo di DOPO_I_BLOCCHI
 * (dal dump osservato sul telefono): un `      mHidden=...` finto nel testo principale resta testo.
 * Dentro titolo e testo una riga a 16 spazi apre una nuova voce solo nella forma completa di AOSP
 * (`nome=null` o `nome=Classe (...`).
 * Limite residuo: un testo costruito apposta nel blocco pubblico (di norma `None`) con una riga di
 * campo di DOPO_I_BLOCCHI seguita da una riga a due spazi; oppure, nel testo principale, una riga
 * `      publicNotification=` finta o una `                nome=null` / `nome=Classe (...` a 16 spazi
 * (tronca il testo). Questi ultimi casi sono ERRORE quando il vero campo o la vera voce compare poi
 * una seconda volta (campo o nome di voce ripetuto, `publicNotification=` dentro il blocco pubblico,
 * blocco che non si chiude mai).
 * Ogni forma non riconosciuta (righe estranee ai record, record senza notification= o extras, un
 * numero di record diverso dalle righe `key=`) e' contata in `nonRiconosciuti`: il chiamante risponde
 * ERRORE, mai un elenco vuoto o testi vuoti in silenzio.
 */
final class AnalisiNotifiche {
    private static final String INTESTAZIONE_SEZIONE = "  Notification List:";
    private static final Pattern PACCHETTO = Pattern.compile("pkg=(\\S+)");
    private static final Pattern CAMPO = Pattern.compile("^ {6}([A-Za-z]+)=(.*)$");
    private static final Pattern RIGA_CHIAVE = Pattern.compile("^ {6}key=");
    private static final Pattern VOCE_EXTRAS = Pattern.compile("^ {16}([A-Za-z0-9_.$]+)=(.*)$");
    private static final Pattern VALORE_TIPO = Pattern.compile("^[A-Za-z0-9_.$\\[\\]]+ \\((.*)$");
    private static final Pattern NUMERO = Pattern.compile("^=?(\\d+)");
    private static final Pattern QUANDO = Pattern.compile("^ {12}when=(\\d+)");

    private AnalisiNotifiche() {}

    private static final String NOTIFICA = "notification", PUBBLICA = "publicNotification";

    /**
     * Campi che AOSP stampa (a 6 spazi) subito dopo i blocchi notification= e publicNotification=.
     * Ricavati dal dump di NotificationRecord osservato sul telefono, in quest'ordine: publicNotification,
     * stats, mContactAffinity, mPackagePriority, mPackageVisibility, mSystemImportance, mAsstImportance,
     * mRuleImportance, mImportance, ..., mHidden, ..., mCreationTimeMs, ..., mUpdateTimeMs. Basta uno
     * qualunque di questi a chiudere il blocco pubblico, cosi' la scomparsa di un singolo campo in
     * un'altra versione di Android non rompe l'analisi.
     */
    private static final Set<String> DOPO_I_BLOCCHI = new HashSet<>(java.util.Arrays.asList(
            "stats", "mContactAffinity", "mPackagePriority", "mPackageVisibility", "mSystemImportance",
            "mImportance", "mHidden", "mCreationTimeMs", "mUpdateTimeMs"));

    /** Campi che in un record non possono comparire due volte (il secondo e' un testo che imita un campo). */
    private static boolean senzaDoppioni(String nome) {
        return nome.equals(NOTIFICA) || nome.equals(PUBBLICA) || DOPO_I_BLOCCHI.contains(nome) || nome.equals("key");
    }

    /**
     * Nuovo stato del blocco dopo una riga `      nome=`: null = fuori da un blocco. Un nome
     * non previsto dentro un blocco lascia lo stato com'e' (e' testo della notifica).
     */
    private static String prossimoBlocco(String corrente, String nome) {
        if (corrente == null) return nome.equals(NOTIFICA) || nome.equals(PUBBLICA) ? nome : null;
        // notification= si chiude solo con il vero publicNotification=; i campi dopo i blocchi
        // chiudono solo il blocco pubblico: un `      mHidden=...` finto nel testo principale resta testo.
        if (corrente.equals(NOTIFICA)) return nome.equals(PUBBLICA) ? PUBBLICA : corrente;
        return DOPO_I_BLOCCHI.contains(nome) ? null : corrente;
    }

    static final class Esito {
        /** false se il dump non ha la sezione delle notifiche: formato sconosciuto. */
        boolean sezioneTrovata;
        /** forme del dump che non si sono riconosciute (0 = tutto compreso). */
        int nonRiconosciuti;
        /** record (marcatore trovato) dei quali non si e' letta la chiave o il blocco notification=. */
        int nonRiconosciutiRecord;
        final List<CampiDati.Notifica> notifiche = new ArrayList<>();
    }

    private static final class Record {
        String pacchetto = "", chiave = null, gruppo = "", flags = "", titolo = "", testo = "";
        boolean nascosta;
        long tempo;

        boolean riepilogo() {
            return flags.contains("GROUP_SUMMARY");
        }
    }

    static Esito analizza(String dump) {
        Esito e = new Esito();
        String[] righe = dump.split("\n", -1);
        int i = 0;
        while (i < righe.length && !righe[i].replace("\r", "").equals(INTESTAZIONE_SEZIONE)) i++;
        if (i == righe.length) return e;
        e.sezioneTrovata = true;
        i++;
        List<Record> record = new ArrayList<>();
        List<String> corrente = null;
        String blocco = null;
        int righeChiave = 0;
        for (; i < righe.length; i++) {
            String r = righe[i].replace("\r", "");
            // La fine della sezione (altra intestazione a 2 spazi) vale solo fuori dai blocchi
            // notification=/publicNotification=: un testo puo' avere righe che iniziano con due spazi.
            if (blocco == null && r.length() > 2 && r.startsWith("  ") && r.charAt(2) != ' ') break;
            if (RIGA_CHIAVE.matcher(r).find()) righeChiave++;
            if (r.startsWith("    NotificationRecord(")) {
                if (corrente != null) leggi(corrente, record, e);
                corrente = new ArrayList<>();
                if (blocco != null) e.nonRiconosciuti++;   // il record precedente non ha chiuso il blocco
                blocco = null;
            } else if (corrente == null) {
                if (!r.trim().isEmpty()) e.nonRiconosciuti++;
                continue;
            } else {
                Matcher c = CAMPO.matcher(r);
                if (c.matches()) {
                    blocco = prossimoBlocco(blocco, c.group(1));
                }
            }
            corrente.add(r);
        }
        if (blocco != null) e.nonRiconosciuti++;   // un blocco che non si chiude: formato sconosciuto
        if (corrente != null) leggi(corrente, record, e);

        // Conteggio indipendente dal testo libero: tanti record quante righe `key=` ai campi.
        if (record.size() + e.nonRiconosciutiRecord != righeChiave) {
            e.nonRiconosciuti++;
        }

        // I riepiloghi di gruppo si saltano se il gruppo ha figli nell'elenco: la tendina li
        // mostra come intestazione, non come notifica.
        Set<String> gruppiConFigli = new HashSet<>();
        for (Record r : record) {
            if (!r.riepilogo() && !r.gruppo.equals(r.chiave)) gruppiConFigli.add(r.gruppo);
        }
        for (Record r : record) {
            if (r.nascosta) continue;
            if (r.riepilogo() && gruppiConFigli.contains(r.gruppo)) continue;
            e.notifiche.add(new CampiDati.Notifica(r.chiave, r.pacchetto, r.titolo, r.testo, r.tempo));
        }
        return e;
    }

    private static void leggi(List<String> righe, List<Record> out, Esito e) {
        Record rec = new Record();
        Matcher m = PACCHETTO.matcher(righe.get(0));
        if (m.find()) rec.pacchetto = m.group(1);

        // Campi a 6 spazi di rientro; le righe senza rientro continuano il campo precedente;
        // il blocco notification= tiene tutte le righe fino al campo successivo.
        Map<String, StringBuilder> campi = new HashMap<>();
        List<String> blocco = new ArrayList<>();
        StringBuilder ultimo = null;
        String stato = null;
        for (int k = 1; k < righe.size(); k++) {
            String r = righe.get(k);
            Matcher c = CAMPO.matcher(r);
            String nuovo = c.matches() ? prossimoBlocco(stato, c.group(1)) : null;
            boolean testoLibero = c.matches() && stato != null && nuovo != null && nuovo.equals(stato);
            if (testoLibero && (c.group(1).equals(PUBBLICA) || c.group(1).equals(NOTIFICA))) {
                e.nonRiconosciuti++;   // blocco dentro un blocco: non e' il formato di AOSP
            }
            if (c.matches() && !testoLibero) {
                if (campi.containsKey(c.group(1)) && senzaDoppioni(c.group(1))) {
                    e.nonRiconosciuti++;   // campo ripetuto: il secondo e' un testo che imita un campo
                }
                ultimo = new StringBuilder(c.group(2));
                campi.put(c.group(1), ultimo);
                stato = nuovo;
            } else if (NOTIFICA.equals(stato)) {
                blocco.add(r);
            } else if (ultimo != null && !r.startsWith(" ")) {
                ultimo.append('\n').append(r);
            }
        }
        StringBuilder chiave = campi.get("key");
        if (chiave == null || rec.pacchetto.isEmpty() || !campi.containsKey("notification")) {
            e.nonRiconosciutiRecord++;
            e.nonRiconosciuti++;
            return;
        }
        rec.chiave = chiave.toString();
        rec.gruppo = campi.containsKey("groupKey") ? campi.get("groupKey").toString() : rec.chiave;
        rec.flags = campi.containsKey("flags") ? campi.get("flags").toString() : "";
        rec.nascosta = campi.containsKey("mHidden") && campi.get("mHidden").toString().contains("true");
        long creazione = 0;
        if (campi.containsKey("mCreationTimeMs")) {
            Matcher n = NUMERO.matcher(campi.get("mCreationTimeMs").toString());
            if (n.find()) creazione = Long.parseLong(n.group(1));
        }
        long quando = 0;
        for (String r : blocco) {
            Matcher q = QUANDO.matcher(r);
            if (q.find()) {
                quando = Long.parseLong(q.group(1));
                break;
            }
        }
        // Ora mostrata nella tendina: `when` della notifica; se manca (0), l'ora di creazione.
        rec.tempo = quando > 0 ? quando : creazione;
        if (!extras(blocco, rec)) e.nonRiconosciuti++;
        out.add(rec);
    }

    /** Titolo e testo dal blocco `extras={ ... }` della notifica; false se il blocco manca o e' vuoto. */
    private static boolean extras(List<String> blocco, Record rec) {
        boolean dentro = false;
        int voci = 0;
        java.util.Set<String> nomi = new HashSet<>();
        String voce = null;
        StringBuilder valore = null;
        boolean haParentesi = false;   // false: voce senza "Classe (valore)", per esempio `=null`
        for (String r : blocco) {
            if (!dentro) {
                if (r.trim().equals("extras={")) dentro = true;
                continue;
            }
            if (r.equals("            }")) break;
            Matcher m = VOCE_EXTRAS.matcher(r);
            boolean unaVoce = m.matches();
            if (unaVoce && valore != null && haParentesi && (voce.equals("android.title") || voce.equals("android.text"))) {
                // Dentro titolo o testo una riga apre una voce solo nella forma completa di AOSP
                // (`nome=null` o `nome=Classe (...`): `x=5` in un codice incollato resta testo.
                String resto0 = m.group(2);
                unaVoce = resto0.equals("null") || VALORE_TIPO.matcher(resto0).matches();
            }
            if (unaVoce) {
                chiudi(voce, valore, haParentesi, rec);
                voci++;
                voce = m.group(1);
                // Un Bundle non ha chiavi doppie: un nome ripetuto e' un testo che ne imita una.
                if (!nomi.add(voce)) return false;
                String resto = m.group(2);
                Matcher t = VALORE_TIPO.matcher(resto);
                haParentesi = t.matches();
                valore = new StringBuilder(haParentesi ? t.group(1) : resto);
            } else if (valore != null && haParentesi && (voce.equals("android.title") || voce.equals("android.text"))) {
                valore.append('\n').append(r);
            }
        }
        chiudi(voce, valore, haParentesi, rec);
        return dentro && voci > 0;
    }

    private static void chiudi(String voce, StringBuilder valore, boolean haParentesi, Record rec) {
        if (voce == null || valore == null || !haParentesi) return;
        String v = valore.toString();
        if (v.endsWith(")")) v = v.substring(0, v.length() - 1);
        if (voce.equals("android.title")) rec.titolo = v;
        else if (voce.equals("android.text")) rec.testo = v;
    }
}
