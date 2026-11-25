/* Registrazione: H.264 (e Opus) dentro un MP4 frammentato, senza
 * ricodificare. Il contenitore lo scrive il muxer "mp4" di libavformat su un
 * AVIOContext nostro che scrive sul descrittore gia' aperto con O_EXCL (cosi'
 * restano la regola "mai sovrascrivere" e il controllo di ogni errore di
 * scrittura). Opzioni: empty_moov (la descrizione delle tracce sta in testa
 * al file, prima dei campioni), default_base_moof, frag_custom (i frammenti
 * li chiudiamo noi: a ogni fotogramma chiave o ogni secondo, e solo allora
 * sappiamo fin dove il file e' "buono"). A chiusura regolare av_write_trailer
 * aggiunge in coda l'indice mfra (durata e ricerca per GStreamer, VLC...) senza
 * riposizionarsi, quindi funziona anche su un'uscita non ricercabile. Dopo ogni
 * frammento il file e' gia' leggibile: se il processo muore si perde solo cio'
 * che era ancora in memoria (e manca l'indice, come in ogni file frammentato
 * interrotto). */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>

#include "comune/buffer.h"
#include "video/cattura_interno.h"

#define PACCHETTO_MAX (32u << 20)         /* come limite di sicurezza sui dati del telefono */
#define TEMPO_MAX (1ULL << 50)            /* us: oltre e' un dato falso (35 anni) */
#define DURATA_MAX_US 1800000000LL        /* un fotogramma non dura oltre 30 minuti (il muxer vuole durate sotto 2^31) */
#define DURATA_NOMINALE_US 33333          /* se non c'e' un fotogramma precedente */
#define FRAMMENTO_US 1000000LL            /* un frammento ogni secondo di video */
#define FRAMMENTO_VOCI 300
#define FRAMMENTO_BYTE (64u << 20)
#define AUDIO_PACCHETTI_FRAMMENTO 50      /* 1 s di pacchetti da 20 ms */
#define AUDIO_TOLLERANZA 4800             /* 100 ms a 48 kHz: sotto, e' lo stesso flusso */
#define AUDIO_SALTO_MAX (1800LL * 48000)
#define PRE_SKIP_OPUS 312
#define MISURA_MAX 16383
#define CONF_MAX 4                        /* SPS e PPS tenuti per tipo */
#define NAL_CONF_MAX 1024
#define IO_BUFFER 65536

/* ---- Annex B -------------------------------------------------------------*/
typedef struct {
    const uint8_t *dati;
    size_t n;
} Nal;

/* Prossimo NAL a partire da *pos. 1 = trovato (puo' essere lungo 0), 0 = fine.
 * Ogni NAL sta tra due codici di inizio 00 00 01; gli zeri finali (parte di un
 * codice a 4 byte o riempimento) sono tolti. Tutto con i limiti di `len`. */
static int nal_succ(const uint8_t *d, size_t len, size_t *pos, Nal *out)
{
    size_t i = *pos;
    while (i + 3 <= len && !(d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1))
        i++;
    if (i + 3 > len)
        return 0;
    size_t ini = i + 3, j = ini;
    while (j + 3 <= len && !(d[j] == 0 && d[j + 1] == 0 && d[j + 2] == 1))
        j++;
    size_t fine = j + 3 <= len ? j : len;
    while (fine > ini && d[fine - 1] == 0)
        fine--;
    out->dati = d + ini;
    out->n = fine - ini;
    *pos = j + 3 <= len ? j : len;
    return 1;
}

typedef struct {
    uint8_t sps[CONF_MAX][NAL_CONF_MAX];
    size_t sps_n[CONF_MAX];
    int nsps;
    uint8_t pps[CONF_MAX][NAL_CONF_MAX];
    size_t pps_n[CONF_MAX];
    int npps;
} Conf;

static void conf_aggiungi(Conf *c, const Nal *nal)
{
    int tipo = nal->dati[0] & 0x1f;
    if (nal->n > NAL_CONF_MAX)
        return;
    if (tipo == 7 && nal->n >= 4 && c->nsps < CONF_MAX) {
        memcpy(c->sps[c->nsps], nal->dati, nal->n);
        c->sps_n[c->nsps++] = nal->n;
    } else if (tipo == 8 && c->npps < CONF_MAX) {
        memcpy(c->pps[c->npps], nal->dati, nal->n);
        c->pps_n[c->npps++] = nal->n;
    }
}

