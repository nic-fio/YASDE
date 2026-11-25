/* Consegna al thread GTK, orologio e registro delle decisioni (shell.h).
 * La consegna e' l'unica via da un thread qualunque al thread GTK
 * (CONTRATTI.md §2): sempre asincrona, anche dal thread GTK stesso, cosi' una
 * funzione di ritorno non gira mai dentro la chiamata che l'ha chiesta. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "shell/shell.h"

typedef struct {
    PhShellFn fn;
    void *dati;
    GDestroyNotify libera;
} Consegna;

static GThread *thread_gtk; /* scritto una volta, all'avvio, dal thread GTK */

static gboolean esegui(gpointer p)
{
    Consegna *c = p;
    c->fn(c->dati);
    return G_SOURCE_REMOVE;
}

static void finisci(gpointer p)
{
    Consegna *c = p;
    if (c->libera != NULL)
        c->libera(c->dati);
    g_free(c);
}

void ph_shell_consegna(PhShellFn fn, void *dati, GDestroyNotify libera)
{
    if (fn == NULL) {
        if (libera != NULL)
            libera(dati);
        return;
    }
    Consegna *c = g_new(Consegna, 1);
    c->fn = fn;
    c->dati = dati;
    c->libera = libera;
    /* Contesto predefinito = quello del ciclo GTK. Priorita' normale: sopra il
     * ridisegno di GTK; chi manda molto (i fotogrammi) deve accorpare da se'
     * (una sola consegna in volo per flusso). L'ordine tra consegne della
     * stessa priorita' e' quello di inserimento. */
    GSource *s = g_idle_source_new();
    g_source_set_priority(s, G_PRIORITY_DEFAULT);
    g_source_set_callback(s, esegui, c, finisci);
    g_source_set_static_name(s, "ph_shell_consegna");
    g_source_attach(s, NULL);
    g_source_unref(s);
}

void ph_shell_segna_thread_gtk(void)
{
    if (thread_gtk == NULL)
        thread_gtk = g_thread_self();
}

bool ph_shell_nel_thread_gtk(void)
{
    return thread_gtk != NULL && thread_gtk == g_thread_self();
}

uint64_t ph_shell_ora_ms(void)
{
    return (uint64_t)(g_get_monotonic_time() / 1000);
}

void ph_shell_decisione(const char *che_cosa, const char *causa_formato, ...)
{
    char causa[512];
    va_list ap;
    va_start(ap, causa_formato);
    vsnprintf(causa, sizeof causa, causa_formato, ap);
    va_end(ap);
    struct timespec ts;
    struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    /* Una sola fprintf: righe intere anche con piu' thread. */
    fprintf(stderr, "[decisione] %02d:%02d:%02d.%03ld %s - causa: %s\n", tm.tm_hour, tm.tm_min,
            tm.tm_sec, ts.tv_nsec / 1000000, che_cosa != NULL ? che_cosa : "?", causa);
}
