/* Prototipo di misura per il punto aperto A1 (specifica 4.3): GTK4, con il
 * suo motore di disegno, riesce a comporre piu' flussi video senza copie e
 * senza scatti mentre si trascinano e ridimensionano le finestre interne?
 * NON e' la shell del prodotto: serve solo a misurare.
 *
 * Una finestra GTK4 con sfondo sfumato e, per ogni pacchetto, una "finestra
 * interna" di widget (titolo, immagine, bordino con la maniglia). Le finestre
 * interne stanno in un contenitore a posizioni libere scritto qui
 * (Scrivania); si trascinano dal titolo, si ridimensionano dalla maniglia in
 * basso a destra (l'immagine viene solo scalata da GTK, al telefono non si
 * chiede nulla), un clic le porta davanti.
 *
 * Video, due modalita':
 *  - predefinita, senza copie: il fotogramma VA-API diventa un DMA-BUF
 *    (av_hwframe_map verso DRM_PRIME) e GTK lo prende con
 *    GdkDmabufTextureBuilder; il fotogramma FFmpeg resta vivo finche' GTK usa
 *    la texture. L'immagine sta in un GtkGraphicsOffload.
 *  - --copia: il fotogramma passa in memoria, viene convertito in RGB qui
 *    (GTK non legge NV12 da memoria) e caricato come texture di memoria.
 *
 * Thread: un thread per flusso legge e decodifica; al thread GTK arriva solo
 * "ecco l'ultimo fotogramma" (g_idle_add); se chi disegna e' indietro il
 * fotogramma vecchio si scarta. I widget si toccano solo dal thread GTK.
 *
 * Uso: prototipo-gtk [--chiave FILE] [--telefono HOST:PORTA] [--via V]
 *                    [--copia] [--giostra SECONDI] JAR PACCHETTO [PACCHETTO...]
 * F11 = schermo intero. --giostra muove e ridimensiona da sola le finestre
 * interne per quei secondi (animazione guidata dal frame clock), manda lo
 * scorrimento continuo a tutte le sessioni e poi chiude. */
#include <gtk/gtk.h>

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/pixfmt.h>

#include "adb/adb.h"
#include "componente/componente.h"
#include "scoperta/scoperta.h"
#include "video/decodifica.h"

#define MAX_FLUSSI 8
#define ALTEZZA_RICHIESTA 1280
#define ALTEZZA_TITOLO 26
#define ALTEZZA_BORDINO 14
#define MAX_CAMPIONI 400000
#define PASSI_SCORRIMENTO 20
#define PASSO_MOVIMENTO_MS 16

#define QUATTROCC(a, b, c, d) \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define FOURCC_NV12 QUATTROCC('N', 'V', '1', '2')
#define FOURCC_P010 QUATTROCC('P', '0', '1', '0')

/* Un fotogramma pronto per il thread GTK: o un DMA-BUF (f) o pixel RGB. */
typedef struct {
    bool drm;
    AVFrame *f;                 /* fotogramma DRM_PRIME (tiene vivo quello VA-API) */
    enum AVPixelFormat sw;      /* formato dei pixel sotto, per ricavare il fourcc */
    uint8_t *rgb;               /* RGB24 in memoria (modalita' copia o ripiego) */
    int stride;
    int w, h;
} Pronto;

typedef struct Flusso {
    const char *pacchetto;
    int indice;
    PhVideoFlusso *flusso;
    PhDecodifica *dec;
    pthread_t th;
    bool th_avviato;
    pthread_mutex_t m;
    Pronto *ultimo;              /* protetto da m */
    atomic_bool pendente;        /* un g_idle_add e' gia' in coda */
    atomic_bool dmabuf_rifiutato;/* GTK ha rifiutato un DMA-BUF: da qui in poi si copia */
    atomic_long ricevuti, mostrati, scartati, errori, dmabuf_ok, dmabuf_ko, ripieghi;
    atomic_int vw, vh;
    atomic_uint id;
    bool errore_noto;            /* solo thread GTK */
    /* Parte grafica (solo thread GTK). x, y, w, h = finestra interna intera. */
    double x, y, w, h;
    double x0, y0, w0, h0;       /* valori a inizio trascinamento */
    GtkWidget *box, *immagine, *offload;
} Flusso;

static Flusso g_fl[MAX_FLUSSI];
static int g_n = 0;
static PhComponente *g_comp = NULL;
static atomic_bool g_ferma;
static bool g_copia = false;
static double g_giostra = 0;
static const char *g_file = NULL;       /* --file: sorgente registrata, senza telefono */
static double g_rapporto = 2.0;         /* altezza/larghezza dello schermo del telefono */
static GMainLoop *g_ciclo = NULL;
static GtkWidget *g_finestra = NULL, *g_scrivania = NULL;

/* ---- Misure della finestra ------------------------------------------------*/
typedef struct {
    double *v;
    size_t n, cap;
} Serie;

static Serie g_tempo, g_presentazione;   /* intervalli in ms */
static gint64 g_ultimo_tempo = 0, g_ultima_pres = 0;
static gint64 g_ultimo_contatore_pres = -1;
static long g_disegni = 0;
static size_t g_idx_tempo = 0, g_idx_pres = 0;
static long g_disegni_prima = 0;
static gint64 g_istante_rapporto = 0, g_istante_inizio = 0;
static char g_motore[96] = "?";
static bool g_motore_vulkan = false;

