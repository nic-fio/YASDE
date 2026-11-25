/* Il widget che disegna lo sfondo del desktop e la sua anteprima nel dialogo
 * (F-02a): colore solido, sfumato (gradiente come il CSS del mockup) o immagine
 * con i quattro adattamenti. Tutti i dati gli arrivano gia' pronti (la texture e'
 * stata decodificata da un thread): qui si disegna e basta. */
#include "interno.h"

#define PH_TIPO_SFONDO (ph_dsk_sfondo_get_type())
G_DECLARE_FINAL_TYPE(PhDskSfondo, ph_dsk_sfondo, PH, DSK_SFONDO, GtkWidget)

struct _PhDskSfondo {
    GtkWidget genitore;
    PhCfgSfondo cfg;
    GdkTexture *immagine;
    int ref_larghezza;
};

G_DEFINE_FINAL_TYPE(PhDskSfondo, ph_dsk_sfondo, GTK_TYPE_WIDGET)

static void colore_da_rgb(GdkRGBA *c, uint32_t rgb)
{
    c->red = ((rgb >> 16) & 0xff) / 255.0f;
    c->green = ((rgb >> 8) & 0xff) / 255.0f;
    c->blue = (rgb & 0xff) / 255.0f;
    c->alpha = 1.0f;
}

static void sfondo_snapshot(GtkWidget *widget, GtkSnapshot *snap)
{
    PhDskSfondo *s = PH_DSK_SFONDO(widget);
    float W = (float)gtk_widget_get_width(widget), H = (float)gtk_widget_get_height(widget);
    if (W <= 0 || H <= 0)
        return;
    graphene_rect_t area = GRAPHENE_RECT_INIT(0, 0, W, H);
    GdkRGBA c1, c2;
    switch (s->cfg.tipo) {
    case PH_CFG_SFONDO_SOLIDO:
        colore_da_rgb(&c1, s->cfg.colore_solido);
        gtk_snapshot_append_color(snap, &c1, &area);
        break;
    case PH_CFG_SFONDO_IMMAGINE: {
        /* Fondo sotto l'immagine (mockup): #0b1220. */
        colore_da_rgb(&c1, 0x0b1220);
        gtk_snapshot_append_color(snap, &c1, &area);
        if (s->immagine == NULL)
            break;
        float iw = (float)gdk_texture_get_width(s->immagine);
        float ih = (float)gdk_texture_get_height(s->immagine);
        if (s->cfg.adattamento == PH_CFG_ADATT_CENTRA) {
            /* Misura naturale in pixel logici; nell'anteprima in proporzione. */
            float f = (float)gtk_widget_get_scale_factor(widget);
            if (f < 1)
                f = 1;
            iw /= f;
            ih /= f;
            if (s->ref_larghezza > 0) {
                float k = W / (float)s->ref_larghezza;
                iw *= k;
                ih *= k;
            }
        }
        PhDskRett r = ph_dsk_rett_immagine(s->cfg.adattamento, W, H, iw, ih);
        if (r.w <= 0 || r.h <= 0)
            break;
        graphene_rect_t dove = GRAPHENE_RECT_INIT(r.x, r.y, r.w, r.h);
        gtk_snapshot_push_clip(snap, &area);
        gtk_snapshot_append_scaled_texture(snap, s->immagine, GSK_SCALING_FILTER_LINEAR, &dove);
        gtk_snapshot_pop(snap);
        break;
    }
    case PH_CFG_SFONDO_SFUMATO:
    default: {
        colore_da_rgb(&c1, s->cfg.colore_da);
        colore_da_rgb(&c2, s->cfg.colore_a);
        float x0, y0, x1, y1;
        ph_dsk_gradiente(s->cfg.direzione, W, H, &x0, &y0, &x1, &y1);
        GskColorStop fermate[2] = {{0.0f, c1}, {1.0f, c2}};
        graphene_point_t p0 = GRAPHENE_POINT_INIT(x0, y0), p1 = GRAPHENE_POINT_INIT(x1, y1);
        gtk_snapshot_append_linear_gradient(snap, &area, &p0, &p1, fermate, 2);
        break;
    }
    }
}

static void sfondo_dispose(GObject *o)
{
    PhDskSfondo *s = PH_DSK_SFONDO(o);
    g_clear_object(&s->immagine);
    G_OBJECT_CLASS(ph_dsk_sfondo_parent_class)->dispose(o);
}

static void ph_dsk_sfondo_class_init(PhDskSfondoClass *k)
{
    G_OBJECT_CLASS(k)->dispose = sfondo_dispose;
    GTK_WIDGET_CLASS(k)->snapshot = sfondo_snapshot;
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(k), "ph-desktop-sfondo");
}

static void ph_dsk_sfondo_init(PhDskSfondo *s)
{
    s->cfg.tipo = PH_CFG_SFONDO_SFUMATO;
    s->cfg.colore_solido = 0x1f2430;
    s->cfg.colore_da = 0x0e2638;
    s->cfg.colore_a = 0x1a625f;
    s->cfg.direzione = PH_CFG_DIR_DIAGONALE;
    s->cfg.adattamento = PH_CFG_ADATT_RIEMPI;
    gtk_widget_set_hexpand(GTK_WIDGET(s), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(s), TRUE);
    gtk_widget_set_overflow(GTK_WIDGET(s), GTK_OVERFLOW_HIDDEN);
}

GtkWidget *ph_dsk_sfondo_nuovo(void)
{
    return g_object_new(PH_TIPO_SFONDO, NULL);
}

void ph_dsk_sfondo_imposta(GtkWidget *w, const PhCfgSfondo *cfg, GdkTexture *immagine,
                           int ref_larghezza)
{
    g_return_if_fail(PH_IS_DSK_SFONDO(w) && cfg != NULL);
    PhDskSfondo *s = PH_DSK_SFONDO(w);
    s->cfg = *cfg;
    if (s->immagine != immagine) {
        g_clear_object(&s->immagine);
        if (immagine != NULL)
            s->immagine = g_object_ref(immagine);
    }
    s->ref_larghezza = ref_larghezza;
    gtk_widget_queue_draw(w);
}
