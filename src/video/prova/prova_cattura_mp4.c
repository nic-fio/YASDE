/* Registrazione di un flusso H.264 vero (generato da FFmpeg nel meson.build)
 * e controllo con ffprobe: stesso numero di fotogrammi, stesse misure, tempi
 * giusti, file riaperto senza avvisi, file troncato ancora leggibile, e una
 * traccia Opus vera (codificata qui con libopus).
 * Uso: prova_cattura_mp4 campione.h264 ffprobe */
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <math.h>

#include "aiuti.h"
#include "video/cattura_interno.h"

#define N_FOTOGRAMMI 120
#define PASSO_US 16667
#define T0_US 5000000ULL

typedef struct {
    size_t ini, lun;
    bool chiave;
} Au;

/* Taglia il flusso in fotogrammi: ognuno finisce con il suo NAL video. */
static int taglia(const uint8_t *d, size_t n, Au *au, int max)
{
    size_t inizio_nal[4096];
    int tipi[4096];
    int k = 0;
    for (size_t i = 0; i + 4 <= n && k < 4096; i++)
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
            inizio_nal[k] = i;
            tipi[k++] = d[i + 3] & 0x1f;
        }
    int nau = 0;
    size_t ini = 0;
    bool chiave = false;
    for (int j = 0; j < k; j++) {
        if (tipi[j] == 5)
            chiave = true;
        if (tipi[j] == 1 || tipi[j] == 5) {
            size_t fine = j + 1 < k ? inizio_nal[j + 1] : n;
            CONTROLLA(nau < max);
            au[nau++] = (Au){ini, fine - ini, chiave};
            ini = fine;
            chiave = false;
        }
    }
    return nau;
}

/* Esegue un comando e ne raccoglie l'output (stdout + stderr). */
static size_t esegui(const char *cmd, char *out, size_t cap)
{
    FILE *p = popen(cmd, "r");
    CONTROLLA(p != NULL);
    size_t n = fread(out, 1, cap - 1, p);
    out[n] = 0;
    pclose(p);
    return n;
}

static long valore(const char *testo, const char *chiave)
{
    const char *p = strstr(testo, chiave);
    CONTROLLA(p != NULL);
    return strtol(p + strlen(chiave), NULL, 10);
}

static int righe(const char *t)
{
    int n = 0;
    for (; *t; t++)
        if (*t == '\n')
            n++;
    return n;
}

static const char *FFPROBE;

static void controlla_video(const char *file, long attesi, bool completo)
{
    char cmd[900], out[65536];
    snprintf(cmd, sizeof cmd,
             "'%s' -v error -count_frames -select_streams v:0 -show_entries "
             "stream=codec_name,width,height,nb_read_frames -of default=nw=1 '%s' 2>&1",
             FFPROBE, file);
    esegui(cmd, out, sizeof out);
    if (completo) {
        /* Nessun avviso e nessun errore: solo le quattro righe chieste. */
        CONTROLLA(righe(out) == 4);
        CONTROLLA(strstr(out, "codec_name=h264") != NULL);
        CONTROLLA(valore(out, "width=") == 1280 && valore(out, "height=") == 720);
        CONTROLLA(valore(out, "nb_read_frames=") == attesi);
    } else {
        long n = valore(out, "nb_read_frames=");
        CONTROLLA(n >= 1 && n < attesi);
    }
}

static void controlla_tempi(const char *file, long attesi)
{
    char cmd[900];
    static char out[1 << 20];
    snprintf(cmd, sizeof cmd,
             "'%s' -v error -select_streams v:0 -show_entries packet=pts,dts,flags -of csv=p=0 '%s' 2>&1",
             FFPROBE, file);
    esegui(cmd, out, sizeof out);
    CONTROLLA(righe(out) == attesi);
    const char *p = out;
    for (long i = 0; i < attesi; i++) {
        long pts = strtol(p, (char **)&p, 10);
        CONTROLLA(*p == ',');
        long dts = strtol(p + 1, (char **)&p, 10);
        CONTROLLA(pts == i * PASSO_US && dts == pts); /* il primo chiave e' il tempo 0 */
        CONTROLLA(*p == ',');
        bool chiave = p[1] == 'K';
        CONTROLLA(chiave == (i % 60 == 0));
        while (*p != '\n')
            p++;
        p++;
    }
}

