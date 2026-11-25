package it.yasde;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.TimeUnit;

/** Comandi di shell con un vero tempo massimo (come `Volume` del pezzo audio, riscritto qui). */
final class ShellPannello {
    private ShellPannello() {}

    /**
     * Esegue il comando (stdout e stderr insieme) e ne ritorna l'output. L'output si legge su un thread
     * a parte, cosi' un comando bloccato non blocca il chiamante: waitFor con scadenza, poi destroyForcibly.
     */
    static String esegui(int secondi, String... comando) throws IOException {
        Process p = new ProcessBuilder(comando).redirectErrorStream(true).start();
        p.getOutputStream().close();
        final ByteArrayOutputStream o = new ByteArrayOutputStream();
        final InputStream in = p.getInputStream();
        Thread lettore = new Thread("yasde-pannello-shell") {
            @Override
            public void run() {
                byte[] buf = new byte[4096];
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
            if (!p.waitFor(secondi, TimeUnit.SECONDS)) {
                p.destroyForcibly();
                throw new IOException("comando scaduto: " + comando[0]);
            }
            lettore.join(2000);
        } catch (InterruptedException e) {
            p.destroyForcibly();
            Thread.currentThread().interrupt();
            throw new IOException("comando interrotto: " + comando[0]);
        }
        synchronized (o) {
            return o.toString("UTF-8");
        }
    }
}
