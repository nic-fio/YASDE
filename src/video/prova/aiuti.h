/* Aiuti dei test di cattura: controlli, cartella di prova, e un lettore dei
 * box MP4 scritto a parte (indipendente da cattura_mp4.c) che controlla la
 * coerenza interna del file senza passare da FFmpeg. */
#ifndef PH_PROVA_CATTURA_AIUTI_H
#define PH_PROVA_CATTURA_AIUTI_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libavcodec/packet.h>
#include <libavformat/avformat.h>

#define CONTROLLA(c) \
    do { \
        if (!(c)) { \
            fprintf(stderr, "%s:%d: controllo fallito: %s\n", __FILE__, __LINE__, #c); \
            exit(1); \
        } \
    } while (0)

/* Cartella di prova nuova nella cartella corrente (mai in /tmp). */
static inline void cartella_prova(char *out, size_t n, const char *nome)
{
    snprintf(out, n, "./%s-XXXXXX", nome);
    CONTROLLA(mkdtemp(out) != NULL);
}

/* Cancella i file di una cartella di prova e la cartella. */
static inline void togli_cartella(const char *dir)
{
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    CONTROLLA(system(cmd) == 0);
}

static inline uint8_t *leggi_file(const char *percorso, size_t *n)
{
    FILE *f = fopen(percorso, "rb");
    CONTROLLA(f != NULL);
    CONTROLLA(fseek(f, 0, SEEK_END) == 0);
    long l = ftell(f);
    CONTROLLA(l >= 0);
    CONTROLLA(fseek(f, 0, SEEK_SET) == 0);
    uint8_t *d = malloc((size_t)l + 1);
    CONTROLLA(d != NULL);
    CONTROLLA(fread(d, 1, (size_t)l, f) == (size_t)l);
    fclose(f);
    *n = (size_t)l;
    return d;
}

static inline uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline uint64_t be64(const uint8_t *p)
{
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

/* Riepilogo di un MP4 riletto con libavformat (come lo leggerebbe un lettore):
 * indice 0 = video, 1 = audio. Tempi nei tick del flusso (video 1/1000000,
 * audio 1/48000), letti dal demuxer, non dal nostro muxer. */
typedef struct {
    int aperto;              /* 1 = il contenitore si apre */
    int n_flussi;
    uint64_t campioni[2];
    int64_t durata[2];       /* pts ultimo - pts primo */
    int64_t dur_min[2], dur_max[2]; /* minimo e massimo salto di pts tra pacchetti consecutivi */
    uint64_t sync[2];
    int64_t pts_primo[2], pts_ultimo[2];
    int64_t tb_den[2];
    uint64_t byte[2];
    uint64_t nal_parametri;  /* NAL SPS/PPS/AUD trovati dentro i campioni video (devono essere 0) */
    int pts_non_crescenti;   /* pacchetti video con pts non crescenti */
    int moof;                /* frammenti nel file */
    int box_completi;        /* 1 = i box di primo livello coprono esattamente il file */
} Riepilogo;

static inline void leggi_mp4(const char *percorso, Riepilogo *r)
{
    memset(r, 0, sizeof *r);
    for (int i = 0; i < 2; i++) {
        r->dur_min[i] = INT64_MAX;
        r->dur_max[i] = INT64_MIN;
        r->pts_primo[i] = r->pts_ultimo[i] = INT64_MIN;
    }
    /* Box di primo livello, a mano: solo per contare i frammenti e vedere se il file e' intero. */
    size_t nb;
    uint8_t *raw = leggi_file(percorso, &nb);
    size_t pos = 0;
    while (nb - pos >= 8) {
        uint32_t sz = be32(raw + pos);
        if (sz < 8 || (size_t)sz > nb - pos)
            break;
        if (memcmp(raw + pos + 4, "moof", 4) == 0)
            r->moof++;
        pos += sz;
    }
    r->box_completi = pos == nb;
    free(raw);

    av_log_set_level(AV_LOG_QUIET); /* i NAL finti dei test fanno lamentare il parser H.264 */
    AVFormatContext *fc = NULL;
    if (avformat_open_input(&fc, percorso, NULL, NULL) < 0)
        return;
    r->aperto = 1;
    r->n_flussi = (int)fc->nb_streams;
    for (unsigned i = 0; i < fc->nb_streams && i < 2; i++)
        r->tb_den[i] = fc->streams[i]->time_base.den;
    AVPacket *p = av_packet_alloc();
    int64_t ultimo_pts_video = INT64_MIN;
    while (av_read_frame(fc, p) >= 0) {
        int i = p->stream_index;
        if (i < 0 || i > 1) {
            av_packet_unref(p);
            continue;
        }
        r->campioni[i]++;
        /* Il campo duration dei pacchetti lo puo' riscrivere il demuxer (stima dal frame rate):
         * le durate vere si leggono dalla distanza tra i pts. */
        if (r->pts_ultimo[i] != INT64_MIN) {
            int64_t d = p->pts - r->pts_ultimo[i];
            if (d < r->dur_min[i])
                r->dur_min[i] = d;
            if (d > r->dur_max[i])
                r->dur_max[i] = d;
        }
        if (p->flags & AV_PKT_FLAG_KEY)
            r->sync[i]++;
        if (r->pts_primo[i] == INT64_MIN)
            r->pts_primo[i] = p->pts;
        r->pts_ultimo[i] = p->pts;
        r->durata[i] = r->pts_ultimo[i] - r->pts_primo[i];
        r->byte[i] += (uint64_t)p->size;
        if (i == 0) {
            if (ultimo_pts_video != INT64_MIN && p->pts <= ultimo_pts_video)
                r->pts_non_crescenti = 1;
            ultimo_pts_video = p->pts;
            /* Campioni in formato "lunghezza + NAL": niente SPS (7), PPS (8), AUD (9). */
            size_t q = 0;
            while (q + 4 <= (size_t)p->size) {
                uint32_t l = be32(p->data + q);
                q += 4;
                if (l == 0 || l > (size_t)p->size - q) {
                    r->nal_parametri += 1000; /* campione malformato */
                    break;
                }
                int t = p->data[q] & 0x1f;
                if (t == 7 || t == 8 || t == 9)
                    r->nal_parametri++;
                q += l;
            }
        }
        av_packet_unref(p);
    }
    av_packet_free(&p);
    avformat_close_input(&fc);
}

#endif
