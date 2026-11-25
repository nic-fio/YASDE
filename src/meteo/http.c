/* HTTP/1.1 minimo sopra la connessione cifrata (tls.h): una GET, risposta
 * letta con limiti di dimensione. Niente redirezioni, niente compressione,
 * nessuna connessione riusata (Connection: close). Tutto cio' che arriva dalla
 * rete e' ostile: ogni lunghezza e' controllata prima di usarla. */
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "interno.h"

#define INTESTAZIONI_MAX (16u * 1024u)
#define RIGA_MAX 8192u

typedef struct {
    PhTls *t;
    const atomic_int *annulla;
    int64_t scadenza;
    PhErrore *e;
    unsigned char buf[4096];
    size_t pos, len;
} Lett;

/* 0 = ho un byte in *c, 1 = fine del flusso, -1 = errore (e riempito). */
static int byte_(Lett *l, int *c)
{
    if (l->pos == l->len) {
        ssize_t r = ph_tls_leggi(l->t, l->buf, sizeof l->buf, l->scadenza, l->annulla, l->e);
        if (r < 0)
            return -1;
        if (r == 0)
            return 1;
        if ((size_t)r > sizeof l->buf) /* un'implementazione difettosa non deve farci leggere fuori */
            return ph_errore(l->e, "il servizio meteo non risponde");
        l->pos = 0;
        l->len = (size_t)r;
    }
    *c = l->buf[l->pos++];
    return 0;
}

static int errore_rotto(Lett *l) { return ph_errore(l->e, "risposta del servizio meteo non valida"); }
static int errore_interrotto(Lett *l) { return ph_errore(l->e, "il servizio meteo ha interrotto la risposta"); }

/* Legge una riga (fino a LF, un solo CR finale e' tolto) in out[cap]; `budget`
 * e' il totale di byte ancora ammessi per questo gruppo di righe. Rifiuta NUL,
 * CR in mezzo, caratteri di controllo (tranne il tab) e 0x7F: con un NUL
 * dentro, il resto della riga sparirebbe per le funzioni di stringa e ognuno
 * capirebbe una cosa diversa (e' la base dei trucchi di «smuggling»). 0 / -1. */
static int riga(Lett *l, char *out, size_t cap, size_t *budget)
{
    size_t n = 0;
    for (;;) {
        int c = 0;
        int r = byte_(l, &c);
        if (r < 0)
            return -1;
        if (r == 1)
            return errore_interrotto(l);
        if (*budget == 0)
            return errore_rotto(l);
        (*budget)--;
        if (c == '\n')
            break;
        if ((c < 0x20 && c != '\t' && c != '\r') || c == 0x7F)
            return errore_rotto(l);
        if (n + 1 >= cap)
            return errore_rotto(l);
        out[n++] = (char)c;
    }
    if (n > 0 && out[n - 1] == '\r')
        n--;
    out[n] = '\0';
    if (memchr(out, '\r', n) != NULL)
        return errore_rotto(l);
    return 0;
}

/* Nome di intestazione: «token» di RFC 9110 (niente spazi, niente due punti). */
static bool nome_valido(const char *s)
{
    if (*s == '\0')
        return false;
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  (c != '\0' && strchr("!#$%&'*+-.^_`|~", c) != NULL);
        if (!ok)
            return false;
    }
    return true;
}

typedef struct {
    char *p;
    size_t n, cap;
    size_t max; /* dimensione massima ammessa del corpo */
} Corpo;

static int corpo_aggiungi(Corpo *c, const void *d, size_t n, PhErrore *e)
{
    if (n > c->max - c->n)
        return ph_errore(e, "risposta del servizio meteo troppo grande");
    if (c->n + n + 1 > c->cap) {
        size_t nc = c->cap ? c->cap : 4096;
        while (nc < c->n + n + 1)
            nc *= 2;
        char *p = realloc(c->p, nc);
        if (p == NULL)
            return ph_errore(e, "meteo: memoria esaurita");
        c->p = p;
        c->cap = nc;
    }
    memcpy(c->p + c->n, d, n);
    c->n += n;
    c->p[c->n] = '\0';
    return 0;
}

/* Copia fino a `max` byte dal lettore (rifornendolo se serve) in *c.
 * 0 = ho copiato qualcosa, 1 = fine del flusso, -1 = errore. */