static void dormi_ms(int ms)
{
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

static void serie_aggiungi(Serie *s, double x)
{
    if (s->n >= MAX_CAMPIONI)
        return;
    if (s->n == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 4096;
        double *nv = realloc(s->v, nc * sizeof *nv);
        if (nv == NULL)
            return;
        s->v = nv;
        s->cap = nc;
    }
    s->v[s->n++] = x;
}

static int conf_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* Mediana, 95, 99 percentile e massimo dei campioni da `da` in poi. */
static void serie_riepilogo(const Serie *s, size_t da, char *out, size_t cap)
{
    size_t n = s->n > da ? s->n - da : 0;
    if (n == 0) {
        snprintf(out, cap, "nessun campione");
        return;
    }
    double *c = malloc(n * sizeof *c);
    if (c == NULL) {
        snprintf(out, cap, "memoria finita");
        return;
    }
    memcpy(c, s->v + da, n * sizeof *c);
    qsort(c, n, sizeof *c, conf_double);
    snprintf(out, cap, "mediana %.2f, p95 %.2f, p99 %.2f, max %.2f ms (%zu campioni)",
             c[(size_t)(0.50 * (double)(n - 1) + 0.5)], c[(size_t)(0.95 * (double)(n - 1) + 0.5)],
             c[(size_t)(0.99 * (double)(n - 1) + 0.5)], c[n - 1], n);
    free(c);
}

/* Dal frame clock: dopo ogni disegno. Intervallo tra i tempi dei fotogrammi
 * disegnati e, quando il sistema li da', tra gli istanti di presentazione. */
static void dopo_disegno(GdkFrameClock *clock, gpointer dati)
{
    (void)dati;
    gint64 t = gdk_frame_clock_get_frame_time(clock);
    if (g_ultimo_tempo != 0)
        serie_aggiungi(&g_tempo, (double)(t - g_ultimo_tempo) / 1000.0);
    g_ultimo_tempo = t;
    g_disegni++;
    gint64 c = gdk_frame_clock_get_frame_counter(clock) - 3;
    if (c > g_ultimo_contatore_pres) {
        GdkFrameTimings *ft = gdk_frame_clock_get_timings(clock, c);
        if (ft != NULL && gdk_frame_timings_get_complete(ft)) {
            gint64 p = gdk_frame_timings_get_presentation_time(ft);
            if (p != 0) {
                if (g_ultima_pres != 0)
                    serie_aggiungi(&g_presentazione, (double)(p - g_ultima_pres) / 1000.0);
                g_ultima_pres = p;
            }
            g_ultimo_contatore_pres = c;
        }
    }
}

static void rapporto_flussi(void)
{
    for (int i = 0; i < g_n; i++) {
        Flusso *s = &g_fl[i];
        fprintf(stderr, "  [%d %s] ricevuti %ld, mostrati %ld, scartati %ld, errori %ld; dmabuf ok %ld, "
                        "rifiutati %ld, ripiego in copia %ld, GTK offload %s\n",
                i + 1, s->pacchetto, atomic_load(&s->ricevuti), atomic_load(&s->mostrati),
                atomic_load(&s->scartati), atomic_load(&s->errori), atomic_load(&s->dmabuf_ok),
                atomic_load(&s->dmabuf_ko), atomic_load(&s->ripieghi),
                g_copia ? "spento (--copia)"
                        : "richiesto (GTK non dice se e' attivo)");
    }
}

static gboolean rapporto_periodico(gpointer dati)
{
    (void)dati;
    gint64 ora = g_get_monotonic_time();
    double sec = (double)(ora - g_istante_rapporto) / 1e6;
    char r[200];
    serie_riepilogo(&g_tempo, g_idx_tempo, r, sizeof r);
    fprintf(stderr, "ultimi %.1f s: finestra %.1f fotogrammi/s disegnati; tempo %s\n", sec,
            (double)(g_disegni - g_disegni_prima) / sec, r);
    serie_riepilogo(&g_presentazione, g_idx_pres, r, sizeof r);
    fprintf(stderr, "  presentazione: %s\n", r);
    rapporto_flussi();
    g_idx_tempo = g_tempo.n;
    g_idx_pres = g_presentazione.n;
    g_disegni_prima = g_disegni;
    g_istante_rapporto = ora;
    return G_SOURCE_CONTINUE;
}

static void riepilogo_finale(void)
{
    double sec = (double)(g_get_monotonic_time() - g_istante_inizio) / 1e6;
    char r[200];
    fprintf(stderr, "== RIEPILOGO (%.1f s) ==\nmotore di disegno: %s%s\n", sec, g_motore,
            g_motore_vulkan ? "" : " (NON Vulkan)");
    fprintf(stderr, "modalita': %s\n", g_copia ? "copia" : "senza copie (DMA-BUF)");
    fprintf(stderr, "finestra: %ld fotogrammi disegnati, %.1f al secondo\n", g_disegni,
            sec > 0 ? (double)g_disegni / sec : 0);
    serie_riepilogo(&g_tempo, 0, r, sizeof r);
    fprintf(stderr, "intervalli tra fotogrammi (frame clock): %s\n", r);
    size_t lunghi = 0;
    for (size_t k = 0; k < g_tempo.n; k++)
        if (g_tempo.v[k] > 33.0)
            lunghi++;
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    double cpu = (double)ru.ru_utime.tv_sec + (double)ru.ru_stime.tv_sec +
                 (double)(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6;
    fprintf(stderr, "pause oltre 33 ms: %zu su %zu intervalli; CPU del processo %.0f%% di un core\n",
            lunghi, g_tempo.n, sec > 0 ? 100.0 * cpu / sec : 0);
    serie_riepilogo(&g_presentazione, 0, r, sizeof r);
    fprintf(stderr, "intervalli di presentazione: %s\n", r);
    rapporto_flussi();
}

/* ---- Contenitore a posizioni libere ---------------------------------------*/
typedef struct {
    GtkWidget parent;
} Scrivania;
typedef struct {
    GtkWidgetClass parent_class;
} ScrivaniaClass;
G_DEFINE_TYPE(Scrivania, scrivania, GTK_TYPE_WIDGET)

static void scrivania_measure(GtkWidget *w, GtkOrientation o, int per, int *min, int *nat,
                              int *min_base, int *nat_base)
{
    (void)w; (void)o; (void)per;
    *min = *nat = 0;
    *min_base = *nat_base = -1;
}

static void scrivania_size_allocate(GtkWidget *w, int larghezza, int altezza, int base)
{
    (void)larghezza; (void)altezza; (void)base;
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        Flusso *s = g_object_get_data(G_OBJECT(c), "fin");
        if (s == NULL)
            continue;
        int mw = 0, mh = 0;
        gtk_widget_measure(c, GTK_ORIENTATION_HORIZONTAL, -1, &mw, NULL, NULL, NULL);
        gtk_widget_measure(c, GTK_ORIENTATION_VERTICAL, -1, &mh, NULL, NULL, NULL);
        int cw = (int)s->w > mw ? (int)s->w : mw, ch = (int)s->h > mh ? (int)s->h : mh;
        graphene_point_t p = {(float)s->x, (float)s->y};
        gtk_widget_allocate(c, cw, ch, -1, gsk_transform_translate(NULL, &p));
    }
}

static void scrivania_snapshot(GtkWidget *w, GtkSnapshot *snap)
{
    graphene_rect_t r = {{0, 0}, {(float)gtk_widget_get_width(w), (float)gtk_widget_get_height(w)}};
    GdkRGBA da = {0.10f, 0.16f, 0.32f, 1.0f}, a = {0.55f, 0.25f, 0.45f, 1.0f};
    GskColorStop st[2] = {{0.0f, da}, {1.0f, a}};
    graphene_point_t p0 = {0, 0}, p1 = {r.size.width, r.size.height};
    gtk_snapshot_append_linear_gradient(snap, &r, &p0, &p1, st, 2);
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c))
        gtk_widget_snapshot_child(w, c, snap);
}

