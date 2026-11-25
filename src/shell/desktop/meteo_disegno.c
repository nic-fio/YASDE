/* Disegni del widget Meteo: le icone delle condizioni (dai gruppi dei codici WMO, versione di
 * giorno e di notte), le barre della settimana e la curva delle ore. Tutto vettoriale con cairo
 * (nitido a ogni scala), disegnato qui: nessuna immagine, nessun codice di terzi. */
#include <math.h>
#include <pango/pangocairo.h>
#include <string.h>

#include "interno.h"

/* Colori e tema. Le icone delle condizioni (sole, nuvole, pioggia, neve...) e la scala azzurro-giallo-
 * arancio delle barre sono «dati»: colori fissi, scritti qui con `colore-dato`. Il TESTO e le linee del
 * grafico, invece, seguono il tema: si leggono dallo stile del widget (colore CSS = --ph-testo). */
static void usa_testo(GtkWidget *w, cairo_t *cr, double alfa)
{
    GdkRGBA c;
    gtk_widget_get_color(w, &c);
    cairo_set_source_rgba(cr, c.red, c.green, c.blue, c.alpha * alfa);
}

/* Il colore del testo del widget che sta disegnando l'icona (copiato da
 * ph_dsk_meteo_disegna_icona, che lo riceve come parametro, a ogni disegno: nessun altro lo imposta,
 * quindi non resta un colore vecchio; il disegno gira solo sul thread GTK). Serve a due cose, senza conoscere il tema:
 * un CONTORNO sottile nel colore del testo (alfa 0,35) attorno a nuvole, luna, fiocchi e nebbia,
 * che sono quasi bianchi; e, se il testo e' scuro (superficie chiara), un riempimento piu' scuro. */
static GdkRGBA testo_icona = {1, 1, 1, 1};

static bool superficie_chiara(void)
{
    double lum = 0.2126 * testo_icona.red + 0.7152 * testo_icona.green + 0.0722 * testo_icona.blue;
    return lum < 0.5; /* testo scuro = fondo chiaro */
}

/* Riempimento quasi bianco: sul fondo chiaro si scurisce (colore-dato: restano i colori delle icone). */
static void colore_chiaro(cairo_t *cr, unsigned rgb, double alfa)
{
    double k = superficie_chiara() ? 0.62 : 1.0;
    cairo_set_source_rgba(cr, k * ((rgb >> 16) & 0xff) / 255.0, k * ((rgb >> 8) & 0xff) / 255.0,
                          k * (rgb & 0xff) / 255.0, alfa);
}

static void contorno(cairo_t *cr)
{
    cairo_set_source_rgba(cr, testo_icona.red, testo_icona.green, testo_icona.blue, testo_icona.alpha * 0.35);
}

/* ---- Icone ----------------------------------------------------------------------------------- */
static void colore(cairo_t *cr, unsigned rgb, double alfa)
{
    cairo_set_source_rgba(cr, ((rgb >> 16) & 0xff) / 255.0, ((rgb >> 8) & 0xff) / 255.0, (rgb & 0xff) / 255.0, alfa);
}

/* Sole: disco con otto raggi (coordinate unitarie centrate in (0,0), raggio del disco r). */
static void sole(cairo_t *cr, double cx, double cy, double r)
{
    colore(cr, 0xfbbf24, 1);
    cairo_set_line_width(cr, r * 0.28);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int i = 0; i < 8; i++) {
        double a = i * G_PI / 4;
        cairo_move_to(cr, cx + cos(a) * r * 1.45, cy + sin(a) * r * 1.45);
        cairo_line_to(cr, cx + cos(a) * r * 1.95, cy + sin(a) * r * 1.95);
        cairo_stroke(cr);
    }
    cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
    cairo_fill(cr);
}

/* Luna: falce (disco meno un disco spostato) con due stelline. */
static void luna(cairo_t *cr, double cx, double cy, double r)
{
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, r * 1.25, 0, 2 * G_PI);
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx + r * 0.65, cy - r * 0.45, r * 1.05, 0, 2 * G_PI);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    colore_chiaro(cr, 0xe2e8f0, 1);
    cairo_fill_preserve(cr);
    contorno(cr);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    colore_chiaro(cr, 0xe2e8f0, 1);
    cairo_arc(cr, cx + r * 1.15, cy + r * 0.55, r * 0.10, 0, 2 * G_PI);
    cairo_fill(cr);
    cairo_arc(cr, cx + r * 1.55, cy - r * 0.15, r * 0.07, 0, 2 * G_PI);
    cairo_fill(cr);
}

