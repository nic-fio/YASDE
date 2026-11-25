/* Realizzazione VERA della connessione cifrata del pezzo meteo (codice di
 * sicurezza, scritto da chi coordina). L'interfaccia e le sue promesse sono in
 * tls.h; qui il perche' delle scelte.
 *
 *  - Il certificato si verifica SEMPRE: catena contro le autorita' del sistema,
 *    nome dell'host, TLS >= 1.2, SNI. Non esiste un modo di spegnerla e non
 *    esiste un ripiego in chiaro: se la verifica fallisce si torna errore.
 *  - Nessun certificato di autorita' sta nel pacchetto (invecchierebbe): si
 *    usano le posizioni predefinite di OpenSSL e, in aggiunta, il primo dei
 *    file noti delle distribuzioni che esiste (l'AppImage porta un OpenSSL
 *    suo, che non sa dove la distribuzione ospite tiene il suo elenco).
 *  - Ogni fase rispetta `scadenza_ms` e guarda `annulla` almeno ogni 200 ms:
 *    risoluzione del nome (getaddrinfo_a: getaddrinfo da sola non si puo'
 *    interrompere), collegamento non bloccante, handshake, scrittura, lettura.
 *  - Una chiusura del flusso senza close_notify e' un ERRORE (non si imposta
 *    SSL_OP_IGNORE_UNEXPECTED_EOF): una risposta troncata da qualcuno in mezzo
 *    non deve passare per completa.
 *  - Le frasi d'errore non contengono mai host, indirizzi o dati ricevuti.
 *  - SIGPIPE: il BIO di OpenSSL scrive con write(); il segnale viene bloccato
 *    nel thread per la durata della chiamata e, se nasce li', consumato. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "tls.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "interno.h"

#define PASSO_MS 200            /* ogni quanto si riguarda `annulla` */
#define MAX_HOST 253            /* lunghezza massima di un nome DNS */
#define MAX_INDIRIZZI 6         /* quanti indirizzi dello stesso nome si provano */
/* Tempo dato a ogni indirizzo tranne l'ultimo: un indirizzo che non risponde
 * (tipico: IPv6 configurato ma scartato in silenzio dalla rete) non deve
 * consumare tutta la scadenza e impedire di provare gli altri. */
#define PER_INDIRIZZO_MS 3500
#define MAX_SCRITTURA (1 << 20) /* nessuna richiesta nostra si avvicina a tanto */

#define FRASE_MUTO "il servizio meteo non risponde"
#define FRASE_CERT "certificato del servizio meteo non valido"
#define FRASE_ANNULLATA "richiesta meteo annullata"

struct PhTls {
    int fd;
    SSL_CTX *ctx;
    SSL *ssl;
    /* Dopo un errore o un'operazione lasciata a meta' la connessione si puo'
     * solo chiudere, e senza close_notify (OpenSSL lo vieta dopo un errore). */
    bool rotta;
};

/* Esito delle attese: pronto, scaduto, annullato. */
enum { ATTESA_PRONTO = 1, ATTESA_SCADUTA = 0, ATTESA_ANNULLATA = -1 };

static bool annullata(const atomic_int *annulla) { return annulla != NULL && atomic_load(annulla) != 0; }

/* Millisecondi che restano, limitati al passo; < 0 se la scadenza e' passata. */
static int resto_ms(int64_t scadenza_ms)
{
    int64_t r = scadenza_ms - ph_meteo_ms();
    if (r <= 0)
        return -1;
    return r > PASSO_MS ? PASSO_MS : (int)r;
}

static int errore_attesa(int esito, PhErrore *e)
{
    return ph_errore(e, esito == ATTESA_ANNULLATA ? FRASE_ANNULLATA : FRASE_MUTO);
}

/* Aspetta che `fd` sia pronto per `eventi`. Anche POLLERR/POLLHUP contano come
 * «pronto»: l'errore vero lo dira' la chiamata successiva. */
static int attendi(int fd, short eventi, int64_t scadenza_ms, const atomic_int *annulla)
{
    for (;;) {
        if (annullata(annulla))
            return ATTESA_ANNULLATA;
        int t = resto_ms(scadenza_ms);
        if (t < 0)
            return ATTESA_SCADUTA;
        struct pollfd p = {.fd = fd, .events = eventi, .revents = 0};
        int r = poll(&p, 1, t);
        if (r > 0)
            return ATTESA_PRONTO;
        if (r < 0 && errno != EINTR)
            return ATTESA_SCADUTA;
    }
}

