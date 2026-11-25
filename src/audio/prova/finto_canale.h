/* Canale finto: sostituisce ph_canale_leggi e ph_canale_chiudi del vero ADB
 * (che nei test del pezzo audio non c'e'). E' un tubo in memoria: chi prova
 * scrive i byte che il telefono avrebbe mandato, il lettore li riceve a pezzi
 * quanti ne arrivano. Usato anche dallo strumento suona. */
#ifndef PH_FINTO_CANALE_H
#define PH_FINTO_CANALE_H

#include <stddef.h>
#include <stdint.h>

#include "adb/adb.h"

PhCanale *fc_crea(void);
/* Si possono chiamare da un altro thread mentre il lettore e' in attesa. */
void fc_scrivi(PhCanale *c, const void *dati, size_t n);
void fc_fine(PhCanale *c);      /* dopo i dati, il lettore vede la chiusura */
void fc_errore(PhCanale *c);    /* il lettore vede un errore (anche coi dati ancora in coda) */
int fc_chiusure(void);          /* quante volte e' stato chiamato ph_canale_chiudi */

/* Aiuti per costruire pacchetti nel formato §2.5. Torna i byte scritti in 'out'. */
size_t fc_impacchetta(uint8_t *out, uint64_t tempo_us, const void *dati, size_t n);

#endif
