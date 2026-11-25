/* Il mondo finto per le prove del collegamento (vedi finto.h). Tutto con un
 * solo mutex: le funzioni finte sono chiamate dal thread del collegamento, dal
 * thread della prova e (per l'avviso di blocco) "dal componente". */
#include "finto.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "abbinamento/abbinamento.h"
#include "adb/adb.h"
#include "comune/buffer.h"
#include "componente/componente.h"
#include "config/config.h"
#include "dati/dati.h"
#include "scoperta/scoperta.h"

typedef struct {
    bool usato;
    char host[64];
    int porta;
    char seriale[64], modello[64], android_id[64];
    bool acceso, abbinato, bloccato, comp_vivo, comp_fallisce;
    int chiave_ok; /* id della chiave a cui il telefono si e' abbinato (0 = nessuna) */
    char nome_mdns[128];
    int gen;   /* sale quando cade: le vecchie connessioni restano morte */
    int cgen;  /* idem per il componente */
} Tel;

struct PhChiave {
    int id;
};
struct PhAdb {
    int tel, gen;
    char banner[160];
};
struct PhComponente {
    int tel, gen, cgen;
    PhEvento evento;
    void *utente;
    struct PhComponente *prossimo;
};
struct PhDati {
    int tel;
    PhDatiAvvisi av;
    struct PhDati *prossimo;
};

static pthread_mutex_t M = PTHREAD_MUTEX_INITIALIZER;
static Tel tel[FINTO_MAX_TEL];
static int scop_lista[FINTO_MAX_TEL], scop_n;
static bool scop_nomi = true;
static int scop_ritardo, scop_chiamate;
static int coll_ritardo, avvia_ritardo, coll_chiamate, avvia_chiamate;
static long coll_istanti[256];
static int abb_tel = -1, abb_chiamate, shell_falliscono, coll_fallite;
static bool abb_visibile = true, dati_rifiuta;
static int abb_ritardo;
static int chiave_salvata, chiavi_create, chiavi_prossimo_id;
static int vivi_adb, vivi_comp, vivi_dati, vivi_chiavi, creati_adb;
static char ordine[512];
static const void *ultimo_adb;
static PhDati *dati_lista;
static PhComponente *comp_lista;
static PhCfgTelefoni noti;
static int salvataggi;
static bool rifiuta_salv;

