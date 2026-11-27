/* Solo per il demo (compilato in demo-desktop, mai nel prodotto): prova a schermo dei widget
 * «Chiamate recenti», «Rete» e «In riproduzione» (PIANO.md 9) con i widget veri sul telefono
 * finto (dati inventati: «Alex Sample», «SampleNet», «Sample Song»). Una sequenza sola, ogni
 * attesa a condizione con scadenza (mai una pausa fissa):
 *   1. i tre widget nascono e dichiarano il loro interesse: i dati arrivano;
 *   2. chiamate: righe come la logica, la persa in corallo, il clic apre l'app del telefono;
 *      permesso negato e funzione assente dicono «Not available on this phone», poi tornano le righe;
 *   3. rete: nome della rete e operatore; modo aereo; permesso negato;
 *   4. lettore: titolo e artista; i tre pulsanti mandano il comando giusto (1, 3, 2) e il brano
 *      cambia; senza sessione «Nothing playing» e i pulsanti sono spenti e non mandano nulla;
 *   5. widget tolti: nessun avviso arriva piu' (l'interesse e' stato tolto).
 * Nomi, numeri e titoli (finti) non si scrivono mai in un messaggio.
 *   nuovi-attendi   se all'uscita la prova non e' arrivata in fondo, il processo esce con errore
 *   nuovi-avvia     parte la sequenza (aspetta il profilo del telefono finto, con scadenza)
 *   nuovi-foto      solo i tre widget affiancati, per fotografarli (nessuna verifica) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "interno.h"
#include "logica_nuovi.h"
#include "shell/finto/finto.h"

void ph_demo_nuovi(PhShell *sh, const char *cosa);
PhFinto *ph_demo_finto(void); /* demo.c */

static bool sequenza_finita;

static void esci_senza_prova(void)
{
    if (!sequenza_finita) {
        fprintf(stderr, "nuovi: la prova non e' arrivata in fondo\n");
        _exit(1);
    }
}

#define SCADENZA_US (8 * G_USEC_PER_SEC)
#define FALLISCI(...) g_error("nuovi: " __VA_ARGS__)
#define VERIFICA(cond, msg)                                                                  \
    do {                                                                                     \
        if (!(cond))                                                                         \
            FALLISCI("%s", msg);                                                             \
    } while (0)
#define ATTENDI(v, cond, msg)                                                                \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            if (g_get_monotonic_time() > (v)->scadenza)                                      \
                FALLISCI("scaduto: %s", msg);                                                \
            return G_SOURCE_CONTINUE;                                                        \
        }                                                                                    \
    } while (0)

typedef struct {
    PhShell *sh;
    GtkWidget *radice;
    PhDesktop *d;
    int fase;
    gint64 scadenza;
    unsigned id[3];                    /* chiamate, rete, lettore */
    int ev[3];                         /* avvisi arrivati: chiamate, rete, media */
    int ev_prima[3];
    unsigned comandi_prima;
    bool barriera_fatta;
    bool barriera_in_corso;
    int barriere;
} Prova;

/* ---- Utilita' sull'albero ------------------------------------------------------------------- */
static void raccogli(GtkWidget *r, const char *classe, GtkWidget **out, int max, int *n)
{
    for (GtkWidget *c = gtk_widget_get_first_child(r); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (gtk_widget_has_css_class(c, classe) && *n < max)
            out[(*n)++] = c;
        raccogli(c, classe, out, max, n);
    }
}

static PhDskWidget *widget_tipo(Prova *v, PhCfgWidgetTipo tipo)
{
    for (size_t i = 0; i < v->d->widget->len; i++) {
        PhDskWidget *w = g_ptr_array_index(v->d->widget, i);
        if (w->tipo == tipo)
            return w;
    }
    return NULL;
}

/* Un'etichetta visibile del widget con il testo dato (e la classe, se non NULL). */
static bool testo_visibile(GtkWidget *r, const char *testo)
{
    for (GtkWidget *c = gtk_widget_get_first_child(r); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (GTK_IS_LABEL(c) && gtk_widget_get_mapped(c) && strcmp(gtk_label_get_text(GTK_LABEL(c)), testo) == 0)
            return true;
        if (testo_visibile(c, testo))
            return true;
    }
    return false;
}

