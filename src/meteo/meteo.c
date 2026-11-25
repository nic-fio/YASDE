/* Oggetto PhMeteo: un thread proprio che esegue le richieste una alla volta,
 * con la regola di frequenza (cache 30 minuti, nuovi tentativi dopo un
 * fallimento a 5, 15, 30 minuti) e la chiusura sicura in parallelo.
 *
 * Disegno: tutto e' guidato dalle chiamate a ph_meteo_chiedi. Non ci sono
 * timer: un fallimento registra solo «non prima di quando»; chi chiede prima
 * riceve subito l'errore precedente, senza collegarsi. Cosi' non esistono
 * raffiche ne' collegamenti senza una richiesta in corso.
 *
 * Riservatezza: le coordinate vivono solo in memoria (richiesta in corso e
 * cache), mai in log, su disco o nei messaggi d'errore. */
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "interno.h"

/* ---- Orologi e limite (sostituibili dalle prove) ---------------------------- */

static _Atomic(int64_t (*)(void)) g_mono_s;
static _Atomic(int64_t (*)(void)) g_unix_s;
static _Atomic int64_t g_limite_ms;
static _Atomic int64_t g_avvii_ms = -1; /* < 0: predefinito */
static _Atomic int64_t g_pausa_ric_ms = -1; /* < 0: predefinito */

int64_t ph_meteo_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int64_t ph_meteo_mono_s(void)
{
    int64_t (*f)(void) = atomic_load(&g_mono_s);
    if (f != NULL)
        return f();
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec;
}

int64_t ph_meteo_unix_s(void)
{
    int64_t (*f)(void) = atomic_load(&g_unix_s);
    if (f != NULL)
        return f();
    return (int64_t)time(NULL);
}

void ph_meteo_prova_orologio(int64_t (*mono_s)(void), int64_t (*unix_s)(void))
{
    atomic_store(&g_mono_s, mono_s);
    atomic_store(&g_unix_s, unix_s);
}

void ph_meteo_prova_limite_ms(int64_t ms) { atomic_store(&g_limite_ms, ms); }

void ph_meteo_prova_avvii_ms(int64_t ms) { atomic_store(&g_avvii_ms, ms); }

void ph_meteo_prova_pausa_ricerca_ms(int64_t ms) { atomic_store(&g_pausa_ric_ms, ms); }

/* Pausa tra l'avvio di due richieste di ricerca, in millisecondi REALI. */
static int64_t pausa_ric_ms(void)
{
    int64_t v = atomic_load(&g_pausa_ric_ms);
    return v >= 0 ? v : (int64_t)PH_METEO_RICERCA_PAUSA_S * 1000;
}

/* Intervallo minimo tra l'AVVIO di due collegamenti (qualunque posizione), in
 * millisecondi reali: l'orologio finto delle prove non lo tocca. */
static int64_t avvii_ms(void)
{
    int64_t v = atomic_load(&g_avvii_ms);
    return v >= 0 ? v : PH_METEO_AVVII_MIN_MS;
}

int64_t ph_meteo_limite_ms(void)
{
    int64_t l = atomic_load(&g_limite_ms);
    return l > 0 ? l : (int64_t)PH_METEO_LIMITE_S * 1000;
}

/* ---- Oggetto ----------------------------------------------------------------- */

#define SLOT_N 4 /* posizioni ricordate (cache e stato dei tentativi) */

/* Una posizione: cache dell'ultima risposta riuscita e conto dei tentativi.
 * Ogni collegamento AVVIATO e' un tentativo, registrato PRIMA di cominciare:
 * cosi' annullare o cambiare posizione a meta' non permette di ricominciare
 * subito (chiedi/annulla/chiedi non e' una raffica). */
typedef struct {
    bool usato;
    int32_t lat, lon;
    uint64_t uso;           /* per scegliere quale posizione dimenticare */
    bool cache_ok;
    int64_t c_mono;         /* secondi monotoni della risposta in cache */
    PhMeteoPrevisione cache;
    int tentativi;          /* collegamenti avviati dall'ultimo successo */
    int64_t prossimo_ok;    /* secondi monotoni: prima di allora niente collegamenti */
    char frase[256];        /* ultimo errore, senza coordinate */
} Slot;

