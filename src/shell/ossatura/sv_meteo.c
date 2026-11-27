/* Servizi veri: meteo (CONTRATTI.md §18, src/meteo/meteo.h).
 *
 * UN SOLO PhMeteo per processo (i suoi tetti valgono per oggetto), creato alla
 * prima osservazione e chiuso da sv_meteo_chiudi PRIMA di chiudere il
 * collegamento: dopo, nessuna callback.
 *
 * GIRO. Un giro (un solo alla volta: chi ne chiede un altro mentre ce n'e'
 * uno in corso lo fa rifare alla fine) legge la posizione del telefono
 * (ph_dati_posizione, torna subito), decide la fonte con la regola «la
 * posizione del telefono vince sempre; la citta' scelta solo quando il
 * telefono non ne da' una», chiede ph_meteo_chiedi e lascia a un timer il
 * giro dopo (5 minuti; 15 s se il telefono ha avviato una ricerca in
 * sottofondo: UNA volta sola). Il pezzo meteo decide da se' se uscire in rete.
 *
 * RISERVATEZZA. Coordinate, localita' e testo cercato vivono solo in variabili
 * locali e nella memoria di questo oggetto: mai nei log, mai su disco (la
 * citta' scelta la salva il pezzo desktop nel profilo, non noi).
 *
 * LUCCHETTO. t->m non si tiene mai durante ph_dati_posizione ne' durante
 * ph_meteo_* (lo prendono le callback del pezzo meteo, e ph_meteo_chiudi le
 * aspetta). Chi chiama ph_meteo_* conta un uso (pm_usa/pm_lascia) e
 * sv_meteo_chiudi aspetta che tornino a zero prima di ph_meteo_chiudi. */
#include <stdio.h>
#include <string.h>

#include "sv_interno.h"

#if PH_OSSATURA_SERVIZI_VERI

int sv_prova_meteo_periodo_ms = 5 * 60 * 1000;
int sv_prova_meteo_ricerca_ms = 15 * 1000;
int sv_prova_meteo_permesso_s = 6 * 3600;
atomic_int sv_prova_meteo_sorgenti = 0;

#define SONDA_MS 100

/* CONTESTI PER RICHIESTA. Il pezzo meteo restituisce come `utente` il puntatore
 * dato a ph_meteo_chiedi / ph_meteo_cerca. Ogni richiesta ha il suo contesto e
 * una callback conta solo se il suo contesto e' ANCORA QUELLO CORRENTE
 * (confronto di puntatori): una vecchia si scarta. Garanzie usate (meteo.h): una
 * richiesta nuova sostituisce il destinatario e il sostituito non riceve nulla;
 * dopo ph_meteo_annulla / ph_meteo_cerca_annulla / ph_meteo_chiudi nessuna
 * callback arriva piu' e una in corso si aspetta; le callback di un tipo girano
 * su un solo thread, una dopo l'altra. Per questo un contesto sostituito o
 * annullato NON si libera subito (una callback in corso, o una richiesta
 * partita mentre si spegneva, puo' ancora leggerlo): passa nella lista
 * *_vecchie e si libera quando arriva la callback del contesto corrente (le
 * precedenti sono finite) o alla chiusura, dopo ph_meteo_chiudi. */
typedef struct SvRq {
    Sv *sv;
    PhSrvMeteoFonte fonte;
    char loc[128];
    guint64 gen;
} SvRq;

typedef struct SvCerca {
    Sv *sv;
    PhSrvLavoro *l;
} SvCerca;

/* Libera un contesto di richiesta: la localita' non resta in memoria. */
static void rq_libera(gpointer p)
{
    explicit_bzero(p, sizeof(SvRq));
    g_free(p);
}

/* t->m preso: la richiesta corrente (se c'e') diventa «vecchia». */
static void rq_ritira_locked(SvMeteo *t)
{
    if (t->rq != NULL)
        t->rq_vecchie = g_list_prepend(t->rq_vecchie, t->rq);
    t->rq = NULL;
}

/* ---- Uso di PhMeteo ----------------------------------------------------------- */
static PhMeteo *pm_usa(SvMeteo *t)
{
    PhMeteo *pm = NULL;
    g_mutex_lock(&t->m);
    if (!t->chiuso && t->pm != NULL) {
        t->uso++;
        pm = t->pm;
    }
    g_mutex_unlock(&t->m);
    return pm;
}

