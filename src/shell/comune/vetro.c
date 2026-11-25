/* Vetro delle superfici della shell (shell.h, "Vetro"): barra, launcher,
 * pannelli, menu, dialoghi, avvisi, widget del desktop.
 *
 * GTK non ha backdrop-filter. Una superficie di vetro e' un widget qualunque
 * (di solito un GtkBox con le classi di base.css) che un pezzo REGISTRA qui;
 * a disegnare cio' che gli sta dietro, sfocato, ci pensa la radice a strati:
 * prima di disegnare il figlio della radice (lo "strato") che contiene una
 * superficie registrata, ridisegna dentro il rettangolo arrotondato della
 * superficie i figli della radice che vengono PRIMA, sfocati e piu' saturi.
 * Di ognuno usa un GtkWidgetPaintable, che riusa il disegno gia' fatto del
 * widget (nessun widget viene ridisegnato dal suo codice) e avvisa quando
 * cambia: allora si ridisegna la radice (solo lei: i figli riusano il loro
 * disegno). Il velo colorato, il bordo e l'ombra sono CSS della superficie
 * (classi ph-vetro-chiaro / ph-vetro-scuro, base.css).
 *
 * Sfocature mai annidate. Una superficie REGISTRATA si ritrae intera, come
 * ogni altro widget: la sua sfocatura la disegna la radice, fuori dal disegno
 * del widget, quindi nel suo ritratto non c'e' (ci sono invece il suo velo, il
 * bordo e l'ombra: dietro un menu il launcher resta chiaro, come con un vero
 * backdrop-filter). Diverso un widget che la sfocatura se la disegna DA SE'
 * (la cornice di una finestra interna, che si segna con
 * ph_shell_vetro_proprio): quello non si ritrae intero ma dai suoi FIGLI, e
 * chi lo contiene si ritrae scendendo fino a lui. Sfocare una cosa gia'
 * sfocata non si vede, e il costo raddoppierebbe a ogni livello (misurato con
 * le finestre in cascata).
 *
 * Una superficie che si sposta, compare o sparisce: non serve accorgersene
 * qui, GTK ridisegna gia' la radice (gtk_widget_queue_draw e la ridisposizione
 * di un discendente risalgono fino a lei) e lo snapshot ricalcola tutto.
 *
 * Limiti noti: (1) dietro una superficie si vede solo cio' che sta negli
 * strati SOTTO il suo, non cio' che le sta sotto nello stesso strato (un'icona
 * sotto un widget del desktop resta nitida); (2) se la scheda grafica non
 * disegna (vedi ph_shell_vetro_per_renderer) non c'e' nessuna sfocatura e il
 * CSS da' un fondo quasi opaco: mai un velo trasparente senza sfocatura.
 * Solo thread GTK. */
#include <string.h>

#include "interno.h"
#include "shell/shell.h"

#define CHIAVE_VETRO "ph-vetro-radice"
#define CHIAVE_SEGNALE "ph-vetro-segnale"

/* I valori di docs/DESIGN_FINESTRE.md e dell'esempio del committente:
 * vetro chiaro blur(26px) saturate(1.6), vetro scuro blur(20px) saturate(1.4). */
#define SIGMA_CHIARO 26.0
#define SATURAZIONE_CHIARO 1.6f
#define SIGMA_SCURO 20.0
#define SATURAZIONE_SCURO 1.4f

typedef struct {
    GtkWidget *w;            /* non posseduto: riferimento debole (voce_morta) */
    int raggio;
    PhShellVetro tipo;
    bool proprio;            /* ha una sfocatura sua: qui serve solo a ritrarlo dai figli */
} Voce;

typedef struct {
    GtkWidget *radice;       /* non posseduto: questo stato muore con lei */
    GArray *voci;            /* di Voce */
    GPtrArray *dipinti;      /* GdkPaintable * dei widget ritratti */
    bool attivo;             /* deciso alla realizzazione della radice */
    unsigned n_disegni;      /* quante sfocature nell'ultimo disegno (per le prove) */
    unsigned n_snapshot;     /* quanti disegni della radice (solo per le prove) */
} Vetro;

static Vetro *vetro_di(GtkWidget *radice)
{
    return radice != NULL ? g_object_get_data(G_OBJECT(radice), CHIAVE_VETRO) : NULL;
}

bool ph_shell_vetro_per_renderer(const char *nome_tipo)
{
    return nome_tipo != NULL &&
           (strcmp(nome_tipo, "GskGLRenderer") == 0 || strcmp(nome_tipo, "GskNglRenderer") == 0 ||
            strcmp(nome_tipo, "GskVulkanRenderer") == 0);
}