typedef struct {
    bool ok;
    char testo[PH_METEO_RICERCA_TESTO_MAX + 1];
    char lingua[3];
    int64_t s;
    char frase[256];
} FallitaRic;

struct PhMeteo {
    pthread_mutex_t mu;
    pthread_cond_t sveglia; /* per il thread: c'e' una richiesta o si chiude (orologio monotono) */
    pthread_cond_t stato;   /* per gli altri: finita una callback, rilasciato un uso */
    pthread_t thread;
    bool stop;
    int usi;                /* chiamate pubbliche in corso (chiudi le aspetta) */
    bool attiva;            /* qualcuno aspetta una risposta */
    int32_t lat, lon;
    PhMeteoFatto fatto;
    void *utente;
    bool in_volo;           /* il thread sta usando la rete */
    bool in_callback;
    atomic_int interrompi;  /* la rete in corso deve fermarsi (annulla, chiudi, posizione cambiata) */
    Slot slot[SLOT_N];
    uint64_t tick;
    bool ha_avviato;
    int64_t ultimo_avvio_ms; /* orologio reale: intervallo minimo tra due avvii */

    /* Ricerca dei luoghi: thread e stato SUOI (non resta dietro una previsione
     * lenta); il lucchetto `mu` e' lo stesso, tenuto solo per istanti. */
    pthread_cond_t sveglia_c;
    pthread_t thread_c;
    bool c_attiva;
    char c_testo[PH_METEO_RICERCA_TESTO_MAX + 1]; /* gia' ripulito */
    char c_lingua[3];
    PhMeteoCercaFatto c_fatto;
    void *c_utente;
    bool c_in_volo, c_in_callback;
    atomic_int c_interrompi;
    bool c_ha_avviato;
    int64_t c_ultimo_ms;    /* orologio reale: ultimo avvio (per la pausa minima) */
    int64_t c_avvii_s[PH_METEO_RICERCA_TETTO]; /* ultimi avvii in rete (secondi monotoni, orologio sostituibile) */
    int c_avvii_n, c_avvii_i;
    FallitaRic cf[4];       /* ultime ricerche FALLITE (per non riprovarle subito) */
    int cf_i;
    int c_attese;           /* per le prove */
    bool cc_ok;             /* cache dell'ultima ricerca riuscita (una sola) */
    char cc_testo[PH_METEO_RICERCA_TESTO_MAX + 1];
    char cc_lingua[3];
    int64_t cc_mono;
    PhMeteoLuogo cc_v[PH_METEO_LUOGHI_MAX];
    size_t cc_n;
    PhMeteo *succ;
};

/* Registro degli oggetti vivi. Dopo ph_meteo_chiudi il puntatore NON si usa
 * piu' (e' un obbligo di chi chiama): il registro serve a far finire in modo
 * pulito le chiamate GIA' iniziate o in corsa con la chiusura, che trovano
 * l'oggetto tolto e tornano errore. Non e' una garanzia per chi usa un
 * puntatore vecchio dopo che l'indirizzo e' stato riutilizzato. */
static pthread_mutex_t g_reg_mu = PTHREAD_MUTEX_INITIALIZER;
static PhMeteo *g_reg;

static PhMeteo *acquisisci(PhMeteo *m)
{
    PhMeteo *r = NULL;
    pthread_mutex_lock(&g_reg_mu);
    for (PhMeteo *p = g_reg; p != NULL; p = p->succ)
        if (p == m) {
            pthread_mutex_lock(&p->mu);
            p->usi++;
            pthread_mutex_unlock(&p->mu);
            r = p;
            break;
        }
    pthread_mutex_unlock(&g_reg_mu);
    return r;
}

static void rilascia(PhMeteo *m)
{
    pthread_mutex_lock(&m->mu);
    m->usi--;
    pthread_cond_broadcast(&m->stato);
    pthread_mutex_unlock(&m->mu);
}

static int64_t ritardo_dopo(int tentativi)
{
    return tentativi <= 1 ? 5 * 60 : tentativi == 2 ? 15 * 60 : 30 * 60;
}

