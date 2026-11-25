/* Prova del vetro (vetro.c) con GTK vero su uno schermo (broadway nel
 * container): registrazione, albero dei nodi della radice, niente sfocature
 * annidate, solo cio' che sta sotto, aggiornamento, riposo, ciclo di vita.
 * Senza schermo esce con 77 (test saltato).
 *
 * I fotogrammi: una tick callback sulla radice li conta e tiene acceso il
 * frame clock (senza, "nessun disegno a riposo" sarebbe vero banalmente). Nel
 * broadway di prova il frame clock e' lento: le attese sono in fotogrammi o
 * su una condizione, sempre con scadenza larga, mai pause fisse.
 *
 * Limiti dichiarati (rami senza prova):
 *  - la ridisposizione di una superficie (spostata, comparsa, sparita) non ha
 *    codice nel vetro: la prova verifica solo l'effetto (la radice si ridisegna,
 *    il rettangolo del clip e' quello nuovo, la sfocatura sparisce), che e' di GTK;
 *  - il renderer su scheda grafica vero (qui c'e' Broadway: si prova solo la
 *    funzione pura e YASDE_SFOCATURA);
 *  - la superficie con larghezza o altezza nulla;
 *  - il ramo "voce_morta" per una voce gia' tolta;
 *  - la sfocatura di una superficie che sta SOTTO un'altra non e' provata a pixel
 *    (si prova l'albero dei nodi, non i colori disegnati: Broadway non sfoca);
 *  - i coefficienti della matrice dei colori si controllano solo sugli elementi
 *    diagonali [0][0] e [2][2]. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "interno.h"
#include "shell/servizi_backend.h"
#include "shell/shell.h"

static int errori;
#define CONTROLLA(c)                                                                               \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: fallito: %s\n", __FILE__, __LINE__, #c);                       \
            errori++;                                                                              \
        }                                                                                          \
    } while (0)

#define SCAD_MS 60000
#define QUIETE_K 3
#define RIPOSO_F 5

/* ---- Attese ----------------------------------------------------------------- */
static unsigned frames;

static gboolean tick_conta(GtkWidget *w, GdkFrameClock *c, gpointer u)
{
    (void)w;
    (void)c;
    (void)u;
    frames++;
    return G_SOURCE_CONTINUE;
}

typedef bool (*Cond)(void *);

static bool aspetta(Cond c, void *u, int ms)
{
    gint64 fine = g_get_monotonic_time() + (gint64)ms * 1000;
    while (g_get_monotonic_time() < fine) {
        if (c(u))
            return true;
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    return c(u);
}

typedef struct {
    unsigned da;
    unsigned n;
} AttesaFrame;

static bool cond_frames(void *u)
{
    AttesaFrame *a = u;
    return frames >= a->da + a->n;
}

static void aspetta_frames(unsigned n)
{
    AttesaFrame a = {frames, n};
    CONTROLLA(aspetta(cond_frames, &a, SCAD_MS));
}

/* ---- Banco: servizi, shell, finestra ----------------------------------------- */
typedef struct {
    PhServizi *s;
    PhShell *sh;
    GtkWidget *win, *radice;
} Banco;

static bool cond_mappata(void *u)
{
    Banco *b = u;
    return gtk_widget_get_mapped(b->radice) && gtk_widget_get_width(b->radice) > 0 &&
           gtk_widget_get_height(b->radice) > 0;
}

static Banco *banco_nuovo(const char *sfocatura)
{
    if (sfocatura != NULL)
        g_setenv("YASDE_SFOCATURA", sfocatura, TRUE);
    else
        g_unsetenv("YASDE_SFOCATURA");
    Banco *b = g_new0(Banco, 1);
    PhServiziBackend vt;
    memset(&vt, 0, sizeof vt);
    b->s = ph_servizi_nuovi(&vt, NULL);
    b->sh = ph_shell_nuova(b->s);
    b->radice = ph_shell_radice(b->sh);
    b->win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(b->win), 800, 600);
    gtk_window_set_child(GTK_WINDOW(b->win), b->radice);
    gtk_widget_add_tick_callback(b->radice, tick_conta, NULL, NULL);
    gtk_window_present(GTK_WINDOW(b->win));
    CONTROLLA(aspetta(cond_mappata, b, SCAD_MS));
    return b;
}

static void pompa_poco(void)
{
    for (int i = 0; i < 50; i++)
        g_main_context_iteration(NULL, FALSE);
}

static void banco_libera(Banco *b)
{
    ph_shell_libera(b->sh);
    gtk_window_destroy(GTK_WINDOW(b->win));
    ph_servizi_libera(b->s);
    pompa_poco();
    g_free(b);
}

/* Un riquadro colorato (ha un disegno suo) in uno strato, a (x,y) con misura (w,h). */
static GtkWidget *scatola(Banco *b, PhShellStrato st, int x, int y, int w, int h)
{
    GtkWidget *r = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(r, "prova-colore");
    gtk_widget_set_size_request(r, w, h);
    gtk_widget_set_halign(r, GTK_ALIGN_START);
    gtk_widget_set_valign(r, GTK_ALIGN_START);
    gtk_widget_set_margin_start(r, x);
    gtk_widget_set_margin_top(r, y);
    ph_shell_strato_aggiungi(b->sh, st, r);
    return r;
}

