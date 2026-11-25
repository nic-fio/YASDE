/* Il contesto della shell (shell.h): radice a strati, contenitore libero,
 * popup, avvisi, gestori, stato della finestra host, trascinamento delle app,
 * accessibilita'. */
#include <stdio.h>
#include <string.h>

#include "interno.h"
#include "shell/shell.h"

/* ======================================================================
 * Radice a strati: ogni figlio riceve tutta l'area e si colloca da se' con
 * halign/valign e margini (lo fa gtk_widget_allocate). I figli sono tenuti
 * in ordine di strato: GTK disegna dal primo all'ultimo e sceglie il
 * destinatario di un clic dall'ultimo al primo, quindi lo strato piu' alto
 * sta sopra in entrambi i sensi. */
#define PH_TIPO_STRATI (ph_strati_get_type())
G_DECLARE_FINAL_TYPE(PhStrati, ph_strati, PH, STRATI, GtkWidget)

struct _PhStrati {
    GtkWidget genitore;
    PhShell *sh;
    int w, h;
};

G_DEFINE_FINAL_TYPE(PhStrati, ph_strati, GTK_TYPE_WIDGET)

static void area_cambiata(void *dati);

static void strati_measure(GtkWidget *w, GtkOrientation o, int per, int *min, int *nat,
                           int *min_base, int *nat_base)
{
    (void)per;
    int mn = 0, nt = 0;
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (!gtk_widget_get_visible(c))
            continue;
        int cm = 0, cn = 0;
        gtk_widget_measure(c, o, -1, &cm, &cn, NULL, NULL);
        if (cm > mn)
            mn = cm;
        if (cn > nt)
            nt = cn;
    }
    *min = mn;
    *nat = nt;
    *min_base = -1;
    *nat_base = -1;
}

static void strati_allocate(GtkWidget *w, int larg, int alt, int base)
{
    (void)base;
    PhStrati *s = PH_STRATI(w);
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (!gtk_widget_get_visible(c))
            continue;
        int mw = 0, mh = 0;
        gtk_widget_measure(c, GTK_ORIENTATION_HORIZONTAL, -1, &mw, NULL, NULL, NULL);
        gtk_widget_measure(c, GTK_ORIENTATION_VERTICAL, -1, &mh, NULL, NULL, NULL);
        gtk_widget_allocate(c, MAX(larg, mw), MAX(alt, mh), -1, NULL);
    }
    if (larg != s->w || alt != s->h) {
        s->w = larg;
        s->h = alt;
        /* L'avviso non parte durante l'allocazione: chi ascolta puo' voler
         * ridisporre (queue_allocate) e lo deve fare fuori da qui. */
        ph_shell_consegna(area_cambiata, g_object_ref(w), g_object_unref);
    }
}

static void strati_dispose(GObject *o)
{
    ph_vetro_radice_finisci(GTK_WIDGET(o));
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(GTK_WIDGET(o))) != NULL)
        gtk_widget_unparent(c);
    G_OBJECT_CLASS(ph_strati_parent_class)->dispose(o);
}

static void ph_strati_class_init(PhStratiClass *k)
{
    G_OBJECT_CLASS(k)->dispose = strati_dispose;
    GTK_WIDGET_CLASS(k)->measure = strati_measure;
    GTK_WIDGET_CLASS(k)->size_allocate = strati_allocate;
    GTK_WIDGET_CLASS(k)->snapshot = ph_vetro_radice_snapshot;
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(k), "ph-shell");
}

static void ph_strati_init(PhStrati *s)
{
    gtk_widget_set_overflow(GTK_WIDGET(s), GTK_OVERFLOW_HIDDEN);
    gtk_widget_add_css_class(GTK_WIDGET(s), "ph-shell");
    gtk_widget_set_focusable(GTK_WIDGET(s), FALSE);
    ph_vetro_radice_inizia(GTK_WIDGET(s));
}

/* ======================================================================
 * Contenitore libero: figli in posizione assoluta; trasparente ai clic fuori
 * dai figli (contains -> FALSE: GTK prova comunque i figli). */
#define PH_TIPO_LIBERO (ph_libero_get_type())
G_DECLARE_FINAL_TYPE(PhLibero, ph_libero, PH, LIBERO, GtkWidget)

struct _PhLibero {
    GtkWidget genitore;
};

