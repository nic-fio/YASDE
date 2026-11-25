/* Prove del pezzo meteo, tutte SENZA Internet: la connessione cifrata e' la
 * finta (tls_finto.c) che parla in chiaro con un server locale scritto qui.
 * Nessuna pausa «per dare tempo»: si aspettano semafori e condizioni con
 * scadenza. Argomento "concorrenza": solo le prove con thread (per TSan). */
#include <arpa/inet.h>
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../interno.h"
#include "tls_finto.h"

static int g_fallite;
#define VERIFICA(c)                                                                                                    \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            g_fallite++;                                                                                               \
            fprintf(stderr, "FALLITO %s:%d: %s\n", __FILE__, __LINE__, #c);                                            \
        }                                                                                                              \
    } while (0)

#define T0 1700000000LL /* «adesso» delle prove: 14 nov 2023, 22:13 UTC */

static struct timespec scade_tra(int ms)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += ms / 1000;
    t.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) {
        t.tv_sec++;
        t.tv_nsec -= 1000000000L;
    }
    return t;
}

/* ---- Orologio finto ------------------------------------------------------- */
static atomic_llong g_mono = 1000;
static int64_t f_mono(void) { return atomic_load(&g_mono); }
static int64_t f_unix(void) { return T0 + (atomic_load(&g_mono) - 1000); }

/* Scarica dall'host delle previsioni con il limite delle previsioni. */
static int http_prova(const char *percorso, const atomic_int *annulla, int64_t scadenza_ms, char **corpo, size_t *n,
                      PhErrore *e)
{
    return ph_meteo_http_scarica(PH_METEO_HOST, percorso, PH_METEO_RISPOSTA_MAX, annulla, scadenza_ms, corpo, n, e);
}

/* ---- Risposte d'esempio (valori finti, formato di Open-Meteo con unixtime) --- */
/* ore/giorni: quanti elementi nei due array «time». */
static char *risposta_esempio(int ore, int giorni, size_t *n)
{
    size_t cap = 64 * 1024 + (size_t)ore * 200 + (size_t)giorni * 300;
    char *b = malloc(cap);
    size_t o = 0;
#define ADD(...) o += (size_t)snprintf(b + o, cap - o, __VA_ARGS__)
    ADD("{\"latitude\":45.46,\"longitude\":9.19,\"generationtime_ms\":0.05,\"utc_offset_seconds\":3600,"
        "\"timezone\":\"Europe\\/Rome\",\"timezone_abbreviation\":\"GMT+1\",\"elevation\":120.0,"
        "\"current_units\":{\"time\":\"unixtime\",\"temperature_2m\":\"\\u00b0C\"},"
        "\"current\":{\"time\":%lld,\"interval\":900,\"temperature_2m\":8.4,\"apparent_temperature\":6.1,"
        "\"relative_humidity_2m\":81,\"precipitation\":0.2,\"weather_code\":61,\"is_day\":0,"
        "\"wind_speed_10m\":12.6},",
        T0);
    const char *campi[] = {"temperature_2m", "weather_code", "is_day", "precipitation_probability"};
    ADD("\"hourly\":{\"time\":[");
    for (int k = 0; k < ore; k++)
        ADD("%s%lld", k ? "," : "", 1699920000LL + 3600LL * k);
    ADD("]");
    for (int f = 0; f < 4; f++) {
        ADD(",\"%s\":[", campi[f]);
        for (int k = 0; k < ore; k++) {
            if (f == 0)
                ADD("%s%d.%d", k ? "," : "", (10 + k) / 2, (k % 2) * 5); /* 5.0 + k * 0.5, senza %f */
            else if (f == 1)
                ADD("%s%d", k ? "," : "", k % 100);
            else if (f == 2)
                ADD("%s%d", k ? "," : "", k % 2);
            else
                ADD("%s%d", k ? "," : "", k % 101);
        }
        ADD("]");
    }
    ADD("},\"daily\":{\"time\":[");
    for (int d = 0; d < giorni; d++)
        ADD("%s%lld", d ? "," : "", 1699916400LL + 86400LL * d);
    ADD("]");
    const char *dc[] = {"weather_code", "temperature_2m_max", "temperature_2m_min", "precipitation_probability_max",
                        "sunrise", "sunset"};
    for (int f = 0; f < 6; f++) {
        ADD(",\"%s\":[", dc[f]);
        for (int d = 0; d < giorni; d++) {
            long long t = 1699916400LL + 86400LL * d;
            if (f == 0)
                ADD("%s%d", d ? "," : "", d);
            else if (f == 1)
                ADD("%s%d.5", d ? "," : "", 10 + d);
            else if (f == 2)
                ADD("%s%d.5", d ? "," : "", 2 + d);
            else if (f == 3)
                ADD("%s%d", d ? "," : "", 10 * d);
            else if (f == 4)
                ADD("%s%lld", d ? "," : "", t + 7 * 3600);
            else
                ADD("%s%lld", d ? "," : "", t + 17 * 3600);
        }
        ADD("]");
    }
    ADD("}}");
#undef ADD
    *n = o;
    return b;
}

static int uguale(double a, double b) { return fabs(a - b) < 1e-9; }

/* Esegue analizza su una copia di dimensione ESATTA (cosi' AddressSanitizer
 * vede ogni lettura oltre la fine). */
static int analizza_str(const char *s, size_t n, PhMeteoPrevisione *out)
{
    char *c = malloc(n ? n : 1);
    if (n)
        memcpy(c, s, n);
    PhErrore e;
    memset(out, 0xAA, sizeof *out);
    int r = ph_meteo_analizza(c, n, T0, out, &e);
    free(c);
    return r;
}

static int e_zero(const PhMeteoPrevisione *p)
{
    PhMeteoPrevisione z;
    memset(&z, 0, sizeof z);
    return memcmp(p, &z, sizeof z) == 0;
}

