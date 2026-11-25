package it.yasde;

import java.util.ArrayList;
import java.util.List;

/**
 * Stato dei gesti in corso (un dito del TOCCO e il PIZZICO) e regole tra sessioni, senza Android:
 * si prova sulla JVM. Dice QUALI eventi iniettare (e su quale sessione); Iniettore li trasforma in
 * MotionEvent. La sessione e' un oggetto qualsiasi (confrontato per identita').
 * Regole: vince l'ultimo gesto arrivato, anche di un'altra sessione (il vecchio si annulla sul SUO
 * schermo); muovi/su di un dito si accettano solo dalla sessione che l'ha messo giu'; la chiusura
 * di una sessione annulla ogni suo gesto. Stato del solo thread di iniezione.
 */
final class Gesti {
    static final int TOCCO_GIU = 0, TOCCO_MUOVI = 1, TOCCO_SU = 2, TOCCO_ANNULLA = 3, PIZZICO = 4;
    /** Pulsante di un gesto: 0 = dito; destro e centrale sono veri pulsanti del mouse (valori = bit 1 e 2 di `pulsanti`). */
    static final int DITO = 0, DESTRO = 2, CENTRALE = 4;

    /** Un'azione da iniettare sullo schermo di `sessione`. */
    static final class Cmd {
        final int tipo;
        final Object sessione;
        final long downTime, tempo;
        final int x, y;
        final int pulsante; // DITO, DESTRO o CENTRALE (solo per i gesti TOCCO)
        final List<Pizzico.Mossa> mosse;

        Cmd(int tipo, Object sessione, long downTime, long tempo, int x, int y, int pulsante, List<Pizzico.Mossa> mosse) {
            this.tipo = tipo;
            this.sessione = sessione;
            this.downTime = downTime;
            this.tempo = tempo;
            this.x = x;
            this.y = y;
            this.pulsante = pulsante;
            this.mosse = mosse;
        }
    }

    /**
     * Azioni di MotionEvent con valori propri (Gesti non usa Android); Iniettore controlla all'avvio che coincidano
     * con quelli di MotionEvent. BUTTON_SECONDARY/TERTIARY coincidono con DESTRO/CENTRALE.
     */
    static final int A_DOWN = 0, A_UP = 1, A_MOVE = 2, A_CANCEL = 3, A_BUTTON_PRESS = 11, A_BUTTON_RELEASE = 12;

    /** Un evento del gesto mouse: azione, buttonState e actionButton (0 se non e' BUTTON_PRESS/RELEASE). */
    static final class Ev {
        final int azione, stato, pulsanteAzione;

        Ev(int azione, int stato, int pulsanteAzione) {
            this.azione = azione;
            this.stato = stato;
            this.pulsanteAzione = pulsanteAzione;
        }
    }

    /**
     * Eventi, in ordine, di un passo del gesto mouse (`tipo` = TOCCO_*, `pulsante` = DESTRO o CENTRALE).
     * Giu': DOWN col pulsante, poi BUTTON_PRESS. Muovi: MOVE col pulsante tenuto. Su: BUTTON_RELEASE poi UP
     * a pulsante 0. Annulla: SOLO CANCEL a pulsante 0, come fa Android: un BUTTON_RELEASE e' un mouseup per
     * Chromium e un centrale annullato aprirebbe comunque il link.
     */
    static List<Ev> eventiMouse(int tipo, int pulsante) {
        List<Ev> out = new ArrayList<Ev>();
        switch (tipo) {
            case TOCCO_GIU:
                out.add(new Ev(A_DOWN, pulsante, 0));
                out.add(new Ev(A_BUTTON_PRESS, pulsante, pulsante));
                break;
            case TOCCO_MUOVI:
                out.add(new Ev(A_MOVE, pulsante, 0));
                break;
            case TOCCO_SU:
                out.add(new Ev(A_BUTTON_RELEASE, 0, pulsante));
                out.add(new Ev(A_UP, 0, 0));
                break;
            default:
                out.add(new Ev(A_CANCEL, 0, 0));
                break;
        }
        return out;
    }

    private final Pizzico pizzico = new Pizzico();
    private Object sessioneTocco, sessionePizzico;
    private boolean giu;
    private long tempoGiu;
    private int ultimoX, ultimoY;
    private int pulsanteGiu; // pulsante del gesto in corso, deciso alla pressione e valido fino al rilascio

    boolean pizzicoAperto() {
        return pizzico.aperto();
    }

    boolean ditoGiu() {
        return giu;
    }

    private Cmd annullaTocco(long ora) {
        giu = false;
        return new Cmd(TOCCO_ANNULLA, sessioneTocco, tempoGiu, ora, ultimoX, ultimoY, pulsanteGiu, null);
    }

