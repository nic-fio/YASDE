/* Buffer di byte che cresce. Inizializzare con PH_BUFFER_VUOTO, liberare con
 * ph_buffer_libera. dati[lunghezza] e' sempre '\0' quando dati != NULL, cosi'
 * un buffer di testo si puo' leggere come stringa C. */
#ifndef PH_BUFFER_H
#define PH_BUFFER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *dati;
    size_t lunghezza;
    size_t capacita;
} PhBuffer;

#define PH_BUFFER_VUOTO ((PhBuffer){NULL, 0, 0})

/* 0 riuscito, -1 memoria finita (il buffer resta valido e invariato). */
int ph_buffer_aggiungi(PhBuffer *b, const void *dati, size_t n);
int ph_buffer_riserva(PhBuffer *b, size_t capacita);
void ph_buffer_svuota(PhBuffer *b); /* lunghezza = 0, tiene la memoria */
void ph_buffer_libera(PhBuffer *b); /* torna a PH_BUFFER_VUOTO */

#endif
