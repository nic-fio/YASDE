/* Pezzo shell-desktop (F-02): il desktop: sfondo, icone, widget, menu.
 * Questo file e' il nucleo: installazione, stato condiviso, sfondo (caricamento
 * e salvataggio), menu contestuali, gestore per il launcher e collegamento ai
 * servizi. Icone, widget e dialoghi sono nei loro file (icone.c, widget.c,
 * dialoghi.c); la logica pura e' in logica.c.
 *
 * Thread: tutto qui gira sul thread GTK. La decodifica e la riduzione delle
 * immagini (immagine.c) girano in un GTask su un thread di lavoro, che non tocca
 * ne' widget ne' servizi: il risultato torna sul thread GTK. */
#include <stdio.h>
#include <string.h>

#include "interno.h"

/* Generata da glib-compile-resources (--manual-register): il CSS del pezzo. */
void ph_risorse_desktop_register_resource(void);

/* ---- Vita ------------------------------------------------------------------------- */
PhDesktop *ph_dsk_ref(PhDesktop *d)
{
    d->rif++;
    return d;
}

void ph_dsk_unref(PhDesktop *d)
{
    if (--d->rif > 0)
        return;
    g_clear_object(&d->annulla);
    g_clear_object(&d->immagine);
    g_ptr_array_unref(d->icone);
    g_ptr_array_unref(d->widget);
    g_free(d);
}

PhCfgTelefono *ph_dsk_profilo(PhDesktop *d)
{
    return ph_srv_profilo(d->srv);
}

void ph_dsk_salva(PhDesktop *d)
{
    d->salvataggi++;
    ph_srv_profilo_salva(d->srv);
}

bool ph_dsk_widget_visibili(PhDesktop *d)
{
    PhCfgPreferenze *pr = ph_srv_preferenze(d->srv);
    return pr == NULL || !pr->widget_nascosti;
}

void ph_dsk_avviso(PhDesktop *d, const char *titolo, const char *testo)
{
    ph_shell_avviso(d->sh, titolo, testo);
}

void ph_dsk_in_radice(PhDesktop *d, GtkWidget *w, double x, double y, double *rx, double *ry)
{
    graphene_point_t in = GRAPHENE_POINT_INIT((float)x, (float)y), out;
    if (!gtk_widget_compute_point(w, ph_shell_radice(d->sh), &in, &out))
        out = in;
    *rx = out.x;
    *ry = out.y;
}

void ph_dsk_misura_fisica(PhDesktop *d, uint32_t *W, uint32_t *H)
{
    int f = gtk_widget_get_scale_factor(ph_shell_radice(d->sh));
    if (f < 1)
        f = 1;
    /* Prima dell'assegnazione dell'area (0) una misura ragionevole. */
    int l = d->area_l > 0 ? d->area_l : 1280, a = d->area_a > 0 ? d->area_a : 800;
    *W = (uint32_t)l * (uint32_t)f;
    *H = (uint32_t)a * (uint32_t)f;
}

/* ---- Sfondo: caricamento dalla cache (thread) --------------------------------------- */
typedef struct {
    PhDesktop *d;
    unsigned gen;
    char nome[PH_CFG_LUN_FILE_CACHE];
} Carico;

static void carico_thread(GTask *t, gpointer sorgente, gpointer dati, GCancellable *c)
{
    (void)sorgente;
    (void)c;
    Carico *k = dati;
    GdkTexture *tx = ph_dsk_immagine_da_cache(k->nome);
    g_task_return_pointer(t, tx, tx != NULL ? g_object_unref : NULL);
}

static void sfondo_inoltra(PhDesktop *d)
{
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p == NULL)
        return;
    ph_dsk_sfondo_imposta(d->sfondo, &p->sfondo, d->immagine, 0);
    ph_dsk_dialoghi_sfondo_cambiato(d);
}

static void carico_fatto(GObject *sorgente, GAsyncResult *r, gpointer dati)
{
    (void)sorgente;
    Carico *k = dati;
    PhDesktop *d = k->d;
    GdkTexture *tx = g_task_propagate_pointer(G_TASK(r), NULL);
    if (d->vivo && k->gen == d->immagine_gen) {
        g_clear_object(&d->immagine);
        d->immagine = tx;
        tx = NULL;
        sfondo_inoltra(d);
    }
    if (tx != NULL)
        g_object_unref(tx);
    ph_dsk_unref(d);
    g_free(k);
}

