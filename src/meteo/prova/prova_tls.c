/* Prove della connessione cifrata VERA (tls.c) contro un server OpenSSL locale
 * su localhost, scritto qui. Autorita' e certificati di prova sono GENERATI al
 * momento con le API di OpenSSL (chiavi solo in memoria, mai nel repository);
 * l'autorita' buona viene scritta in un file temporaneo nella cartella di
 * lavoro (senza chiave) e data a ph_tls_prova_ancore. Nessuna rete esterna,
 * tranne la prova della risoluzione di un nome «.invalid» (riservato e mai
 * esistente), che serve a far girare l'abbandono di getaddrinfo_a. */
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "../interno.h"

static int g_fallite, g_non_verificati;
#define VERIFICA(c)                                                                                                    \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            g_fallite++;                                                                                               \
            fprintf(stderr, "FALLITO %s:%d: %s\n", __FILE__, __LINE__, #c);                                            \
        }                                                                                                              \
    } while (0)

/* ---- Certificati di prova ---------------------------------------------------- */
static int aggiungi_ext(X509 *x, X509V3_CTX *ctx, int nid, const char *valore)
{
    X509_EXTENSION *ex = X509V3_EXT_nconf_nid(NULL, ctx, nid, valore);
    if (ex == NULL)
        return -1;
    int r = X509_add_ext(x, ex, -1) ? 0 : -1;
    X509_EXTENSION_free(ex);
    return r;
}

/* Certificato per `chiave`, firmato da `emittente`/`chiave_em` (NULL = autofirmato).
 * inizio/fine: secondi rispetto a adesso. */
static X509 *cert_nuovo(EVP_PKEY *chiave, X509 *emittente, EVP_PKEY *chiave_em, const char *cn, const char *san,
                        long inizio, long fine, int ca, long serie)
{
    X509 *x = X509_new();
    if (x == NULL)
        exit(2);
    X509_set_version(x, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(x), serie);
    X509_gmtime_adj(X509_getm_notBefore(x), inizio);
    X509_gmtime_adj(X509_getm_notAfter(x), fine);
    X509_set_pubkey(x, chiave);
    X509_NAME *nm = X509_get_subject_name(x);
    X509_NAME_add_entry_by_txt(nm, "CN", MBSTRING_ASC, (const unsigned char *)cn, -1, -1, 0);
    X509_set_issuer_name(x, emittente ? X509_get_subject_name(emittente) : nm);
    X509V3_CTX ctx;
    X509V3_set_ctx(&ctx, emittente ? emittente : x, x, NULL, NULL, 0);
    int ok = aggiungi_ext(x, &ctx, NID_basic_constraints, ca ? "critical,CA:TRUE" : "CA:FALSE");
    if (ca)
        ok |= aggiungi_ext(x, &ctx, NID_key_usage, "critical,keyCertSign,cRLSign");
    if (san != NULL)
        ok |= aggiungi_ext(x, &ctx, NID_subject_alt_name, san);
    if (ok != 0 || X509_sign(x, chiave_em ? chiave_em : chiave, EVP_sha256()) == 0) {
        fprintf(stderr, "generazione del certificato di prova fallita\n");
        exit(2);
    }
    return x;
}

static EVP_PKEY *chiave_nuova(void)
{
    EVP_PKEY *k = EVP_EC_gen("P-256");
    if (k == NULL)
        exit(2);
    return k;
}

/* ---- Server ---------------------------------------------------------------------- */
enum { SM_ECHO, SM_TRONCA, SM_TACE_TCP, SM_TACE_DOPO_HS, SM_SOLO_TLS11, SM_CHIUDI };
#define ECHO_BYTE 100000

static struct {
    int ascolto, porta, pipe_stop[2];
    pthread_t th;
    atomic_int modo, conn, hs_ok, ricevuto_ping;
    X509 *cert;       /* quello che presenta (impostati a server fermo) */
    EVP_PKEY *chiave;
    sem_t accettata, handshake;
} S;

