/* Calendario del mese (CONTRATTI.md §16): tre stili (Mese, Settimana, Agenda) e il
 * Piccolo con «N impegni oggi». Sola lettura. I dati (titoli compresi) stanno solo in
 * questa memoria, mai su disco e mai in un log. I giorni degli eventi si attribuiscono
 * con le regole dell'aggiunta al §16 (logica_wx.c). */
#include <string.h>

#include "interno.h"

#define EV_MAX PH_SRV_CALENDARIO_INTERVALLO_MAX
#define RICHIESTA_OGNI_US (15 * 60 * G_USEC_PER_SEC)

typedef struct {
    int anno, mese;       /* mese mostrato (stile Mese e Piccolo) */
    int64_t inizio;       /* primo giorno mostrato (Settimana: il lunedi'; Agenda: oggi) */
    int64_t scelto;       /* giorno scelto con un clic */
    int64_t oggi;         /* giorno locale all'ultimo controllo */
    PhDskDato stato;
    char frase[192];
    char *titoli[EV_MAX];
    PhDskEvento ev[EV_MAX];
    size_t n;
    bool troncato; /* il telefono ha mandato solo una parte degli impegni */
    char app[PH_CFG_LUN_APP]; /* pacchetto dell'app Calendario del telefono ("" = nessuna) */
    GCancellable *annulla;
    int64_t t_richiesta_us;
} Cal;

static int64_t adesso_ms(void)
{
    return g_get_real_time() / 1000;
}

static void eventi_azzera(Cal *c)
{
    for (size_t i = 0; i < c->n; i++) {
        g_free(c->titoli[i]);
        c->titoli[i] = NULL;
    }
    c->n = 0;
}

/* ---- Intervallo da chiedere ----------------------------------------------------------------- */
static void intervallo(PhDskWidget *w, const Cal *c, int64_t *g0, int *n)
{
    if (w->dim == PH_CFG_WIDGET_PICCOLO || w->stile == PH_DSK_CAL_MESE) {
        PhDskMese m;
        if (ph_dsk_mese(c->anno, c->mese, &m)) {
            *g0 = ph_dsk_giorno_civile(c->anno, c->mese, 1);
            *n = m.giorni;
            return;
        }
    }
    *g0 = c->inizio;
    *n = w->stile == PH_DSK_CAL_SETTIMANA ? 7 : 14;
}

static void cal_fatto(PhServizi *s, PhSrvEsito esito, const char *frase, const PhSrvEventoCalendario *v, size_t n,
                      bool troncato, const char *app, void *utente)
{
    (void)s;
    PhDskWidget *w = utente;
    Cal *c = w->x;
    if (c == NULL || !w->d->vivo)
        return;
    g_clear_object(&c->annulla);
    eventi_azzera(c);
    c->troncato = esito == PH_SRV_ESITO_OK && troncato;
    c->stato = ph_dsk_dato_da_esito(esito, frase);
    g_strlcpy(c->frase, c->stato == PH_DSK_DATO_ERRORE ? frase : "", sizeof c->frase);
    g_strlcpy(c->app, app != NULL ? app : "", sizeof c->app);
    if (esito == PH_SRV_ESITO_OK && v != NULL) {
        if (n > EV_MAX)
            n = EV_MAX;
        for (size_t i = 0; i < n; i++) {
            c->titoli[i] = g_strdup(v[i].titolo != NULL ? v[i].titolo : "");
            c->ev[i].titolo = c->titoli[i];
            c->ev[i].inizio_ms = v[i].inizio_ms;
            c->ev[i].fine_ms = v[i].fine_ms;
            c->ev[i].tutto_il_giorno = v[i].tutto_il_giorno;
        }
        c->n = n;
    }
    ph_dsk_widget_aggiorna_uno(w);
}

static void cal_richiedi(PhDskWidget *w)
{
    Cal *c = w->x;
    int64_t g0;
    int n;
    intervallo(w, c, &g0, &n);
    uint64_t da, a;
    if (!ph_dsk_giorni_intervallo(NULL, g0, n, &da, &a))
        return;
    c->t_richiesta_us = g_get_monotonic_time();
    ph_srv_tel_calendario_intervallo(w->d->srv, da, a, ph_dsk_wx_rinnova(&c->annulla), cal_fatto, w);
}

/* ---- Costruzione del contenuto ------------------------------------------------------------------ */
static void apri_calendario(PhDskWidget *w)
{
    Cal *c = w->x;
    if (c != NULL && c->app[0] != '\0')
        ph_shell_apri_app(w->d->sh, c->app); /* come dal launcher; niente se l'app non e' nota */
}