static void scrivania_dispose(GObject *o)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(GTK_WIDGET(o))) != NULL)
        gtk_widget_unparent(c);
    G_OBJECT_CLASS(scrivania_parent_class)->dispose(o);
}

static void scrivania_class_init(ScrivaniaClass *k)
{
    GtkWidgetClass *wc = GTK_WIDGET_CLASS(k);
    wc->measure = scrivania_measure;
    wc->size_allocate = scrivania_size_allocate;
    wc->snapshot = scrivania_snapshot;
    G_OBJECT_CLASS(k)->dispose = scrivania_dispose;
}

static void scrivania_init(Scrivania *s)
{
    gtk_widget_set_overflow(GTK_WIDGET(s), GTK_OVERFLOW_HIDDEN);
}

/* ---- Thread del flusso ----------------------------------------------------*/
static inline uint8_t limita8(int v)
{
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

/* YUV 4:2:0 (NV12 o planare) -> RGB24, BT.709 a gamma limitata: i colori sono
 * approssimati, qui conta il costo della copia, non la fedelta'. */
static bool yuv_a_rgb(const AVFrame *f, Pronto *p)
{
    bool nv12 = f->format == AV_PIX_FMT_NV12;
    if (!nv12 && f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P)
        return false;
    p->stride = f->width * 3;
    p->w = f->width;
    p->h = f->height;
    p->rgb = malloc((size_t)p->stride * (size_t)f->height);
    if (p->rgb == NULL)
        return false;
    for (int y = 0; y < f->height; y++) {
        const uint8_t *py = f->data[0] + (ptrdiff_t)y * f->linesize[0];
        const uint8_t *pu = f->data[1] + (ptrdiff_t)(y / 2) * f->linesize[1];
        const uint8_t *pv = nv12 ? NULL : f->data[2] + (ptrdiff_t)(y / 2) * f->linesize[2];
        uint8_t *o = p->rgb + (size_t)y * (size_t)p->stride;
        for (int x = 0; x < f->width; x++) {
            int c = (py[x] - 16) * 298 + 128;
            int u = nv12 ? pu[(x & ~1)] : pu[x / 2];
            int v = nv12 ? pu[(x & ~1) + 1] : pv[x / 2];
            u -= 128;
            v -= 128;
            *o++ = limita8((c + 459 * v) >> 8);
            *o++ = limita8((c - 55 * u - 136 * v) >> 8);
            *o++ = limita8((c + 541 * u) >> 8);
        }
    }
    return true;
}

static void libera_pronto(Pronto *p)
{
    if (p == NULL)
        return;
    av_frame_free(&p->f);
    free(p->rgb);
    free(p);
}

/* Prepara il fotogramma per GTK: DMA-BUF se possibile, altrimenti copia. */
static Pronto *prepara(Flusso *s, AVFrame *f)
{
    Pronto *p = calloc(1, sizeof *p);
    if (p == NULL) {
        av_frame_free(&f);
        return NULL;
    }
    if (!g_copia && f->format == AV_PIX_FMT_VAAPI && f->hw_frames_ctx != NULL &&
        !atomic_load(&s->dmabuf_rifiutato)) {
        AVFrame *d = av_frame_alloc();
        if (d != NULL) {
            d->format = AV_PIX_FMT_DRM_PRIME;
            if (av_hwframe_map(d, f, AV_HWFRAME_MAP_READ) == 0) {
                p->drm = true;
                p->f = d;
                p->sw = ((AVHWFramesContext *)f->hw_frames_ctx->data)->sw_format;
                p->w = f->width;
                p->h = f->height;
                av_frame_free(&f);
                return p;
            }
            av_frame_free(&d);
        }
        atomic_fetch_add(&s->dmabuf_ko, 1);
    }
    /* Copia (voluta con --copia, o ripiego). */
    if (!g_copia)
        atomic_fetch_add(&s->ripieghi, 1);
    AVFrame *m = f;
    if (f->hw_frames_ctx != NULL) {
        m = av_frame_alloc();
        if (m == NULL || av_hwframe_transfer_data(m, f, 0) < 0) {
            av_frame_free(&m);
            av_frame_free(&f);
            free(p);
            atomic_fetch_add(&s->errori, 1);
            return NULL;
        }
        av_frame_free(&f);
    }
    bool ok = yuv_a_rgb(m, p);
    av_frame_free(&m);
    if (!ok) {
        free(p);
        atomic_fetch_add(&s->errori, 1);
        return NULL;
    }
    return p;
}

static gboolean consegna(gpointer dati);

/* Decodifica un pacchetto e consegna i fotogrammi che ne escono. */
static void tratta(Flusso *s, bool config, const uint8_t *dati, size_t n, uint64_t tempo_us)
{
    PhErrore e;
    if (ph_decodifica_dai(s->dec, config, dati, n, tempo_us, &e) != 0) {
        atomic_fetch_add(&s->errori, 1);
        return;
    }
    AVFrame *f = NULL;
    while (ph_decodifica_prendi(s->dec, &f, &e) == 1) {
        atomic_fetch_add(&s->ricevuti, 1);
        atomic_store(&s->vw, f->width);
        atomic_store(&s->vh, f->height);
        Pronto *p = prepara(s, f);
        if (p == NULL)
            continue;
        pthread_mutex_lock(&s->m);
        if (s->ultimo != NULL) {
            libera_pronto(s->ultimo);
            atomic_fetch_add(&s->scartati, 1);
        }
        s->ultimo = p;
        pthread_mutex_unlock(&s->m);
        if (!atomic_exchange(&s->pendente, true))
            g_idle_add(consegna, s);
    }
}

/* Sorgente registrata (Annex B): pacchetti ricavati una volta, poi riletti in
 * ciclo a 60 fotogrammi al secondo. Si assume una fetta per fotogramma. */
typedef struct {
    bool config;
    uint8_t *dati;
    size_t n;
} Pacchetto;
static Pacchetto *g_pacchetti = NULL;
static size_t g_n_pacchetti = 0;

static bool carica_file(const char *nome)
{
    FILE *fp = fopen(nome, "rb");
    if (fp == NULL)
        return false;
    fseek(fp, 0, SEEK_END);
    long lung = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *b = lung > 0 ? malloc((size_t)lung) : NULL;
    if (b == NULL || fread(b, 1, (size_t)lung, fp) != (size_t)lung) {
        free(b);
        fclose(fp);
        return false;
    }
    fclose(fp);
    size_t cap = 0, n = (size_t)lung;
    uint8_t *conf = NULL;
    size_t nconf = 0;
    for (size_t i = 0; i + 3 < n;) {
        if (!(b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1)) {
            i++;
            continue;
        }
        size_t ini = i;
        if (ini > 0 && b[ini - 1] == 0)
            ini--;
        size_t j = i + 3;
        while (j + 2 < n && !(b[j] == 0 && b[j + 1] == 0 && b[j + 2] == 1))
            j++;
        size_t fine = j + 2 < n ? j : n;
        if (fine > ini && j + 2 < n && b[j - 1] == 0)
            fine = j - 1;
        int tipo = b[i + 3] & 0x1f;
        if (tipo == 7 || tipo == 8) {
            uint8_t *nc = realloc(conf, nconf + (fine - ini));
            if (nc == NULL)
                return false;
            conf = nc;
            memcpy(conf + nconf, b + ini, fine - ini);
            nconf += fine - ini;
        } else if (tipo == 1 || tipo == 5) {
            if (nconf > 0) {
                if (g_n_pacchetti == cap) {
                    cap = cap ? cap * 2 : 64;
                    g_pacchetti = realloc(g_pacchetti, cap * sizeof *g_pacchetti);
                    if (g_pacchetti == NULL)
                        return false;
                }
                g_pacchetti[g_n_pacchetti++] = (Pacchetto){true, conf, nconf};
                conf = NULL;
                nconf = 0;
            }
            if (g_n_pacchetti == cap) {
                cap = cap ? cap * 2 : 64;
                g_pacchetti = realloc(g_pacchetti, cap * sizeof *g_pacchetti);
                if (g_pacchetti == NULL)
                    return false;
            }
            uint8_t *c = malloc(fine - ini);
            if (c == NULL)
                return false;
            memcpy(c, b + ini, fine - ini);
            g_pacchetti[g_n_pacchetti++] = (Pacchetto){false, c, fine - ini};
        }
        i = j;
    }
    free(conf);
    free(b);
    return g_n_pacchetti > 0;
}

static void *lavoro_file(void *arg)
{
    Flusso *s = arg;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    uint64_t tempo = 0;
    for (size_t k = 0; !atomic_load(&g_ferma); k = (k + 1) % g_n_pacchetti) {
        const Pacchetto *p = &g_pacchetti[k];
        if (!p->config) {
            t.tv_nsec += 16666667L;
            if (t.tv_nsec >= 1000000000L) {
                t.tv_nsec -= 1000000000L;
                t.tv_sec++;
            }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
            tempo += 16667;
        }
        tratta(s, p->config, p->dati, p->n, tempo);
    }
    return NULL;
}

static void *lavoro(void *arg)
{
    Flusso *s = arg;
    PhErrore e;
    while (!atomic_load(&g_ferma)) {
        PhVideoPacchetto pk;
        int r = ph_video_leggi(s->flusso, &pk, 200, &e);
        if (r == PH_CANALE_TEMPO)
            continue;
        if (r != 1) {
            if (!atomic_load(&g_ferma))
                fprintf(stderr, "flusso %d video finito: %s\n", s->indice + 1,
                        r == 0 ? "chiuso dal telefono" : e.testo);
            break;
        }
        if (pk.larghezza > 0 && pk.altezza > 0) {
            atomic_store(&s->vw, pk.larghezza);
            atomic_store(&s->vh, pk.altezza);
        }
        tratta(s, pk.configurazione, pk.dati, pk.lunghezza, pk.tempo_us);
    }
    return NULL;
}

/* ---- Consegna al thread GTK -----------------------------------------------*/
static void rilascia_drm(gpointer d)
{
    AVFrame *f = d;
    av_frame_free(&f);
}

static GdkTexture *texture_da_drm(Flusso *s, Pronto *p, GError **err)
{
    const AVDRMFrameDescriptor *d = (const AVDRMFrameDescriptor *)p->f->data[0];
    int fd[4], offset[4], passo[4], n = 0;
    uint64_t mod = 0;
    for (int l = 0; l < d->nb_layers; l++) {
        for (int k = 0; k < d->layers[l].nb_planes; k++) {
            const AVDRMPlaneDescriptor *pd = &d->layers[l].planes[k];
            if (n == 4 || pd->object_index < 0 || pd->object_index >= d->nb_objects) {
                g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "descrittore DRM non gestito");
                return NULL;
            }
            uint64_t m = d->objects[pd->object_index].format_modifier;
            if (n == 0)
                mod = m;
            else if (m != mod) {
                g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "piani con modificatori diversi");
                return NULL;
            }
            fd[n] = d->objects[pd->object_index].fd;
            offset[n] = (int)pd->offset;
            passo[n] = (int)pd->pitch;
            n++;
        }
    }
    if (n == 0) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "nessun piano");
        return NULL;
    }
    /* Con i piani su livelli separati (R8 + GR88) il formato complessivo si
     * ricava dal formato dei pixel; con un livello solo e' quello del livello. */
    uint32_t fourcc = d->nb_layers == 1 ? d->layers[0].format
                    : p->sw == AV_PIX_FMT_P010 ? FOURCC_P010 : FOURCC_NV12;
    GdkDisplay *disp = gdk_display_get_default();
    GdkDmabufFormats *fm = gdk_display_get_dmabuf_formats(disp);
    if (!s->errore_noto && !gdk_dmabuf_formats_contains(fm, fourcc, mod))
        fprintf(stderr, "flusso %d: GTK non elenca fourcc %.4s modificatore 0x%llx tra i formati importabili\n",
                s->indice + 1, (const char *)&fourcc, (unsigned long long)mod);
    GdkDmabufTextureBuilder *b = gdk_dmabuf_texture_builder_new();
    gdk_dmabuf_texture_builder_set_display(b, disp);
    gdk_dmabuf_texture_builder_set_width(b, (unsigned)p->w);
    gdk_dmabuf_texture_builder_set_height(b, (unsigned)p->h);
    gdk_dmabuf_texture_builder_set_fourcc(b, fourcc);
    gdk_dmabuf_texture_builder_set_modifier(b, mod);
    gdk_dmabuf_texture_builder_set_n_planes(b, (unsigned)n);
    for (int i = 0; i < n; i++) {
        gdk_dmabuf_texture_builder_set_fd(b, (unsigned)i, fd[i]);
        gdk_dmabuf_texture_builder_set_offset(b, (unsigned)i, (unsigned)offset[i]);
        gdk_dmabuf_texture_builder_set_stride(b, (unsigned)i, (unsigned)passo[i]);
    }
    GdkTexture *t = gdk_dmabuf_texture_builder_build(b, rilascia_drm, p->f, err);
    g_object_unref(b);
    /* Se la costruzione fallisce non e' documentato se GTK chiami la funzione
     * di rilascio: per non rischiare un doppio rilascio il fotogramma non si
     * libera piu' qui (al peggio si perde un fotogramma, una volta sola: dopo
     * il primo rifiuto si passa alla copia). */
    p->f = NULL;
    return t;
}

