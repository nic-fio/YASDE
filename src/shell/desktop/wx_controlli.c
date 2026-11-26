/* Interruttori rapidi e «Trova il telefono» (CONTRATTI.md §16). Lo stato viene SEMPRE dai
 * servizi (ph_srv_controlli): mai uno stato locale presunto. Dopo un clic il controllo mostra
 * «In corso» finche' non arriva lo stato nuovo o l'esito del comando. Un comando non
 * permesso o un valore assente dicono «Non disponibile» e restano inattivi.
 *
 * Forma a capsula (passo 2b): tre tasti TONDI con icona. Alla misura minima solo i tre tondi in
 * fila (D2); quando l'altezza lo consente, sotto ogni tondo nome e stato (A7). La scelta la fa la
 * funzione pura ph_dsk_ctl_disposizione sull'area vera che il contenuto riceve (anche durante il
 * ridimensionamento con la maniglia, che non ricostruisce il widget): niente richiami a ogni
 * fotogramma, il gestore di disposizione parte solo quando l'area cambia. */
#include <string.h>

#include "interno.h"
#include "logica_controlli.h"

typedef struct Ctl Ctl;
typedef struct {
    PhDskWidget *w;
    int controllo;
} Cmd;

struct Ctl {
    GtkWidget *btn[3], *ico[3], *et[3], *stato[3];
    bool in_corso[3];
    int prima[3];      /* il valore che c'e' al clic: finche' non cambia il comando e' «in corso» */
    GCancellable *annulla[3];
    Cmd cmd[3];
    bool osserva;
};

static int valore_di(const PhSrvControlli *c, int i)
{
    return i == 0 ? c->suoneria : (i == 1 ? c->non_disturbare : c->torcia);
}

static bool puo_di(const PhSrvControlli *c, int i)
{
    return i == 0 ? c->puo_suoneria : (i == 1 ? c->puo_non_disturbare : c->puo_torcia);
}

static const PhShellIcona *icona_di(int i, int valore)
{
    switch (ph_dsk_ctl_icona(i, valore)) {
    case PH_DSK_CTL_ICONA_VIBRA: return &PH_DSK_ICONA_VIBRA;
    case PH_DSK_CTL_ICONA_SILENZIOSO: return &PH_DSK_ICONA_SILENZIOSO;
    case PH_DSK_CTL_ICONA_LUNA: return &PH_DSK_ICONA_LUNA;
    case PH_DSK_CTL_ICONA_TORCIA: return &PH_DSK_ICONA_TORCIA;
    default: return &PH_DSK_ICONA_SUONA;
    }
}

/* ---- Disposizione ------------------------------------------------------------------------------
 * Due gestori di disposizione: l'area dei tre tasti (posizione e misura di ogni tasto) e la colonna
 * di ogni tasto (tondo, e sotto nome e stato se c'e' posto). Il minimo che dichiarano e' 0: la
 * misura la decide la funzione pura, il contenuto che non entra non fa crescere la scheda.
 * Ogni figlio si misura (gtk_widget_measure) prima di allocarlo e non si alloca mai sotto il suo
 * minimo, come vuole GTK. */
static void area_misura(GtkWidget *w, GtkOrientation o, int per, int *min, int *nat, int *min_b, int *nat_b)
{
    (void)w;
    (void)per;
    *min = 0;
    *nat = o == GTK_ORIENTATION_HORIZONTAL ? 3 * PH_DSK_CTL_LATO_SOLO + 2 * PH_DSK_CTL_SPAZIO_MIN
                                           : PH_DSK_CTL_LATO_SOLO;
    *min_b = -1;
    *nat_b = -1;
}