static int corpo_pezzo(Lett *l, Corpo *c, size_t max)
{
    if (l->pos == l->len) {
        int ch = 0;
        int r = byte_(l, &ch);
        if (r != 0)
            return r;
        l->pos--; /* il byte appena letto resta da consegnare */
    }
    size_t d = l->len - l->pos;
    if (d > max)
        d = max;
    if (corpo_aggiungi(c, l->buf + l->pos, d, l->e) != 0)
        return -1;
    l->pos += d;
    return 0;
}

static int corpo_esatto(Lett *l, Corpo *c, size_t n)
{
    while (n > 0) {
        size_t prima = c->n;
        int r = corpo_pezzo(l, c, n);
        if (r < 0)
            return -1;
        if (r == 1)
            return errore_interrotto(l);
        n -= c->n - prima;
    }
    return 0;
}

static int corpo_chunked(Lett *l, Corpo *c)
{
    /* Tetto COMPLESSIVO per tutte le righe dei pezzi (dimensioni, CRLF, code
     * finali): una risposta a pezzi da un byte non puo' tenerci occupati. */
    size_t budget_tot = INTESTAZIONI_MAX;
    for (;;) {
        char r[128];
        if (riga(l, r, sizeof r, &budget_tot) != 0)
            return -1;
        /* dimensione in esadecimale (al massimo 8 cifre), estensioni dopo ';' ignorate */
        size_t i = 0;
        uint64_t sz = 0;
        while (r[i] != '\0' && r[i] != ';' && r[i] != ' ' && r[i] != '\t') {
            int d = isxdigit((unsigned char)r[i]) ? (r[i] <= '9' ? r[i] - '0' : (r[i] | 0x20) - 'a' + 10) : -1;
            if (d < 0 || i >= 8)
                return errore_rotto(l);
            sz = sz * 16 + (uint64_t)d;
            i++;
        }
        if (i == 0)
            return errore_rotto(l);
        if (sz == 0)
            break;
        if (sz > c->max - c->n)
            return ph_errore(l->e, "risposta del servizio meteo troppo grande");
        if (corpo_esatto(l, c, (size_t)sz) != 0)
            return -1;
        char fine[4];
        if (riga(l, fine, sizeof fine, &budget_tot) != 0)
            return -1;
        if (fine[0] != '\0') /* dopo i dati ci vuole solo CRLF */
            return errore_rotto(l);
    }
    /* Code finali: righe fino alla riga vuota. Se il server chiude subito
     * dopo l'ultimo chunk il corpo e' comunque completo. */
    for (;;) {
        char r[RIGA_MAX];
        int ch = 0;
        if (l->pos == l->len) {
            int q = byte_(l, &ch);
            if (q < 0)
                return -1;
            if (q == 1)
                return 0;
            l->pos--;
        }
        if (riga(l, r, sizeof r, &budget_tot) != 0)
            return -1;
        if (r[0] == '\0')
            return 0;
    }
}

