package it.yasde;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.text.Collator;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

/**
 * Parte pura dei dati del telefono (docs/protocollo.md §2.6): lettura e scrittura dei campi,
 * troncamento delle stringhe, ordinamento e regole di decisione. Nessuna classe di Android,
 * cosi' si prova sulla JVM normale.
 */
final class CampiDati {
    /** Massimo del corpo di una risposta (protocollo §2). */
    static final int MAX_RISPOSTA = 1 << 20;

    // Lunghezze massime in byte UTF-8 (il limite assoluto di una stringa e' 65535).
    static final int MAX_PACCHETTO = 255;
    static final int MAX_NOME = 200;
    static final int MAX_CHIAVE = 4096;
    static final int MAX_TITOLO = 300;
    static final int MAX_TESTO = 2000;

    // Segni di APP_ELENCO.
    static final int SEGNO_SISTEMA = 1;
    static final int SEGNO_VERTICALE = 2;

    // Valori di ActivityInfo.screenOrientation che dichiarano il verticale
    //.
    private static final int ORIENT_PORTRAIT = 1;
    private static final int ORIENT_SENSOR_PORTRAIT = 7;
    private static final int ORIENT_REVERSE_PORTRAIT = 9;
    private static final int ORIENT_USER_PORTRAIT = 12;

    private CampiDati() {}

    // ---- Stringhe ----

    /** Lunghezza UTF-8 di un punto di codice; un surrogato spaiato diventa '?' (1 byte), come in getBytes. */
    private static int byteUtf8(int cp) {
        if (cp >= 0xd800 && cp <= 0xdfff) return 1;
        if (cp < 0x80) return 1;
        if (cp < 0x800) return 2;
        if (cp < 0x10000) return 3;
        return 4;
    }

    /** Taglia la stringa perche' occupi al massimo `maxByte` byte UTF-8, senza spezzare un carattere. */
    static String tronca(String s, int maxByte) {
        if (s == null) return "";
        int usati = 0;
        int i = 0;
        while (i < s.length()) {
            int cp = s.codePointAt(i);
            int b = byteUtf8(cp);
            if (usati + b > maxByte) return s.substring(0, i);
            usati += b;
            i += Character.charCount(cp);
        }
        return s;
    }

    /** stringa = u16 lunghezza in byte + UTF-8, troncata a `maxByte`. */
    static void scriviStringa(DataOutputStream o, String s, int maxByte) throws IOException {
        byte[] b = tronca(s, Math.min(maxByte, 0xffff)).getBytes("UTF-8");
        o.writeShort(b.length);
        o.write(b);
    }

    static String leggiStringa(DataInputStream in) throws IOException {
        int n = in.readUnsignedShort();
        byte[] b = new byte[n];
        in.readFully(b);
        return new String(b, "UTF-8");
    }

    // ---- Regole ----

    /**
     * Regola di «solo verticale»: l'attivita' di avvio dichiara un orientamento verticale fisso
     * (portrait, sensor portrait, reverse portrait, user portrait), qualunque sia la sua
     * ridimensionabilita': anche un'app «ridimensionabile solo per versione dell'SDK» in una finestra
     * larga viene messa in letterbox da Android (colonna al centro con bande nere), quindi la shell la
     * deve aprire in una finestra verticale. Limite: l'orientamento imposto dal programma ad app
     * avviata (setRequestedOrientation) non si vede dal manifesto e resta scoperto.
     */
    static boolean soloVerticale(int orientamento) {
        return orientamento == ORIENT_PORTRAIT || orientamento == ORIENT_SENSOR_PORTRAIT
                || orientamento == ORIENT_REVERSE_PORTRAIT || orientamento == ORIENT_USER_PORTRAIT;
    }

    /**
     * Livello di segnale Wi-Fi 0–4 da un RSSI in dBm, con la formula predefinita di AOSP
     * (da -100 dBm a -55 dBm in 5 livelli).
     */
    static int livelloWifi(int rssi) {
        if (rssi <= -100) return 0;
        if (rssi >= -55) return 4;
        return (rssi + 100) * 4 / 45;
    }

    static int limita(int v, int min, int max) {
        return v < min ? min : (v > max ? max : v);
    }

    // ---- App ----

    static final class App {
        final String pacchetto, nome;
        final boolean sistema, soloVerticale;

        App(String pacchetto, String nome, boolean sistema, boolean soloVerticale) {
            this.pacchetto = pacchetto;
            this.nome = nome;
            this.sistema = sistema;
            this.soloVerticale = soloVerticale;
        }
    }