void ph_dsk_sfondo_applica(PhDesktop *d)
{
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p == NULL || !d->vivo)
        return;
    const PhCfgSfondo *s = &p->sfondo;
    if (s->tipo == PH_CFG_SFONDO_IMMAGINE && s->immagine[0] != '\0') {
        if (strcmp(s->immagine, d->immagine_nome) != 0) {
            g_strlcpy(d->immagine_nome, s->immagine, sizeof d->immagine_nome);
            Carico *k = g_new0(Carico, 1);
            k->d = ph_dsk_ref(d);
            k->gen = ++d->immagine_gen;
            g_strlcpy(k->nome, s->immagine, sizeof k->nome);
            /* La texture vecchia resta a schermo finche' arriva la nuova. */
            GTask *t = g_task_new(NULL, d->annulla, carico_fatto, k);
            g_task_set_task_data(t, k, NULL);
            g_task_run_in_thread(t, carico_thread);
            g_object_unref(t);
        }
    } else if (d->immagine != NULL || d->immagine_nome[0] != '\0') {
        d->immagine_gen++; /* un caricamento in corso non serve piu' */
        g_clear_object(&d->immagine);
        d->immagine_nome[0] = '\0';
    }
    sfondo_inoltra(d);
}

void ph_dsk_sfondo_scegli(PhDesktop *d, const PhCfgSfondo *nuovo)
{
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p == NULL || nuovo == NULL)
        return;
    p->sfondo = *nuovo;
    ph_dsk_salva(d);
    ph_dsk_sfondo_applica(d);
}

/* ---- Sfondo: da un'immagine della galleria del telefono (thread) ------------------------- */
typedef struct {
    PhDesktop *d;
    unsigned gen;
    GBytes *byte;
    uint32_t W, H;
    char precedente[PH_CFG_LUN_FILE_CACHE];
    char nuovo[PH_CFG_LUN_FILE_CACHE];
    PhCfgAdattamento adatt;
    PhErrore e;
    bool ok;
} Prepara;

static void prepara_thread(GTask *t, gpointer sorgente, gpointer dati, GCancellable *c)
{
    (void)sorgente;
    (void)c;
    Prepara *k = dati;
    k->ok = ph_dsk_immagine_prepara(k->byte, k->W, k->H, k->precedente, k->nuovo, sizeof k->nuovo,
                                    &k->e) == 0;
    g_task_return_boolean(t, TRUE);
}

static void prepara_fatto(GObject *sorgente, GAsyncResult *r, gpointer dati)
{
    (void)sorgente;
    Prepara *k = dati;
    PhDesktop *d = k->d;
    gboolean fatto = g_task_propagate_boolean(G_TASK(r), NULL);
    PhCfgTelefono *p = d->vivo && fatto ? ph_dsk_profilo(d) : NULL;
    if (k->ok && (!fatto || p == NULL || k->gen != d->prep_gen)) {
        /* Annullata, superata o shell chiusa: il PNG appena scritto non serve a nessuno. */
        ph_dsk_immagine_togli(k->nuovo);
    } else if (k->ok) {
        /* Il file vecchio NON si cancella qui: il profilo su disco potrebbe ancora
         * nominarlo (salvataggio differito, senza conferma). Lo ripulisce config
         * all'avvio, togliendo i file che nessun profilo nomina. */
        p->sfondo.tipo = PH_CFG_SFONDO_IMMAGINE;
        g_strlcpy(p->sfondo.immagine, k->nuovo, sizeof p->sfondo.immagine);
        p->sfondo.adattamento = k->adatt;
        ph_dsk_salva(d);
        ph_dsk_sfondo_applica(d);
    } else if (fatto && d->vivo && k->gen == d->prep_gen) {
        /* L'utente deve saperlo (immagine oltre i tetti o illeggibile). Su stderr
         * niente messaggi d'errore: contengono percorsi con il nome dell'utente. */
        fprintf(stderr, "desktop: sfondo non cambiato (immagine non utilizzabile)\n");
        ph_dsk_avviso(d, ph_t("Sfondo non cambiato"),
                      ph_t("L'immagine è troppo grande o non è leggibile."));
    }
    g_bytes_unref(k->byte);
    ph_dsk_unref(d);
    g_free(k);
}

