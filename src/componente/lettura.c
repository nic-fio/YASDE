/* Lettura riprendibile da un canale (vedi interno.h). */
#include "componente/interno.h"

int ph_rip_leggi(PhCanale *c, uint8_t *dest, size_t *fatti, size_t totale, int fetta_ms,
                 PhErrore *e)
{
    while (*fatti < totale) {
        ssize_t r = ph_canale_leggi(c, dest + *fatti, totale - *fatti, fetta_ms, e);
        if (r > 0) {
            /* Un canale che torna piu' byte di quelli chiesti e' rotto: non ci fidiamo. */
            if ((size_t)r > totale - *fatti)
                return ph_errore(e, "il canale ha restituito piu' dati del richiesto");
            *fatti += (size_t)r;
        } else if (r == PH_CANALE_CHIUSO) {
            return 0;
        } else if (r == PH_CANALE_TEMPO) {
            return PH_CANALE_TEMPO;
        } else {
            return -1;
        }
    }
    return 1;
}
