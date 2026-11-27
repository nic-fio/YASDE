/* Prova del backend finto e del pezzo comune dei servizi, SENZA schermo:
 * gira con il solo ciclo di GLib (niente gtk_init). Verifica le regole di
 * servizi.h: avvisi sul thread principale, fotografie, annullamento, lavori,
 * icone, trasferimenti, flussi (lettura da un altro thread, ridimensiona,
 * pausa, interruzione), caduta e ritorno della connessione. */
#include <glib/gstdio.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "finto.h"
#include "interno.h"
#include "shell/servizi.h"
#include "shell/servizi_backend.h"
#include "shell/shell.h"

static int errori;
#define CONTROLLA(c)                                                                               \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: fallito: %s\n", __FILE__, __LINE__, #c);                       \
            errori++;                                                                              \
        }                                                                                          \
    } while (0)

static GThread *principale;
static PhFinto *FINTO;
static int avvisi_fuori_thread;

/* Gira il ciclo finche' cond() o il tempo scade. */
static bool aspetta(bool (*cond)(void *), void *dati, int ms)
{
    gint64 fine = g_get_monotonic_time() + (gint64)ms * 1000;
    while (!cond(dati)) {
        if (g_get_monotonic_time() > fine)
            return false;
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    return true;
}

static PhServizi *S;

static void ev_qualunque(PhServizi *s, PhSrvEvento ev, const char *arg, void *u)
{
    (void)s;
    (void)arg;
    if (g_thread_self() != principale)
        avvisi_fuori_thread++;
    int *conta = u;
    conta[ev]++;
}

static bool collegato(void *d)
{
    (void)d;
    PhSrvCollegamento c;
    ph_srv_collegamento(S, &c);
    return c.stato == PH_SRV_COLLEGATO;
}

static bool perso(void *d)
{
    (void)d;
    PhSrvCollegamento c;
    ph_srv_collegamento(S, &c);
    return c.stato == PH_SRV_PERSO;
}

static bool app_pronte(void *d)
{
    (void)d;
    size_t n;
    ph_srv_app(S, &n);
    return n > 0;
}

static bool profilo_arrivato(void *d)
{
    return ((int *)d)[PH_SRV_EV_PROFILO] >= 1;
}

/* Ogni fotografia arriva con una consegna sua: le notifiche dopo le app, in
 * un giro del ciclo successivo (mai dare per scontato che siano gia' li'). */
static bool notifiche_arrivate(void *d)
{
    return ((int *)d)[PH_SRV_EV_NOTIFICHE] >= 1;
}

static bool icona_arrivata(void *d)
{
    return ph_srv_icona(S, d) != NULL;
}

typedef struct {
    bool fatto;
    PhSrvEsito esito;
    size_t n;
    bool ha_download;
    int trovati;
    bool massimo;
    bool dimensione_vista;
} Esito;

static bool fatto(void *d) { return ((Esito *)d)->fatto; }

static void elenco_fatto(PhServizi *s, PhSrvEsito esito, const char *frase, const char *cartella,
                         const PhSrvVoce *voci, size_t n, void *u)
{
    (void)s;
    (void)frase;
    (void)cartella;
    Esito *e = u;
    e->fatto = true;
    e->esito = esito;
    e->n = n;
    for (size_t i = 0; i < n; i++)
        if (strcmp(voci[i].nome, "Download") == 0 && voci[i].cartella)
            e->ha_download = true;
    if (g_thread_self() != principale)
        avvisi_fuori_thread++;
}

static void cerca_trovati(PhServizi *s, const PhSrvTrovato *v, size_t n, void *u)
{
    Esito *e = u;
    for (size_t i = 0; i < n; i++)
        if (strstr(v[i].percorso, "pdf") != NULL) {
            e->trovati++;
            uint64_t byte = 0;
            if (ph_srv_trovato_dimensione(s, &v[i], &byte) && byte > 0)
                e->dimensione_vista = true;
        }
}

static void cerca_fine(PhServizi *s, PhSrvEsito esito, const char *frase, bool massimo, void *u)
{
    (void)s;
    (void)frase;
    Esito *e = u;
    e->fatto = true;
    e->esito = esito;
    e->massimo = massimo;
}

static bool tr_completato(void *d)
{
    (void)d;
    size_t n;
    const PhSrvTrasferimento *t = ph_srv_trasferimenti(S, &n);
    return n > 0 && t[n - 1].stato == PH_SRV_TR_COMPLETATO;
}

/* ---- Aggiunte del 23 nov ---- */
typedef struct {
    bool fatto;
    PhSrvInstEsito esito;
    int avanzamenti;
    uint64_t ultimo_fatti, ultimo_totale;
    bool ha_codice, ha_messaggio;
} Inst;

static void inst_av(PhServizi *s, uint64_t fatti, uint64_t totale, void *u)
{
    (void)s;
    Inst *i = u;
    i->avanzamenti++;
    i->ultimo_fatti = fatti;
    i->ultimo_totale = totale;
}

static void inst_fatto(PhServizi *s, const PhSrvInstRisultato *r, void *u)
{
    (void)s;
    Inst *i = u;
    CONTROLLA(!i->fatto); /* una volta sola */
    i->fatto = true;
    i->esito = r->esito;
    i->ha_codice = r->codice != NULL && r->codice[0] != '\0';
    i->ha_messaggio = r->messaggio != NULL && r->messaggio[0] != '\0';
    if (g_thread_self() != principale)
        avvisi_fuori_thread++;
}

static bool inst_finita(void *d) { return ((Inst *)d)->fatto; }

typedef struct {
    bool fatto;
    PhSrvPannelloEsito esito;
} Pan;

static void pan_fatto(PhServizi *s, PhSrvPannelloEsito esito, const char *frase, void *u)
{
    (void)s;
    (void)frase;
    Pan *p = u;
    p->fatto = true;
    p->esito = esito;
}

static bool pan_finito(void *d) { return ((Pan *)d)->fatto; }

typedef struct {
    bool fatto;
    PhSrvEsito esito;
    PhSrvMemoriaDettaglio d;
    size_t n;
    bool ultimo_giorno_intero, ordinati, ha_frase;
} Wid;

static void dett_fatto(PhServizi *s, PhSrvEsito esito, const char *frase, const PhSrvMemoriaDettaglio *d, void *u)
{
    (void)s;
    (void)frase;
    Wid *w = u;
    w->fatto = true;
    w->esito = esito;
    if (d != NULL)
        w->d = *d;
}

static void cal_fatto(PhServizi *s, PhSrvEsito esito, const char *frase, const PhSrvEventoCalendario *v, size_t n,
                      void *u)
{
    (void)s;
    Wid *w = u;
    w->fatto = true;
    w->esito = esito;
    w->n = n;
    w->ha_frase = frase != NULL;
    w->ordinati = true;
    for (size_t i = 0; i < n; i++) {
        CONTROLLA(v[i].titolo != NULL && v[i].fine_ms >= v[i].inizio_ms);
        if (i > 0 && v[i].inizio_ms < v[i - 1].inizio_ms)
            w->ordinati = false;
    }
    w->ultimo_giorno_intero = n > 0 && v[n - 1].tutto_il_giorno && v[n - 1].inizio_ms % 86400000u == 0;
}

static bool wid_finito(void *d) { return ((Wid *)d)->fatto; }

static bool tr_verso_annullato(void *d)
{
    (void)d;
    size_t n;
    const PhSrvTrasferimento *t = ph_srv_trasferimenti(S, &n);
    int annullati = 0;
    for (size_t i = 0; i < n; i++) {
        if (t[i].verso == PH_SRV_INVIO && (t[i].stato == PH_SRV_TR_ATTESA || t[i].stato == PH_SRV_TR_CORSO))
            return false;
        if (t[i].verso == PH_SRV_INVIO && t[i].stato == PH_SRV_TR_ANNULLATO)
            annullati++;
    }
    return annullati >= 2;
}

static bool ricezione_annullata(void)
{
    size_t n;
    const PhSrvTrasferimento *t = ph_srv_trasferimenti(S, &n);
    for (size_t i = 0; i < n; i++)
        if (t[i].verso == PH_SRV_RICEZIONE && t[i].stato == PH_SRV_TR_ANNULLATO)
            return true;
    return false;
}

static unsigned gen_attesa;
static bool generazione(void *d)
{
    (void)d;
    return ph_srv_notifiche_generazione(S) >= gen_attesa;
}

static bool schermo_spento(void *d)
{
    PhSrvCollegamento c;
    ph_srv_collegamento(S, &c);
    return c.schermo_spento == *(bool *)d;
}

static bool senza_meteo(void *d)
{
    (void)d;
    size_t n;
    ph_srv_app(S, &n);
    return n > 0 && ph_srv_app_cerca(S, "it.finto.meteo") == NULL;
}

typedef struct {
    int conta;
    char *percorso;
    bool png;
    PhSrvEsito esito;
} Scatto;

static void scatto_fatto(PhServizi *s, PhSrvEsito esito, const char *frase, const char *percorso, GBytes *png,
                         void *u)
{
    (void)s;
    (void)frase;
    Scatto *sc = u;
    sc->conta++;
    sc->esito = esito;
    g_free(sc->percorso);
    sc->percorso = g_strdup(percorso);
    sc->png = png != NULL && g_bytes_get_size(png) > 8;
}

static bool scatto_finito(void *d) { return ((Scatto *)d)->conta > 0; }

typedef struct {
    int iniziata, finita, errore;
} Reg;

static void reg_avviso(PhSrvFlusso *f, const PhSrvRegDati *d, void *u)
{
    (void)f;
    Reg *r = u;
    if (d->evento == PH_SRV_REG_INIZIATA)
        r->iniziata++;
    else if (d->evento == PH_SRV_REG_FINITA)
        r->finita++;
    else
        r->errore++;
}

static bool reg_iniziata(void *d) { return ((Reg *)d)->iniziata > 0; }
static bool reg_finita(void *d) { return ((Reg *)d)->finita > 0; }
static bool reg_errore(void *d) { return ((Reg *)d)->errore > 0; }

/* ---- Prima connessione ---- */
typedef struct {
    bool fatto;
    PhSrvAbbEsito esito;
    bool frase;
} Abb;

static void abb_fatto(PhServizi *s, PhSrvAbbEsito esito, const char *frase, void *u)
{
    (void)s;
    Abb *a = u;
    CONTROLLA(!a->fatto);
    a->fatto = true;
    a->esito = esito;
    a->frase = frase != NULL && frase[0] != '\0';
}

static bool abb_finito(void *d) { return ((Abb *)d)->fatto; }

static bool stato_e(void *d)
{
    PhSrvCollegamento c;
    ph_srv_collegamento(S, &c);
    return c.stato == *(PhSrvStato *)d;
}

/* Un secondo telefono finto che parte non abbinato, e uno mai abbinato. */
static void prova_prima_connessione(void)
{
    PhServizi *vecchio = S;
    PhFintoOpzioni o = {.notifiche_ogni_s = -1, .ritardo_ms = 10, .inizio = PH_FINTO_INIZIO_NON_ABBINATO};
    PhFinto *f = ph_finto_nuovo(&o);
    S = ph_finto_servizi(f);
    PhSrvStato na = PH_SRV_NON_ABBINATO, co = PH_SRV_COLLEGATO, ce = PH_SRV_CERCO;
    CONTROLLA(aspetta(stato_e, &na, 3000));
    PhSrvCollegamento c;
    ph_srv_collegamento(S, &c);
    CONTROLLA(c.telefoni_noti == 0 && c.telefoni_trovati == 1);
    CONTROLLA(ph_srv_codice_valido("123456") && !ph_srv_codice_valido("12345") &&
              !ph_srv_codice_valido("1234567") && !ph_srv_codice_valido("12 456") && !ph_srv_codice_valido(NULL));
    /* Richieste non valide: rifiutate, senza toccare il telefono. */
    Abb r1 = {0}, r2 = {0};
    ph_srv_abbina(S, "12345", NULL, 0, NULL, abb_fatto, &r1);
    ph_srv_abbina(S, "123456", "192.168.0.9", 0, NULL, abb_fatto, &r2);
    CONTROLLA(!r1.fatto); /* mai dentro la chiamata */
    CONTROLLA(aspetta(abb_finito, &r1, 3000) && r1.esito == PH_SRV_ABB_RIFIUTATA);
    CONTROLLA(aspetta(abb_finito, &r2, 3000) && r2.esito == PH_SRV_ABB_RIFIUTATA);
    /* Codice sbagliato; durante l'attesa un secondo abbinamento e' rifiutato. */
    Abb a1 = {0}, a2 = {0}, a3 = {0}, a4 = {0};
    ph_srv_abbina(S, "999999", NULL, 0, NULL, abb_fatto, &a1);
    CONTROLLA(ph_srv_abbinamento_in_corso(S));
    ph_srv_abbina(S, "123456", NULL, 0, NULL, abb_fatto, &a2);
    CONTROLLA(aspetta(abb_finito, &a2, 3000) && a2.esito == PH_SRV_ABB_RIFIUTATA);
    CONTROLLA(aspetta(abb_finito, &a1, 3000) && a1.esito == PH_SRV_ABB_FALLITO && a1.frase);
    CONTROLLA(!ph_srv_abbinamento_in_corso(S));
    /* Schermata del codice non aperta (000000), poi il codice giusto con l'indirizzo manuale. */
    ph_srv_abbina(S, "000000", NULL, 0, NULL, abb_fatto, &a3);
    CONTROLLA(aspetta(abb_finito, &a3, 3000) && a3.esito == PH_SRV_ABB_FALLITO);
    ph_srv_abbina(S, "123456", "192.168.0.9", 37000, NULL, abb_fatto, &a4);
    CONTROLLA(aspetta(abb_finito, &a4, 3000) && a4.esito == PH_SRV_ABB_RIUSCITO);
    CONTROLLA(aspetta(stato_e, &co, 3000));
    ph_srv_collegamento(S, &c);
    CONTROLLA(c.telefoni_noti >= 1);
    ph_finto_libera(f);
    for (int i = 0; i < 20; i++)
        g_main_context_iteration(NULL, FALSE);

    /* Mai abbinato e nessun telefono in rete: resta in CERCO; quando compare,
     * NON_ABBINATO. */
    o.inizio = PH_FINTO_INIZIO_CERCA_NUOVO;
    f = ph_finto_nuovo(&o);
    S = ph_finto_servizi(f);
    CONTROLLA(aspetta(stato_e, &ce, 3000));
    Abb a5 = {0};
    ph_srv_abbina(S, "123456", NULL, 0, NULL, abb_fatto, &a5); /* niente da abbinare */
    CONTROLLA(aspetta(abb_finito, &a5, 3000) && a5.esito == PH_SRV_ABB_RIFIUTATA);
    ph_srv_riconnetti_ora(S);
    ph_srv_collegamento(S, &c);
    CONTROLLA(c.stato == PH_SRV_CERCO && c.telefoni_noti == 0);
    ph_finto_telefono_raggiungibile(f, true);
    CONTROLLA(aspetta(stato_e, &na, 3000));
    ph_finto_libera(f);
    for (int i = 0; i < 20; i++)
        g_main_context_iteration(NULL, FALSE);
    S = vecchio;
}

/* ---- Flusso ---- */
typedef struct {
    PhSrvFlusso *f;
    atomic_int fotogrammi;
    atomic_int ultima_w;
    atomic_int zeri;
    atomic_bool fuori;
} Lettura;

static gpointer leggi(gpointer p)
{
    Lettura *l = p;
    for (;;) {
        PhFotogramma fr;
        int r = ph_srv_flusso_leggi(l->f, &fr, 50);
        if (r < 0)
            break;
        if (r == 0) {
            atomic_fetch_add(&l->zeri, 1);
            continue;
        }
        if (fr.tipo != PH_FOTO_MEMORIA || fr.dati == NULL || fr.passo < (size_t)fr.larghezza * 4)
            atomic_store(&l->fuori, true);
        atomic_store(&l->ultima_w, fr.larghezza);
        atomic_fetch_add(&l->fotogrammi, 1);
        ph_fotogramma_rilascia(&fr);
    }
    return NULL;
}

static int misure_dichiarate, ultima_misura_w;
static void avviso_flusso(PhSrvFlusso *f, const PhSrvFlussoDati *d, void *u)
{
    (void)f;
    (void)u;
    if (g_thread_self() != principale)
        avvisi_fuori_thread++;
    if (d->evento == PH_SRV_FL_MISURA) {
        misure_dichiarate++;
        ultima_misura_w = d->larghezza;
    }
}

static int zeri_attesi;
static bool zeri(void *d)
{
    return atomic_load(&((Lettura *)d)->zeri) >= zeri_attesi;
}

static bool almeno(void *d)
{
    Lettura *l = d;
    return atomic_load(&l->fotogrammi) >= 10;
}

static int densita_voluta, misure_prima;
static bool densita_arrivata(void *d)
{
    (void)d;
    return ph_finto_ultima_densita(FINTO) == densita_voluta && misure_dichiarate > misure_prima;
}

static bool misura_480(void *d)
{
    Lettura *l = d;
    return ultima_misura_w == 480 && atomic_load(&l->ultima_w) == 480;
}

/* ---- Widget del 23 nov pomeriggio ------------------------------------------- */
typedef struct {
    int chiamate;
    PhSrvEsito esito;
    bool ha_frase;
    /* calendario */
    size_t n;
    bool ordinati, troncato, ha_trasferta, durata_due_giorni, tutti_a_mezzanotte, ora_sbagliata;
    int domeniche;
    int per_giorno[32]; /* eventi con ora che iniziano quel giorno (locale) */
    char app[64];
    /* sveglia */
    bool presente;
    uint64_t ora_ms;
    /* batteria */
    PhSrvBatteria bat;
} W2;

static bool w2_finito(void *d) { return ((W2 *)d)->chiamate > 0; }

static void w2_comune(W2 *w, PhSrvEsito esito, const char *frase, const char *app)
{
    w->chiamate++;
    w->esito = esito;
    w->ha_frase = frase != NULL;
    g_strlcpy(w->app, app != NULL ? app : "", sizeof w->app);
    if (g_thread_self() != principale)
        avvisi_fuori_thread++;
}

static void mese_fatto(PhServizi *s, PhSrvEsito esito, const char *frase, const PhSrvEventoCalendario *v, size_t n,
                       bool troncato, const char *app, void *u)
{
    (void)s;
    W2 *w = u;
    w2_comune(w, esito, frase, app);
    w->n = n;
    w->troncato = troncato;
    w->ordinati = true;
    w->tutti_a_mezzanotte = true;
    for (size_t i = 0; i < n; i++) {
        CONTROLLA(v[i].titolo != NULL && v[i].fine_ms >= v[i].inizio_ms);
        if (i > 0 && v[i].inizio_ms < v[i - 1].inizio_ms)
            w->ordinati = false;
        if (strcmp(v[i].titolo, "Business trip") == 0 && !v[i].tutto_il_giorno &&
            v[i].fine_ms - v[i].inizio_ms > 2u * 86400000u)
            w->ha_trasferta = true;
        if (v[i].tutto_il_giorno) {
            if (v[i].inizio_ms % 86400000u != 0 || v[i].fine_ms % 86400000u != 0)
                w->tutti_a_mezzanotte = false;
            if (v[i].fine_ms - v[i].inizio_ms == 2u * 86400000u)
                w->durata_due_giorni = true;
            if (strcmp(v[i].titolo, "Family lunch") == 0)
                w->domeniche++;
        } else {
            GDateTime *t = g_date_time_new_from_unix_local((gint64)(v[i].inizio_ms / 1000u));
            w->per_giorno[g_date_time_get_day_of_month(t)]++;
            /* L'ora locale non scivola col cambio dell'ora legale. */
            if (strcmp(v[i].titolo, "Team meeting") == 0 &&
                (g_date_time_get_hour(t) != 9 || g_date_time_get_minute(t) != 30))
                w->ora_sbagliata = true;
            g_date_time_unref(t);
        }
    }
}

static void sveglia_fatta(PhServizi *s, PhSrvEsito esito, const char *frase, bool presente, uint64_t ora_ms,
                          const char *pacchetto, void *u)
{
    (void)s;
    W2 *w = u;
    w2_comune(w, esito, frase, pacchetto);
    w->presente = presente;
    w->ora_ms = ora_ms;
}

static void batteria_fatta(PhServizi *s, PhSrvEsito esito, const char *frase, const PhSrvBatteria *b, void *u)
{
    (void)s;
    W2 *w = u;
    w2_comune(w, esito, frase, NULL);
    CONTROLLA((esito == PH_SRV_ESITO_OK) == (b != NULL));
    if (b != NULL)
        w->bat = *b;
}

static void fatto_w2(PhServizi *s, PhSrvEsito esito, const char *frase, void *u)
{
    (void)s;
    w2_comune(u, esito, frase, NULL);
}

static int *CONTA;

static bool ctl_noto(void *d)
{
    (void)d;
    PhSrvControlli c;
    ph_srv_controlli(S, &c);
    return c.noto;
}

/* d = int[2]: quale (0 suoneria, 1 non disturbare, 2 torcia) e valore atteso. */
static bool ctl_uguale(void *d)
{
    const int *x = d;
    PhSrvControlli c;
    ph_srv_controlli(S, &c);
    return (x[0] == 0 ? c.suoneria : x[0] == 1 ? c.non_disturbare : c.torcia) == x[1];
}

static bool ctl_trova(void *d)
{
    PhSrvControlli c;
    ph_srv_controlli(S, &c);
    return c.trova_in_corso == *(bool *)d;
}

static bool ctl_torcia_possibile(void *d)
{
    PhSrvControlli c;
    ph_srv_controlli(S, &c);
    return c.puo_torcia == *(bool *)d;
}

static bool ctl_tutto_disponibile(void *d)
{
    PhSrvControlli c;
    ph_srv_controlli(S, &c);
    bool tutto = c.puo_suoneria && c.puo_non_disturbare && c.puo_torcia && c.puo_trova && c.suoneria >= 0 &&
                 c.non_disturbare >= 0 && c.torcia >= 0;
    return tutto == *(bool *)d;
}

static bool ctl_nulla_disponibile(void *d)
{
    (void)d;
    PhSrvControlli c;
    ph_srv_controlli(S, &c);
    return !c.puo_suoneria && !c.puo_non_disturbare && !c.puo_torcia && !c.puo_trova &&
           c.suoneria == PH_SRV_NON_DISPONIBILE && c.non_disturbare == PH_SRV_NON_DISPONIBILE &&
           c.torcia == PH_SRV_NON_DISPONIBILE;
}

static bool ctl_eventi_oltre(void *d) { return CONTA[PH_SRV_EV_CONTROLLI] > *(int *)d; }

/* Barriera: una richiesta qualunque (sveglia) torna dopo tutto cio' che e' stato
 * messo in coda prima di lei sul thread del finto: comandi e richieste hanno la
 * stessa priorita' e GLib serve le sorgenti pronte nell'ordine di aggiunta (anche
 * se il thread era fermo oltre il ritardo; lo prova prova_ordine). */
static void barriera(void)
{
    W2 w = {0};
    ph_srv_tel_sveglia(S, NULL, sveglia_fatta, &w);
    CONTROLLA(aspetta(w2_finito, &w, 3000));
}

static uint64_t loc_ms(int y, int m, int d)
{
    GDateTime *t = g_date_time_new_local(y, m, d, 0, 0, 0);
    uint64_t ms = (uint64_t)g_date_time_to_unix(t) * 1000u;
    g_date_time_unref(t);
    return ms;
}

/* Thread del finto tenuto fermo: rilascia quando `rilascia` diventa true (o dopo 5 s). */
typedef struct {
    atomic_bool fermo, rilascia;
} Fermo;

static void fermo_fn(PhFinto *f, void *d)
{
    (void)f;
    Fermo *x = d;
    atomic_store(&x->fermo, true);
    gint64 fine = g_get_monotonic_time() + 5000000;
    while (!atomic_load(&x->rilascia) && g_get_monotonic_time() < fine)
        g_usleep(500);
}

static bool fermo_preso(void *d) { return atomic_load(&((Fermo *)d)->fermo); }
static bool tempo_trascorso(void *d) { return g_get_monotonic_time() >= *(gint64 *)d; }

/* Un comando dato PRIMA di una richiesta vale per la richiesta anche se il thread
 * del finto resta fermo oltre il ritardo della richiesta. */
static void prova_ordine(PhFinto *f)
{
    Fermo x;
    atomic_init(&x.fermo, false);
    atomic_init(&x.rilascia, false);
    finto_lavora(f, 0, fermo_fn, &x, NULL);
    CONTROLLA(aspetta(fermo_preso, &x, 3000));
    ph_finto_sveglia_assente(f, true); /* comando */
    W2 w = {0};
    ph_srv_tel_sveglia(S, NULL, sveglia_fatta, &w); /* richiesta con ritardo (10 ms) */
    gint64 oltre = g_get_monotonic_time() + 80000; /* il ritardo e' davvero passato */
    CONTROLLA(aspetta(tempo_trascorso, &oltre, 1000));
    atomic_store(&x.rilascia, true);
    CONTROLLA(aspetta(w2_finito, &w, 3000) && w.esito == PH_SRV_ESITO_OK && !w.presente);
    ph_finto_sveglia_assente(f, false);
    barriera();
}

/* ---- Meteo finto ------------------------------------------------------------- */
/* Risposte della ricerca dei luoghi. */
typedef struct {
    int chiamate;
    PhSrvEsito esito;
    bool ha_frase, v_non_null, sporco;
    size_t n;
    char primo[128], dettaglio[128];
} Lu;

static bool lu_finito(void *d) { return ((Lu *)d)->chiamate > 0; }

static void luoghi_fatti(PhServizi *s, PhSrvEsito esito, const char *frase, const PhMeteoLuogo *v, size_t n, void *u)
{
    (void)s;
    Lu *l = u;
    l->chiamate++;
    l->esito = esito;
    l->ha_frase = frase != NULL;
    l->v_non_null = v != NULL;
    l->n = n;
    if (v != NULL && n > 0) {
        g_strlcpy(l->primo, v[0].nome, sizeof l->primo);
        g_strlcpy(l->dettaglio, v[0].dettaglio, sizeof l->dettaglio);
        for (size_t i = 0; i < n; i++) {
            /* Mai byte di controllo ne' UTF-8 non valido verso lo schermo, stringhe sempre terminate. */
            if (memchr(v[i].nome, '\0', sizeof v[i].nome) == NULL || memchr(v[i].dettaglio, '\0', sizeof v[i].dettaglio) == NULL ||
                !g_utf8_validate(v[i].nome, -1, NULL) || !g_utf8_validate(v[i].dettaglio, -1, NULL))
                l->sporco = true;
            for (const char *c = v[i].nome; *c != '\0'; c++)
                if ((unsigned char)*c < 0x20 || *c == 0x7f)
                    l->sporco = true;
            for (const char *c = v[i].dettaglio; *c != '\0'; c++)
                if ((unsigned char)*c < 0x20 || *c == 0x7f)
                    l->sporco = true;
        }
        for (size_t i = 0; i < n; i++)
            CONTROLLA(v[i].nome[0] != '\0' && v[i].lat_centesimi >= -9000 && v[i].lat_centesimi <= 9000 &&
                      v[i].lon_centesimi >= -18000 && v[i].lon_centesimi <= 18000);
    }
    if (g_thread_self() != principale)
        avvisi_fuori_thread++;
}

static PhServizi *S_METEO; /* i servizi che le condizioni qui sotto guardano */

static bool met_stato(void *d)
{
    return ph_srv_meteo(S_METEO)->stato == *(PhSrvMeteoStato *)d;
}

static bool met_codice(void *d)
{
    const PhSrvMeteo *m = ph_srv_meteo(S_METEO);
    return m->stato == PH_SRV_METEO_PRONTO && m->dati.adesso.codice == *(int *)d;
}

static bool met_temperatura(void *d)
{
    const PhSrvMeteo *m = ph_srv_meteo(S_METEO);
    return m->stato == PH_SRV_METEO_PRONTO && fabs(m->dati.adesso.temperatura - *(double *)d) < 0.05;
}

static bool met_eventi_oltre(void *d) { return CONTA[PH_SRV_EV_METEO] > *(int *)d; }

static bool met_pronto_con_dati(PhFinto *f, int64_t *ricevuta, double *temperatura)
{
    (void)f;
    const PhSrvMeteo *m = ph_srv_meteo(S_METEO);
    if (m->stato != PH_SRV_METEO_PRONTO || !m->ha_dati)
        return false;
    *ricevuta = m->dati.ricevuta;
    *temperatura = m->dati.adesso.temperatura;
    return true;
}

static void prova_meteo(PhFinto *f, int *conta)
{
    CONTA = conta;
    S_METEO = S;
    PhSrvMeteoStato st_spento = PH_SRV_METEO_SPENTO, st_attesa = PH_SRV_METEO_IN_ATTESA,
                    st_pronto = PH_SRV_METEO_PRONTO, st_nondisp = PH_SRV_METEO_NON_DISPONIBILE,
                    st_senzapos = PH_SRV_METEO_SENZA_POSIZIONE, st_noncoll = PH_SRV_METEO_NON_COLLEGATO;
    const PhSrvMeteo *m = ph_srv_meteo(S);
    CONTROLLA(m->stato == PH_SRV_METEO_SPENTO && !m->ha_dati);

    /* Nessuna pubblicazione senza osservatori, qualunque cosa cambi. */
    int prima = conta[PH_SRV_EV_METEO];
    ph_finto_meteo_modo(f, PH_FINTO_METEO_NON_DISPONIBILE);
    ph_finto_meteo_condizioni(f, 3);
    ph_finto_meteo_nuovi_dati(f);
    ph_finto_meteo_condizioni(f, 0);
    ph_finto_meteo_modo(f, PH_FINTO_METEO_PRONTO);
    barriera();
    CONTROLLA(conta[PH_SRV_EV_METEO] == prima && ph_srv_meteo(S)->stato == PH_SRV_METEO_SPENTO);

    /* Osservando (un arrivo di dati nuovi c'e' gia' stato: 14,5 + 0,1): prima IN_ATTESA, poi (dopo ~300 ms, non subito) PRONTO. */
    gint64 t0 = g_get_monotonic_time();
    ph_srv_meteo_osserva(S, true);
    CONTROLLA(aspetta(met_stato, &st_attesa, 3000));
    CONTROLLA(aspetta(met_stato, &st_pronto, 3000));
    CONTROLLA(g_get_monotonic_time() - t0 >= 200 * 1000);
    m = ph_srv_meteo(S);
    CONTROLLA(m->ha_dati && strcmp(m->localita, "Sample City") == 0 && m->frase[0] == '\0');
    const PhMeteoPrevisione *p = &m->dati;
    CONTROLLA(fabs(p->adesso.temperatura - 14.6) < 0.01 && p->adesso.codice == 2 && p->adesso.giorno == 1);
    CONTROLLA(p->n_ore == PH_METEO_ORE_MAX && p->n_giorni == PH_METEO_GIORNI_MAX && p->scarto_utc_s == 3600);
    CONTROLLA(llabs(p->ricevuta - (int64_t)(g_get_real_time() / 1000000)) < 30 && p->ore[0].ora % 3600 == 0);
    int visti = 0, codici[] = {0, 3, 61, 73, 95, 45};
    bool sotto_zero = false, notte = false, giorno = false;
    for (size_t i = 0; i < p->n_giorni; i++) {
        for (size_t k = 0; k < G_N_ELEMENTS(codici); k++)
            if (p->giorni[i].codice == codici[k])
                visti |= 1 << k;
        if (p->giorni[i].minima < 0)
            sotto_zero = true;
        CONTROLLA(p->giorni[i].massima > p->giorni[i].minima && p->giorni[i].alba > p->giorni[i].giorno &&
                  p->giorni[i].tramonto > p->giorni[i].alba);
    }
    for (size_t i = 0; i < p->n_ore; i++) {
        CONTROLLA(!isnan(p->ore[i].temperatura) && p->ore[i].codice >= 0);
        if (i > 0)
            CONTROLLA(p->ore[i].ora == p->ore[i - 1].ora + 3600);
        notte = notte || p->ore[i].giorno == 0;
        giorno = giorno || p->ore[i].giorno == 1;
    }
    CONTROLLA(visti == 0x3f && sotto_zero && notte && giorno);

    /* Contatore degli osservatori: il secondo non cambia nulla, il primo che smette neanche. */
    prima = conta[PH_SRV_EV_METEO];
    ph_srv_meteo_osserva(S, true);
    ph_srv_meteo_osserva(S, false);
    barriera();
    CONTROLLA(conta[PH_SRV_EV_METEO] == prima && ph_srv_meteo(S)->stato == PH_SRV_METEO_PRONTO);

    /* Condizioni diverse (codice, giorno/notte, sotto zero, valori non noti). */
    static const struct {
        int scenario, codice, giorno;
    } SC[] = {{3, 73, 1}, {1, 0, 0}, {2, 63, 1}, {4, 95, 1}, {5, 45, 0}};
    for (size_t i = 0; i < G_N_ELEMENTS(SC); i++) {
        ph_finto_meteo_condizioni(f, SC[i].scenario);
        CONTROLLA(aspetta(met_codice, (void *)&SC[i].codice, 3000));
        CONTROLLA(ph_srv_meteo(S)->dati.adesso.giorno == SC[i].giorno);
        if (SC[i].scenario == 3)
            CONTROLLA(ph_srv_meteo(S)->dati.adesso.temperatura < 0 && ph_srv_meteo(S)->dati.adesso.percepita < 0);
    }
    int senza_codice = -1;
    ph_finto_meteo_condizioni(f, 6);
    CONTROLLA(aspetta(met_codice, &senza_codice, 3000));
    p = &ph_srv_meteo(S)->dati;
    CONTROLLA(isnan(p->adesso.temperatura) && isnan(p->adesso.umidita) && p->adesso.giorno == -1);
    CONTROLLA(isnan(p->giorni[0].minima) && p->giorni[0].alba == 0 && p->giorni[0].tramonto == 0);
    CONTROLLA(isnan(p->ore[0].temperatura) && p->ore[0].codice == -1 && !isnan(p->ore[1].temperatura));
    int codice_base = 2;
    ph_finto_meteo_condizioni(f, 0);
    CONTROLLA(aspetta(met_codice, &codice_base, 3000));
    ph_finto_meteo_condizioni(f, 99); /* fuori elenco: ignorato */
    barriera();
    CONTROLLA(ph_srv_meteo(S)->dati.adesso.codice == 2);

    /* Dati nuovi: la temperatura sale di 0,1 C. */
    double t1 = 14.7;
    ph_finto_meteo_nuovi_dati(f);
    CONTROLLA(aspetta(met_temperatura, &t1, 3000));

    /* Ripubblicazione periodica (qui ogni secondo). */
    prima = conta[PH_SRV_EV_METEO];
    ph_finto_meteo_intervallo(f, 1);
    int due = prima + 2;
    CONTROLLA(aspetta(met_eventi_oltre, &due, 5000));

    /* Servizio muto: dati vecchi con la loro ora; poi torna. */
    int64_t ricevuta = 0;
    double temp = 0;
    CONTROLLA(met_pronto_con_dati(f, &ricevuta, &temp));
    ph_finto_meteo_intervallo(f, 0);
    ph_finto_meteo_modo(f, PH_FINTO_METEO_NON_DISPONIBILE);
    CONTROLLA(aspetta(met_stato, &st_nondisp, 3000));
    m = ph_srv_meteo(S);
    CONTROLLA(m->frase[0] != '\0' && m->ha_dati && fabs(m->dati.adesso.temperatura - temp) < 1e-9);
    CONTROLLA(m->dati.ricevuta >= ricevuta && m->dati.ricevuta - ricevuta < 3);
    int64_t vecchia = m->dati.ricevuta;
    ph_finto_meteo_modo(f, PH_FINTO_METEO_SENZA_POSIZIONE);
    CONTROLLA(aspetta(met_stato, &st_senzapos, 3000));
    m = ph_srv_meteo(S);
    CONTROLLA(m->frase[0] != '\0' && m->ha_dati && m->dati.ricevuta == vecchia);
    ph_finto_meteo_modo(f, PH_FINTO_METEO_NON_COLLEGATO);
    CONTROLLA(aspetta(met_stato, &st_noncoll, 3000));
    m = ph_srv_meteo(S);
    CONTROLLA(m->frase[0] != '\0' && m->ha_dati && m->dati.ricevuta == vecchia);
    ph_finto_meteo_modo(f, PH_FINTO_METEO_IN_ATTESA);
    CONTROLLA(aspetta(met_stato, &st_attesa, 3000));
    CONTROLLA(ph_srv_meteo(S)->ha_dati && ph_srv_meteo(S)->dati.ricevuta == vecchia);
    ph_finto_meteo_modo(f, PH_FINTO_METEO_PRONTO);
    CONTROLLA(aspetta(met_stato, &st_pronto, 3000));
    CONTROLLA(ph_srv_meteo(S)->frase[0] == '\0');

    /* Smesso di osservare: SPENTO subito e niente piu', neanche la ripubblicazione periodica. */
    ph_finto_meteo_intervallo(f, 1);
    ph_srv_meteo_osserva(S, false);
    CONTROLLA(aspetta(met_stato, &st_spento, 3000));
    barriera();
    prima = conta[PH_SRV_EV_METEO];
    ph_finto_meteo_nuovi_dati(f);
    ph_finto_meteo_modo(f, PH_FINTO_METEO_NON_DISPONIBILE);
    gint64 fine = g_get_monotonic_time() + 2500000; /* oltre due periodi */
    CONTROLLA(aspetta(tempo_trascorso, &fine, 4000));
    CONTROLLA(conta[PH_SRV_EV_METEO] == prima && ph_srv_meteo(S)->stato == PH_SRV_METEO_SPENTO);
    ph_finto_meteo_modo(f, PH_FINTO_METEO_PRONTO);
    /* Riacceso con l'intervallo gia' a 1 s: il ciclo parte con l'osservazione (non solo
     * quando si cambia l'intervallo a meteo acceso): la prima risposta piu' due ripubblicazioni. */
    ph_srv_meteo_osserva(S, true);
    CONTROLLA(aspetta(met_stato, &st_pronto, 3000));
    int dopo_prima = conta[PH_SRV_EV_METEO] + 2;
    CONTROLLA(aspetta(met_eventi_oltre, &dopo_prima, 5000));
    ph_srv_meteo_osserva(S, false);
    CONTROLLA(aspetta(met_stato, &st_spento, 3000));
    ph_finto_meteo_intervallo(f, 0);

    /* Un telefono finto nuovo: servizio muto fin dall'inizio (nessun dato da mostrare),
     * risposta lenta, e chiusura con le sorgenti ancora accese. */
    PhServizi *vecchio = S;
    PhFintoOpzioni o2 = {.notifiche_ogni_s = -1, .ritardo_ms = 10};
    PhFinto *f2 = ph_finto_nuovo(&o2);
    CONTROLLA(f2 != NULL);
    if (f2 != NULL) {
        S = S_METEO = ph_finto_servizi(f2);
        int conta2[PH_SRV_EV_NUMERO] = {0};
        for (int ev = 0; ev < PH_SRV_EV_NUMERO; ev++)
            ph_srv_ascolta(S, (PhSrvEvento)ev, ev_qualunque, conta2);
        CONTROLLA(aspetta(collegato, NULL, 3000));
        ph_finto_meteo_modo(f2, PH_FINTO_METEO_NON_DISPONIBILE);
        ph_srv_meteo_osserva(S, true);
        CONTROLLA(aspetta(met_stato, &st_nondisp, 3000));
        CONTROLLA(!ph_srv_meteo(S)->ha_dati && ph_srv_meteo(S)->frase[0] != '\0');
        ph_srv_meteo_osserva(S, false);
        CONTROLLA(aspetta(met_stato, &st_spento, 3000));
        ph_finto_meteo_modo(f2, PH_FINTO_METEO_PRONTO);
        ph_finto_meteo_lento(f2, true);
        t0 = g_get_monotonic_time();
        ph_srv_meteo_osserva(S, true);
        CONTROLLA(aspetta(met_stato, &st_attesa, 3000));
        gint64 un_poco = g_get_monotonic_time() + 1500000;
        CONTROLLA(aspetta(tempo_trascorso, &un_poco, 3000));
        CONTROLLA(ph_srv_meteo(S)->stato == PH_SRV_METEO_IN_ATTESA); /* 300 ms sono passati, 10 s no */
        CONTROLLA(aspetta(met_stato, &st_pronto, 12000));
        CONTROLLA(g_get_monotonic_time() - t0 >= 9000 * 1000);
        ph_finto_meteo_intervallo(f2, 1);
        barriera();
        /* Una ricerca lenta ancora in attesa alla chiusura: nessuna risposta, ne' prima ne' dopo. */
        Lu lenta = {0};
        ph_finto_meteo_ricerca_lenta(f2, true);
        ph_srv_meteo_cerca(S, "sample", "it", NULL, luoghi_fatti, &lenta);
        PhMeteoLuogo in_coda = {.nome = "Sample Town", .lat_centesimi = 1, .lon_centesimi = 2};
        ph_srv_meteo_localita(S, &in_coda); /* lavoro ancora in coda alla chiusura: la copia non si perde (ASan) */
        ph_finto_libera(f2); /* sorgenti del meteo accese: nessuna perdita, nessun avviso dopo */
        for (int i = 0; i < 20; i++)
            g_main_context_iteration(NULL, FALSE);
        S = S_METEO = vecchio;
        CONTROLLA(conta2[PH_SRV_EV_METEO] >= 3);
        CONTROLLA(lenta.chiamate == 0);
    }
}

static void prova_widget(PhFinto *f, int *conta)
{
    CONTA = conta;
    /* Il fuso con l'ora legale: i cambi d'ora del calendario si esercitano davvero. */
    GDateTime *prima_ora = g_date_time_new_local(2027, 3, 27, 12, 0, 0);
    GDateTime *dopo_ora = g_date_time_new_local(2027, 3, 29, 12, 0, 0);
    CONTROLLA(g_date_time_get_utc_offset(prima_ora) != g_date_time_get_utc_offset(dopo_ora));
    g_date_time_unref(prima_ora);
    g_date_time_unref(dopo_ora);
    prova_ordine(f);
    GDateTime *m1 = g_date_time_new_local(2027, 3, 1, 0, 0, 0);
    GDateTime *m2 = g_date_time_new_local(2027, 4, 1, 0, 0, 0);
    uint64_t da = (uint64_t)g_date_time_to_unix(m1) * 1000u, a = (uint64_t)g_date_time_to_unix(m2) * 1000u;
    g_date_time_unref(m1);
    g_date_time_unref(m2);

    /* Calendario del mese: giorni vuoti, pieni, eventi su piu' giorni, tutto il giorno. */
    W2 c1 = {0};
    ph_srv_tel_calendario_intervallo(S, da, a, NULL, mese_fatto, &c1);
    CONTROLLA(c1.chiamate == 0); /* mai dentro la chiamata */
    CONTROLLA(aspetta(w2_finito, &c1, 3000) && c1.esito == PH_SRV_ESITO_OK && c1.n > 20 && c1.ordinati);
    CONTROLLA(!c1.troncato && strcmp(c1.app, "it.finto.calendario") == 0);
    CONTROLLA(c1.per_giorno[10] == 0 && c1.per_giorno[9] == 3 && c1.per_giorno[12] >= 5); /* vuoto, normale, pieno */
    /* A Roma la domenica 28 febbraio (tutto il giorno, UTC) finisce all'01:00 locale
     * del 1 marzo: tocca l'intervallo, quindi 5 domeniche e non 4. */
    CONTROLLA(c1.domeniche == 5 && c1.durata_due_giorni && c1.tutti_a_mezzanotte && c1.ha_trasferta);
    CONTROLLA(!c1.ora_sbagliata); /* marzo 2027: il 28 passa all'ora legale */
    /* A cavallo d'anno, febbraio di 28 giorni (quattro domeniche), ramo «troncato». */
    W2 y1 = {0}, y2 = {0}, y3 = {0}, y4 = {0};
    ph_srv_tel_calendario_intervallo(S, loc_ms(2026, 12, 15), loc_ms(2027, 1, 15), NULL, mese_fatto, &y1);
    CONTROLLA(aspetta(w2_finito, &y1, 3000) && y1.esito == PH_SRV_ESITO_OK && y1.n > 10 && y1.ordinati);
    CONTROLLA(y1.domeniche == 4 && !y1.ora_sbagliata && y1.per_giorno[31] > 0 && y1.per_giorno[1] > 0);
    ph_srv_tel_calendario_intervallo(S, loc_ms(2027, 2, 1), loc_ms(2027, 3, 1), NULL, mese_fatto, &y2);
    CONTROLLA(aspetta(w2_finito, &y2, 3000) && y2.esito == PH_SRV_ESITO_OK && y2.ordinati && y2.domeniche == 5); /* con il 31 gennaio */
    CONTROLLA(y2.per_giorno[28] == 0 && !y2.troncato); /* il 28 febbraio 2027 e' domenica */
    ph_finto_calendario_massimo(f, 5);
    ph_srv_tel_calendario_intervallo(S, da, a, NULL, mese_fatto, &y3);
    CONTROLLA(aspetta(w2_finito, &y3, 3000) && y3.esito == PH_SRV_ESITO_OK && y3.n == 5 && y3.troncato);
    ph_finto_calendario_massimo(f, 0);
    /* Data oltre l'anno 9999: ERRORE con frase, senza avvisi critici (G_DEBUG=fatal-criticals). */
    ph_srv_tel_calendario_intervallo(S, 300000000000000ull, 300000000000000ull + 86400000u, NULL, mese_fatto, &y4);
    CONTROLLA(aspetta(w2_finito, &y4, 3000) && y4.esito == PH_SRV_ESITO_ERRORE && y4.ha_frase && y4.n == 0);
    /* Solo il 13: la trasferta iniziata il 12 lo tocca ancora; il 1 marzo non la contiene. */
    GDateTime *g13 = g_date_time_new_local(2027, 3, 13, 0, 0, 0);
    uint64_t d13 = (uint64_t)g_date_time_to_unix(g13) * 1000u;
    g_date_time_unref(g13);
    W2 c2 = {0}, c3 = {0};
    ph_srv_tel_calendario_intervallo(S, d13, d13 + 86400000u, NULL, mese_fatto, &c2);
    CONTROLLA(aspetta(w2_finito, &c2, 3000) && c2.esito == PH_SRV_ESITO_OK && c2.ha_trasferta);
    ph_srv_tel_calendario_intervallo(S, da, da + 86400000u, NULL, mese_fatto, &c3);
    CONTROLLA(aspetta(w2_finito, &c3, 3000) && c3.esito == PH_SRV_ESITO_OK && c3.n == 3 && c3.domeniche == 1 && !c3.ha_trasferta);
    /* Oltre 62 giorni: rifiutata dal pezzo comune, senza chiedere. */
    W2 c4 = {0};
    ph_srv_tel_calendario_intervallo(S, da, da + 63ull * 86400000u, NULL, mese_fatto, &c4);
    CONTROLLA(aspetta(w2_finito, &c4, 3000) && c4.esito == PH_SRV_ESITO_ERRORE);
    /* Non disponibile, errore con frase, calendario illeggibile. */
    W2 c5 = {0}, c6 = {0}, c7 = {0}, c8 = {0};
    ph_finto_voce_modo(f, PH_FINTO_VOCE_CALENDARIO_MESE, PH_FINTO_MODO_NON_DISPONIBILE);
    ph_srv_tel_calendario_intervallo(S, da, a, NULL, mese_fatto, &c5);
    CONTROLLA(aspetta(w2_finito, &c5, 3000) && c5.esito == PH_SRV_ESITO_ERRORE && !c5.ha_frase && c5.n == 0);
    ph_finto_voce_modo(f, PH_FINTO_VOCE_CALENDARIO_MESE, PH_FINTO_MODO_ERRORE);
    ph_srv_tel_calendario_intervallo(S, da, a, NULL, mese_fatto, &c6);
    CONTROLLA(aspetta(w2_finito, &c6, 3000) && c6.esito == PH_SRV_ESITO_ERRORE && c6.ha_frase && c6.n == 0);
    ph_finto_voce_modo(f, PH_FINTO_VOCE_CALENDARIO_MESE, PH_FINTO_MODO_NORMALE);
    ph_finto_calendario_illeggibile(f, true);
    ph_srv_tel_calendario_intervallo(S, da, a, NULL, mese_fatto, &c7);
    CONTROLLA(aspetta(w2_finito, &c7, 3000) && c7.esito == PH_SRV_ESITO_ERRORE && c7.ha_frase);
    ph_finto_calendario_illeggibile(f, false);
    /* Annullato: la risposta non arriva (la barriera garantisce che il lavoro e' passato). */
    GCancellable *an = g_cancellable_new();
    ph_srv_tel_calendario_intervallo(S, da, a, an, mese_fatto, &c8);
    g_cancellable_cancel(an);
    g_object_unref(an);
    barriera();
    CONTROLLA(c8.chiamate == 0);

    /* Prossima sveglia: domani alle 07:30, nessuna, non disponibile, errore. */
    W2 s1 = {0}, s2 = {0}, s3 = {0}, s4 = {0};
    ph_srv_tel_sveglia(S, NULL, sveglia_fatta, &s1);
    CONTROLLA(s1.chiamate == 0);
    CONTROLLA(aspetta(w2_finito, &s1, 3000) && s1.esito == PH_SRV_ESITO_OK && s1.presente);
    CONTROLLA(strcmp(s1.app, "it.finto.orologio") == 0);
    uint64_t adesso = (uint64_t)g_get_real_time() / 1000u;
    CONTROLLA(s1.ora_ms > adesso && s1.ora_ms < adesso + 2u * 86400000u);
    GDateTime *ts = g_date_time_new_from_unix_local((gint64)(s1.ora_ms / 1000u));
    CONTROLLA(g_date_time_get_hour(ts) == 7 && g_date_time_get_minute(ts) == 30);
    g_date_time_unref(ts);
    ph_finto_sveglia_assente(f, true);
    ph_srv_tel_sveglia(S, NULL, sveglia_fatta, &s2);
    CONTROLLA(aspetta(w2_finito, &s2, 3000) && s2.esito == PH_SRV_ESITO_OK && !s2.presente && s2.ora_ms == 0);
    ph_finto_sveglia_assente(f, false);
    ph_finto_voce_modo(f, PH_FINTO_VOCE_SVEGLIA, PH_FINTO_MODO_NON_DISPONIBILE);
    ph_srv_tel_sveglia(S, NULL, sveglia_fatta, &s3);
    CONTROLLA(aspetta(w2_finito, &s3, 3000) && s3.esito == PH_SRV_ESITO_ERRORE && !s3.ha_frase);
    ph_finto_voce_modo(f, PH_FINTO_VOCE_SVEGLIA, PH_FINTO_MODO_ERRORE);
    ph_srv_tel_sveglia(S, NULL, sveglia_fatta, &s4);
    CONTROLLA(aspetta(w2_finito, &s4, 3000) && s4.esito == PH_SRV_ESITO_ERRORE && s4.ha_frase);
    ph_finto_voce_modo(f, PH_FINTO_VOCE_SVEGLIA, PH_FINTO_MODO_NORMALE);
    W2 s5 = {0};
    GCancellable *an2 = g_cancellable_new();
    ph_srv_tel_sveglia(S, an2, sveglia_fatta, &s5);
    g_cancellable_cancel(an2);
    g_object_unref(an2);
    barriera();
    CONTROLLA(s5.chiamate == 0);

    /* Batteria in dettaglio: in scarica, in carica, piena, non disponibile, errore. */
    PhSrvTelefono tel;
    W2 b1 = {0}, b2 = {0}, b3 = {0}, b4 = {0}, b5 = {0}, b6 = {0};
    ph_srv_tel_batteria(S, NULL, batteria_fatta, &b1);
    CONTROLLA(b1.chiamate == 0);
    CONTROLLA(aspetta(w2_finito, &b1, 3000) && b1.esito == PH_SRV_ESITO_OK);
    ph_srv_telefono(S, &tel);
    CONTROLLA(b1.bat.livello == tel.batteria && b1.bat.stato == PH_SRV_BAT_IN_SCARICA && b1.bat.alimentazione == 0);
    CONTROLLA(b1.bat.temperatura_nota && b1.bat.temperatura_decimi == 315 && b1.bat.salute == PH_SRV_SALUTE_BUONA);
    CONTROLLA(b1.bat.tensione_mv == 4100 && b1.bat.minuti_carica == -1 && b1.bat.minuti_autonomia == 540);
    CONTROLLA(!tel.in_carica);
    ph_finto_in_carica(f, true);
    ph_srv_tel_batteria(S, NULL, batteria_fatta, &b2);
    CONTROLLA(aspetta(w2_finito, &b2, 3000) && b2.esito == PH_SRV_ESITO_OK);
    CONTROLLA(b2.bat.stato == PH_SRV_BAT_IN_CARICA && b2.bat.alimentazione == PH_SRV_ALIM_RETE);
    CONTROLLA(b2.bat.minuti_carica == 45 && b2.bat.minuti_autonomia == -1);
    ph_srv_telefono(S, &tel);
    CONTROLLA(tel.in_carica); /* la fotografia arriva prima dell'esito */
    ph_finto_batteria(f, 100);
    ph_srv_tel_batteria(S, NULL, batteria_fatta, &b3);
    CONTROLLA(aspetta(w2_finito, &b3, 3000) && b3.bat.stato == PH_SRV_BAT_PIENA && b3.bat.livello == 100);
    CONTROLLA(b3.bat.minuti_carica == 0);
    ph_finto_in_carica(f, false);
    ph_finto_batteria(f, 78);
    ph_finto_voce_modo(f, PH_FINTO_VOCE_BATTERIA, PH_FINTO_MODO_NON_DISPONIBILE);
    ph_srv_tel_batteria(S, NULL, batteria_fatta, &b4);
    CONTROLLA(aspetta(w2_finito, &b4, 3000) && b4.esito == PH_SRV_ESITO_ERRORE && !b4.ha_frase);
    ph_finto_voce_modo(f, PH_FINTO_VOCE_BATTERIA, PH_FINTO_MODO_ERRORE);
    ph_srv_tel_batteria(S, NULL, batteria_fatta, &b5);
    CONTROLLA(aspetta(w2_finito, &b5, 3000) && b5.esito == PH_SRV_ESITO_ERRORE && b5.ha_frase);
    ph_finto_voce_modo(f, PH_FINTO_VOCE_BATTERIA, PH_FINTO_MODO_NORMALE);
    GCancellable *an3 = g_cancellable_new();
    ph_srv_tel_batteria(S, an3, batteria_fatta, &b6);
    g_cancellable_cancel(an3);
    g_object_unref(an3);
    barriera();
    CONTROLLA(b6.chiamate == 0);

    /* Controlli: nessuna fotografia finche' nessuno osserva. */
    PhSrvControlli ctl;
    ph_srv_controlli(S, &ctl);
    CONTROLLA(!ctl.noto);
    int prima = conta[PH_SRV_EV_CONTROLLI];
    ph_finto_controllo_dal_telefono(f, PH_SRV_CTL_NON_DISTURBARE, 1);
    barriera();
    CONTROLLA(conta[PH_SRV_EV_CONTROLLI] == prima);
    /* Osservando arriva lo stato: quello cambiato mentre nessuno guardava. */
    ph_srv_controlli_osserva(S, true);
    CONTROLLA(aspetta(ctl_noto, NULL, 3000));
    ph_srv_controlli(S, &ctl);
    CONTROLLA(ctl.suoneria == PH_SRV_SUONERIA_SUONA && ctl.non_disturbare == 1 && ctl.torcia == 0);
    CONTROLLA(!ctl.trova_in_corso && ctl.puo_suoneria && ctl.puo_non_disturbare && ctl.puo_torcia && ctl.puo_trova);
    /* Comandi: l'esito arriva dopo la fotografia nuova. */
    W2 k1 = {0}, k2 = {0}, k3 = {0};
    ph_srv_controllo_imposta(S, PH_SRV_CTL_SUONERIA, PH_SRV_SUONERIA_SILENZIOSO, NULL, fatto_w2, &k1);
    CONTROLLA(k1.chiamate == 0);
    CONTROLLA(aspetta(w2_finito, &k1, 3000) && k1.esito == PH_SRV_ESITO_OK);
    int v_sil[2] = {0, PH_SRV_SUONERIA_SILENZIOSO};
    CONTROLLA(ctl_uguale(v_sil));
    ph_srv_controllo_imposta(S, PH_SRV_CTL_NON_DISTURBARE, 0, NULL, fatto_w2, &k2);
    CONTROLLA(aspetta(w2_finito, &k2, 3000) && k2.esito == PH_SRV_ESITO_OK);
    int v_nd[2] = {1, 0};
    CONTROLLA(ctl_uguale(v_nd));
    ph_srv_controllo_imposta(S, PH_SRV_CTL_TORCIA, 1, NULL, fatto_w2, &k3);
    CONTROLLA(aspetta(w2_finito, &k3, 3000) && k3.esito == PH_SRV_ESITO_OK);
    int v_t1[2] = {2, 1};
    CONTROLLA(ctl_uguale(v_t1));
    /* Valore fuori intervallo: scartato dal pezzo comune senza toccare lo stato. */
    W2 k4 = {0};
    ph_srv_controllo_imposta(S, PH_SRV_CTL_TORCIA, 7, NULL, fatto_w2, &k4);
    CONTROLLA(aspetta(w2_finito, &k4, 3000) && k4.esito == PH_SRV_ESITO_ERRORE);
    CONTROLLA(ctl_uguale(v_t1));
    /* Cambio fatto dal telefono: qualcuno toglie il silenzioso e spegne la torcia. */
    int ev = conta[PH_SRV_EV_CONTROLLI];
    ph_finto_controllo_dal_telefono(f, PH_SRV_CTL_SUONERIA, PH_SRV_SUONERIA_SUONA);
    CONTROLLA(aspetta(ctl_eventi_oltre, &ev, 3000));
    int v_su[2] = {0, PH_SRV_SUONERIA_SUONA};
    CONTROLLA(ctl_uguale(v_su) && ctl_uguale(v_t1));
    ph_finto_controllo_dal_telefono(f, PH_SRV_CTL_TORCIA, 0);
    int v_t0[2] = {2, 0};
    CONTROLLA(aspetta(ctl_uguale, v_t0, 3000));
    /* Annullati: nessuna modifica e nessuna risposta. */
    W2 k5 = {0}, k6 = {0};
    GCancellable *an4 = g_cancellable_new();
    ph_srv_controllo_imposta(S, PH_SRV_CTL_TORCIA, 1, an4, fatto_w2, &k5);
    GCancellable *an5 = g_cancellable_new();
    ph_srv_trova_avvia(S, 5, an5, fatto_w2, &k6);
    g_cancellable_cancel(an4);
    g_cancellable_cancel(an5);
    g_object_unref(an4);
    g_object_unref(an5);
    barriera();
    CONTROLLA(k5.chiamate == 0 && k6.chiamate == 0);
    CONTROLLA(ctl_uguale(v_t0));
    bool no = false, si = true;
    CONTROLLA(ctl_trova(&no));
    /* Torcia non disponibile. */
    ph_finto_torcia_disponibile(f, false);
    CONTROLLA(aspetta(ctl_torcia_possibile, &no, 3000));
    ph_srv_controlli(S, &ctl);
    CONTROLLA(ctl.torcia == PH_SRV_NON_DISPONIBILE && ctl.puo_suoneria && ctl.puo_non_disturbare && ctl.puo_trova);
    W2 k7 = {0};
    ph_srv_controllo_imposta(S, PH_SRV_CTL_TORCIA, 1, NULL, fatto_w2, &k7);
    CONTROLLA(aspetta(w2_finito, &k7, 3000) && k7.esito == PH_SRV_ESITO_ERRORE && !k7.ha_frase);
    ph_finto_torcia_disponibile(f, true);
    CONTROLLA(aspetta(ctl_torcia_possibile, &si, 3000));
    CONTROLLA(ctl_uguale(v_t0));
    /* Il telefono rifiuta i comandi: ERRORE con frase, stato invariato. */
    ph_finto_voce_modo(f, PH_FINTO_VOCE_CONTROLLI, PH_FINTO_MODO_ERRORE);
    W2 k8 = {0}, k9 = {0};
    ph_srv_controllo_imposta(S, PH_SRV_CTL_NON_DISTURBARE, 1, NULL, fatto_w2, &k8);
    CONTROLLA(aspetta(w2_finito, &k8, 3000) && k8.esito == PH_SRV_ESITO_ERRORE && k8.ha_frase);
    CONTROLLA(ctl_uguale(v_nd));
    ph_srv_trova_avvia(S, 5, NULL, fatto_w2, &k9);
    CONTROLLA(aspetta(w2_finito, &k9, 3000) && k9.esito == PH_SRV_ESITO_ERRORE && k9.ha_frase);
    CONTROLLA(ctl_trova(&no));
    /* Controlli non disponibili: ogni valore -1, ogni comando ERRORE senza frase. */
    ph_finto_voce_modo(f, PH_FINTO_VOCE_CONTROLLI, PH_FINTO_MODO_NON_DISPONIBILE);
    CONTROLLA(aspetta(ctl_nulla_disponibile, NULL, 3000));
    W2 k10 = {0}, k11 = {0};
    ph_srv_controllo_imposta(S, PH_SRV_CTL_SUONERIA, PH_SRV_SUONERIA_VIBRA, NULL, fatto_w2, &k10);
    CONTROLLA(aspetta(w2_finito, &k10, 3000) && k10.esito == PH_SRV_ESITO_ERRORE && !k10.ha_frase);
    ph_srv_trova_avvia(S, 5, NULL, fatto_w2, &k11);
    CONTROLLA(aspetta(w2_finito, &k11, 3000) && k11.esito == PH_SRV_ESITO_ERRORE && !k11.ha_frase);
    ph_finto_voce_modo(f, PH_FINTO_VOCE_CONTROLLI, PH_FINTO_MODO_NORMALE);
    CONTROLLA(aspetta(ctl_tutto_disponibile, &si, 3000));
    CONTROLLA(ctl_uguale(v_su) && ctl_uguale(v_nd));
    /* Trova il telefono: avvio, arresto a mano. */
    W2 t1 = {0}, t2 = {0};
    ph_srv_trova_avvia(S, 5, NULL, fatto_w2, &t1);
    CONTROLLA(t1.chiamate == 0);
    CONTROLLA(aspetta(w2_finito, &t1, 3000) && t1.esito == PH_SRV_ESITO_OK);
    CONTROLLA(ctl_trova(&si)); /* pubblicato prima dell'esito */
    ph_srv_trova_ferma(S);
    CONTROLLA(aspetta(ctl_trova, &no, 3000));
    /* Arresto automatico: dopo il limite (5 s, il minimo) si ferma da solo. */
    gint64 inizio = g_get_monotonic_time();
    ph_srv_trova_avvia(S, 5, NULL, fatto_w2, &t2);
    CONTROLLA(aspetta(w2_finito, &t2, 3000) && t2.esito == PH_SRV_ESITO_OK && ctl_trova(&si));
    CONTROLLA(aspetta(ctl_trova, &no, 9000));
    CONTROLLA(g_get_monotonic_time() - inizio >= 4000 * 1000); /* e' stato il limite, non altro */
    /* Il pezzo comune limita i secondi (0 -> 5): mai un arresto immediato. */
    W2 t3 = {0};
    ph_srv_trova_avvia(S, 0, NULL, fatto_w2, &t3);
    CONTROLLA(aspetta(w2_finito, &t3, 3000) && t3.esito == PH_SRV_ESITO_OK && ctl_trova(&si));
    ph_srv_trova_ferma(S);
    CONTROLLA(aspetta(ctl_trova, &no, 3000));

    /* Caduta del collegamento: ogni richiesta risponde NON_COLLEGATO; al ritorno la
     * fotografia dei controlli (ancora osservati) arriva di nuovo. */
    ph_finto_perdi(f, 1);
    CONTROLLA(aspetta(perso, NULL, 2000));
    W2 p1 = {0}, p2 = {0}, p3 = {0}, p4 = {0}, p5 = {0};
    ph_srv_tel_calendario_intervallo(S, da, a, NULL, mese_fatto, &p1);
    ph_srv_tel_sveglia(S, NULL, sveglia_fatta, &p2);
    ph_srv_tel_batteria(S, NULL, batteria_fatta, &p3);
    ph_srv_controllo_imposta(S, PH_SRV_CTL_TORCIA, 1, NULL, fatto_w2, &p4);
    ph_srv_trova_avvia(S, 5, NULL, fatto_w2, &p5);
    CONTROLLA(aspetta(w2_finito, &p1, 3000) && p1.esito == PH_SRV_ESITO_NON_COLLEGATO);
    CONTROLLA(aspetta(w2_finito, &p2, 3000) && p2.esito == PH_SRV_ESITO_NON_COLLEGATO);
    CONTROLLA(aspetta(w2_finito, &p3, 3000) && p3.esito == PH_SRV_ESITO_NON_COLLEGATO);
    CONTROLLA(aspetta(w2_finito, &p4, 3000) && p4.esito == PH_SRV_ESITO_NON_COLLEGATO);
    CONTROLLA(aspetta(w2_finito, &p5, 3000) && p5.esito == PH_SRV_ESITO_NON_COLLEGATO);
    ev = conta[PH_SRV_EV_CONTROLLI];
    CONTROLLA(aspetta(collegato, NULL, 4000));
    CONTROLLA(aspetta(ctl_eventi_oltre, &ev, 3000));

    /* Smesso di osservare: il cambio fatto dal telefono non e' piu' pubblicato. */
    ph_srv_controlli_osserva(S, false);
    barriera();
    prima = conta[PH_SRV_EV_CONTROLLI];
    ph_finto_controllo_dal_telefono(f, PH_SRV_CTL_TORCIA, 1);
    barriera();
    CONTROLLA(conta[PH_SRV_EV_CONTROLLI] == prima);

    /* Chiusura con «trova» ancora in corso (e controlli osservati di nuovo): niente
     * perdite ne' avvisi dopo ph_finto_libera (lo controllano ASan e la fine della prova). */
    ph_srv_controlli_osserva(S, true);
    W2 t4 = {0};
    ph_srv_trova_avvia(S, 60, NULL, fatto_w2, &t4);
    CONTROLLA(aspetta(w2_finito, &t4, 3000) && t4.esito == PH_SRV_ESITO_OK);
}

/* ---- Chiamate, rete, media (27 nov) ---------------------------------------------- */
static bool chi_stato(void *d) { return ph_srv_chiamate(S)->stato == *(PhSrvLettura *)d; }
static bool rete_stato(void *d) { return ph_srv_rete(S)->stato == *(PhSrvLettura *)d; }
static bool media_stato_l(void *d) { return ph_srv_media(S)->stato_lettura == *(PhSrvLettura *)d; }
static bool media_stato_riprod(void *d) { return ph_srv_media(S)->stato == *(int *)d; }
static bool media_titolo(void *d) { return strcmp(ph_srv_media(S)->titolo, (const char *)d) == 0; }
static bool rete_aereo_on(void *d) { return (ph_srv_rete(S)->aereo == PH_SRV_TRI_SI) == *(bool *)d; }

static void prova_nuovi(PhFinto *f, int *conta)
{
    (void)conta;
    PhSrvLettura ok = PH_SRV_LET_OK, nd = PH_SRV_LET_NON_DISPONIBILE, neg = PH_SRV_LET_NEGATO;
    /* Niente osservatori: niente pubblicazioni (stato resta «in attesa»). */
    int c0 = CONTA[PH_SRV_EV_CHIAMATE] + CONTA[PH_SRV_EV_RETE] + CONTA[PH_SRV_EV_MEDIA];
    ph_finto_media_assente(f, false);
    barriera();
    CONTROLLA(CONTA[PH_SRV_EV_CHIAMATE] + CONTA[PH_SRV_EV_RETE] + CONTA[PH_SRV_EV_MEDIA] == c0);
    CONTROLLA(ph_srv_chiamate(S)->stato == PH_SRV_LET_ATTESA);

    /* Chiamate. */
    ph_srv_chiamate_osserva(S, true);
    CONTROLLA(aspetta(chi_stato, &ok, 3000));
    const PhSrvChiamate *ch = ph_srv_chiamate(S);
    CONTROLLA(ch->n == 6 && ch->v[0].tipo == PH_SRV_CHIAMATA_PERSA && strcmp(ch->v[0].nome, "Alex Sample") == 0);
    CONTROLLA(strcmp(ch->v[0].numero, "+00 000 000 0001") == 0 && ch->v[0].ora_ms > ch->v[1].ora_ms);
    ph_finto_voce_modo(f, PH_FINTO_VOCE_CHIAMATE, PH_FINTO_MODO_ERRORE);
    CONTROLLA(aspetta(chi_stato, &neg, 3000));
    CONTROLLA(ph_srv_chiamate(S)->n == 0);
    ph_finto_voce_modo(f, PH_FINTO_VOCE_CHIAMATE, PH_FINTO_MODO_NON_DISPONIBILE);
    CONTROLLA(aspetta(chi_stato, &nd, 3000));
    ph_finto_voce_modo(f, PH_FINTO_VOCE_CHIAMATE, PH_FINTO_MODO_NORMALE);
    CONTROLLA(aspetta(chi_stato, &ok, 3000));
    /* Smesso di osservare: il cambio di modo non e' piu' pubblicato. */
    ph_srv_chiamate_osserva(S, false);
    barriera();
    int c1 = CONTA[PH_SRV_EV_CHIAMATE];
    ph_finto_voce_modo(f, PH_FINTO_VOCE_CHIAMATE, PH_FINTO_MODO_ERRORE);
    barriera();
    CONTROLLA(CONTA[PH_SRV_EV_CHIAMATE] == c1);
    ph_finto_voce_modo(f, PH_FINTO_VOCE_CHIAMATE, PH_FINTO_MODO_NORMALE);
    barriera();

    /* Rete. */
    ph_srv_rete_osserva(S, true);
    CONTROLLA(aspetta(rete_stato, &ok, 3000));
    const PhSrvReteDettaglio *r = ph_srv_rete(S);
    CONTROLLA(r->wifi == PH_SRV_WIFI_CONNESSO && r->wifi_livello == 3 && strcmp(r->wifi_nome, "SampleNet") == 0);
    CONTROLLA(r->mobile == PH_SRV_MOBILE_IN_SERVIZIO && r->generazione == 4 && r->dati_mobili == PH_SRV_TRI_SI && r->aereo == PH_SRV_TRI_NO);
    CONTROLLA(strcmp(r->operatore, "Sample Mobile") == 0);
    bool si = true;
    ph_finto_rete_aereo(f, true);
    CONTROLLA(aspetta(rete_aereo_on, &si, 3000));
    CONTROLLA(ph_srv_rete(S)->wifi == PH_SRV_WIFI_SPENTO && ph_srv_rete(S)->mobile == PH_SRV_MOBILE_ASSENTE);
    ph_finto_rete_aereo(f, false);
    ph_finto_voce_modo(f, PH_FINTO_VOCE_RETE, PH_FINTO_MODO_ERRORE);
    CONTROLLA(aspetta(rete_stato, &neg, 3000));
    ph_finto_voce_modo(f, PH_FINTO_VOCE_RETE, PH_FINTO_MODO_NORMALE);
    CONTROLLA(aspetta(rete_stato, &ok, 3000));
    ph_srv_rete_osserva(S, false);

    /* Media: stato e comandi che cambiano il finto. */
    ph_srv_media_osserva(S, true);
    CONTROLLA(aspetta(media_stato_l, &ok, 3000));
    const PhSrvMedia *m = ph_srv_media(S);
    CONTROLLA(m->stato == PH_SRV_MEDIA_RIPRODUZIONE && strcmp(m->titolo, "Sample Song") == 0);
    CONTROLLA(strcmp(m->artista, "Sample Artist") == 0 && m->comandi == 7 && m->pacchetto[0] != '\0');
    ph_srv_media_comando(S, 0);  /* fuori elenco: ignorato */
    ph_srv_media_comando(S, 4);
    barriera();
    CONTROLLA(ph_finto_media_comandi(f) == 0);
    int pausa = PH_SRV_MEDIA_PAUSA;
    ph_srv_media_comando(S, PH_SRV_MEDIA_CMD_PLAY_PAUSA);
    CONTROLLA(aspetta(media_stato_riprod, &pausa, 3000));
    CONTROLLA(ph_finto_media_ultimo_comando(f) == 1);
    ph_srv_media_comando(S, PH_SRV_MEDIA_CMD_AVANTI);
    CONTROLLA(aspetta(media_titolo, (void *)"Another Sample Song", 3000));
    ph_srv_media_comando(S, PH_SRV_MEDIA_CMD_INDIETRO);
    CONTROLLA(aspetta(media_titolo, (void *)"Sample Song", 3000));
    ph_finto_media_assente(f, true);
    int nessuna = PH_SRV_MEDIA_NESSUNA;
    CONTROLLA(aspetta(media_stato_riprod, &nessuna, 3000));
    CONTROLLA(ph_srv_media(S)->comandi == 0 && ph_srv_media(S)->titolo[0] == '\0');
    unsigned n = ph_finto_media_comandi(f);
    ph_srv_media_comando(S, PH_SRV_MEDIA_CMD_AVANTI); /* nessun lettore: il telefono non lo riceve */
    barriera();
    CONTROLLA(ph_finto_media_comandi(f) == n);
    ph_finto_media_assente(f, false);
    ph_finto_voce_modo(f, PH_FINTO_VOCE_MEDIA, PH_FINTO_MODO_ERRORE);
    CONTROLLA(aspetta(media_stato_l, &neg, 3000));
    ph_finto_voce_modo(f, PH_FINTO_VOCE_MEDIA, PH_FINTO_MODO_NON_DISPONIBILE);
    CONTROLLA(aspetta(media_stato_l, &nd, 3000));
    ph_finto_voce_modo(f, PH_FINTO_VOCE_MEDIA, PH_FINTO_MODO_NORMALE);
    CONTROLLA(aspetta(media_stato_l, &ok, 3000));

    /* Nessuna sessione: tutto «non disponibile», poi il ritorno rilegge. */
    ph_srv_chiamate_osserva(S, true);
    ph_srv_rete_osserva(S, true);
    ph_finto_perdi(f, 1);
    CONTROLLA(aspetta(perso, NULL, 2000));
    CONTROLLA(aspetta(media_stato_l, &nd, 3000));
    CONTROLLA(aspetta(chi_stato, &nd, 3000) && aspetta(rete_stato, &nd, 3000));
    CONTROLLA(aspetta(collegato, NULL, 4000));
    CONTROLLA(aspetta(media_stato_l, &ok, 3000));
    CONTROLLA(aspetta(chi_stato, &ok, 3000) && aspetta(rete_stato, &ok, 3000));
    ph_srv_chiamate_osserva(S, false);
    ph_srv_rete_osserva(S, false);
    ph_srv_media_osserva(S, false);
    barriera();
}

/* ---- Pulizia di cio' che arriva dal backend e scarto delle pubblicazioni tardive -------- */
static bool ev_oltre(void *d)
{
    const int *v = d; /* {evento, valore da superare} */
    return CONTA[v[0]] > v[1];
}

static void riempi(char *b, size_t n) { memset(b, 'A', n); } /* nessun terminatore */

static void prova_pulizia(PhFinto *f)
{
    PhSrvLettura ok = PH_SRV_LET_OK;
    ph_srv_chiamate_osserva(S, true);
    ph_srv_rete_osserva(S, true);
    ph_srv_media_osserva(S, true);
    CONTROLLA(aspetta(chi_stato, &ok, 3000) && aspetta(rete_stato, &ok, 3000) && aspetta(media_stato_l, &ok, 3000));
    barriera();

    /* Chiamate: n > 20, tipo fuori elenco, stringhe senza terminatore e con UTF-8 rotto. */
    PhSrvChiamate *c = g_new0(PhSrvChiamate, 1);
    c->stato = PH_SRV_LET_OK;
    c->n = 99;
    for (size_t i = 0; i < PH_SRV_CHIAMATE_MAX; i++) {
        c->v[i].tipo = 9;
        riempi(c->v[i].nome, sizeof c->v[i].nome);
        riempi(c->v[i].numero, sizeof c->v[i].numero);
    }
    memcpy(c->v[1].numero, "ab\xff\xfe", 4);
    memset(c->v[1].numero + 4, 0, sizeof c->v[1].numero - 4);
    int ev[2] = {PH_SRV_EV_CHIAMATE, CONTA[PH_SRV_EV_CHIAMATE]};
    ph_servizi_pubblica_chiamate(S, c);
    CONTROLLA(aspetta(ev_oltre, ev, 3000));
    const PhSrvChiamate *ch = ph_srv_chiamate(S);
    CONTROLLA(ch->n == PH_SRV_CHIAMATE_MAX && ch->v[0].tipo == PH_SRV_CHIAMATA_ALTRO);
    CONTROLLA(strlen(ch->v[0].nome) < sizeof ch->v[0].nome && strlen(ch->v[0].numero) < sizeof ch->v[0].numero);
    CONTROLLA(strcmp(ch->v[1].numero, "ab") == 0);
    c->stato = (PhSrvLettura)9; /* fuori elenco: non disponibile, nessuna riga */
    ev[1] = CONTA[PH_SRV_EV_CHIAMATE];
    ph_servizi_pubblica_chiamate(S, c);
    CONTROLLA(aspetta(ev_oltre, ev, 3000));
    CONTROLLA(ph_srv_chiamate(S)->stato == PH_SRV_LET_NON_DISPONIBILE && ph_srv_chiamate(S)->n == 0);
    g_free(c);

    /* Rete: ogni campo fuori elenco; dati_mobili e aereo = 7 diventano «ignoto», mai si o no. */
    PhSrvReteDettaglio r;
    memset(&r, 0, sizeof r);
    r.stato = PH_SRV_LET_OK;
    r.wifi = 9;
    r.wifi_livello = 9;
    r.mobile = 9;
    r.mobile_livello = 9;
    r.generazione = 7;
    r.dati_mobili = 7;
    r.aereo = 7;
    riempi(r.wifi_nome, sizeof r.wifi_nome);
    riempi(r.operatore, sizeof r.operatore);
    ev[0] = PH_SRV_EV_RETE;
    ev[1] = CONTA[PH_SRV_EV_RETE];
    ph_servizi_pubblica_rete(S, &r);
    CONTROLLA(aspetta(ev_oltre, ev, 3000));
    const PhSrvReteDettaglio *rr = ph_srv_rete(S);
    CONTROLLA(rr->wifi == PH_SRV_WIFI_SPENTO && rr->mobile == PH_SRV_MOBILE_ASSENTE);
    CONTROLLA(rr->wifi_livello == 4 && rr->mobile_livello == 4 && rr->generazione == 0);
    CONTROLLA(rr->dati_mobili == PH_SRV_TRI_IGNOTO && rr->aereo == PH_SRV_TRI_IGNOTO);
    CONTROLLA(strlen(rr->wifi_nome) < sizeof rr->wifi_nome && strlen(rr->operatore) < sizeof rr->operatore);
    r.stato = (PhSrvLettura)9;
    ev[1] = CONTA[PH_SRV_EV_RETE];
    ph_servizi_pubblica_rete(S, &r);
    CONTROLLA(aspetta(ev_oltre, ev, 3000));
    CONTROLLA(ph_srv_rete(S)->stato == PH_SRV_LET_NON_DISPONIBILE);

    /* Media: stato fuori elenco, maschera dei comandi, stringhe rotte. */
    PhSrvMedia m;
    memset(&m, 0, sizeof m);
    m.stato_lettura = PH_SRV_LET_OK;
    m.stato = 9;
    m.comandi = 0xff;
    riempi(m.titolo, sizeof m.titolo);
    riempi(m.artista, sizeof m.artista);
    riempi(m.pacchetto, sizeof m.pacchetto);
    memcpy(m.artista, "ok\xc3", 3);
    memset(m.artista + 3, 0, sizeof m.artista - 3);
    ev[0] = PH_SRV_EV_MEDIA;
    ev[1] = CONTA[PH_SRV_EV_MEDIA];
    ph_servizi_pubblica_media(S, &m);
    CONTROLLA(aspetta(ev_oltre, ev, 3000));
    const PhSrvMedia *mm = ph_srv_media(S);
    CONTROLLA(mm->stato == PH_SRV_MEDIA_NESSUNA && mm->comandi == 7);
    CONTROLLA(strlen(mm->titolo) < sizeof mm->titolo && strlen(mm->pacchetto) < sizeof mm->pacchetto);
    CONTROLLA(strcmp(mm->artista, "ok") == 0);

    /* Pubblicazione tardiva: dopo osserva(false) non arriva nessun evento. L'ordine delle consegne
     * e' quello d'invio: la rete (osservata) pubblicata DOPO e' il segnale che la tardiva e' passata. */
    ph_srv_chiamate_osserva(S, false);
    int chi0 = CONTA[PH_SRV_EV_CHIAMATE];
    PhSrvChiamate tardiva;
    memset(&tardiva, 0, sizeof tardiva);
    tardiva.stato = PH_SRV_LET_OK;
    ph_servizi_pubblica_chiamate(S, &tardiva);
    ev[0] = PH_SRV_EV_RETE;
    ev[1] = CONTA[PH_SRV_EV_RETE];
    r.stato = PH_SRV_LET_OK;
    ph_servizi_pubblica_rete(S, &r);
    CONTROLLA(aspetta(ev_oltre, ev, 3000));
    CONTROLLA(CONTA[PH_SRV_EV_CHIAMATE] == chi0);
    ph_srv_rete_osserva(S, false);
    ph_srv_media_osserva(S, false);
    ph_finto_media_assente(f, false);
    barriera();
}

/* ---- Meteo: collegamento, citta' a mano, ricerca dei luoghi ------------------- */
static bool met_fonte_pronto(void *d)
{
    const PhSrvMeteo *m = ph_srv_meteo(S_METEO);
    return m->stato == PH_SRV_METEO_PRONTO && ph_srv_meteo_fonte(S_METEO) == *(PhSrvMeteoFonte *)d;
}

static PhMeteoLuogo luogo(const char *nome, int32_t lat, int32_t lon)
{
    PhMeteoLuogo l = {0};
    g_strlcpy(l.nome, nome, sizeof l.nome);
    g_strlcpy(l.dettaglio, "Sample Region, Sample Country", sizeof l.dettaglio);
    l.lat_centesimi = lat;
    l.lon_centesimi = lon;
    return l;
}

static void cerca_e_aspetta(const char *testo, const char *lingua, Lu *lu)
{
    ph_srv_meteo_cerca(S, testo, lingua, NULL, luoghi_fatti, lu);
    CONTROLLA(lu->chiamate == 0); /* mai dentro la chiamata */
    CONTROLLA(aspetta(lu_finito, lu, 3000));
}

static void prova_meteo_citta(PhFinto *f, int *conta)
{
    CONTA = conta;
    S_METEO = S;
    PhSrvMeteoStato st_pronto = PH_SRV_METEO_PRONTO, st_attesa = PH_SRV_METEO_IN_ATTESA,
                    st_noncoll = PH_SRV_METEO_NON_COLLEGATO, st_senzapos = PH_SRV_METEO_SENZA_POSIZIONE,
                    st_spento = PH_SRV_METEO_SPENTO;
    PhSrvMeteoFonte f_tel = PH_SRV_METEO_FONTE_TELEFONO, f_mano = PH_SRV_METEO_FONTE_A_MANO;
    PhMeteoLuogo borgo = luogo("Sample Town", -1500, 3010);

    /* Ricerca con il meteo spento: rifiutata (nessun collegamento senza widget). */
    Lu l0 = {0};
    cerca_e_aspetta("sample", "it", &l0);
    CONTROLLA(l0.esito == PH_SRV_ESITO_ERRORE && l0.ha_frase && !l0.v_non_null);

    /* Citta' scelta prima di accendere + posizione spenta: la prima pubblicazione e' la citta'. */
    ph_srv_meteo_localita(S, &borgo);
    ph_finto_meteo_modo(f, PH_FINTO_METEO_SENZA_POSIZIONE);
    barriera();
    ph_srv_meteo_osserva(S, true);
    CONTROLLA(aspetta(met_fonte_pronto, &f_mano, 3000));
    const PhSrvMeteo *m = ph_srv_meteo(S);
    CONTROLLA(m->ha_dati && m->frase[0] == '\0' && strcmp(m->localita, "Sample Town") == 0);
    /* La posizione del telefono vince: con la posizione accesa la citta' non conta. */
    ph_finto_meteo_modo(f, PH_FINTO_METEO_PRONTO);
    CONTROLLA(aspetta(met_fonte_pronto, &f_tel, 3000));
    CONTROLLA(strcmp(ph_srv_meteo(S)->localita, "Sample City") == 0);
    /* Nessun telefono (comando): citta' di nuovo. */
    ph_finto_meteo_modo(f, PH_FINTO_METEO_NON_COLLEGATO);
    CONTROLLA(aspetta(met_fonte_pronto, &f_mano, 3000));
    ph_finto_meteo_modo(f, PH_FINTO_METEO_PRONTO);
    CONTROLLA(aspetta(met_fonte_pronto, &f_tel, 3000));
    /* Citta' tolta, o non valida (coordinate fuori limite, nome vuoto): SENZA_POSIZIONE. */
    PhMeteoLuogo fuori = luogo("Fuori", 9001, 0), senza_nome = luogo("", 0, 0);
    PhMeteoLuogo *scartate[] = {NULL, &fuori, &senza_nome};
    ph_finto_meteo_modo(f, PH_FINTO_METEO_SENZA_POSIZIONE);
    for (size_t i = 0; i < G_N_ELEMENTS(scartate); i++) {
        ph_srv_meteo_localita(S, &borgo);
        CONTROLLA(aspetta(met_fonte_pronto, &f_mano, 3000));
        ph_srv_meteo_localita(S, scartate[i]);
        CONTROLLA(aspetta(met_stato, &st_senzapos, 3000));
        CONTROLLA(ph_srv_meteo(S)->frase[0] != '\0');
    }
    ph_srv_meteo_localita(S, &borgo);
    CONTROLLA(aspetta(met_fonte_pronto, &f_mano, 3000));
    ph_finto_meteo_modo(f, PH_FINTO_METEO_PRONTO);
    CONTROLLA(aspetta(met_fonte_pronto, &f_tel, 3000));

    /* Telefono perso, senza citta': NON_COLLEGATO con i dati vecchi; al ritorno la prima risposta riparte. */
    ph_srv_meteo_localita(S, NULL);
    barriera();
    int64_t ricevuta = 0;
    double temp = 0;
    CONTROLLA(met_pronto_con_dati(f, &ricevuta, &temp));
    ph_finto_perdi(f, 1);
    CONTROLLA(aspetta(met_stato, &st_noncoll, 3000));
    m = ph_srv_meteo(S);
    CONTROLLA(m->ha_dati && m->frase[0] != '\0' && m->dati.ricevuta == ricevuta &&
              fabs(m->dati.adesso.temperatura - temp) < 1e-9 && ph_srv_meteo_fonte(S) == PH_SRV_METEO_FONTE_TELEFONO);
    CONTROLLA(aspetta(met_stato, &st_attesa, 4000)); /* telefono tornato: si riparte */
    CONTROLLA(aspetta(met_stato, &st_pronto, 3000));
    /* Telefono perso con una citta' scelta: la citta' prende il posto; al ritorno vince il telefono. */
    ph_srv_meteo_localita(S, &borgo);
    barriera();
    ph_finto_perdi(f, 1);
    CONTROLLA(aspetta(met_fonte_pronto, &f_mano, 3000));
    CONTROLLA(strcmp(ph_srv_meteo(S)->localita, "Sample Town") == 0);
    CONTROLLA(aspetta(met_stato, &st_attesa, 4000));
    CONTROLLA(aspetta(met_fonte_pronto, &f_tel, 3000));
    CONTROLLA(strcmp(ph_srv_meteo(S)->localita, "Sample City") == 0);

    /* Ricerca dei luoghi (meteo osservato). */
    Lu l1 = {0}, l2 = {0}, l3 = {0}, l4 = {0}, l5 = {0};
    cerca_e_aspetta("CITY", "it", &l1); /* senza badare alle maiuscole */
    CONTROLLA(l1.esito == PH_SRV_ESITO_OK && l1.n == 2 && strcmp(l1.primo, "Sample City") == 0 &&
              l1.dettaglio[0] != '\0' && l1.v_non_null);
    cerca_e_aspetta("sampl", "en", &l2); /* matchano tutti e 12: ne arrivano 10 */
    CONTROLLA(l2.esito == PH_SRV_ESITO_OK && l2.n == PH_METEO_LUOGHI_MAX);
    cerca_e_aspetta("zzz", "it", &l3);
    CONTROLLA(l3.esito == PH_SRV_ESITO_OK && l3.n == 0 && l3.v_non_null && !l3.ha_frase);
    cerca_e_aspetta("  lighthouse  ", "it", &l4); /* spazi ai lati tolti */
    CONTROLLA(l4.esito == PH_SRV_ESITO_OK && l4.n == 1 && strcmp(l4.primo, "Sample Lighthouse") == 0);
    Lu lc = {0};
    cerca_e_aspetta("CAFÉ", "en", &lc); /* maiuscole e lettera accentata: «é» si trova */
    CONTROLLA(lc.esito == PH_SRV_ESITO_OK && lc.n == 1 && strcmp(lc.primo, "Café Central") == 0);
    ph_finto_meteo_ricerca_errore(f, true);
    cerca_e_aspetta("sample", "it", &l5);
    CONTROLLA(l5.esito == PH_SRV_ESITO_ERRORE && l5.ha_frase && !l5.v_non_null && l5.n == 0);
    ph_finto_meteo_ricerca_errore(f, false);
    /* Testi e lingue non validi: ERRORE con frase (rifiutati dal pezzo comune). */
    char lungo[PH_METEO_RICERCA_TESTO_MAX + 2];
    memset(lungo, 'a', sizeof lungo);
    lungo[sizeof lungo - 1] = '\0';
    static const char *const BRUTTI[] = {"a", "   ", "", "\xff\xfe"};
    for (size_t i = 0; i < G_N_ELEMENTS(BRUTTI); i++) {
        Lu lb = {0};
        cerca_e_aspetta(BRUTTI[i], "it", &lb);
        CONTROLLA(lb.esito == PH_SRV_ESITO_ERRORE && lb.ha_frase && !lb.v_non_null);
    }
    Lu l6 = {0}, l7 = {0}, l8 = {0}, l9 = {0};
    cerca_e_aspetta(lungo, "it", &l6);
    cerca_e_aspetta("sample", "fr", &l7);
    cerca_e_aspetta("sample", NULL, &l8);
    cerca_e_aspetta(NULL, "it", &l9);
    CONTROLLA(l6.esito == PH_SRV_ESITO_ERRORE && l7.esito == PH_SRV_ESITO_ERRORE && l8.esito == PH_SRV_ESITO_ERRORE &&
              l9.esito == PH_SRV_ESITO_ERRORE);
    /* Ricerche lente in volo: contano finche' non sono annullate (le concluse non contano). */
    CONTROLLA(ph_finto_meteo_ricerche_non_annullate(f) == 0);
    ph_finto_meteo_ricerca_lenta(f, true);
    GCancellable *v1 = g_cancellable_new(), *v2 = g_cancellable_new();
    Lu lv1 = {0}, lv2 = {0};
    ph_srv_meteo_cerca(S, "sample", "it", v1, luoghi_fatti, &lv1);
    CONTROLLA(ph_finto_meteo_ricerche_non_annullate(f) == 1);
    ph_srv_meteo_cerca(S, "sample", "it", v2, luoghi_fatti, &lv2);
    CONTROLLA(ph_finto_meteo_ricerche_non_annullate(f) == 2);
    g_cancellable_cancel(v1);
    CONTROLLA(ph_finto_meteo_ricerche_non_annullate(f) == 1);
    g_cancellable_cancel(v2);
    CONTROLLA(ph_finto_meteo_ricerche_non_annullate(f) == 0);
    g_object_unref(v1);
    g_object_unref(v2);
    ph_finto_meteo_ricerca_lenta(f, false);
    /* Annullata prima e durante: nessuna risposta. La terza (viva) torna dopo le altre (stesso ritardo). */
    Lu a1 = {0}, a2 = {0}, a3 = {0};
    GCancellable *an1 = g_cancellable_new(), *an2 = g_cancellable_new();
    g_cancellable_cancel(an1);
    ph_srv_meteo_cerca(S, "sample", "it", an1, luoghi_fatti, &a1);
    ph_srv_meteo_cerca(S, "sample", "it", an2, luoghi_fatti, &a2);
    g_cancellable_cancel(an2);
    ph_srv_meteo_cerca(S, "lighthouse", "it", NULL, luoghi_fatti, &a3);
    CONTROLLA(aspetta(lu_finito, &a3, 3000) && a3.esito == PH_SRV_ESITO_OK);
    CONTROLLA(a1.chiamate == 0 && a2.chiamate == 0);
    g_object_unref(an1);
    g_object_unref(an2);

    ph_srv_meteo_osserva(S, false);
    CONTROLLA(aspetta(met_stato, &st_spento, 3000));
    ph_srv_meteo_localita(S, NULL);
    barriera();
}

/* Rami del pezzo comune provati con un backend minimo scritto qui. */
static void prova_pubblica_meteo(PhServizi *s);

typedef struct {
    PhSrvLavoro *tenuto;
    int chiamate_cerca;
    int localita_chiamate;
    bool localita_nulla;
    char localita_nome[128];
} Fin;

static void fin_chiudi(void *b)
{
    Fin *d = b;
    if (d->tenuto != NULL) { /* il lavoro ancora aperto si chiude con la chiusura (contratto) */
        ph_servizi_lavoro_luoghi(d->tenuto, PH_SRV_ESITO_ERRORE, "chiuso", NULL, 0);
        d->tenuto = NULL;
    }
}

static void fin_localita(void *b, const PhMeteoLuogo *l)
{
    Fin *d = b;
    d->localita_chiamate++;
    d->localita_nulla = l == NULL;
    g_strlcpy(d->localita_nome, l != NULL ? l->nome : "", sizeof d->localita_nome);
}

static void fin_cerca(void *b, PhSrvLavoro *l, const char *testo, const char *lingua)
{
    Fin *d = b;
    (void)lingua;
    d->chiamate_cerca++;
    PhMeteoLuogo v[150];
    memset(v, 0, sizeof v);
    for (size_t i = 0; i < G_N_ELEMENTS(v); i++) {
        g_snprintf(v[i].nome, sizeof v[i].nome, "Luogo finto %zu", i);
        v[i].lat_centesimi = (int32_t)i * 10;
        v[i].lon_centesimi = (int32_t)i * 20;
    }
    if (strcmp(testo, "misto") == 0) { /* 14 luoghi, tre non validi tra i primi 10: se ne tengono comunque 10 */
        v[0].nome[0] = '\0';
        v[1].lat_centesimi = 9001;
        v[2].lon_centesimi = -18001;
        ph_servizi_lavoro_luoghi(l, PH_SRV_ESITO_OK, NULL, v, 14);
    } else if (strcmp(testo, "tetto") == 0) { /* i primi 100 non validi, il 101-esimo valido: mai esaminato */
        for (size_t i = 0; i < 100; i++)
            v[i].nome[0] = '\0';
        ph_servizi_lavoro_luoghi(l, PH_SRV_ESITO_OK, NULL, v, 120);
    } else if (strcmp(testo, "quasi") == 0) { /* 99 non validi, poi validi: il 100-esimo si esamina */
        for (size_t i = 0; i < 99; i++)
            v[i].nome[0] = '\0';
        ph_servizi_lavoro_luoghi(l, PH_SRV_ESITO_OK, NULL, v, 120);
    } else if (strcmp(testo, "sporchi") == 0) { /* byte di controllo, UTF-8 non valido, parola tagliata */
        g_strlcpy(v[0].nome, "Bo\x01rgo", sizeof v[0].nome);
        g_strlcpy(v[1].nome, "Ca\xff" "sa", sizeof v[1].nome);
        g_strlcpy(v[2].nome, "Pae\xc3", sizeof v[2].nome);
        g_strlcpy(v[2].dettaglio, "Reg\x1b" "ione\xc3", sizeof v[2].dettaglio);
        g_strlcpy(v[1].dettaglio, "\x07\x08", sizeof v[1].dettaglio);
        ph_servizi_lavoro_luoghi(l, PH_SRV_ESITO_OK, NULL, v, 3);
    } else if (strcmp(testo, "senzazero") == 0) {
        /* nome e dettaglio di 128 byte senza zero finale, in un buffer di misura esatta (ASan) */
        PhMeteoLuogo *e = g_malloc(sizeof *e);
        memset(e, 'a', sizeof e->nome);
        memset(e->dettaglio, 'b', sizeof e->dettaglio);
        e->lat_centesimi = 1;
        e->lon_centesimi = 2;
        ph_servizi_lavoro_luoghi(l, PH_SRV_ESITO_OK, NULL, e, 1);
        g_free(e);
    } else if (strcmp(testo, "molti") == 0) {
        ph_servizi_lavoro_luoghi(l, PH_SRV_ESITO_OK, NULL, v, 14);
    } else if (strcmp(testo, "vuoto") == 0) {
        ph_servizi_lavoro_luoghi(l, PH_SRV_ESITO_OK, NULL, NULL, 0);
    } else if (strcmp(testo, "errore") == 0) {
        ph_servizi_lavoro_luoghi(l, PH_SRV_ESITO_ERRORE, "Il servizio non risponde.", v, 3);
    } else if (strcmp(testo, "doppia") == 0) { /* una sola risposta finale */
        ph_servizi_lavoro_luoghi(l, PH_SRV_ESITO_OK, NULL, v, 1);
        ph_servizi_lavoro_luoghi(l, PH_SRV_ESITO_ERRORE, "seconda", NULL, 0);
    } else if (strcmp(testo, "tieni") == 0) {
        d->tenuto = l;
    } else {
        ph_servizi_lavoro_fatto(l, PH_SRV_ESITO_ERRORE, "boh");
    }
}

/* ph_servizi_pubblica_meteo_fonte: stringhe ripulite, valori fuori elenco ricondotti. */
static void prova_pubblica_meteo(PhServizi *s)
{
    static int conta3[PH_SRV_EV_NUMERO]; /* l'ascoltatore resta fino alla chiusura dei servizi */
    int *vecchia_conta = CONTA;
    CONTA = conta3;
    ph_srv_ascolta(s, PH_SRV_EV_METEO, ev_qualunque, conta3);
    int atteso = 0;

    /* (a) località e frase con byte di controllo e lettera accentata tagliata a meta' all'ultimo byte utile. */
    PhSrvMeteo m;
    memset(&m, 0, sizeof m);
    m.stato = PH_SRV_METEO_PRONTO;
    memset(m.localita, 'a', 126);
    m.localita[126] = '\xc3'; /* prima meta' di una lettera a due byte; il byte 127 e' lo zero */
    memset(m.frase, 'b', 190);
    m.frase[190] = '\xc3';
    m.localita[3] = '\x01';
    m.frase[5] = '\x1b';
    ph_servizi_pubblica_meteo_fonte(s, &m, PH_SRV_METEO_FONTE_A_MANO);
    atteso++;
    int un = atteso - 1;
    CONTROLLA(aspetta(met_eventi_oltre, &un, 3000));
    const PhSrvMeteo *r = ph_srv_meteo(s);
    CONTROLLA(g_utf8_validate(r->localita, -1, NULL) && g_utf8_validate(r->frase, -1, NULL));
    CONTROLLA(strchr(r->localita, '\x01') == NULL && strchr(r->frase, '\x1b') == NULL);
    CONTROLLA(strlen(r->localita) >= 100 && strlen(r->frase) >= 100 && strncmp(r->localita, "aaa", 3) == 0);
    CONTROLLA(ph_srv_meteo_fonte(s) == PH_SRV_METEO_FONTE_A_MANO);
    /* Anche una lettera accentata intera al limite e un byte non valido in mezzo. */
    memset(&m, 0, sizeof m);
    m.stato = PH_SRV_METEO_PRONTO;
    g_strlcpy(m.localita, "Cit\xff" "ta\x7f", sizeof m.localita);
    g_strlcpy(m.frase, "Fr\x02" "ase", sizeof m.frase);
    ph_servizi_pubblica_meteo_fonte(s, &m, PH_SRV_METEO_FONTE_TELEFONO);
    un = ++atteso - 1;
    CONTROLLA(aspetta(met_eventi_oltre, &un, 3000));
    r = ph_srv_meteo(s);
    CONTROLLA(g_utf8_validate(r->localita, -1, NULL) && g_utf8_validate(r->frase, -1, NULL));
    CONTROLLA(strchr(r->localita, '\x7f') == NULL && strchr(r->frase, '\x02') == NULL && r->localita[0] == 'C');
    CONTROLLA(ph_srv_meteo_fonte(s) == PH_SRV_METEO_FONTE_TELEFONO);

    /* (d) stato e fonte fuori elenco, ore e giorni oltre il massimo. */
    memset(&m, 0, sizeof m);
    m.stato = (PhSrvMeteoStato)99;
    m.ha_dati = true;
    m.dati.n_ore = 1000;
    m.dati.n_giorni = 1000;
    ph_servizi_pubblica_meteo_fonte(s, &m, (PhSrvMeteoFonte)77);
    un = ++atteso - 1;
    CONTROLLA(aspetta(met_eventi_oltre, &un, 3000));
    r = ph_srv_meteo(s);
    CONTROLLA(r->stato == PH_SRV_METEO_NON_DISPONIBILE && ph_srv_meteo_fonte(s) == PH_SRV_METEO_FONTE_NESSUNA);
    CONTROLLA(r->dati.n_ore == PH_METEO_ORE_MAX && r->dati.n_giorni == PH_METEO_GIORNI_MAX);
    CONTA = vecchia_conta;
}

static void prova_meteo_comune(void)
{
    Fin d = {0};
    PhServiziBackend t = {.chiudi = fin_chiudi, .meteo_localita = fin_localita, .meteo_cerca = fin_cerca};
    PhServizi *vecchio = S;
    PhServizi *s3 = ph_servizi_nuovi(&t, &d);
    CONTROLLA(s3 != NULL);
    if (s3 == NULL)
        return;
    S = s3;
    /* Senza voce nel backend: ERRORE (e localita non fa nulla). */
    Fin vuoto = {0};
    PhServiziBackend senza = {0};
    PhServizi *s4 = ph_servizi_nuovi(&senza, &vuoto);
    CONTROLLA(s4 != NULL);
    if (s4 != NULL) {
        Lu l = {0};
        ph_srv_meteo_cerca(s4, "sample", "it", NULL, luoghi_fatti, &l);
        CONTROLLA(l.chiamate == 0 && aspetta(lu_finito, &l, 3000) && l.esito == PH_SRV_ESITO_ERRORE && l.ha_frase &&
                  !l.v_non_null);
        PhMeteoLuogo b = luogo("Sample Town", 1, 2);
        ph_srv_meteo_localita(s4, &b);
        ph_srv_meteo_localita(s4, NULL);
        ph_servizi_libera(s4);
    }
    /* Meteo non osservato: ERRORE senza chiamare il backend, una sola risposta, dopo il ritorno. */
    Lu no = {0};
    ph_srv_meteo_cerca(S, "molti", "it", NULL, luoghi_fatti, &no);
    CONTROLLA(no.chiamate == 0);
    CONTROLLA(aspetta(lu_finito, &no, 3000) && no.esito == PH_SRV_ESITO_ERRORE && no.ha_frase && !no.v_non_null);
    gint64 poco = g_get_monotonic_time() + 200000;
    CONTROLLA(aspetta(tempo_trascorso, &poco, 1000));
    CONTROLLA(no.chiamate == 1 && d.chiamate_cerca == 0);
    /* Con un widget Meteo sul desktop le ricerche partono. */
    ph_srv_meteo_osserva(S, true);
    /* Testi/lingue non validi non arrivano al backend. */
    Lu l1 = {0}, l2 = {0}, l3 = {0}, l4 = {0}, l5 = {0};
    char lungo[PH_METEO_RICERCA_TESTO_MAX + 2];
    memset(lungo, 'a', sizeof lungo);
    lungo[sizeof lungo - 1] = '\0';
    cerca_e_aspetta("a", "it", &l1);
    cerca_e_aspetta("   ", "it", &l2);
    cerca_e_aspetta(lungo, "it", &l3);
    cerca_e_aspetta("misto", "de", &l4);
    cerca_e_aspetta(NULL, "it", &l5);
    CONTROLLA(d.chiamate_cerca == 0);
    CONTROLLA(l1.esito == PH_SRV_ESITO_ERRORE && l2.esito == PH_SRV_ESITO_ERRORE && l3.esito == PH_SRV_ESITO_ERRORE &&
              l4.esito == PH_SRV_ESITO_ERRORE && l5.esito == PH_SRV_ESITO_ERRORE);
    /* Ricerca valida: spazi tolti, luoghi non validi scartati, al massimo 10, lista vuota non NULL. */
    Lu m1 = {0}, m2 = {0}, m3 = {0}, m4 = {0}, m5 = {0};
    cerca_e_aspetta("  misto ", "en", &m1);
    CONTROLLA(m1.esito == PH_SRV_ESITO_OK && m1.n == PH_METEO_LUOGHI_MAX && strcmp(m1.primo, "Luogo finto 3") == 0);
    Lu t1 = {0}, t2 = {0};
    cerca_e_aspetta("tetto", "it", &t1);
    CONTROLLA(t1.esito == PH_SRV_ESITO_OK && t1.n == 0 && t1.v_non_null); /* il 101-esimo valido non si guarda */
    cerca_e_aspetta("quasi", "it", &t2);
    CONTROLLA(t2.esito == PH_SRV_ESITO_OK && t2.n == 1 && strcmp(t2.primo, "Luogo finto 99") == 0);
    cerca_e_aspetta("molti", "it", &m2);
    CONTROLLA(m2.esito == PH_SRV_ESITO_OK && m2.n == PH_METEO_LUOGHI_MAX);
    cerca_e_aspetta("vuoto", "it", &m3);
    CONTROLLA(m3.esito == PH_SRV_ESITO_OK && m3.n == 0 && m3.v_non_null);
    cerca_e_aspetta("errore", "it", &m4);
    CONTROLLA(m4.esito == PH_SRV_ESITO_ERRORE && m4.ha_frase && !m4.v_non_null && m4.n == 0);
    ph_srv_meteo_cerca(S, "doppia", "it", NULL, luoghi_fatti, &m5);
    CONTROLLA(aspetta(lu_finito, &m5, 3000));
    gint64 fine = g_get_monotonic_time() + 200000; /* il tempo che una seconda risposta (vietata) impiegherebbe */
    CONTROLLA(aspetta(tempo_trascorso, &fine, 1000));
    CONTROLLA(m5.chiamate == 1 && m5.esito == PH_SRV_ESITO_OK && m5.n == 1);
    CONTROLLA(d.chiamate_cerca == 7);
    /* Risultati sporchi o senza zero finale: arrivano ripuliti, UTF-8 valida, stringhe terminate. */
    Lu sp = {0}, sz = {0}, cc = {0};
    cerca_e_aspetta("sporchi", "it", &sp);
    CONTROLLA(sp.esito == PH_SRV_ESITO_OK && sp.n >= 1 && !sp.sporco);
    cerca_e_aspetta("senzazero", "it", &sz);
    CONTROLLA(sz.esito == PH_SRV_ESITO_OK && sz.n == 1 && !sz.sporco && strlen(sz.primo) >= 100);
    /* Carattere di controllo nel testo: ERRORE di convalida, backend non chiamato. */
    cerca_e_aspetta("ab\x01" "cd", "it", &cc);
    CONTROLLA(cc.esito == PH_SRV_ESITO_ERRORE && cc.ha_frase && !cc.v_non_null && d.chiamate_cerca == 9);
    PhMeteoLuogo *senza_zero = g_malloc(sizeof *senza_zero);
    memset(senza_zero, 'a', sizeof senza_zero->nome);
    memset(senza_zero->dettaglio, 'b', sizeof senza_zero->dettaglio);
    senza_zero->lat_centesimi = 1;
    senza_zero->lon_centesimi = 2;
    ph_srv_meteo_localita(S, senza_zero); /* ASan: nessuna lettura oltre il buffer */
    CONTROLLA(!d.localita_nulla && strlen(d.localita_nome) <= 127 && g_utf8_validate(d.localita_nome, -1, NULL));
    g_free(senza_zero);
    prova_pubblica_meteo(S);
    /* Annullata prima della chiamata e durante: nessuna risposta. */
    Lu a1 = {0}, a2 = {0};
    GCancellable *an1 = g_cancellable_new(), *an2 = g_cancellable_new();
    g_cancellable_cancel(an1);
    ph_srv_meteo_cerca(S, "molti", "it", an1, luoghi_fatti, &a1);
    ph_srv_meteo_cerca(S, "tieni", "it", an2, luoghi_fatti, &a2);
    CONTROLLA(d.tenuto != NULL);
    g_cancellable_cancel(an2);
    CONTROLLA(d.tenuto != NULL); /* il pezzo comune deve aver chiamato il backend */
    if (d.tenuto != NULL)
        ph_servizi_lavoro_luoghi(d.tenuto, PH_SRV_ESITO_OK, NULL, NULL, 0); /* arriva, ma nessuno la vuole */
    d.tenuto = NULL;
    Lu viva = {0};
    cerca_e_aspetta("vuoto", "it", &viva); /* torna dopo le altre due (stesso thread di consegna) */
    CONTROLLA(a1.chiamate == 0 && a2.chiamate == 0 && viva.esito == PH_SRV_ESITO_OK);
    g_object_unref(an1);
    g_object_unref(an2);
    /* Citta' a mano: valida (ripulita), fuori limite e nome vuoto = NULL. */
    PhMeteoLuogo ok = luogo("Bo\x01rgo finto", 100, 200), fuori1 = luogo("X", 9001, 0), fuori2 = luogo("X", 0, -18001),
                 vuoto_nome = luogo("", 0, 0);
    ph_srv_meteo_localita(S, &ok);
    CONTROLLA(d.localita_chiamate == 2 && !d.localita_nulla && strchr(d.localita_nome, '\x01') == NULL &&
              strncmp(d.localita_nome, "Bo", 2) == 0);
    PhMeteoLuogo *brutti[] = {&fuori1, &fuori2, &vuoto_nome, NULL};
    for (size_t i = 0; i < G_N_ELEMENTS(brutti); i++) {
        ph_srv_meteo_localita(S, &ok);
        ph_srv_meteo_localita(S, brutti[i]);
        CONTROLLA(d.localita_nulla && d.localita_nome[0] == '\0');
    }
    /* Spento il widget, la ricerca torna a essere rifiutata; riacceso, riparte. */
    ph_srv_meteo_osserva(S, false);
    Lu spento = {0};
    int prima_cerca = d.chiamate_cerca;
    cerca_e_aspetta("vuoto", "it", &spento);
    CONTROLLA(spento.esito == PH_SRV_ESITO_ERRORE && d.chiamate_cerca == prima_cerca);
    ph_srv_meteo_osserva(S, true);
    /* Chiusura dei servizi con una ricerca ancora aperta: nessuna risposta, ne' ora ne' dopo. */
    Lu c1 = {0};
    ph_srv_meteo_cerca(S, "tieni", "it", NULL, luoghi_fatti, &c1);
    CONTROLLA(d.tenuto != NULL);
    ph_servizi_libera(s3);
    for (int i = 0; i < 30; i++)
        g_main_context_iteration(NULL, FALSE);
    CONTROLLA(c1.chiamate == 0 && d.tenuto == NULL);
    S = vecchio;
}

int main(int argc, char **argv)
{
    (void)argc;
    principale = g_thread_self();
    g_setenv("TZ", "Europe/Rome", TRUE); /* ora legale: vedi prova_widget */
    tzset();
    PhFintoOpzioni o = {.notifiche_ogni_s = -1, .ritardo_ms = 10};
    PhFinto *f = ph_finto_nuovo(&o);
    CONTROLLA(f != NULL);
    if (f == NULL)
        return 1;
    S = ph_finto_servizi(f);
    FINTO = f;
    int conta[PH_SRV_EV_NUMERO] = {0};
    for (int ev = 0; ev < PH_SRV_EV_NUMERO; ev++)
        ph_srv_ascolta(S, (PhSrvEvento)ev, ev_qualunque, conta);

    /* Collegamento, app, notifiche, profilo. */
    CONTROLLA(aspetta(collegato, NULL, 3000));
    CONTROLLA(aspetta(app_pronte, NULL, 3000));
    CONTROLLA(ph_srv_app_cerca(S, "it.finto.messaggi") != NULL);
    CONTROLLA(ph_srv_app_cerca(S, "it.finto.messaggi")->solo_verticale);
    size_t nn;
    const char *problema = "x";
    CONTROLLA(aspetta(notifiche_arrivate, conta, 3000));
    ph_srv_notifiche(S, &nn, &problema);
    CONTROLLA(nn == 3 && problema == NULL);
    CONTROLLA(aspetta(profilo_arrivato, conta, 3000)); /* consegnato dopo l'elenco delle app */
    CONTROLLA(ph_srv_profilo(S)->n_icone == 4);
    CONTROLLA(ph_srv_profilo(S)->n_fissate == 2 &&
              ph_cfg_telefono_app_fissata(ph_srv_profilo(S), "it.finto.calcolatrice"));

    /* Icona: prima NULL, poi la texture 128x128 con l'avviso. */
    CONTROLLA(ph_srv_icona(S, "it.finto.note") == NULL);
    CONTROLLA(aspetta(icona_arrivata, "it.finto.note", 3000));
    GdkTexture *t = ph_srv_icona(S, "it.finto.note");
    CONTROLLA(t != NULL && gdk_texture_get_width(t) == PH_SRV_ICONA_LATO);
    CONTROLLA(conta[PH_SRV_EV_ICONA] >= 1);

    /* Elenchi: normale, non consentito, inesistente; annullamento. */
    Esito e1 = {0}, e2 = {0}, e3 = {0}, e4 = {0};
    ph_srv_tel_elenca(S, "/storage/emulated/0", NULL, elenco_fatto, &e1);
    ph_srv_tel_elenca(S, "/data", NULL, elenco_fatto, &e2);
    ph_srv_tel_elenca(S, "/non/esiste", NULL, elenco_fatto, &e3);
    GCancellable *an = g_cancellable_new();
    ph_srv_tel_elenca(S, "/sdcard", an, elenco_fatto, &e4);
    CONTROLLA(!e1.fatto); /* mai dentro la chiamata */
    g_cancellable_cancel(an);
    CONTROLLA(aspetta(fatto, &e1, 3000) && aspetta(fatto, &e2, 3000) && aspetta(fatto, &e3, 3000));
    CONTROLLA(e1.esito == PH_SRV_ESITO_OK && e1.ha_download);
    CONTROLLA(e2.esito == PH_SRV_ESITO_NON_CONSENTITO);
    CONTROLLA(e3.esito == PH_SRV_ESITO_NON_TROVATO);
    /* Sentinella: una richiesta uguale fatta DOPO; il telefono finto risponde
     * nell'ordine, quindi quando arriva la sua risposta quella annullata, se
     * doveva arrivare, sarebbe gia' arrivata. */
    Esito e5 = {0};
    ph_srv_tel_elenca(S, "/sdcard", NULL, elenco_fatto, &e5);
    CONTROLLA(aspetta(fatto, &e5, 3000));
    CONTROLLA(!e4.fatto); /* annullata: la risposta non arriva */
    g_object_unref(an);

    /* Ricerca. */
    Esito ec = {0};
    ph_srv_tel_cerca(S, "/storage/emulated/0", "pdf", false, 0, NULL, cerca_trovati, cerca_fine, &ec);
    CONTROLLA(aspetta(fatto, &ec, 3000));
    CONTROLLA(ec.esito == PH_SRV_ESITO_OK && ec.trovati >= 5 && !ec.massimo);
    CONTROLLA(ec.dimensione_vista); /* (2) la dimensione dei file trovati */
    Esito em = {0};
    ph_srv_tel_cerca(S, "/storage/emulated/0", "a", false, 2, NULL, cerca_trovati, cerca_fine, &em);
    CONTROLLA(aspetta(fatto, &em, 3000) && em.massimo);

    /* Trasferimento: si invia il programma di prova stesso (un file che c'e'). */
    PhErrore err = {{0}};
    CONTROLLA(ph_srv_tr_accoda(S, PH_SRV_INVIO, argv[0], "/sdcard/Download", &err) == 0);
    CONTROLLA(ph_srv_tr_accoda(S, PH_SRV_INVIO, "/non/esiste", "/sdcard", &err) == -1);
    CONTROLLA(aspetta(tr_completato, NULL, 20000));
    CONTROLLA(!ph_srv_trasferimenti_attivi(S, PH_SRV_INVIO));

    /* Trasferimenti recenti (pezzo comune): il completato c'e' e resta anche
     * dopo che la coda toglie i finiti. */
    size_t nr = 0;
    const PhSrvTrasferimento *rec = ph_srv_trasferimenti_recenti(S, &nr);
    CONTROLLA(nr == 1 && rec[0].stato == PH_SRV_TR_COMPLETATO && rec[0].nome != NULL);
    ph_srv_tr_rimuovi_finiti(S);
    Esito sentinella = {0};
    ph_srv_tel_elenca(S, "/sdcard", NULL, elenco_fatto, &sentinella); /* risponde dopo la nuova fotografia */
    CONTROLLA(aspetta(fatto, &sentinella, 3000));
    ph_srv_trasferimenti_recenti(S, &nr);
    CONTROLLA(nr == 1);

    /* (3) Annullare un verso: le due di invio si annullano, la ricezione no. */
    char *qui = g_get_current_dir();
    CONTROLLA(ph_srv_tr_accoda(S, PH_SRV_RICEZIONE, "/storage/emulated/0/Download/notes.txt", qui, &err) == 0);
    CONTROLLA(ph_srv_tr_accoda(S, PH_SRV_INVIO, argv[0], "/sdcard/Download", &err) == 0);
    CONTROLLA(ph_srv_tr_accoda(S, PH_SRV_INVIO, argv[0], "/sdcard/Download", &err) == 0);
    ph_srv_tr_annulla_verso(S, PH_SRV_INVIO);
    CONTROLLA(aspetta(tr_verso_annullato, NULL, 3000));
    CONTROLLA(!ricezione_annullata());
    ph_srv_tr_annulla_verso(S, PH_SRV_RICEZIONE);

    /* (1) Installazione: riuscita con avanzamento, rifiutata, annullata, non valida. */
    Inst i1 = {0}, i2 = {0}, i3 = {0}, i4 = {0};
    CONTROLLA(ph_srv_apk_installa(S, argv[0], inst_av, inst_fatto, &i1) > 0);
    CONTROLLA(aspetta(inst_finita, &i1, 5000));
    CONTROLLA(i1.esito == PH_SRV_INST_RIUSCITO && i1.ha_messaggio && i1.avanzamenti >= 1);
    CONTROLLA(i1.ultimo_totale > 0 && i1.ultimo_fatti == i1.ultimo_totale);
    ph_finto_installazioni_rifiutate(f, true);
    CONTROLLA(ph_srv_apk_installa(S, argv[0], NULL, inst_fatto, &i2) > 0);
    CONTROLLA(aspetta(inst_finita, &i2, 5000));
    CONTROLLA(i2.esito == PH_SRV_INST_RIFIUTATO && i2.ha_codice && i2.ha_messaggio);
    ph_finto_installazioni_rifiutate(f, false);
    guint id3 = ph_srv_apk_installa(S, argv[0], inst_av, inst_fatto, &i3);
    CONTROLLA(id3 > 0);
    ph_srv_installazione_annulla(S, id3);
    CONTROLLA(aspetta(inst_finita, &i3, 5000));
    CONTROLLA(i3.esito == PH_SRV_INST_ANNULLATO);
    CONTROLLA(ph_srv_apk_installa(S, "relativo.apk", NULL, inst_fatto, &i4) == 0);
    CONTROLLA(!i4.fatto); /* mai dentro la chiamata */
    CONTROLLA(aspetta(inst_finita, &i4, 3000) && i4.esito == PH_SRV_INST_ERRORE && i4.ha_messaggio);
    /* Disinstallazione: di sistema rifiutata, normale riuscita (sparisce dall'elenco), nome non valido. */
    Inst d1 = {0}, d2 = {0}, d3 = {0};
    CONTROLLA(ph_srv_app_disinstalla(S, "it.finto.impostazioni", inst_fatto, &d1) > 0);
    CONTROLLA(ph_srv_app_disinstalla(S, "it.finto.meteo", inst_fatto, &d2) > 0);
    CONTROLLA(ph_srv_app_disinstalla(S, "1;rm -rf", inst_fatto, &d3) == 0);
    CONTROLLA(aspetta(inst_finita, &d1, 3000) && d1.esito == PH_SRV_INST_RIFIUTATO);
    CONTROLLA(aspetta(inst_finita, &d2, 3000) && d2.esito == PH_SRV_INST_RIUSCITO);
    CONTROLLA(aspetta(senza_meteo, NULL, 3000));
    CONTROLLA(aspetta(inst_finita, &d3, 3000) && d3.esito == PH_SRV_INST_ERRORE);

    /* (4) Pannello con esito: fatto, rifiutato per chiamata, telefono bloccato. */
    Pan p1 = {0}, p2 = {0}, p3 = {0};
    ph_srv_pannello_chiedi(S, true, NULL, pan_fatto, &p1);
    CONTROLLA(aspetta(pan_finito, &p1, 3000) && p1.esito == PH_SRV_PAN_FATTO);
    PhSrvTelefono tel;
    ph_srv_telefono(S, &tel);
    CONTROLLA(tel.pannello_acceso); /* la fotografia arriva prima dell'esito, stesso thread */
    ph_finto_pannello_risposta(f, PH_SRV_PAN_CHIAMATA);
    ph_srv_pannello_chiedi(S, false, NULL, pan_fatto, &p2);
    CONTROLLA(aspetta(pan_finito, &p2, 3000) && p2.esito == PH_SRV_PAN_CHIAMATA);
    ph_finto_pannello_risposta(f, PH_SRV_PAN_FATTO);
    ph_finto_blocca(f, true);
    ph_srv_pannello_chiedi(S, false, NULL, pan_fatto, &p3);
    CONTROLLA(aspetta(pan_finito, &p3, 3000) && p3.esito == PH_SRV_PAN_BLOCCATO);
    ph_finto_blocca(f, false);
    CONTROLLA(aspetta(collegato, NULL, 3000));

    /* (5) Memoria per categoria e calendario. */
    Wid w1 = {0}, w2 = {0}, w3 = {0}, w4 = {0}, w5 = {0};
    ph_srv_tel_memoria_dettaglio(S, NULL, dett_fatto, &w1);
    CONTROLLA(aspetta(wid_finito, &w1, 3000) && w1.esito == PH_SRV_ESITO_OK);
    CONTROLLA(w1.d.immagini.leggibile && w1.d.immagini.byte > 0 && !w1.d.app.leggibile && w1.d.app.byte == 0);
    ph_srv_tel_calendario(S, 3, NULL, cal_fatto, &w2);
    ph_srv_tel_calendario(S, 0, NULL, cal_fatto, &w3);
    ph_srv_tel_calendario(S, 100, NULL, cal_fatto, &w4);
    CONTROLLA(aspetta(wid_finito, &w2, 3000) && w2.esito == PH_SRV_ESITO_OK && w2.n == 3 && w2.ordinati);
    CONTROLLA(aspetta(wid_finito, &w3, 3000) && w3.n == 1);
    CONTROLLA(aspetta(wid_finito, &w4, 3000) && w4.n == 6 && w4.ultimo_giorno_intero);
    ph_finto_calendario_illeggibile(f, true);
    ph_srv_tel_calendario(S, 4, NULL, cal_fatto, &w5);
    CONTROLLA(aspetta(wid_finito, &w5, 3000) && w5.esito == PH_SRV_ESITO_ERRORE && w5.ha_frase && w5.n == 0);
    ph_finto_calendario_illeggibile(f, false);

    /* (6) Prima lettura: dopo il collegamento iniziale la generazione e' 1;
     * dopo una caduta e ritorno con prima lettura vuota diventa 2, la base e'
     * vuota e le notifiche arrivano dopo, con la stessa generazione. */
    CONTROLLA(ph_srv_notifiche_generazione(S) == 1 && ph_srv_notifiche_pronte(S));
    ph_finto_prima_lettura_vuota(f, true);
    ph_finto_perdi(f, 1);
    CONTROLLA(aspetta(perso, NULL, 2000));
    CONTROLLA(!ph_srv_notifiche_pronte(S));
    gen_attesa = 2;
    CONTROLLA(aspetta(generazione, NULL, 4000));
    size_t nb;
    ph_srv_notifiche(S, &nb, NULL);
    CONTROLLA(nb == 0 && ph_srv_notifiche_pronte(S));
    int prima_ev = conta[PH_SRV_EV_NOTIFICHE] + 1;
    gint64 lim_n = g_get_monotonic_time() + 3000000;
    while (conta[PH_SRV_EV_NOTIFICHE] < prima_ev && g_get_monotonic_time() < lim_n) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    ph_srv_notifiche(S, &nb, NULL);
    CONTROLLA(nb == 3 && ph_srv_notifiche_generazione(S) == 2);
    ph_finto_prima_lettura_vuota(f, false);

    /* (9) Schermo spento dal sistema, distinto dal blocco. */
    bool si = true, no = false;
    ph_finto_schermo_spento(f, true);
    CONTROLLA(aspetta(schermo_spento, &si, 3000));
    PhSrvCollegamento col;
    ph_srv_collegamento(S, &col);
    CONTROLLA(col.stato == PH_SRV_COLLEGATO);
    ph_finto_schermo_spento(f, false);
    CONTROLLA(aspetta(schermo_spento, &no, 3000));

    /* Flusso: lettura da un altro thread, ridimensiona, pausa, chiusura. */
    PhSrvFlussoRichiesta r = {"it.finto.note", 400, 240, 0, 0};
    Lettura l = {0};
    l.f = ph_srv_flusso_apri(S, &r, avviso_flusso, NULL, &err);
    CONTROLLA(l.f != NULL);
    PhSrvFlussoRichiesta storta = {"it.finto.note", 401, 240, 0, 0};
    CONTROLLA(ph_srv_flusso_apri(S, &storta, avviso_flusso, NULL, NULL) == NULL);
    /* Pagine (27 nov): una pagina nella richiesta e' registrata dal telefono finto; pagina 0 no; fuori
     * elenco (3) e' rifiutata prima di toccare il backend. Il flusso `l.f` qui sopra e' di pagina 0. */
    CONTROLLA(ph_finto_pagine_apertura(f) == 0 && ph_finto_ultima_pagina(f) == 0);
    PhSrvFlussoRichiesta con_pagina = {"it.finto.note", 400, 240, 0, PH_SRV_PAGINA_MEMORIA};
    PhSrvFlusso *fpg = ph_srv_flusso_apri(S, &con_pagina, avviso_flusso, NULL, &err);
    CONTROLLA(fpg != NULL && ph_finto_pagine_apertura(f) == 1 && ph_finto_ultima_pagina(f) == PH_SRV_PAGINA_MEMORIA);
    if (fpg != NULL)
        ph_srv_flusso_chiudi(fpg, false);
    PhSrvFlussoRichiesta fuori_elenco = {"it.finto.note", 400, 240, 0, 3};
    CONTROLLA(ph_srv_flusso_apri(S, &fuori_elenco, avviso_flusso, NULL, NULL) == NULL &&
              ph_finto_pagine_apertura(f) == 1);
    if (l.f != NULL) {
        GThread *th = g_thread_new("lettura", leggi, &l);
        CONTROLLA(aspetta(almeno, &l, 3000));
        CONTROLLA(misure_dichiarate >= 1);
        ph_srv_flusso_tocco(l.f, 400, 240, PH_SRV_TOCCO_GIU, 10, 10, 1);
        ph_srv_flusso_tocco(l.f, 400, 240, PH_SRV_TOCCO_SU, 10, 10, 0);
        ph_srv_flusso_ridimensiona(l.f, 480, 320);
        CONTROLLA(aspetta(misura_480, &l, 3000));
        /* Densita' (modalita' tablet): all'apertura quella del telefono (420);
         * un cambio di SOLA densita' arriva come MISURA; 0 = invariata; fuori
         * intervallo: scartato senza raggiungere il telefono. */
        CONTROLLA(ph_finto_ultima_densita(f) == 420);
        misure_prima = misure_dichiarate;
        densita_voluta = 280;
        ph_srv_flusso_ridimensiona_densita(l.f, 480, 320, 280);
        CONTROLLA(aspetta(densita_arrivata, NULL, 3000));
        misure_prima = misure_dichiarate;
        densita_voluta = 280;
        ph_srv_flusso_ridimensiona_densita(l.f, 400, 240, 0); /* densita' invariata, misura nuova */
        CONTROLLA(aspetta(densita_arrivata, NULL, 3000) && ultima_misura_w == 400);
        ph_srv_flusso_ridimensiona_densita(l.f, 400, 240, 10);  /* scartato */
        ph_srv_flusso_ridimensiona_densita(l.f, 480, 320, 200); /* questo arriva dopo */
        misure_prima = misure_dichiarate;
        densita_voluta = 200;
        CONTROLLA(aspetta(densita_arrivata, NULL, 3000) && ultima_misura_w == 480);
        /* Pausa: il flag e' messo subito; si aspetta che la lettura torni due
         * volte «niente» (100 ms senza fotogrammi), poi altre due: in mezzo non
         * deve arrivare nessun fotogramma. */
        ph_srv_flusso_pausa(l.f);
        zeri_attesi = atomic_load(&l.zeri) + 2;
        CONTROLLA(aspetta(zeri, &l, 3000));
        int prima = atomic_load(&l.fotogrammi);
        zeri_attesi = atomic_load(&l.zeri) + 2;
        CONTROLLA(aspetta(zeri, &l, 3000));
        CONTROLLA(atomic_load(&l.fotogrammi) == prima); /* in pausa non arriva nulla */
        ph_srv_flusso_riprendi(l.f);
        int ripresa = atomic_load(&l.fotogrammi);
        gint64 lim = g_get_monotonic_time() + 2000000;
        while (atomic_load(&l.fotogrammi) < ripresa + 5 && g_get_monotonic_time() < lim) {
            g_main_context_iteration(NULL, FALSE);
            g_usleep(1000);
        }
        CONTROLLA(atomic_load(&l.fotogrammi) >= ripresa + 5);

        /* Pizzico: passi validi arrivano (la chiamata e' sincrona fino al
         * finto); fase sconosciuta, coordinate fuori dal video o misura
         * diversa: scartati. */
        unsigned p0 = ph_finto_pizzichi(f);
        int vw = 480, vh = 320; /* misura attuale dopo le prove della densita' */
        ph_srv_flusso_pizzico(l.f, vw, vh, PH_SRV_PIZZICO_INIZIO, 10, 10, 120);
        ph_srv_flusso_pizzico(l.f, vw, vh, PH_SRV_PIZZICO_CONTINUA, 10, 10, 99999);
        ph_srv_flusso_pizzico(l.f, vw, vh, PH_SRV_PIZZICO_FINE, 10, 10, 0);
        CONTROLLA(ph_finto_pizzichi(f) == p0 + 3);
        ph_srv_flusso_pizzico(l.f, vw, vh, 7, 10, 10, 0);
        ph_srv_flusso_pizzico(l.f, vw, vh, PH_SRV_PIZZICO_INIZIO, vw, 10, 0);
        ph_srv_flusso_pizzico(l.f, vw + 8, vh, PH_SRV_PIZZICO_INIZIO, 10, 10, 0);
        CONTROLLA(ph_finto_pizzichi(f) == p0 + 3);

        /* (9) Pagina su un flusso aperto: sincrona fino al finto; 0 e fuori elenco scartate prima. */
        unsigned pg0 = ph_finto_pagine_inviate(f);
        ph_srv_flusso_pagina(l.f, PH_SRV_PAGINA_BATTERIA);
        CONTROLLA(ph_finto_pagine_inviate(f) == pg0 + 1 && ph_finto_ultima_pagina(f) == PH_SRV_PAGINA_BATTERIA);
        ph_srv_flusso_pagina(l.f, PH_SRV_PAGINA_NESSUNA);
        ph_srv_flusso_pagina(l.f, 9);
        CONTROLLA(ph_finto_pagine_inviate(f) == pg0 + 1 && ph_finto_ultima_pagina(f) == PH_SRV_PAGINA_BATTERIA);
        ph_finto_pagina_rifiuta(f, true); /* rifiutata: contata, il flusso resta com'era */
        ph_srv_flusso_pagina(l.f, PH_SRV_PAGINA_MEMORIA);
        ph_finto_pagina_rifiuta(f, false);
        CONTROLLA(ph_finto_pagine_inviate(f) == pg0 + 2 && ph_finto_ultima_pagina(f) == PH_SRV_PAGINA_MEMORIA);

        /* (8) Fotogramma chiave a richiesta. */
        unsigned k0 = ph_finto_chiavi_chieste(f);
        ph_srv_flusso_chiave(l.f);
        CONTROLLA(ph_finto_chiavi_chieste(f) == k0 + 1);

        /* (7) Screenshot: due di fila nello stesso secondo -> due file diversi, mai sovrascritti. */
        char *cartella = g_strdup_printf("%s/prova-screenshot-%d", qui, (int)getpid());
        Scatto s1 = {0}, s2 = {0}, s3 = {0};
        ph_srv_flusso_screenshot(l.f, PH_SRV_FOTO_FILE | PH_SRV_FOTO_APPUNTI, cartella, "Note / prova", NULL,
                                 scatto_fatto, &s1);
        ph_srv_flusso_screenshot(l.f, PH_SRV_FOTO_FILE, cartella, "Note / prova", NULL, scatto_fatto, &s2);
        ph_srv_flusso_screenshot(l.f, PH_SRV_FOTO_FILE, "relativa", "Note", NULL, scatto_fatto, &s3);
        CONTROLLA(aspetta(scatto_finito, &s1, 3000) && aspetta(scatto_finito, &s2, 3000));
        CONTROLLA(aspetta(scatto_finito, &s3, 3000) && s3.esito == PH_SRV_ESITO_ERRORE);
        CONTROLLA(s1.esito == PH_SRV_ESITO_OK && s1.png && s1.percorso != NULL);
        CONTROLLA(s2.esito == PH_SRV_ESITO_OK && !s2.png && s2.percorso != NULL);
        CONTROLLA(s1.percorso != NULL && s2.percorso != NULL && strcmp(s1.percorso, s2.percorso) != 0);
        CONTROLLA(s1.percorso != NULL && g_file_test(s1.percorso, G_FILE_TEST_IS_REGULAR));
        CONTROLLA(s2.percorso != NULL && g_file_test(s2.percorso, G_FILE_TEST_IS_REGULAR));
        CONTROLLA(s1.percorso != NULL && strstr(s1.percorso, "Note _ prova ") != NULL); /* nome ripulito */
        if (s1.percorso != NULL)
            g_unlink(s1.percorso);
        if (s2.percorso != NULL)
            g_unlink(s2.percorso);
        g_rmdir(cartella);
        g_free(s1.percorso);
        g_free(s2.percorso);
        g_free(s3.percorso);

        /* (7) Registrazione: avvio, seconda rifiutata senza toccare la prima, fine; poi guasta. */
        Reg r1 = {0}, r2 = {0}, r3 = {0};
        ph_srv_flusso_registra(l.f, cartella, "Note", true, reg_avviso, &r1);
        CONTROLLA(ph_srv_flusso_registrando(l.f));
        CONTROLLA(aspetta(reg_iniziata, &r1, 3000));
        ph_srv_flusso_registra(l.f, cartella, "Note", true, reg_avviso, &r2);
        CONTROLLA(aspetta(reg_errore, &r2, 3000) && r1.errore == 0);
        CONTROLLA(ph_srv_flusso_registrando(l.f));
        ph_srv_flusso_registra_ferma(l.f);
        CONTROLLA(aspetta(reg_finita, &r1, 3000) && !ph_srv_flusso_registrando(l.f));
        ph_finto_registrazione_guasta(f, true);
        ph_srv_flusso_registra(l.f, cartella, "Note", false, reg_avviso, &r3);
        CONTROLLA(aspetta(reg_errore, &r3, 3000) && !ph_srv_flusso_registrando(l.f));
        ph_finto_registrazione_guasta(f, false);
        g_free(cartella);

        /* (9) Schermo spento: i fotogrammi si fermano e poi ripartono. */
        ph_finto_schermo_spento(f, true);
        CONTROLLA(aspetta(schermo_spento, &si, 3000));
        zeri_attesi = atomic_load(&l.zeri) + 2;
        CONTROLLA(aspetta(zeri, &l, 3000));
        int ferma = atomic_load(&l.fotogrammi);
        zeri_attesi = atomic_load(&l.zeri) + 2;
        CONTROLLA(aspetta(zeri, &l, 3000));
        CONTROLLA(atomic_load(&l.fotogrammi) == ferma);
        ph_finto_schermo_spento(f, false);
        CONTROLLA(aspetta(schermo_spento, &no, 3000));
        CONTROLLA(aspetta(almeno, &l, 3000));

        ph_srv_flusso_interrompi(l.f);
        g_thread_join(th);
        ph_srv_flusso_chiudi(l.f, true);
        CONTROLLA(!atomic_load(&l.fuori));
    }

    g_free(qui);

    /* Caduta e ritorno (la generazione delle notifiche sale ancora). */
    gen_attesa = ph_srv_notifiche_generazione(S) + 1;
    ph_finto_perdi(f, 1);
    CONTROLLA(aspetta(perso, NULL, 2000));
    CONTROLLA(aspetta(collegato, NULL, 4000));
    CONTROLLA(aspetta(generazione, NULL, 4000));

    prova_widget(f, conta);
    prova_nuovi(f, conta);
    prova_pulizia(f);
    prova_meteo(f, conta);
    prova_meteo_citta(f, conta);
    prova_meteo_comune();
    ph_finto_libera(f);
    for (int i = 0; i < 20; i++)
        g_main_context_iteration(NULL, FALSE);
    prova_prima_connessione();
    CONTROLLA(avvisi_fuori_thread == 0);
    f = NULL;
    for (int i = 0; i < 50; i++)
        g_main_context_iteration(NULL, FALSE);
    if (errori == 0)
        printf("prova_finto: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
