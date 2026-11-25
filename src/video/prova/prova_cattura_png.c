/* Prova di screenshot e nomi: PNG scritto e riletto (misure e colori),
 * conversioni di formato, nessuna sovrascrittura, nomi ripuliti. */
#include <dirent.h>
#include <math.h>
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>

#include "aiuti.h"
#include "video/cattura_interno.h"

/* Rilegge un PNG con il decodificatore di FFmpeg: RGB24 compatto. */
static uint8_t *rileggi_png(const uint8_t *dati, size_t n, int *w, int *h)
{
    CONTROLLA(n > 8 && memcmp(dati, "\x89PNG\r\n\x1a\n", 8) == 0);
    const AVCodec *cod = avcodec_find_decoder(AV_CODEC_ID_PNG);
    AVCodecContext *c = avcodec_alloc_context3(cod);
    CONTROLLA(c != NULL && avcodec_open2(c, cod, NULL) >= 0);
    AVPacket *pk = av_packet_alloc();
    CONTROLLA(av_new_packet(pk, (int)n) >= 0);
    memcpy(pk->data, dati, n);
    CONTROLLA(avcodec_send_packet(c, pk) >= 0);
    AVFrame *fr = av_frame_alloc();
    CONTROLLA(avcodec_receive_frame(c, fr) >= 0);
    CONTROLLA(fr->format == AV_PIX_FMT_RGB24);
    *w = fr->width;
    *h = fr->height;
    uint8_t *out = malloc((size_t)fr->width * (size_t)fr->height * 3);
    CONTROLLA(out != NULL);
    for (int y = 0; y < fr->height; y++)
        memcpy(out + (size_t)y * fr->width * 3, fr->data[0] + (size_t)y * fr->linesize[0],
               (size_t)fr->width * 3);
    av_frame_free(&fr);
    av_packet_free(&pk);
    avcodec_free_context(&c);
    return out;
}

static const uint8_t COLORI[4][3] = {{200, 40, 40}, {30, 180, 60}, {40, 60, 220}, {230, 230, 20}};

/* Quadrante di un pixel: la divisione cade su coordinate pari. */
static int quadrante(int x, int y, int w, int h)
{
    return (x >= ((w / 2) & ~1) ? 1 : 0) + (y >= ((h / 2) & ~1) ? 2 : 0);
}

static uint8_t arrotonda(double v)
{
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5);
}

/* Fotogramma sintetico a quattro quadranti nel formato chiesto. */
static AVFrame *fotogramma(enum AVPixelFormat fmt, int w, int h, enum AVColorSpace cs,
                           enum AVColorRange range)
{
    AVFrame *f = av_frame_alloc();
    CONTROLLA(f != NULL);
    f->format = fmt;
    f->width = w;
    f->height = h;
    f->colorspace = cs;
    f->color_range = range;
    CONTROLLA(av_frame_get_buffer(f, 0) >= 0);
    double kr = 0.299, kb = 0.114;
    if (cs == AVCOL_SPC_BT709 || (cs == AVCOL_SPC_UNSPECIFIED && h >= 720)) {
        kr = 0.2126;
        kb = 0.0722;
    }
    double kg = 1 - kr - kb;
    bool pieno = range == AVCOL_RANGE_JPEG || fmt == AV_PIX_FMT_YUVJ420P;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t *c = COLORI[quadrante(x, y, w, h)];
            double Y = kr * c[0] + kg * c[1] + kb * c[2];
            double Cb = (c[2] - Y) / (2 * (1 - kb)), Cr = (c[0] - Y) / (2 * (1 - kr));
            uint8_t yy = pieno ? arrotonda(Y) : arrotonda(16 + Y * 219 / 255);
            uint8_t u = pieno ? arrotonda(128 + Cb) : arrotonda(128 + Cb * 224 / 255);
            uint8_t v = pieno ? arrotonda(128 + Cr) : arrotonda(128 + Cr * 224 / 255);
            if (fmt == AV_PIX_FMT_YUV420P || fmt == AV_PIX_FMT_YUVJ420P || fmt == AV_PIX_FMT_NV12) {
                f->data[0][(size_t)y * f->linesize[0] + x] = yy;
                if (x % 2 == 0 && y % 2 == 0) {
                    if (fmt == AV_PIX_FMT_NV12) {
                        f->data[1][(size_t)(y / 2) * f->linesize[1] + x] = u;
                        f->data[1][(size_t)(y / 2) * f->linesize[1] + x + 1] = v;
                    } else {
                        f->data[1][(size_t)(y / 2) * f->linesize[1] + x / 2] = u;
                        f->data[2][(size_t)(y / 2) * f->linesize[2] + x / 2] = v;
                    }
                }
            } else if (fmt == AV_PIX_FMT_RGB24) {
                memcpy(f->data[0] + (size_t)y * f->linesize[0] + x * 3, c, 3);
            } else { /* BGRA e RGBA */
                uint8_t *p = f->data[0] + (size_t)y * f->linesize[0] + x * 4;
                bool bgr = fmt == AV_PIX_FMT_BGRA;
                p[0] = bgr ? c[2] : c[0];
                p[1] = c[1];
                p[2] = bgr ? c[0] : c[2];
                p[3] = 255;
            }
        }
    }
    return f;
}

