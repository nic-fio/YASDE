/* Test della lingua: scandisce i sorgenti C del progetto cercando le chiamate
 * ph_t("...") e fallisce se un testo non ha la traduzione inglese o se i
 * segnaposto {nome} non coincidono. Il lettore salta commenti, stringhe e
 * caratteri (una chiamata scritta dentro un commento o un letterale non conta)
 * e unisce i letterali adiacenti ("a" "b"). Riconosce ph_t e il marcatore PH_T_
 * (letterali messi in tabelle). Una chiamata il cui argomento non e' un
 * letterale non si puo' controllare: e' un errore.
 * Uso: prova_lingua <cartella src> <cartella dei casi finti>
 * I casi finti (finti/ (file .txt)) provano il test stesso: due errori attesi da
 * ognuno dei file "cattivi" e nessuno da quello buono. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config/config.h"
#include "config/lingua.h"
#include "config/toml.h"

typedef const char *(*CercaFn)(const char *italiano);

typedef struct {
    int errori;
    int avvisi;
    int chiamate;
} Esito;

static bool ident(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* Salta spazi e commenti. */
static const char *salta(const char *p, const char *fine, int *riga)
{
    while (p < fine) {
        if (*p == '\n') {
            (*riga)++;
            p++;
        } else if (*p == ' ' || *p == '\t' || *p == '\r') {
            p++;
        } else if (p + 1 < fine && p[0] == '/' && p[1] == '*') {
            p += 2;
            while (p + 1 < fine && !(p[0] == '*' && p[1] == '/')) {
                if (*p == '\n')
                    (*riga)++;
                p++;
            }
            p = p + 1 < fine ? p + 2 : fine;
        } else if (p + 1 < fine && p[0] == '/' && p[1] == '/') {
            while (p < fine && *p != '\n')
                p++;
        } else {
            break;
        }
    }
    return p;
}

/* Legge un letterale che inizia in *pp (a una virgoletta): lo decodifica e lo
 * accoda a out (lunghezza *n, capacita cap). Torna 0, -1 se non entra o e'
 * malformato. */
static int leggi_letterale(const char **pp, const char *fine, char *out, size_t cap, size_t *n)
{
    const char *p = *pp + 1;
    for (;;) {
        if (p >= fine || *p == '\n')
            return -1;
        char c = *p++;
        if (c == '"')
            break;
        if (c == '\\') {
            if (p >= fine)
                return -1;
            char x = *p++;
            switch (x) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'a': c = '\a'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'v': c = '\v'; break;
            case 'x': {
                unsigned v = 0;
                while (p < fine && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) {
                    char h = *p++;
                    v = (v << 4) | (unsigned)(h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
                }
                c = (char)v;
                break;
            }
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
                unsigned v = (unsigned)(x - '0');
                for (int k = 0; k < 2 && p < fine && *p >= '0' && *p <= '7'; k++)
                    v = v * 8 + (unsigned)(*p++ - '0');
                c = (char)v;
                break;
            }
            default: c = x; break; /* \" \\ \' \? */
            }
        }
        if (*n + 1 >= cap)
            return -1;
        out[(*n)++] = c;
    }
    out[*n] = '\0';
    *pp = p;
    return 0;
}

