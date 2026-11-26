/* Solo per il demo (compilato in demo-desktop, mai nel prodotto): apre o prepara
 * qualcosa senza mouse, per le fotografie di strumenti/foto-demo.sh. Lavora solo
 * con le funzioni interne del pezzo e con i widget trovati per classe CSS.
 * cosa: widget3, menu-desktop, menu-icona, menu-widget, sfondo, sfondo-solido,
 * sfondo-sfumato, sfondo-immagine, sfondo-scegli-prima, catalogo, componenti[:0|1|2],
 * componenti-verifica, chiudi. */
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "interno.h"
#include "shell/finto/finto.h"
#include "shell/servizi_backend.h"

void ph_demo_mostra(PhShell *sh, const char *cosa);
void ph_demo_meteo(PhShell *sh, const char *cosa); /* agente C: demo_meteo.c */
PhFinto *ph_demo_finto(void);                /* demo.c */
void ph_demo_finto_comando(const char *c);   /* demo.c */
static void stress_avvia(PhShell *sh);
void ph_demo_controlli(PhShell *sh, const char *cosa); /* agente D: demo_controlli.c */
/* agente A: inizio */
static void vecchi_avvia(PhShell *sh, PhDesktop *d, PhCfgTelefono *p); /* in fondo al file */
static void vecchi_foto(PhDesktop *d, PhCfgTelefono *p, const char *quale);
uint64_t ph_dsk_widget_adesso_ms(int *scarto_s); /* widget.c: l'«adesso» dei widget (ora congelata se c'e') */
/* Ora congelata di «vecchi» e delle sue foto: le 13:30, quando il «Lunch» del finto (13:00-14:00) e'
 * in corso e restano tre impegni dopo. Cosi' a qualunque ora vera si vedono sempre gli stessi impegni. */
#define VECCHI_ORA (13 * 3600 + 30 * 60)
/* agente A: fine */

/* Il n-esimo widget (in ordine di profondita') con la classe CSS data. */
static GtkWidget *trova_conta(GtkWidget *radice, const char *classe, int *n)
{
    for (GtkWidget *c = gtk_widget_get_first_child(radice); c != NULL;
         c = gtk_widget_get_next_sibling(c)) {
        if (gtk_widget_has_css_class(c, classe) && (*n)-- == 0)
            return c;
        GtkWidget *r = trova_conta(c, classe, n);
        if (r != NULL)
            return r;
    }
    return NULL;
}

static GtkWidget *trova(GtkWidget *radice, const char *classe, int n)
{
    return trova_conta(radice, classe, &n);
}

/* Stato della prova snap-profilo / verifica-profilo. */
static bool snap_fatta, verifica_eseguita, snap_scelto;
static unsigned snap_salv, snap_meteo;
static gint64 snap_scadenza;
static char snap_nome[128], snap_dett[128];
static int32_t snap_lat, snap_lon;

typedef struct {
    PhShell *sh;
} Riprova;

static void conta_meteo(PhServizi *srv, PhSrvEvento ev, const char *arg, void *utente)
{
    (void)srv;
    (void)ev;
    (void)arg;
    (void)utente;
    if (snap_fatta)
        snap_meteo++;
}

static void snap_esci(void)
{
    if (snap_fatta && !verifica_eseguita) {
        fprintf(stderr, "prova meteo: la verifica del profilo non e' stata eseguita\n");
        _exit(1);
    }
}

static gboolean riprova_verifica(gpointer dati)
{
    Riprova *r = dati;
    ph_demo_mostra(r->sh, "verifica-profilo");
    g_free(r);
    return G_SOURCE_REMOVE;
}

/* ---- Prova delle tessere delle icone vere (niente alone quadrato) ------------------------------
 * cosa «tessere»: aggiunge al desktop un'icona di un pacchetto mai chiesto (nasce col segnaposto),
 * apre «App frequenti», poi aspetta CON SCADENZA che tutte le icone vere siano arrivate e controlla
 * la classe ph-desktop-tessera-vera: presente se e solo se dentro c'e' la texture vera (GtkImage).
 * Alla fine, a schermo: la stessa tessera disegnata con e senza la classe; senza classe l'ombra
 * rende non trasparente il pixel sotto il bordo, con la classe no. Se all'uscita la prova non e' finita il processo cade. */
static bool tessere_fatta = false;
static void tessere_esci(void)
{
    if (!tessere_fatta) {
        fprintf(stderr, "prova tessere: la verifica non e' stata completata\n");
        _exit(1);
    }
}

typedef struct {
    PhShell *sh;
    gint64 scadenza;
    GtkWidget *tessera; /* quella del pixel */
    int fase;           /* 0 attesa icone, 1 attesa ridisegno senza classe */
    int somma_con; /* alfa del pixel con la classe */
    bool con_attesa; /* gia' iniziata l'attesa dei fotogrammi prima della misura con la classe */
    gint64 fotogramma; /* contatore dei fotogrammi quando si e' tolta la classe */
    GtkWidget *libero;
} Tessere;

/* Chiede un fotogramma e dice quanti ne sono passati: la finestra della prova (Broadway senza
 * nessun browser collegato) non ne produce da sola, e senza fotogramma le icone appena create
 * restano di misura 0x0 e il cambio di classe CSS non e' ancora stato applicato: lo snapshot
 * del contenitore libero e' allora vuoto (nodo NULL). */
static gint64 fotogrammi(GtkWidget *w)
{
    GdkFrameClock *fc = gtk_widget_get_frame_clock(w);
    if (fc == NULL)
        return -1;
    gdk_frame_clock_request_phase(fc, GDK_FRAME_CLOCK_PHASE_UPDATE);
    return gdk_frame_clock_get_frame_counter(fc);
}

static bool tessera_e_vera(GtkWidget *t)
{
    GtkWidget *f = gtk_widget_get_first_child(t);
    return f != NULL && GTK_IS_IMAGE(f);
}

/* Alfa del pixel (x, y) di `radice` disegnata in una texture (come la prova del launcher); -1 se non
 * si riesce. Si disegna il contenitore libero delle icone, che non ha sfondo: dove non c'e' niente
 * l'alfa e' 0, dove c'e' un'ombra e' > 0. */
static int alfa_pixel(GtkWidget *radice, int x, int y)
{
    int w = gtk_widget_get_width(radice), h = gtk_widget_get_height(radice);
    if (x < 0 || y < 0 || x >= w || y >= h)
        return -1;
    GdkPaintable *p = gtk_widget_paintable_new(radice);
    GtkSnapshot *sn = gtk_snapshot_new();
    gdk_paintable_snapshot(p, sn, w, h);
    GskRenderNode *n = gtk_snapshot_free_to_node(sn);
    GtkNative *nat = gtk_widget_get_native(radice);
    GskRenderer *r = nat != NULL ? gtk_native_get_renderer(nat) : NULL;
    int alfa = -1;
    if (n != NULL && r != NULL) {
        graphene_rect_t area = GRAPHENE_RECT_INIT(0, 0, (float)w, (float)h);
        GdkTexture *t = gsk_renderer_render_texture(r, n, &area);
        if (t != NULL && x < gdk_texture_get_width(t) && y < gdk_texture_get_height(t)) {
            gsize riga = (gsize)gdk_texture_get_width(t) * 4;
            guchar *dati = g_malloc0(riga * (gsize)gdk_texture_get_height(t));
            gdk_texture_download(t, dati, riga); /* B8G8R8A8 premoltiplicato: alfa = 4o byte */
            alfa = dati[(gsize)y * riga + (gsize)x * 4 + 3];
            g_free(dati);
        }
        if (t != NULL)
            g_object_unref(t);
    }
    if (n != NULL)
        gsk_render_node_unref(n);
    g_object_unref(p);
    return alfa;
}

static int pixel_sotto_tessera(GtkWidget *libero, GtkWidget *t)
{
    graphene_rect_t b;
    if (!gtk_widget_compute_bounds(t, libero, &b))
        return -1;
    /* Vicino allo spigolo sinistro, 3 px sotto il bordo: fuori dal nome, dentro l'ombra. */
    return alfa_pixel(libero, (int)b.origin.x + 6, (int)(b.origin.y + b.size.height) + 3);
}

static gboolean tessere_passo(gpointer dati)
{
    Tessere *s = dati;
    GtkWidget *radice = ph_shell_radice(s->sh);
    if (s->fase == 1) {
        /* Il cambio di classe vale solo dopo almeno un fotogramma intero. */
        if (fotogrammi(s->libero) < s->fotogramma + 2) {
            if (g_get_monotonic_time() >= s->scadenza)
                g_error("prova tessere: scadenza, nessun fotogramma dopo il cambio di classe");
            return G_SOURCE_CONTINUE;
        }
        int senza = pixel_sotto_tessera(s->libero, s->tessera);
        gtk_widget_add_css_class(s->tessera, "ph-desktop-tessera-vera"); /* ripristino */
        if (senza < 0 || s->somma_con < 0)
            g_error("prova tessere: pixel non leggibile (con %d, senza %d)", s->somma_con, senza);
        /* Sotto l'icona vera non ci dev'essere alcuna ombra, nemmeno leggera (come il drawer). */
        if (s->somma_con != 0)
            g_error("prova tessere: sotto l'icona vera l'alfa dovrebbe essere 0, invece %d", s->somma_con);
        if (!(senza >= s->somma_con + 8))
            g_error("prova tessere: senza la classe sotto la tessera dovrebbe esserci l'ombra "
                    "(alfa con classe %d, senza %d)", s->somma_con, senza);
        tessere_fatta = true;
        g_message("prova tessere: classe giusta su icone e App frequenti, nessuna ombra sotto l'icona vera "
                  "(alfa %d contro %d con l'ombra)", s->somma_con, senza);
        g_free(s);
        return G_SOURCE_REMOVE;
    }
    /* Fase 0: tutte le icone vere arrivate? */
    int vere_desk = 0, segn_desk = 0, vere_app = 0, segn_app = 0;
    GtkWidget *prima_vera = NULL;
    for (int i = 0;; i++) {
        GtkWidget *t = trova(radice, "ph-desktop-tessera", i);
        if (t == NULL)
            break;
        bool vera = gtk_widget_has_css_class(t, "ph-desktop-tessera-vera");
        if (vera != tessera_e_vera(t))
            g_error("prova tessere: icona del desktop con classe %d ma texture vera %d", vera, tessera_e_vera(t));
        if (vera) {
            vere_desk++;
            if (prima_vera == NULL)
                prima_vera = t;
        } else {
            segn_desk++;
        }
    }
    for (int i = 0;; i++) {
        GtkWidget *t = trova(radice, "ph-desktop-app-tessera", i);
        if (t == NULL)
            break;
        bool vera = gtk_widget_has_css_class(t, "ph-desktop-tessera-vera");
        if (vera != tessera_e_vera(t))
            g_error("prova tessere: tessera di App frequenti con classe %d ma texture vera %d", vera, tessera_e_vera(t));
        if (vera)
            vere_app++;
        else
            segn_app++;
    }
    if (segn_desk > 0 || vere_desk < 4 || vere_app == 0 || segn_app > 0) {
        if (g_get_monotonic_time() >= s->scadenza)
            g_error("prova tessere: scadenza, icone del desktop vere %d (segnaposto %d), App frequenti vere %d "
                    "(segnaposto %d)", vere_desk, segn_desk, vere_app, segn_app);
        return G_SOURCE_CONTINUE;
    }
    /* Le icone devono avere la loro misura (almeno un fotogramma dopo la creazione). */
    if (gtk_widget_get_width(prima_vera) <= 0 || gtk_widget_get_width(s->libero) <= 0) {
        fotogrammi(s->libero);
        if (g_get_monotonic_time() >= s->scadenza)
            g_error("prova tessere: scadenza, le icone del desktop non hanno ricevuto una misura");
        return G_SOURCE_CONTINUE;
    }
    /* La classe arriva insieme alla texture e vale solo dopo un fotogramma intero: si parte a contare
     * da quando tutte le icone sono vere e hanno la misura, e si misura dopo almeno 2 fotogrammi. */
    if (!s->con_attesa) {
        s->con_attesa = true;
        s->fotogramma = fotogrammi(s->libero);
        s->scadenza = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    }
    if (fotogrammi(s->libero) < s->fotogramma + 2) {
        if (g_get_monotonic_time() >= s->scadenza)
            g_error("prova tessere: scadenza, nessun fotogramma dopo l'arrivo delle icone vere");
        return G_SOURCE_CONTINUE;
    }
    /* Pixel con la classe, poi senza (dopo un ridisegno vero), nella fase 1. */
    s->tessera = prima_vera;
    s->somma_con = pixel_sotto_tessera(s->libero, prima_vera);
    gtk_widget_remove_css_class(prima_vera, "ph-desktop-tessera-vera");
    s->fase = 1;
    s->fotogramma = fotogrammi(s->libero);
    s->scadenza = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    g_timeout_add(50, tessere_passo, s);
    return G_SOURCE_REMOVE;
}

static void tessere_avvia(PhShell *sh, PhDesktop *d)
{
    GtkWidget *radice = ph_shell_radice(sh);
    /* Un pacchetto mai chiesto: nasce col segnaposto (l'icona arriva dopo ~30 ms). */
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p == NULL || p->n_icone >= PH_CFG_MAX_ICONE)
        g_error("prova tessere: profilo assente o pieno");
    memset(&p->icone[p->n_icone], 0, sizeof p->icone[0]);
    g_strlcpy(p->icone[p->n_icone].app, "it.finto.tessera-prova", sizeof p->icone[0].app);
    p->icone[p->n_icone].colonna = 5;
    p->icone[p->n_icone].riga = 0;
    p->n_icone++;
    ph_dsk_icone_ricostruisci(d); /* subito, senza aspettare l'evento del profilo */
    int segnaposto = 0;
    for (int i = 0;; i++) {
        GtkWidget *t = trova(radice, "ph-desktop-tessera", i);
        if (t == NULL)
            break;
        if (!tessera_e_vera(t)) {
            segnaposto++;
            if (gtk_widget_has_css_class(t, "ph-desktop-tessera-vera"))
                g_error("prova tessere: un segnaposto ha la classe vera");
            /* Classe rimasta da una texture: il ritorno al segnaposto deve toglierla. */
            gtk_widget_add_css_class(t, "ph-desktop-tessera-vera");
        }
    }
    if (segnaposto == 0)
        g_error("prova tessere: nessun segnaposto subito dopo l'aggiunta");
    ph_dsk_icone_nomi_aggiornati(d); /* rifa' la tessera di chi non ha ancora l'icona */
    for (int i = 0;; i++) {
        GtkWidget *t = trova(radice, "ph-desktop-tessera", i);
        if (t == NULL)
            break;
        if (!tessera_e_vera(t) && gtk_widget_has_css_class(t, "ph-desktop-tessera-vera"))
            g_error("prova tessere: il ritorno al segnaposto non ha tolto la classe");
    }
    ph_demo_mostra(sh, "wx:frequenti");
    atexit(tessere_esci);
    Tessere *s = g_new0(Tessere, 1);
    s->sh = sh;
    s->libero = d->libero;
    s->scadenza = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    g_timeout_add(50, tessere_passo, s);
}

static bool verifica_vetro_fatta = false;
static void vetro_esci(void)
{
    if (!verifica_vetro_fatta) {
        fprintf(stderr, "prova vetro: la verifica non e' stata eseguita\n");
        _exit(1);
    }
}

/* ---- Galleria dei componenti di disegno (componenti.c) -------------------------------------------
 * «componenti[:0|1|2]»: schede di vetro scuro con ogni componente e dati finti, a tre misure (200,
 * 280, 380 px di larghezza); con 0, 1 o 2 solo quella misura (per le fotografie su schermi bassi).
 * Le schede si dispongono in colonne e vanno a capo quando finisce l'altezza. «componenti-verifica»
 * controlla che tutto sia stato disegnato con lo spazio dato, che le tinte arrivino dal CSS e che i
 * clic delle righe arrivino alla callback; se non viene eseguita il processo esce con errore. */
static const struct {
    int larg;
    int alte[5]; /* altezza della scheda per componente: anello, fette, curva, righe, numeri */
} GAL_MISURE[3] = {{200, {150, 250, 220, 190, 150}}, {280, {190, 200, 240, 190, 150}}, {380, {230, 200, 280, 190, 150}}};

static unsigned gal_clic, gal_clic_attesi, gal_dischi, gal_tratti;
static unsigned gal_n_misure;
static bool gal_verificata, gal_costruita;

static void gal_esci(void)
{
    if (gal_costruita && !gal_verificata) {
        fprintf(stderr, "componenti: la verifica non e' stata eseguita\n");
        _exit(1);
    }
}

static void gal_clic_riga(void *dati)
{
    gal_clic++;
    fprintf(stderr, "demo: clic sulla riga \"%s\"\n", (const char *)dati);
}

