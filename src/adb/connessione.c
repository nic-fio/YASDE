/* Connessione ADB: TCP, scambio CNXN/STLS, TLS 1.3, thread lettore, canali.
 *
 * Schema dei blocchi (mai presi insieme tranne dove indicato):
 *  - m_stato     tabella dei canali e tutto lo stato dei canali, viva, riferimenti;
 *  - m_scrittura un pacchetto per volta sul socket (anche durante le attese
 *                di poll: una SSL_write incompleta va ripetuta con gli stessi
 *                argomenti, quindi nessun altro scrive nel frattempo);
 *  - m_ssl       ogni singola chiamata SSL_read/SSL_write (un SSL non e'
 *                usabile da due thread insieme). Il socket e' non bloccante e
 *                si aspetta con poll() FUORI da questo blocco: cosi' lettore e
 *                scrittori non si bloccano a vicenda.
 * Ordine: m_scrittura prima di m_ssl; m_stato non si tiene mentre si fa I/O. */
#include "adb/adb.h"
#include "adb/interno.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Quanto aspettare, al massimo, un OKAY o la scrittura sul socket: il telefono
 * che smette di rispondere non deve bloccare un thread per sempre. */
#define ATTESA_SCRITTURA_MS 30000
/* Oltre questa quantita' di byte non letti in un canale l'OKAY al telefono si
 * trattiene: un lettore lento frena il telefono invece di far crescere la memoria. */
#define SOGLIA_CODA (2u * PH_ADB_MAX_DATI)
/* Con "delayed_ack": finestra di ricezione che concediamo al telefono per ogni
 * canale (byte che puo' mandare senza nostro OKAY) e quanti byte letti si
 * accumulano prima di confermarli: ogni OKAY allarga la finestra di quanto
 * chi legge ha davvero consumato, quindi la coda non supera la finestra
 * (piu' al massimo l'ultimo pacchetto, che il telefono puo' mandare quando
 * la finestra e' ancora positiva ma piu' piccola del pacchetto). */
#define FINESTRA_RICEZIONE (4u * 1024u * 1024u)
#define ACK_SOGLIA (FINESTRA_RICEZIONE / 4u)

#define IO_OK 0
#define IO_ERRORE (-1)
#define IO_TEMPO (-2)
#define IO_FINE (-3)

enum StatoCanale { CANALE_APERTURA, CANALE_APERTO, CANALE_RIFIUTATO };

struct PhCanale {
    PhAdb *adb;
    PhCanale *prossimo;
    uint32_t locale;
    uint32_t remoto;
    enum StatoCanale stato;
    pthread_cond_t cv; /* usa m_stato */
    PhBuffer coda;     /* dati in arrivo non ancora letti */
    size_t letti;      /* byte gia' consumati all'inizio di coda */
    bool puo_scrivere; /* l'ultimo WRTE e' stato confermato con OKAY */
    bool remoto_chiuso;
    bool clse_inviato;
    int64_t finestra_invio; /* delayed_ack: byte che possiamo ancora mandare senza OKAY */
    uint64_t debito;        /* delayed_ack: byte ricevuti e non ancora confermati */
    uint64_t da_confermare; /* delayed_ack: byte gia' letti da chi usa il canale, non ancora confermati */
    bool okay_trattenuto; /* l'OKAY dell'ultimo WRTE parte quando la coda scende sotto soglia */
    bool chiudendo;
    int in_uso; /* thread dentro leggi/scrivi su questo canale */
};

struct PhAdb {
    int fd;
    int sveglia; /* eventfd: una volta scritto resta "pronto" e sveglia ogni poll */
    SSL_CTX *ctx;
    SSL *ssl;
    pthread_mutex_t m_ssl;
    pthread_mutex_t m_scrittura;
    pthread_mutex_t m_stato;
    bool viva;
    bool chiusa; /* ph_adb_chiudi gia' chiamata */
    PhErrore caduta;
    unsigned long err_ssl; /* ultimo errore OpenSSL visto in lettura (per il rifiuto del certificato) */
    pthread_t lettore;
    bool lettore_avviato;
    uint8_t *buf_lettore; /* PH_ADB_MAX_DATI byte */
    bool ack_ritardato;   /* telefono e noi abbiamo concordato "delayed_ack" */
    uint32_t max_dati;    /* il massimo per pacchetto che il telefono accetta */
    char *banner;
    PhCanale *canali;
    uint32_t prossimo_id;
    int riferimenti; /* PhAdb + canali non ancora chiusi */
};

/* ---- Tempo ---------------------------------------------------------------*/
static int64_t ora_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Scadenza assoluta; -1 = senza limite. */
static int64_t scadenza_da(int timeout_ms)
{
    return timeout_ms < 0 ? -1 : ora_ms() + timeout_ms;
}

/* Millisecondi che restano; -1 se senza limite, 0 se scaduta. */
static int resta_ms(int64_t scadenza)
{
    if (scadenza < 0)
        return -1;
    int64_t r = scadenza - ora_ms();
    if (r <= 0)
        return 0;
    return r > INT_MAX ? INT_MAX : (int)r;
}

/* ---- I/O sul socket (in chiaro prima del TLS, poi attraverso SSL) --------*/
/* Aspetta che il socket sia pronto. IO_OK, IO_TEMPO, o IO_ERRORE se la
 * connessione e' stata chiusa/e' caduta (eventfd). */
static int attendi_fd(PhAdb *a, short eventi, int64_t scadenza, PhErrore *e)
{
    struct pollfd p[2] = {{a->fd, eventi, 0}, {a->sveglia, POLLIN, 0}};
    for (;;) {
        int r = poll(p, 2, resta_ms(scadenza));
        if (r < 0) {
            if (errno == EINTR)
                continue;
            ph_errore(e, "poll fallita: %s", strerror(errno));
            return IO_ERRORE;
        }
        if (r == 0) {
            ph_errore(e, "tempo scaduto");
            return IO_TEMPO;
        }
        if (p[1].revents != 0) {
            ph_errore(e, "connessione chiusa");
            return IO_ERRORE;
        }
        return IO_OK;
    }
}