/* ---- SIGPIPE ------------------------------------------------------------ */

typedef struct {
    sigset_t prima;
    bool gia_in_attesa;
    bool bloccato;
} Pipa;

static void pipa_blocca(Pipa *p)
{
    sigset_t s, in_attesa;
    p->bloccato = false;
    p->gia_in_attesa = false;
    if (sigemptyset(&s) != 0 || sigaddset(&s, SIGPIPE) != 0)
        return;
    if (sigpending(&in_attesa) == 0)
        p->gia_in_attesa = sigismember(&in_attesa, SIGPIPE) == 1;
    if (pthread_sigmask(SIG_BLOCK, &s, &p->prima) == 0)
        p->bloccato = true;
}

static void pipa_ripristina(Pipa *p)
{
    if (!p->bloccato)
        return;
    sigset_t s, in_attesa;
    /* Un SIGPIPE nato durante la chiamata (e non gia' in attesa prima) e'
     * nostro: lo si consuma, altrimenti allo sblocco ucciderebbe il programma. */
    if (!p->gia_in_attesa && sigemptyset(&s) == 0 && sigaddset(&s, SIGPIPE) == 0 &&
        sigpending(&in_attesa) == 0 && sigismember(&in_attesa, SIGPIPE) == 1) {
        struct timespec zero = {0, 0};
        (void)sigtimedwait(&s, NULL, &zero);
    }
    (void)pthread_sigmask(SIG_SETMASK, &p->prima, NULL);
}

/* ---- risoluzione del nome ------------------------------------------------ */

/* La richiesta vive nell'heap perche' getaddrinfo_a puo' finire DOPO che noi
 * abbiamo rinunciato (scadenza o annullamento) e scrive ancora nella gaicb:
 * chi dei due arriva per ultimo la libera. stato: 0 in corso, 1 finita (la
 * raccoglie chi aspetta), 2 abbandonata (la libera l'avviso). */
typedef struct {
    struct gaicb cb;
    struct addrinfo cerca;
    char host[MAX_HOST + 1];
    char porta[8];
    atomic_int stato;
} Ricerca;

static void ricerca_libera(Ricerca *r)
{
    if (r->cb.ar_result != NULL)
        freeaddrinfo(r->cb.ar_result);
    free(r);
}

static void ricerca_finita(union sigval v)
{
    Ricerca *r = v.sival_ptr;
    int attesa = 0;
    if (!atomic_compare_exchange_strong(&r->stato, &attesa, 1))
        ricerca_libera(r); /* chi aspettava se n'e' andato */
}

/* Torna la lista degli indirizzi (da liberare con freeaddrinfo) o NULL. */
static struct addrinfo *risolvi(const char *host, uint16_t porta, int64_t scadenza_ms, const atomic_int *annulla,
                                PhErrore *e)
{
    Ricerca *r = calloc(1, sizeof *r);
    if (r == NULL) {
        ph_errore(e, FRASE_MUTO);
        return NULL;
    }
    memcpy(r->host, host, strlen(host) + 1); /* lunghezza gia' controllata da chi chiama */
    snprintf(r->porta, sizeof r->porta, "%u", (unsigned)porta);
    r->cerca.ai_family = AF_UNSPEC;
    r->cerca.ai_socktype = SOCK_STREAM;
    r->cerca.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV;
    r->cb.ar_name = r->host;
    r->cb.ar_service = r->porta;
    r->cb.ar_request = &r->cerca;
    atomic_init(&r->stato, 0);

    struct sigevent avviso;
    memset(&avviso, 0, sizeof avviso);
    avviso.sigev_notify = SIGEV_THREAD;
    avviso.sigev_notify_function = ricerca_finita;
    avviso.sigev_value.sival_ptr = r;

    struct gaicb *lista[1] = {&r->cb};
    if (getaddrinfo_a(GAI_NOWAIT, lista, 1, &avviso) != 0) {
        free(r);
        ph_errore(e, FRASE_MUTO);
        return NULL;
    }

    int esito = ATTESA_PRONTO;
    while (atomic_load(&r->stato) == 0) {
        if (annullata(annulla)) {
            esito = ATTESA_ANNULLATA;
            break;
        }
        int t = resto_ms(scadenza_ms);
        if (t < 0) {
            esito = ATTESA_SCADUTA;
            break;
        }
        struct timespec ts = {.tv_sec = 0, .tv_nsec = (long)t * 1000000L};
        const struct gaicb *const guarda[1] = {&r->cb};
        if (gai_suspend(guarda, 1, &ts) != EAI_AGAIN) {
            /* Finita (o interrotta): l'avviso che cambia lo stato gira su un
             * thread suo e puo' tardare un attimo; non si gira a vuoto. */
            struct timespec poco = {0, 2000000L};
            (void)nanosleep(&poco, NULL);
        }
    }

    if (esito != ATTESA_PRONTO) {
        if (gai_cancel(&r->cb) == EAI_CANCELED) {
            /* Tolta dalla coda prima di partire: nessun avviso arrivera'. */
            ricerca_libera(r);
        } else {
            int attesa = 0;
            if (!atomic_compare_exchange_strong(&r->stato, &attesa, 2))
                ricerca_libera(r); /* era finita proprio adesso */
            /* altrimenti la liberera' ricerca_finita quando la ricerca finisce */
        }
        errore_attesa(esito, e);
        return NULL;
    }

    struct addrinfo *lista_ind = NULL;
    if (gai_error(&r->cb) == 0) {
        lista_ind = r->cb.ar_result;
        r->cb.ar_result = NULL; /* passa a chi chiama */
    }
    ricerca_libera(r);
    if (lista_ind == NULL)
        ph_errore(e, FRASE_MUTO);
    return lista_ind;
}