void ph_dsk_sfondo_da_byte(PhDesktop *d, GBytes *byte, PhCfgAdattamento adatt)
{
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p == NULL || byte == NULL || !d->vivo)
        return;
    Prepara *k = g_new0(Prepara, 1);
    k->d = ph_dsk_ref(d);
    k->gen = ++d->prep_gen;
    k->byte = g_bytes_ref(byte);
    ph_dsk_misura_fisica(d, &k->W, &k->H);
    g_strlcpy(k->precedente, p->sfondo.immagine, sizeof k->precedente);
    k->adatt = adatt;
    GTask *t = g_task_new(NULL, d->annulla, prepara_fatto, k);
    g_task_set_task_data(t, k, NULL);
    g_task_run_in_thread(t, prepara_thread);
    g_object_unref(t);
}

/* ---- Menu contestuali ------------------------------------------------------------------------- */
typedef struct {
    PhDesktop *d;
    char app[PH_CFG_LUN_APP];
    unsigned id;
    unsigned stile, opzione;
} Azione;

static void azione_apri(GtkButton *b, gpointer dati)
{
    (void)b;
    Azione *a = dati;
    ph_shell_popup_chiudi(a->d->sh);
    ph_shell_apri_app(a->d->sh, a->app);
}

static void azione_rimuovi_icona(GtkButton *b, gpointer dati)
{
    (void)b;
    Azione *a = dati;
    ph_shell_popup_chiudi(a->d->sh);
    ph_dsk_icone_rimuovi(a->d, a->app);
}

static void azione_dialogo_sfondo(GtkButton *b, gpointer dati)
{
    (void)b;
    Azione *a = dati;
    ph_shell_popup_chiudi(a->d->sh);
    ph_dsk_dialogo_sfondo_apri(a->d);
}

static void azione_dialogo_widget(GtkButton *b, gpointer dati)
{
    (void)b;
    Azione *a = dati;
    ph_shell_popup_chiudi(a->d->sh);
    ph_dsk_dialogo_widget_apri(a->d);
}

static void azione_stile(GtkButton *b, gpointer dati)
{
    (void)b;
    Azione *a = dati;
    ph_shell_popup_chiudi(a->d->sh);
    unsigned st, op;
    PhCfgWidgetTipo tipo;
    if (ph_dsk_widget_stile_di(a->d, a->id, &tipo, &st, &op))
        ph_dsk_widget_stile_imposta(a->d, a->id, a->stile, op);
}

static void azione_citta(GtkButton *b, gpointer dati)
{
    (void)b;
    Azione *a = dati;
    ph_shell_popup_chiudi(a->d->sh);
    unsigned st, op;
    PhCfgWidgetTipo tipo;
    if (ph_dsk_widget_stile_di(a->d, a->id, &tipo, &st, &op))
        ph_dsk_widget_stile_imposta(a->d, a->id, st, a->opzione);
}

static void azione_luogo(GtkButton *b, gpointer dati)
{
    (void)b;
    Azione *a = dati;
    ph_shell_popup_chiudi(a->d->sh);
    ph_dsk_luogo_apri(a->d);
}

static void azione_rimuovi_widget(GtkButton *b, gpointer dati)
{
    (void)b;
    Azione *a = dati;
    ph_shell_popup_chiudi(a->d->sh);
    ph_dsk_widget_rimuovi(a->d, a->id);
}

static void azione_libera(gpointer dati, GClosure *c)
{
    (void)c;
    g_free(dati);
}

static GtkWidget *menu_voce(PhDesktop *d, const char *testo, int premuto, GCallback cb,
                            const char *app, unsigned id)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "ph-riga");
    GtkWidget *l = gtk_label_new(testo);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_button_set_child(GTK_BUTTON(b), l);
    gtk_widget_set_hexpand(l, TRUE);
    if (premuto >= 0)
        ph_shell_premuto(b, premuto != 0);
    Azione *a = g_new0(Azione, 1);
    a->d = d;
    a->id = id;
    if (app != NULL)
        g_strlcpy(a->app, app, sizeof a->app);
    g_object_set_data(G_OBJECT(b), "ph-desktop-azione", a); /* vive quanto il pulsante (libera il segnale) */
    g_signal_connect_data(b, "clicked", cb, a, azione_libera, 0);
    gtk_box_append(GTK_BOX(d->menu_voci), b);
    return b;
}

