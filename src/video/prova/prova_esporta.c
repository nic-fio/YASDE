/* Prova di ph_esporta: forma MEMORIA con fotogrammi sintetici, rilascio una
 * volta sola, fallimenti a meta', e la logica DMA-BUF con descrittori finti
 * (la mappatura VA-API vera richiede una scheda: non si puo' eseguire qui). */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include <libavutil/frame.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/imgutils.h>

#include "aiuti.h"
#include "video/esporta_interno.h"

#define FOURCC_NV12_PROVA 0x3231564eu /* "NV12" */

static AVFrame *crea(enum AVPixelFormat fmt, int w, int h)
{
    AVFrame *f = av_frame_alloc();
    CONTROLLA(f != NULL);
    f->format = fmt;
    f->width = w;
    f->height = h;
    f->pts = 123456;
    CONTROLLA(av_frame_get_buffer(f, 0) >= 0);
    return f;
}

static void *rilascia_in_thread(void *p)
{
    ph_fotogramma_rilascia(p);
    return NULL;
}

static void prova_memoria(void)
{
    PhErrore e = {0};
    PhFotogramma o;
    /* YUV420P -> BGRA: un colore uniforme (BT.601 limitato, piccolo e non dichiarato). */
    AVFrame *f = crea(AV_PIX_FMT_YUV420P, 64, 48);
    /* (200,40,40): Y=81+... calcolato con BT.601: Y=0.299*200+0.587*40+0.114*40=87.9; Cb,Cr */
    double Y = 0.299 * 200 + 0.587 * 40 + 0.114 * 40, Cb = (40 - Y) / 1.772, Cr = (200 - Y) / 1.402;
    memset(f->data[0], (int)(16 + Y * 219 / 255 + 0.5), (size_t)f->linesize[0] * 48);
    memset(f->data[1], (int)(128 + Cb * 224 / 255 + 0.5), (size_t)f->linesize[1] * 24);
    memset(f->data[2], (int)(128 + Cr * 224 / 255 + 0.5), (size_t)f->linesize[2] * 24);
    AVFrame *orig = f;
    CONTROLLA(ph_esporta(&f, &o, &e) == PH_ESPORTA_MEMORIA);
    CONTROLLA(f == NULL && o.tipo == PH_FOTO_MEMORIA && o.formato == PH_FOTO_B8G8R8A8_PREMOLT);
    CONTROLLA(o.larghezza == 64 && o.altezza == 48 && o.tempo_us == 123456 && o.passo >= 64 * 4);
    CONTROLLA(o.dati != NULL && o.rilascia != NULL);
    (void)orig;
    for (int y = 0; y < 48; y += 7)
        for (int x = 0; x < 64; x += 9) {
            const uint8_t *p = o.dati + (size_t)y * o.passo + (size_t)x * 4; /* B G R A */
            CONTROLLA(abs(p[2] - 200) <= 5 && abs(p[1] - 40) <= 5 && abs(p[0] - 40) <= 5 && p[3] == 255);
        }
    /* Rilascio da un altro thread; una seconda chiamata non fa nulla. */
    pthread_t th;
    PhFotogramma copia = o;
    CONTROLLA(pthread_create(&th, NULL, rilascia_in_thread, &o) == 0);
    CONTROLLA(pthread_join(th, NULL) == 0);
    CONTROLLA(o.rilascia == NULL && o.dati == NULL && o.larghezza == 0);
    ph_fotogramma_rilascia(&o);
    (void)copia;

    /* Formati gia' previsti: nessuna copia, il buffer e' quello del decodificatore. */
    struct {
        enum AVPixelFormat fmt;
        PhFotoFormato atteso;
        int byte;
    } d[] = {{AV_PIX_FMT_BGRA, PH_FOTO_B8G8R8A8_PREMOLT, 4},
             {AV_PIX_FMT_RGBA, PH_FOTO_R8G8B8A8, 4},
             {AV_PIX_FMT_RGB24, PH_FOTO_R8G8B8, 3}};
    for (int i = 0; i < 3; i++) {
        f = crea(d[i].fmt, 33, 17);
        const uint8_t *buffer = f->data[0];
        int passo = f->linesize[0];
        CONTROLLA(ph_esporta(&f, &o, &e) == PH_ESPORTA_MEMORIA);
        CONTROLLA(o.dati == buffer && o.passo == (size_t)passo && o.formato == d[i].atteso);
        CONTROLLA(o.larghezza == 33 && o.altezza == 17 && o.tempo_us == 123456);
        ph_fotogramma_rilascia(&o);
    }
    /* BGR0 (alfa non definito) non si usa com'e': passa dalla conversione. */
    f = crea(AV_PIX_FMT_BGR0, 16, 16);
    const uint8_t *buffer = f->data[0];
    CONTROLLA(ph_esporta(&f, &o, &e) == PH_ESPORTA_MEMORIA && o.dati != buffer);
    ph_fotogramma_rilascia(&o);

    /* Errori: il fotogramma passa comunque alla funzione (niente perdite sotto ASan). */
    f = crea(AV_PIX_FMT_YUV420P, 16, 16);
    f->format = AV_PIX_FMT_VAAPI; /* senza contesto hardware: non esportabile */
    CONTROLLA(ph_esporta(&f, &o, &e) == -1 && f == NULL && o.rilascia == NULL && e.testo[0] != 0);
    f = crea(AV_PIX_FMT_YUV420P, 16, 16);
    uint8_t *piano1 = f->data[1];
    f->data[1] = NULL;
    AVFrame *tmp = f;
    CONTROLLA(ph_esporta(&f, &o, &e) == -1 && o.rilascia == NULL);
    (void)piano1;
    (void)tmp;
    f = crea(AV_PIX_FMT_BGRA, 16, 16);
    f->width = 20000;
    CONTROLLA(ph_esporta(&f, &o, &e) == -1);
    CONTROLLA(ph_esporta(NULL, &o, &e) == -1);
    AVFrame *nulla = NULL;
    CONTROLLA(ph_esporta(&nulla, &o, &e) == -1);
    f = crea(AV_PIX_FMT_BGRA, 16, 16);
    CONTROLLA(ph_esporta(&f, NULL, &e) == -1); /* uscita assente: il fotogramma non e' consumato */
    av_frame_free(&f);
}

