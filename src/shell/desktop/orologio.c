/* Il quadrante dell'Orologio disegnato da noi (stili classico, essenziale e palette):
 * tutto vettoriale (cairo dentro lo snapshot, testo con Pango), nitido a ogni scala,
 * nessuna immagine e nessun dato dal telefono. I colori seguono il tema (vetro chiaro o
 * scuro): il testo e' il colore CSS del widget (--ph-testo, ereditato dalla scheda), il
 * secondario e' lo stesso a meta' opacita', l'accento si legge da un figlio invisibile
 * che il CSS colora con --ph-accento (da C le variabili CSS non si leggono). */
#include <math.h>
#include <pango/pangocairo.h>
#include <string.h>

#include "interno.h"

#define PH_TIPO_OROLOGIO (ph_dsk_orologio_get_type())
G_DECLARE_FINAL_TYPE(PhDskOrologio, ph_dsk_orologio, PH, DSK_OROLOGIO, GtkWidget)

struct _PhDskOrologio {
    GtkWidget genitore;
    PhDskOrologioDati dati;
    GtkWidget *sonda; /* figlio vuoto: il suo colore CSS e' l'accento del tema */
};

/* Colori letti dallo stile prima di disegnare. */
typedef struct {
    GdkRGBA testo, accento;
} Tema;

G_DEFINE_FINAL_TYPE(PhDskOrologio, ph_dsk_orologio, GTK_TYPE_WIDGET)

/* Testo del tema con l'opacita' `a` (moltiplicata per la sua). */
static void usa_testo(cairo_t *cr, const Tema *t, double a)
{
    cairo_set_source_rgba(cr, t->testo.red, t->testo.green, t->testo.blue, t->testo.alpha * a);
}

static void usa_accento(cairo_t *cr, const Tema *t)
{
    cairo_set_source_rgba(cr, t->accento.red, t->accento.green, t->accento.blue, t->accento.alpha);
}

static void testo_centrato(cairo_t *cr, const char *testo, double cx, double cy, double px,
                           bool grassetto)
{
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string("Inter, system-ui");
    pango_font_description_set_weight(fd, grassetto ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_SEMIBOLD);
    pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
    pango_layout_set_font_description(l, fd);
    pango_layout_set_text(l, testo, -1);
    int w, h;
    pango_layout_get_pixel_size(l, &w, &h);
    cairo_move_to(cr, cx - w / 2.0, cy - h / 2.0);
    pango_cairo_show_layout(cr, l);
    pango_font_description_free(fd);
    g_object_unref(l);
}

static void lancetta(cairo_t *cr, double cx, double cy, double angolo_gradi, double dal_centro,
                     double lunghezza, double spessore)
{
    double a = angolo_gradi * G_PI / 180.0;
    double dx = sin(a), dy = -cos(a);
    cairo_set_line_width(cr, spessore);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, cx + dx * dal_centro, cy + dy * dal_centro);
    cairo_line_to(cr, cx + dx * lunghezza, cy + dy * lunghezza);
    cairo_stroke(cr);
}

static void disegna_data(cairo_t *cr, const Tema *t, const char *data, double cx, double y, double px)
{
    if (data[0] == '\0')
        return;
    usa_testo(cr, t, 0.8); /* secondario */
    testo_centrato(cr, data, cx, y, px, false);
}

/* Analogico classico: quadrante con tacche, numeri e tre lancette. */
static void disegna_classico(cairo_t *cr, const Tema *t, const PhDskOrologioDati *d, double larg)
{
    double D = ph_dsk_quadrante_diametro(PH_DSK_OROLOGIO_CLASSICO, (PhCfgWidgetDim)d->dim);
    double R = D / 2.0 - 1.0, cx = larg / 2.0, cy = D / 2.0;
    /* Fondo del quadrante e anello. */
    cairo_arc(cr, cx, cy, R, 0, 2 * G_PI);
    usa_testo(cr, t, 0.06);
    cairo_fill_preserve(cr);
    usa_testo(cr, t, 0.20);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);
    /* Tacche: 60, le ore piu' lunghe e chiare. */
    for (int i = 0; i < 60; i++) {
        bool ora = i % 5 == 0;
        double a = i * 6.0 * G_PI / 180.0;
        double r1 = R - D * 0.035, r0 = R - D * (ora ? 0.085 : 0.055);
        usa_testo(cr, t, ora ? 0.90 : 0.35);
        cairo_set_line_width(cr, ora ? 2.0 : 1.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, cx + sin(a) * r0, cy - cos(a) * r0);
        cairo_line_to(cr, cx + sin(a) * r1, cy - cos(a) * r1);
        cairo_stroke(cr);
    }
    /* Numeri. */
    usa_testo(cr, t, 0.95);
    for (int n = 1; n <= 12; n++) {
        char t[4];
        snprintf(t, sizeof t, "%d", n);
        double a = n * 30.0 * G_PI / 180.0, r = R * 0.68;
        testo_centrato(cr, t, cx + sin(a) * r, cy - cos(a) * r, D * 0.088, true);
    }
    double ah, am, as;
    ph_dsk_angoli_lancette(d->ore, d->minuti, d->secondi, &ah, &am, &as);
    /* Ore e minuti: bianche, con una lieve ombra. */
    cairo_set_source_rgba(cr, 0, 0, 0, 0.30); /* colore-dato: ombra delle lancette, nera su ogni tema */
    lancetta(cr, cx + 1, cy + 1.5, ah, 0, R * 0.48, D * 0.040);
    lancetta(cr, cx + 1, cy + 1.5, am, 0, R * 0.74, D * 0.028);
    usa_testo(cr, t, 1.0);
    lancetta(cr, cx, cy, ah, 0, R * 0.48, D * 0.040);
    lancetta(cr, cx, cy, am, 0, R * 0.74, D * 0.028);
    /* Secondi: sottile, accento, con la coda e il perno. */
    usa_accento(cr, t);
    lancetta(cr, cx, cy, as, -R * 0.16, R * 0.82, 1.5);
    cairo_arc(cr, cx, cy, D * 0.035, 0, 2 * G_PI);
    cairo_fill(cr);
    usa_testo(cr, t, 1.0); /* perno: puntino nel colore del testo sopra il disco d'accento */
    cairo_arc(cr, cx, cy, D * 0.012, 0, 2 * G_PI);
    cairo_fill(cr);
}