static void menu_svuota(PhDesktop *d)
{
    GtkWidget *f;
    while ((f = gtk_widget_get_first_child(d->menu_voci)) != NULL)
        gtk_box_remove(GTK_BOX(d->menu_voci), f);
}

static void menu_titolo(PhDesktop *d, const char *testo)
{
    /* Il maiuscolo del mockup (11 px, .06em) lo da' la classe ph-sezione (base.css). */
    GtkWidget *l = gtk_label_new(testo);
    gtk_widget_add_css_class(l, "ph-sezione");
    gtk_widget_add_css_class(l, "ph-desktop-menu-titolo");
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_box_append(GTK_BOX(d->menu_voci), l);
}

static void menu_chiuso(PhShell *sh, PhShellPopup tipo, GtkWidget *w, void *utente)
{
    (void)sh;
    (void)tipo;
    (void)w;
    (void)utente;
    /* Il gestore dei popup lo ha gia' nascosto; le voci si rifanno alla prossima
     * apertura. */
}

static void menu_mostra(PhDesktop *d, double x, double y, int lm, int am)
{
    int larg = d->area_l, alt = d->area_a;
    int ox, oy;
    /* Un menu piu' alto della finestra scorre (voci dello stile dell'orologio). */
    int massimo = alt > 64 ? alt - 16 : 48;
    if (am > massimo)
        am = massimo;
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(d->menu_scorri), massimo - 14);
    ph_dsk_menu_posizione(x, y, larg, alt, lm, am, &ox, &oy);
    gtk_widget_set_margin_start(d->menu, ox);
    gtk_widget_set_margin_top(d->menu, oy);
    ph_shell_popup_apri(d->sh, PH_POPUP_MENU, d->menu, menu_chiuso, d);
}

void ph_dsk_menu_desktop(PhDesktop *d, double x, double y)
{
    menu_svuota(d);
    menu_voce(d, ph_t("Cambia sfondo…"), -1, G_CALLBACK(azione_dialogo_sfondo), NULL, 0);
    menu_voce(d, ph_t("Aggiungi widget…"), -1, G_CALLBACK(azione_dialogo_widget), NULL, 0);
    menu_mostra(d, x, y, PH_DSK_MENU_DESKTOP_L, PH_DSK_MENU_DESKTOP_A);
}

void ph_dsk_menu_icona(PhDesktop *d, const char *app, double x, double y)
{
    menu_svuota(d);
    menu_voce(d, ph_t("Apri"), -1, G_CALLBACK(azione_apri), app, 0);
    menu_voce(d, ph_t("Rimuovi dal desktop"), -1, G_CALLBACK(azione_rimuovi_icona), app, 0);
    menu_mostra(d, x, y, PH_DSK_MENU_ICONA_L, PH_DSK_MENU_ICONA_A);
}

/* «Nome (attuale)» per lo stile scelto. */
static const char *etichetta_attuale(const char *nome, bool attuale, char *buf, size_t cap)
{
    if (!attuale)
        return nome;
    ph_lingua_componi(buf, cap, ph_t("{dimensione} (attuale)"), "dimensione", nome, (const char *)NULL);
    return buf;
}

