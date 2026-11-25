/* Tabella delle traduzioni inglesi, interna al pezzo `config`. La definisce il
 * file generato da genera_traduzioni.c (ordinata per strcmp sul testo
 * italiano, terminata da {NULL, NULL}); nei test si puo' sostituire con una
 * tabella finta. */
#ifndef PH_LINGUA_TABELLA_H
#define PH_LINGUA_TABELLA_H

#include <stddef.h>

typedef struct {
    const char *it;
    const char *en;
} PhLinguaVoce;

extern const PhLinguaVoce ph_lingua_tabella_en[];
extern const size_t ph_lingua_tabella_en_n; /* voci, senza il terminatore */

#endif