static void dormi_ms(int ms) {
    if (ms <= 0) return;
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

static long adesso_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void copia(char *d, size_t n, const char *s) {
    size_t l = s ? strlen(s) : 0;
    if (l >= n) l = n - 1;
    if (l) memcpy(d, s, l);
    d[l] = '\0';
}

void finto_reset(void) {
    pthread_mutex_lock(&M);
    memset(tel, 0, sizeof tel);
    scop_n = 0;
    scop_nomi = true;
    scop_ritardo = scop_chiamate = 0;
    coll_ritardo = avvia_ritardo = coll_chiamate = avvia_chiamate = 0;
    abb_tel = -1;
    abb_chiamate = 0;
    shell_falliscono = coll_fallite = 0;
    abb_visibile = true;
    dati_rifiuta = false;
    abb_ritardo = 0;
    chiave_salvata = chiavi_create = chiavi_prossimo_id = 0;
    ordine[0] = '\0';
    ultimo_adb = NULL;
    creati_adb = 0;
    ph_cfg_telefoni_predefiniti(&noti);
    salvataggi = 0;
    rifiuta_salv = false;
    pthread_mutex_unlock(&M);
}

void finto_tel(int i, const char *host, int porta, const char *seriale, const char *modello) {
    pthread_mutex_lock(&M);
    Tel *t = &tel[i];
    memset(t, 0, sizeof *t);
    t->usato = true;
    copia(t->host, sizeof t->host, host);
    t->porta = porta;
    copia(t->seriale, sizeof t->seriale, seriale);
    copia(t->modello, sizeof t->modello, modello);
    t->acceso = t->abbinato = t->comp_vivo = true;
    pthread_mutex_unlock(&M);
}
void finto_tel_acceso(int i, bool si) {
    pthread_mutex_lock(&M);
    if (tel[i].acceso && !si) tel[i].gen++;
    tel[i].acceso = si;
    pthread_mutex_unlock(&M);
}
void finto_tel_revoca(int i) {
    pthread_mutex_lock(&M);
    tel[i].abbinato = false;
    tel[i].chiave_ok = 0;
    pthread_mutex_unlock(&M);
}
int finto_chiavi_create(void) {
    pthread_mutex_lock(&M);
    int r = chiavi_create;
    pthread_mutex_unlock(&M);
    return r;
}
int finto_noti_conta(void) {
    pthread_mutex_lock(&M);
    int r = (int)noti.n;
    pthread_mutex_unlock(&M);
    return r;
}
void finto_tel_abbinato(int i, bool si) {
    pthread_mutex_lock(&M);
    tel[i].abbinato = si;
    if (!si) tel[i].chiave_ok = 0;
    pthread_mutex_unlock(&M);
}
void finto_tel_bloccato(int i, bool si) {
    pthread_mutex_lock(&M);
    tel[i].bloccato = si;
    for (PhDati *d = dati_lista; d; d = d->prossimo) {
        if (d->tel == i && d->av.stato) {
            PhDatiStato st;
            memset(&st, 0, sizeof st);
            st.bloccato = si;
            d->av.stato(&st, d->av.utente);
        }
    }
    pthread_mutex_unlock(&M);
}
void finto_tel_blocca_e_cade(int i) {
    pthread_mutex_lock(&M);
    tel[i].bloccato = true;
    for (PhDati *d = dati_lista; d; d = d->prossimo) {
        if (d->tel == i && d->av.stato) {
            PhDatiStato st;
            memset(&st, 0, sizeof st);
            st.bloccato = true;
            d->av.stato(&st, d->av.utente);
        }
    }
    if (tel[i].acceso) tel[i].gen++;
    tel[i].acceso = false;
    pthread_mutex_unlock(&M);
}
void finto_tel_componente_vivo(int i, bool si) {
    pthread_mutex_lock(&M);
    if (tel[i].comp_vivo && !si) tel[i].cgen++;
    tel[i].comp_vivo = si;
    pthread_mutex_unlock(&M);
}
void finto_tel_componente_fallisce(int i, bool si) {
    pthread_mutex_lock(&M);
    tel[i].comp_fallisce = si;
    pthread_mutex_unlock(&M);
}
void finto_tel_android_id(int i, const char *id) {
    pthread_mutex_lock(&M);
    copia(tel[i].android_id, sizeof tel[i].android_id, id);
    pthread_mutex_unlock(&M);
}
void finto_tel_seriale(int i, const char *seriale) {
    pthread_mutex_lock(&M);
    copia(tel[i].seriale, sizeof tel[i].seriale, seriale);
    pthread_mutex_unlock(&M);
}

void finto_tel_nome_mdns(int i, const char *nome) {
    pthread_mutex_lock(&M);
    copia(tel[i].nome_mdns, sizeof tel[i].nome_mdns, nome);
    pthread_mutex_unlock(&M);
}
/* La chiamata avviene senza il lucchetto del finto (un avviso lento non deve
 * fermare le altre funzioni finte); si copiano funzione e utente. */
void finto_tel_evento_video(int i) {
    PhEvento f[4];
    void *u[4];
    int n = 0;
    pthread_mutex_lock(&M);
    for (PhComponente *c = comp_lista; c && n < 4; c = c->prossimo)
        if (c->tel == i && c->evento) {
            f[n] = c->evento;
            u[n++] = c->utente;
        }
    pthread_mutex_unlock(&M);
    for (int k = 0; k < n; k++) f[k](1, 2, 3, 4, u[k]);
}
void finto_tel_notifiche(int i) {
    pthread_mutex_lock(&M);
    for (PhDati *d = dati_lista; d; d = d->prossimo)
        if (d->tel == i && d->av.notifiche) d->av.notifiche(d->av.utente);
    pthread_mutex_unlock(&M);
}
void finto_shell_fallimenti(int n) {
    pthread_mutex_lock(&M);
    shell_falliscono = n;
    pthread_mutex_unlock(&M);
}
void finto_dati_rifiuta(bool si) {
    pthread_mutex_lock(&M);
    dati_rifiuta = si;
    pthread_mutex_unlock(&M);
}
void finto_abbinamento_ritardo_ms(int ms) {
    pthread_mutex_lock(&M);
    abb_ritardo = ms;
    pthread_mutex_unlock(&M);
}
void finto_abbinamento_visibile(bool si) {
    pthread_mutex_lock(&M);
    abb_visibile = si;
    pthread_mutex_unlock(&M);
}
int finto_collega_fallite(void) {
    pthread_mutex_lock(&M);
    int r = coll_fallite;
    pthread_mutex_unlock(&M);
    return r;
}

void finto_scoperta_telefoni(const int *indici, int n) {
    pthread_mutex_lock(&M);
    scop_n = n > FINTO_MAX_TEL ? FINTO_MAX_TEL : n;
    for (int i = 0; i < scop_n; i++) scop_lista[i] = indici[i];
    pthread_mutex_unlock(&M);
}
void finto_scoperta_nomi(bool con_seriale) {
    pthread_mutex_lock(&M);
    scop_nomi = con_seriale;
    pthread_mutex_unlock(&M);
}
void finto_scoperta_ritardo_ms(int ms) {
    pthread_mutex_lock(&M);
    scop_ritardo = ms;
    pthread_mutex_unlock(&M);
}
int finto_scoperta_chiamate(void) {
    pthread_mutex_lock(&M);
    int r = scop_chiamate;
    pthread_mutex_unlock(&M);
    return r;
}
void finto_collega_ritardo_ms(int ms) {
    pthread_mutex_lock(&M);
    coll_ritardo = ms;
    pthread_mutex_unlock(&M);
}
void finto_avvia_ritardo_ms(int ms) {
    pthread_mutex_lock(&M);
    avvia_ritardo = ms;
    pthread_mutex_unlock(&M);
}
int finto_collega_chiamate(void) {
    pthread_mutex_lock(&M);
    int r = coll_chiamate;
    pthread_mutex_unlock(&M);
    return r;
}
int finto_avvia_chiamate(void) {
    pthread_mutex_lock(&M);
    int r = avvia_chiamate;
    pthread_mutex_unlock(&M);
    return r;
}
int finto_collega_istanti(long *ms, int max) {
    pthread_mutex_lock(&M);
    int n = coll_chiamate < 256 ? coll_chiamate : 256;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) ms[i] = coll_istanti[i];
    pthread_mutex_unlock(&M);
    return n;
}
void finto_abbinamento_telefono(int i) {
    pthread_mutex_lock(&M);
    abb_tel = i;
    pthread_mutex_unlock(&M);
}
int finto_abbinamento_chiamate(void) {
    pthread_mutex_lock(&M);
    int r = abb_chiamate;
    pthread_mutex_unlock(&M);
    return r;
}

