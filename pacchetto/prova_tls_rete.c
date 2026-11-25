/* Prova con la RETE vera, senza inviare nulla: apre una connessione TLS
 * all'host dato con il codice vero di src/meteo/tls.c (verifica del certificato
 * contro le autorita' del sistema), poi la chiude. Nessuna richiesta HTTP, nessuna
 * coordinata. Si compila a parte nel container, contro i sorgenti dell'albero
 * da provare, e si lancia con LD_LIBRARY_PATH sulle librerie del pacchetto:
 *   gcc -I src -I src/meteo pacchetto/prova_tls_rete.c src/meteo/tls.c \
 *       src/comune/errore.c $(pkg-config --cflags --libs openssl) -lpthread
 * Uso: prova_tls_rete HOST   (porta 443). Uscita 0 = handshake verificato. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "meteo/tls.h"

int64_t ph_meteo_ms(void);

/* tls.c chiede l'orologio del pezzo meteo (src/meteo/meteo.c): qui lo stesso
 * orologio monotonico in millisecondi, per non portarsi dietro il resto del pezzo. */
int64_t ph_meteo_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "uso: %s HOST\n", argv[0]);
        return 2;
    }
    PhErrore e = {{0}};
    PhTls *t = ph_tls_apri(argv[1], 443, ph_meteo_ms() + 15000, NULL, &e);
    if (t == NULL) {
        printf("handshake NON riuscito: %s\n", e.testo);
        return 1;
    }
    printf("handshake riuscito e certificato verificato\n");
    ph_tls_chiudi(t);
    return 0;
}