G_DEFINE_FINAL_TYPE(PhLibero, ph_libero, GTK_TYPE_WIDGET)

typedef struct {
    int x, y, w, h;
} PosLibera;

static const char *const CHIAVE_POS = "ph-libero-pos";

static void libero_measure(GtkWidget *w, GtkOrientation o, int per, int *min, int *nat,
                           int *min_base, int *nat_base)
{
    (void)w;
    (void)o;
    (void)per;
    *min = 0;
    *nat = 0;
    *min_base = -1;
    *nat_base = -1;
}

static void libero_allocate(GtkWidget *w, int larg, int alt, int base)
{
    (void)larg;
    (void)alt;
    (void)base;
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (!gtk_widget_get_visible(c))
            continue;
        const PosLibera *p = g_object_get_data(G_OBJECT(c), CHIAVE_POS);
        if (p == NULL)
            continue;
        int mw = 0, nw = 0, mh = 0, nh = 0;
        gtk_widget_measure(c, GTK_ORIENTATION_HORIZONTAL, -1, &mw, &nw, NULL, NULL);
        int cw = p->w >= 0 ? MAX(p->w, mw) : nw;
        gtk_widget_measure(c, GTK_ORIENTATION_VERTICAL, cw, &mh, &nh, NULL, NULL);
        int ch = p->h >= 0 ? MAX(p->h, mh) : nh;
        graphene_point_t pt = GRAPHENE_POINT_INIT((float)p->x, (float)p->y);
        gtk_widget_allocate(c, cw, ch, -1, gsk_transform_translate(NULL, &pt));
    }
}

static gboolean libero_contains(GtkWidget *w, double x, double y)
{
    (void)w;
    (void)x;
    (void)y;
    return FALSE;
}

static void libero_dispose(GObject *o)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(GTK_WIDGET(o))) != NULL)
        gtk_widget_unparent(c);
    G_OBJECT_CLASS(ph_libero_parent_class)->dispose(o);
}

static void ph_libero_class_init(PhLiberoClass *k)
{
    G_OBJECT_CLASS(k)->dispose = libero_dispose;
    GTK_WIDGET_CLASS(k)->measure = libero_measure;
    GTK_WIDGET_CLASS(k)->size_allocate = libero_allocate;
    GTK_WIDGET_CLASS(k)->contains = libero_contains;
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(k), "ph-libero");
}

static void ph_libero_init(PhLibero *l)
{
    (void)l;
}

GtkWidget *ph_shell_libero_nuovo(void)
{
    GtkWidget *w = g_object_new(PH_TIPO_LIBERO, NULL);
    gtk_widget_set_hexpand(w, TRUE);
    gtk_widget_set_vexpand(w, TRUE);
    return w;
}

static void pos_imposta(GtkWidget *figlio, int x, int y, int larghezza, int altezza)
{
    PosLibera *p = g_object_get_data(G_OBJECT(figlio), CHIAVE_POS);
    if (p == NULL) {
        p = g_new0(PosLibera, 1);
        g_object_set_data_full(G_OBJECT(figlio), CHIAVE_POS, p, g_free);
    }
    p->x = x;
    p->y = y;
    p->w = larghezza;
    p->h = altezza;
}

void ph_shell_libero_metti(GtkWidget *libero, GtkWidget *figlio, int x, int y, int larghezza,
                           int altezza)
{
    g_return_if_fail(PH_IS_LIBERO(libero) && GTK_IS_WIDGET(figlio));
    pos_imposta(figlio, x, y, larghezza, altezza);
    gtk_widget_set_parent(figlio, libero);
}

void ph_shell_libero_sposta(GtkWidget *libero, GtkWidget *figlio, int x, int y, int larghezza,
                            int altezza)
{
    g_return_if_fail(PH_IS_LIBERO(libero) && gtk_widget_get_parent(figlio) == libero);
    pos_imposta(figlio, x, y, larghezza, altezza);
    gtk_widget_queue_allocate(libero);
}

void ph_shell_libero_togli(GtkWidget *libero, GtkWidget *figlio)
{
    g_return_if_fail(PH_IS_LIBERO(libero) && gtk_widget_get_parent(figlio) == libero);
    gtk_widget_unparent(figlio);
}

