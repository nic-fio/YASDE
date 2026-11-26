/* Componenti di disegno comuni dei widget (anello, barra a fette con legenda, curva, tratto
 * minima-massima, riga d'elenco, numero grande). Contratti in interno.h; geometria e scelte in
 * logica_comp.c (pure, provate senza schermo).
 * Quelli disegnati (anello, fette, curva, tratto, numero) sono un solo tipo GTK con lo
 * snapshot di cairo/Pango, come il quadrante di orologio.c: nessuna immagine, tutto
 * vettoriale e della misura che ricevono. I colori non sono scritti qui: il testo e' il colore
 * CSS del widget (ereditato dalla scheda), tutto il resto si legge da figli invisibili («sonde»)
 * che desktop.css colora con le variabili --ph-desktop-* (da C le variabili CSS non si leggono).
 * Riga e legenda sono widget GTK normali (testo con i puntini, fuoco e tasti gratis). */
#include <math.h>
#include <pango/pangocairo.h>
#include <string.h>

#include "interno.h"

/* ---- Il tipo disegnato ------------------------------------------------------------------ */
#define PH_TIPO_DISEGNO (ph_dsk_disegno_get_type())
G_DECLARE_FINAL_TYPE(PhDskDisegno, ph_dsk_disegno, PH, DSK_DISEGNO, GtkWidget)

typedef enum { D_ANELLO, D_FETTE, D_CURVA, D_TRATTO, D_NUMERO } Quale;

/* Sonde: gli accenti, poi il testo secondario e il binario (solco) delle barre. */
enum { SONDA_TESTO2 = PH_DSK_ACC_N, SONDA_SOLCO, SONDE_N };
static const char *const NOMI_SONDE[SONDE_N] = {"tema", "verde", "ambra", "corallo", "indaco", "viola",
                                                "ardesia", "testo2", "solco"};

struct _PhDskDisegno {
    GtkWidget genitore;
    Quale quale;
    GtkWidget *sonda[SONDE_N];
    /* anello */
    struct {
        bool noto;
        double percento;
        char testo[24];
        int acc;
    } anello;
    /* fette */
    struct {
        int64_t valori[PH_DSK_FETTE_MAX];
        int acc[PH_DSK_FETTE_MAX];
        size_t n;
        int64_t totale;
    } fette;
    /* curva */
    struct {
        double t[PH_DSK_CURVA_MAX];
        char gradi[PH_DSK_CURVA_MAX][16];
        char ore[PH_DSK_CURVA_MAX][16];
        size_t n;
        bool ha_gradi, ha_ore;
        int acc;
    } curva;
    /* tratto */
    struct {
        bool noto;
        double min, max, scala_min, scala_max;
        int da, a;
    } tratto;
    /* numero */
    struct {
        char etichetta[64], numero[32], nota[64];
    } numero;
};

G_DEFINE_FINAL_TYPE(PhDskDisegno, ph_dsk_disegno, GTK_TYPE_WIDGET)

typedef struct {
    GdkRGBA acc[PH_DSK_ACC_N];
    GdkRGBA testo, testo2, solco;
} Tema;

static void tema_leggi(PhDskDisegno *o, Tema *t)
{
    gtk_widget_get_color(GTK_WIDGET(o), &t->testo);
    for (int i = 0; i < PH_DSK_ACC_N; i++)
        gtk_widget_get_color(o->sonda[i], &t->acc[i]);
    gtk_widget_get_color(o->sonda[SONDA_TESTO2], &t->testo2);
    gtk_widget_get_color(o->sonda[SONDA_SOLCO], &t->solco);
}

static void colore(cairo_t *cr, const GdkRGBA *c, double alfa)
{
    cairo_set_source_rgba(cr, c->red, c->green, c->blue, c->alpha * alfa);
}

/* ---- Testo con Pango -------------------------------------------------------------------- */
typedef struct {
    PangoLayout *l;
    int larg, alto;
    double base; /* distanza della linea di base dall'alto */
} Testo;

