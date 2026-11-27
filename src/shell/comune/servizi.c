/* Il tramite tra i backend dei servizi e la shell (servizi.h,
 * servizi_backend.h). Tutto cio' che arriva da un backend passa da
 * ph_shell_consegna: la fotografia nuova si sostituisce sul thread GTK, poi
 * parte l'avviso. Cosi' un puntatore letto dal thread GTK vale fino al
 * ritorno al ciclo principale (servizi.h, FOTOGRAFIE). */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config/lingua.h"
#include "interno.h"
#include "shell/servizi_backend.h"
#include "shell/shell.h"

/* Tetti per i dati dal telefono (ostili). */
#define ICONA_BYTE_MAX (2u * 1024u * 1024u)
#define ICONA_LATO_MAX 1024
#define IMMAGINE_BYTE_MAX (16u * 1024u * 1024u)
#define IMMAGINE_LATO_MAX 4096
#define TESTO_INPUT_MAX 200000 /* come gli appunti (4.11) */
#define SALVA_RITARDO_MS 1000

struct PhServizi {
    atomic_int rif;     /* 1 del proprietario + 1 per ogni consegna in volo */
    atomic_int chiuso;  /* ph_servizi_libera chiamata: le consegne si scartano */
    PhServiziBackend vt;
    void *b;

    PhAscoltatori asc;

    PhSrvCollegamento col;
    PhSrvTelefono tel;
    PhSrvVolume vol;
    PhSrvApp *app;
    size_t n_app;
    PhSrvNotifica *notif;
    size_t n_notif;
    char *problema;
    PhSrvTrasferimento *tr;
    size_t n_tr;
    bool coda_ferma;

    GHashTable *icone;         /* pacchetto -> GdkTexture */
    GHashTable *icone_chieste; /* pacchetto: chiesta (anche fallita) */
    /* Icone del tema Kora (thread GTK; letta al primo uso, vedi ph_srv_icona). */
    bool kora_letta;
    char *kora_dir;            /* NULL = nessuna icona Kora */
    GHashTable *kora_tab;      /* pacchetto -> nome-kora (stringhe nostre), NULL se assente */
    GHashTable *kora_tex;      /* pacchetto -> GdkTexture (caricate al primo uso) */
    GHashTable *kora_falliti;  /* pacchetto: PNG non caricabile, non si riprova */

    PhCfgPreferenze pref;
    PhCfgTelefono *profilo;
    guint salva_profilo, salva_pref;

    /* Aggiunte del 23 nov. Prima lettura delle notifiche (thread GTK). */
    unsigned notif_gen;
    bool notif_pronte;
    bool notif_attesa_prima; /* euristica per i backend senza il flag esplicito */
    /* Installazioni in corso: id -> PhSrvLavoro* (riferimento nostro). */
    GHashTable *inst;
    guint prossima_inst;
    int abbinamenti; /* abbinamenti chiesti e non ancora conclusi (thread GTK) */
    /* Widget del 23 nov pomeriggio (thread GTK). */
    PhSrvControlli ctl;
    int osservatori_ctl;
    PhSrvTrasferimento recenti[PH_SRV_RECENTI_MAX]; /* stringhe nostre */
    size_t n_recenti;
    GHashTable *completati_visti; /* id (uint64 in una stringa) gia' messi tra i recenti */
    /* Meteo (thread GTK). */
    PhSrvMeteo meteo;
    int osservatori_meteo;
    PhSrvMeteoFonte meteo_fonte; /* (23 nov notte) */
    /* Chiamate, rete, media (27 nov, thread GTK). */
    PhSrvChiamate chiamate;
    PhSrvReteDettaglio rete;
    PhSrvMedia media;
    int osservatori_chi, osservatori_rete, osservatori_media;
};

static void recenti_svuota(PhServizi *s);

static PhServizi *rif_prendi(PhServizi *s)
{
    atomic_fetch_add(&s->rif, 1);
    return s;
}

static void rif_lascia(PhServizi *s)
{
    if (atomic_fetch_sub(&s->rif, 1) != 1)
        return;
    /* Ultimo riferimento: la shell ha gia' chiuso il backend. */
    ph_vivi_togli(s);
    ph_srv_app_libera(s->app, s->n_app);
    ph_srv_notifiche_libera(s->notif, s->n_notif);
    ph_srv_trasferimenti_libera(s->tr, s->n_tr);
    free(s->problema);
    g_hash_table_destroy(s->icone);
    g_hash_table_destroy(s->icone_chieste);
    g_free(s->kora_dir);
    if (s->kora_tab != NULL)
        g_hash_table_destroy(s->kora_tab);
    g_hash_table_destroy(s->kora_tex);
    g_hash_table_destroy(s->kora_falliti);
    g_hash_table_destroy(s->inst);
    recenti_svuota(s);
    g_hash_table_destroy(s->completati_visti);
    ph_cfg_telefono_libera(s->profilo);
    ph_ascoltatori_finisci(&s->asc);
    g_free(s);
}

static bool chiusi(PhServizi *s)
{
    return atomic_load(&s->chiuso) != 0;
}

static void emetti(PhServizi *s, PhSrvEvento ev, const char *arg)
{
    guint n = ph_ascoltatori_apri(&s->asc);
    for (guint i = 0; i < n && !chiusi(s); i++) {
        const PhAscoltatore *v = ph_ascoltatori_voce(&s->asc, i, (int)ev);
        if (v != NULL)
            ((PhSrvAvviso)v->fn)(s, ev, arg, v->utente);
    }
    ph_ascoltatori_chiudi(&s->asc);
}

/* ---- Liberazione degli array -------------------------------------------- */
void ph_srv_app_libera(PhSrvApp *v, size_t n)
{
    for (size_t i = 0; v != NULL && i < n; i++) {
        free(v[i].pacchetto);
        free(v[i].nome);
    }
    free(v);
}

void ph_srv_notifiche_libera(PhSrvNotifica *v, size_t n)
{
    for (size_t i = 0; v != NULL && i < n; i++) {
        free(v[i].chiave);
        free(v[i].pacchetto);
        free(v[i].titolo);
        free(v[i].testo);
    }
    free(v);
}

void ph_srv_trasferimenti_libera(PhSrvTrasferimento *v, size_t n)
{
    for (size_t i = 0; v != NULL && i < n; i++) {
        free(v[i].nome);
        free(v[i].origine);
        free(v[i].destinazione);
        free(v[i].errore);
    }
    free(v);
}

void ph_srv_voci_libera(PhSrvVoce *v, size_t n)
{
    for (size_t i = 0; v != NULL && i < n; i++) {
        free(v[i].nome);
        free(v[i].nome_schermo);
    }
    free(v);
}

/* ---- Immagini dal telefono: si guarda l'intestazione PRIMA di decodificare,
 * cosi' un file piccolo che dichiara 30000x30000 pixel non chiede gigabyte. */
static bool misura_png(const uint8_t *d, size_t n, uint32_t *w, uint32_t *h)
{
    static const uint8_t firma[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (n < 24 || memcmp(d, firma, 8) != 0 || memcmp(d + 12, "IHDR", 4) != 0)
        return false;
    *w = (uint32_t)d[16] << 24 | (uint32_t)d[17] << 16 | (uint32_t)d[18] << 8 | d[19];
    *h = (uint32_t)d[20] << 24 | (uint32_t)d[21] << 16 | (uint32_t)d[22] << 8 | d[23];
    return true;
}

static bool misura_jpeg(const uint8_t *d, size_t n, uint32_t *w, uint32_t *h)
{
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8)
        return false;
    size_t i = 2;
    while (i + 4 <= n) {
        if (d[i] != 0xFF)
            return false;
        uint8_t m = d[i + 1];
        if (m == 0xFF) { /* riempimento */
            i++;
            continue;
        }
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
            i += 2;
            continue;
        }
        size_t lun = (size_t)d[i + 2] << 8 | d[i + 3];
        if (lun < 2 || i + 2 + lun > n)
            return false;
        bool sof = (m >= 0xC0 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC;
        if (sof) {
            if (lun < 7)
                return false;
            *h = (uint32_t)d[i + 5] << 8 | d[i + 6];
            *w = (uint32_t)d[i + 7] << 8 | d[i + 8];
            return true;
        }
        if (m == 0xDA || m == 0xD9)
            return false;
        i += 2 + lun;
    }
    return false;
}

/* Decodifica controllata (thread qualunque: gdk_texture_new_from_bytes e'
 * sicura fuori dal thread GTK). NULL se non valida o fuori tetto. */
static GdkTexture *decodifica(const void *dati, size_t n, size_t byte_max, uint32_t lato_max)
{
    uint32_t w = 0, h = 0;
    if (dati == NULL || n == 0 || n > byte_max)
        return NULL;
    if (!misura_png(dati, n, &w, &h) && !misura_jpeg(dati, n, &w, &h))
        return NULL;
    if (w == 0 || h == 0 || w > lato_max || h > lato_max)
        return NULL;
    GBytes *b = g_bytes_new(dati, n);
    GError *err = NULL;
    GdkTexture *t = gdk_texture_new_from_bytes(b, &err);
    g_bytes_unref(b);
    if (t == NULL) {
        g_clear_error(&err);
        return NULL;
    }
    if ((uint32_t)gdk_texture_get_width(t) != w || (uint32_t)gdk_texture_get_height(t) != h) {
        g_object_unref(t);
        return NULL;
    }
    return t;
}

/* ---- Pubblicazioni --------------------------------------------------- */
typedef enum {
    PUB_COLLEGAMENTO,
    PUB_TELEFONO,
    PUB_VOLUME,
    PUB_APP,
    PUB_NOTIFICHE,
    PUB_TRASFERIMENTI,
    PUB_ICONA,
    PUB_APPUNTI,
    PUB_PROFILO,
    PUB_PREFERENZE,
    PUB_CONTROLLI,
    PUB_METEO,
    PUB_DECODIFICA,
    PUB_CHIAMATE,
    PUB_RETE,
    PUB_MEDIA,
} PubTipo;

typedef struct {
    PhServizi *s;
    PubTipo tipo;
    PhSrvCollegamento col;
    PhSrvTelefono tel;
    PhSrvVolume vol;
    PhCfgPreferenze pref;
    void *v;          /* array (app, notifiche, trasferimenti) o profilo */
    size_t n;
    char *testo;      /* problema, pacchetto, appunti */
    bool flag;        /* coda ferma */
    GdkTexture *t;
    int prima;        /* notifiche: 1 prima lettura, 0 no, -1 non detto (euristica) */
    PhSrvControlli ctl;
    PhSrvMeteo *meteo;
    bool forza;       /* meteo: consegnare anche senza osservatori (spegnimento) */
    PhSrvMeteoFonte fonte; /* meteo */
    void *dat;        /* chiamate, rete, media: copia ripulita (g_free) */
} Pub;

static void flush_profilo(PhServizi *s);
static void recenti_aggiorna(PhServizi *s);

static void pub_applica(void *p)
{
    Pub *u = p;
    PhServizi *s = u->s;
    if (chiusi(s))
        return;
    switch (u->tipo) {
    case PUB_COLLEGAMENTO: {
        bool era_collegato = s->col.stato == PH_SRV_COLLEGATO;
        bool era_su = s->col.stato == PH_SRV_COLLEGATO || s->col.stato == PH_SRV_BLOCCATO;
        bool ora_su = u->col.stato == PH_SRV_COLLEGATO || u->col.stato == PH_SRV_BLOCCATO;
        if (!era_su && ora_su)
            s->notif_attesa_prima = true; /* la prossima fotografia e' la base */
        if (!ora_su) {
            s->notif_pronte = false;
            s->notif_attesa_prima = false;
        }
        s->col = u->col;
        s->col.motivo[sizeof s->col.motivo - 1] = '\0';
        s->col.nome[sizeof s->col.nome - 1] = '\0';
        /* Al ritorno della connessione si ritentano le icone fallite. */
        if (!era_collegato && s->col.stato == PH_SRV_COLLEGATO)
            g_hash_table_remove_all(s->icone_chieste);
        emetti(s, PH_SRV_EV_COLLEGAMENTO, NULL);
        break;
    }
    case PUB_TELEFONO:
        s->tel = u->tel;
        emetti(s, PH_SRV_EV_TELEFONO, NULL);
        break;
    case PUB_VOLUME:
        s->vol = u->vol;
        emetti(s, PH_SRV_EV_VOLUME, NULL);
        break;
    case PUB_APP:
        ph_srv_app_libera(s->app, s->n_app);
        s->app = u->v;
        s->n_app = u->n;
        u->v = NULL;
        emetti(s, PH_SRV_EV_APP, NULL);
        break;
    case PUB_NOTIFICHE:
        if (u->prima == 1 || (u->prima < 0 && s->notif_attesa_prima)) {
            s->notif_gen++;
            s->notif_pronte = true;
            s->notif_attesa_prima = false;
        }
        ph_srv_notifiche_libera(s->notif, s->n_notif);
        free(s->problema);
        s->notif = u->v;
        s->n_notif = u->n;
        s->problema = u->testo;
        u->v = NULL;
        u->testo = NULL;
        emetti(s, PH_SRV_EV_NOTIFICHE, NULL);
        break;
    case PUB_TRASFERIMENTI:
        ph_srv_trasferimenti_libera(s->tr, s->n_tr);
        s->tr = u->v;
        s->n_tr = u->n;
        s->coda_ferma = u->flag;
        u->v = NULL;
        recenti_aggiorna(s);
        emetti(s, PH_SRV_EV_TRASFERIMENTI, NULL);
        break;
    case PUB_ICONA:
        g_hash_table_add(s->icone_chieste, g_strdup(u->testo));
        if (u->t != NULL) {
            g_hash_table_replace(s->icone, g_strdup(u->testo), u->t);
            u->t = NULL;
            emetti(s, PH_SRV_EV_ICONA, u->testo);
        }
        break;
    case PUB_APPUNTI:
        emetti(s, PH_SRV_EV_APPUNTI, u->testo);
        break;
    case PUB_PROFILO:
        flush_profilo(s);
        ph_cfg_telefono_libera(s->profilo);
        s->profilo = u->v;
        u->v = NULL;
        recenti_svuota(s); /* un altro telefono: i suoi trasferimenti sono un'altra storia */
        emetti(s, PH_SRV_EV_PROFILO, NULL);
        break;
    case PUB_PREFERENZE:
        s->pref = u->pref;
        emetti(s, PH_SRV_EV_PROFILO, NULL);
        break;
    case PUB_CONTROLLI:
        s->ctl = u->ctl;
        s->ctl.noto = true;
        emetti(s, PH_SRV_EV_CONTROLLI, NULL);
        break;
    case PUB_DECODIFICA:
        emetti(s, PH_SRV_EV_DECODIFICA, NULL);
        break;
    case PUB_CHIAMATE:
        if (s->osservatori_chi == 0)
            break; /* arrivata dopo lo spegnimento: si scarta */
        s->chiamate = *(PhSrvChiamate *)u->dat;
        emetti(s, PH_SRV_EV_CHIAMATE, NULL);
        break;
    case PUB_RETE:
        if (s->osservatori_rete == 0)
            break;
        s->rete = *(PhSrvReteDettaglio *)u->dat;
        emetti(s, PH_SRV_EV_RETE, NULL);
        break;
    case PUB_MEDIA:
        if (s->osservatori_media == 0)
            break;
        s->media = *(PhSrvMedia *)u->dat;
        emetti(s, PH_SRV_EV_MEDIA, NULL);
        break;
    case PUB_METEO:
        if (s->osservatori_meteo == 0 && !u->forza)
            break; /* arrivata dopo lo spegnimento: si scarta */
        s->meteo = *u->meteo;
        s->meteo_fonte = u->fonte;
        emetti(s, PH_SRV_EV_METEO, NULL);
        break;
    }
}