#define LEGGI(nome, var)            \
    int nome(void) {                \
        pthread_mutex_lock(&M);     \
        int r = var;                \
        pthread_mutex_unlock(&M);   \
        return r;                   \
    }
LEGGI(finto_vivi_adb, vivi_adb)
LEGGI(finto_vivi_componenti, vivi_comp)
LEGGI(finto_vivi_dati, vivi_dati)
LEGGI(finto_vivi_chiavi, vivi_chiavi)
LEGGI(finto_creati_adb, creati_adb)

void finto_ordine_rilascio(char *out, size_t cap) {
    pthread_mutex_lock(&M);
    copia(out, cap, ordine);
    pthread_mutex_unlock(&M);
}
const void *finto_ultimo_adb(void) {
    pthread_mutex_lock(&M);
    const void *r = ultimo_adb;
    pthread_mutex_unlock(&M);
    return r;
}

void finto_noti_aggiungi(const char *seriale, const char *nome, const char *indirizzo, long visto) {
    pthread_mutex_lock(&M);
    PhCfgTelefonoNoto *v = &noti.voci[noti.n++];
    memset(v, 0, sizeof *v);
    copia(v->seriale, sizeof v->seriale, seriale);
    copia(v->nome, sizeof v->nome, nome);
    copia(v->indirizzo, sizeof v->indirizzo, indirizzo);
    v->visto = visto;
    pthread_mutex_unlock(&M);
}
bool finto_noti_cerca(const char *seriale, FintoNoto *fuori) {
    bool r = false;
    pthread_mutex_lock(&M);
    for (size_t i = 0; i < noti.n; i++) {
        if (strcmp(noti.voci[i].seriale, seriale) == 0) {
            copia(fuori->seriale, sizeof fuori->seriale, noti.voci[i].seriale);
            copia(fuori->nome, sizeof fuori->nome, noti.voci[i].nome);
            copia(fuori->indirizzo, sizeof fuori->indirizzo, noti.voci[i].indirizzo);
            fuori->visto = (long)noti.voci[i].visto;
            r = true;
        }
    }
    pthread_mutex_unlock(&M);
    return r;
}
void finto_noti_imposta_visto(const char *seriale, long visto) {
    pthread_mutex_lock(&M);
    for (size_t i = 0; i < noti.n; i++)
        if (strcmp(noti.voci[i].seriale, seriale) == 0) noti.voci[i].visto = visto;
    pthread_mutex_unlock(&M);
}
int finto_noti_salvataggi(void) {
    pthread_mutex_lock(&M);
    int r = salvataggi;
    pthread_mutex_unlock(&M);
    return r;
}
void finto_noti_rifiuta_salvataggio(bool si) {
    pthread_mutex_lock(&M);
    rifiuta_salv = si;
    pthread_mutex_unlock(&M);
}

