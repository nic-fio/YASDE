/* Decodifica Opus e riconoscimento dei pacchetti persi / in ritardo. */
#include <string.h>

#include "audio/interno.h"
#include "aiuti.h"

#define SEC 1000000LL

static void decodifica_tono(void)
{
    PhErrore e;
    PhDecoder *d = ph_decoder_crea(&e);
    CONTROLLA(d);
    Tono *t = tono_crea(0.5);
    static float tutto[960 * 2 * 60];
    float pcm[PH_AUDIO_FRAME_MAX * 2];
    uint8_t pk[1500];
    uint64_t tempo;
    size_t tot = 0;
    for (int i = 0; i < 60; i++) {
        int n = tono_pacchetto(t, pk, &tempo);
        int fr = ph_decoder_decodifica(d, pk, (size_t)n, pcm, &e);
        CONTROLLA(fr == 960);
        memcpy(tutto + tot * 2, pcm, 960 * 2 * sizeof(float));
        tot += 960;
    }
    /* Si saltano i primi 10 pacchetti (avvio del codec). */
    double en;
    double q = quota_frequenza(tutto + 10 * 960 * 2, 50 * 960, 2, TONO_HZ, &en);
    CONTROLLA(q > 0.9);
    /* Ampiezza 0.5 -> energia media 0.125. */
    CONTROLLA(en > 0.10 && en < 0.15);
    /* Pacchetti non validi: rifiutati senza danni. */
    uint8_t spazzatura[300];
    srand(7);
    for (int i = 0; i < 300; i++)
        spazzatura[i] = (uint8_t)rand();
    for (size_t n = 0; n <= 300; n += 13) {
        int r = ph_decoder_decodifica(d, spazzatura, n, pcm, NULL);
        CONTROLLA(r == -1 || (r > 0 && r <= PH_AUDIO_FRAME_MAX));
    }
    CONTROLLA(ph_decoder_decodifica(d, spazzatura, 5000, pcm, NULL) == -1);
    CONTROLLA(ph_decoder_occulta(d, 960, pcm, &e) == 960);
    CONTROLLA(ph_decoder_occulta(d, 100, pcm, NULL) == -1);
    tono_libera(t);
    ph_decoder_libera(d);
}

