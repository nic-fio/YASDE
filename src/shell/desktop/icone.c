/* Icone del desktop (F-02b): griglia, aggiunta dal launcher (DnD), spostamento,
 * rimozione. La logica delle celle sta in logica.c (pura, provata senza schermo);
 * qui ci sono i widget e il collegamento al profilo del telefono. */
#include <string.h>

#include "interno.h"

struct PhDskIcona {
    PhDesktop *d;
    size_t indice;    /* posizione in profilo->icone */
    char app[PH_CFG_LUN_APP];
    GtkWidget *bottone;
    GtkWidget *tessera;
    GtkWidget *nome;
    PhDskCella cella; /* dove si mostra (puo' differire dal profilo) */
    bool visibile;
};

/* ---- Nome e tessera --------------------------------------------------------- */
static const char *nome_app(PhDesktop *d, const char *app, bool *noto)
{
    const PhSrvApp *a = ph_srv_app_cerca(d->srv, app);
    if (a != NULL && a->nome != NULL && a->nome[0] != '\0') {
        *noto = true;
        return a->nome;
    }
    /* Elenco non ancora arrivato: niente nome (non mostrare il pacchetto per un
     * istante); elenco arrivato e app sparita: il pacchetto (COMPITO.md). */
    size_t n = 0;
    ph_srv_app(d->srv, &n);
    *noto = n > 0;
    return n > 0 ? app : "";
}

static void tessera_aggiorna(PhDskIcona *ic)
{
    PhDesktop *d = ic->d;
    GtkWidget *vecchio = gtk_widget_get_first_child(ic->tessera);
    if (vecchio != NULL)
        gtk_box_remove(GTK_BOX(ic->tessera), vecchio);
    GdkTexture *t = ph_srv_icona(d->srv, ic->app);
    GtkWidget *nuovo;
    if (t != NULL) {
        /* Misura fissa vera: un GtkPicture prenderebbe quella naturale della texture (128). */
        nuovo = gtk_image_new_from_paintable(GDK_PAINTABLE(t));
        gtk_image_set_pixel_size(GTK_IMAGE(nuovo), 48);
        /* Un'icona vera ha la sua forma: l'ombra rettangolare della tessera le
         * sporgerebbe attorno come un alone quadrato. Resta solo al segnaposto. */
        gtk_widget_add_css_class(ic->tessera, "ph-desktop-tessera-vera");
    } else {
        gtk_widget_remove_css_class(ic->tessera, "ph-desktop-tessera-vera");
        bool noto;
        const char *nome = nome_app(d, ic->app, &noto);
        nuovo = ph_shell_segnaposto_nuovo(ic->app, nome[0] != '\0' ? nome : ic->app, 48, 12);
    }
    gtk_box_append(GTK_BOX(ic->tessera), nuovo);
}

static void nome_aggiorna(PhDskIcona *ic)
{
    bool noto;
    const char *nome = nome_app(ic->d, ic->app, &noto);
    gtk_label_set_text(GTK_LABEL(ic->nome), nome);
    /* L'etichetta accessibile c'e' comunque: il nome, o il pacchetto. */
    ph_shell_etichetta(ic->bottone, nome[0] != '\0' ? nome : ic->app, NULL);
}

/* ---- Posizioni -------------------------------------------------------------- */
static void icona_colloca(PhDskIcona *ic)
{
    gtk_widget_set_visible(ic->bottone, ic->visibile);
    if (!ic->visibile)
        return;
    int x, y;
    ph_dsk_cella_posizione(ic->cella, &x, &y);
    ph_shell_libero_sposta(ic->d->libero, ic->bottone, x, y, PH_DSK_ICONA_L, -1);
}