void ph_shell_libero_in_cima(GtkWidget *libero, GtkWidget *figlio)
{
    g_return_if_fail(PH_IS_LIBERO(libero) && gtk_widget_get_parent(figlio) == libero);
    gtk_widget_insert_before(figlio, libero, NULL);
}

/* ======================================================================
 * Il contesto. */
struct PhShell {
    PhServizi *servizi;
    GtkWidget *radice;      /* PhStrati, con un riferimento nostro */
    GtkWidget *velo_popup;  /* velo trasparente sotto i popup */

    PhShellPopup popup;     /* aperto ora */
    GtkWidget *popup_w;
    PhShellPopupChiuso popup_chiuso;
    void *popup_utente;

    PhAscoltatori ascoltatori;

    PhShellGestoreFinestre g_finestre;
    PhShellGestoreDesktop g_desktop;
    PhShellGestoreAvvisi g_avvisi;
    PhShellGestoreLauncher g_launcher;
    PhShellGestoreTrasferimento g_trasferimento;
    PhShellGestoreSistema g_sistema;
    PhShellGestoreFissate g_fissate;
    bool ha_finestre, ha_desktop, ha_avvisi, ha_launcher, ha_trasferimento, ha_sistema, ha_fissate;

    bool host_visibile;
    bool schermo_intero;
};

static void area_cambiata(void *dati)
{
    PhStrati *s = PH_STRATI(dati);
    if (s->sh != NULL)
        ph_shell_emetti(s->sh, PH_SHELL_EV_AREA);
}

static void popup_dimenticato(gpointer dati, GObject *morto)
{
    /* Il widget del popup aperto e' stato distrutto: si chiude senza toccarlo. */
    PhShell *sh = dati;
    if ((GObject *)sh->popup_w == morto) {
        sh->popup = PH_POPUP_NESSUNO;
        sh->popup_w = NULL;
        sh->popup_chiuso = NULL;
        gtk_widget_set_visible(sh->velo_popup, FALSE);
        ph_shell_emetti(sh, PH_SHELL_EV_POPUP);
    }
}

static void velo_premuto(GtkGestureClick *g, int n, double x, double y, gpointer dati)
{
    (void)n;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
    ph_shell_popup_chiudi(dati);
}

static gboolean tasto_premuto(GtkEventControllerKey *k, guint tasto, guint codice,
                              GdkModifierType stato, gpointer dati)
{
    (void)k;
    (void)codice;
    (void)stato;
    PhShell *sh = dati;
    if (tasto == GDK_KEY_Escape && sh->popup != PH_POPUP_NESSUNO) {
        ph_shell_popup_chiudi(sh);
        return TRUE;
    }
    return FALSE;
}

PhShell *ph_shell_nuova(PhServizi *servizi)
{
    ph_shell_segna_thread_gtk();
    ph_shell_comune_risorse();
    ph_shell_stile_carica("/it/yasde/shell/base.css");

    PhShell *sh = g_new0(PhShell, 1);
    ph_vivi_aggiungi(sh);
    sh->servizi = servizi;
    sh->host_visibile = true;
    ph_ascoltatori_inizia(&sh->ascoltatori);

    PhStrati *s = g_object_new(PH_TIPO_STRATI, NULL);
    s->sh = sh;
    sh->radice = g_object_ref_sink(GTK_WIDGET(s));
    g_object_set_data(G_OBJECT(sh->radice), "ph-shell", sh);

    GtkEventController *k = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(k, GTK_PHASE_CAPTURE);
    g_signal_connect(k, "key-pressed", G_CALLBACK(tasto_premuto), sh);
    gtk_widget_add_controller(sh->radice, k);

    sh->velo_popup = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sh->velo_popup, "ph-velo-popup");
    gtk_widget_set_halign(sh->velo_popup, GTK_ALIGN_FILL);
    gtk_widget_set_valign(sh->velo_popup, GTK_ALIGN_FILL);
    GtkGesture *g = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), 0);
    g_signal_connect(g, "pressed", G_CALLBACK(velo_premuto), sh);
    gtk_widget_add_controller(sh->velo_popup, GTK_EVENT_CONTROLLER(g));
    gtk_widget_set_visible(sh->velo_popup, FALSE);
    ph_shell_strato_aggiungi(sh, PH_STRATO_PANNELLI, sh->velo_popup);
    return sh;
}

