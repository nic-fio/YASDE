package it.yasde;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.List;

/**
 * Parte pura di CHIAMATE (0x91), RETE (0x92), MEDIA (0x93) e MEDIA_COMANDO (0x94), protocollo 3
 * (docs/protocollo.md §2.12): codifica delle risposte, taglio delle stringhe, mappatura dei valori
 * di Android su quelli del protocollo. Niente android.*: si prova sulla JVM. Le costanti di Android
 * sono ricopiate qui con il nome originale accanto, perche' la mappatura sia provabile.
 * RISERVATEZZA: nomi, numeri, reti e titoli non finiscono mai in un log ne' in un'eccezione.
 */
final class CampiTelefonia {
    private CampiTelefonia() {}

    /** Esito della lettura. */
    static final int ESITO_OK = 0, ESITO_NON_DISPONIBILE = 1, ESITO_NEGATO = 2;

    // Byte UTF-8 al massimo (il PC ha il terminatore in piu': 128, 48, 100, 64, 160, 128, 256).
    static final int MAX_NOME = 127, MAX_NUMERO = 47, MAX_RETE = 99, MAX_OPERATORE = 63;
    static final int MAX_TITOLO = 159, MAX_ARTISTA = 127, MAX_PACCHETTO = 255;
    static final int MAX_CHIAMATE = 20;

    /** C'e' una SecurityException nella catena delle cause (anche dentro un InvocationTargetException)? */
    static boolean negato(Throwable t) {
        for (int i = 0; t != null && i < 8; i++) {
            if (t instanceof SecurityException) return true;
            t = t instanceof java.lang.reflect.InvocationTargetException ? ((java.lang.reflect.InvocationTargetException) t).getTargetException() : t.getCause();
        }
        return false;
    }

    /** Nomi delle classi della catena delle cause (mai i messaggi: potrebbero contenere dati). */
    static String catena(Throwable t) {
        StringBuilder b = new StringBuilder();
        for (int i = 0; t != null && i < 6; i++) {
            if (b.length() > 0) b.append(" <- ");
            b.append(t.getClass().getSimpleName());
            t = t instanceof java.lang.reflect.InvocationTargetException ? ((java.lang.reflect.InvocationTargetException) t).getTargetException() : t.getCause();
        }
        return b.toString();
    }

    /** Stringa del protocollo: U+0000 diventa spazio (il PC la tratterebbe come fine), poi taglio sicuro. */
    static void str(DataOutputStream o, String s, int maxByte) throws IOException {
        CampiDati.scriviStringa(o, s == null ? "" : s.replace('\0', ' '), maxByte);
    }

    // ---- Chiamate ----

    /** Controlla il massimo chiesto dal PC (1..20). */
    static int controllaMassimoChiamate(int massimo) {
        if (massimo < 1 || massimo > MAX_CHIAMATE) throw new IllegalArgumentException("massimo delle chiamate non valido (1-20)");
        return massimo;
    }

    /** CallLog.Calls.TYPE -> 0 altro, 1 entrante, 2 uscente, 3 persa, 4 rifiutata. */
    static int tipoChiamata(int tipoAndroid) {
        switch (tipoAndroid) {
            case 1: return 1;   // INCOMING_TYPE
            case 2: return 2;   // OUTGOING_TYPE
            case 3: return 3;   // MISSED_TYPE
            case 5: return 4;   // REJECTED_TYPE
            default: return 0;  // segreteria, bloccata, risposta altrove, ...
        }
    }

    static final class Chiamata {
        final int tipo;
        final long oraMs;
        final long durataS;
        final String nome, numero;

        Chiamata(int tipoAndroid, long oraMs, long durataS, String nome, String numero) {
            this.tipo = tipoChiamata(tipoAndroid);
            this.oraMs = oraMs < 0 ? 0 : oraMs;
            this.durataS = durataS < 0 ? 0 : (durataS > 0xffffffffL ? 0xffffffffL : durataS);
            this.nome = nome == null ? "" : nome;
            this.numero = numero == null ? "" : numero;
        }
    }

