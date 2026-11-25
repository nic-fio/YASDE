/* Percorsi di configurazione e cache, lettura e scrittura sicura dei file. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "config/config.h"

#define CARTELLA_APP "YASDE"

/* Solo ASCII, mai isalnum: dipende dalla lingua del processo. */
static bool alnum_ascii(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* FNV-1a a 32 bit (scritto qui, pubblico dominio come algoritmo). */
static uint32_t impronta(const char *s)
{
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
        h ^= *p;
        h *= 16777619u;
    }
    return h;
}

/* Cartella di base (XDG o ~/<predefinito>), senza "/YASDE". */
static int base_xdg(const char *variabile, const char *predefinito, char *out, size_t cap, PhErrore *e)
{
    const char *x = getenv(variabile);
    int n;
    if (x != NULL && x[0] == '/') {
        n = snprintf(out, cap, "%s", x);
    } else {
        const char *home = getenv("HOME");
        char buf[PATH_MAX];
        struct passwd pw, *res = NULL;
        if (home == NULL || home[0] != '/') {
            if (getpwuid_r(getuid(), &pw, buf, sizeof buf, &res) != 0 || res == NULL ||
                pw.pw_dir == NULL || pw.pw_dir[0] != '/')
                return ph_errore(e, "impossibile trovare la cartella personale dell'utente");
            home = pw.pw_dir;
        }
        n = snprintf(out, cap, "%s/%s", home, predefinito);
    }
    if (n < 0 || (size_t)n >= cap)
        return ph_errore(e, "percorso della cartella di configurazione troppo lungo");
    /* niente '/' finale doppio */
    while (n > 1 && out[n - 1] == '/')
        out[--n] = '\0';
    return 0;
}

static int cartella_app(const char *variabile, const char *predefinito, char *out, size_t cap, PhErrore *e)
{
    if (base_xdg(variabile, predefinito, out, cap, e) < 0)
        return -1;
    size_t n = strlen(out);
    int k = snprintf(out + n, cap - n, "%s%s", n == 1 ? "" : "/", CARTELLA_APP);
    if (k < 0 || (size_t)k >= cap - n)
        return ph_errore(e, "percorso della cartella di configurazione troppo lungo");
    return 0;
}

int ph_cfg_cartella_config(char *out, size_t cap, PhErrore *e)
{
    return cartella_app("XDG_CONFIG_HOME", ".config", out, cap, e);
}

int ph_cfg_cartella_cache(char *out, size_t cap, PhErrore *e)
{
    return cartella_app("XDG_CACHE_HOME", ".cache", out, cap, e);
}

int ph_cfg_seriale_pulito(const char *seriale, char *out, size_t cap, PhErrore *e)
{
    if (seriale == NULL || seriale[0] == '\0')
        return ph_errore(e, "numero di serie vuoto");
    if (cap < PH_CFG_LUN_SERIALE_FILE)
        return ph_errore(e, "buffer troppo piccolo per il numero di serie");
    size_t n = 0;
    bool cambiato = false;
    for (const unsigned char *p = (const unsigned char *)seriale; *p != '\0'; p++) {
        if (n >= PH_CFG_LUN_SERIALE - 1) { /* stesso limite del campo nelle strutture */
            cambiato = true;
            break;
        }
        bool ok = alnum_ascii(*p) || *p == '_' || *p == '-';
        if (!ok)
            cambiato = true;
        out[n++] = ok ? (char)*p : '_';
    }
    out[n] = '\0';
    /* Se la pulizia ha cambiato o troncato qualcosa, due seriali diversi
     * potrebbero dare lo stesso nome: si aggiunge l'impronta dell'originale. */
    if (cambiato)
        snprintf(out + n, cap - n, "-%08x", (unsigned)impronta(seriale));
    return 0;
}

static int in_cartella(int (*cartella)(char *, size_t, PhErrore *), const char *file, char *out, size_t cap,
                       PhErrore *e)
{
    if (cartella(out, cap, e) < 0)
        return -1;
    size_t n = strlen(out);
    int k = snprintf(out + n, cap - n, "/%s", file);
    if (k < 0 || (size_t)k >= cap - n)
        return ph_errore(e, "percorso troppo lungo");
    return 0;
}

int ph_cfg_percorso_preferenze(char *out, size_t cap, PhErrore *e)
{
    return in_cartella(ph_cfg_cartella_config, "preferenze.toml", out, cap, e);
}

int ph_cfg_percorso_telefoni(char *out, size_t cap, PhErrore *e)
{
    return in_cartella(ph_cfg_cartella_config, "telefoni.toml", out, cap, e);
}

