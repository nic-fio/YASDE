/* Abbinamento con codice (Android "Associa dispositivo con codice").
 * Sequenza, ricavata dalla documentazione del protocollo:
 *  1. TLS 1.3 verso la porta di abbinamento, con il certificato della nostra
 *     chiave; quello del telefono non si verifica (e' autofirmato): la
 *     garanzia viene dal codice a sei cifre.
 *  2. Dal canale TLS si esportano 64 byte legati a QUESTA connessione.
 *  3. SPAKE2 con password = codice + quei 64 byte: chi sta in mezzo non
 *     puo' riusare lo scambio su un'altra connessione.
 *  4. Dalla chiave SPAKE2 si deriva una chiave AES-128-GCM; ci si scambiano
 *     le informazioni (la nostra chiave pubblica) cifrate. Se il codice e'
 *     sbagliato la decifratura fallisce: e' l'unica conferma. */
#include "abbinamento/abbinamento.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/ssl.h>

#include "abbinamento/spake2.h"

enum {
    VERSIONE = 1,
    TIPO_SPAKE2 = 0,
    TIPO_INFORMAZIONI = 1,
    INFORMAZIONI = 8192,          /* dimensione fissa: 1 byte di tipo + 8191 di dati */
    CARICO_MASSIMO = 2 * INFORMAZIONI,
    SIGILLO = 16,                 /* autenticazione di AES-GCM */
    ESPORTATI = 64,
};

static const uint8_t NOME_PC[] = "adb pair client";       /* con lo zero finale: 16 byte */
static const uint8_t NOME_TELEFONO[] = "adb pair server"; /* idem */
static const char ETICHETTA[] = "adb-label";              /* si esporta con lo zero finale */
static const char INFO_CHIAVE[] = "adb pairing_auth aes-128-gcm key"; /* senza lo zero */

static int collega_tcp(const char *host, uint16_t porta, int timeout_ms, PhErrore *e)
{
    char servizio[8];
    snprintf(servizio, sizeof servizio, "%u", (unsigned)porta);
    struct addrinfo cerca = {0}, *trovati = NULL;
    cerca.ai_socktype = SOCK_STREAM;
    cerca.ai_family = AF_UNSPEC;
    int r = getaddrinfo(host, servizio, &cerca, &trovati);
    if (r != 0)
        return ph_errore(e, "indirizzo %s non valido: %s", host, gai_strerror(r));
    int fd = -1;
    for (struct addrinfo *a = trovati; a != NULL && fd < 0; a = a->ai_next) {
        int s = socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
        if (s < 0)
            continue;
        int segni = fcntl(s, F_GETFL, 0);
        if (segni < 0 || fcntl(s, F_SETFL, segni | O_NONBLOCK) < 0) {
            close(s);
            continue;
        }
        int c = connect(s, a->ai_addr, a->ai_addrlen);
        if (c < 0 && errno == EINPROGRESS) {
            struct pollfd p = {.fd = s, .events = POLLOUT};
            int errore = 0;
            socklen_t n = sizeof errore;
            int pronto;
            do { /* un segnale che interrompe l'attesa non e' "telefono muto" */
                pronto = poll(&p, 1, timeout_ms);
            } while (pronto < 0 && errno == EINTR);
            if (pronto == 1 &&
                getsockopt(s, SOL_SOCKET, SO_ERROR, &errore, &n) == 0 && errore == 0)
                c = 0;
        }
        if (c == 0 && fcntl(s, F_SETFL, segni) == 0)
            fd = s;
        else
            close(s);
    }
    freeaddrinfo(trovati);
    if (fd < 0)
        return ph_errore(e, "il telefono non risponde su %s:%u", host, (unsigned)porta);
    /* Da qui letture e scritture bloccano, ma mai oltre il tempo dato. */
    struct timeval t = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof t) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof t) != 0) {
        close(fd);
        return ph_errore(e, "impossibile impostare il tempo massimo sul collegamento");
    }
    return fd;
}

static int scrivi_tutto(SSL *ssl, const uint8_t *dati, size_t n)
{
    while (n > 0) {
        int r = SSL_write(ssl, dati, (int)(n > 16384 ? 16384 : n));
        if (r <= 0)
            return -1;
        dati += r;
        n -= (size_t)r;
    }
    return 0;
}

static int leggi_tutto(SSL *ssl, uint8_t *dati, size_t n)
{
    while (n > 0) {
        int r = SSL_read(ssl, dati, (int)(n > 16384 ? 16384 : n));
        if (r <= 0)
            return -1;
        dati += r;
        n -= (size_t)r;
    }
    return 0;
}

static int manda_pacchetto(SSL *ssl, uint8_t tipo, const uint8_t *carico, size_t n)
{
    uint8_t testa[6] = {VERSIONE, tipo, (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8),
                        (uint8_t)n};
    return (scrivi_tutto(ssl, testa, sizeof testa) == 0 && scrivi_tutto(ssl, carico, n) == 0) ? 0 : -1;
}