    private void mossePizzico(List<Cmd> out, Object s, List<Pizzico.Mossa> m) {
        if (!m.isEmpty()) out.add(new Cmd(PIZZICO, s, 0, 0, 0, 0, DITO, m));
    }

    /** Pulsante del gesto dal campo `pulsanti` del TOCCO: vince il destro, poi il centrale; altro (anche bit sconosciuti) = dito. */
    static int pulsanteDi(int pulsanti) {
        if ((pulsanti & DESTRO) != 0) return DESTRO;
        if ((pulsanti & CENTRALE) != 0) return CENTRALE;
        return DITO;
    }

    /** Come sotto, con il sinistro (dito). */
    List<Cmd> tocco(Object s, int azione, int x, int y, long ora) {
        return tocco(s, azione, x, y, 1, ora);
    }

    /**
     * azione: 0 giu', 1 muovi, 2 su, 3 annulla; (x, y) gia' nelle misure dello schermo di destinazione.
     * `pulsanti` conta solo alla pressione: il tipo di gesto resta quello fino al rilascio (al rilascio il PC manda 0).
     */
    List<Cmd> tocco(Object s, int azione, int x, int y, int pulsanti, long ora) {
        List<Cmd> out = new ArrayList<Cmd>();
        switch (azione) {
            case TOCCO_GIU:
                if (pizzico.aperto()) mossePizzico(out, sessionePizzico, pizzico.annulla(ora));
                if (giu) out.add(annullaTocco(ora)); // il dito di un'altra sessione (o lo stesso, rimasto giu')
                giu = true;
                pulsanteGiu = pulsanteDi(pulsanti);
                sessioneTocco = s;
                tempoGiu = ora;
                ultimoX = x;
                ultimoY = y;
                out.add(new Cmd(TOCCO_GIU, s, ora, ora, x, y, pulsanteGiu, null));
                break;
            case TOCCO_MUOVI:
            case TOCCO_SU:
            case TOCCO_ANNULLA:
                if (!giu || sessioneTocco != s) return out;
                ultimoX = x;
                ultimoY = y;
                if (azione != TOCCO_MUOVI) giu = false;
                out.add(new Cmd(azione, s, tempoGiu, ora, x, y, pulsanteGiu, null));
                break;
            default:
                break;
        }
        return out;
    }

    List<Cmd> pizzicoFase(Object s, int fase, int w, int h, int x, int y, int passo, long ora) {
        List<Cmd> out = new ArrayList<Cmd>();
        switch (fase) {
            case 0:
                if (giu) out.add(annullaTocco(ora)); // vince l'ultimo arrivato: il dito, di qualunque sessione, si annulla
                if (pizzico.aperto() && sessionePizzico != s) mossePizzico(out, sessionePizzico, pizzico.fine(ora));
                sessionePizzico = s;
                mossePizzico(out, s, pizzico.inizio(ora, w, h, x, y, passo));
                break;
            case 1:
                if (sessionePizzico == s) mossePizzico(out, s, pizzico.continua(ora, x, y, passo));
                break;
            case 2:
                if (sessionePizzico == s) mossePizzico(out, s, pizzico.fine(ora));
                break;
            case 3:
                if (sessionePizzico == s) mossePizzico(out, s, pizzico.annulla(ora));
                break;
            default:
                break;
        }
        return out;
    }

    /** Prossima scadenza del pizzico (tempo assoluto) o -1. */
    long prossimaScadenza() {
        return pizzico.prossimaScadenza();
    }

    List<Cmd> scadenze(long ora) {
        List<Cmd> out = new ArrayList<Cmd>();
        if (pizzico.aperto()) mossePizzico(out, sessionePizzico, pizzico.scadenze(ora));
        return out;
    }

    /** Chiusura, pausa o cambio di misura di `s`: ogni gesto di `s` (dito e pizzico) si annulla. */
    List<Cmd> chiudiDi(Object s, long ora) {
        List<Cmd> out = new ArrayList<Cmd>();
        if (giu && sessioneTocco == s) out.add(annullaTocco(ora));
        if (pizzico.aperto() && sessionePizzico == s) mossePizzico(out, s, pizzico.annulla(ora));
        return out;
    }

    /** Fine del canale input o errore del thread: nessun dito resta giu', di nessuna sessione. */
    List<Cmd> chiudiTutto(long ora) {
        List<Cmd> out = new ArrayList<Cmd>();
        if (giu) out.add(annullaTocco(ora));
        if (pizzico.aperto()) mossePizzico(out, sessionePizzico, pizzico.annulla(ora));
        return out;
    }
}
