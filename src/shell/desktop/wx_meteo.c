/* Widget Meteo (CONTRATTI.md §18): sei stili (Adesso, Ore, Settimana, Compatto, Oggi, Curva),
 * ogni stato disegnato con cura, attribuzione a Open-Meteo SEMPRE visibile con il suo
 * collegamento. I dati arrivano dai servizi (ph_srv_meteo); niente rete qui. L'osservazione
 * (ph_srv_meteo_osserva) e' attiva solo finche' il widget e' mappato. Mai coordinate, nomi di
 * localita' o testi cercati nei log. */
#include <math.h>
#include <string.h>

#include "interno.h"

typedef struct {
    bool osserva;
    GCancellable *annulla_uri; /* apertura del collegamento di attribuzione */
    PhDskMeteoSezioni sez;     /* agente C: sezioni dello stile «Adesso» all'ultimo disegno */
    bool sez_nota;             /* agente C: `sez` e' valida (c'e' stato un disegno dello stile «Adesso») */
    gulong id_misura[2];       /* agente C: gestori di notify::width-request e notify::height-request */
    int alt_avviso;           /* agente C: px tolti all'altezza per l'avviso dei dati vecchi nell'ultimo disegno */
} Meteo;

/* ---- Utilita' ---------------------------------------------------------------------------------- */
static GtkWidget *etichetta(const char *testo, const char *classe, float xalign)
{
    GtkWidget *l = gtk_label_new(testo);
    if (classe != NULL)
        gtk_widget_add_css_class(l, classe);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    return l;
}

static GtkWidget *etichetta_taglio(const char *testo, const char *classe, float xalign)
{
    GtkWidget *l = etichetta(testo, classe, xalign);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 1);
    return l;
}

static GtkWidget *riga_orizzontale(int spazio)
{
    return gtk_box_new(GTK_ORIENTATION_HORIZONTAL, spazio);
}

/* ---- Apertura del collegamento (attribuzione) --------------------------------------------------- */
/* La funzione di ritorno del lanciatore arriva sempre: struttura propria con il desktop contato,
 * mai il widget. Il lanciatore e' lo stesso dei collegamenti GeoNames del dialogo. */
typedef struct {
    PhDesktop *d;
} ApriUri;

static void uri_fatto(GObject *o, GAsyncResult *r, gpointer dati)
{
    ApriUri *a = dati;
    GError *e = NULL;
    gtk_uri_launcher_launch_finish(GTK_URI_LAUNCHER(o), r, &e);
    g_clear_error(&e); /* nessun testo d'errore: potrebbe contenere l'indirizzo */
    ph_dsk_unref(a->d);
    g_free(a);
    g_object_unref(o);
}

void ph_dsk_apri_uri(PhDesktop *d, GCancellable *annulla, const char *uri)
{
    ApriUri *a = g_new0(ApriUri, 1);
    a->d = ph_dsk_ref(d);
    GtkUriLauncher *l = gtk_uri_launcher_new(uri);
    gtk_uri_launcher_launch(l, ph_shell_finestra(d->sh), annulla, uri_fatto, a);
}

static void attribuzione_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    PhDskWidget *w = dati;
    Meteo *x = w->x;
    if (x != NULL)
        ph_dsk_apri_uri(w->d, ph_dsk_wx_rinnova(&x->annulla_uri), PH_METEO_SITO);
}

/* Riga di attribuzione: 10 px #9aa4b5, un collegamento vero (pulsante), mai tolta. */
static GtkWidget *attribuzione(PhDskWidget *w)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "ph-desktop-meteo-attrib");
    GtkWidget *l = etichetta(ph_t("Dati meteo di Open-Meteo.com"), "ph-desktop-meteo-attrib-testo", 0);
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 1);
    gtk_button_set_child(GTK_BUTTON(b), l);
    gtk_widget_set_halign(b, GTK_ALIGN_START);
    ph_shell_etichetta(b, ph_t("Dati meteo di Open-Meteo.com"), PH_METEO_SITO);
    g_signal_connect(b, "clicked", G_CALLBACK(attribuzione_clic), w);
    return b;
}

/* ---- Stili --------------------------------------------------------------------------------------------- */
static void icona_in(GtkWidget *box, int codice, int giorno, int lato)
{
    gtk_box_append(GTK_BOX(box), ph_dsk_meteo_icona_nuova(codice, giorno, lato));
}

