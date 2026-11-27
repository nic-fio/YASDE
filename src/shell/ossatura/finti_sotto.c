/* Pezzi sotto FINTI per la prova dei servizi veri (prova_servizi_veri.c):
 * collegamento, dati, componente, video, audio, file, adb e trasferimenti
 * sostituiti da funzioni che dormono (come la rete) e contano le chiamate.
 * Solo per la prova: non entra mai nel programma. */
#include <glib.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/audio.h"
#include "collegamento/collegamento.h"
#include "dati/dati.h"
#include "file/file.h"
#include "finti_sotto.h"
#include "meteo/meteo.h"
#include "trasferimenti/installa.h"
#include "trasferimenti/trasferimenti.h"
#include "video/cattura.h"
#include "video/decodifica.h"
#include "video/esporta.h"

FintiSotto finti;

static void dormi_ms(int ms)
{
    g_usleep((gulong)ms * 1000);
}

#define RITARDO_MS 300

static GMutex input_m;
static GCond input_c;

void finti_apri_cancello_input(void)
{
    g_mutex_lock(&input_m);
    atomic_store(&finti.input_gate, false);
    g_cond_broadcast(&input_c);
    g_mutex_unlock(&input_m);
}

static GMutex pag_m;
static GCond pag_c;

void finti_apri_cancello_pagina(void)
{
    g_mutex_lock(&pag_m);
    atomic_store(&finti.pag_gate, false);
    g_cond_broadcast(&pag_c);
    g_mutex_unlock(&pag_m);
}

static void ordine_scrivi(char c)
{
    int k = atomic_fetch_add(&finti.ordine_n, 1);
    if (k < (int)sizeof finti.ordine - 1)
        finti.ordine[k] = c;
}


/* ---- Collegamento ----------------------------------------------------------- */
struct PhCollegamento {
    int x;
};
struct PhColPrestito {
    PhColTelefono tel;
    /* Oggetti prestati: allocati per prestito e liberati alla restituzione,
     * cosi' AddressSanitizer vede ogni uso dopo ph_col_restituisci. */
    void *adb, *comp, *dati;
};

/* Legge un byte dell'oggetto prestato: un uso dopo la restituzione e' un errore per ASan. */
static void tocca(const void *p)
{
    volatile char c = *(const volatile char *)p;
    (void)c;
}

void ph_col_opzioni_predefinite(PhColOpzioni *o)
{
    memset(o, 0, sizeof *o);
}

PhCollegamento *ph_col_avvia(const PhColOpzioni *o, PhErrore *e)
{
    (void)e;
    finti.opz = *o;
    return calloc(1, sizeof(PhCollegamento));
}

static gpointer fuoco_perdita(gpointer p)
{
    (void)p;
    finti.opz.in_perdita(finti.opz.utente);
    return NULL;
}

int ph_col_chiudi(PhCollegamento *c)
{
    /* Come il vero: dal suo thread avvisa "in perdita", aspetta i prestiti
     * (al massimo 3 s), poi l'avviso CHIUSO. */
    finti.connesso = false;
    GThread *t = g_thread_new("fuoco", fuoco_perdita, NULL);
    for (int i = 0; i < 3000 && atomic_load(&finti.presti) > 0; i++)
        g_usleep(1000);
    g_thread_join(t);
    PhColAvviso a;
    memset(&a, 0, sizeof a);
    a.stato = PH_COL_CHIUSO;
    finti.opz.avviso(&a, finti.opz.utente);
    int r = atomic_load(&finti.presti) > 0 ? 1 : 0;
    free(c);
    return r;
}

int ph_col_abbina(PhCollegamento *c, const char *codice, PhErrore *e)
{
    (void)c;
    atomic_fetch_add(&finti.abb_n, 1);
    atomic_store(&finti.abb_codice_ok, strcmp(codice, "123456") == 0);
    return atomic_load(&finti.abb_rifiuta) ? ph_errore(e, "rifiutato") : 0;
}

int ph_col_abbina_a(PhCollegamento *c, const char *host, uint16_t porta, const char *codice, PhErrore *e)
{
    (void)c;
    atomic_fetch_add(&finti.abb_a_n, 1);
    atomic_store(&finti.abb_host_ok, strcmp(host, "192.0.2.7") == 0);
    atomic_store(&finti.abb_porta, porta);
    atomic_store(&finti.abb_codice_ok, strcmp(codice, "123456") == 0);
    return atomic_load(&finti.abb_rifiuta) ? ph_errore(e, "rifiutato") : 0;
}

int ph_col_riconnetti_ora(PhCollegamento *c, PhErrore *e)
{
    (void)c;
    (void)e;
    atomic_fetch_add(&finti.riconnetti, 1);
    return 0;
}

PhColPrestito *ph_col_presta(PhCollegamento *c, PhErrore *e)
{
    (void)c;
    if (!finti.connesso)
        return ph_errore(e, "nessuna sessione") ? NULL : NULL;
    PhColPrestito *p = calloc(1, sizeof *p);
    snprintf(p->tel.seriale, sizeof p->tel.seriale, atomic_load(&finti.altro_seriale) ? "R5CT0000001" : "R5CT0000000");
    snprintf(p->tel.nome, sizeof p->tel.nome, "Modello finto");
    p->tel.info.api = 34;
    p->tel.info.larghezza = 1080;
    p->tel.info.altezza = 2340;
    p->tel.info.densita = 420;
    p->adb = calloc(1, 16);
    p->comp = calloc(1, 16);
    p->dati = calloc(1, 16);
    atomic_fetch_add(&finti.presti, 1);
    atomic_fetch_add(&finti.presti_totali, 1);
    return p;
}

void ph_col_restituisci(PhColPrestito *p)
{
    atomic_fetch_sub(&finti.presti, 1);
    free(p->adb);
    free(p->comp);
    free(p->dati);
    free(p);
}

PhAdb *ph_col_prestito_adb(const PhColPrestito *p) { return p->adb; }
PhComponente *ph_col_prestito_componente(const PhColPrestito *p) { return p->comp; }
PhDati *ph_col_prestito_dati(const PhColPrestito *p) { return p->dati; }
const PhColTelefono *ph_col_prestito_telefono(const PhColPrestito *p) { return &p->tel; }
bool ph_col_prestito_attivo(const PhColPrestito *p) { (void)p; return finti.connesso; }

