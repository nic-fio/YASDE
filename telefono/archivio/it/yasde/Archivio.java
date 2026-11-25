package it.yasde;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.RectF;
import android.media.MediaMetadataRetriever;
import android.os.StatFs;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.IOException;

/** Comandi 0x74 MINIATURA e 0x75 MEMORIA (docs/protocollo.md §2.6). */
public final class Archivio {
    private static final int MINIATURA = 0x74, MEMORIA = 0x75;
    private static final int MAX_PERCORSO = 4096;
    /** Oltre queste misure una foto e' rifiutata: nemmeno la decodifica ridotta sarebbe prudente. */
    private static final long MAX_BYTE_FOTO = 256L << 20;
    /**
     * Tetto dei pixel: 300 MP solo per JPEG, perche' il decodificatore JPEG lavora a strisce e con
     * `inSampleSize` non tiene in memoria l'immagine intera; gli altri formati (PNG, WebP, ...) spesso
     * decodificano comunque l'intera immagine prima di ridurla: 50 MP evitano di esaurire la memoria.
     */
    private static final long MAX_PIXEL_JPEG = 300L * 1000 * 1000, MAX_PIXEL_ALTRI = 50L * 1000 * 1000;
    /** Risposta sempre sotto 1 MiB (Protocollo.MAX_CORPO). */
    private static final int MAX_RISPOSTA = (1 << 20) - 1024;

    private Archivio() {}

    public static void registra() {
        Registro.registraComandi(0x74, 0x75, new GestoreComandi() {
            @Override
            public byte[] gestisci(int tipo, DataInputStream corpo) throws Exception {
                switch (tipo) {
                    case MINIATURA: {
                        String percorso = BinarioArchivio.stringa(corpo, MAX_PERCORSO);
                        int lato = corpo.readUnsignedShort();
                        return miniatura(percorso, lato);
                    }
                    case MEMORIA:
                        return memoria();
                    default:
                        throw new IllegalArgumentException("tipo sconosciuto");
                }
            }
        });
    }

    static byte[] memoria() throws IOException {
        StatFs sf;
        try {
            sf = new StatFs("/storage/emulated/0");
        } catch (IllegalArgumentException e) {
            try {
                sf = new StatFs("/sdcard");
            } catch (IllegalArgumentException e2) {
                throw new IOException("memoria del telefono non leggibile");
            }
        }
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        DataOutputStream o = new DataOutputStream(bo);
        o.writeLong(sf.getTotalBytes());
        o.writeLong(sf.getAvailableBytes());
        return bo.toByteArray();
    }

    static byte[] miniatura(String percorso, int lato) throws IOException {
        if (lato < 32 || lato > 512) throw new IllegalArgumentException("lato non valido (da 32 a 512)");
        if (!percorso.startsWith("/") || percorso.indexOf('\0') >= 0) {
            throw new IllegalArgumentException("percorso non valido");
        }
        File f = new File(percorso);
        if (!f.exists()) throw new IOException("file non trovato");
        if (!f.isFile()) throw new IOException("non è un file");
        if (!f.canRead()) throw new IOException("file non leggibile");

        // Prima solo le misure: dice se e' un'immagine senza caricarla.
        BitmapFactory.Options o = new BitmapFactory.Options();
        o.inJustDecodeBounds = true;
        BitmapFactory.decodeFile(percorso, o);
        Bitmap sorgente;
        int orientazione = 1;
        if (o.outWidth > 0 && o.outHeight > 0) {
            long tetto = "image/jpeg".equals(o.outMimeType) ? MAX_PIXEL_JPEG : MAX_PIXEL_ALTRI;
            if (f.length() > MAX_BYTE_FOTO || (long) o.outWidth * o.outHeight > tetto) {
                throw new IOException("immagine troppo grande");
            }
            int campione = 1;
            while (Math.max(o.outWidth, o.outHeight) / (campione * 2) >= lato) campione *= 2;
            BitmapFactory.Options d = new BitmapFactory.Options();
            d.inSampleSize = campione;
            try {
                sorgente = BitmapFactory.decodeFile(percorso, d);
            } catch (OutOfMemoryError e) {
                throw new IOException("memoria insufficiente per la miniatura");
            }
            if (sorgente == null) throw new IOException("immagine non decodificabile");
            orientazione = orientazioneExif(percorso);
        } else {
            try {
                sorgente = fotogramma(percorso, lato);
            } catch (OutOfMemoryError e) {
                throw new IOException("memoria insufficiente per la miniatura");
            }
            if (sorgente == null) throw new IOException("non è un'immagine né un video");
        }
        try {
            return comprimi(sorgente, orientazione, lato);
        } catch (OutOfMemoryError e) {
            throw new IOException("memoria insufficiente per la miniatura");
        } finally {
            sorgente.recycle();
        }
    }