static bool conf_valida(const Conf *c)
{
    return c->nsps > 0 && c->npps > 0;
}

static bool conf_uguali(const Conf *a, const Conf *b)
{
    if (a->nsps != b->nsps || a->npps != b->npps)
        return false;
    for (int i = 0; i < a->nsps; i++)
        if (a->sps_n[i] != b->sps_n[i] || memcmp(a->sps[i], b->sps[i], a->sps_n[i]) != 0)
            return false;
    for (int i = 0; i < a->npps; i++)
        if (a->pps_n[i] != b->pps_n[i] || memcmp(a->pps[i], b->pps[i], a->pps_n[i]) != 0)
            return false;
    return true;
}


/* SPS/PPS trovati in un fotogramma contro quelli in uso: per ogni tipo
 * presente nel fotogramma l'elenco deve essere identico. */
static bool conf_compatibile(const Conf *nuova, const Conf *uso)
{
    if (nuova->nsps > 0) {
        if (nuova->nsps != uso->nsps)
            return false;
        for (int i = 0; i < uso->nsps; i++)
            if (nuova->sps_n[i] != uso->sps_n[i] ||
                memcmp(nuova->sps[i], uso->sps[i], uso->sps_n[i]) != 0)
                return false;
    }
    if (nuova->npps > 0) {
        if (nuova->npps != uso->npps)
            return false;
        for (int i = 0; i < uso->npps; i++)
            if (nuova->pps_n[i] != uso->pps_n[i] ||
                memcmp(nuova->pps[i], uso->pps[i], uso->pps_n[i]) != 0)
                return false;
    }
    return true;
}

/* extradata Annex B (SPS poi PPS con codice di inizio): il muxer lo trasforma in avcC. */
static int conf_annexb(const Conf *c, PhBuffer *out)
{
    static const uint8_t sc[4] = {0, 0, 0, 1};
    for (int i = 0; i < c->nsps; i++)
        if (ph_buffer_aggiungi(out, sc, 4) != 0 || ph_buffer_aggiungi(out, c->sps[i], c->sps_n[i]) != 0)
            return -1;
    for (int i = 0; i < c->npps; i++)
        if (ph_buffer_aggiungi(out, sc, 4) != 0 || ph_buffer_aggiungi(out, c->pps[i], c->pps_n[i]) != 0)
            return -1;
    return 0;
}

/* ---- Stato -----------------------------------------------------------------*/
struct PhRegistrazione {
    /* Porta d'ingresso: finisci aspetta che chi e' dentro esca. */
    pthread_mutex_t m;
    pthread_cond_t c;
    int in_uso;
    bool chiusa;
    /* Serializza le operazioni sullo stato qui sotto. */
    pthread_mutex_t lavoro;
    bool lavoro_ok, porta_ok;

    int fd;
    char percorso[4096];
    bool con_audio;
    bool guasta;
    char guasto[256];
    PhRegistrazioneStato st;

    /* Muxer */
    AVFormatContext *fc;
    AVIOContext *io;
    AVStream *sv, *sa;
    AVRational tb_v, tb_a;
    int64_t scritti;  /* byte arrivati al file attraverso l'AVIO */
    int64_t buoni;    /* byte alla fine dell'ultimo frammento chiuso */
    bool errore_io;
    int errno_io;
    uint64_t fv_n, fv_byte, fa_n, fa_byte; /* campioni consegnati al muxer, non ancora in un frammento */
    int64_t fv_durata;

    /* Video */
    bool iniziata;
    uint16_t w, h;
    Conf conf;
    bool conf_pronta;    /* conf contiene una configurazione arrivata prima del chiave */
    uint64_t t0_us;      /* tempo grezzo del primo chiave */
    uint64_t ultimo_us;  /* tempo grezzo dell'ultimo fotogramma */
    int64_t tick_video;  /* inizio (us nel file) del fotogramma trattenuto */
    int64_t scarto;      /* (tempo grezzo - t0) - tempo nel file, all'ultimo fotogramma: l'audio lo condivide */
    uint32_t ultima_durata;
    AVPacket *pend;      /* fotogramma trattenuto finche' non si sa quanto dura */
    bool pend_sync;

