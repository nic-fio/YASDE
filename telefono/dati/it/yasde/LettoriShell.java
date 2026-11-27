package it.yasde;

import java.util.ArrayList;
import java.util.List;

/**
 * Lettori PURI (solo java.*, provati sulla JVM) dell'output di tre comandi di Android puro, strade di
 * riserva per quando l'API diretta e' negata all'utente shell: `dumpsys media_session`,
 * `cmd wifi status`, `dumpsys telephony.registry`. Il testo non e' un'interfaccia stabile: i lettori
 * sono TOLLERANTI e PRUDENTI. Se il testo non si capisce tornano null (il chiamante risponde «non
 * disponibile»), mai valori a caso. Nessun dato letto finisce in un log o in un'eccezione.
 *
 * Righe cercate in `dumpsys media_session`, dopo «=== DETAILED SESSION INFO ===» fino a «User Records:»
 * (le sessioni stanno sotto «Sessions Stack - have N sessions:», con rientri che dipendono dalla versione):
 *  - inizio di una sessione: riga che finisce con «(userId=N)» seguita da righe piu' rientrate;
 *  - nella sessione: «package=PKG», «state=PlaybackState {state=NOME(N), ... actions=M, ...}» (anche «state=N»),
 *    «metadata: size=N, description=TITOLO, ARTISTA, ALBUM» (oppure «metadata: null»);
 *  - «Total sessions: N» nel riepilogo (serve solo per accorgersi di un testo non capito).
 * Si sceglie: stato 3-6 (in riproduzione), poi 2 (in pausa), poi la prima con un brano; la riga «active=» non si usa
 * (Android la mostra falsa per sessioni vive). Limite noto: un titolo che contiene «, »
 * viene spezzato in titolo e artista nel punto sbagliato (la descrizione di Android non ha separatori).
 * Limite noto 2: un titolo con un a capo puo' fingere righe di una sessione (rientro, «package=», «state=»):
 * l'effetto e' solo sul testo mostrato e sul pacchetto che il clic apre, e il PC valida comunque ogni pacchetto.
 */
final class LettoriShell {
    private LettoriShell() {}

    // ---- dumpsys media_session ----

    static final class Voce {
        String pacchetto = "", titolo = "", artista = "";
        int stato = -1;          // PlaybackState.STATE_*, -1 = non leggibile
        long azioni = -1;        // -1 = non leggibili
    }