static Slot *cerca_slot(PhMeteo *m, int32_t lat, int32_t lon)
{
    for (int i = 0; i < SLOT_N; i++)
        if (m->slot[i].usato && m->slot[i].lat == lat && m->slot[i].lon == lon)
            return &m->slot[i];
    return NULL;
}

/* Lo slot della posizione; se manca ne prende uno libero o il meno usato di recente. */
static Slot *prendi_slot(PhMeteo *m, int32_t lat, int32_t lon)
{
    Slot *s = cerca_slot(m, lat, lon);
    if (s == NULL) {
        s = &m->slot[0];
        for (int i = 0; i < SLOT_N; i++) {
            if (!m->slot[i].usato) {
                s = &m->slot[i];
                break;
            }
            if (m->slot[i].uso < s->uso)
                s = &m->slot[i];
        }
        memset(s, 0, sizeof *s);
        s->usato = true;
        s->lat = lat;
        s->lon = lon;
    }
    s->uso = ++m->tick;
    return s;
}

/* Una richiesta completa (percorso, rete, analisi). Nessun lucchetto tenuto. */
static int scarica(PhMeteo *m, int32_t lat, int32_t lon, PhMeteoPrevisione *out, PhErrore *e)
{
    char percorso[1024];
    if (ph_meteo_percorso(lat, lon, percorso, sizeof percorso, e) != 0)
        return -1;
    int64_t scad = ph_meteo_ms() + ph_meteo_limite_ms();
    char *corpo = NULL;
    size_t n = 0;
    if (ph_meteo_http_scarica(PH_METEO_HOST, percorso, PH_METEO_RISPOSTA_MAX, &m->interrompi, scad, &corpo, &n, e) != 0)
        return -1;
    int r = ph_meteo_analizza(corpo, n, ph_meteo_unix_s(), out, e);
    free(corpo);
    return r;
}

static void *corpo_thread(void *arg)
{
    PhMeteo *m = arg;
    PhMeteoPrevisione ris = {0};
    char frase[256] = {0};
    pthread_mutex_lock(&m->mu);
    for (;;) {
        while (!m->stop && !m->attiva)
            pthread_cond_wait(&m->sveglia, &m->mu);
        if (m->stop)
            break;
        int32_t lat = m->lat, lon = m->lon;
        int64_t ora = ph_meteo_mono_s();
        bool rispondi = false, ok = false;
        Slot *s = cerca_slot(m, lat, lon);
        if (s != NULL && s->cache_ok && ora >= s->c_mono && ora - s->c_mono < PH_METEO_INTERVALLO_S) {
            s->uso = ++m->tick;
            ris = s->cache;
            ok = true;
            rispondi = true;
        } else if (s != NULL && s->tentativi > 0 && ora < s->prossimo_ok) {
            s->uso = ++m->tick;
            memcpy(frase, s->frase, sizeof frase);
            rispondi = true;
        } else {
            /* Intervallo minimo tra due avvii (anche di posizioni diverse): si
             * aspetta (annullabile: chiedi, annulla e chiudi svegliano) e poi
             * si ridecide, perche' la richiesta puo' essere cambiata. */
            int64_t ms = ph_meteo_ms();
            int64_t consentito = m->ultimo_avvio_ms + avvii_ms();
            if (m->ha_avviato && ms < consentito) {
                struct timespec ts = {.tv_sec = (time_t)(consentito / 1000),
                                      .tv_nsec = (long)(consentito % 1000) * 1000000L};
                pthread_cond_timedwait(&m->sveglia, &m->mu, &ts);
                continue;
            }
            s = prendi_slot(m, lat, lon);
            /* Il tentativo si registra ORA, prima della rete. */
            s->tentativi++;
            s->prossimo_ok = ora + ritardo_dopo(s->tentativi);
            snprintf(s->frase, sizeof s->frase, "%s", "aggiornamento interrotto: riprova piu' tardi");
            m->ha_avviato = true;
            m->ultimo_avvio_ms = ms;
            m->in_volo = true;
            atomic_store(&m->interrompi, 0);
            pthread_mutex_unlock(&m->mu);
            PhErrore e;
            e.testo[0] = '\0';
            int r = scarica(m, lat, lon, &ris, &e);
            pthread_mutex_lock(&m->mu);
            m->in_volo = false;
            if (m->stop)
                break;
            /* Solo questo thread modifica gli slot: `s` e' ancora quello giusto. */
            bool interrotta = atomic_load(&m->interrompi) != 0;
            if (r == 0) {
                /* Una risposta riuscita vale comunque (anche dopo annulla o un
                 * cambio di posizione): e' costata un collegamento. */
                s->cache = ris;
                s->cache_ok = true;
                s->c_mono = ph_meteo_mono_s();
                s->tentativi = 0;
                ok = true;
            } else if (!interrotta) {
                snprintf(s->frase, sizeof s->frase, "%s", e.testo[0] ? e.testo : "il servizio meteo non risponde");
            }
            if (r != 0)
                memcpy(frase, s->frase, sizeof frase);
            if (!m->attiva || m->lat != lat || m->lon != lon)
                continue; /* nessuno la aspetta piu' (o ne serve un'altra) */
            rispondi = true;
        }
        if (!rispondi)
            continue;
        PhMeteoFatto fatto = m->fatto;
        void *utente = m->utente;
        m->attiva = false;
        m->in_callback = true;
        pthread_mutex_unlock(&m->mu);
        /* Mai con il lucchetto: la callback puo' chiamare ph_meteo_chiedi. */
        fatto(ok ? &ris : NULL, ok ? NULL : frase, utente);
        pthread_mutex_lock(&m->mu);
        m->in_callback = false;
        pthread_cond_broadcast(&m->stato);
    }
    pthread_mutex_unlock(&m->mu);
    return NULL;
}