static void controlla_testo(const char *nome, const char *t, size_t lunghezza, CercaFn cerca, Esito *es)
{
    const char *p = t, *fine = t + lunghezza;
    int riga = 1;
    while (p < fine) {
        char c = *p;
        if (c == '\n') {
            riga++;
            p++;
        } else if (c == '/' && p + 1 < fine && (p[1] == '*' || p[1] == '/')) {
            p = salta(p, fine, &riga);
        } else if (c == '"' || c == '\'') {
            /* stringa o carattere: si salta intero */
            p++;
            while (p < fine && *p != c && *p != '\n') {
                if (*p == '\\' && p + 1 < fine)
                    p++;
                p++;
            }
            if (p < fine && *p == c)
                p++;
        } else if (ident(c)) {
            const char *ini = p;
            while (p < fine && ident(*p))
                p++;
            if (!((p - ini == 4 && memcmp(ini, "ph_t", 4) == 0) || (p - ini == 5 && memcmp(ini, "PH_T_", 5) == 0)))
                continue;
            int riga_chiamata = riga;
            const char *q = salta(p, fine, &riga);
            if (q >= fine || *q != '(')
                continue; /* "ph_t" senza parentesi: non e' una chiamata */
            q = salta(q + 1, fine, &riga);
            if (q >= fine || *q != '"') {
                fprintf(stderr, "%s:%d: errore: ph_t()/PH_T_() con un argomento che non e' un letterale\n", nome,
                        riga_chiamata);
                es->errori++;
                p = q;
                continue;
            }
            char testo[2048];
            size_t n = 0;
            int ok = 0;
            while (q < fine && *q == '"') {
                if (leggi_letterale(&q, fine, testo, sizeof testo, &n) < 0) {
                    ok = -1;
                    break;
                }
                ok = 1;
                q = salta(q, fine, &riga);
            }
            if (ok != 1) {
                fprintf(stderr, "%s:%d: errore: letterale di ph_t() malformato o troppo lungo\n", nome, riga_chiamata);
                es->errori++;
                p = q;
                continue;
            }
            es->chiamate++;
            const char *en = cerca(testo);
            if (en == NULL) {
                fprintf(stderr, "%s:%d: errore: manca la traduzione inglese di \"%s\"\n", nome, riga_chiamata, testo);
                es->errori++;
            } else if (ph_lingua_segnaposto_uguali(testo, en) != 1) {
                fprintf(stderr, "%s:%d: errore: segnaposto diversi (o malformati) tra \"%s\" e \"%s\"\n", nome,
                        riga_chiamata, testo, en);
                es->errori++;
            } else if (ph_lingua_segnaposto_uguali(testo, testo) != 1) {
                fprintf(stderr, "%s:%d: errore: segnaposto malformati in \"%s\"\n", nome, riga_chiamata, testo);
                es->errori++;
            }
            p = q;
        } else {
            p++;
        }
    }
}

static int controlla_file(const char *percorso, CercaFn cerca, Esito *es)
{
    PhErrore err = {{0}};
    PhBuffer b = PH_BUFFER_VUOTO;
    int r = ph_cfg_leggi_file(percorso, 8u * 1024 * 1024, &b, &err);
    if (r != 0) {
        fprintf(stderr, "%s: %s\n", percorso, r == 1 ? "file inesistente" : err.testo);
        ph_buffer_libera(&b);
        es->errori++;
        return -1;
    }
    controlla_testo(percorso, (const char *)b.dati, b.lunghezza, cerca, es);
    ph_buffer_libera(&b);
    return 0;
}

static bool finisce_con(const char *s, const char *suffisso)
{
    size_t a = strlen(s), b = strlen(suffisso);
    return a >= b && strcmp(s + a - b, suffisso) == 0;
}

/* Visita ricorsiva di cartella: .c e .h (esclusi lingua.c e lingua.h, che
 * contengono la definizione di ph_t e la citano nella documentazione). */
static void visita(const char *cartella, CercaFn cerca, Esito *es, int profondita)
{
    if (profondita > 12)
        return;
    DIR *d = opendir(cartella);
    if (d == NULL) {
        fprintf(stderr, "%s: impossibile aprire la cartella\n", cartella);
        es->errori++;
        return;
    }
    struct dirent *voce;
    while ((voce = readdir(d)) != NULL) {
        if (voce->d_name[0] == '.')
            continue;
        char percorso[4096];
        if (snprintf(percorso, sizeof percorso, "%s/%s", cartella, voce->d_name) >= (int)sizeof percorso)
            continue;
        struct stat st;
        if (lstat(percorso, &st) < 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            visita(percorso, cerca, es, profondita + 1);
        } else if (S_ISREG(st.st_mode) && (finisce_con(voce->d_name, ".c") || finisce_con(voce->d_name, ".h")) &&
                   strcmp(voce->d_name, "lingua.c") != 0 && strcmp(voce->d_name, "lingua.h") != 0) {
            controlla_file(percorso, cerca, es);
        }
    }
    closedir(d);
}

