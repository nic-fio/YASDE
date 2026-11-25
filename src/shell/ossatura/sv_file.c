/* Servizi veri: cartelle del telefono, ricerca, nuova cartella, miniature,
 * lettura di un file, memoria (5.9.2, 5.9.4). Ogni richiesta e' un lavoro su
 * p_lavori e si chiude con ESATTAMENTE una chiamata finale ph_servizi_lavoro_*.
 * Percorsi e nomi non finiscono mai in un log. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sv_interno.h"

#if PH_OSSATURA_SERVIZI_VERI

/* ---- Cartella ------------------------------------------------------------------------ */
typedef struct {
    PhSrvLavoro *l;
    char *testo;     /* cartella, percorso */
    char *cerca;     /* testo cercato */
    bool solo_cartelle;
    unsigned massimo;
    int lato;
    size_t massimo_byte;
} Job;

static Job *job_nuovo(PhSrvLavoro *l, const char *testo)
{
    Job *j = calloc(1, sizeof *j);
    if (j == NULL)
        return NULL;
    j->l = l;
    j->testo = strdup(testo != NULL ? testo : "");
    if (j->testo == NULL) {
        free(j);
        return NULL;
    }
    return j;
}

static void job_libera(Job *j)
{
    free(j->testo);
    free(j->cerca);
    free(j);
}

/* Se non si puo' nemmeno mettere in coda il lavoro, si chiude subito. */
typedef enum { F_ELENCO, F_CERCA, F_FATTO, F_IMMAGINE, F_BYTE, F_MEMORIA } Fine;

static void chiudi_errore(PhSrvLavoro *l, Fine f, PhSrvEsito esito, const char *frase)
{
    switch (f) {
    case F_ELENCO: ph_servizi_lavoro_elenco(l, esito, frase, NULL, 0); break;
    case F_CERCA: ph_servizi_lavoro_cerca_fine(l, esito, frase, false); break;
    case F_FATTO: ph_servizi_lavoro_fatto(l, esito, frase); break;
    case F_IMMAGINE: ph_servizi_lavoro_immagine(l, esito, frase, NULL, 0); break;
    case F_BYTE: ph_servizi_lavoro_byte(l, esito, frase, NULL); break;
    case F_MEMORIA: ph_servizi_lavoro_memoria(l, esito, 0, 0); break;
    }
}

static void elenca_lavoro(Sv *sv, void *dati)
{
    Job *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        chiudi_errore(j->l, F_ELENCO, PH_SRV_ESITO_NON_COLLEGATO, NULL);
        job_libera(j);
        return;
    }
    PhFileElenco el = {NULL, 0};
    PhErrore e = {{0}};
    PhFileEsito r = ph_file_elenca(s->a, j->testo, &el, &e);
    sv_sessione_lascia(s);
    if (r != PH_FILE_RIUSCITO) {
        PhSrvEsito es = ph_ossatura_esito_file(r);
        chiudi_errore(j->l, F_ELENCO, es,
                      es == PH_SRV_ESITO_ERRORE ? ph_t("Non riesco a leggere questa cartella del telefono") : NULL);
        job_libera(j);
        return;
    }
    PhSrvVoce *v = el.numero > 0 ? calloc(el.numero, sizeof *v) : NULL;
    if (el.numero > 0 && v == NULL) {
        ph_file_elenco_libera(&el);
        chiudi_errore(j->l, F_ELENCO, PH_SRV_ESITO_ERRORE, ph_t("Non riesco a leggere questa cartella del telefono"));
        job_libera(j);
        return;
    }
    bool male = false;
    for (size_t i = 0; i < el.numero; i++) {
        v[i].nome = strdup(el.voci[i].nome);
        v[i].nome_schermo = ph_dati_per_schermo(el.voci[i].nome);
        v[i].cartella = el.voci[i].cartella;
        v[i].dimensione = el.voci[i].dimensione;
        v[i].modificato = el.voci[i].modificato;
        if (v[i].nome == NULL || v[i].nome_schermo == NULL)
            male = true;
    }
    size_t n = el.numero;
    ph_file_elenco_libera(&el);
    if (male) {
        ph_srv_voci_libera(v, n);
        chiudi_errore(j->l, F_ELENCO, PH_SRV_ESITO_ERRORE, ph_t("Non riesco a leggere questa cartella del telefono"));
    } else {
        ph_servizi_lavoro_elenco(j->l, PH_SRV_ESITO_OK, NULL, v, n);
    }
    job_libera(j);
}

