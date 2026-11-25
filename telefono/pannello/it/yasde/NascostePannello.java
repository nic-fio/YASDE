package it.yasde;

import android.content.Context;
import android.content.ContextWrapper;
import android.os.Binder;
import android.os.IBinder;
import android.os.Looper;

import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;

/**
 * Tutte le API nascoste di Android usate dal pannello fisico, per riflessione, in un punto solo
 * (SurfaceControl, IWindowManager, TelephonyManager: classi di AOSP, nessun produttore). Ogni chiamata
 * che fallisce lancia IllegalStateException con una frase chiara: il comando riceve ERRORE, il
 * servizio non cade. Verificato su Android 17 (livello 37), dove il token del display fisico si
 * ottiene ancora da SurfaceControl; su versioni dove sta altrove la prova all'avvio lo dice.
 */
final class NascostePannello {
    private static final String PACCHETTO_SHELL = "com.android.shell";
    /** SurfaceControl.POWER_MODE_*: stesso significato di Display.STATE_OFF / STATE_ON per il compositore. */
    private static final int POTENZA_SPENTO = 0;
    private static final int POTENZA_ACCESO = 2;
    private static final String SERVICES_JAR = "/system/framework/services.jar";

    /**
     * Solo per le prove (le chiamate vere non si possono fare): se impostato, lo stato della chiamata e' «in
     * chiamata» finche' il file esiste. Lo imposta ProvaPannello, mai il servizio.
     */
    static volatile java.io.File chiamataFinta;

    /** Solo per le prove: se il file esiste la prossima riaccensione fallisce (e il file si cancella). */
    static volatile java.io.File accensioneFalliscePrima;
    /** Solo per le prove: se il file esiste, contiene la frequenza (Hz) da riportare invece di quella vera. */
    static volatile java.io.File frequenzaFinta;

    private static Context contesto;
    private static Object gestoreTelefonia;
    private static Object servizioFinestre;
    private static Method metodoBlocco;

    private NascostePannello() {}

    /**
     * Contesto che si presenta come il pacchetto della shell: il sistema controlla che il pacchetto
     * dichiarato appartenga all'uid 2000, e il contesto di sistema direbbe "android".
     */
    private static final class ContestoShell extends ContextWrapper {
        ContestoShell(Context base) {
            super(base);
        }

        @Override
        public String getPackageName() {
            return PACCHETTO_SHELL;
        }

        // Metodo nascosto di Context (niente @Override: non e' nell'android.jar pubblico).
        public String getOpPackageName() {
            return PACCHETTO_SHELL;
        }

        @Override
        public String getAttributionTag() {
            return null;
        }
    }

    private static IllegalStateException manca(String che, Throwable causa) {
        Throwable c = causa;
        if (c instanceof InvocationTargetException && c.getCause() != null) c = c.getCause();
        return new IllegalStateException("API di Android non disponibile (" + che + "): " + c, c);
    }

    /**
     * Contesto di sistema (ActivityThread) mascherato da shell. ActivityThread vuole un Looper sul thread
     * che lo crea: lo si crea su un thread a parte che poi resta in ciclo (il thread ha il suo Looper).
     */
    private static synchronized Context contesto() {
        if (contesto != null) return contesto;
        final Object[] esito = new Object[1];
        final CountDownLatch pronto = new CountDownLatch(1);
        Thread t = new Thread("yasde-pannello-looper") {
            @Override
            public void run() {
                try {
                    if (Looper.myLooper() == null) Looper.prepare();
                    Class<?> at = Class.forName("android.app.ActivityThread");
                    Object thread = at.getMethod("currentActivityThread").invoke(null);
                    if (thread == null) thread = at.getMethod("systemMain").invoke(null);
                    esito[0] = new ContestoShell((Context) at.getMethod("getSystemContext").invoke(thread));
                } catch (Throwable e) {
                    esito[0] = e;
                } finally {
                    pronto.countDown();
                }
                if (esito[0] instanceof Context) Looper.loop();
            }
        };
        t.setDaemon(true);
        t.start();
        try {
            pronto.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw manca("ActivityThread", e);
        }
        if (!(esito[0] instanceof Context)) throw manca("ActivityThread", (Throwable) esito[0]);
        contesto = (Context) esito[0];
        return contesto;
    }

