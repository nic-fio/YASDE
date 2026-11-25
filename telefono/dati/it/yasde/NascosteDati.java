package it.yasde;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.drawable.Drawable;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.BatteryManager;
import android.os.IBinder;
import android.util.DisplayMetrics;

import java.io.ByteArrayOutputStream;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CountDownLatch;

/**
 * Tutto cio' che i dati del telefono chiedono ad Android: elenco app, icone, stato e notifiche
 * (queste ultime dal dump del servizio, analizzato da AnalisiNotifiche).
 * Le API nascoste si usano per riflessione, qui e solo qui. Ogni errore lancia
 * IllegalStateException con una frase chiara: il chiamante la trasforma in ERRORE per il PC.
 * Solo classi di AOSP, nessun produttore.
 */
final class NascosteDati {
    private static final String PACCHETTO_SHELL = "com.android.shell";

    private static Context contesto;

    private NascosteDati() {}

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
        final CountDownLatch pronto = new CountDownLatch(1);
        Thread t = new Thread("yasde-dati-looper") {
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

    // ---- App e icone ----

    private static Intent intentAvvio() {
        return new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
    }

    /** Le app con un'attivita' di avvio, una voce per pacchetto, ordinate per nome. */
    static List<CampiDati.App> elencoApp() {
        try {
            PackageManager pm = contesto().getPackageManager();
            List<ResolveInfo> trovate = pm.queryIntentActivities(intentAvvio(), PackageManager.ResolveInfoFlags.of(0));
            List<CampiDati.App> out = new ArrayList<>();
            Set<String> visti = new HashSet<>();
            for (ResolveInfo ri : trovate) {
                ActivityInfo ai = ri.activityInfo;
                if (ai == null || ai.applicationInfo == null || !visti.add(ai.packageName)) continue;
                String nome;
                try {
                    CharSequence e = ri.loadLabel(pm);
                    nome = e == null ? "" : e.toString().trim();
                } catch (Throwable t) {
                    nome = "";
                }
                if (nome.isEmpty()) nome = ai.packageName;
                boolean sistema = (ai.applicationInfo.flags & (ApplicationInfo.FLAG_SYSTEM | ApplicationInfo.FLAG_UPDATED_SYSTEM_APP)) != 0;
                boolean verticale = CampiDati.soloVerticale(ai.screenOrientation);
                out.add(new CampiDati.App(ai.packageName, nome, sistema, verticale));
            }
            CampiDati.ordinaApp(out);
            return out;
        } catch (RuntimeException e) {
            throw manca("PackageManager", e);
        }
    }

    /** L'icona di avvio dell'app in un PNG lato x lato (le icone adattive sono disegnate con la loro maschera). */
    static byte[] iconaPng(String pacchetto, int lato) {
        Bitmap bm = null;
        try {
            PackageManager pm = contesto().getPackageManager();
            Intent i = intentAvvio().setPackage(pacchetto);
            List<ResolveInfo> trovate = pm.queryIntentActivities(i, PackageManager.ResolveInfoFlags.of(0));
            if (trovate.isEmpty()) throw new IllegalArgumentException("app senza attivita' di avvio: " + pacchetto);
            ResolveInfo ri = trovate.get(0);
            Drawable d = null;
            int risorsa = ri.activityInfo.getIconResource();
            if (risorsa != 0) {
                try {
                    // La densita' piu' alta, cosi' le icone grandi non sono sgranate.
                    d = pm.getResourcesForApplication(ri.activityInfo.applicationInfo)
                            .getDrawableForDensity(risorsa, DisplayMetrics.DENSITY_XXXHIGH, null);
                } catch (Throwable t) {
                    d = null;
                }
            }
            if (d == null) d = ri.loadIcon(pm);
            if (d == null) throw new IllegalStateException("l'app non ha un'icona");
            bm = Bitmap.createBitmap(lato, lato, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(bm);
            d.setBounds(0, 0, lato, lato);
            d.draw(c);
            ByteArrayOutputStream o = new ByteArrayOutputStream();
            if (!bm.compress(Bitmap.CompressFormat.PNG, 100, o)) throw new IllegalStateException("compressione PNG fallita");
            return CampiDati.controllaDimensione(o.toByteArray());
        } catch (OutOfMemoryError e) {
            throw new IllegalStateException("memoria insufficiente per disegnare l'icona");
        } catch (RuntimeException e) {
            if (e instanceof IllegalArgumentException || e instanceof IllegalStateException) throw e;
            throw manca("icona dell'app", e);
        } finally {
            if (bm != null) bm.recycle();
        }
    }

    // ---- Stato ----

    private static <T> T servizio(String nome, Class<T> classe) {
        Object s = contesto().getSystemService(nome);
        if (!classe.isInstance(s)) throw new IllegalStateException("servizio di sistema assente: " + nome);
        return classe.cast(s);
    }

    private static ConnectivityManager gestoreRete;
    private static android.telephony.TelephonyManager gestoreTelefonia;

    /**
     * ConnectivityManager costruito sul contesto mascherato da shell: quello che dà getSystemService
     * userebbe il pacchetto "android" e getNetworkCapabilities fallirebbe con "Package android does
     * not belong to 2000". Il costruttore e' nascosto.
     */
    private static synchronized ConnectivityManager gestoreRete() {
        if (gestoreRete != null) return gestoreRete;
        try {
            Class<?> i = Class.forName("android.net.IConnectivityManager");
            Class<?> sm = Class.forName("android.os.ServiceManager");
            IBinder b = (IBinder) sm.getMethod("getService", String.class).invoke(null, Context.CONNECTIVITY_SERVICE);
            if (b == null) throw new IllegalStateException("servizio connectivity assente");
            Object svc = Class.forName("android.net.IConnectivityManager$Stub").getMethod("asInterface", IBinder.class).invoke(null, b);
            java.lang.reflect.Constructor<ConnectivityManager> c = ConnectivityManager.class.getDeclaredConstructor(Context.class, i);
            c.setAccessible(true);
            gestoreRete = c.newInstance(contesto(), svc);
            return gestoreRete;
        } catch (Throwable t) {
            throw manca("ConnectivityManager(Context, IConnectivityManager)", t);
        }
    }

    /** TelephonyManager sul contesto mascherato (stesso motivo; costruttore nascosto). */
    private static synchronized android.telephony.TelephonyManager gestoreTelefonia() {
        if (gestoreTelefonia != null) return gestoreTelefonia;
        try {
            java.lang.reflect.Constructor<android.telephony.TelephonyManager> c =
                    android.telephony.TelephonyManager.class.getDeclaredConstructor(Context.class);
            c.setAccessible(true);
            gestoreTelefonia = c.newInstance(contesto());
            return gestoreTelefonia;
        } catch (Throwable t) {
            throw manca("TelephonyManager(Context)", t);
        }
    }

    private static int batteria() {
        BatteryManager bm = servizio(Context.BATTERY_SERVICE, BatteryManager.class);
        int livello = bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY);
        if (livello < 0 || livello > 100) throw new IllegalStateException("livello della batteria non leggibile");
        return livello;
    }

    private static boolean inCarica() {
        BatteryManager bm = servizio(Context.BATTERY_SERVICE, BatteryManager.class);
        int st = bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_STATUS);
        if (st == BatteryManager.BATTERY_STATUS_CHARGING) return true;
        if (st == BatteryManager.BATTERY_STATUS_FULL) return bm.isCharging();
        return false;
    }