    /** Ordina per nome senza distinguere maiuscole (ne' accenti); a parita' per pacchetto. */
    static void ordinaApp(List<App> app) {
        final Collator c = Collator.getInstance(Locale.ITALIAN);
        c.setStrength(Collator.PRIMARY);
        Collections.sort(app, new Comparator<App>() {
            @Override
            public int compare(App a, App b) {
                int r = c.compare(a.nome, b.nome);
                return r != 0 ? r : a.pacchetto.compareTo(b.pacchetto);
            }
        });
    }

    static byte[] codificaElenco(List<App> app) throws IOException {
        if (app.size() > 0xffff) throw new IllegalStateException("troppe app per la risposta");
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        DataOutputStream o = new DataOutputStream(b);
        o.writeShort(app.size());
        for (App a : app) {
            scriviStringa(o, a.pacchetto, MAX_PACCHETTO);
            scriviStringa(o, a.nome, MAX_NOME);
            o.writeByte((a.sistema ? SEGNO_SISTEMA : 0) | (a.soloVerticale ? SEGNO_VERTICALE : 0));
        }
        return controllaDimensione(b.toByteArray());
    }

    // ---- Stato ----

    static final class Stato {
        final int batteria, inCarica, rete, segnale, bloccato;

        Stato(int batteria, int inCarica, int rete, int segnale, int bloccato) {
            this.batteria = limita(batteria, 0, 100);
            this.inCarica = inCarica != 0 ? 1 : 0;
            this.rete = limita(rete, 0, 2);
            this.segnale = limita(segnale, 0, 4);
            this.bloccato = bloccato != 0 ? 1 : 0;
        }
    }

    static byte[] codificaStato(Stato s) {
        return new byte[] {(byte) s.batteria, (byte) s.inCarica, (byte) s.rete, (byte) s.segnale, (byte) s.bloccato};
    }

    /**
     * Un cambio di stato che merita l'evento 0x7e: blocco, carica o tipo di rete diversi, oppure
     * batteria cambiata di almeno 1 punto. Il segnale non conta: oscilla di continuo.
     */
    static boolean statoCambiato(Stato prima, Stato dopo) {
        return prima.bloccato != dopo.bloccato || prima.inCarica != dopo.inCarica
                || prima.rete != dopo.rete || prima.batteria != dopo.batteria;
    }

    // ---- Notifiche ----

    static final class Notifica {
        final String chiave, pacchetto, titolo, testo;
        final long ora;

        Notifica(String chiave, String pacchetto, String titolo, String testo, long ora) {
            this.chiave = chiave == null ? "" : chiave;
            this.pacchetto = pacchetto == null ? "" : pacchetto;
            this.titolo = titolo == null ? "" : titolo;
            this.testo = testo == null ? "" : testo;
            this.ora = ora;
        }
    }

    static byte[] codificaNotifiche(List<Notifica> notifiche) throws IOException {
        if (notifiche.size() > 0xffff) throw new IllegalStateException("troppe notifiche per la risposta");
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        DataOutputStream o = new DataOutputStream(b);
        o.writeShort(notifiche.size());
        for (Notifica n : notifiche) {
            scriviStringa(o, n.chiave, MAX_CHIAVE);
            scriviStringa(o, n.pacchetto, MAX_PACCHETTO);
            scriviStringa(o, n.titolo, MAX_TITOLO);
            scriviStringa(o, n.testo, MAX_TESTO);
            o.writeLong(n.ora);
        }
        return controllaDimensione(b.toByteArray());
    }

    /**
     * Firma delle notifiche per accorgersi dei cambi: chiave, ora e lunghezze/hash di titolo e
     * testo (un aggiornamento che cambia solo il testo cambia la firma).
     */
    static String firmaNotifiche(List<Notifica> notifiche) {
        StringBuilder sb = new StringBuilder();
        List<String> righe = new ArrayList<>();
        for (Notifica n : notifiche) {
            righe.add(n.chiave + "|" + n.ora + "|" + n.titolo.hashCode() + "|" + n.testo.hashCode());
        }
        Collections.sort(righe);
        for (String r : righe) sb.append(r).append('\n');
        return sb.toString();
    }

    /** Ogni risposta ha il limite del protocollo: oltre, ERRORE. */
    static byte[] controllaDimensione(byte[] corpo) {
        if (corpo.length > MAX_RISPOSTA) {
            throw new IllegalStateException("risposta troppo grande (oltre 1 MiB)");
        }
        return corpo;
    }