static bool cond_alloc(void *u)
{
    GtkWidget *w = u;
    return gtk_widget_get_mapped(w) && gtk_widget_get_width(w) > 0;
}

static void aspetta_alloc(GtkWidget *w) { CONTROLLA(aspetta(cond_alloc, w, SCAD_MS)); }

/* Un disegno vero ha almeno un nodo: serve che la radice abbia disegnato una volta. */
static bool cond_disegnata(void *u) { return ph_vetro_radice_snapshot_conta(u) > 0; }

/* Assestamento: nessun disegno della radice da QUIETE_K fotogrammi consecutivi. */
typedef struct {
    GtkWidget *radice;
    unsigned conta, fr;
    unsigned quieti;
} Quiete;

static bool cond_quiete(void *u)
{
    Quiete *q = u;
    unsigned c = ph_vetro_radice_snapshot_conta(q->radice);
    if (c != q->conta) {
        q->conta = c;
        q->quieti = 0;
        q->fr = frames;
    } else if (frames != q->fr) {
        q->quieti += frames - q->fr;
        q->fr = frames;
    }
    return q->quieti >= QUIETE_K;
}

static void assesta(Banco *b)
{
    CONTROLLA(aspetta(cond_disegnata, b->radice, SCAD_MS));
    Quiete q = {b->radice, ph_vetro_radice_snapshot_conta(b->radice), frames, 0};
    CONTROLLA(aspetta(cond_quiete, &q, SCAD_MS));
}

/* ---- Albero dei nodi ------------------------------------------------------------ */
typedef struct {
    graphene_rect_t clip;
    float angolo;
    float raggio;
    float sat00, sat22; /* elementi diagonali della matrice dei colori (saturazione) */
    int foglie; /* nodi non strutturali dentro il blur */
    int blur_dentro;
} Catena;

typedef struct {
    int n_blur, annidati, n_cat;
    Catena cat[8];
} Analisi;

static GskRenderNode *senza_contenitori(GskRenderNode *n)
{
    while (n != NULL && gsk_render_node_get_node_type(n) == GSK_CONTAINER_NODE &&
           gsk_container_node_get_n_children(n) == 1)
        n = gsk_container_node_get_child(n, 0);
    return n;
}

static void conta_dentro(GskRenderNode *n, int *foglie, int *blur)
{
    if (n == NULL)
        return;
    switch (gsk_render_node_get_node_type(n)) {
    case GSK_CONTAINER_NODE:
        for (guint k = 0; k < gsk_container_node_get_n_children(n); k++)
            conta_dentro(gsk_container_node_get_child(n, k), foglie, blur);
        break;
    case GSK_CLIP_NODE: conta_dentro(gsk_clip_node_get_child(n), foglie, blur); break;
    case GSK_ROUNDED_CLIP_NODE: conta_dentro(gsk_rounded_clip_node_get_child(n), foglie, blur); break;
    case GSK_TRANSFORM_NODE: conta_dentro(gsk_transform_node_get_child(n), foglie, blur); break;
    case GSK_OPACITY_NODE: conta_dentro(gsk_opacity_node_get_child(n), foglie, blur); break;
    case GSK_COLOR_MATRIX_NODE: conta_dentro(gsk_color_matrix_node_get_child(n), foglie, blur); break;
    case GSK_DEBUG_NODE: conta_dentro(gsk_debug_node_get_child(n), foglie, blur); break;
    case GSK_SHADOW_NODE: conta_dentro(gsk_shadow_node_get_child(n), foglie, blur); break;
    case GSK_BLUR_NODE:
        (*blur)++;
        conta_dentro(gsk_blur_node_get_child(n), foglie, blur);
        break;
    default: (*foglie)++; break;
    }
}