void sv_tel_elenca(void *b, PhSrvLavoro *l, const char *cartella)
{
    Sv *sv = b;
    Job *j = job_nuovo(l, cartella);
    if (j == NULL) {
        chiudi_errore(l, F_ELENCO, PH_SRV_ESITO_ERRORE, NULL);
        return;
    }
    sv_lavoro(sv, sv->p_lavori, elenca_lavoro, j);
}

/* ---- Nuova cartella ------------------------------------------------------------------- */
static void crea_lavoro(Sv *sv, void *dati)
{
    Job *j = dati;
    char cmd[PH_DATI_PERCORSO_MAX * 4 + 16];
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        chiudi_errore(j->l, F_FATTO, PH_SRV_ESITO_NON_COLLEGATO, NULL);
        job_libera(j);
        return;
    }
    if (strlen(j->testo) > PH_DATI_PERCORSO_MAX || ph_ossatura_comando_mkdir(j->testo, cmd, sizeof cmd) != 0) {
        sv_sessione_lascia(s);
        chiudi_errore(j->l, F_FATTO, PH_SRV_ESITO_ERRORE, ph_t("Non riesco a creare la cartella sul telefono"));
        job_libera(j);
        return;
    }
    PhBuffer err = PH_BUFFER_VUOTO;
    int codice = -1;
    PhErrore e = {{0}};
    int r = ph_shell_esegui(s->a, cmd, NULL, &err, &codice, 10000, &e);
    sv_sessione_lascia(s);
    PhSrvEsito es = PH_SRV_ESITO_OK;
    if (r != 0) {
        es = PH_SRV_ESITO_ERRORE;
    } else if (codice != 0) {
        const char *t = err.dati != NULL ? (const char *)err.dati : "";
        if (strstr(t, "Permission denied") != NULL || strstr(t, "Read-only") != NULL)
            es = PH_SRV_ESITO_NON_CONSENTITO;
        else if (strstr(t, "No such file") != NULL)
            es = PH_SRV_ESITO_NON_TROVATO;
        else
            es = PH_SRV_ESITO_ERRORE;
    }
    ph_buffer_libera(&err);
    ph_servizi_lavoro_fatto(j->l, es,
                            es == PH_SRV_ESITO_ERRORE ? ph_t("Non riesco a creare la cartella sul telefono") : NULL);
    job_libera(j);
}

void sv_tel_crea_cartella(void *b, PhSrvLavoro *l, const char *percorso)
{
    Sv *sv = b;
    Job *j = job_nuovo(l, percorso);
    if (j == NULL) {
        chiudi_errore(l, F_FATTO, PH_SRV_ESITO_ERRORE, NULL);
        return;
    }
    sv_lavoro(sv, sv->p_lavori, crea_lavoro, j);
}

/* ---- Miniatura ------------------------------------------------------------------------- */
static void miniatura_lavoro(Sv *sv, void *dati)
{
    Job *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        chiudi_errore(j->l, F_IMMAGINE, PH_SRV_ESITO_NON_COLLEGATO, NULL);
        job_libera(j);
        return;
    }
    PhBuffer jpeg = PH_BUFFER_VUOTO;
    PhErrore e = {{0}};
    int r = -1;
    if (!ph_servizi_lavoro_annullato(j->l) && strlen(j->testo) <= PH_DATI_PERCORSO_MAX)
        r = ph_dati_miniatura(s->d, j->testo, (uint16_t)j->lato, &jpeg, &e);
    sv_sessione_lascia(s);
    /* Le immagini sono ostili: la decodifica con i tetti la fa il pezzo comune. */
    ph_servizi_lavoro_immagine(j->l, r == 0 ? PH_SRV_ESITO_OK : PH_SRV_ESITO_ERRORE, NULL, jpeg.dati,
                               jpeg.lunghezza);
    ph_buffer_libera(&jpeg);
    job_libera(j);
}

