/* Percorso della richiesta e analisi della risposta JSON di Open-Meteo.
 * La risposta arriva da Internet: e' trattata come ostile. L'analizzatore e'
 * nostro, lavora sempre con la lunghezza (mai cerca un NUL), ha limiti su
 * dimensione, profondita', numero di nodi, lunghezza di stringhe e numeri, e
 * non scrive mai fuori dai suoi buffer. */
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interno.h"

/* ---- Percorso -------------------------------------------------------------- */

/* Millesimi interi -> "45.455", "-0.005": niente printf con %f (dipenderebbe
 * dalla lingua del sistema, in italiano scriverebbe la virgola). Il segno si
 * scrive a parte, cosi' i valori tra -0.999 e 0 non lo perdono. */
static void scrivi_millesimi(char *out, size_t cap, int32_t v)
{
    int64_t a = v;
    const char *segno = "";
    if (a < 0) {
        segno = "-";
        a = -a;
    }
    snprintf(out, cap, "%s%lld.%03lld", segno, (long long)(a / 1000), (long long)(a % 1000));
}

int ph_meteo_percorso(int32_t lat_millesimi, int32_t lon_millesimi, char *out, size_t capacita, PhErrore *e)
{
    if (out == NULL || capacita == 0)
        return ph_errore(e, "meteo: buffer del percorso non valido");
    out[0] = '\0';
    /* limiti e passo di 5 millesimi (circa 500 m): altro e' un errore */
    if (lat_millesimi < -90000 || lat_millesimi > 90000 || lon_millesimi < -180000 || lon_millesimi > 180000 ||
        lat_millesimi % 5 != 0 || lon_millesimi % 5 != 0)
        return ph_errore(e, "meteo: posizione non valida");
    char la[32], lo[32];
    scrivi_millesimi(la, sizeof la, lat_millesimi);
    scrivi_millesimi(lo, sizeof lo, lon_millesimi);
    int r = snprintf(out, capacita,
                     "%s?latitude=%s&longitude=%s"
                     "&current=temperature_2m,apparent_temperature,relative_humidity_2m,precipitation,weather_code,"
                     "is_day,wind_speed_10m"
                     "&hourly=temperature_2m,weather_code,is_day,precipitation_probability"
                     "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
                     "sunrise,sunset"
                     "&timezone=auto&timeformat=unixtime&forecast_days=7",
                     PH_METEO_PERCORSO, la, lo);
    if (r < 0 || (size_t)r >= capacita) {
        out[0] = '\0';
        return ph_errore(e, "meteo: percorso troppo lungo per il buffer");
    }
    return 0;
}

/* ---- Lettore JSON ---------------------------------------------------------- */

#define PROF_MAX 16
#define NODI_MAX 20000
#define STR_MAX 4096
#define NUM_MAX 64

enum { T_NULL, T_BOOL, T_NUM, T_STR, T_ARR, T_OBJ };

typedef struct {
    int tipo;
    int32_t primo;     /* primo figlio (array/oggetto), -1 se nessuno */
    int32_t prossimo;  /* fratello successivo, -1 se ultimo */
    const char *chiave; /* nel pool, NUL finale; NULL per gli elementi di array */
    const char *str;    /* per le stringhe: il testo decodificato (nel pool) */
    double num;         /* numeri; per i booleani 1 = true */
} Nodo;

typedef struct {
    const char *s;
    size_t n, i;
    Nodo *nodi;
    size_t n_nodi, cap_nodi;
    char *pool;
    size_t pool_n, pool_cap;
    locale_t loc;
} Ana;

static locale_t g_loc_c;
static pthread_once_t g_loc_once = PTHREAD_ONCE_INIT;
/* Lingua "C" per strtod_l: la lingua del sistema potrebbe volere la virgola. */
static void loc_crea(void) { g_loc_c = newlocale(LC_ALL_MASK, "C", (locale_t)0); }

