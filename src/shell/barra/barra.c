/* Pezzo shell-barra (F-03): la barra in basso (specifica 5.3, mockup righe
 * 192-227 di Main.dc.html).
 *
 * Da sinistra: launcher, separatore, una voce per finestra interna aperta,
 * spazio, invia / scarica (con il puntino dei trasferimenti), separatore, i tre
 * pulsanti del sistema (li crea il pezzo sistema), orologio.
 *
 * Vita delle voci: ogni cambio di finestre (PH_SHELL_EV_FINESTRE) rilegge
 * l'elenco da ph_shell_finestre e lo riconcilia con le voci esistenti, che si
 * aggiornano sul posto (mai ricostruite in blocco: un clic puo' far partire
 * l'avviso mentre il suo stesso pulsante sta gestendo il segnale). Una voce
 * non tiene mai un puntatore a una finestra: conosce solo l'id, che passa a
 * ph_shell_clic_barra; se la finestra e' gia' chiusa il gestore la ignora.
 * Tutto gira sul thread GTK e non fa lavoro pesante. */
#include <string.h>

#include "config/lingua.h"
#include "shell/barra/logica.h"
#include "shell/pezzi.h"

/* Generata da glib-compile-resources (--manual-register): il CSS del pezzo. */
void ph_risorse_barra_register_resource(void);

/* Quante finestre la barra sa mostrare; oltre, le ultime non compaiono (il
 * flusso video ha comunque un tetto ben piu' basso). */
#define BARRA_MAX_VOCI 64
#define LATO_TESSERA 26
#define RAGGIO_TESSERA 7

typedef struct Barra Barra;
struct Clic;

typedef struct {
    unsigned id;
    char pacchetto[PH_CFG_LUN_APP];
    char titolo[128];
    bool specchio;
    bool fissata;         /* sta fra le app fissate (la voce si riconosce dal pacchetto) */
    bool aperta;          /* ha una finestra (id != 0) */
    bool ha_stato;        /* stato, etichetta e tessera gia' impostati almeno una volta */
    bool minimizzata;     /* ultimo stato impostato */
    unsigned giro;        /* generazione dell'ultima riconciliazione che l'ha vista */
    GdkTexture *icona;    /* riferimento tenuto; NULL = segnaposto */
    GtkWidget *radice;    /* GtkOverlay: pulsante + indicatore */
    GtkWidget *bottone;
    GtkWidget *indicatore;
    struct Clic *clic;    /* dati del pulsante: vivono quanto il pulsante */
} Voce;

struct Barra {
    PhShell *sh;
    PhServizi *srv;
    GtkWidget *barra;
    GtkWidget *voci_box;
    GtkWidget *scorri;        /* finestra scorrevole delle voci */
    GtkWidget *fade_sx, *fade_dx; /* sfumature ai bordi: ci sono altre voci da quella parte */
    GPtrArray *voci;      /* Voce*, nell'ordine di ph_shell_finestre */
    unsigned giro;
    GtkWidget *bt_launcher, *bt_invio, *bt_scarica;
    GtkWidget *pt_invio, *pt_scarica;
    GtkWidget *orologio;      /* la scritta HH:MM dentro il pulsante */
    GtkWidget *bt_orologio;
    GtkWidget *pannello_cal;  /* popup del calendario del PC */
    GtkWidget *titolo_cal;    /* data per esteso, in alto */
    GtkWidget *lab_mese, *lab_anno;
    GtkWidget *nomi_giorni[7];
    GtkWidget *celle[42];     /* 6 settimane x 7 giorni, da lunedi' */
    int cal_anno, cal_mese;   /* mese mostrato */
    int oggi_anno, oggi_mese, oggi_giorno;
    guint ev_finestre, ev_popup, ev_host;
    guint ev_icona, ev_trasferimenti, ev_collegamento, ev_app, ev_profilo, ev_fissate;
    GtkWidget *menu;          /* menu del clic destro (popup PH_POPUP_MENU_BARRA) */
    unsigned menu_id;         /* bersaglio del menu: finestra (0 = non aperta) e pacchetto */
    char menu_pacchetto[PH_CFG_LUN_APP];
    guint timer_minuto;
    gulong id_destroy;    /* collegamento al "destroy" della radice */
};

/* Argomento del clic di una voce: copiato per valore, cosi' resta valido anche
 * se la voce viene tolta mentre il segnale e' in corso. */
typedef struct Clic {
    Barra *b;
    unsigned id;          /* finestra; 0 = fissata non aperta */
    char pacchetto[PH_CFG_LUN_APP];
    bool fissata, specchio;
} Clic;

/* ---- Voci delle finestre ------------------------------------------------ */

static void voce_libera(gpointer p)
{
    Voce *v = p;
    g_clear_object(&v->icona);
    g_free(v);
}

static void clic_libera(gpointer dati, GClosure *chiusura)
{
    (void)chiusura;
    g_free(dati);
}

static void clic_voce(GtkButton *bottone, gpointer dati)
{
    (void)bottone;
    const Clic *c = dati;
    /* Copie locali: il gestore puo' emettere subito PH_SHELL_EV_FINESTRE e la
     * riconciliazione puo' togliere questa stessa voce. */
    Barra *b = c->b;
    unsigned id = c->id;
    char pacchetto[PH_CFG_LUN_APP];
    g_strlcpy(pacchetto, c->pacchetto, sizeof pacchetto);
    if (id != 0)
        ph_shell_clic_barra(b->sh, id);
    else
        ph_shell_apri_app(b->sh, pacchetto); /* fissata non aperta: si apre */
}

static void menu_apri(Clic *c, GtkWidget *bottone);

/* Le sfumature si vedono solo se c'e' davvero da scorrere da quella parte. */
static void barra_fade(Barra *b)
{
    GtkAdjustment *a = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(b->scorri));
    double v = gtk_adjustment_get_value(a);
    double fine = gtk_adjustment_get_upper(a) - gtk_adjustment_get_page_size(a);
    gtk_widget_set_visible(b->fade_sx, v > 0.5);
    gtk_widget_set_visible(b->fade_dx, v < fine - 0.5);
}

static void fade_cambiato(GtkAdjustment *a, gpointer dati)
{
    (void)a;
    barra_fade(dati);
}

static gboolean rotella_voci(GtkEventControllerScroll *k, double dx, double dy, gpointer dati)
{
    (void)dx;
    GtkAdjustment *a = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(dati));
    /* Rotella a scatti: dy e' in "scatti" (circa una voce l'uno); touchpad:
     * dy e' gia' in pixel. */
    double passo = gtk_event_controller_scroll_get_unit(k) == GDK_SCROLL_UNIT_WHEEL ? 40.0 : 1.0;
    gtk_adjustment_set_value(a, gtk_adjustment_get_value(a) + dy * passo);
    return TRUE;
}