    @SuppressWarnings("deprecation")
    private static int orientazioneExif(String percorso) {
        try {
            return new android.media.ExifInterface(percorso).getAttributeInt(
                    android.media.ExifInterface.TAG_ORIENTATION, android.media.ExifInterface.ORIENTATION_NORMAL);
        } catch (Throwable t) {
            return 1; // EXIF illeggibile: foto com'e'
        }
    }

    /** Un fotogramma del video, o null se il file non lo e'. */
    private static Bitmap fotogramma(String percorso, int lato) {
        MediaMetadataRetriever r = new MediaMetadataRetriever();
        try {
            r.setDataSource(percorso);
            // Fotogramma gia' ridotto dal decodificatore: a piena risoluzione un 8K pesa ~132 MB.
            int w = intero(r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH));
            int h = intero(r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT));
            int rot = intero(r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_ROTATION));
            if (rot == 90 || rot == 270) {
                int t = w;
                w = h;
                h = t;
            }
            if (w <= 0 || h <= 0) return null;
            float k = Math.min(1f, (float) lato / Math.max(w, h));
            return r.getScaledFrameAtTime(-1, MediaMetadataRetriever.OPTION_CLOSEST_SYNC,
                    Math.max(1, Math.round(w * k)), Math.max(1, Math.round(h * k)));
        } catch (OutOfMemoryError e) {
            throw e; // lo traduce miniatura() in una frase chiara
        } catch (Throwable t) {
            return null;
        } finally {
            try {
                r.release();
            } catch (Throwable t) {
                // niente da fare
            }
        }
    }

    private static int intero(String s) {
        try {
            return s == null ? 0 : Integer.parseInt(s);
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    /** Raddrizza secondo EXIF, riduce a `lato` (mai ingrandisce), su fondo bianco, JPEG. */
    private static byte[] comprimi(Bitmap src, int orientazione, int lato) throws IOException {
        Matrix m = new Matrix();
        switch (orientazione) {
            case 2: m.postScale(-1, 1); break;
            case 3: m.postRotate(180); break;
            case 4: m.postScale(1, -1); break;
            case 5: m.postRotate(90); m.postScale(-1, 1); break;
            case 6: m.postRotate(90); break;
            case 7: m.postRotate(270); m.postScale(-1, 1); break;
            case 8: m.postRotate(270); break;
            default: break;
        }
        RectF r = new RectF(0, 0, src.getWidth(), src.getHeight());
        m.mapRect(r);
        float scala = Math.min(1f, (float) lato / Math.max(r.width(), r.height()));
        m.postScale(scala, scala);
        r.set(0, 0, src.getWidth(), src.getHeight());
        m.mapRect(r);
        m.postTranslate(-r.left, -r.top);
        int w = Math.max(1, Math.round(r.width())), h = Math.max(1, Math.round(r.height()));
        Bitmap dst = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        try {
            Canvas c = new Canvas(dst);
            c.drawColor(Color.WHITE); // un PNG trasparente non diventa nero nel JPEG
            Paint p = new Paint(Paint.FILTER_BITMAP_FLAG);
            c.drawBitmap(src, m, p);
            for (int qualita = 85; qualita >= 40; qualita -= 15) {
                ByteArrayOutputStream bo = new ByteArrayOutputStream();
                if (!dst.compress(Bitmap.CompressFormat.JPEG, qualita, bo)) {
                    throw new IOException("compressione fallita");
                }
                if (bo.size() <= MAX_RISPOSTA) return bo.toByteArray();
            }
            throw new IOException("miniatura troppo grande");
        } finally {
            dst.recycle();
        }
    }
}
