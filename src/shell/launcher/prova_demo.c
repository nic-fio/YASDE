/* Solo per il demo (foto a schermo invisibile, senza mouse ne' tastiera): le
 * opzioni --cerca e --menu aprono la ricerca e il menu del launcher. Non fa
 * parte della libreria di produzione: si compila solo con demo.c. */
#include <stdio.h>
#include <string.h>

#include "launcher_interno.h"
#include "logica.h"
#include "prova_demo.h"

/* Il primo widget dell'albero (in profondita') per cui `f` torna vero. */
typedef gboolean (*Cerca)(GtkWidget *w);

static GtkWidget *trova(GtkWidget *w, Cerca f)
{
    if (f(w))
        return w;
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        GtkWidget *r = trova(c, f);
        if (r != NULL)
            return r;
    }
    return NULL;
}

static gboolean e_campo(GtkWidget *w)
{
    return GTK_IS_ENTRY(w) && gtk_widget_has_css_class(w, "ph-ricerca");
}

static gboolean e_cella(GtkWidget *w)
{
    return g_object_get_data(G_OBJECT(w), PH_LAUNCHER_CHIAVE_CELLA) != NULL;
}

void ph_launcher_demo_cerca(PhShell *sh, const char *testo)
{
    GtkWidget *campo = trova(ph_shell_radice(sh), e_campo);
    if (campo != NULL)
        gtk_editable_set_text(GTK_EDITABLE(campo), testo);
}

void ph_launcher_demo_menu(PhShell *sh)
{
    GtkWidget *w = trova(ph_shell_radice(sh), e_cella);
    PhCella *c = w != NULL ? g_object_get_data(G_OBJECT(w), PH_LAUNCHER_CHIAVE_CELLA) : NULL;
    if (c != NULL && c->pacchetto != NULL)
        ph_launcher_interno_menu_apri(c->l, c->cella, 40, 40, c->pacchetto);
}

static gboolean e_griglia(GtkWidget *w)
{
    return GTK_IS_GRID_VIEW(w) && gtk_widget_has_css_class(w, "ph-launcher-griglia");
}

static gboolean e_menu(GtkWidget *w)
{
    return gtk_widget_has_css_class(w, "ph-menu");
}

/* Qualche velo trasparente (ph-velo-popup) ha preso la classe di vetro? */
static bool veli_con_vetro(GtkWidget *w)
{
    if (gtk_widget_has_css_class(w, "ph-velo-popup") &&
        (gtk_widget_has_css_class(w, "ph-vetro-chiaro") ||
         gtk_widget_has_css_class(w, "ph-vetro-scuro")))
        return true;
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c))
        if (veli_con_vetro(c))
            return true;
    return false;
}

static gboolean e_riquadro(GtkWidget *w)
{
    return gtk_widget_has_css_class(w, "ph-launcher");
}

/* `w` e' `avo` o un suo discendente? */
static bool dentro(GtkWidget *w, GtkWidget *avo)
{
    for (; w != NULL; w = gtk_widget_get_parent(w))
        if (w == avo)
            return true;
    return false;
}

/* Chi riceve un clic in (x, y) della radice: e' il velo dei popup della shell? */
static bool preso_dal_velo(GtkWidget *radice, GtkWidget *riquadro, double x, double y)
{
    GtkWidget *w = gtk_widget_pick(radice, x, y, GTK_PICK_DEFAULT);
    if (w == NULL || dentro(w, riquadro))
        return false;
    for (; w != NULL; w = gtk_widget_get_parent(w))
        if (gtk_widget_has_css_class(w, "ph-velo-popup"))
            return true;
    return false;
}


/* ---- Ombra delle icone vere ---------------------------------------------
 * Un'icona vera ha la sua forma: l'ombra rettangolare del segnaposto le
 * sporgerebbe attorno come un alone quadrato. Si misura il pixel 3 px sotto
 * il bordo dell'icona, al centro, nella cella disegnata in una texture: la
 * cella e' trasparente, quindi senza ombra l'alfa vale 0; con l'ombra (0 4px
 * 12px, .35) e' ben sopra. Per contrasto, un segnaposto messo da noi in un
 * angolo (stesse classi del launcher) deve avere l'ombra: cosi' si sa che la
 * misura la vede. */
enum { SOTTO_ICONA_PX = 3, ALFA_OMBRA_MIN = 20 };

static GtkWidget *sonda_cella, *sonda_icona;

static void sonda_dimenticata(gpointer dati, GObject *morto)
{
    (void)dati;
    (void)morto;
    sonda_cella = sonda_icona = NULL;
}