static gboolean consegna(gpointer dati)
{
    Flusso *s = dati;
    pthread_mutex_lock(&s->m);
    Pronto *p = s->ultimo;
    s->ultimo = NULL;
    atomic_store(&s->pendente, false);
    pthread_mutex_unlock(&s->m);
    if (p == NULL)
        return G_SOURCE_REMOVE;
    GdkTexture *t = NULL;
    if (p->drm) {
        GError *err = NULL;
        t = texture_da_drm(s, p, &err);
        if (t != NULL) {
            atomic_fetch_add(&s->dmabuf_ok, 1);
        } else {
            atomic_fetch_add(&s->dmabuf_ko, 1);
            atomic_store(&s->dmabuf_rifiutato, true);
            if (!s->errore_noto) {
                s->errore_noto = true;
                fprintf(stderr, "flusso %d: GTK rifiuta il DMA-BUF: %s; da ora si copia\n",
                        s->indice + 1, err ? err->message : "?");
            }
        }
        g_clear_error(&err);
    } else {
        GBytes *bytes = g_bytes_new_with_free_func(p->rgb, (gsize)p->stride * (gsize)p->h, free, p->rgb);
        p->rgb = NULL;
        t = gdk_memory_texture_new(p->w, p->h, GDK_MEMORY_R8G8B8, bytes, (gsize)p->stride);
        g_bytes_unref(bytes);
    }
    if (t != NULL) {
        gtk_picture_set_paintable(GTK_PICTURE(s->immagine), GDK_PAINTABLE(t));
        g_object_unref(t);
        atomic_fetch_add(&s->mostrati, 1);
    }
    libera_pronto(p);
    return G_SOURCE_REMOVE;
}

