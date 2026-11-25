/* Pezzo shell-launcher (F-04): il launcher a griglia.
 *
 * Il riquadro vive in PH_STRATO_PANNELLI ed e' il popup PH_POPUP_LAUNCHER (uno
 * solo aperto alla volta, lo gestisce il pezzo comune). Il menu contestuale
 * delle app NON e' un popup separato: aprirlo non deve chiudere il launcher
 * (mockup), quindi e' un widget a parte nello stesso strato, sopra il
 * launcher, con sotto un velo trasparente che lo chiude al clic fuori.
 *
 * Con fino a 2000 app (PH_DATI_APP_MAX) non si ricreano i widget a ogni tasto:
 * la griglia e' un GtkGridView, che costruisce i widget solo per le celle
 * visibili e li riusa; la ricerca e' un filtro sul modello. Le icone si
 * chiedono ai servizi solo per le celle che compaiono (a richiesta) e la
 * cache con il suo limite e' dei servizi: qui si tiene un riferimento solo
 * alle texture delle celle visibili.
 * La logica pura (ricerca, colonne, posizione del menu) e' in logica.c. */
#include <string.h>

#include "config/lingua.h"
#include "launcher_interno.h"
#include "logica.h"
#include "shell/pezzi.h"

/* Generata da glib-compile-resources (--manual-register): il CSS del pezzo. */
void ph_risorse_launcher_register_resource(void);

/* Misure del mockup (px). */
enum {
    LATO_ICONA = 60,
    RAGGIO_ICONA = 16,
    RAGGIO_RIQUADRO = 18, /* come .ph-launcher nel CSS */
    RAGGIO_MENU = 10,     /* come .ph-menu in base.css */
    LATO_ICONA_TRASCINA = 48,
    RAGGIO_ICONA_TRASCINA = 12,
    CAMPO_MAX = 420,
    /* Il riquadro e' centrato: larghezza, margine alto, altezza massima e
     * colonne sono in logica.h (PH_LAUNCHER_*, ph_launcher_misure). Qui solo
     * cio' che serve a togliere dall'altezza massima il resto del riquadro. */
    SPAZIO_RIQUADRO = 18,      /* tra campo e griglia (come il CSS del mockup) */
    RIQUADRO_VERTICALE = 40, /* imbottitura 20+20 in alto e in basso (nessun bordo che occupi spazio) */
    SCORREVOLE_MIN = 60,       /* l'altezza utile della griglia non scende mai sotto una riga */
};

/* ---- Una voce dell'elenco (oggetto del modello della griglia) ----------- */
#define PH_TIPO_VOCE (ph_launcher_voce_get_type())
G_DECLARE_FINAL_TYPE(PhLauncherVoce, ph_launcher_voce, PH, LAUNCHER_VOCE, GObject)

struct _PhLauncherVoce {
    GObject genitore;
    char *pacchetto;
    char *nome;
    char *norm; /* nome normalizzato per la ricerca */
};

G_DEFINE_FINAL_TYPE(PhLauncherVoce, ph_launcher_voce, G_TYPE_OBJECT)

static void voce_finalize(GObject *o)
{
    PhLauncherVoce *v = PH_LAUNCHER_VOCE(o);
    g_free(v->pacchetto);
    g_free(v->nome);
    g_free(v->norm);
    G_OBJECT_CLASS(ph_launcher_voce_parent_class)->finalize(o);
}

static void ph_launcher_voce_class_init(PhLauncherVoceClass *k)
{
    G_OBJECT_CLASS(k)->finalize = voce_finalize;
}

static void ph_launcher_voce_init(PhLauncherVoce *v) { (void)v; }

static PhLauncherVoce *voce_nuova(const char *pacchetto, const char *nome)
{
    PhLauncherVoce *v = g_object_new(PH_TIPO_VOCE, NULL);
    v->pacchetto = g_strdup(pacchetto);
    v->nome = g_strdup(nome != NULL && nome[0] != '\0' ? nome : pacchetto);
    v->norm = ph_launcher_normalizza(v->nome, false);
    return v;
}

static PhLauncher *launcher_ref(PhLauncher *l)
{
    l->rif++;
    return l;
}

static void launcher_unref(PhLauncher *l)
{
    if (--l->rif > 0)
        return;
    g_clear_pointer(&l->celle, g_hash_table_unref);
    g_clear_pointer(&l->voci, g_ptr_array_unref);
    g_clear_object(&l->filtrato);
    g_clear_object(&l->filtro);
    g_clear_object(&l->deposito);
    g_free(l->menu_pacchetto);
    g_free(l);
}

static void chiusura_unref(gpointer dati, GClosure *c)
{
    (void)c;
    launcher_unref(dati);
}

/* Collega un segnale tenendo un riferimento allo stato per tutta la vita del
 * collegamento (il widget puo' vivere un poco piu' dello stato "attivo"). */
static void collega(gpointer oggetto, const char *segnale, GCallback fn, PhLauncher *l)
{
    g_signal_connect_data(oggetto, segnale, fn, launcher_ref(l), chiusura_unref, 0);
}

/* ---- Aprire un'app, menu ------------------------------------------------ */
static bool launcher_aperto(PhLauncher *l)
{
    return ph_shell_popup_aperto(l->sh) == PH_POPUP_LAUNCHER;
}

/* Clic, Invio, "Apri": chiude il launcher (e con lui il menu) e apre l'app
 * (mockup openApp: launcher:false, query:''). */
static void apri_app(PhLauncher *l, const char *pacchetto)
{
    char *p = g_strdup(pacchetto);
    if (launcher_aperto(l))
        ph_shell_popup_chiudi(l->sh);
    ph_shell_apri_app(l->sh, p);
    g_free(p);
}