static void visita(GskRenderNode *n, int in_blur, Analisi *a)
{
    if (n == NULL)
        return;
    switch (gsk_render_node_get_node_type(n)) {
    case GSK_CONTAINER_NODE:
        for (guint k = 0; k < gsk_container_node_get_n_children(n); k++)
            visita(gsk_container_node_get_child(n, k), in_blur, a);
        break;
    case GSK_CLIP_NODE: visita(gsk_clip_node_get_child(n), in_blur, a); break;
    case GSK_ROUNDED_CLIP_NODE: {
        GskRenderNode *m = senza_contenitori(gsk_rounded_clip_node_get_child(n));
        if (m != NULL && gsk_render_node_get_node_type(m) == GSK_COLOR_MATRIX_NODE) {
            GskRenderNode *bl = senza_contenitori(gsk_color_matrix_node_get_child(m));
            if (bl != NULL && gsk_render_node_get_node_type(bl) == GSK_BLUR_NODE && a->n_cat < 8) {
                const GskRoundedRect *r = gsk_rounded_clip_node_get_clip(n);
                Catena *c = &a->cat[a->n_cat++];
                c->clip = r->bounds;
                c->angolo = r->corner[0].width;
                c->raggio = gsk_blur_node_get_radius(bl);
                const graphene_matrix_t *mc = gsk_color_matrix_node_get_color_matrix(m);
                c->sat00 = graphene_matrix_get_value(mc, 0, 0);
                c->sat22 = graphene_matrix_get_value(mc, 2, 2);
                c->foglie = 0;
                c->blur_dentro = 0;
                conta_dentro(gsk_blur_node_get_child(bl), &c->foglie, &c->blur_dentro);
            }
        }
        visita(gsk_rounded_clip_node_get_child(n), in_blur, a);
        break;
    }
    case GSK_BLUR_NODE:
        a->n_blur++;
        if (in_blur > 0)
            a->annidati++;
        visita(gsk_blur_node_get_child(n), in_blur + 1, a);
        break;
    case GSK_TRANSFORM_NODE: visita(gsk_transform_node_get_child(n), in_blur, a); break;
    case GSK_OPACITY_NODE: visita(gsk_opacity_node_get_child(n), in_blur, a); break;
    case GSK_COLOR_MATRIX_NODE: visita(gsk_color_matrix_node_get_child(n), in_blur, a); break;
    case GSK_DEBUG_NODE: visita(gsk_debug_node_get_child(n), in_blur, a); break;
    case GSK_SHADOW_NODE: visita(gsk_shadow_node_get_child(n), in_blur, a); break;
    default: break;
    }
}

/* Disegna la radice in uno snapshot di prova (la stessa funzione del disegno
 * vero) e percorre i nodi. Conta come un disegno in ph_vetro_radice_snapshot_conta:
 * chi misura i disegni veri non la chiama nel mezzo. */
static void analizza(GtkWidget *radice, Analisi *a)
{
    memset(a, 0, sizeof *a);
    GtkSnapshot *s = gtk_snapshot_new();
    ph_vetro_radice_snapshot(radice, s);
    GskRenderNode *n = gtk_snapshot_free_to_node(s);
    visita(n, 0, a);
    if (n != NULL)
        gsk_render_node_unref(n);
}

static graphene_rect_t dove(Banco *b, GtkWidget *w)
{
    graphene_rect_t r = GRAPHENE_RECT_INIT(0, 0, 0, 0);
    CONTROLLA(gtk_widget_compute_bounds(w, b->radice, &r));
    return r;
}

static bool uguale(const graphene_rect_t *a, const graphene_rect_t *b)
{
    return fabsf(a->origin.x - b->origin.x) < 0.01f && fabsf(a->origin.y - b->origin.y) < 0.01f &&
           fabsf(a->size.width - b->size.width) < 0.01f && fabsf(a->size.height - b->size.height) < 0.01f;
}

/* La catena col rettangolo r, o NULL. */
static const Catena *catena_in(const Analisi *a, const graphene_rect_t *r)
{
    for (int k = 0; k < a->n_cat; k++)
        if (uguale(&a->cat[k].clip, r))
            return &a->cat[k];
    return NULL;
}

/* Il widget w e' fra i ritratti dell'ultimo disegno? */
static bool ritratto_di(Banco *b, GtkWidget *w)
{
    for (unsigned k = 0; k < ph_vetro_radice_ritratti(b->radice); k++) {
        GdkPaintable *p = ph_vetro_radice_ritratto(b->radice, k);
        if (p != NULL && gtk_widget_paintable_get_widget(GTK_WIDGET_PAINTABLE(p)) == w)
            return true;
    }
    return false;
}

/* ---- 1: renderer e variabile --------------------------------------------------- */
static void prova_renderer(void)
{
    CONTROLLA(ph_shell_vetro_per_renderer("GskGLRenderer"));
    CONTROLLA(ph_shell_vetro_per_renderer("GskNglRenderer"));
    CONTROLLA(ph_shell_vetro_per_renderer("GskVulkanRenderer"));
    CONTROLLA(!ph_shell_vetro_per_renderer("GskCairoRenderer"));
    CONTROLLA(!ph_shell_vetro_per_renderer("GskBroadwayRenderer"));
    CONTROLLA(!ph_shell_vetro_per_renderer(NULL));
    CONTROLLA(!ph_shell_vetro_per_renderer(""));
    CONTROLLA(!ph_shell_vetro_per_renderer("GskQualcosa"));

    /* La variabile comanda; senza, Broadway (non su scheda grafica) e' spento. */
    const char *valori[] = {"1", "0", NULL};
    for (int k = 0; k < 3; k++) {
        Banco *b = banco_nuovo(valori[k]);
        bool atteso = valori[k] != NULL && strcmp(valori[k], "1") == 0;
        CONTROLLA(ph_shell_vetro_decidi(b->radice) == atteso);
        CONTROLLA(ph_shell_vetro_attivo(b->sh) == atteso);
        CONTROLLA(gtk_widget_has_css_class(b->radice, "ph-vetro-attivo") == atteso);
        banco_libera(b);
    }
}