    /* Audio */
    bool au_iniziato;
    int64_t au_fine;     /* tick a 48 kHz dalla fine dell'ultimo pacchetto */
};

/* Scrittura del muxer sul descrittore: ogni errore resta segnato. */
static int scrivi_cb(void *opaque, const uint8_t *buf, int n)
{
    PhRegistrazione *r = opaque;
    if (r->fd < 0 || r->errore_io) {
        r->errore_io = true;
        if (r->errno_io == 0)
            r->errno_io = EIO;
        return AVERROR(EIO);
    }
    if (ph_cattura_scrivi_tutto(r->fd, buf, (size_t)n) != 0) {
        r->errore_io = true;
        r->errno_io = errno;
        return AVERROR(r->errno_io);
    }
    r->scritti += n;
    return n;
}

/* Registra il guasto, riporta il file all'ultimo frammento chiuso e lo chiude. */
static int guasta_con(PhRegistrazione *r, PhErrore *e, const char *cosa, const char *motivo)
{
    if (!r->guasta) {
        snprintf(r->guasto, sizeof r->guasto, "%s: %s", cosa, motivo);
        r->guasta = true;
        if (r->fd >= 0) {
            if (ftruncate(r->fd, (off_t)r->buoni) != 0) {
                /* su un disco pieno o un dispositivo puo' non riuscire: pazienza */
            }
            close(r->fd);
            r->fd = -1;
        }
    }
    return ph_errore(e, "%s", r->guasto);
}

static int guasta_io(PhRegistrazione *r, PhErrore *e)
{
    char t[128];
    ph_cattura_testo_errno(r->errno_io ? r->errno_io : EIO, t, sizeof t);
    return guasta_con(r, e, "scrittura della registrazione", t);
}

static int guasta_av(PhRegistrazione *r, PhErrore *e, const char *cosa, int codice)
{
    if (r->errore_io)
        return guasta_io(r, e); /* la causa vera e' l'errore del disco */
    char t[128];
    av_strerror(codice, t, sizeof t);
    return guasta_con(r, e, cosa, t);
}

/* Chiude il frammento: tutto cio' che il muxer ha in memoria va sul file e
 * quel punto diventa l'ultimo "buono". */
static int chiudi_frammento(PhRegistrazione *r, PhErrore *e)
{
    if (r->fv_n + r->fa_n > 0) {
        int ret = av_write_frame(r->fc, NULL);
        if (ret < 0)
            return guasta_av(r, e, "chiusura del frammento", ret);
    }
    avio_flush(r->io);
    if (r->errore_io || r->io->error < 0)
        return guasta_io(r, e);
    r->buoni = r->scritti;
    r->st.byte_scritti = (uint64_t)r->scritti;
    r->fv_n = r->fv_byte = r->fa_n = r->fa_byte = 0;
    r->fv_durata = 0;
    return 0;
}

static int consegna(PhRegistrazione *r, AVPacket *pk, PhErrore *e)
{
    int ret = av_write_frame(r->fc, pk);
    if (ret < 0)
        return guasta_av(r, e, "scrittura nel contenitore", ret);
    if (r->errore_io)
        return guasta_io(r, e);
    return 0;
}

/* Libera muxer e AVIO (anche a meta' della creazione). */
static void distruggi_muxer(PhRegistrazione *r)
{
    if (r->fc != NULL)
        avformat_free_context(r->fc); /* pb e' nostro (CUSTOM_IO): non lo chiude */
    r->fc = NULL;
    r->sv = r->sa = NULL;
    if (r->io != NULL) {
        av_freep(&r->io->buffer);
        avio_context_free(&r->io);
    }
}