static const char *classe_temp(PhCfgWidgetDim d)
{
    return d == PH_CFG_WIDGET_PICCOLO ? "ph-desktop-meteo-t-p" : (d == PH_CFG_WIDGET_GRANDE ? "ph-desktop-meteo-t-g" : "ph-desktop-meteo-t-m");
}

static void riga_dato(GtkWidget *box, const char *sx, const char *dx)
{
    GtkWidget *r = riga_orizzontale(10);
    gtk_widget_add_css_class(r, "ph-desktop-w-riga");
    GtkWidget *a = etichetta_taglio(sx, "ph-desktop-w-riga-sx", 0);
    gtk_widget_set_hexpand(a, TRUE);
    gtk_box_append(GTK_BOX(r), a);
    gtk_box_append(GTK_BOX(r), etichetta(dx, "ph-desktop-w-riga-dx", 1));
    gtk_box_append(GTK_BOX(box), r);
}

static void massima_minima(const PhMeteoPrevisione *p, char *out, size_t cap)
{
    char a[16] = "\xE2\x80\x94", b[16] = "\xE2\x80\x94";
    if (p->n_giorni > 0) {
        ph_dsk_meteo_temp(p->giorni[0].massima, a, sizeof a);
        ph_dsk_meteo_temp(p->giorni[0].minima, b, sizeof b);
    }
    ph_lingua_componi(out, cap, ph_t("Max {max} · Min {min}"), "max", a, "min", b, (const char *)NULL);
}

/* agente C: inizio — stile «Adesso» (scheda A3 / C1): cresce con la misura vera del widget.
 * Le sezioni le decide ph_dsk_meteo_sezioni (funzione pura, con prove): qui solo il disegno. */
static GtkWidget *curva_ore(const PhMeteoPrevisione *p, const PhDskMeteoSezioni *s)
{
    size_t n = s->n_ore;
    if (n > p->n_ore)
        n = p->n_ore;
    if (n > PH_METEO_ORE_MAX)
        n = PH_METEO_ORE_MAX;
    double temp[PH_METEO_ORE_MAX];
    char gradi_buf[PH_METEO_ORE_MAX][16], ore_buf[PH_METEO_ORE_MAX][16];
    const char *gradi[PH_METEO_ORE_MAX], *ore[PH_METEO_ORE_MAX];
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (!ph_dsk_meteo_valore_valido(p->ore[i].temperatura))
            break; /* un buco non si inventa: la curva si interrompe li' */
        temp[k] = p->ore[i].temperatura;
        ph_dsk_meteo_temp(temp[k], gradi_buf[k], sizeof gradi_buf[k]);
        ph_dsk_meteo_ora_breve(p->ore[i].ora, p->scarto_utc_s, ore_buf[k], sizeof ore_buf[k]);
        gradi[k] = gradi_buf[k];
        ore[k] = ore_buf[k];
        k++;
    }
    if (k < 2)
        return etichetta(ph_t("Andamento non disponibile"), "ph-desktop-w-sotto", 0);
    GtkWidget *c = ph_dsk_curva_nuova();
    ph_dsk_curva_imposta(c, temp, gradi, ore, k, PH_DSK_ACC_AMBRA);
    gtk_widget_set_size_request(c, -1, s->altezza_curva);
    gtk_widget_set_margin_top(c, 8);
    gtk_widget_add_css_class(c, "ph-desktop-meteo-curva"); /* classe senza stile: la cercano le prove */
    return c;
}