/* Aspetta che il client chiuda (o la fine delle prove), al massimo 8 s. */
static void attendi_chiusura(int fd)
{
    int64_t fine = ph_meteo_ms() + 8000;
    for (;;) {
        struct pollfd p[2] = {{.fd = fd, .events = POLLIN}, {.fd = S.pipe_stop[0], .events = POLLIN}};
        int64_t resto = fine - ph_meteo_ms();
        if (resto <= 0 || poll(p, 2, (int)resto) <= 0 || p[1].revents)
            return;
        char b[512];
        ssize_t r = recv(fd, b, sizeof b, 0);
        if (r <= 0)
            return;
    }
}

static void servi_tls(int fd, int modo)
{
    SSL_CTX *c = SSL_CTX_new(TLS_server_method());
    if (c == NULL)
        return;
    if (modo == SM_SOLO_TLS11) {
        SSL_CTX_set_security_level(c, 0);
        SSL_CTX_set_cipher_list(c, "ALL:@SECLEVEL=0");
        SSL_CTX_set_min_proto_version(c, TLS1_VERSION);
        SSL_CTX_set_max_proto_version(c, TLS1_1_VERSION);
    }
    if (SSL_CTX_use_certificate(c, S.cert) != 1 || SSL_CTX_use_PrivateKey(c, S.chiave) != 1) {
        SSL_CTX_free(c);
        return;
    }
    SSL *ssl = SSL_new(c);
    struct timeval tv = {.tv_sec = 3};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    SSL_set_fd(ssl, fd);
    if (SSL_accept(ssl) == 1) {
        atomic_fetch_add(&S.hs_ok, 1);
        sem_post(&S.handshake);
        char b[16];
        switch (modo) {
        case SM_ECHO:
        case SM_TRONCA: {
            if (SSL_read(ssl, b, 4) == 4 && memcmp(b, "ping", 4) == 0)
                atomic_store(&S.ricevuto_ping, 1);
            if (modo == SM_TRONCA) {
                SSL_write(ssl, "0123456789", 10);
                break; /* fuori: si chiude la socket senza close_notify */
            }
            unsigned char blocco[4096];
            size_t fatti = 0;
            while (fatti < ECHO_BYTE) {
                size_t k = ECHO_BYTE - fatti < sizeof blocco ? ECHO_BYTE - fatti : sizeof blocco;
                for (size_t i = 0; i < k; i++)
                    blocco[i] = (unsigned char)((fatti + i) % 251);
                int w = SSL_write(ssl, blocco, (int)k);
                if (w <= 0)
                    break;
                fatti += (size_t)w;
            }
            SSL_shutdown(ssl); /* close_notify: fine PULITA */
            break;
        }
        case SM_CHIUDI: /* handshake fatto, poi via subito: la socket si chiude senza altro */
            break;
        default: /* SM_TACE_DOPO_HS, SM_SOLO_TLS11 */
            attendi_chiusura(fd);
            break;
        }
    }
    ERR_clear_error();
    SSL_free(ssl); /* non invia nulla: senza SSL_shutdown niente close_notify */
    SSL_CTX_free(c);
}

static void *server_thread(void *arg)
{
    (void)arg;
    /* Le scritture del SERVER su un client che se n'e' andato non devono
     * uccidere la prova; i thread del client restano con il comportamento
     * normale, cosi' si prova anche la protezione da SIGPIPE di tls.c. */
    sigset_t sp;
    sigemptyset(&sp);
    sigaddset(&sp, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &sp, NULL);
    for (;;) {
        struct pollfd p[2] = {{.fd = S.ascolto, .events = POLLIN}, {.fd = S.pipe_stop[0], .events = POLLIN}};
        if (poll(p, 2, -1) < 0)
            continue;
        if (p[1].revents)
            return NULL;
        int fd = accept(S.ascolto, NULL, NULL);
        if (fd < 0)
            continue;
        atomic_fetch_add(&S.conn, 1);
        int modo = atomic_load(&S.modo);
        if (modo == SM_TACE_TCP) {
            sem_post(&S.accettata);
            attendi_chiusura(fd);
        } else
            servi_tls(fd, modo);
        close(fd);
    }
}

