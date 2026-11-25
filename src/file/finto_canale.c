/* Telefono finto: vedi finto_canale.h. Il protocollo sync e' implementato
 * da questo lato a modo suo, per non condividere errori con src/file/file.c. */
#include "finto_canale.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "comune/errore.h"

struct PhCanale {
    PhAdb *a;
    bool shell;
    uint8_t *in;
    size_t in_n, in_cap;
    uint8_t *out;
    size_t out_pos, out_n, out_cap;
    size_t scritti;    /* byte scritti dal client */
    size_t prodotti;   /* byte che il server avrebbe prodotto, prima del troncamento */
    bool bloccato;     /* superato tronca_a */
    bool rotto;        /* richiesta incomprensibile o errore di scrittura simulato */
    bool chiudi_a_fine; /* canale shell: chiuso dopo l'uscita */
    /* SEND in corso */
    bool invio;
    int fd;
    char motivo_fail[128];
    char percorso_invio[4200];
};

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- Radice --------------------------------------------------------------- */

PhAdb *finto_nuovo(void)
{
    PhAdb *a = calloc(1, sizeof *a);
    if (!a)
        return NULL;
    const char *base = getenv("TMPDIR");
    snprintf(a->radice, sizeof a->radice, "%s/phfile_telefono_XXXXXX", base ? base : ".");
    if (!mkdtemp(a->radice)) {
        free(a);
        return NULL;
    }
    finto_opzioni_normali(a);
    return a;
}

void finto_opzioni_normali(PhAdb *a)
{
    memset(&a->o, 0, sizeof a->o);
    a->o.tronca_a = SIZE_MAX;
    a->o.corrompi = -1;
    a->o.scrivi_errore_dopo = SIZE_MAX;
}

static int cancella_voce(const char *p, const struct stat *sb, int tipo, struct FTW *f)
{
    (void)sb;
    (void)tipo;
    (void)f;
    return remove(p);
}

void finto_libera(PhAdb *a)
{
    if (!a)
        return;
    nftw(a->radice, cancella_voce, 16, FTW_DEPTH | FTW_PHYS);
    free(a);
}

void finto_percorso(const PhAdb *a, const char *telefono, char *uscita, size_t n)
{
    snprintf(uscita, n, "%s%s", a->radice, telefono);
}

/* ---- Uscita del server ---------------------------------------------------- */

static void emetti(PhCanale *c, const void *dati, size_t n)
{
    const uint8_t *d = dati;
    for (size_t i = 0; i < n; i++, c->prodotti++) {
        if (c->prodotti >= c->a->o.tronca_a) {
            c->bloccato = true;
            continue;
        }
        if (c->out_n == c->out_cap) {
            size_t nc = c->out_cap ? c->out_cap * 2 : 4096;
            uint8_t *o = realloc(c->out, nc);
            if (!o)
                abort(); /* solo nei test */
            c->out = o;
            c->out_cap = nc;
        }
        uint8_t b = d[i];
        if (c->a->o.corrompi >= 0 && (size_t)c->a->o.corrompi == c->prodotti)
            b ^= 0xFF;
        c->out[c->out_n++] = b;
    }
}

static void emetti_fail(PhCanale *c, const char *msg)
{
    uint8_t h[8] = {'F', 'A', 'I', 'L'};
    put32(h + 4, (uint32_t)strlen(msg));
    emetti(c, h, 8);
    emetti(c, msg, strlen(msg));
}

/* STA2 / DNT2: i campi comuni (errore, dev, ino, modo, nlink, uid, gid,
 * dimensione, tre date) sono 68 byte. */
static void campi(uint8_t *d, uint32_t err, const struct stat *sb, const PhAdb *a)
{
    memset(d, 0, 68);
    put32(d, err);
    if (err)
        return;
    put32(d + 20, sb->st_mode);
    uint64_t dim = (uint64_t)sb->st_size;
    if (S_ISREG(sb->st_mode) && a->o.dimensione_finta)
        dim = a->o.dimensione_finta;
    put64(d + 36, dim);
    put64(d + 44, (uint64_t)sb->st_mtime);
    put64(d + 52, (uint64_t)sb->st_mtime);
    put64(d + 60, (uint64_t)sb->st_mtime);
}