static void testo_ssl(PhErrore *e, const char *contesto)
{
    char msg[160] = "errore sconosciuto";
    unsigned long c = ERR_get_error();
    if (c != 0)
        ERR_error_string_n(c, msg, sizeof msg);
    ERR_clear_error();
    ph_errore(e, "%s: %s", contesto, msg);
}

/* Legge da 1 a n byte. >0 byte letti, IO_FINE, IO_ERRORE, IO_TEMPO. */
static ssize_t io_leggi_una(PhAdb *a, void *buf, size_t n, int64_t scadenza, PhErrore *e)
{
    if (n > INT_MAX)
        n = INT_MAX;
    for (;;) {
        if (a->ssl == NULL) {
            ssize_t r = read(a->fd, buf, n);
            if (r > 0)
                return r;
            if (r == 0)
                return IO_FINE;
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ph_errore(e, "lettura dal telefono: %s", strerror(errno));
                return IO_ERRORE;
            }
            int w = attendi_fd(a, POLLIN, scadenza, e);
            if (w != IO_OK)
                return w;
            continue;
        }
        pthread_mutex_lock(&a->m_ssl);
        ERR_clear_error();
        int r = SSL_read(a->ssl, buf, (int)n);
        int err = r > 0 ? SSL_ERROR_NONE : SSL_get_error(a->ssl, r);
        int en = errno;
        pthread_mutex_unlock(&a->m_ssl);
        if (r > 0)
            return r;
        switch (err) {
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE: {
            int w = attendi_fd(a, err == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, scadenza, e);
            if (w != IO_OK)
                return w;
            break;
        }
        case SSL_ERROR_ZERO_RETURN:
            return IO_FINE;
        case SSL_ERROR_SYSCALL:
            /* chiusura senza close_notify (r == 0 senza errno) = fine del flusso */
            if (r == 0 || en == 0) {
                ERR_clear_error();
                return IO_FINE;
            }
            a->err_ssl = ERR_peek_last_error();
            testo_ssl(e, "lettura dal telefono");
            return IO_ERRORE;
        default:
            /* OpenSSL 3 segnala la chiusura senza close_notify come errore SSL */
            if (err == SSL_ERROR_SSL &&
                ERR_GET_REASON(ERR_peek_last_error()) == SSL_R_UNEXPECTED_EOF_WHILE_READING) {
                ERR_clear_error();
                return IO_FINE;
            }
            a->err_ssl = ERR_peek_last_error();
            testo_ssl(e, "lettura dal telefono");
            return IO_ERRORE;
        }
    }
}

static int io_leggi_esatto(PhAdb *a, void *buf, size_t n, int64_t scadenza, PhErrore *e)
{
    uint8_t *p = buf;
    while (n > 0) {
        ssize_t r = io_leggi_una(a, p, n, scadenza, e);
        if (r == IO_FINE)
            return ph_errore(e, "il telefono ha chiuso la connessione"), IO_FINE;
        if (r < 0)
            return (int)r;
        p += r;
        n -= (size_t)r;
    }
    return IO_OK;
}

/* Scrive tutto. IO_OK, IO_ERRORE, IO_TEMPO. Chiamare con m_scrittura preso
 * (dopo il TLS) o dal solo thread di collegamento (prima). */
static int io_scrivi_tutto(PhAdb *a, const void *buf, size_t n, int64_t scadenza, PhErrore *e)
{
    const uint8_t *p = buf;
    while (n > 0) {
        size_t pezzo = n > INT_MAX ? INT_MAX : n;
        if (a->ssl == NULL) {
            ssize_t r = send(a->fd, p, pezzo, MSG_NOSIGNAL);
            if (r > 0) {
                p += r;
                n -= (size_t)r;
                continue;
            }
            if (r < 0 && errno == EINTR)
                continue;
            if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                ph_errore(e, "scrittura verso il telefono: %s", strerror(errno));
                return IO_ERRORE;
            }
            int w = attendi_fd(a, POLLOUT, scadenza, e);
            if (w != IO_OK)
                return w;
            continue;
        }
        pthread_mutex_lock(&a->m_ssl);
        ERR_clear_error();
        int r = SSL_write(a->ssl, p, (int)pezzo);
        int err = r > 0 ? SSL_ERROR_NONE : SSL_get_error(a->ssl, r);
        pthread_mutex_unlock(&a->m_ssl);
        if (r > 0) {
            p += r;
            n -= (size_t)r;
            continue;
        }
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            /* si riprova con gli stessi argomenti, come vuole OpenSSL */
            int w = attendi_fd(a, err == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, scadenza, e);
            if (w != IO_OK)
                return w;
            continue;
        }
        testo_ssl(e, "scrittura verso il telefono");
        return IO_ERRORE;
    }
    return IO_OK;
}

/* ---- Pacchetti -----------------------------------------------------------*/
/* Dopo il TLS un errore di scrittura lascia il flusso a meta': la
 * connessione e' da buttare. Vedi cadi(). */
static void cadi(PhAdb *a, const char *motivo);