static void giorni_tratto(GtkWidget *box, const PhMeteoPrevisione *p, size_t n)
{
    if (n > p->n_giorni)
        n = p->n_giorni;
    if (n > PH_METEO_GIORNI_MAX)
        n = PH_METEO_GIORNI_MAX;
    double gmin = 0, gmax = 0;
    bool estremi = ph_dsk_meteo_estremi(p->giorni, n, &gmin, &gmax);
    int64_t oggi = n > 0 ? p->giorni[0].giorno : 0;
    for (size_t i = 0; i < n; i++) {
        const PhMeteoGiorno *g = &p->giorni[i];
        GtkWidget *r = riga_orizzontale(8);
        gtk_widget_set_size_request(r, -1, PH_DSK_METEO_ADESSO_ALT_GIORNO);
        char nome[24], mi[16], ma[16], mm[40];
        ph_dsk_meteo_giorno_nome(g->giorno, oggi, p->scarto_utc_s, nome, sizeof nome);
        ph_dsk_meteo_temp(g->minima, mi, sizeof mi);
        ph_dsk_meteo_temp(g->massima, ma, sizeof ma);
        snprintf(mm, sizeof mm, "%s / %s", mi, ma);
        GtkWidget *ln = etichetta(nome, "ph-desktop-meteo-giorno-nome", 0);
        gtk_widget_set_size_request(ln, 40, -1);
        gtk_box_append(GTK_BOX(r), ln);
        GtkWidget *t = ph_dsk_tratto_nuovo();
        bool noto = estremi && ph_dsk_meteo_valore_valido(g->minima) && ph_dsk_meteo_valore_valido(g->massima);
        ph_dsk_tratto_imposta(t, noto, g->minima, g->massima, gmin, gmax, PH_DSK_ACC_INDACO, PH_DSK_ACC_CORALLO);
        gtk_widget_set_hexpand(t, TRUE);
        gtk_widget_set_valign(t, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(r), t);
        GtkWidget *lm = etichetta(mm, "ph-desktop-meteo-max", 1);
        gtk_widget_set_size_request(lm, 64, -1);
        gtk_box_append(GTK_BOX(r), lm);
        gtk_box_append(GTK_BOX(box), r);
    }
}

/* Misura naturale in larghezza di un widget gia' con la sua classe (serve per scegliere la forma). */
static int larghezza_naturale(GtkWidget *w)
{
    int minimo = 0, naturale = 0;
    gtk_widget_measure(w, GTK_ORIENTATION_HORIZONTAL, -1, &minimo, &naturale, NULL, NULL);
    return naturale;
}

/* «18° / 11°» (forma breve della riga massima/minima). */
static void massima_minima_breve(const PhMeteoPrevisione *p, char *out, size_t cap)
{
    char a[16] = "\xE2\x80\x94", b[16] = "\xE2\x80\x94";
    if (p->n_giorni > 0) {
        ph_dsk_meteo_temp(p->giorni[0].massima, a, sizeof a);
        ph_dsk_meteo_temp(p->giorni[0].minima, b, sizeof b);
    }
    snprintf(out, cap, "%s / %s", a, b);
}

static void stile_adesso(GtkWidget *box, const PhMeteoPrevisione *p, const PhDskMeteoSezioni *s,
                         const char *luogo, int larghezza)
{
    const PhMeteoAdesso *a = &p->adesso;
    GtkWidget *r = riga_orizzontale(12);
    icona_in(r, a->codice, a->giorno, s->lato_icona);
    char t[24];
    ph_dsk_meteo_temp(a->temperatura, t, sizeof t);
    static const char *const classi[3] = {"ph-desktop-meteo-t-p", "ph-desktop-meteo-t-m", "ph-desktop-meteo-t-g"};
    int ct = s->corpo_temp < 0 ? 0 : (s->corpo_temp > 2 ? 2 : s->corpo_temp);
    GtkWidget *lt = etichetta(t, classi[ct], 0);
    gtk_widget_set_valign(lt, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(r), lt);
    if (s->testo) {
        GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_valign(col, GTK_ALIGN_CENTER);
        gtk_widget_set_hexpand(col, TRUE);
        if (luogo[0] != '\0')
            gtk_box_append(GTK_BOX(col), etichetta_taglio(luogo, "ph-desktop-meteo-luogo", 0));
        const char *cond = ph_dsk_meteo_condizione(a->codice);
        if (cond[0] != '\0')
            gtk_box_append(GTK_BOX(col), etichetta_taglio(cond, "ph-desktop-w-sotto-nom", 0));
        /* Se «Max 18° · Min 11°» non ci sta per intero accanto a icona e numero: «18° / 11°». */
        char mm[96];
        massima_minima(p, mm, sizeof mm);
        GtkWidget *lm = etichetta_taglio(mm, "ph-desktop-meteo-mm-riga", 0);
        int pad_o, pad_v;
        ph_dsk_imbottitura(PH_DSK_FORMA_MATTONELLA, &pad_o, &pad_v);
        int disponibile = larghezza - 2 * pad_o - s->lato_icona - 12 - larghezza_naturale(lt) - 12;
        GtkWidget *prova = etichetta(mm, "ph-desktop-meteo-mm-riga", 0); /* senza taglio: il testo intero */
        g_object_ref_sink(prova);
        bool breve = ph_dsk_meteo_forma_breve(disponibile, larghezza_naturale(prova));
        g_object_unref(prova);
        if (breve) {
            massima_minima_breve(p, mm, sizeof mm);
            gtk_label_set_text(GTK_LABEL(lm), mm);
        }
        gtk_box_append(GTK_BOX(col), lm);
        gtk_box_append(GTK_BOX(r), col);
    }
    if (!s->curva) { /* senza curva sotto, la testa si centra nello spazio che resta (attribuzione in fondo) */
        gtk_widget_set_vexpand(r, TRUE);
        gtk_widget_set_valign(r, GTK_ALIGN_CENTER);
    }
    gtk_box_append(GTK_BOX(box), r);
    if (s->curva)
        gtk_box_append(GTK_BOX(box), curva_ore(p, s));
    if (s->n_giorni > 0) {
        GtkWidget *g = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(g, "ph-desktop-meteo-giorni"); /* classe senza stile: la cercano le prove */
        gtk_widget_set_margin_top(g, 6);
        giorni_tratto(g, p, s->n_giorni);
        gtk_box_append(GTK_BOX(box), g);
    }
}
/* agente C: fine */

