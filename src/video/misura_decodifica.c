/* Strumento di misura (non entra nell'app): decodifica un file H.264 grezzo
 * (Annex B, come quello salvato da yasde-prova video) con la via chiesta
 * e stampa quanti fotogrammi e in quanto tempo.
 * Uso: misura_decodifica FILE.h264 [automatica|vulkan|vaapi|software] [giri] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>

#include "video/decodifica.h"

static double adesso(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "uso: %s FILE.h264 [automatica|vulkan|vaapi|software] [giri]\n", argv[0]);
        return 2;
    }
    PhVia via = PH_VIA_AUTOMATICA;
    if (argc > 2) {
        if (strcmp(argv[2], "vulkan") == 0) via = PH_VIA_VULKAN;
        else if (strcmp(argv[2], "vaapi") == 0) via = PH_VIA_VAAPI;
        else if (strcmp(argv[2], "software") == 0) via = PH_VIA_SOFTWARE;
    }
    int giri = argc > 3 ? atoi(argv[3]) : 1;
    FILE *f = fopen(argv[1], "rb");
    if (f == NULL) {
        perror(argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long lungo = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *dati = malloc((size_t)lungo + AV_INPUT_BUFFER_PADDING_SIZE);
    if (dati == NULL || fread(dati, 1, (size_t)lungo, f) != (size_t)lungo) {
        fprintf(stderr, "lettura fallita\n");
        return 1;
    }
    memset(dati + lungo, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    fclose(f);

    printf("decodifica Vulkan di fabbrica: %s\n", ph_decodifica_vulkan_di_fabbrica() ? "si" : "no");
    int esito = 0;
    for (int g = 0; g < giri && esito == 0; g++) {
        PhErrore e = {0};
        PhDecodifica *d = ph_decodifica_crea(via, &e);
        if (d == NULL) {
            fprintf(stderr, "errore: %s\n", e.testo);
            return 1;
        }
        /* Il file non ha i confini dei pacchetti: li ritrova FFmpeg. */
        AVCodecParserContext *taglia = av_parser_init(AV_CODEC_ID_H264);
        AVCodecContext *finto = avcodec_alloc_context3(NULL);
        long fotogrammi = 0, pacchetti = 0, errori = 0;
        const char *formato = "?";
        int larghezza = 0, altezza = 0;
        double peggiore = 0, inizio = adesso();
        const uint8_t *p = dati;
        long resto = lungo;
        while (resto > 0 || p != NULL) {
            uint8_t *uscita = NULL;
            int n_uscita = 0;
            int usati = av_parser_parse2(taglia, finto, &uscita, &n_uscita, resto > 0 ? p : NULL,
                                         (int)resto, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (usati < 0)
                break;
            if (resto > 0) {
                p += usati;
                resto -= usati;
            } else if (n_uscita == 0) {
                break;
            }
            if (n_uscita == 0)
                continue;
            double t0 = adesso();
            pacchetti++;
            if (ph_decodifica_dai(d, false, uscita, (size_t)n_uscita, (uint64_t)pacchetti * 16667,
                                  &e) != 0) {
                errori++;
                if (errori == 1)
                    fprintf(stderr, "errore: %s\n", e.testo);
            }
            AVFrame *fr = NULL;
            int r;
            while ((r = ph_decodifica_prendi(d, &fr, &e)) == 1) {
                fotogrammi++;
                formato = av_get_pix_fmt_name(fr->format);
                larghezza = fr->width;
                altezza = fr->height;
                av_frame_free(&fr);
            }
            double dt = adesso() - t0;
            if (dt > peggiore)
                peggiore = dt;
        }
        double durata = adesso() - inizio;
        printf("via %-8s %dx%d formato %-8s pacchetti %ld fotogrammi %ld errori %ld  "
               "media %.2f ms  peggiore %.2f ms  (%.0f al secondo)\n",
               ph_via_nome(ph_decodifica_via(d)), larghezza, altezza, formato, pacchetti,
               fotogrammi, errori, fotogrammi ? durata * 1000.0 / (double)fotogrammi : 0.0,
               peggiore * 1000.0, fotogrammi ? (double)fotogrammi / durata : 0.0);
        if (fotogrammi == 0 || errori > 0)
            esito = 1;
        av_parser_close(taglia);
        avcodec_free_context(&finto);
        ph_decodifica_libera(d);
    }
    free(dati);
    return esito;
}