static void server_avvia(void)
{
    sem_init(&S.accettata, 0, 0);
    sem_init(&S.handshake, 0, 0);
    S.ascolto = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int uno = 1;
    setsockopt(S.ascolto, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof uno);
    struct sockaddr_in a = {.sin_family = AF_INET};
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(S.ascolto, (struct sockaddr *)&a, sizeof a) != 0 || listen(S.ascolto, 16) != 0) {
        perror("server di prova");
        exit(2);
    }
    socklen_t l = sizeof a;
    getsockname(S.ascolto, (struct sockaddr *)&a, &l);
    S.porta = ntohs(a.sin_port);
    if (pipe(S.pipe_stop) != 0)
        exit(2);
    pthread_create(&S.th, NULL, server_thread, NULL);
}

static void server_ferma(void)
{
    if (write(S.pipe_stop[1], "x", 1) != 1)
        exit(2);
    pthread_join(S.th, NULL);
    close(S.ascolto);
    close(S.pipe_stop[0]);
    close(S.pipe_stop[1]);
}

static void server_prepara(int modo, X509 *cert)
{
    atomic_store(&S.modo, modo);
    S.cert = cert; /* il server li legge solo dopo accept: scritti prima di ogni collegamento */
    atomic_store(&S.ricevuto_ping, 0);
    sem_destroy(&S.accettata);
    sem_init(&S.accettata, 0, 0);
    sem_destroy(&S.handshake);
    sem_init(&S.handshake, 0, 0);
}

static int attendi_sem(sem_t *s, int ms)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += ms / 1000;
    t.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) {
        t.tv_sec++;
        t.tv_nsec -= 1000000000L;
    }
    int r;
    while ((r = sem_timedwait(s, &t)) != 0 && errno == EINTR) {
    }
    return r == 0;
}

/* ---- Annullamento da un altro thread ------------------------------------------------ */
typedef struct {
    sem_t *quando;       /* si aspetta questo semaforo, poi si alza il flag */
    atomic_int *flag;
    atomic_llong istante; /* ms in cui il flag e' stato alzato */
} Annullatore;

static void *annullatore(void *a)
{
    Annullatore *x = a;
    if (attendi_sem(x->quando, 5000)) {
        atomic_store(&x->istante, ph_meteo_ms());
        atomic_store(x->flag, 1);
    }
    return NULL;
}

/* ---- Prove --------------------------------------------------------------------------- */
static PhTls *apri_a(uint16_t porta, int64_t limite_ms, const atomic_int *an, PhErrore *e)
{
    e->testo[0] = '\0';
    return ph_tls_apri("localhost", porta, ph_meteo_ms() + limite_ms, an, e);
}

/* Legge fino alla fine; torna l'ultimo esito (0 fine pulita, -1 errore) e i byte
 * ricevuti, verificandone il contenuto. */
static int leggi_tutto(PhTls *t, size_t *tot, int *contenuto_ok)
{
    PhErrore e;
    unsigned char b[3000];
    *tot = 0;
    *contenuto_ok = 1;
    for (;;) {
        ssize_t r = ph_tls_leggi(t, b, sizeof b, ph_meteo_ms() + 5000, NULL, &e);
        if (r <= 0)
            return (int)r;
        for (ssize_t i = 0; i < r; i++)
            if (b[i] != (unsigned char)((*tot + (size_t)i) % 251))
                *contenuto_ok = 0;
        *tot += (size_t)r;
    }
}

static void prova_buona(X509 *buono)
{
    PhErrore e;
    server_prepara(SM_ECHO, buono);
    PhTls *t = apri_a((uint16_t)S.porta, 5000, NULL, &e);
    VERIFICA(t != NULL);
    if (t == NULL) {
        fprintf(stderr, "  apri: %s\n", e.testo);
        return;
    }
    VERIFICA(ph_tls_scrivi(t, "ping", 4, ph_meteo_ms() + 5000, NULL, &e) == 0);
    size_t tot;
    int ok;
    VERIFICA(leggi_tutto(t, &tot, &ok) == 0); /* fine PULITA: close_notify */
    VERIFICA(tot == ECHO_BYTE && ok);
    VERIFICA(atomic_load(&S.ricevuto_ping) == 1);
    /* dopo la fine pulita si continua a leggere 0, mai dati */
    unsigned char b[8];
    VERIFICA(ph_tls_leggi(t, b, sizeof b, ph_meteo_ms() + 1000, NULL, &e) == 0);
    ph_tls_chiudi(t);
    ph_tls_chiudi(NULL);
}