/* ---- Percorso -------------------------------------------------------------- */
static void prova_percorso(void)
{
    char b[1024];
    PhErrore e;
    VERIFICA(ph_meteo_percorso(45455, 9190, b, sizeof b, &e) == 0);
    VERIFICA(strcmp(b, "/v1/forecast?latitude=45.455&longitude=9.190"
                       "&current=temperature_2m,apparent_temperature,relative_humidity_2m,precipitation,weather_code,"
                       "is_day,wind_speed_10m"
                       "&hourly=temperature_2m,weather_code,is_day,precipitation_probability"
                       "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
                       "sunrise,sunset&timezone=auto&timeformat=unixtime&forecast_days=7") == 0);
    /* segno e zeri: i valori tra -0.999 e 0 non perdono il segno, gli zeri non si perdono */
    VERIFICA(ph_meteo_percorso(-5, -5, b, sizeof b, &e) == 0 && strstr(b, "latitude=-0.005&longitude=-0.005&") != NULL);
    VERIFICA(ph_meteo_percorso(5, 5, b, sizeof b, &e) == 0 && strstr(b, "latitude=0.005&longitude=0.005&") != NULL);
    VERIFICA(ph_meteo_percorso(-995, 995, b, sizeof b, &e) == 0 && strstr(b, "latitude=-0.995&longitude=0.995&") != NULL);
    VERIFICA(ph_meteo_percorso(-1000, 1005, b, sizeof b, &e) == 0 && strstr(b, "latitude=-1.000&longitude=1.005&") != NULL);
    VERIFICA(ph_meteo_percorso(0, 0, b, sizeof b, &e) == 0 && strstr(b, "latitude=0.000&longitude=0.000&") != NULL);
    VERIFICA(ph_meteo_percorso(41895, 12495, b, sizeof b, &e) == 0 && strstr(b, "latitude=41.895&longitude=12.495&") != NULL);
    VERIFICA(ph_meteo_percorso(90000, -180000, b, sizeof b, &e) == 0 &&
             strstr(b, "latitude=90.000&longitude=-180.000&") != NULL);
    VERIFICA(ph_meteo_percorso(-90000, 180000, b, sizeof b, &e) == 0 &&
             strstr(b, "latitude=-90.000&longitude=180.000&") != NULL);
    VERIFICA(ph_meteo_percorso(-12340, 120010, b, sizeof b, &e) == 0 &&
             strstr(b, "latitude=-12.340&longitude=120.010&") != NULL);
    /* solo caratteri attesi (nient'altro) */
    for (const char *p = b; *p; p++)
        VERIFICA((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || strchr("_.,=&?/-", *p) != NULL);
    /* tutti i multipli di 5 vicino a zero e agli estremi riescono; i non multipli no */
    for (int v = -50; v <= 50; v++) {
        int r = ph_meteo_percorso(v, v, b, sizeof b, &e);
        VERIFICA(v % 5 == 0 ? r == 0 : (r == -1 && b[0] == '\0'));
    }
    for (int v = 89990; v <= 90010; v++)
        VERIFICA((ph_meteo_percorso(v, 0, b, sizeof b, &e) == 0) == (v % 5 == 0 && v <= 90000));
    for (int v = -180010; v <= -179990; v++)
        VERIFICA((ph_meteo_percorso(0, v, b, sizeof b, &e) == 0) == (v % 5 == 0 && v >= -180000));
    /* fuori campo e buffer piccolo */
    VERIFICA(ph_meteo_percorso(90005, 0, b, sizeof b, &e) == -1);
    VERIFICA(ph_meteo_percorso(-90005, 0, b, sizeof b, &e) == -1);
    VERIFICA(ph_meteo_percorso(0, -180005, b, sizeof b, &e) == -1);
    VERIFICA(ph_meteo_percorso(0, 180005, b, sizeof b, &e) == -1);
    VERIFICA(ph_meteo_percorso(4546, 919, b, sizeof b, &e) == -1); /* centesimi di prima: 4546 non e' multiplo di 5 */
    VERIFICA(ph_meteo_percorso(INT32_MIN, 0, b, sizeof b, &e) == -1);
    VERIFICA(ph_meteo_percorso(0, INT32_MAX, b, sizeof b, &e) == -1);
    VERIFICA(ph_meteo_percorso(45455, 9190, b, 50, &e) == -1 && b[0] == '\0');
    VERIFICA(ph_meteo_percorso(45455, 9190, NULL, 50, &e) == -1);
    VERIFICA(ph_meteo_percorso(45455, 9190, b, 0, &e) == -1);
}

/* ---- Analisi ---------------------------------------------------------------- */
static void prova_analisi_valida(void)
{
    size_t n;
    char *j = risposta_esempio(168, 7, &n);
    PhMeteoPrevisione *p = malloc(sizeof *p);
    VERIFICA(analizza_str(j, n, p) == 0);
    VERIFICA(p->scarto_utc_s == 3600);
    VERIFICA(p->ricevuta == T0);
    VERIFICA(p->adesso.ora == T0);
    VERIFICA(uguale(p->adesso.temperatura, 8.4) && uguale(p->adesso.percepita, 6.1));
    VERIFICA(uguale(p->adesso.umidita, 81) && uguale(p->adesso.precipitazione, 0.2));
    VERIFICA(uguale(p->adesso.vento, 12.6) && p->adesso.codice == 61 && p->adesso.giorno == 0);
    /* ore: la prima e' quella che contiene adesso (k = 22), al massimo 48 */
    VERIFICA(p->n_ore == PH_METEO_ORE_MAX);
    VERIFICA(p->ore[0].ora == 1699920000LL + 3600LL * 22);
    VERIFICA(uguale(p->ore[0].temperatura, 5.0 + 22 * 0.5) && p->ore[0].codice == 22 && p->ore[0].giorno == 0);
    VERIFICA(uguale(p->ore[0].prob_pioggia, 22));
    VERIFICA(p->ore[47].ora == 1699920000LL + 3600LL * 69 && p->ore[47].giorno == 1);
    VERIFICA(p->n_giorni == PH_METEO_GIORNI_MAX);
    VERIFICA(p->giorni[0].giorno == 1699916400LL && p->giorni[0].codice == 0);
    VERIFICA(uguale(p->giorni[3].massima, 13.5) && uguale(p->giorni[3].minima, 5.5));
    VERIFICA(uguale(p->giorni[3].prob_pioggia, 30));
    VERIFICA(p->giorni[3].alba == 1699916400LL + 86400LL * 3 + 7 * 3600);
    VERIFICA(p->giorni[3].tramonto == 1699916400LL + 86400LL * 3 + 17 * 3600);
    free(j);
    free(p);

    /* meno ore del previsto: solo quelle future; piu' giorni del previsto: 7 */
    j = risposta_esempio(30, 10, &n);
    p = malloc(sizeof *p);
    VERIFICA(analizza_str(j, n, p) == 0);
    VERIFICA(p->n_ore == 8 && p->n_giorni == 7); /* k = 22..29 */
    free(j);
    /* tutte le ore passate: nessuna ora, ma la risposta e' valida */
    j = risposta_esempio(10, 2, &n);
    VERIFICA(analizza_str(j, n, p) == 0 && p->n_ore == 0 && p->n_giorni == 2);
    free(j);
    free(p);
}

typedef struct {
    const char *json;
    int ok; /* 1 = deve riuscire */
} Caso;

static void prova_analisi_rotta(void)
{
    PhMeteoPrevisione *p = malloc(sizeof *p);
    const char *u = "\"current\":{\"temperature_2m\":1.5}";
    char tmp[2048];
    Caso casi[] = {
        {"", 0},
        {" ", 0},
        {"{", 0},
        {"[]", 0},
        {"null", 0},
        {"5", 0},
        {"{}", 0},
        {"{\"current\":5}", 0},                /* sezione del tipo sbagliato = nessuna sezione utile */
        {"{\"hourly\":[],\"daily\":null}", 0},
        {"{\"current\":{}} x", 0},             /* roba dopo il valore */
        {"{\"current\":{},}", 0},              /* virgola finale */
        {"{\"current\":{\"a\":}}", 0},
        {"{\"current\":{\"a\" 1}}", 0},
        {"{\"current\":{a:1}}", 0},
        {"{\"current\":{\"a\":01}}", 0},
        {"{\"current\":{\"a\":1.}}", 0},
        {"{\"current\":{\"a\":-}}", 0},
        {"{\"current\":{\"a\":.5}}", 0},
        {"{\"current\":{\"a\":1e}}", 0},
        {"{\"current\":{\"a\":+1}}", 0},
        {"{\"current\":{\"a\":NaN}}", 0},
        {"{\"current\":{\"a\":nul}}", 0},
        {"{\"current\":{\"a\":tru}}", 0},
        {"{\"current\":{\"a\":\"\\x\"}}", 0},   /* escape sconosciuto */
        {"{\"current\":{\"a\":\"\\u12\"}}", 0}, /* \u troncato */
        {"{\"current\":{\"a\":\"\\uZZZZ\"}}", 0},
        {"{\"current\":{\"a\":\"abc}}", 0},     /* stringa non chiusa */
        {"{\"current\":{\"a\":[1,2}}", 0},
        {"{\"current\":{\"a\":[1 2]}}", 0},
        {"{\"current\":{\"temperature_2m\":1.5}", 0}, /* troncato */
        {"{\"current\":{\"temperature_2m\":1.", 0},
        /* validi */
        {"{\"current\":{}}", 1},
        {"{\"current\":{\"temperature_2m\":1.5}}", 1},
        {"{\"curr\\u0065nt\":{\"temperature_2m\":1.5}}", 1},
        {"{\"current\":{\"a\":\"\\ud83d\\ude00 \\ud83d x \\ude00\\u0000\"}}", 1}, /* surrogati, NUL */
        {"  {\n\t\"hourly\" : { } }\r\n", 1},
        {"{\"daily\":{\"time\":[]}}", 1},
    };
    for (size_t i = 0; i < sizeof casi / sizeof *casi; i++) {
        int r = analizza_str(casi[i].json, strlen(casi[i].json), p);
        if (casi[i].ok) {
            VERIFICA(r == 0);
        } else {
            VERIFICA(r == -1 && e_zero(p));
        }
        if ((r == 0) != casi[i].ok)
            fprintf(stderr, "  caso %zu: %s\n", i, casi[i].json);
    }

    /* contenuto: valori non noti, tipi sbagliati, duplicati */
    snprintf(tmp, sizeof tmp,
             "{\"utc_offset_seconds\":\"x\",\"current\":{\"time\":null,\"temperature_2m\":\"abc\","
             "\"apparent_temperature\":null,\"relative_humidity_2m\":true,\"precipitation\":[1],"
             "\"weather_code\":3.5,\"is_day\":2,\"wind_speed_10m\":1e999}}");
    VERIFICA(analizza_str(tmp, strlen(tmp), p) == 0);
    VERIFICA(isnan(p->adesso.temperatura) && isnan(p->adesso.percepita) && isnan(p->adesso.umidita));
    VERIFICA(isnan(p->adesso.precipitazione) && isnan(p->adesso.vento));
    VERIFICA(p->adesso.codice == -1 && p->adesso.giorno == -1 && p->adesso.ora == T0 && p->scarto_utc_s == 0);
    snprintf(tmp, sizeof tmp, "{\"utc_offset_seconds\":1e12,\"current\":{\"weather_code\":100,\"is_day\":1}}");
    VERIFICA(analizza_str(tmp, strlen(tmp), p) == 0 && p->scarto_utc_s == 0 && p->adesso.codice == -1 &&
             p->adesso.giorno == 1);
    snprintf(tmp, sizeof tmp,
             "{\"utc_offset_seconds\":-7200,\"current\":{\"temperature_2m\":1,\"temperature_2m\":2,"
             "\"weather_code\":99,\"time\":1700000123,\"wind_speed_10m\":-1.25E+1}}");
    VERIFICA(analizza_str(tmp, strlen(tmp), p) == 0 && p->scarto_utc_s == -7200);
    VERIFICA(uguale(p->adesso.temperatura, 1) && p->adesso.codice == 99 && p->adesso.ora == 1700000123LL);
    VERIFICA(uguale(p->adesso.vento, -12.5));
    /* senza «current»: adesso tutto non noto */
    snprintf(tmp, sizeof tmp, "{%s}", "\"daily\":{\"time\":[1699916400]}");
    VERIFICA(analizza_str(tmp, strlen(tmp), p) == 0 && isnan(p->adesso.temperatura) && p->adesso.codice == -1 &&
             p->n_giorni == 1 && isnan(p->giorni[0].massima) && p->giorni[0].alba == 0 && p->giorni[0].codice == -1);

    /* array di lunghezze diverse: corti = non noti, lunghi = eccedenza ignorata, null e stringhe nel time saltati */
    snprintf(tmp, sizeof tmp,
             "{\"hourly\":{\"time\":[1700000000,null,\"x\",1700003600,1700007200],\"temperature_2m\":[1.5,2.5],"
             "\"weather_code\":[1,2,3,4,5,6,7,8,9],\"is_day\":[1],\"precipitation_probability\":\"no\"}}");
    VERIFICA(analizza_str(tmp, strlen(tmp), p) == 0 && p->n_ore == 3);
    VERIFICA(p->ore[0].ora == 1700000000LL && uguale(p->ore[0].temperatura, 1.5) && p->ore[0].codice == 1 &&
             p->ore[0].giorno == 1 && isnan(p->ore[0].prob_pioggia));
    VERIFICA(p->ore[1].ora == 1700003600LL && isnan(p->ore[1].temperatura) && p->ore[1].codice == 4 &&
             p->ore[1].giorno == -1); /* le posizioni 1 e 2 del time (null, "x") occupano il loro indice */
    VERIFICA(p->ore[2].ora == 1700007200LL && p->ore[2].codice == 5);
    /* niente «time»: nessuna ora, ma la sezione c'e' */
    snprintf(tmp, sizeof tmp, "{\"hourly\":{\"temperature_2m\":[1,2,3]}}");
    VERIFICA(analizza_str(tmp, strlen(tmp), p) == 0 && p->n_ore == 0);
    (void)u;

    /* profondita': 14 array annidati dentro "current" vanno, 15 no */
    for (int m = 14; m <= 15; m++) {
        char *g = malloc(256);
        size_t o = (size_t)snprintf(g, 256, "{\"current\":{\"x\":");
        for (int k = 0; k < m; k++)
            g[o++] = '[';
        g[o++] = '1';
        for (int k = 0; k < m; k++)
            g[o++] = ']';
        o += (size_t)snprintf(g + o, 256 - o, "}}");
        int r = analizza_str(g, o, p);
        VERIFICA(m == 14 ? r == 0 : (r == -1 && e_zero(p)));
        free(g);
    }
    /* parentesi in quantita' enorme: nessun overflow di stack */
    {
        size_t n = 400000;
        char *g = malloc(n);
        memset(g, '[', n);
        g[0] = '{';
        VERIFICA(analizza_str(g, n, p) == -1 && e_zero(p));
        free(g);
    }
    /* troppi nodi */
    {
        size_t n = 8 + 2 * 30000;
        char *g = malloc(n + 64);
        size_t o = (size_t)snprintf(g, 64, "{\"hourly\":{\"time\":[");
        for (int k = 0; k < 30000; k++) {
            g[o++] = '1';
            g[o++] = ',';
        }
        o += (size_t)snprintf(g + o, 64, "1]}}");
        VERIFICA(analizza_str(g, o, p) == -1 && e_zero(p));
        free(g);
    }
    /* stringa troppo lunga, numero troppo lungo */
    {
        char *g = malloc(8000);
        size_t o = (size_t)snprintf(g, 100, "{\"current\":{\"a\":\"");
        memset(g + o, 'a', 5000);
        o += 5000;
        o += (size_t)snprintf(g + o, 100, "\"}}");
        VERIFICA(analizza_str(g, o, p) == -1 && e_zero(p));
        o = (size_t)snprintf(g, 100, "{\"current\":{\"a\":1");
        memset(g + o, '0', 100);
        o += 100;
        o += (size_t)snprintf(g + o, 100, "}}");
        VERIFICA(analizza_str(g, o, p) == -1 && e_zero(p));
        free(g);
    }
    /* dimensione: esattamente il massimo passa (spazi in coda), un byte in piu' no */
    {
        size_t n = PH_METEO_RISPOSTA_MAX;
        char *g = malloc(n + 1);
        memset(g, ' ', n + 1);
        memcpy(g, "{\"current\":{}}", 14);
        VERIFICA(analizza_str(g, n, p) == 0);
        VERIFICA(analizza_str(g, n + 1, p) == -1 && e_zero(p));
        free(g);
    }
    PhErrore e;
    VERIFICA(ph_meteo_analizza(NULL, 5, T0, p, &e) == -1);
    VERIFICA(ph_meteo_analizza("{}", 2, T0, NULL, &e) == -1);
    free(p);
}

/* Generatore pseudo-casuale riproducibile. */
static uint64_t g_seme = 0x9E3779B97F4A7C15ULL;
static uint32_t alea(void)
{
    g_seme ^= g_seme << 13;
    g_seme ^= g_seme >> 7;
    g_seme ^= g_seme << 17;
    return (uint32_t)(g_seme >> 16);
}

/* Byte a caso e mutazioni di una risposta vera: con qualunque sequenza non
 * deve mai leggere o scrivere fuori dai buffer (lo vede AddressSanitizer) e
 * quando riesce deve rispettare i limiti. */
static void prova_fuzz(void)
{
    size_t n0;
    char *base = risposta_esempio(30, 3, &n0);
    PhMeteoPrevisione *p = malloc(sizeof *p);
    for (int it = 0; it < 30000; it++) {
        size_t n = n0;
        char *c = malloc(n0 + 64);
        memcpy(c, base, n0);
        switch (alea() % 6) {
        case 0: /* byte cambiati */
            for (unsigned k = alea() % 8 + 1; k > 0; k--)
                c[alea() % n] = (char)alea();
            break;
        case 1: /* troncata */
            n = alea() % n0;
            break;
        case 2: /* cancellazione di un tratto */ {
            size_t a = alea() % n0, l = alea() % 40;
            if (a + l > n0)
                l = n0 - a;
            memmove(c + a, c + a + l, n0 - a - l);
            n = n0 - l;
            break;
        }
        case 3: /* inserzione di caratteri di struttura */ {
            const char *s = "{}[]\",:\\u-.eE0123456789ntf";
            size_t a = alea() % n0;
            memmove(c + a + 1, c + a, n0 - a);
            c[a] = s[alea() % strlen(s)];
            n = n0 + 1;
            break;
        }
        case 4: /* tutto a caso, corto */
            n = alea() % 200;
            for (size_t k = 0; k < n; k++)
                c[k] = (char)alea();
            break;
        default: /* a caso ma fatto di caratteri JSON */ {
            const char *s = "{}[]\",:\\u-.eE0123456789ntrufals ";
            n = alea() % 300;
            for (size_t k = 0; k < n; k++)
                c[k] = s[alea() % strlen(s)];
            break;
        }
        }
        int r = analizza_str(c, n, p);
        free(c);
        if (r == 0) {
            VERIFICA(p->n_ore <= PH_METEO_ORE_MAX && p->n_giorni <= PH_METEO_GIORNI_MAX);
            VERIFICA(p->adesso.codice >= -1 && p->adesso.codice <= 99);
            VERIFICA(p->adesso.giorno >= -1 && p->adesso.giorno <= 1);
        } else {
            VERIFICA(e_zero(p));
        }
    }
    free(p);
    free(base);
}

/* ---- Server finto ---------------------------------------------------------------- */
enum {
    M_OK, M_SPEZZATA, M_CHUNKED, M_CL_GRANDE, M_CHUNK_GRANDE, M_CHUNK_SOMMA, M_SENZA_LUNG, M_SENZA_LUNG_GRANDE,
    M_SILENZIO, M_LENTA, M_TRONCATA, M_CODICE, M_INTEST_GRANDI, M_CHUNK_ROTTO, M_CHUNK_SENZA_FINE, M_DOPPIA_CL,
    M_TE_GZIP, M_STATO_BRUTTO, M_HTTP10, M_CHUNK_E_LUNG, M_CHUNK_TRONCATO
};

static struct {
    int ascolto, porta, pipe_stop[2];
    pthread_t th;
    atomic_int modo, codice, conn;
    sem_t richiesta, inviato;
    pthread_mutex_t mu;
    char ultima[4096];
    char *corpo;
    size_t n_corpo;
    atomic_int modo_ric;                /* modo per le richieste di ricerca (/v1/search) */
    _Atomic(const char *) ric_dati;     /* risposta della ricerca (cambiabile a server fermo) */
    atomic_size_t ric_n;
} S;

/* Risposta tipica della ricerca (valori finti, campi come nella documentazione). */
static const char RIC_TIPICA[] =
    "{\"results\":[{\"id\":1,\"name\":\"Milano\",\"latitude\":45.46427,\"longitude\":9.18951,\"elevation\":122.0,"
    "\"feature_code\":\"PPLA\",\"country_code\":\"IT\",\"admin1_id\":3174618,\"timezone\":\"Europe/Rome\","
    "\"population\":1371498,\"country_id\":3175395,\"country\":\"Italia\",\"admin1\":\"Lombardia\"},"
    "{\"id\":2,\"name\":\"Milano\",\"latitude\":40.1,\"longitude\":-75.2,\"country\":\"Stati Uniti\","
    "\"admin1\":\"Texas\"},"
    "{\"id\":3,\"name\":\"Milan\",\"latitude\":-33.9,\"longitude\":151.2}],\"generationtime_ms\":0.6}";

static void invia(int fd, const void *d, size_t n, size_t pezzo)
{
    const char *p = d;
    while (n > 0) {
        size_t k = n < pezzo ? n : pezzo;
        ssize_t r = send(fd, p, k, MSG_NOSIGNAL);
        if (r < 0)
            return;
        p += r;
        n -= (size_t)r;
    }
}

static void invia_f(int fd, const char *f, ...) __attribute__((format(printf, 2, 3)));
static void invia_f(int fd, const char *f, ...)
{
    char b[1024];
    va_list ap;
    va_start(ap, f);
    int n = vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    invia(fd, b, (size_t)n, 4096);
}

/* Aspetta che il client chiuda (o la fine delle prove), al massimo 8 s. */
static void attendi_chiusura(int fd)
{
    int64_t fine = ph_meteo_ms() + 8000;
    for (;;) {
        struct pollfd p[2] = {{.fd = fd, .events = POLLIN}, {.fd = S.pipe_stop[0], .events = POLLIN}};
        int64_t resto = fine - ph_meteo_ms();
        if (resto <= 0 || poll(p, 2, (int)resto) <= 0 || p[1].revents)
            return;
        char b[256];
        ssize_t r = recv(fd, b, sizeof b, 0);
        if (r <= 0)
            return;
    }
}

static void servi(int fd, int modo, int codice, const char *c, size_t n)
{
    switch (modo) {
    case M_OK:
    case M_SPEZZATA:
        invia_f(fd, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", n);
        invia(fd, c, n, modo == M_SPEZZATA ? 3 : 65536);
        break;
    case M_HTTP10:
        invia_f(fd, "HTTP/1.0 200 OK\r\ncontent-length:  %zu \r\n\r\n", n);
        invia(fd, c, n, 65536);
        break;
    case M_CHUNKED: {
        invia_f(fd, "HTTP/1.1 200 OK\r\ntransfer-encoding: Chunked\r\nContent-Type: application/json\r\n\r\n");
        static const size_t dim[] = {1, 7, 100, 1000, 3};
        size_t o = 0;
        for (int k = 0; o < n; k++) {
            size_t l = dim[k % 5];
            if (l > n - o)
                l = n - o;
            invia_f(fd, k % 2 ? "%zx;ext=1\r\n" : "%zX\r\n", l);
            invia(fd, c + o, l, 5);
            invia(fd, "\r\n", 2, 2);
            o += l;
        }
        invia_f(fd, "0\r\nX-Coda: a\r\n\r\n");
        break;
    }
    case M_CHUNK_SENZA_FINE: /* chiude dopo l'ultimo chunk, senza la riga vuota finale */
        invia_f(fd, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n%zx\r\n", n);
        invia(fd, c, n, 65536);
        invia_f(fd, "\r\n0\r\n");
        break;
    case M_SENZA_LUNG:
        invia_f(fd, "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");
        invia(fd, c, n, 65536);
        break;
    case M_CL_GRANDE:
        invia_f(fd, "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\nabcdef", PH_METEO_RISPOSTA_MAX + 1);
        attendi_chiusura(fd);
        break;
    case M_CHUNK_GRANDE:
        invia_f(fd, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n100000\r\nabcdef");
        attendi_chiusura(fd);
        break;
    case M_CHUNK_SOMMA: {
        invia_f(fd, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
        char *b = malloc(65536);
        memset(b, 'a', 65536);
        for (int k = 0; k < 10; k++) {
            invia_f(fd, "10000\r\n");
            invia(fd, b, 65536, 65536);
            invia(fd, "\r\n", 2, 2);
        }
        free(b);
        break;
    }
    case M_SENZA_LUNG_GRANDE: {
        invia_f(fd, "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");
        char *b = malloc(600000);
        memset(b, 'a', 600000);
        invia(fd, b, 600000, 65536);
        free(b);
        break;
    }
    case M_SILENZIO:
        attendi_chiusura(fd);
        break;
    case M_LENTA:
        invia_f(fd, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n", n);
        invia(fd, c, n / 2, 65536);
        sem_post(&S.inviato);
        attendi_chiusura(fd);
        break;
    case M_TRONCATA:
        invia_f(fd, "HTTP/1.1 200 OK\r\nContent-Length: 1000\r\n\r\n0123456789");
        break;
    case M_CHUNK_TRONCATO:
        invia_f(fd, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n64\r\n0123456789");
        break;
    case M_CODICE:
        invia_f(fd, "HTTP/1.1 %d X\r\nLocation: http://esempio.invalid/\r\nContent-Length: 2\r\n\r\n{}", codice);
        break;
    case M_INTEST_GRANDI: {
        invia_f(fd, "HTTP/1.1 200 OK\r\n");
        for (int k = 0; k < 400; k++)
            invia_f(fd, "X-Riempitivo-%d: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n", k);
        invia_f(fd, "Content-Length: 2\r\n\r\n{}");
        break;
    }
    case M_CHUNK_ROTTO:
        invia_f(fd, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n{}\r\n0\r\n\r\n");
        break;
    case M_DOPPIA_CL:
        invia_f(fd, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}");
        break;
    case M_TE_GZIP:
        invia_f(fd, "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n2\r\n{}\r\n0\r\n\r\n");
        break;
    case M_CHUNK_E_LUNG:
        invia_f(fd, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 2\r\n\r\n2\r\n{}\r\n0\r\n\r\n");
        break;
    case M_STATO_BRUTTO:
        invia_f(fd, "HTTX sbagliato\r\n\r\n{}");
        break;
    default:
        break;
    }
}

static void *server_thread(void *arg)
{
    (void)arg;
    for (;;) {
        struct pollfd p[2] = {{.fd = S.ascolto, .events = POLLIN}, {.fd = S.pipe_stop[0], .events = POLLIN}};
        if (poll(p, 2, -1) < 0)
            continue;
        if (p[1].revents)
            return NULL;
        int fd = accept(S.ascolto, NULL, NULL);
        if (fd < 0)
            continue;
        atomic_fetch_add(&S.conn, 1);
        char req[4096];
        size_t n = 0;
        int64_t fine = ph_meteo_ms() + 3000;
        while (n < sizeof req - 1) {
            struct pollfd q = {.fd = fd, .events = POLLIN};
            int64_t resto = fine - ph_meteo_ms();
            if (resto <= 0 || poll(&q, 1, (int)resto) <= 0)
                break;
            ssize_t r = recv(fd, req + n, sizeof req - 1 - n, 0);
            if (r <= 0)
                break;
            n += (size_t)r;
            req[n] = '\0';
            if (strstr(req, "\r\n\r\n"))
                break;
        }
        req[n] = '\0';
        pthread_mutex_lock(&S.mu);
        memcpy(S.ultima, req, n + 1);
        pthread_mutex_unlock(&S.mu);
        sem_post(&S.richiesta);
        if (strncmp(req, "GET " PH_METEO_GEO_PERCORSO, 4 + sizeof PH_METEO_GEO_PERCORSO - 1) == 0)
            servi(fd, atomic_load(&S.modo_ric), atomic_load(&S.codice), atomic_load(&S.ric_dati),
                  atomic_load(&S.ric_n));
        else
            servi(fd, atomic_load(&S.modo), atomic_load(&S.codice), S.corpo, S.n_corpo);
        close(fd);
    }
}

static void server_avvia(void)
{
    size_t n;
    S.corpo = risposta_esempio(168, 7, &n);
    S.n_corpo = n;
    atomic_store(&S.ric_dati, RIC_TIPICA);
    atomic_store(&S.ric_n, sizeof RIC_TIPICA - 1);
    pthread_mutex_init(&S.mu, NULL);
    sem_init(&S.richiesta, 0, 0);
    sem_init(&S.inviato, 0, 0);
    S.ascolto = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int uno = 1;
    setsockopt(S.ascolto, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof uno);
    struct sockaddr_in a = {.sin_family = AF_INET};
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(S.ascolto, (struct sockaddr *)&a, sizeof a) != 0 || listen(S.ascolto, 16) != 0) {
        perror("server finto");
        exit(2);
    }
    socklen_t l = sizeof a;
    getsockname(S.ascolto, (struct sockaddr *)&a, &l);
    S.porta = ntohs(a.sin_port);
    if (pipe(S.pipe_stop) != 0)
        exit(2);
    pthread_create(&S.th, NULL, server_thread, NULL);
    atomic_store(&ph_tls_finto_porta, S.porta);
    sem_init(&ph_tls_finto_in_apri, 0, 0);
}

static void server_ferma(void)
{
    if (write(S.pipe_stop[1], "x", 1) != 1)
        exit(2);
    pthread_join(S.th, NULL);
    close(S.ascolto);
    close(S.pipe_stop[0]);
    close(S.pipe_stop[1]);
    free(S.corpo);
}

/* Riporta tutto allo stato di partenza di una prova. */
static void ripristina(int modo, int codice)
{
    atomic_store(&S.modo, modo);
    atomic_store(&S.modo_ric, M_OK);
    atomic_store(&S.ric_dati, RIC_TIPICA);
    atomic_store(&S.ric_n, sizeof RIC_TIPICA - 1);
    atomic_store(&S.codice, codice);
    atomic_store(&S.conn, 0);
    sem_destroy(&S.richiesta);
    sem_init(&S.richiesta, 0, 0);
    sem_destroy(&S.inviato);
    sem_init(&S.inviato, 0, 0);
    sem_destroy(&ph_tls_finto_in_apri);
    sem_init(&ph_tls_finto_in_apri, 0, 0);
    atomic_store(&ph_tls_finto_trattieni, 0);
    atomic_store(&ph_tls_finto_rifiuta, 0);
    atomic_store(&ph_tls_finto_aperture, 0);
    atomic_store(&ph_tls_finto_connessioni, 0);
    atomic_store(&ph_tls_finto_ignora_annulla, 0);
    ph_tls_finto_memoria(NULL, 0, 0);
    atomic_store(&g_mono, 1000);
    ph_meteo_prova_limite_ms(0);
    ph_meteo_prova_avvii_ms(0); /* nessun intervallo minimo, salvo le prove che lo vogliono */
    ph_meteo_prova_pausa_ricerca_ms(0);
}

static int attendi_sem(sem_t *s, int ms)
{
    struct timespec t = scade_tra(ms);
    int r;
    while ((r = sem_timedwait(s, &t)) != 0 && errno == EINTR) {
    }
    return r == 0;
}

/* ---- HTTP -------------------------------------------------------------------------- */
static int scarica(int modo, int codice, int64_t limite_ms, char **corpo, size_t *n, PhErrore *e)
{
    ripristina(modo, codice);
    e->testo[0] = '\0';
    return http_prova("/v1/forecast?x=1", NULL, ph_meteo_ms() + limite_ms, corpo, n, e);
}

static int conta(const char *s, const char *ago)
{
    int c = 0;
    for (const char *p = s; (p = strstr(p, ago)) != NULL; p += strlen(ago))
        c++;
    return c;
}

static void prova_http(void)
{
    char *c = NULL;
    size_t n = 0;
    PhErrore e;
    int modi_ok[] = {M_OK, M_SPEZZATA, M_CHUNKED, M_SENZA_LUNG, M_HTTP10, M_CHUNK_SENZA_FINE};
    for (size_t i = 0; i < sizeof modi_ok / sizeof *modi_ok; i++) {
        c = NULL;
        VERIFICA(scarica(modi_ok[i], 0, 5000, &c, &n, &e) == 0);
        if (c != NULL) {
            VERIFICA(n == S.n_corpo && memcmp(c, S.corpo, n) == 0 && c[n] == '\0');
            free(c);
        } else
            fprintf(stderr, "  modo %d: %s\n", modi_ok[i], e.testo);
    }
    /* La richiesta: solo le intestazioni previste, niente altro. */
    VERIFICA(scarica(M_OK, 0, 5000, &c, &n, &e) == 0);
    free(c);
    char rich[4096];
    pthread_mutex_lock(&S.mu);
    memcpy(rich, S.ultima, sizeof rich);
    pthread_mutex_unlock(&S.mu);
    VERIFICA(strncmp(rich, "GET /v1/forecast?x=1 HTTP/1.1\r\n", 31) == 0);
    VERIFICA(strstr(rich, "\r\nHost: api.open-meteo.com\r\n") != NULL);
    VERIFICA(strstr(rich, "\r\nUser-Agent: YASDE/2.1\r\n") != NULL);
    VERIFICA(strstr(rich, "\r\nAccept: application/json\r\n") != NULL);
    VERIFICA(strstr(rich, "\r\nConnection: close\r\n\r\n") != NULL);
    VERIFICA(conta(rich, "\r\n") == 6); /* riga + 4 intestazioni + riga vuota: nient'altro */
    char host[128];
    int porta;
    ph_tls_finto_ultimo(host, sizeof host, &porta);
    VERIFICA(strcmp(host, PH_METEO_HOST) == 0 && porta == PH_METEO_PORTA);

    /* errori: tutti -1, nulla da liberare, frase senza dati del percorso */
    struct {
        int modo, codice;
        const char *frase; /* pezzo atteso nella frase, o NULL */
    } rotte[] = {
        {M_CL_GRANDE, 0, "troppo grande"},     {M_CHUNK_GRANDE, 0, "troppo grande"},
        {M_CHUNK_SOMMA, 0, "troppo grande"},   {M_SENZA_LUNG_GRANDE, 0, "troppo grande"},
        {M_TRONCATA, 0, "interrotto"},         {M_CHUNK_TRONCATO, 0, "interrotto"},
        {M_CODICE, 429, "codice 429"},         {M_CODICE, 500, "codice 500"},
        {M_CODICE, 301, "codice 301"},         {M_CODICE, 404, "codice 404"},
        {M_CODICE, 204, "codice 204"},         {M_INTEST_GRANDI, 0, "non valida"},
        {M_CHUNK_ROTTO, 0, "non valida"},      {M_DOPPIA_CL, 0, "non valida"},
        {M_TE_GZIP, 0, "non valida"},          {M_CHUNK_E_LUNG, 0, "non valida"},
        {M_STATO_BRUTTO, 0, "non valida"},
    };
    for (size_t i = 0; i < sizeof rotte / sizeof *rotte; i++) {
        c = (char *)0x1;
        int r = scarica(rotte[i].modo, rotte[i].codice, 5000, &c, &n, &e);
        VERIFICA(r == -1 && c == (char *)0x1);
        VERIFICA(rotte[i].frase == NULL || strstr(e.testo, rotte[i].frase) != NULL);
        VERIFICA(strstr(e.testo, "forecast") == NULL && strstr(e.testo, "x=1") == NULL);
        if (r == 0) {
            free(c);
        } else if (rotte[i].frase && strstr(e.testo, rotte[i].frase) == NULL)
            fprintf(stderr, "  modo %d/%d: frase «%s»\n", rotte[i].modo, rotte[i].codice, e.testo);
    }
    /* un 301 non si segue: una sola connessione */
    c = NULL;
    VERIFICA(scarica(M_CODICE, 301, 5000, &c, &n, &e) == -1);
    VERIFICA(attendi_sem(&S.richiesta, 2000));
    VERIFICA(atomic_load(&S.conn) == 1);

    /* scadenza: server muto o lento oltre il limite */
    int modi_lenti[] = {M_SILENZIO, M_LENTA};
    for (int i = 0; i < 2; i++) {
        int64_t t0 = ph_meteo_ms();
        VERIFICA(scarica(modi_lenti[i], 0, 300, &c, &n, &e) == -1);
        int64_t dt = ph_meteo_ms() - t0;
        VERIFICA(dt >= 250 && dt < 3000);
        VERIFICA(strstr(e.testo, "non risponde") != NULL);
    }
    /* già annullata: esce subito e NON apre nessuna connessione (il finto,
     * come quello vero, guarda l'annullamento prima di tutto). Si contano i
     * connect() del finto: nessuna attesa per «dare tempo» al server. */
    {
        atomic_int annulla = 1;
        ripristina(M_OK, 0);
        int64_t t0 = ph_meteo_ms();
        VERIFICA(http_prova("/x", &annulla, ph_meteo_ms() + 5000, &c, &n, &e) == -1);
        VERIFICA(ph_meteo_ms() - t0 < 1000);
        VERIFICA(atomic_load(&ph_tls_finto_connessioni) == 0);
        VERIFICA(atomic_load(&ph_tls_finto_aperture) == 1);
    }
    /* certificato non valido: la connessione non arriva al server */
    ripristina(M_OK, 0);
    atomic_store(&ph_tls_finto_rifiuta, 1);
    VERIFICA(http_prova("/x", NULL, ph_meteo_ms() + 5000, &c, &n, &e) == -1);
    VERIFICA(strstr(e.testo, "certificato") != NULL && atomic_load(&S.conn) == 0);
    /* porta chiusa */
    {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in a = {.sin_family = AF_INET};
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(s, (struct sockaddr *)&a, sizeof a);
        socklen_t l = sizeof a;
        getsockname(s, (struct sockaddr *)&a, &l);
        close(s);
        ripristina(M_OK, 0);
        atomic_store(&ph_tls_finto_porta, ntohs(a.sin_port));
        VERIFICA(http_prova("/x", NULL, ph_meteo_ms() + 5000, &c, &n, &e) == -1);
        atomic_store(&ph_tls_finto_porta, S.porta);
    }
}

/* ---- Oggetto PhMeteo --------------------------------------------------------------- */
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int n, n_ok, n_err;
    PhMeteoPrevisione ultima;
    char frase[256];
    int dalla_principale; /* callback arrivate sul thread del chiamante (non devono esistere) */
    pthread_t principale;
    int blocca_ms;
    atomic_int dentro, finito;
    int richiedi_ancora;
    PhMeteo *m;
    int32_t lat, lon;
    int azione; /* dentro la callback: 1 = ph_meteo_annulla, 2 = ph_meteo_chiudi (vietate: devono essere innocue) */
} Esito;

static void esito_init(Esito *x)
{
    memset(x, 0, sizeof *x);
    pthread_mutex_init(&x->mu, NULL);
    pthread_cond_init(&x->cv, NULL);
    x->principale = pthread_self();
}

static void esito_fatto(const PhMeteoPrevisione *p, const char *frase, void *u)
{
    Esito *x = u;
    pthread_mutex_lock(&x->mu);
    x->n++;
    if (p != NULL) {
        x->n_ok++;
        x->ultima = *p;
    } else {
        x->n_err++;
        snprintf(x->frase, sizeof x->frase, "%s", frase ? frase : "(NULL)");
    }
    if (pthread_equal(pthread_self(), x->principale))
        x->dalla_principale++;
    atomic_store(&x->dentro, 1);
    pthread_cond_broadcast(&x->cv);
    if (x->blocca_ms > 0) {
        struct timespec t = scade_tra(x->blocca_ms);
        pthread_cond_timedwait(&x->cv, &x->mu, &t); /* nessuno la sveglia: dura il suo tempo */
    }
    int ancora = x->richiedi_ancora > 0 ? x->richiedi_ancora-- : 0;
    pthread_mutex_unlock(&x->mu);
    if (ancora)
        ph_meteo_chiedi(x->m, x->lat, x->lon, esito_fatto, x, NULL);
    if (x->azione == 1)
        ph_meteo_annulla(x->m);
    else if (x->azione == 2)
        ph_meteo_chiudi(x->m);
    atomic_store(&x->finito, 1);
}

static int attendi_n(Esito *x, int n, int ms)
{
    struct timespec t = scade_tra(ms);
    pthread_mutex_lock(&x->mu);
    while (x->n < n && pthread_cond_timedwait(&x->cv, &x->mu, &t) == 0) {
    }
    int ok = x->n >= n;
    pthread_mutex_unlock(&x->mu);
    return ok;
}

static int chiedi_attendi(PhMeteo *m, Esito *x, int32_t lat, int32_t lon, int n_atteso)
{
    PhErrore e;
    if (ph_meteo_chiedi(m, lat, lon, esito_fatto, x, &e) != 0)
        return 0;
    return attendi_n(x, n_atteso, 5000);
}

static PhMeteo *nuovo(void)
{
    PhErrore e;
    PhMeteo *m = ph_meteo_nuovo(&e);
    VERIFICA(m != NULL);
    return m;
}

/* Posizioni estreme ammesse: la richiesta parte davvero (un collegamento) e il
 * percorso arrivato al server ha i numeri esatti del contratto. */
static void prova_estremi(void)
{
    static const struct {
        int32_t lat, lon;
        const char *atteso;
    } c[] = {
        {90000, -180000, "latitude=90.000&longitude=-180.000&"},
        {-90000, 180000, "latitude=-90.000&longitude=180.000&"},
        {-5, 5, "latitude=-0.005&longitude=0.005&"},
        {0, 0, "latitude=0.000&longitude=0.000&"},
    };
    for (size_t i = 0; i < sizeof c / sizeof *c; i++) {
        Esito x;
        esito_init(&x);
        ripristina(M_OK, 0);
        PhMeteo *m = nuovo();
        VERIFICA(chiedi_attendi(m, &x, c[i].lat, c[i].lon, 1) && x.n_ok == 1);
        VERIFICA(atomic_load(&S.conn) == 1); /* una richiesta, un collegamento */
        char rich[4096];
        pthread_mutex_lock(&S.mu);
        memcpy(rich, S.ultima, sizeof rich);
        pthread_mutex_unlock(&S.mu);
        VERIFICA(strstr(rich, c[i].atteso) != NULL);
        ph_meteo_chiudi(m);
    }
}

static void prova_argomenti(void)
{
    PhErrore e;
    Esito x;
    esito_init(&x);
    ripristina(M_OK, 0);
    ph_meteo_annulla(NULL);
    ph_meteo_chiudi(NULL);
    PhMeteo *m = nuovo();
    VERIFICA(ph_meteo_chiedi(NULL, 0, 0, esito_fatto, &x, &e) == -1);
    VERIFICA(ph_meteo_chiedi(m, 0, 0, NULL, &x, &e) == -1);
    VERIFICA(ph_meteo_chiedi(m, 90005, 0, esito_fatto, &x, &e) == -1);
    VERIFICA(ph_meteo_chiedi(m, 0, -180005, esito_fatto, &x, &e) == -1);
    VERIFICA(ph_meteo_chiedi(m, 45461, 9190, esito_fatto, &x, &e) == -1); /* non multiplo di 5 */
    VERIFICA(ph_meteo_chiedi(m, 45460, 9191, esito_fatto, &x, &e) == -1);
    VERIFICA(ph_meteo_chiedi(m, -1, 0, esito_fatto, &x, &e) == -1);
    VERIFICA(atomic_load(&ph_tls_finto_aperture) == 0); /* nessun collegamento senza richiesta */
    ph_meteo_chiudi(m);
    /* dopo la chiusura: errore, mai uso di memoria liberata */
    VERIFICA(ph_meteo_chiedi(m, 0, 0, esito_fatto, &x, &e) == -1);
    ph_meteo_annulla(m);
    ph_meteo_chiudi(m);
    VERIFICA(x.n == 0);
    /* nessuna richiesta: nessun collegamento */
    m = nuovo();
    ph_meteo_chiudi(m);
    VERIFICA(atomic_load(&ph_tls_finto_aperture) == 0 && atomic_load(&S.conn) == 0);
}

static void prova_frequenza(void)
{
    Esito x;
    esito_init(&x);
    ripristina(M_OK, 0);
    PhMeteo *m = nuovo();
    VERIFICA(chiedi_attendi(m, &x, 45460, 9190, 1));
    VERIFICA(x.n_ok == 1 && atomic_load(&S.conn) == 1);
    VERIFICA(x.ultima.n_ore == PH_METEO_ORE_MAX && x.ultima.adesso.codice == 61 && x.ultima.ricevuta == T0);
    /* stessa posizione: dalla cache, nessun collegamento nuovo */
    VERIFICA(chiedi_attendi(m, &x, 45460, 9190, 2));
    VERIFICA(x.n_ok == 2 && atomic_load(&S.conn) == 1);
    atomic_store(&g_mono, 1000 + PH_METEO_INTERVALLO_S - 1);
    VERIFICA(chiedi_attendi(m, &x, 45460, 9190, 3));
    VERIFICA(atomic_load(&S.conn) == 1);
    /* scaduti i 30 minuti: nuova richiesta */
    atomic_store(&g_mono, 1000 + PH_METEO_INTERVALLO_S + 1);
    VERIFICA(chiedi_attendi(m, &x, 45460, 9190, 4));
    VERIFICA(atomic_load(&S.conn) == 2 && x.ultima.ricevuta == T0 + PH_METEO_INTERVALLO_S + 1);
    /* altra posizione: richiesta nuova */
    VERIFICA(chiedi_attendi(m, &x, 46000, 9190, 5));
    VERIFICA(atomic_load(&S.conn) == 3);
    VERIFICA(chiedi_attendi(m, &x, 46000, 9190, 6));
    VERIFICA(atomic_load(&S.conn) == 3);
    /* la cache tiene piu' posizioni: tornare alla prima non apre un collegamento */
    VERIFICA(chiedi_attendi(m, &x, 45460, 9190, 7));
    VERIFICA(atomic_load(&S.conn) == 3);
    /* la callback non arriva mai dentro ph_meteo_chiedi (thread del chiamante) */
    VERIFICA(x.dalla_principale == 0);
    /* nessun dato di posizione nelle frasi */
    VERIFICA(x.n_err == 0);
    ph_meteo_chiudi(m);
}

static void prova_fallimenti(void)
{
    Esito x;
    esito_init(&x);
    ripristina(M_CODICE, 500);
    PhMeteo *m = nuovo();
    int n = 0;
#define CHIEDI(mono, conn_atteso)                                                                                      \
    do {                                                                                                               \
        atomic_store(&g_mono, (mono));                                                                                 \
        VERIFICA(chiedi_attendi(m, &x, 45460, 9190, ++n));                                                               \
        VERIFICA(atomic_load(&S.conn) == (conn_atteso));                                                               \
    } while (0)
    CHIEDI(1000, 1);               /* fallisce: prossima prova dopo 5 minuti (301 s dopo = 1301) */
    VERIFICA(strstr(x.frase, "500") != NULL && x.n_ok == 0);
    VERIFICA(strstr(x.frase, "45.4") == NULL && strstr(x.frase, "9.19") == NULL && strstr(x.frase, "45460") == NULL &&
             strstr(x.frase, "9190") == NULL);
    char prima[256];
    snprintf(prima, sizeof prima, "%s", x.frase);
    CHIEDI(1000, 1);               /* subito: stesso errore, nessun collegamento */
    VERIFICA(strcmp(x.frase, prima) == 0);
    CHIEDI(1000 + 299, 1);
    CHIEDI(1000 + 300, 2);         /* 5 minuti passati: riprova, fallisce, ora 15 minuti */
    CHIEDI(1300 + 899, 2);
    CHIEDI(1300 + 900, 3);         /* poi 30 minuti */
    CHIEDI(2200 + 1799, 3);
    CHIEDI(2200 + 1800, 4);        /* e resta a 30 */
    CHIEDI(4000 + 1799, 4);
    atomic_store(&S.modo, M_OK);
    CHIEDI(4000 + 1800, 5);        /* il servizio torna: riesce */
    VERIFICA(x.n_ok == 1 && x.ultima.n_ore > 0);
    /* dopo il successo si riparte da 5 minuti */
    atomic_store(&S.modo, M_CODICE);
    atomic_store(&S.codice, 429);
    CHIEDI(5800 + PH_METEO_INTERVALLO_S, 6);
    VERIFICA(strstr(x.frase, "429") != NULL);
    CHIEDI(5800 + PH_METEO_INTERVALLO_S + 299, 6);
    CHIEDI(5800 + PH_METEO_INTERVALLO_S + 300, 7);
#undef CHIEDI
    VERIFICA(x.dalla_principale == 0);
    ph_meteo_chiudi(m);

    /* certificato rifiutato: errore con la sua frase, il server non e' mai toccato */
    esito_init(&x);
    ripristina(M_OK, 0);
    atomic_store(&ph_tls_finto_rifiuta, 1);
    m = nuovo();
    VERIFICA(chiedi_attendi(m, &x, 45460, 9190, 1));
    VERIFICA(x.n_err == 1 && strstr(x.frase, "certificato") != NULL && atomic_load(&S.conn) == 0);
    ph_meteo_chiudi(m);
    /* risposta con JSON rotto: errore «non valida», conta come fallimento */
    esito_init(&x);
    ripristina(M_CHUNK_ROTTO, 0);
    m = nuovo();
    VERIFICA(chiedi_attendi(m, &x, 45460, 9190, 1) && x.n_err == 1);
    ph_meteo_chiudi(m);
}

/* Fasi in cui interrompere: 0 collegamento, 1 attesa dell'intestazione, 2 lettura del corpo. */
static int arriva_in_fase(int fase)
{
    if (fase == 0) {
        atomic_store(&ph_tls_finto_trattieni, 1);
        return attendi_sem(&ph_tls_finto_in_apri, 5000);
    }
    return attendi_sem(fase == 1 ? &S.richiesta : &S.inviato, 5000);
}

static void prova_annulla_chiudi_fasi(void)
{
    for (int chiudi = 0; chiudi <= 1; chiudi++)
        for (int fase = 0; fase <= 2; fase++) {
            Esito x;
            esito_init(&x);
            ripristina(fase == 0 ? M_OK : fase == 1 ? M_SILENZIO : M_LENTA, 0);
            if (fase == 0)
                atomic_store(&ph_tls_finto_trattieni, 1);
            PhMeteo *m = nuovo();
            PhErrore e;
            VERIFICA(ph_meteo_chiedi(m, 45460, 9190, esito_fatto, &x, &e) == 0);
            VERIFICA(arriva_in_fase(fase));
            int64_t t0 = ph_meteo_ms();
            if (chiudi) {
                ph_meteo_chiudi(m);
                VERIFICA(ph_meteo_ms() - t0 < 1000);
            } else {
                ph_meteo_annulla(m);
                VERIFICA(ph_meteo_ms() - t0 < 1000);
                atomic_store(&ph_tls_finto_trattieni, 0);
                /* Il collegamento interrotto e' un tentativo: chiedere subito di
                 * nuovo NON ne apre un altro (errore «riprova piu' tardi»);
                 * passati 5 minuti l'oggetto, ancora usabile, riesce. */
                atomic_store(&S.modo, M_OK);
                int aperture = atomic_load(&ph_tls_finto_aperture);
                VERIFICA(chiedi_attendi(m, &x, 45460, 9190, 1) && x.n_err == 1);
                VERIFICA(atomic_load(&ph_tls_finto_aperture) == aperture);
                atomic_store(&g_mono, 1000 + 300);
                VERIFICA(chiedi_attendi(m, &x, 45460, 9190, 2) && x.n_ok == 1);
                int64_t t1 = ph_meteo_ms();
                ph_meteo_chiudi(m);
                VERIFICA(ph_meteo_ms() - t1 < 1000);
            }
            atomic_store(&ph_tls_finto_trattieni, 0);
            /* la richiesta annullata/chiusa non ha prodotto nessuna callback */
            VERIFICA(x.n == (chiudi ? 0 : 2));
        }
}

static void prova_sostituzione(void)
{
    /* Una richiesta in volo per lo stesso posto: la nuova prende il posto
     * del destinatario, la vecchia non riceve nulla. */
    Esito a, b;
    esito_init(&a);
    esito_init(&b);
    ripristina(M_OK, 0);
    atomic_store(&ph_tls_finto_trattieni, 1);
    PhMeteo *m = nuovo();
    PhErrore e;
    VERIFICA(ph_meteo_chiedi(m, 45460, 9190, esito_fatto, &a, &e) == 0);
    VERIFICA(attendi_sem(&ph_tls_finto_in_apri, 5000));
    VERIFICA(ph_meteo_chiedi(m, 45460, 9190, esito_fatto, &b, &e) == 0);
    atomic_store(&ph_tls_finto_trattieni, 0);
    VERIFICA(attendi_n(&b, 1, 5000));
    ph_meteo_chiudi(m);
    VERIFICA(a.n == 0 && b.n == 1 && b.n_ok == 1);

    /* Posto diverso mentre la prima e' in volo: la prima si ferma e il risultato e' per il nuovo posto. */
    esito_init(&a);
    esito_init(&b);
    ripristina(M_OK, 0);
    atomic_store(&ph_tls_finto_trattieni, 1);
    m = nuovo();
    VERIFICA(ph_meteo_chiedi(m, 45460, 9190, esito_fatto, &a, &e) == 0);
    VERIFICA(attendi_sem(&ph_tls_finto_in_apri, 5000));
    VERIFICA(ph_meteo_chiedi(m, 10000, 20000, esito_fatto, &b, &e) == 0);
    atomic_store(&ph_tls_finto_trattieni, 0);
    VERIFICA(attendi_n(&b, 1, 5000));
    ph_meteo_chiudi(m);
    VERIFICA(a.n == 0 && b.n == 1 && b.n_ok == 1);
}

static void prova_callback(void)
{
    /* chiudi e annulla aspettano la callback in corso */
    for (int chiudi = 0; chiudi <= 1; chiudi++) {
        Esito x;
        esito_init(&x);
        x.blocca_ms = 300;
        ripristina(M_OK, 0);
        PhMeteo *m = nuovo();
        PhErrore e;
        VERIFICA(ph_meteo_chiedi(m, 45460, 9190, esito_fatto, &x, &e) == 0);
        VERIFICA(attendi_n(&x, 1, 5000)); /* ora la callback e' in corso (ferma 300 ms) */
        if (chiudi)
            ph_meteo_chiudi(m);
        else
            ph_meteo_annulla(m);
        VERIFICA(atomic_load(&x.finito) == 1);
        if (!chiudi)
            ph_meteo_chiudi(m);
    }
    /* dalla callback si puo' richiamare ph_meteo_chiedi (risposta dalla cache) */
    Esito x;
    esito_init(&x);
    ripristina(M_OK, 0);
    PhMeteo *m = nuovo();
    x.m = m;
    x.lat = 45460;
    x.lon = 9190;
    x.richiedi_ancora = 1;
    PhErrore e;
    VERIFICA(ph_meteo_chiedi(m, 45460, 9190, esito_fatto, &x, &e) == 0);
    VERIFICA(attendi_n(&x, 2, 5000));
    VERIFICA(atomic_load(&S.conn) == 1 && x.n_ok == 2);
    ph_meteo_chiudi(m);
}

typedef struct {
    PhMeteo *m;
    atomic_int *chiamate, *rifiutate;
    atomic_int *cb;
} Martello;

static void conta_cb(const PhMeteoPrevisione *p, const char *f, void *u)
{
    (void)p;
    (void)f;
    atomic_fetch_add((atomic_int *)u, 1);
}

static void *martella(void *a)
{
    Martello *w = a;
    int64_t fine = ph_meteo_ms() + 15000;
    for (int i = 0; ph_meteo_ms() < fine; i++) {
        PhErrore e;
        int r = ph_meteo_chiedi(w->m, (int32_t)(45460 + (i % 3) * 5), 9190, conta_cb, w->cb, &e);
        atomic_fetch_add(w->chiamate, 1);
        if (r != 0) {
            atomic_fetch_add(w->rifiutate, 1);
            return NULL; /* dopo la chiusura: errore, e basta */
        }
        if (i % 7 == 0)
            ph_meteo_annulla(w->m);
        sched_yield();
    }
    return NULL;
}

static void prova_parallelo(void)
{
    ripristina(M_OK, 0);
    PhMeteo *m = nuovo();
    atomic_int chiamate = 0, rifiutate = 0, cb = 0;
    Martello w = {m, &chiamate, &rifiutate, &cb};
    enum { N = 4 };
    pthread_t t[N];
    for (int i = 0; i < N; i++)
        pthread_create(&t[i], NULL, martella, &w);
    int64_t fine = ph_meteo_ms() + 5000;
    while (atomic_load(&chiamate) < 50 && ph_meteo_ms() < fine)
        sched_yield();
    VERIFICA(atomic_load(&chiamate) >= 50);
    ph_meteo_chiudi(m);
    int dopo = atomic_load(&cb);
    for (int i = 0; i < N; i++)
        pthread_join(t[i], NULL);
    VERIFICA(atomic_load(&rifiutate) == N); /* ognuno e' stato respinto dopo la chiusura */
    VERIFICA(atomic_load(&cb) == dopo);     /* nessuna callback dopo il ritorno di chiudi */
}

static int attendi_flag(atomic_int *f, int ms)
{
    int64_t fine = ph_meteo_ms() + ms;
    while (!atomic_load(f) && ph_meteo_ms() < fine)
        sched_yield();
    return atomic_load(f) != 0;
}

/* Cache a 4 posizioni, conteggio dei collegamenti. */
static void prova_tentativi(void)
{
    Esito x;
    esito_init(&x);
    ripristina(M_OK, 0);
    PhMeteo *m = nuovo();
    int n = 0;
#define POS(la, conn_atteso)                                                                                           \
    do {                                                                                                               \
        VERIFICA(chiedi_attendi(m, &x, (la), 9190, ++n));                                                               \
        VERIFICA(atomic_load(&S.conn) == (conn_atteso));                                                               \
    } while (0)
    POS(45460, 1); /* A */
    POS(46000, 2); /* B */
    POS(45460, 2); /* A dalla cache */
    POS(46000, 2); /* B dalla cache: alternare non apre collegamenti */
    POS(47000, 3); /* C */
    POS(48000, 4); /* D: le 4 posizioni sono piene */
    POS(49000, 5); /* E: dimentica la meno usata di recente (A) */
    POS(49000, 5);
    POS(45460, 6); /* A e' stata dimenticata: nuovo collegamento */
    POS(49000, 6); /* E c'e' ancora */
#undef POS
    VERIFICA(x.n_ok == n && x.dalla_principale == 0);
    ph_meteo_chiudi(m);
}

/* Widget tolto e rimesso (annulla poi chiedi), con risposta riuscita o fallita. */
static void prova_chiedi_annulla(void)
{
    Esito x;
    esito_init(&x);
    ripristina(M_CODICE, 500);
    PhMeteo *m = nuovo();
    VERIFICA(chiedi_attendi(m, &x, 45460, 9190, 1) && x.n_err == 1);
    for (int i = 0; i < 5; i++) {
        ph_meteo_annulla(m);
        VERIFICA(chiedi_attendi(m, &x, 45460, 9190, 2 + i) && x.n_err == 2 + i);
    }
    VERIFICA(atomic_load(&S.conn) == 1); /* nessun collegamento in piu' */
    ph_meteo_chiudi(m);

    esito_init(&x);
    ripristina(M_OK, 0);
    m = nuovo();
    VERIFICA(chiedi_attendi(m, &x, 45460, 9190, 1) && x.n_ok == 1);
    for (int i = 0; i < 5; i++) {
        ph_meteo_annulla(m);
        VERIFICA(chiedi_attendi(m, &x, 45460, 9190, 2 + i) && x.n_ok == 2 + i);
    }
    VERIFICA(atomic_load(&S.conn) == 1);
    ph_meteo_chiudi(m);

    /* Una risposta riuscita che arriva dopo annulla va comunque in cache (il
     * finto ignora l'annullamento: la richiesta finisce lo stesso). */
    Esito a, b;
    esito_init(&a);
    esito_init(&b);
    ripristina(M_OK, 0);
    atomic_store(&ph_tls_finto_trattieni, 1);
    m = nuovo();
    PhErrore e;
    VERIFICA(ph_meteo_chiedi(m, 45460, 9190, esito_fatto, &a, &e) == 0);
    VERIFICA(attendi_sem(&ph_tls_finto_in_apri, 5000));
    atomic_store(&ph_tls_finto_ignora_annulla, 1);
    ph_meteo_annulla(m);
    atomic_store(&ph_tls_finto_trattieni, 0);
    VERIFICA(chiedi_attendi(m, &b, 45460, 9190, 1) && b.n_ok == 1);
    VERIFICA(atomic_load(&S.conn) == 1 && a.n == 0);
    ph_meteo_chiudi(m);

    /* ... e dopo un cambio di posizione: la prima finisce in cache, la seconda e' un secondo collegamento. */
    esito_init(&a);
    esito_init(&b);
    ripristina(M_OK, 0);
    atomic_store(&ph_tls_finto_trattieni, 1);
    m = nuovo();
    VERIFICA(ph_meteo_chiedi(m, 45460, 9190, esito_fatto, &a, &e) == 0);
    VERIFICA(attendi_sem(&ph_tls_finto_in_apri, 5000));
    atomic_store(&ph_tls_finto_ignora_annulla, 1);
    VERIFICA(ph_meteo_chiedi(m, 10000, 20000, esito_fatto, &b, &e) == 0);
    atomic_store(&ph_tls_finto_trattieni, 0);
    VERIFICA(attendi_n(&b, 1, 5000) && b.n_ok == 1);
    Esito c;
    esito_init(&c);
    VERIFICA(chiedi_attendi(m, &c, 45460, 9190, 1) && c.n_ok == 1); /* dalla cache */
    VERIFICA(atomic_load(&S.conn) == 2 && a.n == 0);
    ph_meteo_chiudi(m);
}

/* Intervallo minimo tra due avvii, anche per posizioni diverse. */
static void prova_avvii(void)
{
    Esito x;
    esito_init(&x);
    ripristina(M_OK, 0);
    ph_meteo_prova_avvii_ms(800);
    PhMeteo *m = nuovo();
    VERIFICA(chiedi_attendi(m, &x, 45460, 9190, 1));
    int64_t t_a = ph_meteo_ms();
    VERIFICA(chiedi_attendi(m, &x, 46000, 9190, 2));
    VERIFICA(ph_meteo_ms() - t_a >= 600); /* il secondo avvio ha aspettato */
    ph_meteo_prova_avvii_ms(5000);
    /* l'attesa si interrompe subito con annulla e chiudi */
    PhErrore e;
    VERIFICA(ph_meteo_chiedi(m, 47000, 9190, esito_fatto, &x, &e) == 0);
    ph_meteo_annulla(m);
    int64_t t0 = ph_meteo_ms();
    ph_meteo_chiudi(m);
    VERIFICA(ph_meteo_ms() - t0 < 1000);
    VERIFICA(x.n == 2 && atomic_load(&S.conn) == 2);
}

/* annulla e chiudi dalla callback: vietate, ma non devono bloccare ne' liberare. */
static void prova_callback_vietati(void)
{
    for (int azione = 1; azione <= 2; azione++) {
        Esito x;
        esito_init(&x);
        x.azione = azione;
        ripristina(M_OK, 0);
        PhMeteo *m = nuovo();
        x.m = m;
        PhErrore e;
        VERIFICA(ph_meteo_chiedi(m, 45460, 9190, esito_fatto, &x, &e) == 0);
        VERIFICA(attendi_n(&x, 1, 5000));
        VERIFICA(attendi_flag(&x.finito, 5000)); /* la callback e' tornata (nessun deadlock) */
        atomic_store(&x.finito, 0);
        /* l'oggetto e' ancora vivo e utilizzabile (dalla cache) */
        VERIFICA(ph_meteo_chiedi(m, 45460, 9190, esito_fatto, &x, &e) == 0);
        VERIFICA(attendi_n(&x, 2, 5000));
        VERIFICA(attendi_flag(&x.finito, 5000));
        ph_meteo_chiudi(m);
    }
}

/* ---- HTTP: un caso per ogni ramo d'errore, e fuzz, con il finto «da memoria» ---- */
static int http_mem(const char *dati, size_t n, uint32_t seme, char **c, size_t *len, PhErrore *e)
{
    char *copia = malloc(n ? n : 1); /* dimensione esatta: ASan vede ogni lettura oltre */
    if (n)
        memcpy(copia, dati, n);
    ph_tls_finto_memoria(copia, n, seme);
    e->testo[0] = '\0';
    *c = NULL;
    int r = http_prova("/x", NULL, ph_meteo_ms() + 5000, c, len, e);
    ph_tls_finto_memoria(NULL, 0, 0);
    free(copia);
    return r;
}

typedef struct {
    const char *nome;
    const char *dati;
    size_t n;
    int atteso; /* 0 riesce, -1 errore */
} CasoHttp;
#define R(nome, s, atteso) {nome, s, sizeof(s) - 1, atteso}

static char *ripeti(const char *testa, const char *pezzo, int volte, const char *coda, size_t *n)
{
    size_t lt = strlen(testa), lp = strlen(pezzo), lc = strlen(coda);
    char *b = malloc(lt + lp * (size_t)volte + lc + 1);
    memcpy(b, testa, lt);
    for (int i = 0; i < volte; i++)
        memcpy(b + lt + lp * (size_t)i, pezzo, lp);
    memcpy(b + lt + lp * (size_t)volte, coda, lc);
    *n = lt + lp * (size_t)volte + lc;
    return b;
}

static void prova_http_rami(void)
{
    ripristina(M_OK, 0);
    static const CasoHttp t[] = {
        R("ok base", "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}", 0),
        R("ok CL con spazi", "HTTP/1.1 200 OK\r\nContent-Length:   2  \r\n\r\n{}", 0),
        R("ok solo LF", "HTTP/1.1 200 OK\nContent-Length: 2\n\n{}", 0),
        R("ok obs-text e tab", "HTTP/1.1 200 OK\r\nX-A: \xC3\xA9\tb\r\nContent-Length: 2\r\n\r\n{}", 0),
        R("ok chunked", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r\n0\r\n\r\n", 0),
        R("CL non numerico", "HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\n{}", -1),
        R("CL negativo", "HTTP/1.1 200 OK\r\nContent-Length: -5\r\n\r\n{}", -1),
        R("CL 16 cifre", "HTTP/1.1 200 OK\r\nContent-Length: 1234567890123456\r\n\r\n{}", -1),
        R("CL con coda", "HTTP/1.1 200 OK\r\nContent-Length: 2x\r\n\r\n{}", -1),
        R("CL vuoto", "HTTP/1.1 200 OK\r\nContent-Length:\r\n\r\n{}", -1),
        R("CL solo spazi", "HTTP/1.1 200 OK\r\nContent-Length:    \r\n\r\n{}", -1),
        R("CL doppio", "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("chunk 9 cifre", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n000000002\r\n{}\r\n0\r\n\r\n", -1),
        R("chunk non esadecimale", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\ng\r\n{}\r\n0\r\n\r\n", -1),
        R("chunk senza cifre", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n;x\r\n{}\r\n0\r\n\r\n", -1),
        R("chunk dati senza CRLF", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}xx\r\n0\r\n\r\n", -1),
        R("chunk e lunghezza", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 2\r\n\r\n2\r\n{}\r\n0\r\n\r\n", -1),
        R("TE gzip", "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n2\r\n{}\r\n0\r\n\r\n", -1),
        R("riga senza due punti", "HTTP/1.1 200 OK\r\nFoo\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("nome vuoto", "HTTP/1.1 200 OK\r\n: v\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("intestazione piegata", "HTTP/1.1 200 OK\r\nX-A: 1\r\n  piu'\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("nome con spazio prima dei due punti", "HTTP/1.1 200 OK\r\nContent-Length : 2\r\n\r\n{}", -1),
        R("nome con spazio dentro", "HTTP/1.1 200 OK\r\nBad Name: x\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("nome con carattere non ammesso", "HTTP/1.1 200 OK\r\nX(A): x\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("Content-Encoding gzip", "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("NUL in un valore", "HTTP/1.1 200 OK\r\nX-A: a\0b\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("NUL che nasconde Content-Length", "HTTP/1.1 200 OK\r\nX-A: a\0\r\nContent-Length: 99999\r\n\r\n{}", -1),
        R("CR interno", "HTTP/1.1 200 OK\r\nX-A: a\rb\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("controllo 0x01", "HTTP/1.1 200 OK\r\nX-A: a\x01\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("0x7F", "HTTP/1.1 200 OK\r\nX-A: a\x7f\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("NUL nella riga di stato", "HTTP/1.1 200 O\0K\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("NUL nella riga del chunk", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\0\r\n{}\r\n0\r\n\r\n", -1),
        R("risposta 100", "HTTP/1.1 100 Continue\r\n\r\n", -1),
        R("risposta 101", "HTTP/1.1 101 Switching\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("stato HTTP/2", "HTTP/2 200\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("stato con 4 cifre", "HTTP/1.1 2000 OK\r\nContent-Length: 2\r\n\r\n{}", -1),
        R("troncata nelle intestazioni", "HTTP/1.1 200 OK\r\nContent-Le", -1),
        R("vuota", "", -1),
        R("troncata nel corpo", "HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n{}", -1),
    };
    char *c;
    size_t n;
    PhErrore e;
    for (size_t i = 0; i < sizeof t / sizeof *t; i++)
        for (uint32_t seme = 1; seme <= 3; seme++) {
            int r = http_mem(t[i].dati, t[i].n, seme, &c, &n, &e);
            VERIFICA(r == t[i].atteso);
            if (r != t[i].atteso)
                fprintf(stderr, "  caso HTTP «%s» (seme %u): %d, frase «%s»\n", t[i].nome, seme, r, e.testo);
            if (r == 0) {
                VERIFICA(c != NULL && c[n] == '\0');
                free(c);
            } else
                VERIFICA(c == NULL);
        }

    /* casi costruiti: righe troppo lunghe, code, tetto dei pezzi */
    char *b;
    size_t bn;
    b = ripeti("HTTP/1.1 200 OK\r\nX-A: ", "a", 9000, "\r\nContent-Length: 2\r\n\r\n{}", &bn); /* riga > RIGA_MAX */
    VERIFICA(http_mem(b, bn, 1, &c, &n, &e) == -1);
    free(b);
    b = ripeti("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5;", "x", 300, "\r\nabcde\r\n0\r\n\r\n", &bn); /* riga del pezzo > limite */
    VERIFICA(http_mem(b, bn, 1, &c, &n, &e) == -1);
    free(b);
    b = ripeti("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r\n0\r\n",
               "X-Coda: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n", 400, "\r\n", &bn); /* code > tetto */
    VERIFICA(http_mem(b, bn, 1, &c, &n, &e) == -1);
    free(b);
    b = ripeti("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r\n0\r\n",
               "X-Coda: a\r\n", 100, "\r\n", &bn); /* code piccole: vanno */
    VERIFICA(http_mem(b, bn, 1, &c, &n, &e) == 0 && n == 2);
    free(c);
    free(b);
    b = ripeti("HTTP/1.1 200 OK\r\n", "X-Riempitivo: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n", 300,
               "Content-Length: 2\r\n\r\n{}", &bn); /* intestazioni > 16 KiB */
    VERIFICA(http_mem(b, bn, 1, &c, &n, &e) == -1);
    free(b);
    /* tetto complessivo delle righe dei pezzi: 3000 pezzi da un byte vanno, 4000 no */
    for (int k = 0; k < 2; k++) {
        int pezzi = k == 0 ? 3000 : 4000;
        b = ripeti("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n", "1\r\na\r\n", pezzi, "0\r\n\r\n", &bn);
        int r = http_mem(b, bn, 7, &c, &n, &e);
        VERIFICA(k == 0 ? (r == 0 && n == 3000) : r == -1);
        if (r == 0)
            free(c);
        free(b);
    }
}

/* Fuzz di http.c: risposte vere mutate, consegnate a pezzi di lunghezza
 * casuale. Con qualunque sequenza: nessun accesso fuori dai buffer (ASan);
 * se riesce, il corpo rispetta i limiti ed e' terminato da NUL. */
static void prova_http_fuzz(void)
{
    ripristina(M_OK, 0);
    static const char *modelli[] = {
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 20\r\nConnection: close\r\n\r\n{\"a\":\"bcdefghijklm\"}",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5;e=1\r\n{\"a\":\r\n8\r\n\"bcdefg\"\r\n1\r\n}\r\n0\r\nX: y\r\n\r\n",
        "HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n{\"a\":\"bcdefghijklm\"}",
        "HTTP/1.1 500 Errore\r\nContent-Length: 2\r\n\r\n{}",
    };
    for (int it = 0; it < 30000; it++) {
        const char *mod = modelli[alea() % 4];
        size_t n0 = strlen(mod), n = n0;
        char *c = malloc(n0 + 128);
        memcpy(c, mod, n0);
        switch (alea() % 5) {
        case 0:
            for (unsigned k = alea() % 6 + 1; k > 0; k--)
                c[alea() % n] = (char)alea();
            break;
        case 1:
            n = alea() % n0;
            break;
        case 2: {
            const char *sp = "\r\n\0: ;0123456789abcdefgxXchunk-";
            size_t a = alea() % n0;
            memmove(c + a + 1, c + a, n0 - a);
            c[a] = sp[alea() % 30];
            n = n0 + 1;
            break;
        }
        case 3: {
            size_t a = alea() % n0, l = alea() % 12;
            if (a + l > n0)
                l = n0 - a;
            memmove(c + a, c + a + l, n0 - a - l);
            n = n0 - l;
            break;
        }
        default:
            n = alea() % 120;
            for (size_t k = 0; k < n; k++)
                c[k] = "\r\n :;0123456789aHTP/.cunkd-"[alea() % 26];
            break;
        }
        char *corpo;
        size_t len;
        PhErrore e;
        int r = http_mem(c, n, alea(), &corpo, &len, &e);
        free(c);
        if (r == 0) {
            VERIFICA(corpo != NULL && len <= PH_METEO_RISPOSTA_MAX && corpo[len] == '\0');
            free(corpo);
        } else
            VERIFICA(corpo == NULL);
    }
}

/* ======================= Ricerca dei luoghi ======================================= */

static int analizza_ric(const char *s, size_t n, PhMeteoLuogo *v, size_t massimo, size_t *nv)
{
    char *c = malloc(n ? n : 1); /* dimensione esatta: ASan vede ogni lettura oltre */
    if (n)
        memcpy(c, s, n);
    PhErrore e;
    memset(v, 0xAA, massimo * sizeof *v);
    int r = ph_meteo_analizza_ricerca(c, n, v, massimo, nv, &e);
    free(c);
    return r;
}

/* UTF-8 valido e senza caratteri di controllo (per verificare le uscite). */
static int utf8_pulito(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned c = *p;
        size_t k = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (k == 0 || c == 0xC0 || c == 0xC1 || c > 0xF4)
            return 0;
        for (size_t i = 1; i < k; i++)
            if ((p[i] & 0xC0) != 0x80)
                return 0; /* include il NUL: sequenza troncata */
        if (k == 1 && (c < 0x20 || c == 0x7F))
            return 0;
        if (k == 2 && c == 0xC2 && p[1] < 0xA0)
            return 0; /* C1 */
        if (k == 3 && c == 0xED && p[1] >= 0xA0)
            return 0; /* surrogati */
        p += k;
    }
    return 1;
}

static void prova_ricerca_percorso(void)
{
    char b[512];
    PhErrore e;
    VERIFICA(ph_meteo_percorso_ricerca("Milano", "it", b, sizeof b, &e) == 0);
    VERIFICA(strcmp(b, "/v1/search?name=Milano&count=10&language=it&format=json") == 0);
    VERIFICA(ph_meteo_percorso_ricerca("  San Jos\xC3\xA9  ", "en", b, sizeof b, &e) == 0);
    VERIFICA(strcmp(b, "/v1/search?name=San%20Jos%C3%A9&count=10&language=en&format=json") == 0);
    VERIFICA(ph_meteo_percorso_ricerca("L'Aquila", "it", b, sizeof b, &e) == 0 && strstr(b, "name=L%27Aquila&") != NULL);
    VERIFICA(ph_meteo_percorso_ricerca("a&b=c#d e", "it", b, sizeof b, &e) == 0 &&
             strstr(b, "name=a%26b%3Dc%23d%20e&count=10") != NULL);
    VERIFICA(ph_meteo_percorso_ricerca("x/y?z%+;", "it", b, sizeof b, &e) == 0 &&
             strstr(b, "name=x%2Fy%3Fz%25%2B%3B&") != NULL);
    VERIFICA(ph_meteo_percorso_ricerca("-._~ab", "it", b, sizeof b, &e) == 0 && strstr(b, "name=-._~ab&") != NULL);
    VERIFICA(ph_meteo_percorso_ricerca("\xE5\x90\x8D\xE5\x8F\xA4\xE5\xB1\x8B", "ja", b, sizeof b, &e) == 0 &&
             strstr(b, "name=%E5%90%8D%E5%8F%A4%E5%B1%8B&") != NULL);
    /* lunghezze: 2 e 64 byte vanno, 1 e 65 no (dopo aver tolto gli spazi ai lati) */
    char t[200];
    memset(t, 'a', 64);
    t[64] = '\0';
    VERIFICA(ph_meteo_percorso_ricerca(t, "it", b, sizeof b, &e) == 0);
    t[64] = 'a';
    t[65] = '\0';
    VERIFICA(ph_meteo_percorso_ricerca(t, "it", b, sizeof b, &e) == -1 && b[0] == '\0');
    VERIFICA(ph_meteo_percorso_ricerca("ab", "it", b, sizeof b, &e) == 0);
    VERIFICA(ph_meteo_percorso_ricerca("a", "it", b, sizeof b, &e) == -1);
    VERIFICA(ph_meteo_percorso_ricerca("  a ", "it", b, sizeof b, &e) == -1);
    VERIFICA(ph_meteo_percorso_ricerca("", "it", b, sizeof b, &e) == -1);
    VERIFICA(ph_meteo_percorso_ricerca("     ", "it", b, sizeof b, &e) == -1);
    VERIFICA(ph_meteo_percorso_ricerca(NULL, "it", b, sizeof b, &e) == -1);
    memset(t, ' ', 150);
    memcpy(t + 150, "Roma", 5);
    VERIFICA(ph_meteo_percorso_ricerca(t, "it", b, sizeof b, &e) == 0 && strstr(b, "name=Roma&") != NULL);
    /* caratteri di controllo (C0, DEL, C1) e UTF-8 non valido: rifiutati prima della rete */
    const char *cattivi[] = {"ab\ncd",        "ab\rcd",         "ab\tcd",         "ab\x01" "cd",   "ab\x7f" "cd",
                             "ab\xC2\x85",    "\xFF\xFE" "ab",  "\xC0\xAF" "xx",  "\xC1\xBF" "xx", "\xED\xA0\x80" "ab",
                             "ab\xE2\x82",    "\xF4\x90\x80\x80" "x", "\x80\x80",  "ab\xC3",        "\xE0\x80\xAF" "ab",
                             "\xF0\x80\x80\xAF" "a"};
    for (size_t i = 0; i < sizeof cattivi / sizeof *cattivi; i++) {
        VERIFICA(ph_meteo_percorso_ricerca(cattivi[i], "it", b, sizeof b, &e) == -1);
        VERIFICA(b[0] == '\0');
    }
    /* lingua: solo due lettere minuscole */
    const char *lingue[] = {"IT", "ita", "", "i", "i1", "it ", "I t", "\xC3\xA8"};
    for (size_t i = 0; i < sizeof lingue / sizeof *lingue; i++)
        VERIFICA(ph_meteo_percorso_ricerca("Milano", lingue[i], b, sizeof b, &e) == -1);
    VERIFICA(ph_meteo_percorso_ricerca("Milano", NULL, b, sizeof b, &e) == -1);
    VERIFICA(ph_meteo_percorso_ricerca("Milano", "xx", b, sizeof b, &e) == 0);
    /* buffer piccolo o assente */
    VERIFICA(ph_meteo_percorso_ricerca("Milano", "it", b, 20, &e) == -1 && b[0] == '\0');
    VERIFICA(ph_meteo_percorso_ricerca("Milano", "it", NULL, 20, &e) == -1);
    VERIFICA(ph_meteo_percorso_ricerca("Milano", "it", b, 0, &e) == -1);
    /* le frasi d'errore non contengono mai il testo */
    VERIFICA(ph_meteo_percorso_ricerca("SEGRETO-ZZ", "IT", b, sizeof b, &e) == -1 && strstr(e.testo, "SEGRETO") == NULL);
    VERIFICA(ph_meteo_percorso_ricerca("SEGRETO-ZZ\n", "it", b, sizeof b, &e) == -1 && strstr(e.testo, "SEGRETO") == NULL);
    VERIFICA(ph_meteo_percorso_ricerca("SEGRETO-ZZ\xFF", "it", b, sizeof b, &e) == -1 && strstr(e.testo, "SEGRETO") == NULL);
    VERIFICA(ph_meteo_percorso_ricerca("SEGRETO-ZZ", "it", b, 10, &e) == -1 && strstr(e.testo, "SEGRETO") == NULL);
    memset(t, 'S', 100);
    t[100] = '\0';
    VERIFICA(ph_meteo_percorso_ricerca(t, "it", b, sizeof b, &e) == -1 && strstr(e.testo, "SSSS") == NULL);

    /* fuzz della codifica: byte a caso; se riesce, l'uscita ha SOLO la forma attesa
     * e decodificata torna il testo (senza gli spazi ai lati) */
    for (int it = 0; it < 40000; it++) {
        char x[100];
        size_t n = alea() % 80;
        for (size_t k = 0; k < n; k++) {
            uint32_t r = alea();
            x[k] = r % 3 == 0 ? (char)(r >> 8) : "ab Z&=#/?%+\xC3\xA9\x85\n"[(r >> 8) % 15];
            if (x[k] == '\0')
                x[k] = 'q';
        }
        x[n] = '\0';
        char *copia = malloc(n + 1); /* dimensione esatta */
        memcpy(copia, x, n + 1);
        int r = ph_meteo_percorso_ricerca(copia, "it", b, sizeof b, &e);
        if (r == 0) {
            const char *ini = strstr(b, "name=");
            const char *fin = strstr(b, "&count=10&language=it&format=json");
            VERIFICA(strncmp(b, "/v1/search?name=", 16) == 0 && fin != NULL && ini == b + 11);
            VERIFICA(fin != NULL && fin[strlen("&count=10&language=it&format=json")] == '\0');
            char dec[200];
            size_t d = 0;
            int ok = 1;
            for (const char *p = b + 16; fin != NULL && p < fin; p++) {
                if (*p == '%') {
                    int hi = strchr("0123456789ABCDEF", p[1]) ? (int)(strchr("0123456789ABCDEF", p[1]) - "0123456789ABCDEF") : -1;
                    int lo = strchr("0123456789ABCDEF", p[2]) ? (int)(strchr("0123456789ABCDEF", p[2]) - "0123456789ABCDEF") : -1;
                    if (hi < 0 || lo < 0 || p[1] == '\0' || p[2] == '\0') {
                        ok = 0;
                        break;
                    }
                    dec[d++] = (char)(hi * 16 + lo);
                    p += 2;
                } else {
                    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                          strchr("-._~", *p) != NULL))
                        ok = 0;
                    dec[d++] = *p;
                }
            }
            dec[d] = '\0';
            VERIFICA(ok);
            const char *a = copia;
            size_t l = n;
            while (l > 0 && *a == ' ') {
                a++;
                l--;
            }
            while (l > 0 && a[l - 1] == ' ')
                l--;
            VERIFICA(d == l && memcmp(dec, a, l) == 0 && utf8_pulito(dec) && d >= 2 && d <= 64);
            VERIFICA(conta(b, "&") == 3 && strchr(b, '#') == NULL && strchr(b, ' ') == NULL && strchr(b, '\n') == NULL &&
                     strchr(b, '\r') == NULL && conta(b, "?") == 1);
        } else {
            VERIFICA(b[0] == '\0');
        }
        free(copia);
    }
}

static void prova_ricerca_analisi(void)
{
    PhMeteoLuogo v[16];
    size_t n;
    VERIFICA(analizza_ric(RIC_TIPICA, sizeof RIC_TIPICA - 1, v, 10, &n) == 0 && n == 3);
    VERIFICA(strcmp(v[0].nome, "Milano") == 0 && strcmp(v[0].dettaglio, "Lombardia, Italia") == 0);
    VERIFICA(v[0].lat_centesimi == 4546 && v[0].lon_centesimi == 919);
    VERIFICA(strcmp(v[1].dettaglio, "Texas, Stati Uniti") == 0 && v[1].lat_centesimi == 4010 && v[1].lon_centesimi == -7520);
    VERIFICA(strcmp(v[2].nome, "Milan") == 0 && v[2].dettaglio[0] == '\0' && v[2].lat_centesimi == -3390 &&
             v[2].lon_centesimi == 15120);
    /* al massimo `massimo` */
    VERIFICA(analizza_ric(RIC_TIPICA, sizeof RIC_TIPICA - 1, v, 2, &n) == 0 && n == 2);
    VERIFICA(analizza_ric(RIC_TIPICA, sizeof RIC_TIPICA - 1, v, 0, &n) == -1 && n == 0);
    PhErrore e;
    VERIFICA(ph_meteo_analizza_ricerca(RIC_TIPICA, sizeof RIC_TIPICA - 1, NULL, 5, &n, &e) == -1 && n == 0);
    VERIFICA(ph_meteo_analizza_ricerca(RIC_TIPICA, sizeof RIC_TIPICA - 1, v, 5, NULL, &e) == -1);

    struct {
        const char *json;
        int atteso; /* 0 riesce, -1 errore */
        size_t luoghi;
        const char *nome0; /* se luoghi > 0 */
        int lat, lon;
    } t[] = {
        {"{\"generationtime_ms\":0.3}", 0, 0, NULL, 0, 0},
        {"{\"results\":null}", 0, 0, NULL, 0, 0},
        {"{\"results\":[]}", 0, 0, NULL, 0, 0},
        {"{\"error\":false,\"results\":[{\"name\":\"A\",\"latitude\":1,\"longitude\":2}]}", 0, 1, "A", 100, 200},
        {"{\"error\":true,\"reason\":\"Parameter name must be set\"}", -1, 0, NULL, 0, 0},
        {"{\"error\":true,\"results\":[{\"name\":\"A\",\"latitude\":1,\"longitude\":2}]}", -1, 0, NULL, 0, 0},
        {"{\"results\":{\"name\":\"A\"}}", -1, 0, NULL, 0, 0},
        {"{\"results\":5}", -1, 0, NULL, 0, 0},
        {"[]", -1, 0, NULL, 0, 0},
        {"", -1, 0, NULL, 0, 0},
        {"{\"results\":[{\"name\":\"A\"", -1, 0, NULL, 0, 0},
        /* elementi da saltare */
        {"{\"results\":[5,null,\"x\",[1],{\"latitude\":1,\"longitude\":2},{\"name\":\"\",\"latitude\":1,\"longitude\":2},"
         "{\"name\":\"\\u0007\\u0001\",\"latitude\":1,\"longitude\":2},{\"name\":\"B\",\"longitude\":2},"
         "{\"name\":\"B\",\"latitude\":\"1\",\"longitude\":2},{\"name\":\"B\",\"latitude\":1,\"longitude\":null},"
         "{\"name\":7,\"latitude\":1,\"longitude\":2},{\"name\":\"B\",\"latitude\":90.5,\"longitude\":2},"
         "{\"name\":\"B\",\"latitude\":-91,\"longitude\":2},{\"name\":\"B\",\"latitude\":1,\"longitude\":180.01},"
         "{\"name\":\"B\",\"latitude\":1,\"longitude\":-181},{\"name\":\"B\",\"latitude\":1e999,\"longitude\":2},"
         "{\"name\":\"Giusto\",\"latitude\":3,\"longitude\":4}]}", 0, 1, "Giusto", 300, 400},
        /* estremi ammessi e arrotondamento al centesimo piu' vicino, anche per i negativi */
        {"{\"results\":[{\"name\":\"E\",\"latitude\":90,\"longitude\":-180}]}", 0, 1, "E", 9000, -18000},
        {"{\"results\":[{\"name\":\"E\",\"latitude\":-90,\"longitude\":180}]}", 0, 1, "E", -9000, 18000},
        {"{\"results\":[{\"name\":\"R\",\"latitude\":-0.006,\"longitude\":0.004}]}", 0, 1, "R", -1, 0},
        {"{\"results\":[{\"name\":\"R\",\"latitude\":-0.004,\"longitude\":-0.0049}]}", 0, 1, "R", 0, 0},
        {"{\"results\":[{\"name\":\"R\",\"latitude\":-9.1951,\"longitude\":9.1949}]}", 0, 1, "R", -920, 919},
        {"{\"results\":[{\"name\":\"R\",\"latitude\":89.999,\"longitude\":179.999}]}", 0, 1, "R", 9000, 18000},
        /* nomi: byte non validi tolti, controlli tolti, surrogato isolato = U+FFFD, spazi ai lati tolti */
        {"{\"results\":[{\"name\":\"Mi\xFFlano\",\"latitude\":1,\"longitude\":2}]}", 0, 1, "Milano", 100, 200},
        {"{\"results\":[{\"name\":\"Mi\\u0007la\\u0085no\\n\",\"latitude\":1,\"longitude\":2}]}", 0, 1, "Milano", 100, 200},
        {"{\"results\":[{\"name\":\"  Roma  \",\"latitude\":1,\"longitude\":2}]}", 0, 1, "Roma", 100, 200},
        {"{\"results\":[{\"name\":\"x\\ud800y\",\"latitude\":1,\"longitude\":2}]}", 0, 1, "x\xEF\xBF\xBDy", 100, 200},
        {"{\"results\":[{\"name\":\"A\\u200bB\\u202eC\\ufeffD\\u2066E\\u2060F\\u200fG\\u2069H\\u200dI\",\"latitude\":1,\"longitude\":2}]}", 0, 1, "ABCDEFGHI", 100, 200},
        {"{\"results\":[{\"name\":\"A\xE2\x80\x8B" "B\xEF\xBB\xBF" "C\xE2\x80\xAE" "D\",\"latitude\":1,\"longitude\":2}]}", 0, 1, "ABCD", 100, 200},
        {"{\"results\":[{\"name\":\"\\u200b\\u202e\\ufeff\",\"latitude\":1,\"longitude\":2}]}", 0, 0, NULL, 0, 0},
        {"{\"results\":[{\"name\":\"\\ud83d\\ude00\",\"latitude\":1,\"longitude\":2}]}", 0, 1, "\xF0\x9F\x98\x80", 100, 200},
    };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
        int r = analizza_ric(t[i].json, strlen(t[i].json), v, 10, &n);
        VERIFICA(r == t[i].atteso);
        VERIFICA(r == 0 ? n == t[i].luoghi : n == 0);
        if (r == 0 && n == t[i].luoghi && n > 0) {
            VERIFICA(strcmp(v[0].nome, t[i].nome0) == 0 && v[0].lat_centesimi == t[i].lat && v[0].lon_centesimi == t[i].lon);
        }
        if (r != t[i].atteso || (r == 0 && n != t[i].luoghi))
            fprintf(stderr, "  caso ricerca %zu: r=%d n=%zu\n", i, r, n);
    }
    /* troncamento su confine di carattere: 200 x «e accentata» (2 byte), 100 x 3 byte, dettaglio unito */
    {
        char *g = malloc(4096);
        size_t o = (size_t)snprintf(g, 100, "{\"results\":[{\"name\":\"");
        for (int k = 0; k < 200; k++)
            o += (size_t)snprintf(g + o, 8, "\xC3\xA9");
        o += (size_t)snprintf(g + o, 100, "\",\"latitude\":1,\"longitude\":2}]}");
        VERIFICA(analizza_ric(g, o, v, 10, &n) == 0 && n == 1);
        VERIFICA(strlen(v[0].nome) == 126 && utf8_pulito(v[0].nome));
        o = (size_t)snprintf(g, 100, "{\"results\":[{\"name\":\"");
        for (int k = 0; k < 100; k++)
            o += (size_t)snprintf(g + o, 8, "\xE5\x90\x8D");
        o += (size_t)snprintf(g + o, 100, "\",\"latitude\":1,\"longitude\":2}]}");
        VERIFICA(analizza_ric(g, o, v, 10, &n) == 0 && n == 1);
        VERIFICA(strlen(v[0].nome) == 126 && utf8_pulito(v[0].nome));
        o = (size_t)snprintf(g, 100, "{\"results\":[{\"name\":\"N\",\"latitude\":1,\"longitude\":2,\"admin1\":\"");
        memset(g + o, 'a', 120);
        o += 120;
        o += (size_t)snprintf(g + o, 100, "\",\"country\":\"");
        for (int k = 0; k < 60; k++)
            o += (size_t)snprintf(g + o, 8, "\xC3\xA9");
        o += (size_t)snprintf(g + o, 100, "\"}]}");
        VERIFICA(analizza_ric(g, o, v, 10, &n) == 0 && n == 1);
        VERIFICA(strlen(v[0].dettaglio) <= 127 && utf8_pulito(v[0].dettaglio) && strncmp(v[0].dettaglio, "aaaa", 4) == 0);
        /* piu' elementi del massimo */
        o = (size_t)snprintf(g, 100, "{\"results\":[");
        for (int k = 0; k < 15; k++)
            o += (size_t)snprintf(g + o, 100, "%s{\"name\":\"C%d\",\"latitude\":1,\"longitude\":2}", k ? "," : "", k);
        o += (size_t)snprintf(g + o, 100, "]}");
        VERIFICA(analizza_ric(g, o, v, 10, &n) == 0 && n == 10 && strcmp(v[9].nome, "C9") == 0);
        free(g);
    }
    /* dimensione: 64 KiB esatti vanno (spazi in coda), un byte in piu' no */
    {
        size_t m = PH_METEO_RICERCA_RISPOSTA_MAX;
        char *g = malloc(m + 1);
        memset(g, ' ', m + 1);
        memcpy(g, "{\"results\":[]}", 14);
        VERIFICA(analizza_ric(g, m, v, 10, &n) == 0);
        VERIFICA(analizza_ric(g, m + 1, v, 10, &n) == -1 && n == 0);
        free(g);
    }
    /* profondita' e parentesi in quantita' */
    {
        size_t m = 60000;
        char *g = malloc(m);
        memset(g, '[', m);
        g[0] = '{';
        VERIFICA(analizza_ric(g, m, v, 10, &n) == -1 && n == 0);
        free(g);
    }

    /* fuzz dell'analisi: risposta vera mutata e byte a caso; invarianti sulle uscite */
    size_t n0 = sizeof RIC_TIPICA - 1;
    for (int it = 0; it < 30000; it++) {
        size_t m = n0;
        char *c = malloc(n0 + 64);
        memcpy(c, RIC_TIPICA, n0);
        switch (alea() % 5) {
        case 0:
            for (unsigned k = alea() % 8 + 1; k > 0; k--)
                c[alea() % m] = (char)alea();
            break;
        case 1:
            m = alea() % n0;
            break;
        case 2: {
            size_t a = alea() % n0, l = alea() % 40;
            if (a + l > n0)
                l = n0 - a;
            memmove(c + a, c + a + l, n0 - a - l);
            m = n0 - l;
            break;
        }
        case 3: {
            const char *sp = "{}[]\",:\\u-.eE0123456789ntf\xC3\xFF";
            size_t a = alea() % n0;
            memmove(c + a + 1, c + a, n0 - a);
            c[a] = sp[alea() % strlen(sp)];
            m = n0 + 1;
            break;
        }
        default:
            m = alea() % 200;
            for (size_t k = 0; k < m; k++)
                c[k] = (char)alea();
            break;
        }
        size_t massimo = 1 + alea() % 5;
        int r = analizza_ric(c, m, v, massimo, &n);
        free(c);
        if (r == 0) {
            VERIFICA(n <= massimo);
            for (size_t k = 0; k < n; k++) {
                VERIFICA(v[k].nome[0] != '\0' && strlen(v[k].nome) < sizeof v[k].nome && utf8_pulito(v[k].nome));
                VERIFICA(strlen(v[k].dettaglio) < sizeof v[k].dettaglio && utf8_pulito(v[k].dettaglio));
                VERIFICA(v[k].lat_centesimi >= -9000 && v[k].lat_centesimi <= 9000);
                VERIFICA(v[k].lon_centesimi >= -18000 && v[k].lon_centesimi <= 18000);
            }
        } else {
            VERIFICA(n == 0);
        }
    }
}

/* ---- Oggetto: ricerca ------------------------------------------------------------- */
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int n, n_ok, n_err;
    PhMeteoLuogo v[PH_METEO_LUOGHI_MAX];
    size_t nv;
    char frase[256];
    int dalla_principale;
    pthread_t principale;
    int blocca_ms;
    atomic_int finito;
    int richiedi_ancora;
    int azione; /* 1 = ph_meteo_cerca_annulla, 2 = ph_meteo_chiudi, 3 = ph_meteo_annulla (tutte vietate dentro) */
    PhMeteo *m;
    const char *testo;
} EsitoR;

static void er_init(EsitoR *x)
{
    memset(x, 0, sizeof *x);
    pthread_mutex_init(&x->mu, NULL);
    pthread_cond_init(&x->cv, NULL);
    x->principale = pthread_self();
}

static void er_fatto(const PhMeteoLuogo *v, size_t n, const char *frase, void *u)
{
    EsitoR *x = u;
    pthread_mutex_lock(&x->mu);
    x->n++;
    if (v != NULL) {
        x->n_ok++;
        x->nv = n;
        memcpy(x->v, v, n * sizeof *v);
    } else {
        x->n_err++;
        snprintf(x->frase, sizeof x->frase, "%s", frase ? frase : "(NULL)");
    }
    if (pthread_equal(pthread_self(), x->principale))
        x->dalla_principale++;
    pthread_cond_broadcast(&x->cv);
    if (x->blocca_ms > 0) {
        struct timespec t = scade_tra(x->blocca_ms);
        pthread_cond_timedwait(&x->cv, &x->mu, &t);
    }
    int ancora = x->richiedi_ancora > 0 ? x->richiedi_ancora-- : 0;
    pthread_mutex_unlock(&x->mu);
    if (ancora)
        ph_meteo_cerca(x->m, x->testo, "it", er_fatto, x, NULL);
    if (x->azione == 1)
        ph_meteo_cerca_annulla(x->m);
    else if (x->azione == 2)
        ph_meteo_chiudi(x->m);
    else if (x->azione == 3)
        ph_meteo_annulla(x->m);
    atomic_store(&x->finito, 1);
}

static int er_attendi(EsitoR *x, int n, int ms)
{
    struct timespec t = scade_tra(ms);
    pthread_mutex_lock(&x->mu);
    while (x->n < n && pthread_cond_timedwait(&x->cv, &x->mu, &t) == 0) {
    }
    int ok = x->n >= n;
    pthread_mutex_unlock(&x->mu);
    return ok;
}

static int cerca_attendi(PhMeteo *m, EsitoR *x, const char *testo, const char *lingua, int n_atteso)
{
    PhErrore e;
    if (ph_meteo_cerca(m, testo, lingua, er_fatto, x, &e) != 0)
        return 0;
    return er_attendi(x, n_atteso, 5000);
}

static void avanza(int s) { atomic_fetch_add(&g_mono, s); }

static int attendi_attese(PhMeteo *m, int almeno)
{
    int64_t fine = ph_meteo_ms() + 5000;
    while (ph_meteo_prova_attese_ricerca(m) < almeno && ph_meteo_ms() < fine)
        sched_yield();
    return ph_meteo_prova_attese_ricerca(m) >= almeno;
}

static void prova_ricerca_base(void)
{
    EsitoR x;
    er_init(&x);
    ripristina(M_OK, 0);
    PhMeteo *m = nuovo();
    VERIFICA(cerca_attendi(m, &x, "Milano", "it", 1) && x.n_ok == 1 && x.nv == 3);
    VERIFICA(strcmp(x.v[0].nome, "Milano") == 0 && x.v[0].lat_centesimi == 4546 && x.v[0].lon_centesimi == 919);
    VERIFICA(atomic_load(&S.conn) == 1);
    char host[128];
    int porta;
    ph_tls_finto_ultimo(host, sizeof host, &porta);
    VERIFICA(strcmp(host, PH_METEO_GEO_HOST) == 0 && porta == PH_METEO_PORTA);
    char rich[4096];
    pthread_mutex_lock(&S.mu);
    memcpy(rich, S.ultima, sizeof rich);
    pthread_mutex_unlock(&S.mu);
    VERIFICA(strncmp(rich, "GET /v1/search?name=Milano&count=10&language=it&format=json HTTP/1.1\r\n"
                           "Host: geocoding-api.open-meteo.com\r\nUser-Agent: YASDE/2.1\r\n",
                     strlen("GET /v1/search?name=Milano&count=10&language=it&format=json HTTP/1.1\r\n"
                            "Host: geocoding-api.open-meteo.com\r\nUser-Agent: YASDE/2.1\r\n")) == 0);
    VERIFICA(conta(rich, "\r\n") == 6);
    /* la stessa ricerca (anche con spazi ai lati) viene dalla memoria */
    VERIFICA(cerca_attendi(m, &x, "  Milano ", "it", 2) && x.n_ok == 2 && atomic_load(&S.conn) == 1);
    avanza(PH_METEO_INTERVALLO_S - 1);
    VERIFICA(cerca_attendi(m, &x, "Milano", "it", 3) && atomic_load(&S.conn) == 1);
    /* scaduti i 30 minuti, o con un'altra lingua, o un altro testo: rete */
    avanza(2);
    VERIFICA(cerca_attendi(m, &x, "Milano", "it", 4) && atomic_load(&S.conn) == 2);
    avanza(2);
    VERIFICA(cerca_attendi(m, &x, "Milano", "en", 5) && atomic_load(&S.conn) == 3);
    avanza(2);
    VERIFICA(cerca_attendi(m, &x, "Roma", "en", 6) && atomic_load(&S.conn) == 4);
    VERIFICA(x.dalla_principale == 0 && x.n_err == 0);
    /* risultati vuoti = riuscita con zero luoghi */
    static const char vuota[] = "{\"generationtime_ms\":0.1}";
    atomic_store(&S.ric_dati, vuota);
    atomic_store(&S.ric_n, sizeof vuota - 1);
    avanza(2);
    VERIFICA(cerca_attendi(m, &x, "Nullaqui", "it", 7) && x.n_ok == 7 && x.nv == 0);
    ph_meteo_chiudi(m);
}

static char *g_da_liberare;

static void prova_ricerca_errori(void)
{
    EsitoR x;
    er_init(&x);
    ripristina(M_OK, 0);
    atomic_store(&S.modo_ric, M_CODICE);
    atomic_store(&S.codice, 400);
    PhMeteo *m = nuovo();
    VERIFICA(cerca_attendi(m, &x, "SegretoMilano", "it", 1) && x.n_err == 1);
    VERIFICA(strstr(x.frase, "400") != NULL && strstr(x.frase, "Segreto") == NULL && strstr(x.frase, "search") == NULL);
    VERIFICA(strstr(x.frase, "meteo") == NULL && strstr(x.frase, "ricerca dei luoghi") != NULL);
    /* la stessa ricerca appena fallita: subito l'errore di prima, senza rete; dopo 30 s si riprova */
    VERIFICA(cerca_attendi(m, &x, "SegretoMilano", "it", 2) && x.n_err == 2 && atomic_load(&S.conn) == 1);
    avanza(PH_METEO_RICERCA_RIPROVA_S - 1);
    VERIFICA(cerca_attendi(m, &x, "SegretoMilano", "it", 3) && x.n_err == 3 && atomic_load(&S.conn) == 1);
    avanza(1); /* niente ritardo di 5 minuti come per le previsioni */
    atomic_store(&S.modo_ric, M_OK);
    VERIFICA(cerca_attendi(m, &x, "SegretoMilano", "it", 4) && x.n_ok == 1 && atomic_load(&S.conn) == 2);
    /* risposta {"error":true} servita con 200 */
    static const char err[] = "{\"error\":true,\"reason\":\"Parameter name must be set\"}";
    atomic_store(&S.ric_dati, err);
    atomic_store(&S.ric_n, sizeof err - 1);
    avanza(2);
    VERIFICA(cerca_attendi(m, &x, "SegretoRoma", "it", 5) && x.n_err == 4 && strstr(x.frase, "Segreto") == NULL);
    VERIFICA(strstr(x.frase, "meteo") == NULL);
    /* JSON rotto e risposta troppo grande */
    static const char rotto[] = "{\"results\":[{\"name\":";
    atomic_store(&S.ric_dati, rotto);
    atomic_store(&S.ric_n, sizeof rotto - 1);
    avanza(2);
    VERIFICA(cerca_attendi(m, &x, "SegretoTorino", "it", 6) && x.n_err == 5 && strstr(x.frase, "Segreto") == NULL);
    VERIFICA(strstr(x.frase, "meteo") == NULL && strstr(x.frase, "ricerca dei luoghi") != NULL);
    /* buffer che il server finto puo' ancora leggere dopo che il client ha rinunciato: si libera a fine prove */
    char *grande = malloc(PH_METEO_RICERCA_RISPOSTA_MAX + 100);
    g_da_liberare = grande;
    memset(grande, ' ', PH_METEO_RICERCA_RISPOSTA_MAX + 100);
    memcpy(grande, "{\"results\":[]}", 14);
    atomic_store(&S.ric_dati, grande);
    atomic_store(&S.ric_n, PH_METEO_RICERCA_RISPOSTA_MAX + 100);
    avanza(2);
    VERIFICA(cerca_attendi(m, &x, "SegretoNapoli", "it", 7) && x.n_err == 6 && strstr(x.frase, "grande") != NULL);
    VERIFICA(strstr(x.frase, "meteo") == NULL && strstr(x.frase, "Segreto") == NULL);
    /* certificato rifiutato (frase del TLS riscritta) e codice HTTP */
    atomic_store(&ph_tls_finto_rifiuta, 1);
    avanza(2);
    VERIFICA(cerca_attendi(m, &x, "SegretoGenova", "it", 8) && x.n_err == 7 && strstr(x.frase, "certificato") != NULL);
    VERIFICA(strstr(x.frase, "meteo") == NULL && strstr(x.frase, "Segreto") == NULL);
    atomic_store(&ph_tls_finto_rifiuta, 0);
    ph_meteo_chiudi(m);

    /* argomenti non validi: -1, nessuna richiesta, oggetto invariato */
    EsitoR y;
    er_init(&y);
    ripristina(M_OK, 0);
    m = nuovo();
    PhErrore e;
    VERIFICA(ph_meteo_cerca(NULL, "Milano", "it", er_fatto, &y, &e) == -1);
    VERIFICA(ph_meteo_cerca(m, "Milano", "it", NULL, &y, &e) == -1);
    VERIFICA(ph_meteo_cerca(m, "a", "it", er_fatto, &y, &e) == -1);
    VERIFICA(ph_meteo_cerca(m, "Milano", "IT", er_fatto, &y, &e) == -1);
    VERIFICA(ph_meteo_cerca(m, "Mil\nano", "it", er_fatto, &y, &e) == -1);
    VERIFICA(ph_meteo_cerca(m, "Mil\xFF" "ano", "it", er_fatto, &y, &e) == -1);
    VERIFICA(ph_meteo_cerca(m, NULL, "it", er_fatto, &y, &e) == -1);
    VERIFICA(atomic_load(&ph_tls_finto_aperture) == 0 && y.n == 0);
    ph_meteo_cerca_annulla(NULL);
    ph_meteo_chiudi(m);
    VERIFICA(ph_meteo_cerca(m, "Milano", "it", er_fatto, &y, &e) == -1);
    ph_meteo_cerca_annulla(m);
}

static void prova_ricerca_pausa_e_sostituzione(void)
{
    /* pausa minima tra l'AVVIO di due richieste in rete (tempo reale): la seconda
     * ASPETTA e poi parte; non fallisce */
    EsitoR x;
    er_init(&x);
    ripristina(M_OK, 0);
    ph_meteo_prova_pausa_ricerca_ms(800);
    PhMeteo *m = nuovo();
    VERIFICA(cerca_attendi(m, &x, "Milano", "it", 1) && atomic_load(&S.conn) == 1);
    int64_t t_a = ph_meteo_ms();
    PhErrore e;
    VERIFICA(ph_meteo_cerca(m, "Roma", "it", er_fatto, &x, &e) == 0);
    VERIFICA(attendi_attese(m, 1)); /* il thread e' fermo ad aspettare la pausa */
    VERIFICA(atomic_load(&S.conn) == 1 && x.n == 1);
    VERIFICA(er_attendi(&x, 2, 5000) && x.n_ok == 2 && atomic_load(&S.conn) == 2);
    VERIFICA(ph_meteo_ms() - t_a >= 600); /* la seconda ha aspettato */
    /* l'attesa si interrompe subito con cerca_annulla / chiudi */
    ph_meteo_prova_pausa_ricerca_ms(5000);
    int attese = ph_meteo_prova_attese_ricerca(m);
    VERIFICA(ph_meteo_cerca(m, "Torino", "it", er_fatto, &x, &e) == 0);
    VERIFICA(attendi_attese(m, attese + 1));
    int64_t t0 = ph_meteo_ms();
    ph_meteo_cerca_annulla(m);
    ph_meteo_chiudi(m);
    VERIFICA(ph_meteo_ms() - t0 < 1000 && x.n == 2 && atomic_load(&S.conn) == 2);

    /* una ricerca nuova sostituisce quella in corso: la vecchia si interrompe e non risponde */
    EsitoR a, b;
    er_init(&a);
    er_init(&b);
    ripristina(M_OK, 0);
    atomic_store(&S.modo_ric, M_SILENZIO);
    m = nuovo();
    VERIFICA(ph_meteo_cerca(m, "Milano", "it", er_fatto, &a, &e) == 0);
    VERIFICA(attendi_sem(&S.richiesta, 5000)); /* il server finto la tiene ferma */
    atomic_store(&S.modo_ric, M_OK);
    avanza(PH_METEO_RICERCA_PAUSA_S);
    VERIFICA(cerca_attendi(m, &b, "Roma", "it", 1) && b.n_ok == 1);
    ph_meteo_chiudi(m);
    VERIFICA(a.n == 0 && atomic_load(&S.conn) == 2);

    /* ricerca e previsione indipendenti: la ricerca passa mentre la previsione e' ferma */
    Esito f;
    esito_init(&f);
    EsitoR r;
    er_init(&r);
    ripristina(M_OK, 0);
    atomic_store(&ph_tls_finto_trattieni, 1);
    m = nuovo();
    VERIFICA(ph_meteo_chiedi(m, 45460, 9190, esito_fatto, &f, &e) == 0);
    VERIFICA(attendi_sem(&ph_tls_finto_in_apri, 5000)); /* la previsione e' ferma nel collegamento */
    VERIFICA(cerca_attendi(m, &r, "Milano", "it", 1) && r.n_ok == 1);
    VERIFICA(f.n == 0); /* la previsione non e' ancora arrivata */
    atomic_store(&ph_tls_finto_trattieni, 0);
    VERIFICA(attendi_n(&f, 1, 5000) && f.n_ok == 1);
    ph_meteo_chiudi(m);
}

static void prova_ricerca_annulla_chiudi(void)
{
    /* op: 0 cerca_annulla, 1 annulla (tutto), 2 chiudi; fase: 0 silenzio, 1 risposta a meta' */
    for (int op = 0; op <= 2; op++)
        for (int fase = 0; fase <= 1; fase++) {
            EsitoR x;
            er_init(&x);
            ripristina(M_OK, 0);
            atomic_store(&S.modo_ric, fase == 0 ? M_SILENZIO : M_LENTA);
            PhMeteo *m = nuovo();
            PhErrore e;
            VERIFICA(ph_meteo_cerca(m, "Milano", "it", er_fatto, &x, &e) == 0);
            VERIFICA(attendi_sem(fase == 0 ? &S.richiesta : &S.inviato, 5000));
            int64_t t0 = ph_meteo_ms();
            if (op == 0)
                ph_meteo_cerca_annulla(m);
            else if (op == 1)
                ph_meteo_annulla(m);
            else
                ph_meteo_chiudi(m);
            VERIFICA(ph_meteo_ms() - t0 < 1000);
            if (op != 2) {
                /* ancora usabile: dopo la pausa la ricerca riesce */
                atomic_store(&S.modo_ric, M_OK);
                avanza(PH_METEO_RICERCA_PAUSA_S);
                VERIFICA(cerca_attendi(m, &x, "Milano", "it", 1) && x.n_ok == 1);
                ph_meteo_chiudi(m);
            }
            VERIFICA(x.n == (op == 2 ? 0 : 1));
        }
}

static void prova_ricerca_callback(void)
{
    /* chiudi, annulla e cerca_annulla aspettano la callback in corso */
    for (int op = 0; op <= 2; op++) {
        EsitoR x;
        er_init(&x);
        x.blocca_ms = 300;
        ripristina(M_OK, 0);
        PhMeteo *m = nuovo();
        VERIFICA(cerca_attendi(m, &x, "Milano", "it", 1));
        if (op == 0)
            ph_meteo_cerca_annulla(m);
        else if (op == 1)
            ph_meteo_annulla(m);
        else
            ph_meteo_chiudi(m);
        VERIFICA(atomic_load(&x.finito) == 1);
        if (op != 2)
            ph_meteo_chiudi(m);
    }
    /* dalla callback: cerca va (dalla memoria); cerca_annulla, annulla, chiudi sono vietate ma innocue */
    for (int azione = 0; azione <= 3; azione++) {
        EsitoR x;
        er_init(&x);
        ripristina(M_OK, 0);
        PhMeteo *m = nuovo();
        x.m = m;
        x.testo = "Milano";
        if (azione == 0)
            x.richiedi_ancora = 1;
        else
            x.azione = azione;
        PhErrore e;
        VERIFICA(ph_meteo_cerca(m, "Milano", "it", er_fatto, &x, &e) == 0);
        VERIFICA(er_attendi(&x, 1, 5000));
        if (azione == 0) {
            VERIFICA(er_attendi(&x, 2, 5000) && atomic_load(&S.conn) == 1);
        } else {
            VERIFICA(attendi_flag(&x.finito, 5000)); /* nessun blocco */
            atomic_store(&x.finito, 0);
            /* l'oggetto e' ancora vivo (dalla memoria) */
            VERIFICA(ph_meteo_cerca(m, "Milano", "it", er_fatto, &x, &e) == 0);
            VERIFICA(er_attendi(&x, 2, 5000) && attendi_flag(&x.finito, 5000));
        }
        ph_meteo_chiudi(m);
    }
}

/* Tetto alle ricerche in rete: 10 per finestra mobile di 10 minuti. */
static void prova_ricerca_tetto(void)
{
    EsitoR x;
    er_init(&x);
    ripristina(M_OK, 0);
    PhMeteo *m = nuovo();
    char t[32];
    int n = 0;
    for (int k = 0; k < PH_METEO_RICERCA_TETTO; k++) {
        snprintf(t, sizeof t, "Citta%d", k);
        VERIFICA(cerca_attendi(m, &x, t, "it", ++n) && x.n_ok == n);
        avanza(2);
    }
    VERIFICA(atomic_load(&S.conn) == PH_METEO_RICERCA_TETTO); /* avvii a 1000, 1002, ..., 1018 */
    /* l'undicesima e' rifiutata SUBITO, senza rete, con una frase che non ha il testo */
    int aperture = atomic_load(&ph_tls_finto_aperture);
    VERIFICA(cerca_attendi(m, &x, "SegretoUndici", "it", ++n) && x.n_err == 1);
    VERIFICA(strstr(x.frase, "troppe ricerche") != NULL && strstr(x.frase, "Segreto") == NULL &&
             strstr(x.frase, "meteo") == NULL);
    VERIFICA(atomic_load(&S.conn) == PH_METEO_RICERCA_TETTO && atomic_load(&ph_tls_finto_aperture) == aperture);
    /* una risposta dalla memoria non conta e passa anche oltre il tetto */
    VERIFICA(cerca_attendi(m, &x, "Citta9", "it", ++n) && x.n_ok == PH_METEO_RICERCA_TETTO + 1);
    VERIFICA(atomic_load(&S.conn) == PH_METEO_RICERCA_TETTO);
    /* finestra mobile: l'avvio piu' vecchio (1000) esce dalla finestra a 1600, non prima */
    atomic_store(&g_mono, 1000 + PH_METEO_RICERCA_FINESTRA_S - 1);
    VERIFICA(cerca_attendi(m, &x, "SegretoUndici", "it", ++n) && x.n_err == 2);
    VERIFICA(atomic_load(&S.conn) == PH_METEO_RICERCA_TETTO);
    atomic_store(&g_mono, 1000 + PH_METEO_RICERCA_FINESTRA_S);
    VERIFICA(cerca_attendi(m, &x, "Undici", "it", ++n) && x.n_ok == PH_METEO_RICERCA_TETTO + 2);
    VERIFICA(atomic_load(&S.conn) == PH_METEO_RICERCA_TETTO + 1);
    /* un orologio che torna indietro non libera nulla */
    atomic_store(&g_mono, 500);
    VERIFICA(cerca_attendi(m, &x, "SegretoDodici", "it", ++n) && x.n_err == 3);
    VERIFICA(atomic_load(&S.conn) == PH_METEO_RICERCA_TETTO + 1);
    ph_meteo_chiudi(m);

    /* le ricerche annullate contano: 10 avvii interrotti, l'undicesima e' rifiutata */
    EsitoR y;
    er_init(&y);
    ripristina(M_OK, 0);
    atomic_store(&S.modo_ric, M_SILENZIO);
    m = nuovo();
    PhErrore e;
    for (int k = 0; k < PH_METEO_RICERCA_TETTO; k++) {
        VERIFICA(ph_meteo_cerca(m, "Roma", "it", er_fatto, &y, &e) == 0);
        VERIFICA(attendi_sem(&S.richiesta, 5000)); /* la richiesta e' partita in rete */
        ph_meteo_cerca_annulla(m);
        avanza(2);
    }
    VERIFICA(y.n == 0 && atomic_load(&S.conn) == PH_METEO_RICERCA_TETTO);
    /* (il modo del server NON si cambia qui: la 11a non tocca la rete, e cambiarlo mentre
     * il server sta ancora decidendo come trattare la 10a lascerebbe finire quella con successo) */
    int undicesima = cerca_attendi(m, &y, "Roma", "it", 1);
    VERIFICA(undicesima && y.n_err == 1 && strstr(y.frase, "troppe ricerche") != NULL);
    if (!(undicesima && y.n_err == 1 && strstr(y.frase, "troppe ricerche") != NULL))
        fprintf(stderr, "  undicesima: arrivata=%d ok=%d err=%d frase=«%s» conn=%d\n", undicesima, y.n_ok, y.n_err, y.frase,
                atomic_load(&S.conn));
    VERIFICA(atomic_load(&S.conn) == PH_METEO_RICERCA_TETTO);
    ph_meteo_chiudi(m);

    /* la stessa ricerca fallita non si riprova prima di 30 s; le rifiutate per il tetto non sono «fallite» */
    EsitoR z;
    er_init(&z);
    ripristina(M_OK, 0);
    atomic_store(&S.modo_ric, M_CODICE);
    atomic_store(&S.codice, 500);
    m = nuovo();
    VERIFICA(cerca_attendi(m, &z, "Roma", "it", 1) && z.n_err == 1 && atomic_load(&S.conn) == 1);
    VERIFICA(strstr(z.frase, "500") != NULL && strstr(z.frase, "meteo") == NULL);
    VERIFICA(cerca_attendi(m, &z, "Roma", "it", 2) && z.n_err == 2 && atomic_load(&S.conn) == 1);
    VERIFICA(cerca_attendi(m, &z, "Torino", "it", 3) && atomic_load(&S.conn) == 2); /* un'altra ricerca va in rete */
    atomic_store(&S.modo_ric, M_OK);
    avanza(PH_METEO_RICERCA_RIPROVA_S - 1);
    VERIFICA(cerca_attendi(m, &z, "Roma", "it", 4) && z.n_err == 4 && atomic_load(&S.conn) == 2);
    avanza(1);
    VERIFICA(cerca_attendi(m, &z, "Roma", "it", 5) && z.n_ok == 1 && atomic_load(&S.conn) == 3);
    ph_meteo_chiudi(m);
}

int main(int argc, char **argv)
{
    int solo_thread = argc > 1 && strcmp(argv[1], "concorrenza") == 0;
    /* Per tutte le prove una lingua con la virgola decimale (se il sistema ce
     * l'ha): percorso e analisi devono comportarsi uguale. */
    const char *lingue[] = {"it_IT.UTF-8", "it_IT.utf8", "it_IT", "de_DE.UTF-8"};
    for (size_t i = 0; i < sizeof lingue / sizeof *lingue; i++)
        if (setlocale(LC_ALL, lingue[i]) != NULL)
            break;
    ph_meteo_prova_orologio(f_mono, f_unix);
    server_avvia();
    if (!solo_thread) {
        prova_percorso();
        prova_analisi_valida();
        prova_analisi_rotta();
        prova_fuzz();
        prova_http();
        prova_http_rami();
        prova_http_fuzz();
        prova_ricerca_percorso();
        prova_ricerca_analisi();
    }
    prova_argomenti();
    prova_estremi();
    prova_frequenza();
    prova_fallimenti();
    prova_annulla_chiudi_fasi();
    prova_sostituzione();
    prova_callback();
    prova_tentativi();
    prova_chiedi_annulla();
    prova_avvii();
    prova_callback_vietati();
    prova_ricerca_base();
    prova_ricerca_errori();
    prova_ricerca_pausa_e_sostituzione();
    prova_ricerca_annulla_chiudi();
    prova_ricerca_callback();
    prova_ricerca_tetto();
    prova_parallelo();
    server_ferma();
    free(g_da_liberare);
    if (g_fallite) {
        fprintf(stderr, "prova_meteo: %d verifiche fallite\n", g_fallite);
        return 1;
    }
    printf("prova_meteo: tutto ok\n");
    return 0;
}