/* ---- DMA-BUF con descrittori finti ------------------------------------------*/
static int g_chiusi;

static void libera_descrittore(void *opaque, uint8_t *dati)
{
    (void)opaque;
    AVDRMFrameDescriptor *d = (AVDRMFrameDescriptor *)dati;
    for (int i = 0; i < d->nb_objects; i++)
        if (d->objects[i].fd >= 0) {
            CONTROLLA(close(d->objects[i].fd) == 0); /* un secondo close darebbe EBADF */
            g_chiusi++;
        }
    av_free(dati);
}

static AVFrame *finto_drm(int n_oggetti, int n_livelli)
{
    AVDRMFrameDescriptor *d = av_mallocz(sizeof *d);
    CONTROLLA(d != NULL);
    d->nb_objects = n_oggetti;
    for (int i = 0; i < n_oggetti; i++) {
        d->objects[i].fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
        CONTROLLA(d->objects[i].fd >= 0);
        d->objects[i].size = 4096;
        d->objects[i].format_modifier = 0x0100000000000001ULL;
    }
    d->nb_layers = n_livelli;
    AVFrame *f = av_frame_alloc();
    CONTROLLA(f != NULL);
    f->format = AV_PIX_FMT_DRM_PRIME;
    f->data[0] = (uint8_t *)d;
    f->buf[0] = av_buffer_create((uint8_t *)d, sizeof *d, libera_descrittore, NULL, 0);
    CONTROLLA(f->buf[0] != NULL);
    return f;
}

static AVDRMFrameDescriptor *desc(AVFrame *f)
{
    return (AVDRMFrameDescriptor *)f->data[0];
}