static void pub_libera(void *p)
{
    Pub *u = p;
    switch (u->tipo) {
    case PUB_APP:
        ph_srv_app_libera(u->v, u->n);
        break;
    case PUB_NOTIFICHE:
        ph_srv_notifiche_libera(u->v, u->n);
        break;
    case PUB_TRASFERIMENTI:
        ph_srv_trasferimenti_libera(u->v, u->n);
        break;
    case PUB_PROFILO:
        ph_cfg_telefono_libera(u->v);
        break;
    default:
        break;
    }
    free(u->testo);
    g_free(u->meteo);
    g_free(u->dat);
    if (u->t != NULL)
        g_object_unref(u->t);
    rif_lascia(u->s);
    g_free(u);
}

static Pub *pub_nuova(PhServizi *s, PubTipo tipo)
{
    Pub *u = g_new0(Pub, 1);
    u->s = rif_prendi(s);
    u->tipo = tipo;
    return u;
}

static void pub_manda(Pub *u)
{
    ph_shell_consegna(pub_applica, u, pub_libera);
}

void ph_servizi_pubblica_collegamento(PhServizi *s, const PhSrvCollegamento *c)
{
    Pub *u = pub_nuova(s, PUB_COLLEGAMENTO);
    u->col = *c;
    pub_manda(u);
}

void ph_servizi_pubblica_telefono(PhServizi *s, const PhSrvTelefono *t)
{
    Pub *u = pub_nuova(s, PUB_TELEFONO);
    u->tel = *t;
    pub_manda(u);
}

void ph_servizi_pubblica_volume(PhServizi *s, const PhSrvVolume *v)
{
    Pub *u = pub_nuova(s, PUB_VOLUME);
    u->vol = *v;
    if (u->vol.volume < 0)
        u->vol.volume = 0;
    if (u->vol.volume > 100)
        u->vol.volume = 100;
    pub_manda(u);
}

void ph_servizi_pubblica_app(PhServizi *s, PhSrvApp *app, size_t n)
{
    Pub *u = pub_nuova(s, PUB_APP);
    u->v = app;
    u->n = app != NULL ? n : 0;
    pub_manda(u);
}

void ph_servizi_pubblica_notifiche(PhServizi *s, PhSrvNotifica *v, size_t n, const char *problema)
{
    Pub *u = pub_nuova(s, PUB_NOTIFICHE);
    u->v = v;
    u->n = v != NULL ? n : 0;
    u->testo = problema != NULL ? strdup(problema) : NULL;
    u->prima = -1;
    pub_manda(u);
}

void ph_servizi_pubblica_notifiche_lettura(PhServizi *s, PhSrvNotifica *v, size_t n,
                                           const char *problema, bool prima_lettura)
{
    Pub *u = pub_nuova(s, PUB_NOTIFICHE);
    u->v = v;
    u->n = v != NULL ? n : 0;
    u->testo = problema != NULL ? strdup(problema) : NULL;
    u->prima = prima_lettura ? 1 : 0;
    pub_manda(u);
}

void ph_servizi_pubblica_trasferimenti(PhServizi *s, PhSrvTrasferimento *v, size_t n,
                                       bool coda_ferma)
{
    Pub *u = pub_nuova(s, PUB_TRASFERIMENTI);
    u->v = v;
    u->n = v != NULL ? n : 0;
    u->flag = coda_ferma;
    pub_manda(u);
}

void ph_servizi_pubblica_icona(PhServizi *s, const char *pacchetto, const void *png, size_t n)
{
    if (pacchetto == NULL)
        return;
    Pub *u = pub_nuova(s, PUB_ICONA);
    u->testo = strdup(pacchetto);
    u->t = decodifica(png, n, ICONA_BYTE_MAX, ICONA_LATO_MAX);
    pub_manda(u);
}

void ph_servizi_pubblica_appunti(PhServizi *s, const char *testo)
{
    if (testo == NULL || !g_utf8_validate(testo, -1, NULL))
        return;
    Pub *u = pub_nuova(s, PUB_APPUNTI);
    u->testo = strdup(testo);
    pub_manda(u);
}

void ph_servizi_pubblica_profilo(PhServizi *s, PhCfgTelefono *profilo)
{
    if (profilo == NULL)
        return;
    Pub *u = pub_nuova(s, PUB_PROFILO);
    u->v = profilo;
    pub_manda(u);
}

void ph_servizi_pubblica_preferenze(PhServizi *s, const PhCfgPreferenze *p)
{
    Pub *u = pub_nuova(s, PUB_PREFERENZE);
    u->pref = *p;
    pub_manda(u);
}

/* ---- Creazione e fine ------------------------------------------------- */
PhServizi *ph_servizi_nuovi(const PhServiziBackend *tabella, void *b)
{
    g_return_val_if_fail(tabella != NULL, NULL);
    ph_shell_segna_thread_gtk();
    PhServizi *s = g_new0(PhServizi, 1);
    atomic_init(&s->rif, 1);
    atomic_init(&s->chiuso, 0);
    s->vt = *tabella;
    s->b = b;
    ph_ascoltatori_inizia(&s->asc);
    ph_vivi_aggiungi(s);
    s->col.stato = PH_SRV_CERCO;
    s->vol.volume = 100;
    s->icone = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    s->icone_chieste = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    s->kora_tex = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    s->kora_falliti = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    s->inst = g_hash_table_new(g_direct_hash, g_direct_equal);
    s->completati_visti = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    s->ctl.suoneria = s->ctl.non_disturbare = s->ctl.torcia = PH_SRV_NON_DISPONIBILE;
    ph_cfg_preferenze_predefinite(&s->pref);
    s->profilo = ph_cfg_telefono_nuovo();
    if (s->profilo == NULL) {
        ph_vivi_togli(s);
        ph_ascoltatori_finisci(&s->asc);
        g_hash_table_destroy(s->icone);
        g_hash_table_destroy(s->icone_chieste);
        g_hash_table_destroy(s->kora_tex);
        g_hash_table_destroy(s->kora_falliti);
        g_hash_table_destroy(s->inst);
        g_hash_table_destroy(s->completati_visti);
        g_free(s);
        return NULL;
    }
    return s;
}

static void flush_pref(PhServizi *s);
static void inst_chiudi_tutte(PhServizi *s);

void ph_servizi_libera(PhServizi *s)
{
    if (s == NULL)
        return;
    flush_profilo(s);
    flush_pref(s);
    atomic_store(&s->chiuso, 1);
    inst_chiudi_tutte(s); /* le installazioni in corso vedono il flag d'annullamento */
    if (s->vt.chiudi != NULL)
        s->vt.chiudi(s->b);
    s->b = NULL;
    rif_lascia(s);
}

/* ---- Letture ---------------------------------------------------------- */
void ph_srv_collegamento(PhServizi *s, PhSrvCollegamento *out) { *out = s->col; }
void ph_srv_telefono(PhServizi *s, PhSrvTelefono *out) { *out = s->tel; }
void ph_srv_volume(PhServizi *s, PhSrvVolume *out) { *out = s->vol; }

const PhSrvApp *ph_srv_app(PhServizi *s, size_t *n)
{
    *n = s->n_app;
    return s->app;
}

const PhSrvApp *ph_srv_app_cerca(PhServizi *s, const char *pacchetto)
{
    for (size_t i = 0; pacchetto != NULL && i < s->n_app; i++)
        if (strcmp(s->app[i].pacchetto, pacchetto) == 0)
            return &s->app[i];
    return NULL;
}

const PhSrvNotifica *ph_srv_notifiche(PhServizi *s, size_t *n, const char **problema)
{
    *n = s->n_notif;
    if (problema != NULL)
        *problema = s->problema;
    return s->notif;
}

const PhSrvTrasferimento *ph_srv_trasferimenti(PhServizi *s, size_t *n)
{
    *n = s->n_tr;
    return s->tr;
}

bool ph_srv_trasferimenti_attivi(PhServizi *s, PhSrvVerso verso)
{
    if (s->coda_ferma)
        return false;
    for (size_t i = 0; i < s->n_tr; i++)
        if (s->tr[i].verso == verso && (s->tr[i].stato == PH_SRV_TR_ATTESA || s->tr[i].stato == PH_SRV_TR_CORSO))
            return true;
    return false;
}

bool ph_srv_coda_ferma(PhServizi *s) { return s->coda_ferma; }

/* ---- Icone del tema Kora (file separati, vedi servizi.h) ---------------- */
#define KORA_TABELLA_MAX (256u * 1024u)
#define KORA_RIGHE_MAX 2000
#define KORA_PACCHETTO_MAX 127
#define KORA_NOME_MAX 95

/* Legge un file normale fino a `max` byte (un byte in piu' di `max` per
 * accorgersi che e' troppo grande). Buffer nostro con NUL finale; NULL se non
 * c'e', non e' un file normale o non si legge. */
static char *file_piccolo(const char *percorso, size_t max, size_t *n_out)
{
    /* O_NONBLOCK: aprire una FIFO senza scrittore non deve bloccare il thread GTK. */
    int fd = open(percorso, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return NULL;
    }
    char *buf = g_malloc(max + 2);
    size_t n = 0;
    while (n < max + 1) {
        ssize_t r = read(fd, buf + n, max + 1 - n);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            if (r < 0) { /* errore di lettura */
                close(fd);
                g_free(buf);
                return NULL;
            }
            break;
        }
        n += (size_t)r;
    }
    close(fd);
    buf[n] = '\0';
    *n_out = n;
    return buf;
}

static bool kora_pacchetto_valido(const char *p, size_t n)
{
    if (n == 0 || n > KORA_PACCHETTO_MAX)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!g_ascii_isalnum(p[i]) && p[i] != '.' && p[i] != '_')
            return false;
    return true;
}

/* Il nome finisce in un percorso: niente '/', niente "..", non inizia con '.'. */
static bool kora_nome_valido(const char *p, size_t n)
{
    if (n == 0 || n > KORA_NOME_MAX || p[0] == '.')
        return false;
    for (size_t i = 0; i < n; i++)
        if (!g_ascii_isalnum(p[i]) && p[i] != '.' && p[i] != '_' && p[i] != '+' && p[i] != '-')
            return false;
    for (size_t i = 0; i + 1 < n; i++)
        if (p[i] == '.' && p[i + 1] == '.')
            return false;
    return true;
}

/* Una riga alla volta; la riga malformata si salta, le altre valgono. */
static void kora_leggi_tabella(PhServizi *s, char *testo, size_t n, bool troncato)
{
    /* Troncata dal tetto: l'ultima riga e' forse a meta', si scarta. */
    if (troncato) {
        char *ultimo = memrchr(testo, '\n', n);
        n = ultimo != NULL ? (size_t)(ultimo - testo) + 1 : 0;
        testo[n] = '\0';
    }
    int righe = 0;
    size_t i = 0;
    while (i < n && righe < KORA_RIGHE_MAX) {
        char *r = testo + i;
        char *fine = memchr(r, '\n', n - i);
        size_t len = fine != NULL ? (size_t)(fine - r) : n - i;
        i += len + (fine != NULL ? 1 : 0);
        righe++;
        if (len > 0 && r[len - 1] == '\r')
            len--;
        if (len == 0 || r[0] == '#')
            continue;
        char *sp = memchr(r, ' ', len);
        if (sp == NULL)
            continue;
        size_t lp = (size_t)(sp - r), ln = len - lp - 1;
        if (!kora_pacchetto_valido(r, lp) || !kora_nome_valido(sp + 1, ln))
            continue;
        char *pk = g_strndup(r, lp);
        if (g_hash_table_contains(s->kora_tab, pk)) { /* doppio: vale il primo */
            g_free(pk);
            continue;
        }
        g_hash_table_insert(s->kora_tab, pk, g_strndup(sp + 1, ln));
    }
}

/* Al primo uso: cartella dall'ambiente, tabella letta una volta sola. Se manca
 * qualcosa resta kora_tab == NULL (nessuna icona Kora, tutto come prima) e si
 * scrive una riga di registro, una volta. */
static void kora_inizia(PhServizi *s)
{
    s->kora_letta = true;
    const char *env = g_getenv("YASDE_ICONE_KORA");
    const char *appdir = g_getenv("APPDIR");
    if (env != NULL && *env != '\0')
        s->kora_dir = g_strdup(env);
    else if (appdir != NULL && *appdir != '\0')
        s->kora_dir = g_build_filename(appdir, "usr", "share", "yasde", "icone-kora", NULL);
    if (s->kora_dir == NULL)
        return;
    char *percorso = g_build_filename(s->kora_dir, "tabella.txt", NULL);
    size_t n = 0;
    char *testo = file_piccolo(percorso, KORA_TABELLA_MAX, &n);
    g_free(percorso);
    if (testo == NULL) {
        ph_shell_decisione("icone Kora non usate", "tabella.txt assente o illeggibile in %s", s->kora_dir);
        g_clear_pointer(&s->kora_dir, g_free);
        return;
    }
    s->kora_tab = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    kora_leggi_tabella(s, testo, n > KORA_TABELLA_MAX ? KORA_TABELLA_MAX : n, n > KORA_TABELLA_MAX);
    g_free(testo);
}