/* ---- 2: registrazione ----------------------------------------------------------- */
static void prova_registrazione(void)
{
    Banco *b = banco_nuovo("1");
    GtkWidget *ch = scatola(b, PH_STRATO_PANNELLI, 10, 10, 50, 50);
    GtkWidget *sc = scatola(b, PH_STRATO_PANNELLI, 70, 10, 50, 50);
    GtkWidget *pr = scatola(b, PH_STRATO_FINESTRE, 130, 10, 50, 50);
    CONTROLLA(ph_vetro_radice_voci(b->radice) == 0);
    ph_shell_vetro_registra(b->sh, ch, 8, PH_VETRO_CHIARO);
    ph_shell_vetro_registra(b->sh, sc, 8, PH_VETRO_SCURO);
    CONTROLLA(gtk_widget_has_css_class(ch, "ph-vetro-chiaro") && !gtk_widget_has_css_class(ch, "ph-vetro-scuro"));
    CONTROLLA(gtk_widget_has_css_class(sc, "ph-vetro-scuro") && !gtk_widget_has_css_class(sc, "ph-vetro-chiaro"));
    CONTROLLA(ph_vetro_radice_voci(b->radice) == 2);
    /* Due volte non duplica. */
    ph_shell_vetro_registra(b->sh, ch, 8, PH_VETRO_CHIARO);
    CONTROLLA(ph_vetro_radice_voci(b->radice) == 2);
    /* "Proprio": una voce senza classi. */
    ph_shell_vetro_proprio(b->sh, pr);
    CONTROLLA(ph_vetro_radice_voci(b->radice) == 3);
    CONTROLLA(!gtk_widget_has_css_class(pr, "ph-vetro-chiaro") && !gtk_widget_has_css_class(pr, "ph-vetro-scuro"));
    /* Vale la prima registrazione: dopo "proprio" nessuna classe, e (sotto) chiaro poi scuro lascia chiaro. */
    ph_shell_vetro_registra(b->sh, pr, 8, PH_VETRO_SCURO);
    CONTROLLA(ph_vetro_radice_voci(b->radice) == 3);
    CONTROLLA(!gtk_widget_has_css_class(pr, "ph-vetro-chiaro") && !gtk_widget_has_css_class(pr, "ph-vetro-scuro"));
    ph_shell_vetro_registra(b->sh, ch, 8, PH_VETRO_SCURO);
    CONTROLLA(gtk_widget_has_css_class(ch, "ph-vetro-chiaro") && !gtk_widget_has_css_class(ch, "ph-vetro-scuro"));
    /* Togliere un non registrato e' innocuo; un registrato si toglie. */
    GtkWidget *altro = scatola(b, PH_STRATO_TOAST, 0, 0, 10, 10);
    g_object_ref(ch); /* riferimenti miei, oltre a quelli della radice */
    g_object_ref(sc);
    g_object_ref(pr);
    g_object_ref(altro);
    aspetta_alloc(ch); /* il disegno di prova vuole widget gia' disposti */
    aspetta_alloc(sc);
    aspetta_alloc(pr);
    aspetta_alloc(altro);
    ph_shell_vetro_togli(b->sh, altro);
    CONTROLLA(ph_vetro_radice_voci(b->radice) == 3);
    ph_shell_vetro_togli(b->sh, pr);
    CONTROLLA(ph_vetro_radice_voci(b->radice) == 2);
    ph_shell_vetro_togli(b->sh, pr); /* gia' tolto */
    CONTROLLA(ph_vetro_radice_voci(b->radice) == 2);

    /* Un widget registrato e distrutto senza _togli: nessun riferimento pendente. */
    ph_shell_strato_togli(b->sh, sc);
    g_object_unref(sc); /* l'ultimo riferimento: il widget muore */
    CONTROLLA(ph_vetro_radice_voci(b->radice) == 1);
    Analisi a;
    analizza(b->radice, &a); /* nessun errore (i messaggi di GTK sono fatali) */
    gtk_widget_queue_draw(b->radice);
    aspetta_frames(2);

    /* _togli dopo ph_shell_libera: innocuo (AddressSanitizer). */
    PhShell *sh = b->sh;
    ph_shell_libera(sh);
    ph_shell_vetro_togli(sh, ch);
    ph_shell_vetro_togli(sh, altro);
    g_object_unref(ch);
    g_object_unref(pr);
    g_object_unref(altro);
    gtk_window_destroy(GTK_WINDOW(b->win));
    ph_servizi_libera(b->s);
    pompa_poco();
    g_free(b);
}

