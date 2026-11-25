package it.yasde;

import android.util.DisplayMetrics;
import android.view.Display;

import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

/**
 * Punto unico per le API nascoste di Android, usate per riflessione.
 * {@link #verifica()} all'avvio elenca quelle che mancano su questo telefono.
 */
public final class Nascoste {
    private static final String DMG = "android.hardware.display.DisplayManagerGlobal";

    private Nascoste() {}

    /** Nomi delle API nascoste mancanti (vuoto = tutte presenti). */
    public static List<String> verifica() {
        List<String> mancanti = new ArrayList<>();
        try {
            Class<?> c = Class.forName(DMG);
            c.getMethod("getInstance");
            c.getMethod("getRealDisplay", int.class);
        } catch (ReflectiveOperationException | RuntimeException e) {
            mancanti.add("DisplayManagerGlobal.getRealDisplay");
        }
        return mancanti;
    }

    @SuppressWarnings("deprecation") // getRealMetrics: unica via pubblica senza Context
    /** Misure (larghezza, altezza) in pixel e densità in dpi dello schermo reale, senza un Context. */
    public static int[] schermoReale() throws ReflectiveOperationException {
        Class<?> c = Class.forName(DMG);
        Object globale = c.getMethod("getInstance").invoke(null);
        Method m = c.getMethod("getRealDisplay", int.class);
        Display d = (Display) m.invoke(globale, Display.DEFAULT_DISPLAY);
        if (d == null) throw new ReflectiveOperationException("schermo reale assente");
        DisplayMetrics dm = new DisplayMetrics();
        d.getRealMetrics(dm);
        return new int[] {dm.widthPixels, dm.heightPixels, dm.densityDpi};
    }
}