/* La texture Kora del pacchetto (di proprieta' della cache) o NULL. Il PNG si
 * carica una volta; se fallisce non si riprova. */
static GdkTexture *kora_icona(PhServizi *s, const char *pacchetto)
{
    if (!s->kora_letta)
        kora_inizia(s);
    if (s->kora_tab == NULL)
        return NULL;
    GdkTexture *t = g_hash_table_lookup(s->kora_tex, pacchetto);
    if (t != NULL)
        return t;
    const char *nome = g_hash_table_lookup(s->kora_tab, pacchetto);
    if (nome == NULL || g_hash_table_contains(s->kora_falliti, pacchetto))
        return NULL;
    char *file = g_strconcat(nome, ".png", NULL);
    char *percorso = g_build_filename(s->kora_dir, file, NULL);
    size_t n = 0;
    char *dati = file_piccolo(percorso, ICONA_BYTE_MAX, &n);
    g_free(file);
    g_free(percorso);
    if (dati != NULL)
        t = decodifica(dati, n, ICONA_BYTE_MAX, ICONA_LATO_MAX);
    g_free(dati);
    if (t == NULL) {
        g_hash_table_add(s->kora_falliti, g_strdup(pacchetto));
        return NULL;
    }
    g_hash_table_insert(s->kora_tex, g_strdup(pacchetto), t);
    return t;
}

GdkTexture *ph_srv_icona(PhServizi *s, const char *pacchetto)
{
    if (pacchetto == NULL || *pacchetto == '\0')
        return NULL;
    GdkTexture *k = kora_icona(s, pacchetto);
    if (k != NULL)
        return k;
    GdkTexture *t = g_hash_table_lookup(s->icone, pacchetto);
    if (t != NULL)
        return t;
    if (!g_hash_table_contains(s->icone_chieste, pacchetto) && s->vt.icona_chiedi != NULL &&
        s->col.ha_telefono) {
        g_hash_table_add(s->icone_chieste, g_strdup(pacchetto));
        s->vt.icona_chiedi(s->b, pacchetto, PH_SRV_ICONA_LATO);
    }
    return NULL;
}

/* ---- Persistenza ------------------------------------------------------ */
PhCfgPreferenze *ph_srv_preferenze(PhServizi *s) { return &s->pref; }
PhCfgTelefono *ph_srv_profilo(PhServizi *s) { return s->profilo; }

static void salva_profilo_ora(PhServizi *s)
{
    PhCfgTelefono *c = ph_cfg_telefono_nuovo();
    if (c == NULL) {
        fprintf(stderr, "servizi: memoria finita, profilo non salvato\n");
        return;
    }
    memcpy(c, s->profilo, sizeof *c);
    if (s->vt.profilo_salva != NULL)
        s->vt.profilo_salva(s->b, c);
    else
        ph_cfg_telefono_libera(c);
}

static void salva_pref_ora(PhServizi *s)
{
    PhCfgPreferenze *c = malloc(sizeof *c);
    if (c == NULL)
        return;
    *c = s->pref;
    if (s->vt.preferenze_salva != NULL)
        s->vt.preferenze_salva(s->b, c);
    else
        free(c);
}

/* Salvataggio in attesa: lo si fa subito (prima di cambiare telefono o di
 * chiudere). */
static void flush_profilo(PhServizi *s)
{
    if (s->salva_profilo == 0)
        return;
    g_source_remove(s->salva_profilo);
    s->salva_profilo = 0;
    salva_profilo_ora(s);
}

static void flush_pref(PhServizi *s)
{
    if (s->salva_pref == 0)
        return;
    g_source_remove(s->salva_pref);
    s->salva_pref = 0;
    salva_pref_ora(s);
}

static gboolean scade_profilo(gpointer p)
{
    PhServizi *s = p;
    s->salva_profilo = 0; /* la sorgente finisce qui: niente g_source_remove */
    salva_profilo_ora(s);
    return G_SOURCE_REMOVE;
}

static gboolean scade_pref(gpointer p)
{
    PhServizi *s = p;
    s->salva_pref = 0;
    salva_pref_ora(s);
    return G_SOURCE_REMOVE;
}

void ph_srv_profilo_salva(PhServizi *s)
{
    if (s->salva_profilo == 0)
        s->salva_profilo = g_timeout_add(SALVA_RITARDO_MS, scade_profilo, s);
}

void ph_srv_preferenze_salva(PhServizi *s)
{
    if (s->salva_pref == 0)
        s->salva_pref = g_timeout_add(SALVA_RITARDO_MS, scade_pref, s);
}

/* ---- Avvisi ----------------------------------------------------------- */
guint ph_srv_ascolta(PhServizi *s, PhSrvEvento ev, PhSrvAvviso fn, void *utente)
{
    g_return_val_if_fail(s != NULL && ev < PH_SRV_EV_NUMERO, 0);
    return ph_ascoltatori_aggiungi(&s->asc, (int)ev, (PhFnGenerica)fn, utente);
}

void ph_srv_smetti(PhServizi *s, guint id)
{
    /* Dopo la liberazione (ultimo riferimento lasciato) non si tocca nulla. */
    if (s != NULL && id != 0 && ph_vivi_e(s))
        ph_ascoltatori_togli(&s->asc, id);
}

/* ---- Comandi ---------------------------------------------------------- */
void ph_srv_riconnetti_ora(PhServizi *s)
{
    if (s->vt.riconnetti_ora != NULL)
        s->vt.riconnetti_ora(s->b);
}

void ph_srv_pannello(PhServizi *s, bool acceso)
{
    if (s->vt.pannello != NULL)
        s->vt.pannello(s->b, acceso);
}

void ph_srv_imposta_volume(PhServizi *s, int volume)
{
    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    if (s->vt.volume != NULL)
        s->vt.volume(s->b, volume);
}

void ph_srv_imposta_muto(PhServizi *s, bool muto)
{
    if (s->vt.muto != NULL)
        s->vt.muto(s->b, muto);
}

void ph_srv_appunti_imposta(PhServizi *s, const char *testo)
{
    if (testo == NULL || *testo == '\0' || strlen(testo) > TESTO_INPUT_MAX ||
        !g_utf8_validate(testo, -1, NULL))
        return;
    if (s->vt.appunti_imposta != NULL)
        s->vt.appunti_imposta(s->b, testo);
}

/* ---- Lavori ------------------------------------------------------------ */
typedef enum {
    LAV_ELENCO,
    LAV_CERCA,
    LAV_FATTO,
    LAV_IMMAGINE,
    LAV_BYTE,
    LAV_MEMORIA,
    /* aggiunte del 23 nov */
    LAV_INST,
    LAV_PANNELLO,
    LAV_DETTAGLIO,
    LAV_CALENDARIO,
    LAV_SCREENSHOT,
    LAV_ABBINA,
    LAV_CAL_INTERVALLO,
    LAV_SVEGLIA,
    LAV_BATTERIA,
    LAV_LUOGHI, /* ricerca dei luoghi del meteo (23 nov notte) */
} LavTipo;

struct PhSrvLavoro {
    atomic_int rif;
    atomic_int finito;
    PhServizi *s;
    LavTipo tipo;
    GCancellable *annulla;
    PhSrvElencoFatto elenco;
    PhSrvCercaTrovati trovati;
    PhSrvCercaFine fine;
    PhSrvFatto fatto;
    PhSrvImmagineFatta immagine;
    PhSrvByteFatti byte;
    PhSrvMemoriaFatta memoria;
    void *utente;
    char *cartella;    /* per l'elenco */
    /* aggiunte del 23 nov */
    PhSrvInstAvanzamento inst_avanzamento;
    PhSrvInstFatto inst_fatto;
    PhSrvPannelloFatto pannello;
    PhSrvDettaglioFatto dettaglio;
    PhSrvCalendarioFatto calendario;
    PhSrvScreenshotFatto screenshot;
    PhSrvAbbFatto abbina;
    PhSrvCalendarioMeseFatto cal_mese;
    PhSrvSvegliaFatta sveglia;
    PhSrvBatteriaFatta batteria;
    PhSrvMeteoCercaFatta luoghi;
    atomic_bool flag_annulla;     /* installazioni: annullamento chiesto */
    guint id;                     /* installazioni: chiave in s->inst */
    atomic_ullong av_fatti, av_totale;
    atomic_bool av_in_volo;
    struct PhSrvFlusso *flusso;   /* screenshot: riferimento, per non consegnare dopo la chiusura */
};

struct PhSrvFlusso;
static void fl_lascia_esterno(struct PhSrvFlusso *f);
static bool fl_chiuso(struct PhSrvFlusso *f);

static PhSrvLavoro *lav_nuovo(PhServizi *s, LavTipo tipo, GCancellable *annulla, void *utente)
{
    PhSrvLavoro *l = g_new0(PhSrvLavoro, 1);
    atomic_init(&l->rif, 1);
    atomic_init(&l->finito, 0);
    l->s = rif_prendi(s);
    l->tipo = tipo;
    l->annulla = annulla != NULL ? g_object_ref(annulla) : NULL;
    l->utente = utente;
    return l;
}

static void lav_lascia(PhSrvLavoro *l)
{
    if (atomic_fetch_sub(&l->rif, 1) != 1)
        return;
    if (l->annulla != NULL)
        g_object_unref(l->annulla);
    free(l->cartella);
    if (l->flusso != NULL)
        fl_lascia_esterno(l->flusso);
    rif_lascia(l->s);
    g_free(l);
}

bool ph_servizi_lavoro_annullato(PhSrvLavoro *l)
{
    return chiusi(l->s) || (l->annulla != NULL && g_cancellable_is_cancelled(l->annulla)) ||
           atomic_load(&l->flag_annulla) || (l->flusso != NULL && fl_chiuso(l->flusso));
}

/* Valido da consegnare? (thread GTK) */
static bool lav_vivo(PhSrvLavoro *l)
{
    return !chiusi(l->s) && !(l->annulla != NULL && g_cancellable_is_cancelled(l->annulla)) &&
           !(l->flusso != NULL && fl_chiuso(l->flusso));
}

typedef struct {
    PhSrvLavoro *l;
    PhSrvEsito esito;
    char *frase;
    PhSrvVoce *voci;
    size_t n;
    PhSrvTrovato *trovati;
    char **stringhe;   /* proprieta' delle stringhe dei trovati */
    bool massimo;
    GdkTexture *t;
    GBytes *byte;
    uint64_t totale, libera;
    bool finale;
    /* aggiunte del 23 nov */
    PhSrvInstRisultato inst;   /* messaggio e codice puntano a inst_msg / inst_cod */
    char *inst_msg, *inst_cod;
    PhSrvPannelloEsito pannello;
    bool pannello_detto;      /* false = chiusura generica: ERRORE */
    bool ha_dettaglio;
    PhSrvMemoriaDettaglio dettaglio;
    PhSrvEventoCalendario *eventi; /* titoli in stringhe[0..n) */
    char *percorso;
    uint64_t *dims;            /* (2) dimensioni dei trovati, UINT64_MAX = ignota */
    PhSrvAbbEsito abb;
    bool abb_detto;
    /* widget del 23 nov pomeriggio */
    bool troncato, presente, ha_batteria;
    char *app_cal;     /* anche il pacchetto della sveglia */
    uint64_t ora;
    PhSrvBatteria bat;
    PhMeteoLuogo *luoghi; /* (23 nov notte) almeno un elemento se esito OK */
} Ris;

static void ris_applica_inst(Ris *r);

/* Solo thread GTK: il gruppo di risultati della ricerca in consegna. */
static const PhSrvTrovato *gruppo_trovati;
static const uint64_t *gruppo_dims;
static size_t gruppo_n;

bool ph_srv_trovato_dimensione(PhServizi *s, const PhSrvTrovato *t, uint64_t *byte)
{
    (void)s;
    if (t == NULL || gruppo_trovati == NULL || gruppo_dims == NULL || t < gruppo_trovati ||
        t >= gruppo_trovati + gruppo_n)
        return false;
    uint64_t d = gruppo_dims[t - gruppo_trovati];
    if (d == UINT64_MAX || t->cartella)
        return false;
    if (byte != NULL)
        *byte = d;
    return true;
}

static void ris_applica(void *p)
{
    Ris *r = p;
    PhSrvLavoro *l = r->l;
    if (!lav_vivo(l))
        return;
    PhServizi *s = l->s;
    switch (l->tipo) {
    case LAV_ELENCO:
        l->elenco(s, r->esito, r->frase, l->cartella, r->voci, r->n, l->utente);
        break;
    case LAV_CERCA:
        if (!r->finale) {
            /* Il gruppo in consegna, per ph_srv_trovato_dimensione. */
            gruppo_trovati = r->trovati;
            gruppo_dims = r->dims;
            gruppo_n = r->n;
            l->trovati(s, r->trovati, r->n, l->utente);
            gruppo_trovati = NULL;
            gruppo_dims = NULL;
            gruppo_n = 0;
        }
        else
            l->fine(s, r->esito, r->frase, r->massimo, l->utente);
        break;
    case LAV_FATTO:
        l->fatto(s, r->esito, r->frase, l->utente);
        break;
    case LAV_IMMAGINE:
        l->immagine(s, r->t != NULL ? r->esito : (r->esito == PH_SRV_ESITO_OK ? PH_SRV_ESITO_ERRORE : r->esito),
                    r->frase, r->t, l->utente);
        break;
    case LAV_BYTE:
        l->byte(s, r->esito, r->frase, r->byte, l->utente);
        break;
    case LAV_MEMORIA:
        l->memoria(s, r->esito, r->totale, r->libera, l->utente);
        break;
    case LAV_INST:
        ris_applica_inst(r);
        break;
    case LAV_PANNELLO:
        /* Una chiusura generica (ph_servizi_lavoro_fatto) arriva come ERRORE. */
        l->pannello(s, r->pannello_detto ? r->pannello : PH_SRV_PAN_ERRORE, r->frase, l->utente);
        break;
    case LAV_DETTAGLIO:
        l->dettaglio(s, r->ha_dettaglio ? r->esito : (r->esito == PH_SRV_ESITO_OK ? PH_SRV_ESITO_ERRORE : r->esito),
                     r->frase, r->ha_dettaglio ? &r->dettaglio : NULL, l->utente);
        break;
    case LAV_CALENDARIO:
        l->calendario(s, r->esito, r->frase, r->eventi, r->eventi != NULL ? r->n : 0, l->utente);
        break;
    case LAV_SCREENSHOT:
        l->screenshot(s, r->esito, r->frase, r->percorso, r->byte, l->utente);
        break;
    case LAV_ABBINA:
        l->abbina(s, r->abb_detto ? r->abb : PH_SRV_ABB_FALLITO, r->frase, l->utente);
        break;
    case LAV_CAL_INTERVALLO:
        l->cal_mese(s, r->esito, r->frase, r->eventi, r->eventi != NULL ? r->n : 0, r->troncato,
                    r->app_cal != NULL ? r->app_cal : "", l->utente);
        break;
    case LAV_SVEGLIA:
        l->sveglia(s, r->esito, r->frase, r->presente, r->ora, r->app_cal != NULL ? r->app_cal : "", l->utente);
        break;
    case LAV_BATTERIA:
        l->batteria(s, r->ha_batteria ? r->esito : (r->esito == PH_SRV_ESITO_OK ? PH_SRV_ESITO_ERRORE : r->esito),
                    r->frase, r->ha_batteria ? &r->bat : NULL, l->utente);
        break;
    case LAV_LUOGHI:
        l->luoghi(s, r->luoghi != NULL ? r->esito : (r->esito == PH_SRV_ESITO_OK ? PH_SRV_ESITO_ERRORE : r->esito),
                  r->frase, r->luoghi, r->luoghi != NULL ? r->n : 0, l->utente);
        break;
    }
}