static void prova_certificato_rifiutato(const char *nome, X509 *cert)
{
    PhErrore e;
    server_prepara(SM_ECHO, cert);
    PhTls *t = apri_a((uint16_t)S.porta, 5000, NULL, &e);
    VERIFICA(t == NULL);
    if (t != NULL) {
        fprintf(stderr, "  %s: connessione accettata!\n", nome);
        ph_tls_chiudi(t);
        return;
    }
    VERIFICA(strstr(e.testo, "certificato") != NULL);
    if (strstr(e.testo, "certificato") == NULL)
        fprintf(stderr, "  %s: frase «%s»\n", nome, e.testo);
    /* la frase non contiene nome dell'host ne' porta */
    VERIFICA(strstr(e.testo, "localhost") == NULL);
}

static void prova_troncata(X509 *buono)
{
    PhErrore e;
    server_prepara(SM_TRONCA, buono);
    PhTls *t = apri_a((uint16_t)S.porta, 5000, NULL, &e);
    VERIFICA(t != NULL);
    if (t == NULL)
        return;
    VERIFICA(ph_tls_scrivi(t, "ping", 4, ph_meteo_ms() + 5000, NULL, &e) == 0);
    size_t tot;
    int ok;
    int r = leggi_tutto(t, &tot, &ok);
    VERIFICA(r == -1); /* mai 0: la chiusura senza close_notify e' un troncamento */
    VERIFICA(tot <= 10);
    /* e resta errore */
    unsigned char b[8];
    VERIFICA(ph_tls_leggi(t, b, sizeof b, ph_meteo_ms() + 1000, NULL, &e) == -1);
    ph_tls_chiudi(t);
}

static void prova_tempi(X509 *buono)
{
    PhErrore e;
    /* annullamento durante l'handshake: il server accetta e tace */
    {
        server_prepara(SM_TACE_TCP, buono);
        atomic_int flag = 0;
        Annullatore a = {.quando = &S.accettata, .flag = &flag};
        pthread_t th;
        pthread_create(&th, NULL, annullatore, &a);
        PhTls *t = apri_a((uint16_t)S.porta, 10000, &flag, &e);
        int64_t fine = ph_meteo_ms();
        pthread_join(th, NULL);
        VERIFICA(t == NULL);
        VERIFICA(atomic_load(&a.istante) != 0 && fine - atomic_load(&a.istante) < 500);
        VERIFICA(strstr(e.testo, "annullata") != NULL);
        ph_tls_chiudi(t);
    }
    /* scadenza durante l'handshake */
    {
        server_prepara(SM_TACE_TCP, buono);
        int64_t t0 = ph_meteo_ms();
        PhTls *t = apri_a((uint16_t)S.porta, 300, NULL, &e);
        int64_t dt = ph_meteo_ms() - t0;
        VERIFICA(t == NULL && dt >= 250 && dt < 1500);
        VERIFICA(strstr(e.testo, "non risponde") != NULL);
    }
    /* scadenza e annullamento durante la lettura */
    {
        server_prepara(SM_TACE_DOPO_HS, buono);
        PhTls *t = apri_a((uint16_t)S.porta, 5000, NULL, &e);
        VERIFICA(t != NULL);
        if (t != NULL) {
            unsigned char b[16];
            int64_t t0 = ph_meteo_ms();
            VERIFICA(ph_tls_leggi(t, b, sizeof b, ph_meteo_ms() + 300, NULL, &e) == -1);
            int64_t dt = ph_meteo_ms() - t0;
            VERIFICA(dt >= 250 && dt < 1500);
            ph_tls_chiudi(t);
        }
        server_prepara(SM_TACE_DOPO_HS, buono);
        atomic_int flag = 0;
        sem_t quando;
        sem_init(&quando, 0, 0);
        Annullatore a = {.quando = &quando, .flag = &flag};
        pthread_t th;
        pthread_create(&th, NULL, annullatore, &a);
        t = apri_a((uint16_t)S.porta, 5000, NULL, &e);
        VERIFICA(t != NULL);
        if (t != NULL) {
            sem_post(&quando);
            unsigned char b[16];
            VERIFICA(ph_tls_leggi(t, b, sizeof b, ph_meteo_ms() + 10000, &flag, &e) == -1);
            int64_t fine = ph_meteo_ms();
            VERIFICA(atomic_load(&a.istante) != 0 && fine - atomic_load(&a.istante) < 500);
            ph_tls_chiudi(t);
        } else
            sem_post(&quando);
        pthread_join(th, NULL);
        sem_destroy(&quando);
    }
    /* gia' annullata: esce senza collegarsi */
    {
        server_prepara(SM_ECHO, buono);
        int prima = atomic_load(&S.conn);
        atomic_int flag = 1;
        VERIFICA(apri_a((uint16_t)S.porta, 5000, &flag, &e) == NULL);
        VERIFICA(atomic_load(&S.conn) == prima);
    }
}