/* ---- Dati ------------------------------------------------------------------------ */
char *ph_dati_per_schermo(const char *testo) { return strdup(testo); }

int ph_dati_app_elenco(PhDati *d, PhDatiApp **app, size_t *quante, PhErrore *e)
{
    tocca(d);
    (void)d;
    (void)e;
    dormi_ms(RITARDO_MS);
    atomic_fetch_add(&finti.app_elenchi, 1);
    PhDatiApp *v = calloc(2, sizeof *v);
    v[0] = (PhDatiApp){strdup("it.finto.uno"), strdup("Uno"), false, false};
    v[1] = (PhDatiApp){strdup("it.finto.due"), strdup("Due"), false, true};
    *app = v;
    *quante = 2;
    return 0;
}

void ph_dati_app_libera(PhDatiApp *app, size_t quante)
{
    for (size_t i = 0; i < quante; i++) {
        free(app[i].pacchetto);
        free(app[i].nome);
    }
    free(app);
}

int ph_dati_app_icona(PhDati *d, const char *pacchetto, uint16_t lato, PhBuffer *png, PhErrore *e)
{
    tocca(d);
    (void)d;
    (void)pacchetto;
    (void)lato;
    (void)png;
    dormi_ms(RITARDO_MS);
    return ph_errore(e, "nessuna icona");
}

int ph_dati_stato(PhDati *d, PhDatiStato *stato, PhErrore *e)
{
    tocca(d);
    (void)d;
    (void)e;
    *stato = (PhDatiStato){.batteria = 70, .rete = PH_DATI_RETE_WIFI, .segnale = 3};
    return 0;
}

int ph_dati_notifiche(PhDati *d, PhDatiNotifica **n, size_t *quante, PhErrore *e)
{
    tocca(d);
    (void)d;
    (void)e;
    dormi_ms(RITARDO_MS);
    PhDatiNotifica *v = calloc(1, sizeof *v);
    v[0] = (PhDatiNotifica){strdup("k1"), strdup("it.finto.uno"), strdup("Titolo finto"), strdup(""), 1000};
    *n = v;
    *quante = 1;
    return 0;
}

void ph_dati_notifiche_libera(PhDatiNotifica *n, size_t quante)
{
    for (size_t i = 0; i < quante; i++) {
        free(n[i].chiave);
        free(n[i].pacchetto);
        free(n[i].titolo);
        free(n[i].testo);
    }
    free(n);
}

int ph_dati_miniatura(PhDati *d, const char *percorso, uint16_t lato, PhBuffer *jpeg, PhErrore *e)
{
    tocca(d);
    (void)d;
    (void)percorso;
    (void)lato;
    (void)jpeg;
    dormi_ms(RITARDO_MS);
    return ph_errore(e, "nessuna miniatura");
}

int ph_dati_memoria(PhDati *d, PhDatiMemoria *m, PhErrore *e)
{
    tocca(d);
    (void)d;
    (void)e;
    dormi_ms(RITARDO_MS);
    *m = (PhDatiMemoria){1000, 400};
    return 0;
}

PhDatiRicerca *ph_dati_cerca_avvia(PhDati *d, const PhDatiCerca *r, PhDatiTrovato trovato, PhDatiCercaFine fine,
                                   void *utente, PhErrore *e)
{
    (void)d;
    (void)r;
    (void)e;
    int k = atomic_fetch_add(&finti.ricerche_avviate, 1);
    if (k < FINTI_RICERCHE_MAX) {
        finti.trovato[k] = trovato;
        finti.fine[k] = fine;
        finti.utente_ricerca[k] = utente;
    }
    return malloc(1); /* un doppio ph_dati_cerca_annulla e' una doppia free: lo vede ASan */
}

void ph_dati_cerca_annulla(PhDatiRicerca *r)
{
    atomic_fetch_add(&finti.ricerche_annullate, 1);
    free(r);
}

int ph_dati_appunti_apri(PhDati *d, PhDatiAvvisoAppunti avviso, void *utente, PhErrore *e)
{
    (void)d;
    (void)avviso;
    (void)utente;
    (void)e;
    atomic_fetch_add(&finti.appunti_aperti, 1);
    return 0;
}

int ph_dati_appunti_imposta(PhDati *d, const char *testo, PhErrore *e)
{
    tocca(d);
    (void)d;
    (void)testo;
    (void)e;
    dormi_ms(RITARDO_MS);
    return 0;
}

int ph_dati_appunti_incolla(PhDati *d, uint32_t id, const char *testo, PhErrore *e)
{
    (void)d;
    (void)id;
    (void)testo;
    (void)e;
    return 0;
}

void ph_dati_appunti_chiudi(PhDati *d)
{
    (void)d;
    atomic_fetch_add(&finti.appunti_chiusi, 1);
}

/* ---- Componente, video, input ------------------------------------------------------ */
int ph_componente_richiesta(PhComponente *c, uint8_t tipo, const uint8_t *corpo, size_t n, PhBuffer *risposta,
                            PhErrore *e)
{
    tocca(c);
    dormi_ms(RITARDO_MS);
    if (tipo == 0x60 && corpo != NULL && n == 1)
        atomic_fetch_add(corpo[0] != 0 ? &finti.pan_on : &finti.pan_off, 1);
    if (tipo == 0x60 && atomic_load(&finti.pan_rifiuta))
        return ph_errore(e, "rifiutato dal telefono");
    if (tipo == 0x61 && risposta != NULL) {
        uint8_t r[2] = {atomic_load(&finti.pan_stato_acceso) ? 1 : 0, atomic_load(&finti.pan_stato_in_mano) ? 1 : 0};
        ph_buffer_aggiungi(risposta, r, 2);
    }
    return 0;
}

PhCanale *ph_componente_canale(PhComponente *c, const char *riga, PhErrore *e)
{
    (void)c;
    (void)riga;
    (void)e;
    return NULL;
}

