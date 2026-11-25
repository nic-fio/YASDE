/* Chiave RSA: generazione, ricarica, permessi, certificato e formato ADB della chiave pubblica. */
#include "adb/adb.h"

#include <assert.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t le32(const uint8_t *d)
{
    return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
}

int main(void)
{
    char dir[] = "prova-chiave-XXXXXX";
    assert(mkdtemp(dir) != NULL);
    char file[128];
    snprintf(file, sizeof file, "%s/sotto/cartella/chiave", dir);

    PhErrore e;
    PhChiave *k = ph_chiave_carica_o_crea(file, &e); /* la crea, anche le cartelle */
    assert(k != NULL);
    struct stat st;
    assert(stat(file, &st) == 0);
    assert((st.st_mode & 0777) == 0600);

    /* il file e' PKCS#8 */
    FILE *f = fopen(file, "r");
    assert(f != NULL);
    char riga[64];
    assert(fgets(riga, sizeof riga, f) != NULL);
    fclose(f);
    assert(strncmp(riga, "-----BEGIN PRIVATE KEY-----", 27) == 0);

    /* ricarica: stessa chiave */
    PhChiave *k2 = ph_chiave_carica_o_crea(file, &e);
    assert(k2 != NULL);
    assert(EVP_PKEY_eq(ph_chiave_evp(k), ph_chiave_evp(k2)) == 1);

    /* certificato: autofirmato, con la nostra chiave pubblica */
    X509 *x = ph_chiave_x509(k);
    assert(x != NULL);
    assert(X509_verify(x, ph_chiave_evp(k)) == 1);
    EVP_PKEY *pubx = X509_get0_pubkey(x);
    assert(EVP_PKEY_eq(pubx, ph_chiave_evp(k)) == 1);

    /* file che non e' una chiave: errore, non crash */
    char finto[128];
    snprintf(finto, sizeof finto, "%s/rotto", dir);
    f = fopen(finto, "w");
    assert(f != NULL);
    fputs("non sono una chiave\n", f);
    fclose(f);
    assert(ph_chiave_carica_o_crea(finto, &e) == NULL);
    assert(e.testo[0] != '\0');

    /* chiave pubblica ADB */
    PhBuffer b = PH_BUFFER_VUOTO;
    assert(ph_chiave_pubblica_adb(k, &b, &e) == 0);
    assert(b.lunghezza > 0 && b.dati[b.lunghezza - 1] == '\0');
    char *spazio = strchr((char *)b.dati, ' ');
    assert(spazio != NULL && strchr(spazio + 1, '@') != NULL);
    size_t nb64 = (size_t)(spazio - (char *)b.dati);
    assert(nb64 == 700); /* 524 byte -> 175 gruppi da 4, un '=' di padding */

    /* EVP_DecodeBlock conta anche il byte del '=' di padding: 525 invece di 524 */
    uint8_t blob[525];
    assert(EVP_DecodeBlock(blob, b.dati, (int)nb64) == 525);
    assert(b.dati[nb64 - 1] == '=' && b.dati[nb64 - 2] != '=');

    assert(le32(blob) == 64);                /* parole da 32 bit del modulo */
    assert(le32(blob + 8 + 512) == 65537);   /* esponente */

    BIGNUM *n = NULL;
    assert(EVP_PKEY_get_bn_param(ph_chiave_evp(k), OSSL_PKEY_PARAM_RSA_N, &n) == 1);
    uint8_t modulo_le[256];
    assert(BN_bn2lebinpad(n, modulo_le, 256) == 256);
    assert(memcmp(blob + 8, modulo_le, 256) == 0); /* modulo little endian */

    /* n0inv * n[0] == -1 mod 2^32 */
    assert((uint32_t)(le32(blob + 4) * le32(blob + 8)) == 0xffffffffu);

    /* rr = (2^2048)^2 mod n, ricalcolato in altro modo: (2^2048 mod n)^2 mod n */
    BN_CTX *ctx = BN_CTX_new();
    BIGNUM *r = BN_new(), *r2 = BN_new(), *rr = BN_new();
    assert(ctx && r && r2 && rr);
    BN_set_bit(r, 2048);
    assert(BN_mod(r, r, n, ctx) == 1);
    assert(BN_mod_mul(r2, r, r, n, ctx) == 1);
    assert(BN_lebin2bn(blob + 8 + 256, 256, rr) != NULL);
    assert(BN_cmp(rr, r2) == 0);

    BN_free(r);
    BN_free(r2);
    BN_free(rr);
    BN_free(n);
    BN_CTX_free(ctx);
    ph_buffer_libera(&b);
    ph_chiave_libera(k);
    ph_chiave_libera(k2);

    unlink(file);
    unlink(finto);
    char p1[128], p2[128];
    snprintf(p1, sizeof p1, "%s/sotto/cartella", dir);
    snprintf(p2, sizeof p2, "%s/sotto", dir);
    rmdir(p1);
    rmdir(p2);
    rmdir(dir);
    return 0;
}