/* Nuvola: tre cerchi e una base piatta, larga circa 2,3 s e alta 1,3 s attorno a (cx, cy). */
static void nuvola(cairo_t *cr, double cx, double cy, double s, unsigned rgb)
{
    /* Prima il contorno (tratto di 2 px sotto le forme: fuori ne resta 1 px, dentro lo coprono le
     * forme), poi il riempimento. */
    for (int passo = 0; passo < 2; passo++) {
        if (passo == 0) {
            contorno(cr);
            cairo_set_line_width(cr, 2.0);
        } else {
            colore_chiaro(cr, rgb, 1);
        }
        for (int k = 0; k < 4; k++) {
            cairo_new_path(cr);
            if (k == 0)
                cairo_arc(cr, cx - 0.78 * s, cy + 0.20 * s, 0.50 * s, 0, 2 * G_PI);
            else if (k == 1)
                cairo_arc(cr, cx - 0.12 * s, cy - 0.12 * s, 0.72 * s, 0, 2 * G_PI);
            else if (k == 2)
                cairo_arc(cr, cx + 0.62 * s, cy + 0.14 * s, 0.56 * s, 0, 2 * G_PI);
            else
                cairo_rectangle(cr, cx - 0.78 * s, cy + 0.20 * s, 1.40 * s, 0.50 * s);
            if (passo == 0)
                cairo_stroke(cr);
            else
                cairo_fill(cr);
        }
    }
}

/* Condizione non nota: una nuvola solo tratteggiata, neutra (non dice nulla del tempo). */
static void nuvola_ignota(cairo_t *cr, double cx, double cy, double s)
{
    const double d = G_PI / 180.0;
    double tratti[2] = {s * 0.22, s * 0.20};
    colore_chiaro(cr, 0x9aa4b5, 0.9);
    cairo_set_line_width(cr, s * 0.13);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_dash(cr, tratti, 2, 0);
    cairo_new_path(cr);
    cairo_arc(cr, cx - 0.78 * s, cy + 0.20 * s, 0.50 * s, 100 * d, 250 * d);
    cairo_arc(cr, cx - 0.12 * s, cy - 0.12 * s, 0.72 * s, 205 * d, 335 * d);
    cairo_arc(cr, cx + 0.62 * s, cy + 0.14 * s, 0.56 * s, 285 * d, 90 * d);
    cairo_close_path(cr);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);
}

static void goccia(cairo_t *cr, double x, double y, double lun, double spess)
{
    colore(cr, 0x60a5fa, 1);
    cairo_set_line_width(cr, spess);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x - lun * 0.35, y + lun);
    cairo_stroke(cr);
}

static void fiocco(cairo_t *cr, double x, double y, double r)
{
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int passo = 0; passo < 2; passo++) { /* prima il contorno (piu' largo), poi il fiocco */
        if (passo == 0) {
            contorno(cr);
            cairo_set_line_width(cr, r * 0.45 + 2.0);
        } else {
            colore_chiaro(cr, 0xf8fafc, 1);
            cairo_set_line_width(cr, r * 0.45);
        }
        for (int i = 0; i < 3; i++) {
            double a = i * G_PI / 3;
            cairo_move_to(cr, x - cos(a) * r, y - sin(a) * r);
            cairo_line_to(cr, x + cos(a) * r, y + sin(a) * r);
            cairo_stroke(cr);
        }
    }
}

static void fulmine(cairo_t *cr, double x, double y, double s)
{
    colore(cr, 0xfbbf24, 1);
    cairo_new_path(cr);
    cairo_move_to(cr, x + 0.10 * s, y - 0.30 * s);
    cairo_line_to(cr, x - 0.35 * s, y + 0.55 * s);
    cairo_line_to(cr, x - 0.02 * s, y + 0.55 * s);
    cairo_line_to(cr, x - 0.22 * s, y + 1.20 * s);
    cairo_line_to(cr, x + 0.45 * s, y + 0.25 * s);
    cairo_line_to(cr, x + 0.08 * s, y + 0.25 * s);
    cairo_close_path(cr);
    cairo_fill(cr);
}