void ph_dsk_menu_widget(PhDesktop *d, unsigned id, double x, double y)
{
    unsigned stile, citta;
    PhCfgWidgetTipo tipo;
    /* Un id che non c'e' (widget rimosso nel frattempo): nessun menu. */
    if (!ph_dsk_widget_stile_di(d, id, &tipo, &stile, &citta))
        return;
    menu_svuota(d);
    int altezza = PH_DSK_MENU_WIDGET_A;
    char testo_att[160];
    if (ph_dsk_stili_n(tipo) > 1) {
        /* Stili del widget (orologio, calendario del mese) e, per il doppio fuso, la citta'. */
        menu_titolo(d, ph_t("Stile"));
        for (unsigned i = 0; i < ph_dsk_stili_n(tipo); i++) {
            GtkWidget *b = menu_voce(d, etichetta_attuale(ph_dsk_stile_nome_di(tipo, i), i == stile, testo_att, sizeof testo_att),
                                     i == stile ? 1 : 0, G_CALLBACK(azione_stile), NULL, id);
            Azione *a = g_object_get_data(G_OBJECT(b), "ph-desktop-azione");
            if (a != NULL)
                a->stile = i;
        }
        altezza += 26 + (int)ph_dsk_stili_n(tipo) * 34;
        if (tipo == PH_CFG_WIDGET_OROLOGIO && stile == PH_DSK_OROLOGIO_DOPPIO_FUSO) {
            menu_titolo(d, ph_t("Città"));
            for (unsigned i = 0; i < ph_dsk_citta_n(); i++) {
                GtkWidget *b = menu_voce(d, etichetta_attuale(ph_dsk_citta_nome(i), i == citta, testo_att, sizeof testo_att),
                                         i == citta ? 1 : 0, G_CALLBACK(azione_citta), NULL, id);
                Azione *a = g_object_get_data(G_OBJECT(b), "ph-desktop-azione");
                if (a != NULL)
                    a->opzione = i;
            }
            altezza += 26 + (int)ph_dsk_citta_n() * 34;
        }
    }
    if (tipo == PH_CFG_WIDGET_METEO) {
        menu_voce(d, ph_t("Località…"), -1, G_CALLBACK(azione_luogo), NULL, id);
        altezza += 34;
    }
    /* Un separatore, poi «Rimuovi widget» in fondo (come nel mockup). */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_add_css_class(sep, "ph-desktop-menu-sep");
    gtk_box_append(GTK_BOX(d->menu_voci), sep);
    altezza += 9;
    menu_voce(d, ph_t("Rimuovi widget"), -1, G_CALLBACK(azione_rimuovi_widget), NULL, id);
    menu_mostra(d, x, y, PH_DSK_MENU_WIDGET_L, altezza);
}

/* Tasto "Menu" o Maiusc+F10 su un controllo: stesso menu del clic destro. */
typedef struct {
    void (*fn)(GtkWidget *, gpointer);
    gpointer dati;
} TastoMenu;

static gboolean tasto_menu_premuto(GtkEventControllerKey *k, guint tasto, guint codice,
                                   GdkModifierType stato, gpointer dati)
{
    (void)codice;
    TastoMenu *t = dati;
    if (tasto == GDK_KEY_Menu || (tasto == GDK_KEY_F10 && (stato & GDK_SHIFT_MASK))) {
        t->fn(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(k)), t->dati);
        return TRUE;
    }
    return FALSE;
}

static void tasto_menu_libera(gpointer dati, GClosure *c)
{
    (void)c;
    g_free(dati);
}

void ph_dsk_tasto_menu_aggancia(GtkWidget *w, GCallback aperto, gpointer dati)
{
    TastoMenu *t = g_new0(TastoMenu, 1);
    t->fn = (void (*)(GtkWidget *, gpointer))aperto;
    t->dati = dati;
    GtkEventController *k = gtk_event_controller_key_new();
    g_signal_connect_data(k, "key-pressed", G_CALLBACK(tasto_menu_premuto), t, tasto_menu_libera, 0);
    gtk_widget_add_controller(w, k);
}

/* Clic destro sullo sfondo vuoto. */
static void sfondo_clic_destro(GtkGestureClick *g, int n, double x, double y, gpointer dati)
{
    (void)n;
    PhDesktop *d = dati;
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
    double rx, ry;
    ph_dsk_in_radice(d, d->sfondo, x, y, &rx, &ry);
    ph_dsk_menu_desktop(d, rx, ry);
}

/* ---- Gestore per il launcher --------------------------------------------------------------------- */
static bool gestore_sul_desktop(void *io, const char *pacchetto)
{
    return ph_dsk_icone_ha(io, pacchetto);
}

static void gestore_aggiungi(void *io, const char *pacchetto)
{
    ph_dsk_icone_aggiungi(io, pacchetto);
}

static void gestore_rimuovi(void *io, const char *pacchetto)
{
    ph_dsk_icone_rimuovi(io, pacchetto);
}

/* ---- Servizi e avvisi della shell ---------------------------------------------------------------- */
static void area_aggiorna(PhDesktop *d)
{
    ph_shell_area(d->sh, &d->area_l, &d->area_a);
    d->griglia = ph_dsk_griglia(d->area_l, d->area_a, ph_dsk_widget_visibili(d));
}