/* max_larg <= 0: nessun limite; altrimenti il testo si tronca con i puntini. */
static Testo testo_crea(cairo_t *cr, const char *s, double px, bool grassetto, double max_larg)
{
    Testo t;
    t.l = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string("Inter, system-ui");
    pango_font_description_set_weight(fd, grassetto ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
    pango_layout_set_font_description(t.l, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(t.l, s != NULL ? s : "", -1);
    if (max_larg > 0) {
        pango_layout_set_width(t.l, (int)(max_larg * PANGO_SCALE));
        pango_layout_set_ellipsize(t.l, PANGO_ELLIPSIZE_END);
    }
    pango_layout_get_pixel_size(t.l, &t.larg, &t.alto);
    t.base = pango_layout_get_baseline(t.l) / (double)PANGO_SCALE;
    return t;
}

static void testo_mostra(cairo_t *cr, const Testo *t, double x, double y)
{
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, t->l);
}

static void testo_libera(Testo *t)
{
    g_object_unref(t->l);
}

/* Scrive `s` con il centro verticale in y; ancora 0 sinistra, 1 centro, 2 destra; con `limite`
 * (> 0) l'ascissa del testo si tiene dentro 0..limite. */
static void testo_a(cairo_t *cr, const char *s, double x, double y, double px, bool grassetto, int ancora,
                    double limite)
{
    Testo t = testo_crea(cr, s, px, grassetto, 0);
    double x0 = ancora == 0 ? x : (ancora == 1 ? x - t.larg / 2.0 : x - t.larg);
    if (limite > 0) {
        if (x0 + t.larg > limite)
            x0 = limite - t.larg;
        if (x0 < 0)
            x0 = 0;
    }
    testo_mostra(cr, &t, x0, y - t.alto / 2.0);
    testo_libera(&t);
}

static void rett_tondo(cairo_t *cr, double x, double y, double w, double h, double r)
{
    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

/* ---- Anello ------------------------------------------------------------------------------------ */
static void disegna_anello(PhDskDisegno *o, cairo_t *cr, const Tema *t, double W, double H)
{
    double lato = fmin(W, H), raggio, spess;
    ph_dsk_anello_geometria(lato, &raggio, &spess);
    if (raggio <= 0)
        return;
    double cx = W / 2.0, cy = H / 2.0;
    cairo_set_line_width(cr, spess);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    colore(cr, &t->solco, 1.0);
    cairo_arc(cr, cx, cy, raggio, 0, 2 * G_PI);
    cairo_stroke(cr);
    if (o->anello.noto && o->anello.percento > 0) {
        double da, a;
        ph_dsk_anello_angoli(o->anello.percento, &da, &a);
        colore(cr, &t->acc[ph_dsk_accento_valido(o->anello.acc, PH_DSK_ACC_N)], 1.0);
        cairo_arc(cr, cx, cy, raggio, da, a);
        cairo_stroke(cr);
    }
    colore(cr, &t->testo, 1.0);
    /* Il testo sta dentro il cerchio interno: corpo in proporzione al lato, ridotto se e' lungo. */
    double interno = 2 * (raggio - spess / 2.0);
    size_t car = g_utf8_strlen(o->anello.testo, -1);
    double px = ph_dsk_numero_corpo(interno * 0.9, interno * 0.5, car, 8, lato * 0.27);
    testo_a(cr, o->anello.testo, cx, cy, px, true, 1, 0);
}

/* ---- Fette (solo la barra: la legenda e' di widget GTK) ------------------------------------------- */
static void disegna_fette(PhDskDisegno *o, cairo_t *cr, const Tema *t, double W, double H)
{
    int bordi[PH_DSK_FETTE_MAX + 1];
    size_t n = ph_dsk_fette_bordi(o->fette.valori, o->fette.n, o->fette.totale, (int)W, bordi);
    rett_tondo(cr, 0, 0, W, H, H / 2.0);
    colore(cr, &t->solco, 1.0);
    cairo_fill_preserve(cr);
    cairo_clip(cr); /* le fette hanno gli angoli della barra */
    const double sp = 2; /* respiro tra due fette */
    for (size_t i = 0; i < n; i++) {
        double x0 = bordi[i], x1 = bordi[i + 1];
        if (x1 <= x0)
            continue;
        if (i + 1 < n && x1 - x0 > sp)
            x1 -= sp; /* l'ultima fetta chiude sul bordo, le altre lasciano il respiro */
        colore(cr, &t->acc[ph_dsk_accento_valido(o->fette.acc[i], PH_DSK_ACC_N)], 1.0);
        cairo_rectangle(cr, x0, 0, x1 - x0, H);
        cairo_fill(cr);
    }
}

/* ---- Curva ----------------------------------------------------------------------------------------- */
static double larghezza_massima(cairo_t *cr, char (*s)[16], size_t n, double px, bool grassetto, size_t sal)
{
    double m = 0;
    for (size_t i = 0; i < n; i += sal) {
        Testo t = testo_crea(cr, s[i], px, grassetto, 0);
        m = fmax(m, t.larg);
        testo_libera(&t);
    }
    return m;
}

static void disegna_curva(PhDskDisegno *o, cairo_t *cr, const Tema *t, double W, double H)
{
    size_t n = o->curva.n;
    if (n == 0)
        return;
    const double margine = 10, riga_alta = o->curva.ha_gradi ? 16 : 4, riga_bassa = o->curva.ha_ore ? 16 : 4;
    double y0 = riga_alta + 2, y1 = H - riga_bassa - 2;
    if (y1 <= y0)
        return; /* troppo bassa per disegnare qualcosa di leggibile */
    PhDskCurvaPunti P;
    ph_dsk_curva_punti(o->curva.t, n, W, margine, y0, y1, &P);
    n = P.n;
    const GdkRGBA *ac = &t->acc[ph_dsk_accento_valido(o->curva.acc, PH_DSK_ACC_N)];
    if (n >= 2) {
        /* Linea morbida (Catmull-Rom -> Bezier) e area sotto, sfumata verso il basso. */
        cairo_new_path(cr);
        cairo_move_to(cr, P.x[0], P.y[0]);
        for (size_t i = 0; i + 1 < n; i++) {
            double xa = i > 0 ? P.x[i - 1] : P.x[i], ya = i > 0 ? P.y[i - 1] : P.y[i];
            double xb = P.x[i], yb = P.y[i], xc = P.x[i + 1], yc = P.y[i + 1];
            double xd = i + 2 < n ? P.x[i + 2] : P.x[i + 1], yd = i + 2 < n ? P.y[i + 2] : P.y[i + 1];
            cairo_curve_to(cr, xb + (xc - xa) / 6.0, yb + (yc - ya) / 6.0, xc - (xd - xb) / 6.0,
                           yc - (yd - yb) / 6.0, xc, yc);
        }
        cairo_path_t *linea = cairo_copy_path(cr);
        cairo_line_to(cr, P.x[n - 1], y1 + 2);
        cairo_line_to(cr, P.x[0], y1 + 2);
        cairo_close_path(cr);
        cairo_pattern_t *g = cairo_pattern_create_linear(0, y0, 0, y1 + 2);
        cairo_pattern_add_color_stop_rgba(g, 0, ac->red, ac->green, ac->blue, ac->alpha * 0.40);
        cairo_pattern_add_color_stop_rgba(g, 1, ac->red, ac->green, ac->blue, 0.0);
        cairo_set_source(cr, g);
        cairo_fill(cr);
        cairo_pattern_destroy(g);
        cairo_new_path(cr);
        cairo_append_path(cr, linea);
        cairo_path_destroy(linea);
        colore(cr, ac, 1.0);
        cairo_set_line_width(cr, 2.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_stroke(cr);
    }
    /* Gradi sopra i punti e ore sotto, ogni tanti punti quanti ne stanno senza toccarsi. */
    if (o->curva.ha_gradi) {
        size_t passo = ph_dsk_curva_passo_etichette(n, W - 2 * margine,
                                                    larghezza_massima(cr, o->curva.gradi, n, 11, true, 1));
        for (size_t i = 0; i < n; i += passo) {
            colore(cr, ac, 1.0);
            cairo_arc(cr, P.x[i], P.y[i], 2.5, 0, 2 * G_PI);
            cairo_fill(cr);
            colore(cr, &t->testo, 1.0);
            testo_a(cr, o->curva.gradi[i], P.x[i], fmax(P.y[i] - 9, 6), 11, true, 1, W);
        }
    }
    if (o->curva.ha_ore) {
        size_t passo = ph_dsk_curva_passo_etichette(n, W - 2 * margine,
                                                    larghezza_massima(cr, o->curva.ore, n, 11, false, 1));
        colore(cr, &t->testo2, 1.0);
        for (size_t i = 0; i < n; i += passo)
            testo_a(cr, o->curva.ore[i], P.x[i], H - 7, 11, false, 1, W);
    }
}

/* ---- Tratto ------------------------------------------------------------------------------------------ */
static void disegna_tratto(PhDskDisegno *o, cairo_t *cr, const Tema *t, double W, double H)
{
    double spess = fmin(6.0, H), y = (H - spess) / 2.0;
    rett_tondo(cr, 0, y, W, spess, spess / 2.0);
    colore(cr, &t->solco, 1.0);
    cairo_fill(cr);
    double x0, x1;
    if (!o->tratto.noto ||
        !ph_dsk_tratto_px(o->tratto.min, o->tratto.max, o->tratto.scala_min, o->tratto.scala_max, W, spess,
                          &x0, &x1))
        return;
    const GdkRGBA *da = &t->acc[ph_dsk_accento_valido(o->tratto.da, PH_DSK_ACC_N)];
    const GdkRGBA *a = &t->acc[ph_dsk_accento_valido(o->tratto.a, PH_DSK_ACC_N)];
    cairo_pattern_t *g = cairo_pattern_create_linear(x0, 0, x1 > x0 ? x1 : x0 + 1, 0);
    cairo_pattern_add_color_stop_rgba(g, 0, da->red, da->green, da->blue, da->alpha);
    cairo_pattern_add_color_stop_rgba(g, 1, a->red, a->green, a->blue, a->alpha);
    cairo_set_source(cr, g);
    rett_tondo(cr, x0, y, x1 - x0, spess, spess / 2.0);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
}

/* ---- Numero grande ---------------------------------------------------------------------------------- */
static void disegna_numero(PhDskDisegno *o, cairo_t *cr, const Tema *t, double W, double H)
{
    double y = 0;
    if (o->numero.etichetta[0] != '\0') {
        char *maiuscolo = g_utf8_strup(o->numero.etichetta, -1);
        Testo e = testo_crea(cr, maiuscolo, 11, true, W);
        colore(cr, &t->testo2, 1.0);
        testo_mostra(cr, &e, 0, 0);
        y = e.alto + 2;
        testo_libera(&e);
        g_free(maiuscolo);
    }
    if (o->numero.numero[0] == '\0' || y >= H)
        return;
    Testo nota = {0};
    double larg_nota = 0;
    if (o->numero.nota[0] != '\0') {
        nota = testo_crea(cr, o->numero.nota, 12, false, W * 0.5);
        larg_nota = nota.larg + 8;
    }
    double disponibile = W - larg_nota;
    double px = ph_dsk_numero_corpo(disponibile, H - y, g_utf8_strlen(o->numero.numero, -1), 12, 44);
    Testo n = testo_crea(cr, o->numero.numero, px, true, 0);
    while (n.larg > disponibile && px > 12) { /* la stima non conosce il carattere vero */
        testo_libera(&n);
        px -= 1;
        n = testo_crea(cr, o->numero.numero, px, true, 0);
    }
    colore(cr, &t->testo, 1.0);
    testo_mostra(cr, &n, 0, y);
    if (o->numero.nota[0] != '\0') {
        colore(cr, &t->testo2, 1.0);
        testo_mostra(cr, &nota, n.larg + 8, y + n.base - nota.base);
        testo_libera(&nota);
    }
    testo_libera(&n);
}

/* ---- GTK -------------------------------------------------------------------------------------------- */
static void disegno_snapshot(GtkWidget *widget, GtkSnapshot *snap)
{
    PhDskDisegno *o = PH_DSK_DISEGNO(widget);
    double W = gtk_widget_get_width(widget), H = gtk_widget_get_height(widget);
    if (W <= 0 || H <= 0)
        return;
    Tema t;
    tema_leggi(o, &t);
    graphene_rect_t area = GRAPHENE_RECT_INIT(0, 0, (float)W, (float)H);
    cairo_t *cr = gtk_snapshot_append_cairo(snap, &area);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    switch (o->quale) {
    case D_ANELLO: disegna_anello(o, cr, &t, W, H); break;
    case D_FETTE: disegna_fette(o, cr, &t, W, H); break;
    case D_CURVA: disegna_curva(o, cr, &t, W, H); break;
    case D_TRATTO: disegna_tratto(o, cr, &t, W, H); break;
    case D_NUMERO: disegna_numero(o, cr, &t, W, H); break;
    }
    cairo_destroy(cr);
}

/* Misura minima e naturale per tipo: il disegno si adatta a quello che riceve. */
static void disegno_misura(GtkWidget *widget, GtkOrientation orient, int per, int *min, int *nat, int *min_base,
                           int *nat_base)
{
    (void)per;
    static const int M[5][2][2] = {/* [tipo][orizzontale, verticale][minimo, naturale] */
                                   {{40, 96}, {40, 96}},  /* anello */
                                   {{40, 200}, {14, 14}}, /* fette */
                                   {{80, 240}, {56, 84}}, /* curva */
                                   {{24, 120}, {6, 12}},  /* tratto */
                                   {{48, 120}, {28, 56}}}; /* numero */
    const int(*m)[2] = M[PH_DSK_DISEGNO(widget)->quale];
    *min = m[orient == GTK_ORIENTATION_HORIZONTAL ? 0 : 1][0];
    *nat = m[orient == GTK_ORIENTATION_HORIZONTAL ? 0 : 1][1];
    *min_base = *nat_base = -1;
}

static void disegno_alloca(GtkWidget *widget, int larg, int alto, int base)
{
    (void)larg;
    (void)alto;
    (void)base;
    PhDskDisegno *o = PH_DSK_DISEGNO(widget);
    for (int i = 0; i < SONDE_N; i++) {
        int minimo;
        /* GTK vuole una misura prima di ogni allocazione. */
        gtk_widget_measure(o->sonda[i], GTK_ORIENTATION_HORIZONTAL, -1, &minimo, NULL, NULL, NULL);
        gtk_widget_measure(o->sonda[i], GTK_ORIENTATION_VERTICAL, 1, &minimo, NULL, NULL, NULL);
        gtk_widget_allocate(o->sonda[i], 1, 1, -1, NULL);
    }
}

static void disegno_dispose(GObject *obj)
{
    PhDskDisegno *o = PH_DSK_DISEGNO(obj);
    for (int i = 0; i < SONDE_N; i++)
        if (o->sonda[i] != NULL) {
            gtk_widget_unparent(o->sonda[i]);
            o->sonda[i] = NULL;
        }
    G_OBJECT_CLASS(ph_dsk_disegno_parent_class)->dispose(obj);
}

static void ph_dsk_disegno_class_init(PhDskDisegnoClass *k)
{
    G_OBJECT_CLASS(k)->dispose = disegno_dispose;
    GTK_WIDGET_CLASS(k)->measure = disegno_misura;
    GTK_WIDGET_CLASS(k)->size_allocate = disegno_alloca;
    GTK_WIDGET_CLASS(k)->snapshot = disegno_snapshot;
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(k), "ph-desktop-disegno");
    gtk_widget_class_set_accessible_role(GTK_WIDGET_CLASS(k), GTK_ACCESSIBLE_ROLE_IMG);
}

static void ph_dsk_disegno_init(PhDskDisegno *o)
{
    for (int i = 0; i < SONDE_N; i++) {
        char classe[40];
        o->sonda[i] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        g_snprintf(classe, sizeof classe, "ph-desktop-sonda-%s", NOMI_SONDE[i]);
        gtk_widget_add_css_class(o->sonda[i], classe);
        gtk_widget_set_can_target(o->sonda[i], FALSE);
        gtk_widget_set_parent(o->sonda[i], GTK_WIDGET(o));
    }
}

static PhDskDisegno *disegno_nuovo(Quale q)
{
    PhDskDisegno *o = g_object_new(PH_TIPO_DISEGNO, NULL);
    o->quale = q;
    return o;
}

/* Il widget e' uno del nostro tipo e del tipo atteso (altrimenti la chiamata non fa nulla). */
static PhDskDisegno *disegno_di(GtkWidget *w, Quale q)
{
    g_return_val_if_fail(w != NULL && PH_IS_DSK_DISEGNO(w), NULL);
    PhDskDisegno *o = PH_DSK_DISEGNO(w);
    g_return_val_if_fail(o->quale == q, NULL);
    return o;
}

static void etichetta_accessibile(GtkWidget *w, const char *testo)
{
    gtk_accessible_update_property(GTK_ACCESSIBLE(w), GTK_ACCESSIBLE_PROPERTY_LABEL, testo, -1);
}

/* Copia sicura in un buffer fisso (un testo lungo si taglia, mai oltre il buffer). */
static void copia(char *dst, size_t cap, const char *src)
{
    g_strlcpy(dst, src != NULL ? src : "", cap);
    if (!g_utf8_validate(dst, -1, NULL)) { /* tagliato a meta' di un carattere: si accorcia */
        const char *fine;
        g_utf8_validate(dst, -1, &fine);
        dst[fine - dst] = '\0';
    }
}

/* ---- Anello: interfaccia ------------------------------------------------------------------------------- */
GtkWidget *ph_dsk_anello_nuovo(void)
{
    PhDskDisegno *o = disegno_nuovo(D_ANELLO);
    copia(o->anello.testo, sizeof o->anello.testo, "--");
    return GTK_WIDGET(o);
}

void ph_dsk_anello_imposta(GtkWidget *w, double percento, const char *testo, PhDskAccento acc)
{
    PhDskDisegno *o = disegno_di(w, D_ANELLO);
    if (o == NULL)
        return;
    o->anello.noto = isfinite(percento);
    o->anello.percento = percento;
    o->anello.acc = ph_dsk_accento_valido((int)acc, PH_DSK_ACC_N);
    if (testo != NULL)
        copia(o->anello.testo, sizeof o->anello.testo, testo);
    else if (o->anello.noto)
        g_snprintf(o->anello.testo, sizeof o->anello.testo, "%d%%", ph_dsk_percento_valido(percento));
    else
        copia(o->anello.testo, sizeof o->anello.testo, "--");
    etichetta_accessibile(w, o->anello.testo);
    gtk_widget_queue_draw(w);
}

/* ---- Fette: interfaccia ------------------------------------------------------------------------------------ */
/* ---- Contenitore con la sua disposizione (GtkLayoutManager) ----------------------------------------------
 * Legenda delle fette e riga d'elenco dispongono i figli in base alla larghezza ricevuta dentro
 * `measure` e `allocate` di un layout manager: nessun richiamo a ogni fotogramma (terrebbe acceso
 * l'orologio dei fotogrammi di GTK anche a desktop fermo) e nessun figlio staccato e riattaccato. */
#define PH_TIPO_SCATOLA (ph_dsk_scatola_get_type())
G_DECLARE_FINAL_TYPE(PhDskScatola, ph_dsk_scatola, PH, DSK_SCATOLA, GtkWidget)
struct _PhDskScatola {
    GtkWidget genitore;
};
G_DEFINE_FINAL_TYPE(PhDskScatola, ph_dsk_scatola, GTK_TYPE_WIDGET)

static void scatola_dispose(GObject *obj)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(GTK_WIDGET(obj))) != NULL)
        gtk_widget_unparent(c);
    G_OBJECT_CLASS(ph_dsk_scatola_parent_class)->dispose(obj);
}