/* Le frasi di rete e di TLS parlano di «servizio meteo»: per la ricerca dei
 * luoghi si riscrivono (senza mai aggiungere il testo cercato). */
static void frase_ricerca(char *testo, size_t cap)
{
    static const char *const regole[][2] = {
        {"il servizio meteo", "la ricerca dei luoghi"},
        {"del servizio meteo", "della ricerca dei luoghi"},
        {"servizio meteo", "ricerca dei luoghi"},
        {"richiesta meteo", "richiesta di ricerca"},
    };
    char out[256];
    size_t o = 0;
    for (const char *p = testo; *p != '\0' && o + 1 < sizeof out;) {
        size_t r = 0;
        for (; r < sizeof regole / sizeof *regole; r++)
            if (strncmp(p, regole[r][0], strlen(regole[r][0])) == 0)
                break;
        const char *da = r < sizeof regole / sizeof *regole ? regole[r][1] : NULL;
        size_t l = da ? strlen(da) : 1;
        if (o + l + 1 > sizeof out)
            break;
        memcpy(out + o, da ? da : p, l);
        o += l;
        p += da ? strlen(regole[r][0]) : 1;
    }
    out[o] = '\0';
    snprintf(testo, cap, "%s", out);
}

/* Voce di una ricerca fallita da meno di PH_METEO_RICERCA_RIPROVA_S secondi, o NULL. */
static const FallitaRic *fallita_da_poco(const PhMeteo *m, const char *testo, const char *lingua, int64_t ora)
{
    for (int i = 0; i < 4; i++)
        if (m->cf[i].ok && strcmp(m->cf[i].testo, testo) == 0 && strcmp(m->cf[i].lingua, lingua) == 0 &&
            ora >= m->cf[i].s && ora - m->cf[i].s < PH_METEO_RICERCA_RIPROVA_S)
            return &m->cf[i];
    return NULL;
}

/* Avvii in rete della ricerca dentro la finestra mobile (un orologio che torna
 * indietro non libera nulla: la differenza negativa e' "dentro"). */
static int avvii_in_finestra(const PhMeteo *m, int64_t ora)
{
    int n = 0;
    for (int i = 0; i < m->c_avvii_n; i++)
        if (ora - m->c_avvii_s[i] < PH_METEO_RICERCA_FINESTRA_S)
            n++;
    return n;
}

/* ---- Thread della ricerca ------------------------------------------------------- */

