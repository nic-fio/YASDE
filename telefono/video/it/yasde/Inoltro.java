package it.yasde;

import java.io.IOException;

/**
 * Le regole di cio' che esce sul canale video, senza Android (si prova sulla JVM): prima la
 * CONFIGURAZIONE, poi un fotogramma chiave, solo dopo i fotogrammi normali. Un pacchetto che
 * l'uscita scarta (pausa, codificatore non piu' corrente) NON conta come inviato.
 * Lo usa il solo thread del codificatore.
 */
final class Inoltro {
    private Codificatore.Uscita ultima;
    private byte[] config;
    private boolean configInviata, chiaveInviata;

    /** Un pacchetto dal codificatore verso l'uscita `u` (null = canale non aperto: si scarta). */
    void pacchetto(Codificatore.Uscita u, boolean daConfigurazione, boolean chiave, int l, int a, long us,
            byte[] dati) throws IOException {
        if (u != ultima) { // il PC ha (ri)aperto il canale (ogni attivazione porta un oggetto nuovo)
            ultima = u;
            configInviata = false;
            chiaveInviata = false;
        }
        if (daConfigurazione) {
            config = dati;
            configInviata = false;
            if (u != null) inviaConfig(u, l, a);
            return;
        }
        if (u == null) return;
        if (!configInviata && config != null && !inviaConfig(u, l, a)) return;
        if (!chiaveInviata && !chiave) return; // il PC non sa decodificare prima di un fotogramma chiave
        boolean scritto = u.pacchetto(Codificatore.FOTOGRAMMA, chiave ? 1 : 0, l, a, us, dati);
        // Un fotogramma scartato interrompe la catena dei riferimenti: serve una nuova chiave.
        chiaveInviata = scritto && (chiave || chiaveInviata);
    }

    private boolean inviaConfig(Codificatore.Uscita u, int l, int a) throws IOException {
        boolean ok = u.pacchetto(Codificatore.CONFIGURAZIONE, 0, l, a, 0, config);
        if (ok) configInviata = true;
        return ok;
    }
}
