package it.yasde;

import android.media.AudioRecord;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Una cattura audio: regola loopback -> AudioRecord 48 kHz stereo 16 bit -> Opus 128 kbit/s a pacchetti
 * da 20 ms (MediaCodec) -> `Uscita`. Un thread legge e codifica. Il tempo di ogni pacchetto e' il numero
 * di campioni gia' emessi diviso 48000 (non l'orologio): un buco nei pacchetti si vede dal salto del tempo.
 */
final class Cattura {
    static final int FREQUENZA = 48000;
    static final int CANALI = 2;
    static final int CAMPIONI_PACCHETTO = 960; // 20 ms
    private static final int BYTE_FRAME = CAMPIONI_PACCHETTO * CANALI * 2;
    private static final String NOME_GUARDIANO = "audio-volume";

    /** Chi riceve i pacchetti Opus (dati senza intestazione); gira sul thread di cattura. */
    interface Uscita {
        void pacchetto(long tempoUs, byte[] dati) throws IOException;
    }

    /** Solo per la prova isolata: permette di alzare il volume anche senza guardiano. */
    static volatile boolean senzaGuardianoConsentito;

    /** SOLO PER PROVA: se > 0 scarta un blocco PCM ogni N prima dell'encoder, per vedere il salto nei tempi. */
    static volatile int scartaUnOgni;

    private final Uscita uscita;
    private final Runnable allaFine;
    private NascosteAudio.Regola regola;
    private MediaCodec codec;
    private Thread thread;
    private volatile boolean fermato;
    private long persi; // campioni dei blocchi PCM persi finora (solo thread di cattura)
    private long blocchiLetti; // solo thread di cattura
    private boolean chiusa; // sotto `this`: chi arriva dopo il primo aspetta `finita`
    private final CountDownLatch finita = new CountDownLatch(1);

    // volume
    private boolean volumeCambiato;
    private int volumeOriginale, volumeMassimo;


    // statistiche (lette da chi ha avviato)
    volatile long pacchetti, byteTotali, campioni, configurazioni, non20ms, mancanzeInput;
    volatile long scartati; // blocchi scartati dall'interruttore di prova
    volatile String errore;

    private Cattura(Uscita uscita, Runnable allaFine) {
        this.uscita = uscita;
        this.allaFine = allaFine;
    }

    /** Durata in campioni a 48 kHz di un pacchetto Opus (dal byte TOC, RFC 6716 §3.1); -1 se illeggibile. */
    static int campioniDelPacchetto(byte[] d) {
        if (d.length < 1) return -1;
        int toc = d[0] & 0xff;
        int config = toc >> 3;
        int perFotogramma;
        if (config < 12) perFotogramma = new int[] {480, 960, 1920, 2880}[config & 3];
        else if (config < 16) perFotogramma = new int[] {480, 960}[config & 1];
        else perFotogramma = new int[] {120, 240, 480, 960}[config & 3];
        int codice = toc & 3;
        int fotogrammi;
        if (codice == 0) fotogrammi = 1;
        else if (codice < 3) fotogrammi = 2;
        else {
            if (d.length < 2) return -1;
            fotogrammi = d[1] & 0x3f;
        }
        return perFotogramma * fotogrammi;
    }

    /** allaFine (può essere null): chiamato sul thread di cattura se questa muore da sola (errore). */
    static Cattura avvia(Uscita uscita, Runnable allaFine) throws Exception {
        Cattura c = new Cattura(uscita, allaFine);
        try {
            c.parti();
        } catch (Throwable t) {
            c.ferma();
            if (t instanceof Exception) throw (Exception) t;
            throw new IllegalStateException(String.valueOf(t), t);
        }
        return c;
    }