static void *corpo_ricerca(void *arg)
{
    PhMeteo *m = arg;
    PhMeteoLuogo ris[PH_METEO_LUOGHI_MAX];
    size_t nris = 0;
    memset(ris, 0, sizeof ris);
    char frase[256] = {0};
    pthread_mutex_lock(&m->mu);
    for (;;) {
        while (!m->stop && !m->c_attiva)
            pthread_cond_wait(&m->sveglia_c, &m->mu);
        if (m->stop)
            break;
        char testo[sizeof m->c_testo], lingua[sizeof m->c_lingua];
        memcpy(testo, m->c_testo, sizeof testo);
        memcpy(lingua, m->c_lingua, sizeof lingua);
        int64_t ora = ph_meteo_mono_s();
        bool rispondi = false, ok = false;
        if (m->cc_ok && strcmp(m->cc_testo, testo) == 0 && strcmp(m->cc_lingua, lingua) == 0 && ora >= m->cc_mono &&
            ora - m->cc_mono < PH_METEO_INTERVALLO_S) {
            memcpy(ris, m->cc_v, sizeof ris);
            nris = m->cc_n;
            ok = true;
            rispondi = true;
        } else if (fallita_da_poco(m, testo, lingua, ora) != NULL) {
            /* La stessa ricerca e' appena fallita: subito l'errore di prima, senza rete. */
            memcpy(frase, fallita_da_poco(m, testo, lingua, ora)->frase, sizeof frase);
            rispondi = true;
        } else if (avvii_in_finestra(m, ora) >= PH_METEO_RICERCA_TETTO) {
            /* Tetto alle ricerche in rete: risposta subito, senza rete. */
            snprintf(frase, sizeof frase, "%s", "troppe ricerche: riprova tra qualche minuto");
            rispondi = true;
        } else {
            /* Pausa minima fra l'inizio di due richieste in rete (tempo reale): si
             * aspetta (la richiesta non fallisce), annullabile, e si ridecide. */
            int64_t ms = ph_meteo_ms();
            int64_t consentito = m->c_ultimo_ms + pausa_ric_ms();
            if (m->c_ha_avviato && ms < consentito) {
                struct timespec ts = {.tv_sec = (time_t)(consentito / 1000),
                                      .tv_nsec = (long)(consentito % 1000) * 1000000L};
                m->c_attese++;
                pthread_cond_timedwait(&m->sveglia_c, &m->mu, &ts);
                continue;
            }
            m->c_ha_avviato = true;
            m->c_ultimo_ms = ms; /* conta dall'AVVIO, anche se poi viene interrotta */
            /* Ogni avvio conta per il tetto, anche se poi viene interrotto. */
            m->c_avvii_s[m->c_avvii_i] = ora;
            m->c_avvii_i = (m->c_avvii_i + 1) % PH_METEO_RICERCA_TETTO;
            if (m->c_avvii_n < PH_METEO_RICERCA_TETTO)
                m->c_avvii_n++;
            m->c_in_volo = true;
            atomic_store(&m->c_interrompi, 0);
            pthread_mutex_unlock(&m->mu);
            PhErrore e;
            e.testo[0] = '\0';
            char percorso[512];
            char *corpo = NULL;
            size_t n = 0;
            nris = 0;
            int r = ph_meteo_percorso_ricerca(testo, lingua, percorso, sizeof percorso, &e);
            if (r == 0)
                r = ph_meteo_http_scarica(PH_METEO_GEO_HOST, percorso, PH_METEO_RICERCA_RISPOSTA_MAX, &m->c_interrompi,
                                          ph_meteo_ms() + ph_meteo_limite_ms(), &corpo, &n, &e);
            if (r == 0) {
                r = ph_meteo_analizza_ricerca(corpo, n, ris, PH_METEO_LUOGHI_MAX, &nris, &e);
                free(corpo);
            }
            pthread_mutex_lock(&m->mu);
            m->c_in_volo = false;
            if (m->stop)
                break;
            bool interrotta = atomic_load(&m->c_interrompi) != 0;
            if (r == 0) {
                /* Riuscita: vale per la memoria anche se nessuno la aspetta piu'. */
                memcpy(m->cc_testo, testo, sizeof testo);
                memcpy(m->cc_lingua, lingua, sizeof lingua);
                memcpy(m->cc_v, ris, sizeof ris);
                m->cc_n = nris;
                m->cc_mono = ph_meteo_mono_s();
                m->cc_ok = true;
                for (int i = 0; i < 4; i++)
                    if (m->cf[i].ok && strcmp(m->cf[i].testo, testo) == 0 && strcmp(m->cf[i].lingua, lingua) == 0)
                        m->cf[i].ok = false;
                ok = true;
            } else if (interrotta) {
                continue; /* fermata da annulla o da una ricerca diversa: se serve si rifa' */
            } else {
                snprintf(frase, sizeof frase, "%s", e.testo[0] ? e.testo : "la ricerca dei luoghi non risponde");
                frase_ricerca(frase, sizeof frase);
                int fi = m->cf_i;
                for (int i = 0; i < 4; i++) /* stessa ricerca: si aggiorna la sua voce */
                    if (m->cf[i].ok && strcmp(m->cf[i].testo, testo) == 0 && strcmp(m->cf[i].lingua, lingua) == 0)
                        fi = i;
                if (fi == m->cf_i)
                    m->cf_i = (m->cf_i + 1) % 4;
                m->cf[fi].ok = true;
                memcpy(m->cf[fi].testo, testo, sizeof testo);
                memcpy(m->cf[fi].lingua, lingua, sizeof lingua);
                m->cf[fi].s = ph_meteo_mono_s();
                memcpy(m->cf[fi].frase, frase, sizeof frase);
            }
            if (!m->c_attiva || strcmp(m->c_testo, testo) != 0 || strcmp(m->c_lingua, lingua) != 0)
                continue; /* nessuno la aspetta piu' (o se ne aspetta un'altra) */
            rispondi = true;
        }
        if (!rispondi)
            continue;
        PhMeteoCercaFatto fatto = m->c_fatto;
        void *utente = m->c_utente;
        m->c_attiva = false;
        m->c_in_callback = true;
        pthread_mutex_unlock(&m->mu);
        fatto(ok ? ris : NULL, ok ? nris : 0, ok ? NULL : frase, utente);
        pthread_mutex_lock(&m->mu);
        m->c_in_callback = false;
        pthread_cond_broadcast(&m->stato);
    }
    pthread_mutex_unlock(&m->mu);
    return NULL;
}

