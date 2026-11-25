/* Esportazione dei fotogrammi decodificati: DMA-BUF senza copie quando si
 * puo' (stessa via del prototipo strumenti/prototipo-gtk.c, provata sul
 * tablet), altrimenti memoria. */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>

#include "video/colore_interno.h"
#include "video/esporta_interno.h"

#define LATO_MAX 16384
#define FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define FOURCC_NV12 FOURCC('N', 'V', '1', '2')
#define FOURCC_P010 FOURCC('P', '0', '1', '0')

static int errore_av(PhErrore *e, const char *cosa, int codice)
{
    char testo[128];
    av_strerror(codice, testo, sizeof testo);
    return ph_errore(e, "%s: %s", cosa, testo);
}

/* Rilascio: chi tiene vivo il fotogramma (AVFrame mappato, o convertito, o
 * quello del decodificatore). av_frame_free va bene da qualunque thread. */
static void rilascia_frame(void *dati)
{
    AVFrame *f = dati;
    av_frame_free(&f);
}

static PhFotoColore colore_da(enum AVColorSpace cs, enum AVColorRange range, int altezza)
{
    bool pieno = range == AVCOL_RANGE_JPEG;
    switch (cs) {
    case AVCOL_SPC_BT709: return pieno ? PH_FOTO_COLORE_BT709_PIENO : PH_FOTO_COLORE_BT709_LIMITATO;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M: return pieno ? PH_FOTO_COLORE_BT601_PIENO : PH_FOTO_COLORE_BT601_LIMITATO;
    case AVCOL_SPC_UNSPECIFIED:
        if (pieno)
            return altezza >= 720 ? PH_FOTO_COLORE_BT709_PIENO : PH_FOTO_COLORE_BT601_PIENO;
        return altezza >= 720 ? PH_FOTO_COLORE_BT709_LIMITATO : PH_FOTO_COLORE_BT601_LIMITATO;
    default: return PH_FOTO_COLORE_PREDEFINITO;
    }
}

int ph_esporta_drm(AVFrame *drm, int larghezza, int altezza, enum AVPixelFormat sw_format,
                   enum AVColorSpace spazio, enum AVColorRange intervallo, uint64_t tempo_us,
                   PhFotogramma *uscita, PhErrore *e)
{
    *uscita = (PhFotogramma){0};
    const AVDRMFrameDescriptor *d = (const AVDRMFrameDescriptor *)drm->data[0];
    if (drm->format != AV_PIX_FMT_DRM_PRIME || d == NULL || d->nb_layers < 1 || d->nb_objects < 1 ||
        larghezza < 1 || altezza < 1 || larghezza > LATO_MAX || altezza > LATO_MAX) {
        av_frame_free(&drm);
        return ph_errore(e, "descrittore DMA-BUF assente o misure non valide");
    }
    PhFotogramma f = {.tipo = PH_FOTO_DMABUF, .larghezza = larghezza, .altezza = altezza,
                      .tempo_us = tempo_us};
    int n = 0;
    for (int l = 0; l < d->nb_layers; l++) {
        for (int k = 0; k < d->layers[l].nb_planes; k++) {
            const AVDRMPlaneDescriptor *pd = &d->layers[l].planes[k];
            if (n == PH_FOTO_PIANI_MAX || pd->object_index < 0 || pd->object_index >= d->nb_objects ||
                pd->offset < 0 || pd->pitch < 0 || (uint64_t)pd->offset > UINT32_MAX ||
                (uint64_t)pd->pitch > UINT32_MAX) {
                av_frame_free(&drm);
                return ph_errore(e, "descrittore DMA-BUF non gestito (piani, indici, offset o passo)");
            }
            const AVDRMObjectDescriptor *o = &d->objects[pd->object_index];
            if (o->fd < 0) {
                av_frame_free(&drm);
                return ph_errore(e, "descrittore DMA-BUF con fd non valido");
            }
            if (n == 0)
                f.modificatore = o->format_modifier;
            else if (o->format_modifier != f.modificatore) {
                av_frame_free(&drm);
                return ph_errore(e, "piani DMA-BUF con modificatori diversi");
            }
            f.fd[n] = o->fd;
            f.scostamento[n] = (uint32_t)pd->offset;
            f.passo_piano[n] = (uint32_t)pd->pitch;
            n++;
        }
    }
    if (n == 0) {
        av_frame_free(&drm);
        return ph_errore(e, "descrittore DMA-BUF senza piani");
    }
    /* Con i piani su livelli separati (R8 + GR88) il formato complessivo si
     * ricava dal formato dei pixel; con un livello solo e' quello del livello. */
    if (d->nb_layers == 1)
        f.fourcc = d->layers[0].format;
    else if (sw_format == AV_PIX_FMT_NV12)
        f.fourcc = FOURCC_NV12;
    else if (sw_format == AV_PIX_FMT_P010)
        f.fourcc = FOURCC_P010;
    else {
        av_frame_free(&drm);
        return ph_errore(e, "DMA-BUF a piu' livelli con formato non gestito");
    }
    f.piani = n;
    f.colore = colore_da(spazio, intervallo, altezza);
    f.rilascia = rilascia_frame;
    f.dati_rilascio = drm;
    *uscita = f;
    return PH_ESPORTA_DMABUF;
}