    // ---- Pannello ----

    private static Class<?> classeFisici; // la classe si tiene da parte (e la libreria si carica) una volta sola

    /**
     * Controllo SENZA caricare nulla di nativo, per la prova all'avvio: le funzioni dei token ci sono in
     * SurfaceControl, oppure esiste DisplayControl nel framework (senza inizializzarla), oppure c'e'
     * services.jar. Lancia se nessuna delle tre strade esiste.
     */
    private static void controllaDisplayFisici() throws Exception {
        Class<?> sc = Class.forName("android.view.SurfaceControl");
        try {
            sc.getMethod("getPhysicalDisplayToken", long.class);
            sc.getMethod("getPhysicalDisplayIds");
            return;
        } catch (NoSuchMethodException e) {
            // Android 14: si cerca DisplayControl
        }
        try {
            Class.forName("android.view.DisplayControl", false, NascostePannello.class.getClassLoader());
            return;
        } catch (ClassNotFoundException e) {
            // non e' nel framework
        }
        if (!new java.io.File(SERVICES_JAR).isFile()) {
            throw new IllegalStateException("ne' SurfaceControl ne' DisplayControl ne' " + SERVICES_JAR);
        }
    }

    /**
     * Classe che da' id e token dei display fisici. Android 15+ e le versioni vecchie: SurfaceControl.
     * Android 14: le due funzioni stanno in DisplayControl, nel framework o in services.jar (secondo la
     * build); la classe di services.jar si carica con UN DexClassLoader e la sua libreria nativa
     * (android_servers) con il suo classloader (Runtime.loadLibrary0): una volta sola, perche' Android non
     * lascia aprire la stessa libreria da un secondo classloader. Il caricamento nativo avviene solo alla
     * prima richiesta di spegnimento, MAI all'avvio (se il codice nativo di inizializzazione non trovasse
     * una classe su una certa build, chiuderebbe l'intero servizio: cosi' cade solo chi usa il pannello).
     * NON provato su Android 14.
     */
    private static synchronized Class<?> classeDisplayFisici() throws Exception {
        if (classeFisici != null) return classeFisici;
        Class<?> sc = Class.forName("android.view.SurfaceControl");
        try {
            sc.getMethod("getPhysicalDisplayToken", long.class);
            sc.getMethod("getPhysicalDisplayIds");
            classeFisici = sc;
            return sc;
        } catch (NoSuchMethodException e) {
            // Android 14: si cerca DisplayControl
        }
        try {
            classeFisici = Class.forName("android.view.DisplayControl");
            return classeFisici;
        } catch (ClassNotFoundException e) {
            // non e' nel framework: sta in services.jar
        }
        dalvik.system.DexClassLoader dl = new dalvik.system.DexClassLoader(SERVICES_JAR, null, null,
            ClassLoader.getSystemClassLoader());
        Class<?> c = dl.loadClass("com.android.server.display.DisplayControl");
        Method carica = Runtime.class.getDeclaredMethod("loadLibrary0", Class.class, String.class);
        carica.setAccessible(true);
        carica.invoke(Runtime.getRuntime(), c, "android_servers");
        classeFisici = c; // solo a libreria caricata: un errore prima non lascia una classe a meta'
        return c;
    }

