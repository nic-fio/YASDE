/* Strumento di prova con un telefono vero (non fa parte dei test automatici).
 * Uso: prova_vera <host> <porta> <file-chiave> <comando> [argomenti]
 *   elenca CARTELLA | info PERCORSO | invia LOCALE REMOTO | ricevi REMOTO LOCALE
 *   | banner (stampa la presentazione del telefono) | invia-annulla LOCALE REMOTO BYTE   (annulla l'invio dopo BYTE byte) */
#include "file/file.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *nome_esito(PhFileEsito r)
{
    switch (r) {
    case PH_FILE_RIUSCITO: return "RIUSCITO";
    case PH_FILE_ERRORE: return "ERRORE";
    case PH_FILE_ANNULLATO: return "ANNULLATO";
    case PH_FILE_NON_TROVATO: return "NON_TROVATO";
    case PH_FILE_NON_CONSENTITO: return "NON_CONSENTITO";
    case PH_FILE_ESISTE: return "ESISTE";
    }
    return "?";
}

static double adesso(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

typedef struct {
    atomic_bool *annulla;
    uint64_t soglia;
    uint64_t ultimo;
} Prog;

static void progresso(uint64_t fatti, uint64_t totale, void *u)
{
    Prog *p = u;
    (void)totale;
    p->ultimo = fatti;
    if (p->soglia && fatti >= p->soglia)
        atomic_store(p->annulla, true);
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "uso: %s host porta file-chiave comando [argomenti]\n", argv[0]);
        return 2;
    }
    PhErrore e = {{0}};
    PhChiave *k = ph_chiave_carica_o_crea(argv[3], &e);
    if (!k) {
        fprintf(stderr, "chiave: %s\n", e.testo);
        return 1;
    }
    bool non_abbinato = false;
    PhAdb *a = ph_adb_collega(argv[1], (uint16_t)atoi(argv[2]), k, 10000, &non_abbinato, &e);
    if (!a) {
        fprintf(stderr, "collegamento%s: %s\n", non_abbinato ? " (PC non abbinato)" : "", e.testo);
        ph_chiave_libera(k);
        return 1;
    }
    const char *cmd = argv[4];
    PhFileEsito r = PH_FILE_ERRORE;
    int uscita = 0;
    if (strcmp(cmd, "banner") == 0) {
        printf("%s\n", ph_adb_presentazione(a));
        r = PH_FILE_RIUSCITO;
    } else if (strcmp(cmd, "elenca") == 0 && argc == 6) {
        PhFileElenco el;
        r = ph_file_elenca(a, argv[5], &el, &e);
        printf("esito %s", nome_esito(r));
        if (r == PH_FILE_RIUSCITO) {
            printf(", %zu voci\n", el.numero);
            for (size_t i = 0; i < el.numero; i++)
                printf("  %c%c %12" PRIu64 " %" PRId64 " %s\n", el.voci[i].cartella ? 'd' : '-',
                       el.voci[i].collegamento ? 'l' : '-', el.voci[i].dimensione, el.voci[i].modificato,
                       el.voci[i].nome);
        } else {
            printf(": %s\n", e.testo);
        }
        ph_file_elenco_libera(&el);
    } else if (strcmp(cmd, "info") == 0 && argc == 6) {
        PhFileVoce v;
        r = ph_file_info(a, argv[5], &v, &e);
        printf("esito %s", nome_esito(r));
        if (r == PH_FILE_RIUSCITO)
            printf(": cartella=%d collegamento=%d dimensione=%" PRIu64 " modificato=%" PRId64 "\n", v.cartella,
                   v.collegamento, v.dimensione, v.modificato);
        else
            printf(": %s\n", e.testo);
    } else if ((strcmp(cmd, "invia") == 0 || strcmp(cmd, "ricevi") == 0) && argc == 7) {
        bool invio = strcmp(cmd, "invia") == 0;
        Prog p = {NULL, 0, 0};
        double t0 = adesso();
        r = invio ? ph_file_invia(a, argv[5], argv[6], progresso, &p, NULL, &e)
                  : ph_file_ricevi(a, argv[5], argv[6], progresso, &p, NULL, &e);
        double dt = adesso() - t0;
        printf("esito %s in %.2f s, %.1f MB/s (%" PRIu64 " byte)", nome_esito(r), dt,
               dt > 0 ? (double)p.ultimo / 1e6 / dt : 0.0, p.ultimo);
        if (r != PH_FILE_RIUSCITO)
            printf(": %s", e.testo);
        printf("\n");
    } else if (strcmp(cmd, "invia-annulla") == 0 && argc == 8) {
        atomic_bool annulla = false;
        Prog p = {&annulla, strtoull(argv[7], NULL, 10), 0};
        r = ph_file_invia(a, argv[5], argv[6], progresso, &p, &annulla, &e);
        printf("esito %s dopo %" PRIu64 " byte", nome_esito(r), p.ultimo);
        if (r != PH_FILE_ANNULLATO)
            printf(": %s", e.testo);
        printf("\n");
    } else {
        fprintf(stderr, "comando o argomenti non validi\n");
        uscita = 2;
    }
    ph_adb_chiudi(a);
    ph_chiave_libera(k);
    return uscita ? uscita : (r == PH_FILE_RIUSCITO ? 0 : 1);
}