int ph_componente_ascolta(PhComponente *c, uint8_t da, uint8_t a, PhEventoGrezzo f, void *utente, PhErrore *e)
{
    (void)c;
    (void)a;
    (void)e;
    if (f == NULL)
        atomic_fetch_add(&finti.ascolti_tolti, 1);
    if (da == 0x6e) { /* evento del pannello: la prova lo fa partire con finti_pannello_evento */
        finti.pan_cb = f;
        finti.pan_utente = utente;
    }
    return 0;
}

PhVideoFlusso *ph_video_apri(PhComponente *c, const PhVideoRichiesta *r, PhErrore *e)
{
    tocca(c);
    (void)c;
    (void)e;
    atomic_store(&finti.apri_dens, r->densita);
    atomic_store(&finti.apri_pagina, r->pagina);
    dormi_ms(RITARDO_MS);
    atomic_fetch_add(&finti.video_aperti, 1);
    return calloc(1, 16);
}

uint32_t ph_video_id(const PhVideoFlusso *f) { (void)f; return 7; }

int ph_video_leggi(PhVideoFlusso *f, PhVideoPacchetto *p, int timeout_ms, PhErrore *e)
{
    (void)e;
    static const uint8_t nal[] = {0, 0, 0, 1, 0x67, 0x42, 0, 0x1f, 0, 0, 0, 1, 0x68, 0xce, 0x3c, 0x80};
    if (!atomic_load(&finti.video_pacchetti)) {
        dormi_ms(timeout_ms < 10 ? timeout_ms : 10);
        return PH_CANALE_TEMPO;
    }
    dormi_ms(5);
    int *n = (int *)f; /* il primo pacchetto di ogni sessione e' la configurazione */
    memset(p, 0, sizeof *p);
    p->configurazione = *n == 0;
    p->chiave = *n % 30 == 1;
    p->larghezza = 640;
    p->altezza = 400;
    p->tempo_us = (uint64_t)(*n) * 16000;
    p->dati = nal;
    p->lunghezza = sizeof nal;
    (*n)++;
    return 1;
}

int ph_video_ridimensiona(PhVideoFlusso *f, uint16_t w, uint16_t h, uint16_t d, uint16_t *wv, uint16_t *hv,
                          PhErrore *e)
{
    (void)f;
    (void)e;
    atomic_store(&finti.ridim_dens, d);
    atomic_fetch_add(&finti.ridim_n, 1);
    ordine_scrivi('D');
    if (atomic_load(&finti.ridim_rifiuta))
        return ph_errore(e, "rifiutato");
    *wv = w;
    *hv = h;
    return 0;
}

int ph_video_chiave(PhVideoFlusso *f, PhErrore *e)
{
    (void)f; (void)e;
    atomic_fetch_add(&finti.chiavi, 1);
    return 0;
}
int ph_video_pagina(PhVideoFlusso *f, uint8_t pagina, PhErrore *e)
{
    (void)f;
    atomic_fetch_add(&finti.pag_n, 1);
    atomic_store(&finti.pag_ultima, pagina);
    g_mutex_lock(&pag_m);
    gint64 scade = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
    if (atomic_load(&finti.pag_gate)) {
        atomic_store(&finti.pag_in_corso, true);
        while (atomic_load(&finti.pag_gate) && g_cond_wait_until(&pag_c, &pag_m, scade)) {}
        atomic_store(&finti.pag_in_corso, false);
    }
    g_mutex_unlock(&pag_m);
    ordine_scrivi('G');
    if (atomic_load(&finti.pag_rifiuta))
        return ph_errore(e, "rifiutata");
    return 0;
}
int ph_video_pausa(PhVideoFlusso *f, PhErrore *e)
{
    (void)f; (void)e;
    ordine_scrivi('P');
    return 0;
}
int ph_video_riprendi(PhVideoFlusso *f, PhErrore *e)
{
    (void)f; (void)e;
    ordine_scrivi('R');
    return 0;
}

void ph_video_chiudi(PhVideoFlusso *f, bool togli_task)
{
    (void)togli_task;
    ordine_scrivi('V');
    atomic_fetch_add(&finti.video_chiusi, 1);
    free(f);
}

int ph_input_tocco(PhComponente *c, uint32_t id, uint16_t w, uint16_t h, uint8_t az, uint16_t x, uint16_t y,
                   uint8_t pul, PhErrore *e)
{
    tocca(c);
    (void)id; (void)w; (void)h; (void)x; (void)y; (void)pul; (void)e;
    atomic_fetch_add(&finti.tocco_tentati, 1);
    atomic_store(&finti.tocco_az, az);
    if (az == PH_TOCCO_ANNULLA)
        atomic_fetch_add(&finti.tocchi_annulla, 1);
    g_mutex_lock(&input_m);
    while (atomic_load(&finti.input_gate)) /* canale dell'input fermo */
        g_cond_wait(&input_c, &input_m);
    g_mutex_unlock(&input_m);
    dormi_ms(100); /* il telefono ci mette un po': se la pausa lo sorpassa si vede nell'ordine */
    atomic_fetch_add(&finti.tocchi, 1);
    ordine_scrivi('T');
    return 0;
}

int ph_input_pizzico(PhComponente *c, uint32_t id, uint16_t w, uint16_t h, uint8_t fase, uint16_t x, uint16_t y,
                     int16_t passo, PhErrore *e)
{
    tocca(c);
    (void)id; (void)e;
    atomic_store(&finti.pz_w, w);
    atomic_store(&finti.pz_h, h);
    atomic_store(&finti.pz_x, x);
    atomic_store(&finti.pz_y, y);
    atomic_store(&finti.pz_passo, passo);
    if (fase == PH_PIZZICO_ANNULLA)
        atomic_fetch_add(&finti.pz_annulla_n, 1);
    ordine_scrivi("icfa"[fase & 3]); /* i inizio, c continua, f fine, a annulla */
    atomic_fetch_add(&finti.pz_n, 1);
    return 0;
}