bool ph_shell_vetro_decidi(GtkWidget *w)
{
    const char *v = g_getenv("YASDE_SFOCATURA");
    if (v != NULL && v[0] != '\0')
        return strcmp(v, "0") != 0;
    GtkNative *nat = w != NULL ? gtk_widget_get_native(w) : NULL;
    GskRenderer *r = nat != NULL ? gtk_native_get_renderer(nat) : NULL;
    return ph_shell_vetro_per_renderer(r != NULL ? G_OBJECT_TYPE_NAME(r) : NULL);
}

/* ---- ritratti ----------------------------------------------------------- */

static void ritratto_cambiato(GdkPaintable *p, gpointer radice)
{
    (void)p;
    gtk_widget_queue_draw(GTK_WIDGET(radice));
}

static void ritratto_libera(gpointer p)
{
    /* Se qualcun altro tenesse un riferimento al paintable, senza staccare il
     * segnale questo chiamerebbe una radice morta. */
    gulong id = (gulong)GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(p), CHIAVE_SEGNALE));
    if (id != 0)
        g_signal_handler_disconnect(p, id);
    g_object_unref(p);
}

/* Il ritratto di x, portato fra i primi `*usati` (quelli che servono a questo
 * disegno: gli altri si tolgono alla fine). */
static GdkPaintable *ritratto(Vetro *v, GtkWidget *x, guint *usati)
{
    guint dove = v->dipinti->len;
    for (guint k = 0; k < v->dipinti->len; k++) {
        if (gtk_widget_paintable_get_widget(GTK_WIDGET_PAINTABLE(g_ptr_array_index(v->dipinti, k))) == x) {
            dove = k;
            break;
        }
    }
    if (dove == v->dipinti->len) {
        GdkPaintable *p = gtk_widget_paintable_new(x);
        gulong id = g_signal_connect(p, "invalidate-contents", G_CALLBACK(ritratto_cambiato), v->radice);
        g_object_set_data(G_OBJECT(p), CHIAVE_SEGNALE, GSIZE_TO_POINTER((gsize)id));
        g_ptr_array_add(v->dipinti, p);
    }
    /* Uno gia' fra i primi (lo stesso widget dietro due superfici) resta dov'e'. */
    if (dove > *usati) {
        gpointer t = v->dipinti->pdata[*usati];
        v->dipinti->pdata[*usati] = v->dipinti->pdata[dove];
        v->dipinti->pdata[dove] = t;
        dove = *usati;
    }
    if (dove == *usati)
        (*usati)++;
    return g_ptr_array_index(v->dipinti, dove);
}

/* x e' una voce (registrata o "propria")? */
static bool e_voce(const Vetro *v, GtkWidget *x)
{
    for (guint k = 0; k < v->voci->len; k++)
        if (g_array_index(v->voci, Voce, k).w == x)
            return true;
    return false;
}

/* x si disegna da se' la sua sfocatura (ph_shell_vetro_proprio)? */
static bool e_propria(const Vetro *v, GtkWidget *x)
{
    for (guint k = 0; k < v->voci->len; k++) {
        const Voce *e = &g_array_index(v->voci, Voce, k);
        if (e->w == x)
            return e->proprio;
    }
    return false;
}

/* Dentro x (lui escluso) c'e' un widget con una sfocatura sua? */
static bool propria_dentro(const Vetro *v, GtkWidget *x)
{
    for (guint k = 0; k < v->voci->len; k++) {
        const Voce *e = &g_array_index(v->voci, Voce, k);
        if (e->proprio && e->w != x && gtk_widget_is_ancestor(e->w, x))
            return true;
    }
    return false;
}

/* Mette nel disegno x (che sta sotto la superficie), se tocca `attorno`
 * (coordinate della radice). */
static void metti(Vetro *v, GtkSnapshot *s, GtkWidget *x, const graphene_rect_t *attorno, guint *usati)
{
    graphene_rect_t b;
    if (!gtk_widget_get_mapped(x) || !gtk_widget_compute_bounds(x, v->radice, &b) ||
        !graphene_rect_intersection(&b, attorno, NULL))
        return;
    bool propria = e_propria(v, x);
    if (propria || propria_dentro(v, x)) {
        /* Ha (o contiene) una sfocatura sua: dai figli, vedi in testa al file.
         * Di chi si sfoca da se' si tiene il ritaglio sul suo rettangolo (la
         * cornice di una finestra ritaglia il contenuto); gli angoli tondi,
         * sfocati, non si distinguono. */
        if (propria)
            gtk_snapshot_push_clip(s, &b);
        for (GtkWidget *f = gtk_widget_get_first_child(x); f != NULL; f = gtk_widget_get_next_sibling(f))
            metti(v, s, f, attorno, usati);
        if (propria)
            gtk_snapshot_pop(s);
        return;
    }
    GdkPaintable *p = ritratto(v, x, usati);
    gtk_snapshot_save(s);
    gtk_snapshot_translate(s, &b.origin);
    gdk_paintable_snapshot(p, s, b.size.width, b.size.height);
    gtk_snapshot_restore(s);
}

