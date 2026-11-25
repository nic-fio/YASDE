package it.yasde;

import java.io.FileOutputStream;
import java.io.IOException;

/**
 * Prova isolata, senza servizio:
 * CLASSPATH=/data/local/tmp/prova-video.jar app_process / it.yasde.ProvaVideo [pacchetto] [secondi]
 * Apre una sessione, scrive l'H.264 grezzo in /data/local/tmp/prova-video.h264, inietta un tocco e
 * uno scorrimento, prova fotogramma chiave e ridimensionamento, chiude e stampa un riepilogo.
 */
public final class ProvaVideo {
    private static final String FILE = "/data/local/tmp/prova-video.h264";

    private static final class Conti implements Codificatore.Uscita {
        final FileOutputStream file;
        final long t0 = System.nanoTime();
        volatile long pacchetti, byteTot, chiavi, configurazioni;
        volatile long primaConfigMs = -1, primaChiaveMs = -1;
        volatile int l, a;

        Conti(FileOutputStream f) {
            file = f;
        }

        @Override
        public synchronized boolean pacchetto(int genere, int segni, int larg, int alt, long us, byte[] dati)
                throws IOException {
            long ms = (System.nanoTime() - t0) / 1000000;
            if (genere == Codificatore.CONFIGURAZIONE) {
                configurazioni++;
                if (primaConfigMs < 0) primaConfigMs = ms;
            } else if ((segni & 1) != 0) {
                chiavi++;
                if (primaChiaveMs < 0) primaChiaveMs = ms;
            }
            pacchetti++;
            byteTot += dati.length;
            l = larg;
            a = alt;
            file.write(dati);
            return true;
        }
    }

    private static void pausa(long ms) throws InterruptedException {
        Thread.sleep(ms);
    }

    public static void main(String[] args) throws Exception {
        String pacchetto = args.length > 0 ? args[0] : "com.android.settings";
        if (pacchetto.equals("specchio")) pacchetto = "";
        int secondi = args.length > 1 ? Integer.parseInt(args[1]) : 4;
        System.out.println("API mancanti: " + NascosteVideo.verifica());
        int dpi = NascosteVideo.contesto().getResources().getDisplayMetrics().densityDpi;
        long inizio = System.nanoTime();
        Sessione s = Video.apri(1, 1280, 720, dpi, 8000000, 60, pacchetto);
        System.out.println("sessione aperta in " + (System.nanoTime() - inizio) / 1000000 + " ms, dpi " + dpi
                + ", flag 0x" + Integer.toHexString(s.flagUsati()) + ", codificatore " + s.nomeCodificatore() + ", schermo " + s.schermoInput());
        Conti c;
        try (FileOutputStream f = new FileOutputStream(FILE)) {
            c = new Conti(f);
            s.collega(c, null);
            pausa(secondi * 1000L);
            Iniettore in = new Iniettore();
            System.out.println("tocco al centro");
            in.tocco(s, 0, 640, 360);
            pausa(60);
            in.tocco(s, 2, 640, 360);
            pausa(1000);
            System.out.println("scorrimento verso l'alto");
            in.tocco(s, 0, 640, 600);
            for (int i = 1; i <= 8; i++) {
                pausa(16);
                in.tocco(s, 1, 640, 600 - i * 50);
            }
            in.tocco(s, 2, 640, 200);
            pausa(1000);
            long chiaviPrima = c.chiavi;
            long t = System.nanoTime();
            s.chiave();
            for (int i = 0; i < 50 && c.chiavi == chiaviPrima; i++) pausa(20);
            System.out.println("fotogramma chiave a richiesta: " + (c.chiavi > chiaviPrima ? ((System.nanoTime() - t)
                    / 1000000 + " ms") : "NON arrivato"));
            t = System.nanoTime();
            int[] m = s.ridimensiona(960, 536, dpi);
            System.out.println("ridimensionato a " + m[0] + "x" + m[1] + " in " + (System.nanoTime() - t) / 1000000
                    + " ms");
            pausa(2000);
            System.out.println("ultime misure nei pacchetti: " + c.l + "x" + c.a);
            s.scollega(null);
            Video.chiudi(1, true);
        }
        System.out.println("RIEPILOGO pacchetti=" + c.pacchetti + " byte=" + c.byteTot + " chiavi=" + c.chiavi
                + " configurazioni=" + c.configurazioni + " primaConfig=" + c.primaConfigMs + "ms primaChiave="
                + c.primaChiaveMs + "ms file=" + FILE);
        System.exit(0);
    }
}
