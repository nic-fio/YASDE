package it.yasde;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.Charset;

/**
 * Parte pura del protocollo (docs/protocollo.md §2): costanti dei tipi e lettura/scrittura
 * di messaggi e stringhe. Usa solo java.io, così si prova sulla JVM normale.
 */
public final class Protocollo {
    public static final int VERSIONE = 1;
    /** Massimo del corpo di un messaggio. */
    public static final int MAX_CORPO = 1 << 20;

    // Generali
    public static final int SALUTO = 0x01;
    public static final int OK = 0x02;
    public static final int ERRORE = 0x03;
    public static final int CHIUDI = 0x04;
    public static final int PRESENZA = 0x05;
    // Video
    public static final int VIDEO_APRI = 0x40;
    public static final int VIDEO_CHIUDI = 0x41;
    public static final int VIDEO_RIDIMENSIONA = 0x42;
    public static final int VIDEO_CHIAVE = 0x43;
    public static final int VIDEO_EVENTO = 0x48;
    // Input
    public static final int TOCCO = 0x50;
    public static final int ROTELLA = 0x51;
    public static final int TASTO = 0x52;
    public static final int TESTO = 0x53;
    public static final int INDIETRO = 0x54;

    private static final Charset UTF8 = Charset.forName("UTF-8");

    private Protocollo() {}

    /** Un messaggio letto: tipo e corpo. */
    public static final class Messaggio {
        public final int tipo;
        public final byte[] corpo;

        Messaggio(int tipo, byte[] corpo) {
            this.tipo = tipo;
            this.corpo = corpo;
        }
    }

    /**
     * Legge un messaggio. Ritorna null se il flusso finisce prima del tipo (chiusura pulita);
     * IOException se finisce a metà o se la lunghezza supera il limite.
     */
    public static Messaggio leggi(DataInputStream in) throws IOException {
        int tipo = in.read();
        if (tipo < 0) return null;
        int lunghezza = in.readInt();
        if (lunghezza < 0 || lunghezza > MAX_CORPO) {
            throw new IOException("lunghezza fuori limite: " + lunghezza);
        }
        byte[] corpo = new byte[lunghezza];
        in.readFully(corpo);
        return new Messaggio(tipo, corpo);
    }

    /** Scrive un messaggio con una sola write, così non si mescola con altri se il chiamante serializza. */
    public static void scrivi(OutputStream out, int tipo, byte[] corpo) throws IOException {
        if (corpo == null) corpo = new byte[0];
        if (corpo.length > MAX_CORPO) throw new IOException("corpo troppo grande: " + corpo.length);
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

    /** stringa = u16 lunghezza in byte + UTF-8, senza terminatore. */
    public static void scriviStringa(DataOutputStream out, String s) throws IOException {
        byte[] b = s.getBytes(UTF8);
        if (b.length > 0xffff) throw new IOException("stringa troppo lunga");
        out.writeShort(b.length);
        out.write(b);
    }

    public static String leggiStringa(DataInputStream in) throws IOException {
        int n = in.readUnsignedShort();
        byte[] b = new byte[n];
        in.readFully(b);
        return new String(b, UTF8);
    }

    /** Corpo di un ERRORE: una stringa. */
    public static byte[] corpoErrore(String frase) throws IOException {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        scriviStringa(new DataOutputStream(bo), frase);
        return bo.toByteArray();
    }
}
