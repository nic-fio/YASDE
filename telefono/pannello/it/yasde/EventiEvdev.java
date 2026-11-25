package it.yasde;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Lettura degli eventi dei dispositivi di input del kernel (struct input_event): parte pura, senza Android,
 * cosi' si prova sulla JVM. `dim` = 24 byte sui telefoni a 64 bit (timeval di due long), 16 a 32 bit; dopo il
 * tempo vengono type (u16), code (u16), value (s32), little endian.
 */
final class EventiEvdev {
    private static final int EV_KEY = 1;
    private static final int EV_ABS = 3;
    private static final int ABS_MT_TRACKING_ID = 0x39;

    private EventiEvdev() {}

    /**
     * true se tra i primi `n` byte c'e' la pressione di un tasto o l'inizio di un dito (le ripetizioni dei
     * tasti, valore 2, i rilasci, le coordinate e i segnali di sincronia non contano). Una coda incompleta
     * (n non multiplo di `dim`) si ignora.
     */
    static boolean attivita(byte[] buf, int n, int dim) {
        ByteBuffer b = ByteBuffer.wrap(buf, 0, Math.min(n, buf.length) / dim * dim).order(ByteOrder.LITTLE_ENDIAN);
        for (int o = 0; o + dim <= b.limit(); o += dim) {
            int tipo = b.getShort(o + dim - 8) & 0xffff;
            int codice = b.getShort(o + dim - 6) & 0xffff;
            int valore = b.getInt(o + dim - 4);
            if (tipo == EV_KEY && valore == 1) return true;
            if (tipo == EV_ABS && codice == ABS_MT_TRACKING_ID && valore >= 0) return true;
        }
        return false;
    }
}