void ph_dsk_meteo_disegna_icona(cairo_t *cr, const GdkRGBA *testo, int codice, int giorno, double x, double y, double l)
{
    testo_icona = *testo;
    PhDskCond g = ph_dsk_meteo_gruppo(codice);
    if (g == PH_DSK_COND_NESSUNA) {
        /* Codice ignoto: un'icona neutra, mai una a caso. */
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
        nuvola_ignota(cr, x + l / 2, y + l / 2, l / 2.6);
        cairo_restore(cr);
        return;
    }
    bool di_giorno = giorno != 0; /* non noto (-1) = di giorno */
    double cx = x + l / 2, cy = y + l / 2, u = l / 2.6; /* u = misura base: tutto in proporzione */
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    switch (g) {
    case PH_DSK_COND_SERENO:
        if (di_giorno)
            sole(cr, cx, cy, u * 0.52);
        else
            luna(cr, cx, cy, u * 0.58);
        break;
    case PH_DSK_COND_POCO_NUVOLOSO:
        if (di_giorno)
            sole(cr, cx - u * 0.45, cy - u * 0.50, u * 0.34);
        else
            luna(cr, cx - u * 0.45, cy - u * 0.50, u * 0.38);
        nuvola(cr, cx + u * 0.12, cy + u * 0.30, u * 0.62, 0xe2e8f0);
        break;
    case PH_DSK_COND_NUVOLOSO:
        nuvola(cr, cx + u * 0.34, cy - u * 0.32, u * 0.42, 0x94a3b8);
        nuvola(cr, cx - u * 0.05, cy + u * 0.18, u * 0.68, 0xe2e8f0);
        break;
    case PH_DSK_COND_NEBBIA:
        nuvola(cr, cx, cy - u * 0.38, u * 0.58, 0xcbd5e1);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        for (int passo = 0; passo < 2; passo++) {
            if (passo == 0) {
                contorno(cr);
                cairo_set_line_width(cr, u * 0.17 + 2.0);
            } else {
                colore_chiaro(cr, 0xcbd5e1, 0.9);
                cairo_set_line_width(cr, u * 0.17);
            }
            for (int i = 0; i < 3; i++) {
                double yy = cy + u * (0.38 + 0.36 * i), mezza = u * (i == 1 ? 0.95 : 0.78);
                cairo_move_to(cr, cx - mezza, yy);
                cairo_line_to(cr, cx + mezza, yy);
                cairo_stroke(cr);
            }
        }
        break;
    case PH_DSK_COND_PIOGGERELLA:
        nuvola(cr, cx, cy - u * 0.28, u * 0.64, 0xb6c2d3);
        for (int i = 0; i < 3; i++)
            goccia(cr, cx - u * 0.55 + i * u * 0.55, cy + u * 0.62, u * 0.32, u * 0.13);
        break;
    case PH_DSK_COND_PIOGGIA:
        nuvola(cr, cx, cy - u * 0.30, u * 0.64, 0x94a3b8);
        for (int i = 0; i < 3; i++)
            goccia(cr, cx - u * 0.58 + i * u * 0.58, cy + u * 0.50, u * 0.62, u * 0.16);
        break;
    case PH_DSK_COND_NEVE:
        nuvola(cr, cx, cy - u * 0.30, u * 0.64, 0xb6c2d3);
        fiocco(cr, cx - u * 0.60, cy + u * 0.78, u * 0.20);
        fiocco(cr, cx, cy + u * 1.00, u * 0.20);
        fiocco(cr, cx + u * 0.60, cy + u * 0.78, u * 0.20);
        break;
    case PH_DSK_COND_TEMPORALE:
        nuvola(cr, cx, cy - u * 0.36, u * 0.66, 0x64748b);
        fulmine(cr, cx + u * 0.05, cy + u * 0.30, u * 0.80);
        break;
    default:
        break;
    }
    cairo_restore(cr);
}

typedef struct {
    int codice, giorno, lato;
} DatiIcona;

static void icona_disegna(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer dati)
{
    const DatiIcona *d = dati;
    GdkRGBA testo;
    gtk_widget_get_color(GTK_WIDGET(a), &testo);
    double l = w < h ? w : h;
    ph_dsk_meteo_disegna_icona(cr, &testo, d->codice, d->giorno, (w - l) / 2.0, (h - l) / 2.0, l);
}

GtkWidget *ph_dsk_meteo_icona_nuova(int codice, int giorno, int lato)
{
    GtkWidget *a = gtk_drawing_area_new();
    DatiIcona *d = g_new0(DatiIcona, 1);
    d->codice = codice;
    d->giorno = giorno;
    d->lato = lato;
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(a), lato);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(a), lato);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(a), icona_disegna, d, g_free);
    gtk_widget_set_halign(a, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(a, GTK_ALIGN_CENTER);
    return a;
}

/* ---- Barra di un giorno (minima-massima) ------------------------------------------------------------ */
typedef struct {
    double da, a;
    bool c_e;
} DatiBarra;

