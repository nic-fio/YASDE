/* Prova dei servizi veri SENZA telefono e senza schermo, sopra pezzi sotto
 * finti (finti_sotto.c) che dormono 300 ms come la rete. Controlla: nessuna
 * funzione della tabella blocca il thread GTK; i risultati tornano dopo, sul
 * thread GTK; i prestiti presi sono restituiti alla caduta e alla chiusura;
 * ricerche, appunti e coda dei trasferimenti si chiudono prima; il flusso
 * diventa PERSO e al ritorno si riapre da solo; chiudere con richieste in
 * corso non lascia nulla appeso. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/lingua.h"
#include "finti_sotto.h"
#include "ossatura.h"
#include "shell/servizi_backend.h"
#include "shell/shell.h"
#include "sv_interno.h"

static int errori;
#define CONTROLLA(c)                                                                               \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "prova_servizi_veri: fallito: %s (riga %d)\n", #c, __LINE__);          \
            errori++;                                                                              \
        }                                                                                          \
    } while (0)

/* Gira il ciclo principale finche' la condizione e' vera o scade il tempo. */
#define ASPETTA(cond, ms)                                                                          \
    do {                                                                                           \
        gint64 _fine = g_get_monotonic_time() + (gint64)(ms)*1000;                                 \
        while (!(cond) && g_get_monotonic_time() < _fine) {                                        \
            while (g_main_context_iteration(NULL, FALSE)) {}                                       \
            g_usleep(1000);                                                                        \
        }                                                                                          \
    } while (0)

/* Verifica negativa: per `ms` la condizione non deve diventare falsa (dopo un segnale positivo). */
#define RESTA(cond, ms)                                                                            \
    do {                                                                                           \
        gint64 _fine = g_get_monotonic_time() + (gint64)(ms)*1000;                                 \
        while (g_get_monotonic_time() < _fine) {                                                   \
            while (g_main_context_iteration(NULL, FALSE)) {}                                       \
            if (!(cond)) {                                                                         \
                CONTROLLA(cond);                                                                   \
                break;                                                                             \
            }                                                                                      \
            g_usleep(1000);                                                                        \
        }                                                                                          \
    } while (0)

/* ---- Il collegamento finto avvisa da un thread suo ---------------------------- */
typedef struct {
    int tipo; /* 0 avviso di stato, 1 in perdita */
    PhColStato stato;
    const char *motivo;
    int trovati;
    bool arbitraria;
    int esito; /* PhColEsitoAbbinamento dell'avviso */
} Fuoco;

static gpointer fuoco_corpo(gpointer p)
{
    Fuoco *f = p;
    if (f->tipo == 1) {
        finti.opz.in_perdita(finti.opz.utente);
    } else {
        PhColAvviso a;
        memset(&a, 0, sizeof a);
        a.stato = f->stato;
        a.ha_telefono = f->stato == PH_COL_COLLEGATO || f->stato == PH_COL_BLOCCATO;
        snprintf(a.motivo, sizeof a.motivo, "%s", f->motivo != NULL ? f->motivo : "frase");
        a.telefoni_trovati = f->trovati;
        a.scelta_arbitraria = f->arbitraria;
        a.esito_abbinamento = (PhColEsitoAbbinamento)f->esito;
        snprintf(a.telefono.nome, sizeof a.telefono.nome, "Modello finto");
        finti.opz.avviso(&a, finti.opz.utente);
    }
    return NULL;
}

static void fuoco_ex(PhColStato stato, const char *motivo, int trovati, bool arbitraria)
{
    Fuoco f = {0, stato, motivo, trovati, arbitraria, PH_COL_ABB_NESSUNO};
    GThread *t = g_thread_new("fuoco", fuoco_corpo, &f);
    g_thread_join(t);
}

/* L'avviso che CONCLUDE una richiesta di abbinamento (con il suo esito). */
static void fuoco_esito(PhColStato stato, const char *motivo, PhColEsitoAbbinamento esito)
{
    Fuoco f = {0, stato, motivo, 0, false, (int)esito};
    GThread *t = g_thread_new("fuoco", fuoco_corpo, &f);
    g_thread_join(t);
}

static void fuoco(int tipo, PhColStato stato)
{
    Fuoco f = {tipo, stato, NULL, 0, false, PH_COL_ABB_NESSUNO};
    GThread *t = g_thread_new("fuoco", fuoco_corpo, &f);
    g_thread_join(t);
}

/* ---- Richiamate della shell -------------------------------------------------------- */
static int n_pan_fatto;
static PhSrvPannelloEsito es_pan_fatto;
static void cb_pan_fatto(PhServizi *s, PhSrvPannelloEsito es, const char *frase, void *u)
{
    (void)s; (void)frase; (void)u;
    es_pan_fatto = es;
    n_pan_fatto++;
}
static int n_inst, n_dett, n_cal;
static int n_elenco, n_trovati_voci, n_cerca_fine, n_fatto, n_immagine, n_byte, n_memoria;
static int fine_esito[4];
static PhSrvEsito esito_elenco;
static size_t voci_elenco;
static bool nel_gtk_sempre = true;

static void cb_elenco(PhServizi *s, PhSrvEsito es, const char *frase, const char *cartella, const PhSrvVoce *v,
                      size_t n, void *u)
{
    (void)s; (void)frase; (void)cartella; (void)v; (void)u;
    nel_gtk_sempre = nel_gtk_sempre && ph_shell_nel_thread_gtk();
    esito_elenco = es;
    voci_elenco = n;
    n_elenco++;
}

static void cb_trovati(PhServizi *s, const PhSrvTrovato *v, size_t n, void *u)
{
    (void)s; (void)v; (void)u;
    n_trovati_voci += (int)n;
}

static void cb_cerca_fine(PhServizi *s, PhSrvEsito es, const char *frase, bool massimo, void *u)
{
    (void)s; (void)frase; (void)massimo;
    nel_gtk_sempre = nel_gtk_sempre && ph_shell_nel_thread_gtk();
    int k = GPOINTER_TO_INT(u);
    fine_esito[k] = (int)es;
    n_cerca_fine++;
}

static void cb_fatto(PhServizi *s, PhSrvEsito es, const char *frase, void *u)
{
    (void)s; (void)es; (void)frase; (void)u;
    n_fatto++;
}

static void cb_immagine(PhServizi *s, PhSrvEsito es, const char *frase, GdkTexture *t, void *u)
{
    (void)s; (void)es; (void)frase; (void)t; (void)u;
    n_immagine++;
}

static void cb_byte(PhServizi *s, PhSrvEsito es, const char *frase, GBytes *b, void *u)
{
    (void)s; (void)es; (void)frase; (void)b; (void)u;
    n_byte++;
}

static void cb_memoria(PhServizi *s, PhSrvEsito es, uint64_t tot, uint64_t lib, void *u)
{
    (void)s; (void)es; (void)tot; (void)lib; (void)u;
    n_memoria++;
}

static void cb_inst(PhServizi *s, const PhSrvInstRisultato *r, void *u)
{
    (void)s; (void)u;
    if (r->esito == PH_SRV_INST_RIUSCITO)
        n_inst++;
}
static void cb_dett(PhServizi *s, PhSrvEsito es, const char *f, const PhSrvMemoriaDettaglio *d, void *u)
{
    (void)s; (void)f; (void)u;
    if (es == PH_SRV_ESITO_OK && d->immagini.leggibile)
        n_dett++;
}
static void cb_cal(PhServizi *s, PhSrvEsito es, const char *f, const PhSrvEventoCalendario *v, size_t n, void *u)
{
    (void)s; (void)f; (void)v; (void)n; (void)u;
    if (es == PH_SRV_ESITO_ERRORE) /* il finto non ha calendario: errore con frase, mai elenco finto */
        n_cal++;
}
static int n_scatto, n_scatto_ok, ev_reg_inizio, ev_reg_fine;
static void cb_scatto(PhServizi *s, PhSrvEsito es, const char *f, const char *perc, GBytes *png, void *u)
{
    (void)s; (void)f; (void)perc; (void)png; (void)u;
    n_scatto++;
    if (es == PH_SRV_ESITO_OK)
        n_scatto_ok++;
}
static const char *frase_reg_fine;
static bool reg_finita_con_frase;
static void cb_reg(PhSrvFlusso *f, const PhSrvRegDati *d, void *u)
{
    (void)f; (void)u;
    if (d->evento == PH_SRV_REG_INIZIATA)
        ev_reg_inizio++;
    if (d->evento == PH_SRV_REG_FINITA) {
        ev_reg_fine++;
        frase_reg_fine = d->frase;
        reg_finita_con_frase = d->frase != NULL;
    }
}
static atomic_int frames;
static atomic_int gestiti, ridim_fatti, input_fatti;
static void su_input(void) { atomic_fetch_add(&input_fatti, 1); }
static void su_ridim(void) { atomic_fetch_add(&ridim_fatti, 1); }
static void su_gestito(void) { atomic_fetch_add(&gestiti, 1); }
/* Pagina su un flusso aperto: ogni lavoro di pagina (mandato o no) segnala qui l'esito. */
static atomic_int pag_fatte, pag_esito;
static void su_pagina(int esito)
{
    atomic_store(&pag_esito, esito);
    atomic_fetch_add(&pag_fatte, 1);
}
static gpointer lettore_corpo(gpointer p)
{
    PhSrvFlusso *f = p;
    PhFotogramma fr;
    int r;
    while ((r = ph_srv_flusso_leggi(f, &fr, 50)) >= 0) {
        if (r == 1) {
            atomic_fetch_add(&frames, 1);
            ph_fotogramma_rilascia(&fr);
        }
    }
    return NULL;
}
static int n_abb;
static PhSrvAbbEsito es_abb;
static char frase_abb[200];
static void cb_abb(PhServizi *s, PhSrvAbbEsito es, const char *frase, void *u)
{
    (void)s; (void)u;
    nel_gtk_sempre = nel_gtk_sempre && ph_shell_nel_thread_gtk();
    es_abb = es;
    snprintf(frase_abb, sizeof frase_abb, "%s", frase != NULL ? frase : "");
    n_abb++;
}
static int in_corso_all_evento = -1;
static void ev_collegamento(PhServizi *s, PhSrvEvento ev, const char *arg, void *u)
{
    (void)ev; (void)arg; (void)u;
    in_corso_all_evento = ph_srv_abbinamento_in_corso(s); /* com'e' quando la schermata riceve l'evento */
}
static PhSrvStato stato_srv(PhServizi *s)
{
    PhSrvCollegamento c;
    ph_srv_collegamento(s, &c);
    return c.stato;
}