/* Clic destro sull'icona. */
static void destro_voce(GtkGestureClick *g, int n, double x, double y, gpointer dati)
{
    (void)n;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
    menu_apri(dati, gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g)));
}

/* Tasto Menu o Maiusc+F10 con il fuoco sull'icona. */
static gboolean tasto_voce(GtkEventControllerKey *k, guint val, guint code, GdkModifierType st,
                           gpointer dati)
{
    (void)code;
    bool maiusc_f10 = val == GDK_KEY_F10 && (st & GDK_SHIFT_MASK) != 0;
    if (val != GDK_KEY_Menu && !maiusc_f10)
        return FALSE;
    menu_apri(dati, gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(k)));
    return TRUE;
}

/* Il quadratino 26x26: specchio "Tl", icona del telefono, o segnaposto con le
 * iniziali. */
static GtkWidget *tessera_nuova(const Voce *v)
{
    if (v->specchio) {
        GtkWidget *l = gtk_label_new("Tl");
        gtk_widget_add_css_class(l, "ph-barra-specchio");
        gtk_widget_set_size_request(l, LATO_TESSERA, LATO_TESSERA);
        gtk_widget_set_halign(l, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(l, GTK_ALIGN_CENTER);
        return l;
    }
    if (v->icona != NULL) {
        /* Il riquadro ritaglia l'immagine sul raggio; l'immagine e' decorativa:
         * il nome lo da' l'etichetta del pulsante. */
        GtkWidget *quadro = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(quadro, "ph-barra-tessera");
        gtk_widget_set_overflow(quadro, GTK_OVERFLOW_HIDDEN);
        gtk_widget_set_size_request(quadro, LATO_TESSERA, LATO_TESSERA);
        gtk_widget_set_halign(quadro, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(quadro, GTK_ALIGN_CENTER);
        /* La texture e' di 128 px: con pixel_size la misura naturale resta 26
         * (un GtkPicture prenderebbe quella della texture e gonfierebbe la barra). */
        GtkWidget *img = gtk_image_new_from_paintable(GDK_PAINTABLE(v->icona));
        gtk_image_set_pixel_size(GTK_IMAGE(img), LATO_TESSERA);
        gtk_box_append(GTK_BOX(quadro), img);
        return quadro;
    }
    GtkWidget *s = ph_shell_segnaposto_nuovo(v->pacchetto, v->titolo, LATO_TESSERA, RAGGIO_TESSERA);
    gtk_widget_set_halign(s, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(s, GTK_ALIGN_CENTER);
    return s;
}

static void voce_ricostruisci_tessera(Voce *v)
{
    gtk_button_set_child(GTK_BUTTON(v->bottone), tessera_nuova(v));
}

static Voce *voce_nuova(Barra *b, const PhShellFinestra *f)
{
    Voce *v = g_new0(Voce, 1);
    v->id = f->id;
    v->specchio = f->specchio;
    g_strlcpy(v->pacchetto, f->pacchetto, sizeof v->pacchetto);
    g_strlcpy(v->titolo, f->titolo, sizeof v->titolo);

    v->bottone = gtk_button_new();
    gtk_widget_add_css_class(v->bottone, "ph-tb");
    voce_ricostruisci_tessera(v);
    gtk_widget_add_css_class(v->bottone, "ph-barra-voce-bottone");
    Clic *c = g_new0(Clic, 1);
    c->b = b;
    v->clic = c;
    g_signal_connect_data(v->bottone, "clicked", G_CALLBACK(clic_voce), c, clic_libera, 0);
    GtkGesture *destro = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(destro), GDK_BUTTON_SECONDARY);
    g_signal_connect(destro, "pressed", G_CALLBACK(destro_voce), c);
    gtk_widget_add_controller(v->bottone, GTK_EVENT_CONTROLLER(destro));
    GtkEventController *tasti = gtk_event_controller_key_new();
    g_signal_connect(tasti, "key-pressed", G_CALLBACK(tasto_voce), c);
    gtk_widget_add_controller(v->bottone, tasti);

    /* L'indicatore sta a 2 px dal fondo del pulsante, al centro: un overlay
     * che ha il pulsante come figlio principale ne ha la stessa misura. */
    v->indicatore = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(v->indicatore, "ph-barra-indicatore");
    gtk_widget_set_halign(v->indicatore, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(v->indicatore, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(v->indicatore, 2);
    gtk_widget_set_can_target(v->indicatore, FALSE);

    v->radice = gtk_overlay_new();
    gtk_widget_set_valign(v->radice, GTK_ALIGN_CENTER);
    gtk_overlay_set_child(GTK_OVERLAY(v->radice), v->bottone);
    gtk_overlay_add_overlay(GTK_OVERLAY(v->radice), v->indicatore);
    return v;
}

static const char *classe_indicatore(PhBarraIndicatore i)
{
    switch (i) {
    case PH_BARRA_IND_ATTIVA: return "ph-barra-ind-attiva";
    case PH_BARRA_IND_MINIMIZZATA: return "ph-barra-ind-min";
    default: return "ph-barra-ind-aperta";
    }
}

/* L'icona vera, se e' arrivata. Una texture sparita dalla cache non fa tornare
 * al segnaposto: si tiene quella che c'e' (niente sfarfallio). */
static void voce_aggiorna_icona(Barra *b, Voce *v)
{
    if (v->specchio)
        return;
    GdkTexture *t = ph_srv_icona(b->srv, v->pacchetto);
    if (t == NULL || t == v->icona)
        return;
    g_set_object(&v->icona, t);
    voce_ricostruisci_tessera(v);
}

static void voce_aggiorna(Barra *b, Voce *v, const PhShellFinestra *f, bool fissata)
{
    bool aperta = f->id != 0;
    bool titolo_nuovo = strcmp(v->titolo, f->titolo) != 0;
    if (titolo_nuovo) {
        g_strlcpy(v->titolo, f->titolo, sizeof v->titolo);
        if (v->icona == NULL && !v->specchio)
            voce_ricostruisci_tessera(v); /* le iniziali del segnaposto */
    }
    voce_aggiorna_icona(b, v);

    /* Dati del clic e del menu: la finestra puo' aprirsi o chiudersi senza che la
     * voce (una fissata) cambi. */
    v->id = f->id;
    v->clic->id = f->id;
    v->clic->fissata = fissata;
    v->clic->specchio = f->specchio;
    g_strlcpy(v->clic->pacchetto, f->pacchetto, sizeof v->clic->pacchetto);

    /* Fissata non aperta: nessun indicatore. */
    PhBarraIndicatore ind = aperta ? ph_barra_indicatore(f) : PH_BARRA_IND_APERTA;
    gtk_widget_remove_css_class(v->indicatore, "ph-barra-ind-aperta");
    gtk_widget_remove_css_class(v->indicatore, "ph-barra-ind-attiva");
    gtk_widget_remove_css_class(v->indicatore, "ph-barra-ind-min");
    if (aperta)
        gtk_widget_add_css_class(v->indicatore, classe_indicatore(ind));
    if (ind == PH_BARRA_IND_ATTIVA)
        gtk_widget_add_css_class(v->bottone, "ph-barra-attiva");
    else
        gtk_widget_remove_css_class(v->bottone, "ph-barra-attiva");
    if (ind == PH_BARRA_IND_MINIMIZZATA)
        gtk_widget_add_css_class(v->radice, "ph-barra-voce-min");
    else
        gtk_widget_remove_css_class(v->radice, "ph-barra-voce-min");

    /* Etichetta e suggerimento cambiano solo con lo stato o con il nome: a ogni
     * avviso di finestre non si riscrivono le proprieta' accessibili. */
    if (!v->ha_stato || titolo_nuovo || v->minimizzata != f->minimizzata || v->aperta != aperta) {
        char etichetta[256], suggerimento[320];
        if (aperta) {
            ph_barra_etichetta(f, etichetta, sizeof etichetta);
            ph_barra_suggerimento(f, suggerimento, sizeof suggerimento);
        } else {
            ph_barra_etichetta_chiusa(f->titolo, etichetta, sizeof etichetta);
            ph_barra_suggerimento_chiusa(f->titolo, suggerimento, sizeof suggerimento);
        }
        ph_shell_etichetta(v->bottone, etichetta, suggerimento);
    }
    v->aperta = aperta;
    v->fissata = fissata;
    v->minimizzata = f->minimizzata;
    v->ha_stato = true;
}

static void menu_verifica(Barra *b);

/* Le fissate si riconoscono dal pacchetto (la voce resta la stessa quando la
 * finestra si apre o si chiude); le altre dall'id della finestra. */
static Voce *voce_cerca(Barra *b, bool fissata, const char *pacchetto, unsigned id)
{
    for (guint i = 0; i < b->voci->len; i++) {
        Voce *v = g_ptr_array_index(b->voci, i);
        if (v->fissata != fissata)
            continue;
        if (fissata ? strcmp(v->pacchetto, pacchetto) == 0 : v->id == id)
            return v;
    }
    return NULL;
}

/* Rilegge fissate e finestre e riconcilia: aggiorna le voci che restano, crea le
 * nuove, toglie quelle sparite, rimette l'ordine (fissate, poi altre aperte). */
static void barra_aggiorna_voci(Barra *b)
{
    PhShellFinestra elenco[BARRA_MAX_VOCI];
    PhBarraElemento el[BARRA_MAX_VOCI];
    Voce *ordine[BARRA_MAX_VOCI];
    size_t n = ph_shell_finestre(b->sh, elenco, BARRA_MAX_VOCI);
    if (n > BARRA_MAX_VOCI)
        n = BARRA_MAX_VOCI;
    const PhCfgTelefono *prof = ph_srv_profilo(b->srv);
    size_t nf = prof->n_fissate < PH_CFG_MAX_FISSATE ? prof->n_fissate : PH_CFG_MAX_FISSATE;
    size_t ne = ph_barra_ordina(prof->fissate, nf, elenco, n, el, BARRA_MAX_VOCI);

    b->giro++;
    size_t k = 0;
    for (size_t i = 0; i < ne; i++) {
        PhShellFinestra f;
        bool fissata = el[i].fissata >= 0;
        if (el[i].finestra >= 0) {
            f = elenco[el[i].finestra];
        } else {
            /* Fissata non aperta: nome dall'elenco delle app, altrimenti il pacchetto. */
            memset(&f, 0, sizeof f);
            g_strlcpy(f.pacchetto, prof->fissate[el[i].fissata], sizeof f.pacchetto);
            const PhSrvApp *app = ph_srv_app_cerca(b->srv, f.pacchetto);
            g_strlcpy(f.titolo, app != NULL && app->nome != NULL ? app->nome : f.pacchetto,
                      sizeof f.titolo);
        }
        Voce *v = voce_cerca(b, fissata, f.pacchetto, f.id);
        if (v != NULL && v->giro == b->giro)
            continue; /* id ripetuto dal gestore: la prima vale */
        if (v == NULL) {
            v = voce_nuova(b, &f);
            v->fissata = fissata;
            g_ptr_array_add(b->voci, v);
            gtk_box_append(GTK_BOX(b->voci_box), v->radice);
        }
        v->giro = b->giro;
        voce_aggiorna(b, v, &f, fissata);
        ordine[k++] = v;
    }

    /* Finestre chiuse o app tolte: via la voce (il pulsante si distrugge, la
     * struttura si libera; nessuno la tiene). Dal fondo, per non spostare gli
     * indici. */
    for (guint i = b->voci->len; i > 0; i--) {
        Voce *v = g_ptr_array_index(b->voci, i - 1);
        if (v->giro == b->giro)
            continue;
        gtk_box_remove(GTK_BOX(b->voci_box), v->radice);
        g_ptr_array_remove_index(b->voci, i - 1);
    }

    GtkWidget *prima = NULL;
    for (size_t i = 0; i < k; i++) {
        gtk_box_reorder_child_after(GTK_BOX(b->voci_box), ordine[i]->radice, prima);
        prima = ordine[i]->radice;
    }
    menu_verifica(b);
}

static void avviso_finestre(PhShell *sh, PhShellEvento ev, void *utente)
{
    (void)sh;
    (void)ev;
    barra_aggiorna_voci(utente);
}

static void avviso_icona(PhServizi *s, PhSrvEvento ev, const char *pacchetto, void *utente)
{
    (void)s;
    (void)ev;
    Barra *b = utente;
    if (pacchetto == NULL)
        return;
    for (guint i = 0; i < b->voci->len; i++) {
        Voce *v = g_ptr_array_index(b->voci, i);
        if (strcmp(v->pacchetto, pacchetto) == 0)
            voce_aggiorna_icona(b, v);
    }
}

/* Le icone si chiedono solo a telefono collegato (servizi.c): una voce nata
 * prima del collegamento le richiede qui, appena il telefono c'e'. */
static void avviso_collegamento(PhServizi *s, PhSrvEvento ev, const char *argomento, void *utente)
{
    (void)s;
    (void)ev;
    (void)argomento;
    Barra *b = utente;
    for (guint i = 0; i < b->voci->len; i++)
        voce_aggiorna_icona(b, g_ptr_array_index(b->voci, i));
}

/* ---- App fissate ---------------------------------------------------------- */

/* L'elenco sta nel profilo del telefono e lo tocca solo la barra: dopo ogni
 * cambio si salva (in modo differito) e si avvisa chi ascolta; la barra stessa
 * si aggiorna dal suo ascolto di PH_SHELL_EV_FISSATE. */
static void fissata_cambia(Barra *b, const char *pacchetto, bool fissa)
{
    PhCfgTelefono *prof = ph_srv_profilo(b->srv);
    bool cambiato = false;
    if (fissa) {
        PhErrore e;
        memset(&e, 0, sizeof e);
        bool c_era = ph_cfg_telefono_app_fissata(prof, pacchetto);
        if (ph_cfg_telefono_fissa_app(prof, pacchetto, &e) != 0) {
            /* Il profilo non e' cambiato. L'avviso solo se l'elenco e' davvero pieno;
             * un nome non valido finisce nel registro. */
            if (prof->n_fissate >= PH_CFG_MAX_FISSATE)
                ph_shell_avviso(b->sh, ph_t("Barra piena"), ph_t("Togli un'app per fare spazio"));
            else
                ph_shell_decisione("app non fissata", "nome del pacchetto non valido");
            return;
        }
        cambiato = !c_era;
    } else {
        cambiato = ph_cfg_telefono_togli_app(prof, pacchetto) != 0;
    }
    if (!cambiato)
        return;
    ph_srv_profilo_salva(b->srv);
    ph_shell_emetti(b->sh, PH_SHELL_EV_FISSATE);
}

static bool gestore_fissata(void *io, const char *pacchetto)
{
    Barra *b = io;
    return ph_cfg_telefono_app_fissata(ph_srv_profilo(b->srv), pacchetto);
}

static void gestore_fissa(void *io, const char *pacchetto, bool fissa)
{
    fissata_cambia(io, pacchetto, fissa);
}

/* Cambia l'elenco (o arriva il profilo di un altro telefono): si ricostruiscono
 * le voci. Il profilo nuovo si annuncia anche agli altri pezzi. */
static void avviso_fissate(PhShell *sh, PhShellEvento ev, void *utente)
{
    (void)ev;
    Barra *b = utente;
    /* Il menu aperto riguardava lo stato di prima: si chiude. */
    if (ph_shell_popup_aperto(sh) == PH_POPUP_MENU_BARRA)
        ph_shell_popup_chiudi(sh);
    barra_aggiorna_voci(b);
}

static void avviso_profilo(PhServizi *s, PhSrvEvento ev, const char *argomento, void *utente)
{
    (void)s;
    (void)ev;
    (void)argomento;
    Barra *b = utente;
    /* Un altro telefono: il menu aperto non vale piu' (il profilo e' cambiato). */
    if (ph_shell_popup_aperto(b->sh) == PH_POPUP_MENU_BARRA)
        ph_shell_popup_chiudi(b->sh);
    ph_shell_emetti(b->sh, PH_SHELL_EV_FISSATE);
}

static void avviso_app(PhServizi *s, PhSrvEvento ev, const char *argomento, void *utente)
{
    (void)s;
    (void)ev;
    (void)argomento;
    barra_aggiorna_voci(utente); /* i nomi delle fissate non aperte */
}

/* ---- Menu del clic destro ------------------------------------------------ */

static void menu_chiuso(PhShell *sh, PhShellPopup tipo, GtkWidget *w, void *utente)
{
    (void)sh;
    (void)tipo;
    (void)utente;
    gtk_widget_set_visible(w, FALSE);
}

static void menu_scelta(GtkButton *bt, gpointer dati)
{
    (void)bt;
    Barra *b = dati;
    PhBarraMenuVoce voce = (PhBarraMenuVoce)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(bt), "voce"));
    /* Bersaglio copiato prima di chiudere il popup. Se la finestra si e' chiusa
     * nel frattempo il gestore ignora l'id. */
    unsigned id = b->menu_id;
    char pacchetto[PH_CFG_LUN_APP];
    g_strlcpy(pacchetto, b->menu_pacchetto, sizeof pacchetto);
    ph_shell_popup_chiudi(b->sh);
    switch (voce) {
    case PH_BARRA_MENU_CHIUDI: ph_shell_chiudi_finestra(b->sh, id); break;
    case PH_BARRA_MENU_FISSA: fissata_cambia(b, pacchetto, true); break;
    case PH_BARRA_MENU_TOGLI: fissata_cambia(b, pacchetto, false); break;
    }
}

/* Se il bersaglio del menu non c'e' piu' (finestra chiusa, app tolta) si chiude. */
static void menu_verifica(Barra *b)
{
    if (ph_shell_popup_aperto(b->sh) != PH_POPUP_MENU_BARRA)
        return;
    for (guint i = 0; i < b->voci->len; i++) {
        Voce *v = g_ptr_array_index(b->voci, i);
        if (v->id == b->menu_id && strcmp(v->pacchetto, b->menu_pacchetto) == 0)
            return;
    }
    ph_shell_popup_chiudi(b->sh);
}

/* Apre il menu sopra l'icona. Lo specchio dello schermo non e' un'app: niente
 * menu. */
static void menu_apri(Clic *c, GtkWidget *bottone)
{
    Barra *b = c->b;
    if (c->specchio || c->pacchetto[0] == '\0')
        return;
    PhBarraMenuVoce voci[2];
    size_t n = ph_barra_menu(c->id != 0, c->fissata, voci);

    GtkWidget *riga;
    while ((riga = gtk_widget_get_first_child(b->menu)) != NULL)
        gtk_box_remove(GTK_BOX(b->menu), riga);
    for (size_t i = 0; i < n; i++) {
        GtkWidget *bt = g_object_new(GTK_TYPE_BUTTON, "accessible-role",
                                     GTK_ACCESSIBLE_ROLE_MENU_ITEM, NULL);
        gtk_widget_add_css_class(bt, "ph-riga");
        GtkWidget *l = gtk_label_new(ph_barra_menu_testo(voci[i]));
        gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
        gtk_button_set_child(GTK_BUTTON(bt), l);
        g_object_set_data(G_OBJECT(bt), "voce", GINT_TO_POINTER((int)voci[i]));
        g_signal_connect(bt, "clicked", G_CALLBACK(menu_scelta), b);
        gtk_box_append(GTK_BOX(b->menu), bt);
    }
    b->menu_id = c->id;
    g_strlcpy(b->menu_pacchetto, c->pacchetto, sizeof b->menu_pacchetto);

    /* Sopra l'icona: a sinistra all'altezza del pulsante, sopra la barra. */
    graphene_point_t p;
    if (!gtk_widget_compute_point(bottone, ph_shell_radice(b->sh), &GRAPHENE_POINT_INIT(0, 0), &p))
        p.x = 0;
    gtk_widget_set_margin_start(b->menu, p.x > 0 ? (int)p.x : 0);
    ph_shell_popup_apri(b->sh, PH_POPUP_MENU_BARRA, b->menu, menu_chiuso, b);
}

/* ---- Pulsanti fissi ----------------------------------------------------- */

/* Pulsante con icona, dentro un overlay (per il puntino). *bottone riceve il
 * pulsante vero. */
static GtkWidget *pulsante_icona_nuovo(const PhShellIcona *icona, const char *etichetta,
                                       const char *suggerimento, GtkWidget **bottone)
{
    GtkWidget *bt = gtk_button_new();
    gtk_widget_add_css_class(bt, "ph-tb");
    gtk_button_set_child(GTK_BUTTON(bt), ph_shell_icona_nuova(icona));
    ph_shell_etichetta(bt, etichetta, suggerimento);
    ph_shell_espanso(bt, false);
    *bottone = bt;
    return bt;
}

static GtkWidget *puntino_nuovo(void)
{
    GtkWidget *p = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(p, "ph-barra-puntino");
    gtk_widget_set_halign(p, GTK_ALIGN_END);
    gtk_widget_set_valign(p, GTK_ALIGN_START);
    gtk_widget_set_margin_top(p, 6);
    gtk_widget_set_margin_end(p, 6);
    gtk_widget_set_can_target(p, FALSE);
    gtk_widget_set_visible(p, FALSE);
    return p;
}

static GtkWidget *separatore_nuovo(void)
{
    GtkWidget *s = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(s, "ph-separatore");
    gtk_widget_set_valign(s, GTK_ALIGN_CENTER);
    return s;
}

static void clic_launcher(GtkButton *bt, gpointer dati)
{
    (void)bt;
    Barra *b = dati;
    ph_shell_launcher_alterna(b->sh);
}

static void clic_invio(GtkButton *bt, gpointer dati)
{
    (void)bt;
    Barra *b = dati;
    ph_shell_trasferimento_apri(b->sh, PH_SRV_INVIO);
}

static void clic_scarica(GtkButton *bt, gpointer dati)
{
    (void)bt;
    Barra *b = dati;
    ph_shell_trasferimento_apri(b->sh, PH_SRV_RICEZIONE);
}

static bool calendario_aperto(Barra *b);
static void calendario_ridisegna(Barra *b);

/* aria-expanded segue il popup aperto. */
static void barra_aggiorna_popup(Barra *b)
{
    PhShellPopup p = ph_shell_popup_aperto(b->sh);
    ph_shell_espanso(b->bt_launcher, p == PH_POPUP_LAUNCHER);
    ph_shell_espanso(b->bt_invio, p == PH_POPUP_INVIO);
    ph_shell_espanso(b->bt_scarica, p == PH_POPUP_RICEZIONE);
    ph_shell_espanso(b->bt_orologio, calendario_aperto(b));
}

static void avviso_popup(PhShell *sh, PhShellEvento ev, void *utente)
{
    (void)sh;
    (void)ev;
    barra_aggiorna_popup(utente);
}

/* Puntino: la regola e' quella di servizi.h (in attesa o in corso, coda non
 * ferma). */
static void barra_aggiorna_puntini(Barra *b)
{
    gtk_widget_set_visible(b->pt_invio, ph_srv_trasferimenti_attivi(b->srv, PH_SRV_INVIO));
    gtk_widget_set_visible(b->pt_scarica, ph_srv_trasferimenti_attivi(b->srv, PH_SRV_RICEZIONE));
}

static void avviso_trasferimenti(PhServizi *s, PhSrvEvento ev, const char *argomento, void *utente)
{
    (void)s;
    (void)ev;
    (void)argomento;
    barra_aggiorna_puntini(utente);
}

/* ---- Orologio ----------------------------------------------------------- */

static void orologio_aggiorna(Barra *b);

/* Data per esteso nella lingua dell'app (non quella del sistema). */
static void data_testo(GDateTime *ora, char *out, size_t capacita)
{
    ph_barra_data_lunga(g_date_time_get_day_of_week(ora), g_date_time_get_day_of_month(ora),
                        g_date_time_get_month(ora), g_date_time_get_year(ora), out, capacita);
}

static gboolean orologio_scatta(gpointer dati)
{
    Barra *b = dati;
    b->timer_minuto = 0; /* questa sorgente sta per sparire */
    orologio_aggiorna(b);
    return G_SOURCE_REMOVE;
}

/* Scrive l'ora e prepara lo scatto al prossimo minuto esatto (non un
 * intervallo fisso: cosi' il cambio e' puntuale e non ritarda fino a 30 s).
 * Il timer e' ricalcolato a ogni scatto sull'ora attuale, quindi un orologio
 * corretto o uno scatto un po' in ritardo si riassestano da soli. Durante la
 * sospensione del PC il timer si ferma: al ritorno della finestra host
 * (PH_SHELL_EV_HOST) l'ora si ridisegna da qui. */
static void orologio_aggiorna(Barra *b)
{
    GDateTime *ora = g_date_time_new_now_local();
    if (ora == NULL)
        return;
    char testo[8];
    ph_barra_ora_testo(g_date_time_get_hour(ora), g_date_time_get_minute(ora), testo,
                       sizeof testo);
    unsigned ms = ph_barra_ms_al_minuto(g_date_time_get_second(ora), g_date_time_get_microsecond(ora));
        char data[128], etichetta[200];
    data_testo(ora, data, sizeof data);
    ph_barra_etichetta_orologio(testo, data, etichetta, sizeof etichetta);
    /* Anno, mese e giorno si leggono PRIMA di rilasciare l'ora: dopo non si
     * tocca piu'. */
    int y = g_date_time_get_year(ora), mo = g_date_time_get_month(ora),
        gi = g_date_time_get_day_of_month(ora);
    g_date_time_unref(ora);
    /* Mezzanotte con il calendario aperto: oggi e la data in alto passano al
     * giorno nuovo; il mese mostrato (magari sfogliato dall'utente) non cambia. */
    if (calendario_aperto(b)) {
        if (y != b->oggi_anno || mo != b->oggi_mese || gi != b->oggi_giorno) {
            b->oggi_anno = y;
            b->oggi_mese = mo;
            b->oggi_giorno = gi;
            gtk_label_set_text(GTK_LABEL(b->titolo_cal), data);
            calendario_ridisegna(b);
        }
    }
    gtk_label_set_text(GTK_LABEL(b->orologio), testo);
    ph_shell_etichetta(b->bt_orologio, etichetta, data);
    if (b->timer_minuto != 0)
        g_source_remove(b->timer_minuto);
    /* +20 ms: si atterra subito dopo il cambio, non un soffio prima. */
    b->timer_minuto = g_timeout_add(ms + 20, orologio_scatta, b);
}

/* ---- Calendario del PC --------------------------------------------------- */

static void calendario_chiuso(PhShell *sh, PhShellPopup tipo, GtkWidget *w, void *utente)
{
    (void)sh;
    (void)tipo;
    (void)utente;
    gtk_widget_set_visible(w, FALSE);
}

static bool calendario_aperto(Barra *b)
{
    return ph_shell_popup_aperto(b->sh) == PH_POPUP_CALENDARIO &&
           gtk_widget_get_visible(b->pannello_cal);
}

/* Disegna il mese mostrato: nomi e giorni nella lingua dell'app, settimana da
 * lunedi'; il giorno di oggi in evidenza. */
static void calendario_ridisegna(Barra *b)
{
    int col = 0, giorni = 0;
    ph_barra_mese(b->cal_anno, b->cal_mese, &col, &giorni);
    gtk_label_set_text(GTK_LABEL(b->lab_mese), ph_barra_nome_mese(b->cal_mese));
    char anno[16];
    snprintf(anno, sizeof anno, "%d", b->cal_anno);
    gtk_label_set_text(GTK_LABEL(b->lab_anno), anno);
    for (int i = 0; i < 7; i++)
        gtk_label_set_text(GTK_LABEL(b->nomi_giorni[i]), ph_barra_giorno_breve(i));
    bool mese_di_oggi = b->cal_anno == b->oggi_anno && b->cal_mese == b->oggi_mese;
    for (int i = 0; i < 42; i++) {
        int g = i - col + 1;
        char t[16] = "";
        if (g >= 1 && g <= giorni)
            snprintf(t, sizeof t, "%d", g);
        gtk_label_set_text(GTK_LABEL(b->celle[i]), t);
        if (mese_di_oggi && g == b->oggi_giorno)
            gtk_widget_add_css_class(b->celle[i], "ph-barra-giorno-oggi");
        else
            gtk_widget_remove_css_class(b->celle[i], "ph-barra-giorno-oggi");
    }
}

/* Frecce: il valore e' lo spostamento in mesi (anno = 12 mesi). */
static void clic_freccia(GtkButton *bt, gpointer dati)
{
    Barra *b = dati;
    int delta = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(bt), "delta"));
    ph_barra_mese_sposta(&b->cal_anno, &b->cal_mese, delta);
    calendario_ridisegna(b);
}