int ph_input_rotella(PhComponente *c, uint32_t id, uint16_t w, uint16_t h, uint16_t x, uint16_t y, int16_t o,
                     int16_t v, PhErrore *e)
{
    (void)c; (void)id; (void)w; (void)h; (void)x; (void)y; (void)o; (void)v; (void)e;
    return 0;
}

int ph_input_tasto(PhComponente *c, uint32_t id, bool giu, uint32_t codice, uint32_t meta, PhErrore *e)
{
    (void)c; (void)id; (void)giu; (void)codice; (void)meta; (void)e;
    return 0;
}

int ph_input_testo(PhComponente *c, uint32_t id, const char *t, PhErrore *e)
{
    (void)c; (void)id; (void)t; (void)e;
    return 0;
}

int ph_input_indietro(PhComponente *c, uint32_t id, PhErrore *e)
{
    (void)c; (void)id; (void)e;
    return 0;
}

/* ---- Decodifica ed esportazione: un decodificatore finto che RIESCE ------------------------------ */
struct PhDecodifica {
    bool nuovo;   /* appena creato: aspetta una configurazione */
    int pronti;   /* fotogrammi decodificati non ancora presi */
    PhVia via;    /* la via scelta alla creazione */
    bool ripiego; /* la via hardware e' passata al software a meta' (come ripiego_software vero) */
};

PhDecodifica *ph_decodifica_crea(PhVia via, PhErrore *e)
{
    (void)e;
    atomic_fetch_add(&finti.dec_creati, 1);
    PhDecodifica *d = calloc(1, sizeof *d);
    d->nuovo = true;
    /* Come il vero: la via esplicita si rispetta; l'automatica da' VA-API, o il software se non c'e' hardware. */
    d->via = via == PH_VIA_SOFTWARE || atomic_load(&finti.dec_senza_hw) ? PH_VIA_SOFTWARE
             : via == PH_VIA_AUTOMATICA                                   ? PH_VIA_VAAPI
                                                                          : via;
    return d;
}
PhVia ph_decodifica_via(const PhDecodifica *d) { return d->ripiego ? PH_VIA_SOFTWARE : d->via; }
const char *ph_via_nome(PhVia via)
{
    switch (via) {
    case PH_VIA_VULKAN: return "vulkan";
    case PH_VIA_VAAPI: return "vaapi";
    case PH_VIA_SOFTWARE: return "software";
    default: return "automatica";
    }
}
void ph_decodifica_libera(PhDecodifica *d) { free(d); }
int ph_decodifica_dai(PhDecodifica *d, bool c, const uint8_t *p, size_t n, uint64_t t, PhErrore *e)
{
    (void)p; (void)n; (void)t;
    if (c) {
        if (d->nuovo)
            atomic_fetch_add(&finti.cfg_su_nuovo, 1); /* la configurazione conservata e' stata ridata */
        d->nuovo = false;
        return 0;
    }
    if (atomic_load(&finti.dai_fallisce))
        return ph_errore(e, "pacchetto rovinato");
    d->pronti++;
    if (atomic_load(&finti.dec_ripiego) && d->via != PH_VIA_SOFTWARE)
        d->ripiego = true;
    if (d->ripiego && d->via != PH_VIA_SOFTWARE)
        return ph_errore(e, "la via non regge questo flusso"); /* come il vero: il fotogramma esce, dai torna errore */
    return 0;
}
int ph_decodifica_prendi(PhDecodifica *d, struct AVFrame **f, PhErrore *e)
{
    (void)e;
    if (d->pronti == 0)
        return 0;
    d->pronti--;
    *f = (struct AVFrame *)malloc(1);
    return 1;
}
static const uint8_t pixel[640 * 4] = {0};
int ph_esporta(struct AVFrame **f, PhFotogramma *u, PhErrore *e)
{
    (void)e;
    free(*f);
    *f = NULL;
    memset(u, 0, sizeof *u);
    u->tipo = PH_FOTO_MEMORIA;
    u->larghezza = 640;
    u->altezza = 400;
    u->formato = PH_FOTO_B8G8R8A8_PREMOLT;
    u->dati = pixel;
    u->passo = 0; /* il valore non serve: la prova rilascia e basta */
    atomic_fetch_add(&finti.esportati, 1);
    return PH_ESPORTA_MEMORIA;
}

/* ---- Audio ------------------------------------------------------------------------------ */
PhAudio *ph_audio_crea(PhCanale *c, PhAudioAvviso a, void *ctx, PhErrore *e)
{
    (void)c; (void)a; (void)ctx;
    ph_errore(e, "finto");
    return NULL;
}
int ph_audio_avvia(PhAudio *a, PhErrore *e) { (void)a; return ph_errore(e, "finto"); }
void ph_audio_distruggi(PhAudio *a) { (void)a; }
void ph_audio_volume(PhAudio *a, int p) { (void)a; (void)p; }
void ph_audio_muto(PhAudio *a, bool m) { (void)a; (void)m; }

/* ---- File e shell ----------------------------------------------------------------------- */
PhFileEsito ph_file_elenca(PhAdb *a, const char *cartella, PhFileElenco *el, PhErrore *e)
{
    tocca(a);
    (void)a;
    (void)cartella;
    (void)e;
    dormi_ms(RITARDO_MS);
    el->voci = calloc(2, sizeof *el->voci);
    el->voci[0] = (PhFileVoce){strdup("Documenti"), true, false, 0, 10};
    el->voci[1] = (PhFileVoce){strdup("nota.txt"), false, false, 12, 20};
    el->numero = 2;
    return PH_FILE_RIUSCITO;
}

void ph_file_elenco_libera(PhFileElenco *el)
{
    for (size_t i = 0; i < el->numero; i++)
        free(el->voci[i].nome);
    free(el->voci);
    el->voci = NULL;
    el->numero = 0;
}

PhFileEsito ph_file_info(PhAdb *a, const char *p, PhFileVoce *v, PhErrore *e)
{
    tocca(a);
    (void)a; (void)p; (void)e;
    dormi_ms(RITARDO_MS);
    memset(v, 0, sizeof *v);
    v->dimensione = 10;
    return PH_FILE_RIUSCITO;
}

