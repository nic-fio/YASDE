package it.yasde;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Analisi del testo di `dumpsys battery` (ripiego se il ricevitore dell'intento persistente non e'
 * registrabile da un processo shell). Righe `  nome: valore` del servizio "battery" di AOSP:
 * `AC powered: true`, `status: 2`, `health: 2`, `level: 91`, `scale: 100`, `voltage: 4186`,
 * `temperature: 274`. Parte pura, provata sulla JVM. Un campo assente vale Integer.MIN_VALUE (non
 * noto); senza livello e scala l'esito e' null: formato non riconosciuto, mai un valore inventato.
 */
final class AnalisiBatteria {
    private static final Pattern RIGA = Pattern.compile("^\\s*([A-Za-z ]+):\\s*(-?\\d+|true|false)\\s*$");

    private AnalisiBatteria() {}

    static CampiDati.Batteria analizza(String dump, int minutiCarica) {
        int livello = Integer.MIN_VALUE, scala = Integer.MIN_VALUE, stato = 1, salute = 1, tensione = 0, temp = Integer.MIN_VALUE;
        int alimentazione = 0;
        for (String r : dump.split("\n")) {
            Matcher m = RIGA.matcher(r.replace("\r", ""));
            if (!m.matches()) continue;
            String nome = m.group(1).trim(), v = m.group(2);
            boolean vero = v.equals("true");
            if (nome.equals("AC powered") && vero) alimentazione |= 1;
            else if (nome.equals("USB powered") && vero) alimentazione |= 2;
            else if (nome.equals("Wireless powered") && vero) alimentazione |= 4;
            else if (nome.equals("Dock powered") && vero) alimentazione |= 8;
            else if (v.equals("true") || v.equals("false")) continue;
            else if (nome.equals("level")) livello = Integer.parseInt(v);
            else if (nome.equals("scale")) scala = Integer.parseInt(v);
            else if (nome.equals("status")) stato = Integer.parseInt(v);
            else if (nome.equals("health")) salute = Integer.parseInt(v);
            else if (nome.equals("voltage")) tensione = Integer.parseInt(v);
            else if (nome.equals("temperature")) temp = Integer.parseInt(v);
        }
        if (livello == Integer.MIN_VALUE || scala == Integer.MIN_VALUE) return null;
        return CampiDati.Batteria.daAndroid(livello, scala, stato, alimentazione, salute, temp, tensione, minutiCarica);
    }
}