/* ---- Memoria ---------------------------------------------------------------*/
/* Forma MEMORIA. `m` (in memoria, di chi chiama la funzione) passa qui. */
static int esporta_memoria(AVFrame *m, PhFotogramma *uscita, PhErrore *e)
{
    enum AVPixelFormat fmt = (enum AVPixelFormat)m->format;
    if (m->width < 1 || m->height < 1 || m->width > LATO_MAX || m->height > LATO_MAX) {
        int w = m->width, h = m->height;
        av_frame_free(&m);
        return ph_errore(e, "fotogramma con misure %dx%d fuori dai limiti", w, h);
    }
    PhFotoFormato dir;
    bool diretto = true;
    if (fmt == AV_PIX_FMT_BGRA)
        dir = PH_FOTO_B8G8R8A8_PREMOLT;
    else if (fmt == AV_PIX_FMT_RGBA)
        dir = PH_FOTO_R8G8B8A8;
    else if (fmt == AV_PIX_FMT_RGB24)
        dir = PH_FOTO_R8G8B8;
    else
        diretto = false;
    if (!ph_video_piani_ok(m) && sws_isSupportedInput(fmt)) {
        av_frame_free(&m);
        return ph_errore(e, "fotogramma con piani o righe incoerenti");
    }
    if (diretto && ph_video_piani_ok(m)) {
        *uscita = (PhFotogramma){.tipo = PH_FOTO_MEMORIA, .larghezza = m->width, .altezza = m->height,
                                 .tempo_us = (uint64_t)m->pts, .formato = dir, .dati = m->data[0],
                                 .passo = (size_t)m->linesize[0], .rilascia = rilascia_frame,
                                 .dati_rilascio = m};
        return PH_ESPORTA_MEMORIA;
    }
    if (!sws_isSupportedInput(fmt)) {
        const char *nome = av_get_pix_fmt_name(fmt);
        int rc = ph_errore(e, "formato di fotogramma non esportabile: %s", nome != NULL ? nome : "?");
        av_frame_free(&m);
        return rc;
    }
    AVFrame *rgb = av_frame_alloc();
    if (rgb == NULL) {
        av_frame_free(&m);
        return ph_errore(e, "memoria finita");
    }
    rgb->format = AV_PIX_FMT_BGRA;
    rgb->width = m->width;
    rgb->height = m->height;
    if (av_frame_get_buffer(rgb, 0) < 0) {
        av_frame_free(&rgb);
        av_frame_free(&m);
        return ph_errore(e, "memoria finita");
    }
    struct SwsContext *sws = sws_getContext(m->width, m->height, fmt, m->width, m->height,
                                            AV_PIX_FMT_BGRA, SWS_BICUBIC | SWS_ACCURATE_RND, NULL,
                                            NULL, NULL);
    if (sws == NULL) {
        av_frame_free(&rgb);
        av_frame_free(&m);
        return ph_errore(e, "conversione di colore non disponibile");
    }
    bool pieno = m->color_range == AVCOL_RANGE_JPEG || fmt == AV_PIX_FMT_YUVJ420P ||
                 fmt == AV_PIX_FMT_YUVJ422P || fmt == AV_PIX_FMT_YUVJ444P;
    /* Con una sorgente RGB torna -1 (nessuna matrice da cambiare): e' normale, si ignora
     * di proposito; per YUV un esito sbagliato lascia la matrice predefinita e la
     * conversione resta valida. */
    (void)sws_setColorspaceDetails(sws, sws_getCoefficients(ph_video_matrice_sws(m)), pieno ? 1 : 0,
                             sws_getCoefficients(AVCOL_SPC_SMPTE170M), 1, 0, 1 << 16, 1 << 16);
    int r = sws_scale(sws, (const uint8_t *const *)m->data, m->linesize, 0, m->height, rgb->data,
                      rgb->linesize);
    sws_freeContext(sws);
    int altezza = m->height;
    int64_t pts = m->pts;
    av_frame_free(&m);
    if (r != altezza) {
        av_frame_free(&rgb);
        return ph_errore(e, "conversione di colore fallita");
    }
    *uscita = (PhFotogramma){.tipo = PH_FOTO_MEMORIA, .larghezza = rgb->width, .altezza = rgb->height,
                             .tempo_us = (uint64_t)pts, .formato = PH_FOTO_B8G8R8A8_PREMOLT,
                             .dati = rgb->data[0], .passo = (size_t)rgb->linesize[0],
                             .rilascia = rilascia_frame, .dati_rilascio = rgb};
    return PH_ESPORTA_MEMORIA;
}

