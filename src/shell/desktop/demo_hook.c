/* Solo per il demo (compilato in demo-desktop, mai nel prodotto): apre o prepara
 * qualcosa senza mouse, per le fotografie di strumenti/foto-demo.sh. Lavora solo
 * con le funzioni interne del pezzo e con i widget trovati per classe CSS.
 * cosa: widget3, menu-desktop, menu-icona, menu-widget, sfondo, sfondo-solido,
 * sfondo-sfumato, sfondo-immagine, sfondo-scegli-prima, catalogo, chiudi. */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "interno.h"
#include "shell/finto/finto.h"
#include "shell/servizi_backend.h"

void ph_demo_mostra(PhShell *sh, const char *cosa);
PhFinto *ph_demo_finto(void);                /* demo.c */
void ph_demo_finto_comando(const char *c);   /* demo.c */
static void stress_avvia(PhShell *sh);

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

void ph_demo_mostra(PhShell *sh, const char *cosa)
{
    GtkWidget *radice = ph_shell_radice(sh);
    PhDesktop *d = g_object_get_data(G_OBJECT(radice), "ph-desktop");
    PhCfgTelefono *p = d != NULL ? ph_dsk_profilo(d) : NULL;
    if (d == NULL || p == NULL || cosa == NULL)
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
        /* Prova del vetro (fase 5): ogni superficie del pezzo, una volta costruita, ha la classe
         * ph-vetro-chiaro (la mette ph_shell_vetro_registra): widget, menu, tre dialoghi. Si apre
         * ogni cosa e si controlla quella visibile; se una manca, il processo cade. */
        static const char *const classi[] = {"ph-desktop-widget", "ph-menu"};
        int visti = 0;
        for (size_t k = 0; k < G_N_ELEMENTS(classi); k++)
            for (int i = 0;; i++) {
                GtkWidget *w = trova(radice, classi[k], i);
                if (w == NULL)
                    break;
                visti++;
                if (!gtk_widget_has_css_class(w, "ph-vetro-chiaro"))
                    g_error("verifica-vetro: %s senza ph-vetro-chiaro", classi[k]);
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
                if (!gtk_widget_has_css_class(w, "ph-vetro-chiaro"))
                    g_error("verifica-vetro: dialogo %zu senza ph-vetro-chiaro", k);
                dialoghi++;
            }
            if (dialoghi == 0)
                g_error("verifica-vetro: dialogo %zu non trovato dopo l'apertura", k);
            ph_shell_popup_chiudi(sh);
        }
        verifica_vetro_fatta = true;
        g_message("verifica-vetro: widget, menu e dialoghi sono vetro chiaro");
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
        unsigned id = w->id;
        PhCfgWidgetDim dim = (PhCfgWidgetDim)((w->dim + 1) % 3);
        ph_dsk_widget_dimensione(s->d, id, dim);
        w = st_widget(s, s->j);
        if (w != NULL)
            ph_dsk_widget_stile_imposta(s->d, w->id, (w->stile + 1) % ph_dsk_stili_n(w->tipo), w->opzione);
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
    ph_demo_finto_comando("perdi:3");
    fprintf(stderr, "prova d'insieme: %u attese deboli scadute\n", s->scadute);
    stress_finito = true;
    GtkRoot *r = gtk_widget_get_root(s->radice);
    if (r != NULL)
        gtk_window_close(GTK_WINDOW(r));
    return true;
}

static bool (*const ST_PASSI[])(Stress *) = {st_collegato, st_tipi, st_insieme, st_profilo_nuovo, st_chiusura};

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