static void pm_lascia(SvMeteo *t)
{
    g_mutex_lock(&t->m);
    t->uso--;
    g_cond_broadcast(&t->cv);
    g_mutex_unlock(&t->m);
}

/* ---- Pubblicazione (t->m preso: le consegne sono asincrone) ------------------- */
static void pubblica(Sv *sv, PhSrvMeteoStato stato, const char *frase, const char *loc, PhSrvMeteoFonte fonte)
{
    SvMeteo *t = &sv->met;
    PhSrvMeteo m;
    memset(&m, 0, sizeof m);
    m.stato = stato;
    g_strlcpy(m.frase, frase != NULL ? frase : "", sizeof m.frase);
    g_strlcpy(m.localita, loc != NULL ? loc : "", sizeof m.localita);
    if (t->ha_dati) {
        m.ha_dati = true;
        m.dati = t->dati;
    }
    t->stato_pub = stato;
    ph_servizi_pubblica_meteo_fonte(sv->serv, &m, fonte);
}

/* Stato senza una richiesta in corso (posizione che manca, nessun telefono,
 * errore): i dati vecchi, se ci sono, restano con la loro localita' e fonte. */
static void pubblica_con_vecchi(Sv *sv, PhSrvMeteoStato stato, const char *frase)
{
    SvMeteo *t = &sv->met;
    pubblica(sv, stato, frase, t->ha_dati ? t->loc_dati : "", t->ha_dati ? t->fonte_dati : PH_SRV_METEO_FONTE_NESSUNA);
}

/* ---- Timer del prossimo giro -------------------------------------------------------- */
static void giro_chiedi(Sv *sv);

static void timer_libera(gpointer dati)
{
    atomic_fetch_sub(&sv_prova_meteo_sorgenti, 1);
    sv_rif_lascia(dati);
}

static gboolean timer_scaduto(gpointer dati)
{
    Sv *sv = dati;
    SvMeteo *t = &sv->met;
    g_mutex_lock(&t->m);
    if (t->timer != NULL && t->timer == g_main_current_source()) {
        g_source_unref(t->timer); /* il nostro riferimento: la sorgente si toglie da sola */
        t->timer = NULL;
    }
    bool vivo = !t->chiuso && t->osserva;
    if (vivo && t->attesa15) {
        t->attesa15 = false;
        t->rip15 = true;
    }
    g_mutex_unlock(&t->m);
    if (vivo)
        giro_chiedi(sv);
    return G_SOURCE_REMOVE;
}

static void timer_ferma_locked(SvMeteo *t)
{
    if (t->timer != NULL) {
        g_source_destroy(t->timer);
        g_source_unref(t->timer);
        t->timer = NULL;
    }
}

/* t->m preso. */
static void timer_programma_locked(Sv *sv, int ms)
{
    SvMeteo *t = &sv->met;
    timer_ferma_locked(t);
    GSource *src = g_timeout_source_new((guint)ms);
    sv_rif_prendi(sv);
    atomic_fetch_add(&sv_prova_meteo_sorgenti, 1);
    g_source_set_callback(src, timer_scaduto, sv, timer_libera);
    g_source_attach(src, NULL);
    t->timer = src;
}

/* ---- Frasi del pezzo meteo -------------------------------------------------------- */
/* Il pezzo meteo scrive le sue frasi in italiano (sono anche nel log); a schermo
 * va solo una frase tradotta: quelle riconosciute hanno la loro, tutte le altre
 * ricadono sulla frase generica di `ripiego`. Il dettaglio italiano va nel log. */
static const char *frase_meteo(const char *frase, const char *ripiego)
{
    static const struct {
        const char *meteo, *mostrata;
    } note[] = {
        {"aggiornamento interrotto: riprova piu' tardi", PH_T_("Aggiornamento interrotto: riprova piu' tardi.")},
        {"il servizio meteo non risponde", PH_T_("Il servizio meteo non risponde.")},
        {"certificato del servizio meteo non valido", PH_T_("Il certificato del servizio meteo non e' valido.")},
        {"la ricerca dei luoghi non risponde", PH_T_("La ricerca dei luoghi non risponde.")},
        {"troppe ricerche: riprova tra qualche minuto", PH_T_("Troppe ricerche: riprova tra qualche minuto.")},
    };
    if (frase == NULL || frase[0] == '\0')
        return ripiego;
    for (size_t i = 0; i < sizeof note / sizeof note[0]; i++)
        if (strcmp(frase, note[i].meteo) == 0)
            return ph_tv(note[i].mostrata);
    fprintf(stderr, "ossatura: meteo: %s\n", frase);
    return ripiego;
}

