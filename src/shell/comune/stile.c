/* Stile, icone del mockup e segnaposto (shell.h). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "interno.h"
#include "shell/shell.h"

/* Generato da glib-compile-resources (--manual-register). */
void ph_risorse_comune_register_resource(void);

void ph_shell_comune_risorse(void)
{
    static gsize fatto = 0;
    if (g_once_init_enter(&fatto)) {
        ph_risorse_comune_register_resource();
        g_once_init_leave(&fatto, 1);
    }
}

/* ---- CSS ---- */
void ph_shell_stile_carica(const char *percorso_risorsa)
{
    static GHashTable *caricati;
    GdkDisplay *d = gdk_display_get_default();
    if (d == NULL || percorso_risorsa == NULL)
        return;
    if (caricati == NULL)
        caricati = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (g_hash_table_contains(caricati, percorso_risorsa))
        return;
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(p, percorso_risorsa);
    gtk_style_context_add_provider_for_display(d, GTK_STYLE_PROVIDER(p),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
    g_hash_table_add(caricati, g_strdup(percorso_risorsa));
}

static void errore_css(GtkCssProvider *p, GtkCssSection *sez, const GError *err, gpointer dati)
{
    (void)p;
    int *n = dati;
    char *dove = gtk_css_section_to_string(sez);
    fprintf(stderr, "CSS: %s: %s\n", dove, err->message);
    g_free(dove);
    (*n)++;
}

int ph_shell_stile_controlla(const char *percorso_risorsa)
{
    int errori = 0;
    if (!g_resources_get_info(percorso_risorsa, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL, NULL, NULL)) {
        fprintf(stderr, "CSS: risorsa %s assente\n", percorso_risorsa);
        return 1;
    }
    GtkCssProvider *p = gtk_css_provider_new();
    g_signal_connect(p, "parsing-error", G_CALLBACK(errore_css), &errori);
    gtk_css_provider_load_from_resource(p, percorso_risorsa);
    g_object_unref(p);
    return errori;
}

/* ---- Icone ----
 * Tracciati copiati dal mockup (Main.dc.html); rect e circle riscritti come
 * tracciati equivalenti. */
const PhShellIcona PH_ICONA_SCHERMO_INTERO = {"M3 6V3h3M10 3h3v3M13 10v3h-3M6 13H3v-3", NULL, 16, 16, 1.6f, 14, 14, false};
const PhShellIcona PH_ICONA_RIDUCI = {"M4 11.5h8", NULL, 16, 16, 1.6f, 14, 14, false};
const PhShellIcona PH_ICONA_INGRANDISCI_HOST = {
    "M4 3h8a1 1 0 0 1 1 1v8a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z", NULL, 16, 16, 1.6f, 14, 14, false};
const PhShellIcona PH_ICONA_RIPRISTINA_HOST = {
    "M4 5.5h5.5a1 1 0 0 1 1 1V12a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V6.5a1 1 0 0 1 1-1z"
    "M6 5.5V3.8a.8.8 0 0 1 .8-.8h5.4a.8.8 0 0 1 .8.8v5.4a.8.8 0 0 1-.8.8H10.5",
    NULL, 16, 16, 1.6f, 14, 14, false};
const PhShellIcona PH_ICONA_CHIUDI_HOST = {"M4 4l8 8M12 4l-8 8", NULL, 16, 16, 1.6f, 14, 14, false};
const PhShellIcona PH_ICONA_INDIETRO = {"M9.5 3.5 5 8l4.5 4.5", NULL, 16, 16, 1.5f, 16, 16, true};
const PhShellIcona PH_ICONA_SCREENSHOT = {
    "M3.5 4.5h9a1.5 1.5 0 0 1 1.5 1.5v5a1.5 1.5 0 0 1-1.5 1.5h-9A1.5 1.5 0 0 1 2 11V6a1.5 1.5 0 0 1 1.5-1.5z"
    "M10 8.5a2 2 0 1 1-4 0a2 2 0 1 1 4 0z",
    NULL, 16, 16, 1.5f, 16, 16, true};
const PhShellIcona PH_ICONA_REGISTRA = {"M12 8a4 4 0 1 1-8 0a4 4 0 1 1 8 0z", NULL, 16, 16, 1.5f, 16, 16, true};
/* Non nel mockup: "⋮" del menu altri comandi (decisione del committente). */
const PhShellIcona PH_ICONA_ALTRI = {
    NULL,
    "M9.2 3.5a1.2 1.2 0 1 1-2.4 0a1.2 1.2 0 1 1 2.4 0z"
    "M9.2 8a1.2 1.2 0 1 1-2.4 0a1.2 1.2 0 1 1 2.4 0z"
    "M9.2 12.5a1.2 1.2 0 1 1-2.4 0a1.2 1.2 0 1 1 2.4 0z",
    16, 16, 1.5f, 16, 16, true};
/* Finestre interne (DESIGN_FINESTRE.md): minimizza = linea da 10 px, ingrandisci = quadrato 10x10 esterni
 * (linea d'asse a 3.75..12.25), chiudi = due linee da 13 px a +-45 gradi, tutto centrato in 8,8. */
const PhShellIcona PH_ICONA_RIDUCI_FINESTRA = {"M3 8h10", NULL, 16, 16, 1.5f, 16, 16, true};
const PhShellIcona PH_ICONA_INGRANDISCI_FINESTRA = {
    "M3.75 3.75h8.5v8.5h-8.5z", NULL, 16, 16, 1.5f, 16, 16, true};
const PhShellIcona PH_ICONA_CHIUDI_FINESTRA = {"M3.4 3.4l9.2 9.2M12.6 3.4l-9.2 9.2", NULL, 16, 16, 1.5f, 16, 16, true};
const PhShellIcona PH_ICONA_LAUNCHER = {
    "M4.5 3h3A1.5 1.5 0 0 1 9 4.5v3A1.5 1.5 0 0 1 7.5 9h-3A1.5 1.5 0 0 1 3 7.5v-3A1.5 1.5 0 0 1 4.5 3z"
    "M12.5 3h3A1.5 1.5 0 0 1 17 4.5v3A1.5 1.5 0 0 1 15.5 9h-3A1.5 1.5 0 0 1 11 7.5v-3A1.5 1.5 0 0 1 12.5 3z"
    "M4.5 11h3A1.5 1.5 0 0 1 9 12.5v3A1.5 1.5 0 0 1 7.5 17h-3A1.5 1.5 0 0 1 3 15.5v-3A1.5 1.5 0 0 1 4.5 11z"
    "M12.5 11h3a1.5 1.5 0 0 1 1.5 1.5v3a1.5 1.5 0 0 1-1.5 1.5h-3a1.5 1.5 0 0 1-1.5-1.5v-3a1.5 1.5 0 0 1 1.5-1.5z",
    NULL, 20, 20, 1.6f, 20, 20, false};
const PhShellIcona PH_ICONA_INVIA = {"M10 13V4M6.5 7.5L10 4l3.5 3.5M4 13.5V16h12v-2.5", NULL, 20, 20, 1.6f, 20, 20, false};
const PhShellIcona PH_ICONA_SCARICA = {"M10 4v9M6.5 9.5L10 13l3.5-3.5M4 13.5V16h12v-2.5", NULL, 20, 20, 1.6f, 20, 20, false};
const PhShellIcona PH_ICONA_VOLUME = {
    "M3 8v4h3l4 3V5L6 8H3zM13 7.5a3.5 3.5 0 0 1 0 5M15 5a7 7 0 0 1 0 10", NULL, 20, 20, 1.5f, 20, 20, false};
const PhShellIcona PH_ICONA_VOLUME_MUTO = {"M3 8v4h3l4 3V5L6 8H3zM13 8l4 4M17 8l-4 4", NULL, 20, 20, 1.5f, 20, 20, false};
const PhShellIcona PH_ICONA_WIFI = {
    "M3 8.5a10 10 0 0 1 14 0M6 11.5a6 6 0 0 1 8 0M9 14.5a2 2 0 0 1 2 0", NULL, 20, 20, 1.5f, 20, 20, false};
/* Batteria a livello fisso (78% nel mockup): la barra disegna il livello vero. */
const PhShellIcona PH_ICONA_BATTERIA = {
    "M4 6h11a2 2 0 0 1 2 2v4a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2zM19 9v2",
    "M4 8h8v4H4z", 22, 20, 1.5f, 22, 20, false};
const PhShellIcona PH_ICONA_CAMPANELLA = {
    "M5 14h10l-1.4-2V8.5a3.6 3.6 0 0 0-7.2 0V12L5 14zM8.5 16a1.5 1.5 0 0 0 3 0", NULL, 20, 20, 1.5f, 20, 20, false};
const PhShellIcona PH_ICONA_LUCCHETTO = {
    "M7 11h10a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2zM8 11V8a4 4 0 0 1 8 0v3",
    NULL, 24, 24, 1.6f, 40, 40, false};

#define PH_TIPO_ICONA (ph_icona_get_type())
G_DECLARE_FINAL_TYPE(PhIcona, ph_icona, PH, ICONA, GtkWidget)

struct _PhIcona {
    GtkWidget genitore;
    const PhShellIcona *icona;
    GskPath *tratto, *pieno;
};

G_DEFINE_FINAL_TYPE(PhIcona, ph_icona, GTK_TYPE_WIDGET)

static void icona_svuota(PhIcona *i)
{
    g_clear_pointer(&i->tratto, gsk_path_unref);
    g_clear_pointer(&i->pieno, gsk_path_unref);
}

static void icona_measure(GtkWidget *w, GtkOrientation o, int per, int *min, int *nat,
                          int *min_base, int *nat_base)
{
    (void)per;
    PhIcona *i = PH_ICONA(w);
    int v = i->icona == NULL ? 0 : (o == GTK_ORIENTATION_HORIZONTAL ? i->icona->larghezza : i->icona->altezza);
    *min = *nat = v;
    *min_base = *nat_base = -1;
}

static void icona_snapshot(GtkWidget *w, GtkSnapshot *s)
{
    PhIcona *i = PH_ICONA(w);
    if (i->icona == NULL)
        return;
    GdkRGBA colore;
    gtk_widget_get_color(w, &colore);
    float lw = (float)gtk_widget_get_width(w), lh = (float)gtk_widget_get_height(w);
    float sx = (float)i->icona->larghezza / i->icona->vista_l;
    float sy = (float)i->icona->altezza / i->icona->vista_a;
    gtk_snapshot_save(s);
    graphene_point_t c = GRAPHENE_POINT_INIT((lw - (float)i->icona->larghezza) / 2.0f,
                                             (lh - (float)i->icona->altezza) / 2.0f);
    gtk_snapshot_translate(s, &c);
    gtk_snapshot_scale(s, sx, sy);
    if (i->pieno != NULL)
        gtk_snapshot_append_fill(s, i->pieno, GSK_FILL_RULE_WINDING, &colore);
    if (i->tratto != NULL) {
        GskStroke *k = gsk_stroke_new(i->icona->spessore);
        gsk_stroke_set_line_cap(k, i->icona->spigoli_vivi ? GSK_LINE_CAP_BUTT : GSK_LINE_CAP_ROUND);
        gsk_stroke_set_line_join(k, i->icona->spigoli_vivi ? GSK_LINE_JOIN_MITER : GSK_LINE_JOIN_ROUND);
        gtk_snapshot_append_stroke(s, i->tratto, k, &colore);
        gsk_stroke_free(k);
    }
    gtk_snapshot_restore(s);
}

static void icona_finalize(GObject *o)
{
    icona_svuota(PH_ICONA(o));
    G_OBJECT_CLASS(ph_icona_parent_class)->finalize(o);
}

static void ph_icona_class_init(PhIconaClass *k)
{
    G_OBJECT_CLASS(k)->finalize = icona_finalize;
    GTK_WIDGET_CLASS(k)->measure = icona_measure;
    GTK_WIDGET_CLASS(k)->snapshot = icona_snapshot;
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(k), "ph-icona");
    gtk_widget_class_set_accessible_role(GTK_WIDGET_CLASS(k), GTK_ACCESSIBLE_ROLE_PRESENTATION);
}