static void prova_formato(enum AVPixelFormat fmt, int w, int h, enum AVColorSpace cs,
                          enum AVColorRange range, int tolleranza)
{
    AVFrame *f = fotogramma(fmt, w, h, cs, range);
    PhErrore e = {0};
    uint8_t *png = NULL;
    size_t n = 0;
    CONTROLLA(ph_cattura_png_memoria(f, &png, &n, &e) == 0);
    CONTROLLA(png != NULL && n > 8);
    /* Misure nell'intestazione IHDR, lette a mano. */
    CONTROLLA(be32(png + 16) == (uint32_t)w && be32(png + 20) == (uint32_t)h);
    int w2, h2;
    uint8_t *rgb = rileggi_png(png, n, &w2, &h2);
    CONTROLLA(w2 == w && h2 == h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int k = 0; k < 3; k++) {
                /* Con il colore ridotto a meta' risoluzione i bordi tra i quadranti sono sfumati. */
                if (tolleranza > 0 && (abs(x - ((w / 2) & ~1)) < 6 || abs(y - ((h / 2) & ~1)) < 6))
                    continue;
                int atteso = COLORI[quadrante(x, y, w, h)][k];
                int dato = rgb[((size_t)y * w + x) * 3 + k];
                if (abs(atteso - dato) > tolleranza) {
                    fprintf(stderr, "%s %dx%d: pixel (%d,%d) canale %d: atteso %d, dato %d\n",
                            av_get_pix_fmt_name(fmt), w, h, x, y, k, atteso, dato);
                    exit(1);
                }
            }
    free(rgb);
    free(png);
    av_frame_free(&f);
}

static void prova_nomi(void)
{
    struct tm t = {.tm_year = 2026 - 1900, .tm_mon = 10, .tm_mday = 23, .tm_hour = 14, .tm_min = 5,
                   .tm_sec = 9, .tm_isdst = -1};
    time_t q = mktime(&t);
    char s[512];
    PhErrore e = {0};
    CONTROLLA(ph_cattura_nome("Orologio", "png", q, 1, s, sizeof s, &e) == 0);
    CONTROLLA(strcmp(s, "Orologio 2026-11-23 14.05.09.png") == 0);
    CONTROLLA(ph_cattura_nome("Orologio", "mp4", q, 3, s, sizeof s, &e) == 0);
    CONTROLLA(strcmp(s, "Orologio 2026-11-23 14.05.09 (3).mp4") == 0);
    CONTROLLA(ph_cattura_nome("Orologio", "png", q, 2, s, sizeof s, &e) == 0);
    CONTROLLA(strcmp(s, "Orologio 2026-11-23 14.05.09 (2).png") == 0);
    /* Ripulitura. */
    CONTROLLA(ph_cattura_nome("Foo/Bar:x*?\"<>|\\y\n", "png", q, 1, s, sizeof s, &e) == 0);
    CONTROLLA(strncmp(s, "Foo_Bar_x_______y_ 2026", 23) == 0);
    CONTROLLA(strchr(s, '/') == NULL && strchr(s, '\n') == NULL);
    CONTROLLA(ph_cattura_nome("../../etc/passwd", "png", q, 1, s, sizeof s, &e) == 0);
    CONTROLLA(strchr(s, '/') == NULL && s[0] != '.');
    CONTROLLA(ph_cattura_nome("  .nascosta. ", "png", q, 1, s, sizeof s, &e) == 0);
    CONTROLLA(strncmp(s, "nascosta 2026", 13) == 0);
    CONTROLLA(ph_cattura_nome(NULL, "png", q, 1, s, sizeof s, &e) == 0);
    CONTROLLA(strncmp(s, "YASDE 2026", 10) == 0);
    CONTROLLA(ph_cattura_nome("...", "png", q, 1, s, sizeof s, &e) == 0);
    CONTROLLA(strncmp(s, "YASDE 2026", 10) == 0);
    /* UTF-8 lungo: tagliato a 80 byte al massimo e mai a meta' carattere. */
    char lungo[400] = "";
    for (int i = 0; i < 100; i++)
        strcat(lungo, "\xE2\x82\xAC"); /* euro, 3 byte */
    CONTROLLA(ph_cattura_nome(lungo, "png", q, 1, s, sizeof s, &e) == 0);
    const char *sp = strchr(s, ' ');
    size_t nome_len = (size_t)(sp - s);
    CONTROLLA(nome_len <= 80 && nome_len % 3 == 0 && nome_len > 0);
    for (size_t i = 0; i < nome_len; i += 3)
        CONTROLLA(memcmp(s + i, "\xE2\x82\xAC", 3) == 0);
    /* Errori. */
    CONTROLLA(ph_cattura_nome("a", "", q, 1, s, sizeof s, &e) == -1);
    CONTROLLA(ph_cattura_nome("a", "p/g", q, 1, s, sizeof s, &e) == -1);
    CONTROLLA(ph_cattura_nome("a", NULL, q, 1, s, sizeof s, &e) == -1);
    CONTROLLA(ph_cattura_nome("Orologio", "png", q, 1, s, 10, &e) == -1);
}