PhMeteo *ph_meteo_nuovo(PhErrore *e)
{
    PhMeteo *m = calloc(1, sizeof *m);
    if (m == NULL) {
        ph_errore(e, "meteo: memoria esaurita");
        return NULL;
    }
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC); /* le attese a tempo usano l'orologio monotono */
    pthread_mutex_init(&m->mu, NULL);
    pthread_cond_init(&m->sveglia, &ca);
    pthread_cond_init(&m->sveglia_c, &ca);
    pthread_cond_init(&m->stato, NULL);
    pthread_condattr_destroy(&ca);
    atomic_init(&m->interrompi, 0);
    atomic_init(&m->c_interrompi, 0);
    const char *guasto = NULL;
    if (pthread_create(&m->thread, NULL, corpo_thread, m) != 0)
        guasto = "meteo: thread non avviato";
    else if (pthread_create(&m->thread_c, NULL, corpo_ricerca, m) != 0) {
        guasto = "meteo: thread non avviato";
        pthread_mutex_lock(&m->mu);
        m->stop = true;
        pthread_cond_broadcast(&m->sveglia);
        pthread_mutex_unlock(&m->mu);
        if (pthread_join(m->thread, NULL) != 0)
            guasto = "meteo: thread non fermato"; /* si perde la memoria, non la si libera sotto il thread */
    }
    if (guasto != NULL) {
        if (strcmp(guasto, "meteo: thread non fermato") != 0) {
            pthread_cond_destroy(&m->stato);
            pthread_cond_destroy(&m->sveglia_c);
            pthread_cond_destroy(&m->sveglia);
            pthread_mutex_destroy(&m->mu);
            free(m);
        }
        ph_errore(e, "%s", guasto);
        return NULL;
    }
    /* Nel registro solo a thread avviati (i loro id sono definitivi). */
    pthread_mutex_lock(&g_reg_mu);
    m->succ = g_reg;
    g_reg = m;
    pthread_mutex_unlock(&g_reg_mu);
    return m;
}