/* ---- Interazione ----------------------------------------------------------*/
enum { PARTE_TITOLO = 1, PARTE_MANIGLIA = 2, PARTE_CONTENUTO = 3 };

typedef struct {
    Flusso *s;
    int parte;
    double ix, iy;     /* punto di inizio trascinamento, nel contenitore */
    bool premuto;
} Presa;

static Presa g_presa;
static double g_mouse_x = 0, g_mouse_y = 0;

static void in_primo_piano(Flusso *s)
{
    gtk_widget_insert_before(s->box, g_scrivania, NULL);
}

/* Trova finestra interna e parte sotto il punto (coordinate del contenitore). */
static bool trova(double x, double y, Flusso **s, int *parte)
{
    GtkWidget *w = gtk_widget_pick(g_scrivania, x, y, GTK_PICK_DEFAULT);
    *s = NULL;
    *parte = 0;
    for (; w != NULL && w != g_scrivania; w = gtk_widget_get_parent(w)) {
        if (*parte == 0)
            *parte = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "parte"));
        Flusso *f = g_object_get_data(G_OBJECT(w), "fin");
        if (f != NULL) {
            *s = f;
            return *parte != 0;
        }
    }
    return false;
}

/* Dal contenitore alle coordinate del video. */
static bool a_video(Flusso *s, double x, double y, uint16_t *vx, uint16_t *vy, uint16_t *vw,
                    uint16_t *vh, bool *dentro)
{
    int w = atomic_load(&s->vw), h = atomic_load(&s->vh);
    graphene_point_t in = {(float)x, (float)y}, out;
    if (w <= 0 || h <= 0 || !gtk_widget_compute_point(g_scrivania, s->offload, &in, &out))
        return false;
    double lw = gtk_widget_get_width(s->offload), lh = gtk_widget_get_height(s->offload);
    if (lw <= 0 || lh <= 0)
        return false;
    double fx = (double)out.x * w / lw, fy = (double)out.y * h / lh;
    *dentro = fx >= 0 && fy >= 0 && fx < w && fy < h;
    fx = fx < 0 ? 0 : fx > w - 1 ? w - 1 : fx;
    fy = fy < 0 ? 0 : fy > h - 1 ? h - 1 : fy;
    *vx = (uint16_t)fx;
    *vy = (uint16_t)fy;
    *vw = (uint16_t)w;
    *vh = (uint16_t)h;
    return true;
}

static void tocco_contenuto(Flusso *s, uint8_t azione, double x, double y)
{
    uint16_t vx, vy, vw, vh;
    bool dentro;
    if (g_comp != NULL && a_video(s, x, y, &vx, &vy, &vw, &vh, &dentro))
        ph_input_tocco(g_comp, atomic_load(&s->id), vw, vh, azione, vx, vy, azione == PH_TOCCO_SU ? 0 : 1, NULL);
}

static void trascina_inizio(GtkGestureDrag *g, double x, double y, gpointer dati)
{
    (void)g; (void)dati;
    Flusso *s;
    int parte;
    memset(&g_presa, 0, sizeof g_presa);
    if (!trova(x, y, &s, &parte))
        return;
    g_presa.s = s;
    g_presa.parte = parte;
    g_presa.ix = x;
    g_presa.iy = y;
    g_presa.premuto = true;
    s->x0 = s->x; s->y0 = s->y; s->w0 = s->w; s->h0 = s->h;
    in_primo_piano(s);
    if (parte == PARTE_CONTENUTO)
        tocco_contenuto(s, PH_TOCCO_GIU, x, y);
}

static void trascina_passo(GtkGestureDrag *g, double ox, double oy, gpointer dati)
{
    (void)g; (void)dati;
    Flusso *s = g_presa.s;
    if (!g_presa.premuto || s == NULL)
        return;
    if (g_presa.parte == PARTE_TITOLO) {
        s->x = s->x0 + ox;
        s->y = s->y0 + oy;
        gtk_widget_queue_allocate(g_scrivania);
    } else if (g_presa.parte == PARTE_MANIGLIA) {
        s->w = s->w0 + ox > 140 ? s->w0 + ox : 140;
        s->h = s->h0 + oy > 140 ? s->h0 + oy : 140;
        gtk_widget_queue_allocate(g_scrivania);
    } else {
        tocco_contenuto(s, PH_TOCCO_MUOVI, g_presa.ix + ox, g_presa.iy + oy);
    }
}