int ph_cfg_percorso_chiave(char *out, size_t cap, PhErrore *e)
{
    return in_cartella(ph_cfg_cartella_config, "chiave.pem", out, cap, e);
}

int ph_cfg_percorso_telefono(const char *seriale, char *out, size_t cap, PhErrore *e)
{
    char pulito[PH_CFG_LUN_SERIALE_FILE];
    if (ph_cfg_seriale_pulito(seriale, pulito, sizeof pulito, e) < 0)
        return -1;
    char nome[PH_CFG_LUN_SERIALE_FILE + 32];
    snprintf(nome, sizeof nome, "telefono-%s.toml", pulito);
    return in_cartella(ph_cfg_cartella_config, nome, out, cap, e);
}

bool ph_cfg_nome_cache_valido(const char *nome)
{
    if (nome == NULL || nome[0] == '\0' || nome[0] == '.')
        return false;
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)nome; *p != '\0'; p++, n++) {
        bool ok = alnum_ascii(*p) || *p == '_' || *p == '-' || *p == '.';
        if (!ok)
            return false;
    }
    return n < PH_CFG_LUN_FILE_CACHE;
}

int ph_cfg_percorso_cache(const char *nome, char *out, size_t cap, PhErrore *e)
{
    if (!ph_cfg_nome_cache_valido(nome))
        return ph_errore(e, "nome di file nella cache non valido");
    return in_cartella(ph_cfg_cartella_cache, nome, out, cap, e);
}

/* ---------- file ---------- */

/* mkdir -p con permessi 0700; i componenti esistenti non si toccano. */
static int crea_cartelle(const char *dir, PhErrore *e)
{
    char tmp[PATH_MAX];
    size_t n = strlen(dir);
    if (n == 0 || n >= sizeof tmp)
        return ph_errore(e, "percorso della cartella non valido o troppo lungo");
    memcpy(tmp, dir, n + 1);
    for (size_t i = 1; i <= n; i++) {
        if (tmp[i] != '/' && tmp[i] != '\0')
            continue;
        char c = tmp[i];
        tmp[i] = '\0';
        if (mkdir(tmp, 0700) < 0 && errno != EEXIST)
            return ph_errore(e, "impossibile creare la cartella %s: %s", tmp, strerror(errno));
        tmp[i] = c;
    }
    struct stat st;
    if (stat(dir, &st) < 0 || !S_ISDIR(st.st_mode))
        return ph_errore(e, "%s non e' una cartella", dir);
    return 0;
}

static int scrivi_tutto(int fd, const char *p, size_t n)
{
    while (n > 0) {
        ssize_t k = write(fd, p, n);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (k == 0) {
            errno = EIO;
            return -1;
        }
        p += k;
        n -= (size_t)k;
    }
    return 0;
}

int ph_cfg_scrivi_file(const char *percorso, const void *dati, size_t lunghezza, PhErrore *e)
{
    char dir[PATH_MAX], tmp[PATH_MAX + 32];
    size_t n = strlen(percorso);
    const char *barra = strrchr(percorso, '/');
    if (n == 0 || n >= PATH_MAX || barra == NULL || barra[1] == '\0')
        return ph_errore(e, "percorso del file non valido");
    size_t ldir = barra == percorso ? 1 : (size_t)(barra - percorso);
    memcpy(dir, percorso, ldir);
    dir[ldir] = '\0';
    if (crea_cartelle(dir, e) < 0)
        return -1;
    /* La cartella dove scriviamo deve essere dell'utente: altrimenti qualcuno
     * potrebbe sostituire i nostri file. Se e' sua ma scrivibile da gruppo o
     * altri (per esempio con umask 0002) si correggono i
     * permessi invece di rifiutarla. */
    /* Un solo descrittore per controllo e modifica: nessuno puo' sostituire la
     * cartella tra i due passi. I collegamenti simbolici legittimi si seguono. */
    int cfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (cfd < 0)
        return ph_errore(e, "impossibile aprire la cartella %s: %s", dir, strerror(errno));
    struct stat sd;
    if (fstat(cfd, &sd) < 0 || sd.st_uid != geteuid()) {
        close(cfd);
        return ph_errore(e, "la cartella %s non e' dell'utente: la rifiuto", dir);
    }
    if ((sd.st_mode & 022) != 0 && fchmod(cfd, sd.st_mode & 07777 & ~(mode_t)022) < 0) {
        int err = errno;
        close(cfd);
        return ph_errore(e, "impossibile togliere la scrittura ad altri sulla cartella %s: %s", dir, strerror(err));
    }
    close(cfd);

    int k = snprintf(tmp, sizeof tmp, "%s%s.%s.XXXXXX", dir, ldir == 1 && dir[0] == '/' ? "" : "/", barra + 1);
    if (k < 0 || (size_t)k >= sizeof tmp)
        return ph_errore(e, "percorso del file troppo lungo");
    int fd = mkostemp(tmp, O_CLOEXEC); /* crea con 0600 e in modo esclusivo */
    if (fd < 0)
        return ph_errore(e, "impossibile creare il file temporaneo in %s: %s", dir, strerror(errno));
    int err = 0;
    if (fchmod(fd, 0600) < 0 || scrivi_tutto(fd, dati, lunghezza) < 0 || fsync(fd) < 0)
        err = errno;
    if (close(fd) < 0 && err == 0)
        err = errno;
    if (err == 0 && rename(tmp, percorso) < 0)
        err = errno;
    if (err != 0) {
        unlink(tmp);
        return ph_errore(e, "impossibile scrivere %s: %s", percorso, strerror(err));
    }
    /* Rende durevole anche il rename. Se fallisce il file e' comunque al suo
     * posto: non e' un errore da segnalare. */
    int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        (void)fsync(dfd);
        close(dfd);
    }
    return 0;
}