static const char *salta_spazi(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

/* Vero se il valore (spazi finali tolti) e' esattamente `parola`, senza maiuscole. */
static bool valore_uguale(const char *v, const char *parola)
{
    v = salta_spazi(v);
    size_t n = strlen(parola);
    if (strncasecmp(v, parola, n) != 0)
        return false;
    return *salta_spazi(v + n) == '\0';
}

static int risposta(Lett *l, Corpo *c)
{
    size_t budget = INTESTAZIONI_MAX;
    char r[RIGA_MAX];
    if (riga(l, r, sizeof r, &budget) != 0)
        return -1;
    /* "HTTP/1.x 200 ..." */
    if (strncmp(r, "HTTP/1.", 7) != 0 || (r[7] != '0' && r[7] != '1') || r[8] != ' ' || !isdigit((unsigned char)r[9]) ||
        !isdigit((unsigned char)r[10]) || !isdigit((unsigned char)r[11]) || (r[12] != ' ' && r[12] != '\0'))
        return errore_rotto(l);
    int codice = (r[9] - '0') * 100 + (r[10] - '0') * 10 + (r[11] - '0');
    if (codice != 200)
        return ph_errore(l->e, "il servizio meteo non e' disponibile (codice %d)", codice);

    bool chunked = false, ha_lung = false;
    uint64_t lung = 0;
    for (;;) {
        if (riga(l, r, sizeof r, &budget) != 0)
            return -1;
        if (r[0] == '\0')
            break;
        if (r[0] == ' ' || r[0] == '\t') /* intestazioni «piegate»: non le accettiamo */
            return errore_rotto(l);
        char *due = strchr(r, ':');
        if (due == NULL)
            return errore_rotto(l);
        *due = '\0';
        if (!nome_valido(r))
            return errore_rotto(l);
        const char *nome = r, *val = due + 1;
        if (strcasecmp(nome, "Content-Length") == 0) {
            val = salta_spazi(val);
            if (ha_lung || *val == '\0')
                return errore_rotto(l);
            lung = 0;
            size_t k = 0;
            for (; val[k] >= '0' && val[k] <= '9'; k++) {
                if (k >= 15)
                    return errore_rotto(l);
                lung = lung * 10 + (uint64_t)(val[k] - '0');
            }
            if (*salta_spazi(val + k) != '\0' || k == 0)
                return errore_rotto(l);
            ha_lung = true;
        } else if (strcasecmp(nome, "Transfer-Encoding") == 0) {
            if (!valore_uguale(val, "chunked"))
                return errore_rotto(l);
            chunked = true;
        } else if (strcasecmp(nome, "Content-Encoding") == 0) {
            if (!valore_uguale(val, "identity"))
                return errore_rotto(l);
        }
    }
    if (chunked && ha_lung) /* ambiguo: e' il trucco classico del «request smuggling» */
        return errore_rotto(l);
    if (chunked)
        return corpo_chunked(l, c);
    if (ha_lung) {
        if (lung > c->max)
            return ph_errore(l->e, "risposta del servizio meteo troppo grande");
        return corpo_esatto(l, c, (size_t)lung);
    }
    /* Senza lunghezza: il corpo finisce con la chiusura (pulita) del flusso. */
    for (;;) {
        int q = corpo_pezzo(l, c, sizeof l->buf);
        if (q < 0)
            return -1;
        if (q == 1)
            return 0;
    }
}

int ph_meteo_http_scarica(const char *host, const char *percorso, size_t max_corpo, const atomic_int *annulla,
                          int64_t scadenza_ms, char **corpo, size_t *n, PhErrore *e)
{
    if (host == NULL || percorso == NULL || corpo == NULL || n == NULL || max_corpo == 0)
        return ph_errore(e, "meteo: argomenti non validi");
    /* Difesa in profondita': un percorso con spazi, CR, LF o NUL spezzerebbe la
     * richiesta (chi lo costruisce codifica tutto, ma qui non ci si fida). */
    for (const char *q = percorso; *q != '\0'; q++)
        if ((unsigned char)*q <= 0x20 || (unsigned char)*q >= 0x7F)
            return ph_errore(e, "meteo: percorso non valido");
    char req[1536];
    int rl = snprintf(req, sizeof req,
                      "GET %s HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "User-Agent: " PH_METEO_USER_AGENT "\r\n"
                      "Accept: application/json\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      percorso, host);
    if (rl < 0 || (size_t)rl >= sizeof req)
        return ph_errore(e, "meteo: richiesta troppo lunga");

    PhTls *t = ph_tls_apri(host, PH_METEO_PORTA, scadenza_ms, annulla, e);
    if (t == NULL)
        return -1;
    Lett *l = calloc(1, sizeof *l);
    Corpo c = {.max = max_corpo};
    int r = -1;
    if (l == NULL) {
        ph_errore(e, "meteo: memoria esaurita");
        goto fine;
    }
    l->t = t;
    l->annulla = annulla;
    l->scadenza = scadenza_ms;
    l->e = e;
    if (ph_tls_scrivi(t, req, (size_t)rl, scadenza_ms, annulla, e) != 0)
        goto fine;
    if (risposta(l, &c) != 0)
        goto fine;
    if (c.p == NULL && corpo_aggiungi(&c, "", 0, e) != 0) /* corpo vuoto: buffer valido comunque */
        goto fine;
    *corpo = c.p;
    *n = c.n;
    c.p = NULL;
    r = 0;
fine:
    free(c.p);
    free(l);
    ph_tls_chiudi(t);
    return r;
}