static int scrivi_pacchetto(PhAdb *a, uint32_t comando, uint32_t arg0, uint32_t arg1,
                            const void *dati, size_t n, int64_t scadenza, PhErrore *e)
{
    PhPacchettoAdb p;
    uint8_t blocco[PH_ADB_INTESTAZIONE + 4096];
    ph_pacchetto_prepara(&p, comando, arg0, arg1, dati, n);
    ph_pacchetto_codifica(&p, blocco);

    pthread_mutex_lock(&a->m_scrittura);
    int r;
    if (n <= sizeof blocco - PH_ADB_INTESTAZIONE) {
        /* un solo invio: un solo record TLS per i pacchetti piccoli */
        if (n > 0)
            memcpy(blocco + PH_ADB_INTESTAZIONE, dati, n);
        r = io_scrivi_tutto(a, blocco, PH_ADB_INTESTAZIONE + n, scadenza, e);
    } else {
        r = io_scrivi_tutto(a, blocco, PH_ADB_INTESTAZIONE, scadenza, e);
        if (r == IO_OK)
            r = io_scrivi_tutto(a, dati, n, scadenza, e);
    }
    pthread_mutex_unlock(&a->m_scrittura);
    return r;
}

/* Come scrivi_pacchetto, ma dopo il collegamento: ogni fallimento fa cadere
 * la connessione. */
static int invia(PhAdb *a, uint32_t comando, uint32_t arg0, uint32_t arg1, const void *dati,
                 size_t n, PhErrore *e)
{
    PhErrore loc;
    int r = scrivi_pacchetto(a, comando, arg0, arg1, dati, n, scadenza_da(ATTESA_SCRITTURA_MS), &loc);
    if (r != IO_OK) {
        cadi(a, loc.testo);
        ph_errore(e, "%s", loc.testo);
        return -1;
    }
    return 0;
}

/* Legge un pacchetto; i dati vanno in a->buf_lettore. */
static int leggi_pacchetto(PhAdb *a, PhPacchettoAdb *p, int64_t scadenza, PhErrore *e)
{
    uint8_t ih[PH_ADB_INTESTAZIONE];
    int r = io_leggi_esatto(a, ih, sizeof ih, scadenza, e);
    if (r != IO_OK)
        return r;
    if (ph_pacchetto_decodifica(ih, p, e) != 0)
        return IO_ERRORE;
    if (p->lunghezza > 0)
        return io_leggi_esatto(a, a->buf_lettore, p->lunghezza, scadenza, e);
    return IO_OK;
}

/* ---- Vita della connessione ----------------------------------------------*/
static void adb_libera(PhAdb *a)
{
    if (a->ssl != NULL)
        SSL_free(a->ssl);
    if (a->ctx != NULL)
        SSL_CTX_free(a->ctx);
    if (a->fd >= 0)
        close(a->fd);
    if (a->sveglia >= 0)
        close(a->sveglia);
    pthread_mutex_destroy(&a->m_ssl);
    pthread_mutex_destroy(&a->m_scrittura);
    pthread_mutex_destroy(&a->m_stato);
    free(a->buf_lettore);
    free(a->banner);
    free(a);
}

static void adb_rilascia(PhAdb *a)
{
    pthread_mutex_lock(&a->m_stato);
    int n = --a->riferimenti;
    pthread_mutex_unlock(&a->m_stato);
    if (n == 0)
        adb_libera(a);
}

static void cadi(PhAdb *a, const char *motivo)
{
    pthread_mutex_lock(&a->m_stato);
    if (a->viva) {
        a->viva = false;
        snprintf(a->caduta.testo, sizeof a->caduta.testo, "%s", motivo);
        for (PhCanale *c = a->canali; c != NULL; c = c->prossimo)
            pthread_cond_broadcast(&c->cv);
    }
    pthread_mutex_unlock(&a->m_stato);
    /* sveglia chiunque sia in poll(); l'eventfd resta pronto per sempre */
    uint64_t uno = 1;
    ssize_t r = write(a->sveglia, &uno, sizeof uno);
    (void)r;
}

static PhCanale *trova_canale(PhAdb *a, uint32_t locale)
{
    for (PhCanale *c = a->canali; c != NULL; c = c->prossimo)
        if (c->locale == locale)
            return c;
    return NULL;
}

static void togli_canale(PhAdb *a, PhCanale *c)
{
    for (PhCanale **pp = &a->canali; *pp != NULL; pp = &(*pp)->prossimo) {
        if (*pp == c) {
            *pp = c->prossimo;
            return;
        }
    }
}

static uint32_t leggi32(const uint8_t *d)
{
    return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
}

/* La caratteristica e' nel banner come elemento esatto dell'elenco "features=a,b,c". */
static bool ha_caratteristica(const char *banner, const char *nome)
{
    /* "features=" conta solo all'inizio di un campo (inizio, dopo ':' o dopo ';'): non dentro
     * il valore di un altro campo, per esempio un nome di modello */
    const char *f = banner;
    for (;;) {
        f = strstr(f, "features=");
        if (f == NULL)
            return false;
        if (f == banner || f[-1] == ':' || f[-1] == ';')
            break;
        f += 1;
    }
    f += strlen("features=");
    size_t ln = strlen(nome);
    while (*f != '\0' && *f != ';') {
        const char *fine = f;
        while (*fine != '\0' && *fine != ',' && *fine != ';')
            fine++;
        if ((size_t)(fine - f) == ln && memcmp(f, nome, ln) == 0)
            return true;
        f = *fine == ',' ? fine + 1 : fine;
    }
    return false;
}

