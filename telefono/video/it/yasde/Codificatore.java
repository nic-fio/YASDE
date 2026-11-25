package it.yasde;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;
import android.os.Bundle;
import android.view.Surface;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

/**
 * Un codificatore H.264 hardware con la sua Surface d'ingresso e il thread che ne legge i
 * pacchetti. Parte subito e scarta i pacchetti finche' il PC
 * non apre il canale video: lo schermo virtuale ha bisogno della Surface fin dall'apertura.
 */
final class Codificatore {
    static final int CONFIGURAZIONE = 1;
    static final int FOTOGRAMMA = 2;

    /** Dove vanno i pacchetti (il canale del PC, o un file nelle prove). */
    interface Uscita {
        /** Ritorna false se il pacchetto e' stato scartato (non scritto): il chiamante non lo conta come inviato. */
        boolean pacchetto(int genere, int segni, int larghezza, int altezza, long tempoUs, byte[] dati) throws IOException;
    }

    /** Chiamato se il codificatore muore da solo. */
    interface Guasto {
        void errore(Codificatore c, Throwable t);
    }

    private static final String TIPO = MediaFormat.MIMETYPE_VIDEO_AVC;

    final String nome;
    final boolean sps;      // true = configurato con SPS/PPS anteposti ai fotogrammi chiave
    final int larghezza, altezza;
    final Surface superficie;

    private final MediaCodec codec;
    private final Guasto guasto;
    // Stato del solo thread del codificatore (config, flag); attiva/disattiva non prendono mai
    // un lucchetto, cosi' non possono restare bloccati da una write ferma sul canale.
    private volatile Uscita uscita;
    private final Inoltro inoltro = new Inoltro();
    private volatile boolean fermo;
    private volatile long chiavi;
    private Thread thread;

    private Codificatore(String nome, boolean sps, int l, int a, MediaCodec codec, Surface s, Guasto g) {
        this.nome = nome;
        this.sps = sps;
        this.larghezza = l;
        this.altezza = a;
        this.codec = codec;
        this.superficie = s;
        this.guasto = g;
    }

    private static List<String> nomiHardware() {
        List<String> r = new ArrayList<>();
        for (MediaCodecInfo i : new MediaCodecList(MediaCodecList.REGULAR_CODECS).getCodecInfos()) {
            if (!i.isEncoder() || i.isAlias() || !i.isHardwareAccelerated() || i.isSoftwareOnly()) continue;
            for (String t : i.getSupportedTypes()) {
                if (t.equalsIgnoreCase(TIPO)) {
                    r.add(i.getName());
                    break;
                }
            }
        }
        return r;
    }