static void flusso_buchi(void)
{
    PhErrore e;
    PhCodaAudio *coda = ph_coda_crea();
    PhFlusso *f = ph_flusso_crea(coda, &e);
    CONTROLLA(coda && f);
    Tono *t = tono_crea(0.5);
    uint8_t pk[20][1500];
    int len[20];
    uint64_t tempo[20];
    for (int i = 0; i < 20; i++)
        len[i] = tono_pacchetto(t, pk[i], &tempo[i]);
    PhAudioStat s;
    int64_t ora = 10 * SEC;
    /* 0..4 regolari, poi 5 e 6 mancano, 7..9, poi il 8 doppio, poi il 3 in ritardo. */
    for (int i = 0; i < 5; i++)
        CONTROLLA(ph_flusso_pacchetto(f, (int64_t)tempo[i], pk[i], (size_t)len[i], ora += 20000, &e) == 0);
    for (int i = 7; i < 10; i++)
        CONTROLLA(ph_flusso_pacchetto(f, (int64_t)tempo[i], pk[i], (size_t)len[i], ora += 20000, &e) == 0);
    CONTROLLA(ph_flusso_pacchetto(f, (int64_t)tempo[8], pk[8], (size_t)len[8], ora += 1000, &e) == 0);
    CONTROLLA(ph_flusso_pacchetto(f, (int64_t)tempo[3], pk[3], (size_t)len[3], ora += 1000, &e) == 0);
    CONTROLLA(ph_flusso_pacchetto(f, (int64_t)tempo[10], pk[10], (size_t)len[10], ora += 1000, &e) == 0);
    ph_flusso_contatori(f, &s);
    CONTROLLA(s.ricevuti == 9);
    CONTROLLA(s.persi == 2);
    CONTROLLA(s.in_ritardo == 2);
    /* In coda: 8 pacchetti veri + 2 occultati + il decimo = 9+2 pacchetti da 20 ms = 220 ms. */
    PhCodaStat cs;
    ph_coda_stat(coda, &cs);
    CONTROLLA(cs.margine_ms == 220);

    /* Un buco enorme: si conta tutto ma si occulta al massimo 200 ms. */
    CONTROLLA(ph_flusso_pacchetto(f, 3600 * SEC, pk[11], (size_t)len[11], ora += 1000, &e) == 0);
    ph_flusso_contatori(f, &s);
    CONTROLLA(s.persi > 100000 - 1 && s.persi < 200000);
    ph_coda_stat(coda, &cs);
    CONTROLLA(cs.margine_ms <= 220 + 11 * 20);

    /* Spazzatura Opus di fila: prima scartata, poi il flusso si dichiara rotto. */
    uint8_t sp[1] = {0x03};   /* TOC 'conteggio arbitrario' senza il byte del conteggio: invalido */
    int rotti = 0;
    for (int i = 0; i < 300 && rotti == 0; i++)
        if (ph_flusso_pacchetto(f, 3600 * SEC + 20000, sp, 1, ora += 1000, &e) < 0)
            rotti = 1;
    ph_flusso_contatori(f, &s);
    CONTROLLA(s.non_validi == 100 && rotti == 1);

    tono_libera(t);
    ph_flusso_libera(f);
    ph_coda_libera(coda);

    /* Il tempo riparte da zero (nuova sessione): salto indietro di oltre 1 s,
     * ci si risincronizza subito. */
    coda = ph_coda_crea();
    f = ph_flusso_crea(coda, &e);
    t = tono_crea(0.5);
    uint64_t tm;
    uint8_t q[1500];
    int n;
    for (int i = 0; i < 300; i++) {
        n = tono_pacchetto(t, q, &tm);
        CONTROLLA(ph_flusso_pacchetto(f, (int64_t)tm + 100 * SEC, q, (size_t)n, ora += 20000, &e) == 0);
    }
    tono_libera(t);
    t = tono_crea(0.5);
    for (int i = 0; i < 100; i++) {
        n = tono_pacchetto(t, q, &tm);
        CONTROLLA(ph_flusso_pacchetto(f, (int64_t)tm, q, (size_t)n, ora += 20000, &e) == 0);
    }
    ph_flusso_contatori(f, &s);
    CONTROLLA(s.in_ritardo == 1);
    CONTROLLA(s.ricevuti == 400);
    tono_libera(t);
    ph_flusso_libera(f);
    ph_coda_libera(coda);

    /* Salto indietro piccolo (mezzo secondo): 25 ritardi di fila e poi ci si risincronizza. */
    coda = ph_coda_crea();
    f = ph_flusso_crea(coda, &e);
    t = tono_crea(0.5);
    for (int i = 0; i < 300; i++) {
        n = tono_pacchetto(t, q, &tm);
        CONTROLLA(ph_flusso_pacchetto(f, (int64_t)tm + 100 * SEC, q, (size_t)n, ora += 20000, &e) == 0);
    }
    tono_libera(t);
    t = tono_crea(0.5);
    for (int i = 0; i < 100; i++) {
        n = tono_pacchetto(t, q, &tm);
        CONTROLLA(ph_flusso_pacchetto(f, (int64_t)tm + 100 * SEC + 5500000, q, (size_t)n, ora += 20000, &e) == 0);
    }
    ph_flusso_contatori(f, &s);
    CONTROLLA(s.in_ritardo == 25);     /* 24 scartati + quello che fa risincronizzare */
    CONTROLLA(s.ricevuti == 300 + 100 - 24);
    tono_libera(t);
    ph_flusso_libera(f);
    ph_coda_libera(coda);
}

int main(void)
{
    decodifica_tono();
    flusso_buchi();
    return 0;
}