/* Smista un pacchetto ricevuto. Torna -1 se la connessione non e' piu' usabile. */
static int smista(PhAdb *a, const PhPacchettoAdb *p, PhErrore *e)
{
    const uint8_t *dati = a->buf_lettore;
    switch (p->comando) {
    case PH_ADB_OKAY: {
        bool sconosciuto = false, malformato = false;
        pthread_mutex_lock(&a->m_stato);
        PhCanale *c = trova_canale(a, p->arg1);
        /* con delayed_ack ogni OKAY di un canale vivo porta esattamente 4 byte, valore <= INT32_MAX */
        if (c != NULL && a->ack_ritardato && p->arg0 != 0 &&
            (c->stato == CANALE_APERTURA || (c->stato == CANALE_APERTO && c->remoto == p->arg0)) &&
            (p->lunghezza != 4 || leggi32(dati) > (uint32_t)INT32_MAX))
            malformato = true;
        if (malformato) {
            c = NULL; /* niente da aggiornare: la connessione cade (apri e scrivi leggono il motivo) */
        } else if (c == NULL) {
            sconosciuto = true;
        } else if (c->stato == CANALE_APERTURA) {
            if (p->arg0 == 0) { /* id remoto 0 non esiste: e' un rifiuto */
                c->stato = CANALE_RIFIUTATO;
                c->remoto_chiuso = true;
            } else {
                c->remoto = p->arg0;
                c->stato = CANALE_APERTO;
                if (a->ack_ritardato) /* l'OKAY dell'apertura porta la finestra che il telefono ci concede */
                    c->finestra_invio = p->lunghezza >= 4 ? (int64_t)leggi32(dati) : 0;
                else
                    c->puo_scrivere = true;
            }
            pthread_cond_broadcast(&c->cv);
        } else if (c->stato == CANALE_APERTO && c->remoto == p->arg0) {
            if (!a->ack_ritardato) {
                c->puo_scrivere = true;
            } else if (p->lunghezza >= 4) {
                /* OKAY con carico: i byte di cui si allarga la finestra di invio */
                c->finestra_invio += leggi32(dati);
            }
            pthread_cond_broadcast(&c->cv);
        }
        pthread_mutex_unlock(&a->m_stato);
        if (malformato)
            return ph_errore(e, "OKAY del telefono con carico non valido (delayed_ack)");
        /* risposta tardiva a un'apertura scaduta: si chiude il servizio sul telefono */
        if (sconosciuto && p->arg0 != 0 && invia(a, PH_ADB_CLSE, p->arg1, p->arg0, NULL, 0, e) != 0)
            return -1;
        return 0;
    }
    case PH_ADB_WRTE: {
        int accettato = 0, memoria = 0, violazione = 0, trattenuto = 0;
        pthread_mutex_lock(&a->m_stato);
        PhCanale *c = trova_canale(a, p->arg1);
        if (c != NULL && c->stato == CANALE_APERTO && c->remoto == p->arg0 && !c->chiudendo &&
            !c->remoto_chiuso) {
            if (a->ack_ritardato) {
                /* il telefono puo' mandare finche' la sua finestra e' positiva (l'ultimo
                 * pacchetto puo' sforarla); con finestra esaurita non puo' mandare nulla */
                if (c->debito >= FINESTRA_RICEZIONE)
                    violazione = 1;
                else if (ph_buffer_aggiungi(&c->coda, dati, p->lunghezza) == 0) {
                    c->debito += p->lunghezza;
                    accettato = 2; /* nessun OKAY ora: parte quando chi legge consuma */
                    pthread_cond_broadcast(&c->cv);
                } else
                    memoria = 1;
            } else if (c->okay_trattenuto) {
                violazione = 1; /* ha mandato dati senza il nostro OKAY */
            } else if (ph_buffer_aggiungi(&c->coda, dati, p->lunghezza) == 0) {
                if (c->coda.lunghezza - c->letti >= SOGLIA_CODA) {
                    c->okay_trattenuto = true;
                    trattenuto = 1;
                } else {
                    accettato = 1;
                }
                pthread_cond_broadcast(&c->cv);
            } else {
                memoria = 1;
            }
        }
        pthread_mutex_unlock(&a->m_stato);
        if (memoria)
            return ph_errore(e, "memoria finita");
        if (violazione)
            return ph_errore(e, "il telefono ha violato il controllo di flusso");
        if (trattenuto || accettato == 2)
            return 0; /* l'OKAY partira' quando chi legge avra' svuotato la coda */
        /* l'OKAY parte a dati gia' in coda; per un canale sconosciuto (chiuso da noi
         * un attimo fa) si risponde CLSE cosi' il telefono smette di mandare */
        if (invia(a, accettato ? PH_ADB_OKAY : PH_ADB_CLSE, p->arg1, p->arg0, NULL, 0, e) != 0)
            return -1;
        return 0;
    }
    case PH_ADB_CLSE: {
        bool rispondi = false;
        uint32_t remoto = p->arg0;
        pthread_mutex_lock(&a->m_stato);
        PhCanale *c = trova_canale(a, p->arg1);
        if (c != NULL) {
            if (c->stato == CANALE_APERTURA) {
                if (p->arg0 == 0) { /* il telefono rifiuta il servizio */
                    c->stato = CANALE_RIFIUTATO;
                    c->remoto_chiuso = true;
                    pthread_cond_broadcast(&c->cv);
                }
            } else if (c->stato == CANALE_APERTO && c->remoto == p->arg0) {
                if (!c->clse_inviato) {
                    c->clse_inviato = true;
                    rispondi = true;
                }
                c->remoto_chiuso = true;
                pthread_cond_broadcast(&c->cv);
            }
        }
        pthread_mutex_unlock(&a->m_stato);
        if (rispondi && invia(a, PH_ADB_CLSE, p->arg1, remoto, NULL, 0, e) != 0)
            return -1;
        return 0;
    }
    default:
        return 0; /* CNXN/AUTH/STLS fuori tempo e comandi sconosciuti: si ignorano */
    }
}

static void *lettore(void *arg)
{
    PhAdb *a = arg;
    for (;;) {
        PhErrore e;
        PhPacchettoAdb p;
        int r = leggi_pacchetto(a, &p, -1, &e);
        if (r != IO_OK) {
            cadi(a, e.testo);
            break;
        }
        if (smista(a, &p, &e) != 0) {
            cadi(a, e.testo);
            break;
        }
    }
    return NULL;
}

/* ---- Collegamento --------------------------------------------------------*/
static void ignora_sigpipe(void)
{
    /* Le scritture TLS passano da write() di OpenSSL: senza questo un
     * telefono che chiude di colpo ucciderebbe il programma con SIGPIPE.
     * Si cambia solo se nessuno ha gia' scelto un comportamento. */
    struct sigaction sa;
    if (sigaction(SIGPIPE, NULL, &sa) == 0 && sa.sa_handler == SIG_DFL) {
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGPIPE, &sa, NULL);
    }
}