static void su_apri(GtkButton *b, gpointer dati)
{
    (void)b;
    apri_calendario(dati);
}

static void naviga(PhDskWidget *w, int delta)
{
    Cal *c = w->x;
    int64_t oggi = ph_dsk_giorno_locale(NULL, (uint64_t)adesso_ms());
    if (w->stile == PH_DSK_CAL_SETTIMANA) {
        c->inizio += 7 * delta;
        c->scelto = (oggi >= c->inizio && oggi < c->inizio + 7) ? oggi : c->inizio;
    } else {
        ph_dsk_mese_sposta(&c->anno, &c->mese, delta);
        int ay, am, ad;
        ph_dsk_civile_da_giorno(oggi, &ay, &am, &ad);
        c->scelto = (ay == c->anno && am == c->mese) ? oggi : ph_dsk_giorno_civile(c->anno, c->mese, 1);
    }
    ph_dsk_widget_aggiorna_uno(w); /* subito la griglia nuova, senza puntini */
    if (ph_dsk_wx_puo_chiedere(w))
        cal_richiedi(w);
}

static void su_prec(GtkButton *b, gpointer dati)
{
    (void)b;
    naviga(dati, -1);
}

static void su_succ(GtkButton *b, gpointer dati)
{
    (void)b;
    naviga(dati, +1);
}

static void su_giorno(GtkButton *b, gpointer dati)
{
    PhDskWidget *w = dati;
    Cal *c = w->x;
    c->scelto = (int64_t)GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(b), "ph-desktop-giorno"));
    ph_dsk_widget_aggiorna_uno(w);
}

static GtkWidget *bottone_freccia(const PhShellIcona *icona, const char *etichetta, GCallback cb, PhDskWidget *w)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "ph-desktop-freccia");
    gtk_button_set_child(GTK_BUTTON(b), ph_shell_icona_nuova(icona));
    ph_shell_etichetta(b, etichetta, etichetta);
    g_signal_connect(b, "clicked", cb, w);
    return b;
}

/* «Novembre 2026» con le frecce: il titolo apre l'app Calendario del telefono. */
static GtkWidget *intestazione(PhDskWidget *w, const char *titolo, bool frecce)
{
    Cal *c = w->x;
    GtkWidget *r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(r, "ph-desktop-cal-intestazione");
    if (frecce)
        gtk_box_append(GTK_BOX(r), bottone_freccia(&PH_DSK_ICONA_PREC, ph_t("Precedente"), G_CALLBACK(su_prec), w));
    GtkWidget *t = gtk_button_new_with_label(titolo);
    gtk_widget_add_css_class(t, "ph-desktop-cal-titolo");
    gtk_widget_set_hexpand(t, TRUE);
    gtk_widget_set_sensitive(t, c->app[0] != '\0');
    ph_shell_etichetta(t, titolo, c->app[0] != '\0' ? ph_t("Apri l'app Calendario del telefono") : NULL);
    g_signal_connect(t, "clicked", G_CALLBACK(su_apri), w);
    gtk_box_append(GTK_BOX(r), t);
    if (frecce)
        gtk_box_append(GTK_BOX(r), bottone_freccia(&PH_DSK_ICONA_SUCC, ph_t("Successivo"), G_CALLBACK(su_succ), w));
    return r;
}

static GtkWidget *messaggio(const char *testo)
{
    /* Frasi lunghe del telefono: vanno a capo, mai oltre la scheda. */
    GtkWidget *l = gtk_label_new(testo);
    gtk_widget_add_css_class(l, "ph-desktop-w-sotto");
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 1);
    return l;
}

static GtkWidget *etichetta(const char *testo, const char *classe, float xalign)
{
    GtkWidget *l = gtk_label_new(testo);
    gtk_widget_add_css_class(l, classe);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    return l;
}

/* Discreta, quando il telefono ha mandato solo una parte degli impegni. */
static void nota_troncato(PhDskWidget *w, GtkWidget *box)
{
    Cal *c = w->x;
    if (c->troncato)
        gtk_box_append(GTK_BOX(box), messaggio(ph_t("Altri impegni non mostrati")));
}