static GtkWidget *freccia_nuova(Barra *b, const char *simbolo, int delta, const char *etichetta)
{
    GtkWidget *bt = gtk_button_new_with_label(simbolo);
    gtk_widget_add_css_class(bt, "ph-barra-freccia");
    g_object_set_data(G_OBJECT(bt), "delta", GINT_TO_POINTER(delta));
    ph_shell_etichetta(bt, etichetta, etichetta);
    g_signal_connect(bt, "clicked", G_CALLBACK(clic_freccia), b);
    return bt;
}

/* Clic sull'orologio: apre (sul mese di oggi) o, se e' gia' aperto, chiude. */
static void clic_orologio(GtkButton *bt, gpointer dati)
{
    (void)bt;
    Barra *b = dati;
    if (!calendario_aperto(b)) {
        GDateTime *ora = g_date_time_new_now_local();
        if (ora != NULL) {
            b->oggi_anno = g_date_time_get_year(ora);
            b->oggi_mese = g_date_time_get_month(ora);
            b->oggi_giorno = g_date_time_get_day_of_month(ora);
            b->cal_anno = b->oggi_anno;
            b->cal_mese = b->oggi_mese;
            char data[128];
            data_testo(ora, data, sizeof data);
            gtk_label_set_text(GTK_LABEL(b->titolo_cal), data);
            g_date_time_unref(ora);
            calendario_ridisegna(b);
        }
    }
    ph_shell_popup_alterna(b->sh, PH_POPUP_CALENDARIO, b->pannello_cal, calendario_chiuso, b);
}

