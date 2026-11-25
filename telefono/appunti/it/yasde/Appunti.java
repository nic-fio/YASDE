package it.yasde;

import android.content.ClipData;
import android.content.ClipDescription;
import android.net.LocalSocket;
import android.os.PersistableBundle;

import java.io.BufferedInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.List;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;

/**
 * Canale `appunti` (docs/protocollo.md par. 2.8): appunti condivisi PC e telefono, solo testo. Uno solo alla volta.
 *
 * Criterio per cio' che NON passa mai al PC (specifica 4.11):
 *  - copie marcate sensibili: la marca standard di Android, `ClipDescription.EXTRA_IS_SENSITIVE` nelle extras
 *    della descrizione. I gestori di password la mettono (Android 13+) e il sistema la usa per nascondere
 *    l'anteprima: e' il criterio affidabile e senza elenchi da mantenere;
 *  - testi oltre 200.000 byte UTF-8 (e testi vuoti);
 *  - contenuti non testuali (solo text/plain e text/html, e di questo il testo semplice).
 *  - gestori di password: IClipboard.getPrimaryClipSource dice il pacchetto che ha fatto la copia (anche alla
 *    shell, provato); se e' in `Regole.GESTORI_PASSWORD` (i gestori piu' diffusi, per nome) la copia non passa.
 *    La lista copre solo i nomi noti: un gestore sconosciuto che non mette la marca passerebbe.
 *
 * PC -> telefono: APPUNTI_IMPOSTA e APPUNTI_INCOLLA (testi oltre 200.000 byte ignorati). Niente eco: la copia
 * messa da YASDE (etichetta "yasde") non torna al PC. Alla chiusura del canale gli appunti restano come sono.
 */
public final class Appunti {
    /** Quanto un nuovo canale aspetta che il precedente finisca di chiudersi. */
    private static final long ATTESA_FINE_MS = 2000;

    private static final Object serratura = new Object();
    private static LocalSocket attivo; // protetta da serratura
    private static boolean fineCanale = true; // il canale attivo e' del tutto chiuso (protetta da serratura)
    private static boolean chiuso; // il servizio sta chiudendo (protetta da serratura)

    private Appunti() {}

    public static void registra() {
        List<String> mancano = NascosteAppunti.verifica();
        if (mancano.isEmpty()) {
            System.out.println("[appunti] API di Android necessarie: tutte presenti");
        } else {
            for (String m : mancano) System.out.println("[appunti] API MANCANTE: " + m);
        }
        Registro.registraChiusura(new Runnable() {
            @Override
            public void run() {
                chiudiTutto();
            }
        });
        Registro.registraCanale("appunti", new GestoreCanale() {
            @Override
            public void gestisci(LocalSocket s, String argomento) throws Exception {
                canale(s);
            }
        });
    }

    /** Il testo di una copia, o null se non e' testo. */
    private static String testoDi(ClipData c) {
        ClipDescription d = c.getDescription();
        if (d == null || c.getItemCount() < 1) return null;
        if (!d.hasMimeType(ClipDescription.MIMETYPE_TEXT_PLAIN) && !d.hasMimeType(ClipDescription.MIMETYPE_TEXT_HTML)) {
            return null;
        }
        CharSequence t = c.getItemAt(0).getText();
        return t == null ? null : t.toString();
    }

    private static boolean sensibile(ClipData c) {
        ClipDescription d = c.getDescription();
        PersistableBundle extras = d == null ? null : d.getExtras();
        return extras != null && extras.getBoolean(ClipDescription.EXTRA_IS_SENSITIVE, false);
    }

    private static String fonteONull() {
        try {
            return NascosteAppunti.fonte();
        } catch (IllegalStateException e) {
            return null; // il sistema non dice la fonte: resta la marca "sensibile"
        }
    }

    /** Legge gli appunti e, se le regole lo permettono, manda il testo al PC. */
    private static void inviaCambio(OutputStream out, Regole.Eco eco) throws IOException {
        // La fonte si legge prima e dopo gli appunti: se nel frattempo e' arrivata un'altra copia le due
        // differiscono e non si manda (la copia nuova fara' scattare un nuovo avviso).
        String fontePrima = fonteONull();
        ClipData c;
        try {
            c = NascosteAppunti.leggi();
        } catch (IllegalStateException e) {
            System.out.println("[appunti] ERRORE lettura appunti: " + e.getMessage());
            return;
        }
        String fonteDopo = fonteONull();
        if (c == null) return;
        String testo = testoDi(c);
        ClipDescription d = c.getDescription();
        if (testo == null) return; // non testuale: ignorato
        // Il sistema avvisa due volte per ogni copia (visto sul telefono): stessa marca di tempo e stesso
        // testo, quindi si valuta una volta sola. Una copia scartata per fonte incerta non conta come vista.
        long marca = d == null ? 0 : d.getTimestamp();
        int esito = eco.daValutare(marca, testo, fontePrima, fonteDopo);
        if (esito == Regole.Eco.FONTE_INCERTA) {
            System.out.println("[appunti] copia non inviata (fonte incerta)");
            return;
        }
        if (esito == Regole.Eco.DA_GESTORE) {
            System.out.println("[appunti] copia non inviata (da gestore di password: "
                + (Regole.eGestorePassword(fonteDopo) ? fonteDopo : fontePrima) + ")");
            return;
        }
        if (esito == Regole.Eco.DOPPIONE) return;
        if (!Regole.puoPartire(testo, sensibile(c), fonteDopo)) {
            System.out.println("[appunti] copia non inviata (sensibile, vuota o troppo lunga)");
            return;
        }
        CharSequence etichetta = d == null ? null : d.getLabel();
        if (eco.eEco(testo, etichetta == null ? null : etichetta.toString(), fonteDopo)) return;
        byte[] m = Regole.messaggio(Regole.APPUNTI_TESTO, Regole.corpoTesto(testo));
        out.write(m);
        out.flush();
    }