/* ---- Risposta del pezzo meteo (thread del pezzo: tornare in fretta) --------------- */
static void su_previsione(const PhMeteoPrevisione *p, const char *frase, void *utente)
{
    SvRq *c = utente;
    Sv *sv = c->sv; /* contesto vivo per le garanzie sopra (resta in *_vecchie finche' puo' servire) */
    SvMeteo *t = &sv->met;
    g_mutex_lock(&t->m);
    if (c != t->rq) { /* sostituita o annullata: non e' per noi */
        g_mutex_unlock(&t->m);
        return;
    }
    /* E' la risposta della richiesta corrente: le callback precedenti sono finite. */
    t->rq = NULL;
    g_list_free_full(t->rq_vecchie, rq_libera);
    t->rq_vecchie = NULL;
    if (t->chiuso || !t->osserva || c->gen != t->gen) {
        rq_libera(c);
        g_mutex_unlock(&t->m);
        return;
    }
    if (p != NULL) {
        t->ha_dati = true;
        t->dati = *p;
        g_strlcpy(t->loc_dati, c->loc, sizeof t->loc_dati);
        t->fonte_dati = c->fonte;
        pubblica(sv, PH_SRV_METEO_PRONTO, NULL, t->loc_dati, t->fonte_dati);
    } else {
        const char *f = frase_meteo(frase, ph_t("Previsioni del tempo non disponibili in questo momento."));
        if (t->ha_dati)
            pubblica_con_vecchi(sv, PH_SRV_METEO_NON_DISPONIBILE, f);
        else
            pubblica(sv, PH_SRV_METEO_NON_DISPONIBILE, f, c->loc, c->fonte);
    }
    rq_libera(c);
    g_mutex_unlock(&t->m);
}

/* ---- Il giro ---------------------------------------------------------------------------- */
static const char *frase_senza_posizione(PhDatiPosEsito esito)
{
    switch (esito) {
    case PH_DATI_POS_SPENTA:
        return ph_t("La posizione del telefono e' spenta.");
    case PH_DATI_POS_NEGATA:
        return ph_t("YASDE non ha il permesso di usare la posizione del telefono.");
    default:
        return ph_t("Il telefono non conosce ancora la sua posizione.");
    }
}

/* Crea l'unico PhMeteo (nessun lucchetto: crea un thread). Se non si riesce:
 * «non disponibile» e si riprova al giro dopo (timer), mai di corsa. false =
 * il giro finisce qui. Solo il giro la chiama (uno alla volta). */
static bool pm_crea(Sv *sv, guint64 gen)
{
    SvMeteo *t = &sv->met;
    PhErrore e = {{0}};
    PhMeteo *pm = ph_meteo_nuovo(&e);
    bool ok = false;
    g_mutex_lock(&t->m);
    if (pm != NULL && !t->chiuso && t->pm == NULL) {
        t->pm = pm;
        pm = NULL;
    }
    ok = t->pm != NULL;
    if (!ok && !t->chiuso && t->osserva && t->gen == gen) {
        pubblica_con_vecchi(sv, PH_SRV_METEO_NON_DISPONIBILE,
                            ph_t("Previsioni del tempo non disponibili in questo momento."));
        if (!t->attesa15)
            timer_programma_locked(sv, sv_prova_meteo_periodo_ms);
    }
    g_mutex_unlock(&t->m);
    if (pm != NULL) /* chiuso nel frattempo */
        ph_meteo_chiudi(pm);
    return ok;
}

/* UNICO punto di conversione: PhMeteoLuogo (profilo, ricerca) e' in centesimi di grado, il
 * telefono e ph_meteo_chiedi in millesimi. */
static int32_t centesimi_in_millesimi(int32_t centesimi)
{
    return centesimi * 10;
}

