/* Nomi dei file di cattura e creazione senza sovrascrivere. */
#include "video/cattura_interno.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NOME_APP_MAX 80

void ph_cattura_testo_errno(int codice, char *uscita, size_t n)
{
    if (n == 0)
        return;
    /* Con _GNU_SOURCE strerror_r torna il puntatore al testo (che puo' non
     * essere in `uscita`). */
    const char *t = strerror_r(codice, uscita, n);
    if (t != uscita)
        snprintf(uscita, n, "%s", t);
}

int ph_cattura_scrivi_tutto(int fd, const void *dati, size_t n)
{
    const uint8_t *p = dati;
    while (n > 0) {
        ssize_t k = write(fd, p, n);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (k == 0) {
            errno = EIO; /* una write che non avanza mai non puo' essere ignorata */
            return -1;
        }
        p += (size_t)k;
        n -= (size_t)k;
    }
    return 0;
}

/* Ripulisce il nome dell'app in `out` (almeno NOME_APP_MAX + 10 byte). */
static void ripulisci(const char *app, char *out)
{
    size_t k = 0;
    if (app != NULL) {
        for (const unsigned char *p = (const unsigned char *)app; *p != 0 && k < NOME_APP_MAX; p++) {
            unsigned char c = *p;
            if (c < 0x20 || c == 0x7f || strchr("/\\:*?\"<>|", c) != NULL)
                c = '_';
            out[k++] = (char)c;
        }
        /* Taglio sul limite: non lasciare un carattere UTF-8 spezzato. */
        if (k == NOME_APP_MAX && ((unsigned char)app[k] & 0xC0) == 0x80) {
            /* Il prossimo byte e' una continuazione: il carattere e' spezzato. */
            while (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0x80)
                k--; /* continuazioni gia' copiate */
            if (k > 0)
                k--; /* il byte di testa del carattere spezzato */
        }
    }
    out[k] = 0;
    /* Spazi e punti ai bordi (un nome che inizia per '.' sarebbe nascosto). */
    size_t ini = 0;
    while (out[ini] == ' ' || out[ini] == '.')
        ini++;
    while (k > ini && (out[k - 1] == ' ' || out[k - 1] == '.'))
        k--;
    memmove(out, out + ini, k - ini);
    out[k - ini] = 0;
    if (out[0] == 0)
        strcpy(out, "YASDE");
}

int ph_cattura_nome(const char *app, const char *estensione, time_t quando, unsigned indice,
                    char *uscita, size_t n, PhErrore *e)
{
    if (estensione == NULL || estensione[0] == 0 || strchr(estensione, '/') != NULL)
        return ph_errore(e, "estensione del file non valida");
    if (uscita == NULL || n == 0)
        return ph_errore(e, "nome del file: spazio mancante");
    struct tm t;
    if (localtime_r(&quando, &t) == NULL)
        return ph_errore(e, "ora locale non rappresentabile");
    char pulito[NOME_APP_MAX + 10];
    ripulisci(app, pulito);
    char suffisso[24] = "";
    if (indice >= 2)
        snprintf(suffisso, sizeof suffisso, " (%u)", indice);
    int k = snprintf(uscita, n, "%s %04d-%02d-%02d %02d.%02d.%02d%s.%s", pulito, t.tm_year + 1900,
                     t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, suffisso, estensione);
    if (k < 0 || (size_t)k >= n) {
        uscita[0] = 0;
        return ph_errore(e, "nome del file troppo lungo");
    }
    return 0;
}

int ph_cattura_apri_nuovo(const char *cartella, const char *app, const char *estensione,
                          time_t quando, char *percorso, size_t n_percorso, PhErrore *e)
{
    if (cartella == NULL || cartella[0] == 0)
        return ph_errore(e, "cartella di destinazione mancante");
    if (mkdir(cartella, 0755) != 0 && errno != EEXIST) {
        char t[128];
        ph_cattura_testo_errno(errno, t, sizeof t);
        return ph_errore(e, "cartella '%s' non creabile: %s", cartella, t);
    }
    char nome[400];
    for (unsigned i = 1; i <= 999; i++) {
        if (ph_cattura_nome(app, estensione, quando, i, nome, sizeof nome, e) != 0)
            return -1;
        char completo[4096];
        int k = snprintf(completo, sizeof completo, "%s/%s", cartella, nome);
        if (k < 0 || (size_t)k >= sizeof completo || (percorso != NULL && (size_t)k >= n_percorso))
            return ph_errore(e, "percorso del file troppo lungo");
        int fd = open(completo, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
        if (fd >= 0) {
            if (percorso != NULL)
                memcpy(percorso, completo, (size_t)k + 1);
            return fd;
        }
        if (errno == EEXIST)
            continue;
        char t[128];
        ph_cattura_testo_errno(errno, t, sizeof t);
        return ph_errore(e, "file '%s' non creabile: %s", completo, t);
    }
    return ph_errore(e, "troppi file con lo stesso nome in '%s'", cartella);
}