static int connetti_tcp(PhAdb *a, const char *host, uint16_t porta, int64_t scadenza, PhErrore *e)
{
    char sporta[8];
    snprintf(sporta, sizeof sporta, "%u", (unsigned)porta);
    struct addrinfo hint = {0}, *lista = NULL;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_family = AF_UNSPEC;
    int g = getaddrinfo(host, sporta, &hint, &lista);
    if (g != 0)
        return ph_errore(e, "indirizzo del telefono non valido: %s", gai_strerror(g));
    PhErrore ultimo = {{0}};
    snprintf(ultimo.testo, sizeof ultimo.testo, "nessun indirizzo utilizzabile");
    for (struct addrinfo *ai = lista; ai != NULL; ai = ai->ai_next) {
        int s = socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, ai->ai_protocol);
        if (s < 0)
            continue;
        a->fd = s;
        int rc = connect(s, ai->ai_addr, ai->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            if (attendi_fd(a, POLLOUT, scadenza, &ultimo) == IO_OK) {
                int so = 0;
                socklen_t l = sizeof so;
                if (getsockopt(s, SOL_SOCKET, SO_ERROR, &so, &l) == 0 && so == 0)
                    rc = 0;
                else
                    ph_errore(&ultimo, "connessione rifiutata: %s", strerror(so));
            }
        } else if (rc != 0) {
            ph_errore(&ultimo, "connessione rifiutata: %s", strerror(errno));
        }
        if (rc == 0) {
            int uno = 1;
            /* Telefono sparito dal Wi-Fi senza chiudere: senza questi parametri la
             * lettura resterebbe bloccata per ore. Cosi' fallisce in circa 10 s. */
            const int keepidle = 5, keepintvl = 2, keepcnt = 3, user_timeout = 10000;
            if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &uno, sizeof uno) != 0 ||
                setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &uno, sizeof uno) != 0 ||
                setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof keepidle) != 0 ||
                setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof keepintvl) != 0 ||
                setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof keepcnt) != 0 ||
                setsockopt(s, IPPROTO_TCP, TCP_USER_TIMEOUT, &user_timeout, sizeof user_timeout) != 0) {
                ph_errore(&ultimo, "impostazione del socket: %s", strerror(errno));
                close(s);
                a->fd = -1;
                continue;
            }
            freeaddrinfo(lista);
            return 0;
        }
        close(s);
        a->fd = -1;
    }
    freeaddrinfo(lista);
    return ph_errore(e, "%s", ultimo.testo);
}

static bool errore_ssl_di_rifiuto(unsigned long codice)
{
    int motivo = ERR_GET_REASON(codice);
    return motivo == SSL_R_TLSV13_ALERT_CERTIFICATE_REQUIRED ||
           motivo == SSL_R_TLSV1_ALERT_UNKNOWN_CA || motivo == SSL_R_SSLV3_ALERT_BAD_CERTIFICATE ||
           motivo == SSL_R_TLSV1_ALERT_ACCESS_DENIED ||
           motivo == SSL_R_SSLV3_ALERT_CERTIFICATE_UNKNOWN;
}

/* Handshake TLS 1.3 con il certificato del PC. Il certificato del telefono
 * non si verifica (e' autofirmato, come fa adb): cio' che conta e' che il
 * telefono riconosca NOI, ed e' lui a deciderlo in base alla nostra chiave. */
static int handshake_tls(PhAdb *a, const PhChiave *chiave, int64_t scadenza, bool *rifiutato,
                         PhErrore *e)
{
    a->ctx = SSL_CTX_new(TLS_client_method());
    if (a->ctx == NULL || SSL_CTX_set_min_proto_version(a->ctx, TLS1_3_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(a->ctx, TLS1_3_VERSION) != 1 ||
        SSL_CTX_use_certificate(a->ctx, ph_chiave_x509(chiave)) != 1 ||
        SSL_CTX_use_PrivateKey(a->ctx, ph_chiave_evp(chiave)) != 1) {
        testo_ssl(e, "preparazione del TLS");
        return -1;
    }
    SSL_CTX_set_verify(a->ctx, SSL_VERIFY_NONE, NULL);
    SSL *ssl = SSL_new(a->ctx);
    if (ssl == NULL || SSL_set_fd(ssl, a->fd) != 1) {
        SSL_free(ssl);
        testo_ssl(e, "preparazione del TLS");
        return -1;
    }
    SSL_set_connect_state(ssl);
    for (;;) {
        ERR_clear_error();
        int r = SSL_connect(ssl);
        if (r == 1)
            break;
        int err = SSL_get_error(ssl, r);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            if (attendi_fd(a, err == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, scadenza, e) != IO_OK) {
                SSL_free(ssl);
                ph_errore_contesto(e, "saluto TLS");
                return -1;
            }
            continue;
        }
        if (err == SSL_ERROR_SYSCALL && r == 0)
            *rifiutato = true; /* il telefono ha chiuso durante il saluto */
        else if (err == SSL_ERROR_SSL && errore_ssl_di_rifiuto(ERR_peek_last_error()))
            *rifiutato = true;
        testo_ssl(e, "saluto TLS");
        SSL_free(ssl);
        return -1;
    }
    a->ssl = ssl;
    return 0;
}

