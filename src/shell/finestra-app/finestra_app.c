/* Pezzo shell-finestra-app (F-05): le finestre interne delle app (specifica
 * 4.5, 4.6, 4.10, 5.5; mockup righe 91-190). Vedi COMPITO.md.
 *
 * Le REGOLE delle finestre (ordine, aggancio, gesti, misura del flusso,
 * assestamento di 300 ms, traduzione dell'input, "riapri dove eri") sono di
 * src/finestre/finestre.h e qui si usano soltanto: questo file mette i widget,
 * raccoglie gli eventi di GTK e li gira a finestre.h e alla superficie.
 * Il resto della logica pura (tasti, chiavi corte, misure iniziali, nome dello
 * screenshot, passi di pausa) e' in logica.c, provata senza schermo.
 *
 * Tutto gira sul thread GTK. Nessuna richiesta al telefono bloccante: ogni
 * comando passa dalla superficie (che a sua volta non blocca). Lo screenshot
 * si scrive su disco in un thread (GTask). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config/lingua.h"
#include "finestre/finestre.h"
#include "finestra_app_demo.h"
#include "logica.h"
#include "shell/pezzi.h"
#include "shell/superficie.h"

/* Generata da glib-compile-resources (--manual-register): il CSS del pezzo. */
void ph_risorse_finestra_app_register_resource(void);

static const char *const CHIAVE_APP = "ph-finestra-app";

/* Misura (in pixel del telefono) con cui si apre lo specchio: ignorata dal
 * backend per lo specchio (la decide il telefono, 4.7) ma i servizi la
 * vogliono valida (multipli di 8, 64-2560). */
#define SPECCHIO_FLUSSO_W 288
#define SPECCHIO_FLUSSO_H 512

#define PREMUTI_MAX 16

typedef struct App App;
typedef struct Fin Fin;

/* ======================================================================
 * La cornice: il widget di una finestra interna. Dispone da se' i suoi figli
 * (titolo, pulsanti, contenuto, maniglia) PERCHE' il contenuto deve coprire
 * ESATTAMENTE il rettangolo di ph_fin_contenuto: la traduzione dell'input di
 * finestre.h lo presuppone. */
#define PH_TIPO_FA_CORNICE (ph_fa_cornice_get_type())
G_DECLARE_FINAL_TYPE(PhFaCornice, ph_fa_cornice, PH, FA_CORNICE, GtkWidget)

struct _PhFaCornice {
    GtkWidget genitore;
    Fin *fin;
    GtkWidget *sfondo;                       /* fondo del titolo (e cursore "grab") */
    GtkWidget *fondo;                        /* fondo del contenuto dietro la superficie (margine dell'utile) */
    GtkWidget *nome, *rec;
    GtkWidget *puls[PH_FIN_PULS_CHIUDI + 1]; /* indicizzati per PhFinPulsante */
    GtkWidget *sup;                          /* la superficie */
    GtkWidget *maniglia;                     /* angolo di ridimensionamento */
    GtkWidget *filo;                         /* bordo e filo di luce sopra il contenuto (non cliccabile) */
    bool vetro;                              /* sfocatura di cio' che sta dietro il titolo: deciso alla realizzazione */
    GPtrArray *dietro;                       /* GdkPaintable * dei widget dietro il titolo (vedi vetro_disegna) */
#ifdef PH_FINESTRA_APP_DEMO
    unsigned n_snapshot;                     /* quante volte la cornice e' stata disegnata (prove del vetro) */
#endif
};

G_DEFINE_FINAL_TYPE(PhFaCornice, ph_fa_cornice, GTK_TYPE_WIDGET)

struct Fin {
    App *app;
    unsigned id;
    char *pacchetto; /* "" per lo specchio */
    char *titolo;
    char chiave[PH_FIN_APP_MAX];
    bool specchio;
    bool titolo_provvisorio; /* elenco delle app non ancora arrivato: titolo = pacchetto */
    GtkWidget *cornice;
    GtkWidget *sup;
    bool in_pausa;           /* stato di pausa GIA' mandato alla superficie */
    bool primo_piano;        /* stato "primo piano" GIA' mandato alla superficie */
    bool reg_attiva;         /* registrazione VERAMENTE in corso (REC) */
    int req_w, req_h, req_dens; /* ultima misura e densita' chieste (per il registro) */
    int ott_w, ott_h;        /* ultima misura ottenuta scritta nel registro */
    bool misura_sospesa;     /* una richiesta di misura arrivata mentre era in pausa */
    int sosp_w, sosp_h, sosp_dens;
    bool chiudendo;
};

typedef enum { GESTO_NESSUNO = 0, GESTO_FINESTRA, GESTO_TOCCO } TipoGesto;

struct App {
    PhShell *sh;
    PhServizi *srv;
    GtkWidget *radice;
    GtkWidget *cont;       /* contenitore libero nello strato FINESTRE */
    PhFinDesktop *d;
    PhFaChiavi *chiavi;
    GPtrArray *fin;        /* Fin *, in ordine di apertura */
    double scala;
    bool host_visibile;
    GdkSurface *sup_host;  /* la superficie di cui seguiamo la scala */

    GtkWidget *menu;       /* menu "Altri comandi" (nello strato PANNELLI) */
    GtkWidget *menu_ruota; /* la voce "Ruota": non c'e' per le app solo verticali */
    GtkWidget *menu_compat_riga; /* voce "Compatibilita'" con la sua spiegazione */
    GtkWidget *menu_compat;      /* la spunta */
    bool compat_aggiorna;        /* true mentre si imposta la spunta da codice: il segnale non vale */
    unsigned salvataggi_compat;  /* salvataggi del profilo chiesti dalla voce Compatibilita' (per le prove) */
    unsigned menu_fin;

    guint ev_area, ev_host;
    guint srv_profilo, srv_coll, srv_app;

    guint tick;            /* 0 = non attivo */
#ifdef PH_FINESTRA_APP_DEMO
    unsigned n_sincronizza; /* chiamate di sincronizza (prove del vetro, --vetro-diag) */
#endif
    uint64_t ult_att;      /* ultimo momento in cui qualcosa poteva cambiare una misura */

    /* Gesto in corso (un solo puntatore alla volta). */
    TipoGesto gesto;
    unsigned gesto_fin;
    int tocco_vw, tocco_vh, tocco_x, tocco_y; /* ultimo punto mandato, per ANNULLA */
    unsigned tocco_pulsanti; /* bit di TOCCO del pulsante che regge il gesto (1 sinistro, 2 destro, 4 centrale) */
#ifdef PH_FINESTRA_APP_DEMO
    PhFaDemoTocco tocchi_log[PH_FA_DEMO_TOCCHI_MAX]; /* tocchi mandati alla superficie (prove) */
    unsigned n_tocchi_log;
#endif

    double ult_x, ult_y;   /* ultima posizione del puntatore nel contenitore */
    bool ult_valido;
    double acc_x, acc_y;   /* rotella: resto frazionario */

    /* Pizzico (Ctrl + rotella o touchpad): macchina a stati di logica.c + ultimo punto
     * mandato, per chiudere il gesto (FINE) nello stesso punto. */
    PhFaPizzico pz;
    unsigned pz_fin;
    int pz_vw, pz_vh, pz_x, pz_y;
    guint pz_timer;        /* controllo dei 250 ms senza passi; 0 = fermo */
    double zoom_prec;      /* GtkGestureZoom: scala all'ultimo evento */
    unsigned zoom_fin;     /* finestra del gesto di zoom del touchpad, 0 = nessuno */

    GtkIMContext *im;
    struct {
        unsigned fin, codice;
    } premuti[PREMUTI_MAX];
    size_t n_premuti;
};

static uint64_t ora(void) { return ph_shell_ora_ms(); }

static App *app_di(GtkWidget *cont)
{
    return g_object_get_data(G_OBJECT(cont), CHIAVE_APP);
}

static Fin *trova_fin(App *a, unsigned id)
{
    for (guint i = 0; i < a->fin->len; i++) {
        Fin *f = g_ptr_array_index(a->fin, i);
        if (f->id == id)
            return f;
    }
    return NULL;
}

static void cambiato(App *a) { ph_shell_emetti(a->sh, PH_SHELL_EV_FINESTRE); }

static void fin_libera(gpointer p)
{
    Fin *f = p;
    g_free(f->pacchetto);
    g_free(f->titolo);
    g_free(f);
}

/* ======================================================================
 * Widget della cornice. */

/* Misura e assegna: GTK vuole che ogni figlio sia misurato prima di
 * allocarlo, anche se qui la misura la decidiamo noi. */
static void alloca_figlio_f(GtkWidget *w, double x, double y, int larg, int alt)
{
    int mw = 0, mh = 0;
    gtk_widget_measure(w, GTK_ORIENTATION_HORIZONTAL, -1, &mw, NULL, NULL, NULL);
    gtk_widget_measure(w, GTK_ORIENTATION_VERTICAL, MAX(larg, mw), &mh, NULL, NULL, NULL);
    graphene_point_t p = GRAPHENE_POINT_INIT((float)x, (float)y);
    gtk_widget_allocate(w, MAX(larg, mw), MAX(alt, mh), -1, gsk_transform_translate(NULL, &p));
}

static void alloca_figlio(GtkWidget *w, int x, int y, int larg, int alt)
{
    alloca_figlio_f(w, x, y, larg, alt);
}

static int nat_larghezza(GtkWidget *w)
{
    int nat = 0;
    gtk_widget_measure(w, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &nat, NULL, NULL);
    return nat;
}

static void cornice_measure(GtkWidget *w, GtkOrientation o, int per, int *min, int *nat,
                            int *min_base, int *nat_base)
{
    (void)w;
    (void)o;
    (void)per;
    /* La misura la decide finestre.h (il libero usa la misura data). */
    *min = 0;
    *nat = 0;
    *min_base = -1;
    *nat_base = -1;
}

static void cornice_allocate(GtkWidget *w, int larg, int alt, int base)
{
    (void)base;
    PhFaCornice *c = PH_FA_CORNICE(w);

    alloca_figlio(c->sfondo, 0, 0, larg, PH_FIN_TITOLO_H);
    for (int p = PH_FIN_PULS_INDIETRO; p <= PH_FIN_PULS_CHIUDI; p++) {
        if (c->puls[p] != NULL)
            alloca_figlio(c->puls[p], ph_fa_pulsante_x(larg, (PhFinPulsante)p),
                          (PH_FIN_TITOLO_H - PH_FIN_PULS) / 2, PH_FIN_PULS,
                          PH_FIN_PULS);
    }

    /* Nome e REC usano solo lo spazio che avanza in mezzo ai pulsanti. */
    int da = ph_fa_pulsante_x(larg, PH_FIN_PULS_INDIETRO) + PH_FIN_PULS + PH_FIN_PULS_DIST;
    int fino = ph_fa_pulsante_x(larg, PH_FIN_PULS_SCREENSHOT) - PH_FIN_PULS_DIST;
    int recw = gtk_widget_get_visible(c->rec) ? nat_larghezza(c->rec) : 0;
    if (recw > 0 && fino - da - recw < 0)
        recw = 0;
    gtk_widget_set_child_visible(c->rec, recw > 0);
    if (recw > 0)
        alloca_figlio(c->rec, fino - recw, (PH_FIN_TITOLO_H - 16) / 2, recw, 16);
    int nw = fino - da - recw;
    int minimo = 0;
    gtk_widget_measure(c->nome, GTK_ORIENTATION_HORIZONTAL, -1, &minimo, NULL, NULL, NULL);
    gtk_widget_set_child_visible(c->nome, nw >= minimo && nw > 0);
    if (nw >= minimo && nw > 0)
        alloca_figlio(c->nome, da, 0, nw, PH_FIN_TITOLO_H);

    /* Il contenuto: ph_fin_contenuto, relativo alla cornice. In regola automatica
     * (utile_w/h > 0) l'immagine e' 1:1 con i pixel fisici: la superficie sta
     * nell'angolo in alto a sinistra del contenuto ed e' grande utile/scala (multipli
     * di 8 pixel fisici); il margine a destra e in basso (fino a 7 px fisici) resta
     * del colore di fondo della cornice. Con 0 (densita' fissa) la superficie copre
     * tutto il contenuto. */
    PhFinInfo info;
    PhFinRect r;
    Fin *f = c->fin;
    if (f != NULL && f->app->d != NULL && ph_fin_info(f->app->d, f->id, &info, NULL) == 0 &&
        ph_fin_contenuto(f->app->d, f->id, &r, NULL) == 0 && r.w > 0 && r.h > 0) {
        int sw = r.w, sh = r.h;
        double ox = r.x - info.rect.x, oy = r.y - info.rect.y;
        alloca_figlio(c->fondo, (int)ox, (int)oy, r.w, r.h);
        if (info.utile_w > 0 && info.utile_h > 0) {
            double sc = f->app->scala > 0.0 ? f->app->scala : 1.0;
            sw = MAX(1, (int)lround(info.utile_w / sc));
            sh = MAX(1, (int)lround(info.utile_h / sc));
            /* Per il vero 1:1 l'origine dell'immagine deve cadere su un pixel FISICO
             * intero (altrimenti viene ricampionata): finestre.h allinea le finestre
             * normali quando la scala lo permette; per ingrandite, agganciate e scale
             * generiche lo si fa qui. Il conto e' rispetto alla superficie NATIVA,
             * non alla radice della shell: la radice non parte da (0,0) della finestra
             * host (barra del titolo dell'host, margine dell'ombra). */
            double sx0 = 0.0, sy0 = 0.0, tx = 0.0, ty = 0.0;
            GtkNative *nat = gtk_widget_get_native(f->app->cont);
            if (nat != NULL) {
                graphene_point_t zero = GRAPHENE_POINT_INIT(0.0f, 0.0f), q;
                if (gtk_widget_compute_point(f->app->cont, GTK_WIDGET(nat), &zero, &q)) {
                    sx0 = q.x;
                    sy0 = q.y;
                }
                gtk_native_get_surface_transform(nat, &tx, &ty);
                sx0 += tx;
                sy0 += ty;
            }
            ox += ph_fa_origine_pixel_intero(r.x, sx0, sc) - r.x;
            oy += ph_fa_origine_pixel_intero(r.y, sy0, sc) - r.y;
        }
        alloca_figlio_f(c->sup, ox, oy, sw, sh);
    }

    if (gtk_widget_get_visible(c->maniglia))
        alloca_figlio(c->maniglia, larg - PH_FIN_ANGOLO, alt - PH_FIN_ANGOLO, PH_FIN_ANGOLO,
                      PH_FIN_ANGOLO);
    /* Per ultimo: sta sopra a tutto. */
    alloca_figlio(c->filo, 0, 0, larg, alt);
}

/* ----------------------------------------------------------------------
 * Vetro (docs/DESIGN_FINESTRE.md, livello 2): dietro la barra del titolo si
 * vede, sfocato e piu' saturo, cio' che sta sotto la finestra. GTK non ha
 * backdrop-filter: lo si ridisegna qui. "Cio' che sta sotto" sono i widget
 * disegnati PRIMA di questa cornice: gli strati piu' bassi della radice (il
 * desktop) e le finestre piu' in basso nello stesso contenitore. Di ognuno
 * si usa un GtkWidgetPaintable, che riusa il disegno gia' fatto del widget
 * (nessun widget viene disegnato due volte dal suo codice) e avvisa quando
 * cambia: allora basta ridisegnare questa cornice.
 * Solo la barra del titolo: il resto della finestra e' coperto dal contenuto
 * opaco, sfocare li' sarebbe lavoro buttato a ogni fotogramma.
 * Non ci sono cicli: una cornice dipende solo da cio' che sta SOTTO di lei. */
#define VETRO_SIGMA_PX 26.0     /* deviazione della gaussiana (CSS blur(26px)) */
#define VETRO_SATURAZIONE 1.6f  /* CSS saturate(1.6) */
#define VETRO_CATENA_MAX 8      /* antenati fra la cornice e la radice della shell */

static void dietro_cambiato(GdkPaintable *p, gpointer cornice)
{
    (void)p;
    gtk_widget_queue_draw(GTK_WIDGET(cornice));
}

/* Il paintable del widget x, creato alla prima richiesta. */
static GdkPaintable *vetro_dipinto(PhFaCornice *c, GtkWidget *x)
{
    for (guint k = 0; k < c->dietro->len; k++) {
        GdkPaintable *p = g_ptr_array_index(c->dietro, k);
        if (gtk_widget_paintable_get_widget(GTK_WIDGET_PAINTABLE(p)) == x)
            return p;
    }
    GdkPaintable *p = gtk_widget_paintable_new(x);
    gulong id = g_signal_connect(p, "invalidate-contents", G_CALLBACK(dietro_cambiato), c);
    g_object_set_data(G_OBJECT(p), "ph-vetro-segnale", GSIZE_TO_POINTER((gsize)id));
    g_ptr_array_add(c->dietro, p);
    return p;
}

static void vetro_dipinto_libera(gpointer p)
{
    /* Per sicurezza: se qualcun altro tenesse un riferimento al paintable, senza
     * staccare il segnale questo chiamerebbe una cornice morta. */
    gulong id = (gulong)GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(p), "ph-vetro-segnale"));
    if (id != 0)
        g_signal_handler_disconnect(p, id);
    g_object_unref(p);
}

/* Mette nel disegno il ritratto di x (un widget che sta dietro la cornice c),
 * se tocca la zona `attorno` (coordinate di c). */