PhFileEsito ph_file_ricevi(PhAdb *a, const char *r, const char *l, PhFileProgresso pr, void *u,
                           const atomic_bool *an, PhErrore *e)
{
    (void)a; (void)r; (void)l; (void)pr; (void)u; (void)an;
    ph_errore(e, "finto");
    return PH_FILE_ERRORE;
}

int ph_shell_esegui(PhAdb *a, const char *comando, PhBuffer *uscita, PhBuffer *errori, int *codice, int timeout_ms,
                    PhErrore *e)
{
    tocca(a);
    (void)a; (void)comando; (void)uscita; (void)errori; (void)timeout_ms; (void)e;
    dormi_ms(RITARDO_MS);
    if (codice != NULL)
        *codice = 0;
    return 0;
}

/* ---- Trasferimenti ------------------------------------------------------------------------ */
struct PhTrasferimenti {
    int x;
};

PhTrasferimenti *ph_tr_crea(PhAdb *a, PhTrAvviso avviso, void *utente, PhErrore *e)
{
    tocca(a);
    (void)a; (void)avviso; (void)utente; (void)e;
    atomic_fetch_add(&finti.tr_creati, 1);
    return calloc(1, sizeof(PhTrasferimenti));
}

void ph_tr_chiudi(PhTrasferimenti *t)
{
    if (t != NULL)
        atomic_fetch_add(&finti.tr_chiusi, 1);
    free(t);
}

int ph_tr_accoda(PhTrasferimenti *t, PhTrVerso v, const char *o, const char *c, uint64_t *id, PhErrore *e)
{
    (void)t; (void)v; (void)o; (void)c; (void)id;
    return ph_errore(e, "finto");
}
int ph_tr_annulla(PhTrasferimenti *t, uint64_t id, PhErrore *e) { (void)t; (void)id; return ph_errore(e, "finto"); }
void ph_tr_annulla_tutto(PhTrasferimenti *t) { (void)t; }
int ph_tr_elenca(PhTrasferimenti *t, PhTrElenco *el, PhErrore *e)
{
    (void)t; (void)e;
    el->voci = NULL;
    el->numero = 0;
    return 0;
}
void ph_tr_elenco_libera(PhTrElenco *el) { (void)el; }
bool ph_tr_coda_ferma(PhTrasferimenti *t) { (void)t; return false; }
int ph_tr_riprendi(PhTrasferimenti *t, PhAdb *nuova, PhErrore *e)
{
    (void)t; (void)e;
    atomic_fetch_add(nuova == NULL ? &finti.tr_staccata : &finti.tr_ripresa, 1);
    return 0;
}
void ph_tr_rimuovi_finiti(PhTrasferimenti *t) { (void)t; }

/* ---- Aggiunte: installazione, dettaglio memoria, calendario ------------------------------ */
int ph_inst_pulisci(PhAdb *a, PhErrore *e)
{
    tocca(a);
    (void)e;
    atomic_fetch_add(&finti.pulisci, 1);
    return 0;
}

PhInstEsito ph_inst_installa(PhAdb *a, const char *apk, PhInstProgresso pr, void *u, const atomic_bool *annulla,
                             PhInstRisultato *r, PhErrore *e)
{
    tocca(a);
    (void)a; (void)apk; (void)pr; (void)u; (void)annulla; (void)e;
    dormi_ms(RITARDO_MS);
    memset(r, 0, sizeof *r);
    r->esito = PH_INST_RIUSCITO;
    snprintf(r->messaggio, sizeof r->messaggio, "ok");
    return PH_INST_RIUSCITO;
}

PhInstEsito ph_inst_disinstalla(PhAdb *a, const char *pacchetto, PhInstRisultato *r, PhErrore *e)
{
    tocca(a);
    (void)a; (void)pacchetto; (void)e;
    dormi_ms(RITARDO_MS);
    memset(r, 0, sizeof *r);
    r->esito = PH_INST_RIUSCITO;
    snprintf(r->messaggio, sizeof r->messaggio, "ok");
    return PH_INST_RIUSCITO;
}

int ph_dati_memoria_dettaglio(PhDati *d, PhDatiMemoriaDettaglio *m, PhErrore *e)
{
    (void)d; (void)e;
    dormi_ms(RITARDO_MS);
    memset(m, 0, sizeof *m);
    m->immagini = (PhDatiVoceMemoria){10, true};
    return 0;
}

int ph_dati_calendario(PhDati *d, uint8_t massimo, PhDatiEvento **ev, size_t *n, PhErrore *e)
{
    (void)d; (void)massimo; (void)ev; (void)n;
    dormi_ms(RITARDO_MS);
    return ph_errore(e, "finto");
}

void ph_dati_calendario_libera(PhDatiEvento *ev, size_t n) { (void)ev; (void)n; }

/* ---- Cattura: screenshot e registrazione ---------------------------------------------------- */
int ph_cattura_screenshot(const struct AVFrame *f, const char *c, const char *app, char *perc, size_t n, PhErrore *e)
{
    (void)f; (void)c; (void)app; (void)perc; (void)n;
    return ph_errore(e, "finto");
}
int ph_cattura_png_memoria(const struct AVFrame *f, uint8_t **d, size_t *n, PhErrore *e)
{
    (void)f; (void)d; (void)n;
    return ph_errore(e, "finto");
}

struct PhRegistrazione {
    int iniziata;
};

PhRegistrazione *ph_registrazione_inizia(const char *c, const char *app, bool audio, char *perc, size_t n, PhErrore *e)
{
    (void)c; (void)app; (void)audio; (void)e;
    snprintf(perc, n, "/finto/registrazione.mp4");
    atomic_fetch_add(&finti.reg_inizia, 1);
    return calloc(1, sizeof(PhRegistrazione));
}
int ph_registrazione_aggiungi_video(PhRegistrazione *r, bool c, bool k, uint16_t w, uint16_t h, uint64_t t,
                                    const uint8_t *d, size_t n, PhErrore *e)
{
    (void)r; (void)c; (void)k; (void)w; (void)h; (void)t; (void)d; (void)n; (void)e;
    atomic_fetch_add(&finti.reg_pacchetti, 1);
    return 0;
}
int ph_registrazione_aggiungi_audio(PhRegistrazione *r, uint64_t t, const uint8_t *d, size_t n, PhErrore *e)
{
    (void)r; (void)t; (void)d; (void)n; (void)e;
    return -1;
}
void ph_registrazione_stato(PhRegistrazione *r, PhRegistrazioneStato *s)
{
    memset(s, 0, sizeof *s);
    s->iniziata = r != NULL;
}
int ph_registrazione_finisci(PhRegistrazione *r, PhErrore *e)
{
    (void)r; (void)e;
    atomic_fetch_add(&finti.reg_finisci, 1);
    return 0;
}
void ph_registrazione_libera(PhRegistrazione *r)
{
    if (r != NULL)
        atomic_fetch_add(&finti.reg_libera, 1);
    free(r);
}