/* Dietro la superficie e (che sta nello strato `strato`, in `dove`): gli
 * strati che vengono prima, sfocati. */
static void dietro(Vetro *v, GtkSnapshot *s, const Voce *e, GtkWidget *strato, const graphene_rect_t *dove,
                   guint *usati)
{
    const double sigma = e->tipo == PH_VETRO_SCURO ? SIGMA_SCURO : SIGMA_CHIARO;
    const float a = e->tipo == PH_VETRO_SCURO ? SATURAZIONE_SCURO : SATURAZIONE_CHIARO;
    /* La sfocatura porta dentro la superficie anche cio' che le sta attorno. */
    graphene_rect_t attorno = *dove;
    graphene_rect_inset(&attorno, (float)(-3.0 * sigma), (float)(-3.0 * sigma));

    GskRoundedRect forma;
    gsk_rounded_rect_init_from_rect(&forma, dove, (float)e->raggio);
    /* Come GTK per filter: saturate(). */
    graphene_matrix_t colori;
    graphene_vec4_t zero;
    graphene_matrix_init_from_float(&colori, (float[16]){
        0.213f + 0.787f * a, 0.213f - 0.213f * a, 0.213f - 0.213f * a, 0.0f,
        0.715f - 0.715f * a, 0.715f + 0.285f * a, 0.715f - 0.715f * a, 0.0f,
        0.072f - 0.072f * a, 0.072f - 0.072f * a, 0.072f + 0.928f * a, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f});
    graphene_vec4_init(&zero, 0.0f, 0.0f, 0.0f, 0.0f);

    gtk_snapshot_push_rounded_clip(s, &forma);
    gtk_snapshot_push_color_matrix(s, &colori, &zero);
    /* Il raggio di GSK e' il doppio della deviazione (come fa GTK per blur()). */
    gtk_snapshot_push_blur(s, 2.0 * sigma);
    for (GtkWidget *x = gtk_widget_get_first_child(v->radice); x != NULL && x != strato;
         x = gtk_widget_get_next_sibling(x))
        metti(v, s, x, &attorno, usati);
    gtk_snapshot_pop(s);
    gtk_snapshot_pop(s);
    gtk_snapshot_pop(s);
    v->n_disegni++;
}

void ph_vetro_radice_snapshot(GtkWidget *radice, GtkSnapshot *s)
{
    Vetro *v = vetro_di(radice);
    guint usati = 0;
    if (v != NULL) {
        v->n_snapshot++;
        v->n_disegni = 0;
    }
    for (GtkWidget *strato = gtk_widget_get_first_child(radice); strato != NULL;
         strato = gtk_widget_get_next_sibling(strato)) {
        for (guint k = 0; v != NULL && v->attivo && k < v->voci->len; k++) {
            const Voce *e = &g_array_index(v->voci, Voce, k);
            graphene_rect_t b;
            if (e->proprio || !(e->w == strato || gtk_widget_is_ancestor(e->w, strato)) ||
                !gtk_widget_get_mapped(e->w) || !gtk_widget_compute_bounds(e->w, radice, &b) ||
                b.size.width <= 0.0f || b.size.height <= 0.0f)
                continue;
            dietro(v, s, e, strato, &b, &usati);
        }
        gtk_widget_snapshot_child(radice, strato, s);
    }
    /* Chi non sta piu' dietro nessuna superficie non deve piu' far ridisegnare
     * la radice. */
    if (v != NULL && v->dipinti->len > usati)
        g_ptr_array_remove_range(v->dipinti, usati, v->dipinti->len - usati);
}

static void radice_realizzata(GtkWidget *radice, gpointer dati)
{
    Vetro *v = dati;
    v->attivo = ph_shell_vetro_decidi(radice);
    if (v->attivo)
        gtk_widget_add_css_class(radice, "ph-vetro-attivo");
    else
        gtk_widget_remove_css_class(radice, "ph-vetro-attivo");
}

void ph_vetro_radice_inizia(GtkWidget *radice)
{
    Vetro *v = g_new0(Vetro, 1);
    v->radice = radice;
    v->voci = g_array_new(FALSE, TRUE, sizeof(Voce));
    v->dipinti = g_ptr_array_new_with_free_func(ritratto_libera);
    g_object_set_data(G_OBJECT(radice), CHIAVE_VETRO, v);
    g_signal_connect(radice, "realize", G_CALLBACK(radice_realizzata), v);
}