static void gal_anello(GtkWidget *c, int w, int h, int k)
{
    static const double pct[3] = {78, 5, 100};
    static const PhDskAccento acc[3] = {PH_DSK_ACC_VERDE, PH_DSK_ACC_CORALLO, PH_DSK_ACC_TEMA};
    GtkWidget *a = ph_dsk_anello_nuovo();
    int lato = MIN(w - 40, h - 32);
    gtk_widget_set_size_request(a, lato, lato);
    gtk_widget_set_halign(a, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(a, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(a, TRUE);
    ph_dsk_anello_imposta(a, pct[k], NULL, acc[k]);
    gtk_box_append(GTK_BOX(c), a);
    gal_dischi++;
}

static void gal_fette(GtkWidget *c, int k)
{
    static const PhDskFetta f[5] = {{"Images", "21 GB", 21, PH_DSK_ACC_TEMA},
                                    {"Videos", "34 GB", 34, PH_DSK_ACC_INDACO},
                                    {"Audio", "3 GB", 3, PH_DSK_ACC_AMBRA},
                                    {"Apps", "18 GB", 18, PH_DSK_ACC_CORALLO},
                                    {"Other", "10 GB", 10, PH_DSK_ACC_ARDESIA}};
    GtkWidget *n = ph_dsk_numero_nuovo();
    gtk_widget_set_size_request(n, -1, 60);
    ph_dsk_numero_imposta(n, "Storage", "42", "GB free of 128 GB");
    gtk_box_append(GTK_BOX(c), n);
    GtkWidget *fe = ph_dsk_fette_nuove();
    /* La terza misura mostra anche cosa succede con dati incoerenti: somma oltre il totale. */
    ph_dsk_fette_imposta(fe, f, G_N_ELEMENTS(f), k == 2 ? 60 : 128);
    gtk_box_append(GTK_BOX(c), fe);
}

static void gal_curva(GtkWidget *c, int k)
{
    static const size_t punti[3] = {5, 7, 12};
    static const int altezza[3] = {70, 86, 104};
    size_t n = punti[k];
    double t[PH_DSK_CURVA_MAX];
    char gradi[PH_DSK_CURVA_MAX][16], ore[PH_DSK_CURVA_MAX][16];
    const char *g[PH_DSK_CURVA_MAX], *o[PH_DSK_CURVA_MAX];
    for (size_t i = 0; i < n; i++) {
        t[i] = 17 + 5 * sin((double)i * 0.7 + 1.2);
        g_snprintf(gradi[i], sizeof gradi[i], "%d\302\260", (int)lround(t[i]));
        g_snprintf(ore[i], sizeof ore[i], "%02d", (int)((16 + 2 * i) % 24));
        g[i] = gradi[i];
        o[i] = ore[i];
    }
    GtkWidget *cu = ph_dsk_curva_nuova();
    gtk_widget_set_size_request(cu, -1, altezza[k]);
    ph_dsk_curva_imposta(cu, t, g, o, n, PH_DSK_ACC_TEMA);
    gtk_box_append(GTK_BOX(c), cu);
    gal_dischi++;
    static const struct {
        const char *giorno, *testo;
        double min, max;
    } gg[3] = {{"Fri", "12\302\260 / 22\302\260", 12, 22}, {"Sat", "11\302\260 / 20\302\260", 11, 20},
               {"Sun", "13\302\260 / 24\302\260", 13, 24}};
    for (size_t i = 0; i < 3; i++) {
        GtkWidget *riga = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *e = gtk_label_new(gg[i].giorno);
        gtk_widget_set_size_request(e, 30, -1);
        gtk_label_set_xalign(GTK_LABEL(e), 0);
        GtkWidget *tr = ph_dsk_tratto_nuovo();
        ph_dsk_tratto_imposta(tr, true, gg[i].min, gg[i].max, 8, 26, PH_DSK_ACC_TEMA, PH_DSK_ACC_AMBRA);
        GtkWidget *v = gtk_label_new(gg[i].testo);
        gtk_label_set_xalign(GTK_LABEL(v), 1);
        gtk_box_append(GTK_BOX(riga), e);
        gtk_box_append(GTK_BOX(riga), tr);
        gtk_box_append(GTK_BOX(riga), v);
        gtk_box_append(GTK_BOX(c), riga);
        gal_tratti++;
    }
}

static void gal_righe(GtkWidget *c)
{
    static const struct {
        const char *titolo, *sotto, *ora;
        PhDskAccento acc;
        bool tondo, clic;
    } r[3] = {{"Alex", "See you at 8? I booked the table by the window", "15:52", PH_DSK_ACC_VERDE, true, true},
              {"Invoice for October", "Your invoice is ready", "15:20", PH_DSK_ACC_INDACO, false, true},
              {"Dentist", "Today at 17:30", "14:30", PH_DSK_ACC_CORALLO, false, false}};
    for (size_t i = 0; i < 3; i++) {
        GtkWidget *riga = ph_dsk_riga_nuova(r[i].clic ? gal_clic_riga : NULL, (void *)r[i].titolo);
        ph_dsk_riga_imposta(riga, NULL, r[i].acc, r[i].tondo, r[i].titolo, r[i].sotto, r[i].ora);
        gtk_box_append(GTK_BOX(c), riga);
        if (r[i].clic)
            gal_clic_attesi++;
    }
}

static void gal_numeri(GtkWidget *c)
{
    GtkWidget *a = ph_dsk_numero_nuovo();
    gtk_widget_set_size_request(a, -1, 56);
    ph_dsk_numero_imposta(a, "Thu", "26", "November");
    GtkWidget *b = ph_dsk_numero_nuovo();
    gtk_widget_set_size_request(b, -1, 56);
    ph_dsk_numero_imposta(b, "Next alarm", "07:00", "Friday \302\267 in 15 h");
    gtk_box_append(GTK_BOX(c), a);
    gtk_box_append(GTK_BOX(c), b);
}

static void galleria_costruisci(PhShell *sh, PhDesktop *d, PhCfgTelefono *p, int solo)
{
    p->n_widget = 0; /* via i widget veri: lo schermo e' della galleria */
    ph_dsk_widget_ricostruisci(d);
    int x = 16, y = 16, colonna = 0, lim = d->area_a - PH_DSK_BARRA - 12;
    for (int k = 0; k < 3; k++) {
        if (solo >= 0 && solo != k)
            continue;
        gal_n_misure++;
        for (int q = 0; q < 5; q++) {
            int w = GAL_MISURE[k].larg, h = GAL_MISURE[k].alte[q];
            if (y + h > lim && y > 16) { /* a capo: nuova colonna */
                x += colonna + 12;
                y = 16;
                colonna = 0;
            }
            GtkWidget *c = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
            gtk_widget_add_css_class(c, "ph-desktop-galleria");
            gtk_widget_add_css_class(c, "ph-desktop-forma-mattonella");
            gtk_widget_set_size_request(c, w, h);
            ph_shell_vetro_registra(sh, c, PH_DSK_RAGGIO_MATTONELLA, PH_VETRO_SCURO);
            ph_shell_libero_metti(d->libero, c, x, y, w, h);
            switch (q) {
            case 0: gal_anello(c, w, h, k); break;
            case 1: gal_fette(c, k); break;
            case 2: gal_curva(c, k); break;
            case 3: gal_righe(c); break;
            default: gal_numeri(c); break;
            }
            y += h + 12;
            if (w > colonna)
                colonna = w;
        }
        x += colonna + 12;
        y = 16;
        colonna = 0;
    }
    gal_costruita = true;
    atexit(gal_esci);
}

/* Conta i disegni (anello, fette, curva, tratto, numero) e quanti hanno gia' ricevuto una misura. */
static void gal_conta_disegni(GtkWidget *w, unsigned *visti, unsigned *con_misura)
{
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (g_strcmp0(gtk_widget_get_css_name(c), "ph-desktop-disegno") == 0) {
            (*visti)++;
            if (gtk_widget_get_width(c) > 0 && gtk_widget_get_height(c) > 0)
                (*con_misura)++;
        }
        gal_conta_disegni(c, visti, con_misura);
    }
}

/* Disposizione adattiva: alla larghezza di 200 px (contenuto 160) la legenda delle fette sta in UNA
 * colonna e l'ora della riga col titolo lungo scende sotto; a 380 px la legenda e' a due colonne e
 * l'ora sta a destra. NULL se tutto e' come atteso, altrimenti il motivo (il layout arriva in
 * ritardo di un fotogramma: chi chiama riprova fino alla scadenza). */
static const char *gal_disposizione(GtkWidget *radice)
{
    for (int i = 0;; i++) {
        GtkWidget *g = trova(radice, "ph-desktop-legenda", i);
        if (g == NULL)
            break;
        /* Le colonne sono le ascisse diverse delle voci (la legenda dispone da sola i suoi figli). */
        int w = gtk_widget_get_width(g), colonne = 0;
        float visti[PH_DSK_FETTE_MAX];
        for (GtkWidget *c = gtk_widget_get_first_child(g); c != NULL; c = gtk_widget_get_next_sibling(c)) {
            graphene_rect_t b;
            if (!gtk_widget_compute_bounds(c, g, &b))
                return "legenda: voce senza misura";
            bool nuova = true;
            for (int k = 0; k < colonne; k++)
                if (fabsf(visti[k] - b.origin.x) < 1.0f)
                    nuova = false;
            if (nuova && colonne < PH_DSK_FETTE_MAX)
                visti[colonne++] = b.origin.x;
        }
        if ((w > 0 && w <= 170 && colonne != 1) || (w >= 300 && colonne != 2))
            return "legenda: colonne diverse da quelle attese per la larghezza";
    }
    for (int i = 0;; i++) {
        GtkWidget *b = trova(radice, "ph-desktop-riga-elenco", i);
        if (b == NULL)
            break;
        GtkWidget *titolo = trova(b, "ph-desktop-riga-titolo", 0), *ora = trova(b, "ph-desktop-riga-ora", 0),
                  *sotto = trova(b, "ph-desktop-riga-sotto", 0);
        if (g_strcmp0(gtk_label_get_text(GTK_LABEL(titolo)), "Invoice for October") != 0)
            continue;
        int w = gtk_widget_get_width(b);
        /* Sotto: l'ora comincia dove finisce il titolo (a destra di lui non c'e' posto). */
        graphene_rect_t bo, bt;
        if (!gtk_widget_compute_bounds(ora, b, &bo) || !gtk_widget_compute_bounds(titolo, b, &bt))
            return "riga: ora o titolo senza misura";
        bool sotto_ora = bo.origin.y >= bt.origin.y + bt.size.height - 1.0f;
        (void)sotto;
        if ((w > 0 && w <= 170 && !sotto_ora) || (w >= 300 && sotto_ora))
            return "riga: l'ora non e' dove deve stare per la larghezza";
    }
    return NULL;
}

/* Testi guasti e icona rimessa due volte: nessun testo non valido arriva alle etichette, nessun uso di
 * memoria liberata (ASan). */
static void gal_ostili(void)
{
    GtkWidget *r = ph_dsk_riga_nuova(NULL, NULL);
    g_object_ref_sink(r);
    ph_dsk_riga_imposta(r, NULL, PH_DSK_ACC_TEMA, true, "T\xff" "x", "S\xc3", "1\xfe");
    static const char *const classi[] = {"ph-desktop-riga-titolo", "ph-desktop-riga-sotto", "ph-desktop-riga-ora"};
    for (size_t i = 0; i < G_N_ELEMENTS(classi); i++) {
        GtkWidget *l = trova(r, classi[i], 0);
        if (l == NULL || !g_utf8_validate(gtk_label_get_text(GTK_LABEL(l)), -1, NULL))
            g_error("componenti: testo non valido nella riga (%s)", classi[i]);
    }
    GtkWidget *ic = gtk_image_new();
    ph_dsk_riga_icona(r, ic);
    ph_dsk_riga_icona(r, ic); /* la stessa: non deve fare nulla */
    ph_dsk_riga_icona(r, NULL);
    g_object_unref(r);
    GtkWidget *fe = ph_dsk_fette_nuove();
    g_object_ref_sink(fe);
    const PhDskFetta f[1] = {{"N\xffome", "1\xc3", 5, PH_DSK_ACC_TEMA}};
    ph_dsk_fette_imposta(fe, f, 1, 10);
    GtkWidget *l1 = trova(fe, "ph-desktop-legenda-nome", 0), *l2 = trova(fe, "ph-desktop-legenda-valore", 0);
    if (l1 == NULL || l2 == NULL || !g_utf8_validate(gtk_label_get_text(GTK_LABEL(l1)), -1, NULL) ||
        !g_utf8_validate(gtk_label_get_text(GTK_LABEL(l2)), -1, NULL))
        g_error("componenti: testo non valido nella legenda");
    g_object_unref(fe);
}

/* Nessun richiamo a fotogramma (gtk_widget_add_tick_callback) deve restare registrato su un
 * componente: terrebbe acceso l'orologio dei fotogrammi anche a desktop fermo. GTK collega un
 * gestore del segnale "update" dell'orologio con il widget come dato per ogni widget che ha un
 * richiamo a fotogramma: si cerca, in tutto cio' che sta dentro le schede della galleria, un widget
 * con quel gestore. (Un contatore di fotogrammi non va: Broadway senza browser non ne produce.) */
static int gal_richiami_a_fotogramma(GtkWidget *w, GdkFrameClock *fc, guint id)
{
    int n = 0;
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (g_signal_handler_find(fc, (GSignalMatchType)(G_SIGNAL_MATCH_ID | G_SIGNAL_MATCH_DATA), id, 0, NULL, NULL,
                                  c) != 0)
            n++;
        n += gal_richiami_a_fotogramma(c, fc, id);
    }
    return n;
}

static void gal_senza_richiami(GtkWidget *radice)
{
    GdkFrameClock *fc = gtk_widget_get_frame_clock(radice);
    if (fc == NULL)
        g_error("componenti: nessun orologio dei fotogrammi");
    guint id = g_signal_lookup("update", GDK_TYPE_FRAME_CLOCK);
    int tot = 0, schede = 0;
    for (int i = 0;; i++, schede++) {
        GtkWidget *scheda = trova(radice, "ph-desktop-galleria", i);
        if (scheda == NULL)
            break;
        tot += gal_richiami_a_fotogramma(scheda, fc, id);
    }
    if (schede == 0)
        g_error("componenti: nessuna scheda controllata per i richiami a fotogramma");
    if (tot != 0)
        g_error("componenti: %d widget dei componenti hanno un richiamo a fotogramma registrato", tot);
}

static void galleria_controlla(GtkWidget *radice, unsigned visti)
{
    gal_ostili();
    /* Le tinte vengono dal CSS: la sonda del verde ha il colore della variabile. */
    GtkWidget *sonda = trova(radice, "ph-desktop-sonda-verde", 0);
    if (sonda == NULL)
        g_error("componenti: nessuna sonda del verde");
    GdkRGBA verde;
    gtk_widget_get_color(sonda, &verde);
    if (lround(verde.red * 255) != 0x4a || lround(verde.green * 255) != 0xde || lround(verde.blue * 255) != 0x80)
        g_error("componenti: la sonda del verde ha il colore %.2f %.2f %.2f, non quello del CSS", verde.red,
                verde.green, verde.blue);
    /* I clic delle righe cliccabili arrivano alla callback; quella solo da guardare non ne da'. */
    unsigned prima = gal_clic;
    unsigned righe_cliccabili = 0;
    for (int i = 0;; i++) {
        GtkWidget *r = trova(radice, "ph-desktop-riga-elenco", i);
        if (r == NULL)
            break;
        g_signal_emit_by_name(r, "clicked");
        righe_cliccabili++;
    }
    if (righe_cliccabili == 0 || gal_clic - prima != gal_clic_attesi)
        g_error("componenti: %u clic arrivati su %u righe, attesi %u", gal_clic - prima, righe_cliccabili,
                gal_clic_attesi);
    g_message("componenti: %u disegni, tinta verde dal CSS, %u clic", visti, gal_clic - prima);
    gal_senza_richiami(radice);
    gal_verificata = true;
}

/* La misura arriva con il layout del fotogramma: si controlla a ogni fotogramma finche' tutti i
 * disegni ne hanno una (scadenza 4 s: poi la prova cade), senza aspettare un tempo fisso. */
static gboolean galleria_fotogramma(GtkWidget *radice, GdkFrameClock *clock, gpointer dati)
{
    (void)clock;
    gint64 *scadenza = dati;
    unsigned visti = 0, con_misura = 0;
    gal_conta_disegni(radice, &visti, &con_misura);
    /* Per ogni misura: anello, barra a fette, un numero nelle fette, curva, 3 tratti, 2 numeri. */
    unsigned attesi = 9 * gal_n_misure;
    const char *disposizione = NULL;
    if (gal_n_misure != 0 && visti == attesi && con_misura == visti) {
        disposizione = gal_disposizione(radice);
        if (disposizione == NULL) {
            galleria_controlla(radice, visti);
            return G_SOURCE_REMOVE;
        }
    }
    if (g_get_monotonic_time() > *scadenza)
        g_error("componenti: dopo 4 s i disegni sono %u (attesi %u), con una misura vera %u; %s", visti, attesi,
                con_misura, disposizione != NULL ? disposizione : "disposizione non controllata");
    return G_SOURCE_CONTINUE;
}

static void galleria_verifica(GtkWidget *radice)
{
    gint64 *scadenza = g_new(gint64, 1); /* vive quanto il processo: il callback e' l'ultimo a usarlo */
    *scadenza = g_get_monotonic_time() + 4 * G_USEC_PER_SEC;
    gtk_widget_add_tick_callback(radice, galleria_fotogramma, scadenza, NULL);
}

/* Widget predefiniti di un profilo nuovo: a schermo stanno a 12 px l'uno dall'altro, il profilo
 * non viene riscritto finche' l'utente non cambia qualcosa; alla prima modifica (qui un cambio di
 * stile) i posti mostrati finiscono nel profilo, cosi' gli altri non tornano a toccarsi. */