static gboolean menu_aperto(PhLauncher *l) { return l->menu_pacchetto != NULL; }

static gboolean esc_premuto(GtkEventControllerKey *k, guint tasto, guint codice,
                            GdkModifierType stato, gpointer dati);

static void menu_togli_esc(PhLauncher *l)
{
    if (l->esc != NULL && l->esc_finestra != NULL)
        gtk_widget_remove_controller(l->esc_finestra, l->esc);
    if (l->esc_finestra != NULL)
        g_object_remove_weak_pointer(G_OBJECT(l->esc_finestra), (gpointer *)&l->esc_finestra);
    l->esc = NULL;
    l->esc_finestra = NULL;
}

static void menu_chiudi(PhLauncher *l, bool fuoco_al_campo)
{
    if (!menu_aperto(l))
        return;
    g_clear_pointer(&l->menu_pacchetto, g_free);
    gtk_widget_set_visible(l->menu, FALSE);
    gtk_widget_set_visible(l->velo_menu, FALSE);
    menu_togli_esc(l);
    if (fuoco_al_campo && launcher_aperto(l))
        gtk_widget_grab_focus(l->campo);
}

/* Esc con il menu aperto lo chiude e basta: il gestore dei popup, in cattura
 * sulla radice, chiuderebbe invece tutto il launcher. Per passargli davanti si
 * ascolta in cattura sulla finestra, che sta sopra la radice. */
static gboolean esc_premuto(GtkEventControllerKey *k, guint tasto, guint codice,
                            GdkModifierType stato, gpointer dati)
{
    (void)k;
    (void)codice;
    (void)stato;
    PhLauncher *l = dati;
    if (tasto == GDK_KEY_Escape && menu_aperto(l)) {
        menu_chiudi(l, true);
        return TRUE;
    }
    return FALSE;
}

/* Apre il menu per `pacchetto` al punto (x, y) di `riferimento`. */
/* Mostra «Fissa sulla barra» o «Togli dalla barra» secondo lo stato attuale
 * dell'app del menu aperto. */
static void menu_voci_barra(PhLauncher *l)
{
    bool fissata = l->menu_pacchetto != NULL && ph_shell_app_fissata(l->sh, l->menu_pacchetto);
    gtk_widget_set_visible(l->voce_fissa, !fissata);
    gtk_widget_set_visible(l->voce_togli, fissata);
}

void ph_launcher_interno_menu_apri(PhLauncher *l, GtkWidget *riferimento, double x, double y,
                                   const char *pacchetto)
{
    GtkWidget *radice = ph_shell_radice(l->sh);
    graphene_point_t in = GRAPHENE_POINT_INIT((float)x, (float)y), fuori;
    if (!gtk_widget_compute_point(riferimento, radice, &in, &fuori))
        fuori = in;
    int larg = 0, alt = 0, mx = 0, my = 0;
    ph_shell_area(l->sh, &larg, &alt);
    ph_launcher_posizione_menu((int)fuori.x, (int)fuori.y, larg, alt, &mx, &my);

    menu_chiudi(l, false);
    l->menu_pacchetto = g_strdup(pacchetto);
    bool sul = ph_shell_sul_desktop(l->sh, pacchetto);
    gtk_widget_set_visible(l->voce_aggiungi, !sul);
    gtk_widget_set_visible(l->voce_rimuovi, sul);
    menu_voci_barra(l);
    gtk_widget_set_margin_start(l->menu, mx);
    gtk_widget_set_margin_top(l->menu, my);
    gtk_widget_set_visible(l->velo_menu, TRUE);
    gtk_widget_set_visible(l->menu, TRUE);
    gtk_widget_grab_focus(l->voce_apri);

    GtkRoot *r = gtk_widget_get_root(l->menu);
    if (r != NULL && GTK_IS_WIDGET(r)) {
        l->esc_finestra = GTK_WIDGET(r);
        g_object_add_weak_pointer(G_OBJECT(l->esc_finestra), (gpointer *)&l->esc_finestra);
        l->esc = gtk_event_controller_key_new();
        gtk_event_controller_set_propagation_phase(l->esc, GTK_PHASE_CAPTURE);
        g_signal_connect(l->esc, "key-pressed", G_CALLBACK(esc_premuto), l);
        gtk_widget_add_controller(l->esc_finestra, l->esc);
    }
}

/* La cella (se c'e') sotto il punto (x, y) del velo, che copre la radice. */
static PhCella *cella_sotto(PhLauncher *l, GtkWidget *velo, double x, double y)
{
    graphene_point_t in = GRAPHENE_POINT_INIT((float)x, (float)y), fuori;
    if (!gtk_widget_compute_point(velo, l->griglia, &in, &fuori))
        return NULL;
    GtkWidget *w = gtk_widget_pick(l->griglia, fuori.x, fuori.y, GTK_PICK_DEFAULT);
    for (; w != NULL && w != l->griglia; w = gtk_widget_get_parent(w)) {
        PhCella *c = g_object_get_data(G_OBJECT(w), PH_LAUNCHER_CHIAVE_CELLA);
        if (c != NULL)
            return c;
    }
    return NULL;
}