static int conta_classe(GtkWidget *r, const char *classe)
{
    GtkWidget *t[32];
    int n = 0;
    raccogli(r, classe, t, 32, &n);
    return n;
}

static GtkWidget *primo_classe(GtkWidget *r, const char *classe)
{
    GtkWidget *t[32];
    int n = 0;
    raccogli(r, classe, t, 32, &n);
    return n > 0 ? t[0] : NULL;
}

static void avviso(PhServizi *s, PhSrvEvento ev, const char *arg, void *utente)
{
    (void)s;
    (void)arg;
    Prova *v = utente;
    if (ev == PH_SRV_EV_CHIAMATE)
        v->ev[0]++;
    else if (ev == PH_SRV_EV_RETE)
        v->ev[1]++;
    else if (ev == PH_SRV_EV_MEDIA)
        v->ev[2]++;
}

/* Barriera: una richiesta che il telefono finto serve dopo tutto cio' che gli e' stato chiesto
 * prima (lavora in fila): quando risponde, i suoi avvisi precedenti sono gia' arrivati. */
static void barriera_fatta(PhServizi *s, PhSrvEsito esito, const char *frase, bool presente, uint64_t ora_ms,
                           const char *pacchetto, void *utente)
{
    (void)s;
    (void)esito;
    (void)frase;
    (void)presente;
    (void)ora_ms;
    (void)pacchetto;
    Prova *v = utente;
    v->barriere++;
}

static void barriera_chiedi(Prova *v)
{
    ph_srv_tel_sveglia(v->d->srv, NULL, barriera_fatta, v);
}

static void metti_widget(PhCfgTelefono *p)
{
    static const struct {
        PhCfgWidgetTipo tipo;
        int destra, alto, l, a;
    } W[3] = {{PH_CFG_WIDGET_CHIAMATE, 16, 16, 250, 230},
              {PH_CFG_WIDGET_RETE, 16, 262, 250, 180},
              {PH_CFG_WIDGET_RIPRODUZIONE, 282, 16, 250, 170}};
    memset(p->widget, 0, sizeof p->widget);
    for (int i = 0; i < 3; i++) {
        p->widget[i].tipo = W[i].tipo;
        p->widget[i].destra = W[i].destra;
        p->widget[i].alto = W[i].alto;
        p->widget[i].larghezza = W[i].l;
        p->widget[i].altezza = W[i].a;
        p->widget[i].dimensione = PH_CFG_WIDGET_MEDIO;
    }
    p->n_widget = 3;
}