/* ---- config ---------------------------------------------------------------*/
int ph_cfg_percorso_chiave(char *out, size_t capacita, PhErrore *e) {
    (void)e;
    copia(out, capacita, "/finto/chiave.pem");
    return 0;
}
void ph_cfg_telefoni_predefiniti(PhCfgTelefoni *t) { memset(t, 0, sizeof *t); }
int ph_cfg_telefoni_carica(PhCfgTelefoni *t, PhErrore *e) {
    (void)e;
    pthread_mutex_lock(&M);
    *t = noti;
    pthread_mutex_unlock(&M);
    return 0;
}
int ph_cfg_telefoni_salva(const PhCfgTelefoni *t, PhErrore *e) {
    pthread_mutex_lock(&M);
    if (rifiuta_salv) {
        pthread_mutex_unlock(&M);
        return ph_errore(e, "disco pieno finto");
    }
    noti = *t;
    salvataggi++;
    pthread_mutex_unlock(&M);
    return 0;
}

/* ---- chiave ----------------------------------------------------------------*/
PhChiave *ph_chiave_carica_o_crea(const char *percorso, PhErrore *e) {
    (void)percorso;
    (void)e;
    PhChiave *k = calloc(1, sizeof *k);
    pthread_mutex_lock(&M);
    if (chiave_salvata == 0) { /* primo avvio: il file non c'e', si crea */
        chiave_salvata = ++chiavi_prossimo_id;
        chiavi_create++;
    }
    k->id = chiave_salvata;
    vivi_chiavi++;
    pthread_mutex_unlock(&M);
    return k;
}
void ph_chiave_libera(PhChiave *c) {
    pthread_mutex_lock(&M);
    vivi_chiavi--;
    pthread_mutex_unlock(&M);
    free(c);
}

/* ---- scoperta e abbinamento ---------------------------------------------------*/
int ph_scoperta_cerca(int durata_ms, PhTelefonoTrovato *trovati, size_t massimo, PhErrore *e) {
    (void)e;
    pthread_mutex_lock(&M);
    scop_chiamate++;
    int rit = scop_ritardo;
    pthread_mutex_unlock(&M);
    dormi_ms(rit < durata_ms ? rit : durata_ms);
    pthread_mutex_lock(&M);
    size_t n = 0;
    for (int k = 0; k < scop_n && n < massimo; k++) {
        const Tel *t = &tel[scop_lista[k]];
        if (!t->acceso) continue;
        PhTelefonoTrovato *p = &trovati[n++];
        memset(p, 0, sizeof *p);
        if (t->nome_mdns[0]) {
            copia(p->nome, sizeof p->nome, t->nome_mdns);
        } else if (scop_nomi && t->seriale[0]) {
            copia(p->nome, sizeof p->nome, "adb-");
            strncat(p->nome, t->seriale, sizeof p->nome - strlen(p->nome) - 1);
            strncat(p->nome, "-AbCdEf", sizeof p->nome - strlen(p->nome) - 1);
        } else {
            copia(p->nome, sizeof p->nome, "telefono");
        }
        copia(p->indirizzo, sizeof p->indirizzo, t->host);
        p->porta = (uint16_t)t->porta;
    }
    if (abb_visibile && abb_tel >= 0 && tel[abb_tel].acceso && n < massimo) {
        PhTelefonoTrovato *p = &trovati[n++];
        memset(p, 0, sizeof *p);
        copia(p->nome, sizeof p->nome, "adb-abbinamento-ZzZzZz");
        copia(p->indirizzo, sizeof p->indirizzo, tel[abb_tel].host);
        p->porta = (uint16_t)(40000 + abb_tel);
        p->abbinamento = true;
    }
    pthread_mutex_unlock(&M);
    return (int)n;
}