static void ph_dsk_scatola_class_init(PhDskScatolaClass *k)
{
    G_OBJECT_CLASS(k)->dispose = scatola_dispose;
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(k), "ph-desktop-scatola");
}

static void ph_dsk_scatola_init(PhDskScatola *s)
{
    (void)s;
}

/* Un contenitore con quella disposizione (ne prende il possesso). */
static GtkWidget *scatola_nuova(GtkLayoutManager *lm)
{
    GtkWidget *s = g_object_new(PH_TIPO_SCATOLA, NULL);
    gtk_widget_set_layout_manager(s, lm);
    return s;
}

/* Alloca un figlio senza scendere sotto la sua misura minima (altrimenti GTK protesta). */
static void figlio_alloca(GtkWidget *c, int x, int y, int w, int h)
{
    int minl = 0, minh = 0;
    gtk_widget_measure(c, GTK_ORIENTATION_HORIZONTAL, -1, &minl, NULL, NULL, NULL);
    if (w < minl)
        w = minl;
    gtk_widget_measure(c, GTK_ORIENTATION_VERTICAL, w, &minh, NULL, NULL, NULL);
    if (h < minh)
        h = minh;
    GskTransform *t = gsk_transform_translate(NULL, &GRAPHENE_POINT_INIT((float)x, (float)y));
    gtk_widget_allocate(c, w, h, -1, t);
}

