#include "comune/errore.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int ph_errore(PhErrore *e, const char *formato, ...)
{
    if (e == NULL)
        return -1;
    va_list ap;
    va_start(ap, formato);
    vsnprintf(e->testo, sizeof e->testo, formato, ap);
    va_end(ap);
    return -1;
}

int ph_errore_contesto(PhErrore *e, const char *formato, ...)
{
    if (e == NULL)
        return -1;
    char vecchio[sizeof e->testo];
    memcpy(vecchio, e->testo, sizeof vecchio);
    vecchio[sizeof vecchio - 1] = '\0';
    char contesto[sizeof e->testo];
    va_list ap;
    va_start(ap, formato);
    vsnprintf(contesto, sizeof contesto, formato, ap);
    va_end(ap);
    /* Il troncamento e' voluto: il messaggio resta una frase leggibile. */
    snprintf(e->testo, sizeof e->testo, "%.120s: %.130s", contesto, vecchio);
    return -1;
}