static void registra(const char *dir, const uint8_t *d, const Au *au, int nau, bool con_config,
                     AVPacket **opus, int n_opus, char *percorso)
{
    PhErrore e = {0};
    PhRegistrazione *r = ph_registrazione_inizia(dir, "Video Prova", opus != NULL, percorso, 600, &e);
    CONTROLLA(r != NULL);
    if (con_config) {
        /* SPS e PPS del primo fotogramma, come pacchetto di configurazione. */
        CONTROLLA(ph_registrazione_aggiungi_video(r, true, false, 0, 0, 0, d + au[0].ini,
                                                  au[0].lun, &e) == 0);
    }
    int ia = 0;
    for (int i = 0; i < nau; i++) {
        uint64_t t = T0_US + (uint64_t)i * PASSO_US;
        if (ph_registrazione_aggiungi_video(r, false, au[i].chiave, 1280, 720, t, d + au[i].ini,
                                            au[i].lun, &e) != 0) {
            fprintf(stderr, "video %d: %s\n", i, e.testo);
            exit(1);
        }
        /* L'audio fino a questo istante (dopo il video: prima del primo chiave si scarta). */
        while (opus != NULL && ia < n_opus && T0_US + (uint64_t)ia * 20000 <= t) {
            CONTROLLA(ph_registrazione_aggiungi_audio(r, T0_US + (uint64_t)ia * 20000, opus[ia]->data,
                                                      (size_t)opus[ia]->size, &e) == 0);
            ia++;
        }
    }
    while (opus != NULL && ia < n_opus) {
        CONTROLLA(ph_registrazione_aggiungi_audio(r, T0_US + (uint64_t)ia * 20000, opus[ia]->data,
                                                  (size_t)opus[ia]->size, &e) == 0);
        ia++;
    }
    CONTROLLA(ph_registrazione_finisci(r, &e) == 0);
    PhRegistrazioneStato s;
    ph_registrazione_stato(r, &s);
    CONTROLLA(s.video_accettati == (uint64_t)nau && s.video_scartati == 0 && s.tempi_corretti == 0);
    if (s.audio_accettati != (uint64_t)n_opus)
        fprintf(stderr, "audio: accettati %lu scartati %lu su %d\n", (unsigned long)s.audio_accettati, (unsigned long)s.audio_scartati, n_opus);
    CONTROLLA(s.audio_accettati == (uint64_t)n_opus);
    ph_registrazione_libera(r);
}

/* Genera pacchetti Opus veri: una sinusoide stereo di n*20 ms. */
static int genera_opus(AVPacket **pk, int n)
{
    const AVCodec *cod = avcodec_find_encoder_by_name("libopus");
    if (cod == NULL)
        return -1;
    AVCodecContext *c = avcodec_alloc_context3(cod);
    CONTROLLA(c != NULL);
    c->sample_rate = 48000;
    c->sample_fmt = AV_SAMPLE_FMT_S16;
    av_channel_layout_default(&c->ch_layout, 2);
    c->bit_rate = 128000;
    c->time_base = (AVRational){1, 48000};
    CONTROLLA(avcodec_open2(c, cod, NULL) >= 0);
    CONTROLLA(c->frame_size == 960);
    AVFrame *f = av_frame_alloc();
    f->nb_samples = 960;
    f->format = AV_SAMPLE_FMT_S16;
    f->sample_rate = 48000;
    av_channel_layout_default(&f->ch_layout, 2);
    CONTROLLA(av_frame_get_buffer(f, 0) >= 0);
    int got = 0;
    AVPacket *p = av_packet_alloc();
    for (int i = 0; i <= n; i++) { /* l'ultimo giro svuota il codificatore */
        if (i < n) {
            int16_t *s = (int16_t *)f->data[0];
            for (int k = 0; k < 960; k++) {
                int16_t v = (int16_t)(8000 * sin(2 * M_PI * 440.0 * (i * 960 + k) / 48000.0));
                s[k * 2] = v;
                s[k * 2 + 1] = v;
            }
            f->pts = (int64_t)i * 960;
            CONTROLLA(avcodec_send_frame(c, f) >= 0);
        } else {
            CONTROLLA(avcodec_send_frame(c, NULL) >= 0);
        }
        while (avcodec_receive_packet(c, p) >= 0) {
            CONTROLLA(got < n + 8);
            pk[got] = av_packet_alloc();
            CONTROLLA(av_packet_ref(pk[got], p) >= 0);
            got++;
            av_packet_unref(p);
        }
    }
    av_packet_free(&p);
    av_frame_free(&f);
    avcodec_free_context(&c);
    CONTROLLA(got >= n);
    for (int i = n; i < got; i++)
        av_packet_free(&pk[i]); /* pacchetti di coda del codificatore: non servono */
    return n;
}

