/* Chiave RSA del PC: file PEM, certificato autofirmato per il TLS e formato
 * ADB della chiave pubblica. Solo API EVP/BN di OpenSSL 3 (le RSA_* a basso
 * livello sono deprecate). */
#include "adb/adb.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define RSA_BIT 2048
#define RSA_BYTE (RSA_BIT / 8)
#define RSA_PAROLE (RSA_BYTE / 4)
/* struct RSAPublicKey di Android: len, n0inv, n[64], rr[64], exponent */
#define BLOB_ADB (4 + 4 + RSA_BYTE + RSA_BYTE + 4)

struct PhChiave {
    EVP_PKEY *pkey;
    X509 *x509;
};

static void errore_ssl(PhErrore *e, const char *contesto)
{
    char msg[160] = "errore sconosciuto";
    unsigned long c = ERR_get_error();
    if (c != 0)
        ERR_error_string_n(c, msg, sizeof msg);
    ERR_clear_error();
    ph_errore(e, "%s: %s", contesto, msg);
}

/* Crea la cartella e le cartelle superiori (0700), come mkdir -p. */
static int crea_cartelle(const char *percorso, PhErrore *e)
{
    char *copia = strdup(percorso);
    if (copia == NULL)
        return ph_errore(e, "memoria finita");
    char *ultimo = strrchr(copia, '/');
    if (ultimo == NULL || ultimo == copia) {
        free(copia);
        return 0;
    }
    *ultimo = '\0';
    for (char *p = copia + 1; ; p++) {
        if (*p == '/' || *p == '\0') {
            char c = *p;
            *p = '\0';
            if (mkdir(copia, 0700) != 0 && errno != EEXIST) {
                int en = errno;
                free(copia);
                return ph_errore(e, "impossibile creare la cartella della chiave: %s", strerror(en));
            }
            *p = c;
            if (c == '\0')
                break;
        }
    }
    free(copia);
    return 0;
}

static int salva_pem(EVP_PKEY *pkey, const char *percorso, PhErrore *e)
{
    if (crea_cartelle(percorso, e) != 0)
        return -1;
    /* O_EXCL + 0600: la chiave privata non e' mai leggibile da altri, nemmeno per un attimo */
    int fd = open(percorso, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0)
        return ph_errore(e, "impossibile creare il file della chiave: %s", strerror(errno));
    FILE *f = fdopen(fd, "w");
    if (f == NULL) {
        close(fd);
        unlink(percorso);
        return ph_errore(e, "impossibile scrivere il file della chiave");
    }
    /* senza cifratura PEM_write_PrivateKey scrive PKCS#8 ("BEGIN PRIVATE KEY") */
    int ok = PEM_write_PrivateKey(f, pkey, NULL, NULL, 0, NULL, NULL);
    if (fclose(f) != 0)
        ok = 0;
    if (!ok) {
        unlink(percorso);
        errore_ssl(e, "scrittura della chiave");
        return -1;
    }
    return 0;
}

static int crea_certificato(PhChiave *c, PhErrore *e)
{
    X509 *x = X509_new();
    if (x == NULL) {
        errore_ssl(e, "certificato");
        return -1;
    }
    X509_NAME *nome = X509_get_subject_name(x);
    if (X509_set_version(x, 2) != 1 ||
        ASN1_INTEGER_set(X509_get_serialNumber(x), 1) != 1 ||
        X509_gmtime_adj(X509_getm_notBefore(x), -86400L) == NULL ||
        X509_gmtime_adj(X509_getm_notAfter(x), 10L * 365 * 86400) == NULL ||
        X509_set_pubkey(x, c->pkey) != 1 ||
        X509_NAME_add_entry_by_txt(nome, "CN", MBSTRING_ASC, (const unsigned char *)"yasde", -1,
                                   -1, 0) != 1 ||
        X509_set_issuer_name(x, nome) != 1 ||
        X509_sign(x, c->pkey, EVP_sha256()) == 0) {
        errore_ssl(e, "certificato");
        X509_free(x);
        return -1;
    }
    c->x509 = x;
    return 0;
}

PhChiave *ph_chiave_carica_o_crea(const char *percorso, PhErrore *e)
{
    PhChiave *c = calloc(1, sizeof *c);
    if (c == NULL) {
        ph_errore(e, "memoria finita");
        return NULL;
    }
    FILE *f = fopen(percorso, "r");
    if (f != NULL) {
        c->pkey = PEM_read_PrivateKey(f, NULL, NULL, NULL);
        fclose(f);
        if (c->pkey == NULL) {
            errore_ssl(e, "lettura della chiave");
            goto errore;
        }
    } else if (errno == ENOENT) {
        c->pkey = EVP_RSA_gen(RSA_BIT);
        if (c->pkey == NULL) {
            errore_ssl(e, "generazione della chiave");
            goto errore;
        }
        if (salva_pem(c->pkey, percorso, e) != 0)
            goto errore;
    } else {
        ph_errore(e, "impossibile aprire il file della chiave: %s", strerror(errno));
        goto errore;
    }
    if (EVP_PKEY_get_base_id(c->pkey) != EVP_PKEY_RSA || EVP_PKEY_get_bits(c->pkey) != RSA_BIT) {
        ph_errore(e, "la chiave non e' una RSA a 2048 bit");
        goto errore;
    }
    if (crea_certificato(c, e) != 0)
        goto errore;
    return c;
errore:
    ph_chiave_libera(c);
    return NULL;
}