static void trascina_fine(GtkGestureDrag *g, double ox, double oy, gpointer dati)
{
    (void)g; (void)dati;
    if (g_presa.premuto && g_presa.s != NULL && g_presa.parte == PARTE_CONTENUTO)
        tocco_contenuto(g_presa.s, PH_TOCCO_SU, g_presa.ix + ox, g_presa.iy + oy);
    g_presa.premuto = false;
}

static void mouse_mosso(GtkEventControllerMotion *c, double x, double y, gpointer dati)
{
    (void)c; (void)dati;
    g_mouse_x = x;
    g_mouse_y = y;
}

static gboolean rotella(GtkEventControllerScroll *c, double dx, double dy, gpointer dati)
{
    (void)c; (void)dati;
    Flusso *s;
    int parte;
    if (!trova(g_mouse_x, g_mouse_y, &s, &parte) || parte != PARTE_CONTENUTO)
        return FALSE;
    uint16_t vx, vy, vw, vh;
    bool dentro;
    if (g_comp != NULL && a_video(s, g_mouse_x, g_mouse_y, &vx, &vy, &vw, &vh, &dentro) && dentro)
        ph_input_rotella(g_comp, atomic_load(&s->id), vw, vh, vx, vy, (int16_t)(dx * 120.0),
                         (int16_t)(-dy * 120.0), NULL);
    return TRUE;
}

static gboolean tasto(GtkEventControllerKey *c, guint keyval, guint code, GdkModifierType st,
                      gpointer dati)
{
    (void)c; (void)code; (void)st; (void)dati;
    if (keyval == GDK_KEY_F11) {
        if (gtk_window_is_fullscreen(GTK_WINDOW(g_finestra)))
            gtk_window_unfullscreen(GTK_WINDOW(g_finestra));
        else
            gtk_window_fullscreen(GTK_WINDOW(g_finestra));
        return TRUE;
    }
    return FALSE;
}

static gboolean chiusura(GtkWindow *w, gpointer dati)
{
    (void)w; (void)dati;
    g_main_loop_quit(g_ciclo);
    return TRUE;
}

/* ---- Giostra --------------------------------------------------------------*/
static gboolean giro(GtkWidget *w, GdkFrameClock *clock, gpointer dati)
{
    (void)dati;
    static gint64 t0 = 0;
    gint64 ft = gdk_frame_clock_get_frame_time(clock);
    if (t0 == 0)
        t0 = ft;
    double t = (double)(ft - t0) / 1e6;
    if (t >= g_giostra) {
        g_main_loop_quit(g_ciclo);
        return G_SOURCE_REMOVE;
    }
    double dw = gtk_widget_get_width(w);
    for (int i = 0; i < g_n; i++) {
        Flusso *s = &g_fl[i];
        double fase = i * 2.1;
        double larghezza = 280.0 + 90.0 * sin(0.8 * t + fase);
        s->w = larghezza;
        s->h = larghezza * g_rapporto + ALTEZZA_TITOLO + ALTEZZA_BORDINO;
        double centro = dw * (i + 1) / (g_n + 1);
        double ampiezza = g_n > 1 ? dw / (2.0 * g_n) - 20 : dw / 4;
        s->x = centro - s->w / 2 + ampiezza * sin(0.6 * t + fase);
        s->y = 20 + 30 * (1 + sin(0.9 * t + fase));
    }
    gtk_widget_queue_allocate(w);
    return G_SOURCE_CONTINUE;
}

/* Scorrimento continuo su tutte le sessioni, come --movimento di misura-flussi. */
static void *thread_scorrimento(void *arg)
{
    (void)arg;
    unsigned gesto = 0;
    while (!atomic_load(&g_ferma)) {
        for (int k = 0; k <= PASSI_SCORRIMENTO + 1 && !atomic_load(&g_ferma); k++) {
            uint8_t az = k == 0 ? PH_TOCCO_GIU : k > PASSI_SCORRIMENTO ? PH_TOCCO_SU : PH_TOCCO_MUOVI;
            int kk = k > PASSI_SCORRIMENTO ? PASSI_SCORRIMENTO : k;
            if (k > 0)
                dormi_ms(PASSO_MOVIMENTO_MS);
            for (int i = 0; i < g_n; i++) {
                Flusso *s = &g_fl[i];
                int w = atomic_load(&s->vw), h = atomic_load(&s->vh);
                if (w <= 0 || h <= 0)
                    continue;
                int da = (gesto & 1) ? h / 4 : h * 3 / 4, fino = (gesto & 1) ? h * 3 / 4 : h / 4;
                int y = da + (fino - da) * kk / PASSI_SCORRIMENTO;
                ph_input_tocco(g_comp, atomic_load(&s->id), (uint16_t)w, (uint16_t)h, az,
                               (uint16_t)(w / 2), (uint16_t)y, 0, NULL);
            }
        }
        gesto++;
    }
    return NULL;
}

/* ---- Costruzione della finestra interna -----------------------------------*/
static GtkWidget *crea_parte(const char *classe, int parte)
{
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(b, classe);
    g_object_set_data(G_OBJECT(b), "parte", GINT_TO_POINTER(parte));
    return b;
}

static void crea_finestra_interna(Flusso *s)
{
    s->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(s->box, "finestra");
    g_object_set_data(G_OBJECT(s->box), "fin", s);
    GtkWidget *titolo = crea_parte("titolo", PARTE_TITOLO);
    gtk_widget_set_size_request(titolo, -1, ALTEZZA_TITOLO);
    GtkWidget *et = gtk_label_new(s->pacchetto);
    gtk_label_set_xalign(GTK_LABEL(et), 0);
    gtk_label_set_ellipsize(GTK_LABEL(et), PANGO_ELLIPSIZE_END);
    gtk_widget_set_margin_start(et, 8);
    gtk_widget_set_hexpand(et, TRUE);
    gtk_box_append(GTK_BOX(titolo), et);
    s->immagine = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(s->immagine), GTK_CONTENT_FIT_FILL);
    gtk_picture_set_can_shrink(GTK_PICTURE(s->immagine), TRUE);
    s->offload = gtk_graphics_offload_new(s->immagine);
    gtk_graphics_offload_set_enabled(GTK_GRAPHICS_OFFLOAD(s->offload),
                                     g_copia ? GTK_GRAPHICS_OFFLOAD_DISABLED
                                             : GTK_GRAPHICS_OFFLOAD_ENABLED);
    gtk_graphics_offload_set_black_background(GTK_GRAPHICS_OFFLOAD(s->offload), TRUE);
    gtk_widget_set_hexpand(s->offload, TRUE);
    gtk_widget_set_vexpand(s->offload, TRUE);
    g_object_set_data(G_OBJECT(s->offload), "parte", GINT_TO_POINTER(PARTE_CONTENUTO));
    GtkWidget *bordino = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(bordino, -1, ALTEZZA_BORDINO);
    GtkWidget *maniglia = crea_parte("maniglia", PARTE_MANIGLIA);
    gtk_widget_set_size_request(maniglia, 18, ALTEZZA_BORDINO);
    gtk_widget_set_hexpand(maniglia, FALSE);
    GtkWidget *molla = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(molla, TRUE);
    gtk_box_append(GTK_BOX(bordino), molla);
    gtk_box_append(GTK_BOX(bordino), maniglia);
    gtk_box_append(GTK_BOX(s->box), titolo);
    gtk_box_append(GTK_BOX(s->box), s->offload);
    gtk_box_append(GTK_BOX(s->box), bordino);
    gtk_widget_set_parent(s->box, g_scrivania);
}