static void velo_menu_premuto(GtkGestureClick *g, int n, double x, double y, gpointer dati)
{
    (void)n;
    PhLauncher *l = dati;
    if (l->distrutto)
        return;
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
    /* Clic destro su un'altra app con il menu aperto: il menu passa subito a
     * quella (mockup); ogni altro clic chiude soltanto il menu. */
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g)) == GDK_BUTTON_SECONDARY) {
        GtkWidget *velo = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
        PhCella *c = cella_sotto(l, velo, x, y);
        if (c != NULL && c->pacchetto != NULL) {
            char *p = g_strdup(c->pacchetto);
            graphene_point_t in = GRAPHENE_POINT_INIT((float)x, (float)y), fuori;
            GtkWidget *radice = ph_shell_radice(l->sh);
            if (!gtk_widget_compute_point(velo, radice, &in, &fuori))
                fuori = in;
            ph_launcher_interno_menu_apri(l, radice, fuori.x, fuori.y, p);
            g_free(p);
            return;
        }
    }
    menu_chiudi(l, true);
}

static void voce_apri_cliccata(GtkButton *b, gpointer dati)
{
    (void)b;
    PhLauncher *l = dati;
    if (l->distrutto || !menu_aperto(l))
        return;
    char *p = g_strdup(l->menu_pacchetto);
    apri_app(l, p);
    g_free(p);
}

static void voce_aggiungi_cliccata(GtkButton *b, gpointer dati)
{
    (void)b;
    PhLauncher *l = dati;
    if (l->distrutto || !menu_aperto(l))
        return;
    /* Il launcher resta aperto; l'avviso lo da' il desktop. */
    char *p = g_strdup(l->menu_pacchetto);
    menu_chiudi(l, true);
    ph_shell_aggiungi_al_desktop(l->sh, p);
    g_free(p);
}

static void voce_rimuovi_cliccata(GtkButton *b, gpointer dati)
{
    (void)b;
    PhLauncher *l = dati;
    if (l->distrutto || !menu_aperto(l))
        return;
    char *p = g_strdup(l->menu_pacchetto);
    menu_chiudi(l, true);
    ph_shell_rimuovi_dal_desktop(l->sh, p);
    g_free(p);
}

static void voce_barra_cliccata(PhLauncher *l, bool fissa)
{
    if (l->distrutto || !menu_aperto(l))
        return;
    /* Il launcher resta aperto, come per «Aggiungi al desktop». */
    char *p = g_strdup(l->menu_pacchetto);
    menu_chiudi(l, true);
    ph_shell_fissa_app(l->sh, p, fissa);
    g_free(p);
}

static void voce_fissa_cliccata(GtkButton *b, gpointer dati)
{
    (void)b;
    voce_barra_cliccata(dati, true);
}

static void voce_togli_cliccata(GtkButton *b, gpointer dati)
{
    (void)b;
    voce_barra_cliccata(dati, false);
}

/* Le app fissate sono cambiate (la barra, un altro telefono): con il menu
 * aperto la voce deve dire la cosa giusta. */
static void fissate_cambiate(PhShell *sh, PhShellEvento ev, void *utente)
{
    (void)sh;
    (void)ev;
    PhLauncher *l = utente;
    if (!l->distrutto && menu_aperto(l))
        menu_voci_barra(l);
}

static GtkWidget *voce_menu_nuova(PhLauncher *l, const char *testo, GCallback fn)
{
    GtkWidget *b = gtk_button_new();
    GtkWidget *e = gtk_label_new(testo);
    gtk_widget_set_halign(e, GTK_ALIGN_START);
    gtk_button_set_child(GTK_BUTTON(b), e);
    gtk_widget_add_css_class(b, "ph-riga");
    collega(b, "clicked", fn, l);
    return b;
}

/* ---- Icone e celle della griglia --------------------------------------- */
static void cella_libera(gpointer p)
{
    PhCella *c = p;
    PhLauncher *l = c->l;
    if (l->celle != NULL && c->pacchetto != NULL && g_hash_table_lookup(l->celle, c->pacchetto) == c)
        g_hash_table_remove(l->celle, c->pacchetto);
    g_clear_object(&c->texture);
    g_free(c->pacchetto);
    g_free(c->nome);
    launcher_unref(l);
    g_free(c);
}

/* Mette nella cella l'icona vera se i servizi ce l'hanno (altrimenti la
 * richiesta parte da sola e arrivera' PH_SRV_EV_ICONA), o il segnaposto con le
 * iniziali come nel mockup. */
