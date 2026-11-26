/* Solo per il demo (compilato in demo-desktop, mai nel prodotto): prova a schermo del widget Meteo
 * nello stile «Adesso» (agente C). Con un Meteo alle misure delle soglie e in ogni stato (dati, dati
 * vecchi con l'avviso lungo e il pulsante della localita', nessun dato, in attesa) si controlla, con la
 * disposizione vera che GTK ha dato:
 *   - il pulsante dell'attribuzione a Open-Meteo e' DENTRO la scheda e in fondo;
 *   - curva e giorni compaiono e spariscono alle soglie, cambiando la misura a caldo come fa la
 *     maniglia (che non ricostruisce il widget): a farlo e' il gestore della misura del widget;
 *   - dopo la rimozione del widget la sua scheda (tenuta in vita apposta) non ha piu' i gestori.
 * Sequenza (come demo_controlli.c):
 *   meteoa-attendi   se all'uscita la verifica non e' stata eseguita, il processo esce con errore
 *   meteoa-misure    mette un solo Meteo (stile Adesso, 200 x 200) nel profilo
 *   meteoa-verifica  controlla (a fotogrammi, con scadenza) e chiude la sequenza */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "interno.h"
#include "shell/servizi_backend.h"

void ph_demo_meteo(PhShell *sh, const char *cosa);

static bool verifica_fatta;
static unsigned eventi_meteo; /* eventi meteo visti (pubblica() ne produce uno) */

static void conta_eventi(PhServizi *srv, PhSrvEvento ev, const char *arg, void *utente)
{
    (void)srv;
    (void)ev;
    (void)arg;
    (void)utente;
    eventi_meteo++;
}

static void esci_senza_verifica(void)
{
    if (!verifica_fatta) {
        fprintf(stderr, "meteo: la verifica non e' stata eseguita\n");
        _exit(1);
    }
}

/* Il primo widget (in profondita') con la classe data, o NULL. */
static GtkWidget *trova(GtkWidget *radice, const char *classe)
{
    for (GtkWidget *c = gtk_widget_get_first_child(radice); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (gtk_widget_has_css_class(c, classe))
            return c;
        GtkWidget *t = trova(c, classe);
        if (t != NULL)
            return t;
    }
    return NULL;
}

static int conta(GtkWidget *radice, const char *classe)
{
    int n = 0;
    for (GtkWidget *c = gtk_widget_get_first_child(radice); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (gtk_widget_has_css_class(c, classe))
            n++;
        n += conta(c, classe);
    }
    return n;
}

/* ---- Dati finti (nessun nome o coordinata vera) ---------------------------------------------------- */
typedef enum { S_DATI, S_VECCHI, S_NIENTE, S_ATTESA } Stato;

static void pubblica(PhDesktop *d, Stato st)
{
    PhSrvMeteo m;
    memset(&m, 0, sizeof m);
    int64_t adesso = g_get_real_time() / 1000000;
    bool dati = st == S_DATI || st == S_VECCHI;
    m.stato = st == S_DATI ? PH_SRV_METEO_PRONTO
                           : (st == S_ATTESA ? PH_SRV_METEO_IN_ATTESA : PH_SRV_METEO_SENZA_POSIZIONE);
    m.ha_dati = dati;
    if (st == S_VECCHI || st == S_NIENTE) /* frase lunga: la peggiore, va a capo e si tronca */
        g_strlcpy(m.frase,
                  "La posizione del telefono e' spenta e il permesso non e' stato dato: nessun luogo per le previsioni "
                  "di oggi, ne' per quelle dei prossimi giorni (simulato)",
                  sizeof m.frase);
    if (dati) {
        g_strlcpy(m.localita, "Sample City", sizeof m.localita);
        PhMeteoPrevisione *p = &m.dati;
        p->scarto_utc_s = 3600;
        p->ricevuta = st == S_VECCHI ? adesso - 7200 : adesso;
        p->adesso = (PhMeteoAdesso){adesso, 18.0, 17.0, 60, 0.0, 10, 2, 1};
        p->n_ore = PH_METEO_ORE_MAX;
        int64_t inizio = adesso - adesso % 3600;
        for (size_t i = 0; i < p->n_ore; i++) {
            p->ore[i].ora = inizio + (int64_t)i * 3600;
            p->ore[i].temperatura = 12.0 + (double)((i % 24) < 12 ? (i % 24) : 24 - (i % 24)) * 0.8;
            p->ore[i].codice = 2;
            p->ore[i].giorno = 1;
        }
        p->n_giorni = PH_METEO_GIORNI_MAX;
        int64_t mezzanotte = (adesso + p->scarto_utc_s) / 86400 * 86400 - p->scarto_utc_s;
        for (size_t k = 0; k < p->n_giorni; k++) {
            p->giorni[k].giorno = mezzanotte + (int64_t)k * 86400;
            p->giorni[k].codice = 3;
            p->giorni[k].minima = 9.0 + (double)(k % 3);
            p->giorni[k].massima = 18.0 + (double)(k % 4);
            p->giorni[k].prob_pioggia = 20;
        }
    }
    ph_servizi_pubblica_meteo_fonte(d->srv, &m, dati ? PH_SRV_METEO_FONTE_TELEFONO : PH_SRV_METEO_FONTE_NESSUNA);
}