void ph_shell_libera(PhShell *sh)
{
    if (sh == NULL)
        return;
    if (sh->popup_w != NULL)
        g_object_weak_unref(G_OBJECT(sh->popup_w), popup_dimenticato, sh);
    GtkWidget *radice = sh->radice;
    PH_STRATI(radice)->sh = NULL;
    g_object_set_data(G_OBJECT(radice), "ph-shell", NULL);
    /* La radice muore ORA, non quando GTK finira' di liberarla (un riferimento
     * di troppo, per esempio la finestra tenuta viva da un segnale in corso, la
     * farebbe morire in un giro successivo del ciclo principale, dopo shell e
     * servizi): si toglie dalla finestra, poi dispose, che emette "destroy"
     * mentre shell e servizi sono ancora vivi. Quando ph_shell_libera torna, il
     * "destroy" della radice e' gia' avvenuto (una volta sola). */
    GtkWidget *genitore = gtk_widget_get_parent(radice);
    if (genitore != NULL && GTK_IS_WINDOW(genitore)) {
        gtk_window_set_child(GTK_WINDOW(genitore), NULL);
    } else if (genitore != NULL) {
        /* Non e' il contratto (la radice sta dentro una finestra host): la si
         * toglie lo stesso, ma si dice. */
        fprintf(stderr, "shell: la radice ha un genitore che non e' una GtkWindow: la tolgo\n");
        gtk_widget_unparent(radice);
    }
    g_object_run_dispose(G_OBJECT(radice));
    g_object_unref(radice);
    ph_vivi_togli(sh);
    ph_ascoltatori_finisci(&sh->ascoltatori);
    g_free(sh);
}

GtkWidget *ph_shell_radice(PhShell *sh) { return sh->radice; }

bool ph_shell_connessione_da_mostrare(PhSrvStato stato, bool collegato_in_sessione)
{
    switch (stato) {
    case PH_SRV_NON_ABBINATO:
        return true;
    case PH_SRV_CERCO:
    case PH_SRV_PERSO:
        return !collegato_in_sessione;
    case PH_SRV_COLLEGATO:
    case PH_SRV_BLOCCATO:
    case PH_SRV_CHIUSO:
        return false;
    }
    return false;
}

bool ph_shell_radice_viva(GtkWidget *radice)
{
    return radice != NULL && GTK_IS_WIDGET(radice) && g_object_get_data(G_OBJECT(radice), "ph-shell") != NULL;
}
PhServizi *ph_shell_servizi(PhShell *sh) { return sh->servizi; }

GtkWindow *ph_shell_finestra(PhShell *sh)
{
    GtkRoot *r = gtk_widget_get_root(sh->radice);
    return GTK_IS_WINDOW(r) ? GTK_WINDOW(r) : NULL;
}

/* ---- Strati ---- */
static const char *const CHIAVE_STRATO = "ph-strato";

void ph_shell_strato_aggiungi(PhShell *sh, PhShellStrato strato, GtkWidget *w)
{
    g_return_if_fail(sh != NULL && GTK_IS_WIDGET(w) && strato < PH_STRATO_NUMERO);
    g_object_set_data(G_OBJECT(w), CHIAVE_STRATO, GINT_TO_POINTER((int)strato + 1));
    GtkWidget *prima = NULL;
    for (GtkWidget *c = gtk_widget_get_first_child(sh->radice); c != NULL;
         c = gtk_widget_get_next_sibling(c)) {
        int sc = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(c), CHIAVE_STRATO)) - 1;
        if (sc > (int)strato) {
            prima = c;
            break;
        }
    }
    gtk_widget_insert_before(w, sh->radice, prima);
}

void ph_shell_strato_togli(PhShell *sh, GtkWidget *w)
{
    g_return_if_fail(sh != NULL && gtk_widget_get_parent(w) == sh->radice);
    gtk_widget_unparent(w);
}

void ph_shell_area(PhShell *sh, int *larghezza, int *altezza)
{
    if (larghezza != NULL)
        *larghezza = gtk_widget_get_width(sh->radice);
    if (altezza != NULL)
        *altezza = gtk_widget_get_height(sh->radice);
}