static void vetro_metti(PhFaCornice *c, GtkSnapshot *s, GtkWidget *x, const graphene_rect_t *attorno,
                        guint *usati)
{
    graphene_rect_t b;
    if (!gtk_widget_get_mapped(x) || !gtk_widget_get_child_visible(x) ||
        !gtk_widget_compute_bounds(x, GTK_WIDGET(c), &b) ||
        !graphene_rect_intersection(&b, attorno, NULL))
        return;
    GdkPaintable *p = vetro_dipinto(c, x);
    /* In testa quelli usati in questo disegno: gli altri si tolgono alla fine.
     * (dove > usati: uno gia' in testa, cioe' lo stesso widget incontrato due
     * volte, non si sposta; oggi non capita, un widget sta in un posto solo.) */
    guint dove = 0;
    if (g_ptr_array_find(c->dietro, p, &dove) && dove > *usati) {
        gpointer t = c->dietro->pdata[*usati];
        c->dietro->pdata[*usati] = p;
        c->dietro->pdata[dove] = t;
    }
    if (dove >= *usati)
        (*usati)++;
    gtk_snapshot_save(s);
    gtk_snapshot_translate(s, &b.origin);
    gdk_paintable_snapshot(p, s, b.size.width, b.size.height);
    gtk_snapshot_restore(s);
}

/* Dalla cornice su fino alla radice della shell (nodo "ph-shell"): oltre non
 * si guarda, li' c'e' la finestra del PC. Torna quanti sono (la radice e'
 * l'ultimo), 0 se la radice non si trova entro VETRO_CATENA_MAX: allora niente
 * vetro (cornice_realize), perche' non si saprebbe che cosa c'e' dietro. */
static int vetro_catena(GtkWidget *w, GtkWidget *catena[VETRO_CATENA_MAX])
{
    int n = 0;
    for (GtkWidget *x = w; x != NULL && n < VETRO_CATENA_MAX; x = gtk_widget_get_parent(x)) {
        catena[n++] = x;
        if (gtk_widget_has_css_class(x, "ph-shell"))
            return n;
    }
    return 0;
}

static void vetro_disegna(PhFaCornice *c, GtkSnapshot *s, int larg)
{
    GtkWidget *w = GTK_WIDGET(c);
    GtkWidget *catena[VETRO_CATENA_MAX];
    int n = vetro_catena(w, catena);
    if (n == 0)
        return;

    graphene_rect_t zona = GRAPHENE_RECT_INIT(0.0f, 0.0f, (float)larg, (float)PH_FIN_TITOLO_H);
    /* La sfocatura porta dentro la zona anche cio' che le sta attorno. */
    graphene_rect_t attorno = zona;
    graphene_rect_inset(&attorno, (float)(-3.0 * VETRO_SIGMA_PX), (float)(-3.0 * VETRO_SIGMA_PX));

    /* Come GTK per filter: saturate(). */
    const float a = VETRO_SATURAZIONE;
    graphene_matrix_t colori;
    graphene_vec4_t zero;
    graphene_matrix_init_from_float(&colori, (float[16]){
        0.213f + 0.787f * a, 0.213f - 0.213f * a, 0.213f - 0.213f * a, 0.0f,
        0.715f - 0.715f * a, 0.715f + 0.285f * a, 0.715f - 0.715f * a, 0.0f,
        0.072f - 0.072f * a, 0.072f - 0.072f * a, 0.072f + 0.928f * a, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f});
    graphene_vec4_init(&zero, 0.0f, 0.0f, 0.0f, 0.0f);

    guint usati = 0; /* i primi `usati` di c->dietro servono a questo disegno */
    gtk_snapshot_push_clip(s, &zona);
    gtk_snapshot_push_color_matrix(s, &colori, &zero);
    /* Il raggio di GSK e' il doppio della deviazione (come fa GTK per blur()). */
    gtk_snapshot_push_blur(s, 2.0 * VETRO_SIGMA_PX);
    for (int k = n - 1; k >= 1; k--) {
        for (GtkWidget *x = gtk_widget_get_first_child(catena[k]); x != NULL && x != catena[k - 1];
             x = gtk_widget_get_next_sibling(x)) {
            if (!PH_IS_FA_CORNICE(x)) {
                vetro_metti(c, s, x, &attorno, &usati);
                continue;
            }
            /* Un'altra finestra: i suoi FIGLI uno per uno, non la cornice intera.
             * Il ritratto della cornice conterrebbe la SUA sfocatura, che a sua
             * volta contiene quella delle finestre sotto di lei: con le finestre
             * in cascata il lavoro raddoppierebbe a ogni finestra (misurato).
             * Sfocare una cosa gia' sfocata non si vede; cosi' ogni finestra
             * sfoca una volta sola. Il ritaglio sul rettangolo della finestra
             * basta: gli angoli tondi, sfocati a 26 px, non si distinguono. */
            graphene_rect_t b;
            if (!gtk_widget_get_mapped(x) || !gtk_widget_compute_bounds(x, w, &b) ||
                !graphene_rect_intersection(&b, &attorno, NULL))
                continue;
            gtk_snapshot_push_clip(s, &b);
            for (GtkWidget *f = gtk_widget_get_first_child(x); f != NULL; f = gtk_widget_get_next_sibling(f))
                vetro_metti(c, s, f, &attorno, &usati);
            gtk_snapshot_pop(s);
        }
    }
    gtk_snapshot_pop(s);
    gtk_snapshot_pop(s);
    gtk_snapshot_pop(s);
    /* Chi non sta piu' dietro il titolo (finestra chiusa, spostata, salita
     * sopra) non deve piu' far ridisegnare questa cornice. */
    if (c->dietro->len > usati)
        g_ptr_array_remove_range(c->dietro, usati, c->dietro->len - usati);
}

static void cornice_snapshot(GtkWidget *w, GtkSnapshot *s)
{
    PhFaCornice *c = PH_FA_CORNICE(w);
#ifdef PH_FINESTRA_APP_DEMO
    c->n_snapshot++;
#endif
    /* dietro e' NULL solo dopo cornice_dispose: li' non si ritrae piu' nulla. */
    if (c->dietro != NULL) {
        if (c->vetro)
            vetro_disegna(c, s, gtk_widget_get_width(w));
        else if (c->dietro->len > 0)
            g_ptr_array_set_size(c->dietro, 0);
    }
    for (GtkWidget *f = gtk_widget_get_first_child(w); f != NULL; f = gtk_widget_get_next_sibling(f))
        gtk_widget_snapshot_child(w, f, s);
}

/* YASDE_SFOCATURA=0/1 la forza (prove e schermate dei manuali). */
static void cornice_realize(GtkWidget *w)
{
    GTK_WIDGET_CLASS(ph_fa_cornice_parent_class)->realize(w);
    PhFaCornice *c = PH_FA_CORNICE(w);
    c->vetro = ph_shell_vetro_decidi(w);
    /* Mai il velo al 58% senza la sfocatura dietro: fuori dalla shell (radice
     * non trovata) non c'e' modo di disegnarla. */
    GtkWidget *catena[VETRO_CATENA_MAX];
    if (c->vetro && vetro_catena(w, catena) == 0)
        c->vetro = false;
    if (c->vetro)
        gtk_widget_add_css_class(w, "ph-finestra-app-vetro");
    else
        gtk_widget_remove_css_class(w, "ph-finestra-app-vetro");
}

static void cornice_dispose(GObject *o)
{
    PhFaCornice *c = PH_FA_CORNICE(o);
    c->fin = NULL;
    /* Prima di tutto: da qui in poi nessun paintable deve piu' chiamare la cornice. */
    g_clear_pointer(&c->dietro, g_ptr_array_unref);
    GtkWidget *f;
    while ((f = gtk_widget_get_first_child(GTK_WIDGET(o))) != NULL)
        gtk_widget_unparent(f);
    G_OBJECT_CLASS(ph_fa_cornice_parent_class)->dispose(o);
}

static void ph_fa_cornice_class_init(PhFaCorniceClass *k)
{
    G_OBJECT_CLASS(k)->dispose = cornice_dispose;
    GTK_WIDGET_CLASS(k)->measure = cornice_measure;
    GTK_WIDGET_CLASS(k)->size_allocate = cornice_allocate;
    GTK_WIDGET_CLASS(k)->snapshot = cornice_snapshot;
    GTK_WIDGET_CLASS(k)->realize = cornice_realize;
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(k), "ph-finestra-app");
    gtk_widget_class_set_accessible_role(GTK_WIDGET_CLASS(k), GTK_ACCESSIBLE_ROLE_GROUP);
}

static void ph_fa_cornice_init(PhFaCornice *c)
{
    gtk_widget_set_overflow(GTK_WIDGET(c), GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_focusable(GTK_WIDGET(c), TRUE);
    c->dietro = g_ptr_array_new_with_free_func(vetro_dipinto_libera);
}

/* ======================================================================
 * Azioni (dichiarate qui, definite sotto). */
static void sincronizza(App *a);
static void riordina(App *a);
static void salva_ricordi(App *a);
static void chiudi_fin(App *a, Fin *f, bool togli_task);
static void chiudi_differita(App *a, unsigned id, bool togli_task);
static void azione_pulsante(Fin *f, PhFinPulsante p, GtkWidget *pulsante);
static void ruota(App *a, Fin *f);
static void scatta(App *a, Fin *f, bool solo_appunti);
static void pizzico_chiudi(App *a);

static void pulsante_premuto(GtkButton *b, gpointer dati)
{
    Fin *f = dati;
    PhFinPulsante p = (PhFinPulsante)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "ph-puls"));
    azione_pulsante(f, p, GTK_WIDGET(b));
}

static GtkWidget *pulsante_nuovo(Fin *f, PhFinPulsante p, const PhShellIcona *icona,
                                 const char *etichetta)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "ph-finestra-app-puls");
    if (p == PH_FIN_PULS_CHIUDI)
        gtk_widget_add_css_class(b, "ph-finestra-app-chiudi");
    gtk_button_set_child(GTK_BUTTON(b), ph_shell_icona_nuova(icona));
    gtk_widget_set_focus_on_click(b, FALSE);
    /* Etichetta accessibile del mockup (aria-label); il mockup non ha title. */
    ph_shell_etichetta(b, etichetta, NULL);
    g_object_set_data(G_OBJECT(b), "ph-puls", GINT_TO_POINTER((int)p));
    g_signal_connect(b, "clicked", G_CALLBACK(pulsante_premuto), f);
    return b;
}

/* Costruisce la cornice di una finestra con la sua superficie. */
static GtkWidget *cornice_nuova(Fin *f, GtkWidget *sup)
{
    PhFaCornice *c = g_object_new(PH_TIPO_FA_CORNICE, NULL);
    GtkWidget *w = GTK_WIDGET(c);
    c->fin = f;
    c->sup = sup;

    c->sfondo = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(c->sfondo, "ph-finestra-app-titolo");
    gtk_widget_set_cursor_from_name(c->sfondo, "grab");
    gtk_widget_set_parent(c->sfondo, w);

    /* Dietro la superficie: lo stesso fondo delle bande a riposo (superficie.css), cosi'
     * il margine dell'utile (fino a 7 px fisici) non e' un filo bianco. */
    c->fondo = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(c->fondo, "ph-finestra-app-fondo");
    gtk_widget_set_parent(c->fondo, w);

    gtk_widget_set_parent(sup, w);

    c->nome = gtk_label_new(f->titolo);
    gtk_widget_add_css_class(c->nome, "ph-finestra-app-nome");
    gtk_label_set_xalign(GTK_LABEL(c->nome), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(c->nome), PANGO_ELLIPSIZE_END);
    gtk_widget_set_cursor_from_name(c->nome, "grab");
    gtk_widget_set_parent(c->nome, w);

    c->rec = gtk_label_new(ph_t("REC"));
    gtk_widget_add_css_class(c->rec, "ph-finestra-app-rec");
    gtk_widget_set_visible(c->rec, FALSE);
    gtk_widget_set_cursor_from_name(c->rec, "grab");
    gtk_widget_set_parent(c->rec, w);

    c->puls[PH_FIN_PULS_INDIETRO] =
        pulsante_nuovo(f, PH_FIN_PULS_INDIETRO, &PH_ICONA_INDIETRO, ph_t("Indietro"));
    c->puls[PH_FIN_PULS_SCREENSHOT] =
        pulsante_nuovo(f, PH_FIN_PULS_SCREENSHOT, &PH_ICONA_SCREENSHOT, ph_t("Screenshot"));
    c->puls[PH_FIN_PULS_REGISTRA] =
        pulsante_nuovo(f, PH_FIN_PULS_REGISTRA, &PH_ICONA_REGISTRA, ph_t("Registra lo schermo"));
    c->puls[PH_FIN_PULS_ALTRI] =
        pulsante_nuovo(f, PH_FIN_PULS_ALTRI, &PH_ICONA_ALTRI, ph_t("Altri comandi"));
    c->puls[PH_FIN_PULS_MINIMIZZA] = pulsante_nuovo(f, PH_FIN_PULS_MINIMIZZA,
                                                    &PH_ICONA_RIDUCI_FINESTRA, ph_t("Riduci a icona"));
    c->puls[PH_FIN_PULS_INGRANDISCI] =
        pulsante_nuovo(f, PH_FIN_PULS_INGRANDISCI, &PH_ICONA_INGRANDISCI_FINESTRA,
                       ph_t("Ingrandisci o ripristina"));
    c->puls[PH_FIN_PULS_CHIUDI] =
        pulsante_nuovo(f, PH_FIN_PULS_CHIUDI, &PH_ICONA_CHIUDI_FINESTRA, ph_t("Chiudi"));
    for (int p = PH_FIN_PULS_INDIETRO; p <= PH_FIN_PULS_CHIUDI; p++)
        gtk_widget_set_parent(c->puls[p], w);

    /* Maniglia: sopra a tutto, solo per il cursore; l'input lo raccoglie il
     * gesto del contenitore (ph_fin_trova dice ANGOLO). */
    c->maniglia = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_cursor_from_name(c->maniglia, "nwse-resize");
    gtk_widget_set_visible(c->maniglia, FALSE);
    gtk_widget_set_parent(c->maniglia, w);

    /* Bordo e filo di luce: figlio sovrapposto a tutta la cornice, per non mettere un
     * border CSS (entrerebbe nell'allocazione e sposterebbe i figli di 1 px). */
    c->filo = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(c->filo, "ph-finestra-app-filo");
    gtk_widget_set_can_target(c->filo, FALSE);
    gtk_widget_set_can_focus(c->filo, FALSE);
    gtk_widget_set_parent(c->filo, w);

    ph_shell_etichetta(w, f->titolo, NULL);
    return w;
}

/* Aspetto in base allo stato di finestre.h (colori attiva/inattiva, raggio 0
 * da massimizzata o agganciata, REC, maniglia). */
static void aspetto(Fin *f, const PhFinInfo *i)
{
    PhFaCornice *c = PH_FA_CORNICE(f->cornice);
    if (i->attiva)
        gtk_widget_add_css_class(f->cornice, "ph-finestra-app-attiva");
    else
        gtk_widget_remove_css_class(f->cornice, "ph-finestra-app-attiva");
    if (i->stato != PH_FIN_NORMALE)
        gtk_widget_add_css_class(f->cornice, "ph-finestra-app-piena");
    else
        gtk_widget_remove_css_class(f->cornice, "ph-finestra-app-piena");
    gtk_widget_set_visible(c->rec, f->reg_attiva);
    /* I comandi non disponibili si mostrano DISATTIVATI (non nascosti): per le app
     * solo verticali, in registrazione, ecc. lo dice finestre.h (PhFinInfo.puo_*).
     * La maniglia, che e' solo il cursore di ridimensionamento, senza il diritto
     * di ridimensionare non c'e': niente cursore. */
    gtk_widget_set_visible(c->maniglia, i->puo_ridimensionare);
    gtk_widget_set_sensitive(c->puls[PH_FIN_PULS_INGRANDISCI], i->puo_ingrandire);
}

/* ======================================================================
 * Input verso il telefono. */

/* Ogni tocco verso il telefono passa di qui (nel demo di prova viene anche registrato). */
static void manda_tocco(Fin *f, int vw, int vh, int azione, int x, int y, unsigned pulsanti)
{
#ifdef PH_FINESTRA_APP_DEMO
    App *a = f->app;
    if (a->n_tocchi_log < PH_FA_DEMO_TOCCHI_MAX)
        a->tocchi_log[a->n_tocchi_log++] = (PhFaDemoTocco){azione, pulsanti};
#endif
    ph_superficie_tocco(f->sup, vw, vh, azione, x, y, pulsanti);
}

/* Chiude ogni tocco e ogni tasto in corso di una finestra: prima di una
 * pausa, di una chiusura, del cambio di finestra attiva. */
static void chiudi_tocchi(App *a, Fin *f)
{
    if (a->pz.aperto && a->pz_fin == f->id)
        pizzico_chiudi(a); /* anche il pizzico e' un gesto: si chiude prima di pausa e chiusura */
    if (a->gesto == GESTO_TOCCO && a->gesto_fin == f->id) {
        manda_tocco(f, a->tocco_vw, a->tocco_vh, PH_SRV_TOCCO_ANNULLA, a->tocco_x, a->tocco_y, 0);
        a->gesto = GESTO_NESSUNO;
    }
    for (size_t i = 0; i < a->n_premuti;) {
        if (a->premuti[i].fin == f->id) {
            ph_superficie_tasto(f->sup, false, a->premuti[i].codice, 0);
            a->premuti[i] = a->premuti[--a->n_premuti];
        } else {
            i++;
        }
    }
}