    /**
     * Id del display fisico che corrisponde al display 0 di Android (DisplayInfo.address), non il primo
     * della lista: sui pieghevoli potrebbe essere l'altro. Se la corrispondenza non si trova, il primo.
     */
    private static long idDisplayFisico() throws Exception {
        long[] ids = (long[]) classeDisplayFisici().getMethod("getPhysicalDisplayIds").invoke(null);
        if (ids == null || ids.length == 0) throw new IllegalStateException("nessun display fisico");
        if (ids.length == 1) return ids[0];
        try {
            Object dmg = Class.forName("android.hardware.display.DisplayManagerGlobal").getMethod("getInstance").invoke(null);
            Object info = dmg.getClass().getMethod("getDisplayInfo", int.class).invoke(dmg, 0);
            Object ind = info.getClass().getField("address").get(info);
            long id = (Long) ind.getClass().getMethod("getPhysicalDisplayId").invoke(ind);
            for (long x : ids) {
                if (x == id) return x;
            }
        } catch (Throwable t) {
            System.out.println("[pannello] display 0 non associato a un display fisico (" + t + "): uso il primo");
        }
        return ids[0];
    }

    /**
     * Token del display fisico: id e token si ricalcolano a ogni chiamata (un pieghevole puo' essere aperto
     * o chiuso durante la sessione); si tiene da parte solo la classe.
     */
    private static Object token() {
        try {
            Object t = classeDisplayFisici().getMethod("getPhysicalDisplayToken", long.class).invoke(null, idDisplayFisico());
            if (t == null) throw new IllegalStateException("token del display fisico nullo");
            return t;
        } catch (IllegalStateException e) {
            throw e;
        } catch (Throwable t) {
            throw manca("token del display fisico", t);
        }
    }

    /** Prova che il token del display si ottiene (e, su Android 14, carica la libreria): da fare prima di registrare ripristini. */
    static void verificaPannello() {
        token();
    }

    /**
     * Accende o spegne il pannello fisico agendo solo sul compositore: Android continua a credere lo
     * schermo acceso (Display.STATE non cambia), quindi le app sugli schermi virtuali non si fermano.
     */
    static void impostaPannello(boolean acceso) {
        java.io.File guasto = accensioneFalliscePrima;
        if (acceso && guasto != null && guasto.exists()) {
            guasto.delete();
            throw new IllegalStateException("guasto finto della riaccensione (solo prove)");
        }
        try {
            Class.forName("android.view.SurfaceControl").getMethod("setDisplayPowerMode", IBinder.class, int.class)
                .invoke(null, (IBinder) token(), acceso ? POTENZA_ACCESO : POTENZA_SPENTO);
        } catch (IllegalStateException e) {
            throw e;
        } catch (Throwable t) {
            throw manca("SurfaceControl.setDisplayPowerMode", t);
        }
    }

    /**
     * {frequenza attiva, frequenza massima} in Hz del display fisico: vsyncRate del modo attivo e il piu' alto
     * tra i modi con la stessa risoluzione del modo attivo.
     */
    static float[] frequenze() {
        java.io.File finta = frequenzaFinta;
        if (finta != null && finta.exists()) {
            try {
                String[] p = new String(java.nio.file.Files.readAllBytes(finta.toPath()), "UTF-8").trim().split(" ");
                return new float[] {Float.parseFloat(p[0]), Float.parseFloat(p[1])};
            } catch (Exception e) {
                throw new IllegalStateException("frequenza finta illeggibile: " + e);
            }
        }
        try {
            Class<?> sc = Class.forName("android.view.SurfaceControl");
            Object d = sc.getMethod("getDynamicDisplayInfo", long.class).invoke(null, idDisplayFisico());
            int attivo = d.getClass().getField("activeDisplayModeId").getInt(d);
            Object modi = d.getClass().getField("supportedDisplayModes").get(d);
            int quanti = java.lang.reflect.Array.getLength(modi);
            int[] larg = new int[quanti];
            int[] alt = new int[quanti];
            float[] hz = new float[quanti];
            int indiceAttivo = -1;
            for (int i = 0; i < quanti; i++) {
                Object m = java.lang.reflect.Array.get(modi, i);
                larg[i] = m.getClass().getField("width").getInt(m);
                alt[i] = m.getClass().getField("height").getInt(m);
                try {
                    hz[i] = m.getClass().getField("vsyncRate").getFloat(m);
                } catch (NoSuchFieldException e) {
                    hz[i] = m.getClass().getField("refreshRate").getFloat(m); // versioni precedenti
                }
                if (m.getClass().getField("id").getInt(m) == attivo) indiceAttivo = i;
            }
            if (indiceAttivo < 0) throw new IllegalStateException("modo attivo " + attivo + " non elencato");
            float attiva = hz[indiceAttivo];
            float massima = Frequenze.massimaStessaRisoluzione(indiceAttivo, larg, alt, hz);
            return new float[] {attiva, massima};
        } catch (IllegalStateException e) {
            throw e;
        } catch (Throwable t) {
            throw manca("SurfaceControl.getDynamicDisplayInfo", t);
        }
    }