PhAdb *ph_adb_collega(const char *host, uint16_t porta, const PhChiave *chiave, int timeout_ms,
                      bool *non_abbinato, PhErrore *e)
{
    static pthread_once_t una_volta = PTHREAD_ONCE_INIT;
    pthread_once(&una_volta, ignora_sigpipe);
    if (non_abbinato != NULL)
        *non_abbinato = false;

    PhAdb *a = calloc(1, sizeof *a);
    if (a == NULL) {
        ph_errore(e, "memoria finita");
        return NULL;
    }
    a->fd = -1;
    a->riferimenti = 1;
    a->prossimo_id = 1;
    pthread_mutex_init(&a->m_ssl, NULL);
    pthread_mutex_init(&a->m_scrittura, NULL);
    pthread_mutex_init(&a->m_stato, NULL);
    a->sveglia = eventfd(0, EFD_CLOEXEC);
    a->buf_lettore = malloc(PH_ADB_MAX_DATI);
    if (a->sveglia < 0 || a->buf_lettore == NULL) {
        ph_errore(e, "risorse di sistema esaurite");
        goto errore;
    }

    int64_t scadenza = scadenza_da(timeout_ms);
    PhPacchettoAdb p;
    bool rifiutato = false;

    if (connetti_tcp(a, host, porta, scadenza, e) != 0)
        goto errore;

    /* 1. CNXN in chiaro; senza TLS il telefono risponde STLS (o AUTH se non ha TLS). */
    /* Il ';' finale e' necessario: adbd legge il banner come stringa C++ con il '\0'
     * incluso, e senza ';' l'ultima caratteristica diventerebbe "delayed_ack\0", non
     * riconosciuta (con il '\0' dentro, adbd rifiutava ogni OPEN con arg1 != 0). */
    static const char presentazione[] = "host::features=shell_v2,cmd,delayed_ack;";
    int r = scrivi_pacchetto(a, PH_ADB_CNXN, PH_ADB_VERSIONE, PH_ADB_MAX_DATI, presentazione,
                             sizeof presentazione, scadenza, e);
    if (r != IO_OK) {
        ph_errore_contesto(e, "invio della richiesta di collegamento");
        goto errore;
    }
    r = leggi_pacchetto(a, &p, scadenza, e);
    if (r != IO_OK) {
        ph_errore_contesto(e, "attesa della risposta del telefono");
        goto errore;
    }
    if (p.comando == PH_ADB_AUTH) {
        if (non_abbinato != NULL)
            *non_abbinato = true;
        ph_errore(e, "il telefono chiede l'autenticazione AUTH: il PC non e' abbinato");
        goto errore;
    }
    if (p.comando != PH_ADB_STLS) {
        ph_errore(e, "il telefono non offre il TLS (risposta inattesa)");
        goto errore;
    }

    /* 2. STLS di risposta, poi TLS. */
    r = scrivi_pacchetto(a, PH_ADB_STLS, p.arg0, 0, NULL, 0, scadenza, e);
    if (r != IO_OK) {
        ph_errore_contesto(e, "invio STLS");
        goto errore;
    }
    if (handshake_tls(a, chiave, scadenza, &rifiutato, e) != 0) {
        if (rifiutato && non_abbinato != NULL)
            *non_abbinato = true;
        goto errore;
    }

    /* 3. Dentro il TLS arriva il CNXN del telefono. Se invece chiude o chiede
     * AUTH, il telefono non conosce questo PC. */
    a->err_ssl = 0;
    r = leggi_pacchetto(a, &p, scadenza, e);
    /* "non abbinato" solo se il telefono chiude, chiede AUTH o rifiuta il certificato
     * con un alert; un pacchetto malformato o un errore di rete sono un'altra cosa */
    if (r == IO_FINE || (r == IO_ERRORE && a->err_ssl != 0 && errore_ssl_di_rifiuto(a->err_ssl)) ||
        (r == IO_OK && p.comando == PH_ADB_AUTH)) {
        if (non_abbinato != NULL)
            *non_abbinato = true;
        if (r == IO_OK)
            ph_errore(e, "il telefono chiede l'autenticazione AUTH: il PC non e' abbinato");
        else
            ph_errore_contesto(e, "il telefono ha chiuso dopo il TLS (PC non abbinato)");
        goto errore;
    }
    if (r != IO_OK) {
        ph_errore_contesto(e, "attesa del CNXN del telefono");
        goto errore;
    }
    if (p.comando != PH_ADB_CNXN) {
        ph_errore(e, "dopo il TLS il telefono non ha mandato CNXN");
        goto errore;
    }
    a->max_dati = p.arg1 > PH_ADB_MAX_DATI ? PH_ADB_MAX_DATI : p.arg1;
    if (a->max_dati < 256) {
        ph_errore(e, "il telefono dichiara un massimo di dati troppo piccolo");
        goto errore;
    }
    a->banner = malloc((size_t)p.lunghezza + 1);
    if (a->banner == NULL) {
        ph_errore(e, "memoria finita");
        goto errore;
    }
    memcpy(a->banner, a->buf_lettore, p.lunghezza);
    a->banner[p.lunghezza] = '\0';
    /* vale solo se lo dichiarano entrambi: noi l'abbiamo annunciato nel CNXN */
    a->ack_ritardato = ha_caratteristica(a->banner, "delayed_ack");

    a->viva = true;
    if (pthread_create(&a->lettore, NULL, lettore, a) != 0) {
        a->viva = false;
        ph_errore(e, "impossibile avviare il thread lettore");
        goto errore;
    }
    a->lettore_avviato = true;
    return a;

errore:
    a->riferimenti = 0;
    adb_libera(a);
    return NULL;
}

void ph_adb_chiudi(PhAdb *a)
{
    if (a == NULL)
        return;
    pthread_mutex_lock(&a->m_stato);
    bool prima_volta = !a->chiusa;
    a->chiusa = true;
    pthread_mutex_unlock(&a->m_stato);
    if (!prima_volta)
        return;
    cadi(a, "connessione chiusa");
    /* fa tornare subito il thread lettore anche se e' dentro una lettura */
    shutdown(a->fd, SHUT_RDWR);
    if (a->lettore_avviato)
        pthread_join(a->lettore, NULL);
    /* fd e SSL restano fino all'ultimo canale chiuso: un canale che sta
     * finendo un'operazione non deve trovare un fd riusato da altri */
    adb_rilascia(a);
}

