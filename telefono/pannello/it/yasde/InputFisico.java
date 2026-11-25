package it.yasde;

import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructPollfd;

import java.io.FileDescriptor;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * Accorgersi che l'utente tocca lo schermo REALE o preme un tasto fisico.
 *
 * Perche' cosi' (misurato sul telefono): gli eventi dei dispositivi di input del kernel (evdev) sono
 * l'unica fonte che vede solo l'hardware vero. Gli input iniettati dal PC sugli schermi virtuali
 * passano dall'InputDispatcher di Android e non toccano mai /dev/input, quindi non possono contare
 * come «utente che tocca il telefono». Le alternative provate non servono: il monitor di input di
 * `InputManager` richiede una sottoclasse di `InputEventReceiver`, che non esiste nell'android.jar
 * pubblico con cui si compila, e `PowerManager.getLastUserActivityTime` ritorna sempre -1 alla shell.
 *
 * I dispositivi si scelgono per capacita' (non per nome): touchscreen = ha ABS_MT_POSITION_X e
 * INPUT_PROP_DIRECT; tasti = ha uno dei tasti fisici di uso comune. L'elenco lo da `getevent -pl`
 * (comando di AOSP, eseguito una volta sola). La shell legge /dev/input perche' sta nel gruppo `input`.
 * Si ascolta solo mentre il pannello e' spento: a pannello acceso non serve e non costa nulla.
 */
final class InputFisico {
    /** Chiamato da un thread di lettura quando c'e' attivita' dell'utente (o quando la lettura si rompe). */
    interface Ascoltatore {
        void attivita(String che);
    }

    private static final String[] TASTI = {
        "KEY_POWER", "KEY_VOLUMEUP", "KEY_VOLUMEDOWN", "KEY_HOME", "KEY_BACK", "KEY_MENU", "KEY_APPSELECT",
    };
    private static final int ATTESA_MS = 500;

    private static List<String> dispositivi; // trovati una volta sola

    /**
     * Solo per le prove: file con altri percorsi da ascoltare (uno per riga), letto a ogni avvia(). Serve a
     * ProvaPannello per aggiungere una tastiera virtuale (uhid): i tocchi veri non si possono iniettare.
     */
    static volatile java.io.File fileExtra;

    private final List<FileDescriptor> aperti = new ArrayList<>();
    private volatile boolean fermo;

    private InputFisico() {}

    /** Percorsi dei dispositivi da ascoltare: IOException se non si trova nemmeno un touchscreen. */
    private static synchronized List<String> trovaDispositivi() throws IOException {
        if (dispositivi != null) return dispositivi;
        String o = ShellPannello.esegui(10, "getevent", "-pl");
        List<String> trovati = new ArrayList<>();
        boolean touchscreen = false;
        for (String blocco : o.split("(?m)^add device ")) {
            int fine = blocco.indexOf('\n');
            if (fine < 0) continue;
            int due = blocco.indexOf(':');
            if (due < 0 || due > fine) continue;
            String percorso = blocco.substring(due + 1, fine).trim();
            if (!percorso.matches("/dev/input/event[0-9]{1,4}")) continue;
            boolean tocco = blocco.contains("ABS_MT_POSITION_X") && blocco.contains("INPUT_PROP_DIRECT");
            boolean tasti = false;
            for (String t : TASTI) {
                if (blocco.contains(t)) tasti = true;
            }
            if (tocco) touchscreen = true;
            if (tocco || tasti) trovati.add(percorso);
        }
        if (!touchscreen) {
            String causa = o.contains("add device") ? "nessun touchscreen tra i dispositivi di input"
                : "getevent non elenca dispositivi (" + o.trim().replace('\n', ' ') + ")";
            throw new IOException(causa.length() > 300 ? causa.substring(0, 300) : causa);
        }
        dispositivi = trovati;
        return trovati;
    }