static void giro_una(Sv *sv)
{
    SvMeteo *t = &sv->met;
    g_mutex_lock(&t->m);
    if (t->chiuso || !t->osserva) {
        g_mutex_unlock(&t->m);
        return;
    }
    guint64 gen = t->gen;
    bool serve_pm = t->pm == NULL;
    g_mutex_unlock(&t->m);
    if (serve_pm && !pm_crea(sv, gen))
        return;
    g_mutex_lock(&t->m);
    if (t->chiuso || !t->osserva || t->gen != gen) {
        g_mutex_unlock(&t->m);
        return;
    }
    bool ripetizione = t->rip15; /* la rilettura dopo ricerca_avviata: senza permesso nuovo */
    t->rip15 = false;
    bool permessa = !ripetizione && (t->permesso_us == 0 || g_get_monotonic_time() - t->permesso_us >= (gint64)sv_prova_meteo_permesso_s * 1000000);
    g_mutex_unlock(&t->m);

    /* La posizione del telefono (nessun lucchetto: e' una richiesta al telefono). */
    PhDatiPosizione pos;
    memset(&pos, 0, sizeof pos);
    pos.esito = PH_DATI_POS_SCONOSCIUTA;
    bool telefono = false, letta = false;
    SvSessione *s = sv_sessione_usa(sv);
    if (s != NULL) {
        telefono = true;
        PhErrore e = {{0}};
        if (ph_dati_posizione(s->d, permessa, &pos, &e) == 0) {
            letta = true;
        } else {
            fprintf(stderr, "YASDE 2.3: posizione del telefono non letta\n"); /* mai dati */
            explicit_bzero(&pos, sizeof pos);
            pos.esito = PH_DATI_POS_SCONOSCIUTA;
        }
        sv_sessione_lascia(s);
    }

    int32_t lat_millesimi = 0, lon_millesimi = 0; /* l'unita' che vuole ph_meteo_chiedi */
    char loc[128] = "";
    PhSrvMeteoFonte fonte = PH_SRV_METEO_FONTE_NESSUNA;
    bool chiedi = false;
    bool avvia_15 = false;

    g_mutex_lock(&t->m);
    if (t->chiuso || !t->osserva || t->gen != gen) { /* nel frattempo: false, o chiusura */
        g_mutex_unlock(&t->m);
        explicit_bzero(&pos, sizeof pos);
        return;
    }
    if (letta && permessa)
        t->permesso_us = g_get_monotonic_time();
    if (telefono && pos.esito == PH_DATI_POS_NOTA) { /* il telefono vince sempre */
        lat_millesimi = pos.lat_millesimi; /* gia' in millesimi: com'e' */
        lon_millesimi = pos.lon_millesimi;
        g_strlcpy(loc, pos.localita, sizeof loc);
        fonte = PH_SRV_METEO_FONTE_TELEFONO;
        chiedi = true;
    } else if (t->ha_luogo) {
        lat_millesimi = centesimi_in_millesimi(t->luogo.lat_centesimi); /* la citta' e' in centesimi */
        lon_millesimi = centesimi_in_millesimi(t->luogo.lon_centesimi);
        g_strlcpy(loc, t->luogo.nome, sizeof loc);
        fonte = PH_SRV_METEO_FONTE_A_MANO;
        chiedi = true;
    }
    t->fonte_in_uso = fonte;
    if (letta && pos.ricerca_avviata && !ripetizione && !t->attesa15) {
        t->attesa15 = true;
        avvia_15 = true;
    }
    SvRq *ctx = NULL;
    if (chiedi) {
        /* Un contesto nuovo per questa richiesta: la precedente, se c'e', diventa vecchia. */
        rq_ritira_locked(t);
        ctx = g_new0(SvRq, 1);
        ctx->sv = sv;
        ctx->fonte = fonte;
        ctx->gen = gen;
        g_strlcpy(ctx->loc, loc, sizeof ctx->loc);
        t->rq = ctx;
        /* Da «senza posizione» / «nessun telefono» si torna ad «in attesa». */
        if (t->stato_pub == PH_SRV_METEO_SENZA_POSIZIONE || t->stato_pub == PH_SRV_METEO_NON_COLLEGATO)
            pubblica_con_vecchi(sv, PH_SRV_METEO_IN_ATTESA, NULL);
    } else if (!telefono) {
        pubblica_con_vecchi(sv, PH_SRV_METEO_NON_COLLEGATO,
                            ph_t("Nessun telefono collegato: senza posizione non ci sono previsioni."));
    } else {
        pubblica_con_vecchi(sv, PH_SRV_METEO_SENZA_POSIZIONE, frase_senza_posizione(pos.esito));
    }
    if (avvia_15)
        timer_programma_locked(sv, sv_prova_meteo_ricerca_ms);
    else if (!t->attesa15)
        timer_programma_locked(sv, sv_prova_meteo_periodo_ms);
    g_mutex_unlock(&t->m);
    explicit_bzero(&pos, sizeof pos);

    if (!chiedi) {
        explicit_bzero(&lat_millesimi, sizeof lat_millesimi);
        explicit_bzero(&lon_millesimi, sizeof lon_millesimi);
        return;
    }
    bool riuscita = false;
    PhMeteo *pm = pm_usa(t);
    if (pm != NULL) {
        PhErrore e = {{0}};
        riuscita = ph_meteo_chiedi(pm, lat_millesimi, lon_millesimi, su_previsione, ctx, &e) == 0;
        pm_lascia(t);
    }
    explicit_bzero(&lat_millesimi, sizeof lat_millesimi);
    explicit_bzero(&lon_millesimi, sizeof lon_millesimi);
    explicit_bzero(loc, sizeof loc);

    g_mutex_lock(&t->m);
    bool valido = !t->chiuso && t->osserva && t->gen == gen;
    if (valido && !riuscita)
        pubblica_con_vecchi(sv, PH_SRV_METEO_NON_DISPONIBILE,
                            ph_t("Previsioni del tempo non disponibili in questo momento."));
    if (!valido && t->rq == ctx)
        rq_ritira_locked(t);
    g_mutex_unlock(&t->m);
    if (!valido && riuscita) {
        /* Spento (o chiusura) mentre la richiesta partiva: niente collegamento. */
        pm = pm_usa(t);
        if (pm != NULL) {
            ph_meteo_annulla(pm);
            pm_lascia(t);
        }
    }
}

