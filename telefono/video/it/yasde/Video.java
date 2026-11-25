package it.yasde;

import android.net.LocalSocket;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/** Canale `video:<id>` e comandi 0x40-0x4f (docs/protocollo.md §2.2 e §2.3). */
public final class Video {
    private static final int APRI = 0x40, CHIUDI = 0x41, RIDIMENSIONA = 0x42, CHIAVE = 0x43, PAUSA = 0x44, RIPRENDI = 0x45;

    private static final Map<Integer, Sessione> sessioni = new ConcurrentHashMap<>();

    private Video() {}

    public static void registra() {
        List<String> mancano = NascosteVideo.verifica();
        if (mancano.isEmpty()) {
            System.out.println("[video] API di Android necessarie: tutte presenti");
        } else {
            for (String m : mancano) System.out.println("[video] API MANCANTE: " + m);
        }
        Registro.registraComandi(0x40, 0x4f, new GestoreComandi() {
            @Override
            public byte[] gestisci(int tipo, DataInputStream corpo) throws Exception {
                return comando(tipo, corpo);
            }
        });
        Registro.registraChiusura(new Runnable() {
            @Override
            public void run() {
                chiudiTutte();
            }
        });
        Registro.registraCanale("video", new GestoreCanale() {
            @Override
            public void gestisci(LocalSocket s, String argomento) throws Exception {
                canale(s, argomento);
            }
        });
    }

    /**
     * Per il pezzo degli appunti (e chiunque debba iniettare input in una sessione): l'id dello
     * schermo su cui iniettare per la sessione `idSessione` (come `schermoInput()`; 0 per lo
     * specchio), oppure -1 se la sessione non esiste o e' chiusa. Sicuro da qualunque thread.
     */
    public static int schermoDi(int idSessione) {
        Sessione s = sessioni.get(idSessione);
        return s == null || s.chiusa() ? -1 : s.schermoInput();
    }

    static Sessione sessione(int id) {
        return sessioni.get(id);
    }

    private static void controllaMisure(int l, int a, int dpi) {
        if (l < 64 || l > 2560 || a < 64 || a > 2560 || l % 8 != 0 || a % 8 != 0) {
            throw new IllegalArgumentException("misure non valide (multipli di 8 tra 64 e 2560)");
        }
        if (dpi < 40 || dpi > 1000) throw new IllegalArgumentException("densita non valida");
    }

    /** Apre una sessione e la registra (usato anche da ProvaVideo). */
    static Sessione apri(int id, int l, int a, int dpi, long bps, int fps, String pacchetto) throws Exception {
        controllaMisure(l, a, dpi);
        if (!pacchetto.isEmpty() && !pacchetto.matches("[A-Za-z0-9_.]{1,255}")) {
            throw new IllegalArgumentException("nome pacchetto non valido");
        }
        if (bps < 100000 || bps > 200000000L) throw new IllegalArgumentException("bitrate non valido");
        if (fps < 1 || fps > 120) fps = 60;
        if (sessioni.containsKey(id)) throw new IllegalArgumentException("id sessione gia' in uso");
        Sessione s = Sessione.apri(id, l, a, dpi, (int) bps, fps, pacchetto);
        sessioni.put(id, s);
        return s;
    }

    static void chiudi(int id, boolean togliTask) {
        Sessione s = sessioni.remove(id);
        if (s == null) throw new IllegalArgumentException("sessione sconosciuta");
        s.chiudi(togliTask);
    }

    private static Sessione trova(int id) {
        Sessione s = sessioni.get(id);
        if (s == null) throw new IllegalArgumentException("sessione sconosciuta");
        return s;
    }

    private static byte[] comando(int tipo, DataInputStream in) throws Exception {
        switch (tipo) {
            case APRI: {
                int id = in.readInt();
                int l = in.readUnsignedShort(), a = in.readUnsignedShort(), dpi = in.readUnsignedShort();
                long bps = in.readInt() & 0xffffffffL;
                int fps = in.readUnsignedByte();
                String pacchetto = Binario.stringa(in);
                apri(id, l, a, dpi, bps, fps, pacchetto);
                return new byte[0];
            }
            case CHIUDI: {
                int id = in.readInt();
                boolean togli = in.readUnsignedByte() != 0;
                chiudi(id, togli);
                return new byte[0];
            }
            case RIDIMENSIONA: {
                Sessione s = trova(in.readInt());
                int l = in.readUnsignedShort(), a = in.readUnsignedShort(), dpi = in.readUnsignedShort();
                controllaMisure(l, a, dpi);
                int[] m = s.ridimensiona(l, a, dpi);
                ByteArrayOutputStream b = new ByteArrayOutputStream();
                DataOutputStream o = new DataOutputStream(b);
                o.writeShort(m[0]);
                o.writeShort(m[1]);
                return b.toByteArray();
            }
            case PAUSA:
                trova(in.readInt()).pausa();
                return new byte[0];
            case RIPRENDI:
                trova(in.readInt()).riprendi();
                return new byte[0];
            case CHIAVE:
                trova(in.readInt()).chiave();
                return new byte[0];
            default:
                throw new IllegalArgumentException("tipo sconosciuto");
        }
    }

    /** Il PC apre `video:<id>`: parte il flusso e si resta qui finche' il PC chiude. */
    private static void canale(LocalSocket s, String argomento) throws Exception {
        long id;
        try {
            id = Long.parseLong(argomento);
        } catch (NumberFormatException e) {
            throw new IllegalArgumentException("id del canale video non valido");
        }
        if (id < 0 || id > 0xffffffffL) throw new IllegalArgumentException("id del canale video non valido");
        final Sessione sess = trova((int) id);
        final OutputStream out = s.getOutputStream();
        sess.collega(new Codificatore.Uscita() {
            @Override
            public boolean pacchetto(int genere, int segni, int l, int a, long us, byte[] dati) throws IOException {
                Sessione.scriviPacchetto(out, genere, segni, l, a, us, dati);
                return true;
            }
        }, s);
        try {
            InputStream in = s.getInputStream();
            byte[] buf = new byte[64];
            while (in.read(buf) >= 0) {
                // il PC non scrive mai su questo canale: si aspetta solo la chiusura
            }
        } finally {
            sess.scollega(s);
        }
    }

    /**
     * Chiude tutte le sessioni (chiusura del servizio, che concede 3 s in tutto): le chiusure vanno
     * in parallelo, ognuna sul suo thread, e si aspetta con un tetto complessivo di 2,5 s.
     */
    static void chiudiTutte() {
        List<Thread> fine = new ArrayList<>();
        for (Integer id : new ArrayList<>(sessioni.keySet())) {
            Sessione s = sessioni.remove(id);
            if (s == null) continue;
            Thread t = s.chiudi(true);
            if (t != null) fine.add(t);
        }
        long limite = System.currentTimeMillis() + 2500;
        for (Thread t : fine) {
            long resto = limite - System.currentTimeMillis();
            if (resto <= 0) break;
            try {
                t.join(resto);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
        }
    }
}
