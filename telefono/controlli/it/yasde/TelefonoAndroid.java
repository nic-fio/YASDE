package it.yasde;

import android.content.Context;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import java.io.IOException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executor;
import java.util.concurrent.TimeUnit;

/**
 * Il telefono vero, per Controlli e Trova. Suoneria, volume e non disturbare in lettura con le API
 * pubbliche (AudioManager, NotificationManager); scrittura del modo e del volume con AudioManager;
 * non disturbare con `cmd notification set_dnd` (Android puro, nessuna API pubblica lo permette alla shell);
 * torcia con CameraManager.setTorchMode (sulla prima fotocamera con flash).
 *
 * Eccezione di sicurezza o comando assente = Telefono.NonPermesso. Niente è creato prima del primo uso.
 */
final class TelefonoAndroid implements Telefono {
    /** Solo per la prova sul telefono vero (valori dichiarati nello script): volume della suoneria di «trova»; -1 = il massimo. */
    static volatile int volumeTrovaProva = -1;
    /** Solo per la prova: suoneria con il volume del lettore a 0 e senza vibrazione (nessun suono udibile). */
    static volatile boolean suonoSilenziosoProva;

    private static final int FLUSSO = AudioManager.STREAM_RING;
    private static final int ATTESA_TORCIA_S = 2;

    private final Object serraturaSuono = new Object();
    private AudioTrack suono; // protetto da serraturaSuono
    private Vibrator vibratore; // protetto da serraturaSuono

    private final Object serraturaTorcia = new Object();
    private CameraManager camera; // protetto da serraturaTorcia
    private String idTorcia;
    private boolean torciaCercata;
    private boolean statoTorciaNoto;
    private boolean torciaAccesa;
    private final CountDownLatch primoStatoTorcia = new CountDownLatch(1);

    private volatile Boolean comandoNonDisturbare;
    private volatile Boolean comandiRipristino;

    private static AudioManager audio() {
        return (AudioManager) NascosteControlli.servizio(Context.AUDIO_SERVICE);
    }

    // ---- suoneria e volume ----

    @Override
    public int modoSuoneria() throws Exception {
        switch (audio().getRingerMode()) {
            case AudioManager.RINGER_MODE_NORMAL:
                return 0;
            case AudioManager.RINGER_MODE_VIBRATE:
                return 1;
            case AudioManager.RINGER_MODE_SILENT:
                return 2;
            default:
                throw new IOException("modo della suoneria sconosciuto");
        }
    }

    @Override
    public void impostaModoSuoneria(int modo) throws Exception {
        int android;
        switch (modo) {
            case 0:
                android = AudioManager.RINGER_MODE_NORMAL;
                break;
            case 1:
                android = AudioManager.RINGER_MODE_VIBRATE;
                break;
            case 2:
                android = AudioManager.RINGER_MODE_SILENT;
                break;
            default:
                throw new IllegalArgumentException("modo non valido");
        }
        try {
            audio().setRingerMode(android);
        } catch (SecurityException e) {
            throw new NonPermesso("setRingerMode rifiutato");
        }
    }

    @Override
    public int volumeSuoneria() throws Exception {
        return audio().getStreamVolume(FLUSSO);
    }

    @Override
    public int volumeSuoneriaMassimo() throws Exception {
        return audio().getStreamMaxVolume(FLUSSO);
    }

    @Override
    public void impostaVolumeSuoneria(int volume) throws Exception {
        try {
            audio().setStreamVolume(FLUSSO, volume, 0);
        } catch (SecurityException e) {
            throw new NonPermesso("setStreamVolume rifiutato");
        }
    }

    @Override
    public int volumeTrova(int massimo) {
        int p = volumeTrovaProva;
        return p >= 0 ? Math.min(p, massimo) : massimo;
    }

    // ---- non disturbare ----

    /**
     * Si legge l'impostazione globale `zen_mode` con `cmd settings get global zen_mode` (la stessa su cui si
     * basano i ripristini del guardiano). Misurato sul telefono di prova: `NotificationManager` dal contesto di
     * sistema della shell non si crea (NullPointerException in Handler: nessun Looper principale) e
     * `Settings.Global.getInt` e' rifiutato (pacchetto «android» che non corrisponde all'uid 2000).
     * zen_mode: 0 spento, 1 solo priorita', 2 nessuna interruzione, 3 solo sveglie.
     */
    @Override
    public int filtroNonDisturbare() throws Exception {
        String o = ShellControlli.esegui(5, "cmd", "settings", "get", "global", "zen_mode").trim();
        if (!o.matches("[0-3]")) throw new IOException("non disturbare illeggibile");
        return Integer.parseInt(o) + 1;
    }