/* ---- Popup ---- */
void ph_shell_popup_chiudi(PhShell *sh)
{
    if (sh->popup == PH_POPUP_NESSUNO)
        return;
    PhShellPopup tipo = sh->popup;
    GtkWidget *w = sh->popup_w;
    PhShellPopupChiuso cb = sh->popup_chiuso;
    void *ut = sh->popup_utente;
    sh->popup = PH_POPUP_NESSUNO;
    sh->popup_w = NULL;
    sh->popup_chiuso = NULL;
    gtk_widget_set_visible(sh->velo_popup, FALSE);
    if (w != NULL) {
        g_object_weak_unref(G_OBJECT(w), popup_dimenticato, sh);
        gtk_widget_set_visible(w, FALSE);
    }
    if (cb != NULL)
        cb(sh, tipo, w, ut);
    ph_shell_emetti(sh, PH_SHELL_EV_POPUP);
}

void ph_shell_popup_apri(PhShell *sh, PhShellPopup tipo, GtkWidget *w, PhShellPopupChiuso chiuso,
                         void *utente)
{
    g_return_if_fail(sh != NULL && GTK_IS_WIDGET(w) && tipo != PH_POPUP_NESSUNO);
    if (sh->popup != PH_POPUP_NESSUNO) {
        /* Uno solo alla volta: si chiude l'altro senza emettere due avvisi. */
        PhShellPopup vt = sh->popup;
        GtkWidget *vw = sh->popup_w;
        PhShellPopupChiuso vcb = sh->popup_chiuso;
        void *vut = sh->popup_utente;
        sh->popup = PH_POPUP_NESSUNO;
        sh->popup_w = NULL;
        if (vw != NULL) {
            g_object_weak_unref(G_OBJECT(vw), popup_dimenticato, sh);
            gtk_widget_set_visible(vw, FALSE);
        }
        if (vcb != NULL)
            vcb(sh, vt, vw, vut);
    }
    sh->popup = tipo;
    sh->popup_w = w;
    sh->popup_chiuso = chiuso;
    sh->popup_utente = utente;
    g_object_weak_ref(G_OBJECT(w), popup_dimenticato, sh);
    gtk_widget_set_visible(sh->velo_popup, TRUE);
    gtk_widget_set_visible(w, TRUE);
    gtk_widget_child_focus(w, GTK_DIR_TAB_FORWARD);
    ph_shell_emetti(sh, PH_SHELL_EV_POPUP);
}

void ph_shell_popup_alterna(PhShell *sh, PhShellPopup tipo, GtkWidget *w,
                            PhShellPopupChiuso chiuso, void *utente)
{
    if (sh->popup == tipo && sh->popup_w == w)
        ph_shell_popup_chiudi(sh);
    else
        ph_shell_popup_apri(sh, tipo, w, chiuso, utente);
}

PhShellPopup ph_shell_popup_aperto(PhShell *sh) { return sh->popup; }

/* ---- Avvisi ---- */
guint ph_shell_ascolta(PhShell *sh, PhShellEvento ev, PhShellAvviso fn, void *utente)
{
    g_return_val_if_fail(sh != NULL && ev < PH_SHELL_EV_NUMERO, 0);
    return ph_ascoltatori_aggiungi(&sh->ascoltatori, (int)ev, (PhFnGenerica)fn, utente);
}

void ph_shell_smetti(PhShell *sh, guint id)
{
    /* Dopo ph_shell_libera non si tocca nulla (registro degli oggetti vivi). */
    if (sh != NULL && id != 0 && ph_vivi_e(sh))
        ph_ascoltatori_togli(&sh->ascoltatori, id);
}

void ph_shell_emetti(PhShell *sh, PhShellEvento ev)
{
    guint n = ph_ascoltatori_apri(&sh->ascoltatori);
    for (guint i = 0; i < n; i++) {
        const PhAscoltatore *v = ph_ascoltatori_voce(&sh->ascoltatori, i, (int)ev);
        if (v != NULL)
            ((PhShellAvviso)v->fn)(sh, ev, v->utente);
    }
    ph_ascoltatori_chiudi(&sh->ascoltatori);
}

void ph_shell_imposta_host(PhShell *sh, bool visibile, bool schermo_intero)
{
    if (sh->host_visibile == visibile && sh->schermo_intero == schermo_intero)
        return;
    sh->host_visibile = visibile;
    sh->schermo_intero = schermo_intero;
    ph_shell_emetti(sh, PH_SHELL_EV_HOST);
}

