/* Telefono finto per le prove del pezzo "dati" (vedi finto.h). */
#include "finto.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CANALI 4096

struct PhComponente {
    int x;
};

struct FintoCanale {
    char riga[32];
    pthread_mutex_t m;
    pthread_cond_t cv;
    uint8_t *coda;
    size_t lung, cap, inizio;
    bool remoto_chiuso, remoto_errore, chiuso_dal_pc, scrittura_fallisce;
    size_t a_pezzi;
    uint8_t *scritto;
    size_t scritto_lung;
};

struct PhCanale {
    FintoCanale *f;
};

static struct PhComponente componente;
static pthread_mutex_t reg = PTHREAD_MUTEX_INITIALIZER; /* richieste, canali */
static struct {
    bool presente, errore;
    uint8_t *dati;
    size_t n;
    char frase[128];
} risposte[256];
static int richieste;
static uint8_t ult_tipo;
static uint8_t *ult_corpo;
static size_t ult_n;
static FintoCanale *canali[MAX_CANALI];
static int n_canali;
static bool rifiuta;
static int ritardo_ms;
static bool scritture_falliscono;

/* Cancello: con il cancello chiuso ogni richiesta aspetta (senza tenere altri blocchi). */
static pthread_mutex_t m_gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv_gate = PTHREAD_COND_INITIALIZER;
static bool gate_chiuso;
static int gate_attese;
static int in_corso;     /* richieste dentro ph_componente_richiesta (anche ferme nel cancello), sotto m_gate */
static int violazioni;   /* ascolta(NULL) chiamata con richieste in corso, sotto m_gate */

int finto_violazioni(void)
{
    pthread_mutex_lock(&m_gate);
    int n = violazioni;
    pthread_mutex_unlock(&m_gate);
    return n;
}

void finto_cancello(bool chiuso)
{
    pthread_mutex_lock(&m_gate);
    gate_chiuso = chiuso;
    pthread_cond_broadcast(&cv_gate);
    pthread_mutex_unlock(&m_gate);
}

int finto_cancello_attese(void)
{
    pthread_mutex_lock(&m_gate);
    int n = gate_attese;
    pthread_mutex_unlock(&m_gate);
    return n;
}

/* Ascolto: un solo intervallo basta per le prove. */
static pthread_mutex_t m_ascolto = PTHREAD_MUTEX_INITIALIZER;
#define MAX_ASC 4
static struct {
    PhEventoGrezzo f;
    void *utente;
    uint8_t da, a;
} asc[MAX_ASC];
static int asc_guasto; /* l'N-esimo ph_componente_ascolta(NULL) da ora fallisce (0 = mai) */
static int asc_guasto_reg; /* idem per le registrazioni (funzione non NULL) */
static int asc_ritardo_ms; /* pausa dentro ph_componente_ascolta, prima del blocco: allarga le finestre di gara */

void finto_azzera(void)
{
    pthread_mutex_lock(&reg);
    for (size_t i = 0; i < 256; i++) {
        free(risposte[i].dati);
        memset(&risposte[i], 0, sizeof risposte[i]);
    }
    free(ult_corpo);
    ult_corpo = NULL;
    ult_n = 0;
    richieste = 0;
    for (int i = 0; i < n_canali; i++) {
        free(canali[i]->coda);
        free(canali[i]->scritto);
        free(canali[i]);
        canali[i] = NULL;
    }
    n_canali = 0;
    rifiuta = false;
    ritardo_ms = 0;
    scritture_falliscono = false;
    pthread_mutex_unlock(&reg);
    pthread_mutex_lock(&m_ascolto);
    memset(asc, 0, sizeof asc);
    asc_guasto = 0;
    asc_guasto_reg = 0;
    asc_ritardo_ms = 0;
    pthread_mutex_unlock(&m_ascolto);
    pthread_mutex_lock(&m_gate);
    violazioni = 0;
    pthread_mutex_unlock(&m_gate);
}

PhComponente *finto_componente(void)
{
    return &componente;
}

void finto_risposta(uint8_t tipo, const uint8_t *corpo, size_t n)
{
    pthread_mutex_lock(&reg);
    free(risposte[tipo].dati);
    risposte[tipo].dati = n ? malloc(n) : NULL;
    if (n)
        memcpy(risposte[tipo].dati, corpo, n);
    risposte[tipo].n = n;
    risposte[tipo].presente = true;
    risposte[tipo].errore = false;
    pthread_mutex_unlock(&reg);
}

