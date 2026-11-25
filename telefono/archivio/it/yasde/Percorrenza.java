package it.yasde;

import java.io.File;
import java.io.IOException;
import java.nio.file.DirectoryIteratorException;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayDeque;

/**
 * Percorrenza in ampiezza di una cartella per la ricerca di nomi (docs/protocollo.md §2.7).
 * Niente dipendenze da Android: provata su JVM. Tiene in memoria solo la coda delle
 * cartelle ancora da visitare (tetto `maxCartelle`) e l'elenco di UNA cartella alla volta.
 */
final class Percorrenza {
    /** Chi riceve i risultati. Un'eccezione (canale chiuso) ferma la ricerca. */
    interface Ricevitore {
        void trovato(String percorso, boolean cartella) throws IOException;
    }

    /** Chi dice se bisogna fermarsi subito (il PC ha chiuso il canale). */
    interface Interruzione {
        boolean fermati();
    }

    static final int FINITA = 0, LIMITE = 1, INTERROTTA = 3;

    private final int maxRisultati;
    private final int maxCartelle;
    private final long maxMillis;
    private final boolean soloCartelle;
    private final String ago;
    private final Interruzione stop;
    private final String[] esclusi;

    /** Tetto della coda in memoria, somma delle lunghezze dei percorsi (caratteri). */
    static final long MAX_BYTE_CODA = 16L << 20;

    /** Cartelle effettivamente aperte (per le misure). */
    int cartelleVisitate;

    /**
     * `ago` gia' normalizzato con ConfrontoNomi.normalizza. `esclusi`: percorsi completi in cui
     * non si entra MAI scendendo (es. /proc, /sys: enormi e senza file dell'utente: consumerebbero
     * il tetto delle cartelle a scapito di quelle vere). Se la radice e' uno di essi, si cerca.
     */
    Percorrenza(String ago, boolean soloCartelle, int maxRisultati, int maxCartelle, long maxMillis,
            Interruzione stop, String[] esclusi) {
        this.esclusi = esclusi;
        this.ago = ago;
        this.soloCartelle = soloCartelle;
        this.maxRisultati = maxRisultati;
        this.maxCartelle = maxCartelle;
        this.maxMillis = maxMillis;
        this.stop = stop;
    }

    /**
     * Ritorna FINITA (tutto visitato), LIMITE (troppi risultati, troppe cartelle o troppo tempo:
     * risultati parziali) o INTERROTTA (stop). Le eccezioni del ricevitore passano al chiamante.
     */
    int cerca(String radice, Ricevitore r) throws IOException {
        long fine = System.nanoTime() + maxMillis * 1000000L;
        ArrayDeque<String> coda = new ArrayDeque<String>();
        coda.add(radice);
        int trovati = 0;
        boolean troncata = false;
        long byteCoda = radice.length();
        cartelleVisitate = 0;
        while (!coda.isEmpty()) {
            if (stop.fermati()) return INTERROTTA;
            if (cartelleVisitate >= maxCartelle || System.nanoTime() - fine > 0) return LIMITE;
            String cartella = coda.poll();
            byteCoda -= cartella.length();
            cartelleVisitate++;
            String base = cartella.endsWith("/") ? cartella : cartella + "/";
            int n = 0;
            // Letta voce per voce (non File.list(): su una cartella enorme non si potrebbe
            // interrompere); una cartella non leggibile si salta senza errori.
            DirectoryStream<Path> ds;
            try {
                ds = Files.newDirectoryStream(new File(cartella).toPath());
            } catch (IOException | RuntimeException e) {
                continue;
            }
            try {
                for (Path voce : ds) {
                    if ((++n & 31) == 0) {
                        if (stop.fermati()) return INTERROTTA;
                        if (System.nanoTime() - fine > 0) return LIMITE;
                    }
                    String nome = voce.getFileName().toString();
                    if (nome.startsWith(".")) continue;
                    String percorso = base + nome;
                    boolean collegamento;
                    boolean cartellaVera;
                    try {
                        BasicFileAttributes a = Files.readAttributes(voce, BasicFileAttributes.class,
                                LinkOption.NOFOLLOW_LINKS);
                        collegamento = a.isSymbolicLink();
                        cartellaVera = a.isDirectory();
                    } catch (IOException | RuntimeException e) {
                        continue; // sparito o non accessibile
                    }
                    // un collegamento non si segue mai (niente cicli); per i risultati si
                    // guarda comunque a che cosa punta
                    boolean eCartella = collegamento ? new File(percorso).isDirectory() : cartellaVera;
                    if ((!soloCartelle || eCartella) && ConfrontoNomi.contiene(nome, ago)) {
                        r.trovato(percorso, eCartella);
                        if (++trovati >= maxRisultati) return LIMITE;
                    }
                    if (cartellaVera && !collegamento && !escluso(percorso)) {
                        if (cartelleVisitate + coda.size() < maxCartelle && byteCoda + percorso.length() <= MAX_BYTE_CODA) {
                            coda.add(percorso);
                            byteCoda += percorso.length();
                        } else {
                            troncata = true;
                        }
                    }
                }
            } catch (DirectoryIteratorException e) {
                // errore a meta' elenco: si prosegue con le altre cartelle
            } finally {
                try {
                    ds.close();
                } catch (IOException e) {
                    // niente da fare
                }
            }
        }
        return troncata ? LIMITE : FINITA;
    }

    private boolean escluso(String percorso) {
        for (String e : esclusi) {
            if (e.equals(percorso)) return true;
        }
        return false;
    }
}
