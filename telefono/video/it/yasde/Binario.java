package it.yasde;

import java.io.DataInputStream;
import java.io.IOException;

/** Lettura di stringhe del protocollo (u16 + UTF-8), privata di questo pezzo. */
final class Binario {
    private Binario() {}

    static String stringa(DataInputStream in) throws IOException {
        int n = in.readUnsignedShort();
        byte[] b = new byte[n];
        in.readFully(b);
        return new String(b, "UTF-8");
    }
}
