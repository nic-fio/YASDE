/* Vedi tls_finto.h. Ogni attesa e' una poll con scadenza e controllo del flag
 * di annullamento ogni 100 ms (la regola di tls.h e' 250 ms). */
#include "tls_finto.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../meteo.h"
#include "../tls.h"

atomic_int ph_tls_finto_porta;
atomic_int ph_tls_finto_rifiuta;
atomic_int ph_tls_finto_trattieni;
sem_t ph_tls_finto_in_apri;
atomic_int ph_tls_finto_aperture;
atomic_int ph_tls_finto_connessioni;
atomic_int ph_tls_finto_ignora_annulla;

static const char *g_mem;
static size_t g_mem_n;
static uint32_t g_mem_seme;

void ph_tls_finto_memoria(const char *dati, size_t n, uint32_t seme)
{
    g_mem = dati;
    g_mem_n = n;
    g_mem_seme = seme ? seme : 1;
}

/* Realizzazione vera di tls.c, solo per le prove di quella: qui non serve. */
void ph_tls_prova_ancore(const char *file_pem) { (void)file_pem; }

static int annullata(const atomic_int *annulla)
{
    return annulla != NULL && !atomic_load(&ph_tls_finto_ignora_annulla) && atomic_load(annulla) != 0;
}

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static char g_host[128];
static int g_porta;

struct PhTls {
    int fd;           /* -1 in modalita' memoria */
    const char *mem;
    size_t mem_n, mem_pos;
    uint32_t seme;
};

static int64_t ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void ph_tls_finto_ultimo(char *host, size_t cap, int *porta)
{
    pthread_mutex_lock(&g_mu);
    snprintf(host, cap, "%s", g_host);
    *porta = g_porta;
    pthread_mutex_unlock(&g_mu);
}

/* Aspetta fd (eventi) o la scadenza o l'annullamento. 0 pronto, -1 errore. */
static int attendi(int fd, short eventi, int64_t scadenza, const atomic_int *annulla, PhErrore *e)
{
    for (;;) {
        if (annullata(annulla))
            return ph_errore(e, "operazione annullata");
        int64_t resto = scadenza - ms();
        if (resto <= 0)
            return ph_errore(e, "il servizio meteo non risponde");
        struct pollfd p = {.fd = fd, .events = eventi};
        int r = poll(&p, 1, resto > 100 ? 100 : (int)resto);
        if (r > 0)
            return 0;
        if (r < 0 && errno != EINTR)
            return ph_errore(e, "il servizio meteo non risponde");
    }
}

PhTls *ph_tls_apri(const char *host, uint16_t porta, int64_t scadenza_ms, const atomic_int *annulla, PhErrore *e)
{
    atomic_fetch_add(&ph_tls_finto_aperture, 1);
    /* Come tls.c vero: l'annullamento si guarda PRIMA di tutto, senza toccare la rete. */
    if (annullata(annulla)) {
        ph_errore(e, "operazione annullata");
        return NULL;
    }
    pthread_mutex_lock(&g_mu);
    snprintf(g_host, sizeof g_host, "%s", host);
    g_porta = porta;
    pthread_mutex_unlock(&g_mu);

    /* «trattieni» vale solo per il servizio delle previsioni: la ricerca (altro
     * host) passa, cosi' si prova che non resta dietro una previsione ferma. */
    if (atomic_load(&ph_tls_finto_trattieni) && strcmp(host, PH_METEO_HOST) == 0) {
        sem_post(&ph_tls_finto_in_apri);
        while (atomic_load(&ph_tls_finto_trattieni)) { /* solo per l'host delle previsioni */
            if (annullata(annulla)) {
                ph_errore(e, "operazione annullata");
                return NULL;
            }
            if (scadenza_ms - ms() <= 0) {
                ph_errore(e, "il servizio meteo non risponde");
                return NULL;
            }
            poll(NULL, 0, 20);
        }
    }
    if (atomic_load(&ph_tls_finto_rifiuta)) {
        ph_errore(e, "certificato del servizio meteo non valido");
        return NULL;
    }
    if (g_mem != NULL) {
        PhTls *t = calloc(1, sizeof *t);
        if (t == NULL) {
            ph_errore(e, "memoria esaurita");
            return NULL;
        }
        t->fd = -1;
        t->mem = g_mem;
        t->mem_n = g_mem_n;
        t->seme = g_mem_seme;
        return t;
    }
    atomic_fetch_add(&ph_tls_finto_connessioni, 1);
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        ph_errore(e, "il servizio meteo non risponde");
        return NULL;
    }
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons((uint16_t)atomic_load(&ph_tls_finto_porta))};
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0 && errno != EINPROGRESS) {
        close(fd);
        ph_errore(e, "il servizio meteo non risponde");
        return NULL;
    }
    int err = 0;
    socklen_t l = sizeof err;
    if (attendi(fd, POLLOUT, scadenza_ms, annulla, e) != 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l) != 0 ||
        err != 0) {
        if (err != 0)
            ph_errore(e, "il servizio meteo non risponde");
        close(fd);
        return NULL;
    }
    PhTls *t = calloc(1, sizeof *t);
    if (t == NULL) {
        close(fd);
        ph_errore(e, "memoria esaurita");
        return NULL;
    }
    t->fd = fd;
    return t;
}

int ph_tls_scrivi(PhTls *t, const void *dati, size_t n, int64_t scadenza_ms, const atomic_int *annulla, PhErrore *e)
{
    const char *p = dati;
    if (t->fd < 0)
        return 0;
    while (n > 0) {
        if (attendi(t->fd, POLLOUT, scadenza_ms, annulla, e) != 0)
            return -1;
        ssize_t r = send(t->fd, p, n, MSG_NOSIGNAL);
        if (r < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return ph_errore(e, "il servizio meteo non risponde");
        }
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

ssize_t ph_tls_leggi(PhTls *t, void *buf, size_t max, int64_t scadenza_ms, const atomic_int *annulla, PhErrore *e)
{
    if (t->fd < 0) {
        /* Pezzi di lunghezza casuale (spesso piccola) dalla memoria. */
        if (t->mem_pos >= t->mem_n)
            return 0;
        t->seme ^= t->seme << 13;
        t->seme ^= t->seme >> 17;
        t->seme ^= t->seme << 5;
        size_t k = 1 + (t->seme % 4 == 0 ? t->seme >> 8 : (t->seme >> 8) % 8);
        if (k > max)
            k = max;
        if (k > t->mem_n - t->mem_pos)
            k = t->mem_n - t->mem_pos;
        memcpy(buf, t->mem + t->mem_pos, k);
        t->mem_pos += k;
        return (ssize_t)k;
    }
    for (;;) {
        if (attendi(t->fd, POLLIN, scadenza_ms, annulla, e) != 0)
            return -1;
        ssize_t r = recv(t->fd, buf, max, 0);
        if (r >= 0)
            return r;
        if (errno != EAGAIN && errno != EINTR) {
            ph_errore(e, "il servizio meteo non risponde");
            return -1;
        }
    }
}

void ph_tls_chiudi(PhTls *t)
{
    if (t == NULL)
        return;
    if (t->fd >= 0)
        close(t->fd);
    free(t);
}
