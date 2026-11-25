package it.yasde;

import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.os.Build;
import android.os.SystemClock;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;

import java.io.BufferedInputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.Method;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Il servizio sul telefono (docs/protocollo.md §1): lanciato con app_process come utente shell,
 * ascolta su un socket astratto e smista i canali. Ogni thread cattura tutto, così un canale
 * rotto non abbatte il servizio.
 */
public final class Servizio {
    /** Pezzi che si registrano da soli con un metodo statico registra(); assenti = si salta. */
    private static final String[] PEZZI = {"it.yasde.Video", "it.yasde.Input", "it.yasde.Audio",
        "it.yasde.Dati", "it.yasde.Archivio", "it.yasde.Cerca", "it.yasde.Appunti", "it.yasde.Pannello",
        "it.yasde.Controlli"};
    private static final String NOME_SOCKET = "yasde";
    private static final int MAX_RIGA = 64;
    private static final int TIMEOUT_RIGA_MS = 5000;

    private static final Object bloccoLog = new Object();
    private static final AtomicReference<LocalSocket> comandi = new AtomicReference<>();
    private static LocalServerSocket server;
    /** Istante (nanoTime) dell'ultima riga di log di un collegamento rifiutato; 0 = mai. */
    private static long ultimoRifiutoNs;
    private static Custode custode;
    private static volatile boolean inChiusura;
    private static final CountDownLatch chiusuraFinita = new CountDownLatch(1);

    /** Smista i messaggi del canale comandi; ogni messaggio e' un segno di vita del PC (§2.1). */
    private static final Smistatore SMISTATORE = new Smistatore(
        new Smistatore.Orologio() {
            @Override
            public long ora() {
                return SystemClock.elapsedRealtime();
            }
        },
        new Smistatore.Saluto() {
            @Override
            public byte[] rispondi(byte[] corpo) throws Exception {
                return saluto(corpo);
            }
        });

    private Servizio() {}

    /** Log su stdout. Se stdout è chiuso il PC è sparito: si termina. */
    public static void log(String riga) {
        boolean rotto;
        synchronized (bloccoLog) {
            System.out.println(riga);
            rotto = System.out.checkError();
        }
        if (rotto) {
            // fuori dal lock e su un altro thread: arresta() può a sua volta loggare
            new Thread(new Runnable() {
                @Override
                public void run() {
                    arresta();
                }
            }, "arresto-stdout").start();
        }
    }