void ph_chiave_libera(PhChiave *c)
{
    if (c == NULL)
        return;
    X509_free(c->x509);
    EVP_PKEY_free(c->pkey);
    free(c);
}

void *ph_chiave_evp(const PhChiave *c)
{
    return c->pkey;
}

void *ph_chiave_x509(const PhChiave *c)
{
    return c->x509;
}

static void scrivi32(uint8_t *d, uint32_t v)
{
    d[0] = (uint8_t)v;
    d[1] = (uint8_t)(v >> 8);
    d[2] = (uint8_t)(v >> 16);
    d[3] = (uint8_t)(v >> 24);
}

/* Costruisce la struct RSAPublicKey di Android:
 *   len (parole da 32 bit del modulo), n0inv = -1/n[0] mod 2^32,
 *   modulo n, rr = (2^2048)^2 mod n, esponente; tutto little endian.
 * n0inv e rr servono ad adbd per la moltiplicazione di Montgomery. */
static int costruisci_blob(const PhChiave *c, uint8_t blob[BLOB_ADB], PhErrore *e)
{
    BIGNUM *n = NULL, *esp = NULL, *r32 = BN_new(), *n0 = BN_new(), *inv = NULL, *rr = BN_new();
    BN_CTX *ctx = BN_CTX_new();
    int rc = -1;

    if (r32 == NULL || n0 == NULL || rr == NULL || ctx == NULL ||
        EVP_PKEY_get_bn_param(c->pkey, OSSL_PKEY_PARAM_RSA_N, &n) != 1 ||
        EVP_PKEY_get_bn_param(c->pkey, OSSL_PKEY_PARAM_RSA_E, &esp) != 1) {
        errore_ssl(e, "chiave pubblica");
        goto fine;
    }
    BN_ULONG ev = BN_get_word(esp);
    if (ev == (BN_ULONG)-1 || ev > 0xffffffffu) {
        ph_errore(e, "esponente RSA troppo grande");
        goto fine;
    }
    if (BN_set_bit(r32, 32) != 1 || BN_mod(n0, n, r32, ctx) != 1 ||
        (inv = BN_mod_inverse(NULL, n0, r32, ctx)) == NULL) {
        errore_ssl(e, "chiave pubblica");
        goto fine;
    }
    uint32_t n0inv = (uint32_t)0 - (uint32_t)BN_get_word(inv);

    BN_zero(rr);
    if (BN_set_bit(rr, 2 * RSA_BIT) != 1 || BN_mod(rr, rr, n, ctx) != 1) {
        errore_ssl(e, "chiave pubblica");
        goto fine;
    }
    scrivi32(blob, RSA_PAROLE);
    scrivi32(blob + 4, n0inv);
    if (BN_bn2lebinpad(n, blob + 8, RSA_BYTE) != RSA_BYTE ||
        BN_bn2lebinpad(rr, blob + 8 + RSA_BYTE, RSA_BYTE) != RSA_BYTE) {
        ph_errore(e, "chiave pubblica di dimensione inattesa");
        goto fine;
    }
    scrivi32(blob + 8 + 2 * RSA_BYTE, (uint32_t)ev);
    rc = 0;
fine:
    BN_free(n);
    BN_free(esp);
    BN_free(r32);
    BN_free(n0);
    BN_free(inv);
    BN_free(rr);
    BN_CTX_free(ctx);
    return rc;
}

int ph_chiave_pubblica_adb(const PhChiave *c, PhBuffer *uscita, PhErrore *e)
{
    uint8_t blob[BLOB_ADB];
    if (costruisci_blob(c, blob, e) != 0)
        return -1;
    char b64[((BLOB_ADB + 2) / 3) * 4 + 1];
    int nb = EVP_EncodeBlock((unsigned char *)b64, blob, BLOB_ADB);
    if (nb <= 0)
        return ph_errore(e, "codifica base64 della chiave fallita");

    char utente[64] = "yasde", host[128] = "pc";
    const char *u = getenv("USER");
    if (u != NULL && *u != '\0' && strlen(u) < sizeof utente && strchr(u, ' ') == NULL)
        snprintf(utente, sizeof utente, "%s", u);
    if (gethostname(host, sizeof host - 1) != 0 || host[0] == '\0')
        snprintf(host, sizeof host, "pc");
    host[sizeof host - 1] = '\0';

    char nome[256];
    snprintf(nome, sizeof nome, " %s@%s", utente, host);
    ph_buffer_svuota(uscita);
    /* il '\0' finale e' compreso nella lunghezza: adbd lo vuole nel payload */
    if (ph_buffer_aggiungi(uscita, b64, (size_t)nb) != 0 ||
        ph_buffer_aggiungi(uscita, nome, strlen(nome) + 1) != 0)
        return ph_errore(e, "memoria finita");
    return 0;
}
