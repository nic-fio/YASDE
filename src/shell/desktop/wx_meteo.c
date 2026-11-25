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

static void stile_adesso(GtkWidget *box, const PhMeteoPrevisione *p, PhCfgWidgetDim dim)
{
    const PhMeteoAdesso *a = &p->adesso;
    int lato = dim == PH_CFG_WIDGET_PICCOLO ? 44 : (dim == PH_CFG_WIDGET_GRANDE ? 76 : 60);
    GtkWidget *r = riga_orizzontale(12);
    icona_in(r, a->codice, a->giorno, lato);
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(col, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(col, TRUE);
    char t[24];
    ph_dsk_meteo_temp(a->temperatura, t, sizeof t);
    gtk_box_append(GTK_BOX(col), etichetta(t, classe_temp(dim), 0));
    gtk_box_append(GTK_BOX(col), etichetta_taglio(ph_dsk_meteo_condizione(a->codice), "ph-desktop-w-sotto-nom", 0));
    gtk_box_append(GTK_BOX(r), col);
    gtk_box_append(GTK_BOX(box), r);
    if (dim == PH_CFG_WIDGET_PICCOLO)
        return;
    char mm[96];
    massima_minima(p, mm, sizeof mm);
    gtk_box_append(GTK_BOX(box), etichetta_taglio(mm, "ph-desktop-meteo-mm", 0));
    if (dim == PH_CFG_WIDGET_GRANDE) {
        char v[32];
        ph_dsk_meteo_temp_c(a->percepita, v, sizeof v);
        riga_dato(box, ph_t("Percepita"), v);
        ph_dsk_meteo_vento(a->vento, v, sizeof v);
        riga_dato(box, ph_t("Vento"), v);
        ph_dsk_meteo_percento(a->umidita, v, sizeof v);
        riga_dato(box, ph_t("Umidità"), v);
    }
}

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

static void messaggio_stato(GtkWidget *corpo, PhDskWidget *w, const PhDskMeteoStato *s, bool con_dati)
{
    GtkWidget *m = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(m, con_dati ? "ph-desktop-meteo-avviso" : "ph-desktop-meteo-stato");
    if (s->messaggio[0] != '\0') {
        GtkWidget *l = etichetta(s->messaggio, con_dati ? "ph-desktop-meteo-avviso-testo" : "ph-desktop-meteo-stato-testo", 0);
        gtk_label_set_wrap(GTK_LABEL(l), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(l), 1);
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
}

static void meteo_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    PhDesktop *d = w->d;
    const PhSrvMeteo *m = ph_srv_meteo(d->srv);
    PhCfgTelefono *prof = ph_dsk_profilo(d);
    bool luogo_scelto = prof != NULL && prof->meteo_luogo_scelto;
    ph_dsk_meteo_titolo(m->localita, v->titolo, sizeof v->titolo);
    PhDskMeteoStato s;
    ph_dsk_meteo_stato(m->stato, m->ha_dati, m->frase, m->dati.ricevuta, m->dati.scarto_utc_s, luogo_scelto, &s);

    ph_dsk_wx_svuota(w->corpo);
    gtk_widget_set_visible(w->corpo, TRUE);
    if (s.mostra_dati && s.messaggio[0] != '\0')
        messaggio_stato(w->corpo, w, &s, true); /* dati vecchi: l'avviso sta sopra */
    else if (!s.mostra_dati)
        messaggio_stato(w->corpo, w, &s, false);
    if (s.mostra_dati) {
        GtkWidget *cont = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        if (s.grigio)
            gtk_widget_add_css_class(cont, "ph-desktop-meteo-vecchio");
        switch (w->stile) {
        case PH_DSK_METEO_ORE: colonne_ore(cont, &m->dati, ph_dsk_meteo_n_ore(PH_DSK_METEO_ORE, w->dim)); break;
        case PH_DSK_METEO_SETTIMANA: stile_settimana(cont, &m->dati, w->dim); break;
        case PH_DSK_METEO_COMPATTO: stile_compatto(cont, &m->dati, w->dim); break;
        case PH_DSK_METEO_OGGI: stile_oggi(cont, &m->dati, w->dim); break;
        case PH_DSK_METEO_CURVA: stile_curva(cont, &m->dati, w->dim); break;
        default: stile_adesso(cont, &m->dati, w->dim); break;
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

static void meteo_inizia(PhDskWidget *w)
{
    Meteo *x = g_new0(Meteo, 1);
    w->x = x;
    g_signal_connect_data(w->scheda, "map", G_CALLBACK(su_map), w, NULL, G_CONNECT_SWAPPED | G_CONNECT_AFTER);
    g_signal_connect_data(w->scheda, "unmap", G_CALLBACK(su_unmap), w, NULL, G_CONNECT_SWAPPED);
    if (gtk_widget_get_mapped(w->scheda) && ph_dsk_widget_visibili(w->d))
        osserva(w, true); /* ricostruzione a finestra aperta: il «map» e' gia' passato */
}

static void meteo_ferma(PhDskWidget *w)
{
    Meteo *x = w->x;
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