/* Un widget registrato e' stato distrutto senza ph_shell_vetro_togli. */
static void voce_morta(gpointer dati, GObject *morto)
{
    Vetro *v = dati;
    for (guint k = 0; k < v->voci->len; k++) {
        if ((GObject *)g_array_index(v->voci, Voce, k).w == morto) {
            g_array_remove_index(v->voci, k);
            break;
        }
    }
    gtk_widget_queue_draw(v->radice);
}

void ph_vetro_radice_finisci(GtkWidget *radice)
{
    Vetro *v = vetro_di(radice);
    if (v == NULL)
        return;
    g_object_set_data(G_OBJECT(radice), CHIAVE_VETRO, NULL);
    g_signal_handlers_disconnect_by_data(radice, v);
    for (guint k = 0; k < v->voci->len; k++)
        g_object_weak_unref(G_OBJECT(g_array_index(v->voci, Voce, k).w), voce_morta, v);
    g_array_unref(v->voci);
    g_ptr_array_unref(v->dipinti);
    g_free(v);
}

unsigned ph_vetro_radice_disegni(GtkWidget *radice)
{
    Vetro *v = vetro_di(radice);
    return v != NULL ? v->n_disegni : 0;
}

unsigned ph_vetro_radice_ritratti(GtkWidget *radice)
{
    Vetro *v = vetro_di(radice);
    return v != NULL ? v->dipinti->len : 0;
}

/* Solo per le prove: quanti disegni ha fatto la radice, quante voci ha, il
 * ritratto k-esimo dell'ultimo disegno (non posseduto; NULL se k e' fuori). */
unsigned ph_vetro_radice_snapshot_conta(GtkWidget *radice)
{
    Vetro *v = vetro_di(radice);
    return v != NULL ? v->n_snapshot : 0;
}

unsigned ph_vetro_radice_voci(GtkWidget *radice)
{
    Vetro *v = vetro_di(radice);
    return v != NULL ? v->voci->len : 0;
}

GdkPaintable *ph_vetro_radice_ritratto(GtkWidget *radice, unsigned k)
{
    Vetro *v = vetro_di(radice);
    return v != NULL && k < v->dipinti->len ? g_ptr_array_index(v->dipinti, k) : NULL;
}

/* ---- contratto (shell.h) ------------------------------------------------ */

/* true se w e' stato aggiunto ora (false: c'era gia', o la radice sta morendo). */
static bool aggiungi(PhShell *sh, GtkWidget *w, int raggio, PhShellVetro tipo, bool proprio)
{
    g_return_val_if_fail(sh != NULL && GTK_IS_WIDGET(w), false);
    Vetro *v = vetro_di(ph_shell_radice(sh));
    if (v == NULL || e_voce(v, w))
        return false;
    Voce e = {.w = w, .raggio = raggio < 0 ? 0 : raggio, .tipo = tipo, .proprio = proprio};
    g_array_append_val(v->voci, e);
    g_object_weak_ref(G_OBJECT(w), voce_morta, v);
    gtk_widget_queue_draw(v->radice);
    return true;
}

void ph_shell_vetro_registra(PhShell *sh, GtkWidget *w, int raggio, PhShellVetro tipo)
{
    g_return_if_fail(sh != NULL && GTK_IS_WIDGET(w));
    /* La classe solo a chi entra davvero nel registro: un widget gia'
     * registrato (anche con l'altro tipo, o come "proprio") resta com'e'. */
    if (aggiungi(sh, w, raggio, tipo, false))
        gtk_widget_add_css_class(w, tipo == PH_VETRO_SCURO ? "ph-vetro-scuro" : "ph-vetro-chiaro");
}

void ph_shell_vetro_proprio(PhShell *sh, GtkWidget *w)
{
    (void)aggiungi(sh, w, 0, PH_VETRO_CHIARO, true);
}

void ph_shell_vetro_togli(PhShell *sh, GtkWidget *w)
{
    g_return_if_fail(sh != NULL && w != NULL);
    /* Chiamata arrivata dopo ph_shell_libera (dal "destroy" di un pezzo che
     * muore tardi): la shell non c'e' piu', non c'e' nulla da togliere. */
    if (!ph_vivi_e(sh))
        return;
    Vetro *v = vetro_di(ph_shell_radice(sh));
    if (v == NULL)
        return;
    for (guint k = 0; k < v->voci->len; k++) {
        if (g_array_index(v->voci, Voce, k).w == w) {
            g_array_remove_index(v->voci, k);
            g_object_weak_unref(G_OBJECT(w), voce_morta, v);
            gtk_widget_queue_draw(v->radice);
            return;
        }
    }
}

bool ph_shell_vetro_attivo(PhShell *sh)
{
    Vetro *v = sh != NULL ? vetro_di(ph_shell_radice(sh)) : NULL;
    return v != NULL && v->attivo;
}
