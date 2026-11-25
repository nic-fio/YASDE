/* Prove di logica di shell-superficie (4.2, 4.3, 4.5), SENZA schermo: controllo
 * dei fotogrammi, formati e spazi dei colori, disposizione dell'immagine, lo
 * scambio del fotogramma in attesa (anche con due thread e con la chiusura nel
 * mezzo) e la lettura del CSS. */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "logica.h"
#include "shell/shell.h"

void ph_risorse_superficie_register_resource(void);

static int errori;
#define CONTROLLA(c)                                                                                 \
    do {                                                                                             \
        if (!(c)) {                                                                                  \
            fprintf(stderr, "prova_superficie: %s:%d: fallito: %s\n", __FILE__, __LINE__, #c);       \
            errori++;                                                                                \
        }                                                                                            \
    } while (0)

/* ---- Fotogrammi di prova: il rilascio si conta ---------------------------- */
static void conta(void *p) { atomic_fetch_add((atomic_int *)p, 1); }

static const uint8_t PIXEL[64 * 4 * 4];

static PhFotogramma memoria(atomic_int *c, int l, int a, size_t passo)
{
    PhFotogramma f = {0};
    f.tipo = PH_FOTO_MEMORIA;
    f.larghezza = l;
    f.altezza = a;
    f.formato = PH_FOTO_B8G8R8A8_PREMOLT;
    f.dati = PIXEL;
    f.passo = passo;
    f.rilascia = conta;
    f.dati_rilascio = c;
    return f;
}

static PhFotogramma dmabuf(atomic_int *c)
{
    PhFotogramma f = {0};
    f.tipo = PH_FOTO_DMABUF;
    f.larghezza = 64;
    f.altezza = 64;
    f.fourcc = 0x3231564e; /* NV12 */
    f.piani = 2;
    f.fd[0] = 3;
    f.fd[1] = 3;
    f.passo_piano[0] = 64;
    f.passo_piano[1] = 64;
    f.rilascia = conta;
    f.dati_rilascio = c;
    return f;
}

static void prova_controllo(void)
{
    atomic_int c = 0;
    PhFotogramma f = memoria(&c, 16, 16, 64);
    CONTROLLA(ph_sup_controlla(&f));
    f = memoria(&c, 16, 16, 63); /* passo minore di larghezza x 4 */
    CONTROLLA(!ph_sup_controlla(&f));
    f = memoria(&c, 0, 16, 64);
    CONTROLLA(!ph_sup_controlla(&f));
    f = memoria(&c, 16, 8193, 64);
    CONTROLLA(!ph_sup_controlla(&f));
    f = memoria(&c, 8192, 1, 8192 * 4);
    CONTROLLA(ph_sup_controlla(&f));
    f = memoria(&c, 16, 16, 64 + PH_SUP_PASSO_EXTRA);
    CONTROLLA(ph_sup_controlla(&f));
    f = memoria(&c, 16, 16, 64 + PH_SUP_PASSO_EXTRA + 1); /* passo esagerato: GTK vedrebbe un buffer enorme */
    CONTROLLA(!ph_sup_controlla(&f));
    f = memoria(&c, 16, 16, 64);
    f.dati = NULL;
    CONTROLLA(!ph_sup_controlla(&f));
    f = memoria(&c, 16, 16, 64);
    f.formato = (PhFotoFormato)77;
    CONTROLLA(!ph_sup_controlla(&f));
    f = memoria(&c, 16, 16, 47);
    f.formato = PH_FOTO_R8G8B8; /* 3 byte: 48 servono */
    CONTROLLA(!ph_sup_controlla(&f));
    f.passo = 48;
    CONTROLLA(ph_sup_controlla(&f));

    f = dmabuf(&c);
    CONTROLLA(ph_sup_controlla(&f));
    f.piani = 0;
    CONTROLLA(!ph_sup_controlla(&f));
    f = dmabuf(&c);
    f.piani = PH_FOTO_PIANI_MAX + 1;
    CONTROLLA(!ph_sup_controlla(&f));
    f = dmabuf(&c);
    f.fd[1] = -1;
    CONTROLLA(!ph_sup_controlla(&f));
    f = dmabuf(&c);
    f.passo_piano[0] = 0;
    CONTROLLA(!ph_sup_controlla(&f));
    f = dmabuf(&c);
    f.fourcc = 0;
    CONTROLLA(!ph_sup_controlla(&f));
    f = dmabuf(&c);
    f.altezza = -5;
    CONTROLLA(!ph_sup_controlla(&f));
    f.tipo = (PhFotoTipo)9;
    CONTROLLA(!ph_sup_controlla(&f));
    CONTROLLA(!ph_sup_controlla(NULL));
    /* Il controllo non rilascia: la responsabilita' e' di chi lo chiama. */
    CONTROLLA(atomic_load(&c) == 0);
}

static void prova_formati(void)
{
    CONTROLLA(ph_sup_formato_gdk(PH_FOTO_B8G8R8A8_PREMOLT) == GDK_MEMORY_B8G8R8A8_PREMULTIPLIED);
    CONTROLLA(ph_sup_formato_gdk(PH_FOTO_R8G8B8A8) == GDK_MEMORY_R8G8B8A8);
    CONTROLLA(ph_sup_formato_gdk(PH_FOTO_R8G8B8) == GDK_MEMORY_R8G8B8);
    CONTROLLA(ph_sup_formato_gdk((PhFotoFormato)99) == -1);
    CONTROLLA(ph_sup_byte_per_pixel(PH_FOTO_B8G8R8A8_PREMOLT) == 4);
    CONTROLLA(ph_sup_byte_per_pixel(PH_FOTO_R8G8B8A8) == 4);
    CONTROLLA(ph_sup_byte_per_pixel(PH_FOTO_R8G8B8) == 3);
    CONTROLLA(ph_sup_byte_per_pixel((PhFotoFormato)99) == 0);

    unsigned p, t, m;
    bool r;
    CONTROLLA(ph_sup_cicp(PH_FOTO_COLORE_BT709_LIMITATO, &p, &t, &m, &r) && p == 1 && t == 1 && m == 1 && r);
    CONTROLLA(ph_sup_cicp(PH_FOTO_COLORE_BT709_PIENO, &p, &t, &m, &r) && p == 1 && t == 1 && m == 1 && !r);
    CONTROLLA(ph_sup_cicp(PH_FOTO_COLORE_BT601_LIMITATO, &p, &t, &m, &r) && p == 6 && t == 6 && m == 6 && r);
    CONTROLLA(ph_sup_cicp(PH_FOTO_COLORE_BT601_PIENO, &p, &t, &m, &r) && p == 6 && t == 6 && m == 6 && !r);
    CONTROLLA(!ph_sup_cicp(PH_FOTO_COLORE_PREDEFINITO, &p, &t, &m, &r));
}

static void prova_rettangolo(void)
{
    int x, y, l, a;
    /* In movimento: tutta l'area, qualunque sia il video. */
    ph_sup_rettangolo(400, 300, 100, 100, false, &x, &y, &l, &a);
    CONTROLLA(x == 0 && y == 0 && l == 400 && a == 300);
    /* A riposo, stesse proporzioni: tutta l'area. */
    ph_sup_rettangolo(400, 300, 800, 600, true, &x, &y, &l, &a);
    CONTROLLA(x == 0 && y == 0 && l == 400 && a == 300);
    /* Area piu' larga: bande ai lati, immagine centrata, mai stirata. */
    ph_sup_rettangolo(400, 300, 300, 300, true, &x, &y, &l, &a);
    CONTROLLA(l == 300 && a == 300 && x == 50 && y == 0);
    /* Area piu' alta: bande sopra e sotto. */
    ph_sup_rettangolo(300, 400, 300, 300, true, &x, &y, &l, &a);
    CONTROLLA(l == 300 && a == 300 && x == 0 && y == 50);
    /* Video verticale in finestra orizzontale. */
    ph_sup_rettangolo(1000, 500, 400, 800, true, &x, &y, &l, &a);
    CONTROLLA(a == 500 && l == 250 && x == 375 && y == 0);
    /* Misura del video ignota: tutta l'area. */
    ph_sup_rettangolo(400, 300, 0, 0, true, &x, &y, &l, &a);
    CONTROLLA(l == 400 && a == 300);
    /* Valori estremi: nessun overflow, mai fuori dall'area. */
    ph_sup_rettangolo(2000000000, 1, 1, 2000000000, true, &x, &y, &l, &a);
    CONTROLLA(l >= 1 && l <= 2000000000 && a == 1 && x >= 0);
    ph_sup_rettangolo(0, 0, 10, 10, true, &x, &y, &l, &a);
    CONTROLLA(l == 0 && a == 0);
}

/* ---- Scambio: logica a un thread ------------------------------------------ */
static void prova_scambio_semplice(void)
{
    PhSupScambio s;
    ph_sup_scambio_inizia(&s);
    atomic_int a = 0, b = 0, c = 0;
    PhFotogramma fa = memoria(&a, 8, 8, 32), fb = memoria(&b, 8, 8, 32), fc = memoria(&c, 8, 8, 32);
    bool sost;
    CONTROLLA(ph_sup_scambio_metti(&s, &fa, &sost) && !sost); /* parte la consegna */
    CONTROLLA(fa.rilascia == NULL);                            /* ceduto allo scambio */
    CONTROLLA(!ph_sup_scambio_metti(&s, &fb, &sost) && sost); /* una sola in volo; il vecchio rilasciato */
    CONTROLLA(atomic_load(&a) == 1 && atomic_load(&b) == 0);
    PhFotogramma presa;
    CONTROLLA(ph_sup_scambio_prendi(&s, &presa) && presa.dati_rilascio == &b); /* vince l'ultimo */
    ph_fotogramma_rilascia(&presa);
    CONTROLLA(atomic_load(&b) == 1);
    CONTROLLA(!ph_sup_scambio_prendi(&s, &presa)); /* niente di nuovo */
    CONTROLLA(ph_sup_scambio_metti(&s, &fc, &sost) && !sost); /* nuova consegna possibile */
    ph_sup_scambio_chiudi(&s); /* il fotogramma in attesa si rilascia */
    CONTROLLA(atomic_load(&c) == 1);
    /* Dopo la chiusura: rilascio immediato, nessuna consegna. */
    atomic_int d = 0;
    PhFotogramma fd = memoria(&d, 8, 8, 32);
    CONTROLLA(!ph_sup_scambio_metti(&s, &fd, &sost) && atomic_load(&d) == 1);
    ph_sup_scambio_chiudi(&s); /* idempotente */
    ph_sup_scambio_libera(&s);
    CONTROLLA(atomic_load(&a) == 1 && atomic_load(&b) == 1 && atomic_load(&c) == 1);

    /* libera rilascia anche un fotogramma ancora in attesa. */
    ph_sup_scambio_inizia(&s);
    atomic_int e = 0;
    PhFotogramma fe = memoria(&e, 8, 8, 32);
    CONTROLLA(ph_sup_scambio_metti(&s, &fe, &sost));
    ph_sup_scambio_libera(&s);
    CONTROLLA(atomic_load(&e) == 1);
}

/* ---- Scambio: due thread, con la chiusura mentre il produttore va avanti --- */
#define N_FOTO 100000
#define CONSEGNE_PRIMA_DI_CHIUDERE 2

typedef struct {
    PhSupScambio sc;
    atomic_int rilasci[N_FOTO];
    GAsyncQueue *consegne;  /* un gettone per ogni consegna "partita" */
} Gara;

/* Il produttore non aspetta nessuno: la chiusura si sovrappone a metti. */
static gpointer produttore(gpointer p)
{
    Gara *g = p;
    for (int i = 0; i < N_FOTO; i++) {
        PhFotogramma f = memoria(&g->rilasci[i], 8, 8, 32);
        bool sost;
        if (ph_sup_scambio_metti(&g->sc, &f, &sost))
            g_async_queue_push(g->consegne, GINT_TO_POINTER(1));
    }
    g_async_queue_push(g->consegne, GINT_TO_POINTER(2)); /* fine */
    return NULL;
}

static void prova_scambio_thread(void)
{
    Gara *g = g_new0(Gara, 1);
    ph_sup_scambio_inizia(&g->sc);
    g->consegne = g_async_queue_new();
    GThread *t = g_thread_new("prova-produttore", produttore, g);
    bool chiuso = false;
    int arrivate = 0;
    for (;;) {
        /* Aspetta un segnale, con scadenza: mai una pausa a occhio. */
        gpointer v = g_async_queue_timeout_pop(g->consegne, 10 * G_USEC_PER_SEC);
        if (v == NULL) {
            CONTROLLA(!"scaduta l'attesa di una consegna");
            break;
        }
        if (GPOINTER_TO_INT(v) == 2)
            break;
        PhFotogramma f;
        if (ph_sup_scambio_prendi(&g->sc, &f))
            ph_fotogramma_rilascia(&f); /* "mostrato" */
        if (!chiuso && ++arrivate == CONSEGNE_PRIMA_DI_CHIUDERE) {
            ph_sup_scambio_chiudi(&g->sc); /* mentre il produttore continua */
            chiuso = true;
        }
    }
    g_thread_join(t);
    ph_sup_scambio_chiudi(&g->sc);
    int mai = 0, doppi = 0;
    for (int i = 0; i < N_FOTO; i++) {
        int n = atomic_load(&g->rilasci[i]);
        if (n == 0)
            mai++;
        else if (n != 1)
            doppi++;
    }
    CONTROLLA(mai == 0);   /* nessun fotogramma perso senza rilascio (descrittore perso) */
    CONTROLLA(doppi == 0); /* nessun rilascio doppio */
    ph_sup_scambio_libera(&g->sc);
    g_async_queue_unref(g->consegne);
    g_free(g);
}

/* ---- Vita del fotogramma nella texture: rilascio una volta sola ------------ */
static void prova_tenuto(void)
{
    atomic_int c = 0;
    PhFotogramma f;
    PhSupTenuto *t;

    /* Memoria: un solo riferimento (il GBytes), rilasciato da GTK/GBytes. */
    f = memoria(&c, 8, 8, 32);
    t = ph_sup_tenuto_nuovo(&f, 1);
    CONTROLLA(f.rilascia == NULL && atomic_load(&c) == 0);
    ph_sup_tenuto_da_gtk(t);
    CONTROLLA(atomic_load(&c) == 1);

    /* DMA-BUF, costruzione riuscita: rilascia quando GTK lascia la texture. */
    atomic_store(&c, 0);
    f = dmabuf(&c);
    t = ph_sup_tenuto_nuovo(&f, 2);
    ph_sup_tenuto_costruito(t, true);
    CONTROLLA(atomic_load(&c) == 0); /* la texture e' viva */
    ph_sup_tenuto_da_gtk(t);
    CONTROLLA(atomic_load(&c) == 1);

    /* Costruzione fallita, GTK NON ha chiamato il rilascio: rilascia noi, una volta. */
    atomic_store(&c, 0);
    f = dmabuf(&c);
    t = ph_sup_tenuto_nuovo(&f, 2);
    ph_sup_tenuto_costruito(t, false);
    CONTROLLA(atomic_load(&c) == 1);

    /* Costruzione fallita, GTK ha GIA' chiamato il rilascio: una volta sola. */
    atomic_store(&c, 0);
    f = dmabuf(&c);
    t = ph_sup_tenuto_nuovo(&f, 2);
    ph_sup_tenuto_da_gtk(t); /* GTK, dentro build */
    CONTROLLA(atomic_load(&c) == 0); /* ce n'e' ancora uno: il nostro */
    ph_sup_tenuto_costruito(t, false);
    CONTROLLA(atomic_load(&c) == 1);
}

static void prova_sforzo(void)
{
    PhSupSforzo s = {0};
    bool camb, pp;
    unsigned causa;
    gint64 t = 1000000;
    /* Senza sostituiti: nessun cambio. */
    for (int i = 0; i < 20; i++, t += 100000)
        ph_sup_sforzo_nota(&s, t, false, false, &camb, &causa, &pp);
    CONTROLLA(s.livello == 0 && !ph_sup_sforzo_salta(&s));
    /* Sostituiti a ripetizione: sale a 1, poi a 2, mai oltre. */
    int visti = 0, max = 0;
    for (int i = 0; i < 60; i++, t += 50000) {
        ph_sup_sforzo_nota(&s, t, true, false, &camb, &causa, &pp);
        if (camb) {
            visti++;
            CONTROLLA(causa >= PH_SUP_SOGLIA_SOSTITUITI);
        }
        if (s.livello > max)
            max = s.livello;
    }
    CONTROLLA(max == PH_SUP_LIVELLO_MAX && s.livello == PH_SUP_LIVELLO_MAX && visti == 2); /* ogni riduzione si scrive, anche entro 3 s */
    /* A livello 2 passa un fotogramma su quattro, a livello 1 uno su due. */
    int passati = 0;
    for (int i = 0; i < 40; i++)
        passati += !ph_sup_sforzo_salta(&s);
    CONTROLLA(passati == 10);
    s.livello = 1;
    passati = 0;
    for (int i = 0; i < 40; i++)
        passati += !ph_sup_sforzo_salta(&s);
    CONTROLLA(passati == 20);
    s.livello = 2;
    /* Coda vuota (nessun sostituito per una finestra): torna a 0. */
    bool tornato = false;
    for (int i = 0; i < 30; i++, t += 100000) {
        ph_sup_sforzo_nota(&s, t, false, false, &camb, &causa, &pp);
        if (s.livello == 0)
            tornato = true;
    }
    CONTROLLA(tornato && s.livello == 0);
    /* Isteresi: una sola finestra pulita tra due con sostituiti non fa tornare a 0. */
    PhSupSforzo h = {0};
    h.livello = 1;
    h.livello_scritto = 1; /* (partenza gia' scritta nel registro) */
    gint64 th = 5000000;
    ph_sup_sforzo_nota(&h, th, false, false, &camb, &causa, &pp);          /* apre la finestra */
    th += PH_SUP_FINESTRA_US;
    ph_sup_sforzo_nota(&h, th, false, false, &camb, &causa, &pp);          /* 1a pulita */
    CONTROLLA(h.livello == 1 && !camb);
    th += PH_SUP_FINESTRA_US;
    ph_sup_sforzo_nota(&h, th, false, false, &camb, &causa, &pp);          /* 2a pulita */
    CONTROLLA(h.livello == 0 && camb && !pp);
    /* Pulita, con sostituiti, pulita: il livello resta (le pulite ripartono da zero). */
    PhSupSforzo k = {0};
    k.livello = 1;
    gint64 tk = 9000000;
    ph_sup_sforzo_nota(&k, tk, false, false, &camb, &causa, &pp); /* apre */
    tk += PH_SUP_FINESTRA_US;
    ph_sup_sforzo_nota(&k, tk, false, false, &camb, &causa, &pp); /* pulita (1) */
    CONTROLLA(k.livello == 1);
    for (int i = 0; i < 5; i++) {                                  /* con sostituiti (sale a 2) */
        ph_sup_sforzo_nota(&k, tk + i * 10000, true, false, &camb, &causa, &pp);
    }
    tk += PH_SUP_FINESTRA_US;
    ph_sup_sforzo_nota(&k, tk, false, false, &camb, &causa, &pp);
    CONTROLLA(k.livello == 2 && k.puliti == 0);
    tk += PH_SUP_FINESTRA_US;
    ph_sup_sforzo_nota(&k, tk, false, false, &camb, &causa, &pp); /* pulita (1) */
    CONTROLLA(k.livello == 2);
    /* Registro: le riduzioni si scrivono sempre; un RITORNO entro 3 s dall'ultima riga no, dopo 3 s si scrive lo stato vero. */
    PhSupSforzo lg = {0};
    gint64 tl = 30000000;
    ph_sup_sforzo_nota(&lg, tl, false, false, &camb, &causa, &pp); /* apre */
    for (int i = 0; i < 3; i++)
        ph_sup_sforzo_nota(&lg, tl + i * 1000, true, false, &camb, &causa, &pp);
    tl += PH_SUP_FINESTRA_US;
    ph_sup_sforzo_nota(&lg, tl, true, false, &camb, &causa, &pp);
    CONTROLLA(lg.livello == 1 && camb && lg.livello_scritto == 1);
    for (int i = 0; i < 3; i++)
        ph_sup_sforzo_nota(&lg, tl + 1 + i, true, false, &camb, &causa, &pp);
    tl += PH_SUP_FINESTRA_US;
    ph_sup_sforzo_nota(&lg, tl, true, false, &camb, &causa, &pp);
    CONTROLLA(lg.livello == 2 && camb && lg.livello_scritto == 2); /* entro 3 s, ma e' una riduzione */
    bool scritto = false;
    int finestre_lg = 0;
    while (!scritto && finestre_lg < 40) { /* ritorno: pulite, ma entro 3 s dall'ultima riga non si scrive */
        tl += PH_SUP_FINESTRA_US;
        ph_sup_sforzo_nota(&lg, tl, false, false, &camb, &causa, &pp);
        finestre_lg++;
        if (lg.livello == 0 && !camb)
            CONTROLLA(lg.livello_scritto == 2 && tl - lg.ultimo_log < PH_SUP_LOG_MIN_US);
        scritto = camb;
    }
    CONTROLLA(scritto && lg.livello_scritto == 0 && lg.livello == 0 && tl - lg.ultimo_log < 1);
    /* Ricaduta: dopo un ritorno a 0 e una risalita servono piu' finestre pulite. */
    PhSupSforzo rc = {0};
    rc.livello = 1;
    gint64 tr = 20000000;
    int finestre = 0;
    ph_sup_sforzo_nota(&rc, tr, false, false, &camb, &causa, &pp);
    while (rc.livello != 0 && finestre < 100) {
        tr += PH_SUP_FINESTRA_US;
        ph_sup_sforzo_nota(&rc, tr, false, false, &camb, &causa, &pp);
        finestre++;
    }
    CONTROLLA(finestre == 2 && rc.tornato);
    for (int i = 0; i < 4; i++) /* ricade */
        ph_sup_sforzo_nota(&rc, tr + 1000 + i, true, false, &camb, &causa, &pp);
    tr += PH_SUP_FINESTRA_US;
    ph_sup_sforzo_nota(&rc, tr, true, false, &camb, &causa, &pp);
    CONTROLLA(rc.livello == 1 && rc.ricadute == 1);
    finestre = 0;
    while (rc.livello != 0 && finestre < 100) {
        tr += PH_SUP_FINESTRA_US;
        ph_sup_sforzo_nota(&rc, tr, false, false, &camb, &causa, &pp);
        finestre++;
    }
    CONTROLLA(finestre == 4);
    /* In primo piano: mai a un livello alto, nemmeno con tanti sostituiti. */
    PhSupSforzo p = {0};
    for (int i = 0; i < 100; i++, t += 50000)
        ph_sup_sforzo_nota(&p, t, true, true, &camb, &causa, &pp);
    CONTROLLA(p.livello == 0);
    /* Passando in primo piano da un livello alto si torna subito a 0. */
    PhSupSforzo q = {0};
    q.livello = 2;
    ph_sup_sforzo_nota(&q, t, false, true, &camb, &causa, &pp);
    CONTROLLA(camb && q.livello == 0 && pp);
}

static void prova_ritmo(void)
{
    bool camb, pp;
    unsigned causa;
    gint64 t = 1000000;
    atomic_int a = 0, b = 0, c = 0;
    PhSupRitmo r = {0};
    r.sf.livello = 1; /* uno ogni due */
    PhFotogramma fa = memoria(&a, 8, 8, 32), fb = memoria(&b, 8, 8, 32), fc = memoria(&c, 8, 8, 32);
    /* contatore 0: passa; contatore 1: da parte. */
    CONTROLLA(ph_sup_ritmo_arriva(&r, &fa, t, false, &camb, &causa, &pp));
    CONTROLLA(!ph_sup_ritmo_arriva(&r, &fb, t, false, &camb, &causa, &pp) && r.c_e && fb.rilascia == NULL);
    /* L'ULTIMO fotogramma saltato, poi silenzio: viene mostrato (va allo scambio). */
    PhFotogramma out = {0};
    CONTROLLA(ph_sup_ritmo_silenzio(&r, &out, t + 100000, false, &camb, &causa, &pp) &&
              out.dati_rilascio == &b && !r.c_e);
    CONTROLLA(atomic_load(&b) == 0); /* non rilasciato: lo ha il chiamante */
    ph_fotogramma_rilascia(&out);
    CONTROLLA(atomic_load(&b) == 1);
    CONTROLLA(!ph_sup_ritmo_silenzio(&r, &out, t + 200000, false, &camb, &causa, &pp)); /* niente da parte */
    /* Un fotogramma da parte viene rilasciato quando ne passa uno piu' nuovo, e dal successivo da parte. */
    atomic_store(&a, 0); atomic_store(&b, 0); atomic_store(&c, 0);
    r = (PhSupRitmo){0};
    r.sf.livello = 1;
    fa = memoria(&a, 8, 8, 32); fb = memoria(&b, 8, 8, 32); fc = memoria(&c, 8, 8, 32);
    CONTROLLA(ph_sup_ritmo_arriva(&r, &fa, t, false, &camb, &causa, &pp));  /* passa */
    CONTROLLA(!ph_sup_ritmo_arriva(&r, &fb, t, false, &camb, &causa, &pp)); /* b da parte */
    CONTROLLA(ph_sup_ritmo_arriva(&r, &fc, t, false, &camb, &causa, &pp));  /* passa: b si rilascia */
    CONTROLLA(atomic_load(&b) == 1 && !r.c_e);
    /* Alla fine del thread il fotogramma da parte si rilascia una volta sola. */
    atomic_store(&a, 0);
    fa = memoria(&a, 8, 8, 32);
    CONTROLLA(!ph_sup_ritmo_arriva(&r, &fa, t, false, &camb, &causa, &pp)); /* contatore dispari: da parte */
    ph_sup_ritmo_libera(&r);
    ph_sup_ritmo_libera(&r);
    CONTROLLA(atomic_load(&a) == 1);
    /* In primo piano: non salta mai e azzera subito il livello. */
    atomic_store(&a, 0);
    r = (PhSupRitmo){0};
    r.sf.livello = 2;
    for (int i = 0; i < 8; i++) {
        PhFotogramma f = memoria(&a, 8, 8, 32);
        CONTROLLA(ph_sup_ritmo_arriva(&r, &f, t, true, &camb, &causa, &pp));
        if (i == 0)
            CONTROLLA(camb && pp);
        CONTROLLA(f.rilascia != NULL); /* invariato: lo manda il chiamante */
        ph_fotogramma_rilascia(&f);
    }
    CONTROLLA(r.sf.livello == 0 && !r.c_e);
    /* Livello 2 poi silenzio: il battito a tempo riporta a 0. */
    r = (PhSupRitmo){0};
    r.sf.livello = 2;
    for (int i = 0; i < 40; i++, t += 100000)
        ph_sup_ritmo_silenzio(&r, &out, t, false, &camb, &causa, &pp);
    CONTROLLA(r.sf.livello == 0);
}

static void prova_png(void)
{
    GdkTexture *img = NULL;
    char *frase = NULL;
    /* Dati che non sono un PNG: nessuna immagine, frase tradotta (non quella di GTK). */
    GBytes *brutti = g_bytes_new_static("non e' un png", 13);
    CONTROLLA(!ph_sup_decodifica_png(brutti, &img, &frase) && img == NULL && frase != NULL);
    CONTROLLA(strcmp(frase, "Non riesco a leggere l'immagine dello screenshot.") == 0);
    g_free(frase);
    g_bytes_unref(brutti);
    CONTROLLA(!ph_sup_decodifica_png(NULL, &img, &frase) && frase != NULL);
    g_free(frase);
    /* Un PNG vero si decodifica nella stessa misura. */
    static const guint8 px[4 * 3 * 2] = {0};
    GBytes *dati = g_bytes_new_static(px, sizeof px);
    GdkTexture *orig = gdk_memory_texture_new(3, 2, GDK_MEMORY_R8G8B8A8, dati, 12);
    GBytes *png = gdk_texture_save_to_png_bytes(orig);
    CONTROLLA(ph_sup_decodifica_png(png, &img, &frase) && img != NULL && frase == NULL);
    CONTROLLA(img != NULL && gdk_texture_get_width(img) == 3 && gdk_texture_get_height(img) == 2);
    g_clear_object(&img);
    g_bytes_unref(png);
    g_object_unref(orig);
    g_bytes_unref(dati);
    /* Fine della protezione: una richiesta di fotogramma chiave, solo li'. */
    CONTROLLA(ph_sup_chiave_da_chiedere(true, false));
    CONTROLLA(!ph_sup_chiave_da_chiedere(false, false) && !ph_sup_chiave_da_chiedere(false, true) &&
              !ph_sup_chiave_da_chiedere(true, true));
}

static void prova_esito_scatto(void)
{
    PhSupEsitoScatto e;
    /* Nessun PNG chiesto: OK, niente immagine, niente frase nostra. */
    e = ph_sup_esito_scatto(true, true, PH_SUP_DEC_NON_CHIESTA);
    CONTROLLA(e.esito_ok && !e.immagine && !e.frase_nostra);
    /* Decodifica riuscita: OK con immagine (con o senza file). */
    e = ph_sup_esito_scatto(true, true, PH_SUP_DEC_RIUSCITA);
    CONTROLLA(e.esito_ok && e.immagine && !e.frase_nostra);
    e = ph_sup_esito_scatto(true, false, PH_SUP_DEC_RIUSCITA);
    CONTROLLA(e.esito_ok && e.immagine);
    /* Decodifica fallita e thread non creabile: con il file resta OK (senza immagine, con la nostra frase). */
    PhSupDecodifica rami[] = {PH_SUP_DEC_FALLITA, PH_SUP_DEC_NO_THREAD};
    for (int i = 0; i < 2; i++) {
        e = ph_sup_esito_scatto(true, true, rami[i]);
        CONTROLLA(e.esito_ok && !e.immagine && e.frase_nostra);
        /* ...senza file (solo appunti) non resta nulla: errore. */
        e = ph_sup_esito_scatto(true, false, rami[i]);
        CONTROLLA(!e.esito_ok && !e.immagine && e.frase_nostra);
    }
    /* Errore dei servizi: resta errore, la frase e' la loro. */
    e = ph_sup_esito_scatto(false, false, PH_SUP_DEC_NON_CHIESTA);
    CONTROLLA(!e.esito_ok && !e.immagine && !e.frase_nostra);
}

static void prova_esatta(void)
{
    /* 1000 x 600 px fisici di contenuto a scala 1, 1,25, 1,5, 2: la misura esatta e' 1000/scala x 600/scala,
     * l'area allocata e' intera (arrotondata per eccesso). A riposo l'immagine e' ESATTAMENTE quel
     * rettangolo, da (0, 0), e il campionamento esatto e' lecito. */
    const double scale[] = {1.0, 1.25, 1.5, 2.0};
    for (int i = 0; i < 4; i++) {
        double el = 1000.0 / scale[i], ea = 600.0 / scale[i];
        int al = (int)(el + 0.999999), aa = (int)(ea + 0.999999);
        PhSupRett r = ph_sup_rettangolo_esatto(al, aa, el, ea, 1000, 600, scale[i], true);
        CONTROLLA(r.x == 0 && r.y == 0 && r.l == el && r.a == ea);
        /* In movimento: tutta l'area allocata, mai il rettangolo esatto. */
        PhSupRett m = ph_sup_rettangolo_esatto(al, aa, el, ea, 1000, 600, scale[i], false);
        CONTROLLA(m.x == 0 && m.y == 0 && m.l == al && m.a == aa);
    }
    /* Il caso del contratto: 1000 px a 1,5 = 666,667 logici, allocati 667 (= 1000,5 fisici). */
    PhSupRett c = ph_sup_rettangolo_esatto(667, 400, 1000.0 / 1.5, 600.0 / 1.5, 1000, 600, 1.5, true);
    CONTROLLA(c.l > 666.66 && c.l < 666.68 && c.l < 667);
    /* Misura esatta 0 x 0: comportamento di prima (intero, nelle proporzioni a riposo). */
    PhSupRett z = ph_sup_rettangolo_esatto(400, 300, 0, 0, 300, 300, 1.5, true);
    CONTROLLA(z.l == 300 && z.a == 300 && z.x == 50 && z.y == 0);
    /* Texture di proporzioni diverse dal rettangolo: nelle sue proporzioni, centrata, mai stirata. */
    PhSupRett d = ph_sup_rettangolo_esatto(400, 300, 400, 300, 300, 300, 1.0, true);
    CONTROLLA(d.l == 300 && d.a == 300 && d.x == 50 && d.y == 0);
    PhSupRett e = ph_sup_rettangolo_esatto(300, 400, 300, 400, 300, 300, 1.0, true);
    CONTROLLA(e.l == 300 && e.a == 300 && e.x == 0 && e.y == 50);
    /* Video ignoto: tutto il rettangolo esatto. */
    PhSupRett g = ph_sup_rettangolo_esatto(400, 300, 399.5, 299.5, 0, 0, 1.0, true);
    CONTROLLA(g.l == 399.5 && g.a == 299.5);
    /* Misura esatta piu' grande dell'area: mai fuori dall'area. */
    PhSupRett h = ph_sup_rettangolo_esatto(100, 100, 500, 500, 0, 0, 1.0, true);
    CONTROLLA(h.l == 100 && h.a == 100);
}

static void prova_pizzico(void)
{
    /* Normale: tutte le fasi passano. */
    for (int f = PH_SRV_PIZZICO_INIZIO; f <= PH_SRV_PIZZICO_ANNULLA; f++)
        CONTROLLA(ph_sup_pizzico_ammesso(f, false));
    /* In pausa: niente, nemmeno FINE. */
    for (int f = PH_SRV_PIZZICO_INIZIO; f <= PH_SRV_PIZZICO_ANNULLA; f++)
        CONTROLLA(!ph_sup_pizzico_ammesso(f, true));
    /* Il velo (persa o protetto) NON conta: passa come tocco, rotella e tasti. */
    /* Fase sconosciuta. */
    CONTROLLA(!ph_sup_pizzico_ammesso(-1, false) && !ph_sup_pizzico_ammesso(4, false));
}

static void prova_satura(void)
{
    CONTROLLA(ph_sup_satura_i16(120) == 120 && ph_sup_satura_i16(-120) == -120);
    CONTROLLA(ph_sup_satura_i16(100000) == 32767 && ph_sup_satura_i16(-100000) == -32768);
    CONTROLLA(ph_sup_satura_i16(32767) == 32767 && ph_sup_satura_i16(-32768) == -32768);
}

int main(void)
{
    prova_pizzico();
    prova_esatta();
    prova_esito_scatto();
    prova_png();
    prova_ritmo();
    prova_tenuto();
    prova_satura();
    prova_sforzo();
    prova_controllo();
    prova_formati();
    prova_rettangolo();
    prova_scambio_semplice();
    prova_scambio_thread();

    ph_risorse_superficie_register_resource();
    if (ph_shell_stile_controlla("/it/yasde/shell/superficie.css") != 0) {
        fprintf(stderr, "superficie.css: errori di sintassi\n");
        errori++;
    }
    if (errori == 0)
        printf("prova_superficie: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
