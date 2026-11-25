package it.yasde;

import android.content.ClipData;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;

/**
 * Tutte le API nascoste usate dagli appunti, per riflessione, in un punto solo: il servizio di sistema
 * `clipboard` (IClipboard, AOSP) con il pacchetto `com.android.shell` e l'utente 0, l'iniezione di un tasto
 * su uno schermo virtuale e la ricerca dello schermo di una sessione video. Ogni chiamata che fallisce lancia
 * IllegalStateException con una frase chiara: il canale la scrive nel log, il servizio non cade.
 *
 * Le firme di IClipboard cambiano da una versione all'altra (userId, deviceId, attributionTag): i metodi si
 * cercano per nome e gli argomenti si riempiono per tipo (la prima String e' il pacchetto, le altre null;
 * il primo int e' l'utente, gli altri 0).
 */
final class NascosteAppunti {
    static final String PACCHETTO_SHELL = "com.android.shell";
    private static final String DESCRITTORE = "android.content.IOnPrimaryClipChangedListener";

    private static Object servizio;
    private static Class<?> interfaccia;
    private static Class<?> interfacciaAscoltatore;
    private static Object iniettore;
    private static Method iniettaMetodo;
    private static Method impostaSchermo;
    private static boolean codiceSconosciutoVisto;

    private NascosteAppunti() {}

    static IllegalStateException manca(String che, Throwable causa) {
        Throwable c = causa;
        if (c instanceof InvocationTargetException && c.getCause() != null) c = c.getCause();
        return new IllegalStateException("API di Android non disponibile (" + che + "): " + c, c);
    }

    private static synchronized void prepara() {
        if (servizio != null) return;
        try {
            Class<?> sm = Class.forName("android.os.ServiceManager");
            IBinder b = (IBinder) sm.getMethod("getService", String.class).invoke(null, "clipboard");
            if (b == null) throw new IllegalStateException("servizio clipboard assente");
            Class<?> stub = Class.forName("android.content.IClipboard$Stub");
            interfaccia = Class.forName("android.content.IClipboard");
            interfacciaAscoltatore = Class.forName(DESCRITTORE);
            servizio = stub.getMethod("asInterface", IBinder.class).invoke(null, b);
            if (servizio == null) throw new IllegalStateException("IClipboard non ottenibile");
        } catch (IllegalStateException e) {
            throw e;
        } catch (Throwable t) {
            throw manca("IClipboard", t);
        }
    }

    /** Il metodo di IClipboard con quel nome (se ce n'e' piu' d'uno, quello con piu' parametri). */
    private static Method metodo(String nome) {
        prepara();
        Method scelto = null;
        for (Method m : interfaccia.getMethods()) {
            if (!m.getName().equals(nome)) continue;
            if (scelto == null || m.getParameterTypes().length > scelto.getParameterTypes().length) scelto = m;
        }
        if (scelto == null) throw manca("IClipboard." + nome, new NoSuchMethodException(nome));
        return scelto;
    }

    private static Object[] argomenti(Method m, ClipData clip, Object ascoltatore) {
        Class<?>[] tipi = m.getParameterTypes();
        Object[] a = new Object[tipi.length];
        boolean pacchettoDato = false, utenteDato = false;
        for (int i = 0; i < tipi.length; i++) {
            Class<?> t = tipi[i];
            if (t == String.class) {
                a[i] = pacchettoDato ? null : PACCHETTO_SHELL;
                pacchettoDato = true;
            } else if (t == int.class) {
                a[i] = utenteDato ? 0 : android.os.Process.myUid() / 100000;
                utenteDato = true;
            } else if (t == ClipData.class) {
                a[i] = clip;
            } else if (t == interfacciaAscoltatore) {
                a[i] = ascoltatore;
            } else if (t == boolean.class) {
                a[i] = Boolean.FALSE;
            } else {
                throw new IllegalStateException("IClipboard." + m.getName() + ": parametro inatteso " + t);
            }
        }
        return a;
    }