    // ---- Memoria in dettaglio (0x76) ----

    /** Valore non leggibile di MEMORIA_DETTAGLIO: 0xFFFFFFFFFFFFFFFF. */
    static final long NON_LEGGIBILE = -1L;

    static byte[] codificaMemoriaDettaglio(long immagini, long video, long audio, long app) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        DataOutputStream o = new DataOutputStream(b);
        o.writeLong(immagini);
        o.writeLong(video);
        o.writeLong(audio);
        o.writeLong(app);
        return b.toByteArray();
    }

    // ---- Calendario (0x77) ----

    static final int MAX_EVENTI_MIN = 1, MAX_EVENTI_MAX = 16;
    static final int MAX_TITOLO_EVENTO = 300;
    /** Finestra degli eventi: da adesso a 30 giorni. */
    static final long FINESTRA_CALENDARIO_MS = 30L * 86400000L;

    /** `massimo` della richiesta CALENDARIO: 1-16, altrimenti errore. */
    static int controllaMassimoEventi(int massimo) {
        if (massimo < MAX_EVENTI_MIN || massimo > MAX_EVENTI_MAX) {
            throw new IllegalArgumentException("massimo di eventi non valido (1-16)");
        }
        return massimo;
    }

    static final class Evento {
        final String titolo;
        final long inizio, fine;
        final boolean tuttoIlGiorno;

        Evento(String titolo, long inizio, long fine, boolean tuttoIlGiorno) {
            this.titolo = titolo == null ? "" : titolo;
            this.inizio = inizio;
            this.fine = fine;
            this.tuttoIlGiorno = tuttoIlGiorno;
        }
    }

    /** Scrive al piu' `massimo` eventi (gli altri si ignorano), titolo troncato. */
    static byte[] codificaCalendario(List<Evento> eventi, int massimo) throws IOException {
        int n = Math.min(eventi.size(), massimo);
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        DataOutputStream o = new DataOutputStream(b);
        o.writeShort(n);
        for (int i = 0; i < n; i++) {
            Evento e = eventi.get(i);
            scriviStringa(o, e.titolo, MAX_TITOLO_EVENTO);
            o.writeLong(e.inizio);
            o.writeLong(e.fine);
            o.writeByte(e.tuttoIlGiorno ? 1 : 0);
        }
        return controllaDimensione(b.toByteArray());
    }

    // ---- Calendario su un intervallo (0x7b) ----

    static final int MAX_EVENTI_INTERVALLO = 500;
    static final long MAX_INTERVALLO_MS = 62L * 86400000L;
    static final int MAX_PACCHETTO_CALENDARIO = MAX_PACCHETTO;

    /**
     * Controlla gli argomenti di CALENDARIO_INTERVALLO: istanti non negativi (un u64 sopra 2^63 arriva
     * negativo), intervallo non vuoto ne' rovesciato, al massimo 62 giorni, massimo eventi 1-500.
     * Gli istanti sono millisecondi UTC dal 1970: nessun fuso, il cambio dell'ora non conta.
     */
    static void controllaIntervallo(long da, long a, int massimo) {
        if (da < 0 || a < 0) throw new IllegalArgumentException("intervallo non valido (istante negativo)");
        if (a <= da) throw new IllegalArgumentException("intervallo vuoto o rovesciato");
        if (a - da > MAX_INTERVALLO_MS) throw new IllegalArgumentException("intervallo oltre 62 giorni");
        if (massimo < 1 || massimo > MAX_EVENTI_INTERVALLO) throw new IllegalArgumentException("massimo di eventi non valido (1-500)");
    }

    /**
     * L'evento [inizio, fine) si sovrappone a [da, a)? Un evento che finisce esattamente a `da` no; un
     * evento senza durata conta se cade in [da, a). Funziona uguale per eventi su piu' giorni e per
     * quelli «tutto il giorno» (mezzanotte UTC).
     */
    static boolean sovrappone(long inizio, long fine, long da, long a) {
        if (inizio >= a) return false;
        if (fine > inizio) return fine > da;
        return inizio >= da;
    }

    static final class Calendario {
        final List<Evento> eventi;
        final boolean troncato;
        final String pacchetto;

        Calendario(List<Evento> eventi, boolean troncato, String pacchetto) {
            this.eventi = eventi;
            this.troncato = troncato;
            this.pacchetto = pacchetto == null ? "" : pacchetto;
        }
    }

    /** u8 troncato, stringa pacchetto, u16 numero, eventi (titolo troncato): al massimo `massimo`. */
    static byte[] codificaCalendarioIntervallo(Calendario c, int massimo) throws IOException {
        boolean troncato = c.troncato || c.eventi.size() > massimo;
        int n = Math.min(c.eventi.size(), massimo);
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        DataOutputStream o = new DataOutputStream(b);
        o.writeByte(troncato ? 1 : 0);
        scriviStringa(o, c.pacchetto, MAX_PACCHETTO_CALENDARIO);
        o.writeShort(n);
        for (int i = 0; i < n; i++) {
            Evento e = c.eventi.get(i);
            scriviStringa(o, e.titolo, MAX_TITOLO_EVENTO);
            o.writeLong(e.inizio);
            o.writeLong(e.fine);
            o.writeByte(e.tuttoIlGiorno ? 1 : 0);
        }
        return controllaDimensione(b.toByteArray());
    }

    // ---- Sveglia (0x7c) ----

    static final class Sveglia {
        final boolean c;
        final long ora;
        final String pacchetto;

        Sveglia(boolean c, long ora, String pacchetto) {
            this.c = c && ora > 0;
            this.ora = this.c ? ora : 0;
            this.pacchetto = this.c && pacchetto != null ? pacchetto : "";
        }
    }

    static byte[] codificaSveglia(Sveglia s) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        DataOutputStream o = new DataOutputStream(b);
        o.writeByte(s.c ? 1 : 0);
        o.writeLong(s.ora);
        scriviStringa(o, s.pacchetto, MAX_PACCHETTO);
        return b.toByteArray();
    }

    // ---- Batteria in dettaglio (0x7d) ----

    static final int TEMPERATURA_NOTA_NO = 0x7fff;

    static final class Batteria {
        final int livello, stato, alimentazione, temperatura, salute, tensione, minutiCarica, minutiAutonomia;

        Batteria(int livello, int stato, int alimentazione, int temperatura, int salute, int tensione, int minutiCarica, int minutiAutonomia) {
            this.livello = limita(livello, 0, 100);
            this.stato = limita(stato, 0, 4);
            this.alimentazione = alimentazione & 0xf;
            this.temperatura = limita(temperatura, -32768, 0x7fff);
            this.salute = limita(salute, 0, 6);
            this.tensione = limita(tensione, 0, 0xffff);
            this.minutiCarica = minutiCarica < 0 ? -1 : minutiCarica;
            this.minutiAutonomia = minutiAutonomia < 0 ? -1 : minutiAutonomia;
        }

        /**
         * Da valori nel formato di BatteryManager (EXTRA_*). Integer.MIN_VALUE = non noto; livello
         * o scala non validi: null (senza livello non c'e' niente da dire). La carica completa ha senso
         * solo se in carica. L'autonomia e' sempre -1: nessuna API pubblica, non si stima.
         */
        static Batteria daAndroid(int livello, int scala, int stato, int alimentazione, int salute, int temperaturaDecimi, int millivolt, int minutiCarica) {
            if (livello < 0 || scala <= 0) return null;
            int statoP = stato == 2 ? 1 : stato == 3 ? 2 : stato == 4 ? 3 : stato == 5 ? 4 : 0;
            int saluteP = salute == 2 ? 1 : salute == 3 ? 2 : salute == 4 ? 3 : salute == 5 ? 4 : salute == 7 ? 5 : salute == 6 ? 6 : 0;
            int temp = temperaturaDecimi == Integer.MIN_VALUE ? TEMPERATURA_NOTA_NO : temperaturaDecimi;
            int volt = millivolt < 0 ? 0 : millivolt;
            return new Batteria((int) (livello * 100L / scala), statoP, alimentazione, temp, saluteP, volt, statoP == 1 ? minutiCarica : -1, -1);
        }
    }

    static byte[] codificaBatteria(Batteria b) throws IOException {
        ByteArrayOutputStream o1 = new ByteArrayOutputStream();
        DataOutputStream o = new DataOutputStream(o1);
        o.writeByte(b.livello);
        o.writeByte(b.stato);
        o.writeByte(b.alimentazione);
        o.writeShort(b.temperatura);
        o.writeByte(b.salute);
        o.writeShort(b.tensione);
        o.writeInt(b.minutiCarica);
        o.writeInt(b.minutiAutonomia);
        return o1.toByteArray();
    }
}