static void cella_icona(PhCella *c)
{
    if (c->icona != NULL) {
        gtk_box_remove(GTK_BOX(c->cella), c->icona);
        c->icona = NULL;
    }
    g_clear_object(&c->texture);
    if (c->pacchetto == NULL)
        return;
    GdkTexture *t = ph_srv_icona(c->l->srv, c->pacchetto);
    if (t != NULL) {
        c->texture = g_object_ref(t);
        /* Misura fissa vera: un GtkPicture prenderebbe quella della texture (128). */
        GtkWidget *img = gtk_image_new_from_paintable(GDK_PAINTABLE(t));
        gtk_image_set_pixel_size(GTK_IMAGE(img), LATO_ICONA);
        gtk_widget_set_size_request(img, LATO_ICONA, LATO_ICONA);
        gtk_widget_set_overflow(img, GTK_OVERFLOW_HIDDEN); /* raggio 16 sull'immagine */
        /* Un'icona vera ha la SUA forma (angoli piu' tondi, cerchi, sagome): l'ombra
         * CSS, che e' un rettangolo di raggio 16, le sporgerebbe attorno come un
         * alone quadrato (si vede sul vetro chiaro). L'ombra resta al segnaposto. */
        gtk_widget_add_css_class(img, "ph-launcher-icona-vera");
        c->icona = img;
    } else {
        c->icona = ph_shell_segnaposto_nuovo(c->pacchetto, c->nome, LATO_ICONA, RAGGIO_ICONA);
    }
    /* L'icona sta direttamente nella cella, prima del nome. */
    gtk_widget_add_css_class(c->icona, "ph-launcher-icona");
    gtk_widget_set_halign(c->icona, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(c->icona, GTK_ALIGN_START);
    gtk_widget_set_size_request(c->icona, LATO_ICONA, LATO_ICONA);
    gtk_box_prepend(GTK_BOX(c->cella), c->icona);
}

/* Icona di trascinamento: l'icona 48x48 dell'app (mockup: fantasma 48x48). */
static GdkPaintable *icona_trascinamento(PhCella *c)
{
    GdkPaintable *sorgente = c->texture != NULL ? GDK_PAINTABLE(g_object_ref(c->texture))
                                                : gtk_widget_paintable_new(c->icona);
    GtkSnapshot *s = gtk_snapshot_new();
    GskRoundedRect rr;
    gsk_rounded_rect_init_from_rect(
        &rr, &GRAPHENE_RECT_INIT(0, 0, LATO_ICONA_TRASCINA, LATO_ICONA_TRASCINA),
        RAGGIO_ICONA_TRASCINA);
    gtk_snapshot_push_rounded_clip(s, &rr);
    gdk_paintable_snapshot(sorgente, s, LATO_ICONA_TRASCINA, LATO_ICONA_TRASCINA);
    gtk_snapshot_pop(s);
    g_object_unref(sorgente);
    return gtk_snapshot_free_to_paintable(
        s, &GRAPHENE_SIZE_INIT(LATO_ICONA_TRASCINA, LATO_ICONA_TRASCINA));
}

static void cella_menu_premuta(GtkGestureClick *g, int n, double x, double y, gpointer dati)
{
    (void)n;
    PhCella *c = dati;
    if (c->l->distrutto || c->pacchetto == NULL)
        return;
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
    ph_launcher_interno_menu_apri(c->l, c->cella, x, y, c->pacchetto);
}

static GdkContentProvider *cella_trascinamento_prepara(GtkDragSource *src, double x, double y,
                                                       gpointer dati)
{
    (void)src;
    (void)x;
    (void)y;
    PhCella *c = dati;
    if (c->pacchetto == NULL)
        return NULL;
    return ph_shell_app_trascinata_contenuto(c->pacchetto, false);
}

static void chiudi_dopo(void *dati)
{
    PhLauncher *l = dati;
    if (!l->distrutto && launcher_aperto(l))
        ph_shell_popup_chiudi(l->sh);
}

static void cella_trascinamento_inizia(GtkDragSource *src, GdkDrag *drag, gpointer dati)
{
    (void)drag;
    PhCella *c = dati;
    if (c->l->distrutto)
        return;
    GdkPaintable *p = icona_trascinamento(c);
    gtk_drag_source_set_icon(src, p, LATO_ICONA_TRASCINA / 2, LATO_ICONA_TRASCINA / 2);
    g_object_unref(p);
    /* Il launcher si chiude appena il trascinamento parte (5.2.2); il rilascio
     * sul desktop lo gestisce il desktop. Un giro dopo il segnale, per non
     * nascondere la sorgente dentro la sua stessa partenza. */
    ph_shell_consegna(chiudi_dopo, launcher_ref(c->l), (GDestroyNotify)launcher_unref);
}

static void fabbrica_setup(GtkSignalListItemFactory *f, GtkListItem *item, gpointer dati)
{
    (void)f;
    PhLauncher *l = dati;
    PhCella *c = g_new0(PhCella, 1);
    c->l = launcher_ref(l);

    /* Spazi (8 px tra le colonne, 28 tra le righe, 6 tra icona e nome) e
     * imbottitura sono nel CSS: il GtkGridView non ha spaziatura propria. */
    c->cella = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(c->cella, "ph-launcher-cella");

    c->nome_etichetta = gtk_label_new("");
    gtk_widget_add_css_class(c->nome_etichetta, "ph-launcher-nome");
    /* Nome centrato, a capo se lungo: al massimo due righe, poi i puntini
     * (il nome completo e' nel suggerimento della cella). */
    gtk_label_set_wrap(GTK_LABEL(c->nome_etichetta), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(c->nome_etichetta), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines(GTK_LABEL(c->nome_etichetta), 2);
    gtk_label_set_ellipsize(GTK_LABEL(c->nome_etichetta), PANGO_ELLIPSIZE_END);
    gtk_widget_set_valign(c->nome_etichetta, GTK_ALIGN_START);
    gtk_label_set_justify(GTK_LABEL(c->nome_etichetta), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars(GTK_LABEL(c->nome_etichetta), 14);
    gtk_box_append(GTK_BOX(c->cella), c->nome_etichetta);

    /* Clic destro: menu. */
    GtkGesture *dx = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dx), GDK_BUTTON_SECONDARY);
    g_signal_connect(dx, "pressed", G_CALLBACK(cella_menu_premuta), c);
    gtk_widget_add_controller(c->cella, GTK_EVENT_CONTROLLER(dx));

    /* Trascinamento verso il desktop. */
    GtkDragSource *ds = gtk_drag_source_new();
    gtk_drag_source_set_actions(ds, GDK_ACTION_COPY);
    g_signal_connect(ds, "prepare", G_CALLBACK(cella_trascinamento_prepara), c);
    g_signal_connect(ds, "drag-begin", G_CALLBACK(cella_trascinamento_inizia), c);
    gtk_widget_add_controller(c->cella, GTK_EVENT_CONTROLLER(ds));

    g_object_set_data_full(G_OBJECT(c->cella), PH_LAUNCHER_CHIAVE_CELLA, c, cella_libera);
    gtk_list_item_set_child(item, c->cella);
    gtk_list_item_set_selectable(item, FALSE);
}

static PhCella *cella_di(GtkListItem *item)
{
    GtkWidget *w = gtk_list_item_get_child(item);
    return w != NULL ? g_object_get_data(G_OBJECT(w), PH_LAUNCHER_CHIAVE_CELLA) : NULL;
}

static void fabbrica_bind(GtkSignalListItemFactory *f, GtkListItem *item, gpointer dati)
{
    (void)f;
    (void)dati;
    PhCella *c = cella_di(item);
    PhLauncherVoce *v = gtk_list_item_get_item(item);
    if (c == NULL || v == NULL)
        return;
    g_free(c->pacchetto);
    g_free(c->nome);
    c->pacchetto = g_strdup(v->pacchetto);
    c->nome = g_strdup(v->nome);
    gtk_label_set_text(GTK_LABEL(c->nome_etichetta), v->nome);
    gtk_widget_set_tooltip_text(c->cella, v->nome);
    /* Etichetta accessibile = nome dell'app (aria-label del mockup). */
    gtk_list_item_set_accessible_label(item, v->nome);
    g_hash_table_replace(c->l->celle, g_strdup(c->pacchetto), c);
    cella_icona(c);
}

static void fabbrica_unbind(GtkSignalListItemFactory *f, GtkListItem *item, gpointer dati)
{
    (void)f;
    (void)dati;
    PhCella *c = cella_di(item);
    if (c == NULL)
        return;
    if (c->pacchetto != NULL && g_hash_table_lookup(c->l->celle, c->pacchetto) == c)
        g_hash_table_remove(c->l->celle, c->pacchetto);
    if (c->icona != NULL) {
        gtk_box_remove(GTK_BOX(c->cella), c->icona);
        c->icona = NULL;
    }
    g_clear_object(&c->texture);
    g_clear_pointer(&c->pacchetto, g_free);
    g_clear_pointer(&c->nome, g_free);
}

/* ---- Elenco, ricerca, misure ------------------------------------------- */
static void vuoto_aggiorna(PhLauncher *l)
{
    guint tutte = l->voci != NULL ? l->voci->len : 0;
    guint viste = l->filtrato != NULL ? g_list_model_get_n_items(l->filtrato) : 0;
    /* Elenco vuoto perche' le app non sono ancora arrivate: griglia vuota e
     * nessun testo inventato; il messaggio solo se la ricerca non trova nulla. */
    bool nessuna = tutte > 0 && viste == 0;
    gtk_widget_set_visible(l->nessuna, nessuna);
    gtk_widget_set_visible(l->scorrevole, !nessuna);
}

static void filtrato_cambiato(GListModel *m, guint pos, guint tolte, guint aggiunte, gpointer dati)
{
    (void)m;
    (void)pos;
    (void)tolte;
    (void)aggiunte;
    vuoto_aggiorna(dati);
}

/* Rilegge l'elenco dai servizi (ph_srv_app e' vuoto finche' non e' arrivato un
 * elenco COMPLETO). Se e' uguale a quello gia' mostrato non tocca nulla: si
 * chiama a ogni avviso di app, di collegamento e di profilo e all'apertura,
 * cosi' dopo una riconnessione l'elenco e' sempre quello attuale. */
static void elenco_rileggi(PhLauncher *l)
{
    size_t n = 0;
    const PhSrvApp *a = ph_srv_app(l->srv, &n);
    if (a == NULL)
        n = 0;
    GPtrArray *nuove = g_ptr_array_new_with_free_func(g_object_unref);
    /* Un pacchetto due volte: si tiene la prima voce e si scarta il doppione
     * (la griglia associa le icone ai pacchetti, devono essere unici). */
    GHashTable *visti = g_hash_table_new(g_str_hash, g_str_equal);
    for (size_t i = 0; i < n; i++) {
        if (a[i].pacchetto == NULL || a[i].pacchetto[0] == '\0' ||
            g_hash_table_contains(visti, a[i].pacchetto))
            continue;
        PhLauncherVoce *v = voce_nuova(a[i].pacchetto, a[i].nome);
        g_hash_table_add(visti, v->pacchetto);
        g_ptr_array_add(nuove, v);
    }
    g_hash_table_unref(visti);
    bool uguale = nuove->len == l->voci->len;
    for (guint i = 0; uguale && i < nuove->len; i++) {
        PhLauncherVoce *x = g_ptr_array_index(nuove, i), *y = g_ptr_array_index(l->voci, i);
        uguale = strcmp(x->pacchetto, y->pacchetto) == 0 && strcmp(x->nome, y->nome) == 0;
    }
    if (uguale) {
        g_ptr_array_unref(nuove);
        return;
    }
    /* Una sola sostituzione del modello: la griglia rifa' solo le celle visibili. */
    g_list_store_splice(l->deposito, 0, g_list_model_get_n_items(G_LIST_MODEL(l->deposito)),
                        nuove->len > 0 ? nuove->pdata : NULL, nuove->len);
    g_ptr_array_unref(l->voci);
    l->voci = nuove;
    vuoto_aggiorna(l);
}

/* Limite noto: ridimensionando la finestra host a launcher aperto, per un
 * fotogramma larghezza e margine sono ancora quelli vecchi (l'avviso di area
 * arriva dopo la disposizione, in consegna differita). */
static void misure_aggiorna(PhLauncher *l)
{
    int larg = 0, alt = 0;
    ph_shell_area(l->sh, &larg, &alt);
    PhLauncherMisure m;
    ph_launcher_misure(larg, alt, &m);
    gtk_widget_set_size_request(l->riquadro, m.larghezza > 0 ? m.larghezza : -1, -1);
    gtk_widget_set_margin_top(l->riquadro, m.margine_alto);
    gtk_grid_view_set_min_columns(GTK_GRID_VIEW(l->griglia), (guint)m.colonne);
    gtk_grid_view_set_max_columns(GTK_GRID_VIEW(l->griglia), (guint)m.colonne);
    /* Griglia larga quanto le sue colonne, centrata nel riquadro con i margini
     * (vedi la creazione: niente halign). */
    int resto = m.larghezza_interna > m.larghezza_griglia ? m.larghezza_interna - m.larghezza_griglia : 0;
    gtk_widget_set_margin_start(l->griglia, resto / 2);
    gtk_widget_set_margin_end(l->griglia, resto - resto / 2);
    gtk_widget_set_size_request(l->campo, m.larghezza_campo > 0 ? m.larghezza_campo : -1, -1);
    /* Altezza: il riquadro segue il contenuto fino al massimo; oltre, scorre la
     * griglia (il campo resta fisso). Alla griglia tocca il massimo meno il
     * campo, lo spazio e bordo e imbottitura. */
    int campo_h = 0;
    gtk_widget_measure(l->campo, GTK_ORIENTATION_VERTICAL, -1, &campo_h, NULL, NULL, NULL);
    int utile = m.altezza_max - RIQUADRO_VERTICALE - campo_h - SPAZIO_RIQUADRO;
    if (utile < SCORREVOLE_MIN)
        utile = SCORREVOLE_MIN;
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(l->scorrevole), utile);
}

