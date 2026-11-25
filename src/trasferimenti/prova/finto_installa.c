/* Vedi finto_installa.h. */
#include "finto_installa.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comune/buffer.h"

struct PhAdb {
    int dummy;
};

#define MAXV 64
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static FintoInst cfg;
static char *comandi[MAXV];
static size_t n_comandi;
static char *copie[MAXV];
static size_t n_copie;
static char *remoti[MAXV]; /* file presenti sul telefono (NULL = tolto) */
static size_t n_remoti;
static bool caduta;

void finto_inst_reset(void)
{
    pthread_mutex_lock(&m);
    for (size_t i = 0; i < n_comandi; i++)
        free(comandi[i]);
    for (size_t i = 0; i < n_copie; i++)
        free(copie[i]);
    for (size_t i = 0; i < n_remoti; i++)
        free(remoti[i]);
    n_comandi = n_copie = n_remoti = 0;
    caduta = false;
    memset(&cfg, 0, sizeof cfg);
    cfg.copia_esito = PH_FILE_RIUSCITO;
    cfg.pm_uscita = "Success\n";
    pthread_mutex_unlock(&m);
}

void finto_inst_config(const FintoInst *c)
{
    pthread_mutex_lock(&m);
    cfg = *c;
    pthread_mutex_unlock(&m);
}

PhAdb *finto_inst_adb(void) { return calloc(1, sizeof(PhAdb)); }
void finto_inst_adb_libera(PhAdb *a) { free(a); }

size_t finto_inst_comandi(void)
{
    pthread_mutex_lock(&m);
    size_t n = n_comandi;
    pthread_mutex_unlock(&m);
    return n;
}

static char *copia_di(char **v, size_t n, size_t i)
{
    char *r = NULL;
    pthread_mutex_lock(&m);
    if (i < n && v[i])
        r = strdup(v[i]);
    pthread_mutex_unlock(&m);
    return r;
}

char *finto_inst_comando(size_t i) { return copia_di(comandi, n_comandi, i); }
size_t finto_inst_copie(void)
{
    pthread_mutex_lock(&m);
    size_t n = n_copie;
    pthread_mutex_unlock(&m);
    return n;
}
char *finto_inst_copia_remoto(size_t i) { return copia_di(copie, n_copie, i); }

int finto_inst_remoto_esiste(const char *p)
{
    int r = 0;
    pthread_mutex_lock(&m);
    for (size_t i = 0; i < n_remoti; i++)
        if (remoti[i] && strcmp(remoti[i], p) == 0)
            r = 1;
    pthread_mutex_unlock(&m);
    return r;
}

size_t finto_inst_file_sul_telefono(void)
{
    size_t n = 0;
    pthread_mutex_lock(&m);
    for (size_t i = 0; i < n_remoti; i++)
        if (remoti[i])
            n++;
    pthread_mutex_unlock(&m);
    return n;
}

PhFileEsito ph_file_invia(PhAdb *a, const char *locale, const char *remoto, PhFileProgresso progresso,
                          void *utente, const atomic_bool *annulla, PhErrore *e)
{
    (void)a;
    (void)locale;
    pthread_mutex_lock(&m);
    FintoInst c = cfg;
    if (n_copie < MAXV)
        copie[n_copie++] = strdup(remoto);
    /* il file "esiste" subito, anche se la copia fallisce a meta': e' il caso
     * peggiore che la pulizia deve coprire */
    if (n_remoti < MAXV)
        remoti[n_remoti++] = strdup(remoto);
    pthread_mutex_unlock(&m);
    for (int blocco = 1; blocco <= 3; blocco++) {
        if (progresso)
            progresso((uint64_t)blocco * 100, 300, utente);
        if (annulla && atomic_load(annulla) && c.annulla_al_blocco == blocco)
            return PH_FILE_ANNULLATO;
    }
    if (c.copia_esito != PH_FILE_RIUSCITO) {
        ph_errore(e, "finto: copia non riuscita");
        if (c.connessione_caduta) {
            pthread_mutex_lock(&m);
            caduta = true;
            pthread_mutex_unlock(&m);
        }
        return c.copia_esito;
    }
    if (c.connessione_caduta) {
        pthread_mutex_lock(&m);
        caduta = true;
        pthread_mutex_unlock(&m);
    }
    return PH_FILE_RIUSCITO;
}

int ph_shell_esegui(PhAdb *a, const char *comando, PhBuffer *uscita, PhBuffer *errori, int *codice,
                    int timeout_ms, PhErrore *e)
{
    (void)a;
    (void)timeout_ms;
    if (codice)
        *codice = 0;
    pthread_mutex_lock(&m);
    FintoInst c = cfg;
    bool giu = caduta;
    if (n_comandi < MAXV)
        comandi[n_comandi++] = strdup(comando);
    int ris = 0;
    if (giu) {
        ris = -1;
    } else if (strncmp(comando, "rm -f '", 7) == 0) {
        size_t l = strlen(comando);
        if (c.rm_codice == 0 && l > 8 && comando[l - 1] == '\'') {
            for (size_t i = 0; i < n_remoti; i++)
                if (remoti[i] && strlen(remoti[i]) == l - 8 && memcmp(remoti[i], comando + 7, l - 8) == 0) {
                    free(remoti[i]);
                    remoti[i] = NULL;
                }
        }
        if (codice)
            *codice = c.rm_codice;
    } else if (strcmp(comando, "rm -f /data/local/tmp/yasde-apk-*.apk") == 0) {
        if (c.rm_codice == 0)
            for (size_t i = 0; i < n_remoti; i++)
                if (remoti[i] &&
                    strncmp(remoti[i], "/data/local/tmp/yasde-apk-", 26) == 0 &&
                    strlen(remoti[i]) > 4 && strcmp(remoti[i] + strlen(remoti[i]) - 4, ".apk") == 0) {
                    free(remoti[i]);
                    remoti[i] = NULL;
                }
        if (codice)
            *codice = c.rm_codice;
    } else if (strncmp(comando, "pm ", 3) == 0) {
        if (c.pm_scade) {
            ris = -1;
        } else {
            if (uscita && c.pm_uscita)
                ph_buffer_aggiungi(uscita, c.pm_uscita, strlen(c.pm_uscita));
            if (errori && c.pm_errori)
                ph_buffer_aggiungi(errori, c.pm_errori, strlen(c.pm_errori));
        }
    }
    pthread_mutex_unlock(&m);
    if (ris != 0)
        return ph_errore(e, "finto: nessuna risposta dal telefono");
    return 0;
}
