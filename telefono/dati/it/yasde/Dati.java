package it.yasde;

import java.io.DataInputStream;
import java.util.List;

/**
 * Dati del telefono, parte 1: comandi 0x70-0x73, 0x76-0x77, 0x7b-0x7d, 0x90-0x94 ed eventi 0x7e-0x7f (docs/protocollo.md §2.6).
 * Elenco app, icone, stato e notifiche; dopo la prima richiesta di STATO o NOTIFICHE un solo
 * thread controlla (lo stato ogni 2 s, le notifiche ogni 5 s) e avvisa solo se qualcosa e' cambiato (Android non offre a un
 * processo shell un ascoltatore puro: registerListener e' riservato al sistema).
 */
public final class Dati {
    private static final int APP_ELENCO = 0x70, APP_ICONA = 0x71, STATO = 0x72, NOTIFICHE = 0x73;
    private static final int MEMORIA_DETTAGLIO = 0x76, CALENDARIO = 0x77;
    private static final int CALENDARIO_INTERVALLO = 0x7b, SVEGLIA = 0x7c, BATTERIA = 0x7d;
    private static final int POSIZIONE = 0x90;
    private static final int CHIAMATE = 0x91, RETE = 0x92, MEDIA = 0x93, MEDIA_COMANDO = 0x94;
    private static final int STATO_CAMBIATO = 0x7e, NOTIFICHE_CAMBIATE = 0x7f;
    /** Intervalli del controllo dei cambi (protocollo §2.6): lo stato ogni 2 s, le notifiche ogni 5 s (leggerle costa). */
    private static final long INTERVALLO_MS = 2000;
    private static final long INTERVALLO_NOTIFICHE_MS = 5000;

    private static final Object lock = new Object();
    private static boolean statoAttivo, notificheAttive, fermo;
    private static Thread filo;
    private static CampiDati.Stato ultimoStato;
    private static String ultimaFirma;
    private static String ultimoErroreStato = "", ultimoErroreNotifiche = "";
    private static long ultimoControlloNotifiche = Long.MIN_VALUE;   // System.nanoTime(): non torna indietro se l'ora cambia

    private Dati() {}

    public static void registra() {
        List<String> mancano = NascosteDati.verifica();
        if (mancano.isEmpty()) {
            System.out.println("[dati] API di Android necessarie: tutte presenti");
        } else {
            for (String m : mancano) System.out.println("[dati] API MANCANTE: " + m);
        }
        Registro.registraComandi(0x70, 0x73, new GestoreComandi() {
            @Override
            public byte[] gestisci(int tipo, DataInputStream corpo) throws Exception {
                return comando(tipo, corpo);
            }
        });
        // 0x74-0x75 sono di `archivio`: due intervalli separati.
        Registro.registraComandi(0x76, 0x77, new GestoreComandi() {
            @Override
            public byte[] gestisci(int tipo, DataInputStream corpo) throws Exception {
                return comando(tipo, corpo);
            }
        });
        Registro.registraComandi(0x7b, 0x7d, new GestoreComandi() {
            @Override
            public byte[] gestisci(int tipo, DataInputStream corpo) throws Exception {
                return comando(tipo, corpo);
            }
        });
        // 0x80-0x8f sono di `controlli`: qui solo 0x90 (POSIZIONE, §2.11).
        Registro.registraComandi(0x90, 0x9f, new GestoreComandi() {
            @Override
            public byte[] gestisci(int tipo, DataInputStream corpo) throws Exception {
                return comando(tipo, corpo);
            }
        });
        Registro.registraChiusura(new Runnable() {
            @Override
            public void run() {
                NascosteDati.fermaPosizione();
                ferma();
            }
        });
    }