/* ---- 3: albero dei nodi --------------------------------------------------------- */
static void prova_albero(void)
{
    Banco *b = banco_nuovo("1");
    GtkWidget *fondo = scatola(b, PH_STRATO_DESKTOP, 50, 50, 500, 500);
    GtkWidget *chiara = scatola(b, PH_STRATO_PANNELLI, 100, 100, 200, 100);
    GtkWidget *scura = scatola(b, PH_STRATO_BARRA, 400, 400, 200, 80);
    ph_shell_vetro_registra(b->sh, chiara, 12, PH_VETRO_CHIARO);
    ph_shell_vetro_registra(b->sh, scura, 20, PH_VETRO_SCURO);
    /* Una seconda registrazione, con altro tipo e altro raggio, non cambia nulla (il blur resta 52, raggio 12). */
    ph_shell_vetro_registra(b->sh, chiara, 30, PH_VETRO_SCURO);
    CONTROLLA(gtk_widget_has_css_class(chiara, "ph-vetro-chiaro") && !gtk_widget_has_css_class(chiara, "ph-vetro-scuro"));
    aspetta_alloc(chiara);
    aspetta_alloc(scura);
    assesta(b);
    Analisi a;
    analizza(b->radice, &a);
    graphene_rect_t rc = dove(b, chiara), rs = dove(b, scura);
    const Catena *cc = catena_in(&a, &rc), *cs = catena_in(&a, &rs);
    CONTROLLA(a.n_cat == 2 && a.n_blur == 2);
    CONTROLLA(cc != NULL && fabsf(cc->angolo - 12.0f) < 0.01f && fabsf(cc->raggio - 52.0f) < 0.01f);
    CONTROLLA(cs != NULL && fabsf(cs->angolo - 20.0f) < 0.01f && fabsf(cs->raggio - 40.0f) < 0.01f);
    /* Saturazione: 0.213 + 0.787 a e 0.072 + 0.928 a, con a = 1.6 (chiaro) e 1.4 (scuro). */
    CONTROLLA(cc != NULL && fabsf(cc->sat00 - (0.213f + 0.787f * 1.6f)) < 0.001f &&
              fabsf(cc->sat22 - (0.072f + 0.928f * 1.6f)) < 0.001f);
    CONTROLLA(cs != NULL && fabsf(cs->sat00 - (0.213f + 0.787f * 1.4f)) < 0.001f &&
              fabsf(cs->sat22 - (0.072f + 0.928f * 1.4f)) < 0.001f);
    CONTROLLA(cc != NULL && cc->foglie > 0); /* dietro c'e' davvero il fondo */
    CONTROLLA(cs != NULL && cs->foglie > 0);
    CONTROLLA(a.annidati == 0);
    CONTROLLA(ph_vetro_radice_disegni(b->radice) == 2);

    /* Una superficie non mappata non ha sfocatura. */
    gtk_widget_set_visible(scura, FALSE);
    CONTROLLA(!gtk_widget_get_mapped(scura));
    analizza(b->radice, &a);
    CONTROLLA(a.n_cat == 1 && a.n_blur == 1);
    CONTROLLA(catena_in(&a, &rc) != NULL && catena_in(&a, &rs) == NULL);
    CONTROLLA(ph_vetro_radice_disegni(b->radice) == 1);
    /* child_visible falso: non mappata ma ancora con la sua allocazione (il solo caso in cui
     * "non mappata" e "misura nulla" non coincidono): niente sfocatura. */
    gtk_widget_set_child_visible(chiara, FALSE);
    CONTROLLA(!gtk_widget_get_mapped(chiara) && gtk_widget_get_width(chiara) > 0);
    analizza(b->radice, &a);
    CONTROLLA(a.n_cat == 0 && a.n_blur == 0);
    CONTROLLA(ph_vetro_radice_disegni(b->radice) == 0);
    gtk_widget_set_child_visible(chiara, TRUE);
    (void)fondo;
    banco_libera(b);

    /* Vetro spento: nessun blur, nessuna sfocatura contata. */
    b = banco_nuovo("0");
    fondo = scatola(b, PH_STRATO_DESKTOP, 50, 50, 500, 500);
    chiara = scatola(b, PH_STRATO_PANNELLI, 100, 100, 200, 100);
    ph_shell_vetro_registra(b->sh, chiara, 12, PH_VETRO_CHIARO);
    aspetta_alloc(chiara);
    analizza(b->radice, &a);
    CONTROLLA(a.n_blur == 0 && a.n_cat == 0);
    CONTROLLA(ph_vetro_radice_disegni(b->radice) == 0);
    banco_libera(b);
}

/* ---- 4 e 5: niente annidate, solo cio' che sta sotto ---------------------------- */
static GtkWidget *colorata(int w, int h)
{
    GtkWidget *r = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(r, "prova-colore");
    gtk_widget_set_size_request(r, w, h);
    return r;
}

