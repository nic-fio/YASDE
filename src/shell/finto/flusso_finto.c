/* Flussi video finti (finto.h): fotogrammi sintetici a 60 al secondo,
 * generati sul thread di lettura della superficie dentro
 * ph_srv_flusso_leggi (come la decodifica vera). In memoria o in DMA-BUF
 * (memfd + /dev/udmabuf, lineare XRGB8888) per provare la via senza copie.
 * I buffer si riusano (al massimo 4 per flusso): un fotogramma torna libero
 * quando chi lo mostra lo rilascia. */
#include <cairo.h>
#include <errno.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <linux/udmabuf.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "interno.h"
#include "shell/shell.h"

#define FOURCC(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#define DRM_XRGB8888 FOURCC('X', 'R', '2', '4')
#define BUFFER_MAX 4
#define PERIODO_US 16667

/* ---- Buffer riusabili ---------------------------------------------------- */
typedef struct Pool Pool;

typedef struct {
    Pool *pool;
    uint8_t *mem;
    size_t dim;
    int w, h;
    size_t passo;
    int memfd, dmafd; /* -1 in memoria */
    bool libero;
} Buf;

struct Pool {
    atomic_int rif;
    GMutex m;
    GPtrArray *buf; /* di Buf* */
    bool chiuso;
    bool dmabuf_rotto;
};

static void buf_distruggi(Buf *b)
{
    if (b->dmafd >= 0) {
        munmap(b->mem, b->dim);
        close(b->dmafd);
        close(b->memfd);
    } else {
        g_free(b->mem);
    }
    g_free(b);
}

static void pool_lascia(Pool *p)
{
    if (atomic_fetch_sub(&p->rif, 1) != 1)
        return;
    for (guint i = 0; i < p->buf->len; i++)
        buf_distruggi(g_ptr_array_index(p->buf, i));
    g_ptr_array_unref(p->buf);
    g_mutex_clear(&p->m);
    g_free(p);
}

static Buf *buf_dmabuf(int w, int h)
{
    size_t passo = (size_t)w * 4, dim = passo * (size_t)h;
    long pagina = sysconf(_SC_PAGESIZE);
    dim = (dim + (size_t)pagina - 1) / (size_t)pagina * (size_t)pagina;
    int dev = open("/dev/udmabuf", O_RDWR | O_CLOEXEC);
    if (dev < 0)
        return NULL;
    int memfd = memfd_create("ph-finto", MFD_ALLOW_SEALING | MFD_CLOEXEC);
    if (memfd < 0 || ftruncate(memfd, (off_t)dim) != 0 || fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK) != 0) {
        if (memfd >= 0)
            close(memfd);
        close(dev);
        return NULL;
    }
    struct udmabuf_create c = {.memfd = (uint32_t)memfd, .flags = UDMABUF_FLAGS_CLOEXEC, .offset = 0, .size = dim};
    int dmafd = ioctl(dev, UDMABUF_CREATE, &c);
    close(dev);
    if (dmafd < 0) {
        close(memfd);
        return NULL;
    }
    void *mem = mmap(NULL, dim, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    if (mem == MAP_FAILED) {
        close(dmafd);
        close(memfd);
        return NULL;
    }
    Buf *b = g_new0(Buf, 1);
    b->mem = mem;
    b->dim = dim;
    b->w = w;
    b->h = h;
    b->passo = passo;
    b->memfd = memfd;
    b->dmafd = dmafd;
    return b;
}

static Buf *buf_memoria(int w, int h)
{
    Buf *b = g_new0(Buf, 1);
    b->passo = (size_t)w * 4;
    b->dim = b->passo * (size_t)h;
    b->mem = g_try_malloc(b->dim);
    if (b->mem == NULL) {
        g_free(b);
        return NULL;
    }
    b->w = w;
    b->h = h;
    b->memfd = b->dmafd = -1;
    return b;
}

/* Un buffer libero della misura giusta, o uno nuovo; NULL se sono tutti in
 * uso (chi mostra non ha ancora rilasciato: si salta un fotogramma). */
static Buf *pool_prendi(Pool *p, int w, int h, bool dmabuf)
{
    g_mutex_lock(&p->m);
    Buf *trovato = NULL;
    for (guint i = 0; i < p->buf->len;) {
        Buf *b = g_ptr_array_index(p->buf, i);
        bool tipo_ok = (b->dmafd >= 0) == (dmabuf && !p->dmabuf_rotto);
        if (b->libero && (b->w != w || b->h != h || !tipo_ok)) {
            g_ptr_array_remove_index_fast(p->buf, i);
            buf_distruggi(b);
            continue;
        }
        if (b->libero && trovato == NULL)
            trovato = b;
        i++;
    }
    if (trovato == NULL && p->buf->len < BUFFER_MAX) {
        if (dmabuf && !p->dmabuf_rotto) {
            trovato = buf_dmabuf(w, h);
            if (trovato == NULL) {
                p->dmabuf_rotto = true;
                ph_shell_decisione("flusso finto in memoria invece che in DMA-BUF",
                                   "/dev/udmabuf non utilizzabile (errno %d)", errno);
            }
        }
        if (trovato == NULL)
            trovato = buf_memoria(w, h);
        if (trovato != NULL) {
            trovato->pool = p;
            g_ptr_array_add(p->buf, trovato);
        }
    }
    if (trovato != NULL) {
        trovato->libero = false;
        atomic_fetch_add(&p->rif, 1);
    }
    g_mutex_unlock(&p->m);
    return trovato;
}

