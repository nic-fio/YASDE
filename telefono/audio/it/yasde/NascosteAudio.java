package it.yasde;

import android.content.Context;
import android.content.ContextWrapper;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.os.Looper;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;

/**
 * Tutte le API nascoste di Android usate dalla cattura audio, per riflessione, in un punto solo
 * (AudioPolicy, AudioMix, AudioMixingRule: classi di AOSP, nessun produttore). Ogni chiamata che
 * fallisce lancia IllegalStateException con una frase chiara: il canale si chiude, il servizio non cade.
 */
final class NascosteAudio {
    private static final String PACCHETTO_SHELL = "com.android.shell";
    private static final String P = "android.media.audiopolicy.";

    /**
     * Usi (AudioAttributes.USAGE_*) CATTURATI: UNKNOWN, MEDIA, ALARM, NOTIFICATION, NOTIFICATION_EVENT,
     * ASSISTANCE_ACCESSIBILITY, ASSISTANCE_NAVIGATION_GUIDANCE, ASSISTANCE_SONIFICATION, GAME, ASSISTANT.
     * ESCLUSI (restano al telefono, che per questi suona da solo): VOICE_COMMUNICATION,
     * VOICE_COMMUNICATION_SIGNALLING, NOTIFICATION_RINGTONE, NOTIFICATION_COMMUNICATION_REQUEST,
     * NOTIFICATION_COMMUNICATION_INSTANT, NOTIFICATION_COMMUNICATION_DELAYED, VIRTUAL_SOURCE e gli usi
     * di sistema di sicurezza/emergenza/annunci (decisione sulle telefonate e la suoneria: aperta).
     */
    private static final int[] USI = {
        AudioAttributes.USAGE_UNKNOWN, AudioAttributes.USAGE_MEDIA, AudioAttributes.USAGE_ALARM,
        AudioAttributes.USAGE_NOTIFICATION, AudioAttributes.USAGE_NOTIFICATION_EVENT,
        AudioAttributes.USAGE_ASSISTANCE_ACCESSIBILITY,
        AudioAttributes.USAGE_ASSISTANCE_NAVIGATION_GUIDANCE, AudioAttributes.USAGE_ASSISTANCE_SONIFICATION,
        AudioAttributes.USAGE_GAME, AudioAttributes.USAGE_ASSISTANT,
    };

    private static Context contesto;
    private static Looper ciclo;

    private NascosteAudio() {}

    /** Contesto che si presenta come il pacchetto della shell (il sistema controlla che appartenga all'uid 2000). */
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