static void prova_drm(void)
{
    PhErrore e = {0};
    PhFotogramma o;
    /* NV12 in un livello solo, due piani nello stesso oggetto. */
    AVFrame *f = finto_drm(1, 1);
    AVDRMFrameDescriptor *d = desc(f);
    d->layers[0].format = FOURCC_NV12_PROVA;
    d->layers[0].nb_planes = 2;
    d->layers[0].planes[0] = (AVDRMPlaneDescriptor){0, 0, 2048};
    d->layers[0].planes[1] = (AVDRMPlaneDescriptor){0, 1048576, 2048};
    int fd0 = d->objects[0].fd;
    g_chiusi = 0;
    CONTROLLA(ph_esporta_drm(f, 1280, 720, AV_PIX_FMT_NV12, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, 777, &o, &e) == PH_ESPORTA_DMABUF);
    CONTROLLA(o.tipo == PH_FOTO_DMABUF && o.piani == 2 && o.larghezza == 1280 && o.altezza == 720);
    CONTROLLA(o.fourcc == FOURCC_NV12_PROVA && o.modificatore == 0x0100000000000001ULL && o.tempo_us == 777);
    CONTROLLA(o.fd[0] == fd0 && o.fd[1] == fd0 && o.scostamento[1] == 1048576 && o.passo_piano[0] == 2048);
    CONTROLLA(o.colore == PH_FOTO_COLORE_BT709_LIMITATO);
    CONTROLLA(g_chiusi == 0 && fcntl(fd0, F_GETFD) >= 0); /* ancora aperto: la vita e' di chi riceve */
    ph_fotogramma_rilascia(&o);
    CONTROLLA(g_chiusi == 1 && fcntl(fd0, F_GETFD) < 0 && errno == EBADF); /* chiuso una volta sola */
    ph_fotogramma_rilascia(&o);
    CONTROLLA(g_chiusi == 1);

    /* Due livelli (R8 + GR88), due oggetti: il fourcc viene dal formato dei pixel. */
    f = finto_drm(2, 2);
    d = desc(f);
    d->layers[0].format = 0x20203852; /* R8 */
    d->layers[0].nb_planes = 1;
    d->layers[0].planes[0] = (AVDRMPlaneDescriptor){0, 0, 2048};
    d->layers[1].format = 0x38385247; /* GR88 */
    d->layers[1].nb_planes = 1;
    d->layers[1].planes[0] = (AVDRMPlaneDescriptor){1, 0, 2048};
    g_chiusi = 0;
    CONTROLLA(ph_esporta_drm(f, 640, 360, AV_PIX_FMT_NV12, AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, 1, &o, &e) == PH_ESPORTA_DMABUF);
    CONTROLLA(o.fourcc == FOURCC_NV12_PROVA && o.piani == 2 && o.fd[0] != o.fd[1]);
    CONTROLLA(o.colore == PH_FOTO_COLORE_BT601_LIMITATO); /* non dichiarato e basso: BT.601 */
    ph_fotogramma_rilascia(&o);
    CONTROLLA(g_chiusi == 2);

    /* Offset o passo oltre 32 bit: errore, descrittori chiusi una volta. */
    for (int caso = 0; caso < 2; caso++) {
        f = finto_drm(1, 1);
        d = desc(f);
        d->layers[0].format = FOURCC_NV12_PROVA;
        d->layers[0].nb_planes = 1;
        d->layers[0].planes[0] = (AVDRMPlaneDescriptor){0, 0, 2048};
        if (caso == 0)
            d->layers[0].planes[0].offset = (ptrdiff_t)1 << 32;
        else
            d->layers[0].planes[0].pitch = (ptrdiff_t)1 << 32;
        g_chiusi = 0;
        CONTROLLA(ph_esporta_drm(f, 640, 360, AV_PIX_FMT_NV12, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, 1, &o, &e) == -1);
        CONTROLLA(g_chiusi == 1 && o.rilascia == NULL);
    }

    /* Colori. */
    struct {
        enum AVColorSpace cs;
        enum AVColorRange r;
        int h;
        PhFotoColore atteso;
    } col[] = {{AVCOL_SPC_BT709, AVCOL_RANGE_JPEG, 720, PH_FOTO_COLORE_BT709_PIENO},
               {AVCOL_SPC_BT470BG, AVCOL_RANGE_MPEG, 720, PH_FOTO_COLORE_BT601_LIMITATO},
               {AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, 1080, PH_FOTO_COLORE_BT709_LIMITATO},
               {AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_MPEG, 1080, PH_FOTO_COLORE_PREDEFINITO},
               {AVCOL_SPC_BT470BG, AVCOL_RANGE_JPEG, 720, PH_FOTO_COLORE_BT601_PIENO},
               {AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_JPEG, 480, PH_FOTO_COLORE_BT601_PIENO},
               {AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_JPEG, 1080, PH_FOTO_COLORE_BT709_PIENO}};
    for (int i = 0; i < 7; i++) {
        f = finto_drm(1, 1);
        d = desc(f);
        d->layers[0].format = FOURCC_NV12_PROVA;
        d->layers[0].nb_planes = 1;
        d->layers[0].planes[0] = (AVDRMPlaneDescriptor){0, 0, 2048};
        CONTROLLA(ph_esporta_drm(f, 100, col[i].h, AV_PIX_FMT_NV12, col[i].cs, col[i].r, 1, &o, &e) == 0);
        CONTROLLA(o.colore == col[i].atteso);
        ph_fotogramma_rilascia(&o);
    }

    /* Fallimenti: il fotogramma finto passa alla funzione, i descrittori si chiudono una volta e
     * l'uscita e' azzerata. */
    for (int caso = 0; caso < 7; caso++) {
        f = finto_drm(2, caso == 5 || caso == 1 ? 2 : 1);
        d = desc(f);
        d->layers[0].format = FOURCC_NV12_PROVA;
        d->layers[0].nb_planes = 1;
        d->layers[0].planes[0] = (AVDRMPlaneDescriptor){0, 0, 2048};
        int larg = 640, sw = AV_PIX_FMT_NV12;
        switch (caso) {
        case 0: /* modificatori diversi */
            d->layers[0].nb_planes = 2;
            d->layers[0].planes[1] = (AVDRMPlaneDescriptor){1, 0, 2048};
            d->objects[1].format_modifier = 5;
            break;
        case 1: /* troppi piani (3 + 3 su due livelli) */
            d->layers[0].nb_planes = 3;
            d->layers[1].nb_planes = 3;
            for (int k = 0; k < 3; k++)
                d->layers[0].planes[k] = d->layers[1].planes[k] = (AVDRMPlaneDescriptor){0, 0, 64};
            break;
        case 2: /* indice di oggetto fuori misura */
            d->layers[0].planes[0].object_index = 2;
            break;
        case 3: /* fd non valido: se ne chiude uno solo */
            d->objects[0].fd = -1;
            break;
        case 4: /* misure non valide */
            larg = 0;
            break;
        case 5: /* due livelli con un formato di pixel non gestito */
            d->layers[1].format = 1;
            d->layers[1].nb_planes = 1;
            d->layers[1].planes[0] = (AVDRMPlaneDescriptor){1, 0, 64};
            sw = AV_PIX_FMT_YUV420P;
            break;
        case 6: /* nessun piano */
            d->layers[0].nb_planes = 0;
            break;
        }
        int da_chiudere = d->objects[0].fd >= 0 ? 2 : 1;
        g_chiusi = 0;
        CONTROLLA(ph_esporta_drm(f, larg, 360, sw, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, 1, &o, &e) == -1);
        CONTROLLA(o.rilascia == NULL && o.piani == 0 && e.testo[0] != 0);
        CONTROLLA(g_chiusi == da_chiudere);
    }
    /* Frame che non e' DRM_PRIME. */
    f = av_frame_alloc();
    f->format = AV_PIX_FMT_NV12;
    CONTROLLA(ph_esporta_drm(f, 16, 16, AV_PIX_FMT_NV12, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, 1, &o, &e) == -1);
}

int main(void)
{
    prova_memoria();
    prova_drm();
    printf("prova_esporta: ok\n");
    return 0;
}