static bool predefiniti_verificati;
static void predefiniti_esci(void)
{
    if (!predefiniti_verificati) {
        fprintf(stderr, "predefiniti: la verifica non e' stata eseguita\n");
        _exit(1);
    }
}

static void predefiniti_controlla(PhDesktop *d, PhCfgTelefono *p)
{
    static const int alti_profilo[3] = {16, 120, 240};
    for (size_t i = 0; i < 3; i++)
        if (p->widget[i].alto != alti_profilo[i])
            g_error("predefiniti: il profilo e' stato riscritto prima di una modifica dell'utente");
    for (size_t i = 0; i + 1 < 3; i++) {
        const PhDskWidget *a = g_ptr_array_index(d->widget, i), *b = g_ptr_array_index(d->widget, i + 1);
        if (b->alto - (a->alto + a->altezza) != PH_DSK_DISTANZA_WIDGET)
            g_error("predefiniti: tra il widget %zu e il successivo ci sono %d px, attesi %d", i,
                    b->alto - (a->alto + a->altezza), PH_DSK_DISTANZA_WIDGET);
    }
    int mostrati[3];
    for (size_t i = 0; i < 3; i++)
        mostrati[i] = ((const PhDskWidget *)g_ptr_array_index(d->widget, i))->alto;
    /* Una modifica dell'utente: il profilo prende i posti mostrati e dopo la ricostruzione non cambiano. */
    unsigned id = ((const PhDskWidget *)g_ptr_array_index(d->widget, 0))->id;
    ph_dsk_widget_stile_imposta(d, id, PH_DSK_OROLOGIO_CLASSICO, 0);
    for (size_t i = 0; i < 3; i++) {
        const PhDskWidget *w = g_ptr_array_index(d->widget, i);
        if (p->widget[i].alto != mostrati[i] && i > 0)
            g_error("predefiniti: dopo la modifica il profilo non ha il posto mostrato (%zu)", i);
        if (i > 0 && w->alto != mostrati[i])
            g_error("predefiniti: dopo la modifica il widget %zu si e' spostato", i);
    }
    predefiniti_verificati = true;
    g_message("predefiniti: widget a %d px l'uno dall'altro, profilo riscritto solo alla modifica",
              PH_DSK_DISTANZA_WIDGET);
}

/* Il profilo del telefono finto arriva dopo un po': si controlla a ogni fotogramma finche' ci sono
 * i tre widget (scadenza 4 s: poi la prova cade). */
static gboolean predefiniti_fotogramma(GtkWidget *radice, GdkFrameClock *clock, gpointer dati)
{
    (void)clock;
    gint64 *scadenza = dati;
    PhDesktop *d = g_object_get_data(G_OBJECT(radice), "ph-desktop");
    PhCfgTelefono *p = d != NULL ? ph_dsk_profilo(d) : NULL;
    if (p != NULL && d->widget->len == 3 && p->n_widget == 3) {
        predefiniti_controlla(d, p);
        return G_SOURCE_REMOVE;
    }
    if (g_get_monotonic_time() > *scadenza)
        g_error("predefiniti: dopo 4 s i widget a schermo non sono i tre predefiniti");
    return G_SOURCE_CONTINUE;
}

/* «componenti» prima che il profilo del telefono finto sia arrivato: si riprova a ogni fotogramma. */
typedef struct {
    PhShell *sh;
    int solo;
    gint64 scadenza;
} GalAttesa;

static gboolean galleria_attesa_profilo(GtkWidget *radice, GdkFrameClock *clock, gpointer dati)
{
    (void)clock;
    GalAttesa *a = dati;
    PhDesktop *d = g_object_get_data(G_OBJECT(radice), "ph-desktop");
    PhCfgTelefono *p = d != NULL ? ph_dsk_profilo(d) : NULL;
    if (p != NULL) {
        galleria_costruisci(a->sh, d, p, a->solo);
        return G_SOURCE_REMOVE;
    }
    if (g_get_monotonic_time() > a->scadenza)
        g_error("componenti: dopo 4 s il profilo non e' arrivato");
    return G_SOURCE_CONTINUE;
}

void ph_demo_mostra(PhShell *sh, const char *cosa)
{
    GtkWidget *radice = ph_shell_radice(sh);
    PhDesktop *d = g_object_get_data(G_OBJECT(radice), "ph-desktop");
    PhCfgTelefono *p = d != NULL ? ph_dsk_profilo(d) : NULL;
    /* Le verifiche con attesa a fotogrammi partono anche se il profilo non e' ancora arrivato. */
    bool attende = cosa != NULL && (strcmp(cosa, "predefiniti-verifica") == 0 || strcmp(cosa, "componenti-verifica") == 0 ||
                                    strcmp(cosa, "componenti") == 0 || g_str_has_prefix(cosa, "componenti:"));
    /* agente D: prova a schermo degli Interruttori rapidi (demo_controlli.c) */
    if (cosa != NULL && strncmp(cosa, "controlli-", 10) == 0) {
        ph_demo_controlli(sh, cosa);
        return;
    }
    /* fine agente D */
    /* agente C: prova a schermo del Meteo, stile «Adesso» (demo_meteo.c) */
    if (cosa != NULL && strncmp(cosa, "meteoa-", 7) == 0) {
        ph_demo_meteo(sh, cosa);
        return;
    }
    /* fine agente C */
    if (d == NULL || cosa == NULL || (p == NULL && !attende))
        return;
    if (strcmp(cosa, "widget3") == 0) {
        /* Tutti e cinque i widget, nelle tre dimensioni. */
        static const PhCfgWidget w[] = {
            {.tipo = PH_CFG_WIDGET_OROLOGIO, .destra = 16, .alto = 16, .dimensione = PH_CFG_WIDGET_PICCOLO},
            {.tipo = PH_CFG_WIDGET_STATO, .destra = 16, .alto = 96, .dimensione = PH_CFG_WIDGET_GRANDE},
            {.tipo = PH_CFG_WIDGET_NOTIFICHE, .destra = 352, .alto = 16, .dimensione = PH_CFG_WIDGET_GRANDE},
            {.tipo = PH_CFG_WIDGET_MEMORIA, .destra = 352, .alto = 216, .dimensione = PH_CFG_WIDGET_GRANDE},
            {.tipo = PH_CFG_WIDGET_CALENDARIO, .destra = 16, .alto = 288, .dimensione = PH_CFG_WIDGET_GRANDE},
            {.tipo = PH_CFG_WIDGET_OROLOGIO, .destra = 352, .alto = 440, .dimensione = PH_CFG_WIDGET_MEDIO},
        };
        memcpy(p->widget, w, sizeof w);
        p->n_widget = G_N_ELEMENTS(w);
        ph_dsk_widget_ricostruisci(d);
    } else if (g_str_has_prefix(cosa, "orologi:")) {
        /* Sei orologi, uno per stile, nella dimensione P, M o G (foto di tutti gli stili). */
        PhCfgWidgetDim dim = cosa[8] == 'P' ? PH_CFG_WIDGET_PICCOLO
                             : cosa[8] == 'G' ? PH_CFG_WIDGET_GRANDE
                                              : PH_CFG_WIDGET_MEDIO;
        int largo = cosa[8] == 'P' ? 200 : (cosa[8] == 'G' ? 320 : 250);
        int alto_riga = cosa[8] == 'P' ? 175 : (cosa[8] == 'G' ? 260 : 210);
        memset(p->widget, 0, sizeof p->widget);
        for (int i = 0; i < PH_DSK_OROLOGIO_STILI; i++) {
            p->widget[i].tipo = PH_CFG_WIDGET_OROLOGIO;
            p->widget[i].destra = 16 + (i % 3) * (largo + 16);
            p->widget[i].alto = 16 + (i / 3) * alto_riga;
            p->widget[i].dimensione = dim;
            p->widget[i].stile = i;
            p->widget[i].opzione = i == PH_DSK_OROLOGIO_DOPPIO_FUSO ? 6 : 0; /* Tokyo */
        }
        p->n_widget = PH_DSK_OROLOGIO_STILI;
        ph_dsk_widget_ricostruisci(d);
    } else if (g_str_has_prefix(cosa, "wx:")) {
        /* Tre copie di un widget nuovo, Grande, Medio e Piccolo (foto di tutte le misure).
         * wx:<tipo>[:<stile>] con tipo cal, sveglia, batteria, interruttori, trova, frequenti,
         * trasferimenti. */
        static const struct {
            const char *nome;
            PhCfgWidgetTipo tipo;
        } tipi[] = {{"cal", PH_CFG_WIDGET_CALENDARIO_MESE}, {"sveglia", PH_CFG_WIDGET_SVEGLIA},
                    {"batteria", PH_CFG_WIDGET_BATTERIA},   {"interruttori", PH_CFG_WIDGET_INTERRUTTORI},
                    {"trova", PH_CFG_WIDGET_TROVA},
                    {"meteo", PH_CFG_WIDGET_METEO},          {"frequenti", PH_CFG_WIDGET_APP_FREQUENTI},
                    {"trasferimenti", PH_CFG_WIDGET_TRASFERIMENTI}};
        char *copia = g_strdup(cosa + 3);
        char *due = strchr(copia, ':');
        if (strcmp(copia, "frequenti") == 0) {
            /* Aperture finte, con numeri diversi: «frequenti-vuoto» le lascia vuote. */
            static const char *const app[] = {"it.finto.messaggi", "it.finto.browser", "it.finto.note",
                                              "it.finto.foto",     "it.finto.mappe",   "it.finto.musica",
                                              "it.finto.calendario", "it.finto.contatti", "it.finto.file",
                                              "it.finto.meteo",    "it.finto.video",   "it.finto.fotocamera"};
            p->n_aperture = 0;
            for (size_t i = 0; i < G_N_ELEMENTS(app); i++)
                for (size_t k = 0; k < G_N_ELEMENTS(app) - i; k++)
                    ph_cfg_telefono_conta_apertura(p, app[i], 1700000000 + (int64_t)i * 100 + (int64_t)k, NULL);
        } else if (strcmp(copia, "frequenti-vuoto") == 0) {
            p->n_aperture = 0;
            g_strlcpy(copia, "frequenti", strlen(copia) + 1);
        }
        int stile = 0;
        if (due != NULL) {
            *due = '\0';
            stile = atoi(due + 1);
        }
        memset(p->widget, 0, sizeof p->widget);
        p->n_widget = 0;
        for (size_t i = 0; i < G_N_ELEMENTS(tipi); i++) {
            if (strcmp(tipi[i].nome, copia) != 0)
                continue;
            static const struct {
                PhCfgWidgetDim dim;
                int destra;
            } cop[] = {{PH_CFG_WIDGET_GRANDE, 16}, {PH_CFG_WIDGET_MEDIO, 352}, {PH_CFG_WIDGET_PICCOLO, 618}};
            for (size_t k = 0; k < 3; k++) {
                PhCfgWidget *n = &p->widget[p->n_widget++];
                n->tipo = tipi[i].tipo;
                n->destra = cop[k].destra;
                n->alto = 16;
                n->dimensione = cop[k].dim;
                n->stile = stile;
            }
        }
        g_free(copia);
        ph_dsk_widget_ricostruisci(d);
    } else if (g_str_has_prefix(cosa, "clic:")) {
        /* clic:<classe CSS>:<n>: il clic sul n-esimo widget con quella classe. */
        char *copia = g_strdup(cosa + 5);
        char *due = strrchr(copia, ':');
        int n = 0;
        if (due != NULL) {
            *due = '\0';
            n = atoi(due + 1);
        }
        GtkWidget *b = trova(radice, copia, n);
        if (b != NULL)
            g_signal_emit_by_name(b, "clicked");
        g_free(copia);
    } else if (strcmp(cosa, "snap-profilo") == 0 || strcmp(cosa, "verifica-profilo") == 0) {
        /* Prova: un evento meteo con localita' dal telefono non cambia il profilo e non lo salva.
         * snap-profilo fotografa (citta' scelta, salvataggi) e comincia a contare gli eventi meteo;
         * verifica-profilo aspetta di averne visti almeno due DOPO la fotografia (riprovando finche'
         * non arrivano, con una scadenza propria: se scade la prova cade) e solo allora confronta.
         * Se all'uscita la verifica non e' stata eseguita il processo esce con errore. */
        if (cosa[0] == 's') {
            if (!snap_fatta) {
                ph_srv_ascolta(d->srv, PH_SRV_EV_METEO, conta_meteo, NULL);
                atexit(snap_esci);
            }
            snap_scelto = p->meteo_luogo_scelto;
            memcpy(snap_nome, p->meteo_luogo_nome, sizeof snap_nome);
            memcpy(snap_dett, p->meteo_luogo_dettaglio, sizeof snap_dett);
            snap_lat = p->meteo_luogo_lat_centesimi;
            snap_lon = p->meteo_luogo_lon_centesimi;
            snap_salv = d->salvataggi;
            snap_meteo = 0;
            snap_fatta = true;
        } else {
            if (!snap_fatta)
                g_error("verifica-profilo senza snap-profilo");
            if (snap_meteo < 2) {
                gint64 ora = g_get_monotonic_time();
                if (snap_scadenza == 0)
                    snap_scadenza = ora + 4 * G_USEC_PER_SEC;
                if (ora >= snap_scadenza)
                    g_error("prova meteo: dopo la fotografia sono arrivati solo %u eventi meteo (ne servono 2)",
                            snap_meteo);
                Riprova *r = g_new0(Riprova, 1);
                r->sh = sh;
                g_timeout_add(25, riprova_verifica, r);
                return;
            }
            if (snap_scelto != p->meteo_luogo_scelto || memcmp(snap_nome, p->meteo_luogo_nome, sizeof snap_nome) != 0 ||
                memcmp(snap_dett, p->meteo_luogo_dettaglio, sizeof snap_dett) != 0 ||
                snap_lat != p->meteo_luogo_lat_centesimi || snap_lon != p->meteo_luogo_lon_centesimi ||
                snap_salv != d->salvataggi || p->meteo_luogo_scelto)
                g_error("un evento meteo ha cambiato o salvato il profilo");
            verifica_eseguita = true;
            g_message("prova meteo: %u eventi meteo, profilo invariato, nessun salvataggio", snap_meteo);
        }
    } else if (strcmp(cosa, "tessere") == 0) {
        tessere_avvia(sh, d);
    } else if (strcmp(cosa, "vetro-attendi") == 0) {
        /* Se all'uscita verifica-vetro non e' stata eseguita, il processo esce con errore. */
        atexit(vetro_esci);
    } else if (strcmp(cosa, "verifica-vetro") == 0) {
        /* Prova del vetro (fase 5, widget scuri dal passo 8.1): ogni superficie del pezzo, una volta
         * costruita, ha la classe del suo vetro (la mette ph_shell_vetro_registra): i widget sono
         * vetro SCURO (come la barra), menu e tre dialoghi vetro chiaro. Si apre ogni cosa e si
         * controlla quella visibile; se una manca o ha l'altro vetro, il processo cade. */
        static const struct {
            const char *classe, *vetro, *altro;
        } classi[] = {{"ph-desktop-widget", "ph-vetro-scuro", "ph-vetro-chiaro"},
                      {"ph-menu", "ph-vetro-chiaro", "ph-vetro-scuro"}};
        int visti = 0;
        for (size_t k = 0; k < G_N_ELEMENTS(classi); k++)
            for (int i = 0;; i++) {
                GtkWidget *w = trova(radice, classi[k].classe, i);
                if (w == NULL)
                    break;
                visti++;
                if (!gtk_widget_has_css_class(w, classi[k].vetro) || gtk_widget_has_css_class(w, classi[k].altro))
                    g_error("verifica-vetro: %s non e' %s", classi[k].classe, classi[k].vetro);
            }
        if (visti < 3)
            g_error("verifica-vetro: attesi almeno 3 widget/menu, visti %d", visti);
        void (*apri[])(PhDesktop *) = {ph_dsk_dialogo_sfondo_apri, ph_dsk_dialogo_widget_apri, ph_dsk_luogo_apri};
        for (size_t k = 0; k < G_N_ELEMENTS(apri); k++) {
            apri[k](d);
            int dialoghi = 0;
            for (int i = 0;; i++) {
                GtkWidget *w = trova(radice, "ph-dialogo", i);
                if (w == NULL)
                    break;
                if (!gtk_widget_has_css_class(w, "ph-vetro-chiaro") || gtk_widget_has_css_class(w, "ph-vetro-scuro"))
                    g_error("verifica-vetro: dialogo %zu non e' vetro chiaro", k);
                dialoghi++;
            }
            if (dialoghi == 0)
                g_error("verifica-vetro: dialogo %zu non trovato dopo l'apertura", k);
            ph_shell_popup_chiudi(sh);
        }
        verifica_vetro_fatta = true;
        g_message("verifica-vetro: widget vetro scuro, menu e dialoghi vetro chiaro");
    } else if (strcmp(cosa, "componenti") == 0 || g_str_has_prefix(cosa, "componenti:")) {
        int solo = cosa[10] == ':' ? atoi(cosa + 11) : -1;
        if (p != NULL)
            galleria_costruisci(sh, d, p, solo);
        else { /* il profilo non e' ancora arrivato: si aspetta a fotogrammi, con scadenza */
            GalAttesa *a = g_new0(GalAttesa, 1);
            a->sh = sh;
            a->solo = solo;
            a->scadenza = g_get_monotonic_time() + 4 * G_USEC_PER_SEC;
            gtk_widget_add_tick_callback(radice, galleria_attesa_profilo, a, g_free);
        }
    } else if (strcmp(cosa, "predefiniti-attendi") == 0) {
        atexit(predefiniti_esci);
    } else if (strcmp(cosa, "predefiniti-verifica") == 0) {
        gint64 *scadenza = g_new(gint64, 1); /* vive quanto il processo */
        *scadenza = g_get_monotonic_time() + 4 * G_USEC_PER_SEC;
        gtk_widget_add_tick_callback(radice, predefiniti_fotogramma, scadenza, NULL);
    } else if (strcmp(cosa, "componenti-verifica") == 0) {
        galleria_verifica(radice);
    } else if (strcmp(cosa, "vecchi") == 0) { /* agente A */
        vecchi_avvia(sh, d, p);
    } else if (g_str_has_prefix(cosa, "vecchi:")) { /* agente A: foto, nessun controllo */
        vecchi_foto(d, p, cosa + 7);
    } else if (strcmp(cosa, "stress") == 0) {
        stress_avvia(sh);
    } else if (strcmp(cosa, "luogo") == 0) {
        ph_dsk_luogo_apri(d);
    } else if (g_str_has_prefix(cosa, "luogo-cerca:")) {
        /* Scrive il testo nel campo e preme Invio (come l'utente): la ricerca parte solo cosi'. */
        GtkWidget *campo = trova(radice, "ph-campo", 0);
        if (campo != NULL) {
            gtk_editable_set_text(GTK_EDITABLE(campo), cosa + 12);
            g_signal_emit_by_name(campo, "activate");
        }
    } else if (strcmp(cosa, "menu-orologio") == 0) {
        GtkWidget *w = trova(radice, "ph-desktop-widget", 0);
        if (w != NULL)
            ph_dsk_menu_widget(d, GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), "ph-desktop-id")),
                               700, 60);
    } else if (strcmp(cosa, "menu-desktop") == 0) {
        ph_dsk_menu_desktop(d, 520, 300);
    } else if (strcmp(cosa, "menu-icona") == 0) {
        ph_dsk_menu_icona(d, "it.finto.browser", 60, 150);
    } else if (strcmp(cosa, "menu-widget") == 0) {
        GtkWidget *w = trova(radice, "ph-desktop-widget", 0);
        if (w != NULL)
            ph_dsk_menu_widget(d, GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), "ph-desktop-id")),
                               900, 60);
    } else if (strcmp(cosa, "sfondo") == 0) {
        ph_dsk_dialogo_sfondo_apri(d);
    } else if (strcmp(cosa, "sfondo-solido") == 0 || strcmp(cosa, "sfondo-sfumato") == 0) {
        PhCfgSfondo s = p->sfondo;
        s.tipo = strcmp(cosa, "sfondo-solido") == 0 ? PH_CFG_SFONDO_SOLIDO : PH_CFG_SFONDO_SFUMATO;
        ph_dsk_sfondo_scegli(d, &s);
        ph_dsk_dialogo_sfondo_apri(d);
    } else if (strcmp(cosa, "sfondo-immagine") == 0) {
        ph_dsk_dialogo_sfondo_apri(d);
        GtkWidget *scheda = trova(radice, "ph-desktop-scheda", 2);
        if (scheda != NULL)
            g_signal_emit_by_name(scheda, "clicked");
    } else if (g_str_has_prefix(cosa, "tinta:")) {
        /* Clic sulla N-esima tinta della prima tavolozza (quella del solido). */
        GtkWidget *b = trova(radice, "ph-desktop-pal", atoi(cosa + 6));
        if (b != NULL)
            g_signal_emit_by_name(b, "clicked");
    } else if (strcmp(cosa, "sfondo-cartelle") == 0) {
        ph_demo_mostra(sh, "sfondo-immagine");
        GtkWidget *sotto = trova(radice, "ph-desktop-opzione", 5); /* dopo le 4 direzioni */
        if (sotto != NULL)
            g_signal_emit_by_name(sotto, "clicked");
    } else if (strcmp(cosa, "cartelle-su") == 0) {
        GtkWidget *su = trova(radice, "ph-desktop-su", 0);
        if (su != NULL)
            g_signal_emit_by_name(su, "clicked");
    } else if (g_str_has_prefix(cosa, "cartelle-entra:")) {
        /* La riga della sottocartella con quel nome. */
        for (int i = 0;; i++) {
            GtkWidget *b = trova(radice, "ph-desktop-cartella", i);
            if (b == NULL)
                break;
            GtkWidget *riga = gtk_button_get_child(GTK_BUTTON(b));
            GtkWidget *nome = gtk_widget_get_last_child(riga);
            if (GTK_IS_LABEL(nome) && strcmp(gtk_label_get_text(GTK_LABEL(nome)), cosa + 15) == 0) {
                g_signal_emit_by_name(b, "clicked");
                break;
            }
        }
    } else if (strcmp(cosa, "sfondo-scegli-prima") == 0) {
        GtkWidget *m = trova(radice, "ph-desktop-miniatura", 0);
        if (m != NULL)
            g_signal_emit_by_name(m, "clicked");
    } else if (strcmp(cosa, "catalogo") == 0) {
        ph_dsk_dialogo_widget_apri(d);
    } else if (strcmp(cosa, "chiudi") == 0) {
        ph_shell_popup_chiudi(sh);
    }
}