/* ---- Casi ---------------------------------------------------------------------------------------------- */
typedef struct {
    int l, a;
    Stato stato;
    int curva;  /* attesa: 1 si', 0 no, -1 non importa */
    int giorni; /* attesi: numero, -1 non importa */
    const char *nome;
} Caso;

/* Alle soglie di logica_meteo.h: curva da 230, giorni da 230 + 2 x 28; il massimo sono 7 giorni. */
static const Caso CASI[] = {
    {200, 200, S_DATI, 0, 0, "200x200 dati (minimo)"},
    {200, 200, S_VECCHI, 0, 0, "200x200 dati vecchi, avviso lungo e localita'"},
    {200, 200, S_NIENTE, 0, 0, "200x200 senza dati, frase lunga e localita'"},
    {200, 200, S_ATTESA, 0, 0, "200x200 in attesa"},
    {250, 200, S_DATI, 0, 0, "250x200 dati (sotto la soglia della curva)"},
    {250, 230, S_DATI, 1, 0, "250x230 dati (soglia della curva)"},
    {250, 230, S_VECCHI, -1, 0, "250x230 dati vecchi"},
    {320, 286, S_DATI, 1, 2, "320x286 dati (soglia dei giorni)"},
    {250, 200, S_DATI, 0, 0, "tornato a 250x200: curva e giorni spariscono"},
    {480, 640, S_DATI, 1, 7, "480x640 dati (massimo, sette giorni)"},
};

typedef struct {
    GtkWidget *radice;
    PhDesktop *d;
    size_t caso;
    int applicato; /* 0 da pubblicare, 1 attesa dell'evento, 2 misura cambiata: si controlla */
    unsigned atteso;
    gint64 scadenza;
    char *dettaglio;
} Verifica;

static PhDskWidget *il_widget(Verifica *v, GtkWidget **scheda)
{
    GtkWidget *s = trova(v->radice, "ph-desktop-widget");
    if (s == NULL)
        return NULL;
    PhDskWidget *w = g_object_get_data(G_OBJECT(s), "ph-desktop-widget");
    if (w == NULL || w->tipo != PH_CFG_WIDGET_METEO)
        return NULL;
    *scheda = s;
    return w;
}

/* Cambia la misura come la maniglia (widget.c: ridim_calcola e geometria_applica): i campi del widget,
 * la richiesta di misura della scheda (da cui parte notify::width-request) e il posto nel contenitore. */
static void ridimensiona(Verifica *v, PhDskWidget *w, GtkWidget *scheda, int l, int a)
{
    w->larghezza = l;
    w->altezza = a;
    gtk_widget_set_size_request(scheda, l, a);
    ph_shell_libero_sposta(v->d->libero, scheda, v->d->area_l - w->destra - l, w->alto, l, a);
}

