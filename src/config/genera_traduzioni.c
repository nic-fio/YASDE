/* Generatore (gira durante la compilazione, con meson): legge i file .txt di
 * data/en/ e scrive un sorgente C con la tabella delle traduzioni inglesi,
 * ordinata per strcmp. Cosi' le traduzioni stanno dentro l'eseguibile.
 * Uso: genera_traduzioni <uscita.c> <file.txt>...
 * Errori (testo doppio, traduzione vuota, segnaposto diversi, sintassi) fanno
 * fallire la compilazione con il nome del file e la riga. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/config.h"
#include "config/lingua.h"
#include "config/toml.h"

typedef struct {
    char *it;
    char *en;
    const char *file;
    int riga;
} Voce;

static int confronta(const void *a, const void *b)
{
    return strcmp(((const Voce *)a)->it, ((const Voce *)b)->it);
}

/* Un letterale C: ottale a tre cifre per tutto cio' che non e' ASCII stampabile
 * (cosi' un ottale non si fonde con la cifra seguente); '?' protetto per i trigrafi. */
static void scrivi_letterale(FILE *f, const char *s)
{
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
        if (*p == '"' || *p == '\\' || *p == '?')
            fprintf(f, "\\%c", *p);
        else if (*p >= 0x20 && *p < 0x7F)
            fputc(*p, f);
        else
            fprintf(f, "\\%03o", *p);
    }
    fputc('"', f);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "uso: genera_traduzioni uscita.c file.txt...\n");
        return 2;
    }
    Voce *voci = NULL;
    size_t n = 0, cap = 0;
    int esito = 1;

    for (int a = 2; a < argc; a++) {
        PhErrore err = {{0}};
        PhBuffer b = PH_BUFFER_VUOTO;
        PhToml doc;
        int r = ph_cfg_leggi_file(argv[a], PH_TOML_MAX_FILE, &b, &err);
        if (r != 0) {
            fprintf(stderr, "%s: %s\n", argv[a], r == 1 ? "file inesistente" : err.testo);
            ph_buffer_libera(&b);
            goto fine;
        }
        r = ph_toml_leggi((const char *)b.dati, b.lunghezza, &doc, &err);
        ph_buffer_libera(&b);
        if (r < 0) {
            fprintf(stderr, "%s: %s\n", argv[a], err.testo);
            goto fine;
        }
        if (doc.n != 1) {
            fprintf(stderr, "%s:%d: nei file delle traduzioni non ci sono tabelle\n", argv[a],
                    doc.tabelle[1].riga);
            ph_toml_libera(&doc);
            goto fine;
        }
        const PhTomlTabella *t = &doc.tabelle[0];
        for (size_t i = 0; i < t->n; i++) {
            const PhTomlVoce *v = &t->voci[i];
            const char *problema = NULL;
            if (v->tipo != PH_TOML_STRINGA)
                problema = "la traduzione deve essere una stringa";
            else if (v->s[0] == '\0')
                problema = "traduzione vuota";
            else if (ph_lingua_segnaposto_uguali(v->chiave, v->s) != 1)
                problema = "i segnaposto {nome} dell'italiano e dell'inglese non coincidono";
            if (problema != NULL) {
                fprintf(stderr, "%s:%d: %s: \"%s\"\n", argv[a], v->riga, problema, v->chiave);
                ph_toml_libera(&doc);
                goto fine;
            }
            if (n == cap) {
                size_t nc = cap ? cap * 2 : 64;
                Voce *nv = realloc(voci, nc * sizeof *nv);
                if (nv == NULL) {
                    fprintf(stderr, "memoria esaurita\n");
                    ph_toml_libera(&doc);
                    goto fine;
                }
                voci = nv;
                cap = nc;
            }
            voci[n].it = strdup(v->chiave);
            voci[n].en = strdup(v->s);
            voci[n].file = argv[a];
            voci[n].riga = v->riga;
            if (voci[n].it == NULL || voci[n].en == NULL) {
                fprintf(stderr, "memoria esaurita\n");
                free(voci[n].it);
                free(voci[n].en);
                ph_toml_libera(&doc);
                goto fine;
            }
            n++;
        }
        ph_toml_libera(&doc);
    }

    if (n > 1)
        qsort(voci, n, sizeof *voci, confronta);
    for (size_t i = 1; i < n; i++) {
        if (strcmp(voci[i - 1].it, voci[i].it) == 0) {
            fprintf(stderr, "%s:%d: testo tradotto due volte (gia' in %s:%d): \"%s\"\n", voci[i].file,
                    voci[i].riga, voci[i - 1].file, voci[i - 1].riga, voci[i].it);
            goto fine;
        }
    }

    FILE *f = fopen(argv[1], "w");
    if (f == NULL) {
        fprintf(stderr, "impossibile scrivere %s\n", argv[1]);
        goto fine;
    }
    fprintf(f, "/* Generato da genera_traduzioni.c: non modificare. */\n"
               "#include \"config/lingua_tabella.h\"\n\n"
               "const PhLinguaVoce ph_lingua_tabella_en[] = {\n");
    for (size_t i = 0; i < n; i++) {
        fputs("    {", f);
        scrivi_letterale(f, voci[i].it);
        fputs(", ", f);
        scrivi_letterale(f, voci[i].en);
        fputs("},\n", f);
    }
    fprintf(f, "    {NULL, NULL}\n};\nconst size_t ph_lingua_tabella_en_n = %zu;\n", n);
    if (fclose(f) != 0) {
        fprintf(stderr, "errore scrivendo %s\n", argv[1]);
        goto fine;
    }
    esito = 0;
fine:
    for (size_t i = 0; i < n; i++) {
        free(voci[i].it);
        free(voci[i].en);
    }
    free(voci);
    return esito;
}