static GtkWidget *colonna_ora(const PhMeteoPrevisione *p, size_t i, bool prima)
{
    const PhMeteoOra *o = &p->ore[i];
    GtkWidget *c = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand(c, TRUE);
    char h[16], t[16];
    if (prima)
        g_strlcpy(h, ph_t("Ora"), sizeof h);
    else
        ph_dsk_meteo_ora_breve(o->ora, p->scarto_utc_s, h, sizeof h);
    ph_dsk_meteo_temp(o->temperatura, t, sizeof t);
    gtk_box_append(GTK_BOX(c), etichetta(h, "ph-desktop-meteo-ora", 0.5f));
    icona_in(c, o->codice, o->giorno, 24);
    gtk_box_append(GTK_BOX(c), etichetta(t, "ph-desktop-meteo-ora-t", 0.5f));
    return c;
}

static void colonne_ore(GtkWidget *box, const PhMeteoPrevisione *p, size_t n)
{
    if (n > p->n_ore)
        n = p->n_ore;
    GtkWidget *r = riga_orizzontale(0);
    gtk_box_set_homogeneous(GTK_BOX(r), TRUE);
    for (size_t i = 0; i < n; i++)
        gtk_box_append(GTK_BOX(r), colonna_ora(p, i, i == 0));
    gtk_box_append(GTK_BOX(box), r);
}

static void stile_settimana(GtkWidget *box, const PhMeteoPrevisione *p, PhCfgWidgetDim dim)
{
    size_t n = ph_dsk_meteo_n_giorni(PH_DSK_METEO_SETTIMANA, dim);
    if (n > p->n_giorni)
        n = p->n_giorni;
    double gmin = 0, gmax = 0;
    bool estremi = ph_dsk_meteo_estremi(p->giorni, n, &gmin, &gmax);
    int64_t oggi = n > 0 ? p->giorni[0].giorno : 0;
    for (size_t i = 0; i < n; i++) {
        const PhMeteoGiorno *g = &p->giorni[i];
        GtkWidget *r = riga_orizzontale(6);
        gtk_widget_add_css_class(r, "ph-desktop-meteo-giorno");
        char nome[24], mi[16], ma[16];
        ph_dsk_meteo_giorno_nome(g->giorno, oggi, p->scarto_utc_s, nome, sizeof nome);
        ph_dsk_meteo_temp(g->minima, mi, sizeof mi);
        ph_dsk_meteo_temp(g->massima, ma, sizeof ma);
        GtkWidget *l = etichetta(nome, "ph-desktop-meteo-giorno-nome", 0);
        gtk_widget_set_size_request(l, 34, -1);
        gtk_box_append(GTK_BOX(r), l);
        icona_in(r, g->codice, 1, 22);
        GtkWidget *lm = etichetta(mi, "ph-desktop-meteo-min", 1);
        gtk_widget_set_size_request(lm, 28, -1);
        gtk_box_append(GTK_BOX(r), lm);
        double da = 0, a = 1;
        bool c_e = estremi && ph_dsk_meteo_barra(g->minima, g->massima, gmin, gmax, &da, &a);
        gtk_box_append(GTK_BOX(r), ph_dsk_meteo_barra_nuova(c_e, da, a));
        GtkWidget *lx = etichetta(ma, "ph-desktop-meteo-max", 0);
        gtk_widget_set_size_request(lx, 28, -1);
        gtk_box_append(GTK_BOX(r), lx);
        gtk_box_append(GTK_BOX(box), r);
    }
}