/* ---- La sequenza ---------------------------------------------------------------------------- */
static gboolean fotogramma(Prova *v)
{
    PhCfgTelefono *p = ph_dsk_profilo(v->d);
    PhFinto *f = ph_demo_finto();
    PhServizi *srv = v->d->srv;
    switch (v->fase) {
    case 0: { /* il profilo e' arrivato: i tre widget, l'app del telefono nota (il finto non ha Phone vero) */
        ATTENDI(v, p != NULL && p->n_icone >= 1, "il profilo del telefono finto non e' arrivato");
        ph_srv_ascolta(srv, PH_SRV_EV_CHIAMATE, avviso, v);
        ph_srv_ascolta(srv, PH_SRV_EV_RETE, avviso, v);
        ph_srv_ascolta(srv, PH_SRV_EV_MEDIA, avviso, v);
        ph_dsk_telefono_pacchetto_prova("it.finto.chiamate");
        metti_widget(p);
        ph_dsk_widget_ricostruisci(v->d);
        v->fase = 1;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 1: { /* 1. i dati arrivano e i widget li mostrano */
        PhDskWidget *c = widget_tipo(v, PH_CFG_WIDGET_CHIAMATE);
        PhDskWidget *r = widget_tipo(v, PH_CFG_WIDGET_RETE);
        PhDskWidget *m = widget_tipo(v, PH_CFG_WIDGET_RIPRODUZIONE);
        VERIFICA(c != NULL && r != NULL && m != NULL, "i tre widget non ci sono");
        v->id[0] = c->id;
        v->id[1] = r->id;
        v->id[2] = m->id;
        ATTENDI(v, ph_srv_chiamate(srv)->stato == PH_SRV_LET_OK && ph_srv_rete(srv)->stato == PH_SRV_LET_OK &&
                       ph_srv_media(srv)->stato_lettura == PH_SRV_LET_OK,
                "i dati dei tre servizi (l'interesse non e' stato dichiarato?)");
        /* 2. chiamate: tante righe quante dice la logica; la persa e' in corallo */
        size_t attese = ph_dsk_chiamate_da_mostrare(ph_srv_chiamate(srv), c->larghezza, c->altezza);
        ATTENDI(v, conta_classe(c->scheda, "ph-desktop-riga-elenco") == (int)attese && attese >= 2,
                "le righe delle chiamate come le vuole la logica");
        /* Nessuna riga tagliata: l'ultima sta tutta dentro la scheda (meno l'imbottitura di 16). */
        {
            GtkWidget *righe[8];
            int nr = 0;
            raccogli(c->scheda, "ph-desktop-riga-elenco", righe, 8, &nr);
            graphene_rect_t rb;
            VERIFICA(nr > 0 && gtk_widget_compute_bounds(righe[nr - 1], c->scheda, &rb), "misura dell'ultima riga");
            VERIFICA(rb.origin.y + rb.size.height <= (float)(c->altezza - 16) + 1, "l'ultima riga e' tagliata");
        }
        VERIFICA(testo_visibile(c->scheda, "Alex Sample"), "il nome della prima chiamata non si vede");
        VERIFICA(conta_classe(c->scheda, "ph-desktop-chiamata-persa") == 1, "la chiamata persa non e' in corallo");
        VERIFICA(testo_visibile(c->scheda, ph_t("Persa")), "il tipo della chiamata persa non c'e'");
        /* Il clic su una riga apre l'app del telefono. */
        g_demo_aperta[0] = '\0';
        GtkWidget *riga = primo_classe(c->scheda, "ph-desktop-riga-elenco");
        VERIFICA(riga != NULL, "nessuna riga da cliccare");
        g_signal_emit_by_name(riga, "clicked");
        VERIFICA(strcmp(g_demo_aperta, "it.finto.chiamate") == 0, "il clic su una chiamata non apre l'app del telefono");
        /* Senza app del telefono nota le righe non sono pulsanti: nessun clic (pacchetto che il finto non ha). */
        ph_dsk_telefono_pacchetto_prova("it.non.esiste");
        VERIFICA(ph_dsk_telefono_pacchetto(v->d) == NULL, "un pacchetto sconosciuto risulta noto");
        ph_dsk_telefono_pacchetto_prova("it.finto.chiamate");
        /* 3. rete */
        VERIFICA(testo_visibile(r->scheda, "SampleNet"), "il nome della rete Wi-Fi non si vede");
        VERIFICA(testo_visibile(r->scheda, "Sample Mobile \xc2\xb7 4G"), "operatore e generazione non si vedono");
        VERIFICA(!testo_visibile(r->scheda, ph_t("Modalità aereo")), "modo aereo mostrato senza essere attivo");
        /* 4. lettore */
        VERIFICA(testo_visibile(m->scheda, "Sample Song") && testo_visibile(m->scheda, "Sample Artist"),
                 "titolo e artista non si vedono");
        GtkWidget *btn[3];
        int n = 0;
        raccogli(m->scheda, "ph-desktop-media-tasto", btn, 3, &n);
        VERIFICA(n == 3, "i tre pulsanti del lettore non ci sono");
        for (int i = 0; i < 3; i++)
            VERIFICA(gtk_widget_get_sensitive(btn[i]), "un pulsante del lettore e' spento con un brano che suona");
        v->comandi_prima = ph_finto_media_comandi(f);
        /* Ordine nell'albero: indietro, play-pausa, avanti. Play-pausa: comando 1. */
        g_signal_emit_by_name(btn[1], "clicked");
        v->fase = 2;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 2: { /* play-pausa arrivato al telefono; ora avanti (2) */
        ATTENDI(v, ph_finto_media_comandi(f) == v->comandi_prima + 1, "il comando play-pausa non e' arrivato");
        VERIFICA(ph_finto_media_ultimo_comando(f) == PH_SRV_MEDIA_CMD_PLAY_PAUSA, "il pulsante centrale non manda 1");
        PhDskWidget *m = widget_tipo(v, PH_CFG_WIDGET_RIPRODUZIONE);
        GtkWidget *btn[3];
        int n = 0;
        raccogli(m->scheda, "ph-desktop-media-tasto", btn, 3, &n);
        g_signal_emit_by_name(btn[2], "clicked");
        v->fase = 3;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 3: {
        ATTENDI(v, ph_finto_media_comandi(f) == v->comandi_prima + 2, "il comando avanti non e' arrivato");
        VERIFICA(ph_finto_media_ultimo_comando(f) == PH_SRV_MEDIA_CMD_AVANTI, "il pulsante avanti non manda 2");
        PhDskWidget *m = widget_tipo(v, PH_CFG_WIDGET_RIPRODUZIONE);
        ATTENDI(v, testo_visibile(m->scheda, "Another Sample Song"), "il brano nuovo dopo «avanti»");
        GtkWidget *btn[3];
        int n = 0;
        raccogli(m->scheda, "ph-desktop-media-tasto", btn, 3, &n);
        g_signal_emit_by_name(btn[0], "clicked");
        v->fase = 4;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 4: { /* indietro (3); poi il titolo apre l'app che suona, se nota */
        ATTENDI(v, ph_finto_media_comandi(f) == v->comandi_prima + 3, "il comando indietro non e' arrivato");
        VERIFICA(ph_finto_media_ultimo_comando(f) == PH_SRV_MEDIA_CMD_INDIETRO, "il pulsante indietro non manda 3");
        PhDskWidget *m = widget_tipo(v, PH_CFG_WIDGET_RIPRODUZIONE);
        ATTENDI(v, testo_visibile(m->scheda, "Sample Song"), "il brano di prima dopo «indietro»");
        const char *pk = ph_dsk_media_pacchetto(ph_srv_media(srv));
        g_demo_aperta[0] = '\0';
        GtkWidget *tit = primo_classe(m->scheda, "ph-desktop-media-titolo-btn");
        VERIFICA(tit != NULL, "il titolo cliccabile non c'e'");
        g_signal_emit_by_name(tit, "clicked");
        if (pk != NULL && ph_srv_app_cerca(srv, pk) != NULL)
            VERIFICA(strcmp(g_demo_aperta, pk) == 0, "il clic sul titolo non apre l'app che suona");
        else
            VERIFICA(g_demo_aperta[0] == '\0', "il clic sul titolo ha aperto un'app non nota");
        /* Senza sessione: «Nothing playing», pulsanti spenti e muti. */
        ph_finto_media_assente(f, true);
        v->fase = 5;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 5: {
        PhDskWidget *m = widget_tipo(v, PH_CFG_WIDGET_RIPRODUZIONE);
        ATTENDI(v, testo_visibile(m->scheda, ph_t("Niente in riproduzione")), "«Nothing playing»");
        VERIFICA(!testo_visibile(m->scheda, "Sample Song"), "il brano resta con la sessione finita");
        GtkWidget *btn[3];
        int n = 0;
        raccogli(m->scheda, "ph-desktop-media-tasto", btn, 3, &n);
        VERIFICA(n == 3, "i pulsanti non ci sono");
        v->comandi_prima = ph_finto_media_comandi(f);
        for (int i = 0; i < 3; i++) {
            VERIFICA(!gtk_widget_get_sensitive(btn[i]), "un pulsante e' acceso senza brano");
            g_signal_emit_by_name(btn[i], "clicked"); /* anche forzato: non deve partire nulla */
        }
        v->barriere = 0;
        barriera_chiedi(v);
        v->fase = 6;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 6: { /* nessun comando e' partito; permesso negato al lettore: «Not available on this phone» */
        ATTENDI(v, v->barriere == 1, "la barriera dopo i clic sui pulsanti spenti");
        VERIFICA(ph_finto_media_comandi(f) == v->comandi_prima, "un pulsante spento ha mandato un comando");
        ph_finto_voce_modo(f, PH_FINTO_VOCE_MEDIA, PH_FINTO_MODO_ERRORE);
        v->fase = 7;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 7: {
        PhDskWidget *m = widget_tipo(v, PH_CFG_WIDGET_RIPRODUZIONE);
        ATTENDI(v, testo_visibile(m->scheda, ph_t("Non disponibile su questo telefono")), "lettore: non disponibile (negato)");
        ph_finto_voce_modo(f, PH_FINTO_VOCE_MEDIA, PH_FINTO_MODO_NORMALE);
        ph_finto_media_assente(f, false);
        /* Chiamate: permesso negato, poi funzione assente, poi di nuovo le righe. */
        ph_finto_voce_modo(f, PH_FINTO_VOCE_CHIAMATE, PH_FINTO_MODO_ERRORE);
        v->fase = 8;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 8: {
        PhDskWidget *c = widget_tipo(v, PH_CFG_WIDGET_CHIAMATE);
        ATTENDI(v, testo_visibile(c->scheda, ph_t("Non disponibile su questo telefono")), "chiamate: non disponibile (negato)");
        VERIFICA(conta_classe(c->scheda, "ph-desktop-riga-elenco") == 0, "righe delle chiamate con il permesso negato");
        ph_finto_voce_modo(f, PH_FINTO_VOCE_CHIAMATE, PH_FINTO_MODO_NON_DISPONIBILE);
        v->fase = 9;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 9: { /* l'ultimo avviso non cambia il testo: si aspetta lo stato vero del servizio */
        PhDskWidget *c = widget_tipo(v, PH_CFG_WIDGET_CHIAMATE);
        ATTENDI(v, ph_srv_chiamate(srv)->stato == PH_SRV_LET_NON_DISPONIBILE, "chiamate: funzione assente");
        VERIFICA(testo_visibile(c->scheda, ph_t("Non disponibile su questo telefono")), "chiamate: funzione assente non detta");
        ph_finto_voce_modo(f, PH_FINTO_VOCE_CHIAMATE, PH_FINTO_MODO_NORMALE);
        /* Rete: modo aereo. */
        ph_finto_rete_aereo(f, true);
        v->fase = 10;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 10: {
        PhDskWidget *c = widget_tipo(v, PH_CFG_WIDGET_CHIAMATE);
        PhDskWidget *r = widget_tipo(v, PH_CFG_WIDGET_RETE);
        ATTENDI(v, conta_classe(c->scheda, "ph-desktop-riga-elenco") >= 2, "chiamate: tornano le righe");
        ATTENDI(v, testo_visibile(r->scheda, ph_t("Modalità aereo")), "rete: modo aereo");
        VERIFICA(testo_visibile(r->scheda, ph_t("Spento")), "rete: con il modo aereo il Wi-Fi non e' «Off»");
        VERIFICA(!testo_visibile(r->scheda, "SampleNet"), "rete: il nome resta con il modo aereo");
        ph_finto_rete_aereo(f, false);
        ph_finto_voce_modo(f, PH_FINTO_VOCE_RETE, PH_FINTO_MODO_ERRORE);
        v->fase = 11;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 11: {
        PhDskWidget *r = widget_tipo(v, PH_CFG_WIDGET_RETE);
        ATTENDI(v, testo_visibile(r->scheda, ph_t("Non disponibile su questo telefono")), "rete: non disponibile (negato)");
        VERIFICA(!testo_visibile(r->scheda, "SampleNet") && !testo_visibile(r->scheda, ph_t("Modalità aereo")),
                 "rete: dati rimasti con il permesso negato");
        ph_finto_voce_modo(f, PH_FINTO_VOCE_RETE, PH_FINTO_MODO_NORMALE);
        v->fase = 12;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 12: { /* 5. widget tolti: l'interesse sparisce, nessun avviso arriva piu' */
        PhDskWidget *r = widget_tipo(v, PH_CFG_WIDGET_RETE);
        ATTENDI(v, testo_visibile(r->scheda, "SampleNet"), "rete: tornano i dati");
        /* Ogni rimozione ricostruisce i widget (id nuovi): ogni volta si cerca per tipo. */
        static const PhCfgWidgetTipo TIPI[3] = {PH_CFG_WIDGET_CHIAMATE, PH_CFG_WIDGET_RETE, PH_CFG_WIDGET_RIPRODUZIONE};
        for (int i = 0; i < 3; i++) {
            PhDskWidget *da_togliere = widget_tipo(v, TIPI[i]);
            VERIFICA(da_togliere != NULL, "widget da togliere non trovato");
            ph_dsk_widget_rimuovi(v->d, da_togliere->id);
        }
        VERIFICA(widget_tipo(v, PH_CFG_WIDGET_CHIAMATE) == NULL && widget_tipo(v, PH_CFG_WIDGET_RETE) == NULL &&
                     widget_tipo(v, PH_CFG_WIDGET_RIPRODUZIONE) == NULL,
                 "i widget non sono stati tolti");
        v->barriere = 0;
        barriera_chiedi(v); /* dopo le richieste di smettere di osservare */
        v->fase = 13;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 13: {
        ATTENDI(v, v->barriere == 1, "la barriera dopo la rimozione dei widget");
        memcpy(v->ev_prima, v->ev, sizeof v->ev);
        ph_finto_voce_modo(f, PH_FINTO_VOCE_CHIAMATE, PH_FINTO_MODO_ERRORE);
        ph_finto_voce_modo(f, PH_FINTO_VOCE_RETE, PH_FINTO_MODO_ERRORE);
        ph_finto_voce_modo(f, PH_FINTO_VOCE_MEDIA, PH_FINTO_MODO_ERRORE);
        ph_finto_rete_aereo(f, true);
        ph_finto_media_assente(f, true);
        barriera_chiedi(v);
        v->fase = 14;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 14: {
        ATTENDI(v, v->barriere == 2, "la barriera dopo i cambi senza osservatori");
        /* Un secondo giro: gli avvisi differiti, se ce ne fossero, sono arrivati. */
        if (v->barriere == 2) {
            barriera_chiedi(v);
            v->barriere = 3;
            return G_SOURCE_CONTINUE;
        }
        return G_SOURCE_CONTINUE;
    }
    default:
        return G_SOURCE_REMOVE;
    }
}

static gboolean passo(gpointer dati)
{
    Prova *v = dati;
    if (v->fase == 14 && v->barriere == 4) {
        VERIFICA(memcmp(v->ev, v->ev_prima, sizeof v->ev) == 0,
                 "dopo la rimozione dei widget i servizi pubblicano ancora (interesse non tolto)");
        sequenza_finita = true;
        g_message("nuovi: chiamate, rete e lettore: dati, stati vuoti e non disponibili, comandi, interesse tolto");
        return G_SOURCE_REMOVE;
    }
    return fotogramma(v);
}

void ph_demo_nuovi(PhShell *sh, const char *cosa)
{
    GtkWidget *radice = ph_shell_radice(sh);
    if (strcmp(cosa, "nuovi-attendi") == 0) {
        atexit(esci_senza_prova);
    } else if (strcmp(cosa, "nuovi-avvia") == 0 || strcmp(cosa, "nuovi-foto") == 0) {
        PhDesktop *d = g_object_get_data(G_OBJECT(radice), "ph-desktop");
        if (d == NULL)
            FALLISCI("desktop non trovato");
        Prova *v = g_new0(Prova, 1); /* vive quanto il processo */
        v->sh = sh;
        v->radice = radice;
        v->d = d;
        v->scadenza = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
        if (strcmp(cosa, "nuovi-foto") == 0) {
            /* Solo i tre widget (e l'app del telefono nota), nessuna verifica. */
            PhCfgTelefono *p = ph_dsk_profilo(d);
            if (p == NULL)
                return;
            ph_dsk_telefono_pacchetto_prova("it.finto.chiamate");
            metti_widget(p);
            ph_dsk_widget_ricostruisci(d);
            g_free(v);
            return;
        }
        g_timeout_add(10, passo, v);
    }
}