    // ---- Stato del telefono ----

    private static Object servizioPower;
    private static Method metodoInterattivo;

    /**
     * Android e' sveglio (IPowerManager.isInteractive): falso se il sistema ha messo lo schermo a dormire.
     * Chiamata diretta al servizio, senza Context ne' ActivityThread: la usa anche la riaccensione
     * d'emergenza del guardiano, che deve fare il minimo indispensabile.
     */
    static synchronized boolean interattivo() {
        try {
            if (metodoInterattivo == null) {
                Class<?> sm = Class.forName("android.os.ServiceManager");
                IBinder b = (IBinder) sm.getMethod("getService", String.class).invoke(null, Context.POWER_SERVICE);
                if (b == null) throw new IllegalStateException("servizio power assente");
                servizioPower = Class.forName("android.os.IPowerManager$Stub").getMethod("asInterface", IBinder.class)
                    .invoke(null, b);
                metodoInterattivo = Class.forName("android.os.IPowerManager").getMethod("isInteractive");
            }
            return Boolean.TRUE.equals(metodoInterattivo.invoke(servizioPower));
        } catch (IllegalStateException e) {
            metodoInterattivo = null;
            throw e;
        } catch (Throwable t) {
            metodoInterattivo = null;
            throw manca("IPowerManager.isInteractive", t);
        }
    }

    /**
     * IWindowManager.isKeyguardLocked() chiamato direttamente: KeyguardManager non si puo' creare in un
     * processo che non e' un'app.
     */
    static synchronized boolean bloccato() {
        try {
            if (metodoBlocco == null) {
                Class<?> sm = Class.forName("android.os.ServiceManager");
                IBinder b = (IBinder) sm.getMethod("getService", String.class).invoke(null, "window");
                if (b == null) throw new IllegalStateException("servizio window assente");
                servizioFinestre = Class.forName("android.view.IWindowManager$Stub")
                    .getMethod("asInterface", IBinder.class).invoke(null, b);
                metodoBlocco = Class.forName("android.view.IWindowManager").getMethod("isKeyguardLocked");
            }
            return Boolean.TRUE.equals(metodoBlocco.invoke(servizioFinestre));
        } catch (IllegalStateException e) {
            metodoBlocco = null;
            throw e;
        } catch (Throwable t) {
            metodoBlocco = null;
            throw manca("IWindowManager.isKeyguardLocked", t);
        }
    }

    /**
     * TelephonyManager.getCallState() (0 libero, 1 squilla, 2 in conversazione), sul contesto mascherato.
     * E' deprecato per le app (serve un permesso che le app non hanno) ma la shell lo puo' chiamare:
     * misurato, ritorna 0 a telefono libero.
     */
    @SuppressWarnings("deprecation")
    static synchronized boolean inChiamata() {
        java.io.File finta = chiamataFinta;
        if (finta != null) return finta.exists();
        try {
            if (gestoreTelefonia == null) {
                Constructor<android.telephony.TelephonyManager> c =
                    android.telephony.TelephonyManager.class.getDeclaredConstructor(Context.class);
                c.setAccessible(true);
                gestoreTelefonia = c.newInstance(contesto());
            }
            return ((android.telephony.TelephonyManager) gestoreTelefonia).getCallState()
                != android.telephony.TelephonyManager.CALL_STATE_IDLE;
        } catch (IllegalStateException e) {
            throw e;
        } catch (Throwable t) {
            gestoreTelefonia = null;
            throw manca("TelephonyManager.getCallState", t);
        }
    }