static void ph_icona_init(PhIcona *i)
{
    gtk_widget_set_can_target(GTK_WIDGET(i), FALSE);
}

void ph_shell_icona_imposta(GtkWidget *w, const PhShellIcona *icona)
{
    g_return_if_fail(PH_IS_ICONA(w));
    PhIcona *i = PH_ICONA(w);
    icona_svuota(i);
    i->icona = icona;
    if (icona != NULL) {
        if (icona->tratto != NULL && (i->tratto = gsk_path_parse(icona->tratto)) == NULL)
            g_warning("icona: tracciato non valido: %s", icona->tratto);
        if (icona->pieno != NULL && (i->pieno = gsk_path_parse(icona->pieno)) == NULL)
            g_warning("icona: tracciato non valido: %s", icona->pieno);
    }
    gtk_widget_queue_resize(w);
}

GtkWidget *ph_shell_icona_nuova(const PhShellIcona *icona)
{
    GtkWidget *w = g_object_new(PH_TIPO_ICONA, NULL);
    ph_shell_icona_imposta(w, icona);
    return w;
}

/* ---- Segnaposto con le iniziali (mockup: tile colorato + mono) ---- */
#define PH_TIPO_SEGNAPOSTO (ph_segnaposto_get_type())
G_DECLARE_FINAL_TYPE(PhSegnaposto, ph_segnaposto, PH, SEGNAPOSTO, GtkWidget)