/* true se il caso e' soddisfatto; se non lo e' (ancora) lascia in v->dettaglio che cosa manca. */
static bool controlla(Verifica *v, const Caso *c, GtkWidget *scheda)
{
    g_clear_pointer(&v->dettaglio, g_free);
    /* gtk_widget_get_width da' la misura del CONTENUTO (senza l'imbottitura): i bordi veri della scheda
     * si prendono da compute_bounds su se stessa. */
    graphene_rect_t rs;
    if (!gtk_widget_compute_bounds(scheda, scheda, &rs)) {
        v->dettaglio = g_strdup("bordi della scheda non calcolabili");
        return false;
    }
    int sl = (int)lroundf(rs.size.width), sa = (int)lroundf(rs.size.height);
    if (sl != c->l || sa != c->a) {
        v->dettaglio = g_strdup_printf("scheda %dx%d invece di %dx%d", sl, sa, c->l, c->a);
        return false;
    }
    /* Lo stato giusto e' arrivato (e non quello del telefono finto). */
    int avviso = conta(scheda, "ph-desktop-meteo-avviso"), stato = conta(scheda, "ph-desktop-meteo-stato");
    bool vecchi = c->stato == S_VECCHI, niente = c->stato == S_NIENTE || c->stato == S_ATTESA;
    if ((avviso > 0) != vecchi || (stato > 0) != niente) {
        v->dettaglio = g_strdup_printf("stato non quello atteso (avviso %d, stato %d)", avviso, stato);
        return false;
    }
    GtkWidget *att = trova(scheda, "ph-desktop-meteo-attrib");
    if (att == NULL || !gtk_widget_get_mapped(att) || gtk_widget_get_width(att) <= 0 || gtk_widget_get_height(att) <= 0) {
        v->dettaglio = g_strdup("attribuzione assente o non disposta");
        return false;
    }
    graphene_rect_t r;
    if (!gtk_widget_compute_bounds(att, scheda, &r)) {
        v->dettaglio = g_strdup("bordi dell'attribuzione non calcolabili");
        return false;
    }
    int minimo = 0, naturale = 0;
    PhDskWidget *w = g_object_get_data(G_OBJECT(scheda), "ph-desktop-widget");
    gtk_widget_measure(w->interno, GTK_ORIENTATION_VERTICAL, c->l - 40, &minimo, &naturale, NULL, NULL);
    /* Dentro la scheda (mezzo pixel di tolleranza) e in fondo (a meno dell'imbottitura sotto). */
    bool dentro = r.origin.x >= -0.5f && r.origin.y >= -0.5f && r.origin.x + r.size.width <= c->l + 0.5f &&
                  r.origin.y + r.size.height <= c->a + 0.5f;
    bool in_fondo = r.origin.y + r.size.height >= c->a - 40.0f;
    if (!dentro || !in_fondo) {
        v->dettaglio = g_strdup_printf("attribuzione a (%.0f,%.0f) %.0fx%.0f in una scheda %dx%d; contenuto naturale %d "
                                       "di altezza",
                                       r.origin.x, r.origin.y, r.size.width, r.size.height, c->l, c->a, naturale);
        return false;
    }
    /* Curva e giorni: presenti o no, e i giorni (e la curva) dentro la scheda. */
    GtkWidget *curva = trova(scheda, "ph-desktop-meteo-curva");
    GtkWidget *giorni = trova(scheda, "ph-desktop-meteo-giorni");
    int n_giorni = giorni != NULL ? conta(giorni, "ph-desktop-meteo-giorno-nome") : 0;
    if (c->curva >= 0 && (curva != NULL) != (c->curva == 1)) {
        v->dettaglio = g_strdup_printf("curva %s", curva != NULL ? "presente" : "assente");
        return false;
    }
    if (c->giorni >= 0 && n_giorni != c->giorni) {
        v->dettaglio = g_strdup_printf("%d giorni invece di %d", n_giorni, c->giorni);
        return false;
    }
    GtkWidget *parti[2] = {curva, giorni};
    for (int i = 0; i < 2; i++) {
        graphene_rect_t q;
        if (parti[i] != NULL && gtk_widget_compute_bounds(parti[i], scheda, &q) &&
            (q.origin.y < -0.5f || q.origin.y + q.size.height > c->a + 0.5f)) {
            v->dettaglio = g_strdup_printf("%s esce dalla scheda (%.0f..%.0f su %d)", i == 0 ? "curva" : "giorni",
                                           q.origin.y, q.origin.y + q.size.height, c->a);
            return false;
        }
    }
    return true;
}

static void gestori_tolti(Verifica *v)
{
    /* Come st_agente_b (demo_hook.c): tolto il widget, la sua scheda (tenuta viva da noi) non ha piu'
     * nessun gestore della misura, e ridimensionarla non tocca memoria liberata (ASan). */
    GtkWidget *scheda = NULL;
    PhDskWidget *w = il_widget(v, &scheda);
    if (w == NULL)
        g_error("meteo: il widget non c'e' per la prova dei gestori");
    guint sig = g_signal_lookup("notify", GTK_TYPE_WIDGET);
    GQuark q_l = g_quark_from_static_string("width-request"), q_a = g_quark_from_static_string("height-request");
    g_object_ref(scheda);
    if (!g_signal_has_handler_pending(scheda, sig, q_l, TRUE) || !g_signal_has_handler_pending(scheda, sig, q_a, TRUE))
        g_error("meteo: il widget non ha i gestori della misura (la prova non proverebbe nulla)");
    PhCfgTelefono *p = ph_dsk_profilo(v->d);
    memset(p->widget, 0, sizeof p->widget);
    p->n_widget = 0;
    ph_dsk_widget_ricostruisci(v->d); /* `ferma` del Meteo */
    if (g_signal_has_handler_pending(scheda, sig, q_l, TRUE) || g_signal_has_handler_pending(scheda, sig, q_a, TRUE))
        g_error("meteo: la scheda ha ancora i gestori della misura dopo la rimozione");
    gtk_widget_set_size_request(scheda, 333, 222); /* se il gestore c'e' ancora, qui cade */
    g_object_unref(scheda);
}