void finto_risposta_errore(uint8_t tipo, const char *frase)
{
    pthread_mutex_lock(&reg);
    risposte[tipo].presente = true;
    risposte[tipo].errore = true;
    strncpy(risposte[tipo].frase, frase, sizeof risposte[tipo].frase - 1);
    pthread_mutex_unlock(&reg);
}

int finto_richieste(void)
{
    pthread_mutex_lock(&reg);
    int r = richieste;
    pthread_mutex_unlock(&reg);
    return r;
}

uint8_t finto_ultimo_tipo(void)
{
    return ult_tipo;
}

size_t finto_ultimo_corpo(uint8_t *dest, size_t max)
{
    size_t n = ult_n < max ? ult_n : max;
    if (n)
        memcpy(dest, ult_corpo, n);
    return ult_n;
}

static int richiesta_vera(uint8_t tipo, const uint8_t *corpo, size_t n, PhBuffer *risposta, PhErrore *e);

int ph_componente_richiesta(PhComponente *c, uint8_t tipo, const uint8_t *corpo, size_t n,
                            PhBuffer *risposta, PhErrore *e)
{
    (void)c;
    pthread_mutex_lock(&m_gate);
    in_corso++;
    gate_attese++;
    while (gate_chiuso)
        pthread_cond_wait(&cv_gate, &m_gate);
    gate_attese--;
    pthread_mutex_unlock(&m_gate);
    int r = richiesta_vera(tipo, corpo, n, risposta, e);
    pthread_mutex_lock(&m_gate);
    in_corso--;
    pthread_mutex_unlock(&m_gate);
    return r;
}

static int richiesta_vera(uint8_t tipo, const uint8_t *corpo, size_t n, PhBuffer *risposta, PhErrore *e)
{
    pthread_mutex_lock(&reg);
    richieste++;
    ult_tipo = tipo;
    free(ult_corpo);
    ult_corpo = n ? malloc(n) : NULL;
    if (n)
        memcpy(ult_corpo, corpo, n);
    ult_n = n;
    int r = 0;
    if (!risposte[tipo].presente) {
        r = ph_errore(e, "tipo sconosciuto");
    } else if (risposte[tipo].errore) {
        r = ph_errore(e, "%s", risposte[tipo].frase);
    } else if (risposta != NULL && risposte[tipo].n &&
               ph_buffer_aggiungi(risposta, risposte[tipo].dati, risposte[tipo].n) != 0) {
        r = ph_errore(e, "memoria finita");
    }
    pthread_mutex_unlock(&reg);
    return r;
}

int ph_componente_ascolta(PhComponente *c, uint8_t da_tipo, uint8_t a_tipo, PhEventoGrezzo f,
                          void *utente, PhErrore *e)
{
    (void)c;
    int ritardo = asc_ritardo_ms;
    if (ritardo > 0) {
        struct timespec t = {0, (long)ritardo * 1000000L};
        nanosleep(&t, NULL);
    }
    /* Come il vero: aspetta la fine di una chiamata in corso. */
    pthread_mutex_lock(&m_ascolto);
    if (f == NULL) {
        /* Togliere l'ascolto con una richiesta in corso e' la violazione che
         * ph_dati_chiudi deve evitare aspettando. */
        pthread_mutex_lock(&m_gate);
        if (in_corso > 0)
            violazioni++;
        pthread_mutex_unlock(&m_gate);
    }
    if (f == NULL && asc_guasto > 0 && --asc_guasto == 0) {
        pthread_mutex_unlock(&m_ascolto);
        return ph_errore(e, "guasto simulato");
    }
    if (f != NULL && asc_guasto_reg > 0 && --asc_guasto_reg == 0) {
        pthread_mutex_unlock(&m_ascolto);
        return ph_errore(e, "guasto simulato nella registrazione");
    }
    /* Un posto per intervallo: stesso intervallo = sostituisce; f NULL = toglie. */
    int libero = -1, trovato = -1;
    for (int i = 0; i < MAX_ASC; i++) {
        if (asc[i].f == NULL)
            libero = libero < 0 ? i : libero;
        else if (asc[i].da == da_tipo && asc[i].a == a_tipo)
            trovato = i;
    }
    if (f == NULL) {
        if (trovato >= 0)
            memset(&asc[trovato], 0, sizeof asc[trovato]);
    } else {
        int i = trovato >= 0 ? trovato : libero;
        if (i < 0) {
            pthread_mutex_unlock(&m_ascolto);
            return ph_errore(e, "troppi ascoltatori");
        }
        asc[i].f = f;
        asc[i].utente = utente;
        asc[i].da = da_tipo;
        asc[i].a = a_tipo;
    }
    pthread_mutex_unlock(&m_ascolto);
    /* Anche dopo una registrazione si resta fermi un attimo: chi chiama non ne
     * annota l'esito subito (apre la finestra di gara che le prove cercano). */
    if (f != NULL && ritardo > 0) {
        struct timespec t2 = {0, (long)ritardo * 1000000L};
        nanosleep(&t2, NULL);
    }
    return 0;
}

