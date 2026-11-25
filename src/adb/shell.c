/* Comandi shell con il protocollo "shell v2": sul canale viaggiano pacchetti
 * da 1 byte di id + 4 byte di lunghezza (little endian) + dati. */
#include "adb/adb.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SHELL_STDOUT 1
#define SHELL_STDERR 2
#define SHELL_USCITA 3
/* Un pacchetto shell v2 reale e' lungo al massimo qualche decina di KiB; il
 * tetto evita che una lunghezza assurda ci tenga a leggere per sempre. */
#define SHELL_MAX_PACCHETTO (16u * 1024u * 1024u)

static int64_t ora_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int ph_shell_esegui(PhAdb *a, const char *comando, PhBuffer *uscita, PhBuffer *errori, int *codice,
                    int timeout_ms, PhErrore *e)
{
    int64_t scadenza = timeout_ms < 0 ? -1 : ora_ms() + timeout_ms;
#define RESTA() (scadenza < 0 ? -1 : (ora_ms() >= scadenza ? 0 : (int)(scadenza - ora_ms())))

    size_t lc = strlen(comando);
    char *servizio = malloc(lc + sizeof "shell,v2,raw:");
    if (servizio == NULL)
        return ph_errore(e, "memoria finita");
    memcpy(servizio, "shell,v2,raw:", sizeof "shell,v2,raw:" - 1);
    memcpy(servizio + sizeof "shell,v2,raw:" - 1, comando, lc + 1);

    PhCanale *c = ph_canale_apri(a, servizio, RESTA(), e);
    free(servizio);
    if (c == NULL)
        return -1;

    int rc = -1;
    bool finito = false;
    while (!finito) {
        uint8_t ih[5];
        if (ph_canale_leggi_tutto(c, ih, sizeof ih, RESTA(), e) != 0) {
            ph_errore_contesto(e, "comando '%s'", comando);
            goto fine;
        }
        uint32_t len = (uint32_t)ih[1] | ((uint32_t)ih[2] << 8) | ((uint32_t)ih[3] << 16) |
                       ((uint32_t)ih[4] << 24);
        if (len > SHELL_MAX_PACCHETTO) {
            ph_errore(e, "pacchetto shell troppo grande (%u byte)", (unsigned)len);
            goto fine;
        }
        PhBuffer *dest = ih[0] == SHELL_STDOUT ? uscita : ih[0] == SHELL_STDERR ? errori : NULL;
        uint8_t pezzo[4096];
        uint8_t cod = 0;
        bool ha_cod = false;
        while (len > 0) {
            size_t k = len < sizeof pezzo ? len : sizeof pezzo;
            if (ph_canale_leggi_tutto(c, pezzo, k, RESTA(), e) != 0) {
                ph_errore_contesto(e, "comando '%s'", comando);
                goto fine;
            }
            if (dest != NULL && ph_buffer_aggiungi(dest, pezzo, k) != 0) {
                ph_errore(e, "memoria finita");
                goto fine;
            }
            if (ih[0] == SHELL_USCITA && !ha_cod) {
                cod = pezzo[0];
                ha_cod = true;
            }
            len -= (uint32_t)k;
        }
        /* altri id (stdin, chiusura stdin) non arrivano dal telefono: ignorati */
        if (ih[0] == SHELL_USCITA) {
            if (!ha_cod) {
                ph_errore(e, "comando '%s': pacchetto di uscita senza codice", comando);
                goto fine;
            }
            if (codice != NULL)
                *codice = cod;
            finito = true;
        }
    }
    rc = 0;
fine:
    ph_canale_chiudi(c);
    return rc;
#undef RESTA
}