static void ris_libera(void *p)
{
    Ris *r = p;
    /* Installazione: il riferimento della tabella degli annullamenti si toglie
     * qui, anche se la consegna e' stata scartata (servizi chiusi). */
    if (r->finale && r->l->tipo == LAV_INST &&
        g_hash_table_remove(r->l->s->inst, GUINT_TO_POINTER(r->l->id)))
        lav_lascia(r->l);
    /* Abbinamento concluso (anche se la risposta e' stata annullata o scartata). */
    if (r->finale && r->l->tipo == LAV_ABBINA && r->l->s->abbinamenti > 0)
        r->l->s->abbinamenti--;
    free(r->frase);
    ph_srv_voci_libera(r->voci, r->voci != NULL ? r->n : 0);
    if (r->stringhe != NULL) {
        for (size_t i = 0; i < 2 * r->n; i++)
            free(r->stringhe[i]);
        free(r->stringhe);
    }
    free(r->trovati);
    free(r->inst_msg);
    free(r->inst_cod);
    free(r->eventi);
    free(r->percorso);
    free(r->dims);
    free(r->app_cal);
    free(r->luoghi);
    if (r->t != NULL)
        g_object_unref(r->t);
    if (r->byte != NULL)
        g_bytes_unref(r->byte);
    lav_lascia(r->l);
    g_free(r);
}

static Ris *ris_nuovo(PhSrvLavoro *l, PhSrvEsito esito, const char *frase, bool finale)
{
    if (finale && atomic_exchange(&l->finito, 1) != 0) {
        fprintf(stderr, "servizi: un lavoro chiuso due volte (errore del backend)\n");
        return NULL;
    }
    Ris *r = g_new0(Ris, 1);
    atomic_fetch_add(&l->rif, 1);
    r->l = l;
    r->esito = esito;
    r->frase = frase != NULL ? strdup(frase) : NULL;
    r->finale = finale;
    return r;
}

static void ris_manda(Ris *r, bool finale)
{
    PhSrvLavoro *l = r->l;
    ph_shell_consegna(ris_applica, r, ris_libera);
    if (finale)
        lav_lascia(l); /* il riferimento del backend */
}

void ph_servizi_lavoro_elenco(PhSrvLavoro *l, PhSrvEsito esito, const char *frase, PhSrvVoce *voci,
                              size_t n)
{
    Ris *r = ris_nuovo(l, esito, frase, true);
    if (r == NULL) {
        ph_srv_voci_libera(voci, n);
        return;
    }
    r->voci = voci;
    r->n = voci != NULL ? n : 0;
    ris_manda(r, true);
}

void ph_servizi_lavoro_trovati(PhSrvLavoro *l, const PhSrvTrovato *v, size_t n)
{
    ph_servizi_lavoro_trovati_dimensioni(l, v, NULL, n);
}

void ph_servizi_lavoro_trovati_dimensioni(PhSrvLavoro *l, const PhSrvTrovato *v, const uint64_t *dimensioni,
                                          size_t n)
{
    if (n == 0 || v == NULL || atomic_load(&l->finito))
        return;
    Ris *r = ris_nuovo(l, PH_SRV_ESITO_OK, NULL, false);
    r->trovati = calloc(n, sizeof *r->trovati);
    r->stringhe = calloc(2 * n, sizeof *r->stringhe);
    if (r->trovati == NULL || r->stringhe == NULL) {
        r->n = 0;
        ris_libera(r);
        return;
    }
    r->n = n;
    for (size_t i = 0; i < n; i++) {
        r->stringhe[2 * i] = strdup(v[i].percorso != NULL ? v[i].percorso : "");
        r->stringhe[2 * i + 1] = strdup(v[i].percorso_schermo != NULL ? v[i].percorso_schermo
                                                                     : (v[i].percorso != NULL ? v[i].percorso : ""));
        r->trovati[i].percorso = r->stringhe[2 * i] != NULL ? r->stringhe[2 * i] : "";
        r->trovati[i].percorso_schermo = r->stringhe[2 * i + 1] != NULL ? r->stringhe[2 * i + 1] : "";
        r->trovati[i].cartella = v[i].cartella;
    }
    if (dimensioni != NULL) {
        r->dims = calloc(n, sizeof *r->dims);
        for (size_t i = 0; r->dims != NULL && i < n; i++)
            r->dims[i] = v[i].cartella ? UINT64_MAX : dimensioni[i];
    }
    ris_manda(r, false);
}

void ph_servizi_lavoro_cerca_fine(PhSrvLavoro *l, PhSrvEsito esito, const char *frase, bool massimo)
{
    Ris *r = ris_nuovo(l, esito, frase, true);
    if (r == NULL)
        return;
    r->massimo = massimo;
    ris_manda(r, true);
}

void ph_servizi_lavoro_fatto(PhSrvLavoro *l, PhSrvEsito esito, const char *frase)
{
    Ris *r = ris_nuovo(l, esito, frase, true);
    if (r != NULL)
        ris_manda(r, true);
}

void ph_servizi_lavoro_immagine(PhSrvLavoro *l, PhSrvEsito esito, const char *frase, const void *dati,
                                size_t n)
{
    Ris *r = ris_nuovo(l, esito, frase, true);
    if (r == NULL)
        return;
    if (esito == PH_SRV_ESITO_OK)
        r->t = decodifica(dati, n, IMMAGINE_BYTE_MAX, IMMAGINE_LATO_MAX);
    ris_manda(r, true);
}

void ph_servizi_lavoro_byte(PhSrvLavoro *l, PhSrvEsito esito, const char *frase, GBytes *byte)
{
    Ris *r = ris_nuovo(l, esito, frase, true);
    if (r == NULL)
        return;
    r->byte = byte != NULL ? g_bytes_ref(byte) : NULL;
    if (r->esito == PH_SRV_ESITO_OK && r->byte == NULL)
        r->esito = PH_SRV_ESITO_ERRORE;
    ris_manda(r, true);
}

void ph_servizi_lavoro_memoria(PhSrvLavoro *l, PhSrvEsito esito, uint64_t totale, uint64_t libera)
{
    Ris *r = ris_nuovo(l, esito, NULL, true);
    if (r == NULL)
        return;
    r->totale = totale;
    r->libera = libera > totale ? totale : libera;
    ris_manda(r, true);
}

/* Senza backend per una richiesta: risposta immediata (asincrona) di errore. */
static void senza_backend(PhSrvLavoro *l)
{
    switch (l->tipo) {
    case LAV_ELENCO:
        ph_servizi_lavoro_elenco(l, PH_SRV_ESITO_ERRORE, NULL, NULL, 0);
        break;
    case LAV_CERCA:
        ph_servizi_lavoro_cerca_fine(l, PH_SRV_ESITO_ERRORE, NULL, false);
        break;
    case LAV_FATTO:
        ph_servizi_lavoro_fatto(l, PH_SRV_ESITO_ERRORE, NULL);
        break;
    case LAV_IMMAGINE:
        ph_servizi_lavoro_immagine(l, PH_SRV_ESITO_ERRORE, NULL, NULL, 0);
        break;
    case LAV_BYTE:
        ph_servizi_lavoro_byte(l, PH_SRV_ESITO_ERRORE, NULL, NULL);
        break;
    case LAV_MEMORIA:
        ph_servizi_lavoro_memoria(l, PH_SRV_ESITO_ERRORE, 0, 0);
        break;
    case LAV_INST:
    case LAV_PANNELLO:
    case LAV_DETTAGLIO:
    case LAV_CALENDARIO:
    case LAV_SCREENSHOT:
    case LAV_ABBINA:
    case LAV_CAL_INTERVALLO:
    case LAV_SVEGLIA:
    case LAV_BATTERIA:
    case LAV_LUOGHI:
        ph_servizi_lavoro_fatto(l, PH_SRV_ESITO_ERRORE, NULL);
        break;
    }
}

void ph_srv_tel_elenca(PhServizi *s, const char *cartella, GCancellable *annulla,
                       PhSrvElencoFatto fatto, void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_ELENCO, annulla, utente);
    l->elenco = fatto;
    l->cartella = strdup(cartella != NULL ? cartella : "");
    if (s->vt.tel_elenca != NULL && cartella != NULL && cartella[0] == '/')
        s->vt.tel_elenca(s->b, l, cartella);
    else
        senza_backend(l);
}

void ph_srv_tel_cerca(PhServizi *s, const char *cartella, const char *testo, bool solo_cartelle,
                      unsigned massimo, GCancellable *annulla, PhSrvCercaTrovati trovati,
                      PhSrvCercaFine fine, void *utente)
{
    g_return_if_fail(trovati != NULL && fine != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_CERCA, annulla, utente);
    l->trovati = trovati;
    l->fine = fine;
    if (massimo == 0)
        massimo = 30;
    if (s->vt.tel_cerca != NULL && cartella != NULL && cartella[0] == '/' && testo != NULL &&
        *testo != '\0' && g_utf8_validate(testo, -1, NULL))
        s->vt.tel_cerca(s->b, l, cartella, testo, solo_cartelle, massimo);
    else
        senza_backend(l);
}

void ph_srv_tel_crea_cartella(PhServizi *s, const char *percorso, GCancellable *annulla,
                              PhSrvFatto fatto, void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_FATTO, annulla, utente);
    l->fatto = fatto;
    if (s->vt.tel_crea_cartella != NULL && percorso != NULL && percorso[0] == '/')
        s->vt.tel_crea_cartella(s->b, l, percorso);
    else
        senza_backend(l);
}

void ph_srv_tel_miniatura(PhServizi *s, const char *percorso, int lato, GCancellable *annulla,
                          PhSrvImmagineFatta fatto, void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_IMMAGINE, annulla, utente);
    l->immagine = fatto;
    if (lato < 32)
        lato = 32;
    if (lato > 512)
        lato = 512;
    if (s->vt.tel_miniatura != NULL && percorso != NULL && percorso[0] == '/')
        s->vt.tel_miniatura(s->b, l, percorso, lato);
    else
        senza_backend(l);
}

void ph_srv_tel_leggi(PhServizi *s, const char *percorso, size_t massimo, GCancellable *annulla,
                      PhSrvByteFatti fatto, void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_BYTE, annulla, utente);
    l->byte = fatto;
    if (s->vt.tel_leggi != NULL && percorso != NULL && percorso[0] == '/' && massimo > 0)
        s->vt.tel_leggi(s->b, l, percorso, massimo);
    else
        senza_backend(l);
}

void ph_srv_tel_memoria(PhServizi *s, GCancellable *annulla, PhSrvMemoriaFatta fatto, void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_MEMORIA, annulla, utente);
    l->memoria = fatto;
    if (s->vt.tel_memoria != NULL)
        s->vt.tel_memoria(s->b, l);
    else
        senza_backend(l);
}

/* ---- Trasferimenti ---------------------------------------------------- */
int ph_srv_tr_accoda(PhServizi *s, PhSrvVerso verso, const char *origine, const char *cartella,
                     PhErrore *e)
{
    if (origine == NULL || cartella == NULL || *origine == '\0' || *cartella == '\0')
        return ph_errore(e, "%s", ph_t("Non riesco ad accodare il trasferimento."));
    if (s->vt.tr_accoda == NULL)
        return ph_errore(e, "%s", ph_t("Non riesco ad accodare il trasferimento."));
    return s->vt.tr_accoda(s->b, verso, origine, cartella, e);
}

void ph_srv_tr_annulla(PhServizi *s, uint64_t id)
{
    if (s->vt.tr_annulla != NULL)
        s->vt.tr_annulla(s->b, id);
}

void ph_srv_tr_annulla_tutto(PhServizi *s)
{
    if (s->vt.tr_annulla_tutto != NULL)
        s->vt.tr_annulla_tutto(s->b);
}

void ph_srv_tr_rimuovi_finiti(PhServizi *s)
{
    if (s->vt.tr_rimuovi_finiti != NULL)
        s->vt.tr_rimuovi_finiti(s->b);
}

/* ---- Flussi ------------------------------------------------------------ */
struct PhSrvFlusso {
    atomic_int rif;
    atomic_int interrotto;
    atomic_int chiuso;
    PhServizi *s;
    void *fl;
    PhSrvFlussoAvviso avviso;
    void *utente;
    /* aggiunte del 23 nov: registrazione (thread GTK) */
    bool registrando;
    PhSrvRegAvviso reg_avviso;
    void *reg_utente;
};

static void fl_lascia(PhSrvFlusso *f)
{
    if (atomic_fetch_sub(&f->rif, 1) != 1)
        return;
    rif_lascia(f->s);
    g_free(f);
}