struct _PhSegnaposto {
    GtkWidget genitore;
    GdkRGBA colore;
    char iniziali[16];
    int lato, raggio;
};

G_DEFINE_FINAL_TYPE(PhSegnaposto, ph_segnaposto, GTK_TYPE_WIDGET)

static void segnaposto_measure(GtkWidget *w, GtkOrientation o, int per, int *min, int *nat,
                               int *min_base, int *nat_base)
{
    (void)o;
    (void)per;
    *min = *nat = PH_SEGNAPOSTO(w)->lato;
    *min_base = *nat_base = -1;
}

static void segnaposto_snapshot(GtkWidget *w, GtkSnapshot *s)
{
    PhSegnaposto *p = PH_SEGNAPOSTO(w);
    float l = (float)p->lato;
    GskRoundedRect rr;
    gsk_rounded_rect_init_from_rect(&rr, &GRAPHENE_RECT_INIT(0, 0, l, l), (float)p->raggio);
    gtk_snapshot_push_rounded_clip(s, &rr);
    gtk_snapshot_append_color(s, &p->colore, &GRAPHENE_RECT_INIT(0, 0, l, l));
    gtk_snapshot_pop(s);
    PangoLayout *lay = gtk_widget_create_pango_layout(w, p->iniziali);
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_weight(fd, PANGO_WEIGHT_SEMIBOLD);
    pango_font_description_set_absolute_size(fd, l * 0.31 * PANGO_SCALE);
    pango_layout_set_font_description(lay, fd);
    pango_font_description_free(fd);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(lay, &tw, &th);
    gtk_snapshot_save(s);
    graphene_point_t pt = GRAPHENE_POINT_INIT((l - (float)tw) / 2.0f, (l - (float)th) / 2.0f);
    gtk_snapshot_translate(s, &pt);
    GdkRGBA bianco = {1, 1, 1, 1};
    gtk_snapshot_append_layout(s, lay, &bianco);
    gtk_snapshot_restore(s);
    g_object_unref(lay);
}