void sv_tel_miniatura(void *b, PhSrvLavoro *l, const char *percorso, int lato)
{
    Sv *sv = b;
    Job *j = job_nuovo(l, percorso);
    if (j == NULL) {
        chiudi_errore(l, F_IMMAGINE, PH_SRV_ESITO_ERRORE, NULL);
        return;
    }
    j->lato = lato;
    sv_lavoro(sv, sv->p_lavori, miniatura_lavoro, j);
}

/* ---- Memoria --------------------------------------------------------------------------- */
static void memoria_lavoro(Sv *sv, void *dati)
{
    Job *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        chiudi_errore(j->l, F_MEMORIA, PH_SRV_ESITO_NON_COLLEGATO, NULL);
        job_libera(j);
        return;
    }
    PhDatiMemoria m = {0, 0};
    PhErrore e = {{0}};
    int r = ph_dati_memoria(s->d, &m, &e);
    sv_sessione_lascia(s);
    ph_servizi_lavoro_memoria(j->l, r == 0 ? PH_SRV_ESITO_OK : PH_SRV_ESITO_ERRORE, m.totale, m.libera);
    job_libera(j);
}

void sv_tel_memoria(void *b, PhSrvLavoro *l)
{
    Sv *sv = b;
    Job *j = job_nuovo(l, "");
    if (j == NULL) {
        chiudi_errore(l, F_MEMORIA, PH_SRV_ESITO_ERRORE, NULL);
        return;
    }
    sv_lavoro(sv, sv->p_lavori, memoria_lavoro, j);
}

/* ---- Lettura di un file ------------------------------------------------------------------ */
typedef struct {
    PhSrvLavoro *l;
    Sv *sv;
    atomic_bool annulla;
} Progresso;

/* Chiamata dal thread che copia: la copia si ferma se chi ha chiesto ha
 * rinunciato o l'applicazione si chiude. */
static void su_progresso(uint64_t fatti, uint64_t totale, void *u)
{
    (void)fatti;
    (void)totale;
    Progresso *p = u;
    if (ph_servizi_lavoro_annullato(p->l) || atomic_load(&p->sv->chiusura))
        atomic_store(&p->annulla, true);
}

static void leggi_lavoro(Sv *sv, void *dati)
{
    Job *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        chiudi_errore(j->l, F_BYTE, PH_SRV_ESITO_NON_COLLEGATO, NULL);
        job_libera(j);
        return;
    }
    PhErrore e = {{0}};
    PhSrvEsito es = PH_SRV_ESITO_ERRORE;
    GBytes *byte = NULL;
    char dir[PH_CFG_LUN_PERCORSO], nome[PH_CFG_LUN_FILE_CACHE], locale[PH_CFG_LUN_PERCORSO + PH_CFG_LUN_FILE_CACHE];
    PhFileVoce info;
    memset(&info, 0, sizeof info);
    /* Prima la misura: un file troppo grande non si copia nemmeno. */
    PhFileEsito r = ph_file_info(s->a, j->testo, &info, &e);
    if (r != PH_FILE_RIUSCITO) {
        es = ph_ossatura_esito_file(r);
    } else if (info.cartella || info.dimensione > j->massimo_byte) {
        es = PH_SRV_ESITO_ERRORE;
    } else if (ph_cfg_cartella_cache(dir, sizeof dir, &e) == 0 && g_mkdir_with_parents(dir, 0700) == 0) {
        /* Nella cache di YASDE, mai in /tmp; il nome e' nostro. */
        g_snprintf(nome, sizeof nome, "lettura-%d-%u.tmp", (int)getpid(), atomic_fetch_add(&sv->cache_n, 1));
        if (ph_cfg_percorso_cache(nome, locale, sizeof locale, &e) == 0) {
            Progresso p = {.l = j->l, .sv = sv};
            atomic_init(&p.annulla, false);
            r = ph_file_ricevi(s->a, j->testo, locale, su_progresso, &p, &p.annulla, &e);
            if (r == PH_FILE_RIUSCITO) {
                PhBuffer buf = PH_BUFFER_VUOTO;
                if (ph_cfg_leggi_file(locale, j->massimo_byte, &buf, &e) == 0) {
                    byte = g_bytes_new(buf.dati != NULL ? buf.dati : (const uint8_t *)"", buf.lunghezza);
                    es = PH_SRV_ESITO_OK;
                }
                ph_buffer_libera(&buf);
                (void)unlink(locale);
            } else {
                es = ph_ossatura_esito_file(r);
            }
        }
    }
    sv_sessione_lascia(s);
    ph_servizi_lavoro_byte(j->l, es, es == PH_SRV_ESITO_ERRORE ? ph_t("Non riesco a leggere questo file del telefono") : NULL,
                           byte);
    if (byte != NULL)
        g_bytes_unref(byte);
    job_libera(j);
}