static int ev_aperto, ev_perso, ev_ripreso, ev_altro;
static void cb_flusso(PhSrvFlusso *f, const PhSrvFlussoDati *d, void *u)
{
    (void)f; (void)u;
    nel_gtk_sempre = nel_gtk_sempre && ph_shell_nel_thread_gtk();
    switch (d->evento) {
    case PH_SRV_FL_APERTO: ev_aperto++; break;
    case PH_SRV_FL_PERSO: ev_perso++; break;
    case PH_SRV_FL_RIPRESO: ev_ripreso++; break;
    default: ev_altro++; break;
    }
}

/* Una chiamata della tabella deve tornare subito (i pezzi sotto dormono 300 ms). */
static gint64 t0;
#define INIZIO() (t0 = g_get_monotonic_time())
#define FINE_VELOCE(nome)                                                                          \
    do {                                                                                           \
        gint64 _dt = g_get_monotonic_time() - t0;                                                  \
        if (_dt > 50 * 1000) {                                                                     \
            fprintf(stderr, "prova_servizi_veri: %s ha impiegato %ld us sul thread GTK\n", nome,   \
                    (long)_dt);                                                                    \
            errori++;                                                                              \
        }                                                                                          \
    } while (0)

static size_t numero_app(PhServizi *s)
{
    size_t n = 0;
    ph_srv_app(s, &n);
    return n;
}

static size_t numero_notifiche(PhServizi *s)
{
    size_t n = 0;
    ph_srv_notifiche(s, &n, NULL);
    return n;
}

static int n_volume;
static void avviso_volume(PhServizi *s, PhSrvEvento ev, const char *arg, void *u)
{
    (void)s; (void)ev; (void)arg; (void)u;
    n_volume++;
}

static void avviso_vuoto(PhServizi *s, PhSrvEvento ev, const char *arg, void *u)
{
    (void)s; (void)ev; (void)arg; (void)u;
}

/* Un pezzo che smette di ascoltare DOPO la liberazione dei servizi (il "destroy"
 * della radice arrivato tardi, in un giro successivo del ciclo principale) non
 * deve toccare memoria liberata: senza il registro degli oggetti vivi e'
 * un uso dopo la liberazione, e ASan lo vede. */
static void prova_smetti_dopo_libera(void)
{
    PhServiziBackend vuoto;
    memset(&vuoto, 0, sizeof vuoto);
    PhServizi *x = ph_servizi_nuovi(&vuoto, NULL);
    CONTROLLA(x != NULL);
    guint id = ph_srv_ascolta(x, PH_SRV_EV_APP, avviso_vuoto, NULL);
    CONTROLLA(id != 0);
    ph_servizi_libera(x);
    ph_srv_smetti(x, id); /* dopo la liberazione: non fa nulla */

    /* Gli id degli ascolti sono unici nel processo: l'id di un oggetto liberato
     * non puo' togliere l'ascolto di uno nuovo (anche se nasce allo stesso
     * indirizzo). */
    PhServizi *y = ph_servizi_nuovi(&vuoto, NULL);
    guint id2 = ph_srv_ascolta(y, PH_SRV_EV_VOLUME, avviso_volume, NULL);
    CONTROLLA(id2 != 0 && id2 != id);
    ph_srv_smetti(y, id);
    PhSrvVolume vol = {50, false};
    ph_servizi_pubblica_volume(y, &vol);
    ASPETTA(n_volume == 1, 2000);
    CONTROLLA(n_volume == 1);
    ph_servizi_libera(y);
}


/* ---- Widget del 23 nov pomeriggio: richiamate ---------------------------------------------- */
static int n_calmese, n_sveglia, n_batt, n_f2, n_ev_ctl;
static PhSrvEsito es_calmese, es_sveglia, es_batt, es_f2;
static size_t ev_calmese;
static bool tronc_calmese, presente_sveglia;
static char app_calmese[64], pacc_sveglia[64], frase_f2[128];
static int livello_batt;
static PhSrvBatStato stato_batt;

static void cb_calmese(PhServizi *s, PhSrvEsito es, const char *frase, const PhSrvEventoCalendario *v, size_t n,
                       bool troncato, const char *app, void *u)
{
    (void)s; (void)frase; (void)v; (void)u;
    nel_gtk_sempre = nel_gtk_sempre && ph_shell_nel_thread_gtk();
    es_calmese = es;
    ev_calmese = n;
    tronc_calmese = troncato;
    snprintf(app_calmese, sizeof app_calmese, "%s", app != NULL ? app : "(NULL)");
    n_calmese++;
}

static void cb_sveglia(PhServizi *s, PhSrvEsito es, const char *frase, bool presente, uint64_t ora, const char *pacc,
                       void *u)
{
    (void)s; (void)frase; (void)ora; (void)u;
    es_sveglia = es;
    presente_sveglia = presente;
    snprintf(pacc_sveglia, sizeof pacc_sveglia, "%s", pacc != NULL ? pacc : "(NULL)");
    n_sveglia++;
}

static void cb_batt(PhServizi *s, PhSrvEsito es, const char *frase, const PhSrvBatteria *b, void *u)
{
    (void)s; (void)frase; (void)u;
    es_batt = es;
    livello_batt = b != NULL ? b->livello : -1;
    stato_batt = b != NULL ? b->stato : PH_SRV_BAT_SCONOSCIUTO;
    n_batt++;
}

static void cb_f2(PhServizi *s, PhSrvEsito es, const char *frase, void *u)
{
    (void)s; (void)u;
    es_f2 = es;
    snprintf(frase_f2, sizeof frase_f2, "%s", frase != NULL ? frase : "");
    n_f2++;
}

static void ev_controlli(PhServizi *s, PhSrvEvento ev, const char *arg, void *u)
{
    (void)s; (void)ev; (void)arg; (void)u;
    n_ev_ctl++;
}

/* Un evento 0x8e dal thread del componente (come nel programma vero). */
static gpointer ev_ctl_corpo(gpointer p)
{
    PhDatiControlli *d = p;
    if (finti.ctl_cb != NULL)
        finti.ctl_cb(d, finti.ctl_utente);
    return NULL;
}
static void evento_controlli(PhDatiControlli d)
{
    GThread *t = g_thread_new("ev-ctl", ev_ctl_corpo, &d);
    g_thread_join(t);
}

#define DA_MS 1700000000000ull

