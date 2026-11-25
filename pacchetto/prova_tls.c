/* Prova senza rete: con le librerie OpenSSL del pacchetto, la ricerca delle
 * autorita' di certificazione del sistema (la stessa sequenza di
 * src/meteo/tls.c: percorsi predefiniti, poi i file noti delle distribuzioni)
 * trova certificati. Stampa quale libcrypto e' stata caricata. */
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int main(void)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == NULL) {
        fprintf(stderr, "SSL_CTX_new fallita\n");
        return 1;
    }
    (void)SSL_CTX_set_default_verify_paths(ctx);
    static const char *const noti[] = {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
                                       "/etc/ssl/ca-bundle.pem", "/etc/ssl/cert.pem"};
    for (size_t i = 0; i < sizeof noti / sizeof noti[0]; i++) {
        struct stat st;
        if (stat(noti[i], &st) == 0 && S_ISREG(st.st_mode)) {
            (void)SSL_CTX_load_verify_locations(ctx, noti[i], NULL);
            break;
        }
    }
    ERR_clear_error();
    int n = sk_X509_OBJECT_num(X509_STORE_get0_objects(SSL_CTX_get_cert_store(ctx)));
    printf("autorita' caricate: %d\n", n);
    FILE *f = fopen("/proc/self/maps", "r");
    char riga[1024];
    while (f != NULL && fgets(riga, sizeof riga, f) != NULL) {
        char *p = strstr(riga, "libcrypto.so");
        if (p != NULL) {
            printf("libcrypto: %s", strchr(riga, '/') != NULL ? strchr(riga, '/') : riga);
            break;
        }
    }
    if (f != NULL)
        fclose(f);
    SSL_CTX_free(ctx);
    return n > 0 ? 0 : 1;
}