/* Controprova: un client con contesto PREDEFINITO (nessuna impostazione sua:
 * vale la configurazione di OPENSSL_CONF scritta in main) riesce a parlare
 * TLS 1.1 con il server di prova. Se non ci riesce, la prova «tls.c rifiuta
 * TLS 1.1» non dimostrerebbe nulla e va dichiarata non verificata. */
static int server_tls11_funziona(X509 *buono)
{
    server_prepara(SM_SOLO_TLS11, buono);
    SSL_CTX *c = SSL_CTX_new(TLS_client_method());
    SSL *ssl = SSL_new(c);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons((uint16_t)S.porta)};
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int ok = 0;
    if (connect(fd, (struct sockaddr *)&a, sizeof a) == 0) {
        SSL_set_fd(ssl, fd);
        ok = SSL_connect(ssl) == 1 && SSL_version(ssl) == TLS1_1_VERSION;
    }
    ERR_clear_error();
    SSL_free(ssl);
    SSL_CTX_free(c);
    close(fd);
    return ok;
}

static void prova_tls11(X509 *buono)
{
    PhErrore e;
    if (!server_tls11_funziona(buono)) {
        fprintf(stderr, "NON VERIFICATO: nessun client predefinito riesce a parlare TLS 1.1 col server di prova (caso saltato)\n");
        g_non_verificati++;
        return;
    }
    server_prepara(SM_SOLO_TLS11, buono);
    PhTls *t = apri_a((uint16_t)S.porta, 5000, NULL, &e);
    VERIFICA(t == NULL); /* il NOSTRO minimo (TLS 1.2) rifiuta un server che parla solo 1.1 */
    ph_tls_chiudi(t);
}

/* SIGPIPE: il server se ne va subito dopo l'handshake; il client scrive blocchi
 * da 64 KiB finche' fallisce. La disposizione di SIGPIPE in questo thread e'
 * quella predefinita (il server la blocca solo nel suo thread): se tls.c non
 * proteggesse le scritture il processo morirebbe qui. */
static void prova_sigpipe(X509 *buono)
{
    PhErrore e;
    server_prepara(SM_CHIUDI, buono);
    PhTls *t = apri_a((uint16_t)S.porta, 5000, NULL, &e);
    VERIFICA(t != NULL);
    if (t == NULL)
        return;
    VERIFICA(attendi_sem(&S.handshake, 5000));
    unsigned char *blocco = calloc(1, 65536);
    int fallita = 0;
    for (int i = 0; i < 400 && !fallita; i++)
        fallita = ph_tls_scrivi(t, blocco, 65536, ph_meteo_ms() + 5000, NULL, &e) != 0;
    VERIFICA(fallita);
    VERIFICA(ph_tls_scrivi(t, blocco, 65536, ph_meteo_ms() + 5000, NULL, &e) == -1); /* e resta cosi' */
    free(blocco);
    ph_tls_chiudi(t);
}

typedef struct {
    atomic_int *flag;
    int ms;
    atomic_llong istante;
} Dopo;