    /** Intero (anche lungo) che segue `chiave` in `riga`, o -1. */
    static long numeroDopo(String riga, String chiave) {
        int i = riga.indexOf(chiave);
        if (i < 0) return -1;
        i += chiave.length();
        int j = i;
        while (j < riga.length() && j - i < 18 && Character.isDigit(riga.charAt(j))) j++;
        if (j == i) return -1;
        try {
            return Long.parseLong(riga.substring(i, j));
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    /** Stato dopo «{state=»: «NOME(3)» oppure solo «3»; -1 se non si legge. */
    static int statoDaRiga(String riga) {
        int i = riga.indexOf("{state=");
        if (i < 0) return -1;
        i += 7;
        int j = i;
        while (j < riga.length() && Character.isLetter(riga.charAt(j))) j++;
        if (j > i) {
            if (j >= riga.length() || riga.charAt(j) != '(') return -1;
            i = j + 1;
        }
        long n = numeroDopo(riga.substring(i), "");
        return n < 0 || n > 1000 ? -1 : (int) n;
    }

    /**
     * «TITOLO, ARTISTA, ALBUM» diviso DA DESTRA: ultimo campo = album (scartato), penultimo = artista, il
     * resto = titolo (una virgola nel titolo non lo spezza). Con una sola separazione: titolo e artista;
     * senza separatori: tutto titolo. Campi «null» testuali = vuoti.
     */
    static void dividiDescrizione(String desc, Voce v) {
        String d = desc.trim();
        int ultima = d.lastIndexOf(", ");
        int penultima = ultima > 0 ? d.lastIndexOf(", ", ultima - 1) : -1;
        if (penultima >= 0) {
            v.titolo = parteDescrizione(d.substring(0, penultima));
            v.artista = parteDescrizione(d.substring(penultima + 2, ultima));
        } else if (ultima >= 0) {
            v.titolo = parteDescrizione(d.substring(0, ultima));
            v.artista = parteDescrizione(d.substring(ultima + 2));
        } else {
            v.titolo = parteDescrizione(d);
        }
    }

    private static String parteDescrizione(String s) {
        String t = s.trim();
        return t.equals("null") ? "" : t;
    }

    private static int rientro(String riga) {
        int i = 0;
        while (i < riga.length() && riga.charAt(i) == ' ') i++;
        return i;
    }

    private static boolean finisceConUserId(String riga) {
        String t = riga.trim();
        int i = t.lastIndexOf("(userId=");
        return i > 0 && t.endsWith(")") && numeroDopo(t.substring(i), "(userId=") >= 0;
    }

    /** 2 = in riproduzione (o in arrivo: avanzamento rapido, riavvolgimento, buffering), 1 = in pausa, 0 = gli altri stati. */
    private static int classe(int statoAndroid) {
        if (statoAndroid >= 3 && statoAndroid <= 6) return 2;
        return statoAndroid == 2 ? 1 : 0;
    }

    /**
     * Media dal testo di `dumpsys media_session`: la sessione in riproduzione, altrimenti una in pausa,
     * altrimenti la prima con un brano (metadata con descrizione non vuota), qualunque sia lo stato.
     * Torna null se il testo non si capisce. Nessuna sessione in riproduzione, in pausa o con un brano: Media con stato 0 (esito 0, campi vuoti). Con le azioni non leggibili i comandi sono 0.
     * Una sessione e' una riga che finisce con «(userId=N)» seguita da righe piu' rientrate (le righe
     * «Global priority session is ...» non lo sono): il rientro non e' fisso, si ricava dal testo.
     */
    static CampiTelefonia.Media mediaDaDumpsys(String testo) {
        if (testo == null) return null;
        int d = testo.indexOf("=== DETAILED SESSION INFO ===");
        if (d < 0) return null;
        String[] righe = testo.substring(d).split("\\r?\\n");
        long totale = numeroDopo(testo, "Total sessions: ");
        List<Voce> voci = new ArrayList<>();
        // Se c'e' «Sessions Stack - have N sessions:» con N > 0 le sessioni stanno sotto; altrimenti si guarda tutto.
        int inizio = 1;
        for (int i = 1; i < righe.length; i++) {
            if (righe[i].trim().startsWith("Sessions Stack - have ") && numeroDopo(righe[i], "have ") > 0) {
                inizio = i + 1;
                break;
            }
        }
        for (int i = inizio; i < righe.length; i++) {
            String r = righe[i];
            if (r.startsWith("User Records:")) break;
            String tr = r.trim();
            // «Global priority session is ...» e «Media button session is ...» non sono sessioni.
            if (tr.startsWith("Global priority session is") || tr.startsWith("Media button session is")) continue;
            if (!finisceConUserId(r) || i + 1 >= righe.length) continue;
            int k = rientro(r);
            if (rientro(righe[i + 1]) <= k) continue;
            Voce v = new Voce();
            int j = i + 1;
            for (; j < righe.length && (rientro(righe[j]) > k || righe[j].trim().isEmpty()); j++) {
                String t = righe[j].trim();
                if (t.startsWith("package=") && v.pacchetto.isEmpty()) {
                    v.pacchetto = t.substring(8).trim();
                } else if (t.startsWith("state=PlaybackState {")) {
                    v.stato = statoDaRiga(t);
                    v.azioni = numeroDopo(t, ", actions=");
                } else if (t.startsWith("metadata: size=")) {
                    int q = t.indexOf("description=");
                    if (q >= 0) {
                        dividiDescrizione(t.substring(q + 12), v);
                    }
                }
            }
            i = j - 1;
            if (v.stato >= 0 && !v.pacchetto.isEmpty()) voci.add(v);
        }
        CampiTelefonia.Media m = new CampiTelefonia.Media();
        if (voci.isEmpty()) {
            // Nessuna sessione solo se il riepilogo lo dice chiaramente; altrimenti non si e' capito.
            return totale > 0 ? null : m;
        }
        Voce scelta = null;
        for (Voce x : voci) if (classe(x.stato) == 2) { scelta = x; break; }
        if (scelta == null) for (Voce x : voci) if (classe(x.stato) == 1) { scelta = x; break; }
        // Terzo caso: app che non pubblicano lo stato ma pubblicano il brano. Stato 1 (in pausa: il valore piu'
        // prudente, non dichiara una riproduzione che non conosciamo); i comandi restano quelli delle azioni.
        if (scelta == null) for (Voce x : voci) if (!x.titolo.isEmpty() || !x.artista.isEmpty()) { scelta = x; break; }
        if (scelta == null) return m;   // sessioni presenti ma senza brano ne' riproduzione: «nessuna sessione»
        m.stato = classe(scelta.stato) == 2 ? 2 : 1;
        m.comandi = scelta.azioni < 0 ? 0 : CampiTelefonia.comandiPossibili(scelta.azioni, m.stato);
        m.titolo = scelta.titolo;
        m.artista = scelta.artista;
        m.pacchetto = scelta.pacchetto;
        return m;
    }

    // ---- cmd wifi status ----

    /**
     * Nome della rete dal testo di `cmd wifi status`: riga «Wifi is connected to "NOME"» oppure, nella
     * riga WifiInfo, «SSID: "NOME", BSSID: ...». Senza virgolette, senza «&lt;unknown ssid&gt;»; vuoto se non c'e'.
     */
    static String nomeDaWifiStatus(String testo) {
        if (testo == null) return "";
        for (String r : testo.split("\\r?\\n")) {
            String t = r.trim();
            if (t.startsWith("Wifi is connected to ")) return nomeCitato(t.substring(21));
        }
        for (String r : testo.split("\\r?\\n")) {
            int i = r.indexOf("SSID: ");
            // «BSSID: » (riga rientrata) contiene «SSID: »: si scarta se il carattere prima e' 'B'.
            while (i > 0 && r.charAt(i - 1) == 'B') i = r.indexOf("SSID: ", i + 1);
            if (i < 0) continue;
            String resto = r.substring(i + 6);
            int fine = resto.indexOf(", BSSID:");
            return nomeCitato(fine >= 0 ? resto.substring(0, fine) : resto);
        }
        return "";
    }

    /** Nome tra virgolette; una virgoletta d'apertura senza chiusura (output troncato) = nome vuoto. */
    private static String nomeCitato(String s) {
        String t = s.trim();
        if (t.startsWith("\"") && (t.length() < 2 || !t.endsWith("\""))) return "";
        return CampiTelefonia.nomeRete(t);
    }

    // ---- dumpsys telephony.registry ----

    /** Parte mobile: mobile 1 senza servizio / 2 in servizio, livello 0-4, generazione, operatore. */
    static final class Mobile {
        int mobile, livello, generazione;
        String operatore = "";
    }

    /** Generazione dal nome della tecnologia radio (es. «LTE», «NR», «HSPAP»). */
    static int generazioneDaNome(String nome) {
        if (nome == null) return 0;
        switch (nome.trim().toUpperCase()) {
            case "NR": return 5;
            case "LTE": case "LTE_CA": return 4;
            case "UMTS": case "HSDPA": case "HSUPA": case "HSPA": case "HSPAP": case "EVDO_0": case "EVDO_A":
            case "EVDO_B": case "EHRPD": case "TD_SCDMA": return 3;
            case "GPRS": case "EDGE": case "1XRTT": case "CDMA": case "IS95A": case "IS95B": case "IDEN": case "GSM": return 2;
            default: return 0;
        }
    }

    private static String valoreFinoA(String riga, String chiave) {
        int i = riga.indexOf(chiave);
        if (i < 0) return null;
        i += chiave.length();
        int j = i;
        while (j < riga.length() && riga.charAt(j) != ',' && riga.charAt(j) != '}') j++;
        return riga.substring(i, j).trim();
    }

    /** Nome tra parentesi dopo «chiave» numerica: «getRilDataRadioTechnology=14(LTE)» -> «LTE». */
    private static String nomeTecnologia(String riga, String chiave) {
        int i = riga.indexOf(chiave);
        if (i < 0) return null;
        int a = riga.indexOf('(', i), b = a < 0 ? -1 : riga.indexOf(')', a);
        return a < 0 || b < 0 ? null : riga.substring(a + 1, b);
    }

    /**
     * Dal testo di `dumpsys telephony.registry`: per ogni slot, la riga «mServiceState={...}» e la prima
     * «mSignalStrength=...» che la segue. Si prende il primo slot in servizio (voce o dati «0(IN_SERVICE)»);
     * se ci sono slot ma nessuno e' in servizio: mobile 1. Nessuna riga mServiceState: null (non capito).
     */
    static Mobile mobileDaRegistry(String testo) {
        if (testo == null) return null;
        String[] righe = testo.split("\\r?\\n");
        boolean trovati = false;
        for (int i = 0; i < righe.length; i++) {
            String r = righe[i].trim();
            if (!r.startsWith("mServiceState={")) continue;
            trovati = true;
            if (!r.contains("mVoiceRegState=0(IN_SERVICE)") && !r.contains("mDataRegState=0(IN_SERVICE)")) continue;
            Mobile m = new Mobile();
            m.mobile = 2;
            String op = valoreFinoA(r, "mOperatorAlphaLong=");
            if (op == null || op.isEmpty()) op = valoreFinoA(r, "mOperatorAlphaShort=");
            m.operatore = op == null ? "" : op;
            String tec = nomeTecnologia(r, "getRilDataRadioTechnology=");
            int g = generazioneDaNome(tec);
            if (g == 0) g = generazioneDaNome(nomeTecnologia(r, "getRilVoiceRadioTechnology="));
            m.generazione = g;
            for (int j = i + 1; j < righe.length && !righe[j].trim().startsWith("mServiceState={"); j++) {
                String s = righe[j].trim();
                if (!s.startsWith("mSignalStrength=")) continue;
                int migliore = 0, k = 0;
                while ((k = s.indexOf(" level=", k)) >= 0) {
                    k += 7;
                    long n = numeroDopo(s.substring(k - 7), " level=");
                    if (n > migliore) migliore = (int) Math.min(n, 4);
                }
                m.livello = migliore;
                break;
            }
            return m;
        }
        if (!trovati) return null;
        Mobile m = new Mobile();
        m.mobile = 1;
        return m;
    }
}