static void giro_lavoro(Sv *sv, void *dati)
{
    (void)dati;
    SvMeteo *t = &sv->met;
    for (;;) {
        giro_una(sv);
        g_mutex_lock(&t->m);
        if (t->rifai && !t->chiuso && t->osserva) {
            t->rifai = false;
            g_mutex_unlock(&t->m);
            continue;
        }
        t->rifai = false;
        t->in_giro = false;
        g_mutex_unlock(&t->m);
        return;
    }
}

static void giro_chiedi(Sv *sv)
{
    SvMeteo *t = &sv->met;
    g_mutex_lock(&t->m);
    if (t->chiuso || !t->osserva) {
        g_mutex_unlock(&t->m);
        return;
    }
    if (t->in_giro) {
        t->rifai = true;
        g_mutex_unlock(&t->m);
        return;
    }
    t->in_giro = true;
    g_mutex_unlock(&t->m);
    sv_lavoro(sv, sv->p_lavori, giro_lavoro, NULL);
}

/* ---- Ricerca in corso: sonda e chiusura ------------------------------------------------- */
static void sonda_ferma_locked(SvMeteo *t)
{
    if (t->sonda != NULL) {
        g_source_destroy(t->sonda);
        g_source_unref(t->sonda);
        t->sonda = NULL;
    }
}

/* Toglie la ricerca in corso (chi la toglie e' l'unico a chiuderne il lavoro): il
 * contesto passa fra i vecchi, una callback in corso puo' ancora leggerlo. t->m preso. */
static PhSrvLavoro *cerca_prendi_locked(SvMeteo *t)
{
    SvCerca *c = t->cerca;
    if (c == NULL)
        return NULL;
    PhSrvLavoro *l = c->l;
    t->cerca = NULL;
    t->cerca_vecchie = g_list_prepend(t->cerca_vecchie, c);
    sonda_ferma_locked(t);
    return l;
}

static void cerca_chiudi_errore(PhSrvLavoro *l, const char *frase)
{
    ph_servizi_lavoro_fatto(l, PH_SRV_ESITO_ERRORE, frase);
}