static void buf_rilascia(void *dati)
{
    Buf *b = dati;
    Pool *p = b->pool;
    g_mutex_lock(&p->m);
    b->libero = true;
    g_mutex_unlock(&p->m);
    pool_lascia(p);
}

/* ---- Disegno --------------------------------------------------------------- */
static inline uint32_t px(uint32_t rgb) { return 0xff000000u | rgb; }

static void rettangolo(Buf *b, int x, int y, int w, int h, uint32_t c)
{
    int x0 = MAX(x, 0), y0 = MAX(y, 0), x1 = MIN(x + w, b->w), y1 = MIN(y + h, b->h);
    for (int j = y0; j < y1; j++) {
        uint32_t *r = (uint32_t *)(b->mem + (size_t)j * b->passo);
        for (int i = x0; i < x1; i++)
            r[i] = c;
    }
}

/* Cifra a sette segmenti in un riquadro s x 2s. */
static void cifra(Buf *b, int x, int y, int s, int d, uint32_t c)
{
    static const uint8_t seg[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    int t = MAX(s / 5, 2);
    uint8_t m = seg[d % 10];
    if (m & 0x01) rettangolo(b, x, y, s, t, c);
    if (m & 0x02) rettangolo(b, x + s - t, y, t, s, c);
    if (m & 0x04) rettangolo(b, x + s - t, y + s, t, s, c);
    if (m & 0x08) rettangolo(b, x, y + 2 * s - t, s, t, c);
    if (m & 0x10) rettangolo(b, x, y + s, t, s, c);
    if (m & 0x20) rettangolo(b, x, y, t, s, c);
    if (m & 0x40) rettangolo(b, x, y + s - t / 2, s, t, c);
}

static void numero(Buf *b, int x, int y, int s, uint64_t n, uint32_t c)
{
    char t[24];
    int k = snprintf(t, sizeof t, "%llu", (unsigned long long)n);
    for (int i = 0; i < k; i++)
        cifra(b, x + i * (s + s / 2), y, s, t[i] - '0', c);
}

typedef struct {
    int w, h;
    uint64_t n;
    bool protetto, specchio, tocco;
    int tx, ty;
    uint32_t colore;
    int densita; /* dpi dello schermo virtuale, disegnati in basso (giallo) */
    bool aspetto_app; /* immagine neutra "da app" invece di barre e cifre (PhFintoOpzioni) */
} Scena;

static void disegna(Buf *b, const Scena *sc)
{
    static const uint32_t barre[8] = {0xc0c0c0, 0xc0c000, 0x00c0c0, 0x00c000, 0xc000c0, 0xc00000, 0x0000c0, 0x101010};
    if (sc->protetto) {
        rettangolo(b, 0, 0, b->w, b->h, px(0));
        return;
    }
    if (sc->aspetto_app) {
        /* Immagine neutra, a soli rettangoli: fascia del titolo, righe segnaposto
         * (quadrato + due linee) e un pulsante d'azione. Per le schermate del manuale. */
        rettangolo(b, 0, 0, b->w, b->h, px(0xf4f5f7));
        int fascia = MAX(b->h * 9 / 100, 4);
        rettangolo(b, 0, 0, b->w, fascia, px(sc->colore));
        rettangolo(b, b->w / 16, fascia / 3, b->w * 2 / 5, MAX(fascia / 3, 2), px(0xffffff));
        int riga_h = MAX(b->h * 10 / 100, 6), y = fascia + riga_h / 2;
        for (int k = 0; y + riga_h <= b->h * 90 / 100; k++, y += riga_h + riga_h / 3) {
            rettangolo(b, b->w / 16, y, b->w * 88 / 100, riga_h, px(0xffffff));
            rettangolo(b, b->w / 16 + riga_h / 5, y + riga_h / 5, riga_h * 3 / 5, riga_h * 3 / 5, px(0xd4d8e0));
            int lx = b->w / 16 + riga_h, lw = b->w * (k % 2 ? 40 : 55) / 100;
            rettangolo(b, lx, y + riga_h / 4, lw, MAX(riga_h / 6, 2), px(0x8a93a3));
            rettangolo(b, lx, y + riga_h * 5 / 8, lw * 3 / 5, MAX(riga_h / 8, 2), px(0xc3c9d4));
        }
        int q = MAX(b->h / 10, 8);
        rettangolo(b, b->w - q - b->w / 16, b->h - q - b->h / 24, q, q, px(sc->colore));
        return;
    }
    int alto = b->h * 6 / 100, meta = b->h * 60 / 100;
    rettangolo(b, 0, 0, b->w, alto, px(sc->colore));
    uint32_t *riga = (uint32_t *)(b->mem + (size_t)alto * b->passo);
    for (int i = 0; i < b->w; i++) {
        int k = i * 8 / b->w;
        riga[i] = px(barre[sc->specchio ? 7 - k : k]);
    }
    for (int j = alto + 1; j < meta; j++)
        memcpy(b->mem + (size_t)j * b->passo, riga, (size_t)b->w * 4);
    rettangolo(b, 0, meta, b->w, b->h - meta, px(0x202830));
    int s = MAX(b->h / 16, 6);
    numero(b, s / 2, meta + s / 2, s, sc->n, px(0xffffff));
    numero(b, s / 2, b->h - 2 * (s / 2) - s, s / 2, (uint64_t)sc->w, px(0x9aa4b5));
    numero(b, s / 2 + 6 * s, b->h - 2 * (s / 2) - s, s / 2, (uint64_t)sc->h, px(0x9aa4b5));
    /* Densita' ricevuta, accanto alla misura: nelle foto si vede che cosa e'
     * stato chiesto (modalita' tablet). */
    numero(b, s / 2 + 12 * s, b->h - 2 * (s / 2) - s, s / 2, (uint64_t)sc->densita, px(0xfbbf24));
    int q = MAX(b->h / 10, 8);
    int corsa = MAX(b->w - q, 1);
    int x = (int)((sc->n * 8) % (uint64_t)(2 * corsa));
    if (x > corsa)
        x = 2 * corsa - x;
    rettangolo(b, x, b->h - q - 4, q, q, px(0xffffff));
    if (sc->tocco) {
        rettangolo(b, sc->tx - 12, sc->ty - 1, 25, 3, px(0xff3030));
        rettangolo(b, sc->tx - 1, sc->ty - 12, 3, 25, px(0xff3030));
    }
}

/* ---- Flusso ---------------------------------------------------------------- */
struct FlussoFinto {
    atomic_int rif;
    PhFinto *f;
    PhSrvFlusso *sf;
    GMutex m;
    GCond c;
    char *pacchetto;
    bool specchio, banca;
    uint32_t colore;
    int w, h;
    bool aperto, pausa, perso, interrotto, chiuso, protetto, memoria;
    gint64 prossimo_us;
    uint64_t n;
    int tx, ty;
    bool tocco;
    Pool *pool;
    /* aggiunte del 23 nov */
    bool fermo;          /* schermo del telefono spento dal sistema */
    int densita;         /* dpi dello schermo virtuale (0 alla richiesta = quella del telefono, 420) */
    bool registrando;    /* registrazione simulata */
};

static FlussoFinto *fl_prendi(FlussoFinto *fl)
{
    atomic_fetch_add(&fl->rif, 1);
    return fl;
}

static void fl_lascia(gpointer p)
{
    FlussoFinto *fl = p;
    if (atomic_fetch_sub(&fl->rif, 1) != 1)
        return;
    g_mutex_lock(&fl->pool->m);
    fl->pool->chiuso = true;
    g_mutex_unlock(&fl->pool->m);
    pool_lascia(fl->pool);
    g_free(fl->pacchetto);
    g_mutex_clear(&fl->m);
    g_cond_clear(&fl->c);
    g_free(fl);
}

/* Manda un avviso tenendo il lucchetto del flusso: dopo finto_flusso_chiudi
 * (che prende lo stesso lucchetto) non ne parte piu' nessuno. */
static void avvisa(FlussoFinto *fl, PhSrvFlussoEvento ev, const char *frase)
{
    if (fl->chiuso)
        return;
    PhSrvFlussoDati d = {.evento = ev, .larghezza = fl->w, .altezza = fl->h, .protetto = fl->protetto, .frase = frase};
    ph_servizi_flusso_avviso(fl->sf, &d);
}

static void fai_apertura(PhFinto *f, void *dati)
{
    (void)f;
    FlussoFinto *fl = dati;
    g_mutex_lock(&fl->m);
    if (!fl->chiuso) {
        fl->aperto = true;
        avvisa(fl, PH_SRV_FL_APERTO, NULL);
        avvisa(fl, PH_SRV_FL_MISURA, NULL);
        if (fl->banca) {
            fl->protetto = true;
            avvisa(fl, PH_SRV_FL_PROTETTO, NULL);
        }
        g_cond_broadcast(&fl->c);
    }
    g_mutex_unlock(&fl->m);
}

void *finto_flusso_apri(void *b, PhSrvFlusso *sf, const PhSrvFlussoRichiesta *r, PhErrore *e)
{
    PhFinto *f = b;
    g_mutex_lock(&f->m);
    guint n = f->flussi->len;
    g_mutex_unlock(&f->m);
    if (n >= 16) {
        ph_errore(e, "troppi flussi aperti (telefono finto: al massimo 16)");
        return NULL;
    }
    FlussoFinto *fl = g_new0(FlussoFinto, 1);
    atomic_init(&fl->rif, 1);
    fl->f = f;
    fl->sf = sf;
    g_mutex_init(&fl->m);
    g_cond_init(&fl->c);
    fl->specchio = r->pacchetto == NULL || r->pacchetto[0] == '\0';
    fl->pacchetto = g_strdup(fl->specchio ? "" : r->pacchetto);
    fl->banca = strcmp(fl->pacchetto, "it.finto.banca") == 0;
    fl->colore = fl->specchio ? 0x475569 : finto_colore_app(fl->pacchetto);
    /* Specchio: schermo 1080x2340 entro il tetto di 1920 per lato (4.7). */
    fl->w = fl->specchio ? 888 : r->larghezza;
    fl->h = fl->specchio ? 1920 : r->altezza;
    fl->densita = r->densita > 0 ? r->densita : 420;
    atomic_store(&f->ultima_densita, fl->densita);
    if (!fl->specchio && r->pagina != 0) {
        atomic_store(&f->ultima_pagina, r->pagina);
        atomic_fetch_add(&f->pagine_apertura, 1);
        fprintf(stderr, "finto: apro %s sulla pagina %u\n", fl->pacchetto, (unsigned)r->pagina);
    }
    fl->pool = g_new0(Pool, 1);
    atomic_init(&fl->pool->rif, 1);
    g_mutex_init(&fl->pool->m);
    fl->pool->buf = g_ptr_array_new();
    fl->memoria = !f->o.dmabuf;
    g_mutex_lock(&f->m);
    g_ptr_array_add(f->flussi, fl);
    g_mutex_unlock(&f->m);
    fprintf(stderr, "finto: apro il flusso di %s a %dx%d\n", fl->specchio ? "(specchio)" : fl->pacchetto, fl->w, fl->h);
    finto_lavora(f, 200, fai_apertura, fl_prendi(fl), fl_lascia);
    return fl;
}

int finto_flusso_leggi(void *b, void *p, PhFotogramma *out, int timeout_ms)
{
    (void)b;
    FlussoFinto *fl = p;
    gint64 scadenza = g_get_monotonic_time() + (gint64)MAX(timeout_ms, 0) * 1000;
    Scena sc;
    bool dmabuf;
    g_mutex_lock(&fl->m);
    for (;;) {
        if (fl->interrotto || fl->chiuso) {
            g_mutex_unlock(&fl->m);
            return -1;
        }
        if (!fl->aperto || fl->pausa || fl->perso || fl->fermo) {
            if (!g_cond_wait_until(&fl->c, &fl->m, scadenza)) {
                g_mutex_unlock(&fl->m);
                return 0;
            }
            continue;
        }
        gint64 ora = g_get_monotonic_time();
        if (fl->prossimo_us == 0)
            fl->prossimo_us = ora;
        if (ora < fl->prossimo_us) {
            gint64 fino = MIN(fl->prossimo_us, scadenza);
            if (ora >= scadenza) {
                g_mutex_unlock(&fl->m);
                return 0;
            }
            g_cond_wait_until(&fl->c, &fl->m, fino);
            continue;
        }
        fl->prossimo_us += PERIODO_US;
        if (fl->prossimo_us < ora - 3 * PERIODO_US) /* in ritardo: si salta, non si accumula */
            fl->prossimo_us = ora + PERIODO_US;
        sc = (Scena){fl->w, fl->h, fl->n++, fl->protetto, fl->specchio, fl->tocco, fl->tx, fl->ty, fl->colore,
                     fl->densita, fl->f->o.aspetto_app};
        dmabuf = !fl->memoria;
        break;
    }
    g_mutex_unlock(&fl->m);
    Buf *buf = pool_prendi(fl->pool, sc.w, sc.h, dmabuf);
    if (buf == NULL) {
        g_usleep(2000);
        return 0;
    }
    disegna(buf, &sc);
    *out = (PhFotogramma){0};
    out->larghezza = sc.w;
    out->altezza = sc.h;
    out->tempo_us = sc.n * PERIODO_US;
    if (buf->dmafd >= 0) {
        out->tipo = PH_FOTO_DMABUF;
        out->fourcc = DRM_XRGB8888;
        out->modificatore = 0;
        out->piani = 1;
        out->fd[0] = buf->dmafd;
        out->scostamento[0] = 0;
        out->passo_piano[0] = (uint32_t)buf->passo;
        out->colore = PH_FOTO_COLORE_PREDEFINITO;
    } else {
        out->tipo = PH_FOTO_MEMORIA;
        out->formato = PH_FOTO_B8G8R8A8_PREMOLT;
        out->dati = buf->mem;
        out->passo = buf->passo;
    }
    out->rilascia = buf_rilascia;
    out->dati_rilascio = buf;
    return 1;
}

void finto_flusso_interrompi(void *b, void *p)
{
    (void)b;
    FlussoFinto *fl = p;
    g_mutex_lock(&fl->m);
    fl->interrotto = true;
    g_cond_broadcast(&fl->c);
    g_mutex_unlock(&fl->m);
}

void finto_flusso_memoria(void *b, void *p)
{
    (void)b;
    FlussoFinto *fl = p;
    g_mutex_lock(&fl->m);
    if (!fl->memoria)
        ph_shell_decisione("flusso finto in memoria", "la superficie ha rifiutato un DMA-BUF");
    fl->memoria = true;
    g_mutex_unlock(&fl->m);
}

static void fai_ripresa(PhFinto *f, void *dati)
{
    (void)f;
    FlussoFinto *fl = dati;
    g_mutex_lock(&fl->m);
    fl->pausa = false;
    fl->prossimo_us = 0;
    g_cond_broadcast(&fl->c);
    g_mutex_unlock(&fl->m);
}

void finto_flusso_pausa(void *b, void *p, bool pausa)
{
    FlussoFinto *fl = p;
    fprintf(stderr, "finto: flusso di %s %s\n", fl->specchio ? "(specchio)" : fl->pacchetto,
            pausa ? "in pausa" : "ripreso");
    if (pausa) {
        g_mutex_lock(&fl->m);
        fl->pausa = true;
        g_mutex_unlock(&fl->m);
    } else {
        /* Ripresa in ~0,12 s come misurato sul telefono (docs/protocollo.md §2.2). */
        finto_lavora(b, 120, fai_ripresa, fl_prendi(fl), fl_lascia);
    }
}

typedef struct {
    FlussoFinto *fl;
    int w, h;
    int densita; /* 0 = invariata */
} Misura;

static void misura_libera(gpointer p)
{
    Misura *m = p;
    fl_lascia(m->fl);
    g_free(m);
}

static void fai_ridimensiona(PhFinto *f, void *dati)
{
    Misura *m = dati;
    FlussoFinto *fl = m->fl;
    g_mutex_lock(&fl->m);
    int densita = m->densita > 0 ? m->densita : fl->densita;
    if (!fl->chiuso && (fl->w != m->w || fl->h != m->h || fl->densita != densita)) {
        fl->w = m->w;
        fl->h = m->h;
        fl->densita = densita;
        atomic_store(&f->ultima_densita, densita);
        fl->tocco = false;
        avvisa(fl, PH_SRV_FL_MISURA, NULL);
    }
    g_mutex_unlock(&fl->m);
}

void finto_flusso_ridimensiona(void *b, void *p, int larghezza, int altezza)
{
    FlussoFinto *fl = p;
    if (fl->specchio)
        return;
    fprintf(stderr, "finto: ridimensiono %s a %dx%d\n", fl->pacchetto, larghezza, altezza);
    Misura *m = g_new(Misura, 1);
    m->fl = fl_prendi(fl);
    m->w = larghezza;
    m->h = altezza;
    m->densita = 0;
    finto_lavora(b, 100, fai_ridimensiona, m, misura_libera);
}

void finto_flusso_ridimensiona_densita(void *b, void *p, int larghezza, int altezza, int densita)
{
    FlussoFinto *fl = p;
    if (fl->specchio)
        return;
    fprintf(stderr, "finto: ridimensiono %s a %dx%d, densita' %d\n", fl->pacchetto, larghezza, altezza, densita);
    Misura *m = g_new(Misura, 1);
    m->fl = fl_prendi(fl);
    m->w = larghezza;
    m->h = altezza;
    m->densita = densita;
    finto_lavora(b, 100, fai_ridimensiona, m, misura_libera);
}

void finto_flusso_input(void *b, void *p, const PhSrvInput *in)
{
    (void)b;
    FlussoFinto *fl = p;
    g_mutex_lock(&fl->m);
    switch (in->tipo) {
    case PH_SRV_IN_TOCCO:
        if (in->video_w != fl->w || in->video_h != fl->h) {
            /* Regola della specifica (4.5, 4.10): evento calcolato su un'altra misura. */
            fprintf(stderr, "finto: tocco scartato (calcolato su %dx%d, video %dx%d)\n", in->video_w,
                    in->video_h, fl->w, fl->h);
            break;
        }
        fl->tx = in->x;
        fl->ty = in->y;
        fl->tocco = in->azione != PH_SRV_TOCCO_SU && in->azione != PH_SRV_TOCCO_ANNULLA;
        if (in->azione != PH_SRV_TOCCO_MUOVI)
            fprintf(stderr, "finto: tocco %d a %d,%d su %dx%d\n", in->azione, in->x, in->y, in->video_w,
                    in->video_h);
        break;
    case PH_SRV_IN_ROTELLA:
        fprintf(stderr, "finto: rotella %d,%d a %d,%d\n", in->orizzontale, in->verticale, in->x, in->y);
        break;
    case PH_SRV_IN_TASTO:
        fprintf(stderr, "finto: tasto %u %s (meta %u)\n", in->codice, in->giu ? "giu'" : "su", in->meta);
        break;
    case PH_SRV_IN_TESTO:
        fprintf(stderr, "finto: testo di %zu byte\n", strlen(in->testo));
        break;
    case PH_SRV_IN_INDIETRO:
        fprintf(stderr, "finto: indietro\n");
        break;
    }
    g_mutex_unlock(&fl->m);
}

void finto_flusso_chiudi(void *b, void *p, bool togli_task)
{
    PhFinto *f = b;
    FlussoFinto *fl = p;
    g_mutex_lock(&f->m);
    g_ptr_array_remove(f->flussi, fl);
    g_mutex_unlock(&f->m);
    g_mutex_lock(&fl->m);
    fl->chiuso = true;
    g_cond_broadcast(&fl->c);
    g_mutex_unlock(&fl->m);
    fprintf(stderr, "finto: chiudo il flusso di %s%s\n", fl->specchio ? "(specchio)" : fl->pacchetto,
            togli_task ? " e tolgo l'app dai recenti" : "");
    fl_lascia(fl);
}

void finto_flussi_perso(PhFinto *f, bool perso)
{
    g_mutex_lock(&f->m);
    for (guint i = 0; i < f->flussi->len; i++) {
        FlussoFinto *fl = g_ptr_array_index(f->flussi, i);
        g_mutex_lock(&fl->m);
        if (fl->perso != perso) {
            fl->perso = perso;
            fl->prossimo_us = 0;
            avvisa(fl, perso ? PH_SRV_FL_PERSO : PH_SRV_FL_RIPRESO, perso ? "Connessione persa (simulata)" : NULL);
            if (perso && fl->registrando && !fl->chiuso) {
                fl->registrando = false;
                PhSrvRegDati d = {PH_SRV_REG_FINITA, NULL, "Registrazione interrotta: connessione persa (simulata)."};
                ph_servizi_flusso_registrazione(fl->sf, &d);
            }
            g_cond_broadcast(&fl->c);
        }
        g_mutex_unlock(&fl->m);
    }
    g_mutex_unlock(&f->m);
}

void finto_flusso_ultimo_si_chiude(PhFinto *f)
{
    g_mutex_lock(&f->m);
    for (guint i = f->flussi->len; i > 0; i--) {
        FlussoFinto *fl = g_ptr_array_index(f->flussi, i - 1);
        if (fl->specchio)
            continue;
        g_mutex_lock(&fl->m);
        fl->aperto = false;
        fprintf(stderr, "finto: l'app %s si chiude da sola\n", fl->pacchetto);
        avvisa(fl, PH_SRV_FL_APP_CHIUSA, NULL);
        g_mutex_unlock(&fl->m);
        break;
    }
    g_mutex_unlock(&f->m);
}

/* ---- Aggiunte del 23 nov ----------------------------------------------------- */

void finto_flussi_fermi(PhFinto *f, bool fermi)
{
    g_mutex_lock(&f->m);
    for (guint i = 0; i < f->flussi->len; i++) {
        FlussoFinto *fl = g_ptr_array_index(f->flussi, i);
        g_mutex_lock(&fl->m);
        fl->fermo = fermi;
        fl->prossimo_us = 0;
        g_cond_broadcast(&fl->c);
        g_mutex_unlock(&fl->m);
    }
    g_mutex_unlock(&f->m);
}

void finto_flusso_chiave(void *b, void *p)
{
    PhFinto *f = b;
    FlussoFinto *fl = p;
    atomic_fetch_add(&f->chiavi, 1);
    fprintf(stderr, "finto: fotogramma chiave chiesto per %s\n", fl->specchio ? "(specchio)" : fl->pacchetto);
}

void finto_flusso_pagina(void *b, void *p, uint8_t pagina)
{
    PhFinto *f = b;
    FlussoFinto *fl = p;
    atomic_store(&f->ultima_pagina, pagina);
    atomic_fetch_add(&f->pagine_inviate, 1);
    if (atomic_load(&f->pagina_rifiuta))
        fprintf(stderr, "finto: pagina %u rifiutata per %s\n", (unsigned)pagina, fl->specchio ? "(specchio)" : fl->pacchetto);
    else
        fprintf(stderr, "finto: pagina %u mandata a %s\n", (unsigned)pagina, fl->specchio ? "(specchio)" : fl->pacchetto);
}

/* Nome come src/video/cattura.h: "<app ripulita> AAAA-MM-GG HH.MM.SS[ (n)].<est>". */
static char *nome_file(const char *app, const char *est, unsigned indice)
{
    char pulita[96];
    size_t k = 0;
    for (const char *c = app; *c != '\0' && k + 1 < sizeof pulita; c++)
        pulita[k++] = (strchr("/\\:*?\"<>|", *c) != NULL || (unsigned char)*c < 0x20) ? '_' : *c;
    pulita[k] = '\0';
    if (k == 0)
        g_strlcpy(pulita, "YASDE", sizeof pulita);
    GDateTime *t = g_date_time_new_now_local();
    char *data = g_date_time_format(t, "%Y-%m-%d %H.%M.%S");
    g_date_time_unref(t);
    char *r = indice < 2 ? g_strdup_printf("%s %s.%s", pulita, data, est)
                         : g_strdup_printf("%s %s (%u).%s", pulita, data, indice, est);
    g_free(data);
    return r;
}

typedef struct {
    FlussoFinto *fl;
    PhSrvLavoro *l;
    bool finito;
    unsigned dove;
    char *cartella, *app;
} Scatto;

static void scatto_libera(gpointer p)
{
    Scatto *sc = p;
    if (!sc->finito)
        ph_servizi_lavoro_screenshot(sc->l, PH_SRV_ESITO_ERRORE, "chiuso", NULL, NULL);
    fl_lascia(sc->fl);
    g_free(sc->cartella);
    g_free(sc->app);
    g_free(sc);
}

static cairo_status_t scrivi_png(void *c, const unsigned char *d, unsigned int n)
{
    g_byte_array_append(c, d, n);
    return CAIRO_STATUS_SUCCESS;
}

static void fai_scatto(PhFinto *f, void *dati)
{
    (void)f;
    Scatto *sc = dati;
    sc->finito = true;
    FlussoFinto *fl = sc->fl;
    g_mutex_lock(&fl->m);
    bool ha = fl->aperto && !fl->chiuso;
    int w = fl->w, h = fl->h;
    uint32_t col = fl->colore;
    uint64_t n = fl->n;
    g_mutex_unlock(&fl->m);
    if (!ha) {
        ph_servizi_lavoro_screenshot(sc->l, PH_SRV_ESITO_ERRORE, "Nessuna immagine da fotografare.", NULL, NULL);
        return;
    }
    /* L'immagine: la stessa idea dei fotogrammi finti (fascia colorata e barre). */
    cairo_surface_t *su = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t *c = cairo_create(su);
    static const double barre[8][3] = {{.75, .75, .75}, {.75, .75, 0}, {0, .75, .75}, {0, .75, 0},
                                       {.75, 0, .75}, {.75, 0, 0}, {0, 0, .75}, {.06, .06, .06}};
    for (int i = 0; i < 8; i++) {
        cairo_set_source_rgb(c, barre[i][0], barre[i][1], barre[i][2]);
        cairo_rectangle(c, w * i / 8.0, 0, w / 8.0 + 1, h);
        cairo_fill(c);
    }
    cairo_set_source_rgb(c, ((col >> 16) & 0xff) / 255.0, ((col >> 8) & 0xff) / 255.0, (col & 0xff) / 255.0);
    cairo_rectangle(c, 0, 0, w, h * 0.06);
    cairo_fill(c);
    (void)n;
    cairo_destroy(c);
    GByteArray *a = g_byte_array_new();
    bool ok = cairo_surface_write_to_png_stream(su, scrivi_png, a) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(su);
    GBytes *png = g_byte_array_free_to_bytes(a);
    if (!ok) {
        g_bytes_unref(png);
        ph_servizi_lavoro_screenshot(sc->l, PH_SRV_ESITO_ERRORE, "Immagine non generata.", NULL, NULL);
        return;
    }
    char *percorso = NULL;
    const char *frase = NULL;
    if (sc->dove & PH_SRV_FOTO_FILE) {
        /* File VERO, mai sovrascritto (O_EXCL), cartella creata se manca (un livello). */
        if (g_mkdir(sc->cartella, 0755) != 0 && errno != EEXIST)
            frase = "Non riesco a creare la cartella degli screenshot.";
        for (unsigned i = 1; frase == NULL && i <= 999 && percorso == NULL; i++) {
            char *nome = nome_file(sc->app, "png", i);
            char *p = g_build_filename(sc->cartella, nome, NULL);
            g_free(nome);
            int fd = open(p, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
            if (fd < 0) {
                if (errno != EEXIST)
                    frase = "Non riesco a scrivere lo screenshot.";
                g_free(p);
                continue;
            }
            gsize lun = 0;
            const uint8_t *d = g_bytes_get_data(png, &lun);
            bool scritto = true;
            for (gsize fatti = 0; fatti < lun && scritto;) {
                ssize_t k = write(fd, d + fatti, lun - fatti);
                if (k < 0 && errno == EINTR)
                    continue;
                scritto = k > 0;
                fatti += k > 0 ? (gsize)k : 0;
            }
            if (close(fd) != 0 || !scritto) {
                unlink(p);
                g_free(p);
                frase = "Non riesco a scrivere lo screenshot.";
                break;
            }
            percorso = p;
        }
        if (frase == NULL && percorso == NULL)
            frase = "Troppi screenshot con lo stesso nome.";
    }
    if (frase != NULL)
        ph_servizi_lavoro_screenshot(sc->l, PH_SRV_ESITO_ERRORE, frase, NULL, NULL);
    else
        ph_servizi_lavoro_screenshot(sc->l, PH_SRV_ESITO_OK, NULL, percorso,
                                     (sc->dove & PH_SRV_FOTO_APPUNTI) ? png : NULL);
    if (percorso != NULL)
        fprintf(stderr, "finto: screenshot scritto\n");
    g_free(percorso);
    g_bytes_unref(png);
}

void finto_flusso_screenshot(void *b, void *p, PhSrvLavoro *l, unsigned dove, const char *cartella_pc,
                             const char *nome_app)
{
    Scatto *sc = g_new0(Scatto, 1);
    sc->fl = fl_prendi(p);
    sc->l = l;
    sc->dove = dove;
    sc->cartella = g_strdup(cartella_pc != NULL ? cartella_pc : "");
    sc->app = g_strdup(nome_app);
    finto_lavora(b, 50, fai_scatto, sc, scatto_libera);
}

typedef struct {
    FlussoFinto *fl;
    bool avvia;
    char *cartella, *app;
} Registra;

static void registra_libera(gpointer p)
{
    Registra *r = p;
    fl_lascia(r->fl);
    g_free(r->cartella);
    g_free(r->app);
    g_free(r);
}

static void fai_registra(PhFinto *f, void *dati)
{
    Registra *r = dati;
    FlussoFinto *fl = r->fl;
    g_mutex_lock(&fl->m);
    if (!fl->chiuso) {
        if (r->avvia) {
            if (f->reg_guasta) {
                PhSrvRegDati d = {PH_SRV_REG_ERRORE, NULL, "La registrazione non e' partita (guasto simulato)."};
                ph_servizi_flusso_registrazione(fl->sf, &d);
            } else {
                fl->registrando = true;
                char *nome = nome_file(r->app, "mp4", 1);
                char *pe = g_build_filename(r->cartella, nome, NULL);
                PhSrvRegDati d = {PH_SRV_REG_INIZIATA, pe, NULL};
                ph_servizi_flusso_registrazione(fl->sf, &d);
                fprintf(stderr, "finto: registrazione simulata avviata (nessun file)\n");
                g_free(nome);
                g_free(pe);
            }
        } else if (fl->registrando) {
            fl->registrando = false;
            PhSrvRegDati d = {PH_SRV_REG_FINITA, NULL,
                              "Registrazione simulata dal telefono finto: nessun file scritto."};
            ph_servizi_flusso_registrazione(fl->sf, &d);
            fprintf(stderr, "finto: registrazione simulata fermata\n");
        }
    }
    g_mutex_unlock(&fl->m);
}

void finto_flusso_registra(void *b, void *p, bool avvia, const char *cartella_pc, const char *nome_app,
                           bool con_audio)
{
    (void)con_audio;
    Registra *r = g_new0(Registra, 1);
    r->fl = fl_prendi(p);
    r->avvia = avvia;
    r->cartella = g_strdup(cartella_pc != NULL ? cartella_pc : "");
    r->app = g_strdup(nome_app != NULL ? nome_app : "YASDE");
    finto_lavora(b, 30, fai_registra, r, registra_libera);
}

/* Pizzico (23 nov): il telefono finto lo scrive su stderr (fase, centro,
 * passo) e conta i passi ricevuti per le prove; nessun gesto vero. */
void finto_flusso_pizzico(void *b, void *p, int video_w, int video_h, int fase, int x, int y, int passo)
{
    PhFinto *f = b;
    FlussoFinto *fl = p;
    g_mutex_lock(&fl->m);
    bool scarta = video_w != fl->w || video_h != fl->h || fl->pausa;
    g_mutex_unlock(&fl->m);
    if (scarta) {
        fprintf(stderr, "finto: pizzico scartato (misura diversa o in pausa)\n");
        return;
    }
    atomic_fetch_add(&f->pizzichi, 1);
    fprintf(stderr, "finto: pizzico fase %d a %d,%d passo %d\n", fase, x, y, passo);
}
