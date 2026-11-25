package it.yasde;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;

/**
 * Il guardiano. Un processo shell separato (lanciato con setsid, così non muore con il
 * servizio né con la shell del PC) tiene sotto controllo una pipe: il servizio ne tiene
 * aperta l'estremità di scrittura, quindi la morte del servizio, comunque avvenga
 * (anche kill -9), la chiude e il guardiano legge EOF.
 *
 * L'elenco dei ripristini sta in un file in /data/local/tmp riscritto in modo atomico
 * (file temporaneo + rename) a ogni imposta/togli; il guardiano lo esegue come script.
 * Chiusura normale: il servizio esegue lui i ripristini, poi scrive "fine" nella pipe
 * e il guardiano si limita a cancellare i propri file.
 */
public final class Custode implements Guardiano {
    private static final Charset UTF8 = Charset.forName("UTF-8");
    private static final String DIR = "/data/local/tmp/";

    // Il guardiano: legge una riga; se non è "fine" (EOF = servizio morto) esegue l'elenco.
    // Poi cancella elenco e, solo se è ancora lo stesso file visto all'avvio (inode, dimensione, data),
    // il jar: il PC può averlo già ricopiato per ripartire.
    private static final String SCRIPT =
        "ELENCO=$1; JAR=$2\n"
        + "if [ -n \"$JAR\" ]; then FIRMA=$(stat -c '%i %s %Y' \"$JAR\" 2>/dev/null); fi\n"
        + "read -r riga\n"
        + "if [ \"$riga\" != fine ]; then sh \"$ELENCO\"; fi\n"
        + "rm -f \"$ELENCO\" \"$ELENCO.tmp\" \"$ELENCO.pid\"\n"
        + "if [ -n \"$JAR\" ] && [ \"$(stat -c '%i %s %Y' \"$JAR\" 2>/dev/null)\" = \"$FIRMA\" ]; then rm -f \"$JAR\"; fi\n"
        // ultimo atto: uccide i processi rimasti della propria sessione (il guardiano ne è il capo,
        // grazie a setsid): i figli di un ripristino scaduto che timeout non raggiunge, anche se
        // sono stati adottati da init. Se pgrep manca non uccide nulla.
        + "kill -9 $(pgrep -s $$ | grep -v \"^$$\\$\") 2>/dev/null\n";

    private static final class Voce {
        final int ordine;
        final String comando;

        Voce(int ordine, String comando) {
            this.ordine = ordine;
            this.comando = comando;
        }
    }

    private final Map<String, Voce> voci = new LinkedHashMap<>();
    private final File elenco;
    private final File jar;
    private Process processo;
    private OutputStream pipe;
    private boolean fermato;

    /** jar: il file da cancellare alla fine (o null). */
    public Custode(File jar) {
        this.jar = jar;
        this.elenco = new File(DIR + "yasde-custode-" + android.os.Process.myPid() + ".sh");
    }

    /** Lancia il guardiano. Va chiamato prima di accettare connessioni. */
    public synchronized void avvia() throws IOException {
        // Prima il guardiano, poi il file dell'elenco: se il servizio muore tra i due passi il
        // guardiano c'è già e pulisce (un elenco mancante non è un errore per lui); a parti
        // invertite resterebbe un file senza nessuno che lo cancelli.
        ProcessBuilder pb = new ProcessBuilder("setsid", "sh", "-c", SCRIPT, "custode",
            elenco.getPath(), jar == null ? "" : jar.getPath());
        pb.redirectOutput(ProcessBuilder.Redirect.to(new File("/dev/null")));
        pb.redirectError(ProcessBuilder.Redirect.to(new File("/dev/null")));
        processo = pb.start();
        pipe = processo.getOutputStream();
        pausaDiProva();
        try {
            scriviElenco();
        } catch (IOException e) {
            // senza elenco il guardiano non servirebbe a nulla: si ferma (pipe chiusa = esegue
            // l'elenco, che non esiste, e pulisce) e l'errore sale a chi ci ha chiamato
            try {
                pipe.close();
            } catch (IOException ignorata) {
                // il processo è già stato avviato: non c'è altro da fare
            }
            processo = null;
            throw e;
        }
    }