/* ---- Widget del 23 nov pomeriggio ------------------------------------------------------------ */
int ph_dati_calendario_intervallo(PhDati *d, uint64_t da, uint64_t a, uint16_t massimo,
                                  PhDatiCalendarioIntervallo *out, PhErrore *e)
{
    tocca(d);
    (void)massimo;
    dormi_ms(RITARDO_MS);
    atomic_fetch_add(&finti.cal_chiamate, 1);
    atomic_store(&finti.cal_giorni_ultimo, (int)((a - da) / 86400000u));
    memset(out, 0, sizeof *out);
    if (atomic_load(&finti.cal_modo) != 0)
        return ph_errore(e, "calendario non leggibile");
    out->eventi = calloc(2, sizeof *out->eventi);
    out->eventi[0] = (PhDatiEvento){strdup("Evento finto"), da, da + 3600000u, false};
    out->eventi[1] = (PhDatiEvento){strdup("Giorno intero"), da, da + 86400000u, true};
    out->numero = 2;
    out->troncato = true;
    out->app_calendario = strdup("it.finto.calendario");
    return 0;
}

void ph_dati_calendario_intervallo_libera(PhDatiCalendarioIntervallo *c)
{
    for (size_t i = 0; i < c->numero; i++)
        free(c->eventi[i].titolo);
    free(c->eventi);
    free(c->app_calendario);
    memset(c, 0, sizeof *c);
}

int ph_dati_sveglia(PhDati *d, PhDatiSveglia *out, PhErrore *e)
{
    tocca(d);
    dormi_ms(RITARDO_MS);
    memset(out, 0, sizeof *out);
    int m = atomic_load(&finti.sveglia_modo);
    if (m == 2)
        return ph_errore(e, "sveglia non leggibile");
    out->presente = m == 0;
    out->ora_ms = m == 0 ? 1700000000000ull : 0;
    out->pacchetto = strdup(m == 0 ? "it.finto.orologio" : "");
    return 0;
}
void ph_dati_sveglia_libera(PhDatiSveglia *s) { free(s->pacchetto); s->pacchetto = NULL; }

int ph_dati_batteria(PhDati *d, PhDatiBatteria *out, PhErrore *e)
{
    tocca(d);
    dormi_ms(RITARDO_MS);
    if (atomic_load(&finti.bat_modo) != 0)
        return ph_errore(e, "batteria non leggibile");
    *out = (PhDatiBatteria){.livello = 64, .stato = PH_DATI_BAT_IN_CARICA, .alimentazione = 2,
                            .temperatura_nota = true, .temperatura_decimi = 312, .salute = 1,
                            .tensione_mv = 4100, .minuti_carica = 45, .minuti_autonomia = -1};
    return 0;
}

static GMutex gate_m;
static GCond gate_c;

void finti_apri_cancello(void)
{
    g_mutex_lock(&gate_m);
    atomic_store(&finti.ctl_gate, false);
    g_cond_broadcast(&gate_c);
    g_mutex_unlock(&gate_m);
}

int ph_dati_controlli(PhDati *d, PhDatiControlli *out, PhErrore *e)
{
    tocca(d);
    (void)e;
    atomic_fetch_add(&finti.ctl_letture, 1);
    /* Torcia non disponibile su questo telefono finto. Lo stato si "legge" PRIMA
     * del cancello: aperto piu' tardi, la risposta e' gia' vecchia. */
    *out = (PhDatiControlli){.suoneria = 0, .non_disturbare = 1, .torcia = PH_DATI_NON_DISPONIBILE,
                             .trova_in_corso = false, .puo_suoneria = true, .puo_non_disturbare = true,
                             .puo_torcia = false, .puo_trova = true};
    g_mutex_lock(&gate_m);
    if (atomic_load(&finti.ctl_gate)) {
        atomic_store(&finti.ctl_in_lettura, true);
        while (atomic_load(&finti.ctl_gate))
            g_cond_wait(&gate_c, &gate_m);
        atomic_store(&finti.ctl_in_lettura, false);
    }
    g_mutex_unlock(&gate_m);
    return 0;
}

int ph_dati_ascolta_controlli(PhDati *d, PhDatiAvvisoControlli avviso, void *utente, PhErrore *e)
{
    tocca(d);
    (void)e;
    finti.ctl_cb = avviso;
    finti.ctl_utente = utente;
    atomic_fetch_add(avviso != NULL ? &finti.ctl_ascolti : &finti.ctl_ascolti_tolti, 1);
    return 0;
}

int ph_dati_suoneria_imposta(PhDati *d, int modo, PhErrore *e)
{
    tocca(d);
    atomic_fetch_add(&finti.suoneria_n, 1);
    atomic_store(&finti.suoneria_valore, modo);
    return atomic_load(&finti.ctl_rifiuta) ? ph_errore(e, "non permesso dal telefono") : 0;
}
int ph_dati_non_disturbare_imposta(PhDati *d, bool acceso, PhErrore *e)
{
    tocca(d);
    (void)acceso;
    atomic_fetch_add(&finti.nd_n, 1);
    return atomic_load(&finti.ctl_rifiuta) ? ph_errore(e, "non permesso dal telefono") : 0;
}
int ph_dati_torcia_imposta(PhDati *d, bool accesa, PhErrore *e)
{
    tocca(d);
    (void)accesa;
    atomic_fetch_add(&finti.torcia_n, 1);
    return atomic_load(&finti.ctl_rifiuta) ? ph_errore(e, "non permesso dal telefono") : 0;
}
int ph_dati_trova_avvia(PhDati *d, uint16_t secondi, PhErrore *e)
{
    tocca(d);
    atomic_fetch_add(&finti.trova_avvia_n, 1);
    atomic_store(&finti.trova_secondi, secondi);
    return atomic_load(&finti.ctl_rifiuta) ? ph_errore(e, "non permesso dal telefono") : 0;
}
int ph_dati_trova_ferma(PhDati *d, PhErrore *e)
{
    tocca(d);
    (void)e;
    atomic_fetch_add(&finti.trova_ferma_n, 1);
    return 0;
}

