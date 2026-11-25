/* Lettura di messaggi da un canale del protocollo, a prova di telefono ostile. */
#include <string.h>

#include "dati/interno.h"

/* Legge esattamente n byte a fette. 1 fatto, 0 chiuso prima del primo byte,
 * -1 errore (anche chiuso a meta'), -2 stop. */
static int leggi_esatto(PhCanale *c, atomic_bool *stop, int fetta, uint8_t *dest, size_t n,
                        PhErrore *e)
{
    size_t fatti = 0;
    while (fatti < n) {
        if (atomic_load(stop))
            return -2;
        ssize_t r = ph_canale_leggi(c, dest + fatti, n - fatti, fetta, e);
        if (r > 0) {
            /* Un canale che da' piu' del richiesto e' rotto: non ci si fida. */
            if ((size_t)r > n - fatti)
                return ph_errore(e, "il canale ha restituito piu' dati del richiesto");
            fatti += (size_t)r;
        } else if (r == PH_CANALE_CHIUSO) {
            if (fatti == 0)
                return 0;
            return ph_errore(e, "canale chiuso a meta' di un messaggio");
        } else if (r != PH_CANALE_TEMPO) {
            return -1;
        }
    }
    return 1;
}

/* Legge e butta n byte. */
static int scarta(PhCanale *c, atomic_bool *stop, int fetta, size_t n, PhErrore *e)
{
    uint8_t tmp[4096];
    while (n > 0) {
        size_t k = n < sizeof tmp ? n : sizeof tmp;
        int r = leggi_esatto(c, stop, fetta, tmp, k, e);
        if (r == 0)
            return ph_errore(e, "canale chiuso a meta' di un messaggio");
        if (r != 1)
            return r;
        n -= k;
    }
    return 1;
}

int ph_dati_leggi_messaggio(PhCanale *c, atomic_bool *stop, int fetta, size_t max_corpo,
                            uint8_t *tipo, PhBuffer *corpo, bool *scartato, PhErrore *e)
{
    uint8_t testa[5];
    ph_buffer_svuota(corpo);
    *scartato = false;
    int r = leggi_esatto(c, stop, fetta, testa, sizeof testa, e);
    if (r != 1)
        return r;
    size_t lung = ((size_t)testa[1] << 24) | ((size_t)testa[2] << 16) | ((size_t)testa[3] << 8) |
                  testa[4];
    *tipo = testa[0];
    if (lung > PH_DT_MAX_CORPO)
        return ph_errore(e, "messaggio troppo grande (%zu byte)", lung);
    if (lung > max_corpo) {
        *scartato = true;
        return scarta(c, stop, fetta, lung, e);
    }
    if (lung == 0)
        return 1;
    if (ph_buffer_riserva(corpo, lung) != 0)
        return ph_errore(e, "memoria finita");
    r = leggi_esatto(c, stop, fetta, corpo->dati, lung, e);
    if (r == 0)
        return ph_errore(e, "canale chiuso a meta' di un messaggio");
    if (r != 1)
        return r;
    corpo->lunghezza = lung;
    corpo->dati[lung] = '\0';
    return 1;
}

void ph_dati_leggi_str(PhLettore *l, const uint8_t **p, size_t *n)
{
    *p = NULL;
    *n = ph_leggi_u16(l);
    if (l->rotto || *n > l->lunghezza - l->posizione) {
        l->rotto = true;
        *n = 0;
        return;
    }
    *p = l->dati + l->posizione;
    l->posizione += *n;
}