bool ph_adb_viva(PhAdb *a)
{
    pthread_mutex_lock(&a->m_stato);
    bool v = a->viva;
    pthread_mutex_unlock(&a->m_stato);
    return v;
}

const char *ph_adb_presentazione(PhAdb *a)
{
    return a->banner != NULL ? a->banner : "";
}

/* ---- Canali --------------------------------------------------------------*/
/* Attende sul cv del canale (m_stato preso). 0 = svegliato, 1 = scaduto. */
static int attendi_cv(PhCanale *c, int64_t scadenza)
{
    PhAdb *a = c->adb;
    if (scadenza < 0) {
        pthread_cond_wait(&c->cv, &a->m_stato);
        return 0;
    }
    int64_t ms = scadenza - ora_ms();
    if (ms <= 0)
        return 1;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    t.tv_sec += (time_t)(ms / 1000);
    t.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) {
        t.tv_sec++;
        t.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(&c->cv, &a->m_stato, &t) == ETIMEDOUT ? 1 : 0;
}

static void canale_distruggi(PhCanale *c)
{
    pthread_cond_destroy(&c->cv);
    ph_buffer_libera(&c->coda);
    free(c);
}

PhCanale *ph_canale_apri(PhAdb *a, const char *servizio, int timeout_ms, PhErrore *e)
{
    int64_t scadenza = scadenza_da(timeout_ms);
    if (strlen(servizio) + 1 > a->max_dati) {
        ph_errore(e, "nome del servizio troppo lungo");
        return NULL;
    }
    PhCanale *c = calloc(1, sizeof *c);
    if (c == NULL) {
        ph_errore(e, "memoria finita");
        return NULL;
    }
    pthread_condattr_t at;
    pthread_condattr_init(&at);
    pthread_condattr_setclock(&at, CLOCK_MONOTONIC);
    int ci = pthread_cond_init(&c->cv, &at);
    pthread_condattr_destroy(&at);
    if (ci != 0) {
        free(c);
        ph_errore(e, "risorse di sistema esaurite");
        return NULL;
    }
    c->adb = a;
    c->coda = PH_BUFFER_VUOTO;

    pthread_mutex_lock(&a->m_stato);
    if (!a->viva || a->chiusa) {
        ph_errore(e, "apertura di %s: %s", servizio, a->viva ? "connessione chiusa" : a->caduta.testo);
        pthread_mutex_unlock(&a->m_stato);
        canale_distruggi(c);
        return NULL;
    }
    c->locale = a->prossimo_id++;
    c->stato = CANALE_APERTURA;
    c->prossimo = a->canali;
    a->canali = c;
    a->riferimenti++;
    pthread_mutex_unlock(&a->m_stato);

    /* il servizio va mandato con il '\0' finale, come fa adb */
    if (invia(a, PH_ADB_OPEN, c->locale, a->ack_ritardato ? FINESTRA_RICEZIONE : 0, servizio, strlen(servizio) + 1, e) != 0) {
        ph_errore_contesto(e, "apertura di %s", servizio);
        goto fallito;
    }
    pthread_mutex_lock(&a->m_stato);
    while (c->stato == CANALE_APERTURA && a->viva) {
        if (attendi_cv(c, scadenza) != 0)
            break;
    }
    if (c->stato == CANALE_APERTO) {
        pthread_mutex_unlock(&a->m_stato);
        return c;
    }
    if (c->stato == CANALE_RIFIUTATO)
        ph_errore(e, "il telefono ha rifiutato il servizio %s", servizio);
    else if (!a->viva)
        ph_errore(e, "apertura di %s: %s", servizio, a->caduta.testo);
    else
        ph_errore(e, "apertura di %s: tempo scaduto", servizio);
    pthread_mutex_unlock(&a->m_stato);
fallito:
    pthread_mutex_lock(&a->m_stato);
    togli_canale(a, c);
    pthread_mutex_unlock(&a->m_stato);
    canale_distruggi(c);
    adb_rilascia(a);
    return NULL;
}

/* Motivo per cui un canale non e' piu' utilizzabile (m_stato preso). */
static void motivo_fine(PhCanale *c, PhErrore *e)
{
    PhAdb *a = c->adb;
    if (!a->viva)
        ph_errore(e, "%s", a->caduta.testo);
    else if (c->chiudendo)
        ph_errore(e, "canale chiuso");
    else
        ph_errore(e, "il telefono ha chiuso il canale");
}