bool ph_shell_host_visibile(PhShell *sh) { return sh->host_visibile; }
bool ph_shell_schermo_intero(PhShell *sh) { return sh->schermo_intero; }

/* ---- Gestori ---- */
#define REGISTRA(nome, Tipo)                                                                       \
    void ph_shell_registra_##nome(PhShell *sh, const Tipo *g)                                     \
    {                                                                                              \
        if (g == NULL) {                                                                           \
            memset(&sh->g_##nome, 0, sizeof sh->g_##nome);                                         \
            sh->ha_##nome = false;                                                                 \
        } else {                                                                                   \
            sh->g_##nome = *g;                                                                     \
            sh->ha_##nome = true;                                                                  \
        }                                                                                          \
    }
REGISTRA(finestre, PhShellGestoreFinestre)
REGISTRA(desktop, PhShellGestoreDesktop)
REGISTRA(avvisi, PhShellGestoreAvvisi)
REGISTRA(launcher, PhShellGestoreLauncher)
REGISTRA(trasferimento, PhShellGestoreTrasferimento)
REGISTRA(sistema, PhShellGestoreSistema)
REGISTRA(fissate, PhShellGestoreFissate)
#undef REGISTRA

static void manca(const char *che_cosa)
{
    fprintf(stderr, "shell: nessun gestore per \"%s\" (normale in un demo di un pezzo solo)\n",
            che_cosa);
}

void ph_shell_apri_app(PhShell *sh, const char *pacchetto)
{
    if (sh->ha_finestre && sh->g_finestre.apri_app != NULL && pacchetto != NULL)
        sh->g_finestre.apri_app(sh->g_finestre.io, pacchetto);
    else
        manca("apri app");
}

size_t ph_shell_finestre(PhShell *sh, PhShellFinestra *out, size_t max)
{
    if (sh->ha_finestre && sh->g_finestre.elenco != NULL)
        return sh->g_finestre.elenco(sh->g_finestre.io, out, max);
    return 0;
}

void ph_shell_clic_barra(PhShell *sh, unsigned id)
{
    if (sh->ha_finestre && sh->g_finestre.clic_barra != NULL)
        sh->g_finestre.clic_barra(sh->g_finestre.io, id);
    else
        manca("clic sulla barra");
}

void ph_shell_specchio(PhShell *sh, bool mostra)
{
    if (sh->ha_finestre && sh->g_finestre.specchio != NULL)
        sh->g_finestre.specchio(sh->g_finestre.io, mostra);
    else
        manca("schermo del telefono");
}

bool ph_shell_specchio_aperto(PhShell *sh)
{
    return sh->ha_finestre && sh->g_finestre.specchio_aperto != NULL &&
           sh->g_finestre.specchio_aperto(sh->g_finestre.io);
}

bool ph_shell_sul_desktop(PhShell *sh, const char *pacchetto)
{
    return sh->ha_desktop && sh->g_desktop.sul_desktop != NULL && pacchetto != NULL &&
           sh->g_desktop.sul_desktop(sh->g_desktop.io, pacchetto);
}

void ph_shell_aggiungi_al_desktop(PhShell *sh, const char *pacchetto)
{
    if (sh->ha_desktop && sh->g_desktop.aggiungi != NULL && pacchetto != NULL)
        sh->g_desktop.aggiungi(sh->g_desktop.io, pacchetto);
    else
        manca("aggiungi al desktop");
}

void ph_shell_rimuovi_dal_desktop(PhShell *sh, const char *pacchetto)
{
    if (sh->ha_desktop && sh->g_desktop.rimuovi != NULL && pacchetto != NULL)
        sh->g_desktop.rimuovi(sh->g_desktop.io, pacchetto);
    else
        manca("rimuovi dal desktop");
}

void ph_shell_avviso(PhShell *sh, const char *titolo, const char *testo)
{
    if (sh->ha_avvisi && sh->g_avvisi.avviso != NULL)
        sh->g_avvisi.avviso(sh->g_avvisi.io, titolo != NULL ? titolo : "", testo != NULL ? testo : "");
    else
        fprintf(stderr, "shell: avviso \"%s\" - \"%s\" (nessun gestore degli avvisi)\n",
                titolo != NULL ? titolo : "", testo != NULL ? testo : "");
}

void ph_shell_launcher_alterna(PhShell *sh)
{
    if (sh->ha_launcher && sh->g_launcher.alterna != NULL)
        sh->g_launcher.alterna(sh->g_launcher.io);
    else
        manca("launcher");
}

void ph_shell_trasferimento_apri(PhShell *sh, PhSrvVerso verso)
{
    if (sh->ha_trasferimento && sh->g_trasferimento.apri != NULL)
        sh->g_trasferimento.apri(sh->g_trasferimento.io, verso);
    else
        manca("trasferimento");
}

GtkWidget *ph_shell_sistema_pulsante(PhShell *sh, PhShellPopup tipo)
{
    if (sh->ha_sistema && sh->g_sistema.pulsante != NULL)
        return sh->g_sistema.pulsante(sh->g_sistema.io, tipo);
    manca("pulsanti dell'area di sistema");
    return NULL;
}

bool ph_shell_notifica_nascosta(PhShell *sh, const char *chiave, uint64_t ora_ms)
{
    return sh->ha_sistema && sh->g_sistema.nascosta != NULL && chiave != NULL &&
           sh->g_sistema.nascosta(sh->g_sistema.io, chiave, ora_ms);
}

void ph_shell_chiudi_finestra(PhShell *sh, unsigned id)
{
    if (sh->ha_finestre && sh->g_finestre.chiudi != NULL)
        sh->g_finestre.chiudi(sh->g_finestre.io, id);
    else
        manca("chiudi finestra");
}

bool ph_shell_app_fissata(PhShell *sh, const char *pacchetto)
{
    return sh->ha_fissate && sh->g_fissate.fissata != NULL && pacchetto != NULL &&
           sh->g_fissate.fissata(sh->g_fissate.io, pacchetto);
}

void ph_shell_fissa_app(PhShell *sh, const char *pacchetto, bool fissa)
{
    if (sh->ha_fissate && sh->g_fissate.fissa != NULL && pacchetto != NULL)
        sh->g_fissate.fissa(sh->g_fissate.io, pacchetto, fissa);
    else
        manca("app fissate");
}

/* ---- Trascinamento di un'app ---- */
static gpointer app_copia(gpointer p)
{
    return g_memdup2(p, sizeof(PhShellAppTrascinata));
}

/* Registrazione a mano: G_DEFINE_BOXED_TYPE con -Wpedantic da' errore (unione
 * trasparente di GLib). */
GType ph_shell_app_trascinata_get_type(void)
{
    static gsize tipo = 0;
    if (g_once_init_enter(&tipo)) {
        GType t = g_boxed_type_register_static(g_intern_static_string("PhShellAppTrascinata"), app_copia,
                                               g_free);
        g_once_init_leave(&tipo, t);
    }
    return (GType)tipo;
}

GdkContentProvider *ph_shell_app_trascinata_contenuto(const char *pacchetto, bool dal_desktop)
{
    PhShellAppTrascinata a = {{0}, dal_desktop};
    g_strlcpy(a.pacchetto, pacchetto != NULL ? pacchetto : "", sizeof a.pacchetto);
    return gdk_content_provider_new_typed(PH_TIPO_APP_TRASCINATA, &a);
}

/* ---- Accessibilita' ---- */
void ph_shell_etichetta(GtkWidget *w, const char *etichetta, const char *suggerimento)
{
    if (etichetta != NULL)
        gtk_accessible_update_property(GTK_ACCESSIBLE(w), GTK_ACCESSIBLE_PROPERTY_LABEL, etichetta,
                                       -1);
    gtk_widget_set_tooltip_text(w, suggerimento);
}

void ph_shell_espanso(GtkWidget *w, bool espanso)
{
    gtk_accessible_update_state(GTK_ACCESSIBLE(w), GTK_ACCESSIBLE_STATE_EXPANDED, (gboolean)espanso,
                                -1);
}

void ph_shell_premuto(GtkWidget *w, bool premuto)
{
    gtk_accessible_update_state(GTK_ACCESSIBLE(w), GTK_ACCESSIBLE_STATE_PRESSED,
                                premuto ? GTK_ACCESSIBLE_TRISTATE_TRUE : GTK_ACCESSIBLE_TRISTATE_FALSE,
                                -1);
}