/* Crea muxer e flusso AVIO e scrive l'intestazione (ftyp+moov). */
static int avvia(PhRegistrazione *r, PhErrore *e)
{
    int ret = avformat_alloc_output_context2(&r->fc, NULL, "mp4", NULL);
    if (ret < 0)
        return ph_errore(e, "muxer mp4 non disponibile");
    uint8_t *buf = av_malloc(IO_BUFFER);
    if (buf == NULL)
        return ph_errore(e, "memoria finita nella registrazione");
    r->io = avio_alloc_context(buf, IO_BUFFER, 1, r, NULL, scrivi_cb, NULL);
    if (r->io == NULL) {
        av_free(buf);
        return ph_errore(e, "memoria finita nella registrazione");
    }
    r->fc->pb = r->io;
    r->fc->flags |= AVFMT_FLAG_CUSTOM_IO;

    PhBuffer extra = PH_BUFFER_VUOTO;
    if (conf_annexb(&r->conf, &extra) != 0) {
        ph_buffer_libera(&extra);
        return ph_errore(e, "memoria finita nella registrazione");
    }
    r->sv = avformat_new_stream(r->fc, NULL);
    if (r->sv == NULL) {
        ph_buffer_libera(&extra);
        return ph_errore(e, "memoria finita nella registrazione");
    }
    AVCodecParameters *p = r->sv->codecpar;
    p->codec_type = AVMEDIA_TYPE_VIDEO;
    p->codec_id = AV_CODEC_ID_H264;
    p->width = r->w;
    p->height = r->h;
    p->extradata = av_mallocz(extra.lunghezza + AV_INPUT_BUFFER_PADDING_SIZE);
    if (p->extradata == NULL) {
        ph_buffer_libera(&extra);
        return ph_errore(e, "memoria finita nella registrazione");
    }
    memcpy(p->extradata, extra.dati, extra.lunghezza);
    p->extradata_size = (int)extra.lunghezza;
    ph_buffer_libera(&extra);
    r->sv->time_base = (AVRational){1, 1000000};

    if (r->con_audio) {
        r->sa = avformat_new_stream(r->fc, NULL);
        if (r->sa == NULL)
            return ph_errore(e, "memoria finita nella registrazione");
        AVCodecParameters *q = r->sa->codecpar;
        q->codec_type = AVMEDIA_TYPE_AUDIO;
        q->codec_id = AV_CODEC_ID_OPUS;
        q->sample_rate = 48000;
        av_channel_layout_default(&q->ch_layout, 2);
        q->initial_padding = PRE_SKIP_OPUS;
        q->frame_size = 960; /* pacchetti da 20 ms: senza, il muxer avvisa su stderr */
        q->extradata = av_mallocz(19 + AV_INPUT_BUFFER_PADDING_SIZE);
        if (q->extradata == NULL)
            return ph_errore(e, "memoria finita nella registrazione");
        /* OpusHead: versione 1, 2 canali, pre-skip, 48000 Hz, guadagno 0, mappa 0. */
        static const uint8_t testa[19] = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, 2,
                                          PRE_SKIP_OPUS & 0xff, PRE_SKIP_OPUS >> 8,
                                          0x80, 0xBB, 0, 0, 0, 0, 0};
        memcpy(q->extradata, testa, sizeof testa);
        q->extradata_size = sizeof testa;
        r->sa->time_base = (AVRational){1, 48000};
    }
    AVDictionary *opz = NULL;
    av_dict_set(&opz, "movflags", "empty_moov+default_base_moof+frag_custom", 0);
    ret = avformat_write_header(r->fc, &opz);
    av_dict_free(&opz);
    if (ret < 0)
        return guasta_av(r, e, "intestazione della registrazione", ret);
    r->tb_v = r->sv->time_base;
    if (r->sa != NULL)
        r->tb_a = r->sa->time_base;
    avio_flush(r->io);
    if (r->errore_io || r->io->error < 0)
        return guasta_io(r, e);
    r->buoni = r->scritti;
    r->st.byte_scritti = (uint64_t)r->scritti;
    return 0;
}

/* ---- Ciclo di vita ------------------------------------------------------------*/
PhRegistrazione *ph_registrazione_inizia_su_fd(int fd, bool con_audio, PhErrore *e)
{
    PhRegistrazione *r = calloc(1, sizeof *r);
    if (r == NULL) {
        close(fd);
        ph_errore(e, "memoria finita");
        return NULL;
    }
    r->fd = fd;
    r->con_audio = con_audio;
    r->porta_ok = pthread_mutex_init(&r->m, NULL) == 0 && pthread_cond_init(&r->c, NULL) == 0;
    r->lavoro_ok = pthread_mutex_init(&r->lavoro, NULL) == 0;
    if (!r->porta_ok || !r->lavoro_ok) {
        close(fd);
        ph_errore(e, "lucchetti della registrazione non creabili");
        free(r);
        return NULL;
    }
    return r;
}