/* "/negato" e' una cartella protetta come /data: STA2 sulla cartella riesce,
 * ma apertura (LIS2, SEND, RECV) e tutto cio' che sta dentro e' negato. */
static bool negato(const char *p)
{
    return strstr(p, "/negato") != NULL;
}
static bool negato_stat(const char *p)
{
    return strstr(p, "/negato/") != NULL;
}

/* ---- Richieste ------------------------------------------------------------ */

static void fai_stat(PhCanale *c, const char id[4], bool segui, const char *p)
{
    uint8_t r[72];
    memcpy(r, id, 4);
    char reale[5400];
    finto_percorso(c->a, p, reale, sizeof reale);
    struct stat sb = {0};
    int rc = negato_stat(p) ? (errno = EACCES, -1) : (segui ? stat(reale, &sb) : lstat(reale, &sb));
    campi(r + 4, rc ? (uint32_t)errno : 0, &sb, c->a);
    emetti(c, r, sizeof r);
}

static void fai_lista(PhCanale *c, const char *p)
{
    char reale[5400];
    finto_percorso(c->a, p, reale, sizeof reale);
    DIR *d = negato(p) ? NULL : opendir(reale); /* negata: solo DONE, senza nemmeno . e .. */
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            char voce[5800];
            snprintf(voce, sizeof voce, "%s/%s", reale, de->d_name);
            struct stat sb = {0};
            int rc = lstat(voce, &sb);
            uint8_t h[76];
            memcpy(h, "DNT2", 4);
            campi(h + 4, rc ? (uint32_t)errno : 0, &sb, c->a);
            put32(h + 72, (uint32_t)strlen(de->d_name));
            emetti(c, h, sizeof h);
            emetti(c, de->d_name, strlen(de->d_name));
        }
        closedir(d);
    }
    uint8_t fine[76] = {'D', 'O', 'N', 'E'};
    emetti(c, fine, sizeof fine);
}

static void fai_ricevi(PhCanale *c, const char *p)
{
    char reale[5400];
    finto_percorso(c->a, p, reale, sizeof reale);
    int fd = negato(p) ? (errno = EACCES, -1) : open(reale, O_RDONLY);
    if (fd < 0) {
        char m[200];
        snprintf(m, sizeof m, "open failed: %s", strerror(errno));
        emetti_fail(c, m);
        return;
    }
    size_t blocco = c->a->o.blocco_ricevi ? c->a->o.blocco_ricevi : 65536;
    uint8_t *b = malloc(8 + blocco);
    if (!b)
        abort();
    if (c->a->o.lunghezza_assurda) {
        uint8_t h[8] = {'D', 'A', 'T', 'A'};
        put32(h + 4, c->a->o.lunghezza_assurda);
        emetti(c, h, 8);
        free(b);
        close(fd);
        return;
    }
    int n_blocchi = 0;
    ssize_t r;
    while ((r = read(fd, b + 8, blocco)) > 0) {
        memcpy(b, "DATA", 4);
        put32(b + 4, (uint32_t)r);
        emetti(c, b, 8 + (size_t)r);
        if (c->a->o.fail_ricevi_a_meta && ++n_blocchi == 1) {
            emetti_fail(c, "I/O error");
            free(b);
            close(fd);
            return;
        }
    }
    uint8_t fine[8] = {'D', 'O', 'N', 'E'};
    emetti(c, fine, sizeof fine);
    free(b);
    close(fd);
}