/* ---- Posizione del telefono finto ----------------------------------------------------- */
static GMutex pos_m;
static GCond pos_c;
static char pos_localita[128];

void finti_posizione(PhDatiPosEsito esito, int lat_millesimi, int lon_millesimi, const char *localita, bool ricerca_avviata)
{
    g_mutex_lock(&pos_m);
    atomic_store(&finti.pos_esito, (int)esito);
    atomic_store(&finti.pos_lat, lat_millesimi);
    atomic_store(&finti.pos_lon, lon_millesimi);
    atomic_store(&finti.pos_ricerca, ricerca_avviata);
    g_strlcpy(pos_localita, localita != NULL ? localita : "", sizeof pos_localita);
    g_mutex_unlock(&pos_m);
}

void finti_apri_cancello_posizione(void)
{
    g_mutex_lock(&pos_m);
    atomic_store(&finti.pos_gate, false);
    g_cond_broadcast(&pos_c);
    g_mutex_unlock(&pos_m);
}

int ph_dati_posizione(PhDati *d, bool richiesta_permessa, PhDatiPosizione *out, PhErrore *e)
{
    tocca(d);
    atomic_fetch_add(&finti.pos_n, 1);
    atomic_store(&finti.pos_ultima_permessa, richiesta_permessa);
    if (richiesta_permessa)
        atomic_fetch_add(&finti.pos_permessa_n, 1);
    g_mutex_lock(&pos_m);
    if (atomic_load(&finti.pos_gate)) {
        atomic_store(&finti.pos_in_corso, true);
        while (atomic_load(&finti.pos_gate))
            g_cond_wait(&pos_c, &pos_m);
        atomic_store(&finti.pos_in_corso, false);
    }
    memset(out, 0, sizeof *out);
    out->esito = PH_DATI_POS_SCONOSCIUTA;
    bool rifiuta = atomic_load(&finti.pos_rifiuta);
    if (!rifiuta) {
        out->esito = (PhDatiPosEsito)atomic_load(&finti.pos_esito);
        if (out->esito == PH_DATI_POS_NOTA) {
            out->lat_millesimi = atomic_load(&finti.pos_lat);
            out->lon_millesimi = atomic_load(&finti.pos_lon);
            g_strlcpy(out->localita, pos_localita, sizeof out->localita);
        }
        out->ricerca_avviata = atomic_load(&finti.pos_ricerca);
    }
    g_mutex_unlock(&pos_m);
    return rifiuta ? ph_errore(e, "posizione non letta") : 0;
}

/* ---- Pezzo meteo finto -------------------------------------------------------------------- */
struct PhMeteo {
    int x;
};
static GMutex met_m;     /* callback registrate */
static GMutex met_corso; /* tenuto durante una callback: annulla/chiudi la aspettano, come il vero */
static PhMeteo *met_vivo;
static PhMeteoFatto met_cb;
static void *met_u;
static PhMeteoCercaFatto met_ccb;
static void *met_cu;
static PhMeteoFatto met_cb_v; /* destinatari sostituiti (solo per provare le callback vecchie) */
static void *met_u_v;
static PhMeteoCercaFatto met_ccb_v;
static void *met_cu_v;
static GMutex chiedi_m;
static GCond chiedi_c;

void finti_apri_cancello_chiedi(void)
{
    g_mutex_lock(&chiedi_m);
    atomic_store(&finti.m_chiedi_gate, false);
    g_cond_broadcast(&chiedi_c);
    g_mutex_unlock(&chiedi_m);
}

PhMeteoPrevisione finti_previsione(double gradi)
{
    PhMeteoPrevisione p;
    memset(&p, 0, sizeof p);
    p.adesso.temperatura = gradi;
    p.adesso.codice = 1;
    p.n_giorni = 1;
    p.ricevuta = 1000;
    return p;
}

PhMeteo *ph_meteo_nuovo(PhErrore *e)
{
    if (atomic_load(&finti.m_nuovo_rifiuta))
        return ph_errore(e, "meteo non creato") ? NULL : NULL;
    PhMeteo *m = calloc(1, sizeof *m);
    g_mutex_lock(&met_m);
    met_vivo = m;
    g_mutex_unlock(&met_m);
    atomic_fetch_add(&finti.m_creati, 1);
    return m;
}

static bool met_valido(PhMeteo *m)
{
    bool v;
    g_mutex_lock(&met_m);
    v = m != NULL && m == met_vivo;
    g_mutex_unlock(&met_m);
    if (!v)
        atomic_fetch_add(&finti.m_chiedi_dopo_chiudi, 1);
    return v;
}

int ph_meteo_chiedi(PhMeteo *m, int32_t lat_millesimi, int32_t lon_millesimi, PhMeteoFatto fatto, void *utente, PhErrore *e)
{
    if (!met_valido(m))
        return ph_errore(e, "chiuso");
    atomic_fetch_add(&finti.m_chiedi_n, 1);
    atomic_store(&finti.m_lat, lat_millesimi);
    atomic_store(&finti.m_lon, lon_millesimi);
    if (atomic_load(&finti.m_chiedi_rifiuta))
        return ph_errore(e, "rifiutato");
    g_mutex_lock(&chiedi_m);
    if (atomic_load(&finti.m_chiedi_gate)) {
        atomic_store(&finti.m_chiedi_in_corso, true);
        while (atomic_load(&finti.m_chiedi_gate))
            g_cond_wait(&chiedi_c, &chiedi_m);
        atomic_store(&finti.m_chiedi_in_corso, false);
    }
    g_mutex_unlock(&chiedi_m);
    g_mutex_lock(&met_m);
    if (met_cb != NULL) { /* sostituisce il destinatario: il vecchio non riceve nulla */
        met_cb_v = met_cb;
        met_u_v = met_u;
    }
    met_cb = fatto;
    met_u = utente;
    g_mutex_unlock(&met_m);
    return 0;
}