/* Analogico essenziale: solo lancette e quattro tacche, data piccola sotto. */
static void disegna_essenziale(cairo_t *cr, const Tema *t, const PhDskOrologioDati *d, double larg)
{
    double D = ph_dsk_quadrante_diametro(PH_DSK_OROLOGIO_ESSENZIALE, (PhCfgWidgetDim)d->dim);
    double R = D / 2.0, cx = larg / 2.0, cy = D / 2.0;
    for (int i = 0; i < 4; i++) {
        double a = i * 90.0 * G_PI / 180.0;
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        if (i == 0)
            usa_accento(cr, t);
        else
            usa_testo(cr, t, 0.55);
        cairo_set_line_width(cr, i == 0 ? 3.0 : 2.0);
        cairo_move_to(cr, cx + sin(a) * R * 0.86, cy - cos(a) * R * 0.86);
        cairo_line_to(cr, cx + sin(a) * R * 0.98, cy - cos(a) * R * 0.98);
        cairo_stroke(cr);
    }
    double ah, am, as;
    ph_dsk_angoli_lancette(d->ore, d->minuti, 0, &ah, &am, &as);
    usa_testo(cr, t, 1.0);
    lancetta(cr, cx, cy, ah, 0, R * 0.52, D * 0.050);
    lancetta(cr, cx, cy, am, 0, R * 0.80, D * 0.034);
    usa_accento(cr, t);
    cairo_arc(cr, cx, cy, D * 0.04, 0, 2 * G_PI);
    cairo_fill(cr);
    disegna_data(cr, t, d->data, cx, D + 12, 12);
}

static void rettangolo_tondo(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

/* A palette: ore e minuti su due tessere, come un tabellone a pale. */
static void disegna_palette(cairo_t *cr, const Tema *t, const PhDskOrologioDati *d, double larg)
{
    double TH = d->dim == PH_CFG_WIDGET_PICCOLO ? 56 : (d->dim == PH_CFG_WIDGET_GRANDE ? 96 : 76);
    double TW = TH * 1.12, sp = TH * 0.20;
    double x0 = (larg - (2 * TW + sp)) / 2.0;
    for (int k = 0; k < 2; k++) {
        double x = x0 + k * (TW + sp);
        char num[4];
        snprintf(num, sizeof num, "%02d", k == 0 ? d->ore : d->minuti);
        /* Tessera: fondo scuro, meta' alta appena piu' chiara, taglio al centro. */
        rettangolo_tondo(cr, x, 0, TW, TH, 10);
        usa_testo(cr, t, 0.10);
        cairo_fill_preserve(cr);
        usa_testo(cr, t, 0.16);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        cairo_save(cr);
        cairo_rectangle(cr, x, 0, TW, TH / 2.0);
        cairo_clip(cr);
        rettangolo_tondo(cr, x, 0, TW, TH, 10);
        usa_testo(cr, t, 0.07);
        cairo_fill(cr);
        cairo_restore(cr);
        /* Il taglio sta SOTTO le cifre (sottile e scuro): le cifre lo coprono e non sembra
         * un barrato. */
        cairo_set_source_rgba(cr, 0.04, 0.07, 0.11, 0.55); /* colore-dato: taglio scuro della tessera, sotto le cifre */
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, x + 1, TH / 2.0);
        cairo_line_to(cr, x + TW - 1, TH / 2.0);
        cairo_stroke(cr);
        usa_testo(cr, t, 1.0);
        testo_centrato(cr, num, x + TW / 2.0, TH / 2.0, TH * 0.66, true);
    }
    /* Due punti dell'accento tra le tessere. */
    usa_accento(cr, t);
    double cx = larg / 2.0;
    cairo_arc(cr, cx, TH * 0.38, TH * 0.035, 0, 2 * G_PI);
    cairo_fill(cr);
    cairo_arc(cr, cx, TH * 0.62, TH * 0.035, 0, 2 * G_PI);
    cairo_fill(cr);
    if (d->dim != PH_CFG_WIDGET_PICCOLO)
        disegna_data(cr, t, d->data, larg / 2.0, TH + 15, 12);
}