void sv_tel_leggi(void *b, PhSrvLavoro *l, const char *percorso, size_t massimo)
{
    Sv *sv = b;
    Job *j = job_nuovo(l, percorso);
    if (j == NULL) {
        chiudi_errore(l, F_BYTE, PH_SRV_ESITO_ERRORE, NULL);
        return;
    }
    j->massimo_byte = massimo;
    sv_lavoro(sv, sv->p_lavori, leggi_lavoro, j);
}

/* ---- Ricerca ------------------------------------------------------------------------------ */
#define GRUPPO_MAX 32
#define GRUPPO_US (50 * 1000)

typedef struct {
    Sv *sv;
    SvSessione *s;
    PhSrvLavoro *l;
    PhDatiRicerca *r;       /* scritto sotto s->m prima che le richiamate lo leggano */
    atomic_flag finale;     /* la chiamata finale e' stata fatta */
    /* Gruppo in preparazione: solo le richiamate della ricerca (un thread). */
    char *perc[GRUPPO_MAX];
    char *schermo[GRUPPO_MAX];
    bool cartella[GRUPPO_MAX];
    int n;
    int info_fatte;         /* ph_file_info gia' chiesti (al massimo 30) */
    gint64 ultimo_invio_us;
} Ricerca;

static void ricerca_svuota_gruppo(Ricerca *r, bool invia)
{
    if (invia && r->n > 0) {
        PhSrvTrovato t[GRUPPO_MAX];
        uint64_t dim[GRUPPO_MAX];
        for (int i = 0; i < r->n; i++) {
            /* Il telefono non manda la dimensione nei risultati (protocollo
             * 2.7): per i primi 30 file la si chiede qui, sul thread della
             * ricerca (mai sul thread GTK). Cartelle e il resto: ignota. */
            dim[i] = UINT64_MAX;
            if (!r->cartella[i] && r->info_fatte < 30) {
                PhFileVoce v;
                PhErrore e = {{0}};
                memset(&v, 0, sizeof v);
                r->info_fatte++;
                if (ph_file_info(r->s->a, r->perc[i], &v, &e) == PH_FILE_RIUSCITO)
                    dim[i] = v.dimensione;
            }
            t[i] = (PhSrvTrovato){.percorso = r->perc[i], .percorso_schermo = r->schermo[i],
                                  .cartella = r->cartella[i]};
        }
        ph_servizi_lavoro_trovati_dimensioni(r->l, t, dim, (size_t)r->n);
        r->ultimo_invio_us = g_get_monotonic_time();
    }
    for (int i = 0; i < r->n; i++) {
        free(r->perc[i]);
        free(r->schermo[i]);
    }
    r->n = 0;
}

