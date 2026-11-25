package it.yasde;

import android.net.LocalSocket;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.List;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;

/** Canale `audio` (docs/protocollo.md §2.5): aprirlo avvia la cattura, chiuderlo la ferma. Uno solo alla volta. */
public final class Audio {
    /** Pacchetti in attesa verso il PC: oltre questi (5 s) si butta il piu' vecchio, il salto si vede dal tempo. */
    private static final int CODA_MAX = 250;

    /** Quanto un nuovo canale aspetta che il precedente finisca di chiudersi. */
    private static final long ATTESA_FINE_MS = 2000;

    private static final Object serratura = new Object();
    private static Cattura attiva; // protetta da serratura
    private static boolean chiuso; // il servizio sta chiudendo: nessuna nuova cattura (protetta da serratura)
    private static LocalSocket socketAttivo;

    private Audio() {}

    public static void registra() {
        List<String> mancano = NascosteAudio.verifica();
        if (mancano.isEmpty()) {
            System.out.println("[audio] API di Android necessarie: tutte presenti");
        } else {
            for (String m : mancano) System.out.println("[audio] API MANCANTE: " + m);
        }
        Registro.registraChiusura(new Runnable() {
            @Override
            public void run() {
                chiudiTutto();
            }
        });
        Registro.registraCanale("audio", new GestoreCanale() {
            @Override
            public void gestisci(LocalSocket s, String argomento) throws Exception {
                canale(s);
            }
        });
    }

    /** Un pacchetto nel formato di §2.5: tempo (8, big endian), lunghezza (2), dati. */
    static byte[] imballa(long tempoUs, byte[] dati) {
        byte[] b = new byte[10 + dati.length];
        for (int i = 0; i < 8; i++) b[i] = (byte) (tempoUs >>> (56 - 8 * i));
        b[8] = (byte) (dati.length >>> 8);
        b[9] = (byte) dati.length;
        System.arraycopy(dati, 0, b, 10, dati.length);
        return b;
    }

    private static void canale(final LocalSocket s) throws Exception {
        final BlockingQueue<byte[]> coda = new ArrayBlockingQueue<>(CODA_MAX);
        final OutputStream out = s.getOutputStream();
        final Cattura cattura;
        synchronized (serratura) {
            // Anche una cattura che si sta fermando e' ancora "attiva": ferma() gira dentro la serratura.
            // Un canale appena richiuso dal PC puo' non essere ancora notato: si aspetta un poco che finisca,
            // poi si rifiuta (un secondo canale vero resta aperto a lungo e viene chiuso).
            long scade = System.currentTimeMillis() + ATTESA_FINE_MS;
            while (attiva != null || chiuso) {
                if (chiuso) return; // servizio in chiusura: niente cattura (il servizio chiude il socket)
                long resto = scade - System.currentTimeMillis();
                if (resto <= 0) {
                    System.out.println("[audio] secondo canale audio rifiutato");
                    return; // il servizio chiude il socket
                }
                serratura.wait(resto);
            }
            if (chiuso) return;
            cattura = Cattura.avvia(new Cattura.Uscita() {
                @Override
                public void pacchetto(long tempoUs, byte[] dati) {
                    byte[] p = imballa(tempoUs, dati);
                    while (!coda.offer(p)) coda.poll();
                }
            }, new Runnable() {
                @Override
                public void run() {
                    chiudiSocket(s); // la cattura e' morta: il PC lo vede dalla chiusura
                }
            });
            attiva = cattura;
            socketAttivo = s;
        }
        Thread scrittore = null;
        try {
            // Thread che scrive sul socket: una scrittura lenta non deve fermare la cattura.
            scrittore = new Thread("yasde-audio-invio") {
                @Override
                public void run() {
                    try {
                        while (true) {
                            byte[] p = coda.take();
                            out.write(p);
                            out.flush();
                        }
                    } catch (InterruptedException e) {
                        // fine
                    } catch (Throwable t) {
                        chiudiSocket(s);
                    }
                }
            };
            scrittore.setDaemon(true);
            scrittore.start();
            InputStream in = s.getInputStream();
            byte[] buf = new byte[64];
            while (in.read(buf) >= 0) {
                // il PC non scrive mai su questo canale: si aspetta solo la chiusura
            }
        } catch (IOException e) {
            // socket chiuso da noi
        } finally {
            if (scrittore != null) scrittore.interrupt();
            chiudiSocket(s); // sblocca anche una write ferma perche' il PC non legge
            fermaAttiva(cattura);
        }
    }

    /** Chiude in tutti i modi: shutdownOutput sblocca una write ferma, shutdownInput una read. */
    private static void chiudiSocket(LocalSocket s) {
        try {
            s.shutdownOutput();
        } catch (IOException e) {
            // gia' chiuso
        }
        try {
            s.shutdownInput();
        } catch (IOException e) {
            // gia' chiuso
        }
        try {
            s.close();
        } catch (IOException e) {
            // gia' chiuso
        }
    }

    /**
     * Ferma la cattura FUORI dalla serratura (il ripristino del volume lancia comandi esterni): `attiva`
     * resta impostata finche' non ha finito, cosi' un canale nuovo aspetta (mai due regole, mai il guardiano
     * di una cancellato dall'altra); poi, sotto serratura, si azzera e si sveglia chi aspetta.
     */
    private static void fermaAttiva(Cattura c) {
        synchronized (serratura) {
            if (attiva != c) return; // gia' fermata (canale chiuso e chiusura del servizio insieme)
        }
        boolean fatto = false;
        try {
            fatto = c.ferma(); // true solo per chi ha fatto davvero il lavoro; gli altri aspettano e ritornano false
        } catch (RuntimeException | Error e) {
            fatto = true; // il lavoro era suo anche se e' fallito: il posto va liberato comunque
            throw e;
        } finally {
            synchronized (serratura) {
                // Solo chi ha lavorato azzera `attiva`, a lavoro finito: un secondo chiamante scaduto non deve
                // liberare il posto mentre il primo sta ancora ripristinando (il guardiano e' unico).
                if (fatto && attiva == c) {
                    attiva = null;
                    socketAttivo = null;
                }
                serratura.notifyAll();
            }
            if (fatto) {
                System.out.println("[audio] cattura chiusa: pacchetti=" + c.pacchetti + " non20ms=" + c.non20ms
                    + " intestazioni=" + c.configurazioni);
            }
        }
    }

    /** Chiusura ordinata del servizio. */
    static void chiudiTutto() {
        Cattura c;
        LocalSocket s;
        synchronized (serratura) {
            chiuso = true; // dopo i ripristini del guardiano non deve partire altro
            serratura.notifyAll(); // sveglia chi aspetta: trovera' chiuso
            c = attiva;
            s = socketAttivo;
        }
        if (c != null) {
            if (s != null) chiudiSocket(s);
            fermaAttiva(c);
        }
    }
}