/* ---- Legenda: 1 o 2 colonne ------------------------------------------------------------------------------ */
#define LEGENDA_SPAZIO 14 /* tra le due colonne */
#define LEGENDA_SPAZIO_RIGHE 6
#define PH_TIPO_LEGENDA_LM (ph_dsk_legenda_lm_get_type())
G_DECLARE_FINAL_TYPE(PhDskLegendaLm, ph_dsk_legenda_lm, PH, DSK_LEGENDA_LM, GtkLayoutManager)
struct _PhDskLegendaLm {
    GtkLayoutManager genitore;
};
G_DEFINE_FINAL_TYPE(PhDskLegendaLm, ph_dsk_legenda_lm, GTK_TYPE_LAYOUT_MANAGER)

typedef struct {
    size_t n;
    int larg_max; /* larghezza che la voce piu' larga vuole per intero (chiave "ph-dsk-larg" sul figlio) */
    int min_max;  /* la piu' larga delle misure minime */
} LegendaInfo;

static LegendaInfo legenda_info(GtkWidget *w)
{
    LegendaInfo i = {0, 0, 0};
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (!gtk_widget_should_layout(c))
            continue;
        int minl = 0;
        gtk_widget_measure(c, GTK_ORIENTATION_HORIZONTAL, -1, &minl, NULL, NULL, NULL);
        i.n++;
        i.larg_max = MAX(i.larg_max, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(c), "ph-dsk-larg")));
        i.min_max = MAX(i.min_max, minl);
    }
    return i;
}