PhSrvFlusso *ph_srv_flusso_apri(PhServizi *s, const PhSrvFlussoRichiesta *r,
                                PhSrvFlussoAvviso avviso, void *utente, PhErrore *e)
{
    if (r == NULL || avviso == NULL) {
        ph_errore(e, "flusso: argomenti mancanti");
        return NULL;
    }
    bool specchio = r->pacchetto == NULL || r->pacchetto[0] == '\0';
    if (!specchio && (r->larghezza < 64 || r->larghezza > 2560 || r->altezza < 64 ||
                      r->altezza > 2560 || r->larghezza % 8 != 0 || r->altezza % 8 != 0)) {
        ph_errore(e, "flusso: misura %dx%d non valida (multipli di 8, 64-2560)", r->larghezza,
                  r->altezza);
        return NULL;
    }
    if (r->pagina > PH_SRV_PAGINA_MEMORIA) { /* elenco fisso: 0 nessuna, 1 batteria, 2 memoria */
        ph_errore(e, "flusso: pagina %u fuori elenco", (unsigned)r->pagina);
        return NULL;
    }
    if (s->vt.flusso_apri == NULL) {
        ph_errore(e, "flusso: servizio non disponibile");
        return NULL;
    }
    PhSrvFlusso *f = g_new0(PhSrvFlusso, 1);
    atomic_init(&f->rif, 1);
    atomic_init(&f->interrotto, 0);
    atomic_init(&f->chiuso, 0);
    f->s = rif_prendi(s);
    f->avviso = avviso;
    f->utente = utente;
    f->fl = s->vt.flusso_apri(s->b, f, r, e);
    if (f->fl == NULL) {
        fl_lascia(f);
        return NULL;
    }
    return f;
}

int ph_srv_flusso_leggi(PhSrvFlusso *f, PhFotogramma *out, int timeout_ms)
{
    *out = (PhFotogramma){0};
    if (atomic_load(&f->interrotto))
        return -1;
    int r = f->s->vt.flusso_leggi(f->s->b, f->fl, out, timeout_ms);
    if (r == 1 && atomic_load(&f->interrotto)) {
        ph_fotogramma_rilascia(out);
        return -1;
    }
    return r;
}

void ph_srv_flusso_interrompi(PhSrvFlusso *f)
{
    if (atomic_exchange(&f->interrotto, 1) == 0 && f->s->vt.flusso_interrompi != NULL)
        f->s->vt.flusso_interrompi(f->s->b, f->fl);
}

void ph_srv_flusso_preferisci_memoria(PhSrvFlusso *f)
{
    if (f->s->vt.flusso_memoria != NULL)
        f->s->vt.flusso_memoria(f->s->b, f->fl);
}

void ph_srv_flusso_pausa(PhSrvFlusso *f)
{
    if (f->s->vt.flusso_pausa != NULL)
        f->s->vt.flusso_pausa(f->s->b, f->fl, true);
}

void ph_srv_flusso_riprendi(PhSrvFlusso *f)
{
    if (f->s->vt.flusso_pausa != NULL)
        f->s->vt.flusso_pausa(f->s->b, f->fl, false);
}

void ph_srv_flusso_ridimensiona(PhSrvFlusso *f, int larghezza, int altezza)
{
    if (larghezza < 64 || larghezza > 2560 || altezza < 64 || altezza > 2560 || larghezza % 8 != 0 ||
        altezza % 8 != 0) {
        fprintf(stderr, "servizi: ridimensionamento %dx%d scartato (multipli di 8, 64-2560)\n",
                larghezza, altezza);
        return;
    }
    if (f->registrando) {
        /* Durante la registrazione la misura non cambia (5.5, servizi.h). */
        fprintf(stderr, "servizi: ridimensionamento ignorato durante la registrazione\n");
        return;
    }
    if (f->s->vt.flusso_ridimensiona != NULL)
        f->s->vt.flusso_ridimensiona(f->s->b, f->fl, larghezza, altezza);
}

static bool coordinate_valide(int vw, int vh, int x, int y)
{
    return vw > 0 && vh > 0 && vw <= 8192 && vh <= 8192 && x >= 0 && y >= 0 && x < vw && y < vh;
}

static void input(PhSrvFlusso *f, const PhSrvInput *in)
{
    if (f->s->vt.flusso_input != NULL && !atomic_load(&f->chiuso))
        f->s->vt.flusso_input(f->s->b, f->fl, in);
}

void ph_srv_flusso_tocco(PhSrvFlusso *f, int video_w, int video_h, int azione, int x, int y,
                         unsigned pulsanti)
{
    if (azione < PH_SRV_TOCCO_GIU || azione > PH_SRV_TOCCO_ANNULLA || !coordinate_valide(video_w, video_h, x, y))
        return;
    PhSrvInput in = {.tipo = PH_SRV_IN_TOCCO, .video_w = video_w, .video_h = video_h, .azione = azione,
                     .x = x, .y = y, .pulsanti = pulsanti};
    input(f, &in);
}

void ph_srv_flusso_rotella(PhSrvFlusso *f, int video_w, int video_h, int x, int y, int orizzontale,
                           int verticale)
{
    if (!coordinate_valide(video_w, video_h, x, y))
        return;
    PhSrvInput in = {.tipo = PH_SRV_IN_ROTELLA, .video_w = video_w, .video_h = video_h, .x = x, .y = y,
                     .orizzontale = CLAMP(orizzontale, -32767, 32767),
                     .verticale = CLAMP(verticale, -32767, 32767)};
    input(f, &in);
}

void ph_srv_flusso_tasto(PhSrvFlusso *f, bool giu, unsigned codice, unsigned meta)
{
    PhSrvInput in = {.tipo = PH_SRV_IN_TASTO, .giu = giu, .codice = codice, .meta = meta};
    input(f, &in);
}

void ph_srv_flusso_testo(PhSrvFlusso *f, const char *testo)
{
    if (testo == NULL || *testo == '\0' || strlen(testo) > TESTO_INPUT_MAX ||
        !g_utf8_validate(testo, -1, NULL))
        return;
    PhSrvInput in = {.tipo = PH_SRV_IN_TESTO, .testo = testo};
    input(f, &in);
}

void ph_srv_flusso_indietro(PhSrvFlusso *f)
{
    PhSrvInput in = {.tipo = PH_SRV_IN_INDIETRO};
    input(f, &in);
}

void ph_srv_flusso_chiudi(PhSrvFlusso *f, bool togli_task)
{
    if (f == NULL)
        return;
    ph_srv_flusso_interrompi(f);
    atomic_store(&f->chiuso, 1);
    if (f->s->vt.flusso_chiudi != NULL)
        f->s->vt.flusso_chiudi(f->s->b, f->fl, togli_task);
    f->fl = NULL;
    fl_lascia(f);
}

typedef struct {
    PhSrvFlusso *f;
    PhSrvFlussoDati d;
    char *frase;
} AvvisoFlusso;

static void avviso_applica(void *p)
{
    AvvisoFlusso *a = p;
    if (atomic_load(&a->f->chiuso) || chiusi(a->f->s))
        return;
    a->d.frase = a->frase;
    a->f->avviso(a->f, &a->d, a->f->utente);
}

static void avviso_libera(void *p)
{
    AvvisoFlusso *a = p;
    free(a->frase);
    fl_lascia(a->f);
    g_free(a);
}

void ph_servizi_flusso_avviso(PhSrvFlusso *f, const PhSrvFlussoDati *d)
{
    if (f == NULL || d == NULL || atomic_load(&f->chiuso))
        return;
    AvvisoFlusso *a = g_new0(AvvisoFlusso, 1);
    atomic_fetch_add(&f->rif, 1);
    a->f = f;
    a->d = *d;
    a->frase = d->frase != NULL ? strdup(d->frase) : NULL;
    ph_shell_consegna(avviso_applica, a, avviso_libera);
}

/* ======================================================================
 * AGGIUNTE del 23 nov 2026 (servizi.h, in fondo).
 * ====================================================================== */

static void fl_lascia_esterno(struct PhSrvFlusso *f)
{
    fl_lascia(f);
}

static bool fl_chiuso(struct PhSrvFlusso *f)
{
    return atomic_load(&f->chiuso) != 0;
}

/* Testo da mostrare: UTF-8 valido, caratteri di controllo -> spazio, al
 * massimo `max` byte senza spezzare un carattere. Mai scritto nei log. */
static char *per_schermo(const char *t, size_t max)
{
    char *v = g_utf8_make_valid(t != NULL ? t : "", -1);
    for (char *c = v; *c != '\0'; c = g_utf8_next_char(c)) {
        gunichar u = g_utf8_get_char(c);
        if (g_unichar_iscntrl(u) || u == 0x2028 || u == 0x2029) {
            /* stessa lunghezza: si sostituisce byte per byte con spazi */
            const char *dopo = g_utf8_next_char(c);
            memset(c, ' ', (size_t)(dopo - c));
        }
    }
    if (strlen(v) > max) {
        const char *fine = g_utf8_find_prev_char(v, v + max + 1);
        size_t k = fine != NULL ? (size_t)(fine - v) : 0;
        v[k] = '\0';
    }
    char *r = strdup(v);
    g_free(v);
    return r;
}

/* ---- (1) Installazioni ---------------------------------------------- */
static bool nome_pacchetto_valido(const char *n)
{
    if (n == NULL || !g_ascii_isalpha(n[0]))
        return false;
    size_t k = 0;
    for (; n[k] != '\0'; k++)
        if (!(g_ascii_isalnum(n[k]) || n[k] == '_' || n[k] == '.') || k >= 255)
            return false;
    return k >= 1;
}

static void ris_applica_inst(Ris *r)
{
    PhSrvLavoro *l = r->l;
    PhServizi *s = l->s;
    PhSrvInstRisultato v = r->inst;
    if (r->inst_msg == NULL) {
        /* Chiusura generica del lavoro (backend fermato): errore. */
        v.esito = PH_SRV_INST_ERRORE;
        v.temporaneo_rimasto = false;
    }
    v.messaggio = r->inst_msg != NULL ? r->inst_msg
                : (r->frase != NULL ? r->frase : ph_t("Operazione non riuscita: il telefono non e' disponibile."));
    v.codice = r->inst_cod != NULL ? r->inst_cod : "";
    l->inst_fatto(s, &v, l->utente);
}

static void inst_chiudi_tutte(PhServizi *s)
{
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, s->inst);
    while (g_hash_table_iter_next(&it, &k, &v))
        atomic_store(&((PhSrvLavoro *)v)->flag_annulla, true);
}

typedef struct {
    PhSrvLavoro *l;
} AvanzamentoInst;

static void avanzamento_applica(void *p)
{
    PhSrvLavoro *l = p;
    atomic_store(&l->av_in_volo, false);
    if (chiusi(l->s) || atomic_load(&l->finito) || l->inst_avanzamento == NULL)
        return;
    l->inst_avanzamento(l->s, (uint64_t)atomic_load(&l->av_fatti), (uint64_t)atomic_load(&l->av_totale),
                        l->utente);
}

static void avanzamento_libera(void *p)
{
    lav_lascia(p);
}

void ph_servizi_lavoro_avanzamento(PhSrvLavoro *l, uint64_t fatti, uint64_t totale)
{
    if (l == NULL || atomic_load(&l->finito))
        return;
    atomic_store(&l->av_fatti, fatti);
    atomic_store(&l->av_totale, totale);
    if (atomic_exchange(&l->av_in_volo, true))
        return; /* una consegna e' gia' in volo: leggera' i valori nuovi */
    atomic_fetch_add(&l->rif, 1);
    ph_shell_consegna(avanzamento_applica, l, avanzamento_libera);
}

const atomic_bool *ph_servizi_lavoro_flag_annulla(PhSrvLavoro *l)
{
    return &l->flag_annulla;
}

void ph_servizi_lavoro_inst(PhSrvLavoro *l, const PhSrvInstRisultato *ri)
{
    if (ri == NULL) {
        ph_servizi_lavoro_fatto(l, PH_SRV_ESITO_ERRORE, NULL);
        return;
    }
    Ris *r = ris_nuovo(l, ri->esito == PH_SRV_INST_RIUSCITO ? PH_SRV_ESITO_OK : PH_SRV_ESITO_ERRORE, NULL, true);
    if (r == NULL)
        return;
    r->inst = *ri;
    if (r->inst.esito > PH_SRV_INST_ERRORE)
        r->inst.esito = PH_SRV_INST_ERRORE;
    r->inst_msg = per_schermo(ri->messaggio != NULL && ri->messaggio[0] != '\0'
                                  ? ri->messaggio
                                  : ph_t("Operazione non riuscita: il telefono non e' disponibile."),
                              1024);
    r->inst_cod = per_schermo(ri->codice, 95);
    ris_manda(r, true);
}

static guint inst_avvia(PhServizi *s, LavTipo tipo, const char *arg, bool valido, PhSrvInstAvanzamento av,
                        PhSrvInstFatto fatto, void *utente, bool disinstalla)
{
    g_return_val_if_fail(fatto != NULL, 0);
    PhSrvLavoro *l = lav_nuovo(s, tipo, NULL, utente);
    l->inst_avanzamento = av;
    l->inst_fatto = fatto;
    if (++s->prossima_inst == 0)
        s->prossima_inst = 1;
    l->id = s->prossima_inst;
    atomic_fetch_add(&l->rif, 1); /* riferimento di s->inst, tolto alla consegna finale */
    g_hash_table_insert(s->inst, GUINT_TO_POINTER(l->id), l);
    void (*fn)(void *, PhSrvLavoro *, const char *) = disinstalla ? s->vt.app_disinstalla : s->vt.apk_installa;
    if (!valido) {
        PhSrvInstRisultato ri = {PH_SRV_INST_ERRORE,
                                 disinstalla ? ph_t("Il nome dell'app non e' valido.")
                                             : ph_t("Il file da installare non e' valido."),
                                 "", false};
        ph_servizi_lavoro_inst(l, &ri);
        return 0; /* la risposta ERRORE arriva lo stesso */
    }
    if (fn == NULL) {
        PhSrvInstRisultato ri = {PH_SRV_INST_ERRORE, ph_t("Funzione non ancora disponibile."), "", false};
        ph_servizi_lavoro_inst(l, &ri);
        return l->id;
    }
    guint id = l->id;
    fn(s->b, l, arg);
    return id;
}

guint ph_srv_apk_installa(PhServizi *s, const char *apk_pc, PhSrvInstAvanzamento avanzamento,
                          PhSrvInstFatto fatto, void *utente)
{
    bool valido = apk_pc != NULL && apk_pc[0] == '/' && g_utf8_validate(apk_pc, -1, NULL);
    return inst_avvia(s, LAV_INST, apk_pc, valido, avanzamento, fatto, utente, false);
}

guint ph_srv_app_disinstalla(PhServizi *s, const char *pacchetto, PhSrvInstFatto fatto, void *utente)
{
    return inst_avvia(s, LAV_INST, pacchetto, nome_pacchetto_valido(pacchetto), NULL, fatto, utente, true);
}

