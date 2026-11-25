/* «App frequenti» e «Trasferimenti recenti» (CONTRATTI.md §16): due elenchi che vengono solo
 * dal PC (aperture contate nel profilo, ultimi trasferimenti della sessione). Niente richieste
 * al telefono. Riservatezza: nomi di app e di file solo a schermo, mai in un log. */
#include <string.h>

#include "interno.h"

/* ---- App frequenti ---------------------------------------------------------------------------------- */
static void app_clic(GtkButton *b, gpointer dati)
{
    PhDskWidget *w = dati;
    const char *pacchetto = g_object_get_data(G_OBJECT(b), "ph-desktop-pacchetto");
    if (pacchetto != NULL)
        ph_shell_apri_app(w->d->sh, pacchetto); /* come dal launcher */
}

static GtkWidget *tessera_app(PhDskWidget *w, const char *pacchetto, const char *nome, int lato)
{
    GtkWidget *t = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(t, "ph-desktop-app-tessera");
    gtk_widget_set_size_request(t, lato, lato);
    gtk_widget_set_halign(t, GTK_ALIGN_CENTER);
    gtk_widget_set_overflow(t, GTK_OVERFLOW_HIDDEN);
    GdkTexture *tx = ph_srv_icona(w->d->srv, pacchetto);
    GtkWidget *f;
    if (tx != NULL) {
        /* Misura fissa vera (GtkImage): un GtkPicture prenderebbe quella naturale della texture. */
        f = gtk_image_new_from_paintable(GDK_PAINTABLE(tx));
        gtk_image_set_pixel_size(GTK_IMAGE(f), lato);
        /* Icona vera: niente ombra rettangolare (come sul desktop e nel launcher). */
        gtk_widget_add_css_class(t, "ph-desktop-tessera-vera");
    } else {
        f = ph_shell_segnaposto_nuovo(pacchetto, nome, lato, 9);
    }
    gtk_box_append(GTK_BOX(t), f);
    return t;
}

static void app_disegna(PhDskWidget *w, PhDskVista *v)
{
    PhCfgTelefono *p = ph_dsk_profilo(w->d);
    ph_dsk_wx_svuota(w->corpo);
    gtk_widget_set_visible(w->corpo, TRUE);
    snprintf(v->titolo, sizeof v->titolo, "%s", ph_t("App frequenti"));
    size_t idx[12];
    size_t k = p != NULL ? ph_dsk_app_frequenti(p->aperture, p->n_aperture, ph_dsk_app_frequenti_max(w->dim), idx) : 0;
    if (k == 0) {
        GtkWidget *l = gtk_label_new(ph_t("Nessuna app aperta ancora"));
        gtk_widget_add_css_class(l, "ph-desktop-w-sotto");
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_label_set_wrap(GTK_LABEL(l), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(l), 1);
        gtk_box_append(GTK_BOX(w->corpo), l);
        return;
    }
    int colonne = w->dim == PH_CFG_WIDGET_PICCOLO ? 2 : 4;
    int lato = w->dim == PH_CFG_WIDGET_PICCOLO ? 36 : (w->dim == PH_CFG_WIDGET_GRANDE ? 44 : 38);
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(g), TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(g), 4);
    for (size_t i = 0; i < k; i++) {
        const char *pacchetto = p->aperture[idx[i]].app;
        const PhSrvApp *a = ph_srv_app_cerca(w->d->srv, pacchetto);
        const char *nome = a != NULL && a->nome != NULL && a->nome[0] != '\0' ? a->nome : pacchetto;
        GtkWidget *b = gtk_button_new();
        gtk_widget_add_css_class(b, "ph-desktop-app-frequente");
        GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        gtk_widget_set_halign(col, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(col), tessera_app(w, pacchetto, nome, lato));
        GtkWidget *l = gtk_label_new(nome);
        gtk_widget_add_css_class(l, "ph-desktop-app-nome");
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(l), 1);
        gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_CENTER);
        gtk_box_append(GTK_BOX(col), l);
        gtk_button_set_child(GTK_BUTTON(b), col);
        ph_shell_etichetta(b, nome, NULL);
        /* Il pacchetto resta sul pulsante (copia dell'oggetto, liberata con lui). */
        g_object_set_data_full(G_OBJECT(b), "ph-desktop-pacchetto", g_strdup(pacchetto), g_free);
        g_signal_connect(b, "clicked", G_CALLBACK(app_clic), w);
        gtk_grid_attach(GTK_GRID(g), b, (int)(i % (size_t)colonne), (int)(i / (size_t)colonne), 1, 1);
    }
    gtk_box_append(GTK_BOX(w->corpo), g);
}

static void app_inizia(PhDskWidget *w)
{
    w->x = GINT_TO_POINTER(1); /* nessuno stato proprio: il contenuto si rifa' dal profilo */
}

static void app_ferma(PhDskWidget *w)
{
    w->x = NULL;
}

static void app_evento(PhDskWidget *w, int ev)
{
    /* Aperture (profilo e finestre), icone e nomi arrivati: si rifa' l'elenco. */
    if (ev == PH_DSK_EV_FINESTRE || ev == PH_SRV_EV_PROFILO || ev == PH_SRV_EV_ICONA || ev == PH_SRV_EV_APP)
        ph_dsk_widget_aggiorna_uno(w);
}

const PhDskWxVt PH_DSK_WX_APP_FREQUENTI = {
    .inizia = app_inizia,
    .ferma = app_ferma,
    .aggiorna = app_disegna,
    .evento = app_evento,
};

/* ---- Trasferimenti recenti ----------------------------------------------------------------------------- */
typedef struct {
    GCancellable *annulla;
} Trasf;