    static byte[] comando(int tipo, DataInputStream in) throws Exception {
        switch (tipo) {
            case APP_ELENCO:
                return CampiDati.codificaElenco(NascosteDati.elencoApp());
            case APP_ICONA: {
                String pacchetto = CampiDati.leggiStringa(in);
                int lato = in.readUnsignedShort();
                if (lato < 16 || lato > 512) throw new IllegalArgumentException("lato dell'icona non valido (16-512)");
                if (!pacchetto.matches("[A-Za-z0-9_.]{1,255}")) throw new IllegalArgumentException("nome pacchetto non valido");
                return NascosteDati.iconaPng(pacchetto, lato);
            }
            case STATO: {
                CampiDati.Stato s = NascosteDati.stato();
                synchronized (lock) {
                    ultimoStato = s;
                    statoAttivo = true;
                    avviaFilo();
                }
                return CampiDati.codificaStato(s);
            }
            case NOTIFICHE: {
                List<CampiDati.Notifica> n = NascosteDati.notifiche();
                byte[] r = CampiDati.codificaNotifiche(n);
                synchronized (lock) {
                    ultimaFirma = CampiDati.firmaNotifiche(n);
                    notificheAttive = true;
                    avviaFilo();
                }
                return r;
            }
            case MEMORIA_DETTAGLIO: {
                long[] m = NascosteDati.memoriaDettaglio();
                return CampiDati.codificaMemoriaDettaglio(m[0], m[1], m[2], m[3]);
            }
            case CALENDARIO: {
                int massimo = CampiDati.controllaMassimoEventi(in.readUnsignedByte());
                return CampiDati.codificaCalendario(NascosteDati.calendario(massimo), massimo);
            }
            case CALENDARIO_INTERVALLO: {
                long da = in.readLong(), a = in.readLong();
                int massimo = in.readUnsignedShort();
                CampiDati.controllaIntervallo(da, a, massimo);
                return CampiDati.codificaCalendarioIntervallo(NascosteDati.calendarioIntervallo(da, a, massimo), massimo);
            }
            case SVEGLIA:
                return CampiDati.codificaSveglia(NascosteDati.sveglia());
            case BATTERIA:
                return CampiDati.codificaBatteria(NascosteDati.batteriaDettaglio());
            case POSIZIONE: {
                int permessa = in.readUnsignedByte();
                if (permessa > 1) throw new IllegalArgumentException("richiesta permessa non valida (0/1)");
                return LogicaPosizione.codifica(NascosteDati.posizione(permessa == 1));
            }
            case CHIAMATE: {
                int massimo = CampiTelefonia.controllaMassimoChiamate(in.readUnsignedByte());
                senzaCoda(in);
                return NascosteTelefonia.chiamate(massimo);
            }
            case RETE:
                senzaCoda(in);
                return NascosteTelefonia.rete();
            case MEDIA:
                senzaCoda(in);
                return NascosteTelefonia.media();
            case MEDIA_COMANDO: {
                int comando = CampiTelefonia.controllaComando(in.readUnsignedByte());
                senzaCoda(in);
                NascosteTelefonia.comandoMedia(comando);
                return new byte[0];
            }
            default:
                throw new IllegalArgumentException("tipo sconosciuto");
        }
    }

    /** Il corpo e' finito: byte in piu' = richiesta non valida (nulla toccato). */
    private static void senzaCoda(DataInputStream in) throws java.io.IOException {
        if (in.available() != 0) throw new IllegalArgumentException("corpo della richiesta troppo lungo");
    }

    /** Da chiamare con `lock` preso. */
    private static void avviaFilo() {
        if (filo != null || fermo) return;
        filo = new Thread("yasde-dati-cambi") {
            @Override
            public void run() {
                controlla();
            }
        };
        filo.setDaemon(true);
        filo.start();
    }

    private static void ferma() {
        synchronized (lock) {
            fermo = true;
            lock.notifyAll();
        }
    }

    private static void controlla() {
        while (true) {
            synchronized (lock) {
                if (fermo) return;
                try {
                    lock.wait(INTERVALLO_MS);
                } catch (InterruptedException e) {
                    return;
                }
                if (fermo) return;
            }
            // Stato e notifiche in due blocchi separati: un guasto di uno non spegne gli avvisi dell'altro.
            try {
                controllaStato();
                ultimoErroreStato = "";
            } catch (Throwable t) {
                String m = String.valueOf(t);
                if (!m.equals(ultimoErroreStato)) {
                    ultimoErroreStato = m;
                    System.out.println("[dati] controllo dello stato fallito: " + m);
                }
            }
            long ora = System.nanoTime();
            if (ultimoControlloNotifiche == Long.MIN_VALUE || (ora - ultimoControlloNotifiche) / 1000000L >= INTERVALLO_NOTIFICHE_MS) {
                ultimoControlloNotifiche = ora;
                try {
                    controllaNotifiche();
                    ultimoErroreNotifiche = "";
                } catch (Throwable t) {
                    String m = String.valueOf(t);
                    if (!m.equals(ultimoErroreNotifiche)) {
                        ultimoErroreNotifiche = m;
                        System.out.println("[dati] controllo delle notifiche fallito: " + m);
                    }
                }
            }
        }
    }

    private static void controllaStato() throws Exception {
        CampiDati.Stato prima;
        synchronized (lock) {
            if (!statoAttivo) return;
            prima = ultimoStato;
        }
        CampiDati.Stato dopo = NascosteDati.stato();
        if (!CampiDati.statoCambiato(prima, dopo)) return;
        synchronized (lock) {
            ultimoStato = dopo;
        }
        Registro.mandaEvento(STATO_CAMBIATO, CampiDati.codificaStato(dopo));
    }

    private static void controllaNotifiche() throws Exception {
        String prima;
        synchronized (lock) {
            if (!notificheAttive) return;
            prima = ultimaFirma;
        }
        String dopo = CampiDati.firmaNotifiche(NascosteDati.notifiche());
        if (dopo.equals(prima)) return;
        synchronized (lock) {
            ultimaFirma = dopo;
        }
        Registro.mandaEvento(NOTIFICHE_CAMBIATE, new byte[0]);
    }
}