static gboolean sonda_corpo(gpointer dati)
{
    Sv *sv = dati;
    SvMeteo *t = &sv->met;
    PhSrvLavoro *l = NULL;
    g_mutex_lock(&t->m);
    /* Si guarda il lavoro finche' e' nello slot: finche' c'e', nessun altro lo chiude. */
    if (t->cerca != NULL && (t->chiuso || ph_servizi_lavoro_annullato(t->cerca->l)))
        l = cerca_prendi_locked(t);
    g_mutex_unlock(&t->m);
    if (l == NULL)
        return G_SOURCE_CONTINUE;
    PhMeteo *pm = pm_usa(t);
    if (pm != NULL) { /* da questo thread (GTK): mai dentro una callback del pezzo */
        ph_meteo_cerca_annulla(pm);
        pm_lascia(t);
    }
    cerca_chiudi_errore(l, NULL);
    return G_SOURCE_REMOVE;
}

/* Risposta della ricerca (thread del pezzo meteo). */
static void su_luoghi(const PhMeteoLuogo *v, size_t n, const char *frase, void *utente)
{
    SvCerca *c = utente; /* vivo per le garanzie in cima al file */
    Sv *sv = c->sv;
    SvMeteo *t = &sv->met;
    g_mutex_lock(&t->m);
    if (t->chiuso || c != t->cerca) { /* sostituita, annullata o chiusa: non e' per noi */
        g_mutex_unlock(&t->m);
        return;
    }
    PhSrvLavoro *l = c->l;
    t->cerca = NULL;
    sonda_ferma_locked(t);
    /* Risposta della ricerca corrente: le callback di ricerca precedenti sono finite. */
    g_list_free_full(t->cerca_vecchie, g_free);
    t->cerca_vecchie = NULL;
    g_free(c);
    g_mutex_unlock(&t->m);
    if (v != NULL)
        ph_servizi_lavoro_luoghi(l, PH_SRV_ESITO_OK, NULL, v, n);
    else
        cerca_chiudi_errore(l, frase_meteo(frase, ph_t("Ricerca dei luoghi non disponibile.")));
}

/* ---- Tabella ----------------------------------------------------------------------------- */
void sv_meteo_inizia(Sv *sv)
{
    SvMeteo *t = &sv->met;
    g_mutex_init(&t->m);
    g_cond_init(&t->cv);
}

void sv_meteo_libera(Sv *sv)
{
    SvMeteo *t = &sv->met;
    g_mutex_clear(&t->m);
    g_cond_clear(&t->cv);
}

void sv_meteo_osserva(void *b, bool osserva)
{
    if (b == NULL)
        return;
    Sv *sv = b;
    SvMeteo *t = &sv->met;
    if (osserva) {
        g_mutex_lock(&t->m);
        if (t->chiuso) {
            g_mutex_unlock(&t->m);
            return;
        }
        t->osserva = true;
        t->gen++;
        t->stato_pub = PH_SRV_METEO_IN_ATTESA; /* e' quello che il pezzo comune ha appena pubblicato */
        t->ha_dati = false;
        t->fonte_in_uso = PH_SRV_METEO_FONTE_NESSUNA;
        t->attesa15 = false;
        t->rip15 = false;
        g_mutex_unlock(&t->m);
        giro_chiedi(sv); /* il giro crea PhMeteo se manca (e riprova a ogni giro se non ci riesce) */
        return;
    }
    /* Spegnimento: via tutto, nessun collegamento finche' non torna true. */
    g_mutex_lock(&t->m);
    t->osserva = false;
    t->gen++;
    t->ha_dati = false;
    explicit_bzero(&t->dati, sizeof t->dati); /* dati e localita' non restano in memoria da spenti */
    explicit_bzero(t->loc_dati, sizeof t->loc_dati);
    t->fonte_in_uso = PH_SRV_METEO_FONTE_NESSUNA;
    t->attesa15 = false;
    t->rip15 = false;
    t->rifai = false;
    timer_ferma_locked(t);
    rq_ritira_locked(t);
    PhSrvLavoro *l = cerca_prendi_locked(t);
    g_mutex_unlock(&t->m);
    PhMeteo *pm = pm_usa(t);
    if (pm != NULL) {
        ph_meteo_annulla(pm); /* annulla anche la ricerca */
        pm_lascia(t);
    }
    /* Dopo annulla nessuna callback e' piu' in corso: i contesti vecchi si liberano, cosi'
     * le liste hanno un tetto. Quelli delle previsioni solo se nessun giro e' in volo (potrebbe
     * ancora essere dentro ph_meteo_chiedi con uno di essi). */
    g_mutex_lock(&t->m);
    if (!t->in_giro) {
        g_list_free_full(t->rq_vecchie, rq_libera);
        t->rq_vecchie = NULL;
    }
    g_list_free_full(t->cerca_vecchie, g_free);
    t->cerca_vecchie = NULL;
    g_mutex_unlock(&t->m);
    if (l != NULL)
        cerca_chiudi_errore(l, NULL);
}