        /**
         * Da qui AudioRecord prende il pacchetto con cui si presenta (senza, e' «android» e AudioFlinger
         * rifiuta l'uid 2000). La classe AttributionSource ha il costruttore nascosto: per riflessione.
         */
        public android.content.AttributionSource getAttributionSource() {
            try {
                Class<?> b = Class.forName("android.content.AttributionSource$Builder");
                Object bb = b.getConstructor(int.class).newInstance(android.os.Process.myUid());
                b.getMethod("setPackageName", String.class).invoke(bb, PACCHETTO_SHELL);
                b.getMethod("setPid", int.class).invoke(bb, android.os.Process.myPid());
                return (android.content.AttributionSource) b.getMethod("build").invoke(bb);
            } catch (Throwable t) {
                throw manca("AttributionSource", t);
            }
        }
    }

    private static IllegalStateException manca(String che, Throwable causa) {
        Throwable c = causa;
        if (c instanceof InvocationTargetException && c.getCause() != null) c = c.getCause();
        return new IllegalStateException("API di Android non disponibile (" + che + "): " + c, c);
    }

    /**
     * Contesto di sistema (ActivityThread) mascherato da shell, creato su un thread con il suo
     * Looper (ActivityThread lo pretende): lo stesso Looper serve alle callback della policy audio.
     */
    static synchronized Context contesto() {
        if (contesto != null) return contesto;
        final Object[] esito = new Object[1];
        final CountDownLatch pronto = new CountDownLatch(1);
        Thread t = new Thread("yasde-audio-looper") {
            @Override
            public void run() {
                try {
                    if (Looper.myLooper() == null) Looper.prepare();
                    ciclo = Looper.myLooper();
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

    /** Una regola di cattura attiva: la policy registrata e l'AudioRecord da cui si leggono i campioni. */
    static final class Regola {
        private final AudioManager gestore;
        private final Object policy;
        final AudioRecord record;
        private boolean tolta;

        Regola(AudioManager gestore, Object policy, AudioRecord record) {
            this.gestore = gestore;
            this.policy = policy;
            this.record = record;
        }

        /** Ferma la lettura e toglie la regola: il telefono torna a suonare. Idempotente. */
        synchronized void togli() {
            if (tolta) return;
            tolta = true;
            try {
                record.stop();
            } catch (Throwable t) {
                // gia' fermo
            }
            record.release();
            try {
                Method m = AudioManager.class.getMethod("unregisterAudioPolicy", Class.forName(P + "AudioPolicy"));
                m.invoke(gestore, policy);
            } catch (Throwable t) {
                System.out.println("[audio] regola non tolta: " + manca("unregisterAudioPolicy", t).getMessage());
            }
        }
    }

    private static AudioManager gestoreAudio() {
        try {
            AudioManager g = (AudioManager) contesto().getSystemService(Context.AUDIO_SERVICE);
            if (g == null) throw new IllegalStateException("servizio audio assente");
            return g;
        } catch (RuntimeException e) {
            throw e;
        } catch (Throwable t) {
            throw manca("AudioManager", t);
        }
    }

    private static int costante(Class<?> c, String nome) throws Exception {
        return c.getField(nome).getInt(null);
    }

    /**
     * Il registratore della regola. AudioPolicy.createAudioRecordSink lo crea senza contesto e quindi con il
     * pacchetto «android», che per l'uid 2000 viene rifiutato: lo si costruisce qui con gli stessi attributi
     * (sorgente REMOTE_SUBMIX, indirizzo della regola, volume fisso) e il contesto mascherato da shell.
     */
    private static AudioRecord creaRegistratore(Class<?> cMix, Object mix) throws Exception {
        String registrazione = (String) cMix.getMethod("getRegistration").invoke(mix);
        AudioAttributes.Builder ab = new AudioAttributes.Builder();
        AudioAttributes.Builder.class.getMethod("setInternalCapturePreset", int.class).invoke(ab, 8); // REMOTE_SUBMIX
        Method tag = AudioAttributes.Builder.class.getMethod("addTag", String.class);
        tag.invoke(ab, "addr=" + registrazione);
        tag.invoke(ab, AudioRecord.class.getField("SUBMIX_FIXED_VOLUME").get(null));
        AudioFormat formato = new AudioFormat.Builder().setSampleRate(Cattura.FREQUENZA)
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_IN_STEREO).build();
        int minimo = AudioRecord.getMinBufferSize(Cattura.FREQUENZA, AudioFormat.CHANNEL_IN_STEREO,
            AudioFormat.ENCODING_PCM_16BIT);
        AudioRecord.Builder rb = new AudioRecord.Builder();
        AudioRecord.Builder.class.getMethod("setAudioAttributes", AudioAttributes.class).invoke(rb, ab.build());
        rb.setAudioFormat(formato);
        rb.setBufferSizeInBytes(Math.max(minimo, 8 * 3840));
        AudioRecord.Builder.class.getMethod("setContext", Context.class).invoke(rb, contesto());
        return rb.build();
    }

    /**
     * Registra la regola «loopback» (tutto l'audio in riproduzione va al registratore, niente esce dal
     * telefono) e ritorna il registratore gia' avviato, 48 kHz stereo 16 bit.
     */
    static Regola creaRegola() {
        String passo = "AudioMixingRule";
        Object policy = null;
        AudioManager gestore = null;
        try {
            Class<?> cRegolaB = Class.forName(P + "AudioMixingRule$Builder");
            Class<?> cRegola = Class.forName(P + "AudioMixingRule");
            Object rb = cRegolaB.getConstructor().newInstance();
            Method aggiungi = cRegolaB.getMethod("addMixRule", int.class, Object.class);
            int perUso = costante(cRegola, "RULE_MATCH_ATTRIBUTE_USAGE");
            for (int uso : USI) {
                aggiungi.invoke(rb, perUso, new AudioAttributes.Builder().setUsage(uso).build());
            }
            Object regola = cRegolaB.getMethod("build").invoke(rb);

            passo = "AudioMix";
            Class<?> cMixB = Class.forName(P + "AudioMix$Builder");
            Class<?> cMix = Class.forName(P + "AudioMix");
            Object mb = cMixB.getConstructor(cRegola).newInstance(regola);
            AudioFormat formato = new AudioFormat.Builder().setSampleRate(Cattura.FREQUENZA)
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build();
            cMixB.getMethod("setFormat", AudioFormat.class).invoke(mb, formato);
            cMixB.getMethod("setRouteFlags", int.class).invoke(mb, costante(cMix, "ROUTE_FLAG_LOOP_BACK"));
            Object mix = cMixB.getMethod("build").invoke(mb);

            passo = "AudioPolicy";
            Class<?> cPolB = Class.forName(P + "AudioPolicy$Builder");
            Class<?> cPol = Class.forName(P + "AudioPolicy");
            contesto();
            Object pb = cPolB.getConstructor(Context.class).newInstance(contesto);
            cPolB.getMethod("addMix", cMix).invoke(pb, mix);
            cPolB.getMethod("setLooper", Looper.class).invoke(pb, ciclo);
            policy = cPolB.getMethod("build").invoke(pb);

            passo = "registerAudioPolicy";
            gestore = gestoreAudio();
            Object esito = AudioManager.class.getMethod("registerAudioPolicy", cPol).invoke(gestore, policy);
            if (!(esito instanceof Integer) || ((Integer) esito).intValue() != 0) {
                throw new IllegalStateException("il sistema ha rifiutato la regola audio (codice " + esito + ")");
            }

            passo = "registratore (AudioRecord)";
            AudioRecord record = creaRegistratore(cMix, mix);
            if (record == null || record.getState() != AudioRecord.STATE_INITIALIZED) {
                throw new IllegalStateException("registratore della regola non inizializzato");
            }
            record.startRecording();
            if (record.getRecordingState() != AudioRecord.RECORDSTATE_RECORDING) {
                record.release();
                throw new IllegalStateException("registratore della regola non avviato");
            }
            return new Regola(gestore, policy, record);
        } catch (Throwable t) {
            if (policy != null && gestore != null) {
                try {
                    AudioManager.class.getMethod("unregisterAudioPolicy", Class.forName(P + "AudioPolicy"))
                        .invoke(gestore, policy);
                } catch (Throwable ignora) {
                    // la regola non era registrata
                }
            }
            if (t instanceof IllegalStateException) throw (IllegalStateException) t;
            throw manca(passo, t);
        }
    }

    /** Prova all'avvio: elenco delle API nascoste che mancano (vuoto = tutto presente). */
    static List<String> verifica() {
        List<String> mancano = new ArrayList<>();
        try {
            Class<?> at = Class.forName("android.app.ActivityThread");
            at.getMethod("currentActivityThread");
            at.getMethod("systemMain");
            at.getMethod("getSystemContext");
            Class<?> cAs = Class.forName("android.content.AttributionSource$Builder");
            cAs.getConstructor(int.class);
            cAs.getMethod("setPackageName", String.class);
            cAs.getMethod("setPid", int.class);
            cAs.getMethod("build");
            Class<?> cRegolaB = Class.forName(P + "AudioMixingRule$Builder");
            Class<?> cRegola = Class.forName(P + "AudioMixingRule");
            cRegolaB.getConstructor();
            cRegolaB.getMethod("addMixRule", int.class, Object.class);
            cRegolaB.getMethod("build");
            costante(cRegola, "RULE_MATCH_ATTRIBUTE_USAGE");
            Class<?> cMixB = Class.forName(P + "AudioMix$Builder");
            Class<?> cMix = Class.forName(P + "AudioMix");
            cMixB.getConstructor(cRegola);
            cMixB.getMethod("setFormat", AudioFormat.class);
            cMixB.getMethod("setRouteFlags", int.class);
            cMixB.getMethod("build");
            costante(cMix, "ROUTE_FLAG_LOOP_BACK");
            Class<?> cPolB = Class.forName(P + "AudioPolicy$Builder");
            Class<?> cPol = Class.forName(P + "AudioPolicy");
            cPolB.getConstructor(Context.class);
            cPolB.getMethod("addMix", cMix);
            cPolB.getMethod("setLooper", Looper.class);
            cPolB.getMethod("build");
            cMix.getMethod("getRegistration");
            AudioRecord.Builder.class.getMethod("setAudioAttributes", AudioAttributes.class);
            AudioRecord.Builder.class.getMethod("setContext", Context.class);
            AudioAttributes.Builder.class.getMethod("setInternalCapturePreset", int.class);
            AudioAttributes.Builder.class.getMethod("addTag", String.class);
            AudioRecord.class.getField("SUBMIX_FIXED_VOLUME");
            AudioManager.class.getMethod("registerAudioPolicy", cPol);
            AudioManager.class.getMethod("unregisterAudioPolicy", cPol);
        } catch (Throwable t) {
            mancano.add("regola di cattura audio (AudioPolicy): " + t);
        }
        return mancano;
    }
}