static void alla_realizzazione(GtkWidget *w, gpointer dati)
{
    (void)dati;
    GskRenderer *r = gtk_native_get_renderer(GTK_NATIVE(w));
    snprintf(g_motore, sizeof g_motore, "%s", r ? G_OBJECT_TYPE_NAME(r) : "nessuno");
    g_motore_vulkan = r != NULL && strstr(g_motore, "Vulkan") != NULL;
    fprintf(stderr, "disegno: motore GTK %s%s\n", g_motore, g_motore_vulkan ? "" : " (NON Vulkan)");
    GdkDmabufFormats *fm = gdk_display_get_dmabuf_formats(gtk_widget_get_display(w));
    gsize n = gdk_dmabuf_formats_get_n_formats(fm), nv12 = 0;
    for (gsize i = 0; i < n; i++) {
        guint32 fc;
        guint64 mod;
        gdk_dmabuf_formats_get_format(fm, i, &fc, &mod);
        if (fc == FOURCC_NV12)
            nv12++;
    }
    fprintf(stderr, "GTK importa %zu coppie formato/modificatore dmabuf, di cui %zu NV12\n", (size_t)n,
            (size_t)nv12);
    GdkFrameClock *fc = gtk_widget_get_frame_clock(w);
    if (fc != NULL)
        g_signal_connect(fc, "after-paint", G_CALLBACK(dopo_disegno), NULL);
}

static void evento(uint32_t id, uint8_t che_cosa, uint16_t a, uint16_t b, void *utente)
{
    (void)utente;
    static const char *nomi[] = {"?", "l'app si e' chiusa", "schermo protetto", "misure cambiate",
                                 "errore del codificatore"};
    fprintf(stderr, "evento dal telefono (sessione %u): %s (%u, %u)\n", (unsigned)id,
            che_cosa < 5 ? nomi[che_cosa] : "?", (unsigned)a, (unsigned)b);
}

static PhAdb *collega(const char *telefono, const PhChiave *chiave, PhErrore *e)
{
    char host[64];
    unsigned porta = 0;
    if (telefono != NULL) {
        const char *due = strrchr(telefono, ':');
        if (due == NULL || (size_t)(due - telefono) >= sizeof host || sscanf(due + 1, "%u", &porta) != 1) {
            ph_errore(e, "telefono non valido: serve HOST:PORTA");
            return NULL;
        }
        memcpy(host, telefono, (size_t)(due - telefono));
        host[due - telefono] = '\0';
    } else {
        PhTelefonoTrovato t[16];
        int n = ph_scoperta_cerca(5000, t, 16, e);
        if (n < 0)
            return NULL;
        int scelto = -1;
        for (int i = 0; i < n && scelto < 0; i++)
            if (!t[i].abbinamento)
                scelto = i;
        if (scelto < 0) {
            ph_errore(e, "nessun telefono trovato in rete (debug wireless acceso?)");
            return NULL;
        }
        snprintf(host, sizeof host, "%s", t[scelto].indirizzo);
        porta = t[scelto].porta;
    }
    bool non_abbinato = false;
    PhAdb *a = ph_adb_collega(host, (uint16_t)porta, chiave, 10000, &non_abbinato, e);
    if (a == NULL && non_abbinato)
        ph_errore(e, "questo PC non e' abbinato al telefono");
    return a;
}

static const char *stile =
    ".finestra { background: #14161c; border: 1px solid #5b6275; }"
    ".titolo { background: #2b3040; color: #e8eaf0; font-weight: bold; }"
    ".maniglia { background: #8a93a8; }";