PhRegistrazione *ph_registrazione_inizia(const char *cartella, const char *app, bool con_audio,
                                         char *percorso, size_t n_percorso, PhErrore *e)
{
    char scelto[4096];
    int fd = ph_cattura_apri_nuovo(cartella, app, "mp4", time(NULL), scelto, sizeof scelto, e);
    if (fd < 0)
        return NULL;
    PhRegistrazione *r = ph_registrazione_inizia_su_fd(fd, con_audio, e);
    if (r == NULL) {
        unlink(scelto);
        return NULL;
    }
    memcpy(r->percorso, scelto, strlen(scelto) + 1);
    if (percorso != NULL) {
        if (strlen(scelto) >= n_percorso) {
            ph_errore(e, "percorso della registrazione troppo lungo per il buffer");
            ph_registrazione_libera(r); /* il file e' vuoto: finisci lo cancella */
            return NULL;
        }
        memcpy(percorso, scelto, strlen(scelto) + 1);
    }
    return r;
}

/* Entra/esce dalla porta: finisci aspetta chi e' dentro. */
static bool entra(PhRegistrazione *r, PhErrore *e)
{
    pthread_mutex_lock(&r->m);
    if (r->chiusa) {
        pthread_mutex_unlock(&r->m);
        ph_errore(e, "registrazione gia' chiusa");
        return false;
    }
    r->in_uso++;
    pthread_mutex_unlock(&r->m);
    pthread_mutex_lock(&r->lavoro);
    return true;
}

static void esci(PhRegistrazione *r)
{
    pthread_mutex_unlock(&r->lavoro);
    pthread_mutex_lock(&r->m);
    if (--r->in_uso == 0)
        pthread_cond_broadcast(&r->c);
    pthread_mutex_unlock(&r->m);
}

void ph_registrazione_stato(PhRegistrazione *r, PhRegistrazioneStato *s)
{
    if (s == NULL)
        return;
    if (r == NULL) {
        memset(s, 0, sizeof *s);
        return;
    }
    pthread_mutex_lock(&r->lavoro);
    *s = r->st;
    pthread_mutex_unlock(&r->lavoro);
}

/* ---- Video ---------------------------------------------------------------------*/
/* Consegna al muxer il fotogramma trattenuto, con la durata ora nota. */
static int video_completa(PhRegistrazione *r, uint32_t durata, PhErrore *e)
{
    AVPacket *pk = r->pend;
    pk->stream_index = r->sv->index;
    pk->pts = pk->dts = r->tick_video;
    pk->duration = durata;
    if (r->pend_sync)
        pk->flags |= AV_PKT_FLAG_KEY;
    av_packet_rescale_ts(pk, (AVRational){1, 1000000}, r->tb_v);
    size_t dim = (size_t)pk->size;
    int esito = consegna(r, pk, e);
    av_packet_free(&r->pend);
    if (esito != 0)
        return -1;
    r->fv_n++;
    r->fv_byte += dim;
    r->fv_durata += durata;
    r->tick_video += durata;
    r->ultima_durata = durata;
    return 0;
}