/* Traduzioni delle prove sui casi finti. */
static const char *tabella_finta(const char *it)
{
    static const char *const voci[][2] = {
        {"Ciao", "Hello"},
        {"Copiati {numero} file di {nome}", "{nome}: {numero} files copied"},
        {"Una riga spezzata su piu' pezzi", "A line split over several pieces"},
        {"Con \"virgolette\" e\ttabulazione", "With \"quotes\" and tab"},
        {"Graffe {{letterali}} e {valore}", "Braces {{literal}} and {valore}"},
        {"Graffa {isolata", "Open {brace"},
    };
    for (size_t i = 0; i < sizeof voci / sizeof voci[0]; i++)
        if (strcmp(it, voci[i][0]) == 0)
            return voci[i][1];
    return NULL;
}

static const char *tabella_finta_difettosa(const char *it)
{
    if (strcmp(it, "Copiati {numero} file di {nome}") == 0)
        return "{numero} files copied from {origine}"; /* segnaposto diverso */
    return tabella_finta(it);
}

static int errori_prova;

static void aspetta(const char *dir, const char *file, CercaFn cerca, int errori, int avvisi, int chiamate)
{
    char percorso[4096];
    snprintf(percorso, sizeof percorso, "%s/%s", dir, file);
    Esito es = {0, 0, 0};
    if (controlla_file(percorso, cerca, &es) < 0)
        es.errori = -1000;
    if (es.errori != errori || es.avvisi != avvisi || es.chiamate != chiamate) {
        fprintf(stderr, "CASO FINTO %s: attesi %d errori, %d avvisi, %d chiamate; trovati %d, %d, %d\n", file,
                errori, avvisi, chiamate, es.errori, es.avvisi, es.chiamate);
        errori_prova++;
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "uso: prova_lingua <cartella src> <cartella casi finti>\n");
        return 2;
    }
    /* 1) il test deve accorgersi dei problemi (e non lamentarsi a torto) */
    fprintf(stderr, "--- casi finti: gli errori che seguono sono voluti ---\n");
    aspetta(argv[2], "ok.c.txt", tabella_finta, 0, 0, 5);
    aspetta(argv[2], "mancante.c.txt", tabella_finta, 1, 0, 2);
    aspetta(argv[2], "segnaposto.c.txt", tabella_finta_difettosa, 2, 0, 2);
    aspetta(argv[2], "non_letterale.c.txt", tabella_finta, 1, 0, 1);
    aspetta(argv[2], "marcatore.c.txt", tabella_finta, 1, 0, 2);
    aspetta(argv[2], "giro.c.txt", tabella_finta, 0, 0, 2);
    /* Testo troncato a meta' di una chiamata e byte nulli: nessun crash. */
    {
        static const char taglio[] = "x = ph_t(\"Ciao\" \"";
        Esito es = {0, 0, 0};
        for (size_t n = 0; n <= sizeof taglio - 1; n++)
            controlla_testo("taglio", taglio, n, tabella_finta, &es);
        static const char nulli[] = "ph_t(\"a\0b\") ph_t(\0 ph_t";
        controlla_testo("nulli", nulli, sizeof nulli - 1, tabella_finta, &es);
    }
    fprintf(stderr, "--- fine dei casi finti ---\n");

    /* 2) i sorgenti veri del progetto */
    Esito es = {0, 0, 0};
    visita(argv[1], ph_lingua_traduzione_en, &es, 0);
    printf("prova_lingua: %d testi controllati, %d errori nei sorgenti, %d nei casi finti\n", es.chiamate,
           es.errori, errori_prova);
    return es.errori == 0 && errori_prova == 0 ? 0 : 1;
}