static void *annulla_dopo(void *a)
{
    Dopo *d = a;
    poll(NULL, 0, d->ms); /* scadenza della prova, non attesa «per dare tempo» */
    atomic_store(&d->istante, ph_meteo_ms());
    atomic_store(d->flag, 1);
    return NULL;
}

/* Un nome con due indirizzi (localhost = ::1 poi 127.0.0.1): il primo e' MUTO
 * a livello di collegamento TCP: una socket in ascolto con la coda piena (listen
 * con backlog 0 e qualche collegamento gia' in coda) scarta i nuovi SYN, quindi
 * connect() non conclude mai. Il pezzo deve rinunciare al muto dopo la sua
 * parte di tempo e provare l'altro, senza consumare tutta la scadenza. */
static void prova_indirizzo_muto(X509 *buono)
{
    PhErrore e;
    struct addrinfo h, *l = NULL;
    memset(&h, 0, sizeof h);
    h.ai_family = AF_UNSPEC;
    h.ai_socktype = SOCK_STREAM;
    h.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV;
    int ordine_ok = 0;
    if (getaddrinfo("localhost", "443", &h, &l) == 0) {
        if (l != NULL && l->ai_family == AF_INET6)
            for (struct addrinfo *a = l->ai_next; a != NULL; a = a->ai_next)
                if (a->ai_family == AF_INET)
                    ordine_ok = 1;
        freeaddrinfo(l);
    }
    int s6 = socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int riempitori[4] = {-1, -1, -1, -1};
    int uno = 1;
    int pronto = 0;
    if (s6 >= 0 && ordine_ok) {
        setsockopt(s6, IPPROTO_IPV6, IPV6_V6ONLY, &uno, sizeof uno);
        struct sockaddr_in6 a6;
        memset(&a6, 0, sizeof a6);
        a6.sin6_family = AF_INET6;
        a6.sin6_addr = in6addr_loopback;
        a6.sin6_port = htons((uint16_t)S.porta);
        pronto = bind(s6, (struct sockaddr *)&a6, sizeof a6) == 0 && listen(s6, 0) == 0;
        /* riempie la coda: i collegamenti restano aperti fino alla fine della prova */
        for (int k = 0; pronto && k < 4; k++) {
            riempitori[k] = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            (void)connect(riempitori[k], (struct sockaddr *)&a6, sizeof a6);
        }
    }
    if (!pronto) {
        for (int k = 0; k < 4; k++)
            if (riempitori[k] >= 0)
                close(riempitori[k]);
        fprintf(stderr, "NON VERIFICATO: localhost non ha ::1 seguito da 127.0.0.1 (o ::1 non e' usabile): caso dell'indirizzo muto saltato\n");
        g_non_verificati++;
        if (s6 >= 0)
            close(s6);
        return;
    }
    /* il muto costa la sua parte (<= 3500 ms), poi si passa all'indirizzo buono */
    server_prepara(SM_ECHO, buono);
    int64_t t0 = ph_meteo_ms();
    PhTls *t = apri_a((uint16_t)S.porta, 9000, NULL, &e);
    int64_t dt = ph_meteo_ms() - t0;
    VERIFICA(t != NULL);
    if (t == NULL)
        fprintf(stderr, "  indirizzo muto: dopo %lld ms, «%s»\n", (long long)dt, e.testo);
    VERIFICA(dt >= 3000 && dt < 8000); /* ha provato prima il muto, ma senza consumare tutto il tempo */
    ph_tls_chiudi(t);
    /* scadenza totale breve: finisce con la scadenza, senza passare agli altri */
    server_prepara(SM_ECHO, buono);
    t0 = ph_meteo_ms();
    t = apri_a((uint16_t)S.porta, 1500, NULL, &e);
    dt = ph_meteo_ms() - t0;
    VERIFICA(t == NULL && dt >= 1400 && dt < 3000 && atomic_load(&S.hs_ok) >= 1);
    /* annullamento mentre si sta sul muto */
    {
        atomic_int flag = 0;
        Dopo d = {.flag = &flag, .ms = 400};
        pthread_t th;
        pthread_create(&th, NULL, annulla_dopo, &d);
        t = apri_a((uint16_t)S.porta, 9000, &flag, &e);
        int64_t fine = ph_meteo_ms();
        pthread_join(th, NULL);
        VERIFICA(t == NULL && fine - atomic_load(&d.istante) < 500);
    }
    for (int k = 0; k < 4; k++)
        if (riempitori[k] >= 0)
            close(riempitori[k]);
    close(s6);
}