/* ---- Prova d'insieme (demo --mostra stress) -------------------------------------------------------
 * Fa passare il desktop da tutti i casi: ogni tipo di widget in ogni stile e misura, gli eventi del
 * telefono finto, i cambi a caldo, le rimozioni, i menu e i dialoghi, il cambio di profilo e la
 * chiusura della finestra con tutto ancora vivo. Nessun tempo fisso: un passo avanza all'idle (che
 * viene dopo ogni evento e dopo il ridisegno) e le attese sono segnali (un evento dei servizi, un
 * fotogramma, un elemento comparso) con una scadenza di sicurezza. Sotto fatal-criticals e
 * AddressSanitizer: se qualcosa si rompe la prova cade. Se la sequenza non arriva in fondo
 * (blocco, finestra chiusa prima) il processo esce con errore. */
typedef struct {
    PhShell *sh;
    PhDesktop *d;
    GtkWidget *radice; /* NULL dopo la distruzione (puntatore debole) */
    size_t passo;
    size_t n, k, j, l;  /* contatori dentro il passo: tipo, fase, indice, fase di st_lista */
    unsigned eventi;    /* eventi dei servizi visti finora (di qualunque tipo) */
    unsigned per[PH_SRV_EV_NUMERO]; /* ... e per tipo */
    unsigned base;      /* eventi visti quando e' iniziata l'attesa */
    unsigned scadute;   /* attese «qualunque evento» scadute senza evento (solo per il rapporto) */
    gint64 scadenza;    /* 0 = nessuna attesa in corso */
    gint64 fotogramma;  /* contatore dei fotogrammi quando e' iniziata l'attesa */
    gboolean in_fotogramma;
    guint ascolti[PH_SRV_EV_NUMERO];
} Stress;

static bool stress_finito = false;
static bool stress_iniziato = false;
static bool stress_annullo_verificato = false;

static void stress_evento(PhServizi *srv, PhSrvEvento ev, const char *arg, void *utente)
{
    (void)srv;
    (void)arg;
    ((Stress *)utente)->eventi++;
    ((Stress *)utente)->per[ev]++;
}

/* Attesa debole, per i comandi che possono non dare nessun evento (stato letto piu' tardi): true
 * quando e' arrivato un evento qualunque o e' passato `ms`; le scadenze si contano. */
static bool st_qualunque(Stress *s, int ms)
{
    gint64 ora = g_get_monotonic_time();
    if (s->scadenza == 0) {
        s->base = s->eventi;
        s->scadenza = ora + (gint64)ms * 1000;
    }
    if (s->eventi != s->base) {
        s->scadenza = 0;
        return true;
    }
    if (ora >= s->scadenza) {
        s->scadenza = 0;
        s->scadute++;
        return true;
    }
    return false;
}

/* Attesa forte: l'evento PRECISO `ev` deve arrivare entro `ms`, altrimenti la prova cade. */
static bool st_attendi(Stress *s, PhSrvEvento ev, int ms)
{
    gint64 ora = g_get_monotonic_time();
    if (s->scadenza == 0) {
        s->base = s->per[ev];
        s->scadenza = ora + (gint64)ms * 1000;
    }
    if (s->per[ev] != s->base) {
        s->scadenza = 0;
        return true;
    }
    if (ora >= s->scadenza)
        g_error("prova d'insieme: l'evento %d dei servizi non e' arrivato entro %d ms (passo %zu)", (int)ev, ms,
                s->passo);
    return false;
}

/* true dopo che e' stato disegnato almeno un fotogramma (o dopo 150 ms: finestra non visibile). */
static bool st_fotogramma(Stress *s)
{
    GdkFrameClock *fc = gtk_widget_get_frame_clock(s->radice);
    gint64 ora = g_get_monotonic_time();
    gint64 n = fc != NULL ? gdk_frame_clock_get_frame_counter(fc) : 0;
    if (!s->in_fotogramma) {
        s->in_fotogramma = true;
        s->fotogramma = n;
        s->scadenza = ora + 150 * 1000;
    }
    if (n > s->fotogramma || ora >= s->scadenza) {
        s->in_fotogramma = false;
        s->scadenza = 0;
        return true;
    }
    return false;
}

/* Attesa forte su una condizione della finestra (un elemento comparso): scaduta, la prova cade. */
static bool st_finche(Stress *s, bool vero, int ms, const char *cosa)
{
    gint64 ora = g_get_monotonic_time();
    if (s->scadenza == 0)
        s->scadenza = ora + (gint64)ms * 1000;
    if (vero) {
        s->scadenza = 0;
        return true;
    }
    if (ora >= s->scadenza)
        g_error("prova d'insieme: non e' comparso: %s (passo %zu)", cosa, s->passo);
    return false;
}

static PhCfgTelefono *st_profilo(Stress *s)
{
    return ph_dsk_profilo(s->d);
}

static PhDskWidget *st_widget(Stress *s, size_t i)
{
    return i < s->d->widget->len ? g_ptr_array_index(s->d->widget, i) : NULL;
}

/* Un comando del telefono finto e l'evento che deve produrre (-1 = nessuno preciso: si aspetta
 * un evento qualunque, con scadenza, perche' il dato si legge piu' tardi). */
typedef struct {
    const char *c;
    int ev;
} StCmd;

/* Esegue i comandi del finto uno alla volta, ciascuno seguito dall'attesa del suo evento. true alla fine. */
static bool st_lista(Stress *s, const StCmd *cmd)
{
    if (cmd[s->j].c == NULL)
        return true;
    if (s->l == 0) {
        ph_demo_finto_comando(cmd[s->j].c);
        s->l = 1;
    }
    bool fatto = cmd[s->j].ev >= 0 ? st_attendi(s, (PhSrvEvento)cmd[s->j].ev, 2000) : st_qualunque(s, 120);
    if (fatto) {
        s->j++;
        s->l = 0;
    }
    return cmd[s->j].c == NULL;
}

/* Un widget per ogni stile e ogni misura del tipo (al massimo quelli che stanno nel profilo). */
static void st_costruisci_tipo(Stress *s, PhCfgWidgetTipo tipo)
{
    PhCfgTelefono *p = st_profilo(s);
    memset(p->widget, 0, sizeof p->widget);
    p->n_widget = 0;
    size_t n_stili = ph_dsk_stili_n(tipo);
    for (size_t st = 0; st < n_stili; st++)
        for (int dim = PH_CFG_WIDGET_PICCOLO; dim <= PH_CFG_WIDGET_GRANDE; dim++) {
            if (p->n_widget >= PH_CFG_MAX_WIDGET)
                break;
            PhCfgWidget *w = &p->widget[p->n_widget++];
            w->tipo = tipo;
            w->stile = (int)st;
            w->dimensione = (PhCfgWidgetDim)dim;
            w->opzione = tipo == PH_CFG_WIDGET_OROLOGIO && st == PH_DSK_OROLOGIO_DOPPIO_FUSO ? 6 : 0;
            w->destra = 16 + (int)(p->n_widget % 4) * 90;
            w->alto = 16 + (int)(p->n_widget / 4) * 60;
        }
    ph_dsk_widget_ricostruisci(s->d);
}

/* Comandi del telefono finto per ogni tipo (dati nuovi, mancanti, in errore, di nuovo buoni). */
static const struct {
    PhCfgWidgetTipo tipo;
    StCmd cmd[16];
} ST_COPIONE[] = {
    {PH_CFG_WIDGET_OROLOGIO, {{NULL, -1}}},
    {PH_CFG_WIDGET_CALENDARIO, {{"nd:calendario", -1}, {"err:calendario", -1}, {"ok:calendario", -1}, {"cal-max", -1}, {NULL, -1}}},
    {PH_CFG_WIDGET_STATO, {{"batt:15", PH_SRV_EV_TELEFONO}, {"carica", -1}, {"batt:90", PH_SRV_EV_TELEFONO}, {NULL, -1}}},
    {PH_CFG_WIDGET_NOTIFICHE, {{"notifica", PH_SRV_EV_NOTIFICHE}, {"notifica-agg", PH_SRV_EV_NOTIFICHE},
                               {"notifiche-ill:1", -1}, {"notifiche-ill:0", -1}, {NULL, -1}}},
    {PH_CFG_WIDGET_MEMORIA, {{NULL, -1}}},
    {PH_CFG_WIDGET_METEO, {{"meteo-nuovi", PH_SRV_EV_METEO}, {"meteo-cond:1", PH_SRV_EV_METEO},
                           {"meteo-cond:2", PH_SRV_EV_METEO}, {"meteo-cond:3", PH_SRV_EV_METEO},
                           {"meteo-cond:4", PH_SRV_EV_METEO}, {"meteo-cond:5", PH_SRV_EV_METEO},
                           {"meteo-cond:6", PH_SRV_EV_METEO}, {"meteo-cond:0", PH_SRV_EV_METEO},
                           {"meteo-modo:2", PH_SRV_EV_METEO}, {"meteo-modo:1", PH_SRV_EV_METEO},
                           {"meteo-modo:3", PH_SRV_EV_METEO}, {"meteo-modo:4", PH_SRV_EV_METEO},
                           {"meteo-modo:0", PH_SRV_EV_METEO}, {NULL, -1}}},
    {PH_CFG_WIDGET_CALENDARIO_MESE, {{"nd:calendario", -1}, {"err:calendario", -1}, {"ok:calendario", -1}, {NULL, -1}}},
    {PH_CFG_WIDGET_SVEGLIA, {{"sveglia-assente", -1}, {"nd:sveglia", -1}, {"err:sveglia", -1}, {"ok:sveglia", -1}, {NULL, -1}}},
    {PH_CFG_WIDGET_BATTERIA, {{"nd:batteria", -1}, {"err:batteria", -1}, {"ok:batteria", -1},
                              {"batt:5", PH_SRV_EV_TELEFONO}, {"carica", -1}, {NULL, -1}}},
    {PH_CFG_WIDGET_INTERRUTTORI, {{"nd:controlli", -1}, {"err:controlli", -1}, {"ok:controlli", -1}, {"torcia-nd", -1},
                                  {"suoneria-silenzioso", PH_SRV_EV_CONTROLLI}, {"dnd", PH_SRV_EV_CONTROLLI},
                                  {"torcia", PH_SRV_EV_CONTROLLI}, {NULL, -1}}},
    {PH_CFG_WIDGET_APP_FREQUENTI, {{NULL, -1}}},
    {PH_CFG_WIDGET_TRASFERIMENTI, {{"trasf", PH_SRV_EV_TRASFERIMENTI}, {NULL, -1}}},
};

static bool st_collegato(Stress *s)
{
    PhSrvCollegamento c;
    ph_srv_collegamento(s->d->srv, &c);
    if (s->scadenza == 0)
        s->scadenza = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    if (c.stato != PH_SRV_COLLEGATO || st_profilo(s) == NULL) {
        if (g_get_monotonic_time() > s->scadenza)
            g_error("prova d'insieme: il telefono finto non si collega");
        return false;
    }
    /* Qualche apertura, cosi' «App frequenti» ha di che mostrare. */
    static const char *const app[] = {"it.finto.messaggi", "it.finto.browser", "it.finto.note", "it.finto.foto"};
    PhCfgTelefono *p = st_profilo(s);
    for (size_t i = 0; i < G_N_ELEMENTS(app); i++)
        for (size_t q = 0; q <= i; q++)
            ph_cfg_telefono_conta_apertura(p, app[i], 1700000000 + (int64_t)(i * 10 + q), NULL);
    return true;
}

static int st_barre_viste; /* quante barre sono state controllate */

/* La maniglia di ridimensionamento da tastiera: preme una freccia sul controller dei tasti della
 * maniglia (come farebbe l'utente col fuoco su di lei) e controlla che nel profilo finiscano
 * misura, posto e livello che la logica pura calcola per quello scatto. Se togli il salvataggio
 * o il collegamento della maniglia la prova cade. */