static void inizia_invio(PhCanale *c, const char *req)
{
    const char *virgola = strrchr(req, ',');
    c->invio = true;
    c->fd = -1;
    c->motivo_fail[0] = '\0';
    if (!virgola) {
        snprintf(c->motivo_fail, sizeof c->motivo_fail, "bad SEND request");
        return;
    }
    snprintf(c->percorso_invio, sizeof c->percorso_invio, "%.*s", (int)(virgola - req), req);
    if (negato(c->percorso_invio)) {
        snprintf(c->motivo_fail, sizeof c->motivo_fail, "open failed: Permission denied");
        return;
    }
    char reale[5400];
    finto_percorso(c->a, c->percorso_invio, reale, sizeof reale);
    /* Come adbd: le cartelle mancanti si creano. */
    for (char *q = reale + strlen(c->a->radice) + 1; *q; q++) {
        if (*q == '/') {
            *q = '\0';
            mkdir(reale, 0755);
            *q = '/';
        }
    }
    c->fd = open(reale, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (c->fd < 0)
        snprintf(c->motivo_fail, sizeof c->motivo_fail, "open failed: %s", strerror(errno));
}

static void fine_invio(PhCanale *c, uint32_t mtime)
{
    c->invio = false;
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
        if (!c->motivo_fail[0] && c->a->o.fail_invio)
        {
            snprintf(c->motivo_fail, sizeof c->motivo_fail, "write failed: No space left on device");
            char reale[5400];
            finto_percorso(c->a, c->percorso_invio, reale, sizeof reale);
            unlink(reale); /* adbd toglie il parziale quando fallisce lui */
        }
        if (!c->motivo_fail[0]) {
            char reale[5400];
            finto_percorso(c->a, c->percorso_invio, reale, sizeof reale);
            struct timespec t[2] = {{(time_t)mtime, 0}, {(time_t)mtime, 0}};
            utimensat(AT_FDCWD, reale, t, 0);
        }
    }
    if (c->motivo_fail[0]) {
        emetti_fail(c, c->motivo_fail);
    } else {
        uint8_t ok[8] = {'O', 'K', 'A', 'Y'};
        emetti(c, ok, sizeof ok);
    }
}

/* Consuma le richieste complete presenti nell'ingresso. */
static void elabora(PhCanale *c)
{
    size_t pos = 0;
    while (!c->rotto && c->in_n - pos >= 8) {
        const uint8_t *h = c->in + pos;
        uint32_t len = get32(h + 4);
        if (c->invio) {
            if (memcmp(h, "DONE", 4) == 0) {
                fine_invio(c, len);
                pos += 8;
            } else if (memcmp(h, "DATA", 4) == 0 && len <= 65536) {
                if (c->in_n - pos < 8 + (size_t)len)
                    break;
                if (c->fd >= 0 && write(c->fd, h + 8, len) != (ssize_t)len)
                    snprintf(c->motivo_fail, sizeof c->motivo_fail, "write failed: %s", strerror(errno));
                pos += 8 + len;
            } else {
                c->rotto = true;
            }
            continue;
        }
        if (memcmp(h, "QUIT", 4) == 0) {
            pos += 8;
            continue;
        }
        if (len > 4096) {
            c->rotto = true;
            break;
        }
        if (c->in_n - pos < 8 + (size_t)len)
            break;
        char p[4200];
        memcpy(p, h + 8, len);
        p[len] = '\0';
        if (memcmp(h, "STA2", 4) == 0)
            fai_stat(c, "STA2", true, p);
        else if (memcmp(h, "LST2", 4) == 0)
            fai_stat(c, "LST2", false, p);
        else if (memcmp(h, "LIS2", 4) == 0)
            fai_lista(c, p);
        else if (memcmp(h, "RECV", 4) == 0)
            fai_ricevi(c, p);
        else if (memcmp(h, "SEND", 4) == 0)
            inizia_invio(c, p);
        else
            c->rotto = true;
        pos += 8 + len;
    }
    memmove(c->in, c->in + pos, c->in_n - pos);
    c->in_n -= pos;
}

/* ---- API dei canali -------------------------------------------------------- */