    private static void impostaDalPc(String testo, Regole.Eco eco) {
        eco.daPc(testo); // prima: l'avviso di cambio puo' arrivare prima che imposta() ritorni
        try {
            NascosteAppunti.imposta(ClipData.newPlainText(Regole.ETICHETTA, testo));
        } catch (RuntimeException e) {
            eco.annulla(); // la copia non e' arrivata: niente da riconoscere come eco
            throw e;
        }
    }

    private static void esegui(int tipo, byte[] corpo, Regole.Eco eco) {
        try {
            if (tipo == Regole.APPUNTI_IMPOSTA) {
                String t = Regole.leggiImposta(corpo);
                if (t == null) {
                    System.out.println("[appunti] APPUNTI_IMPOSTA ignorato (testo vuoto, troppo lungo o non valido)");
                    return;
                }
                impostaDalPc(t, eco);
            } else if (tipo == Regole.APPUNTI_INCOLLA) {
                Regole.Incolla i = Regole.leggiIncolla(corpo);
                if (i == null) {
                    System.out.println("[appunti] APPUNTI_INCOLLA ignorato (testo vuoto, troppo lungo o non valido)");
                    return;
                }
                int schermo = NascosteAppunti.schermoDi(i.sessione);
                impostaDalPc(i.testo, eco);
                NascosteAppunti.incolla(schermo);
            }
            // altri tipi: ignorati (su questo canale non c'e' risposta)
        } catch (Throwable t) {
            System.out.println("[appunti] ERRORE nel messaggio 0x" + Integer.toHexString(tipo) + ": " + t.getMessage());
        }
    }

    private static void canale(final LocalSocket s) throws Exception {
        synchronized (serratura) {
            // Un canale appena richiuso dal PC puo' non essere ancora notato: si aspetta un poco, poi si rifiuta.
            long scade = System.currentTimeMillis() + ATTESA_FINE_MS;
            while (!fineCanale || chiuso) {
                if (chiuso) return; // servizio in chiusura (il servizio chiude il socket)
                long resto = scade - System.currentTimeMillis();
                if (resto <= 0) {
                    System.out.println("[appunti] secondo canale appunti rifiutato");
                    return;
                }
                serratura.wait(resto);
            }
            fineCanale = false;
            attivo = s;
        }
        final Regole.Eco eco = new Regole.Eco();
        // Un solo avviso in sospeso: piu' cambi ravvicinati si fondono, si legge lo stato piu' recente.
        final BlockingQueue<Object> avvisi = new ArrayBlockingQueue<>(1);
        final OutputStream out = s.getOutputStream();
        Thread invio = new Thread("yasde-appunti-invio") {
            @Override
            public void run() {
                try {
                    while (true) {
                        avvisi.take();
                        inviaCambio(out, eco);
                    }
                } catch (InterruptedException e) {
                    // fine
                } catch (Throwable t) {
                    chiudiSocket(s);
                }
            }
        };
        invio.setDaemon(true);
        NascosteAppunti.Ascolto ascolto = null;
        try {
            invio.start();
            try {
                ascolto = NascosteAppunti.ascolta(new Runnable() {
                    @Override
                    public void run() {
                        avvisi.offer(Boolean.TRUE);
                    }
                });
            } catch (IllegalStateException e) {
                // PC->telefono funziona lo stesso
                System.out.println("[appunti] ERRORE ascolto degli appunti del telefono: " + e.getMessage());
            }
            DataInputStream in = new DataInputStream(new BufferedInputStream(s.getInputStream()));
            while (true) {
                int tipo = in.read();
                if (tipo < 0) break;
                int n = in.readInt();
                if (n < 0 || n > Regole.MAX_CORPO) {
                    System.out.println("[appunti] messaggio oltre il massimo: canale chiuso");
                    break;
                }
                byte[] corpo = new byte[n];
                in.readFully(corpo);
                esegui(tipo, corpo, eco);
            }
        } catch (IOException e) {
            // socket chiuso o troncato
        } finally {
            if (ascolto != null) NascosteAppunti.smetti(ascolto);
            invio.interrupt();
            chiudiSocket(s); // sblocca anche una write ferma perche' il PC non legge
            try {
                invio.join(ATTESA_FINE_MS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            synchronized (serratura) {
                attivo = null;
                fineCanale = true;
                serratura.notifyAll();
            }
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

    /** Chiusura ordinata del servizio: chiude il canale e aspetta (poco) che i suoi thread finiscano. */
    static void chiudiTutto() {
        LocalSocket s;
        synchronized (serratura) {
            chiuso = true;
            serratura.notifyAll(); // sveglia chi aspetta: trovera' chiuso
            s = attivo;
        }
        if (s != null) chiudiSocket(s);
        synchronized (serratura) {
            long scade = System.currentTimeMillis() + ATTESA_FINE_MS;
            while (!fineCanale) {
                long resto = scade - System.currentTimeMillis();
                if (resto <= 0) break;
                try {
                    serratura.wait(resto);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }
    }
}