static void area_cambiata(PhShell *sh, PhShellEvento ev, void *utente)
{
    (void)sh;
    (void)ev;
    misure_aggiorna(utente);
}

static gboolean filtro_corrisponde(gpointer elemento, gpointer dati)
{
    const PhRicerca *r = dati;
    PhLauncherVoce *v = PH_LAUNCHER_VOCE(elemento);
    return ph_launcher_corrisponde(v->norm, r->testo);
}

static void ricerca_libera(gpointer p)
{
    PhRicerca *r = p;
    g_free(r->testo);
    g_free(r);
}

static void campo_cambiato(GtkEditable *e, gpointer dati)
{
    PhLauncher *l = dati;
    if (l->distrutto)
        return;
    g_free(l->ricerca->testo);
    l->ricerca->testo = ph_launcher_normalizza(gtk_editable_get_text(e), true);
    gtk_filter_changed(GTK_FILTER(l->filtro), GTK_FILTER_CHANGE_DIFFERENT);
}

/* Invio: apre la prima app trovata (mockup onQueryKey). */
static void campo_invio(GtkEntry *e, gpointer dati)
{
    (void)e;
    PhLauncher *l = dati;
    if (l->distrutto)
        return;
    guint n = l->voci->len;
    const char **nomi = g_new0(const char *, n > 0 ? n : 1);
    for (guint i = 0; i < n; i++)
        nomi[i] = ((PhLauncherVoce *)g_ptr_array_index(l->voci, i))->norm;
    long i = ph_launcher_prima(nomi, n, l->ricerca->testo);
    g_free(nomi);
    if (i >= 0)
        apri_app(l, ((PhLauncherVoce *)g_ptr_array_index(l->voci, (guint)i))->pacchetto);
}