    @Override
    public void impostaFiltroNonDisturbare(int filtro) throws Exception {
        String arg = ComandiShell.argomentoFiltro(filtro);
        String o = ShellControlli.esegui(10, "cmd", "notification", "set_dnd", arg);
        if (o.contains("SecurityException")) throw new NonPermesso("set_dnd rifiutato");
        if (o.contains("Exception") || o.contains("Error")) throw new IOException("set_dnd non riuscito");
    }

    // ---- torcia ----

    /** Prima fotocamera con flash; registra la callback (una volta) che tiene lo stato aggiornato. */
    private String preparaTorcia() throws Exception {
        synchronized (serraturaTorcia) {
            if (torciaCercata) return idTorcia;
            torciaCercata = true;
            CameraManager cm = (CameraManager) NascosteControlli.servizio(Context.CAMERA_SERVICE);
            if (cm == null) return null;
            try {
                for (String id : cm.getCameraIdList()) {
                    Boolean flash = cm.getCameraCharacteristics(id).get(CameraCharacteristics.FLASH_INFO_AVAILABLE);
                    if (flash != null && flash) {
                        camera = cm;
                        idTorcia = id;
                        break;
                    }
                }
            } catch (CameraAccessException e) {
                torciaCercata = false;
                throw new IOException("fotocamere illeggibili: " + e.getMessage());
            }
            if (idTorcia == null) return null;
            final String mio = idTorcia;
            cm.registerTorchCallback(new Executor() {
                @Override
                public void execute(Runnable r) {
                    r.run();
                }
            }, new CameraManager.TorchCallback() {
                @Override
                public void onTorchModeChanged(String id, boolean abilitata) {
                    if (!id.equals(mio)) return;
                    synchronized (serraturaTorcia) {
                        torciaAccesa = abilitata;
                        statoTorciaNoto = true;
                    }
                    primoStatoTorcia.countDown();
                }
            });
            return idTorcia;
        }
    }

    @Override
    public boolean haTorcia() {
        try {
            return preparaTorcia() != null;
        } catch (Throwable e) {
            return false;
        }
    }

    @Override
    public int torcia() throws Exception {
        if (preparaTorcia() == null) throw new IOException("nessuna torcia");
        if (!primoStatoTorcia.await(ATTESA_TORCIA_S, TimeUnit.SECONDS)) throw new IOException("stato della torcia non noto");
        synchronized (serraturaTorcia) {
            if (!statoTorciaNoto) throw new IOException("stato della torcia non noto");
            return torciaAccesa ? 1 : 0;
        }
    }

    @Override
    public void impostaTorcia(boolean accesa) throws Exception {
        String id = preparaTorcia();
        if (id == null) throw new NonPermesso("nessuna fotocamera con flash");
        CameraManager cm;
        synchronized (serraturaTorcia) {
            cm = camera;
        }
        try {
            cm.setTorchMode(id, accesa);
        } catch (SecurityException e) {
            throw new NonPermesso("setTorchMode rifiutato");
        } catch (CameraAccessException e) {
            throw new IOException("torcia non raggiungibile: " + e.getMessage());
        }
    }

    // ---- suono di «trova il telefono» ----

    private static final int FREQ_CAMPIONI = 22050;

    /**
     * Il suono: un tono generato (nessun file, nessun content provider, nessun MediaPlayer/Ringtone: misurato
     * che dal processo della shell MediaPlayer fa morire il processo per un'eccezione di sicurezza dei suoi
     * thread interni). Due note alternate in ciclo e una pausa, con uso NOTIFICATION_RINGTONE (NON catturato
     * dalla regola dell'audio, §2.5: suona sul telefono, non sul PC), volume del flusso suoneria.
     */
    private static short[] melodia() {
        int nota = FREQ_CAMPIONI * 150 / 1000;
        int pausa = FREQ_CAMPIONI * 700 / 1000;
        short[] b = new short[nota * 4 + pausa];
        double[] freq = {880, 1175, 880, 1175};
        for (int k = 0; k < 4; k++) {
            for (int n = 0; n < nota; n++) {
                double dissolvenza = Math.min(1.0, Math.min(n, nota - n) / 100.0); // niente scatti agli estremi
                b[k * nota + n] = (short) (Math.sin(2 * Math.PI * freq[k] * n / FREQ_CAMPIONI) * 24000 * dissolvenza);
            }
        }
        return b;
    }