static void prova_creazione_senza_sovrascrivere(const char *dir)
{
    struct tm t = {.tm_year = 2026 - 1900, .tm_mon = 10, .tm_mday = 23, .tm_hour = 14, .tm_min = 5,
                   .tm_sec = 9, .tm_isdst = -1};
    time_t q = mktime(&t);
    char p[3][600];
    PhErrore e = {0};
    for (int i = 0; i < 3; i++) {
        int fd = ph_cattura_apri_nuovo(dir, "App", "png", q, p[i], sizeof p[i], &e);
        CONTROLLA(fd >= 0);
        char marca[16];
        snprintf(marca, sizeof marca, "file%d", i);
        CONTROLLA(write(fd, marca, strlen(marca)) == (ssize_t)strlen(marca));
        close(fd);
    }
    CONTROLLA(strstr(p[0], " (") == NULL);
    CONTROLLA(strstr(p[1], "09 (2).png") != NULL);
    CONTROLLA(strstr(p[2], "09 (3).png") != NULL);
    /* Il primo non e' stato toccato dagli altri due. */
    for (int i = 0; i < 3; i++) {
        size_t n;
        uint8_t *d = leggi_file(p[i], &n);
        char marca[16];
        snprintf(marca, sizeof marca, "file%d", i);
        CONTROLLA(n == strlen(marca) && memcmp(d, marca, n) == 0);
        free(d);
    }
    /* Un collegamento simbolico con il nome atteso non viene seguito. */
    char link[700];
    snprintf(link, sizeof link, "%s/Altra 2026-11-23 14.05.09.png", dir);
    char bersaglio[700];
    snprintf(bersaglio, sizeof bersaglio, "%s/bersaglio", dir);
    CONTROLLA(symlink(bersaglio, link) == 0);
    char scelto[600];
    int fd = ph_cattura_apri_nuovo(dir, "Altra", "png", q, scelto, sizeof scelto, &e);
    CONTROLLA(fd >= 0 && strstr(scelto, "09 (2).png") != NULL);
    close(fd);
    CONTROLLA(access(bersaglio, F_OK) != 0); /* il bersaglio non e' stato creato */
    /* Dopo 999 file con lo stesso nome (ora uguale) si rinuncia con un errore, senza toccare nulla. */
    for (int i = 0; i < 999; i++) {
        int f = ph_cattura_apri_nuovo(dir, "Tanti", "png", q, NULL, 0, &e);
        CONTROLLA(f >= 0);
        close(f);
    }
    CONTROLLA(ph_cattura_apri_nuovo(dir, "Tanti", "png", q, NULL, 0, &e) == -1);
    CONTROLLA(strstr(e.testo, "troppi") != NULL);
    /* Cartella impossibile: errore, nessun crash. */
    char x[700];
    snprintf(x, sizeof x, "%s/file0x/dentro/ancora", dir);
    CONTROLLA(ph_cattura_apri_nuovo(x, "App", "png", q, NULL, 0, &e) == -1 && e.testo[0] != 0);
}

static int conta_file(const char *dir)
{
    DIR *d = opendir(dir);
    CONTROLLA(d != NULL);
    int n = 0;
    struct dirent *v;
    while ((v = readdir(d)) != NULL)
        if (v->d_name[0] != '.')
            n++;
    closedir(d);
    return n;
}