static void stile_compatto(GtkWidget *box, const PhMeteoPrevisione *p, PhCfgWidgetDim dim)
{
    const PhMeteoAdesso *a = &p->adesso;
    GtkWidget *r = riga_orizzontale(8);
    icona_in(r, a->codice, a->giorno, 28);
    char t[24];
    ph_dsk_meteo_temp(a->temperatura, t, sizeof t);
    gtk_box_append(GTK_BOX(r), etichetta(t, "ph-desktop-meteo-t-c", 0));
    GtkWidget *c = etichetta_taglio(ph_dsk_meteo_condizione(a->codice), "ph-desktop-w-sotto-nom", 0);
    gtk_widget_set_hexpand(c, TRUE);
    gtk_box_append(GTK_BOX(r), c);
    if (dim != PH_CFG_WIDGET_PICCOLO && p->n_giorni > 0) {
        char ma[16], mi[16], mm[40];
        ph_dsk_meteo_temp(p->giorni[0].massima, ma, sizeof ma);
        ph_dsk_meteo_temp(p->giorni[0].minima, mi, sizeof mi);
        snprintf(mm, sizeof mm, "%s / %s", ma, mi);
        gtk_box_append(GTK_BOX(r), etichetta(mm, "ph-desktop-meteo-mm", 1));
    }
    gtk_box_append(GTK_BOX(box), r);
}

static void stile_oggi(GtkWidget *box, const PhMeteoPrevisione *p, PhCfgWidgetDim dim)
{
    const PhMeteoAdesso *a = &p->adesso;
    GtkWidget *r = riga_orizzontale(10);
    icona_in(r, a->codice, a->giorno, 40);
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(col, GTK_ALIGN_CENTER);
    char t[24];
    ph_dsk_meteo_temp(a->temperatura, t, sizeof t);
    gtk_box_append(GTK_BOX(col), etichetta(t, "ph-desktop-meteo-t-o", 0));
    gtk_box_append(GTK_BOX(col), etichetta_taglio(ph_dsk_meteo_condizione(a->codice), "ph-desktop-w-sotto-nom", 0));
    gtk_widget_set_hexpand(col, TRUE);
    gtk_box_append(GTK_BOX(r), col);
    gtk_box_append(GTK_BOX(box), r);
    size_t righe = ph_dsk_meteo_n_dettagli(dim);
    char v[32];
    GtkWidget *dettagli = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(dettagli, 4);
    if (righe > 0) {
        ph_dsk_meteo_temp_c(a->percepita, v, sizeof v);
        riga_dato(dettagli, ph_t("Percepita"), v);
    }
    if (righe > 1) {
        ph_dsk_meteo_vento(a->vento, v, sizeof v);
        riga_dato(dettagli, ph_t("Vento"), v);
    }
    if (righe > 2) {
        ph_dsk_meteo_percento(p->n_giorni > 0 ? p->giorni[0].prob_pioggia : NAN, v, sizeof v);
        riga_dato(dettagli, ph_t("Pioggia"), v);
    }
    if (righe > 3) {
        ph_dsk_meteo_percento(a->umidita, v, sizeof v);
        riga_dato(dettagli, ph_t("Umidità"), v);
    }
    gtk_box_append(GTK_BOX(box), dettagli);
    size_t n_ore = ph_dsk_meteo_n_ore(PH_DSK_METEO_OGGI, dim);
    if (n_ore > 0) {
        GtkWidget *ore = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_margin_top(ore, 8);
        colonne_ore(ore, p, n_ore);
        gtk_box_append(GTK_BOX(box), ore);
    }
}

