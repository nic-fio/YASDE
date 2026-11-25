package it.yasde;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.Charset;
import java.nio.charset.CodingErrorAction;

/**
 * Parte pura del canale `appunti` (docs/protocollo.md par. 2.8): formato dei messaggi, regole "non passa
 * mai" e soppressione dell'eco. Niente Android qui dentro: si prova sulla JVM normale.
 */
final class Regole {
    static final int APPUNTI_TESTO = 0x58, APPUNTI_IMPOSTA = 0x59, APPUNTI_INCOLLA = 0x5a;

    /** Massimo del testo, in byte UTF-8, in entrambi i versi (specifica 4.11). */
    static final int MAX_TESTO = 200000;

    /** Massimo del corpo di un messaggio (par. 2). */
    static final int MAX_CORPO = 1 << 20;

    private static final Charset UTF8 = Charset.forName("UTF-8");

    private Regole() {}

    /** Un APPUNTI_INCOLLA letto: sessione video e testo. */
    static final class Incolla {
        final int sessione;
        final String testo;

        Incolla(int sessione, String testo) {
            this.sessione = sessione;
            this.testo = testo;
        }
    }

    /**
     * Pacchetti di gestori di password e di app di codici a due fattori noti (la copia di una password o di
     * un codice da questi non va mai al PC, anche se non porta la marca "sensibile"). Un nome vale per se' e
     * per i suoi sotto-pacchetti. Dove non ho potuto controllare il nome su una fonte pubblica sono tenute
     * entrambe le varianti probabili. Il Gestore password di Google copia dai Google Play Services: per
     * quello non si puo' filtrare per pacchetto e vale solo la marca standard "sensibile".
     */
    private static final String[] GESTORI_PASSWORD = {
        "com.x8bit.bitwarden", "com.bitwarden", "com.agilebits.onepassword", "com.onepassword",
        "com.lastpass", "com.dashlane", "io.enpass.app", "com.keepersecurity.passwordmanager",
        "com.callpod.android_apps.keeper", "com.nordpass.android.app.password.manager",
        "proton.android.pass", "me.proton.android.pass", "com.kunzisoft.keepass", "keepass2android",
        "com.stickypassword.android", "com.siber.roboform", "com.roboform.android", "com.zoho.vault",
        "com.google.android.apps.passwords", "com.samsung.android.samsungpass",
        "com.google.android.apps.authenticator2", "com.azure.authenticator", "com.beemdevelopment.aegis",
        "com.authy.authy",
    };

    /** Il pacchetto che ha fatto la copia e' un gestore di password noto? */
    static boolean eGestorePassword(String pacchetto) {
        if (pacchetto == null) return false;
        for (String g : GESTORI_PASSWORD) {
            if (pacchetto.equals(g) || pacchetto.startsWith(g + ".")) return true;
        }
        return false;
    }

    /**
     * Un testo copiato sul telefono puo' andare al PC? No se e' vuoto, se la copia e' marcata sensibile
     * (la marca standard di Android), se viene da un gestore di password noto (`fonte` = pacchetto della
     * copia, null se il sistema non lo dice) o se supera il massimo in byte UTF-8.
     */
    static boolean puoPartire(String testo, boolean sensibile, String fonte) {
        if (testo == null || testo.isEmpty() || sensibile || eGestorePassword(fonte)) return false;
        // Ogni char vale al massimo 3 byte: sopra questa soglia non serve nemmeno codificare.
        if (testo.length() > MAX_TESTO) return false;
        return testo.getBytes(UTF8).length <= MAX_TESTO;
    }

    /**
     * La fonte letta prima e dopo la lettura degli appunti e' affidabile? Solo se le due coincidono e non sono
     * un gestore di password: se tra le due letture e' arrivata un'altra copia, nel dubbio non si manda.
     */
    static boolean fonteSicura(String prima, String dopo) {
        boolean uguali = prima == null ? dopo == null : prima.equals(dopo);
        return uguali && !eGestorePassword(prima) && !eGestorePassword(dopo);
    }

    /** Un messaggio completo: tipo, lunghezza (4, big endian), corpo. */
    static byte[] messaggio(int tipo, byte[] corpo) {
        byte[] m = new byte[5 + corpo.length];
        m[0] = (byte) tipo;
        m[1] = (byte) (corpo.length >>> 24);
        m[2] = (byte) (corpo.length >>> 16);
        m[3] = (byte) (corpo.length >>> 8);
        m[4] = (byte) corpo.length;
        System.arraycopy(corpo, 0, m, 5, corpo.length);
        return m;
    }

    /** Corpo di un testo: u32 lunghezza + UTF-8 (non la stringa a 16 bit). */
    static byte[] corpoTesto(String testo) {
        byte[] t = testo.getBytes(UTF8);
        ByteArrayOutputStream b = new ByteArrayOutputStream(4 + t.length);
        b.write(t.length >>> 24);
        b.write(t.length >>> 16);
        b.write(t.length >>> 8);
        b.write(t.length);
        b.write(t, 0, t.length);
        return b.toByteArray();
    }