void ph_dsk_icone_disponi(PhDesktop *d)
{
    size_t n = d->icone->len;
    if (n == 0)
        return;
    PhCfgTelefono *p = ph_dsk_profilo(d);
    PhDskCella *volute = g_new0(PhDskCella, n);
    PhDskCella *fuori = g_new0(PhDskCella, n);
    bool *vis = g_new0(bool, n);
    for (size_t i = 0; i < n; i++) {
        PhDskIcona *ic = g_ptr_array_index(d->icone, i);
        if (p != NULL && ic->indice < p->n_icone) {
            volute[i].c = p->icone[ic->indice].colonna;
            volute[i].r = p->icone[ic->indice].riga;
        }
    }
    ph_dsk_disponi(d->griglia, volute, n, fuori, vis);
    for (size_t i = 0; i < n; i++) {
        PhDskIcona *ic = g_ptr_array_index(d->icone, i);
        ic->cella = fuori[i];
        ic->visibile = vis[i];
        icona_colloca(ic);
    }
    g_free(volute);
    g_free(fuori);
    g_free(vis);
}

/* Le celle occupate dalle altre icone (quella di nome `escludi` non conta). */
static PhDskCella *occupate(PhDesktop *d, const char *escludi, size_t *n)
{
    PhDskCella *v = g_new0(PhDskCella, d->icone->len + 1);
    size_t k = 0;
    for (size_t i = 0; i < d->icone->len; i++) {
        PhDskIcona *ic = g_ptr_array_index(d->icone, i);
        if (!ic->visibile || (escludi != NULL && strcmp(ic->app, escludi) == 0))
            continue;
        v[k++] = ic->cella;
    }
    *n = k;
    return v;
}

/* ---- Menu e apertura --------------------------------------------------------- */
/* Il PhDskIcona vive quanto il suo pulsante (dato dell'oggetto, liberato alla sua
 * finalizzazione): i gestori dei segnali del pulsante non possono trovarlo liberato.
 * I gestori del trascinamento NON usano `ic`: possono arrivare (drag-end,
 * drag-cancel) dopo che l'icona e' stata ricostruita, e lavorano solo su cio' che
 * ricevono (la sorgente) e sul pulsante che la sorgente ancora possiede. */
#define CHIAVE_APP "ph-desktop-app"
#define CHIAVE_IN_CORSO "ph-desktop-drag-in-corso"

static void icona_cliccata(GtkButton *b, gpointer dati)
{
    PhDskIcona *ic = dati;
    /* Spostare non apre l'app: finche' il trascinamento e' in corso (stato messo da
     * drag-begin e tolto da drag-end / drag-cancel) il clic non fa nulla. */
    if (g_object_get_data(G_OBJECT(b), CHIAVE_IN_CORSO) != NULL)
        return;
    ph_shell_apri_app(ic->d->sh, ic->app);
}

static void icona_menu_pulsante(GtkGestureClick *g, int n, double x, double y, gpointer dati)
{
    (void)n;
    PhDskIcona *ic = dati;
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
    double rx, ry;
    ph_dsk_in_radice(ic->d, ic->bottone, x, y, &rx, &ry);
    ph_dsk_menu_icona(ic->d, ic->app, rx, ry);
}

static void icona_menu_tasto(GtkWidget *w, gpointer dati)
{
    PhDskIcona *ic = dati;
    double rx, ry;
    ph_dsk_in_radice(ic->d, w, gtk_widget_get_width(w) / 2.0, gtk_widget_get_height(w) / 2.0, &rx,
                     &ry);
    ph_dsk_menu_icona(ic->d, ic->app, rx, ry);
}

/* ---- Trascinamento (sorgente) ------------------------------------------------- */
static GdkContentProvider *icona_prepara(GtkDragSource *s, double x, double y, gpointer dati)
{
    (void)dati;
    GtkWidget *b = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(s));
    const char *app = g_object_get_data(G_OBJECT(s), CHIAVE_APP);
    if (b == NULL || app == NULL)
        return NULL;
    /* Il fantasma si congela ORA: l'icona d'origine sbiadisce subito dopo (.35) e il
     * fantasma, vivo, sbiadirebbe con lei. */
    GdkPaintable *vivo = gtk_widget_paintable_new(b);
    GdkPaintable *fermo = gdk_paintable_get_current_image(vivo);
    g_object_unref(vivo);
    gtk_drag_source_set_icon(s, fermo, (int)x, (int)y);
    g_object_unref(fermo);
    return ph_shell_app_trascinata_contenuto(app, true);
}