void ph_srv_installazione_annulla(PhServizi *s, guint id)
{
    PhSrvLavoro *l = id != 0 ? g_hash_table_lookup(s->inst, GUINT_TO_POINTER(id)) : NULL;
    if (l != NULL)
        atomic_store(&l->flag_annulla, true);
}

/* ---- (3) Annullare un verso ------------------------------------------ */
void ph_srv_tr_annulla_verso(PhServizi *s, PhSrvVerso verso)
{
    if (s->vt.tr_annulla_verso != NULL) {
        s->vt.tr_annulla_verso(s->b, verso);
        return;
    }
    /* Ripiego: le voci della fotografia attuale, una per una. */
    for (size_t i = 0; i < s->n_tr && s->vt.tr_annulla != NULL; i++)
        if (s->tr[i].verso == verso && (s->tr[i].stato == PH_SRV_TR_ATTESA || s->tr[i].stato == PH_SRV_TR_CORSO))
            s->vt.tr_annulla(s->b, s->tr[i].id);
}

/* ---- (4) Pannello con esito ------------------------------------------ */
void ph_servizi_lavoro_pannello(PhSrvLavoro *l, PhSrvPannelloEsito esito, const char *frase)
{
    Ris *r = ris_nuovo(l, esito == PH_SRV_PAN_FATTO ? PH_SRV_ESITO_OK : PH_SRV_ESITO_ERRORE, frase, true);
    if (r == NULL)
        return;
    r->pannello = esito > PH_SRV_PAN_SENZA_ESITO ? PH_SRV_PAN_ERRORE : esito;
    r->pannello_detto = true;
    ris_manda(r, true);
}

void ph_srv_pannello_chiedi(PhServizi *s, bool acceso, GCancellable *annulla, PhSrvPannelloFatto fatto,
                            void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_PANNELLO, annulla, utente);
    l->pannello = fatto;
    if (s->vt.pannello_chiedi != NULL) {
        s->vt.pannello_chiedi(s->b, l, acceso);
    } else if (s->vt.pannello != NULL) {
        s->vt.pannello(s->b, acceso);
        ph_servizi_lavoro_pannello(l, PH_SRV_PAN_SENZA_ESITO, NULL);
    } else {
        ph_servizi_lavoro_pannello(l, PH_SRV_PAN_NON_DISPONIBILE, NULL);
    }
}

/* ---- (5) Memoria per categoria e calendario ---------------------------- */
void ph_servizi_lavoro_dettaglio(PhSrvLavoro *l, PhSrvEsito esito, const char *frase,
                                 const PhSrvMemoriaDettaglio *d)
{
    Ris *r = ris_nuovo(l, esito, frase, true);
    if (r == NULL)
        return;
    if (esito == PH_SRV_ESITO_OK && d != NULL) {
        r->dettaglio = *d;
        PhSrvVoceMemoria *v[4] = {&r->dettaglio.immagini, &r->dettaglio.video, &r->dettaglio.audio,
                                  &r->dettaglio.app};
        for (int i = 0; i < 4; i++)
            if (!v[i]->leggibile)
                v[i]->byte = 0;
        r->ha_dettaglio = true;
    }
    ris_manda(r, true);
}

void ph_servizi_lavoro_calendario(PhSrvLavoro *l, PhSrvEsito esito, const char *frase,
                                  const PhSrvEventoCalendario *v, size_t n)
{
    Ris *r = ris_nuovo(l, esito, frase, true);
    if (r == NULL)
        return;
    if (esito == PH_SRV_ESITO_OK && v != NULL && n > 0) {
        if (n > PH_SRV_CALENDARIO_MAX)
            n = PH_SRV_CALENDARIO_MAX;
        r->eventi = calloc(n, sizeof *r->eventi);
        r->stringhe = calloc(2 * n, sizeof *r->stringhe); /* ris_libera ne libera 2*n */
        if (r->eventi == NULL || r->stringhe == NULL) {
            free(r->eventi);
            r->eventi = NULL;
            r->esito = PH_SRV_ESITO_ERRORE;
        } else {
            r->n = n;
            for (size_t i = 0; i < n; i++) {
                r->stringhe[i] = per_schermo(v[i].titolo, 1024);
                r->eventi[i].titolo = r->stringhe[i] != NULL ? r->stringhe[i] : "";
                r->eventi[i].inizio_ms = v[i].inizio_ms;
                r->eventi[i].fine_ms = v[i].fine_ms < v[i].inizio_ms ? v[i].inizio_ms : v[i].fine_ms;
                r->eventi[i].tutto_il_giorno = v[i].tutto_il_giorno;
            }
        }
    }
    ris_manda(r, true);
}

void ph_srv_tel_memoria_dettaglio(PhServizi *s, GCancellable *annulla, PhSrvDettaglioFatto fatto, void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_DETTAGLIO, annulla, utente);
    l->dettaglio = fatto;
    if (s->vt.tel_memoria_dettaglio != NULL)
        s->vt.tel_memoria_dettaglio(s->b, l);
    else
        senza_backend(l);
}

void ph_srv_tel_calendario(PhServizi *s, unsigned massimo, GCancellable *annulla, PhSrvCalendarioFatto fatto,
                           void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_CALENDARIO, annulla, utente);
    l->calendario = fatto;
    if (massimo < 1)
        massimo = 1;
    if (massimo > PH_SRV_CALENDARIO_MAX)
        massimo = PH_SRV_CALENDARIO_MAX;
    if (s->vt.tel_calendario != NULL)
        s->vt.tel_calendario(s->b, l, massimo);
    else
        senza_backend(l);
}

/* ---- (6) Prima lettura delle notifiche ---------------------------------- */
unsigned ph_srv_notifiche_generazione(PhServizi *s) { return s->notif_gen; }
bool ph_srv_notifiche_pronte(PhServizi *s) { return s->notif_pronte; }

/* ---- (7) Screenshot e registrazione -------------------------------------- */
void ph_servizi_lavoro_screenshot(PhSrvLavoro *l, PhSrvEsito esito, const char *frase, const char *percorso,
                                  GBytes *png)
{
    Ris *r = ris_nuovo(l, esito, frase, true);
    if (r == NULL)
        return;
    if (esito == PH_SRV_ESITO_OK) {
        r->percorso = percorso != NULL ? per_schermo(percorso, 4096) : NULL;
        r->byte = png != NULL ? g_bytes_ref(png) : NULL;
        if (r->percorso == NULL && r->byte == NULL)
            r->esito = PH_SRV_ESITO_ERRORE;
    }
    ris_manda(r, true);
}

void ph_srv_flusso_screenshot(PhSrvFlusso *f, unsigned dove, const char *cartella_pc, const char *nome_app,
                              GCancellable *annulla, PhSrvScreenshotFatto fatto, void *utente)
{
    g_return_if_fail(f != NULL && fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(f->s, LAV_SCREENSHOT, annulla, utente);
    l->screenshot = fatto;
    atomic_fetch_add(&f->rif, 1);
    l->flusso = f;
    dove &= (PH_SRV_FOTO_FILE | PH_SRV_FOTO_APPUNTI);
    bool ok = dove != 0 && (!(dove & PH_SRV_FOTO_FILE) || (cartella_pc != NULL && cartella_pc[0] == '/'));
    if (ok && f->s->vt.flusso_screenshot != NULL && !atomic_load(&f->chiuso))
        f->s->vt.flusso_screenshot(f->s->b, f->fl, l, dove, cartella_pc,
                                   nome_app != NULL && nome_app[0] != '\0' ? nome_app : "YASDE");
    else
        ph_servizi_lavoro_screenshot(l, PH_SRV_ESITO_ERRORE,
                                     ok ? ph_t("Funzione non ancora disponibile.") : NULL, NULL, NULL);
}

typedef struct {
    PhSrvFlusso *f;
    PhSrvRegDati d;
    char *percorso, *frase;
    bool tocca_stato; /* false: rifiuto di una seconda registrazione, lo stato resta */
    bool proprio;     /* true: cb/ut propri (il rifiuto va a chi l'ha chiesto) */
    PhSrvRegAvviso cb;
    void *ut;
} AvvisoReg;

static void reg_applica(void *p)
{
    AvvisoReg *a = p;
    PhSrvFlusso *f = a->f;
    if (atomic_load(&f->chiuso) || chiusi(f->s))
        return;
    PhSrvRegAvviso cb = a->proprio ? a->cb : f->reg_avviso;
    void *ut = a->proprio ? a->ut : f->reg_utente;
    if (a->tocca_stato && (a->d.evento == PH_SRV_REG_FINITA || a->d.evento == PH_SRV_REG_ERRORE))
        f->registrando = false;
    a->d.percorso = a->percorso;
    a->d.frase = a->frase;
    if (cb != NULL)
        cb(f, &a->d, ut);
}

static void reg_libera(void *p)
{
    AvvisoReg *a = p;
    free(a->percorso);
    free(a->frase);
    fl_lascia(a->f);
    g_free(a);
}

static void reg_manda(PhSrvFlusso *f, const PhSrvRegDati *d, bool tocca_stato, PhSrvRegAvviso cb, void *ut)
{
    AvvisoReg *a = g_new0(AvvisoReg, 1);
    a->proprio = cb != NULL;
    a->cb = cb;
    a->ut = ut;
    atomic_fetch_add(&f->rif, 1);
    a->f = f;
    a->d = *d;
    a->percorso = d->percorso != NULL ? per_schermo(d->percorso, 4096) : NULL;
    a->frase = d->frase != NULL ? per_schermo(d->frase, 1024) : NULL;
    a->tocca_stato = tocca_stato;
    ph_shell_consegna(reg_applica, a, reg_libera);
}

void ph_servizi_flusso_registrazione(PhSrvFlusso *f, const PhSrvRegDati *d)
{
    if (f == NULL || d == NULL || atomic_load(&f->chiuso))
        return;
    reg_manda(f, d, true, NULL, NULL);
}

void ph_srv_flusso_registra(PhSrvFlusso *f, const char *cartella_pc, const char *nome_app, bool con_audio,
                            PhSrvRegAvviso avviso, void *utente)
{
    g_return_if_fail(f != NULL);
    if (f->registrando) {
        PhSrvRegDati d = {PH_SRV_REG_ERRORE, NULL, ph_t("La registrazione e' gia' in corso.")};
        /* Il rifiuto va a chi lo ha chiesto, senza toccare la registrazione in corso. */
        if (avviso != NULL)
            reg_manda(f, &d, false, avviso, utente);
        return;
    }
    f->reg_avviso = avviso;
    f->reg_utente = utente;
    if (cartella_pc == NULL || cartella_pc[0] != '/' || f->s->vt.flusso_registra == NULL ||
        atomic_load(&f->chiuso)) {
        PhSrvRegDati d = {PH_SRV_REG_ERRORE, NULL,
                          f->s->vt.flusso_registra == NULL ? ph_t("Funzione non ancora disponibile.") : NULL};
        f->registrando = true; /* lo azzera la consegna dell'ERRORE */
        reg_manda(f, &d, true, NULL, NULL);
        return;
    }
    f->registrando = true;
    f->s->vt.flusso_registra(f->s->b, f->fl, true, cartella_pc,
                             nome_app != NULL && nome_app[0] != '\0' ? nome_app : "YASDE", con_audio);
}

void ph_srv_flusso_registra_ferma(PhSrvFlusso *f)
{
    if (f == NULL || !f->registrando || f->s->vt.flusso_registra == NULL)
        return;
    f->s->vt.flusso_registra(f->s->b, f->fl, false, NULL, NULL, false);
}

bool ph_srv_flusso_registrando(PhSrvFlusso *f)
{
    return f != NULL && f->registrando;
}

/* ---- (8) Fotogramma chiave ----------------------------------------------- */
void ph_srv_flusso_chiave(PhSrvFlusso *f)
{
    if (f != NULL && f->s->vt.flusso_chiave != NULL && !atomic_load(&f->chiuso))
        f->s->vt.flusso_chiave(f->s->b, f->fl);
}

/* ---- (9) Pagina su un flusso aperto (27 nov) ------------------------------ */
void ph_srv_flusso_pagina(PhSrvFlusso *f, uint8_t pagina)
{
    if (f == NULL)
        return;
    if (pagina == PH_SRV_PAGINA_NESSUNA || pagina > PH_SRV_PAGINA_MEMORIA) {
        fprintf(stderr, "servizi: pagina %u fuori elenco, scartata\n", (unsigned)pagina);
        return;
    }
    if (f->s->vt.flusso_pagina != NULL && !atomic_load(&f->chiuso))
        f->s->vt.flusso_pagina(f->s->b, f->fl, pagina);
}

/* ---- Densita' nel ridimensionamento (23 nov) ----------------------------- */
void ph_srv_flusso_ridimensiona_densita(PhSrvFlusso *f, int larghezza, int altezza, int densita)
{
    if (f == NULL)
        return;
    if (densita != 0 && (densita < 50 || densita > 2000)) {
        fprintf(stderr, "servizi: ridimensionamento con densita' %d scartato (0 o 50-2000)\n", densita);
        return;
    }
    if (f->s->vt.flusso_ridimensiona_densita == NULL) {
        ph_srv_flusso_ridimensiona(f, larghezza, altezza); /* ripiego: densita' ignorata */
        return;
    }
    if (larghezza < 64 || larghezza > 2560 || altezza < 64 || altezza > 2560 || larghezza % 8 != 0 ||
        altezza % 8 != 0) {
        fprintf(stderr, "servizi: ridimensionamento %dx%d scartato (multipli di 8, 64-2560)\n", larghezza,
                altezza);
        return;
    }
    if (f->registrando) {
        fprintf(stderr, "servizi: ridimensionamento ignorato durante la registrazione\n");
        return;
    }
    if (!atomic_load(&f->chiuso))
        f->s->vt.flusso_ridimensiona_densita(f->s->b, f->fl, larghezza, altezza, densita);
}

/* ---- Prima connessione: abbinamento (23 nov) ------------------------------- */
bool ph_srv_codice_valido(const char *codice)
{
    if (codice == NULL)
        return false;
    for (int i = 0; i < 6; i++)
        if (codice[i] < '0' || codice[i] > '9')
            return false;
    return codice[6] == '\0';
}

static bool host_valido(const char *h)
{
    size_t n = h != NULL ? strlen(h) : 0;
    if (n == 0 || n > 63)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!(g_ascii_isxdigit(h[i]) || h[i] == '.' || h[i] == ':' || g_ascii_isalnum(h[i]) || h[i] == '-' ||
              h[i] == '%'))
            return false;
    return true;
}