static void avviso_area(PhShell *sh, PhShellEvento ev, void *utente)
{
    (void)sh;
    (void)ev;
    PhDesktop *d = utente;
    area_aggiorna(d);
    ph_dsk_icone_disponi(d);
    ph_dsk_widget_riposiziona(d);
    ph_dsk_dialoghi_area(d);
    ph_dsk_luogo_area(d);
}

static void avviso_host(PhShell *sh, PhShellEvento ev, void *utente)
{
    (void)sh;
    (void)ev;
    ph_dsk_widget_timer_riavvia(utente); /* tornata visibile: i quadranti si rimettono in ora */
}

/* Dopo una sospensione del PC o un salto dell'orologio di sistema il timer (monotonico)
 * puo' restare indietro: quando la finestra torna attiva si riallinea subito, senza
 * aggiungere risvegli. */
static void finestra_attiva(GObject *f, GParamSpec *ps, gpointer dati)
{
    (void)ps;
    if (gtk_window_is_active(GTK_WINDOW(f)))
        ph_dsk_widget_timer_riavvia(dati);
}

static void radice_con_finestra(GObject *r, GParamSpec *ps, gpointer dati)
{
    (void)ps;
    PhDesktop *d = dati;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(r));
    GtkWindow *vecchia = g_weak_ref_get(&d->finestra);
    if (vecchia != NULL) {
        if (GTK_ROOT(vecchia) == root) {
            g_object_unref(vecchia);
            return;
        }
        g_signal_handler_disconnect(vecchia, d->id_attiva);
        g_object_unref(vecchia);
        g_weak_ref_set(&d->finestra, NULL);
    }
    if (GTK_IS_WINDOW(root)) {
        g_weak_ref_set(&d->finestra, root);
        d->id_attiva = g_signal_connect(root, "notify::is-active", G_CALLBACK(finestra_attiva), d);
    }
}

static void avviso_finestre(PhShell *sh, PhShellEvento ev, void *utente)
{
    (void)sh;
    (void)ev;
    ph_dsk_widget_evento(utente, PH_DSK_EV_FINESTRE); /* App frequenti: aperture contate */
}

static void avviso_nascoste(PhShell *sh, PhShellEvento ev, void *utente)
{
    (void)sh;
    (void)ev;
    ph_dsk_widget_aggiorna_tutti(utente);
}

static void avviso_servizi(PhServizi *s, PhSrvEvento ev, const char *argomento, void *utente)
{
    (void)s;
    PhDesktop *d = utente;
    switch (ev) {
    case PH_SRV_EV_PROFILO:
        /* Un altro telefono: i puntatori vecchi non valgono, si rilegge tutto. Una
         * lettura o una preparazione in corso era per l'altro telefono: si lascia. */
        d->prep_gen++;
        ph_dsk_dialoghi_annulla_scelta(d);
        ph_dsk_luogo_comunica(d); /* la citta' del meteo del nuovo profilo */
        ph_dsk_widget_dati_azzera(d);
        area_aggiorna(d);
        ph_dsk_sfondo_applica(d);
        ph_dsk_icone_ricostruisci(d);
        ph_dsk_widget_ricostruisci(d);
        break;
    case PH_SRV_EV_ICONA:
        ph_dsk_icone_icona_arrivata(d, argomento);
        ph_dsk_widget_evento(d, (int)ev); /* App frequenti */
        break;
    case PH_SRV_EV_APP:
        ph_dsk_icone_nomi_aggiornati(d);
        ph_dsk_widget_evento(d, (int)ev);
        break;
    case PH_SRV_EV_COLLEGAMENTO: {
        PhSrvCollegamento c;
        ph_srv_collegamento(d->srv, &c);
        if (c.stato == PH_SRV_COLLEGATO && d->stato_prec != PH_SRV_COLLEGATO) {
            ph_dsk_widget_memoria_chiedi(d); /* al collegamento si rileggono i dati dei widget */
            ph_dsk_widget_calendario_chiedi(d);
            ph_dsk_widget_richiedi_nuovi(d);
        }
        d->stato_prec = c.stato;
        ph_dsk_widget_aggiorna_tutti(d);
        break;
    }
    case PH_SRV_EV_TELEFONO:
    case PH_SRV_EV_NOTIFICHE:
        ph_dsk_widget_aggiorna_tutti(d);
        break;
    case PH_SRV_EV_CONTROLLI:
    case PH_SRV_EV_TRASFERIMENTI:
    case PH_SRV_EV_METEO:
        ph_dsk_widget_evento(d, (int)ev);
        break;
    default:
        break;
    }
}

