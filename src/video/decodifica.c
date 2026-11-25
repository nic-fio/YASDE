#include "video/decodifica.h"

#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <vulkan/vulkan.h>

struct PhDecodifica {
    PhVia via;
    AVCodecContext *codec;
    AVBufferRef *dispositivo;      /* scheda grafica per le vie accelerate */
    enum AVPixelFormat formato_hw; /* formato atteso dalla via scelta */
    bool ripiego_software;         /* la via accelerata ha rinunciato a meta' */
    AVPacket *pacchetto;
    uint8_t *configurazione;       /* SPS/PPS da anteporre al prossimo fotogramma */
    size_t n_configurazione;
};

const char *ph_via_nome(PhVia via)
{
    switch (via) {
    case PH_VIA_VULKAN: return "vulkan";
    case PH_VIA_VAAPI: return "vaapi";
    case PH_VIA_SOFTWARE: return "software";
    default: return "automatica";
    }
}

static int errore_av(PhErrore *e, const char *cosa, int codice)
{
    char testo[128];
    av_strerror(codice, testo, sizeof testo);
    return ph_errore(e, "%s: %s", cosa, testo);
}

bool ph_decodifica_vulkan_di_fabbrica(void)
{
    /* Si guarda che cosa dichiara la scheda, senza toccare variabili
     * d'ambiente: una funzione che il driver tiene spenta non conta. */
    bool trovata = false;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo = &app};
    VkInstance istanza;
    if (vkCreateInstance(&ci, NULL, &istanza) != VK_SUCCESS)
        return false;
    uint32_t n = 0;
    if (vkEnumeratePhysicalDevices(istanza, &n, NULL) == VK_SUCCESS && n > 0) {
        VkPhysicalDevice *schede = calloc(n, sizeof *schede);
        if (schede != NULL && vkEnumeratePhysicalDevices(istanza, &n, schede) >= VK_SUCCESS) {
            for (uint32_t i = 0; i < n && !trovata; i++) {
                VkPhysicalDeviceProperties p;
                vkGetPhysicalDeviceProperties(schede[i], &p);
                if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
                    continue; /* il disegno via software non e' una scheda */
                uint32_t ne = 0;
                if (vkEnumerateDeviceExtensionProperties(schede[i], NULL, &ne, NULL) != VK_SUCCESS)
                    continue;
                VkExtensionProperties *est = calloc(ne ? ne : 1, sizeof *est);
                if (est != NULL &&
                    vkEnumerateDeviceExtensionProperties(schede[i], NULL, &ne, est) >= VK_SUCCESS) {
                    for (uint32_t j = 0; j < ne; j++)
                        if (strcmp(est[j].extensionName, "VK_KHR_video_decode_h264") == 0)
                            trovata = true;
                }
                free(est);
            }
        }
        free(schede);
    }
    vkDestroyInstance(istanza, NULL);
    return trovata;
}

/* FFmpeg chiede quale formato usare tra quelli possibili: scegliamo quello
 * della via accelerata; se non c'e' (la scheda non regge questo flusso)
 * annotiamo il ripiego, cosi' chi ha chiesto una via precisa lo viene a sapere. */
static enum AVPixelFormat scegli_formato(AVCodecContext *c, const enum AVPixelFormat *formati)
{
    PhDecodifica *d = c->opaque;
    for (const enum AVPixelFormat *f = formati; *f != AV_PIX_FMT_NONE; f++)
        if (*f == d->formato_hw)
            return *f;
    d->ripiego_software = true;
    return avcodec_default_get_format(c, formati);
}

static PhDecodifica *prova_via(PhVia via, PhErrore *e)
{
    PhDecodifica *d = calloc(1, sizeof *d);
    if (d == NULL) {
        ph_errore(e, "memoria finita");
        return NULL;
    }
    d->via = via;
    d->formato_hw = AV_PIX_FMT_NONE;
    const AVCodec *h264 = avcodec_find_decoder(AV_CODEC_ID_H264);
    d->pacchetto = av_packet_alloc();
    d->codec = h264 ? avcodec_alloc_context3(h264) : NULL;
    if (d->codec == NULL || d->pacchetto == NULL) {
        ph_errore(e, "decodificatore H.264 non disponibile");
        goto errore;
    }
    d->codec->opaque = d;
    /* Latenza minima: nessun fotogramma trattenuto in attesa di riordino. */
    d->codec->flags |= AV_CODEC_FLAG_LOW_DELAY;
    d->codec->flags2 |= AV_CODEC_FLAG2_FAST;

    if (via == PH_VIA_VULKAN || via == PH_VIA_VAAPI) {
        enum AVHWDeviceType tipo =
            via == PH_VIA_VULKAN ? AV_HWDEVICE_TYPE_VULKAN : AV_HWDEVICE_TYPE_VAAPI;
        d->formato_hw = via == PH_VIA_VULKAN ? AV_PIX_FMT_VULKAN : AV_PIX_FMT_VAAPI;
        int r = av_hwdevice_ctx_create(&d->dispositivo, tipo, NULL, NULL, 0);
        if (r < 0) {
            errore_av(e, "accelerazione non disponibile", r);
            ph_errore_contesto(e, "via %s", ph_via_nome(via));
            goto errore;
        }
        d->codec->hw_device_ctx = av_buffer_ref(d->dispositivo);
        if (d->codec->hw_device_ctx == NULL) {
            ph_errore(e, "memoria finita");
            goto errore;
        }
        d->codec->get_format = scegli_formato;
        /* Chi mostra i fotogrammi senza copie (la shell) ne tiene alcuni oltre
         * la vita del decodificatore: quello mostrato, quello in attesa, uno in
         * consegna. Senza superfici in piu' la riserva fissa della scheda
         * grafica si esaurisce e la decodifica si ferma. */
        d->codec->extra_hw_frames = 6;
    } else {
        /* Via software: un thread per fetta, senza ritardo di fotogrammi. */
        d->codec->thread_type = FF_THREAD_SLICE;
        d->codec->thread_count = 0;
    }
    int r = avcodec_open2(d->codec, h264, NULL);
    if (r < 0) {
        errore_av(e, "apertura del decodificatore", r);
        goto errore;
    }
    return d;
errore:
    ph_decodifica_libera(d);
    return NULL;
}