static void icona_trascina_inizio(GtkDragSource *s, GdkDrag *drag, gpointer dati)
{
    (void)drag;
    (void)dati;
    GtkWidget *b = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(s));
    if (b != NULL) {
        g_object_set_data(G_OBJECT(b), CHIAVE_IN_CORSO, GINT_TO_POINTER(1));
        gtk_widget_set_opacity(b, 0.35); /* l'icona d'origine sbiadisce (mockup) */
    }
}

static void icona_trascina_fine_comune(GtkDragSource *s)
{
    GtkWidget *b = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(s));
    if (b == NULL)
        return; /* il pulsante non c'e' piu': nulla da ripristinare */
    gtk_widget_set_opacity(b, 1.0);
    g_object_set_data(G_OBJECT(b), CHIAVE_IN_CORSO, NULL);
}

static void icona_trascina_fine(GtkDragSource *s, GdkDrag *drag, gboolean cancella, gpointer dati)
{
    (void)drag;
    (void)cancella;
    (void)dati;
    icona_trascina_fine_comune(s);
}

static gboolean icona_trascina_annullato(GtkDragSource *s, GdkDrag *drag, GdkDragCancelReason r,
                                         gpointer dati)
{
    (void)drag;
    (void)r;
    (void)dati;
    icona_trascina_fine_comune(s);
    return FALSE; /* l'animazione di ritorno di GTK va bene */
}

/* ---- Creazione --------------------------------------------------------------- */
/* Il pulsante esce dal contenitore; la struttura si libera con lui (g_free sul dato). */
static void icona_libera(gpointer p)
{
    PhDskIcona *ic = p;
    if (ic->bottone != NULL && gtk_widget_get_parent(ic->bottone) != NULL)
        gtk_widget_unparent(ic->bottone);
}

static PhDskIcona *icona_nuova(PhDesktop *d, size_t indice, const char *app)
{
    PhDskIcona *ic = g_new0(PhDskIcona, 1);
    ic->d = d;
    ic->indice = indice;
    g_strlcpy(ic->app, app, sizeof ic->app);

    ic->bottone = gtk_button_new();
    g_object_set_data_full(G_OBJECT(ic->bottone), "ph-desktop-icona", ic, g_free);
    gtk_widget_add_css_class(ic->bottone, "ph-desktop-icona");
    gtk_widget_set_size_request(ic->bottone, PH_DSK_ICONA_L, -1);
    GtkWidget *colonna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_halign(colonna, GTK_ALIGN_CENTER);
    ic->tessera = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(ic->tessera, "ph-desktop-tessera");
    gtk_widget_set_size_request(ic->tessera, 48, 48);
    gtk_widget_set_halign(ic->tessera, GTK_ALIGN_CENTER);
    gtk_widget_set_overflow(ic->tessera, GTK_OVERFLOW_HIDDEN);
    ic->nome = gtk_label_new("");
    gtk_widget_add_css_class(ic->nome, "ph-desktop-nome");
    gtk_label_set_ellipsize(GTK_LABEL(ic->nome), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(ic->nome), 11);
    gtk_label_set_justify(GTK_LABEL(ic->nome), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(colonna), ic->tessera);
    gtk_box_append(GTK_BOX(colonna), ic->nome);
    gtk_button_set_child(GTK_BUTTON(ic->bottone), colonna);
    tessera_aggiorna(ic);
    nome_aggiorna(ic);

    g_signal_connect(ic->bottone, "clicked", G_CALLBACK(icona_cliccata), ic);

    GtkGesture *dx = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dx), GDK_BUTTON_SECONDARY);
    g_signal_connect(dx, "pressed", G_CALLBACK(icona_menu_pulsante), ic);
    gtk_widget_add_controller(ic->bottone, GTK_EVENT_CONTROLLER(dx));

    GtkDragSource *src = gtk_drag_source_new();
    g_object_set_data_full(G_OBJECT(src), CHIAVE_APP, g_strdup(app), g_free);
    gtk_drag_source_set_actions(src, GDK_ACTION_MOVE | GDK_ACTION_COPY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(src), GTK_PHASE_CAPTURE);
    g_signal_connect(src, "prepare", G_CALLBACK(icona_prepara), NULL);
    g_signal_connect(src, "drag-begin", G_CALLBACK(icona_trascina_inizio), NULL);
    g_signal_connect(src, "drag-end", G_CALLBACK(icona_trascina_fine), NULL);
    g_signal_connect(src, "drag-cancel", G_CALLBACK(icona_trascina_annullato), NULL);
    gtk_widget_add_controller(ic->bottone, GTK_EVENT_CONTROLLER(src));

    ph_dsk_tasto_menu_aggancia(ic->bottone, G_CALLBACK(icona_menu_tasto), ic);

    ph_shell_libero_metti(d->libero, ic->bottone, 0, 0, PH_DSK_ICONA_L, -1);
    return ic;
}