/* Un impegno: titolo a sinistra, orario a destra; il clic apre l'app Calendario. */
static GtkWidget *riga_impegno(PhDskWidget *w, size_t idx)
{
    Cal *c = w->x;
    char orario[48];
    ph_dsk_evento_orario(&c->ev[idx], NULL, orario, sizeof orario);
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "ph-desktop-impegno");
    GtkWidget *r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *t = etichetta(c->titoli[idx], "ph-desktop-w-riga-sx", 0);
    gtk_label_set_ellipsize(GTK_LABEL(t), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(t), 1);
    gtk_widget_set_hexpand(t, TRUE);
    gtk_box_append(GTK_BOX(r), t);
    gtk_box_append(GTK_BOX(r), etichetta(orario, "ph-desktop-w-riga-dx", 1));
    gtk_button_set_child(GTK_BUTTON(b), r);
    char et[300];
    ph_lingua_componi(et, sizeof et, ph_t("{titolo}: {testo}"), "titolo", c->titoli[idx], "testo", orario,
                      (const char *)NULL);
    ph_shell_etichetta(b, et, c->app[0] != '\0' ? ph_t("Apri l'app Calendario del telefono") : NULL);
    g_signal_connect(b, "clicked", G_CALLBACK(su_apri), w);
    return b;
}

/* Gli impegni del giorno scelto, al massimo `max` righe. */
static GtkWidget *elenco_giorno(PhDskWidget *w, size_t max)
{
    Cal *c = w->x;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(box, "ph-desktop-cal-elenco");
    if (c->stato == PH_DSK_DATO_NON_DISPONIBILE || c->stato == PH_DSK_DATO_ERRORE) {
        gtk_box_append(GTK_BOX(box), messaggio(c->stato == PH_DSK_DATO_ERRORE ? ph_t("Errore") : ph_t("Non disponibile")));
        if (c->frase[0] != '\0')
            gtk_box_append(GTK_BOX(box), messaggio(c->frase));
        return box;
    }
    if (c->stato == PH_DSK_DATO_ATTESA)
        return box; /* non si inventa nulla */
    size_t idx[EV_MAX];
    size_t k = ph_dsk_eventi_del_giorno(c->ev, c->n, NULL, c->scelto, idx, EV_MAX);
    if (k == 0) {
        gtk_box_append(GTK_BOX(box), etichetta(ph_t("Nessun impegno"), "ph-desktop-w-sotto", 0));
        nota_troncato(w, box);
        return box;
    }
    for (size_t i = 0; i < k && i < max; i++)
        gtk_box_append(GTK_BOX(box), riga_impegno(w, idx[i]));
    if (k > max) {
        char t[64];
        ph_dsk_altri_testo(k - max, t, sizeof t);
        gtk_box_append(GTK_BOX(box), etichetta(t, "ph-desktop-w-sotto", 0));
    }
    nota_troncato(w, box);
    return box;
}

/* Un giorno: bottone vero con il numero e il puntino degli impegni. */
static GtkWidget *bottone_giorno(PhDskWidget *w, int64_t g, int64_t oggi, unsigned conteggio, bool settimana)
{
    Cal *c = w->x;
    int a, m, d;
    ph_dsk_civile_da_giorno(g, &a, &m, &d);
    char num[8];
    snprintf(num, sizeof num, "%d", d);
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "ph-desktop-giorno");
    if (g == oggi)
        gtk_widget_add_css_class(b, "ph-desktop-giorno-oggi");
    if (g == c->scelto)
        gtk_widget_add_css_class(b, "ph-desktop-giorno-scelto");
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_halign(col, GTK_ALIGN_CENTER);
    if (settimana)
        gtk_box_append(GTK_BOX(col), etichetta(ph_dsk_giorno_breve(ph_dsk_giorno_settimana_di(g)),
                                               "ph-desktop-giorno-nome", 0.5f));
    gtk_box_append(GTK_BOX(col), etichetta(num, "ph-desktop-giorno-numero", 0.5f));
    GtkWidget *punti = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_halign(punti, GTK_ALIGN_CENTER);
    unsigned barre = conteggio > 3 ? 3 : conteggio;
    if (!settimana && barre > 0)
        barre = 1; /* nel mese un solo puntino */
    for (unsigned i = 0; i < (barre > 0 ? barre : 1); i++) {
        GtkWidget *p = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(p, "ph-desktop-punto");
        gtk_widget_set_opacity(p, barre > 0 ? 1.0 : 0.0); /* il posto resta: la griglia non salta */
        gtk_box_append(GTK_BOX(punti), p);
    }
    gtk_box_append(GTK_BOX(col), punti);
    gtk_button_set_child(GTK_BUTTON(b), col);
    char data[96], et[160], n2[24], imp[64];
    ph_dsk_data_lunga(ph_dsk_giorno_settimana_di(g) + 1, d, m, data, sizeof data);
    if (conteggio > 0) {
        snprintf(n2, sizeof n2, "%u", conteggio);
        if (conteggio == 1)
            g_strlcpy(imp, ph_t("1 impegno"), sizeof imp);
        else
            ph_lingua_componi(imp, sizeof imp, ph_t("{numero} impegni"), "numero", n2, (const char *)NULL);
        ph_lingua_componi(et, sizeof et, ph_t("{data}, {impegni}"), "data", data, "impegni", imp, (const char *)NULL);
    } else {
        g_strlcpy(et, data, sizeof et);
    }
    ph_shell_etichetta(b, et, NULL);
    ph_shell_premuto(b, g == c->scelto);
    g_object_set_data(G_OBJECT(b), "ph-desktop-giorno", GSIZE_TO_POINTER((gsize)g));
    g_signal_connect(b, "clicked", G_CALLBACK(su_giorno), w);
    return b;
}