int main(int argc, char **argv)
{
    const char *chiave_file = NULL, *telefono = NULL;
    PhVia via = PH_VIA_AUTOMATICA;
    int i = 1;
    for (; i < argc && strncmp(argv[i], "--", 2) == 0; i++) {
        if (strcmp(argv[i], "--copia") == 0) {
            g_copia = true;
            continue;
        }
        if (i + 1 >= argc)
            break;
        if (strcmp(argv[i], "--chiave") == 0) chiave_file = argv[++i];
        else if (strcmp(argv[i], "--telefono") == 0) telefono = argv[++i];
        else if (strcmp(argv[i], "--file") == 0) g_file = argv[++i];
        else if (strcmp(argv[i], "--giostra") == 0) g_giostra = atof(argv[++i]);
        else if (strcmp(argv[i], "--via") == 0) {
            const char *v = argv[++i];
            via = strcmp(v, "vulkan") == 0 ? PH_VIA_VULKAN : strcmp(v, "vaapi") == 0 ? PH_VIA_VAAPI
                : strcmp(v, "software") == 0 ? PH_VIA_SOFTWARE : PH_VIA_AUTOMATICA;
        } else break;
    }
    if (g_file != NULL ? (argc - i < 1 || argc - i > MAX_FLUSSI)
                       : (argc - i < 2 || argc - i - 1 > MAX_FLUSSI)) {
        fprintf(stderr, "uso: %s [--chiave FILE] [--telefono HOST:PORTA] [--via V] [--copia] "
                        "[--giostra SECONDI] JAR PACCHETTO [PACCHETTO...]\n", argv[0]);
        return 2;
    }
    const char *jar = g_file != NULL ? NULL : argv[i];
    int primo_nome = g_file != NULL ? i : i + 1;
    g_n = argc - primo_nome;
    char chiave_predefinita[512];
    if (chiave_file == NULL) {
        const char *casa = getenv("HOME");
        snprintf(chiave_predefinita, sizeof chiave_predefinita, "%s/.config/YASDE/chiave.pem",
                 casa ? casa : ".");
        chiave_file = chiave_predefinita;
    }

    int esito = 1;
    PhErrore e = {{0}};
    PhChiave *chiave = NULL;
    PhAdb *adb = NULL;
    pthread_t th_scorr;
    bool scorr_avviato = false;
    int aperti = 0;
    for (int k = 0; k < g_n; k++) {
        g_fl[k].pacchetto = argv[primo_nome + k];
        g_fl[k].indice = k;
        pthread_mutex_init(&g_fl[k].m, NULL);
    }

    if (!gtk_init_check()) {
        fprintf(stderr, "errore: GTK non riesce ad aprire lo schermo\n");
        return 1;
    }
    PhTelefonoInfo info;
    memset(&info, 0, sizeof info);
    if (g_file != NULL) {
        if (!carica_file(g_file)) {
            fprintf(stderr, "errore: file %s non leggibile o senza fotogrammi\n", g_file);
            return 1;
        }
        fprintf(stderr, "sorgente registrata: %s (%zu pacchetti), nessun telefono\n", g_file, g_n_pacchetti);
    } else {
        chiave = ph_chiave_carica_o_crea(chiave_file, &e);
        if (chiave == NULL) goto errore;
        adb = collega(telefono, chiave, &e);
        if (adb == NULL) goto errore;
        g_comp = ph_componente_avvia(adb, jar, evento, NULL, &info, 60000, &e);
        if (g_comp == NULL) goto errore;
        fprintf(stderr, "telefono: %s, Android API %u, schermo %ux%u a %u dpi\n", info.modello,
                (unsigned)info.api, (unsigned)info.larghezza, (unsigned)info.altezza,
                (unsigned)info.densita);
        g_rapporto = info.larghezza > 0 ? (double)info.altezza / (double)info.larghezza : 2.0;
    }
    unsigned ra = ALTEZZA_RICHIESTA, rl = ((unsigned)((double)ra / g_rapporto) / 8) * 8;
    if (rl < 64) rl = 64;

    for (int k = 0; k < g_n; k++) {
        Flusso *s = &g_fl[k];
        s->dec = ph_decodifica_crea(via, &e);
        if (s->dec == NULL) goto errore;
        if (k == 0)
            fprintf(stderr, "decodifica: via %s\n", ph_via_nome(ph_decodifica_via(s->dec)));
        PhVideoRichiesta r = {.larghezza = (uint16_t)rl, .altezza = (uint16_t)ra,
                              .densita = info.densita, .bit_al_secondo = 6000000, .fps = 60,
                              .pacchetto = s->pacchetto};
        if (g_file == NULL) {
            s->flusso = ph_video_apri(g_comp, &r, &e);
            if (s->flusso == NULL) goto errore;
            atomic_store(&s->id, ph_video_id(s->flusso));
            aperti++;
            if (k + 1 < g_n)
                dormi_ms(500);
        }
    }

    /* Interfaccia. */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, stile);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_finestra = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(g_finestra), "YASDE - prototipo GTK");
    gtk_window_set_default_size(GTK_WINDOW(g_finestra), 1280, 800);
    g_scrivania = g_object_new(scrivania_get_type(), NULL);
    gtk_widget_set_hexpand(g_scrivania, TRUE);
    gtk_widget_set_vexpand(g_scrivania, TRUE);
    gtk_window_set_child(GTK_WINDOW(g_finestra), g_scrivania);
    for (int k = 0; k < g_n; k++) {
        Flusso *s = &g_fl[k];
        crea_finestra_interna(s);
        s->w = 300;
        s->h = s->w * g_rapporto + ALTEZZA_TITOLO + ALTEZZA_BORDINO;
        s->x = 40 + k * 340;
        s->y = 30 + k * 20;
    }
    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(trascina_inizio), NULL);
    g_signal_connect(drag, "drag-update", G_CALLBACK(trascina_passo), NULL);
    g_signal_connect(drag, "drag-end", G_CALLBACK(trascina_fine), NULL);
    gtk_widget_add_controller(g_scrivania, GTK_EVENT_CONTROLLER(drag));
    GtkEventController *mov = gtk_event_controller_motion_new();
    g_signal_connect(mov, "motion", G_CALLBACK(mouse_mosso), NULL);
    gtk_widget_add_controller(g_scrivania, mov);
    GtkEventController *rot = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect(rot, "scroll", G_CALLBACK(rotella), NULL);
    gtk_widget_add_controller(g_scrivania, rot);
    GtkEventController *tas = gtk_event_controller_key_new();
    g_signal_connect(tas, "key-pressed", G_CALLBACK(tasto), NULL);
    gtk_widget_add_controller(g_finestra, tas);
    g_signal_connect(g_finestra, "close-request", G_CALLBACK(chiusura), NULL);
    g_signal_connect_after(g_finestra, "realize", G_CALLBACK(alla_realizzazione), NULL);
    if (g_giostra > 0)
        gtk_widget_add_tick_callback(g_scrivania, giro, NULL, NULL);

    for (int k = 0; k < g_n; k++) {
        if (pthread_create(&g_fl[k].th, NULL, g_file ? lavoro_file : lavoro, &g_fl[k]) != 0) {
            ph_errore(&e, "impossibile avviare il thread del video");
            goto errore;
        }
        g_fl[k].th_avviato = true;
    }
    if (g_giostra > 0 && g_comp != NULL && pthread_create(&th_scorr, NULL, thread_scorrimento, NULL) == 0)
        scorr_avviato = true;

    g_ciclo = g_main_loop_new(NULL, FALSE);
    g_istante_inizio = g_istante_rapporto = g_get_monotonic_time();
    g_timeout_add(2000, rapporto_periodico, NULL);
    gtk_window_present(GTK_WINDOW(g_finestra));
    g_main_loop_run(g_ciclo);
    riepilogo_finale();
    esito = 0;
    goto fine;

errore:
    fprintf(stderr, "errore: %s\n", e.testo);
fine:
    atomic_store(&g_ferma, true);
    if (scorr_avviato)
        pthread_join(th_scorr, NULL);
    for (int k = 0; k < g_n; k++)
        if (g_fl[k].th_avviato)
            pthread_join(g_fl[k].th, NULL);
    for (int k = 0; k < aperti; k++)
        if (g_fl[k].flusso != NULL)
            ph_video_chiudi(g_fl[k].flusso, true);
    if (g_comp != NULL)
        ph_componente_chiudi(g_comp);
    if (adb != NULL)
        ph_adb_chiudi(adb);
    ph_chiave_libera(chiave);
    for (int k = 0; k < g_n; k++) {
        ph_decodifica_libera(g_fl[k].dec);
        libera_pronto(g_fl[k].ultimo);
    }
    /* I widget non si distruggono: il processo finisce subito. */
    return esito;
}