PhCanale *ph_canale_apri(PhAdb *a, const char *servizio, int timeout_ms, PhErrore *e)
{
    (void)timeout_ms;
    if (a->o.rifiuta_apertura) {
        ph_errore(e, "il telefono ha rifiutato il servizio %s", servizio);
        return NULL;
    }
    PhCanale *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->a = a;
    c->fd = -1;
    a->canali_aperti++;
    if (strcmp(servizio, "sync:") == 0)
        return c;
    static const char rm[] = "shell:rm -f -- '";
    if (strncmp(servizio, rm, sizeof rm - 1) == 0) {
        c->shell = true;
        c->chiudi_a_fine = true;
        char p[1200];
        snprintf(p, sizeof p, "%s", servizio + sizeof rm - 1);
        size_t n = strlen(p);
        if (n > 0 && p[n - 1] == '\'')
            p[n - 1] = '\0';
        /* La shell vera toglie gli apici: '\'' diventa '. */
        char *w = p;
        for (char *q = p; *q; q++) {
            if (strncmp(q, "'\\''", 4) == 0) {
                *w++ = '\'';
                q += 3;
            } else {
                *w++ = *q;
            }
        }
        *w = '\0';
        char reale[5400];
        finto_percorso(a, p, reale, sizeof reale);
        unlink(reale);
        a->rm_eseguiti++;
        return c;
    }
    a->canali_aperti--;
    free(c);
    ph_errore(e, "servizio sconosciuto al telefono finto: %s", servizio);
    return NULL;
}

ssize_t ph_canale_leggi(PhCanale *c, void *dati, size_t n, int timeout_ms, PhErrore *e)
{
    if (c->out_pos < c->out_n) {
        size_t d = c->out_n - c->out_pos;
        if (d > n)
            d = n;
        memcpy(dati, c->out + c->out_pos, d);
        c->out_pos += d;
        return (ssize_t)d;
    }
    if (c->rotto) {
        ph_errore(e, "finto: richiesta incomprensibile");
        return PH_CANALE_ERRORE;
    }
    /* Niente altro da dire: il telefono chiude, salvo che si voglia simulare
     * un telefono che tace (stallo). */
    if (c->chiudi_a_fine || !c->a->o.stallo)
        return PH_CANALE_CHIUSO;
    if (timeout_ms < 0) {
        ph_errore(e, "finto: lettura senza fine su un canale senza dati");
        return PH_CANALE_ERRORE;
    }
    /* Il telefono tace: si aspetta davvero, come un timeout vero. */
    struct timespec t = {timeout_ms / 1000, (long)(timeout_ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
    return PH_CANALE_TEMPO;
}

int ph_canale_leggi_tutto(PhCanale *c, void *dati, size_t n, int timeout_ms, PhErrore *e)
{
    uint8_t *p = dati;
    size_t fatti = 0;
    while (fatti < n) {
        ssize_t r = ph_canale_leggi(c, p + fatti, n - fatti, timeout_ms, e);
        if (r <= 0)
            return ph_errore(e, "lettura incompleta");
        fatti += (size_t)r;
    }
    return 0;
}

int ph_canale_scrivi(PhCanale *c, const void *dati, size_t n, PhErrore *e)
{
    if (c->rotto)
        return ph_errore(e, "finto: canale rotto");
    if (c->scritti + n > c->a->o.scrivi_errore_dopo) {
        c->rotto = true;
        return ph_errore(e, "finto: connessione caduta durante la scrittura");
    }
    c->scritti += n;
    if (c->in_n + n > c->in_cap) {
        size_t nc = (c->in_n + n) * 2;
        uint8_t *i = realloc(c->in, nc);
        if (!i)
            abort();
        c->in = i;
        c->in_cap = nc;
    }
    memcpy(c->in + c->in_n, dati, n);
    c->in_n += n;
    elabora(c);
    return 0;
}

void ph_canale_chiudi(PhCanale *c)
{
    if (!c)
        return;
    if (c->fd >= 0)
        close(c->fd); /* un SEND interrotto lascia il file parziale com'e' */
    c->a->ultimo_prodotti = c->prodotti;
    c->a->canali_chiusi++;
    free(c->in);
    free(c->out);
    free(c);
}
