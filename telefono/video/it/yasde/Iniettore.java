package it.yasde;

import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;

/**
 * Costruisce e inietta gli eventi. Va chiamato sempre dallo stesso thread (quello di iniezione
 * di Input): lo stato del gesto in corso non e' protetto.
 */
final class Iniettore {
    private final Gesti gesti = new Gesti(); // stato dei gesti per sessione (logica senza Android)

    private static MotionEvent.PointerProperties[] proprieta(int tipoStrumento) {
        MotionEvent.PointerProperties p = new MotionEvent.PointerProperties();
        p.id = 0;
        p.toolType = tipoStrumento;
        return new MotionEvent.PointerProperties[] {p};
    }

    private static int limita(int v, int max) {
        return v < 0 ? 0 : (v > max - 1 ? max - 1 : v);
    }

    private static void manda(MotionEvent e, int schermo) {
        try {
            NascosteVideo.schermoEvento(e, schermo);
            NascosteVideo.inietta(e);
        } finally {
            e.recycle();
        }
    }

    /** Coordinate del PC (misure della sessione) -> schermo di destinazione (per lo specchio). */
    private static int scala(int v, int da, int a) {
        return da == a ? v : (int) ((long) v * a / da);
    }

    /** Un dito (sinistro). */
    void tocco(Sessione s, int azione, int x, int y) {
        tocco(s, azione, x, y, 1);
    }

    /**
     * azione: 0 giu', 1 muovi, 2 su, 3 annulla. Il sinistro e' un dito; con il destro o il centrale alla
     * pressione l'intero gesto e' un vero mouse (vedi Gesti.pulsanteDi).
     */
    void tocco(Sessione s, int azione, int x, int y, int pulsanti) {
        int w = s.larghezza(), h = s.altezza();
        if (s.specchio()) {
            int[] r = Specchio.misureReali();
            x = scala(x, w, r[0]);
            y = scala(y, h, r[1]);
            w = r[0];
            h = r[1];
        }
        esegui(gesti.tocco(s, azione, limita(x, w), limita(y, h), pulsanti, adesso()));
    }

    private static long adesso() {
        return android.os.SystemClock.uptimeMillis();
    }

    /**
     * PIZZICO (docs/protocollo.md §2.4): fase 0 inizio, 1 continua, 2 fine, 3 annulla; (x, y) e' il centro
     * nella misura del video; passo in 120esimi (positivo = ingrandisci). Le misure le ha gia' controllate
     * Input; qui si scalano sullo schermo di destinazione come per TOCCO.
     */
    void pizzico(Sessione s, int fase, int x, int y, int passo) {
        int w = s.larghezza(), h = s.altezza();
        if (s.specchio()) {
            int[] r = Specchio.misureReali();
            x = scala(x, w, r[0]);
            y = scala(y, h, r[1]);
            w = r[0];
            h = r[1];
        }
        esegui(gesti.pizzicoFase(s, fase, w, h, x, y, passo, adesso()));
    }

    /** Milisecondi fino alla prossima scadenza del pizzico (movimento in coda o chiusura), -1 se nessuna. */
    long attesaPizzico() {
        long t = gesti.prossimaScadenza();
        return t < 0 ? -1 : Math.max(0, t - adesso());
    }

    /** Da chiamare dopo ogni attesa del thread di iniezione: movimenti scaduti e chiusura dopo 400 ms. */
    void scadenzePizzico() {
        esegui(gesti.scadenze(adesso()));
    }

    /** Chiusura, pausa o cambio di misura di `s`: annulla ogni suo gesto (dito e pizzico). */
    void chiudiGestiDi(Sessione s) {
        esegui(gesti.chiudiDi(s, adesso()));
    }

    /** Fine del canale input o errore del thread: nessun dito resta giu'. */
    void chiudiTutto() {
        esegui(gesti.chiudiTutto(adesso()));
    }

    /** Inietta le azioni decise da Gesti; se una fallisce le altre si provano comunque. */
    private void esegui(java.util.List<Gesti.Cmd> azioni) {
        RuntimeException primo = null;
        for (Gesti.Cmd c : azioni) {
            try {
                Sessione s = (Sessione) c.sessione;
                if (c.tipo == Gesti.PIZZICO) {
                    eventiPizzico(s, c.mosse);
                    continue;
                }
                if (c.pulsante != Gesti.DITO) {
                    RuntimeException er = mouse(s, c);
                    if (er != null) throw er;
                    continue;
                }
                int az = azioneDi(c.tipo);
                MotionEvent e = evento(c, az, MotionEvent.TOOL_TYPE_FINGER, InputDevice.SOURCE_TOUCHSCREEN, 0);
                manda(e, s.schermoInput());
            } catch (RuntimeException e) {
                if (primo == null) primo = e;
            }
        }
        if (primo != null) throw primo;
    }