int ph_registrazione_aggiungi_video(PhRegistrazione *r, bool configurazione, bool chiave,
                                    uint16_t larghezza, uint16_t altezza, uint64_t tempo_us,
                                    const uint8_t *dati, size_t lunghezza, PhErrore *e)
{
    if (r == NULL)
        return ph_errore(e, "registrazione assente");
    if (!entra(r, e))
        return -1;
    int esito = -1;
    Conf *cf = NULL;
    PhBuffer camp = PH_BUFFER_VUOTO;
    AVPacket *nuovo = NULL;
    if (r->guasta) {
        ph_errore(e, "%s", r->guasto);
        goto fine;
    }
    if (dati == NULL && lunghezza > 0) {
        ph_errore(e, "pacchetto video senza dati");
        goto fine;
    }
    if (lunghezza > PACCHETTO_MAX) {
        ph_errore(e, "pacchetto video troppo grande (%zu byte)", lunghezza);
        goto fine;
    }
    if (tempo_us > TEMPO_MAX) {
        ph_errore(e, "tempo del pacchetto video non valido");
        goto fine;
    }
    cf = calloc(1, sizeof *cf);
    if (cf == NULL) {
        ph_errore(e, "memoria finita");
        goto fine;
    }
    bool vcl = false, idr = false;
    size_t pos = 0;
    Nal nal;
    static const uint8_t sc[4] = {0, 0, 0, 1};
    while (nal_succ(dati, lunghezza, &pos, &nal)) {
        if (nal.n == 0 || (nal.dati[0] & 0x80))
            continue; /* vuoto o con il bit proibito: non e' H.264 */
        int tipo = nal.dati[0] & 0x1f;
        if (tipo == 7 || tipo == 8) {
            conf_aggiungi(cf, &nal);
            continue;
        }
        if (tipo == 9 || (tipo >= 10 && tipo <= 12))
            continue; /* AUD, fine sequenza/flusso, riempimento: non servono nel file */
        if (configurazione)
            continue; /* in un pacchetto di configurazione conta solo SPS/PPS */
        if (tipo >= 1 && tipo <= 5)
            vcl = true;
        if (tipo == 5)
            idr = true;
        if (ph_buffer_aggiungi(&camp, sc, 4) != 0 || ph_buffer_aggiungi(&camp, nal.dati, nal.n) != 0) {
            ph_errore(e, "memoria finita nella registrazione");
            goto fine;
        }
    }

    if (configurazione) {
        if (!conf_valida(cf)) {
            ph_errore(e, "configurazione H.264 senza SPS o PPS validi");
            goto fine;
        }
        if (r->iniziata) {
            if (!conf_uguali(cf, &r->conf)) {
                ph_errore(e, "configurazione H.264 cambiata durante la registrazione");
                goto fine;
            }
        } else {
            r->conf = *cf;
            r->conf_pronta = true;
        }
        esito = 0;
        goto fine;
    }

    if (!vcl) {
        r->st.video_scartati++; /* vuoto, o senza dati video validi */
        esito = 0;
        goto fine;
    }
    if (!r->iniziata) {
        if (!(chiave && idr)) {
            r->st.video_scartati++;
            esito = 0;
            goto fine;
        }
        if (!conf_valida(cf) && !r->conf_pronta) {
            r->st.video_scartati++; /* senza SPS/PPS il file non sarebbe leggibile */
            esito = 0;
            goto fine;
        }
        if (larghezza == 0 || altezza == 0 || larghezza > MISURA_MAX || altezza > MISURA_MAX) {
            ph_errore(e, "misure del video non valide (%ux%u)", larghezza, altezza);
            goto fine;
        }
    } else {
        if (larghezza != r->w || altezza != r->h) {
            ph_errore(e, "misure cambiate durante la registrazione (%ux%u invece di %ux%u)",
                      larghezza, altezza, r->w, r->h);
            goto fine;
        }
        if ((cf->nsps > 0 || cf->npps > 0) && !conf_compatibile(cf, &r->conf)) {
            ph_errore(e, "SPS/PPS cambiati durante la registrazione");
            goto fine;
        }
    }
    /* Il pacchetto del nuovo fotogramma si crea PRIMA di toccare lo stato:
     * se la memoria finisce, il fotogramma non entra e nulla cambia. */
    nuovo = av_packet_alloc();
    if (nuovo == NULL || av_new_packet(nuovo, (int)camp.lunghezza) < 0) {
        ph_errore(e, "memoria finita nella registrazione");
        goto fine;
    }
    memcpy(nuovo->data, camp.dati, camp.lunghezza);

    if (!r->iniziata) {
        if (conf_valida(cf))
            r->conf = *cf; /* quella dentro il chiave e' la sua */
        r->w = larghezza;
        r->h = altezza;
        if (avvia(r, e) != 0) {
            if (!r->guasta)
                distruggi_muxer(r); /* memoria finita: si potra' riprovare dal chiave dopo */
            goto fine;
        }
        r->iniziata = true;
        r->st.iniziata = true;
        r->t0_us = tempo_us;
        r->ultimo_us = tempo_us;
        r->tick_video = 0;
        r->scarto = 0;
    } else {
        /* La durata del fotogramma trattenuto e' la distanza da questo. */
        int64_t d = (int64_t)tempo_us - (int64_t)r->ultimo_us;
        uint32_t durata;
        if (d <= 0) {
            durata = r->ultima_durata ? r->ultima_durata : DURATA_NOMINALE_US;
            r->st.tempi_corretti++;
        } else if (d > DURATA_MAX_US) {
            durata = (uint32_t)DURATA_MAX_US;
            r->st.tempi_corretti++;
        } else {
            durata = (uint32_t)d;
        }
        r->ultimo_us = tempo_us;
        if (video_completa(r, durata, e) != 0)
            goto fine;
        r->scarto = ((int64_t)tempo_us - (int64_t)r->t0_us) - r->tick_video;
        /* Un fotogramma chiave apre un frammento nuovo; senza chiavi si
         * scrive comunque ogni secondo. */
        if (idr || r->fv_durata >= FRAMMENTO_US || r->fv_n >= FRAMMENTO_VOCI ||
            r->fv_byte >= FRAMMENTO_BYTE) {
            if (chiudi_frammento(r, e) != 0)
                goto fine;
        }
    }
    /* Il nuovo fotogramma diventa quello trattenuto. */
    r->pend = nuovo;
    nuovo = NULL;
    r->pend_sync = idr;
    r->st.video_accettati++;
    esito = 0;
fine:
    free(cf);
    ph_buffer_libera(&camp);
    av_packet_free(&nuovo);
    esci(r);
    return esito;
}