/* Clic (o Invio / Spazio su una cella con il fuoco) su un'app. */
static void griglia_attivata(GtkGridView *g, guint pos, gpointer dati)
{
    (void)g;
    PhLauncher *l = dati;
    if (l->distrutto)
        return;
    PhLauncherVoce *v = g_list_model_get_item(l->filtrato, pos);
    if (v == NULL)
        return;
    apri_app(l, v->pacchetto);
    g_object_unref(v);
}

/* Tasto "menu" o Maiusc+F10 su una cella con il fuoco: stesso menu del clic
 * destro (accessibilita': il menu raggiungibile anche senza mouse). */
static gboolean griglia_tasto(GtkEventControllerKey *k, guint tasto, guint codice,
                              GdkModifierType stato, gpointer dati)
{
    (void)k;
    (void)codice;
    PhLauncher *l = dati;
    bool voluto = tasto == GDK_KEY_Menu ||
                  (tasto == GDK_KEY_F10 && (stato & GDK_SHIFT_MASK) != 0);
    if (!voluto)
        return FALSE;
    GtkRoot *r = gtk_widget_get_root(l->griglia);
    GtkWidget *f = r != NULL ? gtk_root_get_focus(r) : NULL;
    for (GtkWidget *w = f; w != NULL && w != l->griglia; w = gtk_widget_get_parent(w)) {
        GtkWidget *figlio = gtk_widget_get_first_child(w);
        PhCella *c = figlio != NULL ? g_object_get_data(G_OBJECT(figlio), PH_LAUNCHER_CHIAVE_CELLA) : NULL;
        if (c != NULL && c->pacchetto != NULL) {
            ph_launcher_interno_menu_apri(l, c->cella, 24, 24, c->pacchetto);
            return TRUE;
        }
    }
    return FALSE;
}

/* ---- Avvisi dei servizi ------------------------------------------------- */
static void srv_elenco(PhServizi *s, PhSrvEvento ev, const char *arg, void *utente)
{
    (void)s;
    (void)ev;
    (void)arg;
    elenco_rileggi(utente);
}

static void srv_icona(PhServizi *s, PhSrvEvento ev, const char *pacchetto, void *utente)
{
    (void)s;
    (void)ev;
    PhLauncher *l = utente;
    if (pacchetto == NULL || l->celle == NULL)
        return;
    PhCella *c = g_hash_table_lookup(l->celle, pacchetto);
    if (c != NULL && c->texture == NULL)
        cella_icona(c);
}