    private static MediaFormat formato(int l, int a, int bps, int fps, boolean sps) {
        MediaFormat f = MediaFormat.createVideoFormat(TIPO, l, a);
        f.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
        f.setInteger(MediaFormat.KEY_BIT_RATE, bps);
        f.setInteger(MediaFormat.KEY_FRAME_RATE, fps);
        f.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 10);
        f.setInteger(MediaFormat.KEY_PRIORITY, 0); // tempo reale
        // Senza questo, a schermo fermo molti codificatori non emettono nulla.
        f.setLong(MediaFormat.KEY_REPEAT_PREVIOUS_FRAME_AFTER, 100000L);
        if (sps) f.setInteger("prepend-sps-pps-to-idr-frames", 1);
        return f;
    }

    private static Codificatore prova(String nome, boolean sps, int l, int a, int bps, int fps, Guasto g)
            throws IOException {
        MediaCodec c = nome != null ? MediaCodec.createByCodecName(nome) : MediaCodec.createEncoderByType(TIPO);
        Surface s = null;
        try {
            c.configure(formato(l, a, bps, fps, sps), null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            s = c.createInputSurface();
            c.start();
            Codificatore r = new Codificatore(c.getName(), sps, l, a, c, s, g);
            r.avvia();
            return r;
        } catch (RuntimeException e) {
            if (s != null) s.release();
            c.release();
            throw e;
        }
    }

    /**
     * Ripiego in ordine: ogni encoder hardware con SPS/PPS anteposti, poi ogni encoder hardware
     * senza, poi l'encoder predefinito di Android. L'ultimo errore finisce nel messaggio.
     */
    static Codificatore crea(int l, int a, int bps, int fps, Guasto g) throws IOException {
        List<String> hw = nomiHardware();
        String ultimo = "nessun encoder hardware H.264";
        for (int giro = 0; giro < 2; giro++) {
            for (String n : hw) {
                try {
                    return prova(n, giro == 0, l, a, bps, fps, g);
                } catch (RuntimeException | IOException e) {
                    ultimo = n + ": " + e;
                }
            }
        }
        try {
            return prova(null, false, l, a, bps, fps, g);
        } catch (RuntimeException | IOException e) {
            throw new IOException("codificatore H.264 non configurabile (" + ultimo + "; predefinito: " + e + ")", e);
        }
    }

    private void avvia() {
        // Non lo si sospende all'avvio: sospeso fin dall'inizio, a schermo fermo non emette
        // nemmeno la configurazione e poi non c'e' un fotogramma da ripetere. Finche' il canale
        // non c'e' i pacchetti si scartano, e la configurazione si tiene da parte.
        thread = new Thread("yasde-codificatore") {
            @Override
            public void run() {
                ciclo();
            }
        };
        thread.setDaemon(true);
        thread.start();
    }

    private void ciclo() {
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        try {
            while (!fermo) {
                int i = codec.dequeueOutputBuffer(info, 100000);
                if (i < 0) continue;
                if ((info.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0) chiavi++;
                try {
                    ByteBuffer b = codec.getOutputBuffer(i);
                    if (b != null && info.size > 0) {
                        b.position(info.offset);
                        b.limit(info.offset + info.size);
                        byte[] d = new byte[info.size];
                        b.get(d);
                        inoltra(info.flags, info.presentationTimeUs, d);
                    }
                } finally {
                    codec.releaseOutputBuffer(i, false);
                }
            }
        } catch (Throwable t) {
            if (!fermo && guasto != null) guasto.errore(this, t);
        }
    }

    private void inoltra(int flags, long us, byte[] dati) {
        Uscita u = fermo ? null : uscita;
        try {
            inoltro.pacchetto(u, (flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0,
                    (flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0, larghezza, altezza, us, dati);
        } catch (IOException e) {
            if (uscita == u) uscita = null; // canale chiuso dal PC: si continua a scartare
        }
    }

    /** Il PC ha aperto il canale: dal prossimo pacchetto CONFIGURAZIONE e poi un fotogramma chiave. */
    void attiva(Uscita u) {
        uscita = u;
        sospendi(false);
        chiave();
    }

    /** L'uscita corrente (campo volatile): serve al filtro per scartare le uscite ormai sostituite. */
    Uscita uscitaAttuale() {
        return uscita;
    }

    /** Il PC ha chiuso il canale: il codificatore smette di produrre. */
    void disattiva() {
        uscita = null;
        sospendi(true);
    }

    /** Quanti fotogrammi chiave ha emesso finora (con o senza canale). */
    long chiaviViste() {
        return chiavi;
    }

    void chiave() {
        try {
            Bundle b = new Bundle();
            b.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0);
            codec.setParameters(b);
        } catch (RuntimeException e) {
            // codec gia' fermo: nessun effetto
        }
    }

    void sospendi(boolean si) {
        try {
            Bundle b = new Bundle();
            b.putInt(MediaCodec.PARAMETER_KEY_SUSPEND, si ? 1 : 0);
            codec.setParameters(b);
        } catch (RuntimeException e) {
            // non supportato: i pacchetti si scartano comunque senza canale
        }
    }

    void rilascia() {
        fermo = true;
        uscita = null;
        Thread t = thread;
        if (t != null && t != Thread.currentThread()) {
            try {
                t.join(1000);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
        try {
            codec.stop();
        } catch (RuntimeException e) {
            // gia' fermo
        }
        codec.release();
        superficie.release();
    }
}