int main(void)
{
    char *tmp = g_dir_make_tmp("yasde-prova-XXXXXX", NULL);
    CONTROLLA(tmp != NULL);
    g_setenv("XDG_CONFIG_HOME", tmp, TRUE);
    g_setenv("XDG_CACHE_HOME", tmp, TRUE);
    ph_lingua_imposta(PH_LINGUA_IT);
    ph_shell_segna_thread_gtk();

    prova_smetti_dopo_libera();
    finti.connesso = true;
    sv_prova_evento_gestito = su_gestito;
    sv_prova_ridim_fatto = su_ridim;
    sv_prova_pagina_fatto = su_pagina;
    sv_prova_input_fatto = su_input;
    PhCfgPreferenze pref;
    ph_cfg_preferenze_predefinite(&pref);
    PhErrore e = {{0}};
    PhServizi *s = ph_ossatura_servizi_veri(&pref, false, &e);
    CONTROLLA(s != NULL);
    if (s == NULL) {
        fprintf(stderr, "%s\n", e.testo);
        return 1;
    }

    /* ---- Collegamento: lo stato arriva sul thread GTK; sessione, app, notifiche. */
    /* ---- Prima connessione, senza alcun telefono abbinato. */
    PhSrvCollegamento cc;
    ph_srv_ascolta(s, PH_SRV_EV_COLLEGAMENTO, ev_collegamento, NULL);
    fuoco_ex(PH_COL_CERCO, "ricerca del telefono in rete", 0, false);
    ASPETTA(stato_srv(s) == PH_SRV_CERCO, 4000);
    ph_srv_collegamento(s, &cc);
    CONTROLLA(cc.stato == PH_SRV_CERCO && cc.telefoni_noti == 0 && cc.telefoni_trovati == 0 && !cc.scelta_arbitraria);
    /* Un telefono in rete che non conosce questo PC: NON_ABBINATO, con quanti ne ha visti. */
    fuoco_ex(PH_COL_NON_ABBINATO, "il telefono non riconosce questo PC", 2, true);
    ASPETTA(stato_srv(s) == PH_SRV_NON_ABBINATO, 4000);
    ph_srv_collegamento(s, &cc);
    CONTROLLA(cc.telefoni_trovati == 2 && cc.scelta_arbitraria && strcmp(cc.motivo, "il telefono non riconosce questo PC") == 0);
    /* (a) abbinamento riuscito: l'esito e' l'avviso CERCO che segue; il collegamento ha avuto il codice giusto. */
    ph_srv_abbina(s, "123456", NULL, 0, NULL, cb_abb, NULL);
    CONTROLLA(ph_srv_abbinamento_in_corso(s));
    CONTROLLA(atomic_load(&finti.abb_n) == 1 && atomic_load(&finti.abb_codice_ok) && n_abb == 0);
    fuoco_esito(PH_COL_CERCO, "abbinamento riuscito: mi collego", PH_COL_ABB_RIUSCITO);
    ASPETTA(n_abb == 1, 4000);
    CONTROLLA(es_abb == PH_SRV_ABB_RIUSCITO && frase_abb[0] == '\0' && !ph_srv_abbinamento_in_corso(s));
    /* Dopo un collegamento riuscito il telefono e' tra i noti (telefoni.toml): lo si vede dal numero. */
    PhCfgTelefoni *noti = calloc(1, sizeof *noti);
    ph_cfg_telefoni_predefiniti(noti);
    snprintf(noti->voci[0].seriale, sizeof noti->voci[0].seriale, "R5CT0000000");
    snprintf(noti->voci[0].nome, sizeof noti->voci[0].nome, "Finto");
    noti->n = 1;
    CONTROLLA(ph_cfg_telefoni_salva(noti, &e) == 0);
    free(noti);
    fuoco_ex(PH_COL_NON_ABBINATO, "abbinamento non riuscito: controlla il codice e riprova", 1, false);
    ASPETTA(stato_srv(s) == PH_SRV_NON_ABBINATO, 4000);
    ASPETTA((ph_srv_collegamento(s, &cc), cc.telefoni_noti == 1 && cc.telefoni_trovati == 1), 4000);
    CONTROLLA(cc.telefoni_noti == 1);
    /* (b) fallito: la frase e' quella del collegamento. */
    ph_srv_abbina(s, "654321", NULL, 0, NULL, cb_abb, NULL);
    fuoco_esito(PH_COL_NON_ABBINATO, "abbinamento non riuscito: controlla il codice e riprova", PH_COL_ABB_FALLITO);
    ASPETTA(n_abb == 2, 4000);
    CONTROLLA(es_abb == PH_SRV_ABB_FALLITO && strcmp(frase_abb, "abbinamento non riuscito: controlla il codice e riprova") == 0);
    CONTROLLA(!atomic_load(&finti.abb_codice_ok)); /* il codice sbagliato e' arrivato com'era */
    /* (c) indirizzo e porta scritti dall'utente: la variante manuale del collegamento. */
    ph_srv_abbina(s, "123456", "192.0.2.7", 37099, NULL, cb_abb, NULL);
    CONTROLLA(atomic_load(&finti.abb_a_n) == 1 && atomic_load(&finti.abb_host_ok) && atomic_load(&finti.abb_porta) == 37099 &&
              atomic_load(&finti.abb_codice_ok));
    fuoco_esito(PH_COL_CERCO, "abbinamento riuscito: mi collego", PH_COL_ABB_RIUSCITO);
    ASPETTA(n_abb == 3, 4000);
    CONTROLLA(es_abb == PH_SRV_ABB_RIUSCITO);
    /* (d) rifiutata dal collegamento (non in NON_ABBINATO, o uno e' gia' in corso): risposta subito. */
    atomic_store(&finti.abb_rifiuta, true);
    ph_srv_abbina(s, "123456", NULL, 0, NULL, cb_abb, NULL);
    ASPETTA(n_abb == 4, 4000);
    CONTROLLA(es_abb == PH_SRV_ABB_RIFIUTATA && !ph_srv_abbinamento_in_corso(s));
    atomic_store(&finti.abb_rifiuta, false);
    /* (e) codice non valido: rifiutata dal pezzo comune, il collegamento non viene toccato. */
    int abb_prima = atomic_load(&finti.abb_n);
    ph_srv_abbina(s, "12345", NULL, 0, NULL, cb_abb, NULL);
    ASPETTA(n_abb == 5, 4000);
    CONTROLLA(es_abb == PH_SRV_ABB_RIFIUTATA && atomic_load(&finti.abb_n) == abb_prima);
    /* Corsa vista in revisione: un "cerca di nuovo" gia' accettato e non ancora pubblicato come CERCO.
     * Il collegamento rifiuta la richiesta (-1); poi, accettata la richiesta successiva, arriva quel CERCO
     * spontaneo: NON e' l'esito (senza il campo esito_abbinamento darebbe "riuscito"). */
    ph_srv_riconnetti_ora(s);
    atomic_store(&finti.abb_rifiuta, true);
    ph_srv_abbina(s, "123456", NULL, 0, NULL, cb_abb, NULL);
    ASPETTA(n_abb == 6, 4000);
    CONTROLLA(es_abb == PH_SRV_ABB_RIFIUTATA && strstr(frase_abb, "riprova") != NULL);
    atomic_store(&finti.abb_rifiuta, false);
    ph_srv_abbina(s, "654321", NULL, 0, NULL, cb_abb, NULL);
    fuoco_ex(PH_COL_CERCO, "riprovo a collegarmi", 0, false); /* il CERCO del "cerca di nuovo" */
    fuoco_ex(PH_COL_CERCO, "ricerca del telefono in rete", 0, false);
    ASPETTA(false, 150);
    CONTROLLA(n_abb == 6 && ph_srv_abbinamento_in_corso(s));
    fuoco_esito(PH_COL_NON_ABBINATO, "abbinamento non riuscito: controlla il codice e riprova", PH_COL_ABB_FALLITO);
    ASPETTA(n_abb == 7, 4000);
    CONTROLLA(es_abb == PH_SRV_ABB_FALLITO && !ph_srv_abbinamento_in_corso(s));
    /* (f) annullata: la risposta non arriva, ma l'abbinamento si conclude comunque con l'avviso. */
    GCancellable *ann = g_cancellable_new();
    ph_srv_abbina(s, "123456", NULL, 0, ann, cb_abb, NULL);
    g_cancellable_cancel(ann);
    g_object_unref(ann);
    in_corso_all_evento = -1;
    fuoco_esito(PH_COL_CERCO, "abbinamento riuscito: mi collego", PH_COL_ABB_RIUSCITO);
    ASPETTA(!ph_srv_abbinamento_in_corso(s) && in_corso_all_evento != -1, 4000);
    CONTROLLA(n_abb == 7 && !ph_srv_abbinamento_in_corso(s));
    /* L'evento di collegamento della conclusione (anche se annullata) arriva a abbinamento gia' finito. */
    CONTROLLA(in_corso_all_evento == 0);
    /* (g) nessun esito entro il limite: SCADUTA (limite ridotto a 1 s per la prova). */
    sv_prova_abb_limite_s = 1;
    ph_srv_abbina(s, "123456", NULL, 0, NULL, cb_abb, NULL);
    ASPETTA(n_abb == 8, 5000);
    CONTROLLA(es_abb == PH_SRV_ABB_SCADUTA && !ph_srv_abbinamento_in_corso(s));
    sv_prova_abb_limite_s = PH_SRV_ABB_LIMITE_S;
    /* Un avviso senza abbinamento in corso non conclude nulla (e un esito arrivato dopo la scadenza e' ignorato). */
    fuoco_ex(PH_COL_NON_ABBINATO, "x", 0, false);
    ASPETTA(false, 100);
    CONTROLLA(n_abb == 8);

    fuoco(0, PH_COL_CERCO);
    fuoco(0, PH_COL_COLLEGATO);
    ASPETTA(numero_app(s) == 2 && numero_notifiche(s) == 1, 4000);
    CONTROLLA(numero_app(s) == 2);
    CONTROLLA(numero_notifiche(s) == 1);
    CONTROLLA(atomic_load(&finti.presti) == 1);
    CONTROLLA(atomic_load(&finti.tr_creati) == 1);
    CONTROLLA(atomic_load(&finti.pulisci) == 1); /* ph_inst_pulisci a ogni nuova connessione */
    CONTROLLA(atomic_load(&finti.appunti_aperti) == 1);
    PhSrvCollegamento c;
    ph_srv_collegamento(s, &c);
    CONTROLLA(c.stato == PH_SRV_COLLEGATO && c.ha_telefono && strcmp(c.nome, "Modello finto") == 0);
    PhSrvTelefono t;
    ph_srv_telefono(s, &t);
    CONTROLLA(t.noto && t.batteria == 70 && t.rete == PH_SRV_RETE_WIFI);
    PhSrvVolume v;
    ph_srv_volume(s, &v);
    CONTROLLA(v.volume == 100); /* il profilo predefinito */

    /* ---- Nessuna chiamata blocca il thread GTK. */
    INIZIO(); ph_srv_tel_elenca(s, "/sdcard", NULL, cb_elenco, NULL); FINE_VELOCE("tel_elenca");
    INIZIO(); ph_srv_tel_crea_cartella(s, "/sdcard/Nuova", NULL, cb_fatto, NULL); FINE_VELOCE("tel_crea_cartella");
    INIZIO(); ph_srv_tel_miniatura(s, "/sdcard/a.jpg", 128, NULL, cb_immagine, NULL); FINE_VELOCE("tel_miniatura");
    INIZIO(); ph_srv_tel_leggi(s, "/sdcard/a.png", 1000, NULL, cb_byte, NULL); FINE_VELOCE("tel_leggi");
    INIZIO(); ph_srv_tel_memoria(s, NULL, cb_memoria, NULL); FINE_VELOCE("tel_memoria");
    INIZIO(); ph_srv_pannello(s, false); FINE_VELOCE("pannello");
    INIZIO(); ph_srv_appunti_imposta(s, "testo finto"); FINE_VELOCE("appunti_imposta");
    INIZIO(); (void)ph_srv_apk_installa(s, "/home/utente/a.apk", NULL, cb_inst, NULL); FINE_VELOCE("apk_installa");
    INIZIO(); (void)ph_srv_app_disinstalla(s, "it.finto.uno", cb_inst, NULL); FINE_VELOCE("app_disinstalla");
    INIZIO(); ph_srv_tel_memoria_dettaglio(s, NULL, cb_dett, NULL); FINE_VELOCE("memoria_dettaglio");
    INIZIO(); ph_srv_tel_calendario(s, 5, NULL, cb_cal, NULL); FINE_VELOCE("calendario");
    INIZIO(); (void)ph_srv_icona(s, "it.finto.uno"); FINE_VELOCE("icona");
    /* La risposta non e' dentro la chiamata e arriva sul thread GTK. */
    CONTROLLA(n_elenco == 0);
    ASPETTA(n_elenco == 1 && n_fatto == 1 && n_immagine == 1 && n_byte == 1 && n_memoria == 1, 6000);
    CONTROLLA(n_elenco == 1 && esito_elenco == PH_SRV_ESITO_OK && voci_elenco == 2);
    CONTROLLA(n_fatto == 1 && n_immagine == 1 && n_byte == 1 && n_memoria == 1);
    ASPETTA(n_inst == 2 && n_dett == 1 && n_cal == 1, 6000);
    CONTROLLA(n_inst == 2 && n_dett == 1 && n_cal == 1);

    /* ---- Widget nuovi: calendario del mese, sveglia, batteria, interruttori, trova. */
    ph_srv_tel_calendario_intervallo(s, DA_MS, DA_MS + 30 * 86400000ull, NULL, cb_calmese, NULL);
    ph_srv_tel_sveglia(s, NULL, cb_sveglia, NULL);
    ph_srv_tel_batteria(s, NULL, cb_batt, NULL);
    ASPETTA(n_calmese == 1 && n_sveglia == 1 && n_batt == 1, 6000);
    CONTROLLA(es_calmese == PH_SRV_ESITO_OK && ev_calmese == 2 && tronc_calmese &&
              strcmp(app_calmese, "it.finto.calendario") == 0);
    CONTROLLA(atomic_load(&finti.cal_giorni_ultimo) == 30);
    CONTROLLA(es_sveglia == PH_SRV_ESITO_OK && presente_sveglia && strcmp(pacc_sveglia, "it.finto.orologio") == 0);
    CONTROLLA(es_batt == PH_SRV_ESITO_OK && livello_batt == 64 && stato_batt == PH_SRV_BAT_IN_CARICA);
    /* Nessuna sveglia: riuscito ma NON presente (mai una sveglia inventata). */
    atomic_store(&finti.sveglia_modo, 1);
    ph_srv_tel_sveglia(s, NULL, cb_sveglia, NULL);
    ASPETTA(n_sveglia == 2, 4000);
    CONTROLLA(es_sveglia == PH_SRV_ESITO_OK && !presente_sveglia && pacc_sveglia[0] == '\0');
    /* Errori del telefono: ERRORE, mai un valore finto. */
    atomic_store(&finti.cal_modo, 1);
    atomic_store(&finti.sveglia_modo, 2);
    atomic_store(&finti.bat_modo, 1);
    ph_srv_tel_calendario_intervallo(s, DA_MS, DA_MS + 30 * 86400000ull, NULL, cb_calmese, NULL);
    ph_srv_tel_sveglia(s, NULL, cb_sveglia, NULL);
    ph_srv_tel_batteria(s, NULL, cb_batt, NULL);
    ASPETTA(n_calmese == 2 && n_sveglia == 3 && n_batt == 2, 6000);
    CONTROLLA(es_calmese == PH_SRV_ESITO_ERRORE && ev_calmese == 0);
    CONTROLLA(es_sveglia == PH_SRV_ESITO_ERRORE && es_batt == PH_SRV_ESITO_ERRORE && livello_batt == -1);
    atomic_store(&finti.cal_modo, 0);
    atomic_store(&finti.sveglia_modo, 0);
    atomic_store(&finti.bat_modo, 0);
    /* Intervallo oltre i 62 giorni: il pezzo comune lo rifiuta senza toccare il telefono. */
    int cal0 = atomic_load(&finti.cal_chiamate);
    ph_srv_tel_calendario_intervallo(s, DA_MS, DA_MS + 90 * 86400000ull, NULL, cb_calmese, NULL);
    ASPETTA(n_calmese == 3, 4000);
    CONTROLLA(es_calmese == PH_SRV_ESITO_ERRORE && atomic_load(&finti.cal_chiamate) == cal0);

    /* Interruttori: si osserva, lo stato arriva (torcia NON disponibile) e poi ogni evento. */
    PhSrvControlli ct;
    ph_srv_ascolta(s, PH_SRV_EV_CONTROLLI, ev_controlli, NULL);
    ph_srv_controlli_osserva(s, true);
    ASPETTA(atomic_load(&finti.ctl_ascolti) == 1 && n_ev_ctl >= 1, 4000);
    ph_srv_controlli(s, &ct);
    CONTROLLA(ct.noto && ct.suoneria == 0 && ct.non_disturbare == 1 && ct.torcia == PH_SRV_NON_DISPONIBILE);
    CONTROLLA(!ct.puo_torcia && ct.puo_suoneria && !ct.trova_in_corso);
    int ev0 = n_ev_ctl;
    evento_controlli((PhDatiControlli){.suoneria = 1, .non_disturbare = 0, .torcia = PH_DATI_NON_DISPONIBILE,
                                       .trova_in_corso = true, .puo_suoneria = true, .puo_non_disturbare = true,
                                       .puo_torcia = false, .puo_trova = true});
    ASPETTA(n_ev_ctl > ev0, 4000);
    ph_srv_controlli(s, &ct);
    CONTROLLA(ct.suoneria == 1 && ct.non_disturbare == 0 && ct.trova_in_corso);
    /* Comandi: riusciti, rifiutati dal telefono (con la sua frase), mai inventati. */
    ph_srv_controllo_imposta(s, PH_SRV_CTL_SUONERIA, PH_SRV_SUONERIA_VIBRA, NULL, cb_f2, NULL);
    ph_srv_controllo_imposta(s, PH_SRV_CTL_NON_DISTURBARE, 1, NULL, cb_f2, NULL);
    ph_srv_controllo_imposta(s, PH_SRV_CTL_TORCIA, 1, NULL, cb_f2, NULL);
    ASPETTA(n_f2 == 3, 6000);
    CONTROLLA(es_f2 == PH_SRV_ESITO_OK && atomic_load(&finti.suoneria_n) == 1 &&
              atomic_load(&finti.suoneria_valore) == 1 && atomic_load(&finti.nd_n) == 1 &&
              atomic_load(&finti.torcia_n) == 1);
    atomic_store(&finti.ctl_rifiuta, true);
    ph_srv_controllo_imposta(s, PH_SRV_CTL_TORCIA, 0, NULL, cb_f2, NULL);
    ASPETTA(n_f2 == 4, 4000);
    CONTROLLA(es_f2 == PH_SRV_ESITO_ERRORE && strcmp(frase_f2, "Il telefono non ha eseguito il comando.") == 0);
    /* il testo del telefono resta nel log: a schermo non arriva */
    CONTROLLA(strstr(frase_f2, "non permesso") == NULL);
    atomic_store(&finti.ctl_rifiuta, false);
    /* Annullato prima della partenza: nulla al telefono. Un lavoro lento davanti
     * tiene in coda il comando, che si annulla subito. */
    int sn0 = atomic_load(&finti.suoneria_n);
    GCancellable *annulla = g_cancellable_new();
    ph_srv_pannello(s, false);
    ph_srv_controllo_imposta(s, PH_SRV_CTL_SUONERIA, PH_SRV_SUONERIA_SILENZIOSO, annulla, cb_f2, NULL);
    g_cancellable_cancel(annulla);
    g_object_unref(annulla);
    ph_srv_controllo_imposta(s, PH_SRV_CTL_SUONERIA, PH_SRV_SUONERIA_SUONA, NULL, cb_f2, NULL);
    ASPETTA(n_f2 == 5, 6000);
    CONTROLLA(atomic_load(&finti.suoneria_n) == sn0 + 1 && atomic_load(&finti.suoneria_valore) == 0);
    CONTROLLA(n_f2 == 5); /* e la richiamata dell'annullato non arriva */
    /* Trova il telefono: avvio con i secondi limitati, poi arresto dell'utente. */
    ph_srv_trova_avvia(s, 30, NULL, cb_f2, NULL);
    ASPETTA(n_f2 == 6, 4000);
    CONTROLLA(es_f2 == PH_SRV_ESITO_OK && atomic_load(&finti.trova_avvia_n) == 1 &&
              atomic_load(&finti.trova_secondi) == 30);
    int tf0 = atomic_load(&finti.trova_ferma_n);
    ph_srv_trova_ferma(s);
    ASPETTA(atomic_load(&finti.trova_ferma_n) == tf0 + 1, 4000);
    CONTROLLA(atomic_load(&finti.trova_ferma_n) == tf0 + 1);

    /* Corsa tra evento e lettura iniziale: la lettura e' ferma a un cancello, un
     * evento piu' nuovo arriva nel frattempo; la risposta vecchia NON deve vincere
     * (il widget mostrerebbe un valore sbagliato fino al prossimo cambio). */
    ph_srv_controlli_osserva(s, false);
    ASPETTA(finti.ctl_cb == NULL, 4000);
    atomic_store(&finti.ctl_gate, true);
    ph_srv_controlli_osserva(s, true);
    ASPETTA(atomic_load(&finti.ctl_in_lettura) && finti.ctl_cb != NULL, 4000);
    CONTROLLA(atomic_load(&finti.ctl_in_lettura));
    int ev1 = n_ev_ctl;
    evento_controlli((PhDatiControlli){.suoneria = 2, .non_disturbare = 1, .torcia = PH_DATI_NON_DISPONIBILE,
                                       .trova_in_corso = false, .puo_suoneria = true, .puo_non_disturbare = true,
                                       .puo_torcia = false, .puo_trova = true});
    ASPETTA(n_ev_ctl > ev1, 4000);
    finti_apri_cancello(); /* ora la lettura (vecchia: suoneria 0) risponde */
    /* La coda dello stato e' in ordine: quando questo comando e' finito la lettura ha gia' risposto. */
    int nf = n_f2;
    ph_srv_controllo_imposta(s, PH_SRV_CTL_NON_DISTURBARE, 1, NULL, cb_f2, NULL);
    ASPETTA(n_f2 == nf + 1, 4000);
    while (g_main_context_iteration(NULL, FALSE)) {}
    ph_srv_controlli(s, &ct);
    CONTROLLA(ct.suoneria == 2); /* resta il valore dell'evento, piu' nuovo */

    /* ---- Ricerca: risultati a gruppi, fine, una sola annulla per ricerca. */
    INIZIO();
    ph_srv_tel_cerca(s, "/sdcard", "nota", false, 0, NULL, cb_trovati, cb_cerca_fine, GINT_TO_POINTER(0));
    FINE_VELOCE("tel_cerca");
    ASPETTA(atomic_load(&finti.ricerche_avviate) == 1, 4000);
    CONTROLLA(atomic_load(&finti.ricerche_avviate) == 1);
    finti.trovato[0]("/sdcard/nota1.txt", false, finti.utente_ricerca[0]);
    finti.trovato[0]("/sdcard/nota2.txt", false, finti.utente_ricerca[0]);
    finti.trovato[0]("/sdcard/Notab", true, finti.utente_ricerca[0]);
    finti.fine[0](PH_DATI_CERCA_FINITA, finti.utente_ricerca[0]);
    ASPETTA(n_cerca_fine == 1, 2000);
    CONTROLLA(n_cerca_fine == 1 && fine_esito[0] == PH_SRV_ESITO_OK && n_trovati_voci == 3);
    CONTROLLA(atomic_load(&finti.ricerche_annullate) == 1);
    /* Una seconda ricerca resta aperta fino alla caduta. */
    ph_srv_tel_cerca(s, "/sdcard", "altro", false, 0, NULL, cb_trovati, cb_cerca_fine, GINT_TO_POINTER(1));
    ASPETTA(atomic_load(&finti.ricerche_avviate) == 2, 4000);
    CONTROLLA(atomic_load(&finti.ricerche_avviate) == 2);

    /* ---- Un flusso: apertura sul telefono, senza bloccare. */
    PhSrvFlussoRichiesta rq = {"it.finto.uno", 640, 400, 160, 0}; /* densita' 160 all'apertura */
    /* Pagina fuori elenco: rifiutata subito, nulla aperto; con una pagina dell'elenco la richiesta
     * arriva al componente (solo alla prima apertura: vedi la riapertura piu' sotto). */
    rq.pagina = 3;
    {
        int va0 = atomic_load(&finti.video_aperti);
        CONTROLLA(ph_srv_flusso_apri(s, &rq, cb_flusso, NULL, &e) == NULL && e.testo[0] != '\0');
        CONTROLLA(atomic_load(&finti.video_aperti) == va0);
    }
    rq.pagina = 2;
    INIZIO();
    PhSrvFlusso *fl = ph_srv_flusso_apri(s, &rq, cb_flusso, NULL, &e);
    FINE_VELOCE("flusso_apri");
    rq.pagina = 0; /* i flussi seguenti si aprono normali */
    CONTROLLA(fl != NULL);
    ASPETTA(ev_aperto == 1, 4000);
    CONTROLLA(ev_aperto == 1 && atomic_load(&finti.video_aperti) == 1);
    CONTROLLA(atomic_load(&finti.apri_dens) == 160);
    CONTROLLA(atomic_load(&finti.apri_pagina) == 2);

    /* ---- Lettore del flusso, screenshot senza immagine (2 s) e registrazione. */
    GThread *lettore = g_thread_new("lettore", lettore_corpo, fl);
    /* Percorso video vero: pacchetti, decodifica finta che riesce, esportazione, consegna. */
    atomic_store(&finti.video_pacchetti, true);
    ASPETTA(atomic_load(&frames) >= 5, 4000);
    CONTROLLA(atomic_load(&frames) >= 5 && atomic_load(&finti.esportati) >= 5);
    CONTROLLA(atomic_load(&finti.dec_creati) == 1);

    /* Un tocco, il suo annullamento e subito la pausa (come fa finestra-app): la
     * pausa non deve arrivare al telefono prima dei tocchi (code in ordine). */
    ph_srv_flusso_tocco(fl, 640, 400, PH_SRV_TOCCO_GIU, 10, 10, 1);
    ph_srv_flusso_tocco(fl, 640, 400, PH_SRV_TOCCO_ANNULLA, 10, 10, 1);
    ph_srv_flusso_pausa(fl);
    ASPETTA(atomic_load(&finti.ordine_n) >= 3, 4000);
    CONTROLLA(atomic_load(&finti.ordine_n) == 3 && strncmp(finti.ordine, "TTP", 3) == 0);
    ph_srv_flusso_riprendi(fl);
    ASPETTA(atomic_load(&finti.ordine_n) >= 4, 4000);
    CONTROLLA(strncmp(finti.ordine, "TTPR", 4) == 0);

    /* ---- Pizzico: passi inoltrati con i valori giusti, in ordine. */
    int pz0 = atomic_load(&finti.pz_n);
    int oz0 = atomic_load(&finti.ordine_n);
    ph_srv_flusso_pizzico(fl, 640, 400, PH_SRV_PIZZICO_INIZIO, 100, 50, 0);
    ph_srv_flusso_pizzico(fl, 640, 400, PH_SRV_PIZZICO_CONTINUA, 101, 51, 120);
    ph_srv_flusso_pizzico(fl, 640, 400, PH_SRV_PIZZICO_CONTINUA, 102, 52, -240);
    ph_srv_flusso_pizzico(fl, 640, 400, PH_SRV_PIZZICO_FINE, 103, 53, 0);
    ASPETTA(atomic_load(&finti.pz_n) == pz0 + 4, 4000);
    CONTROLLA(atomic_load(&finti.pz_n) == pz0 + 4 && strncmp(finti.ordine + oz0, "iccf", 4) == 0);
    CONTROLLA(atomic_load(&finti.pz_w) == 640 && atomic_load(&finti.pz_h) == 400 && atomic_load(&finti.pz_x) == 103 &&
              atomic_load(&finti.pz_y) == 53 && atomic_load(&finti.pz_passo) == 0);
    /* Misura diversa da quella del video: scartato (il telefono lo scarterebbe). */
    ph_srv_flusso_pizzico(fl, 320, 200, PH_SRV_PIZZICO_INIZIO, 10, 10, 0);
    /* Con un pizzico aperto, la pausa lo annulla PRIMA di partire; in pausa i passi si scartano. */
    int oz1 = atomic_load(&finti.ordine_n);
    ph_srv_flusso_pizzico(fl, 640, 400, PH_SRV_PIZZICO_INIZIO, 100, 50, 0);
    ph_srv_flusso_pizzico(fl, 640, 400, PH_SRV_PIZZICO_CONTINUA, 100, 50, 120);
    ph_srv_flusso_pausa(fl);
    ph_srv_flusso_pizzico(fl, 640, 400, PH_SRV_PIZZICO_INIZIO, 100, 50, 0); /* in pausa: scartato */
    ph_srv_flusso_riprendi(fl);
    ASPETTA(atomic_load(&finti.ordine_n) >= oz1 + 5, 4000);
    CONTROLLA(atomic_load(&finti.ordine_n) == oz1 + 5 && strncmp(finti.ordine + oz1, "icaPR", 5) == 0);
    /* Un cambio di misura annulla il pizzico aperto prima di ridimensionare. */
    int oz2 = atomic_load(&finti.ordine_n);
    ph_srv_flusso_pizzico(fl, 640, 400, PH_SRV_PIZZICO_INIZIO, 100, 50, 0);
    ph_srv_flusso_pizzico(fl, 640, 400, PH_SRV_PIZZICO_CONTINUA, 100, 50, 120);
    ph_srv_flusso_ridimensiona_densita(fl, 640, 400, 0);
    ASPETTA(atomic_load(&finti.ordine_n) >= oz2 + 4, 4000);
    CONTROLLA(atomic_load(&finti.ordine_n) == oz2 + 4 && strncmp(finti.ordine + oz2, "icaD", 4) == 0);

    /* Pacchetti rovinati: dopo l'errore si chiede un chiave, al massimo uno ogni 500 ms. */
    int chiavi0 = atomic_load(&finti.chiavi);
    atomic_store(&finti.dai_fallisce, true);
    ASPETTA(false, 900);
    atomic_store(&finti.dai_fallisce, false);
    int chiavi = atomic_load(&finti.chiavi) - chiavi0;
    CONTROLLA(chiavi >= 1 && chiavi <= 2);

    /* ---- Pagina su un flusso aperto (27 nov): richiesta lenta sul thread dei flussi, mai sul GTK.
     * Riuscita; rifiutata dal telefono (nessun effetto sul flusso); pagina 0 e fuori elenco mai
     * mandate; in pausa non si manda. Ogni passo aspetta il segnale del lavoro, mai un sonno. */
    {
        int pn0 = atomic_load(&finti.pag_n);
        int pf0 = atomic_load(&pag_fatte);
        INIZIO();
        ph_srv_flusso_pagina(fl, PH_SRV_PAGINA_BATTERIA);
        FINE_VELOCE("flusso_pagina");
        ASPETTA(atomic_load(&pag_fatte) == pf0 + 1, 4000);
        CONTROLLA(atomic_load(&pag_fatte) == pf0 + 1 && atomic_load(&pag_esito) == 0);
        CONTROLLA(atomic_load(&finti.pag_n) == pn0 + 1 && atomic_load(&finti.pag_ultima) == 1);
        /* Il telefono rifiuta: il lavoro lo dice (-1) e il flusso resta vivo. */
        atomic_store(&finti.pag_rifiuta, true);
        ph_srv_flusso_pagina(fl, PH_SRV_PAGINA_MEMORIA);
        ASPETTA(atomic_load(&pag_fatte) == pf0 + 2, 4000);
        CONTROLLA(atomic_load(&pag_fatte) == pf0 + 2 && atomic_load(&pag_esito) == -1);
        CONTROLLA(atomic_load(&finti.pag_n) == pn0 + 2 && atomic_load(&finti.pag_ultima) == 2);
        atomic_store(&finti.pag_rifiuta, false);
        /* Pagina 0 e fuori elenco: scartate prima di arrivare al telefono; la coda e' in ordine,
         * quindi la pagina valida che segue arriva sola. */
        ph_srv_flusso_pagina(fl, 0);
        ph_srv_flusso_pagina(fl, 9);
        ph_srv_flusso_pagina(fl, PH_SRV_PAGINA_BATTERIA);
        ASPETTA(atomic_load(&pag_fatte) == pf0 + 3, 4000);
        CONTROLLA(atomic_load(&pag_fatte) == pf0 + 3 && atomic_load(&finti.pag_n) == pn0 + 3);
        /* In pausa (il telefono risponderebbe ERRORE) non si manda: esito 1. */
        int o_pr = atomic_load(&finti.ordine_n);
        ph_srv_flusso_pausa(fl);
        ph_srv_flusso_pagina(fl, PH_SRV_PAGINA_MEMORIA);
        ASPETTA(atomic_load(&pag_fatte) == pf0 + 4, 4000);
        CONTROLLA(atomic_load(&pag_fatte) == pf0 + 4 && atomic_load(&pag_esito) == 1 &&
                  atomic_load(&finti.pag_n) == pn0 + 3);
        ph_srv_flusso_riprendi(fl);
        ASPETTA(atomic_load(&finti.ordine_n) >= o_pr + 2, 4000); /* P e R sono partite */
        CONTROLLA(atomic_load(&finti.ordine_n) == o_pr + 2 && strncmp(finti.ordine + o_pr, "PR", 2) == 0);
    }

    /* GTK rifiuta il DMA-BUF: decodificatore software, con l'ultima configurazione ridata. */
    ph_srv_flusso_preferisci_memoria(fl);
    ASPETTA(atomic_load(&finti.dec_creati) >= 2 && atomic_load(&finti.cfg_su_nuovo) >= 1, 4000);
    CONTROLLA(atomic_load(&finti.dec_creati) == 2 && atomic_load(&finti.cfg_su_nuovo) >= 1);

    /* Densita': solo densita' (stessa misura), poi misura e densita' chieste in
     * pausa (ricordate, applicate alla ripresa, mai prima), poi la riapertura
     * dopo la caduta con l'ultima densita'. */
    int rn = atomic_load(&finti.ridim_n);
    ph_srv_flusso_ridimensiona_densita(fl, 640, 400, 200);
    ASPETTA(atomic_load(&finti.ridim_n) == rn + 1, 4000);
    CONTROLLA(atomic_load(&finti.ridim_n) == rn + 1 && atomic_load(&finti.ridim_dens) == 200);
    int o0 = atomic_load(&finti.ordine_n);
    ph_srv_flusso_pausa(fl);
    ph_srv_flusso_ridimensiona_densita(fl, 640, 400, 240);
    ph_srv_flusso_riprendi(fl);
    ASPETTA(atomic_load(&finti.ordine_n) >= o0 + 3, 4000);
    CONTROLLA(atomic_load(&finti.ordine_n) == o0 + 3 && strncmp(finti.ordine + o0, "PRD", 3) == 0);
    CONTROLLA(atomic_load(&finti.ridim_n) == rn + 2 && atomic_load(&finti.ridim_dens) == 240);
    /* Un ridimensionamento che il telefono rifiuta non cambia la densita' ricordata:
     * il successivo con densita' 0 (invariata) usa ancora 240, non 300. */
    atomic_store(&finti.ridim_rifiuta, true);
    ph_srv_flusso_ridimensiona_densita(fl, 640, 400, 300);
    ASPETTA(atomic_load(&finti.ridim_n) == rn + 3, 4000);
    CONTROLLA(atomic_load(&finti.ridim_n) == rn + 3 && atomic_load(&finti.ridim_dens) == 300);
    atomic_store(&finti.ridim_rifiuta, false);
    ph_srv_flusso_ridimensiona_densita(fl, 640, 400, 0);
    ASPETTA(atomic_load(&finti.ridim_n) == rn + 4, 4000);
    CONTROLLA(atomic_load(&finti.ridim_n) == rn + 4 && atomic_load(&finti.ridim_dens) == 240);

    ph_srv_flusso_screenshot(fl, PH_SRV_FOTO_FILE, "/finto", "App", NULL, cb_scatto, NULL);
    ph_srv_flusso_registra(fl, "/finto", "App", false, cb_reg, NULL);
    ASPETTA(n_scatto == 1 && ev_reg_inizio == 1, 6000);
    CONTROLLA(n_scatto == 1 && n_scatto_ok == 0); /* nessun fotogramma: errore, mai silenzio */
    CONTROLLA(ev_reg_inizio == 1 && atomic_load(&finti.reg_inizia) == 1);

    /* ---- Caduta: i prestiti tornano, ricerca e appunti e coda si chiudono, il flusso e' PERSO. */
    finti.connesso = false;
    fuoco(1, PH_COL_PERSO);
    fuoco(0, PH_COL_PERSO);
    ASPETTA(atomic_load(&finti.presti) == 0 && ev_perso == 1 && n_cerca_fine == 2 && ev_reg_fine == 1, 6000);
    CONTROLLA(atomic_load(&finti.presti) == 0);
    CONTROLLA(atomic_load(&finti.ricerche_annullate) == 2);
    CONTROLLA(atomic_load(&finti.appunti_chiusi) == 1);
    CONTROLLA(atomic_load(&finti.ascolti_tolti) == 1);
    CONTROLLA(atomic_load(&finti.tr_staccata) == 1);
    CONTROLLA(ev_perso == 1);
    /* La registrazione si ferma con la connessione, con la sua frase, e il file si chiude e si libera. */
    CONTROLLA(ev_reg_fine == 1 && reg_finita_con_frase && frase_reg_fine != NULL);
    CONTROLLA(atomic_load(&finti.reg_finisci) == 1 && atomic_load(&finti.reg_libera) == 1);
    CONTROLLA(n_cerca_fine == 2 && fine_esito[1] == PH_SRV_ESITO_NON_COLLEGATO);
    ph_srv_collegamento(s, &c);
    CONTROLLA(c.stato == PH_SRV_PERSO);
    /* Alla perdita: ascolto dei controlli tolto, tutto "non disponibile" (mai l'ultimo valore). */
    CONTROLLA(finti.ctl_cb == NULL && atomic_load(&finti.ctl_ascolti_tolti) >= 1);
    ASPETTA((ph_srv_controlli(s, &ct), ct.suoneria == PH_SRV_NON_DISPONIBILE), 4000);
    CONTROLLA(ct.suoneria == PH_SRV_NON_DISPONIBILE && ct.torcia == PH_SRV_NON_DISPONIBILE &&
              !ct.puo_suoneria && !ct.puo_non_disturbare && !ct.puo_torcia && !ct.puo_trova);
    /* Un ridimensionamento chiesto a flusso PERSO non si perde: la riapertura lo usa. */
    int rf = atomic_load(&ridim_fatti);
    ph_srv_flusso_ridimensiona_densita(fl, 640, 400, 280);
    ASPETTA(atomic_load(&ridim_fatti) == rf + 1, 4000);
    CONTROLLA(atomic_load(&ridim_fatti) == rf + 1);
    /* A flusso perso il pizzico si scarta (e un gesto aperto e' morto con la sessione). */
    int pz_p = atomic_load(&finti.pz_n);
    ph_srv_flusso_pizzico(fl, 640, 400, PH_SRV_PIZZICO_INIZIO, 100, 50, 0);
    ph_srv_flusso_pizzico(fl, 640, 400, PH_SRV_PIZZICO_CONTINUA, 100, 50, 120);
    CONTROLLA(atomic_load(&finti.pz_n) == pz_p);
    /* Senza sessione le richieste si chiudono subito con NON_COLLEGATO. */
    int prima = n_elenco;
    ph_srv_tel_elenca(s, "/sdcard", NULL, cb_elenco, NULL);
    ASPETTA(n_elenco == prima + 1, 2000);
    CONTROLLA(n_elenco == prima + 1 && esito_elenco == PH_SRV_ESITO_NON_COLLEGATO);

    /* Senza sessione anche i widget nuovi rispondono NON_COLLEGATO. */
    int n_cm = n_calmese, n_sv = n_sveglia, n_bt = n_batt, n_ff = n_f2;
    ph_srv_tel_calendario_intervallo(s, DA_MS, DA_MS + 30 * 86400000ull, NULL, cb_calmese, NULL);
    ph_srv_tel_sveglia(s, NULL, cb_sveglia, NULL);
    ph_srv_tel_batteria(s, NULL, cb_batt, NULL);
    ph_srv_controllo_imposta(s, PH_SRV_CTL_TORCIA, 1, NULL, cb_f2, NULL);
    ph_srv_trova_avvia(s, 30, NULL, cb_f2, NULL);
    ASPETTA(n_calmese == n_cm + 1 && n_sveglia == n_sv + 1 && n_batt == n_bt + 1 && n_f2 == n_ff + 2, 4000);
    CONTROLLA(es_calmese == PH_SRV_ESITO_NON_COLLEGATO && es_sveglia == PH_SRV_ESITO_NON_COLLEGATO &&
              es_batt == PH_SRV_ESITO_NON_COLLEGATO && es_f2 == PH_SRV_ESITO_NON_COLLEGATO);

    /* ---- Ritorno: nuova sessione, coda ripresa, flusso riaperto da solo. */
    finti.connesso = true;
    fuoco(0, PH_COL_COLLEGATO);
    ASPETTA(atomic_load(&finti.presti) == 1 && ev_ripreso == 1, 6000);
    CONTROLLA(atomic_load(&finti.presti) == 1);
    CONTROLLA(atomic_load(&finti.pulisci) == 2);
    CONTROLLA(atomic_load(&finti.tr_ripresa) == 1 && atomic_load(&finti.tr_creati) == 1);
    CONTROLLA(ev_ripreso == 1 && ev_aperto == 1);
    CONTROLLA(atomic_load(&finti.video_aperti) == 2 && atomic_load(&finti.video_chiusi) == 1);
    CONTROLLA(atomic_load(&finti.apri_pagina) == 0); /* la riapertura dopo la caduta e' normale */
    CONTROLLA(atomic_load(&finti.apri_dens) == 280); /* riaperto con l'ultima densita' chiesta, anche a flusso PERSO */
    CONTROLLA(numero_app(s) == 2);
    /* Riconnessione: l'osservazione dei controlli riparte da sola e lo stato si ripubblica. */
    ASPETTA(atomic_load(&finti.ctl_ascolti) == 3 && finti.ctl_cb != NULL, 4000);
    CONTROLLA(atomic_load(&finti.ctl_ascolti) == 3 && finti.ctl_cb != NULL);
    ASPETTA((ph_srv_controlli(s, &ct), ct.suoneria == 0), 4000);
    CONTROLLA(ct.noto && ct.suoneria == 0);
    /* BLOCCATO <-> COLLEGATO e' la stessa sessione: nessun prestito nuovo. */
    int totali = atomic_load(&finti.presti_totali);
    int gestiti0 = atomic_load(&gestiti);
    fuoco(0, PH_COL_BLOCCATO);
    fuoco(0, PH_COL_COLLEGATO);
    ASPETTA(atomic_load(&gestiti) >= gestiti0 + 2, 4000); /* il gestore ha finito i due eventi */
    while (g_main_context_iteration(NULL, FALSE)) {}
    CONTROLLA(atomic_load(&finti.presti_totali) == totali && atomic_load(&finti.presti) == 1);

    /* ---- Pannello al collegamento (24 nov 2026): un solo tentativo di spegnimento, in coda.
     * Il segnale di fine e' l'elenco delle app: sta sulla stessa coda ordinata, dopo lo spegnimento. */
    {
        PhSrvTelefono tp;
        /* 1. pannello acceso, non in mano: parte un PANNELLO_IMPOSTA 0 e lo stato diventa spento. */
        atomic_store(&finti.pan_stato_acceso, true);
        int off1 = atomic_load(&finti.pan_off), el1 = atomic_load(&finti.app_elenchi);
        fuoco(1, PH_COL_CERCO);
        fuoco(0, PH_COL_COLLEGATO);
        ASPETTA(atomic_load(&finti.app_elenchi) > el1 && atomic_load(&finti.pan_off) == off1 + 1, 6000);
        CONTROLLA(atomic_load(&finti.pan_off) == off1 + 1);
        ASPETTA((ph_srv_telefono(s, &tp), tp.pannello_noto && !tp.pannello_acceso), 4000);
        CONTROLLA(tp.pannello_noto && !tp.pannello_acceso);
        /* 2. «in mano»: nessuno spegnimento finche' non c'e' input (ci pensa la regola esistente). */
        atomic_store(&finti.pan_stato_in_mano, true);
        off1 = atomic_load(&finti.pan_off);
        el1 = atomic_load(&finti.app_elenchi);
        fuoco(1, PH_COL_CERCO);
        fuoco(0, PH_COL_COLLEGATO);
        ASPETTA(atomic_load(&finti.app_elenchi) > el1, 6000);
        ASPETTA((ph_srv_telefono(s, &tp), tp.in_mano && tp.pannello_acceso), 4000);
        CONTROLLA(tp.in_mano && tp.pannello_acceso && atomic_load(&finti.pan_off) == off1);
        /* 3. il telefono rifiuta: un solo tentativo, poi ci si riallinea (pannello acceso). */
        atomic_store(&finti.pan_stato_in_mano, false);
        atomic_store(&finti.pan_rifiuta, true);
        off1 = atomic_load(&finti.pan_off);
        el1 = atomic_load(&finti.app_elenchi);
        fuoco(1, PH_COL_CERCO);
        fuoco(0, PH_COL_COLLEGATO);
        ASPETTA(atomic_load(&finti.app_elenchi) > el1 && atomic_load(&finti.pan_off) == off1 + 1, 6000);
        ASPETTA((ph_srv_telefono(s, &tp), tp.pannello_noto && tp.pannello_acceso && !tp.in_mano), 4000);
        CONTROLLA(atomic_load(&finti.pan_off) == off1 + 1 && tp.pannello_acceso && !tp.in_mano);
        atomic_store(&finti.pan_rifiuta, false);
        /* Dopo il rifiuto del collegamento non c'e' blocco di 2 s: allo sblocco (evento, motivo 2) il
         * primo input dal PC spegne subito. Col vecchio ramo (pannello_non_prima_us a +2 s) questo tocco
         * cadrebbe nel blocco e pan_off resterebbe off1 + 1. */
        CONTROLLA(finti_pannello_evento(true, 2));
        ASPETTA((ph_srv_telefono(s, &tp), tp.in_mano && tp.pannello_acceso), 4000);
        CONTROLLA(tp.in_mano && tp.pannello_acceso);
        int ea3 = ev_aperto;
        PhSrvFlusso *f3 = ph_srv_flusso_apri(s, &rq, cb_flusso, NULL, &e);
        CONTROLLA(f3 != NULL);
        ASPETTA(ev_aperto == ea3 + 1, 4000);
        ph_srv_flusso_tocco(f3, 640, 400, PH_SRV_TOCCO_GIU, 10, 10, 1);
        ph_srv_flusso_tocco(f3, 640, 400, PH_SRV_TOCCO_SU, 10, 10, 0);
        ASPETTA(atomic_load(&finti.pan_off) == off1 + 2, 1500);
        CONTROLLA(atomic_load(&finti.pan_off) == off1 + 2);
        int vc3 = atomic_load(&finti.video_chiusi);
        ph_srv_flusso_chiudi(f3, false);
        ASPETTA(atomic_load(&finti.video_chiusi) == vc3 + 1, 4000);
        atomic_store(&finti.pan_stato_acceso, false);
    }

    /* ---- Chiusura con richieste ancora in corso: il flusso della shell si
     * chiude per primo, poi i servizi; nulla resta appeso, tutto e' restituito. */
    ph_srv_tel_elenca(s, "/sdcard", NULL, cb_elenco, NULL);
    ph_srv_tel_memoria(s, NULL, cb_memoria, NULL);
    /* C1: barriera scaduta con un SU in coda e perso. Il tocco GIU e' partito (il telefono
     * lo ha), il canale dell'input poi si ferma: un MUOVI resta fermo dentro il
     * componente e il SU resta in coda. Alla chiusura la barriera scade (2 s) e
     * l'ANNULLA diretto copre il tocco ancora giu' (sullo specchio nessun dito resta). */
    int ea_fx = ev_aperto; /* non un numero fisso: la prova del pannello sopra apre e chiude un flusso suo */
    PhSrvFlusso *fx = ph_srv_flusso_apri(s, &rq, cb_flusso, NULL, &e);
    CONTROLLA(fx != NULL);
    ASPETTA(ev_aperto == ea_fx + 1, 4000);
    CONTROLLA(ev_aperto == ea_fx + 1);
    int inp0 = atomic_load(&input_fatti);
    ph_srv_flusso_tocco(fx, 640, 400, PH_SRV_TOCCO_GIU, 10, 10, 1);
    ASPETTA(atomic_load(&input_fatti) == inp0 + 1, 4000); /* partito e registrato */
    atomic_store(&finti.input_gate, true);
    int ta1 = atomic_load(&finti.tocchi_annulla);
    int te1 = atomic_load(&finti.tocco_tentati);
    ph_srv_flusso_tocco(fx, 640, 400, PH_SRV_TOCCO_MUOVI, 12, 12, 1); /* resta fermo al cancello */
    ASPETTA(atomic_load(&finti.tocco_tentati) == te1 + 1, 4000);
    ph_srv_flusso_tocco(fx, 640, 400, PH_SRV_TOCCO_SU, 12, 12, 0);    /* accodato e perso */
    ph_srv_flusso_chiudi(fx, false);
    ASPETTA(atomic_load(&finti.tocchi_annulla) == ta1 + 1, 8000); /* l'ANNULLA diretto arriva dopo la scadenza */
    CONTROLLA(atomic_load(&finti.tocchi_annulla) == ta1 + 1 && atomic_load(&finti.tocco_az) == PH_TOCCO_ANNULLA);
    finti_apri_cancello_input();
    /* Si aspetta che tutto cio' che era fermo finisca (GIU, MUOVI, SU) e il flusso si chiuda. */
    ASPETTA(atomic_load(&input_fatti) == inp0 + 3 && atomic_load(&finti.video_chiusi) >= atomic_load(&finti.video_aperti) - 1, 6000);
    CONTROLLA(atomic_load(&input_fatti) == inp0 + 3);

    /* Una chiusura regolare (la barriera aspetta un input che parte) rimette a posto la
     * regola "dopo una barriera scaduta le successive non aspettano". */
    int ea_fy = ev_aperto;
    PhSrvFlusso *fy = ph_srv_flusso_apri(s, &rq, cb_flusso, NULL, &e);
    CONTROLLA(fy != NULL);
    ASPETTA(ev_aperto == ea_fy + 1, 4000);
    CONTROLLA(ev_aperto == ea_fy + 1);
    int vcy = atomic_load(&finti.video_chiusi);
    ph_srv_flusso_tocco(fy, 640, 400, PH_SRV_TOCCO_GIU, 10, 10, 1);
    ph_srv_flusso_chiudi(fy, false);
    ASPETTA(atomic_load(&finti.video_chiusi) == vcy + 1, 4000);

    /* C2: N flussi con il canale dell'input fermo e un GIU mai partito ciascuno: le
     * barriere di chiusura non si sommano (la prima scade, le altre non aspettano) e
     * non parte nessun ANNULLA per un GIU mai ricevuto dal telefono. */
    PhSrvFlusso *fn[3];
    int ea_fn = ev_aperto;
    for (int i = 0; i < 3; i++) {
        fn[i] = ph_srv_flusso_apri(s, &rq, cb_flusso, NULL, &e);
        CONTROLLA(fn[i] != NULL);
        ASPETTA(ev_aperto == ea_fn + i + 1, 4000);
        CONTROLLA(ev_aperto == ea_fn + i + 1);
    }
    atomic_store(&finti.input_gate, true);
    int inp2 = atomic_load(&input_fatti);
    int ta2 = atomic_load(&finti.tocchi_annulla);
    int vc0 = atomic_load(&finti.video_chiusi);
    for (int i = 0; i < 3; i++)
        ph_srv_flusso_tocco(fn[i], 640, 400, PH_SRV_TOCCO_GIU, 10, 10, 1);
    gint64 t_inizio = g_get_monotonic_time();
    for (int i = 0; i < 3; i++)
        ph_srv_flusso_chiudi(fn[i], false);
    ASPETTA(atomic_load(&finti.video_chiusi) >= vc0 + 3, 9000);
    gint64 durata_ms = (g_get_monotonic_time() - t_inizio) / 1000;
    CONTROLLA(atomic_load(&finti.video_chiusi) >= vc0 + 3);
    CONTROLLA(durata_ms < 4000); /* una sola scadenza (2 s), non 3 x 2 s */
    CONTROLLA(atomic_load(&finti.tocchi_annulla) == ta2);
    finti_apri_cancello_input();
    ASPETTA(atomic_load(&input_fatti) == inp2 + 3, 6000); /* i tre GIU fermi finiscono (o si scartano) */
    CONTROLLA(atomic_load(&input_fatti) == inp2 + 3);
    /* Ancora una chiusura regolare: rimette a posto la regola delle barriere scadute. */
    int ea_fz = ev_aperto;
    PhSrvFlusso *fz = ph_srv_flusso_apri(s, &rq, cb_flusso, NULL, &e);
    CONTROLLA(fz != NULL);
    ASPETTA(ev_aperto == ea_fz + 1, 4000);
    CONTROLLA(ev_aperto == ea_fz + 1);
    int vcz = atomic_load(&finti.video_chiusi);
    ph_srv_flusso_tocco(fz, 640, 400, PH_SRV_TOCCO_GIU, 10, 10, 1);
    ph_srv_flusso_chiudi(fz, false);
    ASPETTA(atomic_load(&finti.video_chiusi) == vcz + 1, 4000);

    /* B: un altro flusso con un tocco e un pizzico aperti alla chiusura: ANNULLA di
     * entrambi PRIMA di VIDEO_CHIUDI (sullo specchio un dito resterebbe giu'). */
    int ea_fl3 = ev_aperto;
    PhSrvFlusso *fl3 = ph_srv_flusso_apri(s, &rq, cb_flusso, NULL, &e);
    CONTROLLA(fl3 != NULL);
    ASPETTA(ev_aperto == ea_fl3 + 1, 4000);
    CONTROLLA(ev_aperto == ea_fl3 + 1);
    int ob = atomic_load(&finti.ordine_n);
    int ta0 = atomic_load(&finti.tocchi_annulla), pz_a0 = atomic_load(&finti.pz_annulla_n);
    int tc0 = atomic_load(&finti.tocchi), pzn0 = atomic_load(&finti.pz_n);
    ph_srv_flusso_tocco(fl3, 640, 400, PH_SRV_TOCCO_GIU, 10, 10, 1);
    ASPETTA(atomic_load(&finti.tocchi) >= tc0 + 1, 4000);
    ph_srv_flusso_pizzico(fl3, 640, 400, PH_SRV_PIZZICO_INIZIO, 100, 50, 0);
    ASPETTA(atomic_load(&finti.pz_n) >= pzn0 + 1, 4000);
    ph_srv_flusso_interrompi(fl3);
    ph_srv_flusso_chiudi(fl3, false);
    ASPETTA(atomic_load(&finti.ordine_n) >= ob + 5, 4000);
    CONTROLLA(atomic_load(&finti.ordine_n) == ob + 5 && strncmp(finti.ordine + ob, "TiaTV", 5) == 0);
    CONTROLLA(atomic_load(&finti.tocchi_annulla) == ta0 + 1 && atomic_load(&finti.pz_annulla_n) == pz_a0 + 1);

    /* A: con SU e FINE ancora in coda alla chiusura: partono (i gesti si chiudono) PRIMA di
     * VIDEO_CHIUDI; un INIZIO in coda a flusso chiuso si scarta. */
    int oa = atomic_load(&finti.ordine_n);
    int pan_on0 = atomic_load(&finti.pan_on);
    int t0a = atomic_load(&finti.tocchi);
    ph_srv_flusso_tocco(fl, 640, 400, PH_SRV_TOCCO_GIU, 10, 10, 1);
    ASPETTA(atomic_load(&finti.tocchi) >= t0a + 1, 4000);
    ph_srv_flusso_interrompi(fl);
    g_thread_join(lettore);
    ph_srv_flusso_tocco(fl, 640, 400, PH_SRV_TOCCO_SU, 10, 10, 0);
    ph_srv_flusso_pizzico(fl, 640, 400, PH_SRV_PIZZICO_INIZIO, 100, 50, 0);
    ph_srv_flusso_pizzico(fl, 640, 400, PH_SRV_PIZZICO_FINE, 100, 50, 0);
    ph_srv_flusso_chiudi(fl, false);
    ASPETTA(atomic_load(&finti.ordine_n) >= oa + 4, 4000);
    CONTROLLA(atomic_load(&finti.ordine_n) == oa + 4 && strncmp(finti.ordine + oa, "TTfV", 4) == 0);
    /* Era l'ultima finestra e il pannello era spento dal PC: NON si riaccende (regola del
     * committente, PIANO.md; la chiusura del flusso, sopra, e' il segnale che il lavoro e' girato). */
    CONTROLLA(atomic_load(&finti.pan_off) >= 1);
    /* Il segnale e' l'interruttore del popup, che gira sulla STESSA coda ordinata (p_stato) da cui
     * partiva la vecchia riaccensione: quando risponde, un IMPOSTA 1 accodato prima dalla chiusura
     * sarebbe gia' passato e il conto darebbe +2. L'interruttore lo riaccende, anche senza finestre. */
    int np0 = n_pan_fatto;
    ph_srv_pannello_chiedi(s, true, NULL, cb_pan_fatto, NULL);
    ASPETTA(n_pan_fatto == np0 + 1, 4000);
    CONTROLLA(n_pan_fatto == np0 + 1 && es_pan_fatto == PH_SRV_PAN_FATTO);
    CONTROLLA(atomic_load(&finti.pan_on) == pan_on0 + 1);
    /* L'utente sblocca il telefono (evento del pannello, motivo 2: «in mano»): aprendo una finestra
     * nuova torna la regola di prima, il pannello si spegne dal PC. */
    CONTROLLA(finti_pannello_evento(true, 2));
    {
        PhSrvTelefono tel;
        ASPETTA((ph_srv_telefono(s, &tel), tel.in_mano && tel.pannello_acceso), 4000);
        CONTROLLA(tel.in_mano && tel.pannello_acceso);
    }
    int off_prima = atomic_load(&finti.pan_off);
    int ea_prima = ev_aperto;
    PhSrvFlusso *fnuovo = ph_srv_flusso_apri(s, &rq, cb_flusso, NULL, &e);
    CONTROLLA(fnuovo != NULL);
    ASPETTA(ev_aperto == ea_prima + 1, 4000);
    ph_srv_flusso_tocco(fnuovo, 640, 400, PH_SRV_TOCCO_GIU, 10, 10, 1); /* il primo input dal PC */
    ph_srv_flusso_tocco(fnuovo, 640, 400, PH_SRV_TOCCO_SU, 10, 10, 0);
    ASPETTA(atomic_load(&finti.pan_off) == off_prima + 1, 6000);
    CONTROLLA(atomic_load(&finti.pan_off) == off_prima + 1);
    /* Chiusa anche questa (e' l'ultima): ancora nessuna riaccensione. */
    int vc_prima = atomic_load(&finti.video_chiusi);
    ph_srv_flusso_chiudi(fnuovo, false);
    ASPETTA(atomic_load(&finti.video_chiusi) == vc_prima + 1, 4000);
    /* PC assente (motivo 6, protocollo 2.9): il telefono ha riacceso il pannello da solo; al primo input
     * dal PC il pannello si rispegne (come dopo uno sblocco). */
    CONTROLLA(finti_pannello_evento(true, 6));
    {
        PhSrvTelefono tel6;
        ASPETTA((ph_srv_telefono(s, &tel6), tel6.in_mano && tel6.pannello_acceso), 4000);
        CONTROLLA(tel6.in_mano && tel6.pannello_acceso);
    }
    int off6 = atomic_load(&finti.pan_off);
    int ea6 = ev_aperto;
    PhSrvFlusso *f6 = ph_srv_flusso_apri(s, &rq, cb_flusso, NULL, &e);
    CONTROLLA(f6 != NULL);
    ASPETTA(ev_aperto == ea6 + 1, 4000);
    ph_srv_flusso_tocco(f6, 640, 400, PH_SRV_TOCCO_GIU, 10, 10, 1);
    ph_srv_flusso_tocco(f6, 640, 400, PH_SRV_TOCCO_SU, 10, 10, 0);
    ASPETTA(atomic_load(&finti.pan_off) == off6 + 1, 6000);
    CONTROLLA(atomic_load(&finti.pan_off) == off6 + 1);
    int vc6 = atomic_load(&finti.video_chiusi);
    ph_srv_flusso_chiudi(f6, false);
    ASPETTA(atomic_load(&finti.video_chiusi) == vc6 + 1, 4000);
    /* Stesso segnale: una riaccensione accodata dalla chiusura sarebbe gia' passata (+2 prima, +3 qui). */
    np0 = n_pan_fatto;
    ph_srv_pannello_chiedi(s, true, NULL, cb_pan_fatto, NULL);
    ASPETTA(n_pan_fatto == np0 + 1, 4000);
    CONTROLLA(n_pan_fatto == np0 + 1 && atomic_load(&finti.pan_on) == pan_on0 + 2);
    /* ---- Pagina e chiusura (27 nov) */
    {
        /* Flusso chiuso con la pagina in coda (la prima ferma dentro il telefono, la seconda dietro
         * di lei, poi la chiusura): la seconda NON parte e la chiusura viene dopo la prima. */
        PhSrvFlusso *fp = ph_srv_flusso_apri(s, &rq, cb_flusso, NULL, &e);
        CONTROLLA(fp != NULL);
        int ea_fp = ev_aperto;
        ASPETTA(ev_aperto == ea_fp + 1, 4000);
        CONTROLLA(ev_aperto == ea_fp + 1);
        int pn1 = atomic_load(&finti.pag_n), pf1 = atomic_load(&pag_fatte);
        int vc_fp = atomic_load(&finti.video_chiusi);
        int o_fp = atomic_load(&finti.ordine_n);
        atomic_store(&finti.pag_gate, true);
        ph_srv_flusso_pagina(fp, PH_SRV_PAGINA_BATTERIA);
        ASPETTA(atomic_load(&finti.pag_in_corso), 4000);
        CONTROLLA(atomic_load(&finti.pag_in_corso));
        ph_srv_flusso_pagina(fp, PH_SRV_PAGINA_MEMORIA); /* in coda dietro la prima */
        ph_srv_flusso_chiudi(fp, false);
        finti_apri_cancello_pagina();
        ASPETTA(atomic_load(&finti.video_chiusi) == vc_fp + 1 && atomic_load(&pag_fatte) == pf1 + 2, 4000);
        CONTROLLA(atomic_load(&finti.video_chiusi) == vc_fp + 1 && atomic_load(&pag_fatte) == pf1 + 2);
        CONTROLLA(atomic_load(&finti.pag_n) == pn1 + 1 && atomic_load(&pag_esito) == 1);
        CONTROLLA(atomic_load(&finti.ordine_n) >= o_fp + 2 && strncmp(finti.ordine + o_fp, "GV", 2) == 0);
    }

    /* Un secondo flusso, che poi sopravvivra' ai servizi (vedi sotto). */
    PhSrvFlusso *fl2 = ph_srv_flusso_apri(s, &rq, cb_flusso, NULL, &e);
    CONTROLLA(fl2 != NULL);
    ASPETTA(atomic_load(&finti.video_aperti) >= 3, 4000);
    /* "Trova il telefono" in corso alla chiusura dell'app: lo si ferma. */
    ph_srv_trova_avvia(s, 60, NULL, cb_f2, NULL);
    ASPETTA(atomic_load(&finti.trova_avvia_n) == 2, 4000);
    int tf1 = atomic_load(&finti.trova_ferma_n);
    int ascolti_tolti1 = atomic_load(&finti.ctl_ascolti_tolti);
    /* Chiusura dell'app durante un abbinamento: si chiude senza risposta e senza lasciare nulla (ASan). */
    int pan_on_chiusura = atomic_load(&finti.pan_on);
    int n_abb_prima = n_abb;
    fuoco_ex(PH_COL_NON_ABBINATO, "x", 0, false);
    ph_srv_abbina(s, "123456", NULL, 0, NULL, cb_abb, NULL);
    INIZIO();
    ph_servizi_libera(s);
    /* La chiusura non manda PANNELLO_IMPOSTA: il telefono torna com'era col ripristino lato telefono
     * (guardiano, alla chiusura del componente), che non e' di questo pezzo. */
    CONTROLLA(atomic_load(&finti.pan_on) == pan_on_chiusura);
    CONTROLLA(n_abb == n_abb_prima);
    (void)t0;
    CONTROLLA(atomic_load(&finti.trova_ferma_n) == tf1 + 1); /* fermato alla chiusura */
    CONTROLLA(finti.ctl_cb == NULL && atomic_load(&finti.ctl_ascolti_tolti) > ascolti_tolti1); /* nessun evento dopo */
    /* Un flusso che sopravvive ai servizi (un widget tenuto vivo da un
     * riferimento di troppo): letture e chiusura tardive non devono toccare il
     * backend liberato (il pezzo comune passa b = NULL). */
    ph_srv_flusso_interrompi(fl2);
    CONTROLLA(ph_srv_flusso_leggi(fl2, &(PhFotogramma){0}, 10) == -1);
    ph_srv_flusso_pausa(fl2);
    {
        int pzf = atomic_load(&finti.pz_n);
        ph_srv_flusso_pizzico(fl2, 640, 400, PH_SRV_PIZZICO_INIZIO, 1, 1, 0);
        CONTROLLA(atomic_load(&finti.pz_n) == pzf);
    }
    ph_srv_flusso_chiudi(fl2, false);
    CONTROLLA(atomic_load(&finti.presti) == 0);
    CONTROLLA(atomic_load(&finti.tr_chiusi) == 1);
    CONTROLLA(atomic_load(&finti.video_aperti) == atomic_load(&finti.video_chiusi));
    while (g_main_context_iteration(NULL, FALSE)) {}
    CONTROLLA(nel_gtk_sempre);

    /* Pulizia della cartella temporanea della prova. */
    char *cmd = g_strdup_printf("rm -rf '%s'", tmp);
    CONTROLLA(system(cmd) == 0);
    g_free(cmd);
    g_free(tmp);
    if (errori == 0)
        printf("prova_servizi_veri: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
