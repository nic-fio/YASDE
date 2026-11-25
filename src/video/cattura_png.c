/* Screenshot: fotogramma decodificato -> RGB24 (libswscale) -> PNG
 * (codificatore PNG di libavcodec). */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>

#include "video/cattura_interno.h"
#include "video/colore_interno.h"

#define LATO_MAX 16384

static int errore_av(PhErrore *e, const char *cosa, int codice)
{
    char testo[128];
    av_strerror(codice, testo, sizeof testo);
    return ph_errore(e, "%s: %s", cosa, testo);
}

/* Converte in un fotogramma RGB24 nuovo (libswscale). NULL a errore. */
static AVFrame *a_rgb24(const AVFrame *f, PhErrore *e)
{
    enum AVPixelFormat fmt = (enum AVPixelFormat)f->format;
    if (!sws_isSupportedInput(fmt)) {
        const char *nome = av_get_pix_fmt_name(fmt);
        ph_errore(e, "formato di fotogramma non supportato per lo screenshot: %s",
                  nome != NULL ? nome : "sconosciuto");
        return NULL;
    }
    if (!ph_video_piani_ok(f)) {
        ph_errore(e, "fotogramma con piani o righe incoerenti");
        return NULL;
    }
    AVFrame *rgb = av_frame_alloc();
    if (rgb == NULL) {
        ph_errore(e, "memoria finita per lo screenshot");
        return NULL;
    }
    rgb->format = AV_PIX_FMT_RGB24;
    rgb->width = f->width;
    rgb->height = f->height;
    if (av_frame_get_buffer(rgb, 0) < 0) {
        av_frame_free(&rgb);
        ph_errore(e, "memoria finita per lo screenshot");
        return NULL;
    }
    struct SwsContext *sws = sws_getContext(f->width, f->height, fmt, f->width, f->height,
                                            AV_PIX_FMT_RGB24, SWS_BICUBIC | SWS_ACCURATE_RND, NULL,
                                            NULL, NULL);
    if (sws == NULL) {
        av_frame_free(&rgb);
        ph_errore(e, "conversione di colore non disponibile per %s", av_get_pix_fmt_name(fmt));
        return NULL;
    }
    bool pieno = f->color_range == AVCOL_RANGE_JPEG || fmt == AV_PIX_FMT_YUVJ420P ||
                 fmt == AV_PIX_FMT_YUVJ422P || fmt == AV_PIX_FMT_YUVJ444P;
    /* Solo per i formati YUV ha senso: con una sorgente RGB torna -1 (non c'e' una
     * matrice da cambiare) ed e' normale, quindi il risultato si ignora di proposito;
     * per YUV l'esito sbagliato lascerebbe la matrice predefinita, ma la conversione
     * resta valida. */
    (void)sws_setColorspaceDetails(sws, sws_getCoefficients(ph_video_matrice_sws(f)), pieno ? 1 : 0,
                             sws_getCoefficients(AVCOL_SPC_SMPTE170M), 1, 0, 1 << 16, 1 << 16);
    int r = sws_scale(sws, (const uint8_t *const *)f->data, f->linesize, 0, f->height, rgb->data,
                      rgb->linesize);
    sws_freeContext(sws);
    if (r != f->height) {
        av_frame_free(&rgb);
        ph_errore(e, "conversione di colore fallita");
        return NULL;
    }
    return rgb;
}

/* Codifica RGB24 compatto in PNG (memoria di malloc). */
static int codifica_png(const AVFrame *rgb, uint8_t **dati, size_t *n, PhErrore *e)
{
    int esito = -1;
    const AVCodec *cod = avcodec_find_encoder(AV_CODEC_ID_PNG);
    AVCodecContext *c = cod != NULL ? avcodec_alloc_context3(cod) : NULL;
    AVPacket *pk = av_packet_alloc();
    if (cod == NULL || c == NULL || pk == NULL) {
        ph_errore(e, "codificatore PNG non disponibile");
        goto fine;
    }
    c->width = rgb->width;
    c->height = rgb->height;
    c->pix_fmt = AV_PIX_FMT_RGB24;
    c->time_base = (AVRational){1, 1};
    int r = avcodec_open2(c, cod, NULL);
    if (r < 0) {
        errore_av(e, "apertura del codificatore PNG", r);
        goto fine;
    }
    r = avcodec_send_frame(c, rgb);
    if (r >= 0)
        r = avcodec_send_frame(c, NULL);
    if (r < 0) {
        errore_av(e, "codifica PNG", r);
        goto fine;
    }
    r = avcodec_receive_packet(c, pk);
    if (r < 0) {
        errore_av(e, "lettura del PNG codificato", r);
        goto fine;
    }
    uint8_t *copia = malloc((size_t)pk->size);
    if (copia == NULL) {
        ph_errore(e, "memoria finita per lo screenshot");
        goto fine;
    }
    memcpy(copia, pk->data, (size_t)pk->size);
    *dati = copia;
    *n = (size_t)pk->size;
    esito = 0;
fine:
    av_packet_free(&pk);
    avcodec_free_context(&c);
    return esito;
}