static void stile_curva(GtkWidget *box, const PhMeteoPrevisione *p, PhCfgWidgetDim dim)
{
    const PhMeteoAdesso *a = &p->adesso;
    GtkWidget *r = riga_orizzontale(10);
    icona_in(r, a->codice, a->giorno, dim == PH_CFG_WIDGET_PICCOLO ? 30 : 38);
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(col, GTK_ALIGN_CENTER);
    char t[24];
    ph_dsk_meteo_temp(a->temperatura, t, sizeof t);
    gtk_box_append(GTK_BOX(col), etichetta(t, classe_temp(dim == PH_CFG_WIDGET_PICCOLO ? PH_CFG_WIDGET_PICCOLO : PH_CFG_WIDGET_MEDIO), 0));
    gtk_widget_set_hexpand(col, TRUE);
    /* La condizione sta sotto la temperatura, come negli altri stili: cosi' ha tutta la larghezza. */
    if (dim != PH_CFG_WIDGET_PICCOLO)
        gtk_box_append(GTK_BOX(col), etichetta_taglio(ph_dsk_meteo_condizione(a->codice), "ph-desktop-w-sotto-nom", 0));
    gtk_box_append(GTK_BOX(r), col);
    gtk_box_append(GTK_BOX(box), r);
    /* Le ore con una temperatura nota (un buco non si inventa: si interrompe la curva li'). */
    size_t n = ph_dsk_meteo_n_ore(PH_DSK_METEO_CURVA, dim);
    if (n > p->n_ore)
        n = p->n_ore;
    double temp[PH_METEO_ORE_MAX];
    char ore[PH_METEO_ORE_MAX][4];
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (!ph_dsk_meteo_valore_valido(p->ore[i].temperatura))
            break; /* non nota o fuori limite (+-150): come negli altri valori */
        temp[k] = p->ore[i].temperatura;
        char h[16];
        /* Il contratto da' UN solo scarto per tutta la previsione (scarto_utc_s): la notte del cambio
         * dell'ora le etichette dopo il cambio possono slittare di un'ora. Non c'e' altro dato. */
        ph_dsk_meteo_ora_breve(p->ore[i].ora, p->scarto_utc_s, h, sizeof h);
        g_strlcpy(ore[k], h, 4);
        k++;
    }
    int altezza = dim == PH_CFG_WIDGET_PICCOLO ? 64 : (dim == PH_CFG_WIDGET_GRANDE ? 100 : 80);
    if (k >= 2)
        gtk_box_append(GTK_BOX(box), ph_dsk_meteo_curva_nuova(temp, (const char (*)[4])ore, k, altezza));
    else
        gtk_box_append(GTK_BOX(box), etichetta(ph_t("Andamento non disponibile"), "ph-desktop-w-sotto", 0));
}

/* ---- Contenuto ---------------------------------------------------------------------------------------- */
static void invito_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    PhDskWidget *w = dati;
    ph_dsk_luogo_apri(w->d);
}

static GtkWidget *messaggio_stato(GtkWidget *corpo, PhDskWidget *w, const PhDskMeteoStato *s, bool con_dati)
{
    GtkWidget *m = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    if (!con_dati)
        gtk_widget_set_vexpand(m, TRUE); /* l'attribuzione resta in fondo anche senza dati */
    gtk_widget_add_css_class(m, con_dati ? "ph-desktop-meteo-avviso" : "ph-desktop-meteo-stato");
    if (s->messaggio[0] != '\0') {
        GtkWidget *l = etichetta(s->messaggio, con_dati ? "ph-desktop-meteo-avviso-testo" : "ph-desktop-meteo-stato-testo", 0);
        gtk_label_set_wrap(GTK_LABEL(l), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(l), 1);
        /* La frase puo' venire dal telefono (lunga a piacere): al massimo 2 o 3 righe, poi i puntini,
         * cosi' l'altezza dello stato e' limitata e l'attribuzione non esce dalla scheda. */
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
        gtk_label_set_lines(GTK_LABEL(l), con_dati ? 2 : 3);
        gtk_box_append(GTK_BOX(m), l);
    }
    if (s->dettaglio[0] != '\0')
        gtk_box_append(GTK_BOX(m), etichetta_taglio(s->dettaglio, "ph-desktop-w-sotto", 0));
    if (s->invito_localita) {
        GtkWidget *b = gtk_button_new_with_label(ph_t("Scegli una località…"));
        gtk_widget_add_css_class(b, "ph-desktop-meteo-invito");
        gtk_widget_set_halign(b, GTK_ALIGN_START);
        g_signal_connect(b, "clicked", G_CALLBACK(invito_clic), w);
        gtk_box_append(GTK_BOX(m), b);
    }
    gtk_box_append(GTK_BOX(corpo), m);
    return m;
}