static void prova_annidate_e_sotto(void)
{
    Banco *b = banco_nuovo("1");
    GtkWidget *A = scatola(b, PH_STRATO_DESKTOP, 60, 60, 300, 200);      /* superficie registrata, strato basso */
    GtkWidget *C = scatola(b, PH_STRATO_DESKTOP, 80, 80, 400, 300);      /* un riquadro normale */
    GtkWidget *P = scatola(b, PH_STRATO_FINESTRE, 90, 90, 300, 200);     /* "proprio", con un figlio */
    GtkWidget *Q = colorata(100, 100);
    gtk_box_append(GTK_BOX(P), Q);
    /* K1 contiene una superficie registrata (Ks): K1 si ritrae INTERO, Ks compresa. */
    GtkWidget *K1 = scatola(b, PH_STRATO_FINESTRE, 250, 60, 200, 100);
    GtkWidget *Ks = colorata(150, 100);
    gtk_box_append(GTK_BOX(K1), Ks);
    /* K2 contiene INSIEME una superficie registrata (Kr), un "proprio" (Kpr, con un figlio Kg) e un
     * riquadro normale (Kp): si ritrae dai figli, ma Kr (non "propria") resta intera. */
    GtkWidget *K2 = scatola(b, PH_STRATO_FINESTRE, 250, 170, 200, 200);
    GtkWidget *Kr = colorata(100, 40);
    GtkWidget *Kpr = colorata(150, 60);
    GtkWidget *Kg = colorata(80, 40);
    GtkWidget *Kp = colorata(150, 60);
    gtk_box_append(GTK_BOX(Kpr), Kg);
    gtk_box_append(GTK_BOX(K2), Kr);
    gtk_box_append(GTK_BOX(K2), Kpr);
    gtk_box_append(GTK_BOX(K2), Kp);
    GtkWidget *B = scatola(b, PH_STRATO_PANNELLI, 120, 120, 200, 100);   /* superficie sopra */
    GtkWidget *U = scatola(b, PH_STRATO_TOAST, 130, 130, 100, 100);      /* sopra B */
    ph_shell_vetro_registra(b->sh, A, 10, PH_VETRO_CHIARO);
    ph_shell_vetro_proprio(b->sh, P);
    ph_shell_vetro_proprio(b->sh, Kpr);
    ph_shell_vetro_registra(b->sh, Ks, 10, PH_VETRO_SCURO);
    ph_shell_vetro_registra(b->sh, Kr, 10, PH_VETRO_CHIARO);
    ph_shell_vetro_registra(b->sh, B, 10, PH_VETRO_SCURO);
    aspetta_alloc(Kr);
    aspetta_alloc(A);
    aspetta_alloc(B);
    aspetta_alloc(Q);
    aspetta_alloc(Kp);
    aspetta_alloc(Kg);
    aspetta_alloc(Ks);
    assesta(b);
    Analisi a;
    analizza(b->radice, &a);
    graphene_rect_t rb = dove(b, B);
    const Catena *cb = catena_in(&a, &rb);
    /* A (senza nulla sotto: vuota, nessun nodo), Ks e B hanno sfocatura; nessun blur dentro un altro. */
    CONTROLLA(cb != NULL && cb->foglie > 0 && cb->blur_dentro == 0);
    graphene_rect_t rk = dove(b, Ks);
    const Catena *ck = catena_in(&a, &rk);
    CONTROLLA(ck != NULL && ck->foglie > 0 && ck->blur_dentro == 0);
    graphene_rect_t rr = dove(b, Kr);
    const Catena *cr = catena_in(&a, &rr);
    CONTROLLA(cr != NULL && cr->foglie > 0 && cr->blur_dentro == 0);
    CONTROLLA(a.n_blur == 3 && a.annidati == 0);
    CONTROLLA(ph_vetro_radice_disegni(b->radice) == 4);
    /* Ritratti INTERI: C, A (una superficie registrata dietro un'altra), K1 (contiene una superficie).
     * Dai figli: Q (figlio del "proprio" P), Kg (figlio del "proprio" Kpr), Kp (figlio normale di K2, che
     * contiene un "proprio"). Kr (registrata dentro K2, accanto a un "proprio") e' ritratta INTERA.
     * Mai: P, Kpr, K2, B, U (sopra), Ks da sola (sta dentro K1). */
    CONTROLLA(ritratto_di(b, C));
    CONTROLLA(ritratto_di(b, A));
    CONTROLLA(ritratto_di(b, K1));
    CONTROLLA(ritratto_di(b, Q));
    CONTROLLA(ritratto_di(b, Kg));
    CONTROLLA(ritratto_di(b, Kp));
    CONTROLLA(ritratto_di(b, Kr));
    CONTROLLA(!ritratto_di(b, P));
    CONTROLLA(!ritratto_di(b, Kpr));
    CONTROLLA(!ritratto_di(b, K2));
    CONTROLLA(!ritratto_di(b, Ks));
    CONTROLLA(!ritratto_di(b, B));
    CONTROLLA(!ritratto_di(b, U));
    CONTROLLA(ph_vetro_radice_ritratti(b->radice) == 7);
    banco_libera(b);

    /* Una superficie nello strato piu' basso, senza nulla sotto: sfocatura vuota, nessun errore. */
    b = banco_nuovo("1");
    A = scatola(b, PH_STRATO_DESKTOP, 60, 60, 300, 200);
    ph_shell_vetro_registra(b->sh, A, 10, PH_VETRO_CHIARO);
    aspetta_alloc(A);
    analizza(b->radice, &a);
    CONTROLLA(a.n_cat == 0 && a.n_blur == 0);
    CONTROLLA(ph_vetro_radice_disegni(b->radice) == 1);
    CONTROLLA(ph_vetro_radice_ritratti(b->radice) == 0);
    banco_libera(b);
}