static int legenda_larghezza_colonna(int larghezza, int colonne)
{
    int cw = (larghezza - (colonne - 1) * LEGENDA_SPAZIO) / colonne;
    return cw < 1 ? 1 : cw;
}

/* Altezza di una riga di voci alla larghezza di colonna cw. */
static int legenda_altezza_voce(GtkWidget *w, int cw)
{
    int h = 0;
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (!gtk_widget_should_layout(c))
            continue;
        int minl = 0, minh = 0;
        gtk_widget_measure(c, GTK_ORIENTATION_HORIZONTAL, -1, &minl, NULL, NULL, NULL);
        gtk_widget_measure(c, GTK_ORIENTATION_VERTICAL, MAX(cw, minl), &minh, NULL, NULL, NULL);
        h = MAX(h, minh);
    }
    return h;
}

static GtkSizeRequestMode legenda_modo(GtkLayoutManager *m, GtkWidget *w)
{
    (void)m;
    (void)w;
    return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void legenda_misura(GtkLayoutManager *m, GtkWidget *w, GtkOrientation o, int per, int *min, int *nat,
                           int *min_base, int *nat_base)
{
    (void)m;
    *min_base = *nat_base = -1;
    LegendaInfo i = legenda_info(w);
    if (o == GTK_ORIENTATION_HORIZONTAL) {
        *min = i.min_max;
        *nat = MAX(i.min_max, i.n > 1 ? 2 * i.larg_max + LEGENDA_SPAZIO : i.larg_max);
        return;
    }
    int colonne = per >= 0 ? ph_dsk_legenda_colonne(per, i.larg_max, LEGENDA_SPAZIO, i.n) : (i.n > 1 ? 2 : 1);
    int cw = per >= 0 ? legenda_larghezza_colonna(per, colonne) : i.larg_max;
    int righe = (int)((i.n + (size_t)colonne - 1) / (size_t)colonne);
    int h = i.n == 0 ? 0 : righe * legenda_altezza_voce(w, cw) + (righe - 1) * LEGENDA_SPAZIO_RIGHE;
    *min = *nat = h;
}

static void legenda_alloca(GtkLayoutManager *m, GtkWidget *w, int larghezza, int altezza, int base)
{
    (void)m;
    (void)altezza;
    (void)base;
    LegendaInfo i = legenda_info(w);
    int colonne = ph_dsk_legenda_colonne(larghezza, i.larg_max, LEGENDA_SPAZIO, i.n);
    int cw = legenda_larghezza_colonna(larghezza, colonne);
    int ih = legenda_altezza_voce(w, cw), k = 0;
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (!gtk_widget_should_layout(c))
            continue;
        figlio_alloca(c, (k % colonne) * (cw + LEGENDA_SPAZIO), (k / colonne) * (ih + LEGENDA_SPAZIO_RIGHE), cw, ih);
        k++;
    }
}

static void ph_dsk_legenda_lm_class_init(PhDskLegendaLmClass *k)
{
    GtkLayoutManagerClass *lc = GTK_LAYOUT_MANAGER_CLASS(k);
    lc->get_request_mode = legenda_modo;
    lc->measure = legenda_misura;
    lc->allocate = legenda_alloca;
}

static void ph_dsk_legenda_lm_init(PhDskLegendaLm *m)
{
    (void)m;
}

typedef struct {
    GtkWidget *barra, *legenda;
} FetteStato;

GtkWidget *ph_dsk_fette_nuove(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    FetteStato *st = g_new0(FetteStato, 1);
    st->barra = GTK_WIDGET(disegno_nuovo(D_FETTE));
    st->legenda = scatola_nuova(g_object_new(PH_TIPO_LEGENDA_LM, NULL));
    gtk_widget_add_css_class(st->legenda, "ph-desktop-legenda");
    gtk_widget_set_hexpand(st->barra, TRUE);
    gtk_widget_set_hexpand(st->legenda, TRUE);
    gtk_box_append(GTK_BOX(box), st->barra);
    gtk_box_append(GTK_BOX(box), st->legenda);
    gtk_widget_set_hexpand(box, TRUE);
    g_object_set_data_full(G_OBJECT(box), "ph-dsk-fette", st, g_free);
    return box;
}

static void tinta_classe(GtkWidget *w, int *corrente, int nuovo)
{
    char classe[40];
    if (*corrente >= 0) {
        g_snprintf(classe, sizeof classe, "ph-desktop-tinta-%s", NOMI_SONDE[*corrente]);
        gtk_widget_remove_css_class(w, classe);
    }
    g_snprintf(classe, sizeof classe, "ph-desktop-tinta-%s", NOMI_SONDE[nuovo]);
    gtk_widget_add_css_class(w, classe);
    *corrente = nuovo;
}

static GtkWidget *etichetta_testo(const char *classe, const char *testo, bool ellissi)
{
    GtkWidget *l = gtk_label_new(testo);
    gtk_widget_add_css_class(l, classe);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    if (ellissi) {
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(l), 1); /* nessuna larghezza minima */
    }
    return l;
}