    // ---- Telefono sveglio (wake lock dello schermo) ----

    /** Etichetta con cui il wake lock compare in `dumpsys power`: la prova sul telefono lo riconosce da qui. */
    static final String ETICHETTA_SVEGLIO = "yasde-sveglio";

    // Un lock a parte (non il lock di classe di interattivo/bloccato/inChiamata/contesto): una presa o un rilascio
    // bloccati nel sistema non devono fermare il controllo del pannello.
    private static final Object serraturaSveglio = new Object();
    private static Binder testimoneSveglio; // il token del wake lock: se il processo muore il sistema lo rilascia da solo
    private static Object powerSveglio; // IPowerManager, tenuto separato da servizioPower per non condividere nulla

    private static Object power() throws Exception {
        if (powerSveglio == null) {
            Class<?> sm = Class.forName("android.os.ServiceManager");
            IBinder b = (IBinder) sm.getMethod("getService", String.class).invoke(null, Context.POWER_SERVICE);
            if (b == null) throw new IllegalStateException("servizio power assente");
            powerSveglio = Class.forName("android.os.IPowerManager$Stub").getMethod("asInterface", IBinder.class)
                .invoke(null, b);
        }
        return powerSveglio;
    }

    /**
     * L'unica firma di acquireWakeLock accettata: (IBinder, int, String, String, ...) con il resto fatto solo di tipi
     * che sappiamo riempire (WorkSource, String, int, IWakeLockCallback). Con piu' metodi dello stesso nome si sceglie
     * quello che rispetta la regola, mai «il primo»; se nessuno la rispetta e' NoSuchMethodException (e quindi «manca»):
     * meglio niente wake lock che argomenti indovinati.
     */
    private static Method trovaAcquire() throws NoSuchMethodException, ClassNotFoundException {
        for (Method m : Class.forName("android.os.IPowerManager").getMethods()) {
            if (!m.getName().equals("acquireWakeLock")) continue;
            Class<?>[] t = m.getParameterTypes();
            if (t.length < 4 || t[0] != IBinder.class || t[1] != int.class || t[2] != String.class || t[3] != String.class) continue;
            boolean noto = true;
            for (int i = 4; i < t.length; i++) {
                if (t[i] != String.class && t[i] != int.class && !t[i].getName().equals("android.os.WorkSource")
                    && !t[i].getName().equals("android.os.IWakeLockCallback")) {
                    noto = false;
                }
            }
            if (noto) return m;
        }
        throw new NoSuchMethodException("IPowerManager.acquireWakeLock(IBinder, int, String, String, ...) con argomenti noti");
    }

    /** releaseWakeLock(IBinder, int): firma esatta, altrimenti manca. */
    private static Method trovaRilascio() throws NoSuchMethodException, ClassNotFoundException {
        return Class.forName("android.os.IPowerManager").getMethod("releaseWakeLock", IBinder.class, int.class);
    }