ssize_t ph_canale_leggi(PhCanale *c, void *dati, size_t n, int timeout_ms, PhErrore *e)
{
    PhAdb *a = c->adb;
    int64_t scadenza = scadenza_da(timeout_ms);
    ssize_t esito;
    if (n == 0)
        return 0;
    pthread_mutex_lock(&a->m_stato);
    c->in_uso++;
    for (;;) {
        size_t disponibili = c->coda.lunghezza - c->letti;
        if (c->chiudendo) {
            motivo_fine(c, e);
            esito = PH_CANALE_ERRORE;
            break;
        }
        if (disponibili > 0) {
            size_t k = disponibili < n ? disponibili : n;
            memcpy(dati, c->coda.dati + c->letti, k);
            c->letti += k;
            if (c->letti == c->coda.lunghezza) {
                ph_buffer_svuota(&c->coda);
                c->letti = 0;
            } else if (c->letti >= 65536 && c->letti * 2 >= c->coda.lunghezza) {
                /* si recupera lo spazio letto solo quando e' la meta': costo ammortizzato */
                memmove(c->coda.dati, c->coda.dati + c->letti, c->coda.lunghezza - c->letti);
                c->coda.lunghezza -= c->letti;
                c->coda.dati[c->coda.lunghezza] = '\0';
                c->letti = 0;
            }
            esito = (ssize_t)k;
            c->da_confermare += k;
            break;
        }
        if (c->remoto_chiuso) {
            esito = PH_CANALE_CHIUSO;
            break;
        }
        if (!a->viva) {
            motivo_fine(c, e);
            esito = PH_CANALE_ERRORE;
            break;
        }
        if (attendi_cv(c, scadenza) != 0) {
            esito = PH_CANALE_TEMPO;
            break;
        }
    }
    if (esito > 0 && a->ack_ritardato) {
        /* delayed_ack: si conferma a blocchi cio' che chi legge ha consumato; la
         * finestra del telefono si riallarga solo cosi', quindi la coda e' limitata */
        if (c->da_confermare >= ACK_SOGLIA && !c->chiudendo && a->viva && !c->remoto_chiuso) {
            uint32_t quanti = (uint32_t)c->da_confermare;
            c->da_confermare = 0;
            c->debito -= quanti;
            uint32_t locale = c->locale, remoto = c->remoto;
            uint8_t carico[4] = {(uint8_t)quanti, (uint8_t)(quanti >> 8), (uint8_t)(quanti >> 16),
                                 (uint8_t)(quanti >> 24)};
            pthread_mutex_unlock(&a->m_stato);
            invia(a, PH_ADB_OKAY, locale, remoto, carico, sizeof carico, NULL);
            pthread_mutex_lock(&a->m_stato);
        }
    } else if (esito > 0 && c->okay_trattenuto && !c->chiudendo && a->viva && !c->remoto_chiuso &&
        c->coda.lunghezza - c->letti < SOGLIA_CODA) {
        /* la coda e' scesa sotto soglia: il telefono puo' mandare ancora. in_uso e' ancora
         * alzato, quindi il canale non sparisce mentre mandiamo. Se l'invio fallisce,
         * invia() fa cadere la connessione; i byte gia' letti si restituiscono comunque. */
        c->okay_trattenuto = false;
        uint32_t locale = c->locale, remoto = c->remoto;
        pthread_mutex_unlock(&a->m_stato);
        invia(a, PH_ADB_OKAY, locale, remoto, NULL, 0, NULL);
        pthread_mutex_lock(&a->m_stato);
    }
    c->in_uso--;
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&a->m_stato);
    return esito;
}

int ph_canale_leggi_tutto(PhCanale *c, void *dati, size_t n, int timeout_ms, PhErrore *e)
{
    int64_t scadenza = scadenza_da(timeout_ms);
    uint8_t *p = dati;
    while (n > 0) {
        ssize_t r = ph_canale_leggi(c, p, n, scadenza < 0 ? -1 : resta_ms(scadenza), e);
        if (r == PH_CANALE_CHIUSO)
            return ph_errore(e, "canale chiuso prima della fine dei dati");
        if (r == PH_CANALE_TEMPO)
            return ph_errore(e, "tempo scaduto in lettura");
        if (r < 0)
            return -1;
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

int ph_canale_scrivi(PhCanale *c, const void *dati, size_t n, PhErrore *e)
{
    PhAdb *a = c->adb;
    const uint8_t *p = dati;
    int rc = -1;
    pthread_mutex_lock(&a->m_stato);
    c->in_uso++;
    while (n > 0) {
        /* controllo di flusso: il WRTE successivo solo dopo l'OKAY del precedente */
        int64_t scadenza = scadenza_da(ATTESA_SCRITTURA_MS);
        bool scaduto = false;
        while (!(a->ack_ritardato ? c->finestra_invio > 0 : c->puo_scrivere) && !c->chiudendo &&
               !c->remoto_chiuso && a->viva) {
            if (attendi_cv(c, scadenza) != 0) {
                scaduto = true;
                break;
            }
        }
        if (c->chiudendo || c->remoto_chiuso || !a->viva) {
            motivo_fine(c, e);
            goto fine;
        }
        if (scaduto) {
            /* fallisce solo questa scrittura: a un telefono sparito pensa il TCP */
            ph_errore(e, "il telefono non conferma i dati (OKAY mancante)");
            goto fine;
        }
        size_t k = n < a->max_dati ? n : a->max_dati;
        if (a->ack_ritardato) {
            /* mai oltre la finestra concessa; piu' WRTE restano in volo senza OKAY */
            if ((int64_t)k > c->finestra_invio)
                k = (size_t)c->finestra_invio;
            c->finestra_invio -= (int64_t)k;
        } else {
            c->puo_scrivere = false;
        }
        uint32_t locale = c->locale, remoto = c->remoto;
        pthread_mutex_unlock(&a->m_stato);
        int r = invia(a, PH_ADB_WRTE, locale, remoto, p, k, e);
        pthread_mutex_lock(&a->m_stato);
        if (r != 0)
            goto fine;
        p += k;
        n -= k;
    }
    rc = 0;
fine:
    c->in_uso--;
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&a->m_stato);
    return rc;
}

void ph_canale_chiudi(PhCanale *c)
{
    if (c == NULL)
        return;
    PhAdb *a = c->adb;
    pthread_mutex_lock(&a->m_stato);
    c->chiudendo = true;
    pthread_cond_broadcast(&c->cv);
    /* chi sta leggendo o scrivendo su questo canale deve uscire prima che lo liberiamo */
    while (c->in_uso > 0)
        pthread_cond_wait(&c->cv, &a->m_stato);
    bool manda = a->viva && !a->chiusa && !c->clse_inviato && c->stato == CANALE_APERTO;
    c->clse_inviato = true;
    uint32_t locale = c->locale, remoto = c->remoto;
    togli_canale(a, c);
    pthread_mutex_unlock(&a->m_stato);
    if (manda) {
        PhErrore ignorato;
        invia(a, PH_ADB_CLSE, locale, remoto, NULL, 0, &ignorato);
    }
    canale_distruggi(c);
    adb_rilascia(a);
}