/* ---- collegamento --------------------------------------------------------- */

static int collega(const struct addrinfo *a, int64_t scadenza_ms, const atomic_int *annulla, int *esito_attesa)
{
    *esito_attesa = ATTESA_PRONTO;
    int fd = socket(a->ai_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, a->ai_protocol);
    if (fd < 0)
        return -1;
    if (connect(fd, a->ai_addr, a->ai_addrlen) == 0)
        return fd;
    if (errno != EINPROGRESS && errno != EINTR) {
        close(fd);
        return -1;
    }
    int att = attendi(fd, POLLOUT, scadenza_ms, annulla);
    if (att != ATTESA_PRONTO) {
        *esito_attesa = att;
        close(fd);
        return -1;
    }
    int err = 0;
    socklen_t n = sizeof err;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &n) != 0 || err != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* ---- autorita' di certificazione ----------------------------------------- */

/* Solo per le prove (tls.h): se impostato, e' l'UNICO elenco di autorita'
 * usato, al posto di quelle del sistema. Il programma vero non lo chiama mai. */
#ifdef PH_METEO_PROVE
/* Esiste SOLO nella compilazione delle prove: nel programma vero la funzione
 * non c'e' proprio, quindi nulla puo' sostituire le autorita' del sistema. */
static const char *ancore_prova;

void ph_tls_prova_ancore(const char *file_pem) { ancore_prova = file_pem; }
#endif

static void carica_autorita(SSL_CTX *ctx)
{
#ifdef PH_METEO_PROVE
    if (ancore_prova != NULL) {
        (void)SSL_CTX_load_verify_locations(ctx, ancore_prova, NULL);
        ERR_clear_error();
        return;
    }
#endif
    /* Le posizioni predefinite dell'OpenSSL in uso (valgono se e' quello del
     * sistema) e le variabili SSL_CERT_FILE / SSL_CERT_DIR. */
    (void)SSL_CTX_set_default_verify_paths(ctx);
    /* In aggiunta il primo elenco noto che esiste: serve quando l'OpenSSL e'
     * quello del pacchetto e non conosce i percorsi della distribuzione. Se
     * nessuno si carica la verifica fallira': e' l'esito giusto, mai un
     * collegamento non verificato. */
    static const char *const noti[] = {
        "/etc/ssl/certs/ca-certificates.crt", /* Debian, Ubuntu, Arch */
        "/etc/pki/tls/certs/ca-bundle.crt",   /* Fedora, RHEL */
        "/etc/ssl/ca-bundle.pem",             /* openSUSE */
        "/etc/ssl/cert.pem",                  /* Alpine e altri */
    };
    for (size_t i = 0; i < sizeof noti / sizeof noti[0]; i++) {
        struct stat st;
        if (stat(noti[i], &st) == 0 && S_ISREG(st.st_mode)) {
            (void)SSL_CTX_load_verify_locations(ctx, noti[i], NULL);
            break;
        }
    }
    ERR_clear_error();
}

