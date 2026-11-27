package it.yasde;

import android.content.Context;
import android.database.Cursor;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.IBinder;
import java.lang.reflect.InvocationTargetException;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Method;
import java.util.concurrent.TimeUnit;
import java.util.ArrayList;
import java.util.List;

/**
 * Lettura (in SOLA lettura) di registro chiamate, rete e sessioni multimediali, e invio dei tre
 * comandi multimediali ammessi (protocollo 3, docs/protocollo.md §2.12). Contesto e servizi come
 * in NascosteDati (contesto mascherato da shell). Ogni eccezione diventa un esito (2 permesso
 * negato, 1 non disponibile): mai un thread morto. RISERVATEZZA: nessun nome, numero, titolo o
 * rete in un log o in un messaggio d'errore; delle eccezioni si usa solo il nome della classe.
 */
final class NascosteTelefonia {
    private static final String PACCHETTO_SHELL = "com.android.shell";
    private static final long SCADENZA_MS = 3000;
    /** Letture che possono lanciare comandi di riserva (ognuno con la sua scadenza di 2 s). */
    private static final long SCADENZA_LUNGA_MS = 6000;
    private static final int COMANDO_SECONDI = 2;
    private static final int TETTO_BYTE = 2 << 20;

    private NascosteTelefonia() {}

    // ---- Interrogazione di un provider come shell (stesso metodo del calendario) ----

    interface Lettore {
        void riga(Cursor c) throws Exception;
    }

    private static void interroga(String autorita, android.net.Uri uri, String[] proiezione, String selezione, String ordine,
                                  int limite, Lettore lettore) throws Exception {
        final IBinder token = new android.os.Binder();
        Object am = Class.forName("android.app.ActivityManager").getMethod("getService").invoke(null);
        Class<?> iam = Class.forName("android.app.IActivityManager");
        Method prendi = NascosteDati.metodoIn(iam, "getContentProviderExternal", 4);
        Method rilascia = NascosteDati.metodoIn(iam, "removeContentProviderExternal", 2);
        if (prendi == null || rilascia == null) throw new NoSuchMethodException("getContentProviderExternal");
        Object holder = prendi.invoke(am, autorita, android.os.Process.myUid() / 100000, token, "yasde");
        if (holder == null) throw new IllegalStateException("provider assente");
        try {
            Object provider = holder.getClass().getField("provider").get(holder);
            if (provider == null) throw new IllegalStateException("provider assente");
            Object fonte = new android.content.AttributionSource.Builder(android.os.Process.myUid()).setPackageName(PACCHETTO_SHELL).build();
            Method q = NascosteDati.metodoIn(provider.getClass(), "query", 5);
            if (q == null) throw new NoSuchMethodException("IContentProvider.query");
            android.os.Bundle args = new android.os.Bundle();
            if (selezione != null) args.putString(android.content.ContentResolver.QUERY_ARG_SQL_SELECTION, selezione);
            if (ordine != null) args.putString(android.content.ContentResolver.QUERY_ARG_SQL_SORT_ORDER, ordine);
            if (limite > 0) args.putInt(android.content.ContentResolver.QUERY_ARG_LIMIT, limite);
            Cursor c = (Cursor) q.invoke(provider, fonte, uri, proiezione, args, null);
            if (c == null) throw new IllegalStateException("cursore nullo");
            try {
                int letti = 0;
                // Il limite si fa rispettare anche qui, se il provider lo ignorasse.
                while ((limite <= 0 || letti < limite) && c.moveToNext()) {
                    lettore.riga(c);
                    letti++;
                }
            } finally {
                c.close();
            }
        } finally {
            try {
                rilascia.invoke(am, autorita, token);
            } catch (Throwable e) {
                // niente altro da fare
            }
        }
    }

    // ---- Chiamate ----

    private static final LetturaUnica<List<CampiTelefonia.Chiamata>> letturaChiamate = new LetturaUnica<>("yasde-dati-chiamate",
            "lettura precedente delle chiamate ancora in corso", "Il registro chiamate non ha risposto in tempo");

