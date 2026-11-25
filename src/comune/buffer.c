#include "comune/buffer.h"

#include <stdlib.h>
#include <string.h>

int ph_buffer_riserva(PhBuffer *b, size_t capacita)
{
    if (capacita == SIZE_MAX)
        return -1;
    if (b->capacita >= capacita + 1)
        return 0;
    size_t nuova = b->capacita ? b->capacita : 64;
    while (nuova < capacita + 1) {
        if (nuova > SIZE_MAX / 2)
            return -1;
        nuova *= 2;
    }
    uint8_t *p = realloc(b->dati, nuova);
    if (p == NULL)
        return -1;
    b->dati = p;
    b->capacita = nuova;
    b->dati[b->lunghezza] = '\0';
    return 0;
}

int ph_buffer_aggiungi(PhBuffer *b, const void *dati, size_t n)
{
    if (n > SIZE_MAX - b->lunghezza - 1)
        return -1;
    if (ph_buffer_riserva(b, b->lunghezza + n) != 0)
        return -1;
    if (n > 0)
        memcpy(b->dati + b->lunghezza, dati, n);
    b->lunghezza += n;
    b->dati[b->lunghezza] = '\0';
    return 0;
}

void ph_buffer_svuota(PhBuffer *b)
{
    b->lunghezza = 0;
    if (b->dati != NULL)
        b->dati[0] = '\0';
}

void ph_buffer_libera(PhBuffer *b)
{
    free(b->dati);
    *b = PH_BUFFER_VUOTO;
}