/* Larghezza in px che il testo vuole per intero, nel font dell'etichetta. */
static double larghezza_etichetta(GtkWidget *l, const char *testo)
{
    PangoLayout *lay = gtk_widget_create_pango_layout(l, testo);
    int w, h;
    pango_layout_get_pixel_size(lay, &w, &h);
    g_object_unref(lay);
    return w;
}

void ph_dsk_fette_imposta(GtkWidget *w, const PhDskFetta *f, size_t n, int64_t totale)
{
    FetteStato *st = g_object_get_data(G_OBJECT(w), "ph-dsk-fette");
    g_return_if_fail(st != NULL);
    if (f == NULL)
        n = 0;
    if (n > PH_DSK_FETTE_MAX)
        n = PH_DSK_FETTE_MAX;
    PhDskDisegno *o = PH_DSK_DISEGNO(st->barra);
    o->fette.n = n;
    o->fette.totale = totale;
    for (size_t i = 0; i < n; i++) {
        o->fette.valori[i] = f[i].valore;
        o->fette.acc[i] = ph_dsk_accento_valido((int)f[i].acc, PH_DSK_ACC_N);
    }
    gtk_widget_queue_draw(st->barra);
    /* Legenda: due colonne (una se i nomi non ci stanno: la sceglie il suo layout manager), quadratino
     * colorato, nome (con i puntini) e valore a destra. I testi arrivano dal telefono: UTF-8 reso
     * valido prima di mostrarli. */
    for (GtkWidget *v; (v = gtk_widget_get_first_child(st->legenda)) != NULL;)
        gtk_widget_unparent(v);
    for (size_t i = 0; i < n; i++) {
        GtkWidget *riga = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *quadro = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(quadro, "ph-desktop-legenda-quadrato");
        gtk_widget_set_valign(quadro, GTK_ALIGN_CENTER);
        int corrente = -1;
        tinta_classe(quadro, &corrente, o->fette.acc[i]);
        char *nome_ok = g_utf8_make_valid(f[i].nome != NULL ? f[i].nome : "", -1);
        char *valore_ok = g_utf8_make_valid(f[i].valore_testo != NULL ? f[i].valore_testo : "", -1);
        GtkWidget *nome = etichetta_testo("ph-desktop-legenda-nome", nome_ok, true);
        gtk_widget_set_hexpand(nome, TRUE);
        GtkWidget *valore = etichetta_testo("ph-desktop-legenda-valore", valore_ok, false);
        double larg = larghezza_etichetta(nome, nome_ok) + larghezza_etichetta(valore, valore_ok) + 9 + 2 * 6 + 4;
        g_object_set_data(G_OBJECT(riga), "ph-dsk-larg", GINT_TO_POINTER((int)ceil(larg)));
        g_free(nome_ok);
        g_free(valore_ok);
        gtk_box_append(GTK_BOX(riga), quadro);
        gtk_box_append(GTK_BOX(riga), nome);
        gtk_box_append(GTK_BOX(riga), valore);
        gtk_widget_set_parent(riga, st->legenda);
    }
    gtk_widget_queue_resize(st->legenda);
}

/* ---- Curva: interfaccia ----------------------------------------------------------------------------------------- */
GtkWidget *ph_dsk_curva_nuova(void)
{
    return GTK_WIDGET(disegno_nuovo(D_CURVA));
}

void ph_dsk_curva_imposta(GtkWidget *w, const double *t, const char *const *gradi, const char *const *ore,
                          size_t n, PhDskAccento acc)
{
    PhDskDisegno *o = disegno_di(w, D_CURVA);
    if (o == NULL)
        return;
    if (t == NULL)
        n = 0;
    if (n > PH_DSK_CURVA_MAX)
        n = PH_DSK_CURVA_MAX;
    o->curva.n = n;
    o->curva.ha_gradi = gradi != NULL;
    o->curva.ha_ore = ore != NULL;
    o->curva.acc = ph_dsk_accento_valido((int)acc, PH_DSK_ACC_N);
    for (size_t i = 0; i < n; i++) {
        o->curva.t[i] = t[i];
        copia(o->curva.gradi[i], sizeof o->curva.gradi[i], gradi != NULL ? gradi[i] : NULL);
        copia(o->curva.ore[i], sizeof o->curva.ore[i], ore != NULL ? ore[i] : NULL);
    }
    gtk_widget_queue_draw(w);
}

/* ---- Tratto: interfaccia ------------------------------------------------------------------------------------------ */
GtkWidget *ph_dsk_tratto_nuovo(void)
{
    PhDskDisegno *o = disegno_nuovo(D_TRATTO);
    gtk_widget_set_hexpand(GTK_WIDGET(o), TRUE);
    gtk_widget_set_valign(GTK_WIDGET(o), GTK_ALIGN_CENTER);
    return GTK_WIDGET(o);
}

void ph_dsk_tratto_imposta(GtkWidget *w, bool noto, double min, double max, double scala_min, double scala_max,
                           PhDskAccento da, PhDskAccento a)
{
    PhDskDisegno *o = disegno_di(w, D_TRATTO);
    if (o == NULL)
        return;
    o->tratto.noto = noto;
    o->tratto.min = min;
    o->tratto.max = max;
    o->tratto.scala_min = scala_min;
    o->tratto.scala_max = scala_max;
    o->tratto.da = ph_dsk_accento_valido((int)da, PH_DSK_ACC_N);
    o->tratto.a = ph_dsk_accento_valido((int)a, PH_DSK_ACC_N);
    gtk_widget_queue_draw(w);
}

/* ---- Numero grande: interfaccia ----------------------------------------------------------------------------------- */
GtkWidget *ph_dsk_numero_nuovo(void)
{
    PhDskDisegno *o = disegno_nuovo(D_NUMERO);
    gtk_widget_set_hexpand(GTK_WIDGET(o), TRUE);
    return GTK_WIDGET(o);
}

void ph_dsk_numero_imposta(GtkWidget *w, const char *etichetta, const char *numero, const char *nota)
{
    PhDskDisegno *o = disegno_di(w, D_NUMERO);
    if (o == NULL)
        return;
    copia(o->numero.etichetta, sizeof o->numero.etichetta, etichetta);
    copia(o->numero.numero, sizeof o->numero.numero, numero);
    copia(o->numero.nota, sizeof o->numero.nota, nota);
    char *a11y = g_strjoin(" ", o->numero.etichetta, o->numero.numero, o->numero.nota, NULL);
    etichetta_accessibile(w, a11y);
    g_free(a11y);
    gtk_widget_queue_draw(w);
}