    private static int u32(byte[] b, int da) {
        return ((b[da] & 0xff) << 24) | ((b[da + 1] & 0xff) << 16) | ((b[da + 2] & 0xff) << 8) | (b[da + 3] & 0xff);
    }

    /**
     * Testo di un corpo che inizia in `da` con u32 lunghezza + UTF-8. Null (messaggio da ignorare) se la
     * lunghezza non torna con i byte presenti, supera il massimo, e' vuota o non e' UTF-8 valido.
     */
    static String testoDa(byte[] corpo, int da) {
        if (corpo == null || da < 0 || corpo.length < da + 4) return null;
        int n = u32(corpo, da);
        if (n <= 0 || n > MAX_TESTO || corpo.length - da - 4 != n) return null;
        try {
            return UTF8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(corpo, da + 4, n)).toString();
        } catch (CharacterCodingException e) {
            return null;
        }
    }

    /** Corpo di APPUNTI_IMPOSTA: il testo, oppure null (da ignorare). */
    static String leggiImposta(byte[] corpo) {
        return testoDa(corpo, 0);
    }

    /** Corpo di APPUNTI_INCOLLA: u32 id + testo, oppure null (da ignorare). */
    static Incolla leggiIncolla(byte[] corpo) {
        if (corpo == null || corpo.length < 4) return null;
        String t = testoDa(corpo, 4);
        return t == null ? null : new Incolla(u32(corpo, 0), t);
    }

    /** Etichetta delle copie messe da YASDE e pacchetto con cui le mette (la shell). */
    static final String ETICHETTA = "yasde";
    static final String PACCHETTO_SHELL = "com.android.shell";

    /**
     * Memoria del canale. Niente eco: e' eco solo la copia che ha messo YASDE stessa (etichetta
     * "yasde" e, se il sistema la dice, fonte la shell) con lo stesso testo dell'ultimo arrivato dal PC.
     * Una copia vera fatta sul telefono, anche con lo stesso testo, ha un'altra etichetta e passa.
     */
    static final class Eco {
        private String ultimoDalPc;
        private long ultimaMarca;
        private String ultimoTesto;

        /** Questa copia (stessa marca di tempo e stesso testo) e' gia' stata valutata? Il sistema avvisa due volte. */
        synchronized boolean giaVisto(long marca, String testo) {
            if (marca != 0 && marca == ultimaMarca && testo != null && testo.equals(ultimoTesto)) return true;
            ultimaMarca = marca;
            ultimoTesto = testo;
            return false;
        }

        private static boolean fonteUguale(String prima, String dopo) {
            return prima == null ? dopo == null : prima.equals(dopo);
        }

        static final int DA_VALUTARE = 0, DOPPIONE = 1, FONTE_INCERTA = 2, DA_GESTORE = 3;

        /**
         * Che fare di questa copia? DA_VALUTARE se e' nuova e la fonte e' coerente; DOPPIONE se e' gia' stata
         * vista; FONTE_INCERTA se le due letture della fonte differiscono (NON si segna come vista: il suo
         * avviso successivo deve poterla valutare); DA_GESTORE la prima volta che una copia viene da un gestore
         * di password (si segna come vista solo se il gestore e' lo stesso prima e dopo, cosi' la riga di log esce una volta sola; le volte dopo DOPPIONE).
         */
        synchronized int daValutare(long marca, String testo, String fontePrima, String fonteDopo) {
            if (eGestorePassword(fontePrima) || eGestorePassword(fonteDopo)) {
                // Segnata come vista solo se e' lo stesso gestore prima e dopo; se le fonti differiscono la copia
                // letta e' probabilmente una nuova di un'altra app: non si manda, ma il suo avviso la rivaluta.
                if (!fonteUguale(fontePrima, fonteDopo)) return DA_GESTORE;
                return giaVisto(marca, testo) ? DOPPIONE : DA_GESTORE;
            }
            if (!fonteSicura(fontePrima, fonteDopo)) return FONTE_INCERTA;
            return giaVisto(marca, testo) ? DOPPIONE : DA_VALUTARE;
        }

        synchronized void daPc(String testo) {
            ultimoDalPc = testo;
        }

        /** imposta() e' fallita: nessuna copia di YASDE e' arrivata negli appunti. */
        synchronized void annulla() {
            ultimoDalPc = null;
        }

        synchronized boolean eEco(String testo, String etichetta, String fonte) {
            return ultimoDalPc != null && ultimoDalPc.equals(testo) && ETICHETTA.equals(etichetta)
                && (fonte == null || PACCHETTO_SHELL.equals(fonte));
        }
    }
}
