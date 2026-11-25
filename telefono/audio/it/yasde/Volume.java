package it.yasde;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Volume «media» del telefono tramite il comando di shell di Android puro `cmd media_session volume`
 * (stesso mezzo per il servizio e per il guardiano, cosi' la lettura e' identica). Ogni scrittura si
 * rilegge: se il valore non e' quello chiesto e' un errore, mai un silenzio.
 */
final class Volume {
    private static final int FLUSSO_MEDIA = 3; // AudioManager.STREAM_MUSIC
    private static final String BASE = "cmd media_session volume --stream " + FLUSSO_MEDIA;
    private static final Pattern LETTO = Pattern.compile("volume is (\\d+) in range \\[(\\d+)\\.\\.(\\d+)\\]");

    private Volume() {}

    /** Valore attuale e massimo del volume media: {valore, massimo}. */
    static int[] leggi() throws IOException {
        String o = esegui("exec " + BASE + " --get 2>&1");
        Matcher m = LETTO.matcher(o);
        if (!m.find()) throw new IOException("volume media illeggibile: " + o.trim());
        return new int[] {Integer.parseInt(m.group(1)), Integer.parseInt(m.group(3))};
    }

    /** Imposta il volume e verifica con una rilettura. */
    static void imposta(int valore) throws IOException {
        esegui("exec " + BASE + " --set " + valore + " 2>&1");
        int letto = leggi()[0];
        if (letto != valore) throw new IOException("volume media non impostato: voluto " + valore + ", letto " + letto);
    }

    /**
     * Riga di shell autonoma per il guardiano: rimette `originale` solo se il volume e' ancora `nostro`.
     * Niente virgolette (il guardiano la racchiude tra apici): l'ultima riga di `--get` e'
     * «[V] volume is N in range ...», quindi N e' la quarta parola.
     */
    static String comandoRipristino(int nostro, int originale) {
        return "set -- $(" + BASE + " --get 2>&1 | tail -n 1); [ x$4 = x" + nostro + " ] && " + BASE
            + " --set " + originale;
    }

    /**
     * Esegue il comando con un vero tempo massimo: l'output si legge su un thread a parte, cosi' un
     * comando bloccato non blocca il chiamante (waitFor con scadenza, poi destroyForcibly).
     */
    private static String esegui(String comando) throws IOException {
        Process p = new ProcessBuilder("sh", "-c", comando).redirectErrorStream(true).start();
        p.getOutputStream().close();
        final ByteArrayOutputStream o = new ByteArrayOutputStream();
        final InputStream in = p.getInputStream();
        Thread lettore = new Thread("yasde-audio-volume") {
            @Override
            public void run() {
                byte[] buf = new byte[1024];
                try {
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        synchronized (o) {
                            o.write(buf, 0, n);
                        }
                    }
                } catch (IOException e) {
                    // processo ucciso
                }
            }
        };
        lettore.setDaemon(true);
        lettore.start();
        try {
            if (!p.waitFor(10, TimeUnit.SECONDS)) {
                p.destroyForcibly();
                throw new IOException("comando del volume scaduto");
            }
            lettore.join(2000);
        } catch (InterruptedException e) {
            p.destroyForcibly();
            Thread.currentThread().interrupt();
            throw new IOException("comando del volume interrotto");
        }
        synchronized (o) {
            return o.toString("UTF-8");
        }
    }
}