/* ---- Riga d'elenco -------------------------------------------------------------------------------------------------- */
#define RIGA_CHIP 32
#define RIGA_SPAZIO 10
#define RIGA_IMBOTTITURA 12 /* 6 + 6 del pulsante: la larghezza utile e' quella del pulsante meno questo */
#define RIGA_ORA_SOTTO_SPAZIO 8
#define PH_TIPO_RIGA_LM (ph_dsk_riga_lm_get_type())
G_DECLARE_FINAL_TYPE(PhDskRigaLm, ph_dsk_riga_lm, PH, DSK_RIGA_LM, GtkLayoutManager)
/* Disposizione della riga: tondo a sinistra, titolo e sottotitolo in colonna, ora a destra; se titolo
 * e ora non stanno affiancati (ph_dsk_riga_ora_sotto) l'ora scende sulla riga del sottotitolo, a
 * destra. Cambia con la larghezza ricevuta, dentro measure/allocate: nessun richiamo a fotogramma. */
struct _PhDskRigaLm {
    GtkLayoutManager genitore;
    GtkWidget *chip, *titolo, *sotto, *ora; /* figli della scatola (vivono quanto lei) */
    double larg_titolo, larg_ora;           /* larghezze volute dal testo per intero */
};
G_DEFINE_FINAL_TYPE(PhDskRigaLm, ph_dsk_riga_lm, GTK_TYPE_LAYOUT_MANAGER)

typedef struct {
    bool ora_vis, sotto_vis, ora_sotto;
    int ow, oh;     /* misura dell'ora */
    int tw;         /* larghezza della colonna dei testi */
    int th, sh;     /* altezza del titolo e del sottotitolo */
    int sw;         /* larghezza del sottotitolo */
    int riga2;      /* altezza della seconda riga (sottotitolo e/o ora sotto) */
} RigaGeo;

static int altezza_a(GtkWidget *c, int w)
{
    int minl = 0, minh = 0;
    gtk_widget_measure(c, GTK_ORIENTATION_HORIZONTAL, -1, &minl, NULL, NULL, NULL);
    gtk_widget_measure(c, GTK_ORIENTATION_VERTICAL, MAX(w, minl), &minh, NULL, NULL, NULL);
    return minh;
}

static void riga_geo(PhDskRigaLm *m, int larghezza, RigaGeo *g)
{
    memset(g, 0, sizeof *g);
    g->ora_vis = gtk_widget_should_layout(m->ora);
    g->sotto_vis = gtk_widget_should_layout(m->sotto);
    if (g->ora_vis) {
        gtk_widget_measure(m->ora, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &g->ow, NULL, NULL);
        g->oh = altezza_a(m->ora, g->ow);
    }
    g->ora_sotto = ph_dsk_riga_ora_sotto(larghezza, RIGA_CHIP, m->larg_titolo, g->ora_vis ? m->larg_ora : 0,
                                         RIGA_SPAZIO);
    g->tw = larghezza - RIGA_CHIP - RIGA_SPAZIO - (g->ora_vis && !g->ora_sotto ? g->ow + RIGA_SPAZIO : 0);
    if (g->tw < 1)
        g->tw = 1;
    g->th = altezza_a(m->titolo, g->tw);
    g->sw = g->tw - (g->ora_vis && g->ora_sotto ? g->ow + RIGA_ORA_SOTTO_SPAZIO : 0);
    if (g->sw < 1)
        g->sw = 1;
    g->sh = g->sotto_vis ? altezza_a(m->sotto, g->sw) : 0;
    g->riga2 = MAX(g->sh, g->ora_vis && g->ora_sotto ? g->oh : 0);
}

static GtkSizeRequestMode riga_modo(GtkLayoutManager *m, GtkWidget *w)
{
    (void)m;
    (void)w;
    return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void riga_misura(GtkLayoutManager *lm, GtkWidget *w, GtkOrientation o, int per, int *min, int *nat,
                        int *min_base, int *nat_base)
{
    (void)w;
    PhDskRigaLm *m = PH_DSK_RIGA_LM(lm);
    *min_base = *nat_base = -1;
    if (m->chip == NULL) { /* ancora senza figli */
        *min = *nat = 0;
        return;
    }
    bool ora_vis = gtk_widget_should_layout(m->ora);
    int mt = 0, ms = 0;
    gtk_widget_measure(m->titolo, GTK_ORIENTATION_HORIZONTAL, -1, &mt, NULL, NULL, NULL);
    gtk_widget_measure(m->sotto, GTK_ORIENTATION_HORIZONTAL, -1, &ms, NULL, NULL, NULL);
    int minimo = RIGA_CHIP + RIGA_SPAZIO + MAX(mt, ms);
    int naturale = RIGA_CHIP + RIGA_SPAZIO + (int)ceil(m->larg_titolo) + (ora_vis ? RIGA_SPAZIO + (int)ceil(m->larg_ora) : 0);
    if (o == GTK_ORIENTATION_HORIZONTAL) {
        *min = minimo;
        *nat = MAX(minimo, naturale);
        return;
    }
    RigaGeo g;
    riga_geo(m, per >= 0 ? MAX(per, minimo) : MAX(minimo, naturale), &g);
    *min = *nat = MAX(RIGA_CHIP, g.th + g.riga2);
}

static void riga_alloca(GtkLayoutManager *lm, GtkWidget *w, int larghezza, int altezza, int base)
{
    (void)w;
    (void)base;
    PhDskRigaLm *m = PH_DSK_RIGA_LM(lm);
    if (m->chip == NULL)
        return;
    RigaGeo g;
    riga_geo(m, larghezza, &g);
    int blocco = g.th + g.riga2, y0 = (altezza - blocco) / 2, tx = RIGA_CHIP + RIGA_SPAZIO;
    if (y0 < 0)
        y0 = 0;
    figlio_alloca(m->chip, 0, (altezza - RIGA_CHIP) / 2, RIGA_CHIP, RIGA_CHIP);
    figlio_alloca(m->titolo, tx, y0, g.tw, g.th);
    if (g.sotto_vis)
        figlio_alloca(m->sotto, tx, y0 + g.th, g.sw, g.sh);
    if (g.ora_vis) {
        if (g.ora_sotto)
            figlio_alloca(m->ora, larghezza - g.ow, y0 + g.th + (g.riga2 - g.oh) / 2, g.ow, g.oh);
        else
            figlio_alloca(m->ora, larghezza - g.ow, (altezza - g.oh) / 2, g.ow, g.oh);
    }
}

static void ph_dsk_riga_lm_class_init(PhDskRigaLmClass *k)
{
    GtkLayoutManagerClass *lc = GTK_LAYOUT_MANAGER_CLASS(k);
    lc->get_request_mode = riga_modo;
    lc->measure = riga_misura;
    lc->allocate = riga_alloca;
}

static void ph_dsk_riga_lm_init(PhDskRigaLm *m)
{
    (void)m;
}

typedef struct {
    PhDskRigaClic clic;
    void *dati;
    GtkWidget *chip, *sigla, *titolo, *sotto, *ora, *icona;
    GtkWidget *scatola;
    PhDskRigaLm *lm; /* della scatola: vive quanto lei */
    int tinta; /* accento corrente del chip (-1 = nessuno) */
} RigaStato;

static void riga_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    RigaStato *st = dati;
    if (st->clic != NULL)
        st->clic(st->dati);
}