void sv_meteo_localita(void *b, const PhMeteoLuogo *l)
{
    if (b == NULL)
        return;
    Sv *sv = b;
    SvMeteo *t = &sv->met;
    g_mutex_lock(&t->m);
    if (t->chiuso) {
        g_mutex_unlock(&t->m);
        return;
    }
    bool uguale = (l == NULL && !t->ha_luogo) ||
                  (l != NULL && t->ha_luogo && strcmp(l->nome, t->luogo.nome) == 0 &&
                   l->lat_centesimi == t->luogo.lat_centesimi && l->lon_centesimi == t->luogo.lon_centesimi);
    t->ha_luogo = l != NULL;
    if (l != NULL)
        t->luogo = *l;
    else
        memset(&t->luogo, 0, sizeof t->luogo);
    /* Si rifa' il giro se la citta' e' (o potrebbe diventare) quella in uso: con
     * il telefono che da' la posizione non cambia nulla. */
    bool rifa = !uguale && t->osserva && (t->fonte_in_uso != PH_SRV_METEO_FONTE_TELEFONO || t->in_giro);
    g_mutex_unlock(&t->m);
    if (rifa)
        giro_chiedi(sv);
}

void sv_meteo_cerca(void *b, PhSrvLavoro *l, const char *testo, const char *lingua)
{
    if (b == NULL) {
        cerca_chiudi_errore(l, ph_t("Ricerca dei luoghi non disponibile."));
        return;
    }
    Sv *sv = b;
    SvMeteo *t = &sv->met;
    g_mutex_lock(&t->m);
    if (t->chiuso || !t->osserva || t->pm == NULL) {
        g_mutex_unlock(&t->m);
        cerca_chiudi_errore(l, ph_t("Ricerca dei luoghi non disponibile."));
        return;
    }
    /* Una sola ricerca alla volta: il pezzo meteo non risponde a quella vecchia,
     * che quindi si chiude qui. */
    PhSrvLavoro *vecchia = cerca_prendi_locked(t);
    SvCerca *c = g_new0(SvCerca, 1);
    c->sv = sv;
    c->l = l;
    t->cerca = c;
    GSource *src = g_timeout_source_new(SONDA_MS);
    sv_rif_prendi(sv);
    atomic_fetch_add(&sv_prova_meteo_sorgenti, 1);
    g_source_set_callback(src, sonda_corpo, sv, timer_libera);
    g_source_attach(src, NULL);
    t->sonda = src;
    g_mutex_unlock(&t->m);
    if (vecchia != NULL)
        cerca_chiudi_errore(vecchia, NULL);

    bool ok = false;
    PhMeteo *pm = pm_usa(t);
    if (pm != NULL) {
        PhErrore e = {{0}};
        ok = ph_meteo_cerca(pm, testo, lingua, su_luoghi, c, &e) == 0;
        pm_lascia(t);
    }
    if (!ok) {
        g_mutex_lock(&t->m);
        PhSrvLavoro *mio = t->cerca == c ? cerca_prendi_locked(t) : NULL;
        g_mutex_unlock(&t->m);
        if (mio != NULL)
            cerca_chiudi_errore(mio, ph_t("Ricerca dei luoghi non disponibile."));
    }
}

void sv_meteo_sessione(Sv *sv)
{
    giro_chiedi(sv);
}