    public static void main(String[] args) {
        try {
            File jar = trovaJar();
            custode = new Custode(jar); // non ancora avviato: se il socket non si prende, ferma() cancella il jar
            // 1) il socket: da qui nessun altro servizio e' vivo
            server = new LocalServerSocket(NOME_SOCKET);
            // 2) i resti di sessioni morte di colpo (adbd riavviato uccide servizio e guardiano insieme),
            //    PRIMA del proprio guardiano: il suo script potrebbe avere il nome di uno morto (stesso pid)
            try {
                Custode.pulisciResti(new File("/data/local/tmp"), new File("/proc"),
                    jar == null ? null : jar.getName(), android.os.Process.myPid(), 4000, new Custode.Verifica() {
                        @Override
                        public boolean eFileNostro(File f) {
                            return fileRegolareNostro(f);
                        }
                    });
            } catch (Throwable t) {
                log("resti: " + t);
            }
            // 3) il guardiano
            custode.avvia();
            Registro.impostaGuardiano(custode);
            Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
                @Override
                public void run() {
                    // SIGHUP/SIGTERM: niente System.exit qui. Se non si finisce, ci pensa il guardiano.
                    arrestaSenzaUscita(true);
                }
            }));

            List<String> mancanti = Nascoste.verifica();
            log(mancanti.isEmpty() ? "API nascoste: tutte presenti" : "API nascoste mancanti: " + mancanti);
            registraPezzi();

            guardiaStdin();
            log("PRONTO");
            while (true) {
                final LocalSocket s = server.accept();
                Thread t = new Thread(new Runnable() {
                    @Override
                    public void run() {
                        connessione(s);
                    }
                }, "canale");
                t.setDaemon(true);
                t.start();
            }
        } catch (Throwable t) {
            if (!inChiusura) log("servizio: " + t);
        }
        arresta();
    }

    /** File regolare (non collegamento simbolico) di proprieta' di shell o del nostro utente. */
    private static boolean fileRegolareNostro(File f) {
        try {
            StructStat st = Os.lstat(f.getPath());
            return OsConstants.S_ISREG(st.st_mode) && (st.st_uid == 2000 || st.st_uid == Os.getuid());
        } catch (Throwable t) {
            return false; // non leggibile = non nostro
        }
    }

    /** Il jar in esecuzione, se sta sotto /data/local/tmp (altrimenti non si cancella nulla). */
    private static File trovaJar() {
        String cp = System.getenv("CLASSPATH");
        if (cp == null) return null;
        String primo = cp.split(":")[0];
        if (primo.startsWith("/data/local/tmp/") && primo.endsWith(".jar") && !primo.contains("..")) {
            return new File(primo);
        }
        return null;
    }

    private static void registraPezzi() {
        for (String nome : PEZZI) {
            try {
                Method m = Class.forName(nome).getMethod("registra");
                m.invoke(null);
                log("registrato " + nome);
            } catch (ClassNotFoundException e) {
                log("pezzo assente: " + nome);
            } catch (Throwable t) {
                log("pezzo " + nome + " non registrato: " + t);
            }
        }
    }

    /** Se stdin della shell si chiude, il PC è sparito. */
    private static void guardiaStdin() {
        Thread t = new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    byte[] b = new byte[256];
                    while (System.in.read(b) >= 0) {
                        // il PC non scrive nulla: si scarta
                    }
                    log("stdin chiuso");
                } catch (Throwable e) {
                    log("stdin: " + e);
                }
                arresta();
            }
        }, "stdin");
        t.setDaemon(true);
        t.start();
    }

    private static void connessione(LocalSocket s) {
        try {
            // Sicurezza: prima di leggere qualunque cosa si guarda CHI si e'
            // collegato (credenziali date dal kernel, non falsificabili). Solo
            // adb (utente shell) o root: un'app del telefono viene chiusa
            // subito. Se le credenziali non si leggono, si rifiuta.
            int uid;
            try {
                uid = s.getPeerCredentials().getUid();
            } catch (Throwable t) {
                log("canale: credenziali non leggibili, rifiutato");
                chiudi(s);
                return;
            }
            if (!Pari.ammesso(uid)) {
                // L'uid non e' un dato personale ed e' cio' che serve sapere se
                // su qualche telefono adb risultasse un altro utente. Al piu' una
                // riga al secondo: una raffica di rifiuti non deve inondare il log.
                long ora = System.nanoTime();
                boolean scrivi;
                synchronized (Servizio.class) {
                    scrivi = ultimoRifiutoNs == 0 || ora - ultimoRifiutoNs >= 1000000000L;
                    if (scrivi) {
                        ultimoRifiutoNs = ora == 0 ? 1 : ora;
                    }
                }
                if (scrivi) {
                    log("canale: collegamento rifiutato (uid " + uid + " non ammesso)");
                }
                chiudi(s);
                return;
            }
            s.setSoTimeout(TIMEOUT_RIGA_MS);
            String riga = leggiRiga(s.getInputStream());
            if (riga == null) {
                log("canale: riga di apertura non valida");
                chiudi(s);
                return;
            }
            if (riga.equals("comandi")) {
                s.setSoTimeout(0);
                canaleComandi(s);
                return;
            }
            int due = riga.indexOf(':');
            String prefisso = due < 0 ? riga : riga.substring(0, due);
            String argomento = due < 0 ? "" : riga.substring(due + 1);
            GestoreCanale g = Registro.canale(prefisso);
            if (g == null) {
                log("canale sconosciuto: " + prefisso);
                chiudi(s);
                return;
            }
            s.setSoTimeout(0);
            try {
                g.gestisci(s, argomento);
            } finally {
                chiudi(s);
            }
        } catch (Throwable t) {
            log("canale: " + t);
            chiudi(s);
        }
    }

    /** Legge fino a '\n' un byte alla volta (niente buffer: dopo la riga i byte sono del protocollo). */
    private static String leggiRiga(InputStream in) throws IOException {
        StringBuilder sb = new StringBuilder();
        while (true) {
            int c = in.read();
            if (c < 0 || sb.length() >= MAX_RIGA) return null;
            if (c == '\n') return sb.toString();
            sb.append((char) c);
        }
    }

    private static void canaleComandi(final LocalSocket s) throws IOException {
        if (!comandi.compareAndSet(null, s)) {
            log("secondo canale comandi rifiutato");
            chiudi(s);
            return;
        }
        final OutputStream out = s.getOutputStream();
        final Object bloccoScrittura = new Object(); // risposte ed eventi non si mescolano
        final Registro.Eventi eventi = new Registro.Eventi() {
            @Override
            public void manda(int tipo, byte[] corpo) {
                try {
                    synchronized (bloccoScrittura) {
                        Protocollo.scrivi(out, tipo, corpo);
                    }
                } catch (IOException e) {
                    log("evento non inviato: " + e);
                }
            }
        };
        Registro.impostaEventi(eventi);
        try {
            DataInputStream in = new DataInputStream(new BufferedInputStream(s.getInputStream()));
            while (true) {
                Protocollo.Messaggio m = Protocollo.leggi(in);
                if (m == null) break;
                byte[] risposta;
                int tipoRisposta = Protocollo.OK;
                try {
                    risposta = SMISTATORE.tratta(m);
                } catch (Throwable t) {
                    tipoRisposta = Protocollo.ERRORE;
                    String msg = t.getMessage();
                    // mai il nome di una classe Java: un'eccezione senza messaggio è un corpo troppo corto
                    risposta = Protocollo.corpoErrore(msg != null ? msg : "richiesta malformata");
                }
                synchronized (bloccoScrittura) {
                    Protocollo.scrivi(out, tipoRisposta, risposta);
                }
                if (m.tipo == Protocollo.CHIUDI && tipoRisposta == Protocollo.OK) break;
            }
        } catch (Throwable t) {
            log("canale comandi: " + t);
        } finally {
            Registro.impostaEventi(null);
            comandi.compareAndSet(s, null);
            chiudi(s);
        }
        arresta(); // canale comandi chiuso: il PC non c'è più
    }

    private static byte[] saluto(byte[] corpo) throws Exception {
        int versione = new DataInputStream(new ByteArrayInputStream(corpo)).readUnsignedShort();
        if (versione != Protocollo.VERSIONE) {
            throw new IllegalStateException("versione del protocollo non supportata");
        }
        int[] schermo = Nascoste.schermoReale();
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        DataOutputStream d = new DataOutputStream(bo);
        d.writeShort(Protocollo.VERSIONE);
        d.writeShort(Build.VERSION.SDK_INT);
        d.writeShort(schermo[0]);
        d.writeShort(schermo[1]);
        d.writeShort(schermo[2]);
        Protocollo.scriviStringa(d, Build.MODEL);
        return bo.toByteArray();
    }

    private static void chiudi(LocalSocket s) {
        try {
            s.close();
        } catch (Throwable t) {
            // già chiuso
        }
    }

    /** Fa una sola volta ripristini e pulizia, poi esce. */
    private static void arresta() {
        if (arrestaSenzaUscita(false)) {
            log("servizio terminato");
            System.exit(0);
        } else {
            // la chiusura la fa un altro thread: se main() tornasse, la JVM uscirebbe a metà dei ripristini
            try {
                chiusuraFinita.await(30, TimeUnit.SECONDS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }

    /** Chiusure dei pezzi su un thread a parte, al massimo 3 s: un pezzo bloccato non ferma i ripristini. */
    private static void chiudiPezzi() {
        Thread t = new Thread(new Runnable() {
            @Override
            public void run() {
                Registro.eseguiChiusure();
            }
        }, "chiusure");
        t.setDaemon(true);
        t.start();
        try {
            t.join(3000);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        if (t.isAlive()) log("chiusure dei pezzi: tempo scaduto");
    }

    /** true se questa chiamata ha fatto la chiusura; false se la faceva già qualcun altro. */
    private static boolean arrestaSenzaUscita(boolean aspetta) {
        boolean giaInCorso;
        synchronized (Servizio.class) {
            giaInCorso = inChiusura;
            inChiusura = true;
        }
        if (giaInCorso) {
            if (aspetta) {
                // gestore di spegnimento: se la chiusura è già in corso, la aspetta
                try {
                    chiusuraFinita.await(25, TimeUnit.SECONDS);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            }
            return false;
        }
        try {
            LocalSocket c = comandi.get();
            if (c != null) chiudi(c);
            if (server != null) server.close();
        } catch (Throwable t) {
            // si prosegue: i ripristini contano più dei socket
        }
        chiudiPezzi();
        try {
            if (custode != null) custode.ferma();
        } finally {
            chiusuraFinita.countDown();
        }
        return true;
    }
}