    /** Punto di sostituzione per le prove: allarga la finestra tra le due fasi dell'avvio (-Dyasde.ritardoCustode=ms). */
    private static void pausaDiProva() {
        int ms = Integer.getInteger("yasde.ritardoCustode", 0);
        if (ms <= 0) return;
        try {
            Thread.sleep(ms);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    @Override
    public synchronized boolean imposta(String nome, int ordine, String comandoShell) {
        if (fermato) {
            // il guardiano non c'è più: la modifica si annulla subito
            try {
                if (!eseguiShell(20000, null, "-c", comandoShell)) Servizio.log("ripristino tardivo: tempo scaduto");
            } catch (Throwable t) {
                Servizio.log("ripristino tardivo: " + t);
            }
            return false; // la modifica non va fatta
        }
        if (processo == null || !processo.isAlive()) {
            Servizio.log("custode: guardiano non vivo, ripristino '" + nome + "' non registrato");
            return false;
        }
        Voce precedente = voci.put(nome, new Voce(ordine, comandoShell));
        if (aggiorna()) return true;
        // scrittura fallita: la voce non è al sicuro, quindi non deve restare come registrata
        if (precedente != null) voci.put(nome, precedente);
        else voci.remove(nome);
        return false;
    }

    @Override
    public synchronized void togli(String nome) {
        // se la riscrittura fallisce l'elenco su disco resta com'era (una voce in più, innocua
        // perché condizionata, è meglio di una in meno)
        if (!fermato && voci.remove(nome) != null && !aggiorna()) {
            Servizio.log("custode: '" + nome + "' tolta solo in memoria, resta nell'elenco su disco");
        }
    }

    /**
     * Chiusura normale: esegue i ripristini, ferma il guardiano, cancella i file. Idempotente.
     * Se un ripristino non finisce in tempo non si scrive "fine" e non si cancella nulla:
     * chiudendo la pipe il guardiano rifà l'elenco da solo.
     */
    public synchronized void ferma() {
        if (fermato) return;
        fermato = true;
        if (processo == null) {
            // il guardiano non e' mai partito: l'elenco con il nostro pid, se c'e', e' un resto
            // vecchio con ripristini non eseguiti (non va buttato); si toglie solo il jar
            if (jar != null) jar.delete();
            return;
        }
        boolean finito = false;
        try {
            finito = eseguiShell(20000, pidElenco(), elenco.getPath());
            if (!finito) Servizio.log("ripristini: tempo scaduto, li rifa il guardiano");
        } catch (Throwable t) {
            Servizio.log("ripristini: " + t);
        }
        try {
            if (finito) {
                pipe.write("fine\n".getBytes(UTF8));
                pipe.flush();
            }
            pipe.close();
        } catch (IOException e) {
            Servizio.log("custode: " + e);
        }
        if (finito) cancellaFile(); // lo fa già il guardiano, ma non deve restare nulla
    }

    private File pidElenco() {
        return new File(elenco.getPath() + ".pid");
    }

    private void cancellaFile() {
        pidElenco().delete();
        elenco.delete();
        new File(elenco.getPath() + ".tmp").delete();
        if (jar != null) jar.delete();
    }

    /** Chi decide se un file è davvero nostro e quindi eseguibile (sul telefono: file regolare, non collegamento, del nostro utente). */
    public interface Verifica {
        boolean eFileNostro(File f);
    }

    /** Dopo tanti avvii in cui lo stesso script non finisce, non si riesegue più prima di PRONTO. */
    static final int MAX_TENTATIVI = 3;
    /** Parte del tempo totale riservata all'uccisione della sessione di un ripristino scaduto. */
    private static final long RISERVA_UCCISIONE_MS = 1000;

    /**
     * Ripulisce i resti di sessioni vecchie (vedi {@link Resti}). Prima esegue i ripristini pendenti
     * degli script di guardiani morti (con il controllo «solo se il valore è ancora il nostro» che
     * ogni voce contiene), poi cancella script e accessori; infine i jar vecchi.
     *
     * Va chiamato dopo aver preso il socket (nessun altro servizio può essere vivo), prima di avviare
     * il proprio guardiano e prima di PRONTO (il PC aspetta PRONTO poco: `tempoMs` è il budget TOTALE,
     * uccisione compresa). Con una scansione dei processi non attendibile non fa nulla. Uno script che
     * non finisce resta dov'è (contiene ripristini dell'utente non eseguiti); un contatore nel file
     * accessorio `.tentativi` impedisce di ritentarlo a ogni avvio: dopo MAX_TENTATIVI avvii falliti
     * non si riesegue più.
     */
    public static void pulisciResti(File dir, File proc, String jarCorrente, int mioPid, long tempoMs, Verifica verifica) {
        String d = dir.getPath().endsWith("/") ? dir.getPath() : dir.getPath() + "/";
        String[] elenco = dir.list();
        if (elenco == null) return;
        List<String> nomi = new ArrayList<>();
        Collections.addAll(nomi, elenco);
        // niente di nostro oltre al jar corrente: la scansione di /proc (costosa) non serve
        boolean qualcosa = false;
        for (String n : nomi) {
            if ((Resti.pidDelloScript(n) != null || Resti.eUnJarNostro(n) || n.startsWith("yasde-custode-"))
                && !n.equals(jarCorrente)) {
                qualcosa = true;
                break;
            }
        }
        if (!qualcosa) return;
        Resti.Vivi vivi = Resti.scansiona(proc, d, mioPid);
        if (!vivi.attendibile) {
            Servizio.log("resti: scansione dei processi non attendibile, non si tocca nulla");
            return;
        }
        Resti.Piano piano = Resti.piano(nomi, d, jarCorrente, vivi, mioPid);
        long fine = System.currentTimeMillis() + tempoMs;
        for (String nome : piano.scriptDaEseguire) {
            File script = new File(d + nome);
            File tentativi = new File(d + nome + ".tentativi");
            if (!verifica.eFileNostro(script)) {
                Servizio.log("resti: script non regolare o non nostro, non eseguito né cancellato: " + nome);
                continue;
            }
            int fatti = leggiTentativi(tentativi);
            if (fatti >= MAX_TENTATIVI) {
                Servizio.log("resti: script non finito in " + fatti + " avvii, non si riesegue (resta sul telefono): " + nome);
                continue;
            }
            long resto = fine - System.currentTimeMillis() - RISERVA_UCCISIONE_MS;
            if (resto <= 0) {
                Servizio.log("resti: tempo finito, script lasciato: " + nome);
                continue;
            }
            scriviTentativi(tentativi, fatti + 1); // prima di eseguire: se il servizio muore qui conta come tentativo
            boolean finito = false;
            try {
                finito = eseguiShell(resto, new File(d + nome + ".pid"), script.getPath());
            } catch (Throwable t) {
                Servizio.log("resti: ripristini di " + nome + ": " + t);
            }
            if (!finito) {
                Servizio.log("resti: ripristini non finiti (tentativo " + (fatti + 1) + "), script lasciato: " + nome);
                continue;
            }
            Servizio.log("resti: ripristini pendenti eseguiti: " + nome);
            script.delete();
            new File(d + nome + ".pid").delete();
            new File(d + nome + ".tmp").delete();
            tentativi.delete();
        }
        for (String nome : piano.accessoriOrfani) new File(d + nome).delete();
        for (String nome : piano.jarVecchi) {
            // un ripristino pendente rimasto (script non eseguito o non finito) può citare questo jar nel
            // suo comando (es. CLASSPATH=<jar> app_process / it.<nome>.Pannello accendi): finché lo script
            // resta, il jar resta, altrimenti il ripristino non funzionerebbe più
            if (citatoDaScriptRimasto(d, nome, piano.scriptDaEseguire, verifica)) {
                Servizio.log("resti: jar vecchio citato da un ripristino non eseguito, lasciato");
                continue;
            }
            if (new File(d + nome).delete()) Servizio.log("resti: jar vecchio tolto");
        }
    }

    private static boolean citatoDaScriptRimasto(String d, String jar, List<String> script, Verifica verifica) {
        for (String s : script) {
            File f = new File(d + s);
            if (!f.exists() && !Files.isSymbolicLink(f.toPath())) continue; // eseguito e cancellato
            // non regolare o non nostro (FIFO, collegamento simbolico...): non si apre, per prudenza il jar resta
            if (!verifica.eFileNostro(f)) return true;
            try {
                if (Resti.leggiLimitato(f).contains(jar)) return true;
            } catch (IOException e) {
                return true; // illeggibile = prudenza
            }
        }
        return false;
    }

    private static int leggiTentativi(File f) {
        try {
            String t = new String(Files.readAllBytes(f.toPath()), UTF8).trim();
            return t.matches("[0-9]{1,3}") ? Integer.parseInt(t) : MAX_TENTATIVI; // illeggibile = prudenza
        } catch (IOException e) {
            return f.exists() ? MAX_TENTATIVI : 0;
        }
    }

    private static void scriviTentativi(File f, int n) {
        try {
            Files.write(f.toPath(), String.valueOf(n).getBytes(UTF8));
        } catch (IOException e) {
            Servizio.log("resti: contatore dei tentativi non scritto: " + e);
        }
    }

    /** Esegue "sh args..."; true se è finito entro il tempo, false se scaduto (la shell viene distrutta). */
    private static boolean eseguiShell(long ms, File pidFile, String... args) throws IOException, InterruptedException {
        List<String> cmd = new ArrayList<>();
        if (pidFile != null) {
            cmd.add("setsid"); // sessione propria: allo scadere si uccidono tutti i suoi processi
            pidFile.delete(); // niente pid vecchio
        }
        cmd.add("sh");
        Collections.addAll(cmd, args);
        Process p = new ProcessBuilder(cmd)
            .redirectErrorStream(true)
            .redirectOutput(ProcessBuilder.Redirect.to(new File("/dev/null")))
            .start();
        p.getOutputStream().close();
        if (p.waitFor(ms, TimeUnit.MILLISECONDS)) return true;
        if (pidFile != null) {
            // l'elenco scrive il proprio pid (= id della sessione, creata da setsid) nella prima riga;
            // si raccoglie e si uccide PRIMA di distruggere sh, e per sessione: l'appartenenza resta
            // anche per gli orfani adottati da init
            try {
                String pid = new String(Files.readAllBytes(pidFile.toPath()), UTF8).trim();
                if (pid.matches("[0-9]{1,9}")) {
                    Process k = new ProcessBuilder("sh", "-c", "kill -9 $(pgrep -s " + pid + ")").start();
                    k.getOutputStream().close();
                    k.waitFor(500, TimeUnit.MILLISECONDS);
                }
            } catch (IOException e) {
                Servizio.log("sessione dei ripristini non uccisa: " + e);
            }
        }
        // SIGKILL di rinforzo: sh in attesa di un figlio ignora SIGTERM
        p.destroyForcibly();
        return false;
    }

    /** Riscrive l'elenco su disco; true se riuscito. */
    private boolean aggiorna() {
        try {
            // punto di sostituzione per la prova sul telefono (-Dyasde.scritturaFallita=true)
            if (Boolean.getBoolean("yasde.scritturaFallita")) throw new IOException("scrittura simulata");
            scriviElenco();
            return true;
        } catch (Exception e) {
            // qualunque errore, non solo di I/O: chi chiama deve ricevere false
            // (e rimettere la voce com'era), mai un'eccezione a metà
            Servizio.log("custode: elenco non scritto: " + e);
            return false;
        }
    }

    /** Riscrittura atomica: file temporaneo, poi rename sullo stesso file system. */
    private void scriviElenco() throws IOException {
        List<Map.Entry<String, Voce>> ordinate = new ArrayList<>(voci.entrySet());
        // sort stabile: a parità di ordine resta l'ordine di inserimento
        Collections.sort(ordinate, new Comparator<Map.Entry<String, Voce>>() {
            @Override
            public int compare(Map.Entry<String, Voce> a, Map.Entry<String, Voce> b) {
                return Integer.compare(a.getValue().ordine, b.getValue().ordine);
            }
        });
        // timeout di toybox (20 s per voce) se esiste; altrimenti la voce gira senza limite
        StringBuilder sb = new StringBuilder("# ripristini di YASDE\n"
            + "echo $$ > \"$0.pid\"\n"
            + "T=; command -v timeout >/dev/null 2>&1 && T='timeout 20'\n");
        for (Map.Entry<String, Voce> e : ordinate) {
            sb.append("# ").append(e.getKey().replace('\n', ' ')).append('\n');
            // una shell per voce: un comando sbagliato non si mangia i successivi
            sb.append("$T sh -c '").append(e.getValue().comando.replace("'", "'\\''")).append("'\n");
        }
        File tmp = new File(elenco.getPath() + ".tmp");
        try (FileOutputStream o = new FileOutputStream(tmp)) {
            o.write(sb.toString().getBytes(UTF8));
            o.getFD().sync();
        }
        if (!tmp.renameTo(elenco)) throw new IOException("rename fallito");
    }
}
