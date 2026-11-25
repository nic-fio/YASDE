/* Lettura dei pacchetti del canale audio (§2.5): tempo (8 byte), lunghezza
 * (2 byte), dati; tutto big endian. */
#include <string.h>

#include "audio/interno.h"

void ph_lettore_inizia(PhLettore *l)
{
    l->ha = 0;
    l->consegnato = false;
}

int ph_lettore_leggi(PhLettore *l, PhCanale *c, int timeout_ms, PhPacchetto *p, PhErrore *e)
{
    if (l->consegnato) {
        l->ha = 0;
        l->consegnato = false;
    }
    for (;;) {
        size_t serve = PH_AUDIO_INTESTAZIONE;
        if (l->ha >= PH_AUDIO_INTESTAZIONE) {
            size_t lung = ((size_t)l->buf[8] << 8) | l->buf[9];
            /* Controllo prima di leggere i dati: una lunghezza falsa non deve
             * mai portarci oltre il buffer. */
            if (lung > PH_AUDIO_PACCHETTO_MAX)
                return ph_errore(e, "pacchetto audio di %zu byte: oltre il massimo di %d", lung,
                                 PH_AUDIO_PACCHETTO_MAX);
            serve = PH_AUDIO_INTESTAZIONE + lung;
        }
        if (l->ha == serve && serve >= PH_AUDIO_INTESTAZIONE) {
            uint64_t t = 0;
            for (int i = 0; i < 8; i++)
                t = (t << 8) | l->buf[i];
            /* Un tempo oltre 2^62 us (146 mila anni) non e' un orologio: spazzatura. */
            if (t >= ((uint64_t)1 << 62))
                return ph_errore(e, "pacchetto audio con tempo assurdo");
            p->tempo_us = (int64_t)t;
            p->dati = l->buf + PH_AUDIO_INTESTAZIONE;
            p->lunghezza = serve - PH_AUDIO_INTESTAZIONE;
            l->consegnato = true;
            return PH_LETTURA_PACCHETTO;
        }
        size_t manca = serve - l->ha;
        ssize_t r = ph_canale_leggi(c, l->buf + l->ha, manca, timeout_ms, e);
        if (r == PH_CANALE_TEMPO)
            return PH_LETTURA_TEMPO;
        if (r == PH_CANALE_CHIUSO) {
            if (l->ha == 0)
                return PH_LETTURA_CHIUSO;
            return ph_errore(e, "canale audio chiuso a meta' di un pacchetto");
        }
        if (r < 0)
            return PH_LETTURA_ERRORE;  /* PhErrore gia' riempito dal canale */
        if ((size_t)r > manca)
            return ph_errore(e, "il canale audio ha dato piu' byte di quelli chiesti");
        l->ha += (size_t)r;
    }
}