/* Alloca `f` nel rettangolo voluto, mai sotto il minimo che il figlio dichiara. */
static void alloca_misurato(GtkWidget *f, int x, int y, int larghezza, int altezza)
{
    int min_l = 0, min_a = 0;
    gtk_widget_measure(f, GTK_ORIENTATION_HORIZONTAL, -1, &min_l, NULL, NULL, NULL);
    if (larghezza < min_l)
        larghezza = min_l;
    gtk_widget_measure(f, GTK_ORIENTATION_VERTICAL, larghezza, &min_a, NULL, NULL, NULL);
    if (altezza < min_a)
        altezza = min_a;
    graphene_point_t p = GRAPHENE_POINT_INIT((float)x, (float)y);
    gtk_widget_allocate(f, larghezza, altezza, -1, gsk_transform_translate(NULL, &p));
}

/* Larghezza (px) di un testo scritto con il carattere vero dell'etichetta, su una riga. */
static int larghezza_testo(GtkLabel *l, const char *testo)
{
    PangoLayout *copia = pango_layout_copy(gtk_label_get_layout(l));
    pango_layout_set_ellipsize(copia, PANGO_ELLIPSIZE_NONE);
    pango_layout_set_width(copia, -1);
    pango_layout_set_text(copia, testo, -1);
    int lw = 0, lh = 0;
    pango_layout_get_pixel_size(copia, &lw, &lh);
    g_object_unref(copia);
    return lw;
}

/* Le tre colonne e le misure vere dei loro testi (nome piu' largo, parola piu' larga, riga). */
static void misure_testo(GtkWidget *area, PhDskCtlMisure *m)
{
    memset(m, 0, sizeof *m);
    int i = 0;
    for (GtkWidget *b = gtk_widget_get_first_child(area); b != NULL && i < 3; b = gtk_widget_get_next_sibling(b), i++) {
        GtkWidget *col = gtk_button_get_child(GTK_BUTTON(b));
        GtkWidget *tondo = col != NULL ? gtk_widget_get_first_child(col) : NULL;
        GtkWidget *nome = tondo != NULL ? gtk_widget_get_next_sibling(tondo) : NULL;
        GtkWidget *stato = nome != NULL ? gtk_widget_get_next_sibling(nome) : NULL;
        if (stato == NULL)
            continue;
        int nat_a = 0;
        gtk_widget_measure(stato, GTK_ORIENTATION_VERTICAL, -1, NULL, &nat_a, NULL, NULL);
        if (nat_a > m->riga)
            m->riga = nat_a;
        const char *t = gtk_label_get_text(GTK_LABEL(nome));
        int tutto = larghezza_testo(GTK_LABEL(nome), t);
        if (tutto > m->nome_largo)
            m->nome_largo = tutto;
        char **parole = g_strsplit(t, " ", -1);
        for (char **p = parole; *p != NULL; p++) {
            int pw = (*p)[0] != '\0' ? larghezza_testo(GTK_LABEL(nome), *p) : 0;
            if (pw > m->parola_larga)
                m->parola_larga = pw;
        }
        g_strfreev(parole);
    }
    int o, v;
    ph_dsk_imbottitura(PH_DSK_FORMA_CAPSULA, &o, &v);
    m->riserva_v = v;
}

static void area_alloca(GtkWidget *area, int larghezza, int altezza, int base)
{
    (void)base;
    PhDskCtlMisure m;
    misure_testo(area, &m);
    PhDskCtlDisposizione dp;
    ph_dsk_ctl_disposizione(larghezza, altezza, &m, &dp);
    int i = 0;
    for (GtkWidget *b = gtk_widget_get_first_child(area); b != NULL && i < 3;
         b = gtk_widget_get_next_sibling(b), i++) {
        GtkWidget *col = gtk_button_get_child(GTK_BUTTON(b));
        if (col != NULL) {
            g_object_set_data(G_OBJECT(col), "ph-ctl-diametro", GINT_TO_POINTER(dp.diametro));
            g_object_set_data(G_OBJECT(col), "ph-ctl-righe", GINT_TO_POINTER(dp.righe_nome));
            g_object_set_data(G_OBJECT(col), "ph-ctl-riga", GINT_TO_POINTER(dp.riga));
        }
        if (!gtk_widget_should_layout(b))
            continue;
        alloca_misurato(b, dp.x + i * (dp.largo + dp.spazio), dp.y, dp.largo, dp.alto);
    }
}