static void orologio_snapshot(GtkWidget *widget, GtkSnapshot *snap)
{
    PhDskOrologio *o = PH_DSK_OROLOGIO(widget);
    Tema t;
    gtk_widget_get_color(widget, &t.testo);
    gtk_widget_get_color(o->sonda, &t.accento);
    double W = gtk_widget_get_width(widget), H = gtk_widget_get_height(widget);
    if (W <= 0 || H <= 0)
        return;
    graphene_rect_t area = GRAPHENE_RECT_INIT(0, 0, (float)W, (float)H);
    cairo_t *cr = gtk_snapshot_append_cairo(snap, &area);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    switch (o->dati.stile) {
    case PH_DSK_OROLOGIO_CLASSICO: disegna_classico(cr, &t, &o->dati, W); break;
    case PH_DSK_OROLOGIO_ESSENZIALE: disegna_essenziale(cr, &t, &o->dati, W); break;
    case PH_DSK_OROLOGIO_PALETTE: disegna_palette(cr, &t, &o->dati, W); break;
    default: break;
    }
    cairo_destroy(cr);
}

/* Il solo figlio e' la sonda dell'accento: misura nulla, mai disegnato. */
static void orologio_misura(GtkWidget *widget, GtkOrientation o, int per, int *min, int *nat, int *min_base,
                            int *nat_base)
{
    (void)widget;
    (void)o;
    (void)per;
    *min = *nat = 0;
    *min_base = *nat_base = -1;
}

static void orologio_alloca(GtkWidget *widget, int larg, int alto, int base)
{
    (void)larg;
    (void)alto;
    (void)base;
    GtkWidget *sonda = PH_DSK_OROLOGIO(widget)->sonda;
    int minimo;
    /* GTK vuole una misura prima di ogni allocazione. */
    gtk_widget_measure(sonda, GTK_ORIENTATION_HORIZONTAL, -1, &minimo, NULL, NULL, NULL);
    gtk_widget_measure(sonda, GTK_ORIENTATION_VERTICAL, 1, &minimo, NULL, NULL, NULL);
    gtk_widget_allocate(sonda, 1, 1, -1, NULL);
}

static void orologio_dispose(GObject *obj)
{
    PhDskOrologio *o = PH_DSK_OROLOGIO(obj);
    if (o->sonda != NULL) {
        gtk_widget_unparent(o->sonda);
        o->sonda = NULL;
    }
    G_OBJECT_CLASS(ph_dsk_orologio_parent_class)->dispose(obj);
}

static void ph_dsk_orologio_class_init(PhDskOrologioClass *k)
{
    G_OBJECT_CLASS(k)->dispose = orologio_dispose;
    GTK_WIDGET_CLASS(k)->measure = orologio_misura;
    GTK_WIDGET_CLASS(k)->size_allocate = orologio_alloca;
    GTK_WIDGET_CLASS(k)->snapshot = orologio_snapshot;
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(k), "ph-desktop-orologio");
}

static void ph_dsk_orologio_init(PhDskOrologio *o)
{
    gtk_widget_set_hexpand(GTK_WIDGET(o), TRUE);
    o->sonda = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(o->sonda, "ph-desktop-orologio-accento");
    gtk_widget_set_can_target(o->sonda, FALSE);
    gtk_widget_set_parent(o->sonda, GTK_WIDGET(o));
}

GtkWidget *ph_dsk_orologio_nuovo(void)
{
    return g_object_new(PH_TIPO_OROLOGIO, NULL);
}

void ph_dsk_orologio_imposta(GtkWidget *w, const PhDskOrologioDati *dati)
{
    g_return_if_fail(PH_IS_DSK_OROLOGIO(w) && dati != NULL);
    PhDskOrologio *o = PH_DSK_OROLOGIO(w);
    if (memcmp(&o->dati, dati, sizeof o->dati) == 0)
        return; /* nulla e' cambiato: niente ridisegno */
    o->dati = *dati;
    gtk_widget_set_size_request(w, -1, ph_dsk_orologio_altezza(dati->stile, (PhCfgWidgetDim)dati->dim));
    gtk_widget_queue_draw(w);
}