static void barra_disegna(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer dati)
{
    const DatiBarra *d = dati;
    double spess = 5, y = h / 2.0 - spess / 2.0;
    usa_testo(GTK_WIDGET(area), cr, 0.18); /* binario della barra */
    cairo_new_sub_path(cr);
    cairo_arc(cr, spess / 2, y + spess / 2, spess / 2, G_PI / 2, 3 * G_PI / 2);
    cairo_arc(cr, w - spess / 2, y + spess / 2, spess / 2, -G_PI / 2, G_PI / 2);
    cairo_close_path(cr);
    cairo_fill(cr);
    if (!d->c_e)
        return;
    double x0 = d->da * w, x1 = d->a * w;
    if (x1 - x0 < spess)
        x1 = x0 + spess;
    /* Dal freddo al caldo: azzurro, giallo, arancio (come la scala dei colori del tempo).
     * colore-dato: i tre colori che seguono sono una scala di temperatura, non il tema. */
    cairo_pattern_t *p = cairo_pattern_create_linear(0, 0, w, 0);
    cairo_pattern_add_color_stop_rgb(p, 0.0, 0x60 / 255.0, 0xa5 / 255.0, 0xfa / 255.0);
    cairo_pattern_add_color_stop_rgb(p, 0.55, 0xfb / 255.0, 0xbf / 255.0, 0x24 / 255.0);
    cairo_pattern_add_color_stop_rgb(p, 1.0, 0xf9 / 255.0, 0x73 / 255.0, 0x16 / 255.0);
    cairo_set_source(cr, p);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x0 + spess / 2, y + spess / 2, spess / 2, G_PI / 2, 3 * G_PI / 2);
    cairo_arc(cr, x1 - spess / 2, y + spess / 2, spess / 2, -G_PI / 2, G_PI / 2);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
}

GtkWidget *ph_dsk_meteo_barra_nuova(bool c_e, double da, double a)
{
    GtkWidget *w = gtk_drawing_area_new();
    DatiBarra *d = g_new0(DatiBarra, 1);
    d->c_e = c_e;
    d->da = da;
    d->a = a;
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(w), 12);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(w), 24);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(w), barra_disegna, d, g_free);
    gtk_widget_set_hexpand(w, TRUE);
    gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
    return w;
}

/* ---- Curva delle temperature ---------------------------------------------------------------------------- */
typedef struct {
    double t[PH_METEO_ORE_MAX];
    char ora[PH_METEO_ORE_MAX][4];
    char etichetta_min[16], etichetta_max[16];
    GtkWidget *sonda; /* fratello vuoto che il CSS colora con l'accento (vive quanto la curva) */
    size_t n;
    int altezza;
} DatiCurva;

static void testo_a(cairo_t *cr, const char *s, double x, double y, double px, int ancora)
{
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string("Inter, system-ui");
    pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
    pango_layout_set_font_description(l, fd);
    pango_layout_set_text(l, s, -1);
    int w, h;
    pango_layout_get_pixel_size(l, &w, &h);
    double x0 = ancora == 0 ? x : (ancora == 1 ? x - w / 2.0 : x - w);
    cairo_move_to(cr, x0, y - h / 2.0);
    pango_cairo_show_layout(cr, l);
    pango_font_description_free(fd);
    g_object_unref(l);
}