/* ---- Audio ---------------------------------------------------------------------*/
/* Campioni a 48 kHz di un pacchetto Opus, dal byte TOC (RFC 6716 3.1); 0 se non valido. */
static uint32_t opus_campioni(const uint8_t *p, size_t n)
{
    if (n == 0)
        return 0;
    static const uint16_t silk[4] = {480, 960, 1920, 2880};
    static const uint16_t ibrido[2] = {480, 960};
    static const uint16_t celt[4] = {120, 240, 480, 960};
    unsigned cfg = p[0] >> 3, code = p[0] & 3;
    uint32_t fs = cfg < 12 ? silk[cfg & 3] : cfg < 16 ? ibrido[cfg & 1] : celt[cfg & 3];
    uint32_t frames;
    if (code == 0)
        frames = 1;
    else if (code < 3)
        frames = 2;
    else {
        if (n < 2)
            return 0;
        frames = p[1] & 0x3f;
        if (frames == 0)
            return 0;
    }
    uint32_t tot = fs * frames;
    return tot > 5760 ? 0 : tot; /* un pacchetto dura al piu' 120 ms */
}

int ph_registrazione_aggiungi_audio(PhRegistrazione *r, uint64_t tempo_us, const uint8_t *dati,
                                    size_t lunghezza, PhErrore *e)
{
    if (r == NULL)
        return ph_errore(e, "registrazione assente");
    if (!entra(r, e))
        return -1;
    int esito = -1;
    AVPacket *pk = NULL;
    if (!r->con_audio) {
        ph_errore(e, "la registrazione e' senza traccia audio");
        goto fine;
    }
    if (r->guasta) {
        ph_errore(e, "%s", r->guasto);
        goto fine;
    }
    if (dati == NULL && lunghezza > 0) {
        ph_errore(e, "pacchetto audio senza dati");
        goto fine;
    }
    if (tempo_us > TEMPO_MAX) {
        ph_errore(e, "tempo del pacchetto audio non valido");
        goto fine;
    }
    esito = 0;
    uint32_t campioni = lunghezza > 65535 ? 0 : opus_campioni(dati, lunghezza);
    /* Stesso scarto del video: un fotogramma con il tempo corretto sposta anche l'audio. */
    int64_t rel = r->iniziata ? ((int64_t)tempo_us - (int64_t)r->t0_us - r->scarto) : -1;
    if (rel < 0 || campioni == 0) {
        r->st.audio_scartati++; /* prima del video, vuoto o non Opus */
        goto fine;
    }
    int64_t tick = (rel / 1000000) * 48000 + (rel % 1000000) * 48000 / 1000000;
    pk = av_packet_alloc();
    if (pk == NULL || av_new_packet(pk, (int)lunghezza) < 0) {
        ph_errore(e, "memoria finita nella registrazione");
        esito = -1; /* nulla e' cambiato: il pacchetto non entra */
        goto fine;
    }
    memcpy(pk->data, dati, lunghezza);
    if (!r->au_iniziato) {
        r->au_iniziato = true;
        r->au_fine = tick;
    } else if (tick > r->au_fine + AUDIO_TOLLERANZA) {
        /* Buco: si chiude il frammento, cosi' il pacchetto dopo di lui comincia
         * a un tempo nuovo e il silenzio resta tale. */
        if (chiudi_frammento(r, e) != 0) {
            esito = -1;
            goto fine;
        }
        int64_t buco = tick - r->au_fine;
        r->au_fine += buco > AUDIO_SALTO_MAX ? AUDIO_SALTO_MAX : buco;
    } /* tempo indietro o dentro la tolleranza: segue il precedente */
    pk->stream_index = r->sa->index;
    pk->pts = pk->dts = r->au_fine;
    pk->duration = campioni;
    pk->flags |= AV_PKT_FLAG_KEY;
    av_packet_rescale_ts(pk, (AVRational){1, 48000}, r->tb_a);
    if (consegna(r, pk, e) != 0) {
        esito = -1;
        goto fine;
    }
    r->au_fine += campioni;
    r->fa_n++;
    r->fa_byte += lunghezza;
    r->st.audio_accettati++;
    if (r->fa_n >= AUDIO_PACCHETTI_FRAMMENTO || r->fa_byte >= (1u << 20))
        if (chiudi_frammento(r, e) != 0)
            esito = -1;
fine:
    av_packet_free(&pk);
    esci(r);
    return esito;
}