size_t ph_decodifica_ordine_automatico(bool vulkan_di_fabbrica, PhVia sequenza[3])
{
    /* VA-API per prima: consegna il fotogramma alla shell senza copie
     * (DMA-BUF). La via Vulkan oggi lo copia in memoria (vedi esporta.h), quindi
     * resta come ripiego dove VA-API non c'e', per esempio con driver che
     * offrono solo Vulkan Video. Il software e' sempre l'ultimo. */
    size_t n = 0;
    sequenza[n++] = PH_VIA_VAAPI;
    if (vulkan_di_fabbrica)
        sequenza[n++] = PH_VIA_VULKAN;
    sequenza[n++] = PH_VIA_SOFTWARE;
    return n;
}

PhDecodifica *ph_decodifica_crea(PhVia via, PhErrore *e)
{
    if (via != PH_VIA_AUTOMATICA)
        return prova_via(via, e);
    PhVia sequenza[3];
    size_t n = ph_decodifica_ordine_automatico(ph_decodifica_vulkan_di_fabbrica(), sequenza);
    PhDecodifica *d = NULL;
    for (size_t i = 0; i < n && d == NULL; i++) {
        /* L'errore si riporta solo dell'ultimo tentativo (il software). */
        d = prova_via(sequenza[i], i + 1 == n ? e : NULL);
    }
    return d;
}

void ph_decodifica_libera(PhDecodifica *d)
{
    if (d == NULL)
        return;
    avcodec_free_context(&d->codec);
    av_buffer_unref(&d->dispositivo);
    av_packet_free(&d->pacchetto);
    free(d->configurazione);
    free(d);
}

PhVia ph_via_reale(PhVia scelta, bool ripiego_software)
{
    return ripiego_software ? PH_VIA_SOFTWARE : scelta;
}

PhVia ph_decodifica_via(const PhDecodifica *d)
{
    return ph_via_reale(d->via, d->ripiego_software);
}

int ph_decodifica_dai(PhDecodifica *d, bool configurazione, const uint8_t *dati, size_t n,
                      uint64_t tempo_us, PhErrore *e)
{
    if (n == 0 || n > (64u << 20))
        return ph_errore(e, "pacchetto video di lunghezza non valida");
    if (configurazione) {
        /* SPS/PPS da soli non sono un fotogramma: si tengono da parte e si
         * antepongono al prossimo (che e' un fotogramma chiave). */
        uint8_t *copia = malloc(n);
        if (copia == NULL)
            return ph_errore(e, "memoria finita");
        memcpy(copia, dati, n);
        free(d->configurazione);
        d->configurazione = copia;
        d->n_configurazione = n;
        return 0;
    }
    size_t totale = d->n_configurazione + n;
    int r = av_new_packet(d->pacchetto, (int)totale); /* aggiunge il margine che FFmpeg vuole */
    if (r < 0)
        return errore_av(e, "memoria per il pacchetto", r);
    if (d->n_configurazione > 0)
        memcpy(d->pacchetto->data, d->configurazione, d->n_configurazione);
    memcpy(d->pacchetto->data + d->n_configurazione, dati, n);
    free(d->configurazione);
    d->configurazione = NULL;
    d->n_configurazione = 0;
    d->pacchetto->pts = (int64_t)tempo_us;
    d->pacchetto->dts = (int64_t)tempo_us;
    r = avcodec_send_packet(d->codec, d->pacchetto);
    av_packet_unref(d->pacchetto);
    if (r < 0 && r != AVERROR(EAGAIN))
        return errore_av(e, "decodifica del pacchetto", r);
    if (d->ripiego_software && d->via != PH_VIA_SOFTWARE)
        return ph_errore(e, "la via %s non regge questo flusso (FFmpeg e' passato al software)",
                         ph_via_nome(d->via));
    return 0;
}

int ph_decodifica_prendi(PhDecodifica *d, struct AVFrame **f, PhErrore *e)
{
    AVFrame *fotogramma = av_frame_alloc();
    if (fotogramma == NULL)
        return ph_errore(e, "memoria finita");
    int r = avcodec_receive_frame(d->codec, fotogramma);
    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
        av_frame_free(&fotogramma);
        return 0;
    }
    if (r < 0) {
        av_frame_free(&fotogramma);
        return errore_av(e, "uscita del decodificatore", r);
    }
    *f = fotogramma;
    return 1;
}