static void curva_disegna(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer dati)
{
    const DatiCurva *d = dati;
    GtkWidget *wa = GTK_WIDGET(area);
    GdkRGBA tc, ac;
    gtk_widget_get_color(wa, &tc); /* colore del testo del tema: punto adesso, etichette */
    gtk_widget_get_color(d->sonda, &ac); /* accento del tema, dalla sonda (colore CSS --ph-accento) */
    if (d->n < 2)
        return;
    double tmin = d->t[0], tmax = d->t[0];
    for (size_t i = 1; i < d->n; i++) {
        tmin = fmin(tmin, d->t[i]);
        tmax = fmax(tmax, d->t[i]);
    }
    if (tmax - tmin < 2.0) { /* curva quasi piatta: non si ingrandisce il rumore */
        double c = (tmax + tmin) / 2.0;
        tmin = c - 1.0;
        tmax = c + 1.0;
    }
    const double margine_x = 6, alto_testo = 12, basso_testo = 14;
    double y0 = alto_testo + 4, y1 = h - basso_testo - 2;
    double px[PH_METEO_ORE_MAX], py[PH_METEO_ORE_MAX];
    for (size_t i = 0; i < d->n; i++) {
        px[i] = margine_x + (w - 2 * margine_x) * (double)i / (double)(d->n - 1);
        py[i] = y1 - (y1 - y0) * (d->t[i] - tmin) / (tmax - tmin);
    }
    /* Linea morbida (Catmull-Rom -> Bezier) e area sotto, sfumata verso il basso. */
    cairo_new_path(cr);
    cairo_move_to(cr, px[0], py[0]);
    for (size_t i = 0; i + 1 < d->n; i++) {
        double xa = i > 0 ? px[i - 1] : px[i], ya = i > 0 ? py[i - 1] : py[i];
        double xb = px[i], yb = py[i], xc = px[i + 1], yc = py[i + 1];
        double xd = i + 2 < d->n ? px[i + 2] : px[i + 1], yd = i + 2 < d->n ? py[i + 2] : py[i + 1];
        cairo_curve_to(cr, xb + (xc - xa) / 6.0, yb + (yc - ya) / 6.0, xc - (xd - xb) / 6.0, yc - (yd - yb) / 6.0, xc, yc);
    }
    cairo_path_t *linea = cairo_copy_path(cr);
    cairo_line_to(cr, px[d->n - 1], h - basso_testo);
    cairo_line_to(cr, px[0], h - basso_testo);
    cairo_close_path(cr);
    cairo_pattern_t *g = cairo_pattern_create_linear(0, y0, 0, h - basso_testo);
    cairo_pattern_add_color_stop_rgba(g, 0, ac.red, ac.green, ac.blue, ac.alpha * 0.40);
    cairo_pattern_add_color_stop_rgba(g, 1, ac.red, ac.green, ac.blue, 0.0);
    cairo_set_source(cr, g);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
    cairo_new_path(cr);
    cairo_append_path(cr, linea);
    cairo_path_destroy(linea);
    cairo_set_source_rgba(cr, ac.red, ac.green, ac.blue, ac.alpha);
    cairo_set_line_width(cr, 2.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_stroke(cr);
    /* Punto di adesso. */
    cairo_set_source_rgba(cr, tc.red, tc.green, tc.blue, tc.alpha);
    cairo_arc(cr, px[0], py[0], 3.5, 0, 2 * G_PI);
    cairo_fill(cr);
    /* Minima e massima con il loro valore. */
    size_t imin = 0, imax = 0;
    for (size_t i = 1; i < d->n; i++) {
        if (d->t[i] < d->t[imin])
            imin = i;
        if (d->t[i] > d->t[imax])
            imax = i;
    }
    usa_testo(wa, cr, 0.95);
    if (imax != 0 || d->n < 3)
        testo_a(cr, d->etichetta_max, px[imax], py[imax] - 8, 10, imax < d->n / 4 ? 0 : (imax > d->n * 3 / 4 ? 2 : 1));
    if (imin != imax && imin != 0)
        testo_a(cr, d->etichetta_min, px[imin], py[imin] + 9, 10, imin < d->n / 4 ? 0 : (imin > d->n * 3 / 4 ? 2 : 1));
    /* Ore: una ogni tre. */
    usa_testo(wa, cr, 0.8); /* ore: secondario */
    for (size_t i = 0; i < d->n; i += 3)
        testo_a(cr, d->ora[i], px[i], h - 6, 9, i == 0 ? 0 : 1);
}

GtkWidget *ph_dsk_meteo_curva_nuova(const double *t, const char (*ore)[4], size_t n, int altezza)
{
    DatiCurva *d = g_new0(DatiCurva, 1);
    d->n = n > PH_METEO_ORE_MAX ? PH_METEO_ORE_MAX : n;
    d->altezza = altezza;
    for (size_t i = 0; i < d->n; i++) {
        d->t[i] = t[i];
        memcpy(d->ora[i], ore[i], 4);
    }
    double mn = d->n > 0 ? d->t[0] : 0, mx = mn;
    for (size_t i = 1; i < d->n; i++) {
        mn = fmin(mn, d->t[i]);
        mx = fmax(mx, d->t[i]);
    }
    ph_dsk_meteo_temp(mn, d->etichetta_min, sizeof d->etichetta_min);
    ph_dsk_meteo_temp(mx, d->etichetta_max, sizeof d->etichetta_max);
    GtkWidget *w = gtk_drawing_area_new();
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(w), altezza);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(w), 40);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(w), curva_disegna, d, g_free);
    gtk_widget_set_hexpand(w, TRUE);
    /* Contenitore con la curva e la sonda dell'accento (stessa classe dell'orologio). */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(box, TRUE);
    d->sonda = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(d->sonda, "ph-desktop-orologio-accento");
    gtk_widget_set_can_target(d->sonda, FALSE);
    gtk_box_append(GTK_BOX(box), w);
    gtk_box_append(GTK_BOX(box), d->sonda);
    return box;
}