void ph_servizi_lavoro_abbina(PhSrvLavoro *l, PhSrvAbbEsito esito, const char *frase)
{
    Ris *r = ris_nuovo(l, esito == PH_SRV_ABB_RIUSCITO ? PH_SRV_ESITO_OK : PH_SRV_ESITO_ERRORE, frase, true);
    if (r == NULL)
        return;
    r->abb = esito > PH_SRV_ABB_NON_DISPONIBILE ? PH_SRV_ABB_FALLITO : esito;
    r->abb_detto = true;
    ris_manda(r, true);
}

void ph_srv_abbina(PhServizi *s, const char *codice, const char *host, int porta, GCancellable *annulla,
                   PhSrvAbbFatto fatto, void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_ABBINA, annulla, utente);
    l->abbina = fatto;
    s->abbinamenti++; /* lo toglie la consegna finale (ris_libera) */
    bool valido = ph_srv_codice_valido(codice) && (host == NULL || (host_valido(host) && porta >= 1 && porta <= 65535));
    if (!valido || s->abbinamenti > 1) {
        ph_servizi_lavoro_abbina(l, PH_SRV_ABB_RIFIUTATA, NULL);
        return;
    }
    if (s->vt.abbina == NULL) {
        ph_servizi_lavoro_abbina(l, PH_SRV_ABB_NON_DISPONIBILE, NULL);
        return;
    }
    /* Copia nostra, cancellata subito dopo: il codice non resta in memoria
     * del pezzo comune (il backend copia cio' che gli serve). */
    char c[7];
    memcpy(c, codice, 7);
    char h[64] = {0};
    if (host != NULL)
        g_strlcpy(h, host, sizeof h);
    s->vt.abbina(s->b, l, c, host != NULL ? h : NULL, host != NULL ? porta : 0);
    explicit_bzero(c, sizeof c);
    explicit_bzero(h, sizeof h);
}

bool ph_srv_abbinamento_in_corso(PhServizi *s)
{
    return s->abbinamenti > 0;
}

/* ---- Widget del 23 nov pomeriggio ---------------------------------------- */
void ph_servizi_lavoro_calendario_intervallo(PhSrvLavoro *l, PhSrvEsito esito, const char *frase,
                                             const PhSrvEventoCalendario *v, size_t n, bool troncato,
                                             const char *app_calendario)
{
    /* Gli eventi si copiano come per il calendario semplice (titoli ripuliti). */
    Ris *r = ris_nuovo(l, esito, frase, true);
    if (r == NULL)
        return;
    if (esito == PH_SRV_ESITO_OK) {
        r->troncato = troncato;
        r->app_cal = per_schermo(app_calendario != NULL ? app_calendario : "", 255);
        if (v != NULL && n > 0) {
            if (n > PH_SRV_CALENDARIO_INTERVALLO_MAX) {
                n = PH_SRV_CALENDARIO_INTERVALLO_MAX;
                r->troncato = true;
            }
            r->eventi = calloc(n, sizeof *r->eventi);
            r->stringhe = calloc(2 * n, sizeof *r->stringhe);
            if (r->eventi == NULL || r->stringhe == NULL) {
                free(r->eventi);
                r->eventi = NULL;
                r->esito = PH_SRV_ESITO_ERRORE;
            } else {
                r->n = n;
                for (size_t i = 0; i < n; i++) {
                    r->stringhe[i] = per_schermo(v[i].titolo, 1024);
                    r->eventi[i].titolo = r->stringhe[i] != NULL ? r->stringhe[i] : "";
                    r->eventi[i].inizio_ms = v[i].inizio_ms;
                    r->eventi[i].fine_ms = v[i].fine_ms < v[i].inizio_ms ? v[i].inizio_ms : v[i].fine_ms;
                    r->eventi[i].tutto_il_giorno = v[i].tutto_il_giorno;
                }
            }
        }
    }
    ris_manda(r, true);
}

void ph_servizi_lavoro_sveglia(PhSrvLavoro *l, PhSrvEsito esito, const char *frase, bool presente, uint64_t ora_ms,
                               const char *pacchetto)
{
    Ris *r = ris_nuovo(l, esito, frase, true);
    if (r == NULL)
        return;
    if (esito == PH_SRV_ESITO_OK) {
        r->presente = presente;
        r->ora = presente ? ora_ms : 0;
        r->app_cal = per_schermo(pacchetto != NULL ? pacchetto : "", 255);
    }
    ris_manda(r, true);
}

void ph_servizi_lavoro_batteria(PhSrvLavoro *l, PhSrvEsito esito, const char *frase, const PhSrvBatteria *b)
{
    Ris *r = ris_nuovo(l, esito, frase, true);
    if (r == NULL)
        return;
    if (esito == PH_SRV_ESITO_OK && b != NULL) {
        r->bat = *b;
        r->bat.livello = CLAMP(r->bat.livello, 0, 100);
        if (r->bat.stato > PH_SRV_BAT_PIENA)
            r->bat.stato = PH_SRV_BAT_SCONOSCIUTO;
        if (r->bat.salute > PH_SRV_SALUTE_ERRORE)
            r->bat.salute = PH_SRV_SALUTE_SCONOSCIUTA;
        r->bat.alimentazione &= 0xfu;
        r->bat.temperatura_decimi = CLAMP(r->bat.temperatura_decimi, -500, 1500);
        if (r->bat.minuti_carica < -1)
            r->bat.minuti_carica = -1;
        if (r->bat.minuti_autonomia < -1)
            r->bat.minuti_autonomia = -1;
        r->ha_batteria = true;
    }
    ris_manda(r, true);
}

void ph_srv_tel_calendario_intervallo(PhServizi *s, uint64_t da_ms, uint64_t a_ms, GCancellable *annulla,
                                      PhSrvCalendarioMeseFatto fatto, void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_CAL_INTERVALLO, annulla, utente);
    l->cal_mese = fatto;
    bool ok = a_ms > da_ms && a_ms - da_ms <= (uint64_t)PH_SRV_CALENDARIO_GIORNI_MAX * 86400000u;
    if (ok && s->vt.tel_calendario_intervallo != NULL)
        s->vt.tel_calendario_intervallo(s->b, l, da_ms, a_ms);
    else
        senza_backend(l);
}

void ph_srv_tel_sveglia(PhServizi *s, GCancellable *annulla, PhSrvSvegliaFatta fatto, void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_SVEGLIA, annulla, utente);
    l->sveglia = fatto;
    if (s->vt.tel_sveglia != NULL)
        s->vt.tel_sveglia(s->b, l);
    else
        senza_backend(l);
}

void ph_srv_tel_batteria(PhServizi *s, GCancellable *annulla, PhSrvBatteriaFatta fatto, void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_BATTERIA, annulla, utente);
    l->batteria = fatto;
    if (s->vt.tel_batteria != NULL)
        s->vt.tel_batteria(s->b, l);
    else
        senza_backend(l);
}

void ph_servizi_pubblica_decodifica_software(PhServizi *s)
{
    pub_manda(pub_nuova(s, PUB_DECODIFICA));
}

void ph_servizi_pubblica_controlli(PhServizi *s, const PhSrvControlli *c)
{
    if (c == NULL)
        return;
    Pub *u = pub_nuova(s, PUB_CONTROLLI);
    u->ctl = *c;
    /* Valori fuori elenco = non disponibili. */
    if (u->ctl.suoneria < PH_SRV_NON_DISPONIBILE || u->ctl.suoneria > PH_SRV_SUONERIA_SILENZIOSO)
        u->ctl.suoneria = PH_SRV_NON_DISPONIBILE;
    if (u->ctl.non_disturbare < PH_SRV_NON_DISPONIBILE || u->ctl.non_disturbare > 1)
        u->ctl.non_disturbare = PH_SRV_NON_DISPONIBILE;
    if (u->ctl.torcia < PH_SRV_NON_DISPONIBILE || u->ctl.torcia > 1)
        u->ctl.torcia = PH_SRV_NON_DISPONIBILE;
    pub_manda(u);
}

void ph_srv_controlli(PhServizi *s, PhSrvControlli *out) { *out = s->ctl; }

void ph_srv_controlli_osserva(PhServizi *s, bool osserva)
{
    if (osserva) {
        if (s->osservatori_ctl++ == 0 && s->vt.controlli_osserva != NULL)
            s->vt.controlli_osserva(s->b, true);
    } else if (s->osservatori_ctl > 0) {
        if (--s->osservatori_ctl == 0 && s->vt.controlli_osserva != NULL)
            s->vt.controlli_osserva(s->b, false);
    }
}

void ph_srv_controllo_imposta(PhServizi *s, PhSrvControllo controllo, int valore, GCancellable *annulla,
                              PhSrvFatto fatto, void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_FATTO, annulla, utente);
    l->fatto = fatto;
    bool ok = (controllo == PH_SRV_CTL_SUONERIA && valore >= PH_SRV_SUONERIA_SUONA &&
               valore <= PH_SRV_SUONERIA_SILENZIOSO) ||
              ((controllo == PH_SRV_CTL_NON_DISTURBARE || controllo == PH_SRV_CTL_TORCIA) && (valore == 0 || valore == 1));
    if (ok && s->vt.controllo_imposta != NULL)
        s->vt.controllo_imposta(s->b, l, controllo, valore);
    else
        senza_backend(l);
}

void ph_srv_trova_avvia(PhServizi *s, int secondi, GCancellable *annulla, PhSrvFatto fatto, void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_FATTO, annulla, utente);
    l->fatto = fatto;
    if (s->vt.trova_avvia != NULL)
        s->vt.trova_avvia(s->b, l, CLAMP(secondi, 5, 120));
    else
        senza_backend(l);
}

void ph_srv_trova_ferma(PhServizi *s)
{
    if (s->vt.trova_ferma != NULL)
        s->vt.trova_ferma(s->b);
}

/* Trasferimenti recenti: i completati nuovi della fotografia appena applicata
 * vanno in testa (thread GTK). */
static void recente_libera(PhSrvTrasferimento *t)
{
    g_free(t->nome);
    g_free(t->origine);
    g_free(t->destinazione);
    g_free(t->errore);
    memset(t, 0, sizeof *t);
}

static void recenti_svuota(PhServizi *s)
{
    for (size_t i = 0; i < s->n_recenti; i++)
        recente_libera(&s->recenti[i]);
    s->n_recenti = 0;
    g_hash_table_remove_all(s->completati_visti);
}

static void recenti_aggiorna(PhServizi *s)
{
    for (size_t i = 0; i < s->n_tr; i++) {
        const PhSrvTrasferimento *t = &s->tr[i];
        if (t->stato != PH_SRV_TR_COMPLETATO)
            continue;
        char chiave[24];
        snprintf(chiave, sizeof chiave, "%llu", (unsigned long long)t->id);
        if (g_hash_table_contains(s->completati_visti, chiave))
            continue;
        g_hash_table_add(s->completati_visti, g_strdup(chiave));
        if (s->n_recenti == PH_SRV_RECENTI_MAX)
            recente_libera(&s->recenti[--s->n_recenti]);
        memmove(&s->recenti[1], &s->recenti[0], s->n_recenti * sizeof s->recenti[0]);
        PhSrvTrasferimento *c = &s->recenti[0];
        *c = *t;
        c->nome = g_strdup(t->nome != NULL ? t->nome : "");
        c->origine = g_strdup(t->origine != NULL ? t->origine : "");
        c->destinazione = g_strdup(t->destinazione != NULL ? t->destinazione : "");
        c->errore = g_strdup("");
        s->n_recenti++;
    }
}

const PhSrvTrasferimento *ph_srv_trasferimenti_recenti(PhServizi *s, size_t *n)
{
    *n = s->n_recenti;
    return s->recenti;
}

/* ---- Pizzico (23 nov) ------------------------------------------------------ */
void ph_srv_flusso_pizzico(PhSrvFlusso *f, int video_w, int video_h, int fase, int x, int y, int passo)
{
    static bool detto;
    if (f == NULL || fase < PH_SRV_PIZZICO_INIZIO || fase > PH_SRV_PIZZICO_ANNULLA ||
        !coordinate_valide(video_w, video_h, x, y) || atomic_load(&f->chiuso))
        return;
    if (f->s->vt.flusso_pizzico == NULL) {
        if (!detto)
            fprintf(stderr, "servizi: pizzico non disponibile in questo backend\n");
        detto = true;
        return;
    }
    f->s->vt.flusso_pizzico(f->s->b, f->fl, video_w, video_h, fase, x, y, CLAMP(passo, -1200, 1200));
}

/* ---- Meteo (23 nov sera) ---------------------------------------------------- */
void ph_servizi_pubblica_meteo(PhServizi *s, const PhSrvMeteo *m)
{
    if (m == NULL)
        return;
    ph_servizi_pubblica_meteo_fonte(s, m, m->ha_dati ? PH_SRV_METEO_FONTE_TELEFONO : PH_SRV_METEO_FONTE_NESSUNA);
}

void ph_servizi_pubblica_meteo_fonte(PhServizi *s, const PhSrvMeteo *m, PhSrvMeteoFonte fonte)
{
    if (m == NULL)
        return;
    Pub *u = pub_nuova(s, PUB_METEO);
    u->fonte = fonte > PH_SRV_METEO_FONTE_A_MANO ? PH_SRV_METEO_FONTE_NESSUNA : fonte;
    u->meteo = g_new(PhSrvMeteo, 1);
    *u->meteo = *m;
    /* Testi che vengono dal telefono o dalla rete: ripuliti per lo schermo
     * (UTF-8 valido, niente caratteri di controllo, troncati a un carattere
     * intero) come ogni altro testo, prima di arrivare a GTK. */
    u->meteo->frase[sizeof u->meteo->frase - 1] = '\0';
    u->meteo->localita[sizeof u->meteo->localita - 1] = '\0';
    char *frase = per_schermo(u->meteo->frase, sizeof u->meteo->frase - 1);
    char *loc = per_schermo(u->meteo->localita, sizeof u->meteo->localita - 1);
    g_strlcpy(u->meteo->frase, frase != NULL ? frase : "", sizeof u->meteo->frase);
    g_strlcpy(u->meteo->localita, loc != NULL ? loc : "", sizeof u->meteo->localita);
    free(frase);
    free(loc);
    if (u->meteo->stato > PH_SRV_METEO_NON_COLLEGATO)
        u->meteo->stato = PH_SRV_METEO_NON_DISPONIBILE;
    if (u->meteo->dati.n_ore > PH_METEO_ORE_MAX)
        u->meteo->dati.n_ore = PH_METEO_ORE_MAX;
    if (u->meteo->dati.n_giorni > PH_METEO_GIORNI_MAX)
        u->meteo->dati.n_giorni = PH_METEO_GIORNI_MAX;
    pub_manda(u);
}