    private static int azioneDi(int tipo) {
        return tipo == Gesti.TOCCO_GIU ? MotionEvent.ACTION_DOWN
                : tipo == Gesti.TOCCO_MUOVI ? MotionEvent.ACTION_MOVE
                : tipo == Gesti.TOCCO_SU ? MotionEvent.ACTION_UP : MotionEvent.ACTION_CANCEL;
    }

    private static MotionEvent evento(Gesti.Cmd c, int az, int strumento, int sorgente, int statoPulsanti) {
        MotionEvent.PointerCoords pc = new MotionEvent.PointerCoords();
        pc.x = c.x;
        pc.y = c.y;
        pc.pressure = az == MotionEvent.ACTION_UP ? 0f : 1f;
        pc.size = 1f;
        return MotionEvent.obtain(c.downTime, c.tempo, az, 1, proprieta(strumento),
                new MotionEvent.PointerCoords[] {pc}, 0, statoPulsanti, 1f, 1f, 0, 0, sorgente, 0);
    }

    private static boolean pulsanteAvvisato;

    /** Un evento BUTTON_PRESS/RELEASE; senza setActionButton si salta (una riga di log, una volta sola). */
    private static void eventoPulsante(Gesti.Cmd c, int az, int pulsante, int stato, int schermo) {
        MotionEvent e = evento(c, az, MotionEvent.TOOL_TYPE_MOUSE, InputDevice.SOURCE_MOUSE, stato);
        if (!NascosteVideo.impostaPulsanteAzione(e, pulsante)) {
            e.recycle();
            if (!pulsanteAvvisato) {
                pulsanteAvvisato = true;
                System.out.println("[input] MotionEvent.setActionButton non disponibile: niente BUTTON_PRESS/RELEASE");
            }
            return;
        }
        manda(e, schermo);
    }

    static {
        // Gesti usa costanti proprie (niente Android): devono coincidere con quelle di MotionEvent.
        if (Gesti.A_DOWN != MotionEvent.ACTION_DOWN || Gesti.A_UP != MotionEvent.ACTION_UP
                || Gesti.A_MOVE != MotionEvent.ACTION_MOVE || Gesti.A_CANCEL != MotionEvent.ACTION_CANCEL
                || Gesti.A_BUTTON_PRESS != MotionEvent.ACTION_BUTTON_PRESS
                || Gesti.A_BUTTON_RELEASE != MotionEvent.ACTION_BUTTON_RELEASE
                || Gesti.DESTRO != MotionEvent.BUTTON_SECONDARY || Gesti.CENTRALE != MotionEvent.BUTTON_TERTIARY) {
            throw new IllegalStateException("costanti di Gesti diverse da quelle di MotionEvent");
        }
    }

    /**
     * Gesto di un vero mouse col destro o il centrale: traduce in MotionEvent gli eventi che sceglie
     * Gesti.eventiMouse. Ogni evento si prova anche se uno prima e' fallito: il rilascio non deve mai
     * restare indietro. Torna la prima eccezione (o null).
     */
    private RuntimeException mouse(Sessione s, Gesti.Cmd c) {
        int schermo = s.schermoInput();
        RuntimeException primo = null;
        for (Gesti.Ev ev : Gesti.eventiMouse(c.tipo, c.pulsante)) {
            try {
                if (ev.azione == MotionEvent.ACTION_BUTTON_PRESS || ev.azione == MotionEvent.ACTION_BUTTON_RELEASE) {
                    eventoPulsante(c, ev.azione, ev.pulsanteAzione, ev.stato, schermo);
                } else {
                    manda(evento(c, ev.azione, MotionEvent.TOOL_TYPE_MOUSE, InputDevice.SOURCE_MOUSE, ev.stato), schermo);
                }
            } catch (RuntimeException e) {
                if (primo == null) primo = e;
            }
        }
        return primo;
    }