/* La funzione di ritorno del launcher arriva SEMPRE, anche dopo l'annullamento e dopo che il
 * widget e' stato tolto o ricostruito: non si passa il widget ma una struttura propria con un
 * riferimento debole alla scheda e il desktop (contato) per sapere se c'e' ancora. */
typedef struct {
    PhDesktop *d;
    GWeakRef scheda;
} Apertura;

static void apertura_fatta(GObject *o, GAsyncResult *r, gpointer dati)
{
    Apertura *a = dati;
    GError *e = NULL;
    gboolean ok = gtk_file_launcher_open_containing_folder_finish(GTK_FILE_LAUNCHER(o), r, &e);
    bool annullata = e != NULL && g_error_matches(e, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error(&e); /* il testo dell'errore ha percorsi: mai su schermo ne' in un log */
    GtkWidget *scheda = g_weak_ref_get(&a->scheda);
    if (!ok && !annullata && scheda != NULL && a->d->vivo)
        ph_shell_avviso(a->d->sh, ph_t("Trasferimenti recenti"), ph_t("Non riesco ad aprire la cartella sul PC."));
    if (scheda != NULL)
        g_object_unref(scheda);
    g_weak_ref_clear(&a->scheda);
    ph_dsk_unref(a->d);
    g_free(a);
    g_object_unref(o);
}

static void trasf_clic(GtkButton *b, gpointer dati)
{
    PhDskWidget *w = dati;
    Trasf *x = w->x;
    size_t i = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(b), "ph-desktop-voce"));
    size_t n = 0;
    const PhSrvTrasferimento *t = ph_srv_trasferimenti_recenti(w->d->srv, &n);
    if (i >= n || x == NULL)
        return;
    /* Ricevuto o inviato, si apre la CARTELLA del PC che contiene il file (decisione di chi
     * coordina): per i ricevuti quella di arrivo, per gli inviati quella di partenza. */
    const char *percorso = t[i].verso == PH_SRV_RICEZIONE ? t[i].destinazione : t[i].origine;
    if (percorso == NULL || percorso[0] != '/')
        return;
    GFile *f = g_file_new_for_path(percorso);
    GtkFileLauncher *l = gtk_file_launcher_new(f);
    g_object_unref(f);
    Apertura *a = g_new0(Apertura, 1);
    a->d = ph_dsk_ref(w->d);
    g_weak_ref_init(&a->scheda, w->scheda);
    gtk_file_launcher_open_containing_folder(l, ph_shell_finestra(w->d->sh), ph_dsk_wx_rinnova(&x->annulla),
                                             apertura_fatta, a);
}

static void trasf_disegna(PhDskWidget *w, PhDskVista *v)
{
    ph_dsk_wx_svuota(w->corpo);
    gtk_widget_set_visible(w->corpo, TRUE);
    snprintf(v->titolo, sizeof v->titolo, "%s", ph_t("Trasferimenti recenti"));
    size_t n = 0;
    const PhSrvTrasferimento *t = ph_srv_trasferimenti_recenti(w->d->srv, &n);
    size_t max = ph_dsk_trasferimenti_max(w->dim);
    if (n == 0) {
        GtkWidget *l = gtk_label_new(ph_t("Nessun trasferimento in questa sessione"));
        gtk_widget_add_css_class(l, "ph-desktop-w-sotto");
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_label_set_wrap(GTK_LABEL(l), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(l), 1);
        gtk_box_append(GTK_BOX(w->corpo), l);
        return;
    }
    for (size_t i = 0; i < n && i < max; i++) {
        char nome[256];
        ph_dsk_nome_sicuro(t[i].nome, nome, sizeof nome);
        bool ricevuto = t[i].verso == PH_SRV_RICEZIONE;
        GtkWidget *b = gtk_button_new();
        gtk_widget_add_css_class(b, "ph-desktop-trasferimento");
        GtkWidget *r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_append(GTK_BOX(r), ph_shell_icona_nuova(ricevuto ? &PH_ICONA_SCARICA : &PH_ICONA_INVIA));
        GtkWidget *l = gtk_label_new(nome);
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_max_width_chars(GTK_LABEL(l), 1);
        gtk_widget_set_hexpand(l, TRUE);
        gtk_widget_add_css_class(l, "ph-desktop-w-riga-sx");
        gtk_box_append(GTK_BOX(r), l);
        gtk_button_set_child(GTK_BUTTON(b), r);
        char et[400];
        ph_lingua_componi(et, sizeof et, ph_t("Apri la cartella di {nome}"), "nome", nome,
                          (const char *)NULL);
        ph_shell_etichetta(b, et, NULL);
        g_object_set_data(G_OBJECT(b), "ph-desktop-voce", GSIZE_TO_POINTER(i));
        g_signal_connect(b, "clicked", G_CALLBACK(trasf_clic), w);
        gtk_box_append(GTK_BOX(w->corpo), b);
    }
}

static void trasf_inizia(PhDskWidget *w)
{
    w->x = g_new0(Trasf, 1);
}

static void trasf_ferma(PhDskWidget *w)
{
    Trasf *x = w->x;
    ph_dsk_wx_annulla(&x->annulla); /* la finestra «apri» non puo' arrivare dopo la chiusura */
    g_free(x);
    w->x = NULL;
}

static void trasf_evento(PhDskWidget *w, int ev)
{
    if (ev == PH_SRV_EV_TRASFERIMENTI || ev == PH_SRV_EV_PROFILO)
        ph_dsk_widget_aggiorna_uno(w);
}

const PhDskWxVt PH_DSK_WX_TRASFERIMENTI = {
    .inizia = trasf_inizia,
    .ferma = trasf_ferma,
    .aggiorna = trasf_disegna,
    .evento = trasf_evento,
};