void ph_launcher_demo_sonda(PhShell *sh)
{
    sonda_icona = ph_shell_segnaposto_nuovo("it.prova.sonda", "Sonda", 60, 16);
    gtk_widget_add_css_class(sonda_icona, "ph-launcher-icona");
    gtk_widget_set_halign(sonda_icona, GTK_ALIGN_CENTER);
    sonda_cella = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(sonda_cella, "ph-launcher-cella");
    gtk_box_append(GTK_BOX(sonda_cella), sonda_icona);
    gtk_box_append(GTK_BOX(sonda_cella), gtk_label_new("Sonda"));
    gtk_widget_set_halign(sonda_cella, GTK_ALIGN_START);
    gtk_widget_set_valign(sonda_cella, GTK_ALIGN_END);
    gtk_widget_set_can_target(sonda_cella, FALSE); /* i clic non la vedono */
    g_object_weak_ref(G_OBJECT(sonda_cella), sonda_dimenticata, NULL);
    ph_shell_strato_aggiungi(sh, PH_STRATO_PANNELLI, sonda_cella);
}

/* Alfa del pixel 3 px sotto l'icona (al centro) nella cella disegnata; -1 se
 * non si riesce a disegnare. */
static int alfa_sotto_icona(GtkWidget *cella, GtkWidget *icona)
{
    int w = gtk_widget_get_width(cella), h = gtk_widget_get_height(cella);
    graphene_rect_t bi;
    if (w <= 0 || h <= 0 || !gtk_widget_compute_bounds(icona, cella, &bi))
        return -1;
    int x = (int)(bi.origin.x + bi.size.width / 2), y = (int)(bi.origin.y + bi.size.height) + SOTTO_ICONA_PX;
    if (x < 0 || x >= w || y < 0 || y >= h)
        return -1;
    GdkPaintable *pt = gtk_widget_paintable_new(cella);
    GtkSnapshot *s = gtk_snapshot_new();
    gdk_paintable_snapshot(pt, s, w, h);
    g_object_unref(pt);
    GskRenderNode *nodo = gtk_snapshot_free_to_node(s);
    GtkNative *nat = gtk_widget_get_native(cella);
    GskRenderer *rend = nat != NULL ? gtk_native_get_renderer(nat) : NULL;
    if (nodo == NULL || rend == NULL) {
        if (nodo != NULL)
            gsk_render_node_unref(nodo);
        return -1;
    }
    GdkTexture *t = gsk_renderer_render_texture(rend, nodo, &GRAPHENE_RECT_INIT(0, 0, w, h));
    gsk_render_node_unref(nodo);
    if (t == NULL)
        return -1;
    int tw = gdk_texture_get_width(t), th = gdk_texture_get_height(t);
    int alfa = -1;
    if (x < tw && y < th) {
        gsize stride = (gsize)tw * 4;
        guchar *dati = g_malloc0(stride * (gsize)th);
        gdk_texture_download(t, dati, stride); /* B8G8R8A8 premoltiplicato: alfa = 4o byte */
        alfa = dati[(gsize)y * stride + (gsize)x * 4 + 3];
        g_free(dati);
    }
    g_object_unref(t);
    return alfa;
}

static void raccogli_celle(GtkWidget *w, GPtrArray *out)
{
    if (g_object_get_data(G_OBJECT(w), PH_LAUNCHER_CHIAVE_CELLA) != NULL)
        g_ptr_array_add(out, g_object_get_data(G_OBJECT(w), PH_LAUNCHER_CHIAVE_CELLA));
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c))
        raccogli_celle(c, out);
}

#define VERIFICA(cond)                                                                             \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            if (stampa)                                                                            \
                fprintf(stderr, "prova a schermo: fallito: %s\n", #cond);                          \
            errori++;                                                                              \
        }                                                                                          \
    } while (0)