    private void eventiPizzico(Sessione s, java.util.List<Pizzico.Mossa> mosse) {
        for (Pizzico.Mossa m : mosse) {
            int n = m.dita;
            MotionEvent.PointerProperties[] pp = new MotionEvent.PointerProperties[n];
            MotionEvent.PointerCoords[] pc = new MotionEvent.PointerCoords[n];
            for (int i = 0; i < n; i++) {
                pp[i] = new MotionEvent.PointerProperties();
                pp[i].id = i; // identificativi stabili per tutto il gesto
                pp[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
                pc[i] = new MotionEvent.PointerCoords();
                pc[i].x = i == 0 ? m.x0 : m.x1;
                pc[i].y = i == 0 ? m.y0 : m.y1;
                pc[i].pressure = 1f;
                pc[i].size = 0.05f;
                pc[i].touchMajor = 40f;
                pc[i].touchMinor = 40f;
            }
            int azione;
            switch (m.tipo) {
                case Pizzico.GIU:
                    azione = MotionEvent.ACTION_DOWN;
                    break;
                case Pizzico.DITO2_GIU:
                    azione = MotionEvent.ACTION_POINTER_DOWN | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT);
                    break;
                case Pizzico.MUOVI:
                    azione = MotionEvent.ACTION_MOVE;
                    break;
                case Pizzico.DITO2_SU:
                    azione = MotionEvent.ACTION_POINTER_UP | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT);
                    break;
                case Pizzico.SU:
                    azione = MotionEvent.ACTION_UP;
                    break;
                default:
                    azione = MotionEvent.ACTION_CANCEL;
                    break;
            }
            MotionEvent e = MotionEvent.obtain(m.downTime, m.tempo, azione, n, pp, pc, 0, 0, 1f, 1f, 0, 0,
                    InputDevice.SOURCE_TOUCHSCREEN, 0);
            manda(e, s.schermoInput());
        }
    }

    /** Rotella in 120esimi di scatto; ACTION_SCROLL con sorgente mouse. */
    void rotella(Sessione s, int x, int y, int orizzontale, int verticale) {
        int schermo = s.schermoInput();
        int w = s.larghezza(), h = s.altezza();
        if (s.specchio()) {
            int[] r = Specchio.misureReali();
            x = scala(x, w, r[0]);
            y = scala(y, h, r[1]);
            w = r[0];
            h = r[1];
        }
        MotionEvent.PointerCoords c = new MotionEvent.PointerCoords();
        c.x = limita(x, w);
        c.y = limita(y, h);
        // Verticale positivo = rotella girata in avanti (lontano dall'utente): il contenuto torna
        // verso l'inizio della pagina. E' lo stesso verso di AXIS_VSCROLL positivo (provato sul
        // telefono), quindi nessuna inversione.
        c.setAxisValue(MotionEvent.AXIS_HSCROLL, orizzontale / 120f);
        c.setAxisValue(MotionEvent.AXIS_VSCROLL, verticale / 120f);
        long ora = android.os.SystemClock.uptimeMillis();
        MotionEvent e = MotionEvent.obtain(ora, ora, MotionEvent.ACTION_SCROLL, 1,
                proprieta(MotionEvent.TOOL_TYPE_MOUSE), new MotionEvent.PointerCoords[] {c}, 0, 0, 1f, 1f, 0, 0,
                InputDevice.SOURCE_MOUSE, 0);
        manda(e, schermo);
    }

    private static void tastoSuSchermo(KeyEvent e, int schermo) {
        NascosteVideo.schermoEvento(e, schermo);
        NascosteVideo.inietta(e);
    }

    /** azione: 0 giu', 1 su. */
    void tasto(Sessione s, int azione, int codice, int modificatori) {
        long ora = android.os.SystemClock.uptimeMillis();
        KeyEvent e = new KeyEvent(ora, ora, azione == 0 ? KeyEvent.ACTION_DOWN : KeyEvent.ACTION_UP, codice, 0,
                modificatori, KeyCharacterMap.VIRTUAL_KEYBOARD, 0, 0, InputDevice.SOURCE_KEYBOARD);
        tastoSuSchermo(e, s.schermoInput());
    }

    void indietro(Sessione s) {
        tasto(s, 0, KeyEvent.KEYCODE_BACK, 0);
        tasto(s, 1, KeyEvent.KEYCODE_BACK, 0);
    }

    /** Testo come tasti virtuali; i caratteri che la mappa non sa produrre si saltano (con una riga di log). */
    void testo(Sessione s, String testo) {
        KeyCharacterMap mappa = KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD);
        int schermo = s.schermoInput();
        for (int i = 0; i < testo.length(); i++) {
            KeyEvent[] ev = mappa.getEvents(new char[] {testo.charAt(i)});
            if (ev == null) {
                System.out.println("[input] carattere non inseribile come tasto: U+"
                        + Integer.toHexString(testo.charAt(i)));
                continue;
            }
            for (KeyEvent e : ev) tastoSuSchermo(e, schermo);
        }
    }
}