static void prova_argomenti_e_rete(void)
{
    PhErrore e;
    int prima = atomic_load(&S.conn);
    char lungo[300];
    memset(lungo, 'a', sizeof lungo - 1);
    lungo[sizeof lungo - 1] = '\0';
    const char *cattivi[] = {"", "bad host", "a/b", "ho\nst", "localhost ", "lo:calhost", "h\xC3\xA9st", lungo, "a_b"};
    for (size_t i = 0; i < sizeof cattivi / sizeof *cattivi; i++) {
        int64_t t0 = ph_meteo_ms();
        e.testo[0] = '\0';
        VERIFICA(ph_tls_apri(cattivi[i], (uint16_t)S.porta, ph_meteo_ms() + 5000, NULL, &e) == NULL);
        VERIFICA(ph_meteo_ms() - t0 < 500 && e.testo[0] != '\0');
    }
    VERIFICA(ph_tls_apri(NULL, (uint16_t)S.porta, ph_meteo_ms() + 5000, NULL, &e) == NULL);
    VERIFICA(ph_tls_apri("localhost", 0, ph_meteo_ms() + 5000, NULL, &e) == NULL);
    VERIFICA(atomic_load(&S.conn) == prima); /* nessuna connessione partita */

    /* porta chiusa */
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET};
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, (struct sockaddr *)&a, sizeof a);
    socklen_t l = sizeof a;
    getsockname(s, (struct sockaddr *)&a, &l);
    close(s);
    int64_t t0 = ph_meteo_ms();
    VERIFICA(apri_a(ntohs(a.sin_port), 5000, NULL, &e) == NULL);
    VERIFICA(ph_meteo_ms() - t0 < 3000);

    /* nome che non si risolve (dominio riservato .invalid): scadenza o errore,
     * mai attese lunghe. 200 iterazioni con scadenze tra 0 e 50 ms (e, a
     * meta', annullamento a mezza risoluzione) esercitano l'abbandono della
     * ricerca (getaddrinfo_a) in corsa, sotto AddressSanitizer. */
    for (int i = 0; i < 200; i++) {
        atomic_int flag = 0;
        t0 = ph_meteo_ms();
        PhTls *t;
        if (i % 2) {
            t = ph_tls_apri("prova-inesistente.invalid", 443, ph_meteo_ms() + (i * 7) % 51, NULL, &e);
        } else {
            Dopo d = {.flag = &flag, .ms = (i * 3) % 40};
            pthread_t th;
            pthread_create(&th, NULL, annulla_dopo, &d);
            t = ph_tls_apri("prova-inesistente.invalid", 443, ph_meteo_ms() + 5000, &flag, &e);
            pthread_join(th, NULL);
        }
        VERIFICA(t == NULL);
        VERIFICA(ph_meteo_ms() - t0 < 2000);
    }
}

