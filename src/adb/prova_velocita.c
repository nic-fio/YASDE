/* Misura (non verifica) la velocita' di un canale contro il finto telefono su
 * localhost: 100 MiB in scrittura (verso il telefono) e in lettura. Il
 * finto telefono dichiara pacchetti da 1 MiB come un telefono vero. Con i
 * controlli di memoria attivi i numeri sono un limite inferiore. */
#include "adb/adb.h"
#include "finto_telefono.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TOT (100L * 1024 * 1024)

static double ora_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void misura(PhChiave *k, bool delayed)
{
    PhErrore e;
    uint8_t *blocco = calloc(1, 1024 * 1024);
    assert(blocco != NULL);
    uint16_t porta;
    FintoTelefono *ft = ft_avvia_cfg(k, FT_NORMALE, 1024 * 1024, delayed, &porta);
    assert(ft != NULL);
    PhAdb *a = ph_adb_collega("127.0.0.1", porta, k, 10000, NULL, &e);
    assert(a != NULL);

    /* verso il telefono */
    PhCanale *c = ph_canale_apri(a, "sink:", 2000, &e);
    assert(c != NULL);
    double t0 = ora_s();
    for (long n = 0; n < TOT; n += 1024 * 1024)
        assert(ph_canale_scrivi(c, blocco, 1024 * 1024, &e) == 0);
    for (int i = 0; i < 6000 && ft_byte_sink(ft) < TOT; i++)
        usleep(1000);
    assert(ft_byte_sink(ft) == TOT);
    double dt = ora_s() - t0;
    printf("%s scrittura: %.1f MiB/s\n", delayed ? "delayed_ack" : "senza delayed_ack", 100.0 / dt);
    ph_canale_chiudi(c);

    /* dal telefono */
    char serv[32];
    snprintf(serv, sizeof serv, "sorgente:%ld", TOT);
    t0 = ora_s();
    c = ph_canale_apri(a, serv, 2000, &e);
    assert(c != NULL);
    for (long n = 0; n < TOT; n += 1024 * 1024)
        assert(ph_canale_leggi_tutto(c, blocco, 1024 * 1024, 20000, &e) == 0);
    dt = ora_s() - t0;
    printf("%s lettura: %.1f MiB/s\n", delayed ? "delayed_ack" : "senza delayed_ack", 100.0 / dt);
    ph_canale_chiudi(c);

    free(blocco);
    ph_adb_chiudi(a);
    ft_ferma(ft);
}

int main(void)
{
    char dir[] = "prova-velocita-XXXXXX";
    assert(mkdtemp(dir) != NULL);
    char file[64];
    snprintf(file, sizeof file, "%s/chiave", dir);
    PhErrore e;
    PhChiave *k = ph_chiave_carica_o_crea(file, &e);
    assert(k != NULL);
    misura(k, false);
    misura(k, true);
    ph_chiave_libera(k);
    unlink(file);
    rmdir(dir);
    return 0;
}
