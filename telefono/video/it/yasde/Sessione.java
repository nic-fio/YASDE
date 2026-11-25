package it.yasde;

import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.net.LocalSocket;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.List;
import java.util.concurrent.TimeUnit;

/** Una sessione video: schermo virtuale + codificatore + (se c'e') l'app lanciata sopra. */
final class Sessione {
    // Flag degli schermi virtuali (DisplayManager.VIRTUAL_DISPLAY_FLAG_*; alcuni sono nascosti).
    private static final int PUBBLICO = 1, SOLO_SUO = 1 << 3, SPECCHIO = 1 << 4, TOCCO = 1 << 6,
            DISTRUGGI = 1 << 8, FIDATO = 1 << 10, GRUPPO_PROPRIO = 1 << 11, SEMPRE_SBLOCCATO = 1 << 12;

    final int id;
    final String pacchetto;
    private final int bps, fps;
    private int larghezza, altezza, dpi;
    private VirtualDisplay schermo;
    private int idSchermo;
    private int flagUsati;
    private volatile Codificatore codificatore;
    private volatile Codificatore.Uscita uscita;
    private LocalSocket socketCanale;
    private final java.util.concurrent.locks.ReentrantLock scrittura = new java.util.concurrent.locks.ReentrantLock(); // una sola write alla volta sul canale video
    private ComponentName attivita;
    private volatile boolean chiusa;
    private boolean protetto; // solo il thread del controllore
    private volatile boolean transizione; // pausa, ripresa o ridimensionamento in corso
    private volatile boolean inPausa; // superficie staccata dallo schermo (docs/protocollo.md §2.2)
    private Thread guardia;

    private Sessione(int id, int l, int a, int dpi, int bps, int fps, String pacchetto) {
        this.id = id;
        this.larghezza = l;
        this.altezza = a;
        this.dpi = dpi;
        this.bps = bps;
        this.fps = fps;
        this.pacchetto = pacchetto;
    }

    static Sessione apri(int id, int l, int a, int dpi, int bps, int fps, String pacchetto) throws Exception {
        Sessione s = new Sessione(id, l, a, dpi, bps, fps, pacchetto);
        try {
            if (!pacchetto.isEmpty()) s.risolvi(); // prima di consumare codificatore e schermo
            s.codificatore = Codificatore.crea(l, a, bps, fps, s.ascoltaGuasti());
            s.creaSchermo();
            if (!pacchetto.isEmpty()) s.lancia();
        } catch (Throwable t) {
            s.rilascia(false);
            throw t;
        }
        if (!pacchetto.isEmpty()) {
            s.avviaGuardia();
            ControlloSchermi.registra(s);
        }
        return s;
    }

    /** Vero mentre pausa/ripresa/ridimensionamento sono in corso: non iniziano nuovi gesti. */
    boolean gestiBloccati() {
        return transizione;
    }

    boolean inPausa() {
        return inPausa;
    }

    boolean chiusa() {
        return chiusa;
    }

    boolean specchio() {
        return pacchetto.isEmpty();
    }

    /** Id dello schermo a cui vanno gli eventi di input (0 = schermo reale, per lo specchio). */
    int schermoInput() {
        return specchio() ? 0 : idSchermo;
    }

    synchronized int larghezza() {
        return larghezza;
    }

    synchronized int altezza() {
        return altezza;
    }

    int flagUsati() {
        return flagUsati;
    }

    String nomeCodificatore() {
        Codificatore c = codificatore;
        return c == null ? "?" : c.nome + (c.sps ? "" : " (senza SPS/PPS anteposti)");
    }

    // Insiemi di flag dal piu' ricco al piu' semplice: il primo che il sistema accetta vince.
    private int[] insiemiFlag() {
        if (specchio()) {
            return new int[] {SPECCHIO | PUBBLICO, SPECCHIO, SPECCHIO | SOLO_SUO};
        }
        int base = SOLO_SUO | TOCCO | DISTRUGGI;
        return new int[] {
            PUBBLICO | base | FIDATO | GRUPPO_PROPRIO | SEMPRE_SBLOCCATO,
            PUBBLICO | base | FIDATO | GRUPPO_PROPRIO,
            PUBBLICO | base,
            base};
    }