    private static Object chiama(String nome, ClipData clip, Object ascoltatore) {
        Method m = metodo(nome);
        try {
            return m.invoke(servizio, argomenti(m, clip, ascoltatore));
        } catch (IllegalStateException e) {
            throw e;
        } catch (Throwable t) {
            throw manca("IClipboard." + nome, t);
        }
    }

    /** Gli appunti attuali del telefono, o null se vuoti. */
    static ClipData leggi() {
        return (ClipData) chiama("getPrimaryClip", null, null);
    }

    static void imposta(ClipData clip) {
        chiama("setPrimaryClip", clip, null);
    }

    /** Il pacchetto che ha fatto la copia attuale (IClipboard.getPrimaryClipSource), o null se non lo dice. */
    static String fonte() {
        return (String) chiama("getPrimaryClipSource", null, null);
    }

    /** Svuota gli appunti (serve alle prove sul telefono, per rimettere le cose come erano). */
    static void svuota() {
        chiama("clearPrimaryClip", null, null);
    }

    /** Un ascoltatore registrato: serve per toglierlo. */
    static final class Ascolto {
        final Object proxy;

        Ascolto(Object proxy) {
            this.proxy = proxy;
        }
    }

    /** Il codice di dispatchPrimaryClipChanged: la costante dello Stub di AOSP, 1 come riserva. */
    private static int codiceCambio() {
        try {
            java.lang.reflect.Field f = Class.forName("android.content.IOnPrimaryClipChangedListener$Stub")
                .getDeclaredField("TRANSACTION_dispatchPrimaryClipChanged");
            f.setAccessible(true);
            return f.getInt(null);
        } catch (Throwable t) {
            return IBinder.FIRST_CALL_TRANSACTION;
        }
    }

    /**
     * Chiede al sistema di chiamare `quandoCambia` (su un thread del binder: deve tornare subito) a ogni
     * cambio degli appunti. IOnPrimaryClipChangedListener non si puo' estendere a compile-time: si usa un
     * Binder con la sua unica transazione (dispatchPrimaryClipChanged = 1, senza argomenti) dietro un Proxy.
     */
    static Ascolto ascolta(final Runnable quandoCambia) {
        prepara();
        final int codiceCambio = codiceCambio();
        final Binder binder = new Binder() {
            @Override
            protected boolean onTransact(int codice, Parcel dati, Parcel risposta, int flag) throws android.os.RemoteException {
                if (codice == codiceCambio) {
                    try {
                        quandoCambia.run();
                    } catch (Throwable t) {
                        System.out.println("[appunti] errore nell'avviso di cambio: " + t);
                    }
                    if (risposta != null && (flag & IBinder.FLAG_ONEWAY) == 0) risposta.writeNoException();
                    return true;
                }
                synchronized (NascosteAppunti.class) {
                    if (!codiceSconosciutoVisto) {
                        codiceSconosciutoVisto = true;
                        System.out.println("[appunti] ascoltatore: transazione sconosciuta " + codice);
                    }
                }
                return super.onTransact(codice, dati, risposta, flag);
            }
        };
        binder.attachInterface(null, DESCRITTORE);
        Object proxy = Proxy.newProxyInstance(interfacciaAscoltatore.getClassLoader(),
            new Class<?>[] {interfacciaAscoltatore}, new InvocationHandler() {
                @Override
                public Object invoke(Object p, Method m, Object[] args) {
                    String n = m.getName();
                    if (n.equals("asBinder")) return binder;
                    if (n.equals("hashCode")) return System.identityHashCode(p);
                    if (n.equals("equals")) return p == args[0];
                    if (n.equals("toString")) return "ascoltatore appunti";
                    return null;
                }
            });
        chiama("addPrimaryClipChangedListener", null, proxy);
        return new Ascolto(proxy);
    }

    static void smetti(Ascolto a) {
        try {
            chiama("removePrimaryClipChangedListener", null, a.proxy);
        } catch (Throwable t) {
            System.out.println("[appunti] ascoltatore non tolto: " + t.getMessage());
        }
    }