const PhSrvMeteo *ph_srv_meteo(PhServizi *s) { return &s->meteo; }

/* Cambio di stato deciso qui: consegnato come una pubblicazione (l'avviso non
 * parte mai dentro la chiamata, regola 3 di servizi.h). */
static void meteo_stato(PhServizi *s, PhSrvMeteoStato stato, const char *frase, bool forza)
{
    Pub *u = pub_nuova(s, PUB_METEO);
    u->meteo = g_new(PhSrvMeteo, 1);
    *u->meteo = s->meteo; /* i dati vecchi restano (solo in memoria) */
    u->fonte = s->meteo_fonte;
    u->meteo->stato = stato;
    g_strlcpy(u->meteo->frase, frase != NULL ? frase : "", sizeof u->meteo->frase);
    u->forza = forza;
    pub_manda(u);
}

void ph_srv_meteo_osserva(PhServizi *s, bool osserva)
{
    if (osserva) {
        if (s->osservatori_meteo++ > 0)
            return;
        if (s->vt.meteo_osserva != NULL) {
            meteo_stato(s, PH_SRV_METEO_IN_ATTESA, NULL, false);
            s->vt.meteo_osserva(s->b, true);
        } else {
            meteo_stato(s, PH_SRV_METEO_NON_DISPONIBILE, ph_t("Funzione non ancora disponibile."), false);
        }
    } else if (s->osservatori_meteo > 0) {
        if (--s->osservatori_meteo > 0)
            return;
        if (s->vt.meteo_osserva != NULL)
            s->vt.meteo_osserva(s->b, false);
        meteo_stato(s, PH_SRV_METEO_SPENTO, NULL, true);
    }
}

/* ---- Meteo: citta' scelta a mano e ricerca dei luoghi (23 nov notte) ------ */
PhSrvMeteoFonte ph_srv_meteo_fonte(PhServizi *s) { return s->meteo_fonte; }

static bool luogo_valido(const PhMeteoLuogo *l)
{
    return l != NULL && l->nome[0] != '\0' && l->lat_centesimi >= -9000 && l->lat_centesimi <= 9000 &&
           l->lon_centesimi >= -18000 && l->lon_centesimi <= 18000;
}

/* Copia ripulita per lo schermo (stringhe sempre terminate, UTF-8 valido). */
static void luogo_ripulito(const PhMeteoLuogo *da, PhMeteoLuogo *a)
{
    *a = (PhMeteoLuogo){.lat_centesimi = da->lat_centesimi, .lon_centesimi = da->lon_centesimi};
    char nome[sizeof da->nome + 1], dett[sizeof da->dettaglio + 1];
    memcpy(nome, da->nome, sizeof da->nome);
    nome[sizeof da->nome] = '\0';
    memcpy(dett, da->dettaglio, sizeof da->dettaglio);
    dett[sizeof da->dettaglio] = '\0';
    char *n = per_schermo(nome, sizeof a->nome - 1);
    char *d = per_schermo(dett, sizeof a->dettaglio - 1);
    g_strlcpy(a->nome, n != NULL ? n : "", sizeof a->nome);
    g_strlcpy(a->dettaglio, d != NULL ? d : "", sizeof a->dettaglio);
    free(n);
    free(d);
}

void ph_srv_meteo_localita(PhServizi *s, const PhMeteoLuogo *l)
{
    if (s->vt.meteo_localita == NULL)
        return;
    if (!luogo_valido(l)) {
        s->vt.meteo_localita(s->b, NULL);
        return;
    }
    PhMeteoLuogo c;
    luogo_ripulito(l, &c);
    s->vt.meteo_localita(s->b, c.nome[0] != '\0' ? &c : NULL);
}

void ph_srv_meteo_cerca(PhServizi *s, const char *testo, const char *lingua, GCancellable *annulla,
                        PhSrvMeteoCercaFatta fatto, void *utente)
{
    g_return_if_fail(fatto != NULL);
    PhSrvLavoro *l = lav_nuovo(s, LAV_LUOGHI, annulla, utente);
    l->luoghi = fatto;
    if (s->vt.meteo_cerca == NULL) {
        ph_servizi_lavoro_fatto(l, PH_SRV_ESITO_ERRORE, ph_t("Funzione non ancora disponibile."));
        return;
    }
    /* CONTRATTI.md §18: senza un widget Meteo sul desktop non parte nessun
     * collegamento, nemmeno la ricerca. Qui, per valere con ogni backend; la
     * risposta arriva come sempre dopo il ritorno (ph_shell_consegna). */
    if (s->osservatori_meteo == 0) {
        ph_servizi_lavoro_fatto(l, PH_SRV_ESITO_ERRORE, ph_t("Aggiungi prima il widget Meteo."));
        return;
    }
    char *t = testo != NULL && g_utf8_validate(testo, -1, NULL) ? g_strstrip(g_strdup(testo)) : NULL;
    bool lingua_ok = lingua != NULL && (strcmp(lingua, "it") == 0 || strcmp(lingua, "en") == 0);
    bool controllo = false; /* caratteri di controllo dentro il testo: rifiutato qui */
    for (const char *c = t; c != NULL && *c != '\0'; c = g_utf8_next_char(c)) {
        gunichar u = g_utf8_get_char(c);
        if (g_unichar_iscntrl(u) || u == 0x2028 || u == 0x2029)
            controllo = true;
    }
    if (t == NULL || controllo || strlen(t) < 2 || strlen(t) > PH_METEO_RICERCA_TESTO_MAX || !lingua_ok) {
        g_free(t);
        ph_servizi_lavoro_fatto(l, PH_SRV_ESITO_ERRORE, ph_t("Scrivi il nome di una localita' (almeno due lettere)."));
        return;
    }
    s->vt.meteo_cerca(s->b, l, t, lingua);
    g_free(t);
}

#define LUOGHI_ESAMINATI_MAX 100

void ph_servizi_lavoro_luoghi(PhSrvLavoro *l, PhSrvEsito esito, const char *frase, const PhMeteoLuogo *v, size_t n)
{
    Ris *r = ris_nuovo(l, esito, frase, true);
    if (r == NULL)
        return;
    if (esito == PH_SRV_ESITO_OK) {
        if (v == NULL)
            n = 0;
        /* Prima si scartano i non validi, poi si tiene al massimo
         * PH_METEO_LUOGHI_MAX; si esaminano al massimo LUOGHI_ESAMINATI_MAX
         * elementi contro liste enormi. */
        if (n > LUOGHI_ESAMINATI_MAX)
            n = LUOGHI_ESAMINATI_MAX;
        r->luoghi = calloc(PH_METEO_LUOGHI_MAX, sizeof *r->luoghi); /* mai NULL con OK, anche con 0 luoghi */
        if (r->luoghi == NULL) {
            r->esito = PH_SRV_ESITO_ERRORE;
        } else {
            size_t k = 0;
            for (size_t i = 0; i < n && k < PH_METEO_LUOGHI_MAX; i++) {
                if (!luogo_valido(&v[i]))
                    continue;
                luogo_ripulito(&v[i], &r->luoghi[k]);
                if (r->luoghi[k].nome[0] != '\0')
                    k++;
            }
            r->n = k;
        }
    }
    ris_manda(r, true);
}

/* ---- Chiamate recenti, rete, in riproduzione (27 nov) ------------------- */
/* Stringa che arriva dal telefono: sempre terminata e UTF-8 valido (tagliata
 * al primo byte non valido). */
static void str_pulita(char *b, size_t cap)
{
    b[cap - 1] = '\0';
    const char *fine = NULL;
    if (!g_utf8_validate(b, -1, &fine))
        b[fine - b] = '\0';
}

static PhSrvLettura let_valida(PhSrvLettura l)
{
    return (unsigned)l <= PH_SRV_LET_NEGATO ? l : PH_SRV_LET_NON_DISPONIBILE;
}

void ph_servizi_pubblica_chiamate(PhServizi *s, const PhSrvChiamate *c)
{
    if (c == NULL)
        return;
    Pub *u = pub_nuova(s, PUB_CHIAMATE);
    PhSrvChiamate *d = g_new(PhSrvChiamate, 1);
    *d = *c;
    d->stato = let_valida(d->stato);
    if (d->n > PH_SRV_CHIAMATE_MAX)
        d->n = PH_SRV_CHIAMATE_MAX;
    if (d->stato != PH_SRV_LET_OK)
        d->n = 0;
    for (size_t i = 0; i < d->n; i++) {
        if (d->v[i].tipo > PH_SRV_CHIAMATA_RIFIUTATA)
            d->v[i].tipo = PH_SRV_CHIAMATA_ALTRO;
        str_pulita(d->v[i].nome, sizeof d->v[i].nome);
        str_pulita(d->v[i].numero, sizeof d->v[i].numero);
    }
    u->dat = d;
    pub_manda(u);
}

void ph_servizi_pubblica_rete(PhServizi *s, const PhSrvReteDettaglio *r)
{
    if (r == NULL)
        return;
    Pub *u = pub_nuova(s, PUB_RETE);
    PhSrvReteDettaglio *d = g_new(PhSrvReteDettaglio, 1);
    *d = *r;
    d->stato = let_valida(d->stato);
    if (d->wifi > PH_SRV_WIFI_CONNESSO)
        d->wifi = PH_SRV_WIFI_SPENTO;
    if (d->mobile > PH_SRV_MOBILE_IN_SERVIZIO)
        d->mobile = PH_SRV_MOBILE_ASSENTE;
    if (d->wifi_livello > 4)
        d->wifi_livello = 4;
    if (d->mobile_livello > 4)
        d->mobile_livello = 4;
    if (d->generazione != 2 && d->generazione != 3 && d->generazione != 4 && d->generazione != 5)
        d->generazione = 0;
    if (d->dati_mobili > PH_SRV_TRI_IGNOTO)
        d->dati_mobili = PH_SRV_TRI_IGNOTO;
    if (d->aereo > PH_SRV_TRI_IGNOTO)
        d->aereo = PH_SRV_TRI_IGNOTO;
    str_pulita(d->wifi_nome, sizeof d->wifi_nome);
    str_pulita(d->operatore, sizeof d->operatore);
    u->dat = d;
    pub_manda(u);
}

void ph_servizi_pubblica_media(PhServizi *s, const PhSrvMedia *m)
{
    if (m == NULL)
        return;
    Pub *u = pub_nuova(s, PUB_MEDIA);
    PhSrvMedia *d = g_new(PhSrvMedia, 1);
    *d = *m;
    d->stato_lettura = let_valida(d->stato_lettura);
    if (d->stato > PH_SRV_MEDIA_RIPRODUZIONE)
        d->stato = PH_SRV_MEDIA_NESSUNA;
    d->comandi &= PH_SRV_MEDIA_PUO_PLAY_PAUSA | PH_SRV_MEDIA_PUO_AVANTI | PH_SRV_MEDIA_PUO_INDIETRO;
    str_pulita(d->titolo, sizeof d->titolo);
    str_pulita(d->artista, sizeof d->artista);
    str_pulita(d->pacchetto, sizeof d->pacchetto);
    u->dat = d;
    pub_manda(u);
}

const PhSrvChiamate *ph_srv_chiamate(PhServizi *s) { return &s->chiamate; }
const PhSrvReteDettaglio *ph_srv_rete(PhServizi *s) { return &s->rete; }
const PhSrvMedia *ph_srv_media(PhServizi *s) { return &s->media; }

/* Contatore comune alle tre voci. Al primo osservatore lo stato torna «in
 * attesa» (senza evento: chi aggiunge il widget legge lo stato) e, se il
 * backend non sa leggere, si pubblica subito «non disponibile». */
static void osserva_voce(PhServizi *s, bool osserva, int *cont, void (*vt)(void *, bool),
                         void (*azzera)(PhServizi *), void (*non_disp)(PhServizi *))
{
    if (osserva) {
        if ((*cont)++ > 0)
            return;
        azzera(s);
        if (vt != NULL)
            vt(s->b, true);
        else
            non_disp(s);
    } else if (*cont > 0) {
        if (--(*cont) == 0 && vt != NULL)
            vt(s->b, false);
    }
}

static void chi_azzera(PhServizi *s) { s->chiamate = (PhSrvChiamate){.stato = PH_SRV_LET_ATTESA}; }
static void rete_azzera(PhServizi *s) { s->rete = (PhSrvReteDettaglio){.stato = PH_SRV_LET_ATTESA}; }
static void media_azzera(PhServizi *s) { s->media = (PhSrvMedia){.stato_lettura = PH_SRV_LET_ATTESA}; }
static void chi_non_disp(PhServizi *s)
{
    PhSrvChiamate c = {.stato = PH_SRV_LET_NON_DISPONIBILE};
    ph_servizi_pubblica_chiamate(s, &c);
}
static void rete_non_disp(PhServizi *s)
{
    PhSrvReteDettaglio r = {.stato = PH_SRV_LET_NON_DISPONIBILE};
    ph_servizi_pubblica_rete(s, &r);
}
static void media_non_disp(PhServizi *s)
{
    PhSrvMedia m = {.stato_lettura = PH_SRV_LET_NON_DISPONIBILE};
    ph_servizi_pubblica_media(s, &m);
}

void ph_srv_chiamate_osserva(PhServizi *s, bool osserva)
{
    osserva_voce(s, osserva, &s->osservatori_chi, s->vt.chiamate_osserva, chi_azzera, chi_non_disp);
}

void ph_srv_rete_osserva(PhServizi *s, bool osserva)
{
    osserva_voce(s, osserva, &s->osservatori_rete, s->vt.rete_osserva, rete_azzera, rete_non_disp);
}

void ph_srv_media_osserva(PhServizi *s, bool osserva)
{
    osserva_voce(s, osserva, &s->osservatori_media, s->vt.media_osserva, media_azzera, media_non_disp);
}

void ph_srv_media_comando(PhServizi *s, int comando)
{
    if (comando < PH_SRV_MEDIA_CMD_PLAY_PAUSA || comando > PH_SRV_MEDIA_CMD_INDIETRO || chiusi(s))
        return;
    if (s->vt.media_comando != NULL)
        s->vt.media_comando(s->b, comando);
}