static void col_misura(GtkWidget *w, GtkOrientation o, int per, int *min, int *nat, int *min_b, int *nat_b)
{
    (void)w;
    (void)per;
    (void)o;
    *min = 0;
    *nat = PH_DSK_CTL_LATO;
    *min_b = -1;
    *nat_b = -1;
}

/* Figli della colonna, in ordine: tondo (con l'icona), nome, stato. Il blocco tondo + nome + stato
 * riempie la colonna dall'alto: e' l'area a centrarlo in verticale. */
static void col_alloca(GtkWidget *col, int larghezza, int altezza, int base)
{
    (void)base;
    (void)altezza;
    int d = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(col), "ph-ctl-diametro"));
    int righe = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(col), "ph-ctl-righe"));
    int riga = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(col), "ph-ctl-riga"));
    bool testo = righe > 0;
    if (d > larghezza)
        d = larghezza;
    GtkWidget *tondo = gtk_widget_get_first_child(col);
    GtkWidget *nome = tondo != NULL ? gtk_widget_get_next_sibling(tondo) : NULL;
    GtkWidget *stato = nome != NULL ? gtk_widget_get_next_sibling(nome) : NULL;
    if (tondo != NULL && gtk_widget_should_layout(tondo))
        alloca_misurato(tondo, (larghezza - d) / 2, 0, d, d);
    /* Senza posto il testo non si disegna: lo dicono il suggerimento e l'etichetta accessibile. */
    if (nome != NULL) {
        gtk_widget_set_child_visible(nome, testo);
        if (testo && gtk_widget_should_layout(nome))
            alloca_misurato(nome, 0, d + PH_DSK_CTL_GAP, larghezza, righe * riga);
    }
    if (stato != NULL) {
        gtk_widget_set_child_visible(stato, testo);
        if (testo && gtk_widget_should_layout(stato))
            alloca_misurato(stato, 0, d + PH_DSK_CTL_GAP + righe * riga, larghezza, riga);
    }
}

/* ---- Aspetto ---------------------------------------------------------------------------------- */
static const char *const CLASSI_ASPETTO[] = {"ph-desktop-interruttore-spento", "ph-desktop-interruttore-attivo",
                                             "ph-desktop-interruttore-in-corso", "ph-desktop-interruttore-nd",
                                             "ph-desktop-interruttore-attesa",
                                             "ph-desktop-interruttore-bordo"};

static void ctl_disegna(PhDskWidget *w)
{
    Ctl *x = w->x;
    PhSrvControlli c;
    ph_srv_controlli(w->d->srv, &c);
    for (int i = 0; i < 3; i++) {
        int valore = valore_di(&c, i);
        PhDskInterruttore ii;
        ph_dsk_interruttore(i, valore, c.noto && puo_di(&c, i), x->in_corso[i], &ii);
        ph_shell_icona_imposta(x->ico[i], icona_di(i, valore));
        gtk_label_set_text(GTK_LABEL(x->et[i]), ii.etichetta);
        /* Prima della prima risposta non si dice nulla (non si inventa). */
        gtk_label_set_text(GTK_LABEL(x->stato[i]), c.noto ? ii.stato : "");
        /* Inattivo solo se il comando non e' possibile; in corso resta attivabile dalla tastiera
         * (il clic si ignora) cosi' il fuoco non sparisce a meta' comando. */
        gtk_widget_set_sensitive(x->btn[i], c.noto && ii.disponibile);
        char sug[160], et[160];
        ph_dsk_ctl_testi(c.noto, &ii, et, sizeof et, sug, sizeof sug);
        /* Nei soli tondi lo stato non e' scritto: sta nel suggerimento e nell'etichetta accessibile. */
        gtk_widget_set_tooltip_text(x->btn[i], c.noto ? sug : NULL);
        PhDskCtlAspetto a = ph_dsk_ctl_aspetto(c.noto, i, valore, &ii);
        for (int k = 0; k < (int)G_N_ELEMENTS(CLASSI_ASPETTO); k++) {
            if (k == (int)a)
                gtk_widget_add_css_class(x->btn[i], CLASSI_ASPETTO[k]);
            else
                gtk_widget_remove_css_class(x->btn[i], CLASSI_ASPETTO[k]);
        }
        gtk_accessible_update_property(GTK_ACCESSIBLE(x->btn[i]), GTK_ACCESSIBLE_PROPERTY_LABEL, et, -1);
        ph_shell_premuto(x->btn[i], a == PH_DSK_CTL_ATTIVO);
    }
}