static const PhSrvEvento EVENTI_ASCOLTATI[] = {PH_SRV_EV_PROFILO,  PH_SRV_EV_ICONA,
                                               PH_SRV_EV_APP,      PH_SRV_EV_COLLEGAMENTO,
                                               PH_SRV_EV_TELEFONO, PH_SRV_EV_NOTIFICHE,
                                               PH_SRV_EV_CONTROLLI, PH_SRV_EV_TRASFERIMENTI,
                                               PH_SRV_EV_METEO};

/* La radice e' distrutta (da ph_shell_libera o con la finestra): i widget stanno
 * morendo con lei. Si staccano gli ascolti, si annullano le richieste in volo e
 * si lascia la memoria alle operazioni asincrone ancora in corso (rif). */
static void radice_distrutta(GtkWidget *w, gpointer dati)
{
    PhDesktop *d = dati;
    if (!d->vivo)
        return; /* idempotente: una seconda chiamata non fa nulla */
    /* Note rapide: il buffer condiviso si stacca da noi. */
    ph_dsk_note_chiudi(d);
    d->vivo = false;
    /* Prima si smette di ascoltare (le *_smetti sono sicure anche a shell liberata)... */
    for (size_t i = 0; i < G_N_ELEMENTS(EVENTI_ASCOLTATI); i++)
        ph_srv_smetti(d->srv, d->ev_srv[i]);
    ph_shell_smetti(d->sh, d->ev_area);
    ph_shell_smetti(d->sh, d->ev_nascoste);
    ph_shell_smetti(d->sh, d->ev_host);
    ph_shell_smetti(d->sh, d->ev_finestre);
    {
        /* Il gestore sulla finestra (puo' vivere piu' della radice) si toglie prima di tutto. */
        GtkWindow *f = g_weak_ref_get(&d->finestra);
        if (f != NULL) {
            g_signal_handler_disconnect(f, d->id_attiva);
            g_object_unref(f);
        }
        g_weak_ref_clear(&d->finestra);
        g_signal_handler_disconnect(w, d->id_root);
    }
    /* ...poi il gestore: se la shell intera sta chiudendo (ph_shell_libera) i gestori
     * sono gia' stati tolti e `sh` non va piu' toccato. */
    if (ph_shell_radice_viva(w))
        ph_shell_registra_desktop(d->sh, NULL);
    /* Richieste in volo e timer: nessuno resta vivo dopo questo gestore (il timer dei
     * widget e' tolto in ph_dsk_widget_libera). Dopo qui nessun puntatore a PhShell o
     * PhServizi viene usato: le operazioni asincrone ancora in corso controllano `vivo`. */
    g_cancellable_cancel(d->annulla);
    ph_dsk_icone_libera(d);
    ph_dsk_widget_libera(d);
    ph_dsk_dialoghi_libera(d->dlg);
    d->dlg = NULL;
    ph_dsk_luogo_libera(d->luogo);
    d->luogo = NULL;
    g_object_unref(d->sfondo);
    g_object_unref(d->libero);
    g_object_unref(d->menu);
    ph_dsk_unref(d);
}

