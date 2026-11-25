package it.yasde;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/**
 * Geometria e macchina a stati del PIZZICO (docs/protocollo.md §2.4), senza Android: si prova sulla JVM.
 * Produce "mosse" (eventi delle due dita) con i loro tempi; chi la usa le trasforma in MotionEvent e
 * le inietta. Il tempo (in millisecondi) lo passa sempre il chiamante. Stato del solo thread di iniezione.
 */
final class Pizzico {
    static final int GIU = 0, DITO2_GIU = 1, MUOVI = 2, DITO2_SU = 3, SU = 4, ANNULLA = 5;

    static final long SCADENZA_MS = 400, INTERVALLO_MS = 8;
    static final int MAX_PER_MOVIMENTO = 30, LIMITE_PASSO = 1200;
    private static final double FATTORE = 1.25, INIZIALE = 0.25, MINIMA = 0.05, MASSIMA = 0.90;
    private static final double S_MIN = 120 * Math.log(MINIMA / INIZIALE) / Math.log(FATTORE);
    private static final double S_MAX = 120 * Math.log(MASSIMA / INIZIALE) / Math.log(FATTORE);

    /** Un evento da iniettare: `dita` puntatori (1 o 2), tipo, tempo, tempo di inizio del gesto. */
    static final class Mossa {
        final int tipo, dita, x0, y0, x1, y1;
        final long tempo, downTime;

        Mossa(int tipo, int dita, long tempo, long downTime, int x0, int y0, int x1, int y1) {
            this.tipo = tipo;
            this.dita = dita;
            this.tempo = tempo;
            this.downTime = downTime;
            this.x0 = x0;
            this.y0 = y0;
            this.x1 = x1;
            this.y1 = y1;
        }
    }

    private boolean aperto;
    private int w, h, cxRichiesto, cyRichiesto;
    private double lato, passiTotali, scostX, scostY;
    private long downTime, ultimoTempo = Long.MIN_VALUE / 2, ultimaAttivita;
    private final ArrayDeque<long[]> pendenti = new ArrayDeque<long[]>(); // {tempo, passo}

    boolean aperto() {
        return aperto;
    }

    /** Prossima scadenza (tempo assoluto) di un movimento in coda o della chiusura automatica; -1 se chiuso. */
    long prossimaScadenza() {
        if (!aperto) return -1;
        long t = ultimaAttivita + SCADENZA_MS;
        long[] p = pendenti.peekFirst();
        return p != null ? Math.min(t, p[0]) : t;
    }

    private long tempo(long ora) {
        ultimoTempo = Math.max(ora, ultimoTempo + 1);
        return ultimoTempo;
    }

    private static int limita(int v, int max) {
        return v < 0 ? 0 : (v > max - 1 ? max - 1 : v);
    }

    private Mossa posizione(int tipo, int dita, long ora) {
        double d = lato * INIZIALE * Math.pow(FATTORE, passiTotali / 120.0);
        double mezza = d / 2;
        double cx = cxRichiesto + scostX, cy = cyRichiesto + scostY;
        double minX = mezza, maxX = (w - 1) - mezza;
        if (cx < minX) cx = minX;
        if (cx > maxX) cx = maxX;
        cy = Math.max(0, Math.min(h - 1, cy));
        scostX = cx - cxRichiesto; // lo spostamento verso l'interno resta per il resto del gesto
        scostY = cy - cyRichiesto;
        int x0 = limita((int) Math.round(cx - mezza), w), x1 = limita((int) Math.round(cx + mezza), w);
        int y = limita((int) Math.round(cy), h);
        return new Mossa(tipo, dita, tempo(ora), downTime, x0, y, x1, y);
    }

    private void applicaPasso(int passo) {
        passiTotali = Math.max(S_MIN, Math.min(S_MAX, passiTotali + passo)); // niente accumulo oltre i limiti
    }