/* ---- Apertura e chiusura ------------------------------------------------ */
/* Il launcher si e' chiuso (clic fuori, Esc, altro popup, clic su un'app): il
 * testo cercato si svuota (mockup closePopups) e il menu sparisce. */
static void popup_chiuso(PhShell *sh, PhShellPopup tipo, GtkWidget *w, void *utente)
{
    (void)sh;
    (void)tipo;
    (void)w;
    PhLauncher *l = utente;
    menu_chiudi(l, false);
    gtk_editable_set_text(GTK_EDITABLE(l->campo), "");
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(l->scorrevole));
    gtk_adjustment_set_value(adj, 0);
}

static void gestore_alterna(void *io)
{
    PhLauncher *l = io;
    if (l->distrutto)
        return;
    bool apre = !launcher_aperto(l);
    if (apre) {
        /* Elenco e misure sono quelli di adesso, non quelli dell'ultima volta. */
        elenco_rileggi(l);
        misure_aggiorna(l);
    }
    ph_shell_popup_alterna(l->sh, PH_POPUP_LAUNCHER, l->riquadro, popup_chiuso, l);
    if (apre)
        gtk_widget_grab_focus(l->campo); /* il fuoco va al campo */
}

static void radice_distrutta(GtkWidget *w, gpointer dati)
{
    PhLauncher *l = dati;
    if (l->distrutto)
        return;
    /* Pulizia: prima si smette di ascoltare (nessun avviso puo' piu' arrivare),
     * poi si rilascia. Arriva dopo che la radice ha staccato i figli: per questo
     * le celle e la fabbrica tengono ciascuna un riferimento allo stato. */
    l->distrutto = true;
    menu_togli_esc(l);
    g_signal_handlers_disconnect_by_data(l->filtrato, l);
    for (size_t i = 0; i < G_N_ELEMENTS(l->id_srv); i++)
        ph_srv_smetti(l->srv, l->id_srv[i]);
    /* Se la shell intera sta chiudendo (ph_shell_libera) gli ascoltatori e i
     * gestori sono gia' stati tolti e `sh` puo' non esserci piu'. */
    if (ph_shell_radice_viva(w)) {
        ph_shell_smetti(l->sh, l->id_area);
        ph_shell_smetti(l->sh, l->id_fissate);
        ph_shell_registra_launcher(l->sh, NULL);
    }
    launcher_unref(l);
}