void ph_desktop_installa(PhShell *sh)
{
    static gsize risorse = 0;
    if (g_once_init_enter(&risorse)) {
        ph_risorse_desktop_register_resource();
        g_once_init_leave(&risorse, 1);
    }
    ph_shell_stile_carica("/it/yasde/shell/desktop.css");

    PhDesktop *d = g_new0(PhDesktop, 1);
    d->rif = 1;
    d->vivo = true;
    d->sh = sh;
    d->srv = ph_shell_servizi(sh);
    d->annulla = g_cancellable_new();
    d->icone = g_ptr_array_new_with_free_func(ph_dsk_icone_libera_elemento);
    d->widget = g_ptr_array_new_with_free_func(ph_dsk_widget_libera_elemento);
    d->stato_prec = PH_SRV_CERCO;

    /* Strato desktop: lo sfondo (che prende il clic destro) e sopra il contenitore
     * libero con icone, widget e il riquadro tratteggiato. */
    d->sfondo = ph_dsk_sfondo_nuovo();
    GtkGesture *dx = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dx), GDK_BUTTON_SECONDARY);
    g_signal_connect(dx, "pressed", G_CALLBACK(sfondo_clic_destro), d);
    gtk_widget_add_controller(d->sfondo, GTK_EVENT_CONTROLLER(dx));
    d->libero = ph_shell_libero_nuovo();
    d->tratteggio = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(d->tratteggio, "ph-desktop-tratteggio");
    gtk_widget_set_can_target(d->tratteggio, FALSE);
    gtk_widget_set_visible(d->tratteggio, FALSE);
    ph_shell_libero_metti(d->libero, d->tratteggio, 0, 0, PH_DSK_ICONA_L, PH_DSK_ICONA_A);
    ph_shell_strato_aggiungi(sh, PH_STRATO_DESKTOP, d->sfondo);
    ph_shell_strato_aggiungi(sh, PH_STRATO_DESKTOP, d->libero);
    /* Si puo' lasciare un'icona sia sullo sfondo vuoto sia sopra un'altra icona o
     * un widget (figli del contenitore libero). */
    ph_dsk_icone_bersaglio_aggancia(d, d->sfondo);
    ph_dsk_icone_bersaglio_aggancia(d, d->libero);

    d->menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(d->menu, "ph-menu");
    ph_shell_vetro_registra(sh, d->menu, 10, PH_VETRO_CHIARO); /* raggio di ph-menu */
    d->menu_voci = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    d->menu_scorri = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(d->menu_scorri), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(d->menu_scorri), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(d->menu_scorri), d->menu_voci);
    gtk_box_append(GTK_BOX(d->menu), d->menu_scorri);
    gtk_widget_set_halign(d->menu, GTK_ALIGN_START);
    gtk_widget_set_valign(d->menu, GTK_ALIGN_START);
    gtk_widget_set_visible(d->menu, FALSE);
    ph_shell_strato_aggiungi(sh, PH_STRATO_PANNELLI, d->menu);
    d->dlg = ph_dsk_dialoghi_nuovi(d);
    d->luogo = ph_dsk_luogo_nuovo(d);

    area_aggiorna(d);
    PhShellGestoreDesktop g = {.io = d,
                               .sul_desktop = gestore_sul_desktop,
                               .aggiungi = gestore_aggiungi,
                               .rimuovi = gestore_rimuovi};
    ph_shell_registra_desktop(sh, &g);

    for (size_t i = 0; i < G_N_ELEMENTS(EVENTI_ASCOLTATI); i++)
        d->ev_srv[i] = ph_srv_ascolta(d->srv, EVENTI_ASCOLTATI[i], avviso_servizi, d);
    d->ev_area = ph_shell_ascolta(sh, PH_SHELL_EV_AREA, avviso_area, d);
    d->ev_nascoste = ph_shell_ascolta(sh, PH_SHELL_EV_NASCOSTE, avviso_nascoste, d);
    d->ev_host = ph_shell_ascolta(sh, PH_SHELL_EV_HOST, avviso_host, d);
    d->ev_finestre = ph_shell_ascolta(sh, PH_SHELL_EV_FINESTRE, avviso_finestre, d);

    /* strati_dispose stacca i figli PRIMA del "destroy" della radice: un riferimento
     * tiene vivi i widget finche' esistono i nostri ascolti. */
    g_object_ref(d->sfondo);
    g_object_ref(d->libero);
    g_object_ref(d->menu);
    ph_dsk_luogo_comunica(d); /* all'avvio: da sola non fa partire nessun collegamento */
    ph_dsk_sfondo_applica(d);
    ph_dsk_icone_ricostruisci(d);
    ph_dsk_widget_ricostruisci(d);
    g_object_set_data(G_OBJECT(ph_shell_radice(sh)), "ph-desktop", d);
    g_weak_ref_init(&d->finestra, NULL);
    d->id_root = g_signal_connect(ph_shell_radice(sh), "notify::root", G_CALLBACK(radice_con_finestra), d);
    radice_con_finestra(G_OBJECT(ph_shell_radice(sh)), NULL, d); /* gia' in una finestra */
    g_signal_connect(ph_shell_radice(sh), "destroy", G_CALLBACK(radice_distrutta), d);
}