int ph_abbinamento_esegui(const char *host, uint16_t porta, const char *codice,
                          const PhChiave *chiave, int timeout_ms, PhErrore *e) {
    (void)timeout_ms;
    pthread_mutex_lock(&M);
    abb_chiamate++;
    int rit = abb_ritardo;
    pthread_mutex_unlock(&M);
    dormi_ms(rit);
    pthread_mutex_lock(&M);
    bool ok = abb_tel >= 0 && strcmp(codice, "123456") == 0 &&
              strcmp(host, tel[abb_tel].host) == 0 && porta == 40000 + abb_tel;
    if (ok) tel[abb_tel].chiave_ok = chiave->id;
    pthread_mutex_unlock(&M);
    /* gli errori finti riportano l'indirizzo, come puo' fare un pezzo vero */
    if (!ok) return ph_errore(e, "codice o indirizzo di abbinamento errati (%s:%u)", host, porta);
    return 0;
}

/* ---- adb --------------------------------------------------------------------*/
PhAdb *ph_adb_collega(const char *host, uint16_t porta, const PhChiave *chiave, int timeout_ms,
                      bool *non_abbinato, PhErrore *e) {
    (void)timeout_ms;
    if (non_abbinato) *non_abbinato = false;
    pthread_mutex_lock(&M);
    long ora = adesso_ms();
    if (coll_chiamate < 256) coll_istanti[coll_chiamate] = ora;
    coll_chiamate++;
    int rit = coll_ritardo;
    pthread_mutex_unlock(&M);
    dormi_ms(rit);
    pthread_mutex_lock(&M);
    int idx = -1;
    for (int i = 0; i < FINTO_MAX_TEL; i++)
        if (tel[i].usato && tel[i].acceso && tel[i].porta == porta && strcmp(tel[i].host, host) == 0)
            idx = i;
    if (idx < 0) {
        coll_fallite++;
        pthread_mutex_unlock(&M);
        ph_errore(e, "nessuno risponde a %s:%u", host, porta);
        return NULL;
    }
    if (!tel[idx].abbinato && tel[idx].chiave_ok != chiave->id) {
        if (non_abbinato) *non_abbinato = true;
        coll_fallite++;
        pthread_mutex_unlock(&M);
        ph_errore(e, "chiave non riconosciuta da %s", host);
        return NULL;
    }
    PhAdb *a = calloc(1, sizeof *a);
    a->tel = idx;
    a->gen = tel[idx].gen;
    copia(a->banner, sizeof a->banner, "device::ro.product.name=nome;ro.product.model=");
    strncat(a->banner, tel[idx].modello, sizeof a->banner - strlen(a->banner) - 1);
    strncat(a->banner, ";ro.product.device=dev;", sizeof a->banner - strlen(a->banner) - 1);
    vivi_adb++;
    creati_adb++;
    ultimo_adb = a;
    pthread_mutex_unlock(&M);
    return a;
}
void ph_adb_chiudi(PhAdb *a) {
    pthread_mutex_lock(&M);
    vivi_adb--;
    strncat(ordine, "A", sizeof ordine - strlen(ordine) - 1);
    pthread_mutex_unlock(&M);
    free(a);
}
bool ph_adb_viva(PhAdb *a) {
    pthread_mutex_lock(&M);
    bool r = tel[a->tel].acceso && tel[a->tel].gen == a->gen;
    pthread_mutex_unlock(&M);
    return r;
}
const char *ph_adb_presentazione(PhAdb *a) { return a->banner; }