static void cmd_fatto(PhServizi *s, PhSrvEsito esito, const char *frase, void *utente)
{
    (void)s;
    Cmd *m = utente;
    PhDskWidget *w = m->w;
    Ctl *x = w->x;
    if (x == NULL || !w->d->vivo)
        return;
    g_clear_object(&x->annulla[m->controllo]);
    x->in_corso[m->controllo] = false; /* l'esito e' arrivato: lo stato vero lo dice la fotografia */
    if (esito != PH_SRV_ESITO_OK) {
        PhDskInterruttore ii;
        ph_dsk_interruttore(m->controllo, 0, true, false, &ii);
        ph_shell_avviso(w->d->sh, ii.etichetta,
                        frase != NULL && frase[0] != '\0' ? frase : ph_t("Il telefono non ha eseguito il comando"));
    }
    ctl_disegna(w);
}

static void su_clic(GtkButton *b, gpointer dati)
{
    PhDskWidget *w = dati;
    Ctl *x = w->x;
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "ph-desktop-controllo"));
    PhSrvControlli c;
    ph_srv_controlli(w->d->srv, &c);
    int valore = valore_di(&c, i);
    if (!c.noto || !puo_di(&c, i) || valore == PH_SRV_NON_DISPONIBILE || x->in_corso[i])
        return;
    x->in_corso[i] = true;
    x->prima[i] = valore;
    x->cmd[i].w = w;
    x->cmd[i].controllo = i;
    ph_srv_controllo_imposta(w->d->srv, (PhSrvControllo)i, ph_dsk_prossimo_valore(i, valore),
                             ph_dsk_wx_rinnova(&x->annulla[i]), cmd_fatto, &x->cmd[i]);
    ctl_disegna(w);
}

