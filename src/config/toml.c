/* Lettore e scrittore del sottoinsieme di TOML (vedi toml.h). Il lettore
 * lavora su un buffer con lunghezza esplicita e non si fida di nulla: ogni
 * avanzamento controlla `fine`, ogni allocazione e' limitata. */
#include "config/toml.h"

#include <errno.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- utilita' ---------- */

/* Solo ASCII, mai isalnum/isdigit: dipendono dalla lingua del processo. */
static bool cifra_ascii(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static bool carattere_chiave(unsigned char c)
{
    return cifra_ascii(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
}

static bool carattere_tabella(unsigned char c)
{
    return carattere_chiave(c) || c == '.';
}

/* Lunghezza (1-4) della sequenza UTF-8 valida all'inizio di s, 0 se non e'
 * valida: niente sequenze non minime, niente surrogati, massimo U+10FFFF. */
static size_t utf8_seq(const unsigned char *s, size_t n)
{
    unsigned char c = s[0];
    if (c < 0x80)
        return 1;
    size_t extra;
    uint32_t cp;
    uint32_t minimo;
    if ((c & 0xE0) == 0xC0) {
        extra = 1;
        cp = c & 0x1F;
        minimo = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
        cp = c & 0x0F;
        minimo = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
        cp = c & 0x07;
        minimo = 0x10000;
    } else {
        return 0;
    }
    if (n - 1 < extra)
        return 0;
    for (size_t k = 1; k <= extra; k++) {
        if ((s[k] & 0xC0) != 0x80)
            return 0;
        cp = (cp << 6) | (s[k] & 0x3F);
    }
    if (cp < minimo || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return 0;
    return extra + 1;
}

static bool utf8_valido(const unsigned char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t k = utf8_seq(s + i, n - i);
        if (k == 0)
            return false;
        i += k;
    }
    return true;
}

/* Scrive cp in UTF-8 (cp gia' validato); torna i byte scritti. */
static size_t utf8_scrivi(uint32_t cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static int cifra_esa(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static void *cresci(void *vecchio, size_t *capacita, size_t n, size_t dim, size_t iniziale)
{
    if (n < *capacita)
        return vecchio;
    size_t nuova = *capacita ? *capacita * 2 : iniziale;
    if (nuova > SIZE_MAX / dim)
        return NULL;
    void *p = realloc(vecchio, nuova * dim);
    if (p != NULL)
        *capacita = nuova;
    return p;
}

/* ---------- lettura ---------- */

/* Legge una stringa tra virgolette; *pp punta alla virgoletta di apertura.
 * Torna una copia allocata, oppure NULL con *msg valorizzato. A fine lettura
 * *pp punta dopo la virgoletta di chiusura. */
static char *leggi_stringa(const char **pp, const char *fine, const char **msg)
{
    const char *p = *pp + 1;
    char *out = malloc((size_t)(fine - p) + 1);
    if (out == NULL) {
        *msg = "memoria esaurita";
        return NULL;
    }
    size_t n = 0;
    for (;;) {
        if (p >= fine) {
            *msg = "virgolette non chiuse";
            goto errore;
        }
        unsigned char c = (unsigned char)*p++;
        if (c == '"')
            break;
        if (c == '\\') {
            if (p >= fine) {
                *msg = "virgolette non chiuse";
                goto errore;
            }
            char x = *p++;
            switch (x) {
            case '"': out[n++] = '"'; break;
            case '\\': out[n++] = '\\'; break;
            case 'n': out[n++] = '\n'; break;
            case 't': out[n++] = '\t'; break;
            case 'r': out[n++] = '\r'; break;
            case 'b': out[n++] = '\b'; break;
            case 'f': out[n++] = '\f'; break;
            case 'u':
            case 'U': {
                int cifre = x == 'u' ? 4 : 8;
                if (fine - p < cifre) {
                    *msg = "sequenza \\u incompleta";
                    goto errore;
                }
                uint32_t cp = 0;
                for (int k = 0; k < cifre; k++) {
                    int d = cifra_esa(p[k]);
                    if (d < 0) {
                        *msg = "sequenza \\u non valida";
                        goto errore;
                    }
                    cp = (cp << 4) | (uint32_t)d;
                }
                p += cifre;
                if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                    *msg = "carattere \\u non ammesso";
                    goto errore;
                }
                n += utf8_scrivi(cp, out + n);
                break;
            }
            default:
                *msg = "sequenza di escape sconosciuta";
                goto errore;
            }
            continue;
        }
        if ((c < 0x20 && c != '\t') || c == 0x7F) {
            *msg = "carattere di controllo dentro la stringa";
            goto errore;
        }
        out[n++] = (char)c;
    }
    out[n] = '\0';
    if (!utf8_valido((const unsigned char *)out, n)) {
        *msg = "UTF-8 non valido";
        goto errore;
    }
    *pp = p;
    return out;
errore:
    free(out);
    return NULL;
}

/* strtod senza dipendere dalla lingua del processo (virgola decimale). */
static double decimale_c(const char *s)
{
    locale_t c = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    double v = c != (locale_t)0 ? strtod_l(s, NULL, c) : strtod(s, NULL);
    if (c != (locale_t)0)
        freelocale(c);
    return v;
}

/* Interpreta il token di un numero: -1 non e' un numero, altrimenti 0
 * (intero) o 1 (decimale). */
static int forma_numero(const char *t, size_t n)
{
    size_t i = 0;
    if (i < n && (t[i] == '+' || t[i] == '-'))
        i++;
    size_t cifre = 0;
    while (i < n && cifra_ascii((unsigned char)t[i])) {
        i++;
        cifre++;
    }
    if (cifre == 0)
        return -1;
    int decimale = 0;
    if (i < n && t[i] == '.') {
        i++;
        size_t fr = 0;
        while (i < n && cifra_ascii((unsigned char)t[i])) {
            i++;
            fr++;
        }
        if (fr == 0)
            return -1;
        decimale = 1;
    }
    if (i < n && (t[i] == 'e' || t[i] == 'E')) {
        i++;
        if (i < n && (t[i] == '+' || t[i] == '-'))
            i++;
        size_t es = 0;
        while (i < n && cifra_ascii((unsigned char)t[i])) {
            i++;
            es++;
        }
        if (es == 0)
            return -1;
        decimale = 1;
    }
    return i == n ? decimale : -1;
}

static int aggiungi_tabella(PhToml *doc, const char *nome, bool ripetuta, int riga, const char **msg)
{
    for (size_t k = 1; k < doc->n; k++) {
        if (strcmp(doc->tabelle[k].nome, nome) == 0 && (!ripetuta || !doc->tabelle[k].ripetuta)) {
            *msg = "tabella definita due volte";
            return -1;
        }
    }
    if (doc->n >= PH_TOML_MAX_TABELLE) {
        *msg = "troppe tabelle";
        return -1;
    }
    PhTomlTabella *nuove = cresci(doc->tabelle, &doc->capacita, doc->n, sizeof *nuove, 8);
    char *copia = strdup(nome);
    if (nuove == NULL || copia == NULL) {
        if (nuove != NULL)
            doc->tabelle = nuove;
        free(copia);
        *msg = "memoria esaurita";
        return -1;
    }
    doc->tabelle = nuove;
    doc->tabelle[doc->n] = (PhTomlTabella){copia, ripetuta, riga, NULL, 0, 0};
    doc->n++;
    return 0;
}

/* Aggiunge la voce all'ultima tabella; prende possesso di chiave e s. */
static int aggiungi_voce(PhToml *doc, PhTomlVoce *v, const char **msg)
{
    PhTomlTabella *t = &doc->tabelle[doc->n - 1];
    for (size_t k = 0; k < t->n; k++) {
        if (strcmp(t->voci[k].chiave, v->chiave) == 0) {
            *msg = "chiave definita due volte";
            return -1;
        }
    }
    if (t->n >= PH_TOML_MAX_VOCI) {
        *msg = "troppe voci nella tabella";
        return -1;
    }
    PhTomlVoce *nuove = cresci(t->voci, &t->capacita, t->n, sizeof *nuove, 8);
    if (nuove == NULL) {
        *msg = "memoria esaurita";
        return -1;
    }
    t->voci = nuove;
    t->voci[t->n++] = *v;
    return 0;
}

static const char *salta_spazi(const char *p, const char *fine)
{
    while (p < fine && (*p == ' ' || *p == '\t'))
        p++;
    return p;
}

/* Dopo un elemento: spazi poi fine riga o commento. */
static bool fine_riga_ok(const char *p, const char *fine)
{
    p = salta_spazi(p, fine);
    return p == fine || *p == '#';
}

static int leggi_riga(PhToml *doc, const char *p, const char *fine, int riga, PhErrore *e)
{
    const char *msg = NULL;
    p = salta_spazi(p, fine);
    if (p == fine || *p == '#')
        return 0;

    if (*p == '[') {
        bool ripetuta = p + 1 < fine && p[1] == '[';
        p += ripetuta ? 2 : 1;
        p = salta_spazi(p, fine);
        const char *ini = p;
        while (p < fine && carattere_tabella((unsigned char)*p))
            p++;
        size_t ln = (size_t)(p - ini);
        p = salta_spazi(p, fine);
        if (ln == 0 || ln > 64) {
            msg = "nome di tabella vuoto, troppo lungo o con caratteri non ammessi";
            goto errore;
        }
        char nome[65];
        memcpy(nome, ini, ln);
        nome[ln] = '\0';
        size_t chiusure = ripetuta ? 2 : 1;
        for (size_t k = 0; k < chiusure; k++) {
            if (p >= fine || *p != ']') {
                msg = "intestazione di tabella non chiusa";
                goto errore;
            }
            p++;
        }
        if (!fine_riga_ok(p, fine)) {
            msg = "caratteri dopo l'intestazione";
            goto errore;
        }
        if (aggiungi_tabella(doc, nome, ripetuta, riga, &msg) < 0)
            goto errore;
        return 0;
    }

    /* chiave = valore */
    PhTomlVoce v = {NULL, PH_TOML_BOOLEANO, NULL, 0, 0.0, false, riga};
    if (*p == '"') {
        v.chiave = leggi_stringa(&p, fine, &msg);
        if (v.chiave == NULL)
            goto errore;
        if (v.chiave[0] == '\0') {
            msg = "chiave vuota";
            goto errore_voce;
        }
    } else {
        const char *ini = p;
        while (p < fine && carattere_chiave((unsigned char)*p))
            p++;
        if (p == ini) {
            msg = "attesa una chiave o un'intestazione [tabella]";
            goto errore;
        }
        v.chiave = strndup(ini, (size_t)(p - ini));
        if (v.chiave == NULL) {
            msg = "memoria esaurita";
            goto errore;
        }
    }
    p = salta_spazi(p, fine);
    if (p >= fine || *p != '=') {
        msg = "manca il segno '=' dopo la chiave";
        goto errore_voce;
    }
    p = salta_spazi(p + 1, fine);
    if (p >= fine || *p == '#') {
        msg = "manca il valore";
        goto errore_voce;
    }

    if (*p == '"') {
        v.tipo = PH_TOML_STRINGA;
        v.s = leggi_stringa(&p, fine, &msg);
        if (v.s == NULL)
            goto errore_voce;
    } else {
        const char *ini = p;
        while (p < fine && *p != ' ' && *p != '\t' && *p != '#')
            p++;
        size_t ln = (size_t)(p - ini);
        if (ln == 4 && memcmp(ini, "true", 4) == 0) {
            v.tipo = PH_TOML_BOOLEANO;
            v.b = true;
        } else if (ln == 5 && memcmp(ini, "false", 5) == 0) {
            v.tipo = PH_TOML_BOOLEANO;
            v.b = false;
        } else {
            if (ln > 64) {
                msg = "valore non riconosciuto";
                goto errore_voce;
            }
            char tok[65];
            memcpy(tok, ini, ln);
            tok[ln] = '\0';
            int forma = forma_numero(tok, ln);
            if (forma < 0) {
                msg = "valore non riconosciuto (stringhe tra virgolette, numeri, true/false)";
                goto errore_voce;
            }
            if (forma == 0) {
                errno = 0;
                char *fin = NULL;
                long long x = strtoll(tok, &fin, 10);
                if (errno == ERANGE || fin == tok || *fin != '\0') {
                    msg = "numero troppo grande";
                    goto errore_voce;
                }
                v.tipo = PH_TOML_INTERO;
                v.i = x;
            } else {
                double x = decimale_c(tok);
                if (!isfinite(x)) {
                    msg = "numero decimale troppo grande";
                    goto errore_voce;
                }
                v.tipo = PH_TOML_DECIMALE;
                v.d = x;
            }
        }
    }
    if (!fine_riga_ok(p, fine)) {
        msg = "caratteri dopo il valore";
        goto errore_voce;
    }
    if (aggiungi_voce(doc, &v, &msg) < 0)
        goto errore_voce;
    return 0;

errore_voce:
    free(v.chiave);
    free(v.s);
errore:
    return ph_errore(e, "riga %d: %s", riga, msg != NULL ? msg : "errore");
}

void ph_toml_libera(PhToml *doc)
{
    for (size_t k = 0; k < doc->n; k++) {
        PhTomlTabella *t = &doc->tabelle[k];
        for (size_t j = 0; j < t->n; j++) {
            free(t->voci[j].chiave);
            free(t->voci[j].s);
        }
        free(t->voci);
        free(t->nome);
    }
    free(doc->tabelle);
    *doc = (PhToml){NULL, 0, 0};
}

int ph_toml_leggi(const char *testo, size_t lunghezza, PhToml *doc, PhErrore *e)
{
    *doc = (PhToml){NULL, 0, 0};
    if (lunghezza > PH_TOML_MAX_FILE)
        return ph_errore(e, "file troppo grande (oltre %u byte)", PH_TOML_MAX_FILE);
    if (lunghezza > 0) {
        const char *nul = memchr(testo, '\0', lunghezza);
        if (nul != NULL) {
            int riga = 1;
            for (const char *q = testo; q < nul; q++)
                if (*q == '\n')
                    riga++;
            return ph_errore(e, "riga %d: byte nullo nel file", riga);
        }
    }
    const char *msg = NULL;
    if (aggiungi_tabella(doc, "", false, 0, &msg) < 0) {
        ph_toml_libera(doc);
        return ph_errore(e, "%s", msg);
    }
    int riga = 0;
    const char *p = testo;
    const char *fine = testo + lunghezza;
    while (p < fine) {
        riga++;
        const char *nl = memchr(p, '\n', (size_t)(fine - p));
        const char *fr = nl != NULL ? nl : fine;
        const char *prossima = nl != NULL ? nl + 1 : fine;
        if (fr > p && fr[-1] == '\r')
            fr--;
        if ((size_t)(fr - p) > PH_TOML_MAX_RIGA) {
            ph_toml_libera(doc);
            return ph_errore(e, "riga %d: riga troppo lunga (oltre %u byte)", riga, PH_TOML_MAX_RIGA);
        }
        if (leggi_riga(doc, p, fr, riga, e) < 0) {
            ph_toml_libera(doc);
            return -1;
        }
        p = prossima;
    }
    return 0;
}

const PhTomlVoce *ph_toml_voce(const PhTomlTabella *t, const char *chiave, PhTomlTipo tipo)
{
    for (size_t k = 0; k < t->n; k++)
        if (strcmp(t->voci[k].chiave, chiave) == 0)
            return t->voci[k].tipo == tipo ? &t->voci[k] : NULL;
    return NULL;
}

/* ---------- scrittura ---------- */

static int agg(PhBuffer *b, const char *s)
{
    return ph_buffer_aggiungi(b, s, strlen(s));
}

/* Scrive una stringa che il lettore rilegge identica. Il testo che arriva dal
 * telefono (nomi) puo' non essere UTF-8: i byte non validi diventano '?', cosi'
 * un nome ostile non rende illeggibile tutto il file. */
static int agg_stringa(PhBuffer *b, const char *s)
{
    if (agg(b, "\"") < 0)
        return -1;
    size_t n = strlen(s);
    size_t i = 0;
    while (i < n) {
        const unsigned char *p = (const unsigned char *)s + i;
        size_t k = utf8_seq(p, n - i);
        char tmp[8];
        const char *out = tmp;
        if (k == 0) {
            out = "?";
            k = 1;
        } else if (k > 1) {
            memcpy(tmp, p, k);
            tmp[k] = '\0';
        } else {
            switch (*p) {
            case '"': out = "\\\""; break;
            case '\\': out = "\\\\"; break;
            case '\n': out = "\\n"; break;
            case '\t': out = "\\t"; break;
            case '\r': out = "\\r"; break;
            default:
                if (*p < 0x20 || *p == 0x7F)
                    snprintf(tmp, sizeof tmp, "\\u%04X", *p);
                else {
                    tmp[0] = (char)*p;
                    tmp[1] = '\0';
                }
            }
        }
        if (agg(b, out) < 0)
            return -1;
        i += k;
    }
    return agg(b, "\"");
}

static int agg_chiave(PhBuffer *b, const char *k)
{
    bool nuda = k[0] != '\0';
    for (const char *p = k; *p != '\0'; p++)
        if (!carattere_chiave((unsigned char)*p))
            nuda = false;
    if (nuda)
        return agg(b, k);
    return agg_stringa(b, k);
}

static int inizio_voce(PhBuffer *b, const char *chiave)
{
    if (agg_chiave(b, chiave) < 0 || agg(b, " = ") < 0)
        return -1;
    return 0;
}

int ph_toml_scrivi_commento(PhBuffer *b, const char *testo)
{
    /* Un a capo dentro il commento lo spezzerebbe: ogni riga ha il suo '#'. */
    const char *p = testo;
    for (;;) {
        const char *nl = strchr(p, '\n');
        size_t ln = nl != NULL ? (size_t)(nl - p) : strlen(p);
        if (agg(b, "# ") < 0 || ph_buffer_aggiungi(b, p, ln) < 0 || agg(b, "\n") < 0)
            return -1;
        if (nl == NULL)
            return 0;
        p = nl + 1;
    }
}

int ph_toml_scrivi_tabella(PhBuffer *b, const char *nome, bool ripetuta)
{
    if (agg(b, ripetuta ? "\n[[" : "\n[") < 0 || agg(b, nome) < 0 || agg(b, ripetuta ? "]]\n" : "]\n") < 0)
        return -1;
    return 0;
}

int ph_toml_scrivi_stringa(PhBuffer *b, const char *chiave, const char *valore)
{
    if (inizio_voce(b, chiave) < 0 || agg_stringa(b, valore) < 0 || agg(b, "\n") < 0)
        return -1;
    return 0;
}

int ph_toml_scrivi_intero(PhBuffer *b, const char *chiave, int64_t valore)
{
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%lld", (long long)valore);
    if (inizio_voce(b, chiave) < 0 || agg(b, tmp) < 0 || agg(b, "\n") < 0)
        return -1;
    return 0;
}

int ph_toml_scrivi_decimale(PhBuffer *b, const char *chiave, double valore)
{
    char tmp[64];
    if (!isfinite(valore))
        valore = 0.0;
    snprintf(tmp, sizeof tmp, "%.17g", valore);
    /* La lingua del processo potrebbe usare la virgola: nel file sempre il punto. */
    bool segno_decimale = false;
    for (char *p = tmp; *p != '\0'; p++) {
        if (*p == ',')
            *p = '.';
        if (*p == '.' || *p == 'e' || *p == 'E')
            segno_decimale = true;
    }
    if (!segno_decimale)
        strncat(tmp, ".0", sizeof tmp - strlen(tmp) - 1);
    if (inizio_voce(b, chiave) < 0 || agg(b, tmp) < 0 || agg(b, "\n") < 0)
        return -1;
    return 0;
}

int ph_toml_scrivi_booleano(PhBuffer *b, const char *chiave, bool valore)
{
    if (inizio_voce(b, chiave) < 0 || agg(b, valore ? "true" : "false") < 0 || agg(b, "\n") < 0)
        return -1;
    return 0;
}