int ph_shell_esegui(PhAdb *a, const char *comando, PhBuffer *uscita, PhBuffer *errori, int *codice,
                    int timeout_ms, PhErrore *e) {
    (void)errori;
    (void)timeout_ms;
    if (!ph_adb_viva(a)) return ph_errore(e, "connessione caduta");
    char r[80];
    pthread_mutex_lock(&M);
    if (shell_falliscono > 0) {
        shell_falliscono--;
        pthread_mutex_unlock(&M);
        return ph_errore(e, "tempo scaduto");
    }
    const Tel *t = &tel[a->tel];
    if (strcmp(comando, "getprop ro.serialno") == 0) {
        copia(r, sizeof r, t->seriale);
    } else if (strcmp(comando, "settings get secure android_id") == 0) {
        copia(r, sizeof r, t->android_id[0] ? t->android_id : "null");
    } else {
        copia(r, sizeof r, "");
    }
    pthread_mutex_unlock(&M);
    strncat(r, "\n", sizeof r - strlen(r) - 1);
    if (codice) *codice = 0;
    if (uscita && ph_buffer_aggiungi(uscita, r, strlen(r)) != 0) return ph_errore(e, "memoria");
    return 0;
}

/* ---- componente -------------------------------------------------------------*/
PhComponente *ph_componente_avvia(PhAdb *a, const char *jar_locale, PhEvento evento, void *utente,
                                  PhTelefonoInfo *info, int timeout_ms, PhErrore *e) {
    (void)jar_locale;
    (void)timeout_ms;
    pthread_mutex_lock(&M);
    avvia_chiamate++;
    int rit = avvia_ritardo;
    pthread_mutex_unlock(&M);
    dormi_ms(rit);
    pthread_mutex_lock(&M);
    Tel *t = &tel[a->tel];
    if (t->comp_fallisce || !t->comp_vivo || !t->acceso || t->gen != a->gen) {
        pthread_mutex_unlock(&M);
        ph_errore(e, "il servizio non parte su %s", t->host);
        return NULL;
    }
    PhComponente *c = calloc(1, sizeof *c);
    c->tel = a->tel;
    c->gen = a->gen;
    c->cgen = t->cgen;
    c->evento = evento;
    c->utente = utente;
    c->prossimo = comp_lista;
    comp_lista = c;
    vivi_comp++;
    memset(info, 0, sizeof *info);
    info->api = 34;
    info->larghezza = 1080;
    info->altezza = 2340;
    info->densita = 420;
    copia(info->modello, sizeof info->modello, t->modello);
    pthread_mutex_unlock(&M);
    return c;
}
void ph_componente_chiudi(PhComponente *c) {
    pthread_mutex_lock(&M);
    for (PhComponente **p = &comp_lista; *p; p = &(*p)->prossimo)
        if (*p == c) {
            *p = c->prossimo;
            break;
        }
    vivi_comp--;
    strncat(ordine, "C", sizeof ordine - strlen(ordine) - 1);
    pthread_mutex_unlock(&M);
    free(c);
}
bool ph_componente_vivo(PhComponente *c) {
    pthread_mutex_lock(&M);
    const Tel *t = &tel[c->tel];
    bool r = t->acceso && t->gen == c->gen && t->comp_vivo && t->cgen == c->cgen;
    pthread_mutex_unlock(&M);
    return r;
}

/* ---- dati ---------------------------------------------------------------------*/
PhDati *ph_dati_apri(PhComponente *c, const PhDatiAvvisi *avvisi, PhErrore *e) {
    (void)e;
    PhDati *d = calloc(1, sizeof *d);
    d->tel = c->tel;
    d->av = *avvisi;
    pthread_mutex_lock(&M);
    d->prossimo = dati_lista;
    dati_lista = d;
    vivi_dati++;
    pthread_mutex_unlock(&M);
    return d;
}
int ph_dati_chiudi(PhDati *d, PhErrore *e) {
    pthread_mutex_lock(&M);
    if (dati_rifiuta) {
        pthread_mutex_unlock(&M);
        return ph_errore(e, "ricerca ancora aperta");
    }
    for (PhDati **p = &dati_lista; *p; p = &(*p)->prossimo)
        if (*p == d) {
            *p = d->prossimo;
            break;
        }
    vivi_dati--;
    strncat(ordine, "D", sizeof ordine - strlen(ordine) - 1);
    pthread_mutex_unlock(&M);
    free(d);
    return 0;
}
int ph_dati_stato(PhDati *d, PhDatiStato *stato, PhErrore *e) {
    (void)e;
    memset(stato, 0, sizeof *stato);
    pthread_mutex_lock(&M);
    stato->bloccato = tel[d->tel].bloccato;
    stato->batteria = 80;
    pthread_mutex_unlock(&M);
    return 0;
}