/* ---- 6: aggiornamento ----------------------------------------------------------- */
typedef struct {
    GtkWidget *radice;
    unsigned oltre;
} AttesaConta;

static bool cond_conta_sale(void *u)
{
    AttesaConta *a = u;
    return ph_vetro_radice_snapshot_conta(a->radice) > a->oltre;
}

static bool disegna_di_nuovo(GtkWidget *radice, unsigned prima)
{
    AttesaConta a = {radice, prima};
    return aspetta(cond_conta_sale, &a, SCAD_MS);
}

static bool cond_non_mappato(void *u) { return !gtk_widget_get_mapped(u); }

typedef struct {
    Banco *b;
    GtkWidget *w;
    float y;
} AttesaY;

static bool cond_y(void *u)
{
    AttesaY *a = u;
    graphene_rect_t r;
    return gtk_widget_get_mapped(a->w) && gtk_widget_compute_bounds(a->w, a->b->radice, &r) &&
           fabsf(r.origin.y - a->y) < 0.01f;
}

static void prova_aggiornamento(void)
{
    Banco *b = banco_nuovo("1");
    GtkWidget *D = scatola(b, PH_STRATO_DESKTOP, 50, 50, 500, 500);
    GtkWidget *E = scatola(b, PH_STRATO_DESKTOP, 110, 110, 40, 40);
    GtkWidget *S = scatola(b, PH_STRATO_PANNELLI, 100, 100, 200, 100);
    ph_shell_vetro_registra(b->sh, S, 12, PH_VETRO_CHIARO);
    aspetta_alloc(S);
    aspetta_alloc(E);
    assesta(b);
    Analisi a;

    /* (i) un widget sotto si ridisegna: la radice si ridisegna. */
    unsigned n0 = ph_vetro_radice_snapshot_conta(b->radice);
    gtk_widget_queue_draw(D);
    CONTROLLA(disegna_di_nuovo(b->radice, n0));
    assesta(b);
    /* (i-bis) il ritratto avvisa da solo (senza che nessun widget si ridisegni). */
    analizza(b->radice, &a);
    GdkPaintable *p = ph_vetro_radice_ritratto(b->radice, 0);
    CONTROLLA(p != NULL);
    assesta(b);
    n0 = ph_vetro_radice_snapshot_conta(b->radice);
    if (p != NULL)
        gdk_paintable_invalidate_contents(p);
    CONTROLLA(disegna_di_nuovo(b->radice, n0));
    assesta(b);

    /* (ii) la superficie si sposta: il clip e' quello nuovo. */
    graphene_rect_t prima = dove(b, S);
    n0 = ph_vetro_radice_snapshot_conta(b->radice);
    gtk_widget_set_margin_top(S, 220);
    AttesaY ay = {b, S, 220.0f};
    CONTROLLA(aspetta(cond_y, &ay, SCAD_MS));
    CONTROLLA(disegna_di_nuovo(b->radice, n0));
    assesta(b);
    graphene_rect_t dopo = dove(b, S);
    CONTROLLA(!uguale(&prima, &dopo));
    analizza(b->radice, &a);
    CONTROLLA(a.n_cat == 1 && catena_in(&a, &dopo) != NULL && catena_in(&a, &prima) == NULL);

    /* (iii) nascondi e mostra. */
    n0 = ph_vetro_radice_snapshot_conta(b->radice);
    gtk_widget_set_visible(S, FALSE);
    CONTROLLA(aspetta(cond_non_mappato, S, SCAD_MS));
    CONTROLLA(disegna_di_nuovo(b->radice, n0));
    assesta(b);
    analizza(b->radice, &a);
    CONTROLLA(a.n_blur == 0);
    n0 = ph_vetro_radice_snapshot_conta(b->radice);
    gtk_widget_set_visible(S, TRUE);
    aspetta_alloc(S);
    CONTROLLA(disegna_di_nuovo(b->radice, n0));
    assesta(b);
    analizza(b->radice, &a);
    dopo = dove(b, S);
    CONTROLLA(a.n_cat == 1 && catena_in(&a, &dopo) != NULL);

    /* (iv) un widget ritratto: dentro 3 sigma (78 px) dalla superficie resta, oltre esce. S sta in x 100..300. */
    n0 = ph_vetro_radice_snapshot_conta(b->radice);
    gtk_widget_set_margin_start(E, 360); /* a 60 px dal bordo destro: dentro */
    gtk_widget_set_margin_top(E, 240);
    AttesaY ae = {b, E, 240.0f};
    CONTROLLA(aspetta(cond_y, &ae, SCAD_MS));
    CONTROLLA(disegna_di_nuovo(b->radice, n0));
    assesta(b);
    analizza(b->radice, &a);
    CONTROLLA(ritratto_di(b, E));
    n0 = ph_vetro_radice_snapshot_conta(b->radice);
    gtk_widget_set_margin_start(E, 410); /* a 110 px: fuori */
    gtk_widget_set_margin_top(E, 250);
    AttesaY af = {b, E, 250.0f};
    CONTROLLA(aspetta(cond_y, &af, SCAD_MS));
    CONTROLLA(disegna_di_nuovo(b->radice, n0));
    assesta(b);
    analizza(b->radice, &a);
    CONTROLLA(!ritratto_di(b, E));
    CONTROLLA(ritratto_di(b, D));
    CONTROLLA(ph_vetro_radice_ritratti(b->radice) == 1);
    banco_libera(b);
}