static void avviso_host(PhShell *sh, PhShellEvento ev, void *utente)
{
    (void)ev;
    if (ph_shell_host_visibile(sh))
        orologio_aggiorna(utente);
}

/* ---- Vita --------------------------------------------------------------- */

/* La radice della shell viene distrutta. Gli strati staccano i figli PRIMA di
 * questo avviso: senza il riferimento tenuto in ph_barra_installa i widget
 * sarebbero gia' finalizzati mentre gli ascolti sono ancora attivi. Ordine:
 * smetti di ascoltare, togli il timer, lascia la barra, libera le voci. */
static void barra_distrutta(GtkWidget *radice, gpointer dati)
{
    Barra *b = dati;
    /* Una volta sola, anche se il segnale tornasse. */
    if (b->id_destroy != 0)
        g_signal_handler_disconnect(radice, b->id_destroy);
    b->id_destroy = 0;
    /* Le *_smetti sono sicure anche dopo la liberazione (registro degli oggetti
     * vivi); si azzerano gli id per non ripeterle. */
    if (b->ev_finestre != 0)
        ph_shell_smetti(b->sh, b->ev_finestre);
    if (b->ev_popup != 0)
        ph_shell_smetti(b->sh, b->ev_popup);
    if (b->ev_host != 0)
        ph_shell_smetti(b->sh, b->ev_host);
    if (b->ev_fissate != 0)
        ph_shell_smetti(b->sh, b->ev_fissate);
    if (b->ev_icona != 0)
        ph_srv_smetti(b->srv, b->ev_icona);
    if (b->ev_trasferimenti != 0)
        ph_srv_smetti(b->srv, b->ev_trasferimenti);
    if (b->ev_collegamento != 0)
        ph_srv_smetti(b->srv, b->ev_collegamento);
    if (b->ev_app != 0)
        ph_srv_smetti(b->srv, b->ev_app);
    if (b->ev_profilo != 0)
        ph_srv_smetti(b->srv, b->ev_profilo);
    b->ev_finestre = b->ev_popup = b->ev_host = b->ev_fissate = 0;
    b->ev_icona = b->ev_trasferimenti = b->ev_collegamento = b->ev_app = b->ev_profilo = 0;
    /* ph_shell_registra_* non e' coperta dal registro: solo con la radice viva
     * (durante il "destroy" di ph_shell_libera e' falsa e la shell sta per
     * sparire con i suoi gestori). */
    if (ph_shell_radice_viva(radice))
        ph_shell_registra_fissate(b->sh, NULL);
    if (b->timer_minuto != 0) {
        g_source_remove(b->timer_minuto);
        b->timer_minuto = 0;
    }
    /* Nessun puntatore a shell o servizi si usa da qui in poi. */
    g_object_unref(b->barra);
    g_object_unref(b->pannello_cal);
    g_object_unref(b->menu);
    g_ptr_array_free(b->voci, TRUE);
    g_free(b);
}