/* ---- Fine ----------------------------------------------------------------------*/
int ph_registrazione_finisci(PhRegistrazione *r, PhErrore *e)
{
    if (r == NULL)
        return ph_errore(e, "registrazione assente");
    pthread_mutex_lock(&r->m);
    if (r->chiusa) {
        pthread_mutex_unlock(&r->m);
        return ph_errore(e, "registrazione gia' chiusa");
    }
    r->chiusa = true; /* da qui le nuove chiamate tornano -1 */
    while (r->in_uso > 0)
        pthread_cond_wait(&r->c, &r->m); /* attesa dichiarata nel contratto */
    pthread_mutex_unlock(&r->m);

    pthread_mutex_lock(&r->lavoro);
    int esito = 0;
    if (r->iniziata && !r->guasta) {
        /* L'ultimo fotogramma dura quanto il precedente. */
        uint32_t durata = r->ultima_durata ? r->ultima_durata : DURATA_NOMINALE_US;
        if (r->pend != NULL && video_completa(r, durata, e) != 0)
            esito = -1;
        if (esito == 0 && chiudi_frammento(r, e) != 0)
            esito = -1;
        if (esito == 0) {
            int ret = av_write_trailer(r->fc);
            if (ret < 0) {
                guasta_av(r, e, "chiusura del contenitore", ret);
                esito = -1;
            } else if (chiudi_frammento(r, e) != 0) {
                esito = -1;
            }
        }
    }
    if (r->guasta) {
        ph_errore(e, "%s", r->guasto);
        esito = -1;
    }
    if (r->fd >= 0) {
        if (close(r->fd) != 0 && esito == 0) {
            char t[128];
            ph_cattura_testo_errno(errno, t, sizeof t);
            ph_errore(e, "chiusura della registrazione: %s", t);
            esito = -1;
        }
        r->fd = -1;
    }
    if (!r->iniziata && r->percorso[0] != 0) {
        unlink(r->percorso); /* nessun fotogramma chiave (o intestazione non scritta): file inutile */
        r->percorso[0] = 0;
    }
    pthread_mutex_unlock(&r->lavoro);
    return esito;
}

void ph_registrazione_libera(PhRegistrazione *r)
{
    if (r == NULL)
        return;
    pthread_mutex_lock(&r->m);
    bool gia = r->chiusa;
    pthread_mutex_unlock(&r->m);
    if (!gia)
        (void)ph_registrazione_finisci(r, NULL);
    av_packet_free(&r->pend);
    distruggi_muxer(r);
    pthread_mutex_destroy(&r->lavoro);
    pthread_cond_destroy(&r->c);
    pthread_mutex_destroy(&r->m);
    free(r);
}