static void prova_screenshot_file(const char *dir)
{
    AVFrame *f = fotogramma(AV_PIX_FMT_YUV420P, 64, 48, AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED);
    PhErrore e = {0};
    char p1[600], p2[600];
    CONTROLLA(ph_cattura_screenshot(f, dir, "Prova app", p1, sizeof p1, &e) == 0);
    CONTROLLA(ph_cattura_screenshot(f, dir, "Prova app", p2, sizeof p2, &e) == 0);
    CONTROLLA(strcmp(p1, p2) != 0);
    size_t n;
    uint8_t *d = leggi_file(p1, &n);
    int w, h;
    uint8_t *rgb = rileggi_png(d, n, &w, &h);
    CONTROLLA(w == 64 && h == 48);
    free(rgb);
    free(d);
    /* Percorso troppo lungo per il buffer: errore e nessun file in piu'. */
    char piccolo[8];
    int prima = conta_file(dir);
    CONTROLLA(ph_cattura_screenshot(f, dir, "Prova app", piccolo, sizeof piccolo, &e) == -1);
    CONTROLLA(conta_file(dir) == prima);
    av_frame_free(&f);
}

int main(void)
{
    prova_nomi();
    /* Colori: BT.601 (piccolo), BT.709 dichiarato, 709 per altezza >= 720, intervallo pieno, NV12, RGB. */
    prova_formato(AV_PIX_FMT_YUV420P, 64, 48, AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, 4);
    prova_formato(AV_PIX_FMT_YUV420P, 33, 17, AVCOL_SPC_BT470BG, AVCOL_RANGE_MPEG, 4); /* misure dispari */
    prova_formato(AV_PIX_FMT_YUV420P, 64, 48, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, 4);
    prova_formato(AV_PIX_FMT_YUV420P, 64, 720, AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, 4);
    prova_formato(AV_PIX_FMT_YUV420P, 64, 48, AVCOL_SPC_BT709, AVCOL_RANGE_JPEG, 4);
    prova_formato(AV_PIX_FMT_YUVJ420P, 40, 24, AVCOL_SPC_BT470BG, AVCOL_RANGE_UNSPECIFIED, 4);
    prova_formato(AV_PIX_FMT_NV12, 50, 30, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, 4);
    prova_formato(AV_PIX_FMT_RGB24, 31, 9, AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, 0);
    prova_formato(AV_PIX_FMT_BGRA, 16, 16, AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, 0);
    prova_formato(AV_PIX_FMT_RGBA, 16, 16, AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, 0);

    /* Il codice colore sbagliato si vede: 601 letto come 709 dovrebbe superare la tolleranza. */
    {
        AVFrame *f = fotogramma(AV_PIX_FMT_YUV420P, 64, 48, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG);
        f->colorspace = AVCOL_SPC_BT470BG; /* si finge che il flusso dichiari un'altra matrice */
        PhErrore e = {0};
        uint8_t *png = NULL;
        size_t n = 0;
        CONTROLLA(ph_cattura_png_memoria(f, &png, &n, &e) == 0);
        int w, h;
        uint8_t *rgb = rileggi_png(png, n, &w, &h);
        int grande = 0;
        for (int k = 0; k < 3; k++)
            if (abs(COLORI[0][k] - rgb[k]) > 6)
                grande = 1;
        CONTROLLA(grande); /* la matrice dichiarata e' davvero usata */
        free(rgb);
        free(png);
        av_frame_free(&f);
    }

    /* Errori. */
    {
        PhErrore e = {0};
        uint8_t *png = (uint8_t *)1;
        size_t n = 5;
        CONTROLLA(ph_cattura_png_memoria(NULL, &png, &n, &e) == -1 && png == NULL && n == 0);
        CONTROLLA(e.testo[0] != 0);
        /* Formato che swscale non legge (memoria della scheda senza contesto): errore chiaro. */
        AVFrame *f = fotogramma(AV_PIX_FMT_YUV420P, 16, 16, AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED);
        f->format = AV_PIX_FMT_VAAPI;
        CONTROLLA(ph_cattura_png_memoria(f, &png, &n, &e) == -1 && png == NULL);
        CONTROLLA(strstr(e.testo, "vaapi") != NULL);
        f->format = AV_PIX_FMT_YUV420P;
        f->width = 0;
        CONTROLLA(ph_cattura_png_memoria(f, &png, &n, &e) == -1);
        f->width = 20000;
        CONTROLLA(ph_cattura_png_memoria(f, &png, &n, &e) == -1);
        f->width = 16;
        f->format = AV_PIX_FMT_YUV420P;
        uint8_t *piano1 = f->data[1];
        f->data[1] = NULL; /* piano mancante: errore, non crash */
        CONTROLLA(ph_cattura_png_memoria(f, &png, &n, &e) == -1);
        f->data[1] = piano1;
        f->linesize[0] = 4; /* riga piu' corta della larghezza */
        CONTROLLA(ph_cattura_png_memoria(f, &png, &n, &e) == -1);
        av_frame_free(&f);
    }

    char dir[64];
    cartella_prova(dir, sizeof dir, "prova-png");
    prova_creazione_senza_sovrascrivere(dir);
    prova_screenshot_file(dir);
    togli_cartella(dir);
    printf("prova_cattura_png: ok\n");
    return 0;
}
