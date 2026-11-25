/* Parte pura della lingua: segnaposto e composizione. Non dipende dalla
 * tabella delle traduzioni, cosi' la usa anche il generatore che la produce. */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "config/lingua.h"

#define MAX_COPPIE 16
#define MAX_SEGNAPOSTO 32
#define MAX_NOME 63

static bool inizio_nome(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool corpo_nome(char c)
{
    return inizio_nome(c) || (c >= '0' && c <= '9');
}

/* Se in p[0] inizia un segnaposto {nome} valido torna la lunghezza del nome
 * (1..63) e *fine punta dopo '}'; altrimenti 0. */
static size_t leggi_segnaposto(const char *p, const char **fine)
{
    if (p[0] != '{' || !inizio_nome(p[1]))
        return 0;
    size_t n = 1;
    while (corpo_nome(p[1 + n]))
        n++;
    if (p[1 + n] != '}' || n > MAX_NOME)
        return 0;
    *fine = p + n + 2;
    return n;
}

typedef struct {
    char nome[MAX_NOME + 1];
} Nome;

static int confronta_nomi(const void *a, const void *b)
{
    return strcmp(((const Nome *)a)->nome, ((const Nome *)b)->nome);
}

/* Elenca i segnaposto ordinati; torna il numero, -1 se malformato o troppi. */
static int elenca(const char *t, Nome *v)
{
    int n = 0;
    for (const char *p = t; *p != '\0';) {
        if (p[0] == '{' && p[1] == '{') {
            p += 2;
        } else if (p[0] == '}' && p[1] == '}') {
            p += 2;
        } else if (p[0] == '{') {
            const char *fine;
            size_t ln = leggi_segnaposto(p, &fine);
            if (ln == 0 || n >= MAX_SEGNAPOSTO)
                return -1;
            memcpy(v[n].nome, p + 1, ln);
            v[n].nome[ln] = '\0';
            n++;
            p = fine;
        } else if (p[0] == '}') {
            return -1;
        } else {
            p++;
        }
    }
    qsort(v, (size_t)n, sizeof *v, confronta_nomi);
    return n;
}

int ph_lingua_segnaposto_uguali(const char *a, const char *b)
{
    Nome va[MAX_SEGNAPOSTO], vb[MAX_SEGNAPOSTO];
    int na = elenca(a, va), nb = elenca(b, vb);
    if (na < 0 || nb < 0)
        return -1;
    if (na != nb)
        return 0;
    for (int i = 0; i < na; i++)
        if (strcmp(va[i].nome, vb[i].nome) != 0)
            return 0;
    return 1;
}

typedef struct {
    char *out;
    size_t cap, n;
    int primo_scartato; /* primo byte non scritto, -1 se nessuno */
} Uscita;

static void emetti(Uscita *u, const char *s, size_t ln)
{
    for (size_t i = 0; i < ln; i++) {
        if (u->cap > 0 && u->n < u->cap - 1)
            u->out[u->n] = s[i];
        else if (u->primo_scartato < 0)
            u->primo_scartato = (unsigned char)s[i];
        u->n++;
    }
}

size_t ph_lingua_componi(char *out, size_t capacita, const char *modello, ...)
{
    const char *nomi[MAX_COPPIE];
    const char *valori[MAX_COPPIE];
    size_t n = 0;
    bool valore_nullo = false;
    va_list ap;
    va_start(ap, modello);
    for (;;) {
        const char *nome = va_arg(ap, const char *);
        if (nome == NULL)
            break;
        const char *valore = va_arg(ap, const char *);
        if (valore == NULL) { /* errore: non si legge altro dagli argomenti */
            valore_nullo = true;
            break;
        }
        if (n < MAX_COPPIE) {
            nomi[n] = nome;
            valori[n] = valore;
            n++;
        }
    }
    va_end(ap);
    if (valore_nullo) { /* errore del chiamante: niente di mezzo scritto */
        if (capacita > 0)
            out[0] = '\0';
        return (size_t)-1;
    }

    Uscita u = {out, capacita, 0, -1};
    if (modello == NULL)
        modello = "";
    for (const char *p = modello; *p != '\0';) {
        const char *fine;
        if (p[0] == '{' && p[1] == '{') {
            emetti(&u, "{", 1);
            p += 2;
        } else if (p[0] == '}' && p[1] == '}') {
            emetti(&u, "}", 1);
            p += 2;
        } else if (p[0] == '{' && leggi_segnaposto(p, &fine) > 0) {
            size_t ln = (size_t)(fine - p) - 2;
            const char *val = NULL;
            for (size_t i = 0; i < n; i++)
                if (strlen(nomi[i]) == ln && memcmp(nomi[i], p + 1, ln) == 0) {
                    val = valori[i];
                    break;
                }
            if (val != NULL)
                emetti(&u, val, strlen(val));
            else
                emetti(&u, p, (size_t)(fine - p)); /* senza coppia: resta com'e' */
            p = fine;
        } else {
            emetti(&u, p, 1);
            p++;
        }
    }
    if (capacita > 0) {
        size_t fine_scritto = u.n < capacita ? u.n : capacita - 1;
        if (u.n >= capacita && u.primo_scartato >= 0 && (u.primo_scartato & 0xC0) == 0x80) {
            /* Il taglio cadrebbe in mezzo a un carattere: arretra al suo inizio. */
            while (fine_scritto > 0) {
                fine_scritto--;
                if (((unsigned char)out[fine_scritto] & 0xC0) != 0x80)
                    break;
            }
        }
        out[fine_scritto] = '\0';
    }
    return u.n;
}
