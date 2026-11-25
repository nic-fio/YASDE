package it.yasde;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Resti di sessioni vecchie in /data/local/tmp (parte pura: nessuna classe Android, i processi vivi
 * si leggono da una cartella tipo /proc passata da fuori, così si prova sulla JVM).
 *
 * Se adbd si riavvia (debug senza fili spento e riacceso) init uccide tutti i processi del suo
 * gruppo, anche quelli con setsid: servizio e guardiano muoiono insieme e restano il jar e lo
 * script dei ripristini. Il servizio che parte dopo li ripulisce: SOLO i nomi che sono nostri per
 * forma esatta, mai per altro, e solo se nessun processo vivo li sta usando.
 */
public final class Resti {
    private static final Charset UTF8 = Charset.forName("UTF-8");
    private static final Pattern SCRIPT = Pattern.compile("yasde-custode-([0-9]{1,10})\\.sh");
    private static final Pattern SCRIPT_ACCESSORIO = Pattern.compile("yasde-custode-([0-9]{1,10})\\.sh\\.(pid|tmp|tentativi)");
    private static final Pattern JAR = Pattern.compile("yasde-[0-9a-f]{8}\\.jar");

    private Resti() {}

    /** Che cosa c'è di vivo: pid dei servizi YASDE e percorsi sotto la cartella citati da un processo. */
    public static final class Vivi {
        public final Set<Integer> servizi = new HashSet<>();
        public final Set<String> percorsi = new HashSet<>();
        /**
         * Falso se la scansione non è affidabile: la cartella dei processi non si è potuta leggere, oppure
         * tra i processi letti non compare il servizio stesso (segno che si vede solo una parte del
         * telefono). Con una scansione non affidabile NON si esegue né si cancella nulla.
         */
        public boolean attendibile;
    }

    /** Che cosa fare dei resti. I nomi sono relativi alla cartella. */
    public static final class Piano {
        /** Script di guardiani morti da eseguire (ripristini pendenti) e poi cancellare con i loro accessori. */
        public final List<String> scriptDaEseguire = new ArrayList<>();
        /** .pid e .tmp di script che non esistono più: da cancellare. */
        public final List<String> accessoriOrfani = new ArrayList<>();
        /** Jar di sessioni vecchie che nessuno usa. */
        public final List<String> jarVecchi = new ArrayList<>();
    }

    /** Nome (relativo) di uno script del guardiano, o null. */
    public static Integer pidDelloScript(String nome) {
        Matcher m = SCRIPT.matcher(nome);
        return m.matches() ? Integer.valueOf(m.group(1)) : null;
    }

    public static boolean eUnJarNostro(String nome) {
        return JAR.matcher(nome).matches();
    }

    /**
     * Legge i processi dalla cartella `proc` (con una sottocartella per pid e il file cmdline,
     * argomenti separati da NUL). `dir` è la cartella dei file nostri, con lo "/" finale.
     * Processi illeggibili (di altri utenti) si saltano. `mioPid`: il pid di questo servizio, che
     * deve comparire tra i processi letti con it.yasde.Servizio, altrimenti la scansione non è attendibile.
     */
    public static Vivi scansiona(File proc, String dir, int mioPid) {
        Vivi v = new Vivi();
        Pattern citato = Pattern.compile(Pattern.quote(dir) + "yasde-[0-9a-zA-Z._-]+");
        File[] voci = proc.listFiles();
        if (voci == null) return v; // attendibile resta falso
        for (File f : voci) {
            String n = f.getName();
            if (!n.matches("[0-9]{1,10}")) continue;
            String cmd;
            try {
                cmd = leggi(new File(f, "cmdline"));
            } catch (IOException e) {
                continue;
            }
            if (cmd.contains("it.yasde.Servizio")) v.servizi.add(Integer.valueOf(n));
            Matcher m = citato.matcher(cmd);
            while (m.find()) v.percorsi.add(m.group());
        }
        v.attendibile = v.servizi.contains(Integer.valueOf(mioPid));
        return v;
    }

    /** Legge un file nostro, al massimo 64 KB. */
    static String leggiLimitato(File f) throws IOException {
        return leggi(f);
    }

    private static String leggi(File f) throws IOException {
        try (InputStream in = new FileInputStream(f)) {
            byte[] b = new byte[65536];
            int tot = 0, r;
            while (tot < b.length && (r = in.read(b, tot, b.length - tot)) > 0) tot += r;
            return new String(b, 0, tot, UTF8).replace('\0', ' ');
        }
    }

    /**
     * Decide che cosa ripulire. `nomi`: contenuto della cartella; `jarCorrente`: nome del jar della
     * sessione in corso (o null). Uno script è «morto» se il suo pid non è un servizio vivo e nessun
     * processo vivo (il suo guardiano, che sta per eseguirlo) lo cita; un jar è «vecchio» se non è
     * quello corrente e nessun processo vivo lo cita. `mioPid`: il servizio che sta ripulendo; il suo
     * guardiano non è ancora partito, quindi ogni script o accessorio col suo pid è per forza un resto vecchio.
     */
    public static Piano piano(List<String> nomi, String dir, String jarCorrente, Vivi vivi, int mioPid) {
        Piano p = new Piano();
        Set<String> presenti = new HashSet<>(nomi);
        for (String nome : nomi) {
            Integer pid = pidDelloScript(nome);
            if (pid != null) {
                if ((pid == mioPid || !vivi.servizi.contains(pid)) && !vivi.percorsi.contains(dir + nome)) p.scriptDaEseguire.add(nome);
                continue;
            }
            Matcher a = SCRIPT_ACCESSORIO.matcher(nome);
            if (a.matches()) {
                String script = "yasde-custode-" + a.group(1) + ".sh";
                Integer apid = Integer.valueOf(a.group(1));
                if (!presenti.contains(script) && (apid == mioPid || !vivi.servizi.contains(apid)) && !vivi.percorsi.contains(dir + script)) {
                    p.accessoriOrfani.add(nome);
                }
                continue;
            }
            if (eUnJarNostro(nome) && !nome.equals(jarCorrente) && !vivi.percorsi.contains(dir + nome)) {
                p.jarVecchi.add(nome);
            }
        }
        return p;
    }
}