void ph_barra_installa(PhShell *sh)
{
    static gsize risorse = 0;
    if (g_once_init_enter(&risorse)) {
        ph_risorse_barra_register_resource();
        g_once_init_leave(&risorse, 1);
    }
    ph_shell_stile_carica("/it/yasde/shell/barra.css");

    Barra *b = g_new0(Barra, 1);
    b->sh = sh;
    b->srv = ph_shell_servizi(sh);
    b->voci = g_ptr_array_new_with_free_func(voce_libera);

    b->barra = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(b->barra, "ph-barra-radice");
    gtk_widget_set_size_request(b->barra, -1, 48);
    gtk_widget_set_halign(b->barra, GTK_ALIGN_FILL);
    gtk_widget_set_valign(b->barra, GTK_ALIGN_END);
    ph_shell_vetro_registra(sh, b->barra, 0, PH_VETRO_SCURO);

    /* 1. Launcher */
    pulsante_icona_nuovo(&PH_ICONA_LAUNCHER, ph_t("Launcher delle app"), ph_t("Launcher delle app"), &b->bt_launcher);
    gtk_widget_set_valign(b->bt_launcher, GTK_ALIGN_CENTER);
    g_signal_connect(b->bt_launcher, "clicked", G_CALLBACK(clic_launcher), b);
    gtk_box_append(GTK_BOX(b->barra), b->bt_launcher);

    /* 2. Separatore, 3. app aperte */
    gtk_box_append(GTK_BOX(b->barra), separatore_nuovo());
    /* Le voci stanno in una finestra scorrevole: con molte app la parte destra
     * della barra resta sempre visibile e la finestra host puo' restringersi
     * (altrimenti la larghezza minima dei figli la terrebbe larga). */
    b->voci_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_valign(b->voci_box, GTK_ALIGN_CENTER);
    GtkWidget *scorri = gtk_scrolled_window_new();
    b->scorri = scorri;
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scorri), GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scorri), TRUE);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scorri), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scorri), b->voci_box);
    gtk_widget_set_hexpand(scorri, TRUE);
    gtk_widget_set_valign(scorri, GTK_ALIGN_CENTER);
    /* La rotella verticale sopra le voci le fa scorrere in orizzontale. */
    GtkEventController *rotella = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    gtk_event_controller_set_propagation_phase(rotella, GTK_PHASE_CAPTURE);
    g_signal_connect(rotella, "scroll", G_CALLBACK(rotella_voci), scorri);
    gtk_widget_add_controller(scorri, rotella);
    /* Sfumature sui bordi della zona scorrevole (non intercettano i clic). */
    GtkWidget *sovra = gtk_overlay_new();
    gtk_widget_set_hexpand(sovra, TRUE);
    gtk_widget_set_valign(sovra, GTK_ALIGN_CENTER);
    gtk_overlay_set_child(GTK_OVERLAY(sovra), scorri);
    b->fade_sx = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    b->fade_dx = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(b->fade_sx, "ph-barra-fade-sx");
    gtk_widget_add_css_class(b->fade_dx, "ph-barra-fade-dx");
    gtk_widget_set_halign(b->fade_sx, GTK_ALIGN_START);
    gtk_widget_set_halign(b->fade_dx, GTK_ALIGN_END);
    GtkWidget *fade[2] = {b->fade_sx, b->fade_dx};
    for (int i = 0; i < 2; i++) {
        gtk_widget_set_can_target(fade[i], FALSE);
        gtk_widget_set_visible(fade[i], FALSE);
        gtk_overlay_add_overlay(GTK_OVERLAY(sovra), fade[i]);
    }
    GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scorri));
    g_signal_connect(hadj, "value-changed", G_CALLBACK(fade_cambiato), b);
    g_signal_connect(hadj, "changed", G_CALLBACK(fade_cambiato), b);
    gtk_box_append(GTK_BOX(b->barra), sovra);

    /* 4. Spazio flessibile: lo da' la finestra scorrevole delle voci (hexpand). */

    /* 5. Area di sistema: invia, scarica (con il puntino), separatore */
    GtkWidget *ov_invio = gtk_overlay_new();
    gtk_widget_set_valign(ov_invio, GTK_ALIGN_CENTER);
    gtk_overlay_set_child(GTK_OVERLAY(ov_invio),
                          pulsante_icona_nuovo(&PH_ICONA_INVIA, ph_t("Invia file al telefono"),
                                               ph_t("Invia file al telefono (PC verso telefono)"),
                                               &b->bt_invio));
    b->pt_invio = puntino_nuovo();
    gtk_overlay_add_overlay(GTK_OVERLAY(ov_invio), b->pt_invio);
    g_signal_connect(b->bt_invio, "clicked", G_CALLBACK(clic_invio), b);
    gtk_box_append(GTK_BOX(b->barra), ov_invio);

    GtkWidget *ov_scarica = gtk_overlay_new();
    gtk_widget_set_valign(ov_scarica, GTK_ALIGN_CENTER);
    gtk_overlay_set_child(GTK_OVERLAY(ov_scarica),
                          pulsante_icona_nuovo(&PH_ICONA_SCARICA, ph_t("Scarica file dal telefono"),
                                               ph_t("Scarica file dal telefono (telefono verso PC)"),
                                               &b->bt_scarica));
    b->pt_scarica = puntino_nuovo();
    gtk_overlay_add_overlay(GTK_OVERLAY(ov_scarica), b->pt_scarica);
    g_signal_connect(b->bt_scarica, "clicked", G_CALLBACK(clic_scarica), b);
    gtk_box_append(GTK_BOX(b->barra), ov_scarica);

    gtk_box_append(GTK_BOX(b->barra), separatore_nuovo());

    /* Volume, stato del telefono, notifiche: li crea e li aggiorna il pezzo
     * sistema; senza gestore il posto resta vuoto. */
    static const PhShellPopup tre[] = {PH_POPUP_VOLUME, PH_POPUP_TELEFONO, PH_POPUP_NOTIFICHE};
    for (size_t i = 0; i < G_N_ELEMENTS(tre); i++) {
        GtkWidget *w = ph_shell_sistema_pulsante(sh, tre[i]);
        if (w == NULL)
            continue;
        gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(b->barra), w);
    }

    /* Orologio: non e' un pulsante. */
    b->orologio = gtk_label_new("");
    gtk_widget_add_css_class(b->orologio, "ph-barra-orologio");
    b->bt_orologio = gtk_button_new();
    gtk_widget_add_css_class(b->bt_orologio, "ph-tb");
    gtk_widget_add_css_class(b->bt_orologio, "ph-barra-orologio-bottone");
    gtk_button_set_child(GTK_BUTTON(b->bt_orologio), b->orologio);
    gtk_widget_set_valign(b->bt_orologio, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_end(b->bt_orologio, 4);
    ph_shell_espanso(b->bt_orologio, false);
    g_signal_connect(b->bt_orologio, "clicked", G_CALLBACK(clic_orologio), b);
    gtk_box_append(GTK_BOX(b->barra), b->bt_orologio);

    /* Popup del calendario: sopra la barra, a destra, nello strato dei pannelli. */
    b->pannello_cal = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(b->pannello_cal, "ph-pannello");
    gtk_widget_add_css_class(b->pannello_cal, "ph-barra-calendario");
    ph_shell_vetro_registra(sh, b->pannello_cal, 14, PH_VETRO_CHIARO); /* raggio di ph-pannello */
    gtk_widget_set_halign(b->pannello_cal, GTK_ALIGN_END);
    gtk_widget_set_valign(b->pannello_cal, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(b->pannello_cal, 56);
    gtk_widget_set_margin_end(b->pannello_cal, 8);
    gtk_widget_set_visible(b->pannello_cal, FALSE);
    b->titolo_cal = gtk_label_new("");
    gtk_widget_add_css_class(b->titolo_cal, "ph-barra-data");
    gtk_label_set_xalign(GTK_LABEL(b->titolo_cal), 0.0f);
    gtk_box_append(GTK_BOX(b->pannello_cal), b->titolo_cal);
    /* Intestazione: mese e anno con le frecce. */
    GtkWidget *testa = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    b->lab_mese = gtk_label_new("");
    gtk_widget_add_css_class(b->lab_mese, "ph-barra-mese");
    gtk_label_set_width_chars(GTK_LABEL(b->lab_mese), 10);
    b->lab_anno = gtk_label_new("");
    gtk_widget_add_css_class(b->lab_anno, "ph-barra-mese");
    gtk_label_set_width_chars(GTK_LABEL(b->lab_anno), 5);
    GtkWidget *molla = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(molla, TRUE);
    gtk_box_append(GTK_BOX(testa), freccia_nuova(b, "\xE2\x80\xB9", -1, ph_t("Mese precedente")));
    gtk_box_append(GTK_BOX(testa), b->lab_mese);
    gtk_box_append(GTK_BOX(testa), freccia_nuova(b, "\xE2\x80\xBA", 1, ph_t("Mese successivo")));
    gtk_box_append(GTK_BOX(testa), molla);
    gtk_box_append(GTK_BOX(testa), freccia_nuova(b, "\xE2\x80\xB9", -12, ph_t("Anno precedente")));
    gtk_box_append(GTK_BOX(testa), b->lab_anno);
    gtk_box_append(GTK_BOX(testa), freccia_nuova(b, "\xE2\x80\xBA", 12, ph_t("Anno successivo")));
    gtk_box_append(GTK_BOX(b->pannello_cal), testa);
    /* Griglia: riga 0 = nomi dei giorni, poi 6 settimane. */
    GtkWidget *griglia = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(griglia), TRUE);
    for (int i = 0; i < 7; i++) {
        b->nomi_giorni[i] = gtk_label_new("");
        gtk_widget_add_css_class(b->nomi_giorni[i], "ph-barra-nome-giorno");
        gtk_grid_attach(GTK_GRID(griglia), b->nomi_giorni[i], i, 0, 1, 1);
    }
    for (int i = 0; i < 42; i++) {
        b->celle[i] = gtk_label_new("");
        gtk_widget_add_css_class(b->celle[i], "ph-barra-giorno");
        gtk_grid_attach(GTK_GRID(griglia), b->celle[i], i % 7, 1 + i / 7, 1, 1);
    }
    gtk_box_append(GTK_BOX(b->pannello_cal), griglia);
    ph_shell_strato_aggiungi(sh, PH_STRATO_PANNELLI, b->pannello_cal);

    /* Menu del clic destro: le righe si rifanno a ogni apertura. */
    b->menu = g_object_new(GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL,
                           "accessible-role", GTK_ACCESSIBLE_ROLE_MENU, NULL);
    gtk_widget_add_css_class(b->menu, "ph-menu");
    ph_shell_vetro_registra(sh, b->menu, 10, PH_VETRO_CHIARO); /* raggio di ph-menu */
    gtk_widget_set_halign(b->menu, GTK_ALIGN_START);
    gtk_widget_set_valign(b->menu, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(b->menu, 56);
    gtk_widget_set_visible(b->menu, FALSE);
    ph_shell_strato_aggiungi(sh, PH_STRATO_PANNELLI, b->menu);
    g_object_ref(b->pannello_cal); /* come la barra: vedi barra_distrutta */
    g_object_ref(b->menu);

    ph_shell_strato_aggiungi(sh, PH_STRATO_BARRA, b->barra);
    g_object_ref(b->barra); /* vedi barra_distrutta */

    b->ev_finestre = ph_shell_ascolta(sh, PH_SHELL_EV_FINESTRE, avviso_finestre, b);
    b->ev_popup = ph_shell_ascolta(sh, PH_SHELL_EV_POPUP, avviso_popup, b);
    b->ev_host = ph_shell_ascolta(sh, PH_SHELL_EV_HOST, avviso_host, b);
    b->ev_collegamento =
        ph_srv_ascolta(b->srv, PH_SRV_EV_COLLEGAMENTO, avviso_collegamento, b);
    b->ev_app = ph_srv_ascolta(b->srv, PH_SRV_EV_APP, avviso_app, b);
    b->ev_profilo = ph_srv_ascolta(b->srv, PH_SRV_EV_PROFILO, avviso_profilo, b);
    b->ev_fissate = ph_shell_ascolta(sh, PH_SHELL_EV_FISSATE, avviso_fissate, b);
    PhShellGestoreFissate gfis = {.io = b, .fissata = gestore_fissata, .fissa = gestore_fissa};
    ph_shell_registra_fissate(sh, &gfis);
    b->ev_icona = ph_srv_ascolta(b->srv, PH_SRV_EV_ICONA, avviso_icona, b);
    b->ev_trasferimenti =
        ph_srv_ascolta(b->srv, PH_SRV_EV_TRASFERIMENTI, avviso_trasferimenti, b);
    b->id_destroy = g_signal_connect(ph_shell_radice(sh), "destroy", G_CALLBACK(barra_distrutta), b);

    barra_aggiorna_voci(b);
    barra_aggiorna_popup(b);
    barra_aggiorna_puntini(b);
    orologio_aggiorna(b);
}