static void st_prova_maniglia(Stress *s, size_t quale, guint tasto)
{
    PhDskWidget *w = st_widget(s, quale);
    PhCfgTelefono *p = st_profilo(s);
    if (w == NULL || p == NULL || w->indice >= p->n_widget)
        return;
    size_t ip = w->indice;
    PhDskMisura prima = {w->larghezza, w->altezza};
    double dx = tasto == GDK_KEY_Right ? PH_DSK_PASSO : (tasto == GDK_KEY_Left ? -PH_DSK_PASSO : 0);
    double dy = tasto == GDK_KEY_Down ? PH_DSK_PASSO : (tasto == GDK_KEY_Up ? -PH_DSK_PASSO : 0);
    if (w->forma == PH_DSK_FORMA_TONDO)
        dx = dy = dx != 0 ? dx : dy;
    PhDskMisura atteso;
    int destra_atteso;
    ph_dsk_ridimensiona(w->tipo, w->stile, prima, w->destra, w->alto, dx, dy,
                        s->d->area_a - PH_DSK_BARRA, &atteso, &destra_atteso);
    PhCfgWidgetDim livello_atteso = ph_dsk_livello(w->tipo, w->stile, atteso);
    PhCfgWidgetTipo tipo = w->tipo;
    GListModel *controller = gtk_widget_observe_controllers(w->maniglia);
    gboolean preso = FALSE;
    for (guint i = 0; i < g_list_model_get_n_items(controller); i++) {
        GObject *c = g_list_model_get_item(controller, i);
        if (GTK_IS_EVENT_CONTROLLER_KEY(c))
            g_signal_emit_by_name(c, "key-pressed", tasto, 0u, 0, &preso);
        g_object_unref(c);
    }
    g_object_unref(controller); /* w puo' essere stato ricostruito: da qui non si tocca piu' */
    if (!preso)
        g_error("prova d'insieme: la maniglia non ha preso il tasto");
    PhCfgWidget *c = &p->widget[ip];
    if (c->tipo != tipo || c->larghezza != atteso.larghezza || c->altezza != atteso.altezza ||
        c->destra != destra_atteso || c->dimensione != livello_atteso)
        g_error("prova d'insieme: la maniglia non ha salvato la misura attesa (tipo %d)", (int)tipo);
    /* A schermo: il widget (anche ricostruito) ha la misura salvata. */
    for (size_t i = 0; i < s->d->widget->len; i++) {
        PhDskWidget *n = g_ptr_array_index(s->d->widget, i);
        if (n->indice == ip && (n->larghezza != atteso.larghezza || n->altezza != atteso.altezza))
            g_error("prova d'insieme: la misura a schermo non e' quella salvata (tipo %d)", (int)tipo);
        /* La barra di avanzamento e' proporzionale alla misura di adesso (anche senza dati nuovi). */
        if (n->indice == ip && n->percento > 0) {
            int po, pv, lb, ab;
            ph_dsk_imbottitura(n->forma, &po, &pv);
            gtk_widget_get_size_request(n->pieno, &lb, &ab);
            if (lb != (n->larghezza - 2 * po) * n->percento / 100)
                g_error("prova d'insieme: la barra non segue la misura (tipo %d)", (int)tipo);
            st_barre_viste++;
        }
    }
}

/* Un widget che non c'e': nessun menu e nessun popup aperto. */
static void st_prova_menu_inesistente(Stress *s)
{
    ph_shell_popup_chiudi(s->d->sh);
    ph_dsk_menu_widget(s->d, 0xFFFFFFu, 100, 100);
    if (ph_shell_popup_aperto(s->d->sh) != PH_POPUP_NESSUNO)
        g_error("prova d'insieme: menu aperto per un widget che non esiste");
}

/* Ridimensiona l'ultimo widget, lo rimuove, ne aggiunge uno: la voce nuova in coda al profilo ha
 * misura, stile e opzione a zero (la voce rimossa lasciava i suoi valori). */
static void st_prova_voce_nuova(Stress *s)
{
    PhCfgTelefono *p = st_profilo(s);
    size_t ultimo = s->d->widget->len;
    g_message("prova d'insieme: barre controllate finora: %d", st_barre_viste);
    if (p == NULL || ultimo == 0)
        return;
    PhDskWidget *w = st_widget(s, ultimo - 1);
    if (w->indice != p->n_widget - 1 || ph_dsk_widget_nome(w->tipo) == NULL)
        return;
    PhCfgWidgetTipo tipo = w->tipo;
    unsigned id = w->id;
    st_prova_maniglia(s, ultimo - 1, GDK_KEY_Right); /* una misura salvata */
    PhCfgWidget *c = &p->widget[p->n_widget - 1];
    c->stile = 1;
    c->opzione = 3;
    c->larghezza = c->larghezza > 0 ? c->larghezza : 300;
    c->altezza = c->altezza > 0 ? c->altezza : 200;
    ph_dsk_widget_rimuovi(s->d, id);
    size_t n = p->n_widget;
    ph_dsk_widget_aggiungi(s->d, tipo);
    if (p->n_widget != n + 1)
        g_error("prova d'insieme: widget non aggiunto");
    const PhCfgWidget *nuova = &p->widget[n];
    if (nuova->tipo != tipo || nuova->larghezza != 0 || nuova->altezza != 0 || nuova->stile != 0 ||
        nuova->opzione != 0 || nuova->dimensione != PH_CFG_WIDGET_MEDIO)
        g_error("prova d'insieme: la voce nuova conserva valori di un'altra (tipo %d)", (int)tipo);
}

/* Ogni tipo: costruzione in tutti gli stili e misure, eventi, cambi a caldo, rimozioni. */
static bool st_tipi(Stress *s)
{
    if (s->n >= G_N_ELEMENTS(ST_COPIONE))
        return true;
    PhCfgWidgetTipo tipo = ST_COPIONE[s->n].tipo;
    switch (s->k) {
    case 0:
        st_costruisci_tipo(s, tipo);
        s->k = 1;
        s->j = 0;
        break;
    case 1: /* un fotogramma: i widget si disegnano in ogni stile */
        if (st_fotogramma(s))
            s->k = 2;
        break;
    case 2: /* gli eventi del telefono */
        if (st_lista(s, ST_COPIONE[s->n].cmd)) {
            s->k = 3;
            s->j = 0;
        }
        break;
    case 3: { /* cambio di misura e di stile a caldo, un widget alla volta */
        PhDskWidget *w = st_widget(s, s->j);
        if (w == NULL) {
            s->k = 4;
            s->j = 0;
            break;
        }
        if (s->j == 0) {
            st_prova_menu_inesistente(s);
            st_prova_voce_nuova(s);
            w = st_widget(s, s->j);
            if (w == NULL)
                break;
        }
        /* Misura a caldo: la misura equivalente al livello successivo, scritta nel profilo
         * (come fa la maniglia), poi si ricostruisce. */
        PhCfgWidgetDim dim = (PhCfgWidgetDim)((w->dim + 1) % 3);
        PhCfgTelefono *pw = st_profilo(s);
        if (w->indice < pw->n_widget) {
            PhDskMisura m = ph_dsk_misura_equivalente(w->tipo, w->stile, dim);
            pw->widget[w->indice].larghezza = m.larghezza;
            pw->widget[w->indice].altezza = m.altezza;
            pw->widget[w->indice].dimensione = dim;
            ph_dsk_widget_ricostruisci(s->d);
        }
        st_prova_maniglia(s, s->j, GDK_KEY_Right);
        st_prova_maniglia(s, s->j, GDK_KEY_Left);
        st_prova_maniglia(s, s->j, GDK_KEY_Down);
        w = st_widget(s, s->j);
        if (w != NULL) {
            unsigned nuovo_stile = (w->stile + 1) % ph_dsk_stili_n(w->tipo);
            size_t ip = w->indice;
            PhDskMisura atteso = ph_dsk_misura_cambio_stile(w->tipo, w->stile, nuovo_stile,
                                                            (PhDskMisura){w->larghezza, w->altezza});
            bool cambia = nuovo_stile != w->stile;
            PhCfgWidgetTipo tipo = w->tipo;
            ph_dsk_widget_stile_imposta(s->d, w->id, nuovo_stile, w->opzione);
            /* Forma uguale: la misura si tiene (entro i limiti); forma diversa: si perde (0/0). */
            PhCfgTelefono *pc = st_profilo(s);
            if (cambia && pc != NULL && ip < pc->n_widget &&
                (pc->widget[ip].larghezza != atteso.larghezza || pc->widget[ip].altezza != atteso.altezza))
                g_error("prova d'insieme: misura dopo il cambio di stile sbagliata (tipo %d)", (int)tipo);
        }
        if (s->j % 4 == 3)
            s->k = 5; /* ogni tanto aspetta un fotogramma: si disegna dopo il cambio */
        else
            s->j++;
        break;
    }
    case 5:
        if (st_fotogramma(s)) {
            s->j++;
            s->k = 3;
        }
        break;
    case 4: { /* via i primi due uno a uno, poi tutti insieme */
        PhDskWidget *w = st_widget(s, 0);
        if (s->j < 2 && w != NULL) {
            ph_dsk_widget_rimuovi(s->d, w->id);
            s->j++;
            break;
        }
        PhCfgTelefono *p = st_profilo(s);
        memset(p->widget, 0, sizeof p->widget);
        p->n_widget = 0;
        ph_dsk_widget_ricostruisci(s->d);
        fprintf(stderr, "prova d'insieme: tipo %zu finito a %lld ms\n", s->n, (long long)(g_get_monotonic_time() / 1000 % 1000000));
        s->n++;
        s->k = 0;
        s->j = 0;
        break;
    }
    }
    return false;
}

/* Un widget di ogni tipo del catalogo, misura media, sul profilo corrente. */
static void st_tutti_i_tipi(Stress *s, PhCfgTelefono *p)
{
    memset(p->widget, 0, sizeof p->widget);
    p->n_widget = 0;
    for (size_t i = 0; i < G_N_ELEMENTS(ST_COPIONE) && p->n_widget < PH_CFG_MAX_WIDGET; i++) {
        PhCfgWidget *w = &p->widget[p->n_widget++];
        w->tipo = ST_COPIONE[i].tipo;
        w->stile = 0;
        w->dimensione = PH_CFG_WIDGET_MEDIO;
        w->destra = 16 + (int)(i % 4) * 90;
        w->alto = 16 + (int)(i / 4) * 70;
    }
    (void)s;
}

static GtkWidget *st_trova(Stress *s, const char *classe)
{
    return trova(s->radice, classe, 0);
}

/* C'e' un'etichetta con quella classe CSS e quel testo? */
static bool st_testo(Stress *s, const char *classe, const char *testo)
{
    for (int i = 0;; i++) {
        GtkWidget *w = trova(s->radice, classe, i);
        if (w == NULL)
            return false;
        if (GTK_IS_LABEL(w) && strcmp(gtk_label_get_text(GTK_LABEL(w)), testo) == 0)
            return true;
    }
}

static void st_chiudi_popup(Stress *s)
{
    ph_shell_popup_chiudi(s->sh);
}

/* Menu, dialoghi e ricerche, con tutti i tipi di widget presenti e il telefono che si perde e ritorna. */
static bool st_insieme(Stress *s)
{
    static const StCmd modi[] = {{"nd:calendario", -1}, {"err:sveglia", -1}, {"nd:batteria", -1},
                                 {"err:controlli", -1}, {"meteo-modo:3", PH_SRV_EV_METEO}, {"meteo-modo:4", PH_SRV_EV_METEO},
                                 {"meteo-modo:1", PH_SRV_EV_METEO}, {"meteo-modo:0", PH_SRV_EV_METEO},
                                 {"ok:calendario", -1}, {"ok:sveglia", -1}, {"ok:batteria", -1}, {"ok:controlli", -1},
                                 {NULL, -1}};
    PhSrvCollegamento c;
    switch (s->k) {
    case 0:
        st_tutti_i_tipi(s, st_profilo(s));
        ph_dsk_widget_ricostruisci(s->d);
        ph_dsk_menu_desktop(s->d, 200, 200);
        s->k = 1;
        break;
    case 1:
        if (!st_fotogramma(s))
            break;
        st_chiudi_popup(s);
        for (size_t i = 0; i < s->d->widget->len; i++) { /* il menu di ogni widget, aperto e chiuso */
            PhDskWidget *w = st_widget(s, i);
            ph_dsk_menu_widget(s->d, w->id, 300, 300);
            st_chiudi_popup(s);
        }
        ph_dsk_dialogo_widget_apri(s->d); /* «Aggiungi widget» */
        s->k = 2;
        break;
    case 2: {
        if (!st_fotogramma(s))
            break;
        GtkWidget *ag = st_trova(s, "ph-desktop-aggiungi");
        if (ag != NULL)
            g_signal_emit_by_name(ag, "clicked");
        st_chiudi_popup(s);
        /* «Località…» con la ricerca ancora in corso alla chiusura */
        ph_demo_finto_comando("ricerca-lenta:1");
        ph_dsk_luogo_apri(s->d);
        ph_demo_mostra(s->sh, "luogo-cerca:sample");
        st_chiudi_popup(s);
        ph_demo_finto_comando("ricerca-lenta:0");
        s->k = 3;
        break;
    }
    case 3: /* una ricerca che finisce e un luogo scelto */
        ph_dsk_luogo_apri(s->d);
        ph_demo_mostra(s->sh, "luogo-cerca:sample");
        s->k = 4;
        break;
    case 4:
        if (!st_finche(s, st_trova(s, "ph-desktop-luogo-riga") != NULL, 3000, "il risultato della ricerca"))
            break;
        ph_demo_mostra(s->sh, "clic:ph-desktop-luogo-riga:0");
        if (!st_profilo(s)->meteo_luogo_scelto)
            g_error("prova d'insieme: il luogo scelto non e' nel profilo");
        ph_demo_finto_comando("ricerca-errore:1"); /* e una che fallisce */
        ph_dsk_luogo_apri(s->d);
        ph_demo_mostra(s->sh, "luogo-cerca:sample");
        s->k = 5;
        break;
    case 5: /* l'errore della ricerca compare nella frase del dialogo */
        if (!st_finche(s, st_testo(s, "ph-desktop-nota", "Il servizio meteo non risponde."), 3000,
                       "l'errore della ricerca"))
            break;
        st_chiudi_popup(s);
        ph_demo_finto_comando("ricerca-errore:0");
        ph_demo_finto_comando("perdi:1"); /* telefono perso e ritrovato */
        s->k = 6;
        break;
    case 6: /* deve passare da PERSO ... */
        ph_srv_collegamento(s->d->srv, &c);
        if (!st_finche(s, c.stato == PH_SRV_PERSO, 3000, "il telefono perso"))
            break;
        s->k = 8;
        break;
    case 8: /* ... e tornare COLLEGATO */
        ph_srv_collegamento(s->d->srv, &c);
        if (!st_finche(s, c.stato == PH_SRV_COLLEGATO, 8000, "il telefono ritrovato"))
            break;
        s->k = 7;
        s->j = 0;
        break;
    case 7: /* dati mancanti e ritrovati per tutti */
        if (st_lista(s, modi))
            return true;
        break;
    }
    return false;
}

/* Un altro telefono: arriva un profilo nuovo, con widget diversi, mentre quelli vecchi sono vivi.
 * Due profili di fila; di ciascuno si aspetta l'evento PROFILO e si controlla che sia quello. */
static void st_pubblica_profilo(int giro)
{
    PhCfgTelefono *n = ph_cfg_telefono_nuovo();
    if (n == NULL)
        g_error("prova d'insieme: profilo nuovo non creato");
    memset(n->widget, 0, sizeof n->widget);
    n->n_widget = 0;
    static const PhCfgWidgetTipo t[2][3] = {{PH_CFG_WIDGET_METEO, PH_CFG_WIDGET_OROLOGIO, PH_CFG_WIDGET_BATTERIA},
                                            {PH_CFG_WIDGET_CALENDARIO_MESE, PH_CFG_WIDGET_METEO,
                                             PH_CFG_WIDGET_TRASFERIMENTI}};
    for (int i = 0; i < 3; i++) {
        PhCfgWidget *w = &n->widget[n->n_widget++];
        w->tipo = t[giro][i];
        w->dimensione = (PhCfgWidgetDim)i;
        w->stile = giro + i;
        w->destra = 16 + i * 120;
        w->alto = 16;
    }
    if (giro == 1) { /* questo ha anche una citta' scelta: il meteo la comunica ai servizi */
        n->meteo_luogo_scelto = true;
        g_strlcpy(n->meteo_luogo_nome, "Luogo finto", sizeof n->meteo_luogo_nome);
        n->meteo_luogo_lat_centesimi = 1000;
        n->meteo_luogo_lon_centesimi = 2000;
    }
    ph_servizi_pubblica_profilo(ph_finto_servizi(ph_demo_finto()), n);
}

static bool st_profilo_nuovo(Stress *s)
{
    switch (s->k) {
    case 0:
        st_pubblica_profilo(0);
        st_attendi(s, PH_SRV_EV_PROFILO, 3000); /* avvia l'attesa nello stesso istante: l'evento arriva dopo */
        s->k = 1;
        break;
    case 1:
        if (st_attendi(s, PH_SRV_EV_PROFILO, 3000)) {
            if (s->d->widget->len != 3)
                g_error("prova d'insieme: il primo profilo nuovo ha %u widget, attesi 3", s->d->widget->len);
            s->k = 2;
        }
        break;
    case 2:
        st_pubblica_profilo(1);
        st_attendi(s, PH_SRV_EV_PROFILO, 3000);
        s->k = 3;
        break;
    case 3:
        if (st_attendi(s, PH_SRV_EV_PROFILO, 3000)) {
            if (s->d->widget->len != 3 || !st_profilo(s)->meteo_luogo_scelto)
                g_error("prova d'insieme: il secondo profilo nuovo non e' quello atteso");
            return true;
        }
        break;
    }
    return false;
}

