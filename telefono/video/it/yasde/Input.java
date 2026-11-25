package it.yasde;

import android.net.LocalSocket;

import java.io.DataInputStream;
import java.io.IOException;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;

/** Canale `input` (docs/protocollo.md §2.4): un thread legge, un solo thread inietta. */
public final class Input {
    private static final int TOCCO = 0x50, ROTELLA = 0x51, TASTO = 0x52, TESTO = 0x53, INDIETRO = 0x54, PIZZICO = 0x55;
    private static final int MAX_CORPO = 1 << 20;
    private static final int MAX_CODA = 256;

    private static BlockingQueue<Runnable> coda;
    private static final Iniettore iniettore = new Iniettore();

    private Input() {}

    public static void registra() {
        Registro.registraCanale("input", new GestoreCanale() {
            @Override
            public void gestisci(LocalSocket s, String argomento) throws Exception {
                canale(s, argomento);
            }
        });
    }

    /** Coda limitata: se e' piena chi legge il canale aspetta, quindi nessun evento va perso. */
    private static synchronized BlockingQueue<Runnable> coda() {
        if (coda == null) {
            final BlockingQueue<Runnable> q = new ArrayBlockingQueue<>(MAX_CODA);
            Thread t = new Thread("yasde-iniezione") {
                @Override
                public void run() {
                    while (true) {
                        try {
                            // Il pizzico ha le sue scadenze (movimenti da ~8 ms, chiusura a 400 ms): si
                            // aspetta il prossimo messaggio al massimo fino a quella scadenza.
                            long attesa = iniettore.attesaPizzico();
                            Runnable r = attesa < 0 ? q.take() : q.poll(attesa, java.util.concurrent.TimeUnit.MILLISECONDS);
                            if (r != null) r.run();
                            iniettore.scadenzePizzico();
                        } catch (InterruptedException e) {
                            chiudiTutto();
                            return;
                        } catch (Throwable e) {
                            System.out.println("[input] errore nell'iniezione: " + e);
                            chiudiTutto(); // nessun dito resta giu'
                        }
                    }
                }
            };
            t.setDaemon(true);
            t.start();
            coda = q;
        }
        return coda;
    }

    private static void chiudiTutto() {
        try {
            iniettore.chiudiTutto();
        } catch (Throwable e) {
            System.out.println("[input] gesti non chiusi: " + e);
        }
    }