static gboolean fotogramma(GtkWidget *radice, GdkFrameClock *clock, gpointer dati)
{
    (void)clock;
    (void)radice;
    Verifica *v = dati;
    GtkWidget *scheda = NULL;
    PhDskWidget *w = il_widget(v, &scheda);
    if (w == NULL) {
        if (g_get_monotonic_time() > v->scadenza)
            g_error("meteo: il widget non e' comparso");
        return G_SOURCE_CONTINUE;
    }
    const Caso *c = &CASI[v->caso];
    /* Prima lo stato, e si aspetta l'evento meteo (il widget si e' ridisegnato con la misura di prima);
     * SOLO DOPO la misura nuova, cosi' a ridisegnare e' il gestore della misura e non l'evento. */
    if (v->applicato == 0) {
        v->atteso = eventi_meteo + 1;
        pubblica(v->d, c->stato);
        v->applicato = 1;
        v->scadenza = g_get_monotonic_time() + 4 * G_USEC_PER_SEC;
        return G_SOURCE_CONTINUE;
    }
    if (v->applicato == 1) {
        if (eventi_meteo < v->atteso) {
            if (g_get_monotonic_time() > v->scadenza)
                g_error("meteo: caso «%s»: l'evento meteo non e' arrivato", c->nome);
            return G_SOURCE_CONTINUE;
        }
        ridimensiona(v, w, scheda, c->l, c->a);
        v->applicato = 2;
        v->scadenza = g_get_monotonic_time() + 4 * G_USEC_PER_SEC;
        return G_SOURCE_CONTINUE;
    }
    if (!controlla(v, c, scheda)) {
        if (g_get_monotonic_time() > v->scadenza)
            g_error("meteo: caso «%s»: %s", c->nome, v->dettaglio != NULL ? v->dettaglio : "?");
        return G_SOURCE_CONTINUE;
    }
    g_message("meteo: ok «%s»", c->nome);
    v->caso++;
    v->applicato = 0;
    if (v->caso < G_N_ELEMENTS(CASI))
        return G_SOURCE_CONTINUE;
    gestori_tolti(v);
    verifica_fatta = true;
    g_message("meteo: attribuzione dentro e in fondo a ogni misura e stato, curva e giorni alle soglie a caldo, "
              "gestori tolti");
    return G_SOURCE_REMOVE;
}

void ph_demo_meteo(PhShell *sh, const char *cosa)
{
    GtkWidget *radice = ph_shell_radice(sh);
    PhDesktop *d = g_object_get_data(G_OBJECT(radice), "ph-desktop");
    PhCfgTelefono *p = d != NULL ? ph_dsk_profilo(d) : NULL;
    if (strcmp(cosa, "meteoa-attendi") == 0) {
        atexit(esci_senza_verifica);
        return;
    }
    if (p == NULL)
        return;
    if (strcmp(cosa, "meteoa-misure") == 0) {
        memset(p->widget, 0, sizeof p->widget);
        p->widget[0].tipo = PH_CFG_WIDGET_METEO;
        p->widget[0].stile = PH_DSK_METEO_ADESSO;
        p->widget[0].destra = 16;
        p->widget[0].alto = 16;
        p->widget[0].larghezza = 200;
        p->widget[0].altezza = 200;
        p->widget[0].dimensione = PH_CFG_WIDGET_PICCOLO;
        p->n_widget = 1;
        p->meteo_luogo_scelto = false; /* cosi' gli stati mostrano anche il pulsante della localita' */
        ph_dsk_widget_ricostruisci(d);
    } else if (strcmp(cosa, "meteoa-verifica") == 0) {
        Verifica *v = g_new0(Verifica, 1); /* vive quanto il processo */
        v->radice = radice;
        v->d = d;
        ph_srv_ascolta(d->srv, PH_SRV_EV_METEO, conta_eventi, NULL);
        v->scadenza = g_get_monotonic_time() + 4 * G_USEC_PER_SEC;
        gtk_widget_add_tick_callback(radice, fotogramma, v, NULL);
    }
}