/* Si prende il diritto di annullare e liberare la ricerca: chi toglie la
 * ricerca dall'elenco della sessione e' l'unico a chiamare
 * ph_dati_cerca_annulla (una volta sola) e a liberarla. */
static bool ricerca_reclama(Ricerca *r)
{
    g_mutex_lock(&r->s->m);
    GList *p = g_list_find(r->s->ricerche, r);
    if (p != NULL)
        r->s->ricerche = g_list_delete_link(r->s->ricerche, p);
    g_mutex_unlock(&r->s->m);
    return p != NULL;
}

static void ricerca_finale(Ricerca *r, PhSrvEsito esito, const char *frase, bool massimo)
{
    if (!atomic_flag_test_and_set(&r->finale))
        ph_servizi_lavoro_cerca_fine(r->l, esito, frase, massimo);
}

static void ricerca_libera(Ricerca *r)
{
    ricerca_svuota_gruppo(r, false);
    free(r);
}

/* Annulla (da un altro thread o dalla propria richiamata) una ricerca di cui
 * si e' preso il diritto. */
static void ricerca_chiudi_reclamata(Ricerca *r, PhSrvEsito esito, const char *frase)
{
    ph_dati_cerca_annulla(r->r);
    ricerca_finale(r, esito, frase, false);
    ricerca_libera(r);
}

static void su_trovato(const char *percorso, bool cartella, void *u)
{
    Ricerca *r = u;
    if (ph_servizi_lavoro_annullato(r->l) || atomic_load(&r->sv->chiusura)) {
        /* Chi ha chiesto ha rinunciato: si ferma la ricerca sul telefono. */
        if (ricerca_reclama(r))
            ricerca_chiudi_reclamata(r, PH_SRV_ESITO_ERRORE, NULL);
        return;
    }
    char *p = strdup(percorso);
    char *sc = ph_dati_per_schermo(percorso);
    if (p == NULL || sc == NULL) {
        free(p);
        free(sc);
        return;
    }
    r->perc[r->n] = p;
    r->schermo[r->n] = sc;
    r->cartella[r->n] = cartella;
    r->n++;
    /* Il primo risultato parte subito; poi a gruppi (50 ms o 32). */
    if (r->n == GRUPPO_MAX || g_get_monotonic_time() - r->ultimo_invio_us >= GRUPPO_US)
        ricerca_svuota_gruppo(r, true);
}

static void su_fine(PhDatiCercaMotivo motivo, void *u)
{
    Ricerca *r = u;
    ricerca_svuota_gruppo(r, true);
    if (motivo == PH_DATI_CERCA_ERRORE)
        ricerca_finale(r, PH_SRV_ESITO_ERRORE, ph_t("Non riesco a cercare in questa cartella"), false);
    else
        ricerca_finale(r, PH_SRV_ESITO_OK, NULL, motivo == PH_DATI_CERCA_MASSIMO);
    /* Il diritto di annullare l'ha chi la toglie dall'elenco: se e' gia'
     * andata (sessione che si chiude, annullamento) ci pensa lui. */
    if (ricerca_reclama(r)) {
        ph_dati_cerca_annulla(r->r); /* lecita dal proprio avviso, va fatta sempre */
        ricerca_libera(r);
    }
}