/* ---- funzioni dell'interfaccia --------------------------------------------- */

static bool host_valido(const char *host)
{
    if (host == NULL)
        return false;
    size_t n = strnlen(host, MAX_HOST + 1);
    if (n == 0 || n > MAX_HOST)
        return false;
    /* Solo un nome DNS: lettere, cifre, trattino, punto. Niente indirizzi
     * letterali con caratteri strani, niente spazi o caratteri di controllo. */
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)host[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
        if (!ok)
            return false;
    }
    return true;
}

PhTls *ph_tls_apri(const char *host, uint16_t porta, int64_t scadenza_ms, const atomic_int *annulla, PhErrore *e)
{
    if (!host_valido(host) || porta == 0) {
        ph_errore(e, FRASE_MUTO);
        return NULL;
    }
    if (annullata(annulla)) {
        ph_errore(e, FRASE_ANNULLATA);
        return NULL;
    }

    struct addrinfo *ind = risolvi(host, porta, scadenza_ms, annulla, e);
    if (ind == NULL)
        return NULL;

    int fd = -1;
    int att = ATTESA_PRONTO;
    int provati = 0;
    for (const struct addrinfo *a = ind; a != NULL && fd < 0 && provati < MAX_INDIRIZZI; a = a->ai_next) {
        if (a->ai_family != AF_INET && a->ai_family != AF_INET6)
            continue;
        provati++;
        /* L'ultimo indirizzo ha tutto il tempo che resta; gli altri una parte. */
        int64_t fine = scadenza_ms;
        if (a->ai_next != NULL && provati < MAX_INDIRIZZI) {
            int64_t parte = ph_meteo_ms() + PER_INDIRIZZO_MS;
            if (parte < fine)
                fine = parte;
        }
        fd = collega(a, fine, annulla, &att);
        /* Si rinuncia agli altri solo per annullamento o a scadenza TOTALE. */
        if (fd < 0 && (att == ATTESA_ANNULLATA || ph_meteo_ms() >= scadenza_ms))
            break;
    }
    freeaddrinfo(ind);
    if (fd < 0) {
        errore_attesa(att == ATTESA_ANNULLATA ? ATTESA_ANNULLATA : ATTESA_SCADUTA, e);
        return NULL;
    }

    PhTls *t = calloc(1, sizeof *t);
    if (t == NULL) {
        close(fd);
        ph_errore(e, FRASE_MUTO);
        return NULL;
    }
    t->fd = fd;

    ERR_clear_error();
    t->ctx = SSL_CTX_new(TLS_client_method());
    if (t->ctx == NULL || SSL_CTX_set_min_proto_version(t->ctx, TLS1_2_VERSION) != 1)
        goto muto;
    SSL_CTX_set_verify(t->ctx, SSL_VERIFY_PEER, NULL);
    (void)SSL_CTX_set_mode(t->ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    carica_autorita(t->ctx);

    t->ssl = SSL_new(t->ctx);
    if (t->ssl == NULL)
        goto muto;
    /* Il nome serve due volte: SNI (quale sito chiediamo) e verifica (il
     * certificato deve essere di QUEL nome; niente caratteri jolly parziali). */
    SSL_set_hostflags(t->ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    if (SSL_set_tlsext_host_name(t->ssl, host) != 1 || SSL_set1_host(t->ssl, host) != 1 ||
        SSL_set_fd(t->ssl, t->fd) != 1)
        goto muto;

    for (;;) {
        Pipa p;
        pipa_blocca(&p);
        ERR_clear_error();
        int r = SSL_connect(t->ssl);
        int err = r == 1 ? SSL_ERROR_NONE : SSL_get_error(t->ssl, r);
        pipa_ripristina(&p);
        if (r == 1)
            break;
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            /* Distinguere serve solo alla frase: l'esito e' comunque un errore. */
            bool cert = SSL_get_verify_result(t->ssl) != X509_V_OK;
            ph_errore(e, cert ? FRASE_CERT : FRASE_MUTO);
            goto chiudi;
        }
        att = attendi(t->fd, err == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, scadenza_ms, annulla);
        if (att != ATTESA_PRONTO) {
            errore_attesa(att, e);
            goto chiudi;
        }
    }

    /* Cintura e bretelle: handshake riuscito vuol gia' dire verifica riuscita,
     * ma lo si ricontrolla prima di consegnare la connessione. */
    if (SSL_get_verify_result(t->ssl) != X509_V_OK || SSL_get0_peer_certificate(t->ssl) == NULL) {
        ph_errore(e, FRASE_CERT);
        goto chiudi;
    }
    ERR_clear_error();
    return t;

muto:
    ph_errore(e, FRASE_MUTO);
chiudi:
    ERR_clear_error();
    if (t->ssl != NULL)
        SSL_free(t->ssl);
    if (t->ctx != NULL)
        SSL_CTX_free(t->ctx);
    close(t->fd);
    free(t);
    return NULL;
}

int ph_tls_scrivi(PhTls *t, const void *dati, size_t n, int64_t scadenza_ms, const atomic_int *annulla, PhErrore *e)
{
    if (t == NULL || (dati == NULL && n > 0) || n > MAX_SCRITTURA)
        return ph_errore(e, FRASE_MUTO);
    const unsigned char *p = dati;
    size_t fatti = 0;
    while (fatti < n) {
        if (annullata(annulla)) {
            if (fatti > 0)
                t->rotta = true; /* scrittura lasciata a meta' */
            return ph_errore(e, FRASE_ANNULLATA);
        }
        Pipa pp;
        pipa_blocca(&pp);
        ERR_clear_error();
        size_t scritti = 0;
        int r = SSL_write_ex(t->ssl, p + fatti, n - fatti, &scritti);
        int err = r == 1 ? SSL_ERROR_NONE : SSL_get_error(t->ssl, r);
        pipa_ripristina(&pp);
        if (r == 1) {
            if (scritti == 0 || scritti > n - fatti)
                return ph_errore(e, FRASE_MUTO);
            fatti += scritti;
            continue;
        }
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            ERR_clear_error();
            t->rotta = true;
            return ph_errore(e, FRASE_MUTO);
        }
        int att = attendi(t->fd, err == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, scadenza_ms, annulla);
        if (att != ATTESA_PRONTO) {
            t->rotta = true; /* scrittura lasciata a meta' */
            return errore_attesa(att, e);
        }
    }
    return 0;
}