int ph_meteo_chiedi(PhMeteo *m, int32_t lat_millesimi, int32_t lon_millesimi, PhMeteoFatto fatto, void *utente,
                    PhErrore *e)
{
    if (m == NULL || fatto == NULL)
        return ph_errore(e, "meteo: argomenti non validi");
    if (lat_millesimi < -90000 || lat_millesimi > 90000 || lon_millesimi < -180000 || lon_millesimi > 180000 ||
        lat_millesimi % 5 != 0 || lon_millesimi % 5 != 0)
        return ph_errore(e, "meteo: posizione non valida");
    PhMeteo *o = acquisisci(m);
    if (o == NULL)
        return ph_errore(e, "meteo: oggetto chiuso");
    pthread_mutex_lock(&o->mu);
    if (o->stop) {
        pthread_mutex_unlock(&o->mu);
        rilascia(o);
        return ph_errore(e, "meteo: oggetto chiuso");
    }
    /* Una richiesta in volo per un altro posto non serve piu': si ferma (conta
     * comunque come tentativo). Per lo stesso posto basta cambiare destinatario. */
    if (o->attiva && o->in_volo && (o->lat != lat_millesimi || o->lon != lon_millesimi))
        atomic_store(&o->interrompi, 1);
    o->lat = lat_millesimi;
    o->lon = lon_millesimi;
    o->fatto = fatto;
    o->utente = utente;
    o->attiva = true;
    pthread_cond_signal(&o->sveglia);
    pthread_mutex_unlock(&o->mu);
    rilascia(o);
    return 0;
}

/* Dentro una delle nostre funzioni di ritorno? (annulla non puo' aspettare se'
 * stessa; chiudi non puo' liberare il thread su cui gira.) */
static bool in_callback_di(const PhMeteo *o)
{
    return pthread_equal(pthread_self(), o->thread) || pthread_equal(pthread_self(), o->thread_c);
}

/* Con `mu` tenuto. */
static void annulla_ricerca_l(PhMeteo *o)
{
    o->c_attiva = false;
    o->c_fatto = NULL;
    o->c_utente = NULL;
    atomic_store(&o->c_interrompi, 1);
    pthread_cond_broadcast(&o->sveglia_c); /* sveglia anche l'attesa della pausa minima */
}

void ph_meteo_annulla(PhMeteo *m)
{
    PhMeteo *o = acquisisci(m);
    if (o == NULL)
        return;
    pthread_mutex_lock(&o->mu);
    o->attiva = false;
    o->fatto = NULL;
    o->utente = NULL;
    atomic_store(&o->interrompi, 1);
    pthread_cond_broadcast(&o->sveglia); /* sveglia un'eventuale attesa dell'intervallo minimo */
    annulla_ricerca_l(o);
    /* Le callback in corso si aspettano: dopo il ritorno non ne arriva nessuna.
     * Ma se siamo NOI dentro una callback (vietato dal contratto, e comunque
     * innocuo) aspettare quella vorrebbe dire aspettare se' stessi. */
    if (!pthread_equal(pthread_self(), o->thread))
        while (o->in_callback)
            pthread_cond_wait(&o->stato, &o->mu);
    if (!pthread_equal(pthread_self(), o->thread_c))
        while (o->c_in_callback)
            pthread_cond_wait(&o->stato, &o->mu);
    pthread_mutex_unlock(&o->mu);
    rilascia(o);
}