    /**
     * Prova all'avvio: null se la rilevazione dell'utente e' disponibile (dispositivi trovati e apribili),
     * altrimenti la causa vera. Senza, il pannello non si spegne.
     */
    static String verifica() {
        try {
            for (String p : trovaDispositivi()) {
                Os.close(Os.open(p, OsConstants.O_RDONLY | OsConstants.O_CLOEXEC, 0));
            }
            return null;
        } catch (IOException e) {
            return e.getMessage();
        } catch (ErrnoException e) {
            return "dispositivo di input non apribile: " + e.getMessage();
        }
    }

    /** Apre i dispositivi e comincia ad ascoltare. Se uno non si apre, niente resta aperto. */
    static InputFisico avvia(final Ascoltatore a) throws IOException {
        List<String> percorsi = new ArrayList<>(trovaDispositivi());
        java.io.File extra = fileExtra;
        if (extra != null && extra.exists()) {
            for (String r : new String(java.nio.file.Files.readAllBytes(extra.toPath()), "UTF-8").split("\n")) {
                if (r.matches("/dev/input/event[0-9]{1,4}")) percorsi.add(r);
            }
        }
        InputFisico f = new InputFisico();
        try {
            for (String p : percorsi) {
                f.aperti.add(Os.open(p, OsConstants.O_RDONLY | OsConstants.O_CLOEXEC, 0));
            }
        } catch (ErrnoException e) {
            f.chiudiTutto();
            throw new IOException("dispositivo di input non apribile: " + e.getMessage());
        }
        for (int i = 0; i < f.aperti.size(); i++) {
            final FileDescriptor fd = f.aperti.get(i);
            final String nome = percorsi.get(i);
            Thread t = new Thread("yasde-pannello-input") {
                @Override
                public void run() {
                    f.leggi(fd, nome, a);
                }
            };
            t.setDaemon(true);
            t.start();
        }
        return f;
    }

    /**
     * Chiede ai thread di fermarsi (escono entro ATTESA_MS e chiudono da soli il proprio dispositivo).
     * Non aspetta: puo' essere chiamato dallo stesso thread di lettura, o con una serratura in mano
     * che un thread di lettura sta aspettando. Idempotente.
     */
    void ferma() {
        fermo = true;
    }

    private void chiudiTutto() {
        for (FileDescriptor fd : aperti) {
            chiudi(fd);
        }
        aperti.clear();
    }

    private static void chiudi(FileDescriptor fd) {
        try {
            Os.close(fd);
        } catch (ErrnoException e) {
            // gia' chiuso
        }
    }

    /** Un thread per dispositivo: attende con scadenza (cosi' si puo' fermare) e guarda ogni evento. */
    private void leggi(FileDescriptor fd, String nome, Ascoltatore a) {
        // struct input_event del kernel: timeval (2 long), type u16, code u16, value s32
        int dim = android.os.Process.is64Bit() ? 24 : 16;
        byte[] buf = new byte[dim * 32];
        StructPollfd[] p = {new StructPollfd()};
        p[0].fd = fd;
        p[0].events = (short) OsConstants.POLLIN;
        try {
            while (!fermo) {
                p[0].revents = 0;
                if (Os.poll(p, ATTESA_MS) == 0) continue;
                if ((p[0].revents & OsConstants.POLLIN) == 0) {
                    a.attivita("lettura di " + nome + " interrotta (poll " + p[0].revents + ")");
                    return;
                }
                int n = Os.read(fd, buf, 0, buf.length);
                if (n <= 0) {
                    a.attivita("lettura di " + nome + " finita");
                    return;
                }
                if (EventiEvdev.attivita(buf, n, dim)) a.attivita("evento di " + nome);
            }
        } catch (Throwable e) {
            // qualunque guasto (anche RuntimeException o Error): non si resta al buio, si tratta come attivita'
            // dell'utente; il thread non muore in silenzio
            if (!fermo) a.attivita("lettura di " + nome + " fallita: " + e);
        } finally {
            chiudi(fd);
        }
    }
}
