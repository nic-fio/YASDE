/* Decodifica Opus 48 kHz stereo (libopus). */
#include <opus/opus.h>
#include <stdlib.h>

#include "audio/interno.h"

struct PhDecoder {
    OpusDecoder *opus;
};

PhDecoder *ph_decoder_crea(PhErrore *e)
{
    PhDecoder *d = calloc(1, sizeof *d);
    if (!d) {
        ph_errore(e, "memoria esaurita creando il decodificatore audio");
        return NULL;
    }
    int err = 0;
    d->opus = opus_decoder_create(PH_AUDIO_RATE, PH_AUDIO_CANALI, &err);
    if (!d->opus || err != OPUS_OK) {
        ph_errore(e, "decodificatore Opus non creato: %s", opus_strerror(err));
        free(d);
        return NULL;
    }
    return d;
}

void ph_decoder_libera(PhDecoder *d)
{
    if (!d)
        return;
    opus_decoder_destroy(d->opus);
    free(d);
}

void ph_decoder_azzera(PhDecoder *d)
{
    opus_decoder_ctl(d->opus, OPUS_RESET_STATE);
}

int ph_decoder_decodifica(PhDecoder *d, const uint8_t *dati, size_t n, float *out, PhErrore *e)
{
    if (n == 0 || n > PH_AUDIO_PACCHETTO_MAX)
        return ph_errore(e, "pacchetto Opus di %zu byte non valido", n);
    int r = opus_decode_float(d->opus, dati, (opus_int32)n, out, PH_AUDIO_FRAME_MAX, 0);
    if (r < 0)
        return ph_errore(e, "pacchetto Opus non decodificabile: %s", opus_strerror(r));
    if (r == 0 || r > PH_AUDIO_FRAME_MAX)
        return ph_errore(e, "pacchetto Opus con %d frame", r);
    return r;
}

int ph_decoder_occulta(PhDecoder *d, int frame, float *out, PhErrore *e)
{
    if (frame < 120 || frame > PH_AUDIO_FRAME_MAX || frame % 120 != 0)
        return ph_errore(e, "occultamento di %d frame non valido", frame);
    /* Dati nulli = pacchetto perso: Opus inventa la continuazione del suono. */
    int r = opus_decode_float(d->opus, NULL, 0, out, frame, 0);
    if (r < 0)
        return ph_errore(e, "occultamento Opus fallito: %s", opus_strerror(r));
    return r;
}
