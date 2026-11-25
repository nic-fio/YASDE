/* Dal pacchetto alla coda: decodifica e riconoscimento dei buchi.
 * Il campo "tempo" conta i campioni catturati dal telefono: un salto in avanti
 * di uno o piu' pacchetti vuol dire pacchetti persi (si occultano), un salto
 * indietro un pacchetto arrivato tardi o doppio (si scarta). */
#include <stdlib.h>

#include "audio/interno.h"

#define SOGLIA_US 10000          /* mezzo pacchetto: sotto e' solo rumore del tempo */
#define SALTO_INDIETRO_US 1000000
#define MAX_OCCULTATI 10         /* oltre 200 ms di buco non si inventa altro suono */
#define RITARDI_PER_RISINCRONIZZARE 25  /* il telefono ha ricominciato il tempo da capo */
#define NON_VALIDI_DI_FILA_MAX 100

struct PhFlusso {
    PhDecoder *dec;
    PhCodaAudio *coda;
    bool ha_atteso;
    int64_t atteso_us;          /* tempo che dovrebbe avere il prossimo pacchetto */
    int ultimi_frame;
    int ritardi_di_fila;
    int non_validi_di_fila;
    _Atomic uint64_t ricevuti, persi, in_ritardo, non_validi;
    float pcm[PH_AUDIO_FRAME_MAX * PH_AUDIO_CANALI];
};

PhFlusso *ph_flusso_crea(PhCodaAudio *coda, PhErrore *e)
{
    PhFlusso *f = calloc(1, sizeof *f);
    if (!f) {
        ph_errore(e, "memoria esaurita creando il flusso audio");
        return NULL;
    }
    f->dec = ph_decoder_crea(e);
    if (!f->dec) {
        free(f);
        return NULL;
    }
    f->coda = coda;
    f->ultimi_frame = 960;
    return f;
}

void ph_flusso_libera(PhFlusso *f)
{
    if (!f)
        return;
    ph_decoder_libera(f->dec);
    free(f);
}

int ph_flusso_pacchetto(PhFlusso *f, int64_t tempo_us, const uint8_t *dati, size_t n,
                        int64_t ora_us, PhErrore *e)
{
    if (n == 0)
        return 0;  /* pacchetto vuoto: ignorato; il buco si vedra' dal tempo */

    if (f->ha_atteso) {
        int64_t delta = tempo_us - f->atteso_us;
        if (delta < -SOGLIA_US) {
            atomic_fetch_add(&f->in_ritardo, 1);
            /* Un salto indietro di oltre 1 s non e' un ritardatario (la rete non
             * riordina cosi' tanto): il tempo del telefono e' ripartito, subito. */
            if (delta >= -SALTO_INDIETRO_US && ++f->ritardi_di_fila < RITARDI_PER_RISINCRONIZZARE)
                return 0;
            /* Troppi di fila: non sono ritardatari, e' il tempo che e' ripartito. */
            ph_coda_svuota(f->coda);
            ph_decoder_azzera(f->dec);
            f->ha_atteso = false;
        } else if (delta > SOGLIA_US) {
            int64_t dur = (int64_t)f->ultimi_frame * 1000000 / PH_AUDIO_RATE;
            int64_t persi = (delta + dur / 2) / dur;
            if (persi > 100000)
                persi = 100000;
            atomic_fetch_add(&f->persi, (uint64_t)persi);
            int occulta = persi > MAX_OCCULTATI ? MAX_OCCULTATI : (int)persi;
            for (int k = 0; k < occulta; k++) {
                int r = ph_decoder_occulta(f->dec, f->ultimi_frame, f->pcm, NULL);
                if (r <= 0)
                    break;
                ph_coda_inserisci_occultato(f->coda, f->pcm, (size_t)r);
            }
        }
    }
    f->ritardi_di_fila = 0;

    int r = ph_decoder_decodifica(f->dec, dati, n, f->pcm, NULL);
    if (r < 0) {
        atomic_fetch_add(&f->non_validi, 1);
        if (++f->non_validi_di_fila >= NON_VALIDI_DI_FILA_MAX)
            return ph_errore(e, "il flusso audio non e' Opus: %d pacchetti non validi di fila",
                             NON_VALIDI_DI_FILA_MAX);
        return 0;
    }
    f->non_validi_di_fila = 0;
    atomic_fetch_add(&f->ricevuti, 1);
    ph_coda_inserisci(f->coda, f->pcm, (size_t)r, tempo_us, ora_us);
    f->ultimi_frame = r;
    f->ha_atteso = true;
    f->atteso_us = tempo_us + (int64_t)r * 1000000 / PH_AUDIO_RATE;
    return 0;
}

void ph_flusso_contatori(PhFlusso *f, PhAudioStat *s)
{
    s->ricevuti = atomic_load(&f->ricevuti);
    s->persi = atomic_load(&f->persi);
    s->in_ritardo = atomic_load(&f->in_ritardo);
    s->non_validi = atomic_load(&f->non_validi);
}
