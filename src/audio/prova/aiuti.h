/* Piccoli aiuti comuni ai test del pezzo audio. */
#ifndef PH_AUDIO_AIUTI_H
#define PH_AUDIO_AIUTI_H

#include <math.h>
#include <opus/opus.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CONTROLLA(cond)                                                                    \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "%s:%d: controllo fallito: %s\n", __FILE__, __LINE__, #cond);  \
            exit(1);                                                                       \
        }                                                                                  \
    } while (0)

#define TONO_HZ 440.0

/* Codificatore di prova: tono a 440 Hz, 48 kHz stereo, 128 kbit/s, 20 ms. */
typedef struct {
    OpusEncoder *enc;
    uint64_t campioni;
    double ampiezza;
} Tono;

static inline Tono *tono_crea(double ampiezza)
{
    Tono *t = calloc(1, sizeof *t);
    int err = 0;
    t->enc = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &err);
    CONTROLLA(t->enc && err == OPUS_OK);
    opus_encoder_ctl(t->enc, OPUS_SET_BITRATE(128000));
    t->ampiezza = ampiezza;
    return t;
}

static inline void tono_libera(Tono *t)
{
    opus_encoder_destroy(t->enc);
    free(t);
}

/* Prossimo pacchetto Opus; *tempo_us e' il campo "tempo" di §2.5. */
static inline int tono_pacchetto(Tono *t, uint8_t *out, uint64_t *tempo_us)
{
    float pcm[960 * 2];
    for (int i = 0; i < 960; i++) {
        float v = (float)(t->ampiezza * sin(2 * M_PI * TONO_HZ * (double)(t->campioni + (uint64_t)i) / 48000.0));
        pcm[2 * i] = v;
        pcm[2 * i + 1] = v;
    }
    *tempo_us = t->campioni * 1000000 / 48000;
    t->campioni += 960;
    int r = opus_encode_float(t->enc, pcm, 960, out, 1500);
    CONTROLLA(r > 0);
    return r;
}

/* Energia (media del quadrato) di 'x' (un canale ogni 'passo') e quota alla frequenza 'hz'
 * misurata con Goertzel: torna il rapporto energia-a-hz / energia-totale (0..1). */
static inline double quota_frequenza(const float *x, size_t n, size_t passo, double hz, double *energia)
{
    double w = 2 * M_PI * hz / 48000.0, coef = 2 * cos(w), s1 = 0, s2 = 0, tot = 0;
    for (size_t i = 0; i < n; i++) {
        double v = x[i * passo];
        double s = v + coef * s1 - s2;
        s2 = s1;
        s1 = s;
        tot += v * v;
    }
    double potenza = s1 * s1 + s2 * s2 - coef * s1 * s2;   /* |X(k)|^2 */
    if (energia)
        *energia = tot / (double)n;
    if (tot <= 0)
        return 0;
    /* Per un tono puro |X|^2 = (A*n/2)^2 e tot = A^2*n/2: rapporto = |X|^2 * 2 / (n * tot) */
    return potenza * 2.0 / ((double)n * tot);
}

/* Come quota_frequenza ma cerca il picco tra 420 e 460 Hz: in tempo reale la coda
 * puo' comprimere o allungare di qualche percento (e' il suo mestiere), quindi
 * il tono non e' esattamente a 440 Hz. */
static inline double picco_frequenza(const float *x, size_t n, size_t passo, double *energia)
{
    double m = 0;
    for (double hz = 420; hz <= 460; hz += 1) {
        double q = quota_frequenza(x, n, passo, hz, energia);
        if (q > m)
            m = q;
    }
    return m;
}

#endif