GtkWidget *ph_dsk_riga_nuova(PhDskRigaClic clic, void *dati)
{
    RigaStato *st = g_new0(RigaStato, 1);
    st->clic = clic;
    st->dati = dati;
    st->tinta = -1;
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "ph-desktop-riga-elenco");
    gtk_widget_set_hexpand(b, TRUE);
    g_object_set_data_full(G_OBJECT(b), "ph-dsk-riga", st, g_free);
    st->lm = g_object_new(PH_TIPO_RIGA_LM, NULL);
    st->scatola = scatola_nuova(GTK_LAYOUT_MANAGER(st->lm)); /* la scatola prende il possesso */
    st->chip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(st->chip, "ph-desktop-chip");
    st->sigla = gtk_label_new("");
    gtk_widget_set_hexpand(st->sigla, TRUE);
    gtk_box_append(GTK_BOX(st->chip), st->sigla);
    st->titolo = etichetta_testo("ph-desktop-riga-titolo", "", true);
    st->sotto = etichetta_testo("ph-desktop-riga-sotto", "", true);
    st->ora = etichetta_testo("ph-desktop-riga-ora", "", false);
    st->lm->chip = st->chip;
    st->lm->titolo = st->titolo;
    st->lm->sotto = st->sotto;
    st->lm->ora = st->ora;
    gtk_widget_set_parent(st->chip, st->scatola);
    gtk_widget_set_parent(st->titolo, st->scatola);
    gtk_widget_set_parent(st->sotto, st->scatola);
    gtk_widget_set_parent(st->ora, st->scatola);
    gtk_button_set_child(GTK_BUTTON(b), st->scatola);
    if (clic != NULL)
        g_signal_connect(b, "clicked", G_CALLBACK(riga_clic), st);
    else { /* solo da guardare: niente fuoco, niente clic (il trascinamento del widget passa) */
        gtk_widget_set_can_target(b, FALSE);
        gtk_widget_set_focusable(b, FALSE);
    }
    return b;
}

void ph_dsk_riga_imposta(GtkWidget *r, const char *sigla, PhDskAccento acc, bool tondo, const char *titolo,
                         const char *sottotitolo, const char *ora)
{
    RigaStato *st = r != NULL ? g_object_get_data(G_OBJECT(r), "ph-dsk-riga") : NULL;
    g_return_if_fail(st != NULL);
    char s[16];
    if (sigla != NULL && sigla[0] != '\0')
        copia(s, sizeof s, sigla);
    else
        ph_dsk_sigla(titolo, s, sizeof s);
    if (st->icona == NULL) /* con un'icona al posto della sigla il testo non si tocca */
        gtk_label_set_text(GTK_LABEL(st->sigla), s);
    tinta_classe(st->chip, &st->tinta, ph_dsk_accento_valido((int)acc, PH_DSK_ACC_N));
    if (tondo)
        gtk_widget_add_css_class(st->chip, "ph-desktop-chip-tondo");
    else
        gtk_widget_remove_css_class(st->chip, "ph-desktop-chip-tondo");
    /* Anche i testi sono dati del telefono: UTF-8 reso valido (i byte guasti diventano U+FFFD). */
    char *t_ok = g_utf8_make_valid(titolo != NULL ? titolo : "", -1);
    char *s_ok = g_utf8_make_valid(sottotitolo != NULL ? sottotitolo : "", -1);
    char *o_ok = g_utf8_make_valid(ora != NULL ? ora : "", -1);
    gtk_label_set_text(GTK_LABEL(st->titolo), t_ok);
    gtk_label_set_text(GTK_LABEL(st->sotto), s_ok);
    gtk_widget_set_visible(st->sotto, s_ok[0] != '\0');
    gtk_label_set_text(GTK_LABEL(st->ora), o_ok);
    gtk_widget_set_visible(st->ora, o_ok[0] != '\0');
    st->lm->larg_titolo = larghezza_etichetta(st->titolo, t_ok);
    st->lm->larg_ora = larghezza_etichetta(st->ora, o_ok);
    gtk_widget_queue_resize(st->scatola); /* la disposizione dipende dalle larghezze dei testi */
    char *a11y = g_strjoin(", ", t_ok, s_ok, o_ok, NULL);
    etichetta_accessibile(r, a11y);
    g_free(a11y);
    g_free(t_ok);
    g_free(s_ok);
    g_free(o_ok);
}

void ph_dsk_riga_icona(GtkWidget *r, GtkWidget *icona)
{
    RigaStato *st = r != NULL ? g_object_get_data(G_OBJECT(r), "ph-dsk-riga") : NULL;
    g_return_if_fail(st != NULL);
    if (icona == st->icona)
        return; /* gia' messa: toglierla e rimetterla la libererebbe */
    if (st->icona != NULL) {
        gtk_box_remove(GTK_BOX(st->chip), st->icona);
        st->icona = NULL;
    }
    gtk_widget_set_visible(st->sigla, icona == NULL);
    if (icona != NULL) {
        gtk_widget_set_hexpand(icona, TRUE);
        gtk_widget_set_halign(icona, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(icona, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(st->chip), icona);
        st->icona = icona;
    }
}
