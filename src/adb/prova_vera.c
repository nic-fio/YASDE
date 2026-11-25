/* Non e' un test automatico: collega il client a un telefono vero (gia'
 * abbinato alla chiave data) ed esegue un comando shell.
 * Uso: prova_vera <host> <porta> <file-chiave> <comando>
 *      prova_vera <host> <porta> <file-chiave> --invia <file-locale> <file-remoto>
 * La seconda forma misura l'invio di un file sul canale (servizio exec:) e
 * stampa i MiB/s; il tempo comprende l'attesa che il file remoto abbia la
 * dimensione giusta. */
#include "adb/adb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double ora_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int invia_file(PhAdb *a, const char *locale, const char *remoto)
{
    PhErrore e;
    FILE *f = fopen(locale, "rb");
    if (f == NULL) {
        fprintf(stderr, "file locale non apribile\n");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long tot = ftell(f);
    fseek(f, 0, SEEK_SET);
    char serv[512];
    snprintf(serv, sizeof serv, "exec:cat > %s", remoto);
    static uint8_t blocco[1024 * 1024];
    double t0 = ora_s();
    PhCanale *c = ph_canale_apri(a, serv, 5000, &e);
    if (c == NULL) {
        fprintf(stderr, "apertura: %s\n", e.testo);
        fclose(f);
        return 1;
    }
    size_t n;
    int rc = 0;
    /* PROVA_VERA_BLOCCO: byte per ogni ph_canale_scrivi (come un trasferimento a pezzi piccoli) */
    size_t pezzo = getenv("PROVA_VERA_BLOCCO") != NULL ? (size_t)atol(getenv("PROVA_VERA_BLOCCO")) : sizeof blocco;
    if (pezzo == 0 || pezzo > sizeof blocco)
        pezzo = sizeof blocco;
    while (rc == 0 && (n = fread(blocco, 1, pezzo, f)) > 0)
        if (ph_canale_scrivi(c, blocco, n, &e) != 0) {
            fprintf(stderr, "scrittura: %s\n", e.testo);
            rc = 1;
        }
    fclose(f);
    ph_canale_chiudi(c);
    if (rc != 0)
        return 1;
    /* il telefono finisce di scrivere dopo la chiusura: si aspetta la dimensione giusta */
    char cmd[600];
    snprintf(cmd, sizeof cmd, "stat -c %%s %s", remoto);
    for (int i = 0; i < 600; i++) {
        PhBuffer out = PH_BUFFER_VUOTO;
        int codice = 0;
        if (ph_shell_esegui(a, cmd, &out, NULL, &codice, 5000, &e) == 0 && out.dati != NULL &&
            atol((const char *)out.dati) == tot) {
            ph_buffer_libera(&out);
            double dt = ora_s() - t0;
            printf("inviati %ld byte in %.2f s: %.2f MiB/s\n", tot, dt, (double)tot / 1048576.0 / dt);
            return 0;
        }
        ph_buffer_libera(&out);
        struct timespec p = {0, 20 * 1000 * 1000};
        nanosleep(&p, NULL);
    }
    fprintf(stderr, "il file remoto non ha raggiunto la dimensione attesa\n");
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 5 && argc != 7) {
        fprintf(stderr, "uso: %s <host> <porta> <file-chiave> <comando>\n", argv[0]);
        return 2;
    }
    char *fine = NULL;
    long porta = strtol(argv[2], &fine, 10);
    if (*argv[2] == '\0' || *fine != '\0' || porta < 1 || porta > 65535) {
        fprintf(stderr, "porta non valida\n");
        return 2;
    }
    PhErrore e;
    PhChiave *k = ph_chiave_carica_o_crea(argv[3], &e);
    if (k == NULL) {
        fprintf(stderr, "chiave: %s\n", e.testo);
        return 1;
    }
    bool non_abbinato = false;
    PhAdb *a = ph_adb_collega(argv[1], (uint16_t)porta, k, 10000, &non_abbinato, &e);
    if (a == NULL) {
        fprintf(stderr, "collegamento: %s%s\n", e.testo, non_abbinato ? " [non abbinato]" : "");
        ph_chiave_libera(k);
        return 1;
    }
    /* con PROVA_VERA_FEATURES stampa solo l'elenco delle caratteristiche del telefono (non il banner intero) */
    if (getenv("PROVA_VERA_FEATURES") != NULL) {
        const char *f = strstr(ph_adb_presentazione(a), "features=");
        printf("caratteristiche: %s\n", f != NULL ? f + 9 : "(nessuna)");
    }
    if (argc == 7) {
        if (strcmp(argv[4], "--invia") != 0) {
            fprintf(stderr, "uso sbagliato\n");
            return 2;
        }
        int r = invia_file(a, argv[5], argv[6]);
        ph_adb_chiudi(a);
        ph_chiave_libera(k);
        return r;
    }
    PhBuffer out = PH_BUFFER_VUOTO, err = PH_BUFFER_VUOTO;
    int codice = -1;
    int rc = ph_shell_esegui(a, argv[4], &out, &err, &codice, 15000, &e);
    if (rc != 0)
        fprintf(stderr, "comando: %s\n", e.testo);
    else {
        printf("stdout: %.*s", (int)out.lunghezza, out.dati ? (const char *)out.dati : "");
        printf("stderr: %.*s", (int)err.lunghezza, err.dati ? (const char *)err.dati : "");
        printf("uscita: %d\n", codice);
    }
    ph_buffer_libera(&out);
    ph_buffer_libera(&err);
    ph_adb_chiudi(a);
    ph_chiave_libera(k);
    return rc == 0 ? 0 : 1;
}