    /**
     * Prende un wake lock che tiene lo schermo acceso (Android lo crede acceso, il pannello fisico resta
     * come lo ha messo YASDE): il telefono non si addormenta ne' si blocca per inattivita'. Chiamata
     * diretta a IPowerManager con il pacchetto della shell (il contesto di sistema direbbe «android», che
     * non appartiene all'uid 2000); non sveglia lo schermo (niente ACQUIRE_CAUSES_WAKEUP) e non conta come
     * attivita' dell'utente. Il token e' un Binder del nostro processo: alla morte del processo, comunque
     * avvenga, il sistema rilascia il wake lock da solo. Riprenderlo con lo stesso token non ne crea un secondo.
     * Argomenti: token, flags, etichetta, pacchetto; poi nulla per WorkSource, String e callback, 0 (display
     * predefinito) per l'eventuale int finale (vedi trovaAcquire).
     */
    @SuppressWarnings("deprecation")
    static void prendiSveglio() {
        try {
            Method m = trovaAcquire();
            Object[] a;
            Object servizio;
            synchronized (serraturaSveglio) { // solo lo stato: la chiamata al sistema sta FUORI, cosi' un rilascio non resta bloccato da una presa
                if (testimoneSveglio == null) testimoneSveglio = new Binder();
                Class<?>[] tipi = m.getParameterTypes();
                a = new Object[tipi.length];
                a[0] = testimoneSveglio;
                a[1] = Integer.valueOf(android.os.PowerManager.SCREEN_BRIGHT_WAKE_LOCK);
                a[2] = ETICHETTA_SVEGLIO;
                a[3] = PACCHETTO_SHELL;
                for (int i = 4; i < tipi.length; i++) a[i] = tipi[i] == int.class ? (Object) Integer.valueOf(0) : null;
                servizio = power();
            }
            m.invoke(servizio, a);
        } catch (IllegalStateException e) {
            throw e;
        } catch (Throwable t) {
            throw manca("IPowerManager.acquireWakeLock", t);
        }
    }

    /** Rilascia il wake lock (anche se non era preso: il sistema ignora un token sconosciuto). */
    static void rilasciaSveglio() {
        try {
            Binder token;
            Object servizio;
            synchronized (serraturaSveglio) {
                token = testimoneSveglio;
                if (token == null) return; // mai preso in questo processo
                servizio = power();
            }
            trovaRilascio().invoke(servizio, token, Integer.valueOf(0));
        } catch (IllegalStateException e) {
            throw e;
        } catch (Throwable t) {
            throw manca("IPowerManager.releaseWakeLock", t);
        }
    }

    /** Prova all'avvio: elenco delle API nascoste che mancano (vuoto = tutto presente). */
    static List<String> verifica() {
        List<String> mancano = new ArrayList<>();
        try {
            Class<?> sc = Class.forName("android.view.SurfaceControl");
            sc.getMethod("setDisplayPowerMode", IBinder.class, int.class);
            controllaDisplayFisici(); // SurfaceControl, oppure DisplayControl (Android 14): senza caricare nulla di nativo
        } catch (Throwable t) {
            mancano.add("token del display fisico e spegnimento (SurfaceControl / DisplayControl): " + t);
        }
        try {
            Class<?> at = Class.forName("android.app.ActivityThread");
            at.getMethod("currentActivityThread");
            at.getMethod("systemMain");
            at.getMethod("getSystemContext");
            android.telephony.TelephonyManager.class.getDeclaredConstructor(Context.class);
        } catch (Throwable t) {
            mancano.add("stato della chiamata (ActivityThread, TelephonyManager): " + t);
        }
        try {
            Class.forName("android.os.IPowerManager").getMethod("isInteractive");
            Class.forName("android.os.IPowerManager$Stub").getMethod("asInterface", IBinder.class);
        } catch (Throwable t) {
            mancano.add("stato dello schermo (IPowerManager): " + t);
        }
        try {
            trovaAcquire();
            trovaRilascio();
        } catch (Throwable t) {
            mancano.add("telefono sveglio (IPowerManager.acquireWakeLock / releaseWakeLock): " + t);
        }
        try {
            Class.forName("android.view.IWindowManager").getMethod("isKeyguardLocked");
            Class.forName("android.view.IWindowManager$Stub").getMethod("asInterface", IBinder.class);
        } catch (Throwable t) {
            mancano.add("blocco dello schermo (IWindowManager): " + t);
        }
        return mancano;
    }
}