    private static synchronized void preparaIniezione() {
        if (iniettaMetodo != null) return;
        Throwable ultimo = null;
        // Android 14+: InputManagerGlobal; prima: InputManager.getInstance().
        String[] classi = {"android.hardware.input.InputManagerGlobal", "android.hardware.input.InputManager"};
        for (String nome : classi) {
            try {
                Class<?> c = Class.forName(nome);
                Object ist = c.getMethod("getInstance").invoke(null);
                if (ist == null) continue;
                iniettaMetodo = c.getMethod("injectInputEvent", InputEvent.class, int.class);
                iniettore = ist;
                impostaSchermo = InputEvent.class.getMethod("setDisplayId", int.class);
                return;
            } catch (Throwable t) {
                ultimo = t;
            }
        }
        throw manca("injectInputEvent", ultimo);
    }

    private static void tasto(int schermo, int azione, int codice, int meta) {
        long ora = android.os.SystemClock.uptimeMillis();
        KeyEvent e = new KeyEvent(ora, ora, azione, codice, 0, meta, KeyCharacterMap.VIRTUAL_KEYBOARD, 0, 0,
            InputDevice.SOURCE_KEYBOARD);
        try {
            impostaSchermo.invoke(e, schermo);
            iniettaMetodo.invoke(iniettore, e, 0);
        } catch (Throwable t) {
            throw manca("injectInputEvent", t);
        }
    }

    /** Manda "incolla" (KEYCODE_PASTE, giu' e su) allo schermo indicato. */
    static void incolla(int schermo) {
        preparaIniezione();
        tasto(schermo, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_PASTE, 0);
        tasto(schermo, KeyEvent.ACTION_UP, KeyEvent.KEYCODE_PASTE, 0);
    }

    /**
     * L'id dello schermo virtuale di una sessione video. Le classi di `video` non si possono nominare:
     * per riflessione sul metodo pubblico `Video.schermoDi(int)` (-1 se la sessione non esiste). Se il metodo
     * manca (jar senza video) o la sessione non c'e', IllegalStateException con una frase chiara.
     */
    static int schermoDi(int idSessione) {
        int schermo;
        try {
            Method m = Class.forName("it.yasde.Video").getMethod("schermoDi", int.class);
            schermo = ((Number) m.invoke(null, idSessione)).intValue();
        } catch (Throwable t) {
            throw manca("Video.schermoDi", t);
        }
        if (schermo < 0) throw new IllegalStateException("sessione video " + idSessione + " inesistente");
        return schermo;
    }

    /** Prova all'avvio: elenco delle API nascoste che mancano (vuoto = tutto presente). */
    static List<String> verifica() {
        List<String> mancano = new ArrayList<>();
        try {
            for (String n : new String[] {"getPrimaryClip", "setPrimaryClip", "addPrimaryClipChangedListener",
                "removePrimaryClipChangedListener", "getPrimaryClipSource"}) {
                Method m = metodo(n);
                argomenti(m, null, null);
            }
        } catch (Throwable t) {
            mancano.add("appunti (IClipboard): " + t.getMessage());
        }
        try {
            fonte();
        } catch (Throwable t) {
            mancano.add("fonte della copia (getPrimaryClipSource): " + t.getMessage()
                + " (il controllo sui gestori di password si riduce alla marca sensibile)");
        }
        try {
            Class.forName("android.content.IOnPrimaryClipChangedListener$Stub")
                .getDeclaredField("TRANSACTION_dispatchPrimaryClipChanged");
        } catch (Throwable t) {
            mancano.add("codice della transazione dell'ascoltatore (si usa 1): " + t);
        }
        try {
            preparaIniezione(); // le stesse API che usera' l'incolla
        } catch (Throwable t) {
            mancano.add("iniezione del tasto incolla: " + t.getMessage());
        }
        return mancano;
    }
}