int ph_cfg_leggi_file(const char *percorso, size_t massimo, PhBuffer *out, PhErrore *e)
{
    ph_buffer_svuota(out);
    /* O_NONBLOCK: un FIFO al posto del file non deve bloccare il programma. */
    int fd = open(percorso, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT)
            return 1;
        return ph_errore(e, "impossibile aprire %s: %s", percorso, strerror(errno));
    }
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return ph_errore(e, "%s non e' un file normale", percorso);
    }
    char buf[8192];
    for (;;) {
        ssize_t k = read(fd, buf, sizeof buf);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            int err = errno;
            close(fd);
            ph_buffer_svuota(out);
            return ph_errore(e, "impossibile leggere %s: %s", percorso, strerror(err));
        }
        if (k == 0)
            break;
        if (out->lunghezza + (size_t)k > massimo) {
            close(fd);
            ph_buffer_svuota(out);
            ph_errore(e, "%s e' troppo grande (oltre %zu byte)", percorso, massimo);
            return 2;
        }
        if (ph_buffer_aggiungi(out, buf, (size_t)k) < 0) {
            close(fd);
            ph_buffer_svuota(out);
            return ph_errore(e, "memoria esaurita leggendo %s", percorso);
        }
    }
    close(fd);
    return 0;
}

/* ---------- temporanei abbandonati ---------- */

/* ".<nome>.XXXXXX": inizia con '.', finisce con un punto e sei caratteri
 * alfanumerici, e nel mezzo c'e' un nome con estensione. */
static bool nome_temporaneo(const char *nome)
{
    size_t n = strlen(nome);
    if (n < 10 || nome[0] != '.' || nome[n - 7] != '.')
        return false;
    for (size_t i = n - 6; i < n; i++)
        if (!alnum_ascii((unsigned char)nome[i]))
            return false;
    for (size_t i = 1; i < n - 7; i++)
        if (nome[i] == '.')
            return true;
    return false;
}

static int pulisci_cartella(const char *dir, long secondi, time_t ora)
{
    DIR *d = opendir(dir);
    if (d == NULL)
        return 0;
    int tolti = 0;
    struct dirent *v;
    while ((v = readdir(d)) != NULL) {
        if (!nome_temporaneo(v->d_name))
            continue;
        char p[PATH_MAX];
        struct stat st;
        if (snprintf(p, sizeof p, "%s/%s", dir, v->d_name) >= (int)sizeof p || lstat(p, &st) < 0)
            continue;
        if (S_ISREG(st.st_mode) && st.st_uid == geteuid() && ora - st.st_mtime > secondi && unlink(p) == 0)
            tolti++;
    }
    closedir(d);
    return tolti;
}

int ph_cfg_pulisci_temporanei(long secondi, PhErrore *e)
{
    char dir[PATH_MAX];
    if (secondi < 60)
        secondi = 60; /* mai toccare un temporaneo di una scrittura in corso */
    time_t ora = time(NULL);
    int tolti = 0;
    if (ph_cfg_cartella_config(dir, sizeof dir, e) < 0)
        return -1;
    tolti += pulisci_cartella(dir, secondi, ora);
    if (ph_cfg_cartella_cache(dir, sizeof dir, e) < 0)
        return -1;
    tolti += pulisci_cartella(dir, secondi, ora);
    return tolti;
}