int main(int argc, char **argv)
{
    CONTROLLA(argc == 3);
    FFPROBE = argv[2];
    size_t n;
    uint8_t *d = leggi_file(argv[1], &n);
    static Au au[N_FOTOGRAMMI + 8];
    int nau = taglia(d, n, au, N_FOTOGRAMMI + 8);
    CONTROLLA(nau == N_FOTOGRAMMI && au[0].chiave && au[60].chiave && !au[1].chiave);

    char dir[64], p[600];
    cartella_prova(dir, sizeof dir, "prova-mp4");

    /* Con pacchetto di configurazione separato, e con la configurazione solo dentro il primo chiave. */
    registra(dir, d, au, nau, true, NULL, 0, p);
    controlla_video(p, N_FOTOGRAMMI, true);
    controlla_tempi(p, N_FOTOGRAMMI);
    char p_intero[600];
    snprintf(p_intero, sizeof p_intero, "%s", p);
    registra(dir, d, au, nau, false, NULL, 0, p);
    controlla_video(p, N_FOTOGRAMMI, true);

    /* Chiusura regolare: indice mfra in coda e durata leggibile (non N/A) anche da altri lettori. */
    {
        size_t nr;
        uint8_t *raw = leggi_file(p_intero, &nr);
        CONTROLLA(nr > 16 && memcmp(raw + nr - 12, "mfro", 4) == 0);
        free(raw);
        char cmd[900], out[4096];
        snprintf(cmd, sizeof cmd, "'%s' -v error -show_entries format=duration -of default=nw=1:nk=1 '%s' 2>&1",
                 FFPROBE, p_intero);
        esegui(cmd, out, sizeof out);
        CONTROLLA(strstr(out, "N/A") == NULL && atof(out) > 1.9 && atof(out) < 2.1);
    }

    /* Il file e' riempito da veri byte H.264: non piu' piccoli del flusso meno la configurazione. */
    size_t nf;
    uint8_t *f = leggi_file(p_intero, &nf);
    CONTROLLA(nf > n / 2 && nf < n + 65536);
    Riepilogo ri;
    leggi_mp4(p_intero, &ri);
    CONTROLLA(ri.aperto && ri.box_completi && ri.campioni[0] == N_FOTOGRAMMI && ri.nal_parametri == 0);

    /* Troncato a meta' di un frammento: ancora leggibile, con meno fotogrammi. */
    char tronco[700];
    snprintf(tronco, sizeof tronco, "%s/troncato.mp4", dir);
    FILE *t = fopen(tronco, "wb");
    CONTROLLA(t != NULL && fwrite(f, 1, nf * 6 / 10, t) == nf * 6 / 10);
    fclose(t);
    controlla_video(tronco, N_FOTOGRAMMI, false);
    /* Troncato appena dopo l'intestazione (nessun frammento): non si pretende nulla dai
     * fotogrammi, ma ffprobe non deve rifiutare il contenitore. */
    free(f);

    /* Audio Opus vero. */
    AVPacket *opus[N_FOTOGRAMMI * 2];
    int n_opus = genera_opus(opus, 100);
    if (n_opus < 0) {
        fprintf(stderr, "libopus assente: parte audio non provata\n");
        togli_cartella(dir);
        return 77; /* meson: prova saltata */
    }
    CONTROLLA(n_opus == 100);
    registra(dir, d, au, nau, true, opus, n_opus, p);
    {
        char cmd[900], out[65536];
        snprintf(cmd, sizeof cmd,
                 "'%s' -v error -count_frames -count_packets -show_entries stream=codec_name,channels,sample_rate,"
                 "nb_read_packets -of default=nw=1 '%s' 2>&1",
                 FFPROBE, p);
        esegui(cmd, out, sizeof out);
        CONTROLLA(strstr(out, "codec_name=opus") != NULL);
        CONTROLLA(valore(out, "channels=") == 2 && valore(out, "sample_rate=") == 48000);
        /* Il video e' ancora tutto e l'audio ha tutti i suoi pacchetti. */
        const char *a = strstr(out, "codec_name=opus");
        CONTROLLA(valore(a, "nb_read_packets=") == 100);
        CONTROLLA(valore(out, "nb_read_packets=") == N_FOTOGRAMMI || valore(a, "nb_read_packets=") == 100);
        CONTROLLA(strstr(out, "rror") == NULL && strstr(out, "arning") == NULL);
        /* Durata del contenitore vicina ai 2 s del video. */
        snprintf(cmd, sizeof cmd, "'%s' -v error -show_entries format=duration -of default=nw=1:nk=1 '%s' 2>&1",
                 FFPROBE, p);
        esegui(cmd, out, sizeof out);
        double dur = atof(out);
        CONTROLLA(dur > 1.9 && dur < 2.1);
    }
    for (int i = 0; i < n_opus; i++)
        av_packet_free(&opus[i]);
    free(d);
    togli_cartella(dir);
    printf("prova_cattura_mp4: ok\n");
    return 0;
}
