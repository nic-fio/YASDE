package it.yasde;

import android.content.Context;
import android.content.ContextWrapper;
import android.hardware.display.DisplayManager;
import android.view.InputEvent;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

/**
 * Tutte le API nascoste di Android usate da video e input, per riflessione, in un punto solo.
 * Ogni chiamata che fallisce lancia IllegalStateException con una frase chiara: il chiamante la
 * trasforma in ERRORE per il PC, il servizio non cade. Solo classi di AOSP, nessun produttore.
 */
final class NascosteVideo {
    private static final String PACCHETTO_SHELL = "com.android.shell";

    private static Context contesto;
    private static DisplayManager gestoreSchermi;
    private static Object iniettoreOggetto;
    private static Method iniettoreMetodo;
    private static Method impostaSchermo;
    private static Method impostaPulsante;
    private static Object gestoreTask;
    private static Method elencoTask;
    private static Method togliTask;

    private NascosteVideo() {}

    /**
     * Contesto che si presenta come il pacchetto della shell: il sistema controlla che il
     * pacchetto dichiarato appartenga all'uid 2000, e il contesto di sistema direbbe "android".
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
     * Contesto di sistema (ActivityThread) mascherato da shell. ActivityThread vuole un Looper
     * sul thread che lo crea e i thread del servizio non ne hanno: lo si crea su un thread
     * dedicato, che resta in ciclo per tutta la vita del processo.
     */
    static synchronized Context contesto() {
        if (contesto != null) return contesto;
        final Object[] esito = new Object[1];
        final java.util.concurrent.CountDownLatch pronto = new java.util.concurrent.CountDownLatch(1);
        Thread t = new Thread("yasde-looper") {
            @Override
            public void run() {
                try {
                    if (android.os.Looper.myLooper() == null) android.os.Looper.prepare();
                    Class<?> at = Class.forName("android.app.ActivityThread");
                    Object thread = at.getMethod("currentActivityThread").invoke(null);
                    if (thread == null) thread = at.getMethod("systemMain").invoke(null);
                    esito[0] = new ContestoShell((Context) at.getMethod("getSystemContext").invoke(thread));
                } catch (Throwable e) {
                    esito[0] = e;
                } finally {
                    pronto.countDown();
                }
                if (esito[0] instanceof Context) android.os.Looper.loop();
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

    /** DisplayManager costruito sul contesto mascherato (il costruttore e' nascosto). */
    static synchronized DisplayManager gestoreSchermi() {
        if (gestoreSchermi != null) return gestoreSchermi;
        try {
            Constructor<DisplayManager> c = DisplayManager.class.getDeclaredConstructor(Context.class);
            c.setAccessible(true);
            gestoreSchermi = c.newInstance(contesto());
            return gestoreSchermi;
        } catch (Throwable t) {
            throw manca("DisplayManager(Context)", t);
        }
    }

    private static Method cercaMetodo(Class<?> c, String nome, int parametri) throws NoSuchMethodException {
        for (Method m : c.getDeclaredMethods()) {
            if (m.getName().equals(nome) && m.getParameterTypes().length == parametri) {
                m.setAccessible(true);
                return m;
            }
        }
        throw new NoSuchMethodException(c.getName() + "." + nome + "/" + parametri);
    }

    private static synchronized void preparaIniezione() {
        if (iniettoreMetodo != null) return;
        Throwable ultimo = null;
        // Android 14+: InputManagerGlobal; prima: InputManager.getInstance().
        String[] classi = {"android.hardware.input.InputManagerGlobal", "android.hardware.input.InputManager"};
        for (String nome : classi) {
            try {
                Class<?> c = Class.forName(nome);
                Object ist = c.getMethod("getInstance").invoke(null);
                if (ist == null) continue;
                iniettoreMetodo = cercaMetodo(c, "injectInputEvent", 2);
                iniettoreOggetto = ist;
                return;
            } catch (Throwable t) {
                ultimo = t;
            }
        }
        throw manca("injectInputEvent", ultimo);
    }

    /** Inietta un evento (modo asincrono). */
    static void inietta(InputEvent e) {
        preparaIniezione();
        try {
            iniettoreMetodo.invoke(iniettoreOggetto, e, 0);
        } catch (Throwable t) {
            throw manca("injectInputEvent", t);
        }
    }

    /** Assegna l'evento a uno schermo (InputEvent.setDisplayId e' nascosto). */
    static void schermoEvento(InputEvent e, int idSchermo) {
        try {
            synchronized (NascosteVideo.class) {
                if (impostaSchermo == null) impostaSchermo = InputEvent.class.getMethod("setDisplayId", int.class);
            }
            impostaSchermo.invoke(e, idSchermo);
        } catch (Throwable t) {
            throw manca("InputEvent.setDisplayId", t);
        }
    }

    /** MotionEvent.setActionButton e' nascosto: per riflessione. false se non c'e' (si va avanti senza BUTTON_*). */
    static boolean impostaPulsanteAzione(android.view.MotionEvent e, int pulsante) {
        try {
            synchronized (NascosteVideo.class) {
                if (impostaPulsante == null) impostaPulsante = android.view.MotionEvent.class.getMethod("setActionButton", int.class);
            }
            impostaPulsante.invoke(e, pulsante);
            return true;
        } catch (Throwable t) {
            return false;
        }
    }

    private static synchronized void preparaTask() {
        if (elencoTask != null) return;
        try {
            Class<?> atm = Class.forName("android.app.ActivityTaskManager");
            Object svc = atm.getMethod("getService").invoke(null);
            Class<?> i = Class.forName("android.app.IActivityTaskManager");
            Method el;
            try {
                el = cercaMetodo(i, "getTasks", 4);
            } catch (NoSuchMethodException e) {
                el = cercaMetodo(i, "getTasks", 3);
            }
            togliTask = cercaMetodo(i, "removeTask", 1);
            gestoreTask = svc;
            elencoTask = el;
        } catch (Throwable t) {
            throw manca("IActivityTaskManager", t);
        }
    }

    /** Gli id dei task che stanno sullo schermo dato. */
    static List<Integer> taskDelloSchermo(int idSchermo) {
        preparaTask();
        try {
            Object r;
            if (elencoTask.getParameterTypes().length == 4) {
                r = elencoTask.invoke(gestoreTask, 100, false, false, idSchermo);
            } else {
                r = elencoTask.invoke(gestoreTask, 100, false, false);
            }
            List<Integer> out = new ArrayList<>();
            if (!(r instanceof List)) return out;
            for (Object info : (List<?>) r) {
                Field fs = info.getClass().getField("displayId");
                if (fs.getInt(info) != idSchermo) continue;
                out.add(info.getClass().getField("taskId").getInt(info));
            }
            return out;
        } catch (Throwable t) {
            throw manca("getTasks", t);
        }
    }

    static boolean togliTask(int idTask) {
        preparaTask();
        try {
            return Boolean.TRUE.equals(togliTask.invoke(gestoreTask, idTask));
        } catch (Throwable t) {
            throw manca("removeTask", t);
        }
    }

    private static synchronized Object servizioFinestre() {
        try {
            Class<?> sm = Class.forName("android.os.ServiceManager");
            Object b = sm.getMethod("getService", String.class).invoke(null, "window");
            if (b == null) throw new IllegalStateException("servizio window assente");
            return b;
        } catch (Throwable t) {
            throw manca("ServiceManager.getService(window)", t);
        }
    }

    /** Il testo che stamperebbe `dumpsys window windows`, senza lanciare processi. */
    static String dumpFinestre() {
        final android.os.IBinder b = (android.os.IBinder) servizioFinestre();
        try {
            android.os.ParcelFileDescriptor[] p = android.os.ParcelFileDescriptor.createPipe();
            final java.io.InputStream in = new android.os.ParcelFileDescriptor.AutoCloseInputStream(p[0]);
            final java.io.ByteArrayOutputStream o = new java.io.ByteArrayOutputStream();
            Thread lettore = new Thread("yasde-dump") {
                @Override
                public void run() {
                    byte[] buf = new byte[16384];
                    int n;
                    try {
                        while ((n = in.read(buf)) > 0) {
                            if (o.size() < (8 << 20)) o.write(buf, 0, n);
                        }
                    } catch (java.io.IOException e) {
                        // fine del dump
                    }
                }
            };
            lettore.setDaemon(true);
            lettore.start();
            try {
                try {
                    b.dump(p[1].getFileDescriptor(), new String[] {"windows"});
                } finally {
                    p[1].close();
                }
                lettore.join(3000);
                return o.toString("UTF-8");
            } finally {
                in.close();
            }
        } catch (Throwable t) {
            throw manca("dump del servizio window", t);
        }
    }

    /** Prova all'avvio: elenco delle API nascoste che mancano (vuoto = tutto presente). */
    static List<String> verifica() {
        List<String> mancano = new ArrayList<>();
        try {
            gestoreSchermi();
        } catch (RuntimeException e) {
            mancano.add("schermi virtuali: " + e.getMessage());
        }
        try {
            preparaIniezione();
        } catch (RuntimeException e) {
            mancano.add("iniezione input: " + e.getMessage());
        }
        try {
            InputEvent.class.getMethod("setDisplayId", int.class);
        } catch (Throwable t) {
            mancano.add("InputEvent.setDisplayId");
        }
        try {
            servizioFinestre();
        } catch (RuntimeException e) {
            mancano.add("finestre protette (evento 2): " + e.getMessage());
        }
        try {
            preparaTask();
        } catch (RuntimeException e) {
            mancano.add("elenco task (chiusura app, tolta dai recenti): " + e.getMessage());
        }
        return mancano;
    }
}