static void ctl_inizia(PhDskWidget *w)
{
    Ctl *x = g_new0(Ctl, 1);
    w->x = x;
    GtkWidget *area = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); /* GtkBox: stacca i figli da solo */
    gtk_widget_set_layout_manager(area, gtk_custom_layout_new(NULL, area_misura, area_alloca));
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    gtk_widget_add_css_class(area, "ph-desktop-interruttori");
    for (int i = 0; i < 3; i++) {
        x->btn[i] = gtk_button_new();
        gtk_widget_add_css_class(x->btn[i], "ph-desktop-interruttore");
        gtk_widget_add_css_class(x->btn[i], CLASSI_ASPETTO[PH_DSK_CTL_ATTESA_DATI]);
        GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_layout_manager(col, gtk_custom_layout_new(NULL, col_misura, col_alloca));
        GtkWidget *tondo = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(tondo, "ph-desktop-interruttore-tondo");
        x->ico[i] = ph_shell_icona_nuova(&PH_DSK_ICONA_SUONA);
        gtk_widget_set_halign(x->ico[i], GTK_ALIGN_CENTER);
        gtk_widget_set_valign(x->ico[i], GTK_ALIGN_CENTER);
        gtk_widget_set_vexpand(x->ico[i], TRUE);
        gtk_box_append(GTK_BOX(tondo), x->ico[i]);
        x->et[i] = gtk_label_new("");
        gtk_widget_add_css_class(x->et[i], "ph-desktop-interruttore-nome");
        x->stato[i] = gtk_label_new("");
        gtk_widget_add_css_class(x->stato[i], "ph-desktop-interruttore-stato");
        gtk_label_set_ellipsize(GTK_LABEL(x->et[i]), PANGO_ELLIPSIZE_END);
        gtk_label_set_ellipsize(GTK_LABEL(x->stato[i]), PANGO_ELLIPSIZE_END);
        gtk_label_set_width_chars(GTK_LABEL(x->et[i]), 1);
        gtk_label_set_width_chars(GTK_LABEL(x->stato[i]), 1);
        /* Il nome non si taglia mai: se non sta su una riga va a capo tra le parole (al massimo due
         * righe, deciso da ph_dsk_ctl_disposizione); centrato sotto il tondo. */
        gtk_label_set_wrap(GTK_LABEL(x->et[i]), TRUE);
        gtk_label_set_wrap_mode(GTK_LABEL(x->et[i]), PANGO_WRAP_WORD);
        gtk_label_set_lines(GTK_LABEL(x->et[i]), 2);
        gtk_label_set_justify(GTK_LABEL(x->et[i]), GTK_JUSTIFY_CENTER);
        gtk_label_set_xalign(GTK_LABEL(x->et[i]), 0.5f);
        gtk_label_set_yalign(GTK_LABEL(x->et[i]), 0.0f);
        gtk_box_append(GTK_BOX(col), tondo);
        gtk_box_append(GTK_BOX(col), x->et[i]);
        gtk_box_append(GTK_BOX(col), x->stato[i]);
        gtk_button_set_child(GTK_BUTTON(x->btn[i]), col);
        g_object_set_data(G_OBJECT(x->btn[i]), "ph-desktop-controllo", GINT_TO_POINTER(i));
        g_signal_connect(x->btn[i], "clicked", G_CALLBACK(su_clic), w);
        gtk_box_append(GTK_BOX(area), x->btn[i]);
    }
    gtk_box_append(GTK_BOX(w->corpo), area);
    gtk_widget_set_visible(w->corpo, TRUE);
    /* Gli interruttori arrivano da soli finche' il widget c'e' (osservati). */
    ph_srv_controlli_osserva(w->d->srv, true);
    x->osserva = true;
    ctl_disegna(w);
}

static void ctl_ferma(PhDskWidget *w)
{
    Ctl *x = w->x;
    for (int i = 0; i < 3; i++)
        ph_dsk_wx_annulla(&x->annulla[i]);
    if (x->osserva && w->d->vivo) /* nel "destroy" della radice i servizi non si toccano */
        ph_srv_controlli_osserva(w->d->srv, false);
    g_free(x);
    w->x = NULL;
}

static void ctl_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    (void)v; /* la capsula non ha titolo: nome e stato stanno nei tasti */
    ctl_disegna(w);
}

static void ctl_evento(PhDskWidget *w, int ev)
{
    if (ev != PH_SRV_EV_CONTROLLI)
        return;
    Ctl *x = w->x;
    PhSrvControlli c;
    ph_srv_controlli(w->d->srv, &c);
    for (int i = 0; i < 3; i++)
        if (x->in_corso[i] && valore_di(&c, i) != x->prima[i])
            x->in_corso[i] = false; /* e' arrivato lo stato nuovo */
    ctl_disegna(w);
}

const PhDskWxVt PH_DSK_WX_INTERRUTTORI = {
    .inizia = ctl_inizia,
    .ferma = ctl_ferma,
    .aggiorna = ctl_aggiorna,
    .evento = ctl_evento,
};

/* ---- Trova il telefono ----------------------------------------------------------------------------- */
typedef struct {
    GtkWidget *btn, *ico, *testo, *resta;
    bool in_attesa;      /* comando di avvio mandato, nessun esito ancora */
    bool era;            /* trova_in_corso all'ultimo controllo */
    int64_t inizio_us;   /* quando l'ABBIAMO avviato noi (0 = non lo sappiamo: nessun conto alla rovescia) */
    GCancellable *annulla;
    bool osserva;
} Trova;