void ph_meteo_chiudi(PhMeteo *m)
{
    /* Si toglie dal registro: da qui nessuna nuova chiamata lo raggiunge. */
    PhMeteo *o = NULL;
    pthread_mutex_lock(&g_reg_mu);
    for (PhMeteo **p = &g_reg; *p != NULL; p = &(*p)->succ)
        if (*p == m) {
            /* Dalle callback sarebbe un'attesa di se' stessi e il thread
             * liberato mentre gira: la chiamata e' RIFIUTATA, l'oggetto resta com'e'. */
            if (in_callback_di(*p)) {
                pthread_mutex_unlock(&g_reg_mu);
                return;
            }
            o = *p;
            *p = o->succ;
            break;
        }
    pthread_mutex_unlock(&g_reg_mu);
    if (o == NULL)
        return;
    pthread_mutex_lock(&o->mu);
    o->stop = true;
    o->attiva = false;
    atomic_store(&o->interrompi, 1);
    o->c_attiva = false;
    atomic_store(&o->c_interrompi, 1);
    pthread_cond_broadcast(&o->sveglia);
    pthread_cond_broadcast(&o->sveglia_c);
    while (o->usi > 0) /* le chiamate gia' iniziate finiscono prima */
        pthread_cond_wait(&o->stato, &o->mu);
    pthread_mutex_unlock(&o->mu);
    /* Se un join fallisse il thread potrebbe girare ancora: meglio perdere la
     * memoria che liberarla sotto i suoi piedi. */
    int j1 = pthread_join(o->thread, NULL);
    int j2 = pthread_join(o->thread_c, NULL);
    if (j1 != 0 || j2 != 0)
        return;
    pthread_cond_destroy(&o->stato);
    pthread_cond_destroy(&o->sveglia_c);
    pthread_cond_destroy(&o->sveglia);
    pthread_mutex_destroy(&o->mu);
    memset(o, 0, sizeof *o); /* cache e posizione non restano in memoria */
    free(o);
}

/* ---- Ricerca dei luoghi (funzioni pubbliche) ----------------------------------------- */

int ph_meteo_cerca(PhMeteo *m, const char *testo, const char *lingua, PhMeteoCercaFatto fatto, void *utente,
                   PhErrore *e)
{
    if (m == NULL || fatto == NULL)
        return ph_errore(e, "meteo: argomenti non validi");
    /* Validazione completa PRIMA di toccare l'oggetto: se torna errore nessuna
     * richiesta parte e nulla cambia. */
    char percorso[512], pulito[PH_METEO_RICERCA_TESTO_MAX + 1];
    if (ph_meteo_percorso_ricerca(testo, lingua, percorso, sizeof percorso, e) != 0 ||
        ph_meteo_testo_ricerca(testo, pulito, e) != 0)
        return -1;
    PhMeteo *o = acquisisci(m);
    if (o == NULL)
        return ph_errore(e, "meteo: oggetto chiuso");
    pthread_mutex_lock(&o->mu);
    if (o->stop) {
        pthread_mutex_unlock(&o->mu);
        rilascia(o);
        return ph_errore(e, "meteo: oggetto chiuso");
    }
    /* Una ricerca in volo per un altro testo si ferma; per lo stesso basta
     * cambiare destinatario. La vecchia non risponde. */
    if (o->c_attiva && o->c_in_volo && (strcmp(o->c_testo, pulito) != 0 || strcmp(o->c_lingua, lingua) != 0))
        atomic_store(&o->c_interrompi, 1);
    memcpy(o->c_testo, pulito, sizeof pulito);
    memcpy(o->c_lingua, lingua, 3);
    o->c_fatto = fatto;
    o->c_utente = utente;
    o->c_attiva = true;
    pthread_cond_signal(&o->sveglia_c);
    pthread_mutex_unlock(&o->mu);
    rilascia(o);
    return 0;
}

void ph_meteo_cerca_annulla(PhMeteo *m)
{
    PhMeteo *o = acquisisci(m);
    if (o == NULL)
        return;
    pthread_mutex_lock(&o->mu);
    annulla_ricerca_l(o);
    if (!pthread_equal(pthread_self(), o->thread_c))
        while (o->c_in_callback)
            pthread_cond_wait(&o->stato, &o->mu);
    pthread_mutex_unlock(&o->mu);
    rilascia(o);
}

int ph_meteo_prova_attese_ricerca(PhMeteo *m)
{
    pthread_mutex_lock(&m->mu);
    int n = m->c_attese;
    pthread_mutex_unlock(&m->mu);
    return n;
}