    /** esito, n, poi n chiamate (le piu' recenti per prime, come arrivano). Con esito != 0 la lista e' vuota. */
    static byte[] codificaChiamate(int esito, List<Chiamata> v, int massimo) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        DataOutputStream o = new DataOutputStream(b);
        int n = esito == ESITO_OK ? Math.min(Math.min(v.size(), massimo), MAX_CHIAMATE) : 0;
        o.writeByte(esito);
        o.writeByte(n);
        for (int i = 0; i < n; i++) {
            Chiamata c = v.get(i);
            o.writeByte(c.tipo);
            o.writeLong(c.oraMs);
            o.writeInt((int) c.durataS);
            str(o, c.nome, MAX_NOME);
            str(o, c.numero, MAX_NUMERO);
        }
        return b.toByteArray();
    }

    // ---- Rete ----

    /** Via le virgolette e il segnaposto di Android per il nome non leggibile. */
    static String nomeRete(String ssid) {
        if (ssid == null) return "";
        String s = ssid.trim();
        if (s.length() >= 2 && s.charAt(0) == '"' && s.charAt(s.length() - 1) == '"') s = s.substring(1, s.length() - 1);
        if (s.equals("<unknown ssid>") || s.equals("0x")) return "";
        return s;
    }

    /** TelephonyManager.NETWORK_TYPE_* -> generazione 0 ignota, 2, 3, 4, 5. */
    static int generazione(int tipoRete) {
        switch (tipoRete) {
            case 1: case 2: case 4: case 7: case 11:                   // GPRS, EDGE, CDMA, 1xRTT, IDEN
                return 2;
            case 3: case 5: case 6: case 8: case 9: case 10:           // UMTS, EVDO_0/A, HSDPA, HSUPA, HSPA
            case 12: case 14: case 15: case 17:                        // EVDO_B, EHRPD, HSPAP, TD_SCDMA
                return 3;
            case 13: case 19:                                          // LTE, LTE_CA
                return 4;
            case 20:                                                   // NR
                return 5;
            default:
                return 0;
        }
    }

    static final class Rete {
        int esito;
        int wifi, wifiLivello;
        String wifiNome = "";
        int mobile, mobileLivello, generazione;
        String operatore = "";
        int datiMobili = 2, aereo = 2;   // 0 no, 1 si', 2 ignoto (mai un valore inventato)
    }

    static byte[] codificaRete(Rete r) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        DataOutputStream o = new DataOutputStream(b);
        boolean ok = r.esito == ESITO_OK;
        o.writeByte(r.esito);
        o.writeByte(ok ? CampiDati.limita(r.wifi, 0, 2) : 0);
        o.writeByte(ok ? CampiDati.limita(r.wifiLivello, 0, 4) : 0);
        str(o, ok ? r.wifiNome : "", MAX_RETE);
        o.writeByte(ok ? CampiDati.limita(r.mobile, 0, 2) : 0);
        o.writeByte(ok ? CampiDati.limita(r.mobileLivello, 0, 4) : 0);
        int g = r.generazione;
        o.writeByte(ok && g >= 2 && g <= 5 ? g : 0);
        str(o, ok ? r.operatore : "", MAX_OPERATORE);
        o.writeByte(ok ? CampiDati.limita(r.datiMobili, 0, 2) : 0);
        o.writeByte(ok ? CampiDati.limita(r.aereo, 0, 2) : 0);
        return b.toByteArray();
    }

    // ---- Media ----

    /** Comandi del protocollo. */
    static final int CMD_PLAY_PAUSA = 1, CMD_AVANTI = 2, CMD_INDIETRO = 3;

    static int controllaComando(int comando) {
        if (comando < CMD_PLAY_PAUSA || comando > CMD_INDIETRO) throw new IllegalArgumentException("comando media non valido");
        return comando;
    }

    /** PlaybackState.STATE_* -> 1 in pausa, 2 in riproduzione (sessione attiva: mai 0). */
    static int statoMedia(int statoAndroid) {
        switch (statoAndroid) {
            case 3: case 4: case 5: case 6: case 8:   // PLAYING, FAST_FORWARDING, REWINDING, BUFFERING, CONNECTING
                return 2;
            default:
                return 1;
        }
    }

    /**
     * Azioni di PlaybackState (ACTION_*) e stato (protocollo: 2 in riproduzione, altro in pausa) ->
     * bit 0 play/pausa, bit 1 avanti, bit 2 indietro. Il bit 0 vale solo se il comando fa qualcosa di
     * sensato: in riproduzione serve PAUSE o PLAY_PAUSE, in pausa o fermo serve PLAY o PLAY_PAUSE.
     * PLAY = 4, PAUSE = 2, PLAY_PAUSE = 512, SKIP_TO_NEXT = 32, SKIP_TO_PREVIOUS = 16.
     */
    static int comandiPossibili(long azioni, int statoProtocollo) {
        int m = 0;
        long giusta = statoProtocollo == 2 ? (2L | 512L) : (4L | 512L);
        if ((azioni & giusta) != 0) m |= 1;
        if ((azioni & 32L) != 0) m |= 2;
        if ((azioni & 16L) != 0) m |= 4;
        return m;
    }

    /** Il comando (1..3) e' tra quelli possibili (bit comando-1)? */
    static boolean comandoPermesso(int comando, int maschera) {
        return (maschera & (1 << (comando - 1))) != 0;
    }

    /** Indice della sessione scelta: la prima in riproduzione, altrimenti la prima; -1 se non ce ne sono. */
    static int scegliSessione(int[] statiProtocollo) {
        if (statiProtocollo == null || statiProtocollo.length == 0) return -1;
        for (int i = 0; i < statiProtocollo.length; i++) if (statiProtocollo[i] == 2) return i;
        return 0;
    }

    static final class Media {
        int esito, stato, comandi;
        String titolo = "", artista = "", pacchetto = "";
    }

    static byte[] codificaMedia(Media m) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        DataOutputStream o = new DataOutputStream(b);
        boolean ok = m.esito == ESITO_OK && m.stato != 0;
        o.writeByte(m.esito);
        o.writeByte(m.esito == ESITO_OK ? CampiDati.limita(m.stato, 0, 2) : 0);
        str(o, ok ? m.titolo : "", MAX_TITOLO);
        str(o, ok ? m.artista : "", MAX_ARTISTA);
        str(o, ok ? m.pacchetto : "", MAX_PACCHETTO);
        o.writeByte(ok ? m.comandi & 7 : 0);
        return b.toByteArray();
    }
}
