package it.yasde;

import android.content.Context;
import android.util.DisplayMetrics;
import android.view.Display;

/** Misure dello schermo reale (per portare l'input di una sessione-specchio sullo schermo vero). */
final class Specchio {
    private Specchio() {}

    @SuppressWarnings("deprecation") // getRealMetrics: unico modo pubblico per la misura logica
    static int[] misureReali() {
        Context c = NascosteVideo.contesto();
        Display d = c.getSystemService(android.hardware.display.DisplayManager.class).getDisplay(0);
        DisplayMetrics m = new DisplayMetrics();
        d.getRealMetrics(m);
        return new int[] {m.widthPixels, m.heightPixels};
    }
}