    /** Risposta CHIAMATE gia' codificata: le piu' recenti per prime. */
    static byte[] chiamate(final int massimo) throws Exception {
        int esito = CampiTelefonia.ESITO_OK;
        List<CampiTelefonia.Chiamata> v = new ArrayList<>();
        try {
            v = letturaChiamate.esegui(new LetturaUnica.Lavoro<List<CampiTelefonia.Chiamata>>() {
                @Override
                public List<CampiTelefonia.Chiamata> esegui() throws Exception {
                    final List<CampiTelefonia.Chiamata> out = new ArrayList<>();
                    interroga("call_log", android.provider.CallLog.Calls.CONTENT_URI,
                            new String[] {android.provider.CallLog.Calls.TYPE, android.provider.CallLog.Calls.DATE,
                                    android.provider.CallLog.Calls.DURATION, android.provider.CallLog.Calls.CACHED_NAME,
                                    android.provider.CallLog.Calls.NUMBER},
                            null, android.provider.CallLog.Calls.DATE + " DESC", massimo, new Lettore() {
                                @Override
                                public void riga(Cursor c) {
                                    out.add(new CampiTelefonia.Chiamata(c.getInt(0), c.getLong(1), c.getLong(2), c.getString(3), c.getString(4)));
                                }
                            });
                    return out;
                }
            }, SCADENZA_MS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            esito = CampiTelefonia.ESITO_NON_DISPONIBILE;
        } catch (RuntimeException e) {
            esito = CampiTelefonia.negato(e) ? CampiTelefonia.ESITO_NEGATO : CampiTelefonia.ESITO_NON_DISPONIBILE;
            System.out.println("[dati] chiamate non leggibili: " + CampiTelefonia.catena(e));
        }
        return CampiTelefonia.codificaChiamate(esito, esito == CampiTelefonia.ESITO_OK ? v : new ArrayList<CampiTelefonia.Chiamata>(), massimo);
    }

    // ---- Comandi di riserva (Android puro, sola lettura salvo i tasti media) ----

    private static final class Risultato {
        int codice = -1;
        String testo = "";
    }

    /**
     * Esegue un comando con lista di argomenti (mai una stringa di shell), scadenza vera e output
     * letto fino a TETTO_BYTE (il resto si scarta). Nessun lock tenuto. L'output non va mai in un log.
     */
    private static Risultato esegui(String... comando) throws IOException {
        Process p = new ProcessBuilder(comando).redirectErrorStream(true).start();
        InputStream in = p.getInputStream();
        try {
            p.getOutputStream().close();
            final ByteArrayOutputStream o = new ByteArrayOutputStream();
            final InputStream flusso = in;
            Thread lettore = new Thread("yasde-dati-shell") {
                @Override
                public void run() {
                    byte[] buf = new byte[8192];
                    try {
                        int n;
                        while ((n = flusso.read(buf)) > 0) {
                            synchronized (o) {
                                if (o.size() < TETTO_BYTE) o.write(buf, 0, Math.min(n, TETTO_BYTE - o.size()));
                            }
                        }
                    } catch (IOException e) {
                        // processo ucciso
                    }
                }
            };
            lettore.setDaemon(true);
            lettore.start();
            try {
                if (!p.waitFor(COMANDO_SECONDI, TimeUnit.SECONDS)) throw new Scaduto("comando scaduto");
                lettore.join(1000);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new Scaduto("comando interrotto");
            }
            Risultato r = new Risultato();
            r.codice = p.exitValue();
            synchronized (o) {
                r.testo = o.toString("UTF-8");
            }
            return r;
        } finally {
            try {
                in.close();
            } catch (IOException e) {
                // gia' chiuso
            }
            p.destroyForcibly();   // no-op se e' gia' finito
        }
    }

    /** Il comando e' partito ma non e' finito in tempo (o l'attesa e' stata interrotta). */
    private static final class Scaduto extends IOException {
        private static final long serialVersionUID = 1L;

        Scaduto(String frase) {
            super(frase);
        }
    }

    // ---- Rete ----

    private static final LetturaUnica<CampiTelefonia.Rete> letturaRete = new LetturaUnica<>("yasde-dati-rete",
            "lettura precedente della rete ancora in corso", "La rete non ha risposto in tempo");

    /** Valore 0/1 di una impostazione globale dal provider "settings"; -1 se non si legge. Logga la catena (senza dati). */
    private static int impostazioneGlobale(final String nome) {
        final int[] valore = {-1};
        try {
            interroga("settings", android.net.Uri.parse("content://settings/global"), new String[] {"value"},
                    "name='" + nome + "'", null, 1, new Lettore() {
                        @Override
                        public void riga(Cursor c) {
                            String v = c.getString(0);
                            if ("0".equals(v)) valore[0] = 0;
                            else if ("1".equals(v)) valore[0] = 1;
                        }
                    });
        } catch (Throwable t) {
            System.out.println("[dati] impostazione non leggibile: " + CampiTelefonia.catena(t));
        }
        return valore[0];
    }

    @SuppressWarnings("deprecation")
    private static CampiTelefonia.Rete leggiRete() {
        CampiTelefonia.Rete r = new CampiTelefonia.Rete();
        ConnectivityManager cm = NascosteDati.gestoreRete();
        // Wi-Fi: acceso se il gestore lo dice; connesso se c'e' una rete con trasporto Wi-Fi.
        try {
            NetworkCapabilities wifi = null;
            android.net.wifi.WifiManager wm = NascosteDati.servizio(Context.WIFI_SERVICE, android.net.wifi.WifiManager.class);
            boolean acceso = wm.getWifiState() == android.net.wifi.WifiManager.WIFI_STATE_ENABLED;
            for (Network n : cm.getAllNetworks()) {
                NetworkCapabilities c = cm.getNetworkCapabilities(n);
                if (c != null && c.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) && !c.hasTransport(NetworkCapabilities.TRANSPORT_VPN)) {
                    wifi = c;
                    break;
                }
            }
            r.wifi = wifi != null ? 2 : (acceso ? 1 : 0);
            if (wifi != null) {
                r.wifiLivello = NascosteDati.segnaleWifi(wifi);
                try {
                    android.net.wifi.WifiInfo wi = wm.getConnectionInfo();
                    if (wi != null) r.wifiNome = CampiTelefonia.nomeRete(wi.getSSID());
                } catch (Throwable t) {
                    System.out.println("[dati] nome rete non leggibile: " + CampiTelefonia.catena(t));
                }
                if (r.wifiNome.isEmpty()) {
                    // Senza permesso di posizione l'SSID e' nascosto: `cmd wifi status` lo riporta.
                    try {
                        Risultato x = esegui("cmd", "wifi", "status");
                        if (x.codice == 0) r.wifiNome = LettoriShell.nomeDaWifiStatus(x.testo);
                    } catch (Throwable t) {
                        System.out.println("[dati] cmd wifi status non riuscito: " + CampiTelefonia.catena(t));
                    }
                }
            }
        } catch (Throwable t) {
            r.wifi = 0;
            r.wifiLivello = 0;
            r.wifiNome = "";
            System.out.println("[dati] wifi non leggibile: " + CampiTelefonia.catena(t));
        }
        // Mobile: via API; se nega, o dice «assente» (con due slot il primo puo' essere vuoto), il registro di telefonia.
        boolean riserva = false;
        boolean api_fallita = false;
        try {
            android.telephony.TelephonyManager tm = NascosteDati.gestoreTelefonia();
            if (tm.getSimState() == android.telephony.TelephonyManager.SIM_STATE_ABSENT) {
                r.mobile = 0;
                riserva = true;
            } else {
                r.mobileLivello = NascosteDati.segnaleCellulare();
                boolean inServizio = r.mobileLivello > 0;
                try {
                    android.telephony.ServiceState ss = tm.getServiceState();
                    if (ss != null) inServizio = ss.getState() == android.telephony.ServiceState.STATE_IN_SERVICE;
                } catch (Throwable t) {
                    System.out.println("[dati] ServiceState non leggibile: " + CampiTelefonia.catena(t));
                }
                r.mobile = inServizio ? 2 : 1;
                if (!inServizio) r.mobileLivello = 0;
                if (inServizio) {
                    String op = tm.getNetworkOperatorName();
                    r.operatore = op == null ? "" : op;
                    try {
                        r.generazione = CampiTelefonia.generazione(tm.getDataNetworkType());
                    } catch (Throwable t) {
                        System.out.println("[dati] tipo di rete non leggibile: " + CampiTelefonia.catena(t));
                    }
                }
            }
        } catch (Throwable t) {
            r.mobile = 0;
            r.mobileLivello = 0;
            r.generazione = 0;
            r.operatore = "";
            api_fallita = true;
            riserva = true;
            System.out.println("[dati] mobile non leggibile: " + CampiTelefonia.catena(t));
        }
        if (riserva) {
            try {
                Risultato x = esegui("dumpsys", "telephony.registry");
                LettoriShell.Mobile m = x.codice == 0 ? LettoriShell.mobileDaRegistry(x.testo) : null;
                // API fallita: vale il registro (anche «senza servizio»). API «assente»: si cambia solo se
                // il registro dice «in servizio» (con slot vuoti dice «senza servizio» e «assente» resta vero).
                if (m != null && (api_fallita || m.mobile == 2)) {
                    r.mobile = m.mobile;
                    r.mobileLivello = m.livello;
                    r.generazione = m.generazione;
                    r.operatore = m.operatore;
                }
            } catch (Throwable t) {
                System.out.println("[dati] registro telefonia non leggibile: " + CampiTelefonia.catena(t));
            }
        }
        // Dati mobili e modalita' aereo: mai un valore inventato, 2 = ignoto.
        r.datiMobili = 2;
        try {
            r.datiMobili = NascosteDati.gestoreTelefonia().isDataEnabled() ? 1 : 0;
        } catch (Throwable t) {
            System.out.println("[dati] dati mobili (API) non leggibili: " + CampiTelefonia.catena(t));
            int v = impostazioneGlobale("mobile_data");
            if (v >= 0) r.datiMobili = v;
        }
        int a = impostazioneGlobale("airplane_mode_on");
        r.aereo = a >= 0 ? a : 2;
        r.esito = CampiTelefonia.ESITO_OK;
        return r;
    }