int main(void)
{
    /* Cartella temporanea della prova (nome casuale, nella cartella di lavoro:
     * mai /tmp con nome prevedibile). Ci vanno la configurazione di OpenSSL e
     * il file dell'autorita' di prova (senza chiave). */
    char dir[64] = "./prova-tls-XXXXXX";
    if (mkdtemp(dir) == NULL) {
        perror("mkdtemp");
        return 2;
    }
    char cnf[128], ca_file[128];
    snprintf(cnf, sizeof cnf, "%s/openssl.cnf", dir);
    snprintf(ca_file, sizeof ca_file, "%s/ca.pem", dir);
    /* Configurazione di OpenSSL PRIMA di qualunque chiamata alla libreria: un
     * contesto predefinito (anche quello di un client senza impostazioni)
     * ammette TLS 1.0/1.1 e il livello di sicurezza 0. Cosi' il caso «server
     * solo TLS 1.1» prova davvero il NOSTRO minimo (TLS 1.2 in tls.c), non il
     * minimo predefinito della distribuzione. Non cambia la verifica dei
     * certificati, che le prove restanti controllano come prima. */
    FILE *fc = fopen(cnf, "w");
    if (fc == NULL) {
        perror("openssl.cnf");
        return 2;
    }
    fputs("openssl_conf = openssl_init\n[openssl_init]\nssl_conf = ssl_sect\n[ssl_sect]\nsystem_default = "
          "system_default_sect\n[system_default_sect]\nCipherString = DEFAULT:@SECLEVEL=0\nMinProtocol = None\n",
          fc);
    fclose(fc);
    setenv("OPENSSL_CONF", cnf, 1);

    EVP_PKEY *k_ca = chiave_nuova(), *k_altra = chiave_nuova(), *k_foglia = chiave_nuova();
    X509 *ca = cert_nuovo(k_ca, NULL, NULL, "Prova CA 1", NULL, -86400, 86400L * 30, 1, 1);
    X509 *altra = cert_nuovo(k_altra, NULL, NULL, "Prova CA 2", NULL, -86400, 86400L * 30, 1, 2);
    X509 *buono = cert_nuovo(k_foglia, ca, k_ca, "localhost", "DNS:localhost", -86400, 86400L * 30, 0, 10);
    X509 *nome_sbagliato = cert_nuovo(k_foglia, ca, k_ca, "altro.esempio", "DNS:altro.esempio", -86400, 86400L * 30, 0, 11);
    X509 *sconosciuta = cert_nuovo(k_foglia, altra, k_altra, "localhost", "DNS:localhost", -86400, 86400L * 30, 0, 12);
    X509 *scaduto = cert_nuovo(k_foglia, ca, k_ca, "localhost", "DNS:localhost", -86400L * 30, -86400, 0, 13);
    X509 *futuro = cert_nuovo(k_foglia, ca, k_ca, "localhost", "DNS:localhost", 86400, 86400L * 30, 0, 14);
    X509 *autofirmato = cert_nuovo(k_foglia, NULL, NULL, "localhost", "DNS:localhost", -86400, 86400L * 30, 0, 15);

    FILE *f = fopen(ca_file, "w");
    if (f == NULL || PEM_write_X509(f, ca) != 1) {
        fprintf(stderr, "non riesco a scrivere il file dell'autorita' di prova\n");
        return 2;
    }
    fclose(f);
    ph_tls_prova_ancore(ca_file);

    S.chiave = k_foglia;
    server_avvia();

    prova_buona(buono);
    prova_certificato_rifiutato("nome sbagliato", nome_sbagliato);
    prova_certificato_rifiutato("autorita' sconosciuta", sconosciuta);
    prova_certificato_rifiutato("scaduto", scaduto);
    prova_certificato_rifiutato("non ancora valido", futuro);
    prova_certificato_rifiutato("autofirmato", autofirmato);
    prova_troncata(buono);
    prova_tempi(buono);
    prova_tls11(buono);
    prova_sigpipe(buono);
    prova_indirizzo_muto(buono);
    prova_argomenti_e_rete();
    /* gli avvisi di ricerche abbandonate che ancora girano arrivano prima dell'uscita (attesa con scadenza) */
    poll(NULL, 0, 300);

    server_ferma();
    ph_tls_prova_ancore(NULL);
    unlink(ca_file);
    unlink(cnf);
    rmdir(dir);
    X509_free(ca);
    X509_free(altra);
    X509_free(buono);
    X509_free(nome_sbagliato);
    X509_free(sconosciuta);
    X509_free(scaduto);
    X509_free(futuro);
    X509_free(autofirmato);
    EVP_PKEY_free(k_ca);
    EVP_PKEY_free(k_altra);
    EVP_PKEY_free(k_foglia);
    if (g_fallite) {
        fprintf(stderr, "prova_tls: %d verifiche fallite\n", g_fallite);
        return 1;
    }
    printf("prova_tls: tutto ok (casi non verificati: %d)\n", g_non_verificati);
    /* 77 = SKIP per meson: se un caso non e' stato verificato il riepilogo lo mostra, non e' un verde pieno */
    return g_non_verificati ? 77 : 0;
}