static int err_json(PhErrore *e) { return ph_errore(e, "risposta del servizio meteo non valida"); }

static void spazi(Ana *a)
{
    while (a->i < a->n && (a->s[a->i] == ' ' || a->s[a->i] == '\t' || a->s[a->i] == '\n' || a->s[a->i] == '\r'))
        a->i++;
}

static int32_t nodo_nuovo(Ana *a, int tipo, const char *chiave)
{
    if (a->n_nodi >= NODI_MAX)
        return -1;
    if (a->n_nodi == a->cap_nodi) {
        size_t nc = a->cap_nodi ? a->cap_nodi * 2 : 256;
        if (nc > NODI_MAX)
            nc = NODI_MAX;
        Nodo *p = realloc(a->nodi, nc * sizeof *p);
        if (p == NULL)
            return -1;
        a->nodi = p;
        a->cap_nodi = nc;
    }
    Nodo *n = &a->nodi[a->n_nodi];
    n->tipo = tipo;
    n->primo = -1;
    n->prossimo = -1;
    n->chiave = chiave;
    n->str = NULL;
    n->num = 0;
    return (int32_t)a->n_nodi++;
}

static int esa(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int leggi_u4(Ana *a, unsigned *v)
{
    if (a->n - a->i < 4)
        return -1;
    unsigned x = 0;
    for (int k = 0; k < 4; k++) {
        int d = esa((unsigned char)a->s[a->i + (size_t)k]);
        if (d < 0)
            return -1;
        x = x * 16 + (unsigned)d;
    }
    a->i += 4;
    *v = x;
    return 0;
}

/* Scrive un punto Unicode in UTF-8 nel pool (al massimo 4 byte). */
static void utf8(Ana *a, unsigned cp)
{
    char *p = a->pool + a->pool_n;
    if (cp < 0x80) {
        p[0] = (char)cp;
        a->pool_n += 1;
    } else if (cp < 0x800) {
        p[0] = (char)(0xC0 | (cp >> 6));
        p[1] = (char)(0x80 | (cp & 0x3F));
        a->pool_n += 2;
    } else if (cp < 0x10000) {
        p[0] = (char)(0xE0 | (cp >> 12));
        p[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        p[2] = (char)(0x80 | (cp & 0x3F));
        a->pool_n += 3;
    } else {
        p[0] = (char)(0xF0 | (cp >> 18));
        p[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        p[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        p[3] = (char)(0x80 | (cp & 0x3F));
        a->pool_n += 4;
    }
}

/* Legge una stringa (a->i sul primo carattere dopo le virgolette di apertura),
 * decodifica gli escape nel pool e la termina con NUL. Il pool ha n+8 byte e
 * una stringa decodificata e' sempre piu' corta della sua forma scritta, ma
 * lo spazio si controlla comunque prima di ogni scrittura. */
static int stringa(Ana *a, const char **out)
{
    size_t inizio = a->pool_n;
    size_t lung = 0;
    for (;;) {
        if (a->i >= a->n)
            return -1;
        unsigned char c = (unsigned char)a->s[a->i++];
        if (c == '"')
            break;
        if (c < 0x20)
            return -1;
        if (a->pool_cap - a->pool_n < 8 || lung >= STR_MAX)
            return -1;
        if (c != '\\') {
            a->pool[a->pool_n++] = (char)c;
            lung++;
            continue;
        }
        if (a->i >= a->n)
            return -1;
        char x = a->s[a->i++];
        switch (x) {
        case '"': a->pool[a->pool_n++] = '"'; break;
        case '\\': a->pool[a->pool_n++] = '\\'; break;
        case '/': a->pool[a->pool_n++] = '/'; break;
        case 'b': a->pool[a->pool_n++] = '\b'; break;
        case 'f': a->pool[a->pool_n++] = '\f'; break;
        case 'n': a->pool[a->pool_n++] = '\n'; break;
        case 'r': a->pool[a->pool_n++] = '\r'; break;
        case 't': a->pool[a->pool_n++] = '\t'; break;
        case 'u': {
            unsigned cp;
            if (leggi_u4(a, &cp) != 0)
                return -1;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                /* Coppia surrogata: se il seguito non e' valido, U+FFFD. */
                unsigned lo;
                if (a->n - a->i >= 6 && a->s[a->i] == '\\' && a->s[a->i + 1] == 'u') {
                    size_t salva = a->i;
                    a->i += 2;
                    if (leggi_u4(a, &lo) == 0 && lo >= 0xDC00 && lo <= 0xDFFF)
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else {
                        a->i = salva;
                        cp = 0xFFFD;
                    }
                } else
                    cp = 0xFFFD;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF)
                cp = 0xFFFD;
            if (cp == 0)
                cp = 0xFFFD; /* un NUL dentro la stringa la accorcerebbe nei confronti */
            utf8(a, cp);
            break;
        }
        default:
            return -1;
        }
        lung++;
    }
    if (a->pool_cap - a->pool_n < 1)
        return -1;
    a->pool[a->pool_n++] = '\0';
    *out = a->pool + inizio;
    return 0;
}

/* Numero JSON (grammatica stretta), convertito con la lingua "C". */
static int numero(Ana *a, double *v)
{
    size_t i = a->i;
    const char *s = a->s;
    size_t n = a->n;
    if (i < n && s[i] == '-')
        i++;
    if (i >= n)
        return -1;
    if (s[i] == '0')
        i++;
    else if (s[i] >= '1' && s[i] <= '9')
        while (i < n && s[i] >= '0' && s[i] <= '9')
            i++;
    else
        return -1;
    if (i < n && s[i] == '.') {
        i++;
        size_t c = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            i++;
            c++;
        }
        if (c == 0)
            return -1;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-'))
            i++;
        size_t c = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            i++;
            c++;
        }
        if (c == 0)
            return -1;
    }
    size_t len = i - a->i;
    if (len > NUM_MAX)
        return -1;
    char tmp[NUM_MAX + 1];
    memcpy(tmp, s + a->i, len);
    tmp[len] = '\0';
    *v = strtod_l(tmp, NULL, a->loc); /* inf (es. 1e999) lo scarta chi legge */
    a->i = i;
    return 0;
}

static int valore(Ana *a, int prof, const char *chiave, int32_t *idx);

static int contenitore(Ana *a, int prof, int tipo, const char *chiave, int32_t *idx)
{
    if (prof > PROF_MAX)
        return -1;
    int32_t me = nodo_nuovo(a, tipo, chiave);
    if (me < 0)
        return -1;
    char chiusura = tipo == T_OBJ ? '}' : ']';
    a->i++; /* '{' o '[' */
    spazi(a);
    if (a->i < a->n && a->s[a->i] == chiusura) {
        a->i++;
        *idx = me;
        return 0;
    }
    int32_t ultimo = -1;
    for (;;) {
        const char *k = NULL;
        spazi(a);
        if (tipo == T_OBJ) {
            if (a->i >= a->n || a->s[a->i] != '"')
                return -1;
            a->i++;
            if (stringa(a, &k) != 0)
                return -1;
            spazi(a);
            if (a->i >= a->n || a->s[a->i] != ':')
                return -1;
            a->i++;
        }
        int32_t f;
        if (valore(a, prof + 1, k, &f) != 0)
            return -1;
        if (ultimo < 0)
            a->nodi[me].primo = f;
        else
            a->nodi[ultimo].prossimo = f;
        ultimo = f;
        spazi(a);
        if (a->i >= a->n)
            return -1;
        char c = a->s[a->i++];
        if (c == chiusura)
            break;
        if (c != ',')
            return -1;
    }
    *idx = me;
    return 0;
}

static int valore(Ana *a, int prof, const char *chiave, int32_t *idx)
{
    spazi(a);
    if (a->i >= a->n)
        return -1;
    char c = a->s[a->i];
    if (c == '{')
        return contenitore(a, prof, T_OBJ, chiave, idx);
    if (c == '[')
        return contenitore(a, prof, T_ARR, chiave, idx);
    if (c == '"') {
        a->i++;
        const char *testo;
        if (stringa(a, &testo) != 0)
            return -1;
        int32_t id = nodo_nuovo(a, T_STR, chiave);
        if (id < 0)
            return -1;
        a->nodi[id].str = testo;
        *idx = id;
        return 0;
    }
    if (c == 't' || c == 'f' || c == 'n') {
        const char *lett = c == 't' ? "true" : c == 'f' ? "false" : "null";
        size_t l = strlen(lett);
        if (a->n - a->i < l || memcmp(a->s + a->i, lett, l) != 0)
            return -1;
        a->i += l;
        int32_t id = nodo_nuovo(a, c == 'n' ? T_NULL : T_BOOL, chiave);
        if (id < 0)
            return -1;
        a->nodi[id].num = c == 't' ? 1 : 0;
        *idx = id;
        return 0;
    }
    double v;
    if (numero(a, &v) != 0)
        return -1;
    int32_t id = nodo_nuovo(a, T_NUM, chiave);
    if (id < 0)
        return -1;
    a->nodi[id].num = v;
    *idx = id;
    return 0;
}

/* ---- Estrazione dei campi --------------------------------------------------- */

/* Figlio di un oggetto con quella chiave (il primo, se ripetuta); -1 se manca. */
static int32_t figlio(const Ana *a, int32_t obj, const char *chiave)
{
    if (obj < 0 || a->nodi[obj].tipo != T_OBJ)
        return -1;
    for (int32_t c = a->nodi[obj].primo; c >= 0; c = a->nodi[c].prossimo)
        if (a->nodi[c].chiave != NULL && strcmp(a->nodi[c].chiave, chiave) == 0)
            return c;
    return -1;
}

static int32_t tipato(const Ana *a, int32_t obj, const char *chiave, int tipo)
{
    int32_t c = figlio(a, obj, chiave);
    return (c >= 0 && a->nodi[c].tipo == tipo) ? c : -1;
}

/* Numero finito di un nodo (-1 = nessun nodo): altrimenti NAN. */
static double dbl(const Ana *a, int32_t nodo)
{
    if (nodo < 0 || a->nodi[nodo].tipo != T_NUM || !isfinite(a->nodi[nodo].num))
        return NAN;
    return a->nodi[nodo].num;
}

static int codice_di(double v)
{
    if (isnan(v) || v != floor(v) || v < 0 || v > 99)
        return -1;
    return (int)v;
}

static int giorno_di(double v)
{
    if (v == 0)
        return 0;
    if (v == 1)
        return 1;
    return -1;
}

/* Secondi Unix plausibili (entro +-1e12) o falso. */
static bool tempo_di(double v, int64_t *out)
{
    if (isnan(v) || fabs(v) > 1e12)
        return false;
    *out = (int64_t)floor(v);
    return true;
}

static int32_t primo_elem(const Ana *a, int32_t arr)
{
    return arr >= 0 ? a->nodi[arr].primo : -1;
}

static int32_t avanti(const Ana *a, int32_t c) { return c >= 0 ? a->nodi[c].prossimo : -1; }

static void estrai_adesso(const Ana *a, int32_t cur, int64_t adesso, PhMeteoAdesso *o)
{
    int64_t t;
    o->ora = tempo_di(dbl(a, figlio(a, cur, "time")), &t) ? t : adesso;
    o->temperatura = dbl(a, figlio(a, cur, "temperature_2m"));
    o->percepita = dbl(a, figlio(a, cur, "apparent_temperature"));
    o->umidita = dbl(a, figlio(a, cur, "relative_humidity_2m"));
    o->precipitazione = dbl(a, figlio(a, cur, "precipitation"));
    o->vento = dbl(a, figlio(a, cur, "wind_speed_10m"));
    o->codice = codice_di(dbl(a, figlio(a, cur, "weather_code")));
    o->giorno = giorno_di(dbl(a, figlio(a, cur, "is_day")));
}

static void estrai_ore(const Ana *a, int32_t h, int64_t adesso, PhMeteoPrevisione *out)
{
    /* Un cursore per array, avanzati insieme: l'accesso resta lineare anche
     * con array enormi (niente ricerche dell'elemento k-esimo). */
    int32_t ct = primo_elem(a, tipato(a, h, "time", T_ARR));
    int32_t cT = primo_elem(a, tipato(a, h, "temperature_2m", T_ARR));
    int32_t cC = primo_elem(a, tipato(a, h, "weather_code", T_ARR));
    int32_t cD = primo_elem(a, tipato(a, h, "is_day", T_ARR));
    int32_t cP = primo_elem(a, tipato(a, h, "precipitation_probability", T_ARR));
    for (; ct >= 0 && out->n_ore < PH_METEO_ORE_MAX;
         ct = avanti(a, ct), cT = avanti(a, cT), cC = avanti(a, cC), cD = avanti(a, cD), cP = avanti(a, cP)) {
        int64_t ora;
        if (!tempo_di(dbl(a, ct), &ora))
            continue;
        if (ora + 3600 <= adesso) /* ora gia' finita */
            continue;
        PhMeteoOra *o = &out->ore[out->n_ore++];
        o->ora = ora;
        o->temperatura = dbl(a, cT);
        o->codice = codice_di(dbl(a, cC));
        o->giorno = giorno_di(dbl(a, cD));
        o->prob_pioggia = dbl(a, cP);
    }
}

static void estrai_giorni(const Ana *a, int32_t d, PhMeteoPrevisione *out)
{
    int32_t ct = primo_elem(a, tipato(a, d, "time", T_ARR));
    int32_t cC = primo_elem(a, tipato(a, d, "weather_code", T_ARR));
    int32_t cX = primo_elem(a, tipato(a, d, "temperature_2m_max", T_ARR));
    int32_t cN = primo_elem(a, tipato(a, d, "temperature_2m_min", T_ARR));
    int32_t cP = primo_elem(a, tipato(a, d, "precipitation_probability_max", T_ARR));
    int32_t cA = primo_elem(a, tipato(a, d, "sunrise", T_ARR));
    int32_t cS = primo_elem(a, tipato(a, d, "sunset", T_ARR));
    for (; ct >= 0 && out->n_giorni < PH_METEO_GIORNI_MAX; ct = avanti(a, ct), cC = avanti(a, cC), cX = avanti(a, cX),
                                                           cN = avanti(a, cN), cP = avanti(a, cP), cA = avanti(a, cA),
                                                           cS = avanti(a, cS)) {
        int64_t g;
        if (!tempo_di(dbl(a, ct), &g))
            continue;
        PhMeteoGiorno *o = &out->giorni[out->n_giorni++];
        o->giorno = g;
        o->codice = codice_di(dbl(a, cC));
        o->massima = dbl(a, cX);
        o->minima = dbl(a, cN);
        o->prob_pioggia = dbl(a, cP);
        int64_t t;
        o->alba = tempo_di(dbl(a, cA), &t) ? t : 0;
        o->tramonto = tempo_di(dbl(a, cS), &t) ? t : 0;
    }
}

/* Prepara il lettore e legge il documento (radice = oggetto). 0 / -1 (e riempito).
 * In ogni caso chi chiama deve poi chiamare ana_libera. */
static int ana_leggi(Ana *a, const char *json, size_t n, int32_t *radice, PhErrore *e)
{
    memset(a, 0, sizeof *a);
    pthread_once(&g_loc_once, loc_crea);
    if (g_loc_c == (locale_t)0)
        return ph_errore(e, "meteo: lingua C non disponibile");
    a->s = json;
    a->n = n;
    a->loc = g_loc_c;
    a->pool_cap = n + 8;
    a->pool = malloc(a->pool_cap);
    if (a->pool == NULL)
        return ph_errore(e, "meteo: memoria esaurita");
    spazi(a);
    if (a->i >= a->n || a->s[a->i] != '{' || valore(a, 1, NULL, radice) != 0)
        return err_json(e);
    spazi(a);
    if (a->i != a->n) /* roba dopo il valore */
        return err_json(e);
    return 0;
}

static void ana_libera(Ana *a)
{
    free(a->nodi);
    free(a->pool);
}

int ph_meteo_analizza(const char *json, size_t n, int64_t adesso, PhMeteoPrevisione *out, PhErrore *e)
{
    if (out == NULL)
        return ph_errore(e, "meteo: argomenti non validi");
    memset(out, 0, sizeof *out);
    if (json == NULL || n == 0 || n > PH_METEO_RISPOSTA_MAX)
        return err_json(e);
    Ana a;
    int32_t radice = -1;
    int r = -1;
    if (ana_leggi(&a, json, n, &radice, e) != 0)
        goto fine;
    int32_t cur = tipato(&a, radice, "current", T_OBJ);
    int32_t hor = tipato(&a, radice, "hourly", T_OBJ);
    int32_t dai = tipato(&a, radice, "daily", T_OBJ);
    if (cur < 0 && hor < 0 && dai < 0) {
        err_json(e);
        goto fine;
    }
    out->ricevuta = adesso;
    double off = dbl(&a, figlio(&a, radice, "utc_offset_seconds"));
    out->scarto_utc_s = (!isnan(off) && fabs(off) <= 86400.0) ? (int)off : 0;
    out->adesso = (PhMeteoAdesso){.ora = adesso,
                                  .temperatura = NAN,
                                  .percepita = NAN,
                                  .umidita = NAN,
                                  .precipitazione = NAN,
                                  .vento = NAN,
                                  .codice = -1,
                                  .giorno = -1};
    if (cur >= 0)
        estrai_adesso(&a, cur, adesso, &out->adesso);
    if (hor >= 0)
        estrai_ore(&a, hor, adesso, out);
    if (dai >= 0)
        estrai_giorni(&a, dai, out);
    r = 0;
fine:
    ana_libera(&a);
    if (r != 0)
        memset(out, 0, sizeof *out);
    return r;
}

/* ---- Ricerca dei luoghi -------------------------------------------------------- */

/* Lunghezza (1-4) della sequenza UTF-8 VALIDA all'inizio di s, 0 se non lo e'
 * (byte di continuazione isolati, forme troppo lunghe, surrogati, oltre
 * U+10FFFF, sequenza troncata). Il punto in *cp. */
static int utf8_seq(const unsigned char *s, size_t n, uint32_t *cp)
{
    if (n == 0)
        return 0;
    unsigned c = s[0];
#define CONT(k) (n > (size_t)(k) && (s[k] & 0xC0) == 0x80)
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if (c >= 0xC2 && c <= 0xDF && CONT(1)) {
        *cp = ((c & 0x1Fu) << 6) | (s[1] & 0x3Fu);
        return 2;
    }
    if (c >= 0xE0 && c <= 0xEF && CONT(1) && CONT(2)) {
        uint32_t v = ((c & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu);
        if (v < 0x800 || (v >= 0xD800 && v <= 0xDFFF))
            return 0;
        *cp = v;
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4 && CONT(1) && CONT(2) && CONT(3)) {
        uint32_t v = ((c & 0x07u) << 18) | ((s[1] & 0x3Fu) << 12) | ((s[2] & 0x3Fu) << 6) | (s[3] & 0x3Fu);
        if (v < 0x10000 || v > 0x10FFFF)
            return 0;
        *cp = v;
        return 4;
    }
#undef CONT
    return 0;
}

/* Caratteri invisibili di formato e di direzione (spazio a larghezza zero,
 * marcatori di direzione, isolamenti, BOM): in un nome ricevuto potrebbero
 * nascondere o riordinare il testo mostrato. */
static bool e_invisibile(uint32_t cp)
{
    return (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x2069) ||
           cp == 0xFEFF;
}

/* Caratteri di controllo: C0, DEL e C1. */
static bool e_controllo(uint32_t cp) { return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F); }

int ph_meteo_testo_ricerca(const char *testo, char *out, PhErrore *e)
{
    if (testo == NULL || out == NULL)
        return ph_errore(e, "ricerca: testo non valido");
    size_t n = strnlen(testo, 4096);
    if (n == 4096)
        return ph_errore(e, "ricerca: testo troppo lungo");
    size_t a = 0;
    while (a < n && testo[a] == ' ')
        a++;
    while (n > a && testo[n - 1] == ' ')
        n--;
    size_t len = n - a;
    if (len < 2)
        return ph_errore(e, "ricerca: scrivi almeno due caratteri");
    if (len > PH_METEO_RICERCA_TESTO_MAX)
        return ph_errore(e, "ricerca: testo troppo lungo");
    const unsigned char *s = (const unsigned char *)testo + a;
    for (size_t i = 0; i < len;) {
        uint32_t cp;
        int k = utf8_seq(s + i, len - i, &cp);
        if (k == 0 || e_controllo(cp))
            return ph_errore(e, "ricerca: il testo contiene caratteri non validi");
        i += (size_t)k;
    }
    memcpy(out, s, len);
    out[len] = '\0';
    return 0;
}

int ph_meteo_percorso_ricerca(const char *testo, const char *lingua, char *out, size_t capacita, PhErrore *e)
{
    if (out == NULL || capacita == 0)
        return ph_errore(e, "ricerca: buffer del percorso non valido");
    out[0] = '\0';
    if (lingua == NULL || strlen(lingua) != 2 || lingua[0] < 'a' || lingua[0] > 'z' || lingua[1] < 'a' || lingua[1] > 'z')
        return ph_errore(e, "ricerca: lingua non valida");
    char pulito[PH_METEO_RICERCA_TESTO_MAX + 1];
    if (ph_meteo_testo_ricerca(testo, pulito, e) != 0)
        return -1;
    /* Codifica %XX di TUTTO cio' che non e' lettera/cifra ASCII o -._~: nessun
     * byte dell'utente puo' diventare un separatore, un parametro in piu', un
     * ritorno a capo o un frammento. */
    static const char esa[] = "0123456789ABCDEF";
    char codificato[3 * PH_METEO_RICERCA_TESTO_MAX + 1];
    size_t k = 0;
    for (const char *p = pulito; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
            c == '_' || c == '~')
            codificato[k++] = (char)c;
        else {
            codificato[k++] = '%';
            codificato[k++] = esa[c >> 4];
            codificato[k++] = esa[c & 15];
        }
    }
    codificato[k] = '\0';
    int r = snprintf(out, capacita, "%s?name=%s&count=%d&language=%s&format=json", PH_METEO_GEO_PERCORSO, codificato,
                     PH_METEO_LUOGHI_MAX, lingua);
    if (r < 0 || (size_t)r >= capacita) {
        out[0] = '\0';
        return ph_errore(e, "ricerca: percorso troppo lungo per il buffer");
    }
    return 0;
}