static void meteo_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    PhDesktop *d = w->d;
    const PhSrvMeteo *m = ph_srv_meteo(d->srv);
    PhCfgTelefono *prof = ph_dsk_profilo(d);
    bool luogo_scelto = prof != NULL && prof->meteo_luogo_scelto;
    PhDskMeteoStato s;
    ph_dsk_meteo_stato(m->stato, m->ha_dati, m->frase, m->dati.ricevuta, m->dati.scarto_utc_s, luogo_scelto, &s);
    /* agente C: lo stile «Adesso» con i dati scrive il luogo nel proprio disegno (se la larghezza lo
     * permette): niente titolo sopra. Negli stati senza dati, e negli altri stili, il titolo resta. */
    bool adesso = w->stile != PH_DSK_METEO_ORE && w->stile != PH_DSK_METEO_SETTIMANA &&
                  w->stile != PH_DSK_METEO_COMPATTO && w->stile != PH_DSK_METEO_OGGI &&
                  w->stile != PH_DSK_METEO_CURVA;
    if (!adesso) /* «Adesso»: mai il titolo, il luogo sta nel disegno e gli stati restano compatti */
        ph_dsk_meteo_titolo(m->localita, v->titolo, sizeof v->titolo);
    Meteo *x = w->x;
    if (x != NULL)
        x->sez_nota = false; /* torna vera solo se lo stile «Adesso» disegna i dati qui sotto */

    ph_dsk_wx_svuota(w->corpo);
    gtk_widget_set_visible(w->corpo, TRUE);
    GtkWidget *avviso = NULL;
    if (s.mostra_dati && s.messaggio[0] != '\0')
        avviso = messaggio_stato(w->corpo, w, &s, true); /* dati vecchi: l'avviso sta sopra */
    else if (!s.mostra_dati)
        messaggio_stato(w->corpo, w, &s, false);
    if (s.mostra_dati) {
        GtkWidget *cont = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_vexpand(cont, TRUE); /* l'attribuzione sta sempre in fondo alla scheda */
        if (s.grigio)
            gtk_widget_add_css_class(cont, "ph-desktop-meteo-vecchio");
        switch (w->stile) {
        case PH_DSK_METEO_ORE: colonne_ore(cont, &m->dati, ph_dsk_meteo_n_ore(PH_DSK_METEO_ORE, w->dim)); break;
        case PH_DSK_METEO_SETTIMANA: stile_settimana(cont, &m->dati, w->dim); break;
        case PH_DSK_METEO_COMPATTO: stile_compatto(cont, &m->dati, w->dim); break;
        case PH_DSK_METEO_OGGI: stile_oggi(cont, &m->dati, w->dim); break;
        case PH_DSK_METEO_CURVA: stile_curva(cont, &m->dati, w->dim); break;
        default: {
            /* agente C: l'avviso dei dati vecchi sta sopra e prende spazio: si toglie dall'altezza. */
            int tolti = 0;
            if (avviso != NULL) { /* altezza VERA dell'avviso alla larghezza che ha (va a capo), piu' il suo margine */
                int pad_o, pad_v, minimo = 0, naturale = 0;
                ph_dsk_imbottitura(PH_DSK_FORMA_MATTONELLA, &pad_o, &pad_v);
                gtk_widget_measure(avviso, GTK_ORIENTATION_VERTICAL, w->larghezza - 2 * pad_o, &minimo, &naturale,
                                   NULL, NULL);
                tolti = naturale + 8;
                if (tolti < PH_DSK_METEO_ADESSO_ALT_AVVISO / 2)
                    tolti = PH_DSK_METEO_ADESSO_ALT_AVVISO / 2;
            }
            PhDskMeteoSezioni sez = ph_dsk_meteo_sezioni(w->larghezza, w->altezza - tolti);
            if (x != NULL) {
                x->sez = sez;
                x->sez_nota = true;
                x->alt_avviso = tolti;
            }
            char *luogo = g_utf8_make_valid(m->localita, -1); /* dato di rete: UTF-8 non garantito */
            stile_adesso(cont, &m->dati, &sez, luogo, w->larghezza);
            g_free(luogo);
            break;
        }
        }
        gtk_box_append(GTK_BOX(w->corpo), cont);
    }
    GtkWidget *att = attribuzione(w);
    gtk_widget_set_margin_top(att, 8);
    gtk_box_append(GTK_BOX(w->corpo), att);
}