/* ---- 7: niente ridisegni a riposo ------------------------------------------------ */
static void prova_riposo(void)
{
    Banco *b = banco_nuovo("1");
    GtkWidget *D = scatola(b, PH_STRATO_DESKTOP, 50, 50, 500, 500);
    GtkWidget *S = scatola(b, PH_STRATO_PANNELLI, 100, 100, 200, 100);
    ph_shell_vetro_registra(b->sh, S, 12, PH_VETRO_CHIARO);
    aspetta_alloc(S);
    aspetta_alloc(D);
    assesta(b);
    unsigned conta0 = ph_vetro_radice_snapshot_conta(b->radice);
    unsigned f0 = frames;
    aspetta_frames(RIPOSO_F);
    CONTROLLA(frames - f0 >= RIPOSO_F); /* i fotogrammi girano davvero */
    CONTROLLA(ph_vetro_radice_snapshot_conta(b->radice) == conta0);
    banco_libera(b);
}

typedef struct {
    GdkFrameClock *clock;
    gint64 da;
} CicloClock;

static bool cond_clock(void *u)
{
    CicloClock *c = u;
    gdk_frame_clock_request_phase(c->clock, GDK_FRAME_CLOCK_PHASE_UPDATE);
    return gdk_frame_clock_get_frame_counter(c->clock) >= c->da + 3;
}

/* ---- 8: ciclo di vita ------------------------------------------------------------ */
static void prova_ciclo_di_vita(void)
{
    Banco *b = banco_nuovo("1");
    GtkWidget *D = scatola(b, PH_STRATO_DESKTOP, 50, 50, 500, 500);
    GtkWidget *E = scatola(b, PH_STRATO_DESKTOP, 110, 110, 40, 40);
    GtkWidget *S = scatola(b, PH_STRATO_PANNELLI, 100, 100, 200, 100);
    ph_shell_vetro_registra(b->sh, S, 12, PH_VETRO_CHIARO);
    aspetta_alloc(S);
    aspetta_alloc(E);
    assesta(b);
    Analisi a;
    analizza(b->radice, &a);
    unsigned n = ph_vetro_radice_ritratti(b->radice);
    CONTROLLA(n == 2);
    guint sig = g_signal_lookup("invalidate-contents", GDK_TYPE_PAINTABLE);
    GPtrArray *tenuti = g_ptr_array_new_with_free_func(g_object_unref);
    for (unsigned k = 0; k < n; k++) {
        GdkPaintable *p = ph_vetro_radice_ritratto(b->radice, k);
        CONTROLLA(p != NULL);
        if (p == NULL)
            continue;
        g_ptr_array_add(tenuti, g_object_ref(p));
        CONTROLLA(g_signal_handler_find(p, G_SIGNAL_MATCH_ID, sig, 0, NULL, NULL, NULL) != 0);
    }
    GdkFrameClock *clock = g_object_ref(gtk_widget_get_frame_clock(b->radice));
    ph_shell_libera(b->sh);
    /* Nessun gestore resta sui paintable che la prova tiene vivi... */
    for (guint k = 0; k < tenuti->len; k++)
        CONTROLLA(g_signal_handler_find(g_ptr_array_index(tenuti, k), G_SIGNAL_MATCH_ID, sig, 0, NULL, NULL,
                                        NULL) == 0);
    /* ...quindi un avviso di un ritratto non raggiunge piu' la radice (liberata: AddressSanitizer). */
    for (guint k = 0; k < tenuti->len; k++)
        gdk_paintable_invalidate_contents(g_ptr_array_index(tenuti, k));
    /* La finestra e il suo frame clock vivono ancora: qualche fotogramma senza errori (conteggio con scadenza). */
    CicloClock cc = {clock, gdk_frame_clock_get_frame_counter(clock)};
    CONTROLLA(aspetta(cond_clock, &cc, SCAD_MS));
    g_object_unref(clock);
    g_ptr_array_unref(tenuti);
    (void)D;
    gtk_window_destroy(GTK_WINDOW(b->win));
    ph_servizi_libera(b->s);
    pompa_poco();
    g_free(b);
}

int main(void)
{
    if (!gtk_init_check()) {
        fprintf(stderr, "nessuno schermo: prova saltata\n");
        return 77;
    }
    /* Nessun avviso di GTK deve comparire. */
    g_log_set_always_fatal((GLogLevelFlags)(G_LOG_FATAL_MASK | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL));
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, ".prova-colore { background-color: #cc3322; }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    prova_renderer();
    prova_registrazione();
    prova_albero();
    prova_annidate_e_sotto();
    prova_aggiornamento();
    prova_riposo();
    prova_ciclo_di_vita();

    g_object_unref(css);
    if (errori == 0)
        printf("prova_vetro: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