static void trova_disegna(PhDskWidget *w)
{
    Trova *x = w->x;
    PhSrvControlli c;
    ph_srv_controlli(w->d->srv, &c);
    bool suona = c.noto && c.trova_in_corso;
    int64_t ora = g_get_monotonic_time();
    /* Il tempo rimasto NON lo dicono i servizi: il conto alla rovescia c'e' solo per lo squillo
     * che abbiamo avviato noi (quando e' partito lo sappiamo); altrimenti «Sta squillando». */
    if (x->era && !suona)
        x->inizio_us = 0;
    if (!suona && x->inizio_us != 0 && !x->in_attesa && ora - x->inizio_us > 3 * G_USEC_PER_SEC)
        x->inizio_us = 0; /* il comando non ha mai fatto squillare: si dimentica */
    bool cambio = suona != x->era;
    x->era = suona;
    /* Mentre squilla serve un aggiornamento al secondo: si riprogramma il timer condiviso. */
    if (cambio)
        ph_dsk_widget_timer_riprogramma(w->d);
    bool non_disp = c.noto && !c.puo_trova;
    bool utilizzabile = c.noto && c.puo_trova && !x->in_attesa;
    gtk_label_set_text(GTK_LABEL(x->testo), non_disp ? ph_t("Non disponibile")
                                                      : (x->in_attesa ? ph_t("In corso")
                                                                      : (suona ? ph_t("Ferma") : ph_t("Fai squillare"))));
    gtk_widget_set_visible(x->ico, !non_disp);
    gtk_widget_set_sensitive(x->btn, utilizzabile);
    gtk_widget_set_tooltip_text(x->btn, non_disp ? ph_t("Non disponibile su questo telefono") : NULL);
    if (non_disp)
        gtk_widget_add_css_class(x->btn, "ph-desktop-trova-spento");
    else
        gtk_widget_remove_css_class(x->btn, "ph-desktop-trova-spento");
    if (suona)
        gtk_widget_add_css_class(x->btn, "ph-desktop-trova-ferma");
    else
        gtk_widget_remove_css_class(x->btn, "ph-desktop-trova-ferma");
    char t[48] = "";
    if (suona && x->inizio_us != 0)
        ph_dsk_trova_testo(ph_dsk_trova_rimasti(PH_DSK_TROVA_SECONDI, (ora - x->inizio_us) / 1000), t, sizeof t);
    else if (suona)
        snprintf(t, sizeof t, "%s", ph_t("Sta squillando"));
    gtk_label_set_text(GTK_LABEL(x->resta), t);
    gtk_widget_set_visible(x->resta, t[0] != '\0' && w->dim != PH_CFG_WIDGET_PICCOLO);
    char et[160];
    ph_lingua_componi(et, sizeof et, ph_t("{titolo}: {testo}"), "titolo", ph_t("Trova il telefono"), "testo",
                      non_disp ? ph_t("Non disponibile") : (suona ? ph_t("Ferma") : ph_t("Fai squillare")), (const char *)NULL);
    ph_shell_etichetta(x->btn, et, NULL);
}

static void trova_fatto(PhServizi *s, PhSrvEsito esito, const char *frase, void *utente)
{
    (void)s;
    PhDskWidget *w = utente;
    Trova *x = w->x;
    if (x == NULL || !w->d->vivo)
        return;
    g_clear_object(&x->annulla);
    x->in_attesa = false;
    if (esito != PH_SRV_ESITO_OK)
        ph_shell_avviso(w->d->sh, ph_t("Trova il telefono"),
                        frase != NULL && frase[0] != '\0' ? frase : ph_t("Il telefono non ha eseguito il comando"));
    trova_disegna(w);
}

