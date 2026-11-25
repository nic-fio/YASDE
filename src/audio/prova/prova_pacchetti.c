/* Lettura dei pacchetti dal canale: pezzi, timeout, lunghezze false, chiusure. */
#include <string.h>

#include "audio/interno.h"
#include "aiuti.h"
#include "finto_canale.h"

static int leggi_fino(PhLettore *l, PhCanale *c, PhPacchetto *p, PhErrore *e)
{
    for (int i = 0; i < 1000; i++) {
        int r = ph_lettore_leggi(l, c, 5, p, e);
        if (r != PH_LETTURA_TEMPO)
            return r;
    }
    return PH_LETTURA_TEMPO;
}

int main(void)
{
    uint8_t dati[4100], pk[4200];
    for (size_t i = 0; i < sizeof dati; i++)
        dati[i] = (uint8_t)(i * 7 + 1);
    PhErrore e;
    PhPacchetto p;
    PhLettore l;

    /* Un pacchetto spedito un byte alla volta, poi due insieme. */
    {
        PhCanale *c = fc_crea();
        ph_lettore_inizia(&l);
        size_t n = fc_impacchetta(pk, 123456789012345ULL, dati, 100);
        for (size_t i = 0; i < n; i++) {
            fc_scrivi(c, pk + i, 1);
            int r = ph_lettore_leggi(&l, c, 5, &p, &e);
            CONTROLLA(r == (i + 1 < n ? PH_LETTURA_TEMPO : PH_LETTURA_PACCHETTO));
        }
        CONTROLLA(p.tempo_us == 123456789012345LL && p.lunghezza == 100 && memcmp(p.dati, dati, 100) == 0);
        size_t n1 = fc_impacchetta(pk, 5, dati, 10);
        size_t n2 = fc_impacchetta(pk + n1, 6, dati + 10, 20);
        fc_scrivi(c, pk, n1 + n2);
        CONTROLLA(leggi_fino(&l, c, &p, &e) == PH_LETTURA_PACCHETTO && p.tempo_us == 5 && p.lunghezza == 10);
        CONTROLLA(leggi_fino(&l, c, &p, &e) == PH_LETTURA_PACCHETTO && p.tempo_us == 6 && p.lunghezza == 20 &&
                  memcmp(p.dati, dati + 10, 20) == 0);
        /* Pacchetto vuoto: valido, lunghezza 0. */
        n = fc_impacchetta(pk, 7, dati, 0);
        fc_scrivi(c, pk, n);
        CONTROLLA(leggi_fino(&l, c, &p, &e) == PH_LETTURA_PACCHETTO && p.lunghezza == 0);
        /* Chiusura a un confine = fine normale. */
        fc_fine(c);
        CONTROLLA(leggi_fino(&l, c, &p, &e) == PH_LETTURA_CHIUSO);
        ph_canale_chiudi(c);
    }
    /* Pacchetto massimo (4000) accettato; 4001 e 65535 rifiutati prima di leggere i dati. */
    {
        PhCanale *c = fc_crea();
        ph_lettore_inizia(&l);
        size_t n = fc_impacchetta(pk, 1, dati, 4000);
        fc_scrivi(c, pk, n);
        CONTROLLA(leggi_fino(&l, c, &p, &e) == PH_LETTURA_PACCHETTO && p.lunghezza == 4000 &&
                  memcmp(p.dati, dati, 4000) == 0);
        ph_canale_chiudi(c);
    }
    {
        PhCanale *c = fc_crea();
        ph_lettore_inizia(&l);
        size_t n = fc_impacchetta(pk, 1, dati, 4001);
        fc_scrivi(c, pk, n);
        e.testo[0] = 0;
        CONTROLLA(leggi_fino(&l, c, &p, &e) == PH_LETTURA_ERRORE && e.testo[0]);
        ph_canale_chiudi(c);
        c = fc_crea();
        ph_lettore_inizia(&l);
        uint8_t h[10] = {0, 0, 0, 0, 0, 0, 0, 1, 0xff, 0xff};
        fc_scrivi(c, h, 10);
        CONTROLLA(leggi_fino(&l, c, &p, &e) == PH_LETTURA_ERRORE);
        ph_canale_chiudi(c);
    }
    /* Tempo assurdo. */
    {
        PhCanale *c = fc_crea();
        ph_lettore_inizia(&l);
        size_t n = fc_impacchetta(pk, ~(uint64_t)0, dati, 5);
        fc_scrivi(c, pk, n);
        CONTROLLA(leggi_fino(&l, c, &p, &e) == PH_LETTURA_ERRORE);
        ph_canale_chiudi(c);
    }
    /* Chiusura a meta' intestazione e a meta' dati: errore. */
    for (int taglio = 1; taglio < 10 + 50; taglio += 7) {
        PhCanale *c = fc_crea();
        ph_lettore_inizia(&l);
        size_t n = fc_impacchetta(pk, 1, dati, 50);
        fc_scrivi(c, pk, (size_t)taglio < n ? (size_t)taglio : n);
        fc_fine(c);
        CONTROLLA(leggi_fino(&l, c, &p, &e) == PH_LETTURA_ERRORE);
        ph_canale_chiudi(c);
    }
    /* Errore del canale. */
    {
        PhCanale *c = fc_crea();
        ph_lettore_inizia(&l);
        fc_errore(c);
        CONTROLLA(leggi_fino(&l, c, &p, &e) == PH_LETTURA_ERRORE);
        ph_canale_chiudi(c);
    }
    /* Spazzatura casuale: mai fuori dai buffer (lo controlla AddressSanitizer). */
    {
        srand(12345);
        for (int prova = 0; prova < 200; prova++) {
            PhCanale *c = fc_crea();
            ph_lettore_inizia(&l);
            uint8_t sp[5000];
            for (size_t i = 0; i < sizeof sp; i++)
                sp[i] = (uint8_t)rand();
            if (prova % 2)
                for (size_t i = 8; i + 2 < sizeof sp; i += 30)
                    sp[i] = 0;   /* lunghezze piccole: tanti pacchetti */
            fc_scrivi(c, sp, sizeof sp);
            fc_fine(c);
            int r;
            do {
                r = ph_lettore_leggi(&l, c, 5, &p, &e);
                if (r == PH_LETTURA_PACCHETTO)
                    CONTROLLA(p.lunghezza <= PH_AUDIO_PACCHETTO_MAX);
            } while (r == PH_LETTURA_PACCHETTO || r == PH_LETTURA_TEMPO);
            CONTROLLA(r == PH_LETTURA_ERRORE || r == PH_LETTURA_CHIUSO);
            ph_canale_chiudi(c);
        }
    }
    return 0;
}
