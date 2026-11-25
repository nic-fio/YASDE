#include "finto_telefono.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <unistd.h>

#include "adb/interno.h"

#define MAX_CANALI 64

enum Tipo { T_ECO, T_LENTO, T_SILENZIO, T_SHELL, T_SORGENTE, T_SINK, T_NOOKAY };

typedef struct {
    bool usato;
    enum Tipo tipo;
    uint32_t mio;    /* id lato telefono */
    uint32_t client; /* id lato client */
    bool ok_pendente; /* "lento:": OKAY da mandare allo scadere di ok_scadenza */
    int64_t ok_scadenza;
    int64_t asb;             /* delayed_ack: finestra di invio verso il client */
    uint64_t nonack;         /* delayed_ack: byte del client non ancora confermati */
    uint64_t ok_bytes;       /* delayed_ack: byte da confermare con il prossimo OKAY ritardato */
    bool ok_apertura;        /* l'OKAY ritardato e' quello dell'apertura */
    uint64_t restanti; /* "sorgente:": byte ancora da mandare */
    uint64_t mandati;
} Canale;

struct FintoTelefono {
    const PhChiave *chiave;
    FtModo modo;
    int ascolto;
    _Atomic int fd;
    SSL_CTX *ctx;
    SSL *ssl;
    pthread_t thread;
    Canale canali[MAX_CANALI];
    uint32_t prossimo;
    uint8_t *dati;
    _Atomic bool cert_client;
    _Atomic int violazioni;
    _Atomic long byte_lento;
    _Atomic int okay;
    _Atomic int wrte;
    _Atomic int clse;
    _Atomic long byte_sorgente;
    _Atomic long byte_sink;
    uint32_t max_dati;
    bool delayed_cfg; /* il finto telefono dichiara delayed_ack */
    bool delayed;     /* concordato: lo dichiara anche il client */
    _Atomic long max_in_volo;
    uint8_t sorg[PH_ADB_MAX_DATI];
};

uint8_t ft_pattern(uint32_t seme, size_t i)
{
    return (uint8_t)((i * 31u + seme * 7u + (i >> 8)) & 0xff);
}