static void ph_segnaposto_class_init(PhSegnapostoClass *k)
{
    GTK_WIDGET_CLASS(k)->measure = segnaposto_measure;
    GTK_WIDGET_CLASS(k)->snapshot = segnaposto_snapshot;
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(k), "ph-segnaposto");
    gtk_widget_class_set_accessible_role(GTK_WIDGET_CLASS(k), GTK_ACCESSIBLE_ROLE_PRESENTATION);
}

static void ph_segnaposto_init(PhSegnaposto *p)
{
    gtk_widget_set_can_target(GTK_WIDGET(p), FALSE);
}

GtkWidget *ph_shell_segnaposto_nuovo(const char *pacchetto, const char *nome, int lato, int raggio)
{
    /* Le tinte delle app del mockup (APPS[].color). */
    static const char *const tinte[] = {"#2563eb", "#b45309", "#047857", "#be185d", "#7c3aed", "#0f766e",
                                        "#b91c1c", "#475569", "#0369a1", "#4338ca", "#0e7490", "#15803d",
                                        "#a16207", "#1d4ed8", "#9d174d", "#166534"};
    PhSegnaposto *p = g_object_new(PH_TIPO_SEGNAPOSTO, NULL);
    p->lato = lato > 0 ? lato : 48;
    p->raggio = raggio >= 0 ? raggio : 12;
    guint h = g_str_hash(pacchetto != NULL ? pacchetto : "");
    gdk_rgba_parse(&p->colore, tinte[h % G_N_ELEMENTS(tinte)]);
    /* Iniziali: i primi due caratteri del nome, il primo maiuscolo. */
    const char *n = (nome != NULL && *nome != '\0') ? nome : "?";
    if (!g_utf8_validate(n, -1, NULL))
        n = "?";
    gunichar a = g_unichar_toupper(g_utf8_get_char(n));
    const char *seconda = g_utf8_next_char(n);
    char buf[16] = {0};
    int k = g_unichar_to_utf8(a, buf);
    if (*seconda != '\0')
        k += g_unichar_to_utf8(g_utf8_get_char(seconda), buf + k);
    buf[k] = '\0';
    g_strlcpy(p->iniziali, buf, sizeof p->iniziali);
    return GTK_WIDGET(p);
}
