package it.yasde;

import android.net.LocalSocket;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Canale `cerca` (docs/protocollo.md §2.7): una ricerca di nomi per canale.
 *
 * Limiti (scelti per tenere la ricerca utile e sempre breve):
 *  - massimo 100000 cartelle visitate (coda compresa) e 30 secondi in tutto;
 *    superati: CERCA_FINE con motivo 1 e i risultati gia' mandati restano validi;
 *  - /proc, /sys, /dev, /acct, /config non si attraversano scendendo da `/` (ma si cercano se sono la partenza);
 *  - coda delle cartelle da visitare al massimo 16 MB di percorsi;
 *  - massimo di risultati: quello chiesto dal PC, mai oltre 5000 (0 = 5000);
 *  - cartella di partenza inesistente, non assoluta o non leggibile: CERCA_FINE motivo 2;
 *  - percorso di partenza al massimo 4096 byte, testo cercato al massimo 1024 byte.
 */
public final class Cerca {
    private static final int CERCA = 0x78, TROVATO = 0x79, CERCA_FINE = 0x7a;
    static final int MAX_CARTELLE = 100000;
    static final long MAX_MILLIS = 30000;
    static final int MAX_RISULTATI = 5000;
    private static final int MAX_PERCORSO = 4096, MAX_TESTO = 1024, MAX_MESSAGGIO = 8192;

    /** Mai attraversate scendendo da una cartella piu' in alto (ma permesse come partenza esplicita). */
    static final String[] ESCLUSI = {"/proc", "/sys", "/dev", "/acct", "/config"};

    private Cerca() {}

    public static void registra() {
        Registro.registraCanale("cerca", new GestoreCanale() {
            @Override
            public void gestisci(LocalSocket s, String argomento) throws Exception {
                canale(s);
            }
        });
    }

    private static void fine(OutputStream out, int motivo) {
        try {
            BinarioArchivio.scrivi(out, CERCA_FINE, new byte[] {(byte) motivo});
        } catch (IOException e) {
            // canale gia' chiuso: nessuno da avvisare
        }
    }

    private static void canale(final LocalSocket s) throws Exception {
        final InputStream grezzo = s.getInputStream();
        final OutputStream out = s.getOutputStream();
        final String radice0, testo;
        final boolean soloCartelle;
        final int massimo;
        try {
            DataInputStream in = new DataInputStream(grezzo);
            int tipo = in.read();
            if (tipo != CERCA) throw new IllegalArgumentException("messaggio non valido");
            int lunghezza = in.readInt();
            if (lunghezza < 0 || lunghezza > MAX_MESSAGGIO) throw new IllegalArgumentException("messaggio troppo grande");
            byte[] corpo = new byte[lunghezza];
            in.readFully(corpo);
            DataInputStream c = new DataInputStream(new ByteArrayInputStream(corpo));
            radice0 = BinarioArchivio.stringa(c, MAX_PERCORSO);
            testo = BinarioArchivio.stringa(c, MAX_TESTO);
            soloCartelle = c.readUnsignedByte() != 0;
            int m = c.readUnsignedShort();
            massimo = (m == 0 || m > MAX_RISULTATI) ? MAX_RISULTATI : m;
        } catch (Throwable t) {
            fine(out, 2);
            closeQuiet(s);
            return;
        }
        final String ago = ConfrontoNomi.normalizza(testo);
        // vuoto dopo la normalizzazione (es. solo accenti combinanti) = troverebbe tutto: rifiutato
        String r = null;
        if (radice0.startsWith("/") && radice0.indexOf('\0') < 0) {
            try {
                // solo a livello di testo (toglie //, /./ e ..): niente getCanonicalPath, la
                // partenza resta quella scelta dall'utente e i collegamenti non si risolvono.
                // Serve perche' l'esclusione di /proc & co. confronta percorsi esatti.
                r = new java.io.File(radice0.replaceAll("/+", "/")).toPath().normalize().toString();
            } catch (RuntimeException e) {
                r = null;
            }
        }
        final String radice = r;
        if (radice == null || ago.isEmpty() || !isDirLeggibile(new java.io.File(radice))) {
            fine(out, 2);
            closeQuiet(s);
            return;
        }

        // Il PC dopo CERCA non scrive piu': una lettura che ritorna significa canale chiuso.
        // Questo thread finisce da solo quando chiudiamo il socket (in `finally`).
        final boolean[] chiuso = new boolean[1];
        Thread osservatore = new Thread("cerca-osservatore") {
            @Override
            public void run() {
                try {
                    byte[] b = new byte[64];
                    while (grezzo.read(b) >= 0) {
                        // ignora: non dovrebbe arrivare nulla
                    }
                } catch (Throwable t) {
                    // chiuso
                }
                synchronized (chiuso) {
                    chiuso[0] = true;
                }
            }
        };
        osservatore.setDaemon(true);
        osservatore.start();

        Percorrenza p = new Percorrenza(ago, soloCartelle, massimo,
                MAX_CARTELLE, MAX_MILLIS, new Percorrenza.Interruzione() {
                    @Override
                    public boolean fermati() {
                        synchronized (chiuso) {
                            return chiuso[0];
                        }
                    }
                }, ESCLUSI);
        int motivo;
        try {
            int esito = p.cerca(radice, new Percorrenza.Ricevitore() {
                @Override
                public void trovato(String percorso, boolean cartella) throws IOException {
                    BinarioArchivio.scrivi(out, TROVATO, BinarioArchivio.trovato(percorso, cartella));
                }
            });
            if (esito == Percorrenza.INTERROTTA) return;
            motivo = esito == Percorrenza.FINITA ? 0 : 1;
            fine(out, motivo);
        } catch (IOException e) {
            // scrittura fallita: il PC ha chiuso
        } catch (Throwable t) {
            fine(out, 2);
        } finally {
            closeQuiet(s);
            // il socket chiuso sblocca l'osservatore; si aspetta poco per non restare vivi
            try {
                osservatore.join(500);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }

    /** La cartella di partenza deve esistere, essere una cartella ed essere leggibile (altrimenti motivo 2). */
    private static boolean isDirLeggibile(java.io.File f) {
        return f.isDirectory() && f.canRead();
    }

    private static void closeQuiet(LocalSocket s) {
        try {
            s.shutdownInput();
        } catch (Throwable t) {
            // gia' chiuso
        }
        try {
            s.close();
        } catch (Throwable t) {
            // gia' chiuso
        }
    }
}
