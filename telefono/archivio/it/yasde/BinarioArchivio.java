package it.yasde;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.OutputStream;

/** Lettura e scrittura di stringhe e messaggi del protocollo, privata di questo pezzo. */
final class BinarioArchivio {
    private BinarioArchivio() {}

    /** Stringa u16 + UTF-8; se supera `massimoByte` e' un errore (le lunghezze arrivano dal PC). */
    static String stringa(DataInputStream in, int massimoByte) throws IOException {
        int n = in.readUnsignedShort();
        if (n > massimoByte) throw new IllegalArgumentException("testo troppo lungo");
        byte[] b = new byte[n];
        in.readFully(b);
        return new String(b, "UTF-8");
    }

    /** Un messaggio completo (tipo, lunghezza, corpo) con una sola write. */
    static void scrivi(OutputStream out, int tipo, byte[] corpo) throws IOException {
        byte[] m = new byte[5 + corpo.length];
        m[0] = (byte) tipo;
        m[1] = (byte) (corpo.length >>> 24);
        m[2] = (byte) (corpo.length >>> 16);
        m[3] = (byte) (corpo.length >>> 8);
        m[4] = (byte) corpo.length;
        System.arraycopy(corpo, 0, m, 5, corpo.length);
        out.write(m);
        out.flush();
    }

    /** Corpo di TROVATO. */
    static byte[] trovato(String percorso, boolean cartella) throws IOException {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        DataOutputStream o = new DataOutputStream(bo);
        byte[] p = percorso.getBytes("UTF-8");
        if (p.length > 0xffff) throw new IOException("percorso troppo lungo");
        o.writeShort(p.length);
        o.write(p);
        o.writeByte(cartella ? 1 : 0);
        return bo.toByteArray();
    }
}
