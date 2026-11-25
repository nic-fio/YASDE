package it.yasde;

import android.content.Context;
import android.content.ContextWrapper;
import android.os.Handler;
import android.os.Looper;
import java.lang.reflect.InvocationTargetException;
import java.util.concurrent.CountDownLatch;

/**
 * L'unica API nascosta del pezzo: il contesto di sistema (ActivityThread), per riflessione, presentato come
 * il pacchetto della shell (il sistema controlla che il pacchetto appartenga all'uid 2000). Se manca,
 * IllegalStateException con una frase chiara: il comando risponde ERRORE, il servizio non cade.
 */
final class NascosteControlli {
    private static final String PACCHETTO_SHELL = "com.android.shell";
    private static Context contesto;
    private static Handler ciclo; // sul thread del Looper che ha creato il contesto

    private NascosteControlli() {}

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

        /** Il contesto di sistema non ha un thread principale (misurato: null) e NotificationManager ci crea un Handler. */
        @Override
        public Looper getMainLooper() {
            Handler h = ciclo;
            return h != null ? h.getLooper() : super.getMainLooper();
        }
    }

    private static IllegalStateException manca(String che, Throwable causa) {
        Throwable c = causa;
        if (c instanceof InvocationTargetException && c.getCause() != null) c = c.getCause();
        return new IllegalStateException("API di Android non disponibile (" + che + "): " + c, c);
    }

    /** Prova all'avvio: quali API nascoste mancano (elenco vuoto = tutte presenti). Non crea nulla. */
    static java.util.List<String> verifica() {
        java.util.List<String> mancanti = new java.util.ArrayList<>();
        try {
            Class<?> at = Class.forName("android.app.ActivityThread");
            for (String m : new String[] {"currentActivityThread", "systemMain", "getSystemContext"}) {
                try {
                    at.getMethod(m);
                } catch (NoSuchMethodException e) {
                    mancanti.add("ActivityThread." + m);
                }
            }
        } catch (ClassNotFoundException e) {
            mancanti.add("ActivityThread");
        }
        try {
            Context.class.getMethod("getOpPackageName");
        } catch (NoSuchMethodException e) {
            mancanti.add("Context.getOpPackageName");
        }
        return mancanti;
    }

    /** Contesto di sistema creato su un thread con il suo Looper (ActivityThread lo pretende). */
    static synchronized Context contesto() {
        if (contesto != null) return contesto;
        final Object[] esito = new Object[1];
        final CountDownLatch pronto = new CountDownLatch(1);
        Thread t = new Thread("yasde-controlli-looper") {
            @Override
            public void run() {
                try {
                    if (Looper.myLooper() == null) Looper.prepare();
                    ciclo = new Handler(Looper.myLooper());
                    Class<?> at = Class.forName("android.app.ActivityThread");
                    Object thread = at.getMethod("currentActivityThread").invoke(null);
                    if (thread == null) thread = at.getMethod("systemMain").invoke(null);
                    Context sistema = (Context) at.getMethod("getSystemContext").invoke(thread);
                    Context base = sistema;
                    esito[0] = new ContestoShell(base);
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
            if (!pronto.await(10, java.util.concurrent.TimeUnit.SECONDS)) {
                throw new IllegalStateException("API di Android non disponibile (ActivityThread): nessuna risposta in 10 s");
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw manca("ActivityThread", e);
        }
        if (!(esito[0] instanceof Context)) throw manca("ActivityThread", (Throwable) esito[0]);
        contesto = (Context) esito[0];
        return contesto;
    }

    /**
     * Un servizio di sistema del contesto. Alcuni (NotificationManager) creano un Handler sul thread che li
     * chiede per la prima volta e senza Looper falliscono con NullPointerException (misurato): il servizio si
     * chiede quindi sul thread del Looper del contesto; poi, messo in cache, si usa da qualunque thread.
     */
    static Object servizio(final String nome) {
        final Context c = contesto();
        final Object[] esito = new Object[1];
        final CountDownLatch pronto = new CountDownLatch(1);
        if (!ciclo.post(new Runnable() {
            @Override
            public void run() {
                try {
                    esito[0] = c.getSystemService(nome);
                } catch (Throwable e) {
                    esito[0] = e;
                } finally {
                    pronto.countDown();
                }
            }
        })) {
            throw new IllegalStateException("thread dei servizi di sistema non raggiungibile");
        }
        try {
            if (!pronto.await(10, java.util.concurrent.TimeUnit.SECONDS)) {
                throw new IllegalStateException("servizio di sistema non ottenuto in tempo: " + nome);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrotto: " + nome);
        }
        if (esito[0] instanceof Throwable) throw manca(nome, (Throwable) esito[0]);
        if (esito[0] == null) throw new IllegalStateException("servizio di sistema assente: " + nome);
        return esito[0];
    }
}