int ph_launcher_demo_verifica(PhShell *sh, bool stampa, bool scorre)
{
    int errori = 0;
    GtkWidget *radice = ph_shell_radice(sh);
    GtkWidget *r = trova(radice, e_riquadro);
    VERIFICA(r != NULL);
    if (r == NULL)
        return errori;
    int larg = 0, alt = 0;
    ph_shell_area(sh, &larg, &alt);
    PhLauncherMisure m;
    ph_launcher_misure(larg, alt, &m);
    VERIFICA(larg > 0 && alt > 0);
    if (stampa) /* alla scadenza: i valori veri, per capire che cosa mancava */
        fprintf(stderr, "prova a schermo: area %dx%d, attesi larghezza %d margine %d massimo %d "
                        "interna %d colonne %d\n", larg, alt, m.larghezza, m.margine_alto,
                m.altezza_max, m.larghezza_interna, m.colonne);
    VERIFICA(gtk_widget_get_mapped(r));
    graphene_rect_t b;
    VERIFICA(gtk_widget_compute_bounds(r, radice, &b));
    if (stampa)
        fprintf(stderr, "prova a schermo: riquadro x %.0f y %.0f %.0fx%.0f\n", b.origin.x,
                b.origin.y, b.size.width, b.size.height);
    /* Larghezza, centratura, distanza dall'alto, altezza entro il massimo. */
    VERIFICA((int)b.size.width == m.larghezza);
    double sbilancio = (b.origin.x + b.size.width / 2) - larg / 2.0; /* centro del riquadro */
    VERIFICA(sbilancio > -1.5 && sbilancio < 1.5);
    VERIFICA((int)b.origin.y == m.margine_alto);
    VERIFICA((int)b.size.height > 0 && (int)b.size.height <= m.altezza_max);
    /* Griglia: ha le app; la larghezza interna reale e' quella calcolata (il
     * CSS e le costanti di logica.h devono andare d'accordo); se le app non ci
     * stanno la griglia scorre e il riquadro arriva al massimo. */
    GtkWidget *g = trova(r, e_griglia);
    VERIFICA(g != NULL);
    /* Icone: la classe segue la texture (vera = senza ombra), e dove l'icona e'
     * vera la cella sotto l'icona e' trasparente. Serve almeno una icona vera
     * gia' arrivata (se no si riprova fino alla scadenza); la sonda col
     * segnaposto prova che l'ombra, dove c'e', si vede nella misura. */
    GPtrArray *celle = g_ptr_array_new();
    raccogli_celle(radice, celle);
    int vere = 0, misurate = 0;
    for (guint i = 0; i < celle->len; i++) {
        PhCella *c = g_ptr_array_index(celle, i);
        if (c->pacchetto == NULL || c->icona == NULL)
            continue;
        bool vera = c->texture != NULL;
        VERIFICA(gtk_widget_has_css_class(c->icona, "ph-launcher-icona-vera") == vera);
        if (!vera)
            continue;
        vere++;
        if (misurate < 3 && gtk_widget_get_mapped(c->cella)) {
            int a = alfa_sotto_icona(c->cella, c->icona);
            VERIFICA(a == 0);
            misurate++;
        }
    }
    g_ptr_array_unref(celle);
    VERIFICA(vere > 0);
    VERIFICA(misurate > 0);
    VERIFICA(sonda_cella != NULL && sonda_icona != NULL);
    if (sonda_cella != NULL && sonda_icona != NULL) {
        VERIFICA(!gtk_widget_has_css_class(sonda_icona, "ph-launcher-icona-vera"));
        VERIFICA(alfa_sotto_icona(sonda_cella, sonda_icona) >= ALFA_OMBRA_MIN);
    }
    if (g == NULL)
        return errori;
    GListModel *modello = G_LIST_MODEL(gtk_grid_view_get_model(GTK_GRID_VIEW(g)));
    VERIFICA(modello != NULL && g_list_model_get_n_items(modello) > 0);
    GtkWidget *sc = gtk_widget_get_ancestor(g, GTK_TYPE_SCROLLED_WINDOW);
    VERIFICA(sc != NULL);
    if (sc == NULL)
        return errori;
    graphene_rect_t bs;
    VERIFICA(gtk_widget_compute_bounds(sc, r, &bs));
    VERIFICA((int)bs.size.width == m.larghezza_interna);
    /* La griglia e' larga quanto le sue colonne e centrata nella finestra scorrevole. */
    graphene_rect_t bg;
    VERIFICA(gtk_widget_compute_bounds(g, sc, &bg));
    VERIFICA((int)bg.size.width == m.larghezza_griglia);
    double fuori = bg.origin.x - (bs.size.width - bg.size.width - bg.origin.x);
    VERIFICA(fuori > -1.5 && fuori < 1.5);
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sc));
    bool scorre_ora = gtk_adjustment_get_upper(adj) > gtk_adjustment_get_page_size(adj) + 0.5;
    if (stampa)
        fprintf(stderr, "prova a schermo: elementi %u, scorrevole %.0fx%.0f, upper %.0f page %.0f\n",
                modello != NULL ? g_list_model_get_n_items(modello) : 0, bs.size.width,
                bs.size.height, gtk_adjustment_get_upper(adj), gtk_adjustment_get_page_size(adj));
    VERIFICA(scorre_ora == scorre);
    if (scorre) {
        int diff = (int)b.size.height - m.altezza_max;
        VERIFICA(diff >= -2 && diff <= 0);
    }
    /* Vetro: riquadro e menu sono superfici chiare, i veli no. */
    VERIFICA(gtk_widget_has_css_class(r, "ph-vetro-chiaro"));
    GtkWidget *menu = trova(radice, e_menu);
    VERIFICA(menu != NULL && gtk_widget_has_css_class(menu, "ph-vetro-chiaro"));
    VERIFICA(!veli_con_vetro(radice));
    /* Clic nell'area ora scoperta (a sinistra, a destra, sotto): lo prende il
     * velo dei popup (che chiude il launcher), non cio' che sta sotto. Dentro
     * il riquadro no. */
    double cy = b.origin.y + b.size.height / 2;
    VERIFICA(preso_dal_velo(radice, r, b.origin.x / 2, cy));
    VERIFICA(preso_dal_velo(radice, r, b.origin.x + b.size.width + b.origin.x / 2, cy));
    VERIFICA(preso_dal_velo(radice, r, larg / 2.0, b.origin.y + b.size.height + 10));
    VERIFICA(!preso_dal_velo(radice, r, larg / 2.0, b.origin.y + 10));
    return errori;
}