static void cerca_lavoro(Sv *sv, void *dati)
{
    Job *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        chiudi_errore(j->l, F_CERCA, PH_SRV_ESITO_NON_COLLEGATO, NULL);
        job_libera(j);
        return;
    }
    /* Le ricerche gia' abbandonate dalla shell (l'utente ha digitato altro) si
     * fermano ora: il telefono non deve continuare a cercare. */
    GList *da_fermare = NULL;
    g_mutex_lock(&s->m);
    for (GList *p = s->ricerche; p != NULL;) {
        GList *succ = p->next;
        Ricerca *v = p->data;
        if (ph_servizi_lavoro_annullato(v->l)) {
            s->ricerche = g_list_delete_link(s->ricerche, p);
            da_fermare = g_list_prepend(da_fermare, v);
        }
        p = succ;
    }
    guint aperte = g_list_length(s->ricerche);
    g_mutex_unlock(&s->m);
    for (GList *p = da_fermare; p != NULL; p = p->next)
        ricerca_chiudi_reclamata(p->data, PH_SRV_ESITO_ERRORE, NULL);
    g_list_free(da_fermare);

    Ricerca *r = calloc(1, sizeof *r);
    PhErrore e = {{0}};
    if (r == NULL || aperte >= SV_RICERCHE_MAX || strlen(j->cerca) > PH_DATI_CERCA_TESTO_MAX ||
        strlen(j->testo) > PH_DATI_PERCORSO_MAX) {
        free(r);
        sv_sessione_lascia(s);
        chiudi_errore(j->l, F_CERCA, PH_SRV_ESITO_ERRORE, ph_t("Non riesco a cercare in questa cartella"));
        job_libera(j);
        return;
    }
    r->sv = sv;
    r->s = s;
    r->l = j->l;
    atomic_flag_clear(&r->finale);
    PhDatiCerca c = {.cartella = j->testo, .testo = j->cerca, .solo_cartelle = j->solo_cartelle,
                     .massimo = (uint16_t)(j->massimo > PH_DATI_CERCA_MAX ? PH_DATI_CERCA_MAX : j->massimo)};
    /* Sotto il lucchetto della sessione: le richiamate (che prendono lo stesso
     * lucchetto per togliere la ricerca) aspettano che sia registrata.
     * ph_dati_cerca_avvia torna subito. */
    g_mutex_lock(&s->m);
    if (!s->attiva) {
        g_mutex_unlock(&s->m);
        free(r);
        sv_sessione_lascia(s);
        chiudi_errore(j->l, F_CERCA, PH_SRV_ESITO_NON_COLLEGATO, NULL);
        job_libera(j);
        return;
    }
    r->r = ph_dati_cerca_avvia(s->d, &c, su_trovato, su_fine, r, &e);
    if (r->r != NULL)
        s->ricerche = g_list_prepend(s->ricerche, r);
    g_mutex_unlock(&s->m);
    if (r->r == NULL) {
        free(r);
        sv_sessione_lascia(s);
        chiudi_errore(j->l, F_CERCA, PH_SRV_ESITO_ERRORE, ph_t("Non riesco a cercare in questa cartella"));
        job_libera(j);
        return;
    }
    sv_sessione_lascia(s);
    job_libera(j);
}

void sv_tel_cerca(void *b, PhSrvLavoro *l, const char *cartella, const char *testo, bool solo_cartelle,
                  unsigned massimo)
{
    Sv *sv = b;
    Job *j = job_nuovo(l, cartella);
    if (j == NULL) {
        chiudi_errore(l, F_CERCA, PH_SRV_ESITO_ERRORE, NULL);
        return;
    }
    j->cerca = strdup(testo);
    if (j->cerca == NULL) {
        job_libera(j);
        chiudi_errore(l, F_CERCA, PH_SRV_ESITO_ERRORE, NULL);
        return;
    }
    j->solo_cartelle = solo_cartelle;
    j->massimo = massimo;
    sv_lavoro(sv, sv->p_lavori, cerca_lavoro, j);
}

/* Prima di restituire il prestito: le ricerche aperte si annullano (regola 4).
 * Il gestore chiama questa quando nessuno usa piu' la sessione, quindi nessuno
 * ne aggiunge; le richiamate possono ancora togliersi da sole. */
void sv_file_teardown(SvSessione *s)
{
    g_mutex_lock(&s->m);
    GList *tutte = s->ricerche;
    s->ricerche = NULL;
    g_mutex_unlock(&s->m);
    for (GList *p = tutte; p != NULL; p = p->next)
        ricerca_chiudi_reclamata(p->data, PH_SRV_ESITO_NON_COLLEGATO, NULL);
    g_list_free(tutte);
}

#endif