/* Porta lo stato di pausa del flusso a quello voluto (minimizzata o finestra
 * host non visibile). Prima della pausa si chiude ogni tocco (4.6); alla
 * ripresa, prima si riprende e poi si manda l'eventuale misura rimasta in
 * sospeso (in pausa niente ridimensionamento). */
static void riallinea_pausa(App *a, Fin *f, const PhFinInfo *i)
{
    bool voluta = ph_fa_pausa_voluta(a->host_visibile, i->minimizzata);
    switch (ph_fa_passo_pausa(f->in_pausa, voluta)) {
    case PH_FA_PASSO_PAUSA:
        /* In pausa il file avrebbe immagine ferma: la registrazione si ferma. */
        if (ph_superficie_registrando(f->sup))
            ph_superficie_registra_ferma(f->sup);
        chiudi_tocchi(a, f);
        ph_superficie_pausa(f->sup, true);
        f->in_pausa = true;
        break;
    case PH_FA_PASSO_RIPRENDI:
        ph_superficie_pausa(f->sup, false);
        f->in_pausa = false;
        if (f->misura_sospesa) {
            ph_superficie_ridimensiona_densita(f->sup, f->sosp_w, f->sosp_h, f->sosp_dens);
            f->misura_sospesa = false;
        }
        break;
    case PH_FA_PASSO_NIENTE:
        break;
    }
}

/* Densita' delle app (4.4): quella del profilo se impostata, altrimenti quella
 * del telefono (420 come segnaposto finche' non e' collegato). */
static double densita_app(App *a)
{
    PhCfgTelefono *pf = ph_srv_profilo(a->srv);
    PhSrvCollegamento c;
    ph_srv_collegamento(a->srv, &c);
    return pf != NULL && pf->densita_app > 0 ? pf->densita_app : c.densita > 0 ? c.densita : 420;
}

/* Registro delle decisioni: a ogni apertura e ridimensionamento, i numeri da
 * cui leggere che cosa e' stato chiesto al telefono (contenuto in pixel logici
 * e fisici, scala, densita' e misura del flusso); la misura ottenuta si scrive
 * all'arrivo (PH_SUP_EV_MISURA). Nessun dato personale. */
static void registra_misura(App *a, Fin *f, const char *motivo, int w, int h, int densita)
{
    PhFinRect c;
    PhFinInfo i;
    f->req_w = w;
    f->req_h = h;
    f->req_dens = densita;
    if (ph_fin_contenuto(a->d, f->id, &c, NULL) != 0 || ph_fin_info(a->d, f->id, &i, NULL) != 0)
        return;
    char riga[512];
    ph_fa_riga_misura(riga, sizeof riga, motivo, c.w, c.h, a->scala, w, h, densita, densita_app(a),
                      i.tablet, f->specchio, i.fattore, i.punti_w, i.punti_h);
    ph_shell_decisione("misura del flusso", "%s", riga);
}