void ph_launcher_installa(PhShell *sh)
{
    static gsize risorse = 0;
    if (g_once_init_enter(&risorse)) {
        ph_risorse_launcher_register_resource();
        g_once_init_leave(&risorse, 1);
    }
    ph_shell_stile_carica("/it/yasde/shell/launcher.css");

    PhLauncher *l = g_new0(PhLauncher, 1);
    l->rif = 1;
    l->sh = sh;
    l->srv = ph_shell_servizi(sh);
    l->voci = g_ptr_array_new_with_free_func(g_object_unref);
    l->celle = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    l->deposito = g_list_store_new(PH_TIPO_VOCE);
    l->ricerca = g_new0(PhRicerca, 1);
    l->ricerca->testo = g_strdup("");
    l->filtro = gtk_custom_filter_new(filtro_corrisponde, l->ricerca, ricerca_libera);
    l->filtrato = G_LIST_MODEL(gtk_filter_list_model_new(G_LIST_MODEL(g_object_ref(l->deposito)),
                                                         GTK_FILTER(g_object_ref(l->filtro))));

    /* Il riquadro: centrato in orizzontale, in alto a distanza fissa, alto
     * quanto il contenuto (misure_aggiorna dà larghezza, margine e massimo).
     * L'area attorno resta scoperta: il clic li' lo prende il velo dei popup
     * della shell (sotto di noi) e chiude il launcher. */
    l->riquadro = gtk_box_new(GTK_ORIENTATION_VERTICAL, SPAZIO_RIQUADRO);
    gtk_widget_add_css_class(l->riquadro, "ph-launcher");
    gtk_widget_set_halign(l->riquadro, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(l->riquadro, GTK_ALIGN_START);
    gtk_widget_set_overflow(l->riquadro, GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_visible(l->riquadro, FALSE);

    l->campo = gtk_entry_new();
    gtk_widget_add_css_class(l->campo, "ph-campo");
    gtk_widget_add_css_class(l->campo, "ph-ricerca");
    gtk_widget_set_halign(l->campo, GTK_ALIGN_CENTER);
    gtk_entry_set_placeholder_text(GTK_ENTRY(l->campo), ph_t("Cerca app (Invio apre la prima)"));
    ph_shell_etichetta(l->campo, ph_t("Cerca app"), NULL);
    collega(l->campo, "changed", G_CALLBACK(campo_cambiato), l);
    collega(l->campo, "activate", G_CALLBACK(campo_invio), l);
    gtk_box_append(GTK_BOX(l->riquadro), l->campo);

    GtkListItemFactory *fabbrica = gtk_signal_list_item_factory_new();
    collega(fabbrica, "setup", G_CALLBACK(fabbrica_setup), l);
    collega(fabbrica, "bind", G_CALLBACK(fabbrica_bind), l);
    collega(fabbrica, "unbind", G_CALLBACK(fabbrica_unbind), l);
    GtkNoSelection *sel = gtk_no_selection_new(g_object_ref(l->filtrato));
    l->griglia = gtk_grid_view_new(GTK_SELECTION_MODEL(sel), fabbrica);
    gtk_widget_add_css_class(l->griglia, "ph-launcher-griglia");
    gtk_grid_view_set_single_click_activate(GTK_GRID_VIEW(l->griglia), TRUE);
    collega(l->griglia, "activate", G_CALLBACK(griglia_attivata), l);
    GtkEventController *tasti = gtk_event_controller_key_new();
    collega(tasti, "key-pressed", G_CALLBACK(griglia_tasto), l);
    gtk_widget_add_controller(l->griglia, tasti);
    /* Senza riferimento (lo stato possiede il modello): si scollega alla distruzione. */
    g_signal_connect(l->filtrato, "items-changed", G_CALLBACK(filtrato_cambiato), l);

    l->scorrevole = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(l->scorrevole), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(l->scorrevole), l->griglia);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(l->scorrevole), TRUE);
    /* NIENTE halign CENTER sulla griglia: con un allineamento diverso da FILL GTK
     * misura la larghezza per l'altezza data (gtk_widget_allocate) e la griglia,
     * che come altezza minima dichiara tutte le righe, protesta ("Trying to
     * measure GtkGridView for height of ..., but it needs at least ...") appena
     * le app non ci stanno e deve scorrere. Si centra con i margini laterali. */
    gtk_box_append(GTK_BOX(l->riquadro), l->scorrevole);

    l->nessuna = gtk_label_new(ph_t("Nessuna app trovata"));
    gtk_widget_add_css_class(l->nessuna, "ph-launcher-nessuna");
    gtk_widget_set_halign(l->nessuna, GTK_ALIGN_FILL);
    gtk_widget_set_valign(l->nessuna, GTK_ALIGN_START);
    gtk_widget_set_visible(l->nessuna, FALSE);
    gtk_box_append(GTK_BOX(l->riquadro), l->nessuna);
    ph_shell_strato_aggiungi(sh, PH_STRATO_PANNELLI, l->riquadro);
    /* Vetro chiaro; raggio 18 come nel CSS. */
    ph_shell_vetro_registra(sh, l->riquadro, RAGGIO_RIQUADRO, PH_VETRO_CHIARO);

    /* Menu contestuale: sopra il launcher, con un velo trasparente sotto che
     * lo chiude al clic fuori senza chiudere il launcher. */
    l->velo_menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(l->velo_menu, "ph-velo-popup");
    gtk_widget_set_halign(l->velo_menu, GTK_ALIGN_FILL);
    gtk_widget_set_valign(l->velo_menu, GTK_ALIGN_FILL);
    gtk_widget_set_visible(l->velo_menu, FALSE);
    GtkGesture *clic = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(clic), 0);
    collega(clic, "pressed", G_CALLBACK(velo_menu_premuto), l);
    gtk_widget_add_controller(l->velo_menu, GTK_EVENT_CONTROLLER(clic));
    ph_shell_strato_aggiungi(sh, PH_STRATO_PANNELLI, l->velo_menu);

    l->menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(l->menu, "ph-menu");
    /* Vetro chiaro anche il menu (raggio 10 di ph-menu); il velo no: e' trasparente. */
    ph_shell_vetro_registra(sh, l->menu, RAGGIO_MENU, PH_VETRO_CHIARO);
    gtk_widget_set_halign(l->menu, GTK_ALIGN_START);
    gtk_widget_set_valign(l->menu, GTK_ALIGN_START);
    gtk_widget_set_visible(l->menu, FALSE);
    l->voce_apri = voce_menu_nuova(l, ph_t("Apri"), G_CALLBACK(voce_apri_cliccata));
    l->voce_aggiungi = voce_menu_nuova(l, ph_t("Aggiungi al desktop"),
                                       G_CALLBACK(voce_aggiungi_cliccata));
    l->voce_rimuovi = voce_menu_nuova(l, ph_t("Rimuovi dal desktop"),
                                      G_CALLBACK(voce_rimuovi_cliccata));
    gtk_box_append(GTK_BOX(l->menu), l->voce_apri);
    gtk_box_append(GTK_BOX(l->menu), l->voce_aggiungi);
    l->voce_fissa = voce_menu_nuova(l, ph_t("Fissa sulla barra"), G_CALLBACK(voce_fissa_cliccata));
    l->voce_togli = voce_menu_nuova(l, ph_t("Togli dalla barra"), G_CALLBACK(voce_togli_cliccata));
    gtk_box_append(GTK_BOX(l->menu), l->voce_rimuovi);
    gtk_box_append(GTK_BOX(l->menu), l->voce_fissa);
    gtk_box_append(GTK_BOX(l->menu), l->voce_togli);
    ph_shell_strato_aggiungi(sh, PH_STRATO_PANNELLI, l->menu);

    l->id_srv[0] = ph_srv_ascolta(l->srv, PH_SRV_EV_APP, srv_elenco, l);
    l->id_srv[1] = ph_srv_ascolta(l->srv, PH_SRV_EV_COLLEGAMENTO, srv_elenco, l);
    l->id_srv[2] = ph_srv_ascolta(l->srv, PH_SRV_EV_PROFILO, srv_elenco, l);
    l->id_srv[3] = ph_srv_ascolta(l->srv, PH_SRV_EV_ICONA, srv_icona, l);
    l->id_area = ph_shell_ascolta(sh, PH_SHELL_EV_AREA, area_cambiata, l);
    l->id_fissate = ph_shell_ascolta(sh, PH_SHELL_EV_FISSATE, fissate_cambiate, l);

    PhShellGestoreLauncher g = {.io = l, .alterna = gestore_alterna};
    ph_shell_registra_launcher(sh, &g);

    misure_aggiorna(l);
    elenco_rileggi(l);
    g_signal_connect(ph_shell_radice(sh), "destroy", G_CALLBACK(radice_distrutta), l);
}
