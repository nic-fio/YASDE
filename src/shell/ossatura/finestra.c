/* Finestra principale (F-01, specifica 5.1, mockup righe 40-61): la finestra
 * host con la radice della shell dentro. Barra del titolo di sistema con i
 * controlli standard e UN pulsante "Schermo intero"; in schermo intero GTK
 * nasconde la barra e una striscia di 6 px sul bordo alto si espande a 36 px
 * al passaggio del mouse. Qui nasce anche lo stato host (4.14): finestra
 * minimizzata o sospesa -> ph_shell_imposta_host. */
#include "config/lingua.h"
#include "logica.h"
#include "ossatura.h"

typedef struct {
    PhShell *sh;
    GtkWindow *win;
    GtkWidget *barra;       /* GtkHeaderBar */
    GtkWidget *pulsante;    /* "Schermo intero" nella barra del titolo */
    GtkWidget *striscia;    /* PH_STRATO_TITOLO_HOST */
    GtkWidget *rivela;      /* GtkRevealer dentro la striscia */
    GtkWidget *pulsante_striscia;
    GdkSurface *superficie; /* quella a cui sono collegato "notify::state" */
    gulong id_stato;
    bool visibile;          /* ultimo stato host comunicato */
} Finestra;

/* ---- Stato host (4.14) --------------------------------------------------- */
static void host_aggiorna(Finestra *f)
{
    if (f->sh == NULL)
        return; /* la shell e' stata liberata: la finestra non parla piu' con lei */
    bool visibile = true;
    if (f->superficie != NULL && GDK_IS_TOPLEVEL(f->superficie))
        visibile = ph_host_visibile_da_stato(gdk_toplevel_get_state(GDK_TOPLEVEL(f->superficie)));
    bool intero = gtk_window_is_fullscreen(f->win);
    if (visibile != f->visibile) {
        f->visibile = visibile;
        if (!visibile)
            ph_shell_decisione("schermo del PC non visibile", "finestra host minimizzata o sospesa");
        else
            ph_shell_decisione("schermo del PC visibile", "finestra host di nuovo visibile");
    }
    /* Lo imposta alla shell, che avvisa (PH_SHELL_EV_HOST) solo se cambia. */
    ph_shell_imposta_host(f->sh, visibile, intero);
}

static void su_stato_toplevel(GObject *o, GParamSpec *p, gpointer dati)
{
    (void)o;
    (void)p;
    host_aggiorna(dati);
}

static void superficie_scollega(Finestra *f)
{
    if (f->superficie != NULL && f->id_stato != 0)
        g_signal_handler_disconnect(f->superficie, f->id_stato);
    f->superficie = NULL;
    f->id_stato = 0;
}

static void su_realize(GtkWidget *w, gpointer dati)
{
    Finestra *f = dati;
    GdkSurface *s = gtk_native_get_surface(GTK_NATIVE(w));
    superficie_scollega(f);
    if (s != NULL) {
        f->superficie = s;
        f->id_stato = g_signal_connect(s, "notify::state", G_CALLBACK(su_stato_toplevel), f);
    }
    host_aggiorna(f);
}

static void su_unrealize(GtkWidget *w, gpointer dati)
{
    (void)w;
    superficie_scollega(dati);
}

/* ---- Schermo intero -------------------------------------------------------- */
static void schermo_intero_alterna(Finestra *f)
{
    if (gtk_window_is_fullscreen(f->win))
        gtk_window_unfullscreen(f->win);
    else
        gtk_window_fullscreen(f->win);
}

static void su_pulsante(GtkButton *b, gpointer dati)
{
    (void)b;
    schermo_intero_alterna(dati);
}

static void su_schermo_intero(GObject *o, GParamSpec *p, gpointer dati)
{
    (void)o;
    (void)p;
    Finestra *f = dati;
    bool intero = gtk_window_is_fullscreen(f->win);
    if (f->sh == NULL)
        return;
    /* Nella barra del titolo l'etichetta cambia; in schermo intero la barra
     * non c'e' e compare la striscia. Entrare e uscire non tocca i flussi. */
    if (intero)
        ph_shell_etichetta(f->pulsante, ph_t("Esci da schermo intero"), ph_t("Esci da schermo intero"));
    else
        ph_shell_etichetta(f->pulsante, ph_t("Schermo intero"), ph_t("Schermo intero"));
    gtk_widget_set_visible(f->barra, !intero);
    gtk_widget_set_visible(f->striscia, intero);
    if (!intero)
        gtk_revealer_set_reveal_child(GTK_REVEALER(f->rivela), FALSE);
    host_aggiorna(f);
}

static gboolean su_tasto(GtkEventControllerKey *k, guint tasto, guint codice, GdkModifierType stato,
                         gpointer dati)
{
    (void)k;
    (void)codice;
    (void)stato;
    /* F11 e' della shell: non va al telefono (4.10). */
    if (tasto == GDK_KEY_F11) {
        schermo_intero_alterna(dati);
        return TRUE;
    }
    return FALSE;
}

/* ---- Striscia: 6 px che si espandono a 36 px al passaggio del mouse ------- */
static void su_entra(GtkEventControllerMotion *c, double x, double y, gpointer dati)
{
    (void)c;
    (void)x;
    (void)y;
    gtk_revealer_set_reveal_child(GTK_REVEALER(((Finestra *)dati)->rivela), TRUE);
}