void ph_dsk_icone_ricostruisci(PhDesktop *d)
{
    g_ptr_array_set_size(d->icone, 0); /* libera i widget (icona_libera) */
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p != NULL)
        for (size_t i = 0; i < p->n_icone && i < PH_CFG_MAX_ICONE; i++)
            g_ptr_array_add(d->icone, icona_nuova(d, i, p->icone[i].app));
    ph_dsk_icone_disponi(d);
    ph_shell_emetti(d->sh, PH_SHELL_EV_DESKTOP);
}

void ph_dsk_icone_libera(PhDesktop *d)
{
    /* I widget sono gia' stati distrutti con la radice: si libera solo la memoria. */
    for (size_t i = 0; i < d->icone->len; i++) {
        PhDskIcona *ic = g_ptr_array_index(d->icone, i);
        ic->bottone = NULL; /* i widget sono gia' staccati: la struttura la libera il pulsante */
    }
    g_ptr_array_set_size(d->icone, 0);
}

/* ---- Aggiornamenti dai servizi ------------------------------------------------- */
void ph_dsk_icone_icona_arrivata(PhDesktop *d, const char *pacchetto)
{
    for (size_t i = 0; i < d->icone->len; i++) {
        PhDskIcona *ic = g_ptr_array_index(d->icone, i);
        if (pacchetto == NULL || strcmp(ic->app, pacchetto) == 0)
            tessera_aggiorna(ic);
    }
}

void ph_dsk_icone_nomi_aggiornati(PhDesktop *d)
{
    for (size_t i = 0; i < d->icone->len; i++) {
        PhDskIcona *ic = g_ptr_array_index(d->icone, i);
        nome_aggiorna(ic);
        if (ph_srv_icona(d->srv, ic->app) == NULL)
            tessera_aggiorna(ic); /* il segnaposto ha le iniziali del nome */
    }
}

/* ---- Gestore del desktop (launcher -> icone) ------------------------------------ */
bool ph_dsk_icone_ha(PhDesktop *d, const char *app)
{
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p == NULL || app == NULL)
        return false;
    for (size_t i = 0; i < p->n_icone; i++)
        if (strcmp(p->icone[i].app, app) == 0)
            return true;
    return false;
}

static const char *nome_per_avviso(PhDesktop *d, const char *app)
{
    const PhSrvApp *a = ph_srv_app_cerca(d->srv, app);
    return a != NULL && a->nome != NULL ? a->nome : app;
}

