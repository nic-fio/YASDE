#include "finto_canale.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct PhCanale {
    pthread_mutex_t m;
    pthread_cond_t cv;
    uint8_t *dati;
    size_t n, cap;
    int fine, errore, chiuso;
};

static atomic_int chiusure;

PhCanale *fc_crea(void)
{
    PhCanale *c = calloc(1, sizeof *c);
    if (!c)
        abort();
    pthread_mutex_init(&c->m, NULL);
    pthread_condattr_t at;
    pthread_condattr_init(&at);
    pthread_condattr_setclock(&at, CLOCK_MONOTONIC);
    pthread_cond_init(&c->cv, &at);
    pthread_condattr_destroy(&at);
    return c;
}

void fc_scrivi(PhCanale *c, const void *dati, size_t n)
{
    pthread_mutex_lock(&c->m);
    if (c->n + n > c->cap) {
        c->cap = (c->n + n) * 2;
        c->dati = realloc(c->dati, c->cap);
        if (!c->dati)
            abort();
    }
    memcpy(c->dati + c->n, dati, n);
    c->n += n;
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->m);
}

void fc_fine(PhCanale *c)
{
    pthread_mutex_lock(&c->m);
    c->fine = 1;
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->m);
}

void fc_errore(PhCanale *c)
{
    pthread_mutex_lock(&c->m);
    c->errore = 1;
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->m);
}

int fc_chiusure(void)
{
    return atomic_load(&chiusure);
}

size_t fc_impacchetta(uint8_t *out, uint64_t tempo_us, const void *dati, size_t n)
{
    for (int i = 0; i < 8; i++)
        out[i] = (uint8_t)(tempo_us >> (56 - 8 * i));
    out[8] = (uint8_t)(n >> 8);
    out[9] = (uint8_t)n;
    memcpy(out + 10, dati, n);
    return n + 10;
}

ssize_t ph_canale_leggi(PhCanale *c, void *dati, size_t n, int timeout_ms, PhErrore *e)
{
    pthread_mutex_lock(&c->m);
    struct timespec lim;
    clock_gettime(CLOCK_MONOTONIC, &lim);
    if (timeout_ms > 0) {
        lim.tv_sec += timeout_ms / 1000;
        lim.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (lim.tv_nsec >= 1000000000L) {
            lim.tv_sec++;
            lim.tv_nsec -= 1000000000L;
        }
    }
    while (c->n == 0 && !c->fine && !c->errore && !c->chiuso) {
        if (timeout_ms < 0) {
            pthread_cond_wait(&c->cv, &c->m);
        } else if (pthread_cond_timedwait(&c->cv, &c->m, &lim) != 0) {
            pthread_mutex_unlock(&c->m);
            return PH_CANALE_TEMPO;
        }
    }
    ssize_t r;
    if (c->errore || c->chiuso) {
        pthread_mutex_unlock(&c->m);
        ph_errore(e, "canale finto in errore");
        return PH_CANALE_ERRORE;
    }
    if (c->n == 0) {
        r = PH_CANALE_CHIUSO;
    } else {
        size_t k = c->n < n ? c->n : n;
        memcpy(dati, c->dati, k);
        memmove(c->dati, c->dati + k, c->n - k);
        c->n -= k;
        r = (ssize_t)k;
    }
    pthread_mutex_unlock(&c->m);
    return r;
}

void ph_canale_chiudi(PhCanale *c)
{
    atomic_fetch_add(&chiusure, 1);
    free(c->dati);
    pthread_mutex_destroy(&c->m);
    pthread_cond_destroy(&c->cv);
    free(c);
}