void sv_meteo_telefono(Sv *sv, const char *seriale)
{
    SvMeteo *t = &sv->met;
    g_mutex_lock(&t->m);
    if (t->chiuso || strcmp(t->seriale, seriale) == 0) {
        g_mutex_unlock(&t->m);
        return;
    }
    bool primo = t->seriale[0] == '\0';
    g_strlcpy(t->seriale, seriale, sizeof t->seriale);
    /* Un altro telefono: i dati, la posizione e la localita' del primo non si
     * mostrano al secondo. La citta' scelta e' del profilo: il desktop la rimanda
     * (ph_srv_meteo_localita) a ogni profilo caricato, subito dopo questa chiamata.
     * La prima volta niente da togliere: la citta' puo' essere stata impostata
     * prima ancora che ci fosse un telefono. */
    t->gen++; /* le risposte in volo del telefono di prima si scartano */
    t->ha_dati = false;
    explicit_bzero(&t->dati, sizeof t->dati);
    explicit_bzero(t->loc_dati, sizeof t->loc_dati);
    t->fonte_dati = PH_SRV_METEO_FONTE_NESSUNA;
    t->fonte_in_uso = PH_SRV_METEO_FONTE_NESSUNA;
    t->permesso_us = 0;
    t->attesa15 = false;
    t->rip15 = false;
    if (!primo) {
        t->ha_luogo = false;
        explicit_bzero(&t->luogo, sizeof t->luogo);
    }
    if (t->osserva)
        pubblica(sv, PH_SRV_METEO_IN_ATTESA, NULL, "", PH_SRV_METEO_FONTE_NESSUNA);
    g_mutex_unlock(&t->m);
}

void sv_meteo_chiudi(Sv *sv)
{
    SvMeteo *t = &sv->met;
    g_mutex_lock(&t->m);
    t->chiuso = true;
    t->osserva = false;
    t->gen++;
    timer_ferma_locked(t);
    rq_ritira_locked(t);
    PhSrvLavoro *l = cerca_prendi_locked(t);
    while (t->uso > 0)
        g_cond_wait(&t->cv, &t->m);
    PhMeteo *pm = t->pm;
    t->pm = NULL;
    explicit_bzero(&t->luogo, sizeof t->luogo);
    explicit_bzero(&t->dati, sizeof t->dati);
    explicit_bzero(t->loc_dati, sizeof t->loc_dati);
    explicit_bzero(t->seriale, sizeof t->seriale);
    g_mutex_unlock(&t->m);
    if (pm != NULL)
        ph_meteo_chiudi(pm); /* ferma la richiesta in volo e aspetta le callback in corso */
    /* Nessuna callback puo' piu' arrivare: i contesti si liberano. */
    g_mutex_lock(&t->m);
    g_list_free_full(t->rq_vecchie, rq_libera);
    t->rq_vecchie = NULL;
    g_list_free_full(t->cerca_vecchie, g_free);
    t->cerca_vecchie = NULL;
    g_mutex_unlock(&t->m);
    if (l != NULL)
        cerca_chiudi_errore(l, NULL);
}

/* ---- Solo per le prove -------------------------------------------------------------- */
int sv_prova_meteo_vecchie(Sv *sv)
{
    g_mutex_lock(&sv->met.m);
    int n = (int)g_list_length(sv->met.rq_vecchie);
    g_mutex_unlock(&sv->met.m);
    return n;
}

bool sv_prova_meteo_in_giro(Sv *sv)
{
    g_mutex_lock(&sv->met.m);
    bool r = sv->met.in_giro;
    g_mutex_unlock(&sv->met.m);
    return r;
}

/* Dati e nome della localita' dei dati sono a zero byte (da spenti non restano in memoria). */
bool sv_prova_meteo_azzerato(Sv *sv)
{
    SvMeteo *t = &sv->met;
    g_mutex_lock(&t->m);
    const unsigned char *d = (const unsigned char *)&t->dati;
    bool zero = !t->ha_dati && t->loc_dati[0] == '\0';
    for (size_t i = 0; i < sizeof t->dati && zero; i++)
        zero = d[i] == 0;
    for (size_t i = 0; i < sizeof t->loc_dati && zero; i++)
        zero = t->loc_dati[i] == 0;
    g_mutex_unlock(&t->m);
    return zero;
}

#endif