void ph_meteo_annulla(PhMeteo *m)
{
    if (!met_valido(m))
        return;
    atomic_fetch_add(&finti.m_annulla_n, 1);
    g_mutex_lock(&met_corso); /* aspetta una callback in corso */
    g_mutex_lock(&met_m);
    met_cb = NULL;
    met_ccb = NULL;
    met_cb_v = NULL;
    met_ccb_v = NULL;
    g_mutex_unlock(&met_m);
    g_mutex_unlock(&met_corso);
}

void ph_meteo_chiudi(PhMeteo *m)
{
    if (!met_valido(m))
        return;
    g_mutex_lock(&met_corso);
    g_mutex_lock(&met_m);
    met_cb = NULL;
    met_ccb = NULL;
    met_cb_v = NULL;
    met_ccb_v = NULL;
    met_vivo = NULL;
    g_mutex_unlock(&met_m);
    g_mutex_unlock(&met_corso);
    atomic_fetch_add(&finti.m_chiusi, 1);
    free(m);
}

int ph_meteo_cerca(PhMeteo *m, const char *testo, const char *lingua, PhMeteoCercaFatto fatto, void *utente,
                   PhErrore *e)
{
    (void)lingua;
    if (!met_valido(m))
        return ph_errore(e, "chiuso");
    atomic_fetch_add(&finti.m_cerca_n, 1);
    g_strlcpy(finti.m_cerca_testo, testo, sizeof finti.m_cerca_testo);
    if (atomic_load(&finti.m_cerca_rifiuta))
        return ph_errore(e, "rifiutato");
    g_mutex_lock(&met_m);
    if (met_ccb != NULL) {
        met_ccb_v = met_ccb;
        met_cu_v = met_cu;
    }
    met_ccb = fatto; /* sostituisce quella in corso: la vecchia non risponde */
    met_cu = utente;
    g_mutex_unlock(&met_m);
    return 0;
}

void ph_meteo_cerca_annulla(PhMeteo *m)
{
    if (!met_valido(m))
        return;
    atomic_fetch_add(&finti.m_cerca_annulla_n, 1);
    g_mutex_lock(&met_corso);
    g_mutex_lock(&met_m);
    met_ccb = NULL;
    met_ccb_v = NULL;
    g_mutex_unlock(&met_m);
    g_mutex_unlock(&met_corso);
}

typedef struct {
    bool vecchia;
    bool cerca;
    const PhMeteoPrevisione *p;
    const PhMeteoLuogo *v;
    size_t n;
    const char *frase;
    bool consegnata;
} Rispondi;

static gpointer rispondi_corpo(gpointer dati)
{
    Rispondi *r = dati;
    g_mutex_lock(&met_corso);
    g_mutex_lock(&met_m);
    PhMeteoFatto cb = r->cerca ? NULL : (r->vecchia ? met_cb_v : met_cb);
    PhMeteoCercaFatto ccb = r->cerca ? (r->vecchia ? met_ccb_v : met_ccb) : NULL;
    void *u = r->cerca ? (r->vecchia ? met_cu_v : met_cu) : (r->vecchia ? met_u_v : met_u);
    if (cb != NULL) { /* una risposta sola per richiesta */
        if (r->vecchia)
            met_cb_v = NULL;
        else
            met_cb = NULL;
    }
    if (ccb != NULL) {
        if (r->vecchia)
            met_ccb_v = NULL;
        else
            met_ccb = NULL;
    }
    g_mutex_unlock(&met_m);
    if (cb != NULL) {
        r->consegnata = true;
        cb(r->p, r->frase, u);
    } else if (ccb != NULL) {
        r->consegnata = true;
        ccb(r->v, r->n, r->frase, u);
    }
    g_mutex_unlock(&met_corso);
    return NULL;
}

static bool rispondi(Rispondi *r)
{
    GThread *t = g_thread_new("meteo-finto", rispondi_corpo, r);
    g_thread_join(t);
    return r->consegnata;
}

bool finti_meteo_rispondi(const PhMeteoPrevisione *p, const char *frase)
{
    Rispondi r = {.p = p, .frase = frase};
    return rispondi(&r);
}

bool finti_meteo_cerca_rispondi(const PhMeteoLuogo *v, size_t n, const char *frase)
{
    Rispondi r = {.cerca = true, .v = v, .n = n, .frase = frase};
    return rispondi(&r);
}

bool finti_meteo_rispondi_vecchia(const PhMeteoPrevisione *p, const char *frase)
{
    Rispondi r = {.vecchia = true, .p = p, .frase = frase};
    return rispondi(&r);
}

bool finti_meteo_cerca_rispondi_vecchia(const PhMeteoLuogo *v, size_t n, const char *frase)
{
    Rispondi r = {.vecchia = true, .cerca = true, .v = v, .n = n, .frase = frase};
    return rispondi(&r);
}

/* ---- Evento del pannello dal telefono finto ----------------------------------------------- */
typedef struct {
    PhEventoGrezzo f;
    void *u;
    uint8_t corpo[2];
} PanEv;

static gpointer pan_ev_corpo(gpointer p)
{
    PanEv *e = p;
    e->f(0x6e, e->corpo, 2, e->u);
    return NULL;
}

bool finti_pannello_evento(bool acceso, int motivo)
{
    PanEv e = {.f = finti.pan_cb, .u = finti.pan_utente, .corpo = {acceso ? 1 : 0, (uint8_t)motivo}};
    if (e.f == NULL)
        return false;
    GThread *t = g_thread_new("pannello-finto", pan_ev_corpo, &e);
    g_thread_join(t);
    return true;
}