/* ---- I/O del telefono (bloccante, un solo thread) -------------------------*/
static int leggi_esatto(FintoTelefono *ft, void *buf, size_t n)
{
    uint8_t *p = buf;
    while (n > 0) {
        ssize_t r;
        if (ft->ssl != NULL) {
            int k = SSL_read(ft->ssl, p, n > 1 << 20 ? 1 << 20 : (int)n);
            r = k > 0 ? k : -1;
        } else {
            r = read(atomic_load(&ft->fd), p, n);
            if (r == 0)
                r = -1;
        }
        if (r < 0)
            return -1;
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

static int scrivi_tutto(FintoTelefono *ft, const void *buf, size_t n)
{
    if (ft->ssl != NULL)
        return SSL_write(ft->ssl, buf, (int)n) == (int)n ? 0 : -1;
    return send(atomic_load(&ft->fd), buf, n, MSG_NOSIGNAL) == (ssize_t)n ? 0 : -1;
}

static int manda(FintoTelefono *ft, uint32_t cmd, uint32_t a0, uint32_t a1, const void *d, size_t n)
{
    PhPacchettoAdb p;
    uint8_t ih[PH_ADB_INTESTAZIONE];
    ph_pacchetto_prepara(&p, cmd, a0, a1, d, n);
    ph_pacchetto_codifica(&p, ih);
    if (scrivi_tutto(ft, ih, sizeof ih) != 0)
        return -1;
    if (n > 0 && scrivi_tutto(ft, d, n) != 0)
        return -1;
    if (cmd == PH_ADB_WRTE)
        atomic_fetch_add(&ft->wrte, 1);
    return 0;
}

static void scrivi32(uint8_t *d, uint32_t v)
{
    d[0] = (uint8_t)v;
    d[1] = (uint8_t)(v >> 8);
    d[2] = (uint8_t)(v >> 16);
    d[3] = (uint8_t)(v >> 24);
}

/* OKAY che conferma n byte (delayed_ack) o semplice OKAY. */
static int manda_ack(FintoTelefono *ft, Canale *c, uint32_t n)
{
    uint8_t carico[4];
    if (!ft->delayed)
        return manda(ft, PH_ADB_OKAY, c->mio, c->client, NULL, 0);
    scrivi32(carico, n);
    return manda(ft, PH_ADB_OKAY, c->mio, c->client, carico, 4);
}

/* OKAY dell'apertura: con delayed_ack porta la finestra che concediamo al client. */
static int manda_ok_apertura(FintoTelefono *ft, Canale *c)
{
    return manda_ack(ft, c, FT_FINESTRA);
}

static int leggi_pacchetto(FintoTelefono *ft, PhPacchettoAdb *p)
{
    uint8_t ih[PH_ADB_INTESTAZIONE];
    PhErrore e;
    if (leggi_esatto(ft, ih, sizeof ih) != 0 || ph_pacchetto_decodifica(ih, p, &e) != 0)
        return -1;
    return p->lunghezza > 0 ? leggi_esatto(ft, ft->dati, p->lunghezza) : 0;
}

static int64_t ora_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ---- Servizi --------------------------------------------------------------*/
static void shell_v2(uint8_t *fuori, uint8_t id, uint32_t len)
{
    fuori[0] = id;
    fuori[1] = (uint8_t)len;
    fuori[2] = (uint8_t)(len >> 8);
    fuori[3] = (uint8_t)(len >> 16);
    fuori[4] = (uint8_t)(len >> 24);
}

static int servi_shell(FintoTelefono *ft, Canale *c, const char *cmd)
{
    uint8_t b[5 + 64];
    if (strcmp(cmd, "prova") == 0) {
        /* il pacchetto stdout e' spezzato in due WRTE proprio in mezzo ai dati */
        shell_v2(b, 1, 5);
        memcpy(b + 5, "ci", 2);
        if (manda(ft, PH_ADB_WRTE, c->mio, c->client, b, 7) != 0)
            return -1;
        if (manda(ft, PH_ADB_WRTE, c->mio, c->client, "ao\n", 3) != 0)
            return -1;
        shell_v2(b, 2, 7);
        memcpy(b + 5, "errore\n", 7);
        if (manda(ft, PH_ADB_WRTE, c->mio, c->client, b, 12) != 0)
            return -1;
        /* intestazione dell'uscita divisa a meta' */
        shell_v2(b, 3, 1);
        b[5] = 7;
        if (manda(ft, PH_ADB_WRTE, c->mio, c->client, b, 3) != 0 ||
            manda(ft, PH_ADB_WRTE, c->mio, c->client, b + 3, 3) != 0)
            return -1;
        return manda(ft, PH_ADB_CLSE, c->mio, c->client, NULL, 0);
    }
    if (strcmp(cmd, "muto") == 0) {
        shell_v2(b, 3, 1);
        b[5] = 0;
        if (manda(ft, PH_ADB_WRTE, c->mio, c->client, b, 6) != 0)
            return -1;
        return manda(ft, PH_ADB_CLSE, c->mio, c->client, NULL, 0);
    }
    if (strcmp(cmd, "grosso") == 0) {
        enum { TOT = 100000, PEZZO = 30000 };
        uint8_t *g = malloc(5 + TOT + 6);
        if (g == NULL)
            return -1;
        shell_v2(g, 1, TOT);
        for (size_t i = 0; i < TOT; i++)
            g[5 + i] = ft_pattern(1, i);
        shell_v2(g + 5 + TOT, 3, 1);
        g[5 + TOT + 5] = 0;
        size_t tot = 5 + TOT + 6;
        int rc = 0;
        for (size_t off = 0; off < tot && rc == 0; off += PEZZO)
            rc = manda(ft, PH_ADB_WRTE, c->mio, c->client, g + off, tot - off < PEZZO ? tot - off : PEZZO);
        free(g);
        if (rc != 0)
            return -1;
        return manda(ft, PH_ADB_CLSE, c->mio, c->client, NULL, 0);
    }
    if (strcmp(cmd, "falso") == 0) {
        /* lunghezza del pacchetto shell v2 assurda: il client non deve aspettare 4 GiB */
        shell_v2(b, 1, 0xfffffff0u);
        return manda(ft, PH_ADB_WRTE, c->mio, c->client, b, 5);
    }
    return 0; /* "pendi": canale aperto, nessuna risposta */
}

/* Un pezzo di "sorgente:"; il successivo parte quando arriva l'OKAY del client. */
static int manda_prossimo(FintoTelefono *ft, Canale *c)
{
    if (c->restanti == 0)
        return 0;
    size_t k = c->restanti < ft->max_dati ? (size_t)c->restanti : ft->max_dati;
    for (size_t i = 0; i < k; i++)
        ft->sorg[i] = ft_pattern(3, (size_t)c->mandati + i);
    c->restanti -= k;
    c->mandati += k;
    atomic_fetch_add(&ft->byte_sorgente, (long)k);
    return manda(ft, PH_ADB_WRTE, c->mio, c->client, ft->sorg, k);
}

/* delayed_ack: manda finche' la finestra e' positiva (l'ultimo pacchetto puo' sforarla). */
static int sorgente_avanti(FintoTelefono *ft, Canale *c)
{
    if (!ft->delayed)
        return manda_prossimo(ft, c);
    while (c->restanti > 0 && c->asb > 0) {
        uint64_t prima = c->mandati;
        if (manda_prossimo(ft, c) != 0)
            return -1;
        c->asb -= (int64_t)(c->mandati - prima);
    }
    return 0;
}

static int ostile(FintoTelefono *ft, Canale *c, int scenario)
{
    if (scenario == 0) {
        /* id che non esistono: il client deve ignorarli (o rispondere CLSE) senza cadere */
        if (manda(ft, PH_ADB_OKAY, 4242, 9999, NULL, 0) != 0 ||
            manda(ft, PH_ADB_WRTE, 4242, 9999, "xyz", 3) != 0 ||
            manda(ft, PH_ADB_CLSE, 4242, 9999, NULL, 0) != 0 ||
            manda(ft, PH_ADB_WRTE, 4242, c->client, "xyz", 3) != 0 || /* id nostro giusto, remoto sbagliato */
            manda(ft, PH_ADB_OKAY, 4242, c->client, NULL, 0) != 0 ||
            manda(ft, PH_ADB_CLSE, 4242, c->client, NULL, 0) != 0)
            return -1;
        return 0;
    }
    if (scenario == 3) /* OKAY con carico di 2 byte invece di 4 */
        return manda(ft, PH_ADB_OKAY, c->mio, c->client, "\x01\x00", 2);
    if (scenario == 4) /* OKAY con valore oltre INT32_MAX */
        return manda(ft, PH_ADB_OKAY, c->mio, c->client, "\x00\x00\x00\x80", 4);
    PhPacchettoAdb p;
    uint8_t ih[PH_ADB_INTESTAZIONE];
    ph_pacchetto_prepara(&p, PH_ADB_WRTE, c->mio, c->client, "abc", 3);
    if (scenario == 2)
        p.lunghezza = PH_ADB_MAX_DATI + 1;
    ph_pacchetto_codifica(&p, ih);
    if (scenario == 1)
        ih[20] ^= 0xff; /* magic */
    return scrivi_tutto(ft, ih, sizeof ih);
}

static int apri(FintoTelefono *ft, const PhPacchettoAdb *p)
{
    const char *serv = (const char *)ft->dati;
    if (p->lunghezza == 0 || serv[p->lunghezza - 1] != '\0')
        return manda(ft, PH_ADB_CLSE, 0, p->arg0, NULL, 0);
    Canale *c = NULL;
    for (int i = 0; i < MAX_CANALI && c == NULL; i++)
        if (!ft->canali[i].usato)
            c = &ft->canali[i];
    enum Tipo tipo = T_SILENZIO;
    const char *cmd = NULL;
    if (strcmp(serv, "eco:") == 0)
        tipo = T_ECO;
    else if (strcmp(serv, "lento:") == 0)
        tipo = T_LENTO;
    else if (strcmp(serv, "silenzio:") == 0 || strcmp(serv, "chiudi:") == 0 ||
             strcmp(serv, "grande:") == 0)
        tipo = T_SILENZIO;
    else if (strcmp(serv, "sink:") == 0)
        tipo = T_SINK;
    else if (strcmp(serv, "nookay:") == 0)
        tipo = T_NOOKAY;
    else if (strncmp(serv, "sorgente:", 9) == 0)
        tipo = T_SORGENTE;
    else if (strcmp(serv, "tardivo:") == 0 || strncmp(serv, "ostile:", 7) == 0 ||
             strncmp(serv, "fuori:", 6) == 0 ||
             strcmp(serv, "apertura0:") == 0)
        tipo = T_SILENZIO;
    else if (strncmp(serv, "shell,v2,raw:", 13) == 0) {
        tipo = T_SHELL;
        cmd = serv + 13;
    } else
        c = NULL; /* sconosciuto: rifiutato */
    if (c == NULL)
        return manda(ft, PH_ADB_CLSE, 0, p->arg0, NULL, 0);
    c->usato = true;
    c->ok_pendente = false;
    c->tipo = tipo;
    c->mio = ++ft->prossimo;
    c->client = p->arg0;
    c->restanti = c->mandati = 0;
    c->nonack = c->ok_bytes = 0;
    c->ok_apertura = false;
    /* arg1 dell'OPEN: la finestra concessa dal client (delayed_ack), altrimenti 0 */
    c->asb = ft->delayed ? (int64_t)p->arg1 : 0;
    if ((ft->delayed && p->arg1 == 0) || (!ft->delayed && p->arg1 != 0))
        atomic_fetch_add(&ft->violazioni, 1);
    if (strcmp(serv, "tardivo:") == 0) {
        /* l'OKAY parte dal ciclo principale dopo 400 ms: il client ha gia' rinunciato */
        c->ok_pendente = true;
        c->ok_apertura = true;
        c->ok_scadenza = ora_ms() + 400;
        return 0;
    }
    if (strcmp(serv, "apertura0:") == 0) {
        /* OKAY di apertura senza carico: con delayed_ack e' una finestra non concessa */
        if (manda(ft, PH_ADB_OKAY, c->mio, c->client, NULL, 0) != 0)
            return -1;
        return 0;
    }
    if (manda_ok_apertura(ft, c) != 0)
        return -1;
    if (tipo == T_SORGENTE) {
        c->restanti = strtoull(serv + 9, NULL, 10);
        return sorgente_avanti(ft, c);
    }
    if (strncmp(serv, "fuori:", 6) == 0) {
        /* ignora la finestra del client: deve farlo cadere */
        uint8_t *g = malloc(ft->max_dati);
        if (g == NULL)
            return -1;
        memset(g, 0x5a, ft->max_dati);
        uint64_t resto = strtoull(serv + 6, NULL, 10);
        int rc = 0;
        while (resto > 0 && rc == 0) {
            size_t k = resto < ft->max_dati ? (size_t)resto : ft->max_dati;
            rc = manda(ft, PH_ADB_WRTE, c->mio, c->client, g, k);
            resto -= k;
        }
        free(g);
        return rc;
    }
    if (strncmp(serv, "ostile:", 7) == 0)
        return ostile(ft, c, atoi(serv + 7));
    if (strcmp(serv, "chiudi:") == 0) {
        if (manda(ft, PH_ADB_WRTE, c->mio, c->client, "abc", 3) != 0)
            return -1;
        return manda(ft, PH_ADB_CLSE, c->mio, c->client, NULL, 0);
    }
    if (strcmp(serv, "grande:") == 0) {
        enum { TOT = 300000, PEZZO = 60000 };
        uint8_t *g = malloc(TOT);
        if (g == NULL)
            return -1;
        for (size_t i = 0; i < TOT; i++)
            g[i] = ft_pattern(2, i);
        int rc = 0;
        for (size_t off = 0; off < TOT && rc == 0; off += PEZZO)
            rc = manda(ft, PH_ADB_WRTE, c->mio, c->client, g + off, PEZZO);
        free(g);
        return rc;
    }
    if (tipo == T_SHELL)
        return servi_shell(ft, c, cmd);
    return 0;
}

static Canale *trova(FintoTelefono *ft, uint32_t mio)
{
    for (int i = 0; i < MAX_CANALI; i++)
        if (ft->canali[i].usato && ft->canali[i].mio == mio)
            return &ft->canali[i];
    return NULL;
}

static int scrittura(FintoTelefono *ft, const PhPacchettoAdb *p)
{
    Canale *c = trova(ft, p->arg1);
    if (c == NULL)
        return manda(ft, PH_ADB_CLSE, p->arg1, p->arg0, NULL, 0);
    if (p->lunghezza > ft->max_dati)
        atomic_fetch_add(&ft->violazioni, 1);
    if (ft->delayed) {
        /* il client non puo' avere in volo piu' della finestra che gli abbiamo concesso */
        if (c->nonack >= FT_FINESTRA)
            atomic_fetch_add(&ft->violazioni, 1);
        c->nonack += p->lunghezza;
        if ((long)c->nonack > atomic_load(&ft->max_in_volo))
            atomic_store(&ft->max_in_volo, (long)c->nonack);
    }
    if (c->tipo == T_LENTO) {
        atomic_fetch_add(&ft->byte_lento, p->lunghezza);
        if (ft->delayed) {
            /* una sola conferma cumulativa 100 ms dopo il primo pacchetto */
            if (!c->ok_pendente) {
                c->ok_pendente = true;
                c->ok_scadenza = ora_ms() + 100;
            }
            c->ok_bytes += p->lunghezza;
            return 0;
        }
        /* se il client rispetta il controllo di flusso, finche' non mandiamo
         * l'OKAY non puo' averci scritto un altro WRTE */
        if (c->ok_pendente)
            atomic_fetch_add(&ft->violazioni, 1);
        c->ok_pendente = true;
        c->ok_scadenza = ora_ms() + 100;
        return 0; /* l'OKAY parte dal ciclo principale dopo 100 ms */
    }
    if (c->tipo == T_NOOKAY)
        return 0;
    c->nonack = c->nonack >= p->lunghezza ? c->nonack - p->lunghezza : 0;
    if (manda_ack(ft, c, p->lunghezza) != 0)
        return -1;
    if (c->tipo == T_SINK)
        atomic_fetch_add(&ft->byte_sink, p->lunghezza);
    if (c->tipo == T_ECO)
        return manda(ft, PH_ADB_WRTE, c->mio, c->client, ft->dati, p->lunghezza);
    return 0;
}

static int accetta_tls(FintoTelefono *ft)
{
    ft->ssl = SSL_new(ft->ctx);
    if (ft->ssl == NULL || SSL_set_fd(ft->ssl, atomic_load(&ft->fd)) != 1 || SSL_accept(ft->ssl) != 1)
        return -1;
    X509 *x = SSL_get1_peer_certificate(ft->ssl);
    if (x != NULL) {
        atomic_store(&ft->cert_client, true);
        X509_free(x);
    }
    return 0;
}

static int verifica_sempre(X509_STORE_CTX *s, void *arg)
{
    (void)s;
    (void)arg;
    return 1; /* come adbd: il certificato del client e' autofirmato; conta la chiave */
}

static void *thread_telefono(void *arg)
{
    FintoTelefono *ft = arg;
    PhPacchettoAdb p;
    int s = accept(ft->ascolto, NULL, NULL);
    if (s < 0)
        return NULL;
    atomic_store(&ft->fd, s);
    int uno = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &uno, sizeof uno); /* come un vero adbd: niente Nagle */
    if (leggi_pacchetto(ft, &p) != 0 || p.comando != PH_ADB_CNXN)
        goto fine;
    /* delayed_ack vale solo se lo dichiarano entrambi. Come adbd, il nome seguito dal '\0' finale
     * (senza ';' o ',' dopo) NON conta: e' la trappola che faceva rifiutare i canali. */
    ft->delayed = ft->delayed_cfg && ft->modo != FT_BANNER_INGANNO && p.lunghezza > 0 && memchr(ft->dati, '\0', p.lunghezza) != NULL &&
                  (strstr((const char *)ft->dati, "delayed_ack;") != NULL ||
                   strstr((const char *)ft->dati, "delayed_ack,") != NULL);
    if (ft->modo == FT_AUTH_SUBITO) {
        manda(ft, PH_ADB_AUTH, 1, 0, "0123456789012345678901234567890123456789", 20);
        usleep(200 * 1000);
        goto fine;
    }
    if (manda(ft, PH_ADB_STLS, PH_ADB_VERSIONE_STLS, 0, NULL, 0) != 0 || leggi_pacchetto(ft, &p) != 0 ||
        p.comando != PH_ADB_STLS || accetta_tls(ft) != 0)
        goto fine;
    if (ft->modo == FT_CHIUDE_DOPO_TLS)
        goto fine;
    if (ft->modo == FT_AUTH_DOPO_TLS) {
        manda(ft, PH_ADB_AUTH, 1, 0, "0123456789012345678901234567890123456789", 20);
        usleep(200 * 1000);
        goto fine;
    }
    static const char banner_a[] = "device::ro.product.model=FINTO;features=shell_v2,cmd";
    static const char banner_i[] =
        "device::ro.product.model=features=delayed_ack;features=shell_v2,cmd";
    static const char banner_d[] = "device::ro.product.model=FINTO;features=shell_v2,cmd,delayed_ack";
    int rc_cnxn = ft->modo == FT_BANNER_INGANNO
                      ? manda(ft, PH_ADB_CNXN, PH_ADB_VERSIONE, ft->max_dati, banner_i, sizeof banner_i)
                  : ft->delayed_cfg
                      ? manda(ft, PH_ADB_CNXN, PH_ADB_VERSIONE, ft->max_dati, banner_d, sizeof banner_d)
                      : manda(ft, PH_ADB_CNXN, PH_ADB_VERSIONE, ft->max_dati, banner_a, sizeof banner_a);
    if (rc_cnxn != 0)
        goto fine;
    for (;;) {
        /* OKAY ritardati dei canali "lento:" */
        int64_t prima = -1;
        for (int i = 0; i < MAX_CANALI; i++)
            if (ft->canali[i].usato && ft->canali[i].ok_pendente &&
                (prima < 0 || ft->canali[i].ok_scadenza < prima))
                prima = ft->canali[i].ok_scadenza;
        if (prima >= 0) {
            int64_t resta = prima - ora_ms();
            struct pollfd pf = {atomic_load(&ft->fd), POLLIN, 0};
            if (SSL_pending(ft->ssl) == 0 && poll(&pf, 1, resta > 0 ? (int)resta : 0) == 0) {
                for (int i = 0; i < MAX_CANALI; i++) {
                    Canale *c = &ft->canali[i];
                    if (c->usato && c->ok_pendente && c->ok_scadenza <= ora_ms()) {
                        c->ok_pendente = false;
                        uint64_t n = c->ok_apertura ? FT_FINESTRA : c->ok_bytes;
                        c->nonack = c->nonack >= c->ok_bytes ? c->nonack - c->ok_bytes : 0;
                        c->ok_bytes = 0;
                        c->ok_apertura = false;
                        if (manda_ack(ft, c, (uint32_t)n) != 0)
                            goto fine;
                    }
                }
                continue;
            }
        }
        if (leggi_pacchetto(ft, &p) != 0)
            break;
        int rc = 0;
        if (p.comando == PH_ADB_OPEN)
            rc = apri(ft, &p);
        else if (p.comando == PH_ADB_WRTE)
            rc = scrittura(ft, &p);
        else if (p.comando == PH_ADB_OKAY) {
            atomic_fetch_add(&ft->okay, 1);
            Canale *c = trova(ft, p.arg1);
            if (c != NULL && c->tipo == T_SORGENTE) {
                if (ft->delayed && p.lunghezza >= 4)
                    c->asb += (int64_t)((uint32_t)ft->dati[0] | ((uint32_t)ft->dati[1] << 8) |
                                        ((uint32_t)ft->dati[2] << 16) | ((uint32_t)ft->dati[3] << 24));
                rc = sorgente_avanti(ft, c);
            }
        }
        else if (p.comando == PH_ADB_CLSE) {
            atomic_fetch_add(&ft->clse, 1);
            Canale *c = trova(ft, p.arg1);
            if (c != NULL) {
                c->usato = false;
                rc = manda(ft, PH_ADB_CLSE, c->mio, c->client, NULL, 0);
            }
        }
        if (rc != 0)
            break;
    }
fine:
    /* il telefono chiude davvero: il client deve vedere la fine del flusso */
    shutdown(atomic_load(&ft->fd), SHUT_RDWR);
    return NULL;
}

FintoTelefono *ft_avvia(const PhChiave *chiave, FtModo modo, uint16_t *porta)
{
    return ft_avvia_max(chiave, modo, FT_MAX_DATI, porta);
}

FintoTelefono *ft_avvia_max(const PhChiave *chiave, FtModo modo, uint32_t max_dati, uint16_t *porta)
{
    return ft_avvia_cfg(chiave, modo, max_dati, false, porta);
}

FintoTelefono *ft_avvia_cfg(const PhChiave *chiave, FtModo modo, uint32_t max_dati, bool delayed_ack,
                            uint16_t *porta)
{
    FintoTelefono *ft = calloc(1, sizeof *ft);
    if (ft == NULL)
        return NULL;
    ft->chiave = chiave;
    ft->modo = modo;
    ft->max_dati = max_dati;
    ft->delayed_cfg = delayed_ack;
    atomic_init(&ft->fd, -1);
    ft->dati = malloc(PH_ADB_MAX_DATI);
    ft->ctx = SSL_CTX_new(TLS_server_method());
    if (ft->dati == NULL || ft->ctx == NULL ||
        SSL_CTX_set_min_proto_version(ft->ctx, TLS1_3_VERSION) != 1 ||
        SSL_CTX_use_certificate(ft->ctx, ph_chiave_x509(chiave)) != 1 ||
        SSL_CTX_use_PrivateKey(ft->ctx, ph_chiave_evp(chiave)) != 1)
        goto errore;
    /* chiede il certificato al client, come adbd, e lo accetta qualunque sia */
    SSL_CTX_set_verify(ft->ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    SSL_CTX_set_cert_verify_callback(ft->ctx, verifica_sempre, NULL);

    ft->ascolto = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t l = sizeof sa;
    if (ft->ascolto < 0 || bind(ft->ascolto, (struct sockaddr *)&sa, sizeof sa) != 0 ||
        listen(ft->ascolto, 1) != 0 || getsockname(ft->ascolto, (struct sockaddr *)&sa, &l) != 0)
        goto errore;
    *porta = ntohs(sa.sin_port);
    if (pthread_create(&ft->thread, NULL, thread_telefono, ft) != 0)
        goto errore;
    return ft;
errore:
    if (ft->ascolto > 0)
        close(ft->ascolto);
    SSL_CTX_free(ft->ctx);
    free(ft->dati);
    free(ft);
    return NULL;
}

void ft_cadi(FintoTelefono *ft)
{
    int s = atomic_load(&ft->fd);
    if (s >= 0)
        shutdown(s, SHUT_RDWR);
}

void ft_ferma(FintoTelefono *ft)
{
    /* se il client non si e' mai collegato, il thread e' fermo in accept() */
    shutdown(ft->ascolto, SHUT_RDWR);
    ft_cadi(ft);
    pthread_join(ft->thread, NULL);
    if (ft->ssl != NULL)
        SSL_free(ft->ssl);
    if (atomic_load(&ft->fd) >= 0)
        close(atomic_load(&ft->fd));
    close(ft->ascolto);
    SSL_CTX_free(ft->ctx);
    free(ft->dati);
    free(ft);
}

bool ft_certificato_client_ricevuto(FintoTelefono *ft) { return atomic_load(&ft->cert_client); }
int ft_violazioni_flusso(FintoTelefono *ft) { return atomic_load(&ft->violazioni); }
long ft_byte_lento(FintoTelefono *ft) { return atomic_load(&ft->byte_lento); }
int ft_okay_ricevuti(FintoTelefono *ft) { return atomic_load(&ft->okay); }
int ft_wrte_inviati(FintoTelefono *ft) { return atomic_load(&ft->wrte); }
int ft_clse_ricevuti(FintoTelefono *ft) { return atomic_load(&ft->clse); }
long ft_byte_sorgente(FintoTelefono *ft) { return atomic_load(&ft->byte_sorgente); }
long ft_byte_sink(FintoTelefono *ft) { return atomic_load(&ft->byte_sink); }
long ft_max_in_volo(FintoTelefono *ft) { return atomic_load(&ft->max_in_volo); }