    /**
     * Annulla ogni gesto aperto (dito e pizzico) di una sessione, sul thread di iniezione, e aspetta
     * al massimo 150 ms che sia fatto. Da chiamare PRIMA di pausa, cambio di misura e rilascio dello
     * schermo, senza tenere lock.
     */
    static void chiudiGesti(final Sessione s) {
        BlockingQueue<Runnable> q;
        synchronized (Input.class) {
            q = coda;
        }
        if (q == null) return;
        try {
            // 1) il thread che legge il canale input puo' essere in ritardo sul PC (che manda l'ANNULLA prima
            // della pausa): gli si lascia, per poco, il tempo di mettere in coda cio' che e' gia' arrivato.
            attendiLettore(50);
            // 2) barriera sulla coda di iniezione: tutto cio' che c'era prima e' stato iniettato; poi si chiude
            // cio' che risulta ancora aperto di questa sessione.
            Barriera.esegui(q, new Runnable() {
                @Override
                public void run() {
                    iniettore.chiudiGestiDi(s);
                }
            }, 50, 150);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private static volatile DataInputStream lettore; // il canale input aperto
    private static volatile boolean inMessaggio; // il lettore ha letto un tipo e non ha ancora messo in coda il messaggio

    /** Aspetta (al massimo `ms`) che il lettore abbia svuotato quanto e' gia' arrivato sul canale. */
    private static void attendiLettore(long ms) throws InterruptedException {
        long fine = System.nanoTime() + ms * 1000000L;
        while (System.nanoTime() < fine) {
            DataInputStream in = lettore;
            if (in == null) return;
            int disponibili;
            try {
                disponibili = in.available();
            } catch (IOException e) {
                return;
            }
            if (disponibili == 0 && !inMessaggio) return;
            Thread.sleep(2);
        }
    }

    private static void canale(LocalSocket s, String argomento) throws Exception {
        DataInputStream in = new DataInputStream(s.getInputStream()) /* senza buffer: available() non si blocca dietro una read ferma */;
        BlockingQueue<Runnable> q = coda();
        lettore = in;
        try {
            while (true) {
                int tipo;
                try {
                    tipo = in.readUnsignedByte();
                } catch (java.io.EOFException e) {
                    return;
                }
                inMessaggio = true; // da qui fino alla messa in coda il messaggio e' a meta'
                int n = in.readInt();
                if (n < 0 || n > MAX_CORPO) throw new IOException("messaggio input troppo grande");
                final byte[] corpo = new byte[n];
                in.readFully(corpo);
                final int t = tipo;
                q.put(new Runnable() {
                    @Override
                    public void run() {
                        try {
                            esegui(t, new DataInputStream(new java.io.ByteArrayInputStream(corpo)));
                        } catch (Throwable e) {
                            System.out.println("[input] evento " + t + " non iniettato: " + e);
                        }
                    }
                });
                inMessaggio = false;
            }
        } finally {
            inMessaggio = false;
            lettore = null;
            // Fine del canale (PC sparito, errore): nessun dito resta giu', di nessuna sessione.
            try {
                q.offer(new Runnable() {
                    @Override
                    public void run() {
                        chiudiTutto();
                    }
                }, 50, java.util.concurrent.TimeUnit.MILLISECONDS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }

    /** Esegue un evento sul thread di iniezione; eventi su misure vecchie si scartano. */
    private static void esegui(int tipo, DataInputStream in) throws IOException {
        if (tipo < TOCCO || tipo > PIZZICO) return; // tipo sconosciuto: ignorato (nessuna risposta su questo canale)
        Sessione s = Video.sessione(in.readInt());
        if (s == null) return;
        switch (tipo) {
            case TOCCO: {
                int l = in.readUnsignedShort(), a = in.readUnsignedShort();
                int azione = in.readUnsignedByte();
                int x = in.readUnsignedShort(), y = in.readUnsignedShort();
                // Pulsanti (docs/protocollo.md 2.4): conta solo alla pressione. Se manca (PC vecchio) vale sinistro.
                int pulsanti = in.available() > 0 ? in.readUnsignedByte() : 1;
                if (l != s.larghezza() || a != s.altezza()) return;
                if (azione == 0 && s.gestiBloccati()) return; // pausa o cambio di misura in corso
                iniettore.tocco(s, azione, x, y, pulsanti);
                break;
            }
            case ROTELLA: {
                int l = in.readUnsignedShort(), a = in.readUnsignedShort();
                int x = in.readUnsignedShort(), y = in.readUnsignedShort();
                int o = in.readShort(), v = in.readShort();
                if (l != s.larghezza() || a != s.altezza()) return;
                iniettore.rotella(s, x, y, o, v);
                break;
            }
            case TASTO: {
                int azione = in.readUnsignedByte();
                int codice = in.readInt(), mod = in.readInt();
                iniettore.tasto(s, azione, codice, mod);
                break;
            }
            case PIZZICO: {
                int l = in.readUnsignedShort(), a = in.readUnsignedShort();
                int fase = in.readUnsignedByte();
                int x = in.readUnsignedShort(), y = in.readUnsignedShort();
                int passo = in.readShort();
                if (l != s.larghezza() || a != s.altezza() || fase > 3 || x >= l || y >= a) return;
                if (s.inPausa() || s.gestiBloccati()) return; // in pausa il pizzico si scarta
                iniettore.pizzico(s, fase, x, y, passo);
                break;
            }
            case TESTO:
                iniettore.testo(s, Binario.stringa(in));
                break;
            default:
                iniettore.indietro(s);
                break;
        }
    }
}