static void disegna_mese(PhDskWidget *w, int64_t oggi)
{
    Cal *c = w->x;
    char titolo[96];
    ph_dsk_mese_titolo(c->anno, c->mese, titolo, sizeof titolo);
    gtk_box_append(GTK_BOX(w->corpo), intestazione(w, titolo, true));
    PhDskMese m;
    if (!ph_dsk_mese(c->anno, c->mese, &m))
        return;
    int64_t g1 = ph_dsk_giorno_civile(c->anno, c->mese, 1);
    uint8_t conti[31];
    ph_dsk_conta_per_giorno(c->ev, c->n, NULL, g1, m.giorni, conti);
    GtkWidget *g = gtk_grid_new();
    gtk_widget_add_css_class(g, "ph-desktop-cal-griglia");
    gtk_grid_set_row_spacing(GTK_GRID(g), 2);
    gtk_grid_set_column_spacing(GTK_GRID(g), 2);
    gtk_grid_set_column_homogeneous(GTK_GRID(g), TRUE);
    for (int col = 0; col < 7; col++)
        gtk_grid_attach(GTK_GRID(g), etichetta(ph_dsk_giorno_breve(col), "ph-desktop-giorno-nome", 0.5f), col, 0, 1, 1);
    for (int cella = 0; cella < m.righe * 7; cella++) {
        int giorno = ph_dsk_mese_cella(&m, cella);
        GtkWidget *x;
        if (giorno == 0)
            x = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        else
            x = bottone_giorno(w, g1 + giorno - 1, oggi, c->stato == PH_DSK_DATO_OK ? conti[giorno - 1] : 0, false);
        gtk_grid_attach(GTK_GRID(g), x, cella % 7, 1 + cella / 7, 1, 1);
    }
    gtk_box_append(GTK_BOX(w->corpo), g);
    gtk_box_append(GTK_BOX(w->corpo), elenco_giorno(w, w->dim == PH_CFG_WIDGET_GRANDE ? 4 : 2));
}

static void disegna_settimana(PhDskWidget *w, int64_t oggi)
{
    Cal *c = w->x;
    char titolo[96];
    int a, m, d;
    ph_dsk_civile_da_giorno(c->inizio + 3, &a, &m, &d); /* il giovedi' decide il mese del titolo */
    ph_dsk_mese_titolo(a, m, titolo, sizeof titolo);
    gtk_box_append(GTK_BOX(w->corpo), intestazione(w, titolo, true));
    uint8_t conti[7];
    ph_dsk_conta_per_giorno(c->ev, c->n, NULL, c->inizio, 7, conti);
    GtkWidget *riga = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_set_homogeneous(GTK_BOX(riga), TRUE);
    for (int i = 0; i < 7; i++)
        gtk_box_append(GTK_BOX(riga), bottone_giorno(w, c->inizio + i, oggi, c->stato == PH_DSK_DATO_OK ? conti[i] : 0, true));
    gtk_box_append(GTK_BOX(w->corpo), riga);
    gtk_box_append(GTK_BOX(w->corpo), elenco_giorno(w, w->dim == PH_CFG_WIDGET_GRANDE ? 4 : 2));
}

