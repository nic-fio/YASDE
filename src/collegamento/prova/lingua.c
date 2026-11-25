/* Lingua finta per le prove del collegamento (con -Dpezzo=collegamento il pezzo
 * config non c'e'): ph_t(), ph_tv() e ph_lingua_componi(). Il nome del file e'
 * "lingua.c" apposta: la scansione del test `lingua` salta i file con questo
 * nome (qui ph_t e' definita, non chiamata con un letterale). */
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "config/lingua.h"
#include "finto.h"

static atomic_bool lingua_en;
void finto_lingua_inglese(bool si) { atomic_store(&lingua_en, si); }

const char *ph_t(const char *it) {
    if (!it) return "";
    if (!atomic_load(&lingua_en)) return it;
    static pthread_mutex_t Mi = PTHREAD_MUTEX_INITIALIZER;
    static struct Voce { char *it, *en; struct Voce *p; } *voci;
    pthread_mutex_lock(&Mi);
    for (struct Voce *v = voci; v; v = v->p)
        if (strcmp(v->it, it) == 0) {
            pthread_mutex_unlock(&Mi);
            return v->en;
        }
    struct Voce *v = calloc(1, sizeof *v);
    v->it = strdup(it);
    v->en = malloc(strlen(it) + 6);
    strcpy(v->en, "[en] ");
    strcat(v->en, it);
    v->p = voci;
    voci = v;
    pthread_mutex_unlock(&Mi);
    return v->en;
}
const char *ph_tv(const char *it) { return ph_t(it); }

/* Sostituisce {nome} con il valore (solo quel che serve al collegamento). */
size_t ph_lingua_componi(char *out, size_t capacita, const char *modello, ...) {
    const char *nomi[16], *valori[16];
    int n = 0;
    va_list ap;
    va_start(ap, modello);
    for (const char *k; n < 16 && (k = va_arg(ap, const char *)) != NULL;) {
        nomi[n] = k;
        valori[n++] = va_arg(ap, const char *);
    }
    va_end(ap);
    size_t j = 0;
    for (const char *p = modello; *p && j + 1 < capacita; p++) {
        if (*p == '{') {
            const char *fine = strchr(p, '}');
            for (int i = 0; fine && i < n; i++)
                if (strlen(nomi[i]) == (size_t)(fine - p - 1) && strncmp(p + 1, nomi[i], strlen(nomi[i])) == 0) {
                    for (const char *v = valori[i]; *v && j + 1 < capacita; v++) out[j++] = *v;
                    p = fine;
                    goto avanti;
                }
        }
        out[j++] = *p;
    avanti:;
    }
    if (capacita) out[j] = '\0';
    return j;
}