/* La ricostruzione delle icone distrugge i widget: se parte da dentro un
 * rilascio di trascinamento (la sorgente e' una di quelle icone) si rimanda al
 * giro successivo del ciclo, con ph_shell_consegna. */
typedef struct {
    PhDesktop *d;
    char *nome;
    bool nuova;
} Dopo;

static void dopo_esegui(void *p)
{
    Dopo *x = p;
    if (!x->d->vivo)
        return;
    ph_dsk_icone_ricostruisci(x->d);
    if (x->nuova)
        ph_dsk_avviso(x->d, ph_t("Aggiunta al desktop"), x->nome);
}

static void dopo_libera(void *p)
{
    Dopo *x = p;
    ph_dsk_unref(x->d);
    g_free(x->nome);
    g_free(x);
}

/* Mette l'app (nuova o gia' presente) nella cella data e salva. */
static void icona_in_cella(PhDesktop *d, const char *app, PhDskCella c)
{
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p == NULL)
        return;
    size_t i;
    for (i = 0; i < p->n_icone; i++)
        if (strcmp(p->icone[i].app, app) == 0)
            break;
    bool nuova = i == p->n_icone;
    if (nuova) {
        if (p->n_icone >= PH_CFG_MAX_ICONE)
            return;
        memset(&p->icone[i], 0, sizeof p->icone[i]);
        g_strlcpy(p->icone[i].app, app, sizeof p->icone[i].app);
        p->n_icone++;
    }
    p->icone[i].colonna = c.c;
    p->icone[i].riga = c.r;
    ph_dsk_salva(d);
    Dopo *x = g_new0(Dopo, 1);
    x->d = ph_dsk_ref(d);
    x->nome = g_strdup(nome_per_avviso(d, app));
    x->nuova = nuova;
    ph_shell_consegna(dopo_esegui, x, dopo_libera);
}

void ph_dsk_icone_aggiungi(PhDesktop *d, const char *app)
{
    if (app == NULL || app[0] == '\0' || ph_dsk_icone_ha(d, app))
        return;
    size_t n;
    PhDskCella *occ = occupate(d, NULL, &n);
    PhDskCella c;
    bool ok = ph_dsk_cella_libera(d->griglia, occ, n, (PhDskCella){0, 0}, &c);
    g_free(occ);
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (!ok || p == NULL || p->n_icone >= PH_CFG_MAX_ICONE) {
        ph_dsk_avviso(d, ph_t("Desktop pieno"), ph_t("Rimuovi un'icona per fare spazio"));
        return;
    }
    icona_in_cella(d, app, c);
}

void ph_dsk_icone_rimuovi(PhDesktop *d, const char *app)
{
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p == NULL || app == NULL)
        return;
    for (size_t i = 0; i < p->n_icone; i++) {
        if (strcmp(p->icone[i].app, app) != 0)
            continue;
        memmove(&p->icone[i], &p->icone[i + 1], (p->n_icone - i - 1) * sizeof p->icone[0]);
        p->n_icone--;
        ph_dsk_salva(d);
        ph_dsk_icone_ricostruisci(d);
        return;
    }
}

/* ---- Bersaglio del trascinamento (sul desktop) -------------------------------------- */
static const PhShellAppTrascinata *valore_app(GtkDropTarget *t)
{
    const GValue *v = gtk_drop_target_get_value(t);
    if (v == NULL || !G_VALUE_HOLDS(v, PH_TIPO_APP_TRASCINATA))
        return NULL;
    return g_value_get_boxed(v);
}

static GdkDragAction azione_preferita(GtkDropTarget *t)
{
    GdkDrop *drop = gtk_drop_target_get_current_drop(t);
    GdkDragAction a = drop != NULL ? gdk_drop_get_actions(drop) : GDK_ACTION_COPY;
    return (a & GDK_ACTION_MOVE) ? GDK_ACTION_MOVE : GDK_ACTION_COPY;
}