static void disegna_agenda(PhDskWidget *w, int64_t oggi)
{
    Cal *c = w->x;
    gtk_box_append(GTK_BOX(w->corpo), intestazione(w, ph_dsk_stile_nome_di(PH_CFG_WIDGET_CALENDARIO_MESE, PH_DSK_CAL_AGENDA), false));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(box, "ph-desktop-cal-elenco");
    if (c->stato != PH_DSK_DATO_OK) {
        if (c->stato != PH_DSK_DATO_ATTESA)
            gtk_box_append(GTK_BOX(box), messaggio(c->stato == PH_DSK_DATO_ERRORE ? ph_t("Errore") : ph_t("Non disponibile")));
        if (c->frase[0] != '\0')
            gtk_box_append(GTK_BOX(box), messaggio(c->frase));
        gtk_box_append(GTK_BOX(w->corpo), box);
        return;
    }
    PhDskAgendaRiga righe[16];
    size_t max = w->dim == PH_CFG_WIDGET_GRANDE ? 10 : 6;
    size_t k = ph_dsk_agenda(c->ev, c->n, NULL, oggi, 14, righe, max);
    if (k == 0)
        gtk_box_append(GTK_BOX(box), etichetta(ph_t("Nessun impegno"), "ph-desktop-w-sotto", 0));
    for (size_t i = 0; i < k; i++) {
        if (righe[i].evento == (size_t)-1) {
            char e[48];
            ph_dsk_giorno_etichetta(righe[i].giorno, oggi, e, sizeof e);
            gtk_box_append(GTK_BOX(box), etichetta(e, "ph-desktop-agenda-giorno", 0));
        } else {
            gtk_box_append(GTK_BOX(box), riga_impegno(w, righe[i].evento));
        }
    }
    nota_troncato(w, box);
    gtk_box_append(GTK_BOX(w->corpo), box);
}

/* ---- Tabella ------------------------------------------------------------------------------------------ */
static void cal_inizia(PhDskWidget *w)
{
    Cal *c = g_new0(Cal, 1);
    w->x = c;
    c->oggi = ph_dsk_giorno_locale(NULL, (uint64_t)adesso_ms());
    int d;
    ph_dsk_civile_da_giorno(c->oggi, &c->anno, &c->mese, &d);
    c->scelto = c->oggi;
    c->inizio = w->stile == PH_DSK_CAL_SETTIMANA ? ph_dsk_inizio_settimana(c->oggi) : c->oggi;
    c->stato = PH_DSK_DATO_ATTESA;
}

static void cal_ferma(PhDskWidget *w)
{
    Cal *c = w->x;
    ph_dsk_wx_annulla(&c->annulla);
    eventi_azzera(c);
    g_free(c);
    w->x = NULL;
}

static void cal_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    Cal *c = w->x;
    int64_t oggi = ph_dsk_giorno_locale(NULL, (uint64_t)adesso_ms());
    ph_dsk_wx_svuota(w->corpo);
    if (w->dim == PH_CFG_WIDGET_PICCOLO) {
        gtk_widget_set_visible(w->corpo, FALSE);
        ph_dsk_vista_calendario_mese_piccolo(c->stato, c->frase, c->ev, c->n, NULL, oggi, v);
        return;
    }
    gtk_widget_set_visible(w->corpo, TRUE);
    if (w->stile == PH_DSK_CAL_SETTIMANA)
        disegna_settimana(w, oggi);
    else if (w->stile == PH_DSK_CAL_AGENDA)
        disegna_agenda(w, oggi);
    else
        disegna_mese(w, oggi);
}

static void cal_tick(PhDskWidget *w, int64_t ora_us)
{
    Cal *c = w->x;
    int64_t oggi = ph_dsk_giorno_locale(NULL, (uint64_t)adesso_ms());
    bool cambio_giorno = oggi != c->oggi;
    if (cambio_giorno) {
        c->oggi = oggi;
        int d;
        if (w->stile == PH_DSK_CAL_AGENDA)
            c->inizio = oggi;
        if (w->dim == PH_CFG_WIDGET_PICCOLO)
            ph_dsk_civile_da_giorno(oggi, &c->anno, &c->mese, &d);
        ph_dsk_widget_aggiorna_uno(w);
    }
    /* Ogni 15 minuti (e al cambio di giorno); il cambio di mese lo chiede la navigazione. */
    if ((cambio_giorno || ora_us - c->t_richiesta_us >= RICHIESTA_OGNI_US) && ph_dsk_wx_puo_chiedere(w))
        cal_richiedi(w);
}

static void cal_richiedi_vt(PhDskWidget *w)
{
    cal_richiedi(w);
}

const PhDskWxVt PH_DSK_WX_CALENDARIO_MESE = {
    .inizia = cal_inizia,
    .ferma = cal_ferma,
    .aggiorna = cal_aggiorna,
    .richiedi = cal_richiedi_vt,
    .tick = cal_tick,
};