/* Ripulisce una stringa ricevuta per lo schermo: tiene solo sequenze UTF-8
 * valide, toglie i caratteri di controllo e gli spazi ai lati, e si ferma
 * prima di spezzare un carattere (dst ha `cap` byte, NUL compreso). */
static void pulisci(const char *src, char *dst, size_t cap)
{
    size_t o = 0;
    size_t n = strlen(src);
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        int k = utf8_seq((const unsigned char *)src + i, n - i, &cp);
        if (k == 0) { /* byte non valido: si butta */
            i++;
            continue;
        }
        if (!e_controllo(cp) && !e_invisibile(cp) && !(cp == ' ' && o == 0)) {
            if (o + (size_t)k + 1 > cap)
                break;
            memcpy(dst + o, src + i, (size_t)k);
            o += (size_t)k;
        }
        i += (size_t)k;
    }
    while (o > 0 && dst[o - 1] == ' ')
        o--;
    dst[o] = '\0';
}

int ph_meteo_analizza_ricerca(const char *json, size_t len, PhMeteoLuogo *v, size_t massimo, size_t *n, PhErrore *e)
{
    if (n == NULL)
        return ph_errore(e, "meteo: argomenti non validi");
    *n = 0;
    if (v == NULL || massimo == 0)
        return ph_errore(e, "meteo: argomenti non validi");
    if (json == NULL || len == 0 || len > PH_METEO_RICERCA_RISPOSTA_MAX)
        return ph_errore(e, "risposta della ricerca dei luoghi non valida");
    Ana a;
    int32_t radice = -1;
    int r = -1;
    size_t trovati = 0;
    if (ana_leggi(&a, json, len, &radice, e) != 0) {
        ph_errore(e, "risposta della ricerca dei luoghi non valida");
        goto fine;
    }
    int32_t er = figlio(&a, radice, "error");
    if (er >= 0 && a.nodi[er].tipo == T_BOOL && a.nodi[er].num == 1) {
        ph_errore(e, "il servizio di ricerca ha risposto con un errore");
        goto fine;
    }
    int32_t res = figlio(&a, radice, "results");
    if (res >= 0 && a.nodi[res].tipo != T_NULL) {
        if (a.nodi[res].tipo != T_ARR) {
            ph_errore(e, "risposta della ricerca dei luoghi non valida");
            goto fine;
        }
        for (int32_t c = a.nodi[res].primo; c >= 0 && trovati < massimo; c = a.nodi[c].prossimo) {
            if (a.nodi[c].tipo != T_OBJ)
                continue;
            int32_t nn = tipato(&a, c, "name", T_STR);
            double la = dbl(&a, figlio(&a, c, "latitude"));
            double lo = dbl(&a, figlio(&a, c, "longitude"));
            if (nn < 0 || isnan(la) || isnan(lo) || la < -90.0 || la > 90.0 || lo < -180.0 || lo > 180.0)
                continue; /* senza i campi obbligatori, o fuori limite */
            PhMeteoLuogo L;
            memset(&L, 0, sizeof L);
            pulisci(a.nodi[nn].str, L.nome, sizeof L.nome);
            if (L.nome[0] == '\0')
                continue;
            /* Arrotondato al centesimo PIU' VICINO (lround: a meta' si va via dallo zero, anche per i negativi). */
            L.lat_centesimi = (int32_t)lround(la * 100.0);
            L.lon_centesimi = (int32_t)lround(lo * 100.0);
            char admin[sizeof L.dettaglio], paese[sizeof L.dettaglio];
            admin[0] = paese[0] = '\0';
            int32_t ad = tipato(&a, c, "admin1", T_STR);
            int32_t pa = tipato(&a, c, "country", T_STR);
            if (ad >= 0)
                pulisci(a.nodi[ad].str, admin, sizeof admin);
            if (pa >= 0)
                pulisci(a.nodi[pa].str, paese, sizeof paese);
            char unito[2 * sizeof L.dettaglio + 4];
            snprintf(unito, sizeof unito, "%s%s%s", admin, (admin[0] && paese[0]) ? ", " : "", paese);
            pulisci(unito, L.dettaglio, sizeof L.dettaglio); /* tronca su confine di carattere */
            v[trovati++] = L;
        }
    }
    r = 0;
fine:
    ana_libera(&a);
    *n = r == 0 ? trovati : 0;
    return r;
}