void finto_togliere_ascolto_fallisce_alla_n(int n)
{
    pthread_mutex_lock(&m_ascolto);
    asc_guasto = n;
    pthread_mutex_unlock(&m_ascolto);
}

void finto_registrare_ascolto_fallisce_alla_n(int n)
{
    pthread_mutex_lock(&m_ascolto);
    asc_guasto_reg = n;
    pthread_mutex_unlock(&m_ascolto);
}

void finto_ritardo_ascolta_ms(int ms)
{
    asc_ritardo_ms = ms; /* da impostare prima di avviare altri thread */
}

void finto_togliere_ascolto_fallisce_una_volta(void)
{
    finto_togliere_ascolto_fallisce_alla_n(1);
}

void finto_evento(uint8_t tipo, const uint8_t *corpo, size_t n)
{
    pthread_mutex_lock(&m_ascolto);
    for (int i = 0; i < MAX_ASC; i++)
        if (asc[i].f != NULL && tipo >= asc[i].da && tipo <= asc[i].a)
            asc[i].f(tipo, corpo, n, asc[i].utente);
    pthread_mutex_unlock(&m_ascolto);
}

/* true se c'e' almeno un ascoltatore registrato. */
bool finto_ascolto_registrato(void)
{
    pthread_mutex_lock(&m_ascolto);
    bool r = false;
    for (int i = 0; i < MAX_ASC; i++)
        r = r || asc[i].f != NULL;
    pthread_mutex_unlock(&m_ascolto);
    return r;
}

/* ---- Canali ---------------------------------------------------------------*/
void finto_rifiuta_canali(bool r)
{
    pthread_mutex_lock(&reg);
    rifiuta = r;
    pthread_mutex_unlock(&reg);
}

void finto_scritture_falliscono(bool v)
{
    pthread_mutex_lock(&reg);
    scritture_falliscono = v;
    pthread_mutex_unlock(&reg);
}

void finto_ritardo_apertura_ms(int ms)
{
    pthread_mutex_lock(&reg);
    ritardo_ms = ms;
    pthread_mutex_unlock(&reg);
}

PhCanale *ph_componente_canale(PhComponente *c, const char *riga, PhErrore *e)
{
    (void)c;
    pthread_mutex_lock(&reg);
    int ritardo = ritardo_ms;
    bool no = rifiuta || n_canali >= MAX_CANALI;
    pthread_mutex_unlock(&reg);
    if (ritardo > 0) {
        struct timespec t = {ritardo / 1000, (long)(ritardo % 1000) * 1000000L};
        nanosleep(&t, NULL);
    }
    if (no) {
        ph_errore(e, "canale rifiutato");
        return NULL;
    }
    FintoCanale *f = calloc(1, sizeof *f);
    PhCanale *pc = calloc(1, sizeof *pc);
    strncpy(f->riga, riga, sizeof f->riga - 1);
    pthread_mutex_init(&f->m, NULL);
    pthread_cond_init(&f->cv, NULL);
    pc->f = f;
    pthread_mutex_lock(&reg);
    canali[n_canali++] = f;
    pthread_mutex_unlock(&reg);
    return pc;
}

int finto_canali_aperti(void)
{
    pthread_mutex_lock(&reg);
    int n = n_canali;
    pthread_mutex_unlock(&reg);
    return n;
}

FintoCanale *finto_canale(int n)
{
    pthread_mutex_lock(&reg);
    FintoCanale *f = n < n_canali ? canali[n] : NULL;
    pthread_mutex_unlock(&reg);
    return f;
}

const char *finto_canale_riga(FintoCanale *f)
{
    return f->riga;
}

void finto_canale_spingi(FintoCanale *f, const void *dati, size_t n)
{
    if (n == 0)
        return;
    pthread_mutex_lock(&f->m);
    if (f->lung + n > f->cap) {
        f->cap = (f->lung + n) * 2;
        f->coda = realloc(f->coda, f->cap);
    }
    memcpy(f->coda + f->lung, dati, n);
    f->lung += n;
    pthread_cond_broadcast(&f->cv);
    pthread_mutex_unlock(&f->m);
}