    static byte[] rete() throws Exception {
        CampiTelefonia.Rete r;
        try {
            r = letturaRete.esegui(new LetturaUnica.Lavoro<CampiTelefonia.Rete>() {
                @Override
                public CampiTelefonia.Rete esegui() {
                    return leggiRete();
                }
            }, SCADENZA_LUNGA_MS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            r = new CampiTelefonia.Rete();
            r.esito = CampiTelefonia.ESITO_NON_DISPONIBILE;
        } catch (RuntimeException e) {
            // Scaduta, Occupata o lettura fallita: nessun dato, l'esito dice perche'.
            r = new CampiTelefonia.Rete();
            r.esito = CampiTelefonia.negato(e) ? CampiTelefonia.ESITO_NEGATO : CampiTelefonia.ESITO_NON_DISPONIBILE;
            System.out.println("[dati] rete non leggibile: " + CampiTelefonia.catena(e));
        }
        return CampiTelefonia.codificaRete(r);
    }

    // ---- Media ----

    private static final LetturaUnica<byte[]> letturaMedia = new LetturaUnica<>("yasde-dati-media",
            "lettura precedente dei media ancora in corso", "La sessione multimediale non ha risposto in tempo");

    /** Rifiuto voluto di un comando (nessuna sessione, comando non permesso): non si ritenta per altra via. */
    static final class Rifiuto extends IllegalStateException {
        private static final long serialVersionUID = 1L;

        Rifiuto(String frase) {
            super(frase);
        }
    }

    /** Sessioni attive; SecurityException se non e' permesso, IllegalStateException se il servizio manca. */
    private static List<android.media.session.MediaController> sessioni() {
        android.media.session.MediaSessionManager m =
                NascosteDati.servizio(Context.MEDIA_SESSION_SERVICE, android.media.session.MediaSessionManager.class);
        List<android.media.session.MediaController> l = m.getActiveSessions(null);
        return l == null ? new ArrayList<android.media.session.MediaController>() : l;
    }

    private static int statoProtocollo(android.media.session.MediaController c) {
        android.media.session.PlaybackState p = c.getPlaybackState();
        return p == null ? 1 : CampiTelefonia.statoMedia(p.getState());
    }

    private static android.media.session.MediaController sceglila(List<android.media.session.MediaController> l) {
        int[] stati = new int[l.size()];
        for (int i = 0; i < stati.length; i++) stati[i] = statoProtocollo(l.get(i));
        int i = CampiTelefonia.scegliSessione(stati);
        return i < 0 ? null : l.get(i);
    }

    private static int comandiDi(android.media.session.MediaController c) {
        android.media.session.PlaybackState p = c.getPlaybackState();
        return p == null ? 0 : CampiTelefonia.comandiPossibili(p.getActions(), CampiTelefonia.statoMedia(p.getState()));
    }

    private static CampiTelefonia.Media mediaApi() {
        CampiTelefonia.Media m = new CampiTelefonia.Media();
        android.media.session.MediaController c = sceglila(sessioni());
        if (c != null) {
            m.stato = statoProtocollo(c);
            m.comandi = comandiDi(c);
            m.pacchetto = c.getPackageName();
            android.media.MediaMetadata md = c.getMetadata();
            if (md != null) {
                m.titolo = md.getString(android.media.MediaMetadata.METADATA_KEY_TITLE);
                String a = md.getString(android.media.MediaMetadata.METADATA_KEY_ARTIST);
                if (a == null || a.isEmpty()) a = md.getString(android.media.MediaMetadata.METADATA_KEY_ALBUM_ARTIST);
                m.artista = a;
            }
            if (m.titolo == null) m.titolo = "";
            if (m.artista == null) m.artista = "";
            if (m.pacchetto == null) m.pacchetto = "";
        }
        return m;
    }

    /** Strada di riserva: `dumpsys media_session` (sola lettura). null se il testo non si capisce. */
    private static CampiTelefonia.Media mediaShell() throws IOException {
        Risultato x = esegui("dumpsys", "media_session");
        return x.codice == 0 ? LettoriShell.mediaDaDumpsys(x.testo) : null;
    }

    /** Risposta MEDIA gia' codificata. */
    static byte[] media() throws Exception {
        byte[] r;
        try {
            r = letturaMedia.esegui(new LetturaUnica.Lavoro<byte[]>() {
                @Override
                public byte[] esegui() throws Exception {
                    CampiTelefonia.Media m;
                    try {
                        m = mediaApi();
                    } catch (Exception e) {
                        System.out.println("[dati] media via API non leggibili: " + CampiTelefonia.catena(e));
                        try {
                            m = mediaShell();
                        } catch (Exception e2) {
                            System.out.println("[dati] media via dumpsys non leggibili: " + CampiTelefonia.catena(e2));
                            m = null;
                        }
                        if (m == null) {
                            return esitoMedia(CampiTelefonia.negato(e) ? CampiTelefonia.ESITO_NEGATO : CampiTelefonia.ESITO_NON_DISPONIBILE);
                        }
                    }
                    return CampiTelefonia.codificaMedia(m);
                }
            }, SCADENZA_LUNGA_MS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            r = esitoMedia(CampiTelefonia.ESITO_NON_DISPONIBILE);
        } catch (RuntimeException e) {
            r = esitoMedia(CampiTelefonia.negato(e) ? CampiTelefonia.ESITO_NEGATO : CampiTelefonia.ESITO_NON_DISPONIBILE);
            System.out.println("[dati] media non leggibili: " + CampiTelefonia.catena(e));
        }
        return r;
    }

    private static byte[] esitoMedia(int esito) throws Exception {
        CampiTelefonia.Media m = new CampiTelefonia.Media();
        m.esito = esito;
        return CampiTelefonia.codificaMedia(m);
    }

    private static void comandoApi(int comando) {
        android.media.session.MediaController c = sceglila(sessioni());
        if (c == null) throw new Rifiuto("nessuna sessione multimediale");
        if (!CampiTelefonia.comandoPermesso(comando, comandiDi(c))) throw new Rifiuto("comando non permesso dalla sessione");
        android.media.session.MediaController.TransportControls t = c.getTransportControls();
        if (comando == CampiTelefonia.CMD_AVANTI) {
            t.skipToNext();
        } else if (comando == CampiTelefonia.CMD_INDIETRO) {
            t.skipToPrevious();
        } else if (statoProtocollo(c) == 2) {
            t.pause();
        } else {
            t.play();
        }
    }

    /**
     * Strada di riserva: solo se `dumpsys` ha trovato una sessione e il comando e' tra i possibili
     * (azioni illeggibili = nessun comando). Solo i tre tasti ammessi, mai altri. Se `cmd media_session
     * dispatch` e' partito (codice d'uscita o scadenza) NON si ripiega su `input keyevent`: un secondo
     * play/pausa annullerebbe il primo. Si ripiega solo se il comando non c'e' (non parte, o «Can't find service»).
     */
    private static void comandoShell(int comando) throws IOException {
        CampiTelefonia.Media m = mediaShell();
        if (m == null) throw new IllegalStateException("sessione multimediale non leggibile");
        if (m.stato == 0) throw new Rifiuto("nessuna sessione multimediale");
        if (!CampiTelefonia.comandoPermesso(comando, m.comandi)) throw new Rifiuto("comando non permesso dalla sessione");
        String chiave = comando == CampiTelefonia.CMD_AVANTI ? "next" : comando == CampiTelefonia.CMD_INDIETRO ? "previous" : "play-pause";
        String tasto = comando == CampiTelefonia.CMD_AVANTI ? "87" : comando == CampiTelefonia.CMD_INDIETRO ? "88" : "85";
        Risultato x;
        try {
            x = esegui("cmd", "media_session", "dispatch", chiave);
        } catch (Scaduto e) {
            throw new IllegalStateException("comando multimediale non confermato");
        } catch (IOException e) {
            x = null;   // `cmd` non parte: si prova il tasto
        }
        if (x != null) {
            if (x.codice == 0) return;
            if (!x.testo.contains("Can't find service") && !x.testo.contains("No service")) {
                throw new IllegalStateException("comando multimediale non riuscito");
            }
        }
        x = esegui("input", "keyevent", tasto);
        if (x.codice != 0) throw new IllegalStateException("comando multimediale non riuscito");
    }

    /**
     * Comando multimediale (1 play/pausa, 2 avanti, 3 indietro, gia' controllato). Se non c'e' una
     * sessione o la sessione non permette il comando: IllegalStateException (ERRORE al PC), nulla toccato.
     * Mai altri comandi oltre a questi tre.
     */
    static void comandoMedia(final int comando) throws Exception {
        try {
            letturaMedia.esegui(new LetturaUnica.Lavoro<byte[]>() {
                @Override
                public byte[] esegui() throws Exception {
                    try {
                        comandoApi(comando);
                    } catch (Rifiuto r) {
                        throw r;
                    } catch (Exception e) {
                        System.out.println("[dati] comando media via API non riuscito: " + CampiTelefonia.catena(e));
                        comandoShell(comando);
                    }
                    return new byte[0];
                }
            }, SCADENZA_LUNGA_MS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("comando interrotto");
        } catch (LetturaUnica.Fallita e) {
            Throwable causa = e.getCause();
            if (causa instanceof IllegalStateException) throw (IllegalStateException) causa;
            throw new IllegalStateException("comando multimediale non riuscito");
        }
    }
}