/* ---- agente B (passo 2b): batteria e sveglia ----
 * Clic: senza l'app nell'elenco del telefono non si apre nulla; con l'app si apre quella (anche col
 * pacchetto che il telefono finto da' con la sveglia). Gestori: dopo la rimozione del widget la sua
 * scheda (tenuta in vita apposta) non ha piu' nessun gestore di notify::width-request /
 * height-request (lo provano le verifiche con g_signal_has_handler_pending; ridimensionare la
 * scheda dopo, sotto ASan, e' una guardia in piu'). Forma: il tondo della sveglia resta un cerchio
 * a due misure (larghezza e altezza allocate uguali). Batteria: a ogni misura anello, stato e
 * gettoni stanno dentro il cerchio, e i gettoni visibili sono quelli che la logica decide. */
#include "logica_telefono.h"

static void ab_clic_e_gestori(Stress *s)
{
    st_tutti_i_tipi(s, st_profilo(s));
    ph_dsk_widget_ricostruisci(s->d);
    PhDskWidget *bat = NULL, *sve = NULL;
    for (size_t i = 0; i < s->d->widget->len; i++) {
        PhDskWidget *w = st_widget(s, i);
        if (w->tipo == PH_CFG_WIDGET_BATTERIA)
            bat = w;
        else if (w->tipo == PH_CFG_WIDGET_SVEGLIA)
            sve = w;
    }
    if (bat == NULL || sve == NULL)
        g_error("prova d'insieme: batteria o sveglia mancano dal copione");
    PhDskWidget *due[2] = {bat, sve};
    for (int i = 0; i < 2; i++) {
        const char *r = ph_dsk_wx_telefono_clic_prova(due[i], "it.inesistente.prova");
        if (r != NULL)
            g_error("prova d'insieme: il clic ha aperto un'app che il telefono non ha (%s)", r);
        r = ph_dsk_wx_telefono_clic_prova(due[i], "it.finto.orologio");
        if (r == NULL || strcmp(r, "it.finto.orologio") != 0)
            g_error("prova d'insieme: il clic non ha aperto l'app nota");
        r = ph_dsk_wx_telefono_clic_prova(due[i], "");
        if (r != NULL)
            g_error("prova d'insieme: il clic con un pacchetto vuoto ha aperto qualcosa");
    }
    /* Il finto non ha l'app Impostazioni: il clic vero sulla batteria non fa nulla. */
    if (ph_dsk_wx_telefono_clic_prova(bat, NULL) != NULL)
        g_error("prova d'insieme: la batteria ha aperto un'app che il finto non ha");

    guint sig = g_signal_lookup("notify", GTK_TYPE_WIDGET);
    GQuark q_l = g_quark_from_static_string("width-request"), q_a = g_quark_from_static_string("height-request");
    GtkWidget *schede[2] = {g_object_ref(bat->scheda), g_object_ref(sve->scheda)};
    for (int i = 0; i < 2; i++)
        if (!g_signal_has_handler_pending(schede[i], sig, q_l, TRUE) ||
            !g_signal_has_handler_pending(schede[i], sig, q_a, TRUE))
            g_error("prova d'insieme: il widget %d non ha i gestori della misura (la prova non proverebbe nulla)", i);
    PhCfgTelefono *p = st_profilo(s);
    memset(p->widget, 0, sizeof p->widget);
    p->n_widget = 0;
    ph_dsk_widget_ricostruisci(s->d); /* via tutti: `ferma` di ognuno */
    for (int i = 0; i < 2; i++) {
        if (g_signal_has_handler_pending(schede[i], sig, q_l, TRUE) ||
            g_signal_has_handler_pending(schede[i], sig, q_a, TRUE))
            g_error("prova d'insieme: il widget %d ha ancora i gestori della misura dopo la rimozione", i);
        gtk_widget_set_size_request(schede[i], 333 + i, 222 + i); /* guardia: ASan se qualcosa e' rimasto */
        g_object_unref(schede[i]);
    }
}

/* Tutti i tipi sul desktop, con la misura data al widget `tipo`; torna quel widget. */
static PhDskWidget *ab_misura(Stress *s, PhCfgWidgetTipo tipo, int larghezza, int altezza)
{
    PhCfgTelefono *p = st_profilo(s);
    st_tutti_i_tipi(s, p);
    for (size_t i = 0; i < p->n_widget; i++) {
        if (p->widget[i].tipo == tipo) {
            p->widget[i].larghezza = larghezza;
            p->widget[i].altezza = altezza;
        }
    }
    ph_dsk_widget_ricostruisci(s->d);
    for (size_t i = 0; i < s->d->widget->len; i++)
        if (st_widget(s, i)->tipo == tipo)
            return st_widget(s, i);
    g_error("prova d'insieme: widget %d non trovato", (int)tipo);
    return NULL;
}

/* Il rettangolo di `w` nelle coordinate della scheda; false se non si sa (non ancora disegnato). */
static bool ab_rettangolo(GtkWidget *w, GtkWidget *scheda, graphene_rect_t *r)
{
    return gtk_widget_get_width(w) > 0 && gtk_widget_get_height(w) > 0 && gtk_widget_compute_bounds(w, scheda, r);
}

/* Tutti e quattro gli angoli del rettangolo dentro il cerchio (centro e raggio), 1 px di tolleranza. */
static bool ab_nel_cerchio(const graphene_rect_t *r, double cx, double cy, double raggio)
{
    const double xs[2] = {r->origin.x, r->origin.x + r->size.width}, ys[2] = {r->origin.y, r->origin.y + r->size.height};
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            if (hypot(xs[i] - cx, ys[j] - cy) > raggio + 1.0)
                return false;
    return true;
}

static const int AB_SVE[][2] = {{200, 72}, {480, 128}};
static const int AB_BAT[] = {136, 176, 216, 256, 320};

static bool st_agente_b(Stress *s)
{
    switch (s->k) {
    case 0:
        ab_clic_e_gestori(s);
        s->n = 0;
        s->k = 1;
        break;
    case 1: /* sveglia a una misura */
        ph_demo_finto_comando("ok:sveglia");
        ph_finto_sveglia_assente(ph_demo_finto(), false); /* il copione l'aveva tolta: serve il suo pacchetto */
        ab_misura(s, PH_CFG_WIDGET_SVEGLIA, AB_SVE[s->n][0], AB_SVE[s->n][1]);
        s->k = 2;
        break;
    case 2: { /* il tondo e' un cerchio, della misura che la logica ha deciso */
        PhDskWidget *sve = NULL;
        for (size_t i = 0; i < s->d->widget->len; i++)
            if (st_widget(s, i)->tipo == PH_CFG_WIDGET_SVEGLIA)
                sve = st_widget(s, i);
        GtkWidget *t = st_trova(s, "ph-desktop-tel-tondo");
        if (!st_finche(s, sve != NULL && t != NULL && gtk_widget_get_width(t) > 0 && gtk_widget_get_height(t) > 0, 3000,
                       "il tondo della sveglia disegnato"))
            break;
        int lato = ph_dsk_sve_disposizione(sve->larghezza, sve->altezza, true).tondo_lato;
        if (gtk_widget_get_width(t) != gtk_widget_get_height(t) || gtk_widget_get_width(t) != lato || lato < 24)
            g_error("prova d'insieme: il tondo della sveglia %dx%d e' %dx%d, atteso un cerchio di %d", sve->larghezza,
                    sve->altezza, gtk_widget_get_width(t), gtk_widget_get_height(t), lato);
        /* L'icona sta al centro esatto del tondo (1 px di tolleranza). */
        GtkWidget *icona = gtk_widget_get_first_child(t);
        graphene_rect_t ri;
        if (icona == NULL || !gtk_widget_compute_bounds(icona, t, &ri) ||
            fabs(ri.origin.x + ri.size.width / 2.0 - gtk_widget_get_width(t) / 2.0) > 1.0 ||
            fabs(ri.origin.y + ri.size.height / 2.0 - gtk_widget_get_height(t) / 2.0) > 1.0)
            g_error("prova d'insieme: l'icona della sveglia %dx%d non e' al centro del tondo (x %.1f y %.1f l %.1f a %.1f, tondo %dx%d)",
                    sve->larghezza, sve->altezza, icona != NULL ? ri.origin.x : -1.0, icona != NULL ? ri.origin.y : -1.0,
                    icona != NULL ? ri.size.width : -1.0, icona != NULL ? ri.size.height : -1.0, gtk_widget_get_width(t),
                    gtk_widget_get_height(t));
        if (++s->n < G_N_ELEMENTS(AB_SVE))
            s->k = 1;
        else
            s->k = 3;
        break;
    }
    case 3: { /* col pacchetto che il finto da' con la sveglia (arriva con i dati: attesa con scadenza) */
        PhDskWidget *sve = NULL;
        for (size_t i = 0; i < s->d->widget->len; i++)
            if (st_widget(s, i)->tipo == PH_CFG_WIDGET_SVEGLIA)
                sve = st_widget(s, i);
        const char *r = sve != NULL ? ph_dsk_wx_telefono_clic_prova(sve, NULL) : NULL;
        if (!st_finche(s, r != NULL, 3000, "il clic della sveglia con il pacchetto del telefono"))
            break;
        if (strcmp(r, "it.finto.orologio") != 0)
            g_error("prova d'insieme: la sveglia ha aperto %s invece dell'app data dal telefono", r);
        s->n = 0;
        s->k = 4;
        break;
    }
    case 4: /* batteria a una misura */
        ph_demo_finto_comando("ok:batteria");
        ab_misura(s, PH_CFG_WIDGET_BATTERIA, AB_BAT[s->n], AB_BAT[s->n]);
        s->k = 5;
        break;
    case 5: { /* niente esce dal cerchio, e i gettoni sono quelli decisi dalla logica */
        PhDskWidget *bat = NULL;
        for (size_t i = 0; i < s->d->widget->len; i++)
            if (st_widget(s, i)->tipo == PH_CFG_WIDGET_BATTERIA)
                bat = st_widget(s, i);
        GtkWidget *stato = st_trova(s, "ph-desktop-tel-stato");
        GtkWidget *anello = bat != NULL ? gtk_widget_get_first_child(gtk_widget_get_parent(stato != NULL ? stato : bat->scheda)) : NULL;
        bool pronto = bat != NULL && stato != NULL && anello != NULL && gtk_label_get_text(GTK_LABEL(stato))[0] != '\0' &&
                      gtk_widget_get_width(stato) > 0 && gtk_widget_get_width(anello) > 0;
        if (!st_finche(s, pronto, 3000, "la batteria disegnata con i dati"))
            break;
        double cx = bat->larghezza / 2.0, cy = bat->altezza / 2.0, raggio = (bat->larghezza < bat->altezza ? bat->larghezza : bat->altezza) / 2.0;
        const bool tutti[PH_DSK_BAT_G_N] = {true, true, true}; /* il finto da' temperatura, salute e tensione */
        PhDskBatDisp dp = ph_dsk_bat_disposizione(bat->larghezza, bat->altezza, tutti);
        int attesi = dp.gettone[0] + dp.gettone[1] + dp.gettone[2], visti = 0;
        graphene_rect_t r;
        /* Tutto misurato nello stesso riferimento (il contenitore delle schede): il cerchio e' quello della
         * scheda come GTK lo ha davvero disposto, non la misura chiesta. */
        GtkWidget *rif = s->d->libero;
        graphene_rect_t rs;
        if (!ab_rettangolo(bat->scheda, rif, &rs))
            g_error("prova d'insieme: batteria %d: scheda non misurabile", bat->larghezza);
        cx = rs.origin.x + rs.size.width / 2.0;
        cy = rs.origin.y + rs.size.height / 2.0;
        raggio = (rs.size.width < rs.size.height ? rs.size.width : rs.size.height) / 2.0;
        if (gtk_widget_get_width(anello) != dp.lato_anello || gtk_widget_get_height(anello) != dp.lato_anello)
            g_error("prova d'insieme: l'anello della batteria %d e' %dx%d, deciso %d", bat->larghezza,
                    gtk_widget_get_width(anello), gtk_widget_get_height(anello), dp.lato_anello);
        if (!ab_rettangolo(anello, rif, &r) || !ab_nel_cerchio(&r, cx, cy, raggio))
            g_error("prova d'insieme: l'anello della batteria %d esce dal cerchio (x %.0f y %.0f l %.0f a %.0f, scheda %dx%d)",
                    bat->larghezza, r.origin.x, r.origin.y, r.size.width, r.size.height, bat->larghezza, bat->altezza);
        if (dp.stato && (!ab_rettangolo(stato, rif, &r) || !ab_nel_cerchio(&r, cx, cy, raggio)))
            g_error("prova d'insieme: lo stato della batteria %d esce dal cerchio", bat->larghezza);
        for (int i = 0;; i++) {
            GtkWidget *g = trova(s->radice, "ph-desktop-tel-gettone", i);
            if (g == NULL)
                break;
            if (!gtk_widget_is_visible(g))
                continue;
            visti++;
            if (!ab_rettangolo(g, rif, &r) || !ab_nel_cerchio(&r, cx, cy, raggio))
                g_error("prova d'insieme: un gettone della batteria %d esce dal cerchio", bat->larghezza);
        }
        if (visti != attesi)
            g_error("prova d'insieme: batteria %d: %d gettoni visibili, la logica ne vuole %d", bat->larghezza, visti,
                    attesi);
        if (++s->n < G_N_ELEMENTS(AB_BAT)) {
            s->k = 4;
            break;
        }
        return true;
    }
    }
    return false;
}
/* ---- fine agente B ---- */

/* Fine: un widget di ogni tipo, un dialogo con una ricerca in volo, il telefono che si perde, e la
 * finestra si chiude. */
static bool st_chiusura(Stress *s)
{
    st_tutti_i_tipi(s, st_profilo(s));
    ph_dsk_widget_ricostruisci(s->d);
    ph_demo_finto_comando("ricerca-lenta:1");
    ph_dsk_luogo_apri(s->d);
    ph_demo_mostra(s->sh, "luogo-cerca:sample");
    /* Prima di chiudere deve esserci davvero una ricerca in volo e non annullata (altrimenti il
     * controllo di dopo, «zero non annullate», passerebbe a vuoto). */
    int in_volo = ph_finto_meteo_ricerche_non_annullate(ph_demo_finto());
    if (in_volo < 1)
        g_error("prova d'insieme: nessuna ricerca in volo prima della chiusura (%d)", in_volo);
    /* La prova della barra deve aver controllato almeno una barra: se nessuna e' stata vista il
     * conto non prova niente. */
    if (st_barre_viste == 0)
        g_error("prova d'insieme: nessuna barra di avanzamento e' stata controllata");
    ph_demo_finto_comando("perdi:3");
    fprintf(stderr, "prova d'insieme: %u attese deboli scadute\n", s->scadute);
    stress_finito = true;
    GtkRoot *r = gtk_widget_get_root(s->radice);
    if (r != NULL)
        gtk_window_close(GTK_WINDOW(r));
    return true;
}

static bool (*const ST_PASSI[])(Stress *) = {st_collegato, st_tipi, st_insieme, st_profilo_nuovo,
                                                     st_agente_b /* agente B */, st_chiusura};