    @Override
    public void avviaSuono(Fermata fermata) throws Exception {
        short[] pcm = melodia();
        AudioTrack tr = new AudioTrack.Builder()
            .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_NOTIFICATION_RINGTONE)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
            .setAudioFormat(new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(FREQ_CAMPIONI).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
            .setBufferSizeInBytes(pcm.length * 2)
            .setTransferMode(AudioTrack.MODE_STATIC)
            .build();
        boolean muto = suonoSilenziosoProva;
        Vibrator v = null;
        synchronized (serraturaSuono) {
            suono = tr;
        }
        try {
            // un AudioTrack statico nasce «senza dati» e diventa inizializzato solo dopo la write
            if (tr.getState() == AudioTrack.STATE_UNINITIALIZED) throw new IOException("il suono non si inizializza");
            if (tr.write(pcm, 0, pcm.length) != pcm.length) throw new IOException("suono non caricato");
            if (tr.getState() != AudioTrack.STATE_INITIALIZED) throw new IOException("il suono non si inizializza dopo il caricamento");
            if (tr.setLoopPoints(0, pcm.length, -1) != AudioTrack.SUCCESS) throw new IOException("ciclo del suono non impostato");
            tr.setVolume(muto ? 0f : 1f);
            if (fermata.richiesta()) throw new IllegalStateException("ricerca fermata prima di suonare");
            tr.play();
            if (tr.getPlayState() != AudioTrack.PLAYSTATE_PLAYING) throw new IOException("il suono non parte");
            if (!muto) {
                // la vibrazione e' un di piu': se non si puo' (contesto di sistema, pacchetto non della shell) il suono resta
                try {
                    VibratorManager vm = (VibratorManager) NascosteControlli.servizio(Context.VIBRATOR_MANAGER_SERVICE);
                    v = vm.getDefaultVibrator();
                    v.vibrate(VibrationEffect.createWaveform(new long[] {0, 800, 600}, 0));
                    synchronized (serraturaSuono) {
                        vibratore = v;
                    }
                } catch (Throwable e) {
                    System.out.println("[controlli] vibrazione non disponibile: " + e);
                }
            }
        } catch (Throwable e) {
            fermaSuono();
            if (e instanceof Exception) throw (Exception) e;
            throw new IOException("suono non avviato: " + e);
        }
    }

    @Override
    public void fermaSuono() {
        AudioTrack r;
        Vibrator v;
        synchronized (serraturaSuono) {
            r = suono;
            v = vibratore;
            suono = null;
            vibratore = null;
        }
        if (r != null) {
            try {
                r.stop();
            } catch (Throwable e) {
                System.out.println("[controlli] suono non fermato: " + e);
            }
            try {
                r.release();
            } catch (Throwable e) {
                System.out.println("[controlli] suono non rilasciato: " + e);
            }
        }
        if (v != null) {
            try {
                v.cancel();
            } catch (Throwable e) {
                System.out.println("[controlli] vibrazione non fermata: " + e);
            }
        }
    }

    // ---- guide dei comandi (senza eseguire nulla che cambi) ----

    private static boolean guidaContiene(String parola, String... comando) {
        try {
            return ShellControlli.esegui(10, comando).contains(parola);
        } catch (Throwable e) {
            return false;
        }
    }

    @Override
    public boolean nonDisturbareManualeSenzaScadenza() throws Exception {
        return AnalisiNonDisturbare.manualeSenzaScadenza(ShellControlli.esegui(20, "dumpsys", "notification", "--zen"));
    }

    @Override
    public boolean haComandoNonDisturbare() {
        Boolean b = comandoNonDisturbare;
        if (b == null) {
            b = guidaContiene("set_dnd", "cmd", "notification", "help");
            comandoNonDisturbare = b;
        }
        return b;
    }

    @Override
    public boolean haComandiRipristino() {
        Boolean b = comandiRipristino;
        if (b == null) {
            b = haComandoNonDisturbare()
                && guidaContiene("volume", "cmd", "media_session", "help")
                && guidaContiene("set-ringer-mode", "cmd", "audio");
            comandiRipristino = b;
        }
        return b;
    }
}