static gboolean tick(GtkWidget *w, GdkFrameClock *clock, gpointer dati)
{
    (void)w;
    (void)clock;
    App *a = dati;
    uint64_t t = ora();
    PhFinRichiesta r[PH_FIN_MAX];
    size_t n = ph_fin_avanza(a->d, t, r, PH_FIN_MAX);
    for (size_t k = 0; k < n; k++) {
        Fin *f = trova_fin(a, r[k].id);
        if (f == NULL)
            continue;
        registra_misura(a, f, f->in_pausa ? "ridimensionamento (in pausa: si manda alla ripresa)"
                                           : "ridimensionamento",
                        r[k].w, r[k].h, r[k].densita);
        if (f->in_pausa) {
            f->misura_sospesa = true;
            f->sosp_w = r[k].w;
            f->sosp_h = r[k].h;
            f->sosp_dens = r[k].densita;
        } else {
            ph_superficie_ridimensiona_densita(f->sup, r[k].w, r[k].h, r[k].densita);
        }
    }
    /* Nessun cambio da un po' (assestamento + margine) e nessun gesto: il
     * ciclo dei fotogrammi non serve piu'; si riaccende da sincronizza(). */
    if (a->gesto != GESTO_FINESTRA && t > a->ult_att && t - a->ult_att > PH_FIN_ASSESTA_MS + 200) {
        a->tick = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* Limite noto: se uno strato sotto le finestre (il desktop) venisse nascosto o
 * mostrato senza che le finestre cambino, il vetro resterebbe vecchio fino al
 * prossimo ridisegno della cornice. Oggi lo strato del desktop non si nasconde.
 *
 * Una finestra che si sposta, si nasconde o cambia di piano non cambia il
 * PROPRIO disegno, quindi il suo paintable non avvisa nessuno: ma cambia cio'
 * che le altre vedono attraverso il titolo. Si ridisegnano tutte le cornici
 * (solo la cornice: i figli, video compreso, riusano il disegno gia' fatto). */
static void vetro_ridisegna(App *a)
{
    for (guint k = 0; k < a->fin->len; k++) {
        Fin *f = g_ptr_array_index(a->fin, k);
        if (f->cornice != NULL)
            gtk_widget_queue_draw(f->cornice);
    }
}

/* L'unico punto in cui lo stato di finestre.h diventa widget: posizioni,
 * visibilita', aspetto e pausa di tutte le finestre. Ogni cosa che puo'
 * cambiare una misura passa di qui e risveglia il ciclo dell'assestamento. */
static void sincronizza(App *a)
{
#ifdef PH_FINESTRA_APP_DEMO
    a->n_sincronizza++;
#endif
    for (guint k = 0; k < a->fin->len; k++) {
        Fin *f = g_ptr_array_index(a->fin, k);
        PhFinInfo i;
        if (ph_fin_info(a->d, f->id, &i, NULL) != 0)
            continue;
        ph_shell_libero_sposta(a->cont, f->cornice, i.rect.x, i.rect.y, i.rect.w, i.rect.h);
        gtk_widget_set_visible(f->cornice, !i.minimizzata);
        aspetto(f, &i);
        riallinea_pausa(a, f, &i);
        /* Una sola superficie in primo piano: quella della finestra attiva. */
        bool pp = i.attiva && !i.minimizzata;
        if (pp != f->primo_piano) {
            f->primo_piano = pp;
            ph_superficie_primo_piano(f->sup, pp);
        }
    }
    a->ult_att = ora();
    if (a->tick == 0)
        a->tick = gtk_widget_add_tick_callback(a->cont, tick, a, NULL);
    vetro_ridisegna(a);
}

/* L'ordine dei widget segue ph_fin_ordine (dal fondo alla cima). */
static void riordina(App *a)
{
    unsigned ids[PH_FIN_MAX];
    size_t n = ph_fin_ordine(a->d, ids, PH_FIN_MAX);
    for (size_t k = 0; k < n; k++) {
        Fin *f = trova_fin(a, ids[k]);
        if (f != NULL)
            ph_shell_libero_in_cima(a->cont, f->cornice);
    }
    vetro_ridisegna(a);
}

/* La finestra diventa attiva, davanti, con la tastiera. Chiamato dopo
 * ph_fin_porta_davanti / apri / ripristina. */
static void attiva_effetti(App *a, Fin *f)
{
    riordina(a);
    sincronizza(a);
    gtk_widget_grab_focus(f->cornice);
    cambiato(a);
}

/* Pressione su una finestra: davanti e attiva (mockup onMouseDown). */
static void porta_davanti(App *a, Fin *f)
{
    unsigned prima = ph_fin_attiva(a->d);
    if (ph_fin_porta_davanti(a->d, f->id, NULL) != 0)
        return;
    riordina(a);
    sincronizza(a);
    gtk_widget_grab_focus(f->cornice);
    if (prima != f->id)
        cambiato(a);
}

static bool su_pulsante(App *a, double x, double y)
{
    GtkWidget *w = gtk_widget_pick(a->cont, x, y, GTK_PICK_DEFAULT);
    for (; w != NULL && w != a->cont; w = gtk_widget_get_parent(w))
        if (GTK_IS_BUTTON(w))
            return true;
    return false;
}

/* Punto (x, y) della shell nelle coordinate del video della finestra f, sul
 * rettangolo che l'immagine occupa ADESSO (con bande a riposo). fissa: vedi
 * ph_fa_traduci_immagine. false = niente input (nessuna immagine o misura,
 * oppure punto nelle bande). */
static bool colpo_immagine(Fin *f, double x, double y, bool fissa, int *vw, int *vh, int *vx,
                           int *vy)
{
    App *a = f->app;
    PhFinInfo i;
    PhFinRect c;
    double ix, iy, iw, ih;
    if (ph_fin_info(a->d, f->id, &i, NULL) != 0 || ph_fin_contenuto(a->d, f->id, &c, NULL) != 0)
        return false;
    if (!ph_superficie_rettangolo_immagine(f->sup, &ix, &iy, &iw, &ih))
        return false;
    if (!ph_fa_traduci_immagine(c.x + ix, c.y + iy, iw, ih, i.video_w, i.video_h, x, y, fissa, vx,
                                vy))
        return false;
    *vw = i.video_w;
    *vh = i.video_h;
    return true;
}

/* gs puo' essere NULL (prove del demo senza GTK): niente stato da impostare. */
static void sequenza_stato(GtkGesture *gs, GtkEventSequenceState st)
{
    if (gs != NULL)
        gtk_gesture_set_state(gs, st);
}

/* Inizio di un gesto con il pulsante `bottone` (numero GDK) in (x, y). Il sinistro fa tutto
 * (sposta, ridimensiona, tocco); destro e centrale valgono SOLO sul contenuto di una finestra
 * app e li' diventano un tocco con il loro bit: ovunque altro (titolo, bordi, pulsanti, fuori
 * dalle finestre) non cambia niente, ne' il gesto ne' la finestra attiva. */
static void gesto_inizia_pulsante(App *a, GtkGesture *gs, unsigned bottone, double x, double y)
{
    unsigned pulsanti = ph_fa_pulsante_tocco(bottone);
    if (pulsanti == 0) { /* pulsante che non va al telefono */
        sequenza_stato(gs, GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    PhFinColpo c;
    if (pulsanti != 1) {
        ph_fin_trova(a->d, x, y, &c);
        if (c.id == 0 || c.zona != PH_FIN_ZONA_CONTENUTO) {
            sequenza_stato(gs, GTK_EVENT_SEQUENCE_DENIED);
            return;
        }
    }
    pizzico_chiudi(a); /* prima di qualunque tocco il pizzico in corso si chiude */
    /* Difesa: con GTK un secondo pulsante azzera il gesto prima di arrivare qui (vedi set_button in
     * installa), quindi questo ramo non scatta; resta per un gesto che non viene da GTK (touch
     * multiplo) e per le prove. */
    if (a->gesto != GESTO_NESSUNO) {
        sequenza_stato(gs, GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    ph_fin_trova(a->d, x, y, &c);
    Fin *f = c.id != 0 ? trova_fin(a, c.id) : NULL;
    if (f == NULL) {
        sequenza_stato(gs, GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    /* Pressione su una finestra qualunque: davanti e attiva. */
    porta_davanti(a, f);

    switch (c.zona) {
    case PH_FIN_ZONA_TITOLO:
        if (ph_fin_gesto_inizia(a->d, f->id, PH_FIN_GESTO_SPOSTA, x, y, ora(), NULL) == 0) {
            a->gesto = GESTO_FINESTRA;
            a->gesto_fin = f->id;
            sequenza_stato(gs, GTK_EVENT_SEQUENCE_CLAIMED);
            return;
        }
        break;
    case PH_FIN_ZONA_ANGOLO:
        if (ph_fin_gesto_inizia(a->d, f->id, PH_FIN_GESTO_RIDIMENSIONA, x, y, ora(), NULL) == 0) {
            a->gesto = GESTO_FINESTRA;
            a->gesto_fin = f->id;
            sequenza_stato(gs, GTK_EVENT_SEQUENCE_CLAIMED);
            return;
        }
        break;
    case PH_FIN_ZONA_CONTENUTO:
        /* I pulsanti dentro la superficie (velo di connessione persa) sono
         * della superficie: non sono input per il telefono. */
        if (c.valido && !f->in_pausa && !su_pulsante(a, x, y)) {
            int vw, vh, vx, vy;
            /* Il punto deve cadere sull'immagine, non nelle bande. */
            if (!colpo_immagine(f, x, y, false, &vw, &vh, &vx, &vy))
                break;
            manda_tocco(f, vw, vh, PH_SRV_TOCCO_GIU, vx, vy, pulsanti);
            a->gesto = GESTO_TOCCO;
            a->gesto_fin = f->id;
            a->tocco_vw = vw;
            a->tocco_vh = vh;
            a->tocco_x = vx;
            a->tocco_y = vy;
            a->tocco_pulsanti = pulsanti;
            /* NON si rivendica la sequenza: con un secondo dito lo zoom deve poter
             * riconoscere il pizzico (ph_fa_zoom_inizio annulla questo tocco). */
            return;
        }
        break;
    default: /* pulsanti: li gestisce GtkButton */
        break;
    }
    sequenza_stato(gs, GTK_EVENT_SEQUENCE_DENIED);
}

static void gesto_inizia(GtkGestureDrag *g, double x, double y, gpointer dati)
{
    GtkGesture *gs = GTK_GESTURE(g);
    unsigned b = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gs));
    if (b == 0) /* il tocco da touchscreen conta come pulsante sinistro */
        b = GDK_BUTTON_PRIMARY;
    gesto_inizia_pulsante(dati, gs, b, x, y);
}

/* Punto di un tocco in corso nelle coordinate del video della SUA finestra. */
static bool punto_tocco(App *a, double x, double y, int *vw, int *vh, int *vx, int *vy)
{
    Fin *f = trova_fin(a, a->gesto_fin);
    return f != NULL && colpo_immagine(f, x, y, true, vw, vh, vx, vy);
}

static void gesto_aggiorna_punto(App *a, double x, double y)
{
    if (a->gesto == GESTO_NESSUNO)
        return;
    if (a->gesto == GESTO_FINESTRA) {
        /* Nessuna richiesta al telefono: il video lo scala la GPU. */
        if (ph_fin_gesto_muovi(a->d, x, y, ora(), NULL) == 0)
            sincronizza(a);
    } else {
        Fin *f = trova_fin(a, a->gesto_fin);
        int vw, vh, vx, vy;
        if (f != NULL && !f->in_pausa && punto_tocco(a, x, y, &vw, &vh, &vx, &vy)) {
            manda_tocco(f, vw, vh, PH_SRV_TOCCO_MUOVI, vx, vy, a->tocco_pulsanti);
            a->tocco_vw = vw;
            a->tocco_vh = vh;
            a->tocco_x = vx;
            a->tocco_y = vy;
        }
    }
}

static void gesto_aggiorna(GtkGestureDrag *g, double ox, double oy, gpointer dati)
{
    double sx, sy;
    if (!gtk_gesture_drag_get_start_point(g, &sx, &sy))
        return;
    gesto_aggiorna_punto(dati, sx + ox, sy + oy);
}

static void gesto_fine_punto(App *a, double x, double y)
{
    if (a->gesto == GESTO_NESSUNO)
        return;
    TipoGesto tipo = a->gesto;
    a->gesto = GESTO_NESSUNO;
    if (tipo == GESTO_FINESTRA) {
        PhFinAggancio agg;
        ph_fin_gesto_finisci(a->d, x, y, ora(), &agg, NULL);
        sincronizza(a);
        salva_ricordi(a); /* a movimento assestato */
    } else {
        Fin *f = trova_fin(a, a->gesto_fin);
        if (f == NULL)
            return;
        int vw, vh, vx, vy;
        if (punto_tocco(a, x, y, &vw, &vh, &vx, &vy))
            manda_tocco(f, vw, vh, PH_SRV_TOCCO_SU, vx, vy, 0);
        else /* misura sparita: il tocco si chiude comunque */
            manda_tocco(f, a->tocco_vw, a->tocco_vh, PH_SRV_TOCCO_ANNULLA, a->tocco_x, a->tocco_y, 0);
    }
}

static void gesto_fine(GtkGestureDrag *g, double ox, double oy, gpointer dati)
{
    double sx, sy;
    if (!gtk_gesture_drag_get_start_point(g, &sx, &sy))
        return;
    gesto_fine_punto(dati, sx + ox, sy + oy);
}

/* Perdita del puntatore (grab perso, finestra host che perde il fuoco...). */
static void gesto_annulla_tutto(App *a)
{
    TipoGesto tipo = a->gesto;
    if (tipo == GESTO_NESSUNO)
        return;
    a->gesto = GESTO_NESSUNO;
    if (tipo == GESTO_FINESTRA) {
        ph_fin_gesto_annulla(a->d, ora());
        sincronizza(a);
    } else {
        Fin *f = trova_fin(a, a->gesto_fin);
        if (f != NULL)
            manda_tocco(f, a->tocco_vw, a->tocco_vh, PH_SRV_TOCCO_ANNULLA, a->tocco_x, a->tocco_y, 0);
    }
}

static void gesto_annulla(GtkGesture *g, GdkEventSequence *seq, gpointer dati)
{
    (void)g;
    (void)seq;
    gesto_annulla_tutto(dati);
}

static void puntatore_mosso(GtkEventControllerMotion *m, double x, double y, gpointer dati)
{
    (void)m;
    App *a = dati;
    a->ult_x = x;
    a->ult_y = y;
    a->ult_valido = true;
    if (a->pz.aperto) { /* il puntatore e' uscito dal contenuto della finestra del pizzico? */
        PhFinColpo c;
        ph_fin_trova(a->d, x, y, &c);
        if (c.id != a->pz_fin || c.zona != PH_FIN_ZONA_CONTENUTO)
            pizzico_chiudi(a);
    }
}

static void puntatore_uscito(GtkEventControllerMotion *m, gpointer dati)
{
    (void)m;
    App *a = dati;
    a->ult_valido = false;
    pizzico_chiudi(a);
}

/* Con GTK >= 4.8 il touchpad manda PIXEL (unita' SURFACE), la rotella del
 * mouse scatti (WHEEL): i pixel si convertono in scatti con un fattore fisso,
 * da tarare dal vivo. */
#define ROTELLA_PIXEL_PER_SCATTO 15.0

/* ---- Pizzico ------------------------------------------------------------- */
static void pizzico_chiudi(App *a)
{
    if (ph_fa_pz_chiudi(&a->pz) != PH_FA_PZ_FINE)
        return;
    Fin *f = trova_fin(a, a->pz_fin);
    if (f != NULL)
        ph_superficie_pizzico(f->sup, a->pz_vw, a->pz_vh, PH_SRV_PIZZICO_FINE, a->pz_x, a->pz_y, 0);
    if (a->pz_timer != 0) {
        g_source_remove(a->pz_timer);
        a->pz_timer = 0;
    }
}

/* FINE 250 ms dopo l'ultimo passo: controllo a intervalli corti, il tempo lo decide la
 * logica (ph_fa_pz_scaduto). */
static gboolean pizzico_timer(gpointer dati)
{
    App *a = dati;
    if (!a->pz.aperto) {
        a->pz_timer = 0;
        return G_SOURCE_REMOVE;
    }
    if (ph_fa_pz_scaduto(&a->pz, ora())) {
        a->pz_timer = 0; /* gia' tolto: pizzico_chiudi non lo rimuove due volte */
        pizzico_chiudi(a);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* Un passo di zoom (in scatti, positivo = ingrandisci) sul punto (vx, vy) del video di f. */
static void pizzico_passo(App *a, Fin *f, double scatti, int vw, int vh, int vx, int vy)
{
    if (a->pz.aperto && a->pz_fin != f->id)
        pizzico_chiudi(a); /* un gesto alla volta, della sua finestra */
    int passo = 0;
    PhFaPzAzione az = ph_fa_pz_passo(&a->pz, scatti, ora(), &passo);
    if (az == PH_FA_PZ_NIENTE)
        return;
    a->pz_fin = f->id;
    a->pz_vw = vw;
    a->pz_vh = vh;
    a->pz_x = vx;
    a->pz_y = vy;
    ph_superficie_pizzico(f->sup, vw, vh,
                          az == PH_FA_PZ_INIZIO ? PH_SRV_PIZZICO_INIZIO : PH_SRV_PIZZICO_CONTINUA, vx,
                          vy, passo);
    if (a->pz_timer == 0)
        a->pz_timer = g_timeout_add(40, pizzico_timer, a);
}

static gboolean rotella(GtkEventControllerScroll *s, double dx, double dy, gpointer dati)
{
    bool pixel = gtk_event_controller_scroll_get_unit(s) == GDK_SCROLL_UNIT_SURFACE;
    App *a = dati;
    bool zoom = ph_fa_scroll_e_zoom(
        gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(s)));
    if (!zoom)
        pizzico_chiudi(a); /* rotella normale: l'eventuale zoom in corso e' finito */
    if (!a->ult_valido) {
        pizzico_chiudi(a);
        return FALSE;
    }
    PhFinColpo c;
    ph_fin_trova(a->d, a->ult_x, a->ult_y, &c);
    Fin *f = c.id != 0 ? trova_fin(a, c.id) : NULL;
    int vw, vh, vx, vy;
    /* Fuori dal contenuto, nelle bande o sopra un pulsante: ne' rotella ne' zoom (un
     * pizzico aperto si chiude). */
    if (f == NULL || c.zona != PH_FIN_ZONA_CONTENUTO || f->in_pausa || !c.valido ||
        su_pulsante(a, a->ult_x, a->ult_y) ||
        !colpo_immagine(f, a->ult_x, a->ult_y, false, &vw, &vh, &vx, &vy)) {
        if (zoom)
            pizzico_chiudi(a);
        return FALSE;
    }
    if (zoom) {
        /* Ctrl + rotella: NON e' una rotella ma un pizzico nel punto del puntatore
         * (rotella in su = dy negativo di GTK = ingrandisce). */
        pizzico_passo(a, f, -ph_fa_scatti_da_delta(pixel, ROTELLA_PIXEL_PER_SCATTO, dy), vw, vh, vx, vy);
        return TRUE;
    }
    int px = ph_fa_passi_rotella(&a->acc_x, ph_fa_scatti_da_delta(pixel, ROTELLA_PIXEL_PER_SCATTO, dx));
    int py = ph_fa_passi_rotella(&a->acc_y, ph_fa_scatti_da_delta(pixel, ROTELLA_PIXEL_PER_SCATTO, dy));
    /* px, py sono in 120esimi di scatto. Convenzione di Android: verticale
     * positivo = rotella in avanti (su), GTK da' positivo = giu': si inverte qui,
     * una volta sola; l'orizzontale ha lo stesso verso. */
    if (px != 0 || py != 0)
        ph_superficie_rotella(f->sup, vw, vh, vx, vy, px, -py);
    return TRUE;
}

/* ---- Tastiera (4.10): la finestra attiva ha il fuoco. ---- */
static Fin *finestra_attiva(App *a)
{
    unsigned id = ph_fin_attiva(a->d);
    return id != 0 ? trova_fin(a, id) : NULL;
}

/* Registra un tasto tenuto giu'. false se la tabella e' piena: in quel caso il
 * "giu'" NON va mandato (il "su" non potrebbe partire mai e il tasto resterebbe
 * premuto sul telefono). Un tasto gia' registrato (ripetizione) torna true. */
static bool registra_premuto(App *a, Fin *f, unsigned codice)
{
    for (size_t i = 0; i < a->n_premuti; i++)
        if (a->premuti[i].fin == f->id && a->premuti[i].codice == codice)
            return true;
    if (a->n_premuti >= PREMUTI_MAX)
        return false;
    a->premuti[a->n_premuti].fin = f->id;
    a->premuti[a->n_premuti].codice = codice;
    a->n_premuti++;
    return true;
}

static gboolean tasto_premuto(GtkEventControllerKey *k, guint keyval, guint keycode,
                              GdkModifierType m, gpointer dati)
{
    (void)k;
    (void)keycode;
    App *a = dati;
    Fin *f = finestra_attiva(a);
    if (f == NULL || f->in_pausa)
        return FALSE;

    /* Riservati alla shell: non vanno al telefono. */
    switch (ph_fa_tasto_riservato(keyval, m)) {
    case PH_FA_RISERVATO_RUOTA:
        ruota(a, f);
        return TRUE;
    case PH_FA_RISERVATO_CHIUDI_APP:
        chiudi_differita(a, f->id, true);
        return TRUE;
    case PH_FA_RISERVATO_COPIA_SCREENSHOT:
        scatta(a, f, true);
        return TRUE;
    case PH_FA_RISERVATO_SCHERMO_INTERO:
        return FALSE; /* lo gestisce l'ossatura */
    case PH_FA_RISERVATO_NESSUNO:
        break;
    }

    /* Esc: se non c'e' un popup (lo avrebbe chiuso il gestore dei popup prima di
     * noi) vale "Indietro" con la preferenza esc_indietro, altrimenti e' il tasto. */
    if (keyval == GDK_KEY_Escape && (m & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK)) == 0) {
        const PhCfgPreferenze *p = ph_srv_preferenze(a->srv);
        if (p != NULL && p->esc_indietro) {
            ph_superficie_indietro(f->sup);
            return TRUE;
        }
    }

    /* Caratteri stampabili senza Ctrl/Alt: testo (di norma li ha gia' presi il
     * metodo di input; qui arrivano quando non li ha filtrati). */
    if (ph_fa_tasto_e_testo(keyval, m)) {
        char utf8[8] = {0};
        gunichar ch = gdk_keyval_to_unicode(keyval);
        g_unichar_to_utf8(ch, utf8);
        ph_superficie_testo(f->sup, utf8);
        return TRUE;
    }

    unsigned codice = ph_fa_tasto_android(keyval);
    if (codice == 0)
        return FALSE;
    if (!registra_premuto(a, f, codice))
        return TRUE; /* tabella piena: il tasto si perde, ma non resta premuto */
    ph_superficie_tasto(f->sup, true, codice, ph_fa_meta_android(m));
    return TRUE;
}

static void tasto_rilasciato(GtkEventControllerKey *k, guint keyval, guint keycode,
                             GdkModifierType m, gpointer dati)
{
    (void)k;
    (void)keycode;
    App *a = dati;
    unsigned codice = ph_fa_tasto_android(keyval);
    if (codice == 0)
        return;
    /* Il "su" parte solo per un "giu'" mandato davvero (e alla stessa finestra). */
    for (size_t i = 0; i < a->n_premuti; i++) {
        if (a->premuti[i].codice != codice)
            continue;
        Fin *f = trova_fin(a, a->premuti[i].fin);
        if (f != NULL)
            ph_superficie_tasto(f->sup, false, codice, ph_fa_meta_android(m));
        a->premuti[i] = a->premuti[--a->n_premuti];
        return;
    }
}

/* Rilascio di Ctrl: lo zoom in corso finisce. */
static gboolean modificatori_cambiati(GtkEventControllerKey *k, GdkModifierType stato, gpointer dati)
{
    (void)k;
    App *a = dati;
    if ((stato & GDK_CONTROL_MASK) == 0)
        pizzico_chiudi(a);
    return FALSE;
}

/* Pizzico del touchpad (GtkGestureZoom): stesso messaggio, passi dalla legge del
 * telefono (x1,25 ogni 120). Sulle superfici touch la prima pressione e' gia' del
 * gesto di trascinamento: lo zoom a due dita da touch non interferisce. */
static void zoom_inizia(GtkGesture *g, GdkEventSequence *seq, gpointer dati)
{
    (void)seq;
    App *a = dati;
    double x, y;
    PhFinColpo c;
    if (!gtk_gesture_get_bounding_box_center(g, &x, &y)) {
        gtk_gesture_set_state(g, GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    ph_fin_trova(a->d, x, y, &c);
    Fin *f = c.id != 0 ? trova_fin(a, c.id) : NULL;
    if (f == NULL || c.zona != PH_FIN_ZONA_CONTENUTO || f->in_pausa) {
        gtk_gesture_set_state(g, GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    /* Touchscreen: il primo dito e' gia' un tocco. Si annulla al telefono (nessun tocco
     * spurio) e lo zoom parte; se e' in corso lo spostamento o il ridimensionamento
     * di una finestra, lo zoom si rifiuta. */
    switch (ph_fa_zoom_inizio(a->gesto == GESTO_TOCCO, a->gesto == GESTO_FINESTRA)) {
    case PH_FA_ZOOM_RIFIUTA:
        gtk_gesture_set_state(g, GTK_EVENT_SEQUENCE_DENIED);
        return;
    case PH_FA_ZOOM_ANNULLA_TOCCO: {
        Fin *tf = trova_fin(a, a->gesto_fin);
        if (tf != NULL)
            manda_tocco(tf, a->tocco_vw, a->tocco_vh, PH_SRV_TOCCO_ANNULLA, a->tocco_x, a->tocco_y, 0);
        a->gesto = GESTO_NESSUNO; /* gli update e la fine del dito si ignorano */
        break;
    }
    case PH_FA_ZOOM_PROCEDI:
        break;
    }
    pizzico_chiudi(a);
    a->zoom_fin = f->id;
    a->zoom_prec = 1.0;
}

static void zoom_cambiato(GtkGestureZoom *z, double scala, gpointer dati)
{
    App *a = dati;
    Fin *f = a->zoom_fin != 0 ? trova_fin(a, a->zoom_fin) : NULL;
    double x, y;
    int vw, vh, vx, vy;
    if (f == NULL || f->in_pausa || !gtk_gesture_get_bounding_box_center(GTK_GESTURE(z), &x, &y))
        return;
    double rapporto = a->zoom_prec > 0.0 ? scala / a->zoom_prec : 1.0;
    a->zoom_prec = scala;
    if (!colpo_immagine(f, x, y, true, &vw, &vh, &vx, &vy)) /* fisso: il gesto resta della finestra */
        return;
    pizzico_passo(a, f, ph_fa_scatti_da_rapporto(rapporto), vw, vh, vx, vy);
}

static void zoom_finito(GtkGesture *g, GdkEventSequence *seq, gpointer dati)
{
    (void)g;
    (void)seq;
    App *a = dati;
    a->zoom_fin = 0;
    pizzico_chiudi(a);
}

static void testo_composto(GtkIMContext *im, const char *testo, gpointer dati)
{
    (void)im;
    App *a = dati;
    Fin *f = finestra_attiva(a);
    if (f != NULL && !f->in_pausa && testo != NULL && testo[0] != '\0')
        ph_superficie_testo(f->sup, testo);
}

static void fuoco_entra(GtkEventControllerFocus *c, gpointer dati)
{
    (void)c;
    App *a = dati;
    gtk_im_context_focus_in(a->im);
}

static void fuoco_esce(GtkEventControllerFocus *c, gpointer dati)
{
    (void)c;
    App *a = dati;
    gtk_im_context_focus_out(a->im);
    pizzico_chiudi(a); /* perdita del fuoco */
    /* Nessun tasto resta premuto sul telefono se la tastiera se ne va. */
    while (a->n_premuti > 0) {
        size_t i = a->n_premuti - 1;
        Fin *f = trova_fin(a, a->premuti[i].fin);
        if (f != NULL)
            ph_superficie_tasto(f->sup, false, a->premuti[i].codice, 0);
        a->n_premuti--;
    }
}

/* ======================================================================
 * Screenshot e registrazione (5.5): le fa la superficie (con i servizi e
 * src/video/cattura.h, che decide i nomi dei file e non sovrascrive mai); qui
 * si scelgono la cartella, gli avvisi e gli appunti. */

/* Cartella del PC per screenshot e registrazioni: <Immagini>/YASDE, o la
 * home se manca la cartella Immagini. in_immagini dice quale dei due. */
static char *cartella_catture(bool *in_immagini)
{
    const char *immagini = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    *in_immagini = immagini != NULL;
    return ph_fa_cartella_screenshot(immagini != NULL ? immagini : g_get_home_dir());
}

/* "<cartella> e copiato negli appunti" con la cartella vera del file. */
static void avviso_salvato(App *a, const char *percorso)
{
    bool in_immagini;
    char *standard = cartella_catture(&in_immagini);
    char *cartella = g_path_get_dirname(percorso);
    if (in_immagini && strcmp(cartella, standard) == 0) {
        ph_shell_avviso(a->sh, ph_t("Screenshot salvato"),
                        ph_t("Immagini/YASDE e copiato negli appunti"));
    } else {
        /* Manca la cartella Immagini (o il file e' altrove): si dice la cartella vera. */
        char testo[PH_CFG_LUN_PERCORSO + 64];
        ph_lingua_componi(testo, sizeof testo, ph_t("{cartella} e copiato negli appunti"),
                          "cartella", cartella, (const char *)NULL);
        ph_shell_avviso(a->sh, ph_t("Screenshot salvato"), testo);
    }
    g_free(cartella);
    g_free(standard);
}

static void appunti_imposta(App *a, GdkTexture *immagine)
{
    if (immagine != NULL) /* in memoria: mai la DMA-BUF mostrata */
        gdk_clipboard_set_texture(gdk_display_get_clipboard(gtk_widget_get_display(a->cont)),
                                  immagine);
}

/* Le funzioni di ritorno non arrivano dopo ph_superficie_chiudi: Fin e' vivo. */
static void screenshot_file_fatto(GtkWidget *sup, PhSrvEsito esito, const char *frase,
                                  const char *percorso, GdkTexture *immagine, void *dati)
{
    (void)sup;
    Fin *f = dati;
    App *a = f->app;
    if (esito != PH_SRV_ESITO_OK || percorso == NULL) {
        ph_shell_avviso(a->sh, ph_t("Screenshot non salvato"), frase != NULL ? frase : "");
        return;
    }
    appunti_imposta(a, immagine);
    avviso_salvato(a, percorso);
}

static void screenshot_appunti_fatto(GtkWidget *sup, PhSrvEsito esito, const char *frase,
                                     const char *percorso, GdkTexture *immagine, void *dati)
{
    (void)sup;
    (void)percorso;
    Fin *f = dati;
    App *a = f->app;
    if (esito != PH_SRV_ESITO_OK || immagine == NULL) {
        ph_shell_avviso(a->sh, ph_t("Screenshot non copiato"), frase != NULL ? frase : "");
        return;
    }
    appunti_imposta(a, immagine);
    ph_shell_avviso(a->sh, ph_t("Screenshot copiato negli appunti"), "");
}

/* solo_appunti = Ctrl+Maiusc+C; altrimenti il pulsante: file e appunti. */
static void scatta(App *a, Fin *f, bool solo_appunti)
{
    (void)a;
    bool in_immagini;
    char *cartella = cartella_catture(&in_immagini);
    if (solo_appunti)
        ph_superficie_screenshot(f->sup, PH_SRV_FOTO_APPUNTI, cartella, f->titolo, NULL,
                                 screenshot_appunti_fatto, f);
    else
        ph_superficie_screenshot(f->sup, PH_SRV_FOTO_FILE | PH_SRV_FOTO_APPUNTI, cartella,
                                 f->titolo, NULL, screenshot_file_fatto, f);
    g_free(cartella);
}

/* ---- Registrazione dello schermo ---------------------------------------- */
/* Lo stato che si vede (REC) e' quello VERO, dagli avvisi della superficie. Il
 * segno di finestre.h (misura ferma, niente massimizza/aggancio/ruota) si mette
 * alla richiesta e si toglie a registrazione finita o non partita.
 * SCELTE: (1) solo schermo, senza audio (la specifica non parla di audio);
 * (2) la registrazione si ferma in modo ordinato quando il flusso va in pausa
 * (finestra minimizzata o finestra host non visibile) e quando la finestra si
 * chiude: altrimenti il file avrebbe minuti di immagine ferma; (3) a fine
 * registrazione un avviso dice la cartella del file. */
static void reg_avviso(GtkWidget *sup, const PhSrvRegDati *d, void *dati)
{
    (void)sup;
    Fin *f = dati;
    App *a = f->app;
    switch (d->evento) {
    case PH_SRV_REG_INIZIATA:
        f->reg_attiva = true;
        sincronizza(a);
        return;
    case PH_SRV_REG_FINITA:
    case PH_SRV_REG_ERRORE:
        break;
    }
    f->reg_attiva = false;
    ph_fin_imposta_registrazione(a->d, f->id, false, ora(), NULL);
    sincronizza(a); /* REC si spegne e l'assestamento riparte */
    if (d->evento == PH_SRV_REG_ERRORE) {
        ph_shell_avviso(a->sh, ph_t("Registrazione non riuscita"), d->frase != NULL ? d->frase : "");
    } else if (d->percorso == NULL) {
        ph_shell_avviso(a->sh, ph_t("Registrazione non riuscita"),
                        d->frase != NULL ? d->frase : ph_t("Nessuna immagine registrata."));
    } else if (d->frase != NULL) {
        ph_shell_avviso(a->sh, ph_t("Registrazione interrotta"), d->frase);
    } else {
        char *cartella = g_path_get_dirname(d->percorso);
        ph_shell_avviso(a->sh, ph_t("Registrazione salvata"), cartella);
        g_free(cartella);
    }
}

/* L'UNICO punto in cui la registrazione parte e si ferma. */
static void registrazione_applica(App *a, Fin *f, bool avvia)
{
    if (!avvia) {
        ph_superficie_registra_ferma(f->sup); /* arriva FINITA */
        return;
    }
    if (ph_fin_imposta_registrazione(a->d, f->id, true, ora(), NULL) != 0)
        return;
    bool in_immagini;
    char *cartella = cartella_catture(&in_immagini);
    ph_superficie_registra(f->sup, cartella, f->titolo, false, reg_avviso, f);
    g_free(cartella);
    sincronizza(a);
}

static void alterna_registrazione(App *a, Fin *f)
{
    registrazione_applica(a, f, !ph_superficie_registrando(f->sup));
}

/* ======================================================================
 * Menu "Altri comandi" (⋮). */
static void menu_chiuso(PhShell *sh, PhShellPopup tipo, GtkWidget *w, void *dati)
{
    (void)sh;
    (void)tipo;
    (void)w;
    App *a = dati;
    a->menu_fin = 0;
}

static void menu_ruota(GtkButton *b, gpointer dati)
{
    (void)b;
    App *a = dati;
    unsigned id = a->menu_fin;
    ph_shell_popup_chiudi(a->sh);
    Fin *f = id != 0 ? trova_fin(a, id) : NULL;
    if (f != NULL)
        ruota(a, f);
}

static void menu_chiudi_app(GtkButton *b, gpointer dati)
{
    (void)b;
    App *a = dati;
    unsigned id = a->menu_fin;
    ph_shell_popup_chiudi(a->sh);
    if (id != 0)
        chiudi_differita(a, id, true);
}

static GtkWidget *voce_menu(const char *testo, GCallback fn, App *a)
{
    GtkWidget *b = gtk_button_new_with_label(testo);
    gtk_widget_add_css_class(b, "ph-riga");
    gtk_widget_add_css_class(b, "ph-finestra-app-voce");
    GtkWidget *et = gtk_button_get_child(GTK_BUTTON(b));
    if (GTK_IS_LABEL(et))
        gtk_label_set_xalign(GTK_LABEL(et), 0.0f);
    g_signal_connect(b, "clicked", fn, a);
    return b;
}

/* Voce a spunta "Compatibilita'" (CONTRATTI.md §19): per le app che si vedono
 * rimpicciolite perche' usano la densita' dello schermo VERO del telefono. Scelta
 * dell'utente per pacchetto: profilo (salvato solo qui), poi finestre.h, che chiede
 * subito al telefono misura e densita' nuove (ridimensionamento con densita'). */
static void compat_imposta_spunta(App *a, bool attiva)
{
    a->compat_aggiorna = true;
    gtk_check_button_set_active(GTK_CHECK_BUTTON(a->menu_compat), attiva);
    a->compat_aggiorna = false;
}

static void compat_salva(App *a)
{
    a->salvataggi_compat++;
    ph_srv_profilo_salva(a->srv);
}

static void compat_toggled(GtkCheckButton *b, gpointer dati)
{
    App *a = dati;
    if (a->compat_aggiorna)
        return;
    unsigned id = a->menu_fin;
    Fin *f = id != 0 ? trova_fin(a, id) : NULL;
    bool attiva = gtk_check_button_get_active(b);
    PhCfgTelefono *pf = ph_srv_profilo(a->srv);
    PhErrore e = {{0}};
    if (f == NULL || f->specchio || pf == NULL) {
        compat_imposta_spunta(a, !attiva);
        return;
    }
    if (ph_cfg_telefono_imposta_compatibilita(pf, f->pacchetto, attiva, &e) != 0) {
        /* Elenco pieno o nome non valido: nulla e' cambiato, la spunta torna com'era.
         * L'utente vede la frase della config solo se e' quella tradotta dell'elenco
         * pieno; ogni altro testo (italiano, tecnico) va nel log. */
        compat_imposta_spunta(a, !attiva);
        const char *piena = ph_t("Troppe app in compatibilità.");
        if (strcmp(e.testo, piena) != 0)
            fprintf(stderr, "finestra-app: compatibilita' non cambiata: %s\n", e.testo);
        ph_shell_avviso(a->sh, ph_t("Compatibilità"),
                        strcmp(e.testo, piena) == 0 ? piena
                                                    : ph_t("Non riesco a cambiare la compatibilità di questa app."));
        ph_shell_popup_chiudi(a->sh);
        return;
    }
    compat_salva(a);
    if (ph_fin_imposta_compatibilita(a->d, f->id, attiva, ora(), &e) != 0) {
        /* Rifiutata (per esempio in registrazione: la voce e' disattivata, ma per
         * sicurezza): si annulla anche nel profilo. */
        ph_cfg_telefono_imposta_compatibilita(pf, f->pacchetto, !attiva, NULL);
        compat_salva(a);
        compat_imposta_spunta(a, !attiva);
    } else {
        /* La richiesta con misura E densita' esce da ph_fin_avanza al prossimo giro. */
        sincronizza(a);
    }
    ph_shell_popup_chiudi(a->sh);
}

static void apri_menu(App *a, Fin *f, GtkWidget *pulsante)
{
    if (a->menu == NULL) {
        a->menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(a->menu, "ph-menu");
        gtk_widget_set_halign(a->menu, GTK_ALIGN_START);
        gtk_widget_set_valign(a->menu, GTK_ALIGN_START);
        a->menu_ruota = voce_menu(ph_t("Ruota"), G_CALLBACK(menu_ruota), a);
        gtk_box_append(GTK_BOX(a->menu), a->menu_ruota);
        /* Compatibilita': tra Ruota e Chiudi app, con sotto la spiegazione piccola e grigia. */
        a->menu_compat_riga = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        a->menu_compat = gtk_check_button_new_with_label(ph_t("Compatibilità"));
        gtk_widget_add_css_class(a->menu_compat, "ph-finestra-app-compat");
        g_signal_connect(a->menu_compat, "toggled", G_CALLBACK(compat_toggled), a);
        GtkWidget *spiega = gtk_label_new(ph_t("Per le app che si vedono troppo piccole"));
        gtk_widget_add_css_class(spiega, "ph-finestra-app-spiegazione");
        gtk_label_set_xalign(GTK_LABEL(spiega), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(spiega), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(spiega), 26);
        gtk_box_append(GTK_BOX(a->menu_compat_riga), a->menu_compat);
        gtk_box_append(GTK_BOX(a->menu_compat_riga), spiega);
        gtk_box_append(GTK_BOX(a->menu), a->menu_compat_riga);
        gtk_box_append(GTK_BOX(a->menu),
                       voce_menu(ph_t("Chiudi app"), G_CALLBACK(menu_chiudi_app), a));
        gtk_widget_set_visible(a->menu, FALSE);
        ph_shell_strato_aggiungi(a->sh, PH_STRATO_PANNELLI, a->menu);
        /* Superficie di vetro CHIARO; raggio = quello di .ph-menu in base.css. */
        ph_shell_vetro_registra(a->sh, a->menu, 10, PH_VETRO_CHIARO);
    }
    /* Sotto il pulsante, allineato al suo bordo destro. */
    graphene_point_t zero = GRAPHENE_POINT_INIT(0.0f, 0.0f), p;
    if (!gtk_widget_compute_point(pulsante, a->radice, &zero, &p))
        p = zero;
    /* "Ruota" e' una voce di ridimensionamento: per le app solo verticali (e per le
     * finestre che non si possono ridimensionare ora) resta visibile ma
     * DISATTIVATA. finestre.h la rifiuta anche da massimizzata, agganciata e
     * per lo specchio. */
    PhFinInfo info;
    bool ruotabile = ph_fin_info(a->d, f->id, &info, NULL) == 0 && info.puo_ridimensionare &&
                     info.stato == PH_FIN_NORMALE;
    gtk_widget_set_sensitive(a->menu_ruota, ruotabile);
    /* Compatibilita': assente nello specchio (la sua misura la decide il telefono),
     * DISATTIVATA durante una registrazione (la misura del flusso non puo' cambiare),
     * spunta dal profilo del telefono. */
    gtk_widget_set_visible(a->menu_compat_riga, !f->specchio);
    gtk_widget_set_sensitive(a->menu_compat_riga, !f->reg_attiva && !info.registrando);
    PhCfgTelefono *pf = ph_srv_profilo(a->srv);
    compat_imposta_spunta(a, pf != NULL && !f->specchio &&
                                 ph_cfg_telefono_in_compatibilita(pf, f->pacchetto));
    /* Un widget nascosto si misura 0: va mostrato prima di misurarlo. */
    gtk_widget_set_visible(a->menu, TRUE);
    int nat = 0;
    gtk_widget_measure(a->menu, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &nat, NULL, NULL);
    gtk_widget_set_margin_start(a->menu, MAX(0, (int)p.x + PH_FIN_PULS - nat));
    gtk_widget_set_margin_top(a->menu, (int)p.y + PH_FIN_PULS + 4);
    a->menu_fin = f->id;
    ph_shell_popup_apri(a->sh, PH_POPUP_ALTRI_COMANDI, a->menu, menu_chiuso, a);
}

/* ======================================================================
 * Ciclo di vita delle finestre. */

static void salva_ricordi(App *a)
{
    const PhCfgPreferenze *pr = ph_srv_preferenze(a->srv);
    PhCfgTelefono *pf = ph_srv_profilo(a->srv);
    if (pr == NULL || pf == NULL || !pr->riapri_dove_eri)
        return;
    PhFinRicordo r[PH_FIN_MEM_MAX];
    size_t n = ph_fin_esporta(a->d, r, PH_FIN_MEM_MAX);
    pf->n_finestre = ph_fa_ricordi_a_profilo(a->chiavi, r, n, pf->finestre, PH_CFG_MAX_FINESTRE);
    ph_srv_profilo_salva(a->srv);
}

static void chiudi_fin(App *a, Fin *f, bool togli_task)
{
    if (f->chiudendo)
        return;
    f->chiudendo = true;
    unsigned id = f->id;

    /* Chiude i tocchi (e azzera il gesto se era un tocco); uno spostamento in
     * corso lo annulla gia' ph_fin_chiudi, qui basta dimenticarlo. */
    chiudi_tocchi(a, f);
    if (a->gesto == GESTO_FINESTRA && a->gesto_fin == id)
        a->gesto = GESTO_NESSUNO;
    if (a->menu_fin == id && ph_shell_popup_aperto(a->sh) == PH_POPUP_ALTRI_COMANDI)
        ph_shell_popup_chiudi(a->sh);
    a->menu_fin = 0;

    /* La superficie ferma il suo thread e chiude il flusso (togli_task =
     * app tolta dai recenti, 4.6); poi il widget e la finestra di finestre.h. */
    /* Una registrazione in corso si ferma in modo ordinato (il file resta
     * completo); l'avviso non arriva piu' una volta chiusa la superficie. */
    if (ph_superficie_registrando(f->sup))
        ph_superficie_registra_ferma(f->sup);
    ph_superficie_chiudi(f->sup, togli_task && !f->specchio);
    PH_FA_CORNICE(f->cornice)->fin = NULL;
    ph_shell_libero_togli(a->cont, f->cornice);
    ph_fin_chiudi(a->d, id, ora(), NULL);
    salva_ricordi(a);
    g_ptr_array_remove(a->fin, f); /* libera f */
    sincronizza(a);
    cambiato(a);
}

typedef struct {
    GWeakRef cont;
    unsigned id;
    bool togli_task;
} Differita;

static void differita_esegui(void *dati)
{
    Differita *d = dati;
    GtkWidget *cont = g_weak_ref_get(&d->cont);
    if (cont == NULL)
        return;
    App *a = app_di(cont);
    if (a != NULL) {
        Fin *f = trova_fin(a, d->id);
        if (f != NULL)
            chiudi_fin(a, f, d->togli_task);
    }
    g_object_unref(cont);
}

static void differita_libera(gpointer p)
{
    Differita *d = p;
    g_weak_ref_clear(&d->cont);
    g_free(d);
}

/* Chiusura da dentro un'avviso della superficie o di un pulsante: mai nel
 * mezzo della chiamata che la causa (la superficie non puo' essere chiusa da
 * un suo avviso). La finestra torna a essere cercata per id: se nel frattempo
 * e' sparita non succede nulla. */
static void chiudi_differita(App *a, unsigned id, bool togli_task)
{
    Fin *f = trova_fin(a, id);
    if (f == NULL || f->chiudendo)
        return;
    Differita *d = g_new0(Differita, 1);
    g_weak_ref_init(&d->cont, a->cont);
    d->id = id;
    d->togli_task = togli_task;
    ph_shell_consegna(differita_esegui, d, differita_libera);
}

static void minimizza(App *a, Fin *f)
{
    if (ph_fin_minimizza(a->d, f->id, ora(), NULL) != 0)
        return;
    if (a->gesto == GESTO_FINESTRA && a->gesto_fin == f->id)
        a->gesto = GESTO_NESSUNO;
    if (a->menu_fin == f->id && ph_shell_popup_aperto(a->sh) == PH_POPUP_ALTRI_COMANDI)
        ph_shell_popup_chiudi(a->sh);
    sincronizza(a); /* widget nascosto e, dopo aver chiuso ogni tocco, pausa */
    cambiato(a);
}

static void ruota(App *a, Fin *f)
{
    if (ph_fin_ruota(a->d, f->id, ora(), NULL) != 0)
        return; /* rifiutata: nessun effetto */
    sincronizza(a);
    salva_ricordi(a);
}

static void massimizza(App *a, Fin *f)
{
    if (ph_fin_massimizza_ripristina(a->d, f->id, ora(), NULL) != 0)
        return;
    sincronizza(a);
    salva_ricordi(a);
}

static void azione_pulsante(Fin *f, PhFinPulsante p, GtkWidget *pulsante)
{
    App *a = f->app;
    switch (p) {
    case PH_FIN_PULS_INDIETRO:
        if (!f->in_pausa)
            ph_superficie_indietro(f->sup);
        break;
    case PH_FIN_PULS_SCREENSHOT:
        scatta(a, f, false);
        break;
    case PH_FIN_PULS_REGISTRA:
        alterna_registrazione(a, f);
        break;
    case PH_FIN_PULS_ALTRI:
        apri_menu(a, f, pulsante);
        break;
    case PH_FIN_PULS_MINIMIZZA:
        minimizza(a, f);
        break;
    case PH_FIN_PULS_INGRANDISCI:
        massimizza(a, f);
        break;
    case PH_FIN_PULS_CHIUDI:
        chiudi_differita(a, f->id, true);
        break;
    default:
        break;
    }
}

/* Avvisi della superficie (arrivano sul thread GTK, mai dopo la chiusura). */
static void sup_avviso(GtkWidget *sup, PhSuperficieEvento ev, int larghezza, int altezza, void *dati)
{
    (void)sup;
    Fin *f = dati;
    App *a = f->app;
    switch (ev) {
    case PH_SUP_EV_MISURA:
        /* Una riga solo quando la misura cambia (la superficie la avvisa anche all'apertura). */
        if (larghezza != f->ott_w || altezza != f->ott_h) {
            f->ott_w = larghezza;
            f->ott_h = altezza;
            ph_shell_decisione("misura del flusso", "ottenuta %dx%d (chiesta %dx%d a %d dpi)",
                               larghezza, altezza, f->req_w, f->req_h, f->req_dens);
        }
        /* La misura vera del video: serve all'input (4.10); i tocchi calcolati
         * su una misura vecchia li scarta il telefono. */
        ph_fin_dichiara_video(a->d, f->id, larghezza, altezza, NULL);
        break;
    case PH_SUP_EV_APP_CHIUSA:
        /* L'app si e' chiusa da sola: niente "togli dai recenti". */
        chiudi_differita(a, f->id, false);
        break;
    case PH_SUP_EV_CHIUDI:
        chiudi_differita(a, f->id, true);
        break;
    case PH_SUP_EV_ERRORE:
        ph_shell_avviso(a->sh, f->titolo,
                        ph_t("Il flusso dell'app si e' interrotto: la finestra e' stata chiusa."));
        chiudi_differita(a, f->id, false);
        break;
    }
}

/* ======================================================================
 * Aprire una finestra (gestore apri_app e specchio). */

/* Sullo schermo la frase tradotta (piu' la frase dei servizi, gia' pronta per
 * l'utente, se c'e'); il dettaglio tecnico di finestre.h va su stderr. */
static void avviso_apertura(App *a, const char *dettaglio, const char *frase_utente)
{
    if (dettaglio != NULL && dettaglio[0] != '\0')
        fprintf(stderr, "finestra-app: apertura non riuscita: %s\n", dettaglio);
    ph_shell_avviso(a->sh, ph_t("Non riesco ad aprire l'app"),
                    frase_utente != NULL ? frase_utente : "");
}

/* pacchetto "" = specchio dello schermo reale (4.7). pagina: PH_SHELL_PAGINA_NESSUNA = avvio
 * normale; altrimenti, finestra nuova: la pagina e' nella richiesta di apertura del flusso;
 * finestra gia' aperta: portata davanti come sempre e la pagina e' mandata al suo flusso (senza
 * attese, esito ignorato: un errore del telefono non chiude ne' sporca la finestra). */
static void apri(App *a, const char *pacchetto, PhShellPagina pagina)
{
    bool specchio = pacchetto[0] == '\0';
    PhErrore e = {{0}};
    char chiave[PH_FIN_APP_MAX];
    if (!ph_fa_chiave_di(a->chiavi, pacchetto, chiave)) {
        avviso_apertura(a, NULL, NULL);
        return;
    }
    const PhCfgPreferenze *pr = ph_srv_preferenze(a->srv);
    ph_fin_riapri_dove_eri(a->d, pr != NULL && pr->riapri_dove_eri);

    const PhSrvApp *info = specchio ? NULL : ph_srv_app_cerca(a->srv, pacchetto);
    /* Titolo: il nome dell'app (o il pacchetto se l'elenco non e' ancora
     * arrivato; si aggiorna all'arrivo). */
    char *titolo = g_strdup(specchio ? ph_t("Schermo del telefono")
                                     : (info != NULL && info->nome != NULL ? info->nome : pacchetto));
    PhFinSpec spec = ph_fa_scegli_spec(info, specchio);
    bool nuova = false;
    unsigned id = ph_fin_apri(a->d, chiave, titolo, &spec, ora(), &nuova, &e);
    if (id == 0) {
        avviso_apertura(a, e.testo, NULL);
        g_free(titolo);
        return;
    }
    if (!nuova) {
        /* Gia' aperta: ph_fin_apri l'ha ripristinata (se minimizzata), portata
         * davanti e resa attiva; qui i widget la seguono. */
        Fin *f = trova_fin(a, id);
        g_free(titolo);
        if (f != NULL) {
            attiva_effetti(a, f); /* prima: se era minimizzata il flusso riprende, poi la pagina */
            if (pagina != PH_SHELL_PAGINA_NESSUNA && !specchio)
                ph_superficie_pagina(f->sup, (uint8_t)pagina);
        }
        return;
    }

    /* App gia' in compatibilita' (profilo): PRIMA di leggere misura e densita' per
     * aprire il flusso, cosi' l'app parte gia' con la densita' giusta. */
    if (!specchio) {
        const PhCfgTelefono *pfc = ph_srv_profilo(a->srv);
        if (pfc != NULL && ph_cfg_telefono_in_compatibilita(pfc, pacchetto) &&
            ph_fin_imposta_compatibilita(a->d, id, true, ora(), &e) != 0)
            fprintf(stderr, "finestra-app: compatibilita' non applicata: %s\n", e.testo);
    }
    int w = SPECCHIO_FLUSSO_W, h = SPECCHIO_FLUSSO_H, dens = 0; /* specchio: nessuna densita' */
    if (!specchio && ph_fin_misura_flusso_densita(a->d, id, &w, &h, &dens, &e) != 0) {
        ph_fin_chiudi(a->d, id, ora(), NULL);
        avviso_apertura(a, e.testo, NULL);
        g_free(titolo);
        return;
    }

    Fin *f = g_new0(Fin, 1);
    f->app = a;
    f->id = id;
    f->pacchetto = g_strdup(pacchetto);
    f->titolo = titolo;
    g_strlcpy(f->chiave, chiave, sizeof f->chiave);
    f->specchio = specchio;
    f->titolo_provvisorio = !specchio && info == NULL;

    registra_misura(a, f, "apertura", w, h, dens);
    PhSrvFlussoRichiesta r = {.pacchetto = pacchetto, .larghezza = w, .altezza = h, .densita = dens,
                              .pagina = specchio ? 0 : (uint8_t)pagina};
    GtkWidget *sup = ph_superficie_nuova(a->sh, &r, sup_avviso, f, &e);
    if (sup == NULL) {
        ph_fin_chiudi(a->d, id, ora(), NULL);
        /* Il testo di e e' tecnico (italiano): nel log; a schermo una frase tradotta. */
        avviso_apertura(a, e.testo, ph_t("Non riesco ad avviare il video dell'app."));
        fin_libera(f);
        return;
    }
    f->sup = sup;
    f->cornice = cornice_nuova(f, sup);

    PhFinInfo i;
    if (ph_fin_info(a->d, id, &i, NULL) != 0)
        memset(&i, 0, sizeof i);
    ph_shell_libero_metti(a->cont, f->cornice, i.rect.x, i.rect.y, i.rect.w, i.rect.h);
    /* La cornice si sfoca da se' il titolo: chi la ritrae da sopra (barra,
     * pannelli) deve prenderla dai figli (shell.h, "Vetro"). */
    ph_shell_vetro_proprio(a->sh, f->cornice);
    g_ptr_array_add(a->fin, f);
    attiva_effetti(a, f);
    /* Finestra NUOVA (qui si arriva solo da ph_fin_apri con nuova = true): conta
     * un'apertura nel profilo e chiede il salvataggio (differito dal pezzo comune).
     * Mai nel registro: e' uno storico d'uso. */
    if (ph_fa_conta_apertura(ph_srv_profilo(a->srv), pacchetto, nuova, specchio,
                             (int64_t)(g_get_real_time() / G_USEC_PER_SEC)))
        ph_srv_profilo_salva(a->srv);
}

/* ======================================================================
 * Gestore PhShellGestoreFinestre. */
static void gf_apri_app(void *io, const char *pacchetto)
{
    if (pacchetto == NULL || pacchetto[0] == '\0')
        return;
    apri(io, pacchetto, PH_SHELL_PAGINA_NESSUNA);
}

/* Pagina fuori elenco: scartata (ph_shell_apri_app_pagina la filtra prima, qui si ripete per chi
 * chiama il gestore da solo). */
static void gf_apri_pagina(void *io, const char *pacchetto, PhShellPagina pagina)
{
    if (pacchetto == NULL || pacchetto[0] == '\0')
        return;
    if (pagina != PH_SHELL_PAGINA_NESSUNA && pagina != PH_SHELL_PAGINA_BATTERIA &&
        pagina != PH_SHELL_PAGINA_MEMORIA) {
        fprintf(stderr, "finestra-app: pagina %d fuori elenco, scartata\n", (int)pagina);
        return;
    }
    apri(io, pacchetto, pagina);
}

static size_t gf_elenco(void *io, PhShellFinestra *out, size_t max)
{
    App *a = io;
    size_t n = 0;
    for (guint k = 0; k < a->fin->len && n < max; k++) {
        Fin *f = g_ptr_array_index(a->fin, k);
        PhFinInfo i;
        if (ph_fin_info(a->d, f->id, &i, NULL) != 0)
            continue;
        PhShellFinestra *o = &out[n++];
        memset(o, 0, sizeof *o);
        o->id = f->id;
        g_strlcpy(o->pacchetto, f->pacchetto, sizeof o->pacchetto);
        g_strlcpy(o->titolo, f->titolo, sizeof o->titolo);
        o->minimizzata = i.minimizzata;
        o->attiva = i.attiva;
        o->specchio = f->specchio;
    }
    return n;
}

static void gf_clic_barra(void *io, unsigned id)
{
    App *a = io;
    Fin *f = trova_fin(a, id);
    if (f == NULL)
        return;
    PhFinAzione az = PH_FIN_AZ_NESSUNA;
    if (ph_fin_clic_barra(a->d, id, ora(), &az, NULL) != 0)
        return;
    switch (az) {
    case PH_FIN_AZ_MINIMIZZATA:
        if (a->gesto == GESTO_FINESTRA && a->gesto_fin == id)
            a->gesto = GESTO_NESSUNO;
        if (a->menu_fin == id && ph_shell_popup_aperto(a->sh) == PH_POPUP_ALTRI_COMANDI)
            ph_shell_popup_chiudi(a->sh);
        sincronizza(a);
        cambiato(a);
        break;
    case PH_FIN_AZ_RIPRISTINATA:
    case PH_FIN_AZ_DAVANTI:
        attiva_effetti(a, f);
        break;
    case PH_FIN_AZ_NESSUNA:
        break;
    }
}

/* Dal menu del clic destro sulla barra ("Chiudi app"): stesso effetto del
 * pulsante Chiudi (ferma la registrazione, chiude i tocchi, toglie l'app dai
 * recenti; lo specchio si chiude senza recenti). Anche minimizzata; id
 * sconosciuto: nulla. */
static void gf_chiudi(void *io, unsigned id)
{
    App *a = io;
    /* Differita, come Chiudi e Ctrl+W: la barra sta ancora gestendo il suo clic
     * e un avviso FINESTRE emesso qui rientrerebbe a meta' del suo giro. */
    chiudi_differita(a, id, true);
}

static void gf_specchio(void *io, bool mostra)
{
    App *a = io;
    if (mostra) {
        apri(a, "", PH_SHELL_PAGINA_NESSUNA);
        return;
    }
    char chiave[PH_FIN_APP_MAX];
    if (!ph_fa_chiave_di(a->chiavi, "", chiave))
        return;
    unsigned id = ph_fin_cerca_app(a->d, chiave);
    if (id != 0)
        chiudi_differita(a, id, false); /* differita: vedi gf_chiudi */
}

static bool gf_specchio_aperto(void *io)
{
    App *a = io;
    char chiave[PH_FIN_APP_MAX];
    return ph_fa_chiave_di(a->chiavi, "", chiave) && ph_fin_cerca_app(a->d, chiave) != 0;
}

/* ======================================================================
 * Ascolto di shell e servizi. */

static double scala_host(App *a)
{
    GtkWindow *w = ph_shell_finestra(a->sh);
    GdkSurface *s = w != NULL ? gtk_native_get_surface(GTK_NATIVE(w)) : NULL;
    double sc = s != NULL ? gdk_surface_get_scale(s) : 1.0;
    return sc >= 0.1 && sc <= 64.0 && isfinite(sc) ? sc : 1.0;
}

/* La misura della radice e la scala della shell passano a finestre.h (che
 * lavora in pixel fisici e scala). */
static void area_aggiorna(App *a)
{
    int lw = 0, lh = 0;
    ph_shell_area(a->sh, &lw, &lh);
    if (lw <= 0 || lh <= 0)
        return;
    a->scala = scala_host(a);
    PhErrore e = {{0}};
    int fw = MAX(1, (int)lround(lw * a->scala));
    int fh = MAX(1, (int)lround(lh * a->scala));
    if (ph_fin_imposta_host(a->d, fw, fh, a->scala, ora(), &e) != 0) {
        fprintf(stderr, "finestra-app: area non accettata: %s\n", e.testo);
        return;
    }
    sincronizza(a);
}

static void scala_cambiata(GdkSurface *s, GParamSpec *p, gpointer dati)
{
    (void)s;
    (void)p;
    App *a = app_di(GTK_WIDGET(dati));
    if (a != NULL)
        area_aggiorna(a);
}

static void contenitore_mappato(GtkWidget *cont, gpointer dati)
{
    App *a = dati;
    GtkWindow *w = ph_shell_finestra(a->sh);
    GdkSurface *s = w != NULL ? gtk_native_get_surface(GTK_NATIVE(w)) : NULL;
    if (s != NULL && s != a->sup_host) {
        a->sup_host = s;
        g_signal_connect_object(s, "notify::scale", G_CALLBACK(scala_cambiata), cont,
                                G_CONNECT_DEFAULT);
    }
    area_aggiorna(a);
}

static void ev_area(PhShell *sh, PhShellEvento ev, void *dati)
{
    (void)sh;
    (void)ev;
    area_aggiorna(dati);
}

/* Finestra host non visibile (4.14): tutte le superfici in pausa; al ritorno
 * riprendono SOLO quelle non minimizzate (lo decide ph_fa_pausa_voluta). */
static void ev_host(PhShell *sh, PhShellEvento ev, void *dati)
{
    (void)ev;
    App *a = dati;
    bool visibile = ph_shell_host_visibile(sh);
    if (visibile == a->host_visibile)
        return;
    a->host_visibile = visibile;
    if (!visibile)
        ph_shell_decisione("flussi in pausa", "finestra host non visibile");
    else
        ph_shell_decisione("flussi ripresi", "finestra host di nuovo visibile");
    sincronizza(a);
}

/* Densita' delle app (4.4): quella del profilo se impostata, altrimenti quella
 * del telefono (420 come segnaposto finche' non e' collegato). */
static void qualita_aggiorna(App *a)
{
    PhErrore e = {{0}};
    PhCfgTelefono *pf = ph_srv_profilo(a->srv);
    double dens = densita_app(a);
    if (ph_fin_imposta_qualita(a->d, dens, PH_FIN_DENSITA_RIF, PH_FIN_FLUSSO_TETTO, ora(), &e) != 0)
        fprintf(stderr, "finestra-app: densita' non accettata: %s\n", e.testo);
    /* Densita' scelta dall'utente: FISSA; 0: regola automatica (tablet). Si
     * rifa' a ogni cambio di profilo e di collegamento. */
    if (ph_fin_imposta_densita_automatica(a->d, ph_fa_densita_automatica(pf != NULL ? pf->densita_app : 0),
                                          ora(), &e) != 0)
        fprintf(stderr, "finestra-app: regola della densita' non accettata: %s\n", e.testo);
    sincronizza(a);
}

/* Schermo del telefono (pixel e densita' propria) per le misure predefinite delle
 * finestre aperte DOPO: arriva con il collegamento (PhSrvCollegamento). Se la
 * misura non e' nota (telefono non collegato) non si inventa nulla: finestre.h usa
 * il suo ripiego. */
static void telefono_aggiorna(App *a)
{
    PhSrvCollegamento c;
    ph_srv_collegamento(a->srv, &c);
    if (!c.ha_telefono || c.schermo_w <= 0 || c.schermo_h <= 0 || c.densita <= 0)
        return;
    PhErrore e = {{0}};
    if (ph_fin_imposta_telefono(a->d, c.schermo_w, c.schermo_h, c.densita, ora(), &e) != 0)
        fprintf(stderr, "finestra-app: schermo del telefono non accettato: %s\n", e.testo);
}

static void ricordi_importa(App *a)
{
    PhCfgTelefono *pf = ph_srv_profilo(a->srv);
    if (pf == NULL)
        return;
    PhFinRicordo r[PH_FIN_MEM_MAX];
    size_t n = ph_fa_ricordi_da_profilo(a->chiavi, pf->finestre, MIN(pf->n_finestre, (size_t)PH_CFG_MAX_FINESTRE), r,
                                   PH_FIN_MEM_MAX);
    ph_fin_importa(a->d, r, n);
}

/* Cambio di telefono/profilo: le finestre gia' aperte rileggono dal profilo NUOVO se la loro
 * app e' in compatibilita' e allineano finestre.h (la spunta del menu si rilegge a ogni
 * apertura del menu). Non salva nulla: il profilo e' appena stato caricato. */
static void compat_allinea(App *a)
{
    const PhCfgTelefono *pf = ph_srv_profilo(a->srv);
    if (pf == NULL)
        return;
    bool cambio = false;
    for (guint k = 0; k < a->fin->len; k++) {
        Fin *f = g_ptr_array_index(a->fin, k);
        if (f->specchio)
            continue;
        bool voluta = ph_cfg_telefono_in_compatibilita(pf, f->pacchetto);
        if (voluta == ph_fin_compatibilita(a->d, f->id))
            continue;
        PhErrore e = {{0}};
        if (ph_fin_imposta_compatibilita(a->d, f->id, voluta, ora(), &e) == 0)
            cambio = true;
        else /* per esempio in registrazione: resta com'e', senza toccare il profilo */
            fprintf(stderr, "finestra-app: compatibilita' non allineata: %s\n", e.testo);
    }
    if (cambio)
        sincronizza(a); /* la richiesta con misura e densita' esce dal tick */
}

static void srv_profilo(PhServizi *s, PhSrvEvento ev, const char *arg, void *dati)
{
    (void)s;
    (void)ev;
    (void)arg;
    App *a = dati;
    qualita_aggiorna(a);
    ricordi_importa(a);
    compat_allinea(a);
}

static void srv_collegamento(PhServizi *s, PhSrvEvento ev, const char *arg, void *dati)
{
    (void)s;
    (void)ev;
    (void)arg;
    telefono_aggiorna(dati);
    qualita_aggiorna(dati);
}

/* Arriva l'elenco delle app: i titoli provvisori (pacchetto) diventano nomi. */
static void srv_app(PhServizi *s, PhSrvEvento ev, const char *arg, void *dati)
{
    (void)ev;
    (void)arg;
    App *a = dati;
    bool cambio = false, forma = false;
    for (guint k = 0; k < a->fin->len; k++) {
        Fin *f = g_ptr_array_index(a->fin, k);
        if (f->specchio)
            continue;
        const PhSrvApp *info = ph_srv_app_cerca(s, f->pacchetto);
        if (info == NULL)
            continue;
        /* Titoli provvisori (pacchetto) -> nomi. */
        if (f->titolo_provvisorio && info->nome != NULL) {
            g_free(f->titolo);
            f->titolo = g_strdup(info->nome);
            f->titolo_provvisorio = false;
            gtk_label_set_text(GTK_LABEL(PH_FA_CORNICE(f->cornice)->nome), f->titolo);
            ph_shell_etichetta(f->cornice, f->titolo, NULL);
            cambio = true;
        }
        /* "Solo verticale" arrivato o cambiato DOPO l'apertura (elenco in ritardo o
         * aggiornato): la finestra gia' aperta prende la forma giusta. */
        PhFinInfo i;
        if (ph_fin_info(a->d, f->id, &i, NULL) == 0 && i.solo_verticale != info->solo_verticale) {
            PhErrore e = {{0}};
            if (ph_fin_imposta_solo_verticale(a->d, f->id, info->solo_verticale, ora(), &e) == 0) {
                if (a->gesto == GESTO_FINESTRA && a->gesto_fin == f->id)
                    a->gesto = GESTO_NESSUNO; /* finestre.h ha chiuso il gesto */
                forma = true;
            } else {
                fprintf(stderr, "finestra-app: forma della finestra non cambiata: %s\n", e.testo);
            }
        }
    }
    if (forma)
        sincronizza(a);
    if (cambio)
        cambiato(a);
}

/* La radice si distrugge. strati_dispose (comune/shell.c) stacca prima i
 * figli e solo dopo emette "destroy" della radice: per questo teniamo un
 * riferimento al contenitore (preso all'installazione) e la pulizia e'
 * agganciata al "destroy" della RADICE, non a quello del contenitore (che
 * arriverebbe a ascolti ancora attivi). Prima si smette di ascoltare, poi si
 * chiudono le superfici e si libera, per ultimo si rilascia il contenitore.
 * Le consegne ancora in coda trovano il contenitore senza App e non fanno nulla. */
static void distruggi(GtkWidget *radice, gpointer dati)
{
    (void)radice;
    App *a = dati;
    GtkWidget *cont = a->cont;
    ph_srv_smetti(a->srv, a->srv_profilo);
    ph_srv_smetti(a->srv, a->srv_coll);
    ph_srv_smetti(a->srv, a->srv_app);
    /* ph_shell_libera puo' essere gia' stata chiamata: lo si vede dal dato che
     * toglie dalla radice. */
    if (ph_shell_radice_viva(a->radice)) {
        ph_shell_smetti(a->sh, a->ev_area);
        ph_shell_smetti(a->sh, a->ev_host);
        ph_shell_registra_finestre(a->sh, NULL);
    }
    g_object_set_data(G_OBJECT(cont), CHIAVE_APP, NULL);
    if (a->pz_timer != 0) {
        g_source_remove(a->pz_timer);
        a->pz_timer = 0;
    }
    if (a->tick != 0) {
        gtk_widget_remove_tick_callback(cont, a->tick);
        a->tick = 0;
    }
    for (guint k = 0; k < a->fin->len; k++) {
        Fin *f = g_ptr_array_index(a->fin, k);
        PH_FA_CORNICE(f->cornice)->fin = NULL;
        ph_superficie_chiudi(f->sup, false);
    }
    gtk_im_context_set_client_widget(a->im, NULL);
    g_object_unref(a->im);
    g_ptr_array_free(a->fin, TRUE);
    ph_fin_libera(a->d);
    ph_fa_chiavi_libera(a->chiavi);
    g_free(a);
    g_object_unref(cont);
}

void ph_finestra_app_installa(PhShell *sh)
{
    static gsize risorse = 0;
    if (g_once_init_enter(&risorse)) {
        ph_risorse_finestra_app_register_resource();
        g_once_init_leave(&risorse, 1);
    }
    ph_shell_stile_carica("/it/yasde/shell/finestra-app.css");

    App *a = g_new0(App, 1);
    a->sh = sh;
    a->srv = ph_shell_servizi(sh);
    a->radice = ph_shell_radice(sh);
    a->scala = 1.0;
    ph_fa_pz_inizializza(&a->pz);
    a->host_visibile = ph_shell_host_visibile(sh);
    a->chiavi = ph_fa_chiavi_nuove();
    a->fin = g_ptr_array_new_with_free_func(fin_libera);

    /* Misura di partenza provvisoria: quella vera arriva con l'area. */
    int lw = 0, lh = 0;
    ph_shell_area(sh, &lw, &lh);
    PhErrore e = {{0}};
    a->d = ph_fin_crea(lw > 0 ? lw : 1280, lh > 0 ? lh : 800, 1.0, &e);
    if (a->d == NULL) {
        fprintf(stderr, "finestra-app: %s\n", e.testo);
        g_ptr_array_free(a->fin, TRUE);
        ph_fa_chiavi_libera(a->chiavi);
        g_free(a);
        return;
    }
    /* Il piede diagnostico resta nascosto (decisione del committente). */
    ph_fin_imposta_pie(a->d, false, ora(), NULL);

    a->cont = ph_shell_libero_nuovo();
    /* Il contenitore e' l'AREA UTILE (la radice meno la barra di 48 in basso) e
     * ritaglia i figli: una finestra trascinata oltre il bordo inferiore (resta
     * visibile il titolo) viene TAGLIATA li', mai disegnata sopra la barra della
     * shell ne' fuori dalla radice. L'origine resta in alto a sinistra: le
     * coordinate dei gesti non cambiano. */
    gtk_widget_set_margin_bottom(a->cont, PH_FIN_BARRA_H);
    gtk_widget_set_overflow(a->cont, GTK_OVERFLOW_HIDDEN);
    g_object_set_data(G_OBJECT(a->cont), CHIAVE_APP, a);
    ph_shell_strato_aggiungi(sh, PH_STRATO_FINESTRE, a->cont);
    g_object_ref(a->cont); /* vedi distruggi() */

    /* Gesti sul contenitore, in cattura: ph_fin_trova decide chi e che zona. */
    GtkGesture *g = gtk_gesture_drag_new();
    /* Tutti i pulsanti: gesto_inizia_pulsante sceglie quali valgono e dove. Un pulsante alla volta:
     * con un gesto attivo, la pressione di un ALTRO pulsante fa azzerare il controller a GTK
     * (gtkgesturesingle.c, gtk_gesture_single_handle_event: gtk_event_controller_reset), quindi
     * arriva "cancel" e gesto_annulla_tutto chiude il gesto in corso. */
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), 0);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(g), GTK_PHASE_CAPTURE);
    g_signal_connect(g, "drag-begin", G_CALLBACK(gesto_inizia), a);
    g_signal_connect(g, "drag-update", G_CALLBACK(gesto_aggiorna), a);
    g_signal_connect(g, "drag-end", G_CALLBACK(gesto_fine), a);
    g_signal_connect(g, "cancel", G_CALLBACK(gesto_annulla), a);
    gtk_widget_add_controller(a->cont, GTK_EVENT_CONTROLLER(g));

    GtkEventController *mo = gtk_event_controller_motion_new();
    gtk_event_controller_set_propagation_phase(mo, GTK_PHASE_CAPTURE);
    g_signal_connect(mo, "motion", G_CALLBACK(puntatore_mosso), a);
    g_signal_connect(mo, "enter", G_CALLBACK(puntatore_mosso), a);
    g_signal_connect(mo, "leave", G_CALLBACK(puntatore_uscito), a);
    gtk_widget_add_controller(a->cont, mo);

    GtkEventController *ro = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    gtk_event_controller_set_propagation_phase(ro, GTK_PHASE_CAPTURE);
    g_signal_connect(ro, "scroll", G_CALLBACK(rotella), a);
    gtk_widget_add_controller(a->cont, ro);

    /* Tastiera: fuoco alla finestra attiva (le cornici sono focalizzabili). */
    a->im = gtk_im_multicontext_new();
    gtk_im_context_set_client_widget(a->im, a->cont);
    g_signal_connect(a->im, "commit", G_CALLBACK(testo_composto), a);
    GtkGesture *zm = gtk_gesture_zoom_new();
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(zm), GTK_PHASE_CAPTURE);
    g_signal_connect(zm, "begin", G_CALLBACK(zoom_inizia), a);
    g_signal_connect(zm, "scale-changed", G_CALLBACK(zoom_cambiato), a);
    g_signal_connect(zm, "end", G_CALLBACK(zoom_finito), a);
    g_signal_connect(zm, "cancel", G_CALLBACK(zoom_finito), a);
    gtk_widget_add_controller(a->cont, GTK_EVENT_CONTROLLER(zm));

    GtkEventController *ta = gtk_event_controller_key_new();
    gtk_event_controller_key_set_im_context(GTK_EVENT_CONTROLLER_KEY(ta), a->im);
    g_signal_connect(ta, "key-pressed", G_CALLBACK(tasto_premuto), a);
    g_signal_connect(ta, "key-released", G_CALLBACK(tasto_rilasciato), a);
    g_signal_connect(ta, "modifiers", G_CALLBACK(modificatori_cambiati), a);
    gtk_widget_add_controller(a->cont, ta);
    GtkEventController *fu = gtk_event_controller_focus_new();
    g_signal_connect(fu, "enter", G_CALLBACK(fuoco_entra), a);
    g_signal_connect(fu, "leave", G_CALLBACK(fuoco_esce), a);
    gtk_widget_add_controller(a->cont, fu);

    g_signal_connect(a->cont, "map", G_CALLBACK(contenitore_mappato), a);
    g_signal_connect(a->radice, "destroy", G_CALLBACK(distruggi), a);

    a->ev_area = ph_shell_ascolta(sh, PH_SHELL_EV_AREA, ev_area, a);
    a->ev_host = ph_shell_ascolta(sh, PH_SHELL_EV_HOST, ev_host, a);
    a->srv_profilo = ph_srv_ascolta(a->srv, PH_SRV_EV_PROFILO, srv_profilo, a);
    a->srv_coll = ph_srv_ascolta(a->srv, PH_SRV_EV_COLLEGAMENTO, srv_collegamento, a);
    a->srv_app = ph_srv_ascolta(a->srv, PH_SRV_EV_APP, srv_app, a);

    telefono_aggiorna(a);
    qualita_aggiorna(a);
    ricordi_importa(a);
    area_aggiorna(a);

    PhShellGestoreFinestre gf = {
        .io = a,
        .apri_app = gf_apri_app,
        .apri_pagina = gf_apri_pagina,
        .elenco = gf_elenco,
        .clic_barra = gf_clic_barra,
        .specchio = gf_specchio,
        .specchio_aperto = gf_specchio_aperto,
        .chiudi = gf_chiudi,
    };
    ph_shell_registra_finestre(sh, &gf);
}

#ifdef PH_FINESTRA_APP_DEMO /* compilato solo nella libreria del demo (meson.build) */
/* ======================================================================
 * Aiuti per il demo (finestra_app_demo.h): il demo non ha mouse ne' tastiera
 * (schermo invisibile per le fotografie) e riproduce qui gli stati. */
static App *app_da_shell(PhShell *sh)
{
    for (GtkWidget *c = gtk_widget_get_first_child(ph_shell_radice(sh)); c != NULL;
         c = gtk_widget_get_next_sibling(c)) {
        App *a = app_di(c);
        if (a != NULL)
            return a;
    }
    return NULL;
}

bool ph_finestra_app_demo_azione(PhShell *sh, const char *azione, unsigned indice)
{
    App *a = app_da_shell(sh);
    if (a == NULL || indice >= a->fin->len)
        return false;
    Fin *f = g_ptr_array_index(a->fin, indice);
    if (strcmp(azione, "massimizza") == 0) {
        massimizza(a, f);
    } else if (strcmp(azione, "minimizza") == 0) {
        minimizza(a, f);
    } else if (strcmp(azione, "registra") == 0) {
        alterna_registrazione(a, f);
    } else if (strcmp(azione, "menu") == 0) {
        apri_menu(a, f, PH_FA_CORNICE(f->cornice)->puls[PH_FIN_PULS_ALTRI]);
    } else if (strcmp(azione, "compat-si") == 0 || strcmp(azione, "compat-no") == 0) {
        /* Il clic sulla voce "Compatibilita'" del menu: apre il menu e cambia la spunta
         * (il segnale "toggled" e' lo stesso del clic vero). */
        apri_menu(a, f, PH_FA_CORNICE(f->cornice)->puls[PH_FIN_PULS_ALTRI]);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(a->menu_compat), strcmp(azione, "compat-si") == 0);
    } else if (strcmp(azione, "zoom") == 0) {
        /* Tre scatti di Ctrl + rotella al centro dell'immagine; la FINE arriva da sola
         * 250 ms dopo (il timer del pizzico). */
        PhFinRect c;
        double ix, iy, iw, ih;
        int vw, vh, vx, vy;
        if (ph_fin_contenuto(a->d, f->id, &c, NULL) != 0 ||
            !ph_superficie_rettangolo_immagine(f->sup, &ix, &iy, &iw, &ih) ||
            !colpo_immagine(f, c.x + ix + iw / 2.0, c.y + iy + ih / 2.0, false, &vw, &vh, &vx, &vy))
            return false;
        for (int k = 0; k < 3; k++)
            pizzico_passo(a, f, 1.0, vw, vh, vx, vy);
    } else if (strcmp(azione, "sotto") == 0) {
        /* Trascina la finestra dal titolo fino oltre il bordo inferiore dell'area. */
        PhFinInfo i;
        if (ph_fin_info(a->d, f->id, &i, NULL) != 0)
            return false;
        porta_davanti(a, f);
        PhFinRect area = ph_fin_area(a->d);
        double x0 = i.rect.x + 50.0, y0 = i.rect.y + 10.0;
        if (ph_fin_gesto_inizia(a->d, f->id, PH_FIN_GESTO_SPOSTA, x0, y0, ora(), NULL) != 0)
            return false;
        ph_fin_gesto_muovi(a->d, x0, area.h - 20.0, ora(), NULL);
        ph_fin_gesto_finisci(a->d, x0, area.h - 20.0, ora(), NULL, NULL);
        sincronizza(a);
    } else if (strcmp(azione, "aggancia-sinistra") == 0) {
        /* Lo stesso gesto dell'utente: dal titolo fino al bordo sinistro. */
        PhFinInfo i;
        if (ph_fin_info(a->d, f->id, &i, NULL) != 0)
            return false;
        porta_davanti(a, f); /* come la pressione dell'utente */
        double y = i.rect.y + 10.0, x0 = i.rect.x + 50.0;
        if (ph_fin_gesto_inizia(a->d, f->id, PH_FIN_GESTO_SPOSTA, x0, y, ora(), NULL) != 0)
            return false;
        ph_fin_gesto_muovi(a->d, 5.0, y, ora(), NULL);
        ph_fin_gesto_finisci(a->d, 5.0, y, ora(), NULL, NULL);
        sincronizza(a);
    } else if (strcmp(azione, "aggancia-destra") == 0) {
        /* Come aggancia-sinistra, fino al bordo destro dell'area. */
        PhFinInfo i;
        if (ph_fin_info(a->d, f->id, &i, NULL) != 0)
            return false;
        porta_davanti(a, f);
        PhFinRect area = ph_fin_area(a->d);
        double y = i.rect.y + 10.0, x0 = i.rect.x + 50.0;
        if (ph_fin_gesto_inizia(a->d, f->id, PH_FIN_GESTO_SPOSTA, x0, y, ora(), NULL) != 0)
            return false;
        ph_fin_gesto_muovi(a->d, area.w - 5.0, y, ora(), NULL);
        ph_fin_gesto_finisci(a->d, area.w - 5.0, y, ora(), NULL, NULL);
        sincronizza(a);
    } else {
        return false;
    }
    return true;
}

/* Ultima misura e densita' CHIESTE al telefono per la finestra (per le prove). */
bool ph_finestra_app_demo_richiesta(PhShell *sh, unsigned indice, int *w, int *h, int *densita)
{
    App *a = app_da_shell(sh);
    if (a == NULL || indice >= a->fin->len)
        return false;
    Fin *f = g_ptr_array_index(a->fin, indice);
    *w = f->req_w;
    *h = f->req_h;
    *densita = f->req_dens;
    return true;
}

/* Stato della compatibilita' di una finestra (per le prove): in finestre.h, la spunta del menu
 * e i salvataggi del profilo chiesti dalla voce. */
bool ph_finestra_app_demo_stato(PhShell *sh, unsigned indice, bool *in_finestre, bool *spunta,
                                unsigned *salvataggi)
{
    App *a = app_da_shell(sh);
    if (a == NULL || indice >= a->fin->len)
        return false;
    Fin *f = g_ptr_array_index(a->fin, indice);
    *in_finestre = ph_fin_compatibilita(a->d, f->id);
    *spunta = a->menu_compat != NULL && gtk_check_button_get_active(GTK_CHECK_BUTTON(a->menu_compat));
    *salvataggi = a->salvataggi_compat;
    return true;
}

/* La cornice non sposta ne' rimpicciolisce i figli: il rettangolo REALE (rispetto alla cornice) del
 * pulsante Chiudi, dell'indietro e del fondo del contenuto coincide con quello che dicono le costanti e
 * finestre.h, e la cornice e' larga e alta quanto il rettangolo della finestra. Un border CSS sulla
 * cornice (che in GTK4 entra nell'allocazione) fa fallire questa prova. */
bool ph_finestra_app_demo_geometria(PhShell *sh, unsigned indice)
{
    App *a = app_da_shell(sh);
    if (a == NULL || indice >= a->fin->len)
        return false;
    Fin *f = g_ptr_array_index(a->fin, indice);
    PhFaCornice *c = PH_FA_CORNICE(f->cornice);
    PhFinInfo info;
    PhFinRect r;
    if (ph_fin_info(a->d, f->id, &info, NULL) != 0 || ph_fin_contenuto(a->d, f->id, &r, NULL) != 0)
        return false;
    graphene_rect_t b;
    bool ok = fabs((double)gtk_widget_get_width(f->cornice) - info.rect.w) < 0.01 &&
              fabs((double)gtk_widget_get_height(f->cornice) - info.rect.h) < 0.01;
    static const PhFinPulsante prova[] = {PH_FIN_PULS_INDIETRO, PH_FIN_PULS_CHIUDI};
    for (size_t k = 0; k < G_N_ELEMENTS(prova); k++) {
        if (!gtk_widget_compute_bounds(c->puls[prova[k]], f->cornice, &b))
            return false;
        ok = ok && fabs(b.origin.x - ph_fa_pulsante_x(info.rect.w, prova[k])) < 0.01 &&
             fabs(b.origin.y - (PH_FIN_TITOLO_H - PH_FIN_PULS) / 2) < 0.01 &&
             fabs(b.size.width - PH_FIN_PULS) < 0.01 && fabs(b.size.height - PH_FIN_PULS) < 0.01;
    }
    if (!gtk_widget_compute_bounds(c->fondo, f->cornice, &b))
        return false;
    return ok && fabs(b.origin.x - (r.x - info.rect.x)) < 0.01 &&
           fabs(b.origin.y - (r.y - info.rect.y)) < 0.01 && fabs(b.size.width - r.w) < 0.01 &&
           fabs(b.size.height - r.h) < 0.01;
}

/* ---- Ispezione del vetro (prove) ---- */
static Fin *demo_fin(PhShell *sh, unsigned indice, App **ao)
{
    App *a = app_da_shell(sh);
    if (a == NULL || indice >= a->fin->len)
        return NULL;
    if (ao != NULL)
        *ao = a;
    return g_ptr_array_index(a->fin, indice);
}

bool ph_finestra_app_demo_e_cornice(GtkWidget *w) { return PH_IS_FA_CORNICE(w); }

GtkWidget *ph_finestra_app_demo_cornice(PhShell *sh, unsigned indice)
{
    Fin *f = demo_fin(sh, indice, NULL);
    return f != NULL ? f->cornice : NULL;
}

bool ph_finestra_app_demo_vetro(PhShell *sh, unsigned indice, bool *attivo, bool *classe)
{
    Fin *f = demo_fin(sh, indice, NULL);
    if (f == NULL)
        return false;
    *attivo = PH_FA_CORNICE(f->cornice)->vetro;
    *classe = gtk_widget_has_css_class(f->cornice, "ph-finestra-app-vetro");
    return true;
}

guint ph_finestra_app_demo_dietro(PhShell *sh, unsigned indice, GdkPaintable **p, guint max)
{
    Fin *f = demo_fin(sh, indice, NULL);
    if (f == NULL || PH_FA_CORNICE(f->cornice)->dietro == NULL)
        return 0;
    GPtrArray *v = PH_FA_CORNICE(f->cornice)->dietro;
    for (guint k = 0; k < v->len && k < max; k++)
        p[k] = g_ptr_array_index(v, k);
    return v->len;
}

bool ph_finestra_app_demo_vetro_per_renderer(const char *nome_tipo) { return ph_shell_vetro_per_renderer(nome_tipo); }

unsigned ph_finestra_app_demo_snapshot_cornice(PhShell *sh, unsigned indice)
{
    Fin *f = demo_fin(sh, indice, NULL);
    return f != NULL ? PH_FA_CORNICE(f->cornice)->n_snapshot : 0;
}

unsigned ph_finestra_app_demo_snapshot_conta(PhShell *sh)
{
    App *a = app_da_shell(sh);
    unsigned n = 0;
    for (guint k = 0; a != NULL && k < a->fin->len; k++)
        n += PH_FA_CORNICE(((Fin *)g_ptr_array_index(a->fin, k))->cornice)->n_snapshot;
    return n;
}

static bool rett_uguale(const graphene_rect_t *r, float x, float y, float w, float h)
{
    return fabsf(r->origin.x - x) < 0.01f && fabsf(r->origin.y - y) < 0.01f &&
           fabsf(r->size.width - w) < 0.01f && fabsf(r->size.height - h) < 0.01f;
}

/* Salta i contenitori con un solo figlio. */
static GskRenderNode *senza_contenitori(GskRenderNode *n)
{
    while (n != NULL && gsk_render_node_get_node_type(n) == GSK_CONTAINER_NODE &&
           gsk_container_node_get_n_children(n) == 1)
        n = gsk_container_node_get_child(n, 0);
    return n;
}

static void albero_visita(GskRenderNode *n, int in_blur, float larg, PhFaDemoAlbero *o)
{
    if (n == NULL)
        return;
    switch (gsk_render_node_get_node_type(n)) {
    case GSK_CONTAINER_NODE:
        for (guint k = 0; k < gsk_container_node_get_n_children(n); k++)
            albero_visita(gsk_container_node_get_child(n, k), in_blur, larg, o);
        break;
    case GSK_CLIP_NODE: {
        if (rett_uguale(gsk_clip_node_get_clip(n), 0.0f, 0.0f, larg, (float)PH_FIN_TITOLO_H)) {
            o->clip_zona++;
            GskRenderNode *m = senza_contenitori(gsk_clip_node_get_child(n));
            if (m != NULL && gsk_render_node_get_node_type(m) == GSK_COLOR_MATRIX_NODE) {
                GskRenderNode *b = senza_contenitori(gsk_color_matrix_node_get_child(m));
                if (b != NULL && gsk_render_node_get_node_type(b) == GSK_BLUR_NODE &&
                    fabsf(gsk_blur_node_get_radius(b) - 2.0f * (float)VETRO_SIGMA_PX) < 0.01f)
                    o->catena_ok++;
            }
        }
        albero_visita(gsk_clip_node_get_child(n), in_blur, larg, o);
        break;
    }
    case GSK_BLUR_NODE:
        o->n_blur++;
        if (in_blur > 0)
            o->blur_annidati++;
        albero_visita(gsk_blur_node_get_child(n), in_blur + 1, larg, o);
        break;
    case GSK_ROUNDED_CLIP_NODE: albero_visita(gsk_rounded_clip_node_get_child(n), in_blur, larg, o); break;
    case GSK_TRANSFORM_NODE: albero_visita(gsk_transform_node_get_child(n), in_blur, larg, o); break;
    case GSK_OPACITY_NODE: albero_visita(gsk_opacity_node_get_child(n), in_blur, larg, o); break;
    case GSK_COLOR_MATRIX_NODE: albero_visita(gsk_color_matrix_node_get_child(n), in_blur, larg, o); break;
    case GSK_DEBUG_NODE: albero_visita(gsk_debug_node_get_child(n), in_blur, larg, o); break;
    case GSK_SHADOW_NODE: albero_visita(gsk_shadow_node_get_child(n), in_blur, larg, o); break;
    case GSK_REPEAT_NODE: albero_visita(gsk_repeat_node_get_child(n), in_blur, larg, o); break;
    case GSK_FILL_NODE: albero_visita(gsk_fill_node_get_child(n), in_blur, larg, o); break;
    case GSK_STROKE_NODE: albero_visita(gsk_stroke_node_get_child(n), in_blur, larg, o); break;
    case GSK_MASK_NODE:
        albero_visita(gsk_mask_node_get_source(n), in_blur, larg, o);
        albero_visita(gsk_mask_node_get_mask(n), in_blur, larg, o);
        break;
    case GSK_BLEND_NODE:
        albero_visita(gsk_blend_node_get_bottom_child(n), in_blur, larg, o);
        albero_visita(gsk_blend_node_get_top_child(n), in_blur, larg, o);
        break;
    case GSK_CROSS_FADE_NODE:
        albero_visita(gsk_cross_fade_node_get_start_child(n), in_blur, larg, o);
        albero_visita(gsk_cross_fade_node_get_end_child(n), in_blur, larg, o);
        break;
    default:
        break;
    }
}

/* Disegna la cornice in uno snapshot di prova (la stessa funzione del disegno vero) e
 * percorre i nodi: quanti blur, quanti dentro un altro blur, e se c'e' la catena
 * clip(0,0,larg,TITOLO_H) -> color matrix -> blur(2*sigma). Il contatore dei disegni
 * non conta questo giro. */
bool ph_finestra_app_demo_albero(PhShell *sh, unsigned indice, PhFaDemoAlbero *o)
{
    Fin *f = demo_fin(sh, indice, NULL);
    if (f == NULL)
        return false;
    PhFaCornice *c = PH_FA_CORNICE(f->cornice);
    memset(o, 0, sizeof *o);
    GtkSnapshot *s = gtk_snapshot_new();
    cornice_snapshot(f->cornice, s);
    c->n_snapshot--;
    GskRenderNode *n = gtk_snapshot_free_to_node(s);
    albero_visita(n, 0, (float)gtk_widget_get_width(f->cornice), o);
    if (n != NULL)
        gsk_render_node_unref(n);
    return true;
}

/* Accende o spegne il vetro di tutte le cornici (come farebbe la realizzazione), per confrontare i
 * ridisegni a riposo con e senza vetro. */
void ph_finestra_app_demo_vetro_imposta(PhShell *sh, bool acceso)
{
    App *a = app_da_shell(sh);
    for (guint k = 0; a != NULL && k < a->fin->len; k++) {
        GtkWidget *w = ((Fin *)g_ptr_array_index(a->fin, k))->cornice;
        PH_FA_CORNICE(w)->vetro = acceso;
        if (acceso)
            gtk_widget_add_css_class(w, "ph-finestra-app-vetro");
        else
            gtk_widget_remove_css_class(w, "ph-finestra-app-vetro");
        gtk_widget_queue_draw(w);
    }
}

unsigned ph_finestra_app_demo_sincronizza_conta(PhShell *sh)
{
    App *a = app_da_shell(sh);
    return a != NULL ? a->n_sincronizza : 0;
}

/* L'assestamento (misure, ciclo dei fotogrammi di finestra_app) e' finito: il tick si e' spento. */
bool ph_finestra_app_demo_assestato(PhShell *sh)
{
    App *a = app_da_shell(sh);
    return a != NULL && a->tick == 0;
}

/* Il menu "altri comandi" (NULL se non e' mai stato aperto) e la visita dei nodi di un albero qualunque. */
GtkWidget *ph_finestra_app_demo_menu(PhShell *sh)
{
    App *a = app_da_shell(sh);
    return a != NULL ? a->menu : NULL;
}

void ph_finestra_app_demo_albero_nodo(GskRenderNode *n, float larg, PhFaDemoAlbero *o)
{
    memset(o, 0, sizeof *o);
    albero_visita(n, 0, larg, o);
}

/* Chiude la finestra SUBITO (la chiusura vera e' differita a un'idle): serve per vedere i
 * ritratti dei suoi figli morti PRIMA del disegno successivo di un'altra cornice. */
bool ph_finestra_app_demo_chiudi_ora(PhShell *sh, unsigned indice)
{
    App *a = NULL;
    Fin *f = demo_fin(sh, indice, &a);
    if (f == NULL)
        return false;
    chiudi_fin(a, f, true);
    return true;
}

/* Sposta la finestra (e la porta davanti) con lo stesso gesto del titolo, in modo che
 * il suo angolo vada in (x, y). */
bool ph_finestra_app_demo_posiziona(PhShell *sh, unsigned indice, double x, double y)
{
    App *a = NULL;
    Fin *f = demo_fin(sh, indice, &a);
    PhFinInfo i;
    if (f == NULL || ph_fin_info(a->d, f->id, &i, NULL) != 0)
        return false;
    porta_davanti(a, f);
    double x0 = i.rect.x + 100.0, y0 = i.rect.y + 10.0;
    if (ph_fin_gesto_inizia(a->d, f->id, PH_FIN_GESTO_SPOSTA, x0, y0, ora(), NULL) != 0)
        return false;
    double bx = x0 + (x - i.rect.x), by = y0 + (y - i.rect.y);
    int r1 = ph_fin_gesto_muovi(a->d, bx, by, ora(), NULL);
    int r2 = ph_fin_gesto_finisci(a->d, bx, by, ora(), NULL, NULL);
    sincronizza(a);
    return r1 == 0 && r2 == 0;
}

/* Punto della finestra per le prove del mouse: "contenuto" = centro del contenuto, "titolo" = nella
 * barra del titolo (come il gesto di posiziona), "pulsante" = centro del pulsante di chiusura,
 * "angolo" = maniglia di ridimensionamento. false se il punto NON cade davvero in quella zona di
 * QUELLA finestra (finestra sovrapposta, non ridimensionabile...): una prova che non prova niente
 * deve fallire, non passare. */
static bool demo_punto(App *a, Fin *f, const char *zona, double *x, double *y)
{
    PhFinInfo i;
    PhFinRect c;
    PhFinZona atteso;
    if (ph_fin_info(a->d, f->id, &i, NULL) != 0 || ph_fin_contenuto(a->d, f->id, &c, NULL) != 0)
        return false;
    if (strcmp(zona, "contenuto") == 0) {
        *x = c.x + c.w / 2.0;
        *y = c.y + c.h / 2.0;
        atteso = PH_FIN_ZONA_CONTENUTO;
    } else if (strcmp(zona, "titolo") == 0) {
        *x = i.rect.x + 60.0; /* tra il pulsante indietro e quelli di destra */
        *y = i.rect.y + 10.0;
        atteso = PH_FIN_ZONA_TITOLO;
    } else if (strcmp(zona, "pulsante") == 0) {
        *x = i.rect.x + i.rect.w - PH_FIN_PULS_MARG_DX - PH_FIN_PULS / 2.0;
        *y = i.rect.y + PH_FIN_TITOLO_H / 2.0;
        atteso = PH_FIN_ZONA_PULSANTE;
    } else if (strcmp(zona, "angolo") == 0) {
        *x = i.rect.x + i.rect.w - 3.0;
        *y = i.rect.y + i.rect.h - 3.0;
        atteso = PH_FIN_ZONA_ANGOLO;
    } else {
        return false;
    }
    PhFinColpo k;
    ph_fin_trova(a->d, *x, *y, &k);
    return k.id == f->id && k.zona == atteso;
}

bool ph_finestra_app_demo_mouse_pronto(PhShell *sh, unsigned indice)
{
    App *a = NULL;
    Fin *f = demo_fin(sh, indice, &a);
    double x, y;
    int vw, vh, vx, vy;
    PhFinColpo k;
    /* Le stesse condizioni con cui gesto_inizia_pulsante accetta un tocco sul contenuto. */
    return f != NULL && !f->in_pausa && demo_punto(a, f, "contenuto", &x, &y) &&
           (ph_fin_trova(a->d, x, y, &k), k.valido) && !su_pulsante(a, x, y) &&
           colpo_immagine(f, x, y, false, &vw, &vh, &vx, &vy);
}

bool ph_finestra_app_demo_mouse(PhShell *sh, unsigned indice, const char *zona, const char *azione,
                                unsigned bottone)
{
    App *a = NULL;
    Fin *f = demo_fin(sh, indice, &a);
    double x, y;
    if (f == NULL || !demo_punto(a, f, zona, &x, &y))
        return false;
    if (strcmp(azione, "giu") == 0) {
        gesto_inizia_pulsante(a, NULL, bottone, x, y);
    } else if (strcmp(azione, "muovi") == 0) {
        gesto_aggiorna_punto(a, x + 3.0, y + 2.0);
    } else if (strcmp(azione, "su") == 0) {
        gesto_fine_punto(a, x + 3.0, y + 2.0);
    } else if (strcmp(azione, "annulla") == 0) {
        gesto_annulla_tutto(a);
    } else {
        return false;
    }
    return true;
}

unsigned ph_finestra_app_demo_tocchi(PhShell *sh, PhFaDemoTocco *out, unsigned max)
{
    App *a = app_da_shell(sh);
    if (a == NULL)
        return 0;
    unsigned n = a->n_tocchi_log < max ? a->n_tocchi_log : max;
    memcpy(out, a->tocchi_log, n * sizeof *out);
    return n;
}

void ph_finestra_app_demo_tocchi_azzera(PhShell *sh)
{
    App *a = app_da_shell(sh);
    if (a != NULL)
        a->n_tocchi_log = 0;
}

bool ph_finestra_app_demo_gesto_in_corso(PhShell *sh)
{
    App *a = app_da_shell(sh);
    return a != NULL && a->gesto != GESTO_NESSUNO;
}

/* Simula l'arrivo di PH_SRV_EV_PROFILO (cambio di telefono o profilo ricaricato). */
void ph_finestra_app_demo_profilo_ricaricato(PhShell *sh)
{
    App *a = app_da_shell(sh);
    if (a != NULL)
        srv_profilo(a->srv, PH_SRV_EV_PROFILO, NULL, a);
}
#endif /* PH_FINESTRA_APP_DEMO */