static void trova_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    PhDskWidget *w = dati;
    Trova *x = w->x;
    PhSrvControlli c;
    ph_srv_controlli(w->d->srv, &c);
    if (!c.noto || !c.puo_trova || x->in_attesa)
        return;
    if (c.trova_in_corso) {
        ph_srv_trova_ferma(w->d->srv);
        return;
    }
    /* Nessuna conferma: un clic e squilla, per il tempo predefinito. */
    x->in_attesa = true;
    x->inizio_us = g_get_monotonic_time(); /* lo squillo lo avviamo noi: il tempo lo sappiamo */
    ph_srv_trova_avvia(w->d->srv, PH_DSK_TROVA_SECONDI, ph_dsk_wx_rinnova(&x->annulla), trova_fatto, w);
    trova_disegna(w);
}

static void trova_inizia(PhDskWidget *w)
{
    Trova *x = g_new0(Trova, 1);
    w->x = x;
    x->btn = gtk_button_new();
    gtk_widget_add_css_class(x->btn, "ph-desktop-trova");
    GtkWidget *r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(r, GTK_ALIGN_CENTER);
    x->ico = ph_shell_icona_nuova(&PH_DSK_ICONA_TROVA);
    x->testo = gtk_label_new("");
    gtk_box_append(GTK_BOX(r), x->ico);
    gtk_box_append(GTK_BOX(r), x->testo);
    gtk_button_set_child(GTK_BUTTON(x->btn), r);
    g_signal_connect(x->btn, "clicked", G_CALLBACK(trova_clic), w);
    x->resta = gtk_label_new("");
    gtk_widget_add_css_class(x->resta, "ph-desktop-w-sotto");
    gtk_widget_set_margin_top(x->resta, 6);
    gtk_box_append(GTK_BOX(w->corpo), x->btn);
    gtk_box_append(GTK_BOX(w->corpo), x->resta);
    if (w->dim == PH_CFG_WIDGET_GRANDE) {
        GtkWidget *n = gtk_label_new(ph_t("Squilla anche se il telefono è in silenzioso e si ferma da solo."));
        gtk_widget_add_css_class(n, "ph-desktop-w-sotto");
        gtk_label_set_xalign(GTK_LABEL(n), 0);
        gtk_label_set_wrap(GTK_LABEL(n), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(n), 1);
        gtk_widget_set_margin_top(n, 6);
        gtk_box_append(GTK_BOX(w->corpo), n);
    }
    gtk_widget_set_visible(w->corpo, TRUE);
    ph_srv_controlli_osserva(w->d->srv, true);
    x->osserva = true;
    trova_disegna(w);
}

static void trova_ferma(PhDskWidget *w)
{
    Trova *x = w->x;
    ph_dsk_wx_annulla(&x->annulla);
    if (x->osserva && w->d->vivo) /* nel "destroy" della radice i servizi non si toccano */
        ph_srv_controlli_osserva(w->d->srv, false);
    g_free(x);
    w->x = NULL;
}

static void trova_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    trova_disegna(w);
    if (w->dim != PH_CFG_WIDGET_PICCOLO)
        snprintf(v->titolo, sizeof v->titolo, "%s", ph_t("Trova il telefono"));
}

static void trova_evento(PhDskWidget *w, int ev)
{
    if (ev == PH_SRV_EV_CONTROLLI)
        trova_disegna(w);
}

static void trova_tick(PhDskWidget *w, int64_t ora_us)
{
    (void)ora_us;
    Trova *x = w->x;
    if (x->era)
        trova_disegna(w); /* il conto alla rovescia scorre */
}

static bool trova_secondi(PhDskWidget *w)
{
    Trova *x = w->x;
    return x->era && x->inizio_us != 0; /* un aggiornamento al secondo solo con il conto alla rovescia */
}

const PhDskWxVt PH_DSK_WX_TROVA = {
    .inizia = trova_inizia,
    .ferma = trova_ferma,
    .aggiorna = trova_aggiorna,
    .tick = trova_tick,
    .secondi = trova_secondi,
    .evento = trova_evento,
};