    private void creaSchermo() {
        DisplayManager dm = NascosteVideo.gestoreSchermi();
        String ultimo = "nessun insieme di flag provato";
        for (int flag : insiemiFlag()) {
            try {
                VirtualDisplay vd = dm.createVirtualDisplay("yasde-" + id, larghezza, altezza, dpi,
                        codificatore.superficie, flag);
                if (vd != null) {
                    schermo = vd;
                    flagUsati = flag;
                    idSchermo = vd.getDisplay().getDisplayId();
                    return;
                }
                ultimo = "null con flag 0x" + Integer.toHexString(flag);
            } catch (RuntimeException e) {
                ultimo = "flag 0x" + Integer.toHexString(flag) + ": " + e;
            }
        }
        throw new IllegalStateException("schermo virtuale non creabile (" + ultimo + ")");
    }

    /** Trova l'attivita' di avvio del pacchetto con il PackageManager (niente risoluzione implicita). */
    private void risolvi() {
        Intent in = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER).setPackage(pacchetto);
        List<ResolveInfo> r = NascosteVideo.contesto().getPackageManager()
                .queryIntentActivities(in, PackageManager.ResolveInfoFlags.of(0));
        if (r == null || r.isEmpty()) throw new IllegalStateException("l'app non ha un'attivita' di avvio");
        String classe = r.get(0).activityInfo.name;
        if (!classe.matches("[A-Za-z0-9_.$]+")) throw new IllegalStateException("nome dell'attivita' non valido");
        attivita = new ComponentName(r.get(0).activityInfo.packageName, classe);
        if (!pacchetto.equals(attivita.getPackageName())) throw new IllegalStateException("pacchetto inatteso");
    }

    /** Lancia l'attivita' sullo schermo virtuale e controlla l'esito (con tempo massimo). */
    private void lancia() throws Exception {
        String[] cmd = {"am", "start-activity", "--display", String.valueOf(idSchermo), "-n",
            attivita.getPackageName() + "/" + attivita.getClassName(), "-f", "0x18000000"};
        final Process p = new ProcessBuilder(cmd).redirectErrorStream(true).start();
        p.getOutputStream().close();
        final ByteArrayOutputStream o = new ByteArrayOutputStream();
        Thread lettore = new Thread("yasde-am") {
            @Override
            public void run() {
                byte[] buf = new byte[1024];
                int n;
                try {
                    while ((n = p.getInputStream().read(buf)) > 0) {
                        synchronized (o) {
                            if (o.size() < 8192) o.write(buf, 0, n);
                        }
                    }
                } catch (IOException e) {
                    // processo terminato
                }
            }
        };
        lettore.setDaemon(true);
        lettore.start();
        if (!p.waitFor(10, TimeUnit.SECONDS)) {
            p.destroyForcibly();
            throw new IllegalStateException("avvio dell'app: tempo scaduto");
        }
        lettore.join(1000);
        String testo;
        synchronized (o) {
            testo = o.toString("UTF-8");
        }
        if (p.exitValue() != 0 || testo.contains("Error") || testo.contains("Warning: Activity not started")) {
            String riga = testo.trim().replace('\n', ' ');
            throw new IllegalStateException("app non avviata: " + (riga.length() > 200 ? riga.substring(0, 200) : riga));
        }
    }

    /** Controlla ogni secondo che l'app sia ancora sullo schermo; se sparisce manda l'evento 1. */
    private void avviaGuardia() {
        guardia = new Thread("yasde-guardia-app") {
            @Override
            public void run() {
                controllaApp();
            }
        };
        guardia.setDaemon(true);
        guardia.start();
    }

    private void controllaApp() {
        boolean vista = false;
        int vuote = 0;
        try {
            while (!chiusa) {
                Thread.sleep(1000);
                List<Integer> t = NascosteVideo.taskDelloSchermo(idSchermo);
                if (!t.isEmpty()) {
                    vista = true;
                    vuote = 0;
                } else if (vista && ++vuote >= 2) {
                    if (chiusa) return;
                    evento(1, 0, 0);
                    return;
                }
            }
        } catch (InterruptedException e) {
            // chiusura
        } catch (RuntimeException e) {
            System.out.println("[video] controllo app non disponibile: " + e.getMessage());
        }
    }

    int idSchermoApp() {
        return idSchermo;
    }

    /** Dal controllore condiviso: l'evento 2 parte solo quando lo stato cambia. */
    void statoProtezione(boolean p) {
        if (p == protetto || chiusa) return;
        protetto = p;
        evento(2, p ? 1 : 0, 0);
    }

    private void evento(int che, int a, int b) {
        ByteBuffer bb = ByteBuffer.allocate(9);
        bb.putInt(id).put((byte) che).putShort((short) a).putShort((short) b);
        Registro.mandaEvento(0x48, bb.array());
    }

    private Codificatore.Guasto ascoltaGuasti() {
        return new Codificatore.Guasto() {
            @Override
            public void errore(Codificatore c, Throwable t) {
                guasto(c, t);
            }
        };
    }

    private void guasto(Codificatore c, Throwable t) {
        synchronized (this) {
            if (chiusa || c != codificatore) return;
        }
        System.out.println("[video] errore del codificatore (sessione " + id + "): " + t);
        evento(4, 0, 0);
    }

    /** Il PC ha aperto il canale video: parte il flusso. */
    synchronized void collega(Codificatore.Uscita u, LocalSocket socket) {
        LocalSocket vecchio = socketCanale;
        uscita = u;
        socketCanale = socket;
        if (vecchio != null && vecchio != socket) {
            // Un solo canale video per sessione. Lo shutdown sveglia una write ferma (il PC non legge).
            try {
                vecchio.shutdownOutput();
            } catch (IOException e) {
                // gia' chiuso
            }
            try {
                vecchio.shutdownInput();
            } catch (IOException e) {
                // gia' chiuso
            }
            try {
                vecchio.close();
            } catch (IOException e) {
                // gia' chiuso
            }
        }
        Codificatore c = codificatore;
        if (!chiusa && c != null) {
            c.attiva(filtrata(c));
            if (inPausa) c.sospendi(true); else chiaveInterna();
        }
    }

    /**
     * Uscita di un codificatore verso il canale: scrive una sola write alla volta e scarta i
     * pacchetti di un codificatore che non e' piu' quello della sessione (dopo un ridimensionamento
     * non devono uscire pacchetti con le misure vecchie).
     */
    private Codificatore.Uscita filtrata(final Codificatore c) {
        return new Codificatore.Uscita() {
            @Override
            public boolean pacchetto(int genere, int segni, int l, int a, long us, byte[] dati) throws IOException {
                scrittura.lock();
                try {
                    Codificatore.Uscita u = uscita;
                    // Anche se questo oggetto non e' piu' l'uscita corrente del codificatore (il thread del
                    // codificatore puo' averlo letto prima della ripresa): ha lo stato vecchio, si scarta.
                    if (c != codificatore || u == null || inPausa || c.uscitaAttuale() != this) return false;
                    return u.pacchetto(genere, segni, l, a, us, dati);
                } finally {
                    scrittura.unlock();
                }
            }
        };
    }

    /** Il canale `socket` e' finito; se nel frattempo ne e' stato aperto un altro, questo non lo tocca. */
    synchronized void scollega(LocalSocket socket) {
        if (socketCanale != socket) return;
        uscita = null;
        socketCanale = null;
        if (codificatore != null) codificatore.disattiva();
    }

    /**
     * Chiede un fotogramma chiave. A schermo fermo il codificatore non ha nulla da codificare e
     * la richiesta resta senza risposta: se in 250 ms non arriva nulla si forza un ridisegno
     * staccando e riattaccando la Surface (rimedio innescato da questo sintomo).
     */
    synchronized void chiave() {
        if (inPausa) throw new IllegalStateException("sessione in pausa");
        chiaveInterna();
    }

    private void chiaveInterna() {
        final Codificatore c = codificatore;
        if (c == null) return;
        final long prima = c.chiaviViste();
        c.chiave();
        Thread t = new Thread("yasde-ridisegno") {
            @Override
            public void run() {
                try {
                    for (int i = 0; i < 12; i++) {
                        Thread.sleep(20);
                        if (c.chiaviViste() != prima) return;
                    }
                } catch (InterruptedException e) {
                    return;
                }
                ridisegna(c);
            }
        };
        t.setDaemon(true);
        t.start();
    }

    private synchronized void ridisegna(Codificatore c) {
        if (chiusa || inPausa || c != codificatore || schermo == null) return;
        System.out.println("[video] fotogramma chiave non arrivato: ridisegno forzato");
        try {
            schermo.setSurface(null);
            schermo.setSurface(c.superficie);
        } catch (RuntimeException e) {
            System.out.println("[video] ridisegno non riuscito: " + e);
        }
    }

    /** Ridimensiona lo schermo e ricrea il codificatore; ritorna le misure effettive. */
    int[] ridimensiona(int l, int a, int d) throws IOException {
        if (inPausa) throw new IllegalStateException("sessione in pausa");
        transizione = true;
        try {
            Input.chiudiGesti(this); // un gesto aperto non sopravvive al cambio di misura
            return ridimensionaOra(l, a, d);
        } finally {
            transizione = false;
        }
    }

    private int[] ridimensionaOra(int l, int a, int d) throws IOException {
        Codificatore nuovo = Codificatore.crea(l, a, bps, fps, ascoltaGuasti());
        Codificatore vecchio;
        synchronized (this) {
            if (chiusa || inPausa) {
                nuovo.rilascia();
                throw new IllegalStateException(chiusa ? "sessione chiusa" : "sessione in pausa");
            }
            try {
                schermo.resize(l, a, d);
                schermo.setSurface(nuovo.superficie);
            } catch (RuntimeException e) {
                nuovo.rilascia();
                throw e;
            }
            vecchio = codificatore;
            codificatore = nuovo; // da ora i pacchetti del vecchio sono scartati da filtrata()
            larghezza = l;
            altezza = a;
            dpi = d;
            if (uscita != null) {
                nuovo.attiva(filtrata(nuovo));
                chiave();
            }
        }
        // Fuori dal monitor: se il PC non legge il canale il vecchio puo' metterci un po' a fermarsi.
        vecchio.disattiva();
        vecchio.rilascia();
        return new int[] {l, a};
    }

    /**
     * Pausa: stacca la superficie del codificatore dallo schermo virtuale. Schermo, app e
     * codificatore restano vivi; i pacchetti ancora in uscita si scartano.
     */
    void pausa() {
        transizione = true;
        try {
            // PRIMA di staccare la superficie: dopo, Android non consegna piu' l'input all'app e
            // l'ACTION_CANCEL andrebbe perso (alla ripresa l'app avrebbe ancora le dita giu').
            Input.chiudiGesti(this);
            synchronized (this) {
                if (chiusa) throw new IllegalStateException("sessione chiusa");
                if (inPausa) return;
                schermo.setSurface(null);
                inPausa = true;
                codificatore.sospendi(true); // niente piu' ripetizioni dell'ultimo fotogramma da buttare
            }
        } finally {
            transizione = false;
        }
        // Dopo l'OK non si scrive piu' nulla: si aspetta (poco, fuori dal monitor) l'eventuale write
        // in corso. Se il PC non legge e la write e' ferma, quel pacchetto e' "prima della pausa".
        try {
            if (scrittura.tryLock(300, TimeUnit.MILLISECONDS)) scrittura.unlock();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    /**
     * Ripresa: il codificatore riparte da CONFIGURAZIONE e fotogramma chiave (nessun pacchetto
     * vecchio passa prima), poi si riattacca la superficie e si chiede il fotogramma chiave
     * (con ridisegno forzato se l'app e' ferma).
     */
    void riprendi() {
        Input.chiudiGesti(this); // un dito messo giu' durante la pausa non e' mai arrivato all'app
        riprendiOra();
    }

    private synchronized void riprendiOra() {
        if (chiusa) throw new IllegalStateException("sessione chiusa");
        if (!inPausa) return;
        Codificatore c = codificatore;
        if (uscita != null) c.attiva(filtrata(c)); // oggetto nuovo: azzera config e attesa del fotogramma chiave
        inPausa = false;
        try {
            schermo.setSurface(c.superficie);
            c.sospendi(false);
        } catch (RuntimeException e) {
            inPausa = true;
            c.sospendi(true);
            throw e;
        }
        chiaveInterna();
    }

    /** Cio' che resta da fare dopo la chiusura sincrona: l'attesa e il rilascio, su un thread proprio. */
    private static final class Resti {
        VirtualDisplay vd;
        Codificatore c;
    }

    /**
     * Chiude la sessione. La parte che deve valere subito (sessione chiusa, nessun evento, canale
     * chiuso) e' sincrona; con togliTask i task dello schermo si tolgono e si aspetta (max 2 s) che
     * lo schermo si svuoti PRIMA di rilasciarlo, ma su un thread a parte, cosi' il canale dei comandi
     * non resta fermo. Senza togliTask non si tocca nessun task. Ritorna il thread che finisce il
     * lavoro (null se la sessione era gia' chiusa), per chi vuole aspettarlo con un limite.
     */
    Thread chiudi(final boolean togliTask) {
        final Resti r = iniziaChiusura();
        if (r == null) return null;
        Thread t = new Thread("yasde-chiusura") {
            @Override
            public void run() {
                completaChiusura(r, togliTask);
            }
        };
        t.setDaemon(true);
        t.start();
        return t;
    }

    /** Chiusura completa sul thread del chiamante (per i fallimenti in apertura). */
    private void rilascia(boolean togliTask) {
        Resti r = iniziaChiusura();
        if (r != null) completaChiusura(r, togliTask);
    }

    private Resti iniziaChiusura() {
        LocalSocket sock;
        Resti r = new Resti();
        synchronized (this) {
            if (chiusa) return null;
            chiusa = true;
            sock = socketCanale;
            r.c = codificatore;
            r.vd = schermo;
            codificatore = null;
            schermo = null;
        }
        ControlloSchermi.togli(this);
        if (guardia != null) guardia.interrupt();
        // Prima di tutto il canale: se il PC non legge, il thread del codificatore e' fermo in write.
        if (sock != null) {
            try {
                sock.shutdownOutput();
            } catch (IOException e) {
                // gia' chiuso
            }
            try {
                sock.close();
            } catch (IOException e) {
                // gia' chiuso
            }
        }
        return r;
    }

    private void completaChiusura(Resti r, boolean togliTask) {
        Input.chiudiGesti(this); // prima del rilascio dello schermo (per lo specchio e' lo schermo reale)
        // Ogni passo regge il fallimento del precedente: se togliere i task o rilasciare lo schermo
        // lancia un errore imprevisto, il codificatore (ce ne sono pochi) va rilasciato comunque.
        try {
            if (togliTask && r.vd != null && !specchio()) togliTaskDelloSchermo();
        } catch (Throwable t) {
            System.out.println("[video] rimozione dei task non riuscita: " + t);
        } finally {
            try {
                if (r.vd != null) r.vd.release();
            } catch (Throwable t) {
                System.out.println("[video] rilascio dello schermo non riuscito: " + t);
            } finally {
                if (r.c != null) r.c.rilascia();
            }
        }
    }

    /**
     * Con "Chiudi" (togli_task=1) i task rimasti sullo schermo si tolgono con removeTask e si aspetta
     * che lo schermo si svuoti prima di rilasciarlo: la rimozione e' asincrona e uno schermo rilasciato
     * a meta' puo' restare nel gestore delle finestre. Nessun Indietro: cambierebbe lo stato dell'app.
     */
    private void togliTaskDelloSchermo() {
        try {
            LiberaTask.esegui(new LiberaTask.Schermo() {
                @Override
                public List<Integer> task() {
                    return NascosteVideo.taskDelloSchermo(idSchermo);
                }

                @Override
                public boolean togli(int idTask) {
                    return NascosteVideo.togliTask(idTask);
                }

                @Override
                public void attendi() throws InterruptedException {
                    Thread.sleep(100);
                }

                @Override
                public void riga(String testo) {
                    System.out.println("[video] " + testo);
                }
            }, 15);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } catch (RuntimeException e) {
            System.out.println("[video] task non tolti: " + e.getMessage());
        }
    }

    /** Scrive un pacchetto nel formato di docs/protocollo.md §2.3 (un'unica write). */
    static void scriviPacchetto(java.io.OutputStream out, int genere, int segni, int l, int a, long us,
            byte[] dati) throws IOException {
        ByteBuffer b = ByteBuffer.allocate(18 + dati.length);
        b.put((byte) genere).put((byte) segni).putShort((short) l).putShort((short) a).putLong(us)
                .putInt(dati.length).put(dati);
        out.write(b.array());
        out.flush();
    }

}