void finto_canale_chiudi_remoto(FintoCanale *f)
{
    pthread_mutex_lock(&f->m);
    f->remoto_chiuso = true;
    pthread_cond_broadcast(&f->cv);
    pthread_mutex_unlock(&f->m);
}

void finto_canale_errore_remoto(FintoCanale *f)
{
    pthread_mutex_lock(&f->m);
    f->remoto_errore = true;
    pthread_cond_broadcast(&f->cv);
    pthread_mutex_unlock(&f->m);
}

void finto_canale_a_pezzi(FintoCanale *f, size_t max)
{
    pthread_mutex_lock(&f->m);
    f->a_pezzi = max;
    pthread_mutex_unlock(&f->m);
}

void finto_canale_scrittura_fallisce(FintoCanale *f, bool v)
{
    pthread_mutex_lock(&f->m);
    f->scrittura_fallisce = v;
    pthread_mutex_unlock(&f->m);
}

size_t finto_canale_scritto(FintoCanale *f, uint8_t *dest, size_t max)
{
    pthread_mutex_lock(&f->m);
    size_t n = f->scritto_lung < max ? f->scritto_lung : max;
    if (n)
        memcpy(dest, f->scritto, n);
    size_t tot = f->scritto_lung;
    pthread_mutex_unlock(&f->m);
    return tot;
}

bool finto_canale_chiuso_dal_pc(FintoCanale *f)
{
    pthread_mutex_lock(&f->m);
    bool r = f->chiuso_dal_pc;
    pthread_mutex_unlock(&f->m);
    return r;
}

ssize_t ph_canale_leggi(PhCanale *c, void *dati, size_t n, int timeout_ms, PhErrore *e)
{
    FintoCanale *f = c->f;
    pthread_mutex_lock(&f->m);
    if (f->lung == f->inizio && !f->remoto_chiuso && !f->remoto_errore && timeout_ms != 0) {
        struct timespec t;
        clock_gettime(CLOCK_REALTIME, &t);
        long ms = timeout_ms < 0 ? 100000 : timeout_ms;
        t.tv_sec += ms / 1000;
        t.tv_nsec += (ms % 1000) * 1000000L;
        if (t.tv_nsec >= 1000000000L) {
            t.tv_sec++;
            t.tv_nsec -= 1000000000L;
        }
        while (f->lung == f->inizio && !f->remoto_chiuso && !f->remoto_errore &&
               !f->chiuso_dal_pc) {
            if (pthread_cond_timedwait(&f->cv, &f->m, &t) != 0)
                break;
        }
    }
    ssize_t r;
    size_t disp = f->lung - f->inizio;
    if (disp > 0) {
        size_t k = n < disp ? n : disp;
        if (f->a_pezzi && k > f->a_pezzi)
            k = f->a_pezzi;
        memcpy(dati, f->coda + f->inizio, k);
        f->inizio += k;
        r = (ssize_t)k;
    } else if (f->remoto_errore) {
        r = ph_errore(e, "connessione caduta");
    } else if (f->remoto_chiuso) {
        r = PH_CANALE_CHIUSO;
    } else {
        r = PH_CANALE_TEMPO;
    }
    pthread_mutex_unlock(&f->m);
    return r;
}

int ph_canale_scrivi(PhCanale *c, const void *dati, size_t n, PhErrore *e)
{
    FintoCanale *f = c->f;
    pthread_mutex_lock(&reg);
    bool globale = scritture_falliscono;
    pthread_mutex_unlock(&reg);
    pthread_mutex_lock(&f->m);
    int r = 0;
    if (globale || f->scrittura_fallisce || f->chiuso_dal_pc) {
        r = ph_errore(e, "scrittura fallita");
    } else {
        f->scritto = realloc(f->scritto, f->scritto_lung + n + 1);
        memcpy(f->scritto + f->scritto_lung, dati, n);
        f->scritto_lung += n;
    }
    pthread_mutex_unlock(&f->m);
    return r;
}

void ph_canale_chiudi(PhCanale *c)
{
    pthread_mutex_lock(&c->f->m);
    c->f->chiuso_dal_pc = true;
    pthread_cond_broadcast(&c->f->cv);
    pthread_mutex_unlock(&c->f->m);
    free(c);
}

bool finto_ascolto_per(uint8_t tipo)
{
    pthread_mutex_lock(&m_ascolto);
    bool r = false;
    for (int i = 0; i < MAX_ASC; i++)
        r = r || (asc[i].f != NULL && tipo >= asc[i].da && tipo <= asc[i].a);
    pthread_mutex_unlock(&m_ascolto);
    return r;
}