int ph_cattura_png_memoria(const AVFrame *f, uint8_t **dati, size_t *n, PhErrore *e)
{
    if (dati != NULL)
        *dati = NULL;
    if (n != NULL)
        *n = 0;
    if (f == NULL || dati == NULL || n == NULL)
        return ph_errore(e, "screenshot: argomenti mancanti");
    if (f->width < 1 || f->height < 1 || f->width > LATO_MAX || f->height > LATO_MAX)
        return ph_errore(e, "screenshot: misure %dx%d fuori dai limiti", f->width, f->height);
    AVFrame *sw = NULL;
    const AVFrame *src = f;
    if (f->hw_frames_ctx != NULL) {
        /* Fotogramma nella memoria della scheda grafica: copia in memoria normale. */
        sw = av_frame_alloc();
        if (sw == NULL)
            return ph_errore(e, "memoria finita per lo screenshot");
        int r = av_hwframe_transfer_data(sw, f, 0);
        if (r < 0) {
            av_frame_free(&sw);
            return errore_av(e, "copia del fotogramma dalla scheda grafica", r);
        }
        sw->colorspace = f->colorspace;
        sw->color_range = f->color_range;
        sw->width = f->width;
        sw->height = f->height;
        src = sw;
        if (src->width < 1 || src->height < 1 || src->width > LATO_MAX || src->height > LATO_MAX) {
            av_frame_free(&sw);
            return ph_errore(e, "screenshot: misure fuori dai limiti");
        }
    }
    AVFrame *rgb = a_rgb24(src, e);
    int esito = -1;
    if (rgb != NULL) {
        esito = codifica_png(rgb, dati, n, e);
        av_frame_free(&rgb);
    }
    av_frame_free(&sw);
    return esito;
}

int ph_cattura_screenshot(const AVFrame *f, const char *cartella, const char *app, char *percorso,
                          size_t n_percorso, PhErrore *e)
{
    uint8_t *png = NULL;
    size_t n = 0;
    /* Prima il PNG: se la codifica fallisce non si crea nessun file. */
    if (ph_cattura_png_memoria(f, &png, &n, e) != 0)
        return -1;
    char scelto[4096];
    int fd = ph_cattura_apri_nuovo(cartella, app, "png", time(NULL), scelto, sizeof scelto, e);
    if (fd < 0) {
        free(png);
        return -1;
    }
    int esito = 0;
    if (ph_cattura_scrivi_tutto(fd, png, n) != 0) {
        char t[128];
        ph_cattura_testo_errno(errno, t, sizeof t);
        ph_errore(e, "scrittura dello screenshot in '%s': %s", scelto, t);
        esito = -1;
    }
    if (close(fd) != 0 && esito == 0) {
        char t[128];
        ph_cattura_testo_errno(errno, t, sizeof t);
        ph_errore(e, "chiusura dello screenshot '%s': %s", scelto, t);
        esito = -1;
    }
    free(png);
    if (esito != 0) {
        unlink(scelto); /* niente PNG a meta' */
        return -1;
    }
    if (percorso != NULL) {
        if (strlen(scelto) >= n_percorso) {
            /* Il file c'e' ma il chiamante non puo' saperne il nome: meglio
             * annullare che lasciare un file orfano. */
            unlink(scelto);
            return ph_errore(e, "percorso dello screenshot troppo lungo per il buffer");
        }
        memcpy(percorso, scelto, strlen(scelto) + 1);
    }
    return 0;
}