/* La cella dove finirebbe l'icona lasciata nel punto (x, y); false se annullato
 * (sopra la barra, o nessuna cella libera). */
static bool cella_di_rilascio(PhDesktop *d, const char *app, double x, double y, PhDskCella *c)
{
    if (y > d->area_a - PH_DSK_BARRA)
        return false; /* rilascio sopra la barra: annullato (mockup onDeskUp) */
    size_t n;
    PhDskCella *occ = occupate(d, app, &n);
    PhDskCella voluta = ph_dsk_cella_al_punto(d->griglia, x, y);
    bool ok = ph_dsk_cella_libera(d->griglia, occ, n, voluta, c);
    g_free(occ);
    return ok;
}

static void tratteggio_nascondi(PhDesktop *d)
{
    gtk_widget_set_visible(d->tratteggio, FALSE);
}

static GdkDragAction bersaglio_movimento(GtkDropTarget *t, double x, double y, gpointer dati)
{
    PhDesktop *d = dati;
    const PhShellAppTrascinata *a = valore_app(t);
    PhDskCella c;
    if (!cella_di_rilascio(d, a != NULL ? a->pacchetto : NULL, x, y, &c)) {
        tratteggio_nascondi(d);
        return 0;
    }
    int px, py;
    ph_dsk_cella_posizione(c, &px, &py);
    ph_shell_libero_sposta(d->libero, d->tratteggio, px, py, PH_DSK_ICONA_L, PH_DSK_ICONA_A);
    gtk_widget_set_visible(d->tratteggio, TRUE);
    return azione_preferita(t);
}

static void bersaglio_uscita(GtkDropTarget *t, gpointer dati)
{
    (void)t;
    tratteggio_nascondi(dati);
}

static gboolean bersaglio_rilascio(GtkDropTarget *t, const GValue *v, double x, double y,
                                   gpointer dati)
{
    (void)t;
    PhDesktop *d = dati;
    tratteggio_nascondi(d);
    if (!G_VALUE_HOLDS(v, PH_TIPO_APP_TRASCINATA))
        return FALSE;
    const PhShellAppTrascinata *a = g_value_get_boxed(v);
    if (a == NULL || a->pacchetto[0] == '\0')
        return FALSE;
    char app[PH_CFG_LUN_APP];
    g_strlcpy(app, a->pacchetto, sizeof app); /* il valore non vale oltre la chiamata */
    PhDskCella c;
    if (!cella_di_rilascio(d, app, x, y, &c))
        return FALSE;
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p == NULL)
        return FALSE;
    if (!ph_dsk_icone_ha(d, app) && p->n_icone >= PH_CFG_MAX_ICONE) {
        ph_dsk_avviso(d, ph_t("Desktop pieno"), ph_t("Rimuovi un'icona per fare spazio"));
        return FALSE;
    }
    icona_in_cella(d, app, c);
    return TRUE;
}

void ph_dsk_icone_bersaglio_aggancia(PhDesktop *d, GtkWidget *w)
{
    GtkDropTarget *t = gtk_drop_target_new(PH_TIPO_APP_TRASCINATA, GDK_ACTION_MOVE | GDK_ACTION_COPY);
    gtk_drop_target_set_preload(t, TRUE); /* il valore serve gia' durante il passaggio */
    g_signal_connect(t, "motion", G_CALLBACK(bersaglio_movimento), d);
    g_signal_connect(t, "leave", G_CALLBACK(bersaglio_uscita), d);
    g_signal_connect(t, "drop", G_CALLBACK(bersaglio_rilascio), d);
    gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(t));
}

/* g_ptr_array_set_size libera gli elementi con icona_libera: la funzione e'
 * collegata alla creazione dell'array in desktop.c. */
void ph_dsk_icone_libera_elemento(gpointer p)
{
    icona_libera(p);
}