static void su_esce(GtkEventControllerMotion *c, gpointer dati)
{
    (void)c;
    gtk_revealer_set_reveal_child(GTK_REVEALER(((Finestra *)dati)->rivela), FALSE);
}

static GtkWidget *striscia_nuova(Finestra *f)
{
    GtkWidget *s = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(s, "ph-ossatura-striscia");
    gtk_widget_set_halign(s, GTK_ALIGN_FILL);
    gtk_widget_set_valign(s, GTK_ALIGN_START);

    f->rivela = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(f->rivela), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    /* Criterio 5.1: espansione in meno di 150 ms. */
    gtk_revealer_set_transition_duration(GTK_REVEALER(f->rivela), 100);
    gtk_box_append(GTK_BOX(s), f->rivela);

    /* La riga da 36 px del mockup: nome a sinistra, pulsante a destra. */
    GtkWidget *riga = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(riga, "ph-ossatura-titolo-riga");
    gtk_revealer_set_child(GTK_REVEALER(f->rivela), riga);

    GtkWidget *nome = gtk_label_new("YASDE");
    gtk_widget_add_css_class(nome, "ph-ossatura-titolo-nome");
    gtk_widget_set_hexpand(nome, TRUE);
    gtk_widget_set_halign(nome, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(riga), nome);

    f->pulsante_striscia = gtk_button_new();
    gtk_widget_add_css_class(f->pulsante_striscia, "ph-ossatura-wc");
    gtk_button_set_child(GTK_BUTTON(f->pulsante_striscia), ph_shell_icona_nuova(&PH_ICONA_SCHERMO_INTERO));
    ph_shell_etichetta(f->pulsante_striscia, ph_t("Esci da schermo intero"), ph_t("Esci da schermo intero"));
    g_signal_connect(f->pulsante_striscia, "clicked", G_CALLBACK(su_pulsante), f);
    gtk_box_append(GTK_BOX(riga), f->pulsante_striscia);

    GtkEventController *m = gtk_event_controller_motion_new();
    g_signal_connect(m, "enter", G_CALLBACK(su_entra), f);
    g_signal_connect(m, "leave", G_CALLBACK(su_esce), f);
    gtk_widget_add_controller(s, m);

    gtk_widget_set_visible(s, FALSE);
    return s;
}

/* ---- Vita --------------------------------------------------------------------- */
static void finestra_libera(gpointer p)
{
    Finestra *f = p;
    superficie_scollega(f);
    g_free(f);
}

GtkWindow *ph_finestra_nuova(GtkApplication *app, PhShell *sh)
{
    Finestra *f = g_new0(Finestra, 1);
    f->sh = sh;
    f->visibile = true;

    GtkWidget *w = app != NULL ? gtk_application_window_new(app) : gtk_window_new();
    f->win = GTK_WINDOW(w);
    gtk_window_set_title(f->win, "YASDE");
    /* 980x584 (mockup); minima 640x420 (limiti di onRootMove). */
    gtk_window_set_default_size(f->win, 980, 584);
    gtk_widget_set_size_request(w, 640, 420);
    g_object_set_data_full(G_OBJECT(w), "ph-ossatura-finestra", f, finestra_libera);

    /* Barra del titolo di sistema: controlli standard e un solo pulsante. */
    f->barra = gtk_header_bar_new();
    f->pulsante = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(f->pulsante), ph_shell_icona_nuova(&PH_ICONA_SCHERMO_INTERO));
    ph_shell_etichetta(f->pulsante, ph_t("Schermo intero"), ph_t("Schermo intero"));
    g_signal_connect(f->pulsante, "clicked", G_CALLBACK(su_pulsante), f);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(f->barra), f->pulsante);
    gtk_window_set_titlebar(f->win, f->barra);

    gtk_window_set_child(f->win, ph_shell_radice(sh));

    /* Velo e striscia: strati della radice (il velo sopra la striscia no: la
     * striscia sta piu' in alto, 9500, e resta usabile per uscire). */
    ph_velo_installa(sh);
    f->striscia = striscia_nuova(f);
    ph_shell_strato_aggiungi(sh, PH_STRATO_TITOLO_HOST, f->striscia);

    /* F11 in cattura sulla finestra: arriva prima di qualunque finestra interna. */
    GtkEventController *k = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(k, GTK_PHASE_CAPTURE);
    g_signal_connect(k, "key-pressed", G_CALLBACK(su_tasto), f);
    gtk_widget_add_controller(w, k);

    g_signal_connect(w, "notify::fullscreened", G_CALLBACK(su_schermo_intero), f);
    g_signal_connect(w, "realize", G_CALLBACK(su_realize), f);
    g_signal_connect(w, "unrealize", G_CALLBACK(su_unrealize), f);
    return f->win;
}

void ph_finestra_distacca(GtkWindow *win)
{
    Finestra *f = g_object_get_data(G_OBJECT(win), "ph-ossatura-finestra");
    if (f == NULL)
        return;
    superficie_scollega(f);
    f->sh = NULL;
}