    private void parti() throws Exception {
        // Il volume di prima si legge PRIMA di registrare la regola: con la regola attiva il percorso
        // dell'uscita cambia e non si deve mai salvare il valore di un altro dispositivo.
        leggiVolumePrima();
        regola = NascosteAudio.creaRegola();
        MediaFormat f = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_OPUS, FREQUENZA, CANALI);
        f.setInteger(MediaFormat.KEY_BIT_RATE, 128000);
        f.setInteger(MediaFormat.KEY_PCM_ENCODING, android.media.AudioFormat.ENCODING_PCM_16BIT);
        f.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, BYTE_FRAME);
        codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_OPUS);
        codec.configure(f, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
        codec.start();
        alzaVolume();
        thread = new Thread("yasde-audio") {
            @Override
            public void run() {
                ciclo();
            }
        };
        thread.start();
    }

    /**
     * Volume media al massimo (con 0 alcune app non avviano l'audio). Il valore di prima si legge PRIMA di
     * cambiare, e il ripristino va nel guardiano prima del cambio: se il servizio muore, lo rimette lui
     * solo se il volume e' ancora quello che abbiamo messo (il valore dell'utente vince).
     */
    private void leggiVolumePrima() throws IOException {
        int[] v = Volume.leggi();
        volumeOriginale = v[0];
        volumeMassimo = v[1];
    }

    private void alzaVolume() throws IOException {
        if (volumeOriginale >= volumeMassimo) return;
        Guardiano g = Registro.guardiano();
        if (g == null && !senzaGuardianoConsentito) {
            // Senza guardiano un crollo del servizio lascerebbe il volume al massimo: non lo si tocca.
            System.out.println("[audio] nessun guardiano: il volume media non viene alzato");
            return;
        }
        if (g != null && !g.imposta(NOME_GUARDIANO, 50, Volume.comandoRipristino(volumeMassimo, volumeOriginale))) {
            // Il ripristino non e' al sicuro: la cattura prosegue senza toccare il volume.
            System.out.println("[audio] guardiano non sicuro: il volume media non viene alzato");
            return;
        }
        volumeCambiato = true;
        try {
            Volume.imposta(volumeMassimo);
        } catch (IOException e) {
            ripristinaVolume();
            throw e;
        }
        System.out.println("[audio] volume media " + volumeOriginale + " -> " + volumeMassimo);
    }

    /**
     * Rimette il volume di prima, ma toglie il guardiano SOLO a verifica riuscita (o se il volume non e' piu'
     * quello messo da noi: vince l'utente; un «su» premuto a 15 e' ancora «il nostro»). `Volume.imposta`
     * rilegge da se'; se non coincide si ritenta UNA volta. Una sola riga di log, solo numeri, per capire
     * il caso successivo. Gira fuori da ogni lock: i comandi esterni possono durare.
     */
    private void ripristinaVolume() {
        if (!volumeCambiato) return;
        int trovato = -1;
        String azione;
        boolean finito = false;
        try {
            trovato = Volume.leggi()[0];
            if (trovato == volumeMassimo) {
                try {
                    Volume.imposta(volumeOriginale);
                } catch (IOException e) {
                    System.out.println("[audio] ripristino del volume non coincide, si ritenta: " + e.getMessage());
                    Volume.imposta(volumeOriginale);
                }
                azione = "ripristinato";
            } else {
                azione = "lasciato (cambiato dall'utente)";
            }
            finito = true;
        } catch (IOException e) {
            azione = "verifica fallita (resta il guardiano)";
        }
        System.out.println("[audio] volume media: prima=" + volumeOriginale + " massimo=" + volumeMassimo
            + " alla chiusura=" + trovato + " azione=" + azione);
        if (!finito) return;
        volumeCambiato = false;
        Guardiano g = Registro.guardiano();
        if (g != null) g.togli(NOME_GUARDIANO);
    }

    private void ciclo() {
        byte[] frame = new byte[BYTE_FRAME];
        long inCampioni = 0;
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        try {
            AudioRecord rec = regola.record;
            while (!fermato) {
                int letti = 0;
                while (letti < BYTE_FRAME && !fermato) {
                    int n = rec.read(frame, letti, BYTE_FRAME - letti);
                    if (n < 0) throw new IOException("lettura audio fallita (codice " + n + ")");
                    letti += n;
                }
                if (fermato) break;
                blocchiLetti++;
                int i = -1;
                if (scartaUnOgni > 0 && blocchiLetti % scartaUnOgni == 0) {
                    scartati++; // simulazione di prova: il blocco non arriva all'encoder
                } else {
                    i = codec.dequeueInputBuffer(200000);
                }
                if (i < 0) {
                    // Blocco perso (encoder senza buffer libero, o scartato per prova): i suoi campioni si
                    // sommano a `persi` e da li' al tempo dei pacchetti, senza dipendere da come l'encoder numera.
                    mancanzeInput++;
                    persi += CAMPIONI_PACCHETTO;
                } else {
                    ByteBuffer b = codec.getInputBuffer(i);
                    b.clear();
                    b.put(frame, 0, BYTE_FRAME);
                    codec.queueInputBuffer(i, 0, BYTE_FRAME, inCampioni * 1000000L / FREQUENZA, 0);
                }
                inCampioni += CAMPIONI_PACCHETTO;
                drena(info);
            }
        } catch (Throwable t) {
            if (!fermato) {
                errore = t.toString();
                System.out.println("[audio] cattura interrotta: " + t);
                if (allaFine != null) allaFine.run();
            }
        }
    }

    private void drena(MediaCodec.BufferInfo info) throws IOException {
        while (!fermato) {
            int o = codec.dequeueOutputBuffer(info, 0);
            if (o == MediaCodec.INFO_TRY_AGAIN_LATER) return;
            if (o < 0) continue; // cambio di formato e simili: nessun dato
            try {
                if ((info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                    configurazioni++; // intestazioni Opus (non audio): non vanno al PC
                    continue;
                }
                if (info.size <= 0) continue;
                ByteBuffer b = codec.getOutputBuffer(o);
                byte[] dati = new byte[info.size];
                b.position(info.offset);
                b.get(dati);
                int c = campioniDelPacchetto(dati);
                if (c != CAMPIONI_PACCHETTO) non20ms++;
                // Tempo = (campioni dei pacchetti emessi + campioni dei blocchi persi) / 48000. NON si usa il
                // presentationTimeUs dell'encoder (potrebbe contare i campioni che ha codificato, ignorando il
                // nostro): cosi' un blocco perso lascia sempre un salto di 20 ms. Il salto compare con il
                // ritardo dell'encoder (al massimo un pacchetto), ma il totale e' esatto.
                long tempo = (campioni + persi) * 1000000L / FREQUENZA;
                campioni += c > 0 ? c : CAMPIONI_PACCHETTO;
                pacchetti++;
                byteTotali += dati.length;
                uscita.pacchetto(tempo, dati);
            } finally {
                codec.releaseOutputBuffer(o, false);
            }
        }
    }

    /**
     * Ferma tutto, toglie la regola (il telefono torna a suonare) e ripristina il volume. Non tiene lock
     * durante i comandi esterni: il primo chiamante lavora e ritorna true, gli altri aspettano (con scadenza)
     * che finisca e ritornano false.
     */
    boolean ferma() {
        boolean secondo;
        synchronized (this) {
            secondo = chiusa; // sotto il lucchetto solo lettura e impostazione del flag
            if (!secondo) {
                chiusa = true;
                fermato = true;
            }
        }
        if (secondo) {
            try {
                finita.await(40, TimeUnit.SECONDS); // fuori dal lucchetto
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            return false; // il lavoro l'ha fatto un altro
        }
        try {
            if (regola != null) regola.togli(); // sblocca anche la lettura
            if (thread != null && thread != Thread.currentThread()) {
                try {
                    thread.join(3000);
                    if (thread.isAlive()) {
                        StringBuilder sb = new StringBuilder("[audio] thread di cattura ancora vivo dopo 3 s:");
                        for (StackTraceElement e : thread.getStackTrace()) sb.append("\n    ").append(e);
                        System.out.println(sb);
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            }
            if (codec != null) {
                try {
                    codec.stop();
                } catch (Throwable t) {
                    // non era avviato
                }
                codec.release();
            }
            ripristinaVolume();
        } finally {
            finita.countDown();
        }
        return true;
    }
}