/* ---- Osservazione e ciclo di vita -------------------------------------------------------------------- */
static void osserva(PhDskWidget *w, bool si)
{
    Meteo *x = w->x;
    if (x == NULL || x->osserva == si || !w->d->vivo)
        return;
    x->osserva = si;
    ph_srv_meteo_osserva(w->d->srv, si);
}

static void su_map(PhDskWidget *w)
{
    if (ph_dsk_widget_visibili(w->d))
        osserva(w, true);
    ph_dsk_widget_aggiorna_uno(w);
}

static void su_unmap(PhDskWidget *w)
{
    /* Nel «destroy» della shell i servizi non si toccano: si smette solo se la shell e' viva. */
    if (w->d->vivo && ph_shell_radice_viva(ph_shell_radice(w->d->sh)))
        osserva(w, false);
}

/* agente C: la misura della scheda cambia (la maniglia chiama gtk_widget_set_size_request): lo stile
 * «Adesso» si ridisegna SOLO se le sezioni da mostrare sono cambiate (soglie in
 * ph_dsk_meteo_sezioni). Nessun richiamo a ogni fotogramma: il segnale arriva solo a misura cambiata. */
static void su_misura(GObject *o, GParamSpec *ps, gpointer dati)
{
    (void)o;
    (void)ps;
    PhDskWidget *w = dati;
    Meteo *x = w->x;
    if (x == NULL || !x->sez_nota || !w->d->vivo)
        return;
    /* Con l'avviso dei dati vecchi sopra, la sua altezza dipende dalla larghezza (va a capo): si
     * ridisegna a ogni cambio di misura. Senza avviso, solo se le sezioni cambiano. */
    if (x->alt_avviso == 0) {
        PhDskMeteoSezioni nuove = ph_dsk_meteo_sezioni(w->larghezza, w->altezza);
        if (ph_dsk_meteo_sezioni_uguali(&x->sez, &nuove))
            return;
    }
    ph_dsk_widget_aggiorna_uno(w);
}

static void meteo_inizia(PhDskWidget *w)
{
    Meteo *x = g_new0(Meteo, 1);
    w->x = x;
    x->id_misura[0] = g_signal_connect(w->scheda, "notify::width-request", G_CALLBACK(su_misura), w);
    x->id_misura[1] = g_signal_connect(w->scheda, "notify::height-request", G_CALLBACK(su_misura), w);
    g_signal_connect_data(w->scheda, "map", G_CALLBACK(su_map), w, NULL, G_CONNECT_SWAPPED | G_CONNECT_AFTER);
    g_signal_connect_data(w->scheda, "unmap", G_CALLBACK(su_unmap), w, NULL, G_CONNECT_SWAPPED);
    if (gtk_widget_get_mapped(w->scheda) && ph_dsk_widget_visibili(w->d))
        osserva(w, true); /* ricostruzione a finestra aperta: il «map» e' gia' passato */
}

static void meteo_ferma(PhDskWidget *w)
{
    Meteo *x = w->x;
    for (size_t i = 0; i < 2; i++) /* agente C */
        if (w->scheda != NULL && x->id_misura[i] != 0)
            g_signal_handler_disconnect(w->scheda, x->id_misura[i]);
    ph_dsk_wx_annulla(&x->annulla_uri);
    if (x->osserva && w->d->vivo)
        ph_srv_meteo_osserva(w->d->srv, false);
    g_free(x);
    w->x = NULL;
}

static void meteo_evento(PhDskWidget *w, int ev)
{
    /* Un evento meteo solo ridisegna: il profilo non si tocca mai (la posizione e la localita' del
     * telefono non vi entrano; solo la citta' scelta a mano, da meteo_luogo.c). */
    if (ev == PH_SRV_EV_METEO || ev == PH_SRV_EV_PROFILO)
        ph_dsk_widget_aggiorna_uno(w);
}

const PhDskWxVt PH_DSK_WX_METEO = {
    .inizia = meteo_inizia,
    .ferma = meteo_ferma,
    .aggiorna = meteo_aggiorna,
    .evento = meteo_evento,
};