/* Legge un pacchetto del tipo atteso in carico[capacita]; scrive la lunghezza
 * in *n. La lunghezza dichiarata dal telefono si controlla prima di leggere. */
static int ricevi_pacchetto(SSL *ssl, uint8_t tipo_atteso, uint8_t *carico, size_t capacita,
                            size_t *n, PhErrore *e)
{
    uint8_t testa[6];
    if (leggi_tutto(ssl, testa, sizeof testa) != 0)
        return ph_errore(e, "il telefono ha chiuso l'abbinamento (codice scaduto o schermata chiusa?)");
    size_t lunghezza = ((size_t)testa[2] << 24) | ((size_t)testa[3] << 16) | ((size_t)testa[4] << 8) |
                       (size_t)testa[5];
    if (testa[0] != VERSIONE)
        return ph_errore(e, "versione dell'abbinamento non supportata (%u)", (unsigned)testa[0]);
    if (testa[1] != tipo_atteso)
        return ph_errore(e, "risposta inattesa dal telefono durante l'abbinamento");
    if (lunghezza == 0 || lunghezza > CARICO_MASSIMO || lunghezza > capacita)
        return ph_errore(e, "risposta del telefono di lunghezza non valida");
    if (leggi_tutto(ssl, carico, lunghezza) != 0)
        return ph_errore(e, "risposta del telefono interrotta");
    *n = lunghezza;
    return 0;
}

static int deriva_chiave_aes(const uint8_t segreto[PH_SPAKE2_CHIAVE], uint8_t chiave[16])
{
    EVP_PKEY_CTX *k = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
    size_t n = 16;
    int esito = -1;
    if (k != NULL && EVP_PKEY_derive_init(k) == 1 && EVP_PKEY_CTX_set_hkdf_md(k, EVP_sha256()) == 1 &&
        EVP_PKEY_CTX_set1_hkdf_key(k, segreto, PH_SPAKE2_CHIAVE) == 1 &&
        EVP_PKEY_CTX_add1_hkdf_info(k, (const unsigned char *)INFO_CHIAVE,
                                    (int)(sizeof INFO_CHIAVE - 1)) == 1 &&
        EVP_PKEY_derive(k, chiave, &n) == 1 && n == 16)
        esito = 0;
    EVP_PKEY_CTX_free(k);
    return esito;
}

/* AES-128-GCM. Il numero d'ordine (nonce) e' un contatore: 8 byte little
 * endian + 4 zeri; ogni direzione ha il suo contatore che parte da 0. Qui
 * ogni lato manda un solo messaggio, quindi il contatore vale sempre 0. */
static int aes_gcm(int cifra, const uint8_t chiave[16], uint64_t contatore, const uint8_t *in,
                   size_t n, uint8_t *out, uint8_t sigillo[SIGILLO])
{
    uint8_t ordine[12] = {0};
    for (int i = 0; i < 8; i++)
        ordine[i] = (uint8_t)(contatore >> (8 * i));
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    int fatti = 0, coda = 0, esito = -1;
    if (c == NULL || EVP_CipherInit_ex(c, EVP_aes_128_gcm(), NULL, chiave, ordine, cifra) != 1)
        goto fine;
    if (!cifra && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, SIGILLO, sigillo) != 1)
        goto fine;
    if (EVP_CipherUpdate(c, out, &fatti, in, (int)n) != 1 || (size_t)fatti != n)
        goto fine;
    if (EVP_CipherFinal_ex(c, out + fatti, &coda) != 1) /* in decifratura: sigillo sbagliato */
        goto fine;
    if (cifra && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, SIGILLO, sigillo) != 1)
        goto fine;
    esito = 0;
fine:
    EVP_CIPHER_CTX_free(c);
    return esito;
}