static gboolean stress_tick(gpointer dati)
{
    Stress *s = dati;
    if (s->passo >= G_N_ELEMENTS(ST_PASSI))
        return G_SOURCE_REMOVE;
    if (s->radice == NULL)
        return G_SOURCE_REMOVE; /* la finestra e' gia' stata distrutta */
    if (ST_PASSI[s->passo](s)) {
        fprintf(stderr, "prova d'insieme: passo %zu finito\n", s->passo);
        s->passo++;
        s->n = s->k = s->j = s->l = 0;
        s->scadenza = 0;
        s->in_fotogramma = false;
    }
    return s->passo >= G_N_ELEMENTS(ST_PASSI) ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

/* Alla distruzione della radice (dopo il gestore del desktop, collegato prima): il dialogo
 * «Località…» e' stato liberato, quindi la ricerca lenta ancora in volo deve risultare annullata. Il
 * finto e' ancora vivo (lo libera ph_finto_demo dopo ph_shell_libera). */
static void stress_radice_distrutta(GtkWidget *w, gpointer dati)
{
    (void)w;
    (void)dati;
    if (!stress_finito)
        return;
    int non_annullate = ph_finto_meteo_ricerche_non_annullate(ph_demo_finto());
    if (non_annullate != 0)
        g_error("prova d'insieme: %d ricerche di luoghi in volo non annullate alla chiusura", non_annullate);
    fprintf(stderr, "prova d'insieme: la ricerca in volo alla chiusura e' stata annullata\n");
    stress_annullo_verificato = true;
}

static void stress_esci(void)
{
    if (stress_iniziato && !stress_finito) {
        fprintf(stderr, "prova d'insieme: la sequenza non e' arrivata in fondo\n");
        _exit(1);
    }
    if (stress_iniziato && !stress_annullo_verificato) {
        fprintf(stderr, "prova d'insieme: l'annullamento della ricerca alla chiusura non e' stato verificato\n");
        _exit(1);
    }
}

static void stress_avvia(PhShell *sh)
{
    GtkWidget *radice = ph_shell_radice(sh);
    PhDesktop *d = g_object_get_data(G_OBJECT(radice), "ph-desktop");
    if (d == NULL || stress_iniziato)
        return;
    Stress *s = g_new0(Stress, 1); /* vive fino alla fine del processo: i suoi ascolti non si staccano */
    s->sh = sh;
    s->d = d;
    s->radice = radice;
    g_object_add_weak_pointer(G_OBJECT(radice), (gpointer *)&s->radice);
    for (int e = 0; e < PH_SRV_EV_NUMERO; e++)
        s->ascolti[e] = ph_srv_ascolta(d->srv, (PhSrvEvento)e, stress_evento, s);
    g_signal_connect(radice, "destroy", G_CALLBACK(stress_radice_distrutta), NULL);
    stress_iniziato = true;
    atexit(stress_esci);
    g_idle_add(stress_tick, s);
}

/* ---- agente A: widget Memoria, Notifiche, Calendario del giorno e Stato per misura vera ---------------
 * "vecchi": la prova. Mette i quattro widget in misure libere, aspetta i dati del telefono finto e, a
 * ogni misura, controlla che cio' che si vede sia quello che la logica pura decide, che i clic aprano
 * l'app giusta (o niente) e che i ritocchi di componenti.c reggano. Le misure sono: grandi, quelle da
 * cambiare con la maniglia (si passa dai tasti freccia della maniglia vera), la fascia di larghezza
 * in cui la legenda della memoria puo' stare in una o in due colonne, le minime.
 * "vecchi:grandi" e "vecchi:minime": solo per le foto, i quattro widget fermi a quella misura, senza
 * controlli ne' uscita. Se la prova non arriva in fondo il processo esce 1. */
static char vecchi_aperta[256];
static bool vecchi_iniziato, vecchi_finito;

static void vecchi_apri(void *io, const char *pacchetto)
{
    (void)io;
    g_strlcpy(vecchi_aperta, pacchetto, sizeof vecchi_aperta);
}

static size_t vecchi_elenco(void *io, PhShellFinestra *out, size_t max)
{
    (void)io;
    (void)out;
    (void)max;
    return 0;
}

static void vecchi_clic_barra(void *io, unsigned id)
{
    (void)io;
    (void)id;
}

static void vecchi_specchio(void *io, bool mostra)
{
    (void)io;
    (void)mostra;
}

static bool vecchi_specchio_aperto(void *io)
{
    (void)io;
    return false;
}

static void vecchi_esci(void)
{
    if (vecchi_iniziato && !vecchi_finito) {
        fprintf(stderr, "vecchi: la verifica non e' arrivata in fondo\n");
        _exit(1);
    }
}

/* Le misure: notifiche, memoria, calendario del giorno, stato (in quest'ordine). */
typedef struct {
    PhCfgWidgetTipo tipo;
    int destra, alto, l, a;
} VecchiMis;
static const VecchiMis VM_GRANDI[4] = {{PH_CFG_WIDGET_NOTIFICHE, 16, 16, 320, 240},
                                       {PH_CFG_WIDGET_MEMORIA, 352, 16, 320, 208},
                                       {PH_CFG_WIDGET_CALENDARIO, 16, 280, 320, 210},
                                       {PH_CFG_WIDGET_STATO, 352, 240, 320, 164}};
static const VecchiMis VM_MANIGLIA[4] = {{PH_CFG_WIDGET_NOTIFICHE, 16, 16, 250, 148},
                                         {PH_CFG_WIDGET_MEMORIA, 352, 16, 200, 120},
                                         {PH_CFG_WIDGET_CALENDARIO, 16, 280, 250, 150},
                                         {PH_CFG_WIDGET_STATO, 352, 250, 250, 124}};
/* La fascia dove le colonne della legenda possono divergere dalla stima (280-305 di larghezza). */
static const VecchiMis VM_BANDA[4] = {{PH_CFG_WIDGET_NOTIFICHE, 16, 16, 320, 240},
                                      {PH_CFG_WIDGET_MEMORIA, 352, 16, 290, 208},
                                      {PH_CFG_WIDGET_CALENDARIO, 16, 280, 320, 210},
                                      {PH_CFG_WIDGET_STATO, 352, 240, 320, 164}};
static const VecchiMis VM_MINIME[4] = {{PH_CFG_WIDGET_NOTIFICHE, 16, 16, 200, 104},
                                       {PH_CFG_WIDGET_MEMORIA, 352, 16, 200, 120},
                                       {PH_CFG_WIDGET_CALENDARIO, 16, 280, 200, 110},
                                       {PH_CFG_WIDGET_STATO, 352, 250, 200, 100}};

static void vecchi_imposta(PhDesktop *d, PhCfgTelefono *p, const VecchiMis *m)
{
    memset(p->widget, 0, sizeof p->widget);
    for (int i = 0; i < 4; i++) {
        p->widget[i].tipo = m[i].tipo;
        p->widget[i].destra = m[i].destra;
        p->widget[i].alto = m[i].alto;
        p->widget[i].larghezza = m[i].l;
        p->widget[i].altezza = m[i].a;
        p->widget[i].dimensione = PH_CFG_WIDGET_MEDIO;
    }
    p->n_widget = 4;
    ph_dsk_widget_ricostruisci(d);
}

static void vecchi_foto(PhDesktop *d, PhCfgTelefono *p, const char *quale)
{
    ph_dsk_orologio_ora_fissa(VECCHI_ORA); /* il calendario mostra impegni, non lo stato vuoto */
    if (strcmp(quale, "grandi") == 0)
        vecchi_imposta(d, p, VM_GRANDI);
    else if (strcmp(quale, "minime") == 0)
        vecchi_imposta(d, p, VM_MINIME);
}

static int vecchi_conta(GtkWidget *radice, const char *classe)
{
    int n = 0;
    while (trova(radice, classe, n) != NULL)
        n++;
    return n;
}

static int vecchi_conta_visibili(GtkWidget *radice, const char *classe)
{
    int n = 0;
    for (int i = 0;; i++) {
        GtkWidget *x = trova(radice, classe, i);
        if (x == NULL)
            break;
        n += gtk_widget_is_visible(x) ? 1 : 0; /* un antenato nascosto lo rende invisibile */
    }
    return n;
}

static PhDskWidget *vecchi_widget(PhDesktop *d, PhCfgWidgetTipo tipo)
{
    for (size_t i = 0; i < d->widget->len; i++) {
        PhDskWidget *w = g_ptr_array_index(d->widget, i);
        if (w->tipo == tipo)
            return w;
    }
    g_error("vecchi: widget di tipo %d non trovato", (int)tipo);
}

/* Il primo controllo del tipo dato (un gesto del clic, un controllo dei tasti) del widget. */
static GObject *vecchi_controllo(GtkWidget *w, GType tipo)
{
    GListModel *m = gtk_widget_observe_controllers(w);
    GObject *trovato = NULL;
    for (guint i = 0; i < g_list_model_get_n_items(m) && trovato == NULL; i++) {
        GObject *c = g_list_model_get_item(m, i);
        if (g_type_is_a(G_OBJECT_TYPE(c), tipo))
            trovato = c;
        g_object_unref(c); /* il widget lo tiene vivo */
    }
    g_object_unref(m);
    return trovato;
}

/* Premi una freccia sulla maniglia VERA del widget (come da tastiera): passa da ridim_tasto, cioe'
 * dallo stesso percorso del trascinamento (misura, geometria, contenuto, profilo). Il widget puo'
 * essere ricostruito a ogni passo: si ritrova ogni volta. */
static void vecchi_maniglia(PhDesktop *d, PhCfgWidgetTipo tipo, guint tasto, int volte)
{
    for (int i = 0; i < volte; i++) {
        PhDskWidget *w = vecchi_widget(d, tipo);
        GObject *k = vecchi_controllo(w->maniglia, GTK_TYPE_EVENT_CONTROLLER_KEY);
        if (k == NULL)
            g_error("vecchi: la maniglia non ha il controllo dei tasti");
        gboolean preso = FALSE;
        g_signal_emit_by_name(k, "key-pressed", tasto, 0u, (GdkModifierType)0, &preso);
        if (!preso)
            g_error("vecchi: la maniglia non ha preso la freccia");
    }
}

/* Quante colonne userebbe la legenda della memoria (-1 se la barra non c'e'). */
static int vecchi_colonne(PhDskWidget *w)
{
    GtkWidget *leg = trova(w->corpo, "ph-desktop-legenda", 0);
    if (leg == NULL)
        return -1;
    int ul, ua;
    ph_dsk_vecchi_utile(w->larghezza, w->altezza, &ul, &ua);
    return ph_dsk_fette_legenda_colonne(gtk_widget_get_parent(leg), ul);
}

static size_t vecchi_fette_attese(PhDesktop *d)
{
    PhDskMemFetta fe[PH_DSK_MEM_N];
    uint64_t libera = d->memoria_libera > d->memoria_totale ? d->memoria_totale : d->memoria_libera;
    return ph_dsk_memoria_fette(d->dettaglio_noto ? &d->dettaglio : NULL, d->memoria_totale - libera, fe);
}

/* La legenda della memoria sta dentro la scheda (non tagliata sotto o a destra). Torna 0 se la
 * misura vera non e' ancora arrivata (il layout arriva con il fotogramma), 1 se va bene; se e'
 * fuori, la prova cade. */
static int vecchi_geometria(PhDesktop *d)
{
    PhDskWidget *w = vecchi_widget(d, PH_CFG_WIDGET_MEMORIA);
    int sh = gtk_widget_get_height(w->scheda), sl = gtk_widget_get_width(w->scheda);
    if (sh <= 0 || sl <= 0)
        return 0;
    /* Calendario del giorno: la colonna degli impegni comincia subito dopo quella del giorno (circa
     * 14 px), non attaccata al bordo destro con un vuoto in mezzo. */
    PhDskWidget *cw = vecchi_widget(d, PH_CFG_WIDGET_CALENDARIO);
    GtkWidget *colg = trova(cw->corpo, "ph-desktop-giorno-colonna", 0), *imp = trova(cw->corpo, "ph-desktop-giorno-impegni", 0);
    if (colg != NULL) {
        if (imp == NULL)
            g_error("vecchi: calendario: giorno a sinistra senza la colonna degli impegni");
        if (gtk_widget_get_width(colg) <= 0 || gtk_widget_get_width(imp) <= 0)
            return 0;
        graphene_rect_t bc, bi;
        if (!gtk_widget_compute_bounds(colg, cw->corpo, &bc) || !gtk_widget_compute_bounds(imp, cw->corpo, &bi))
            return 0;
        float vuoto = bi.origin.x - (bc.origin.x + bc.size.width);
        if (vuoto < 0 || vuoto >= 24)
            g_error("vecchi: calendario %dx%d: tra la colonna del giorno e quella degli impegni ci sono %.0f px "
                    "(attesi meno di 24: il giorno finisce a %.0f, gli impegni cominciano a %.0f)",
                    cw->larghezza, cw->altezza, vuoto, bc.origin.x + bc.size.width, bi.origin.x);
    }
    GtkWidget *leg = trova(w->corpo, "ph-desktop-legenda", 0);
    if (leg == NULL || !gtk_widget_is_visible(leg))
        return 1;
    if (gtk_widget_get_height(leg) <= 0)
        return 0;
    graphene_rect_t b;
    if (!gtk_widget_compute_bounds(leg, w->scheda, &b))
        return 0;
    int pad_o, pad_v;
    ph_dsk_imbottitura(w->forma, &pad_o, &pad_v);
    if (b.origin.y + b.size.height > sh + pad_v + 1 || b.origin.x + b.size.width > sl + pad_o + 1 || b.origin.x < 0)
        g_error("vecchi: la legenda della memoria esce dalla scheda: sotto a %.0f su %d, a destra a %.0f su %d "
                "(%dx%d, colonne %d)",
                b.origin.y + b.size.height, sh + pad_v, b.origin.x + b.size.width, sl + pad_o, w->larghezza,
                w->altezza, vecchi_colonne(w));
    return 1;
}

static void vecchi_controlla(PhDesktop *d, const char *fase)
{
    /* Notifiche: tante righe quante la logica dice per la misura, ognuna un pulsante verso la sua app. */
    PhDskWidget *w = vecchi_widget(d, PH_CFG_WIDGET_NOTIFICHE);
    size_t n = 0;
    const PhSrvNotifica *tutte = ph_srv_notifiche(d->srv, &n, NULL);
    size_t visibili = 0;
    const char *prima_app = NULL;
    for (size_t i = 0; i < n; i++)
        if (!ph_shell_notifica_nascosta(d->sh, tutte[i].chiave, tutte[i].ora_ms)) {
            if (prima_app == NULL)
                prima_app = tutte[i].pacchetto;
            visibili++;
        }
    size_t max = ph_dsk_notifiche_righe_max(w->larghezza, w->altezza);
    size_t attese = visibili < max ? visibili : max;
    int righe = vecchi_conta(w->corpo, "ph-desktop-riga-elenco");
    if (attese == 0 || (size_t)righe != attese)
        g_error("vecchi (%s): notifiche: %d righe, attese %zu (%zu visibili, ne stanno %zu a %dx%d)", fase, righe,
                attese, visibili, max, w->larghezza, w->altezza);
    vecchi_aperta[0] = '\0';
    g_signal_emit_by_name(trova(w->corpo, "ph-desktop-riga-elenco", 0), "clicked");
    if (prima_app == NULL || strcmp(vecchi_aperta, prima_app) != 0)
        g_error("vecchi (%s): il clic sulla prima notifica non ha aperto la sua app", fase);

    /* Memoria: numero sempre; barra e legenda secondo la logica (con le colonne vere del componente). */
    w = vecchi_widget(d, PH_CFG_WIDGET_MEMORIA);
    if (!d->memoria_nota)
        g_error("vecchi (%s): memoria non nota", fase);
    size_t nf = vecchi_fette_attese(d);
    int colonne = vecchi_colonne(w);
    PhDskMemoriaDisp disp = ph_dsk_memoria_disp(w->larghezza, w->altezza, nf, colonne);
    if (disp.barra != (colonne >= 0))
        g_error("vecchi (%s): memoria: barra %d ma legenda %s", fase, disp.barra, colonne >= 0 ? "presente" : "assente");
    int voci = vecchi_conta_visibili(w->corpo, "ph-desktop-legenda-nome");
    if (voci != (disp.barra && disp.legenda ? (int)nf : 0))
        g_error("vecchi (%s): memoria: %d voci visibili in legenda, attese %d (barra %d, legenda %d, fette %zu)",
                fase, voci, disp.barra && disp.legenda ? (int)nf : 0, disp.barra, disp.legenda, nf);
    if (!gtk_widget_get_visible(w->corpo))
        g_error("vecchi (%s): memoria: il corpo non si vede", fase);
    /* La dicitura «libero su N GB» sta sotto il numero (un'etichetta a parte) o accanto (disegnata). */
    if (!disp.nota_accanto != (vecchi_conta(w->corpo, "ph-desktop-w-sotto-nom") == 1))
        g_error("vecchi (%s): memoria: dicitura %s ma l'etichetta sotto %s", fase,
                disp.nota_accanto ? "accanto" : "sotto", vecchi_conta(w->corpo, "ph-desktop-w-sotto-nom") == 1 ? "c'e'" : "manca");

    /* Calendario: impegni in numero giusto, «ora» solo su quelli in corso, pulsanti verso l'app nota. */
    w = vecchi_widget(d, PH_CFG_WIDGET_CALENDARIO);
    if (!d->eventi_noti || d->app_calendario == NULL)
        g_error("vecchi (%s): calendario: dati o app non noti", fase);
    PhDskEvento ev[PH_SRV_CALENDARIO_MAX];
    for (size_t i = 0; i < d->n_eventi; i++)
        ev[i] = (PhDskEvento){d->eventi[i].titolo, d->eventi[i].inizio_ms, d->eventi[i].fine_ms,
                              d->eventi[i].tutto_il_giorno};
    /* L'ora e' quella congelata (VECCHI_ORA, le 13:30): gli impegni del finto sono a orari fissi della
     * giornata, quindi a qualunque ora vera si hanno SEMPRE gli stessi: «Lunch» (13:00-14:00) in corso,
     * poi Client call, Gym e Dinner. La prova non dipende dall'orologio. */
    int scarto = 0;
    uint64_t ora_ms = ph_dsk_widget_adesso_ms(&scarto);
    PhDskCalDisp cd = ph_dsk_calendario_disp(w->larghezza, w->altezza);
    size_t idx[PH_SRV_CALENDARIO_MAX];
    size_t restano = ph_dsk_eventi_di_oggi(ev, d->n_eventi, ora_ms, scarto, PH_SRV_CALENDARIO_MAX, idx);
    size_t k = ph_dsk_eventi_di_oggi(ev, d->n_eventi, ora_ms, scarto,
                                     cd.max_impegni < PH_SRV_CALENDARIO_MAX ? cd.max_impegni : PH_SRV_CALENDARIO_MAX, idx);
    size_t attesi_k = cd.max_impegni < 4 ? cd.max_impegni : 4;
    if (restano != 4 || k != attesi_k)
        g_error("vecchi (%s): calendario: alle 13:30 il finto deve dare 4 impegni (dati %zu) e se ne vedono %zu "
                "(attesi %zu): la prova non esercita cio' che deve",
                fase, restano, k, attesi_k);
    int impegni = vecchi_conta(w->corpo, "ph-desktop-giorno-impegno");
    int in_corso = 0;
    for (size_t i = 0; i < k; i++)
        in_corso += ph_dsk_impegno_in_corso(&ev[idx[i]], ora_ms) ? 1 : 0;
    if (in_corso != 1)
        g_error("vecchi (%s): calendario: attesi 1 impegno in corso, trovati %d", fase, in_corso);
    if ((size_t)impegni != k || vecchi_conta(w->corpo, "ph-desktop-giorno-impegno-ora") != in_corso)
        g_error("vecchi (%s): calendario: %d impegni (attesi %zu), %d «ora» (attesi %d)", fase, impegni, k,
                vecchi_conta(w->corpo, "ph-desktop-giorno-impegno-ora"), in_corso);
    GtkWidget *primo = trova(w->corpo, "ph-desktop-giorno-impegno", 0);
    if (primo == NULL || !GTK_IS_BUTTON(primo))
        g_error("vecchi (%s): calendario: con l'app nota l'impegno deve essere un pulsante", fase);
    vecchi_aperta[0] = '\0';
    g_signal_emit_by_name(primo, "clicked");
    if (strcmp(vecchi_aperta, d->app_calendario) != 0)
        g_error("vecchi (%s): calendario: il clic non ha aperto l'app del calendario", fase);
    if (cd.colonna_giorno != (vecchi_conta(w->corpo, "ph-desktop-w-sotto-nom") > 0) ||
        cd.colonna_giorno == gtk_widget_get_visible(w->titolo))
        g_error("vecchi (%s): calendario: giorno a sinistra %d ma titolo visibile %d", fase, cd.colonna_giorno,
                gtk_widget_get_visible(w->titolo));

    /* Stato: ESATTAMENTE le righe che ci stanno nell'altezza (o tutte quelle che il telefono da'). */
    w = vecchi_widget(d, PH_CFG_WIDGET_STATO);
    PhSrvTelefono t;
    PhSrvCollegamento c;
    ph_srv_telefono(d->srv, &t);
    ph_srv_collegamento(d->srv, &c);
    PhDskVista tutte_le_righe;
    ph_dsk_vista_stato(PH_CFG_WIDGET_GRANDE, &t, &c, &tutte_le_righe);
    size_t attese_stato = tutte_le_righe.n_righe < ph_dsk_stato_righe(w->larghezza, w->altezza)
                              ? tutte_le_righe.n_righe
                              : ph_dsk_stato_righe(w->larghezza, w->altezza);
    size_t visibili_stato = 0;
    for (size_t i = 0; i < PH_DSK_RIGHE_MAX; i++)
        visibili_stato += gtk_widget_get_visible(w->riga[i]) ? 1 : 0;
    if (visibili_stato != attese_stato)
        g_error("vecchi (%s): stato: %zu righe visibili, attese %zu (a %dx%d)", fase, visibili_stato, attese_stato,
                w->larghezza, w->altezza);
}

/* Il clic della memoria: apre Impostazioni solo se il telefono ce l'ha. Il telefono finto non ha
 * com.android.settings: di norma il clic non apre niente; con il pacchetto di prova (l'app finta
 * delle impostazioni) apre quella e il cursore diventa una mano. */
static void vecchi_impostazioni(PhDesktop *d)
{
    PhDskWidget *w = vecchi_widget(d, PH_CFG_WIDGET_MEMORIA);
    GtkGesture *cl = GTK_GESTURE(vecchi_controllo(w->corpo, GTK_TYPE_GESTURE_CLICK));
    if (cl == NULL)
        g_error("vecchi: memoria: nessun controllo del clic");
    vecchi_aperta[0] = '\0';
    g_signal_emit_by_name(cl, "released", 1, 5.0, 5.0);
    if (vecchi_aperta[0] != '\0')
        g_error("vecchi: memoria: Impostazioni non c'e' sul telefono ma il clic ha aperto %s", vecchi_aperta);
    if (gtk_widget_get_cursor(w->corpo) != NULL)
        g_error("vecchi: memoria: il cursore e' una mano senza nessuna app da aprire");
    ph_dsk_impostazioni_pacchetto_prova("it.finto.impostazioni");
    ph_dsk_widget_aggiorna_tutti(d);
    w = vecchi_widget(d, PH_CFG_WIDGET_MEMORIA);
    cl = GTK_GESTURE(vecchi_controllo(w->corpo, GTK_TYPE_GESTURE_CLICK));
    g_signal_emit_by_name(cl, "released", 1, 5.0, 5.0);
    if (strcmp(vecchi_aperta, "it.finto.impostazioni") != 0)
        g_error("vecchi: memoria: il clic non ha aperto Impostazioni (aperto \"%s\")", vecchi_aperta);
    if (gtk_widget_get_cursor(w->corpo) == NULL)
        g_error("vecchi: memoria: con Impostazioni il cursore dovrebbe essere una mano");
    ph_dsk_impostazioni_pacchetto_prova(NULL);
    ph_dsk_widget_aggiorna_tutti(d);
}

/* Senza l'app del calendario (non nota) gli impegni si vedono ma non sono pulsanti e non prendono i
 * clic: il trascinamento del widget passa. */
static void vecchi_senza_app_calendario(PhDesktop *d)
{
    g_clear_pointer(&d->app_calendario, g_free);
    ph_dsk_widget_aggiorna_tutti(d);
    PhDskWidget *w = vecchi_widget(d, PH_CFG_WIDGET_CALENDARIO);
    GtkWidget *primo = trova(w->corpo, "ph-desktop-giorno-impegno", 0);
    if (primo != NULL && (GTK_IS_BUTTON(primo) || gtk_widget_get_can_target(primo)))
        g_error("vecchi: calendario: senza l'app nota l'impegno e' ancora un pulsante o prende i clic");
}

static bool vecchi_ha_tinta(GtkWidget *w)
{
    for (int i = 0; i < PH_DSK_ACC_N; i++) {
        char classe[48];
        snprintf(classe, sizeof classe, "ph-desktop-tinta-%s", ph_dsk_accento_nome(i));
        if (gtk_widget_has_css_class(w, classe))
            return true;
    }
    return false;
}

/* Il minimo della riga d'elenco con l'ora visibile non conta solo il titolo: deve far stare il
 * sottotitolo e l'ora insieme. Con l'icona vera nessun fondo tinto dietro (e tornato quando l'icona
 * si toglie). E il layout manager non guarda piu' figli liberati dopo la distruzione della scatola
 * (sotto ASan, senza il ritocco, qui si legge memoria liberata). */
static void vecchi_riga(void)
{
    GtkWidget *r = ph_dsk_riga_nuova(NULL, NULL);
    g_object_ref_sink(r);
    ph_dsk_riga_imposta(r, NULL, PH_DSK_ACC_TEMA, true, "Un titolo piuttosto lungo", "Sotto", "15:52");
    GtkWidget *ora = trova(r, "ph-desktop-riga-ora", 0);
    int minimo = 0, larg_ora = 0;
    gtk_widget_measure(r, GTK_ORIENTATION_HORIZONTAL, -1, &minimo, NULL, NULL, NULL);
    gtk_widget_measure(ora, GTK_ORIENTATION_HORIZONTAL, -1, &larg_ora, NULL, NULL, NULL);
    /* chip 32 + spazio 10 + (sottotitolo + ora + 8): il sottotitolo qui ha minimo ~0, quindi si
     * controlla almeno chip + spazio + ora + 8. */
    if (larg_ora < 10 || minimo < 32 + 10 + larg_ora + 8)
        g_error("vecchi: minimo della riga %d con l'ora larga %d: l'ora non ci sta", minimo, larg_ora);

    GtkWidget *chip = trova(r, "ph-desktop-chip", 0);
    if (chip == NULL || !vecchi_ha_tinta(chip) || gtk_widget_has_css_class(chip, "ph-desktop-chip-vero"))
        g_error("vecchi: la sigla deve avere il fondo tinto");
    GtkWidget *ic = gtk_image_new();
    ph_dsk_riga_icona(r, ic);
    if (vecchi_ha_tinta(chip) || !gtk_widget_has_css_class(chip, "ph-desktop-chip-vero"))
        g_error("vecchi: con l'icona vera il fondo tinto deve sparire");
    ph_dsk_riga_imposta(r, NULL, PH_DSK_ACC_VERDE, true, "Altro", "Sotto", "15:52"); /* a icona messa: resta senza fondo */
    if (vecchi_ha_tinta(chip))
        g_error("vecchi: con l'icona vera un nuovo imposta ha rimesso il fondo tinto");
    ph_dsk_riga_icona(r, NULL);
    if (!vecchi_ha_tinta(chip) || gtk_widget_has_css_class(chip, "ph-desktop-chip-vero") ||
        !gtk_widget_has_css_class(chip, "ph-desktop-tinta-verde"))
        g_error("vecchi: tolta l'icona il fondo tinto (verde) deve tornare");

    GtkWidget *scatola = gtk_button_get_child(GTK_BUTTON(r));
    GtkLayoutManager *lm = gtk_widget_get_layout_manager(scatola);
    if (lm == NULL)
        g_error("vecchi: la scatola della riga non ha il suo layout manager");
    g_object_ref(lm);
    g_object_ref(scatola);
    gtk_button_set_child(GTK_BUTTON(r), NULL); /* senza genitore: il dispose a mano e' ammesso */
    GtkLayoutManagerClass *k = GTK_LAYOUT_MANAGER_GET_CLASS(lm);
    g_object_run_dispose(G_OBJECT(scatola)); /* figli staccati e liberati, il layout manager resta */
    int a = -1, b = -1, c = -1, e = -1;
    k->measure(lm, scatola, GTK_ORIENTATION_HORIZONTAL, -1, &a, &b, &c, &e);
    if (a != 0 || b != 0)
        g_error("vecchi: il layout manager di una scatola distrutta misura %d/%d, non 0", a, b);
    g_object_unref(lm);
    g_object_unref(scatola);
    g_object_unref(r);
}

typedef struct {
    PhDesktop *d;
    gint64 scadenza;
    int fase;
    bool applicata; /* la misura della fase e' stata messa: si aspetta il layout e si controlla */
    bool svuotato;  /* fasi «vuoto»: gli impegni sono stati tolti e il widget rifatto */
} Vecchi;

static void vecchi_avanza(Vecchi *v)
{
    v->fase++;
    v->applicata = false;
    v->svuotato = false;
    v->scadenza = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
}

/* Stato vuoto del calendario (nessun impegno): il testo si legge PER INTERO, mai con i puntini, e non
 * va a capo lettera per lettera. Torna 0 se il layout non e' ancora arrivato. */
static int vecchi_vuoto(PhDesktop *d, const char *fase)
{
    PhDskWidget *w = vecchi_widget(d, PH_CFG_WIDGET_CALENDARIO);
    if (vecchi_conta(w->corpo, "ph-desktop-giorno-impegno") != 0)
        g_error("vecchi (%s): calendario vuoto ma ci sono impegni", fase);
    GtkWidget *l = trova(w->corpo, "ph-desktop-w-sotto", 0);
    if (l == NULL || !GTK_IS_LABEL(l))
        g_error("vecchi (%s): calendario vuoto: manca il testo dello stato vuoto", fase);
    if (gtk_widget_get_width(l) <= 0)
        return 0;
    PangoLayout *lay = gtk_label_get_layout(GTK_LABEL(l));
    if (gtk_label_get_ellipsize(GTK_LABEL(l)) != PANGO_ELLIPSIZE_NONE || pango_layout_is_ellipsized(lay))
        g_error("vecchi (%s): il testo dello stato vuoto e' troncato con i puntini (\"%s\")", fase,
                gtk_label_get_text(GTK_LABEL(l)));
    if (pango_layout_get_line_count(lay) > 2)
        g_error("vecchi (%s): il testo dello stato vuoto va a capo in %d righe (larghezza %d)", fase,
                pango_layout_get_line_count(lay), gtk_widget_get_width(l));
    return 1;
}

/* Toglie gli impegni (come un telefono con il calendario vuoto) e rifa' i widget. */
static void vecchi_svuota_calendario(PhDesktop *d)
{
    for (size_t i = 0; i < d->n_eventi; i++) {
        g_free(d->eventi[i].titolo);
        d->eventi[i].titolo = NULL;
    }
    d->n_eventi = 0;
    d->eventi_noti = true; /* elenco vuoto vero: «nessun impegno», non «dati assenti» */
    ph_dsk_widget_aggiorna_tutti(d);
}

#define VECCHI_FASI 6
static gboolean vecchi_tick(gpointer dati)
{
    Vecchi *v = dati;
    PhDesktop *d = v->d;
    size_t n_notifiche = 0;
    ph_srv_notifiche(d->srv, &n_notifiche, NULL);
    bool pronto = d->memoria_nota && d->dettaglio_noto && d->eventi_noti && d->app_calendario != NULL &&
                  n_notifiche > 0;
    if (v->fase < 4 && !pronto) {
        if (g_get_monotonic_time() > v->scadenza)
            g_error("vecchi: dopo 5 s mancano dati: memoria %d dettaglio %d eventi %d app calendario %d notifiche %zu",
                    d->memoria_nota, d->dettaglio_noto, d->eventi_noti, d->app_calendario != NULL, n_notifiche);
        return G_SOURCE_CONTINUE;
    }
    PhCfgTelefono *p = ph_dsk_profilo(d);
    static const VecchiMis *const MISURE[VECCHI_FASI] = {VM_GRANDI, VM_MANIGLIA, VM_BANDA, VM_MINIME, VM_GRANDI,
                                                         VM_MINIME};
    static const char *const NOMI[VECCHI_FASI] = {"grandi", "maniglia", "fascia 290", "minime", "vuoto 320",
                                                  "vuoto 200"};
    if (v->fase < VECCHI_FASI && !v->applicata) {
        vecchi_imposta(d, p, MISURE[v->fase]);
        v->applicata = true;
        return G_SOURCE_CONTINUE; /* il layout arriva con il fotogramma */
    }
    if (v->fase < VECCHI_FASI && !vecchi_geometria(d)) {
        if (g_get_monotonic_time() > v->scadenza)
            g_error("vecchi (%s): dopo 5 s la misura vera dei widget non e' arrivata", NOMI[v->fase]);
        return G_SOURCE_CONTINUE;
    }
    switch (v->fase) {
    case 0:
        vecchi_controlla(d, NOMI[0]);
        vecchi_impostazioni(d);
        break;
    case 1:
        vecchi_controlla(d, "maniglia, prima");
        /* Dal percorso vero della maniglia, senza cambiare livello: il contenuto si rifa' solo da qui. */
        vecchi_maniglia(d, PH_CFG_WIDGET_NOTIFICHE, GDK_KEY_Down, 6); /* 148 -> 196: da 2 a 3 righe */
        vecchi_maniglia(d, PH_CFG_WIDGET_MEMORIA, GDK_KEY_Down, 10);  /* 120 -> 200: compare la legenda */
        vecchi_maniglia(d, PH_CFG_WIDGET_CALENDARIO, GDK_KEY_Down, 1); /* 150 -> 158: da 2 a 3 impegni */
        vecchi_maniglia(d, PH_CFG_WIDGET_STATO, GDK_KEY_Down, 2);      /* 124 -> 140: da 1 a 2 righe */
        vecchi_controlla(d, "maniglia, dopo");
        break;
    case 2:
        vecchi_controlla(d, NOMI[2]);
        break;
    case 3:
        vecchi_controlla(d, NOMI[3]);
        vecchi_riga();
        vecchi_senza_app_calendario(d);
        break;
    case 4:
    case 5:
        /* Prima si lascia finire le richieste in volo (il finto rimanderebbe i suoi impegni), poi si
         * svuota il calendario, e al fotogramma dopo si guarda il testo vero. */
        if (!v->svuotato) {
            if (d->annulla_calendario != NULL || d->annulla_app_cal != NULL || !d->eventi_noti) {
                if (g_get_monotonic_time() > v->scadenza)
                    g_error("vecchi (%s): le richieste del calendario non finiscono", NOMI[v->fase]);
                return G_SOURCE_CONTINUE;
            }
            vecchi_svuota_calendario(d);
            v->svuotato = true;
            return G_SOURCE_CONTINUE;
        }
        if (!vecchi_vuoto(d, NOMI[v->fase])) {
            if (g_get_monotonic_time() > v->scadenza)
                g_error("vecchi (%s): dopo 5 s il testo dello stato vuoto non ha una misura", NOMI[v->fase]);
            return G_SOURCE_CONTINUE;
        }
        break;
    default: break;
    }
    vecchi_avanza(v);
    if (v->fase >= VECCHI_FASI) {
        vecchi_finito = true;
        g_message("vecchi: quattro widget controllati a sei misure (maniglia vera compresa), clic, "
                  "legenda dentro la scheda, stato vuoto del calendario e ritocchi dei componenti");
        g_free(v);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void vecchi_avvia(PhShell *sh, PhDesktop *d, PhCfgTelefono *p)
{
    if (vecchi_iniziato)
        return;
    vecchi_iniziato = true;
    atexit(vecchi_esci);
    ph_dsk_orologio_ora_fissa(VECCHI_ORA); /* la prova non dipende dall'ora vera */
    PhShellGestoreFinestre gf = {.io = NULL,
                                 .apri_app = vecchi_apri,
                                 .elenco = vecchi_elenco,
                                 .clic_barra = vecchi_clic_barra,
                                 .specchio = vecchi_specchio,
                                 .specchio_aperto = vecchi_specchio_aperto};
    ph_shell_registra_finestre(sh, &gf);
    /* Subito i quattro widget (servono per far partire le richieste dei loro dati): la prima misura. */
    vecchi_imposta(d, p, VM_GRANDI);
    Vecchi *v = g_new0(Vecchi, 1);
    v->d = d;
    v->applicata = true;
    v->scadenza = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    g_timeout_add(100, vecchi_tick, v);
}