    /** Il gesto che c'e' (se c'e') si chiude con le dita alzate. */
    List<Mossa> fine(long ora) {
        List<Mossa> out = new ArrayList<Mossa>();
        if (!aperto) return out;
        boolean mosso = false;
        while (!pendenti.isEmpty()) { // i movimenti in coda si riassumono in uno solo
            applicaPasso((int) pendenti.pollFirst()[1]);
            mosso = true;
        }
        if (mosso) out.add(posizione(MUOVI, 2, ora));
        out.add(posizione(DITO2_SU, 2, ora));
        out.add(posizione(SU, 1, ora));
        aperto = false;
        return out;
    }

    List<Mossa> annulla(long ora) {
        List<Mossa> out = new ArrayList<Mossa>();
        if (!aperto) return out;
        pendenti.clear();
        out.add(posizione(ANNULLA, 2, ora));
        aperto = false;
        return out;
    }

    private void pianifica(long ora, int passo) {
        if (passo == 0) return;
        int n = (Math.abs(passo) + MAX_PER_MOVIMENTO - 1) / MAX_PER_MOVIMENTO;
        long t = ora;
        long[] ultimo = pendenti.peekLast();
        if (ultimo != null) t = Math.max(t, ultimo[0] + INTERVALLO_MS);
        for (int i = 0; i < n; i++) {
            int parte = (int) ((long) passo * (i + 1) / n - (long) passo * i / n);
            pendenti.addLast(new long[] {t + INTERVALLO_MS * i, parte});
        }
    }

    /** I movimenti in coda scaduti e, se e' passato troppo tempo senza passi, la chiusura. */
    List<Mossa> scadenze(long ora) {
        List<Mossa> out = new ArrayList<Mossa>();
        if (!aperto) return out;
        while (!pendenti.isEmpty() && pendenti.peekFirst()[0] <= ora) {
            applicaPasso((int) pendenti.pollFirst()[1]);
            out.add(posizione(MUOVI, 2, ora));
        }
        if (aperto && pendenti.isEmpty() && ora - ultimaAttivita >= SCADENZA_MS) out.addAll(fine(ora));
        return out;
    }

    /**
     * Fase 0. `w`, `h`: misure dello schermo su cui si inietta; (x, y): centro nelle stesse misure.
     * Un gesto gia' aperto si chiude prima. Fuori misura: nessuna mossa.
     */
    List<Mossa> inizio(long ora, int w, int h, int x, int y, int passo) {
        List<Mossa> out = new ArrayList<Mossa>();
        if (aperto) out.addAll(fine(ora));
        if (w < 2 || h < 2 || x < 0 || y < 0 || x >= w || y >= h) return out;
        this.w = w;
        this.h = h;
        lato = Math.min(w, h);
        cxRichiesto = x;
        cyRichiesto = y;
        passiTotali = 0;
        scostX = 0;
        scostY = 0;
        pendenti.clear();
        aperto = true;
        ultimaAttivita = ora;
        downTime = tempo(ora);
        ultimoTempo = downTime - 1; // GIU porta proprio downTime
        out.add(posizione(GIU, 1, ora));
        out.add(posizione(DITO2_GIU, 2, ora));
        pianifica(ora, Math.max(-LIMITE_PASSO, Math.min(LIMITE_PASSO, passo)));
        out.addAll(scadenze(ora));
        return out;
    }

    /** Fase 1: un nuovo centro (stesse misure) e un passo. Senza gesto aperto o fuori misura: nulla. */
    List<Mossa> continua(long ora, int x, int y, int passo) {
        List<Mossa> out = new ArrayList<Mossa>();
        if (!aperto || x < 0 || y < 0 || x >= w || y >= h) return out;
        boolean centro = x != cxRichiesto || y != cyRichiesto;
        cxRichiesto = x;
        cyRichiesto = y;
        ultimaAttivita = ora;
        int p = Math.max(-LIMITE_PASSO, Math.min(LIMITE_PASSO, passo));
        pianifica(ora, p);
        List<Mossa> subito = scadenze(ora);
        if (subito.isEmpty() && centro) out.add(posizione(MUOVI, 2, ora));
        out.addAll(subito);
        return out;
    }
}