int ph_abbinamento_esegui(const char *host, uint16_t porta, const char *codice,
                          const PhChiave *chiave, int timeout_ms, PhErrore *e)
{
    if (host == NULL || codice == NULL || chiave == NULL)
        return ph_errore(e, "abbinamento: dati mancanti");
    size_t n_codice = strlen(codice);
    if (n_codice == 0 || n_codice > 32)
        return ph_errore(e, "il codice di abbinamento non e' valido");
    if (timeout_ms <= 0)
        timeout_ms = 10000;
    /* OpenSSL scrive con write(): se il telefono chiude mentre scriviamo
     * (codice scaduto, schermata chiusa) il processo riceverebbe SIGPIPE e
     * morirebbe. L'abbinamento avviene prima di ogni collegamento, quindi non
     * possiamo contare sul pezzo adb per averlo gia' ignorato. */
    signal(SIGPIPE, SIG_IGN);

    int esito = -1;
    int fd = -1;
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    PhSpake2 *spake = NULL;
    PhBuffer pubblica = PH_BUFFER_VUOTO;
    uint8_t password[32 + ESPORTATI];
    uint8_t mio[PH_SPAKE2_MESSAGGIO], suo[PH_SPAKE2_MESSAGGIO];
    uint8_t segreto[PH_SPAKE2_CHIAVE], aes[16];
    uint8_t *chiaro = calloc(1, INFORMAZIONI);
    uint8_t *cifrato = malloc(CARICO_MASSIMO);
    size_t n = 0;
    if (chiaro == NULL || cifrato == NULL) {
        ph_errore(e, "memoria finita");
        goto fine;
    }

    fd = collega_tcp(host, porta, timeout_ms, e);
    if (fd < 0)
        goto fine;

    ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == NULL || SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION) != 1 ||
        SSL_CTX_use_certificate(ctx, ph_chiave_x509(chiave)) != 1 ||
        SSL_CTX_use_PrivateKey(ctx, ph_chiave_evp(chiave)) != 1) {
        ph_errore(e, "impossibile preparare il collegamento cifrato per l'abbinamento");
        goto fine;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    ssl = SSL_new(ctx);
    if (ssl == NULL || SSL_set_fd(ssl, fd) != 1 || SSL_connect(ssl) != 1) {
        ph_errore(e, "collegamento cifrato con il telefono non riuscito");
        goto fine;
    }

    /* password = codice + 64 byte esportati da questa connessione TLS */
    memcpy(password, codice, n_codice);
    if (SSL_export_keying_material(ssl, password + n_codice, ESPORTATI, ETICHETTA, sizeof ETICHETTA,
                                   NULL, 0, 0) != 1) {
        ph_errore(e, "impossibile legare il codice al collegamento cifrato");
        goto fine;
    }

    spake = ph_spake2_crea(PH_SPAKE2_ALICE, NOME_PC, sizeof NOME_PC, NOME_TELEFONO,
                           sizeof NOME_TELEFONO);
    if (spake == NULL || ph_spake2_messaggio(spake, password, n_codice + ESPORTATI, mio) != 0) {
        ph_errore(e, "errore interno nello scambio del codice");
        goto fine;
    }
    if (manda_pacchetto(ssl, TIPO_SPAKE2, mio, sizeof mio) != 0) {
        ph_errore(e, "invio al telefono non riuscito");
        goto fine;
    }
    if (ricevi_pacchetto(ssl, TIPO_SPAKE2, suo, sizeof suo, &n, e) != 0)
        goto fine;
    if (n != sizeof suo || ph_spake2_chiave(spake, suo, segreto) != 0 ||
        deriva_chiave_aes(segreto, aes) != 0) {
        ph_errore(e, "risposta del telefono non valida nello scambio del codice");
        goto fine;
    }

    /* Le nostre informazioni: tipo 0 = chiave pubblica ADB, poi il testo
     * della chiave; il resto degli 8192 byte resta a zero. */
    if (ph_chiave_pubblica_adb(chiave, &pubblica, e) != 0)
        goto fine;
    if (pubblica.lunghezza > INFORMAZIONI - 2) {
        ph_errore(e, "chiave pubblica troppo lunga");
        goto fine;
    }
    chiaro[0] = 0;
    memcpy(chiaro + 1, pubblica.dati, pubblica.lunghezza);
    if (aes_gcm(1, aes, 0, chiaro, INFORMAZIONI, cifrato, cifrato + INFORMAZIONI) != 0) {
        ph_errore(e, "errore interno nella cifratura");
        goto fine;
    }
    if (manda_pacchetto(ssl, TIPO_INFORMAZIONI, cifrato, INFORMAZIONI + SIGILLO) != 0) {
        ph_errore(e, "invio al telefono non riuscito");
        goto fine;
    }

    /* Se il codice e' sbagliato il telefono non riesce a leggere il nostro
     * messaggio e chiude, oppure noi non riusciamo a leggere il suo. */
    PhErrore ignorato;
    if (ricevi_pacchetto(ssl, TIPO_INFORMAZIONI, cifrato, CARICO_MASSIMO, &n, &ignorato) != 0 ||
        n != INFORMAZIONI + SIGILLO ||
        aes_gcm(0, aes, 0, cifrato, INFORMAZIONI, chiaro, cifrato + INFORMAZIONI) != 0) {
        ph_errore(e, "codice di abbinamento sbagliato o scaduto");
        goto fine;
    }
    esito = 0;

fine:
    if (ssl != NULL) {
        if (esito == 0)
            SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    SSL_CTX_free(ctx);
    if (fd >= 0)
        close(fd);
    ph_spake2_libera(spake);
    ph_buffer_libera(&pubblica);
    OPENSSL_cleanse(password, sizeof password);
    OPENSSL_cleanse(segreto, sizeof segreto);
    OPENSSL_cleanse(aes, sizeof aes);
    if (chiaro != NULL)
        OPENSSL_cleanse(chiaro, INFORMAZIONI);
    free(chiaro);
    free(cifrato);
    ERR_clear_error();
    return esito;
}