int ph_esporta(AVFrame **fotogramma, PhFotogramma *uscita, PhErrore *e)
{
    if (uscita != NULL)
        *uscita = (PhFotogramma){0};
    if (fotogramma == NULL || *fotogramma == NULL || uscita == NULL)
        return ph_errore(e, "esportazione: argomenti mancanti");
    AVFrame *f = *fotogramma;
    *fotogramma = NULL; /* da qui e' nostro */
    if (f->pts < 0)
        f->pts = 0;
    if (f->hw_frames_ctx == NULL)
        return esporta_memoria(f, uscita, e);

    /* Nella memoria della scheda grafica: prima la via diretta (VA-API). */
    if (f->format == AV_PIX_FMT_VAAPI) {
        AVFrame *d = av_frame_alloc();
        if (d != NULL) {
            d->format = AV_PIX_FMT_DRM_PRIME;
            if (av_hwframe_map(d, f, AV_HWFRAME_MAP_READ) == 0) {
                enum AVPixelFormat sw = ((AVHWFramesContext *)f->hw_frames_ctx->data)->sw_format;
                int w = f->width, h = f->height;
                uint64_t t = (uint64_t)f->pts;
                enum AVColorSpace cs = f->colorspace;
                enum AVColorRange cr = f->color_range;
                av_frame_free(&f); /* il fotogramma mappato tiene un suo riferimento */
                return ph_esporta_drm(d, w, h, sw, cs, cr, t, uscita, e) < 0
                           ? -1
                           : PH_ESPORTA_DMABUF;
            }
            av_frame_free(&d);
        }
        /* mappa rifiutata: ripiego */
    }
    AVFrame *m = av_frame_alloc();
    if (m == NULL) {
        av_frame_free(&f);
        return ph_errore(e, "memoria finita");
    }
    int r = av_hwframe_transfer_data(m, f, 0);
    if (r < 0) {
        av_frame_free(&m);
        av_frame_free(&f);
        return errore_av(e, "copia del fotogramma dalla scheda grafica", r);
    }
    m->pts = f->pts;
    m->colorspace = f->colorspace;
    m->color_range = f->color_range;
    m->width = f->width;
    m->height = f->height;
    av_frame_free(&f);
    int via = esporta_memoria(m, uscita, e);
    return via < 0 ? -1 : PH_ESPORTA_RIPIEGO;
}