    /** La rete di uscita: l'attiva, oppure (se e' una VPN) la prima sottostante con Internet. */
    @SuppressWarnings("deprecation")
    private static NetworkCapabilities reteDiUscita(ConnectivityManager cm) {
        Network attiva = cm.getActiveNetwork();
        if (attiva == null) return null;
        NetworkCapabilities nc = cm.getNetworkCapabilities(attiva);
        if (nc == null || !nc.hasTransport(NetworkCapabilities.TRANSPORT_VPN)) return nc;
        for (Network n : cm.getAllNetworks()) {
            NetworkCapabilities c = cm.getNetworkCapabilities(n);
            if (c != null && !c.hasTransport(NetworkCapabilities.TRANSPORT_VPN)
                    && c.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)) {
                return c;
            }
        }
        return nc;
    }

    private static int segnaleCellulare() {
        try {
            android.telephony.TelephonyManager tm = gestoreTelefonia();
            android.telephony.SignalStrength ss = tm.getSignalStrength();
            return ss == null ? 0 : ss.getLevel();
        } catch (Throwable t) {
            return 0;
        }
    }

    @SuppressWarnings("deprecation")
    private static int segnaleWifi(NetworkCapabilities nc) {
        int rssi = nc.getSignalStrength();
        if (rssi != NetworkCapabilities.SIGNAL_STRENGTH_UNSPECIFIED) return CampiDati.livelloWifi(rssi);
        try {
            android.net.wifi.WifiManager wm = servizio(Context.WIFI_SERVICE, android.net.wifi.WifiManager.class);
            android.net.wifi.WifiInfo wi = wm.getConnectionInfo();
            if (wi != null && wi.getRssi() > -127) return CampiDati.livelloWifi(wi.getRssi());
        } catch (Throwable t) {
            // segnale non leggibile: resta 0
        }
        return 0;
    }

    private static Object servizioFinestre;
    private static Method metodoBlocco;

    /**
     * IWindowManager.isKeyguardLocked() chiamato direttamente: KeyguardManager non si puo' creare
     * in un processo che non e' un'app (WindowManagerGlobal vuole la memoria condivisa delle app).
     */
    private static synchronized boolean schermoBloccato() {
        try {
            if (metodoBlocco == null) {
                Class<?> sm = Class.forName("android.os.ServiceManager");
                IBinder b = (IBinder) sm.getMethod("getService", String.class).invoke(null, "window");
                if (b == null) throw new IllegalStateException("servizio window assente");
                servizioFinestre = Class.forName("android.view.IWindowManager$Stub").getMethod("asInterface", IBinder.class).invoke(null, b);
                metodoBlocco = Class.forName("android.view.IWindowManager").getMethod("isKeyguardLocked");
            }
            return Boolean.TRUE.equals(metodoBlocco.invoke(servizioFinestre));
        } catch (Throwable t) {
            metodoBlocco = null;
            throw manca("IWindowManager.isKeyguardLocked", t);
        }
    }

    /** Batteria, carica, rete, segnale e blocco, con le API pubbliche di sistema. */
    static CampiDati.Stato stato() {
        try {
            int livello = batteria();
            boolean carica = inCarica();
            ConnectivityManager cm = gestoreRete();
            NetworkCapabilities nc = reteDiUscita(cm);
            int rete = 0, segnale = 0;
            if (nc != null) {
                if (nc.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                    rete = 1;
                    segnale = segnaleWifi(nc);
                } else if (nc.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) {
                    rete = 2;
                    segnale = segnaleCellulare();
                }
            }
            boolean bloccato = schermoBloccato();
            return new CampiDati.Stato(livello, carica ? 1 : 0, rete, segnale, bloccato ? 1 : 0);
        } catch (IllegalStateException e) {
            throw e;
        } catch (RuntimeException e) {
            throw manca("stato del telefono", e);
        }
    }

    // ---- Notifiche ----

    private static IBinder servizioNotifiche() {
        try {
            Class<?> sm = Class.forName("android.os.ServiceManager");
            IBinder b = (IBinder) sm.getMethod("getService", String.class).invoke(null, "notification");
            if (b == null) throw new IllegalStateException("servizio notification assente");
            return b;
        } catch (Throwable t) {
            throw manca("ServiceManager.getService(notification)", t);
        }
    }

    /** Massimo del dump letto: oltre, e' incompleto e non si usa. */
    private static final int MAX_DUMP = 8 << 20;
    private static final long SCADENZA_DUMP_MS = 3000;

    /**
     * Il testo che stamperebbe `dumpsys notification --noredact`, senza lanciare processi.
     * INotificationManager.getActiveNotifications* risponde "Access denied, requires
     * ACCESS_NOTIFICATIONS" all'utente shell (misurato): resta il dump, che la shell puo' leggere.
     * Il dump parte con dumpAsync e si legge con una scadenza di 3 s; un dump troppo grande o
     * non finito entro la scadenza e' ERRORE, mai un elenco parziale. Nota: le altre chiamate ai
     * servizi di sistema (Binder sincrono) non hanno scadenza propria.
     */
    private static String dumpNotifiche() {
        try {
            return dumpServizio(servizioNotifiche(), new String[] {"--noredact"}, "notifiche");
        } catch (IllegalStateException t) {
            if (t.getMessage() != null && t.getMessage().startsWith("dump ")) throw t;
            throw new IllegalStateException("Il sistema non permette di leggere le notifiche senza privilegi: " + t.getMessage(), t);
        }
    }

    /** Dump di un servizio di sistema con scadenza e limite di dimensione (vedi dumpNotifiche). */
    private static String dumpServizio(final IBinder b, String[] argomenti, String cosa) {
        android.os.ParcelFileDescriptor[] p = null;
        java.io.InputStream in = null;
        try {
            p = android.os.ParcelFileDescriptor.createPipe();
            final java.io.InputStream flusso = new android.os.ParcelFileDescriptor.AutoCloseInputStream(p[0]);
            in = flusso;
            final ByteArrayOutputStream o = new ByteArrayOutputStream();
            final boolean[] troppo = new boolean[1];
            Thread lettore = new Thread("yasde-dati-dump") {
                @Override
                public void run() {
                    byte[] buf = new byte[16384];
                    int n;
                    try {
                        while ((n = flusso.read(buf)) > 0) {
                            if (o.size() + n > MAX_DUMP) {
                                troppo[0] = true;
                            } else if (!troppo[0]) {
                                o.write(buf, 0, n);
                            }
                        }
                    } catch (java.io.IOException e) {
                        // chiuso dalla scadenza: il chiamante lo sa
                    }
                }
            };
            lettore.setDaemon(true);
            lettore.start();
            b.dumpAsync(p[1].getFileDescriptor(), argomenti);
            // Chiusa subito la nostra copia del lato di scrittura: la fine del dump arriva quando
            // il servizio chiude la sua.
            p[1].close();
            p[1] = null;
            lettore.join(SCADENZA_DUMP_MS);
            if (lettore.isAlive()) {
                flusso.close();
                throw new IllegalStateException("dump delle " + cosa + " incompleto (scaduto)");
            }
            if (troppo[0]) throw new IllegalStateException("dump delle " + cosa + " incompleto (troppo grande)");
            return o.toString("UTF-8");
        } catch (IllegalStateException t) {
            throw t;
        } catch (Throwable t) {
            throw new IllegalStateException("lettura del dump delle " + cosa + " non riuscita: " + t, t);
        } finally {
            try {
                if (p != null && p[1] != null) p[1].close();
                if (in != null) in.close();
            } catch (java.io.IOException e) {
                // niente da fare
            }
        }
    }

    /**
     * Le notifiche attive dell'utente, come le elenca il dump del servizio. Se il formato del
     * dump non e' riconosciuto: ERRORE (mai un elenco vuoto finto).
     */
    static List<CampiDati.Notifica> notifiche() {
        AnalisiNotifiche.Esito e = AnalisiNotifiche.analizza(dumpNotifiche());
        if (!e.sezioneTrovata) {
            throw new IllegalStateException("Formato del dump delle notifiche non riconosciuto: sezione dell'elenco assente");
        }
        if (e.nonRiconosciuti > 0) {
            throw new IllegalStateException("Formato del dump delle notifiche non riconosciuto: " + e.nonRiconosciuti + " voci illeggibili");
        }
        return e.notifiche;
    }

    // ---- Memoria in dettaglio ----

    private static android.app.usage.StorageStatsManager gestoreStatistiche;

    /** StorageStatsManager sul contesto mascherato da shell (costruttore nascosto, servizio "storagestats"). */
    private static synchronized android.app.usage.StorageStatsManager gestoreStatistiche() {
        if (gestoreStatistiche != null) return gestoreStatistiche;
        try {
            Class<?> i = Class.forName("android.app.usage.IStorageStatsManager");
            Class<?> sm = Class.forName("android.os.ServiceManager");
            IBinder b = (IBinder) sm.getMethod("getService", String.class).invoke(null, "storagestats");
            if (b == null) throw new IllegalStateException("servizio storagestats assente");
            Object svc = Class.forName("android.app.usage.IStorageStatsManager$Stub").getMethod("asInterface", IBinder.class).invoke(null, b);
            java.lang.reflect.Constructor<android.app.usage.StorageStatsManager> c =
                    android.app.usage.StorageStatsManager.class.getDeclaredConstructor(Context.class, i);
            c.setAccessible(true);
            gestoreStatistiche = c.newInstance(contesto(), svc);
            return gestoreStatistiche;
        } catch (Throwable t) {
            throw manca("StorageStatsManager(Context, IStorageStatsManager)", t);
        }
    }

    private static final long SCADENZA_MEMORIA_MS = 3000;
    private static final LetturaUnica<long[]> letturaMemoria = new LetturaUnica<>("yasde-dati-memoria",
            "lettura precedente della memoria ancora in corso", "la memoria non ha risposto in tempo");

    /**
     * Byte di immagini, video, audio e app (codice + dati; i dati comprendono gia' la cache) dell'utente.
     * Un valore che il sistema non da' vale CampiDati.NON_LEGGIBILE; se manca il servizio intero: errore.
     * La lettura gira su un thread con scadenza di 3 s e una sola alla volta: se scade o la precedente e'
     * ancora in corso, tutti i valori sono non leggibili.
     */
    static long[] memoriaDettaglio() {
        final android.app.usage.StorageStatsManager m = gestoreStatistiche();
        final java.util.UUID u = android.os.storage.StorageManager.UUID_DEFAULT;
        final android.os.UserHandle utente = android.os.Process.myUserHandle();
        try {
            return letturaMemoria.esegui(new LetturaUnica.Lavoro<long[]>() {
                @Override
                public long[] esegui() {
                    long[] r = {CampiDati.NON_LEGGIBILE, CampiDati.NON_LEGGIBILE, CampiDati.NON_LEGGIBILE, CampiDati.NON_LEGGIBILE};
                    try {
                        android.app.usage.ExternalStorageStats es = m.queryExternalStatsForUser(u, utente);
                        r[0] = es.getImageBytes();
                        r[1] = es.getVideoBytes();
                        r[2] = es.getAudioBytes();
                    } catch (Throwable t) {
                        // restano non leggibili
                    }
                    try {
                        android.app.usage.StorageStats s = m.queryStatsForUser(u, utente);
                        r[3] = s.getAppBytes() + s.getDataBytes();   // i dati comprendono gia' la cache
                    } catch (Throwable t) {
                        // resta non leggibile
                    }
                    return r;
                }
            }, SCADENZA_MEMORIA_MS);
        } catch (LetturaUnica.Occupata | LetturaUnica.Scaduta e) {
            return new long[] {CampiDati.NON_LEGGIBILE, CampiDati.NON_LEGGIBILE, CampiDati.NON_LEGGIBILE, CampiDati.NON_LEGGIBILE};
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("lettura della memoria interrotta");
        }
    }

    // ---- Calendario ----

    private static final String PROVIDER_CALENDARIO = "com.android.calendar";
    private static final long SCADENZA_CALENDARIO_MS = 3000;

    private static Method metodoIn(Class<?> c, String nome, int parametri) {
        for (Method m : c.getMethods()) {
            if (m.getName().equals(nome) && m.getParameterTypes().length == parametri) return m;
        }
        return null;
    }

    /**
     * Eventi da adesso a 30 giorni, per inizio. Si legge come fa il comando `content`: il provider
     * si prende con IActivityManager.getContentProviderExternal e si chiama come shell (il
     * ContentResolver del contesto di sistema fallirebbe con "calling package android does not
     * match uid 2000"). La lettura gira su un thread con scadenza di 3 s: se il provider non
     * risponde, errore. Sono dati personali: nessun titolo finisce in un log o in un messaggio
     * d'errore (delle eccezioni si usa solo il nome della classe).
     */
    private static final LetturaUnica<CampiDati.Calendario> letturaCalendario = new LetturaUnica<>("yasde-dati-calendario",
            "lettura precedente del calendario ancora in corso", "Il calendario non ha risposto in tempo");

    /** Eventi da adesso a 30 giorni (CALENDARIO, 0x77): non vengono filtrati ne' segnalati come troncati. */
    static List<CampiDati.Evento> calendario(int massimo) {
        long ora = System.currentTimeMillis();
        return leggiIntervallo(ora, ora + CampiDati.FINESTRA_CALENDARIO_MS, massimo, false).eventi;
    }

    /**
     * Eventi che si sovrappongono a [da, a) (CALENDARIO_INTERVALLO, 0x7b), per inizio, dai soli
     * calendari visibili, al massimo `massimo`; `troncato` dice se ce n'erano di piu'. Gli argomenti
     * sono gia' controllati da CampiDati.controllaIntervallo. Ogni istante e' in millisecondi UTC dal
     * 1970: nessuna conversione di fuso, quindi il cambio dell'ora non entra mai nei calcoli.
     */
    static CampiDati.Calendario calendarioIntervallo(long da, long a, int massimo) {
        CampiDati.Calendario c = leggiIntervallo(da, a, massimo, true);
        return new CampiDati.Calendario(c.eventi, c.troncato, pacchettoCalendario());
    }

    private static CampiDati.Calendario leggiIntervallo(final long da, final long a, final int massimo, final boolean sovrapposti) {
        try {
            return letturaCalendario.esegui(new LetturaUnica.Lavoro<CampiDati.Calendario>() {
                @Override
                public CampiDati.Calendario esegui() throws Exception {
                    List<CampiDati.Evento> out = new ArrayList<>();
                    leggiCalendario(da, a, massimo, sovrapposti, out);
                    boolean troncato = out.size() > massimo;
                    while (out.size() > massimo) out.remove(out.size() - 1);
                    return new CampiDati.Calendario(out, troncato, "");
                }
            }, SCADENZA_CALENDARIO_MS);
        } catch (LetturaUnica.Fallita f) {
            throw erroreCalendario(f.getCause());
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("lettura del calendario interrotta");
        }
    }

    /** Pacchetto dell'app Calendario predefinita (ACTION_MAIN + CATEGORY_APP_CALENDAR); vuoto se nessuna. */
    private static String pacchettoCalendario() {
        try {
            Intent i = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_APP_CALENDAR);
            List<ResolveInfo> l = contesto().getPackageManager().queryIntentActivities(i, PackageManager.ResolveInfoFlags.of(0));
            if (l.isEmpty() || l.get(0).activityInfo == null) return "";
            return l.get(0).activityInfo.packageName;
        } catch (Throwable t) {
            return "";
        }
    }

    /** API mancante e permesso negato sono cose diverse; nessun testo dell'eccezione (potrebbe contenere dati). */
    private static IllegalStateException erroreCalendario(Throwable causa) {
        Throwable c = causa instanceof InvocationTargetException && causa.getCause() != null ? causa.getCause() : causa;
        if (c instanceof NoSuchMethodException || c instanceof NoSuchFieldException || c instanceof ClassNotFoundException
                || c instanceof LinkageError || c instanceof IllegalAccessException) {
            return new IllegalStateException("API di Android non disponibile (calendario): " + c.getClass().getSimpleName());
        }
        if (c instanceof SecurityException) {
            return new IllegalStateException("Il calendario non e' leggibile senza privilegi (permesso negato)");
        }
        return new IllegalStateException("Lettura del calendario non riuscita (" + c.getClass().getSimpleName() + ")");
    }

    private static void leggiCalendario(long da, long a, int massimo, boolean sovrapposti, List<CampiDati.Evento> out) throws Exception {
        final IBinder token = new android.os.Binder();
        Object am = Class.forName("android.app.ActivityManager").getMethod("getService").invoke(null);
        Class<?> iam = Class.forName("android.app.IActivityManager");
        Method prendi = metodoIn(iam, "getContentProviderExternal", 4);
        Method rilascia = metodoIn(iam, "removeContentProviderExternal", 2);
        if (prendi == null || rilascia == null) throw new NoSuchMethodException("getContentProviderExternal");
        Object holder = prendi.invoke(am, PROVIDER_CALENDARIO, android.os.Process.myUid() / 100000 /* utente del processo (PER_USER_RANGE), lo stesso di myUserHandle() */, token, "yasde");
        if (holder == null) throw new IllegalStateException("provider del calendario assente");
        try {
            Object provider = holder.getClass().getField("provider").get(holder);
            if (provider == null) throw new IllegalStateException("provider del calendario assente");
            Object fonte = new android.content.AttributionSource.Builder(android.os.Process.myUid()).setPackageName(PACCHETTO_SHELL).build();
            Method q = metodoIn(provider.getClass(), "query", 5);
            if (q == null) throw new NoSuchMethodException("IContentProvider.query");
            android.net.Uri.Builder b = android.provider.CalendarContract.Instances.CONTENT_URI.buildUpon();
            android.content.ContentUris.appendId(b, da);
            android.content.ContentUris.appendId(b, a);
            String[] proiezione = {android.provider.CalendarContract.Instances.TITLE, android.provider.CalendarContract.Instances.BEGIN,
                    android.provider.CalendarContract.Instances.END, android.provider.CalendarContract.Instances.ALL_DAY};
            android.os.Bundle args = new android.os.Bundle();
            // solo i calendari visibili: quelli che mostra l'app Calendario
            args.putString(android.content.ContentResolver.QUERY_ARG_SQL_SELECTION, android.provider.CalendarContract.Instances.VISIBLE + "=1");
            args.putString(android.content.ContentResolver.QUERY_ARG_SQL_SORT_ORDER, android.provider.CalendarContract.Instances.BEGIN + " ASC");
            android.database.Cursor c = (android.database.Cursor) q.invoke(provider, fonte, b.build(), proiezione, args, null);
            if (c == null) throw new IllegalStateException("cursore del calendario nullo");
            try {
                // Si legge un evento in piu' del massimo per sapere se c'era altro (troncato).
                while (out.size() <= massimo && c.moveToNext()) {
                    long inizio = c.getLong(1), fine = c.getLong(2);
                    // Il provider include anche gli eventi che finiscono esattamente a `da`.
                    if (sovrapposti && !CampiDati.sovrappone(inizio, fine, da, a)) continue;
                    out.add(new CampiDati.Evento(c.getString(0), inizio, fine, c.getInt(3) != 0));
                }
            } finally {
                c.close();
            }
        } finally {
            // Sempre rilasciato, anche su errore.
            try {
                rilascia.invoke(am, PROVIDER_CALENDARIO, token);
            } catch (Throwable e) {
                // niente altro da fare
            }
        }
    }

    // ---- Sveglia ----

    /** Prossima sveglia: AlarmManager.getNextAlarmClock() (API pubblica, nessun permesso). */
    static CampiDati.Sveglia sveglia() {
        try {
            android.app.AlarmManager am = servizio(Context.ALARM_SERVICE, android.app.AlarmManager.class);
            android.app.AlarmManager.AlarmClockInfo info = am.getNextAlarmClock();
            if (info == null) return new CampiDati.Sveglia(false, 0, "");
            String pacchetto = "";
            try {
                android.app.PendingIntent pi = info.getShowIntent();
                if (pi != null && pi.getCreatorPackage() != null) pacchetto = pi.getCreatorPackage();
            } catch (Throwable t) {
                // pacchetto non noto: vuoto
            }
            return new CampiDati.Sveglia(true, info.getTriggerTime(), pacchetto);
        } catch (IllegalStateException e) {
            throw e;
        } catch (RuntimeException e) {
            throw manca("AlarmManager.getNextAlarmClock", e);
        }
    }

    // ---- Batteria in dettaglio ----

    /**
     * Livello, stato, alimentazione, temperatura, salute e tensione dall'intento persistente
     * ACTION_BATTERY_CHANGED; se un processo shell non puo' registrare il ricevitore (l'AMS non
     * riconosce il chiamante) si ripiega sul dump del servizio "battery", analizzato da
     * AnalisiBatteria. Minuti alla carica completa da BatteryManager.computeChargeTimeRemaining
     * (API pubblica); autonomia stimata: -1, perche' Android non ha un'API pubblica per leggerla
     * e non si stima.
     */
    private static boolean ripiegoBatteriaDetto;

    static synchronized CampiDati.Batteria batteriaDettaglio() {
        CampiDati.Batteria b = null;
        try {
            Intent i = contesto().registerReceiver(null, new android.content.IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            if (i != null) {
                int plugged = i.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0);
                b = CampiDati.Batteria.daAndroid(i.getIntExtra(BatteryManager.EXTRA_LEVEL, -1), i.getIntExtra(BatteryManager.EXTRA_SCALE, -1),
                        i.getIntExtra(BatteryManager.EXTRA_STATUS, 1), plugged, i.getIntExtra(BatteryManager.EXTRA_HEALTH, 1),
                        i.hasExtra(BatteryManager.EXTRA_TEMPERATURE) ? i.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) : Integer.MIN_VALUE,
                        i.getIntExtra(BatteryManager.EXTRA_VOLTAGE, 0), minutiCarica());
            }
        } catch (Throwable t) {
            b = null;
        }
        if (b == null) {
            if (!ripiegoBatteriaDetto) {
                ripiegoBatteriaDetto = true;
                System.out.println("[dati] batteria: ripiego sul dump");   // senza dati, una volta sola
            }
            String dump = dumpServizio((IBinder) servizioPerNome("battery"), new String[0], "batteria");
            b = AnalisiBatteria.analizza(dump, minutiCarica());
            if (b == null) throw new IllegalStateException("Formato del dump della batteria non riconosciuto");
        }
        return b;
    }

    private static int minutiCarica() {
        try {
            long ms = servizio(Context.BATTERY_SERVICE, BatteryManager.class).computeChargeTimeRemaining();
            return ms < 0 ? -1 : (int) Math.min(ms / 60000L, 100000L);
        } catch (Throwable t) {
            return -1;
        }
    }

    private static Object servizioPerNome(String nome) {
        try {
            Class<?> sm = Class.forName("android.os.ServiceManager");
            Object b = sm.getMethod("getService", String.class).invoke(null, nome);
            if (b == null) throw new IllegalStateException("servizio " + nome + " assente");
            return b;
        } catch (IllegalStateException e) {
            throw e;
        } catch (Throwable t) {
            throw manca("ServiceManager.getService(" + nome + ")", t);
        }
    }

    // ---- Posizione approssimativa (solo per il widget Meteo) ----

    /**
     * Il nome della localita' (Geocoder del telefono, dove c'e'): il committente lo vuole. Il Geocoder
     * riceve solo le coordinate arrotondate. Falso = nessuna chiamata al Geocoder, localita' vuota.
     */
    static final boolean LOCALITA_ABILITATA = true;

    private static final long SCADENZA_RICHIESTA_POSIZIONE_MS = 10000;
    private static final long SCADENZA_LETTURA_POSIZIONE_MS = 2000;
    private static final long SCADENZA_GEOCODER_MS = 2000;
    private static android.location.LocationManager gestorePosizione;
    private static final LogicaPosizione.StatoPosizione statoPosizione = new LogicaPosizione.StatoPosizione();
    private static Thread filoPosizione;
    private static final LetturaUnica<LogicaPosizione.Risposta> letturaPosizione = new LetturaUnica<>("yasde-dati-posizione",
            "lettura precedente della posizione ancora in corso", "la posizione non ha risposto in tempo");
    private static final LetturaUnica<String> letturaLocalita = new LetturaUnica<>("yasde-dati-geocoder", "", "");

    /**
     * LocationManager sul contesto mascherato da shell (costruttore nascosto, servizio "location"):
     * quello di getSystemService userebbe il pacchetto "android" e il sistema lo rifiuterebbe.
     * Messaggi d'errore generici: mai coordinate ne' localita' in un'eccezione.
     */
    private static synchronized android.location.LocationManager gestorePosizione() {
        if (gestorePosizione != null) return gestorePosizione;
        try {
            Class<?> i = Class.forName("android.location.ILocationManager");
            Object b = servizioPerNome("location");
            Object svc = Class.forName("android.location.ILocationManager$Stub").getMethod("asInterface", IBinder.class).invoke(null, b);
            java.lang.reflect.Constructor<android.location.LocationManager> c =
                    android.location.LocationManager.class.getDeclaredConstructor(Context.class, i);
            c.setAccessible(true);
            gestorePosizione = c.newInstance(contesto(), svc);
            return gestorePosizione;
        } catch (Throwable t) {
            throw new IllegalStateException("API di Android non disponibile (posizione)");
        }
    }

    private static int codiceFornitore(String nome) {
        if (nome.equals(android.location.LocationManager.NETWORK_PROVIDER)) return LogicaPosizione.FORNITORE_RETE;
        if (nome.equals(android.location.LocationManager.GPS_PROVIDER)) return LogicaPosizione.FORNITORE_GPS;
        if (nome.equals(android.location.LocationManager.PASSIVE_PROVIDER)) return LogicaPosizione.FORNITORE_PASSIVO;
        return LogicaPosizione.FORNITORE_FUSO;
    }

    /** Chiusura del servizio: nessun lavoro nuovo e quello in corso viene interrotto (annulla la richiesta in finally). */
    static void fermaPosizione() {
        statoPosizione.chiudi();
        Thread t;
        synchronized (NascosteDati.class) {
            t = filoPosizione;
        }
        if (t != null) t.interrupt();
    }

    /**
     * POSIZIONE risponde SEMPRE SUBITO (decine di ms): l'ultima posizione nota (la piu' recente tra i
     * fornitori e quella trovata dall'ultima ricerca), arrotondata sul telefono, col nome gia' in
     * memoria. Se il PC lo permette e serve (nessuna posizione o piu' di 6 ore, freno di 10 minuti,
     * un solo lavoro alla volta) AVVIA su un thread la richiesta a bassa potenza sul fornitore di
     * rete (10 s, mai il GPS) e poi il nome (Geocoder, 2 s), e lo dice con `ricercaAvviata`. Mai
     * attese di rete dentro la richiesta. Posizione spenta, permesso negato e nessuna posizione sono
     * ESITI, non errori. La lettura veloce ha una scadenza di 2 s: se scade, «nessuna posizione».
     */
    static LogicaPosizione.Risposta posizione(final boolean richiestaPermessa) {
        final android.location.LocationManager lm = gestorePosizione();
        try {
            return letturaPosizione.esegui(new LetturaUnica.Lavoro<LogicaPosizione.Risposta>() {
                @Override
                public LogicaPosizione.Risposta esegui() throws Exception {
                    return rispondiPosizione(lm, richiestaPermessa);
                }
            }, SCADENZA_LETTURA_POSIZIONE_MS);
        } catch (LetturaUnica.Occupata | LetturaUnica.Scaduta e) {
            return new LogicaPosizione.Risposta(LogicaPosizione.Risultato.senzaPosizione(LogicaPosizione.ESITO_NESSUNA), false);
        } catch (LetturaUnica.Fallita f) {
            throw new IllegalStateException("Lettura della posizione non riuscita");
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("lettura della posizione interrotta");
        }
    }

    private static LogicaPosizione.Risposta rispondiPosizione(android.location.LocationManager lm, boolean permessa) {
        if (!lm.isLocationEnabled()) {
            return new LogicaPosizione.Risposta(LogicaPosizione.Risultato.senzaPosizione(LogicaPosizione.ESITO_SPENTA), false);
        }
        List<LogicaPosizione.Candidata> candidate = new ArrayList<>();
        int tentati = 0, negati = 0;
        List<String> fornitori = lm.getAllProviders();
        long adesso = android.os.SystemClock.elapsedRealtime();
        for (String f : fornitori) {
            tentati++;
            try {
                android.location.Location l = lm.getLastKnownLocation(f);
                if (l != null) {
                    long eta = l.getElapsedRealtimeNanos() > 0 ? Math.max(0, adesso - l.getElapsedRealtimeNanos() / 1000000L) : -1;
                    candidate.add(new LogicaPosizione.Candidata(l.getLatitude(), l.getLongitude(), eta, codiceFornitore(f)));
                }
            } catch (SecurityException e) {
                negati++;
            } catch (RuntimeException e) {
                // fornitore non leggibile: si passa al prossimo
            }
        }
        candidate.add(statoPosizione.memorizzata(adesso));
        LogicaPosizione.Candidata scelta = LogicaPosizione.scegli(candidate);
        boolean reteOk = false;
        try {
            reteOk = fornitori.contains(android.location.LocationManager.NETWORK_PROVIDER)
                    && lm.isProviderEnabled(android.location.LocationManager.NETWORK_PROVIDER);
        } catch (RuntimeException e) {
            reteOk = false;
        }
        String nome = "";
        boolean nomeNoto = true;
        if (scelta != null) {
            nome = statoPosizione.nomePer(LogicaPosizione.millesimi5(scelta.lat), LogicaPosizione.millesimi5(scelta.lon));
            nomeNoto = !nome.isEmpty();
        }
        int che = statoPosizione.tentaAvvio(permessa, scelta, reteOk, LOCALITA_ABILITATA, nomeNoto, adesso);
        boolean avviata = che != LogicaPosizione.StatoPosizione.NIENTE && avviaSottofondo(lm, che == LogicaPosizione.StatoPosizione.CERCA, scelta);
        if (scelta == null) {
            boolean negato = tentati > 0 && negati == tentati;
            return new LogicaPosizione.Risposta(LogicaPosizione.Risultato.senzaPosizione(negato ? LogicaPosizione.ESITO_NEGATO : LogicaPosizione.ESITO_NESSUNA), avviata);
        }
        return new LogicaPosizione.Risposta(LogicaPosizione.Risultato.nota(scelta, nome), avviata);
    }

    /** Avvia il lavoro in sottofondo; se il thread non parte, lo stato torna libero e si risponde «non avviata». */
    private static boolean avviaSottofondo(final android.location.LocationManager lm, final boolean cerca, final LogicaPosizione.Candidata scelta) {
        Thread t = new Thread("yasde-dati-posizione-sottofondo") {
            @Override
            public void run() {
                lavoroPosizione(lm, cerca, scelta);
            }
        };
        t.setDaemon(true);
        synchronized (NascosteDati.class) {
            filoPosizione = t;
        }
        try {
            t.start();
            return true;
        } catch (Throwable e) {
            statoPosizione.finito();
            return false;
        }
    }

    /**
     * Il lavoro in sottofondo: una sola getCurrentLocation di rete (10 s) e poi il nome (2 s). Qualunque
     * esito (anche un Throwable) libera lo stato; la richiesta e' sempre annullata (finally in
     * richiestaRete), quindi nessun ascoltatore resta registrato. Niente va nei log.
     */
    private static void lavoroPosizione(final android.location.LocationManager lm, final boolean cerca, final LogicaPosizione.Candidata scelta) {
        // Se il servizio e' stato chiuso tra tentaAvvio e l'avvio del thread, eseguiLavoro non fa nulla; libera sempre lo stato.
        statoPosizione.eseguiLavoro(new Runnable() {
            @Override
            public void run() {
                lavoroPosizioneSicuro(lm, cerca, scelta);
            }
        });
    }

    private static void lavoroPosizioneSicuro(android.location.LocationManager lm, boolean cerca, LogicaPosizione.Candidata scelta) {
        try {
            LogicaPosizione.Candidata bersaglio = scelta;
            if (cerca) {
                try {
                    android.location.Location l = richiestaRete(lm);
                    if (l != null && !statoPosizione.chiuso()) {
                        LogicaPosizione.Candidata nuova = new LogicaPosizione.Candidata(l.getLatitude(), l.getLongitude(), 0, LogicaPosizione.FORNITORE_RETE);
                        if (LogicaPosizione.valida(nuova.lat, nuova.lon)) {
                            statoPosizione.memorizza(nuova, android.os.SystemClock.elapsedRealtime());
                            // da qui in poi si lavora sulla copia arrotondata, non sulle coordinate esatte
                            bersaglio = statoPosizione.memorizzata(android.os.SystemClock.elapsedRealtime());
                        }
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    return;
                } catch (RuntimeException e) {
                    // nessuna posizione dalla richiesta (permesso negato, fornitore assente): nulla cambia
                }
            }
            if (LOCALITA_ABILITATA && bersaglio != null && !statoPosizione.chiuso()) {
                int la = LogicaPosizione.millesimi5(bersaglio.lat), lo = LogicaPosizione.millesimi5(bersaglio.lon);
                if (statoPosizione.nomePer(la, lo).isEmpty()) {
                    String nome = localita(la, lo);
                    if (!nome.isEmpty()) statoPosizione.impostaNome(la, lo, nome);
                }
            }
        } catch (Throwable t) {
            // il lavoro non deve lasciare nulla: lo stato lo libera eseguiLavoro
        }
    }

    /** UNA getCurrentLocation sul solo fornitore di rete, con limite di 10 s; l'annullamento e' sempre in finally. */
    private static android.location.Location richiestaRete(android.location.LocationManager lm) throws InterruptedException {
        final android.location.Location[] esito = new android.location.Location[1];
        final CountDownLatch fatto = new CountDownLatch(1);
        android.os.CancellationSignal annulla = new android.os.CancellationSignal();
        try {
            lm.getCurrentLocation(android.location.LocationManager.NETWORK_PROVIDER, annulla, new java.util.concurrent.Executor() {
                @Override
                public void execute(Runnable r) {
                    r.run();
                }
            }, new java.util.function.Consumer<android.location.Location>() {
                @Override
                public void accept(android.location.Location l) {
                    esito[0] = l;
                    fatto.countDown();
                }
            });
            fatto.await(SCADENZA_RICHIESTA_POSIZIONE_MS, java.util.concurrent.TimeUnit.MILLISECONDS);
        } finally {
            annulla.cancel();   // nessun ascoltatore resta registrato, anche su errore
        }
        return esito[0];
    }

    /** Nome della localita' dalle coordinate GIA' arrotondate; vuoto se il Geocoder manca, tarda piu' di 2 s o fallisce. */
    @SuppressWarnings("deprecation")
    private static String localita(int latMill, int lonMill) {
        return LogicaPosizione.nomeLocalita(letturaLocalita, new LogicaPosizione.Cercatore() {
            @Override
            public String cerca(int la, int lo) throws Exception {
                if (!android.location.Geocoder.isPresent()) return "";
                android.location.Geocoder g = new android.location.Geocoder(contesto(), java.util.Locale.getDefault());
                List<android.location.Address> l = g.getFromLocation(la / 1000.0, lo / 1000.0, 1);
                if (l == null || l.isEmpty()) return "";
                android.location.Address a = l.get(0);
                return a.getLocality() != null ? a.getLocality() : (a.getSubAdminArea() != null ? a.getSubAdminArea() : a.getAdminArea());
            }
        }, latMill, lonMill, SCADENZA_GEOCODER_MS);
    }

    /** Prova all'avvio: elenco delle API che mancano (vuoto = tutto presente). */
    static List<String> verifica() {
        List<String> mancano = new ArrayList<>();
        try {
            contesto().getPackageManager();
        } catch (RuntimeException e) {
            mancano.add("elenco app: " + e.getMessage());
        }
        try {
            servizioNotifiche();
        } catch (RuntimeException e) {
            mancano.add("notifiche: " + e.getMessage());
        }
        try {
            gestoreTelefonia();
        } catch (RuntimeException e) {
            mancano.add("segnale cellulare: " + e.getMessage());
        }
        try {
            gestoreStatistiche();
        } catch (RuntimeException e) {
            mancano.add("memoria in dettaglio: " + e.getMessage());
        }
        try {
            Class<?> iam = Class.forName("android.app.IActivityManager");
            if (metodoIn(iam, "getContentProviderExternal", 4) == null || metodoIn(iam, "removeContentProviderExternal", 2) == null) {
                mancano.add("calendario: getContentProviderExternal/removeContentProviderExternal");
            }
            Class.forName("android.content.AttributionSource");
        } catch (Throwable t) {
            mancano.add("calendario: " + t.getClass().getSimpleName());
        }
        try {
            schermoBloccato();
        } catch (RuntimeException e) {
            mancano.add("blocco dello schermo: " + e.getMessage());
        }
        try {
            gestoreRete();
        } catch (RuntimeException e) {
            mancano.add("rete: " + e.getMessage());
        }
        return mancano;
    }
}