ssize_t ph_tls_leggi(PhTls *t, void *buf, size_t max, int64_t scadenza_ms, const atomic_int *annulla, PhErrore *e)
{
    if (t == NULL || buf == NULL || max == 0) {
        ph_errore(e, FRASE_MUTO);
        return -1;
    }
    if (max > (size_t)INT32_MAX)
        max = (size_t)INT32_MAX;
    for (;;) {
        if (annullata(annulla)) {
            ph_errore(e, FRASE_ANNULLATA);
            return -1;
        }
        Pipa pp;
        pipa_blocca(&pp);
        ERR_clear_error();
        size_t letti = 0;
        int r = SSL_read_ex(t->ssl, buf, max, &letti);
        int err = r == 1 ? SSL_ERROR_NONE : SSL_get_error(t->ssl, r);
        pipa_ripristina(&pp);
        if (r == 1) {
            if (letti == 0 || letti > max) { /* non deve succedere: mai fidarsi */
                ph_errore(e, FRASE_MUTO);
                return -1;
            }
            return (ssize_t)letti;
        }
        if (err == SSL_ERROR_ZERO_RETURN)
            return 0; /* close_notify: fine pulita */
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            /* Compresa la chiusura senza close_notify: e' un troncamento. */
            ERR_clear_error();
            t->rotta = true;
            ph_errore(e, FRASE_MUTO);
            return -1;
        }
        int att = attendi(t->fd, err == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, scadenza_ms, annulla);
        if (att != ATTESA_PRONTO) {
            t->rotta = true;
            errore_attesa(att, e);
            return -1;
        }
    }
}

void ph_tls_chiudi(PhTls *t)
{
    if (t == NULL)
        return;
    if (t->ssl != NULL) {
        if (!t->rotta) {
            /* Un solo tentativo di close_notify, senza aspettare la risposta:
             * la socket non blocca, quindi non puo' fermarsi qui. */
            Pipa p;
            pipa_blocca(&p);
            ERR_clear_error();
            (void)SSL_shutdown(t->ssl);
            pipa_ripristina(&p);
            ERR_clear_error();
        }
        SSL_free(t->ssl);
    }
    if (t->ctx != NULL)
        SSL_CTX_free(t->ctx);
    if (t->fd >= 0)
        close(t->fd);
    free(t);
}
