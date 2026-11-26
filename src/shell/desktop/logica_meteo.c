/* Logica pura del widget Meteo (logica_meteo.h). */
#include "logica_meteo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config/lingua.h"
#include "logica_wx.h"

const char *ph_dsk_meteo_stile_nome(unsigned stile)
{
    static const char *const nomi[PH_DSK_METEO_STILI] = {PH_T_("Adesso"), PH_T_("Ore"),     PH_T_("Settimana"),
                                                         PH_T_("Compatto"), PH_T_("Oggi"), PH_T_("Curva")};
    return stile < PH_DSK_METEO_STILI ? ph_tv(nomi[stile]) : NULL;
}

PhDskCond ph_dsk_meteo_gruppo(int c)
{
    if (c == 0)
        return PH_DSK_COND_SERENO;
    if (c == 1 || c == 2)
        return PH_DSK_COND_POCO_NUVOLOSO;
    if (c == 3)
        return PH_DSK_COND_NUVOLOSO;
    if (c == 45 || c == 48)
        return PH_DSK_COND_NEBBIA;
    if (c >= 51 && c <= 57)
        return PH_DSK_COND_PIOGGERELLA;
    if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82))
        return PH_DSK_COND_PIOGGIA;
    if ((c >= 71 && c <= 77) || c == 85 || c == 86)
        return PH_DSK_COND_NEVE;
    if (c >= 95 && c <= 99)
        return PH_DSK_COND_TEMPORALE;
    return PH_DSK_COND_NESSUNA;
}

const char *ph_dsk_meteo_condizione(int c)
{
    switch (c) {
    case 0: return ph_t("Sereno");
    case 1: return ph_t("Prevalentemente sereno");
    case 2: return ph_t("Poco nuvoloso");
    case 3: return ph_t("Nuvoloso");
    case 45: return ph_t("Nebbia");
    case 48: return ph_t("Nebbia con brina");
    case 51: return ph_t("Pioggerella debole");
    case 53: return ph_t("Pioggerella");
    case 55: return ph_t("Pioggerella intensa");
    case 56:
    case 57: return ph_t("Pioggerella gelata");
    case 61: return ph_t("Pioggia debole");
    case 63: return ph_t("Pioggia");
    case 65: return ph_t("Pioggia forte");
    case 66:
    case 67: return ph_t("Pioggia gelata");
    case 71: return ph_t("Neve debole");
    case 73: return ph_t("Neve");
    case 75: return ph_t("Neve intensa");
    case 77: return ph_t("Granelli di neve");
    case 80: return ph_t("Rovesci deboli");
    case 81: return ph_t("Rovesci");
    case 82: return ph_t("Rovesci violenti");
    case 85: return ph_t("Rovesci di neve");
    case 86: return ph_t("Rovesci di neve intensi");
    case 95: return ph_t("Temporale");
    case 96:
    case 99: return ph_t("Temporale con grandine");
    default: return "";
    }
}

/* ---- Valori ------------------------------------------------------------------------------ */
#define TRATTINO "\xE2\x80\x94"

static bool noto(double v, double limite)
{
    return isfinite(v) && fabs(v) <= limite;
}

static void scrivi(char *out, size_t cap, const char *s)
{
    if (cap > 0)
        snprintf(out, cap, "%s", s);
}

void ph_dsk_meteo_temp(double gradi, char *out, size_t cap)
{
    if (!noto(gradi, 150)) {
        scrivi(out, cap, TRATTINO);
        return;
    }
    long r = (long)floor(gradi + 0.5);
    if (cap > 0)
        snprintf(out, cap, "%ld\xC2\xB0", r); /* r == 0 non porta mai il segno: long non ha lo zero negativo */
}

static void decimale1(double v, char *numero, size_t cap)
{
    char sep = ph_lingua_attiva() == PH_LINGUA_IT ? ',' : '.';
    long dec = (long)floor(fabs(v) * 10.0 + 0.5);
    bool neg = v < 0 && dec != 0;
    snprintf(numero, cap, "%s%ld%c%ld", neg ? "-" : "", dec / 10, sep, dec % 10);
}

void ph_dsk_meteo_temp_c(double gradi, char *out, size_t cap)
{
    if (!noto(gradi, 150)) {
        scrivi(out, cap, TRATTINO);
        return;
    }
    char n[32];
    decimale1(gradi, n, sizeof n);
    ph_lingua_componi(out, cap, ph_t("{numero} °C"), "numero", n, (const char *)NULL);
}

void ph_dsk_meteo_mm(double mm, char *out, size_t cap)
{
    if (!noto(mm, 2000) || mm < 0) {
        scrivi(out, cap, TRATTINO);
        return;
    }
    char n[32];
    decimale1(mm, n, sizeof n);
    ph_lingua_componi(out, cap, ph_t("{numero} mm"), "numero", n, (const char *)NULL);
}

void ph_dsk_meteo_vento(double kmh, char *out, size_t cap)
{
    if (!noto(kmh, 1000) || kmh < 0) {
        scrivi(out, cap, TRATTINO);
        return;
    }
    char n[24];
    snprintf(n, sizeof n, "%ld", (long)floor(kmh + 0.5));
    ph_lingua_componi(out, cap, ph_t("{numero} km/h"), "numero", n, (const char *)NULL);
}

void ph_dsk_meteo_percento(double v, char *out, size_t cap)
{
    if (!isfinite(v)) {
        scrivi(out, cap, TRATTINO);
        return;
    }
    if (v < 0)
        v = 0;
    if (v > 100)
        v = 100;
    char n[24];
    snprintf(n, sizeof n, "%ld", (long)floor(v + 0.5));
    ph_lingua_componi(out, cap, ph_t("{numero} %"), "numero", n, (const char *)NULL);
}

void ph_dsk_meteo_titolo(const char *localita, char *out, size_t cap)
{
    if (cap == 0)
        return;
    if (localita == NULL || localita[0] == '\0') {
        scrivi(out, cap, ph_t("Meteo"));
        return;
    }
    ph_lingua_componi(out, cap, ph_t("Meteo — {localita}"), "localita", localita, (const char *)NULL);
}

/* Secondi del giorno e giorno civile di un istante nel fuso del luogo. */
static bool locale(int64_t ora_s, int scarto_s, int64_t *giorno, int *sec_giorno)
{
    if (ora_s < 0 || ora_s > 253402300799LL || scarto_s < -86400 || scarto_s > 86400)
        return false;
    int64_t t = ora_s + scarto_s;
    int64_t g = t / 86400;
    int64_t r = t % 86400;
    if (r < 0) {
        r += 86400;
        g--;
    }
    *giorno = g;
    *sec_giorno = (int)r;
    return true;
}

void ph_dsk_meteo_ora_luogo(int64_t ora_s, int scarto_s, char *out, size_t cap)
{
    int64_t g;
    int s;
    if (cap == 0)
        return;
    if (!locale(ora_s, scarto_s, &g, &s)) {
        scrivi(out, cap, TRATTINO);
        return;
    }
    snprintf(out, cap, "%02d:%02d", s / 3600, (s % 3600) / 60);
}

void ph_dsk_meteo_ora_breve(int64_t ora_s, int scarto_s, char *out, size_t cap)
{
    int64_t g;
    int s;
    if (cap == 0)
        return;
    if (!locale(ora_s, scarto_s, &g, &s)) {
        scrivi(out, cap, TRATTINO);
        return;
    }
    snprintf(out, cap, "%02d", s / 3600);
}

void ph_dsk_meteo_giorno_nome(int64_t giorno_s, int64_t oggi_s, int scarto_s, char *out, size_t cap)
{
    int64_t g, go;
    int s;
    if (cap == 0)
        return;
    /* `giorno` e' la mezzanotte LOCALE del luogo espressa in secondi Unix: aggiungendo lo scarto
     * si ha la mezzanotte «in UTC» del giorno civile. */
    if (!locale(giorno_s, scarto_s, &g, &s) || !locale(oggi_s, scarto_s, &go, &s)) {
        scrivi(out, cap, TRATTINO);
        return;
    }
    if (g == go)
        scrivi(out, cap, ph_t("Oggi"));
    else
        scrivi(out, cap, ph_dsk_giorno_breve(ph_dsk_giorno_settimana_di(g)));
}

/* ---- Quanto mostrare ------------------------------------------------------------------------------ */
static size_t per_dim(PhCfgWidgetDim d, size_t p, size_t m, size_t g)
{
    return d == PH_CFG_WIDGET_PICCOLO ? p : (d == PH_CFG_WIDGET_GRANDE ? g : m);
}

size_t ph_dsk_meteo_n_ore(unsigned stile, PhCfgWidgetDim d)
{
    switch (stile) {
    case PH_DSK_METEO_ORE: return per_dim(d, 4, 5, 7);
    case PH_DSK_METEO_OGGI: return d == PH_CFG_WIDGET_GRANDE ? 6 : 0;
    case PH_DSK_METEO_CURVA: return per_dim(d, 12, 12, 24);
    default: return 0;
    }
}

size_t ph_dsk_meteo_n_giorni(unsigned stile, PhCfgWidgetDim d)
{
    return stile == PH_DSK_METEO_SETTIMANA ? per_dim(d, 3, 5, 7) : 0;
}

size_t ph_dsk_meteo_n_dettagli(PhCfgWidgetDim d)
{
    return per_dim(d, 2, 3, 4);
}

PhDskMeteoSezioni ph_dsk_meteo_sezioni(int larghezza, int altezza)
{
    PhDskMeteoSezioni s = {0};
    if (larghezza > 100000)
        larghezza = 100000;
    if (altezza > 100000)
        altezza = 100000;
    s.testo = larghezza >= PH_DSK_METEO_ADESSO_LARG_TESTO;
    /* Anche piccolo il numero e l'icona sono medi: la mattonella al minimo non resta vuota. */
    s.corpo_temp = larghezza >= 300 && altezza >= 300 ? 2 : 1;
    s.lato_icona = s.corpo_temp == 2 ? 56 : 48;
    if (larghezza < PH_DSK_METEO_ADESSO_LARG_CURVA || altezza < PH_DSK_METEO_ADESSO_ALT_CURVA)
        return s;
    s.curva = true;
    s.n_ore = larghezza >= 400 ? 16 : (larghezza >= 300 ? 12 : 8);
    int resto = altezza - PH_DSK_METEO_ADESSO_ALT_CURVA;
    size_t g = (size_t)(resto / PH_DSK_METEO_ADESSO_ALT_GIORNO);
    if (g > PH_METEO_GIORNI_MAX)
        g = PH_METEO_GIORNI_MAX;
    s.n_giorni = g >= PH_DSK_METEO_ADESSO_GIORNI_MIN ? g : 0;
    resto -= (int)s.n_giorni * PH_DSK_METEO_ADESSO_ALT_GIORNO;
    if (resto > PH_DSK_METEO_ADESSO_ALT_CURVA_EXTRA)
        resto = PH_DSK_METEO_ADESSO_ALT_CURVA_EXTRA;
    s.altezza_curva = PH_DSK_METEO_ADESSO_ALT_CURVA_BASE + resto;
    return s;
}

bool ph_dsk_meteo_forma_breve(int disponibile, int larghezza_lunga)
{
    /* Misure non utilizzabili (ancora non note): si resta sulla forma lunga, che va a capo da sola. */
    if (disponibile <= 0 || larghezza_lunga <= 0)
        return false;
    return larghezza_lunga > disponibile;
}

bool ph_dsk_meteo_sezioni_uguali(const PhDskMeteoSezioni *a, const PhDskMeteoSezioni *b)
{
    return a->testo == b->testo && a->curva == b->curva && a->n_ore == b->n_ore && a->n_giorni == b->n_giorni &&
           a->altezza_curva == b->altezza_curva && a->corpo_temp == b->corpo_temp && a->lato_icona == b->lato_icona;
}

bool ph_dsk_meteo_estremi(const PhMeteoGiorno *g, size_t n, double *minimo, double *massimo)
{
    bool c_e = false;
    for (size_t i = 0; g != NULL && i < n; i++) {
        double a = g[i].minima, b = g[i].massima;
        if (noto(a, 150)) {
            if (!c_e || a < *minimo)
                *minimo = a;
            if (!c_e || a > *massimo)
                *massimo = a;
            c_e = true;
        }
        if (noto(b, 150)) {
            if (!c_e || b < *minimo)
                *minimo = b;
            if (!c_e || b > *massimo)
                *massimo = b;
            c_e = true;
        }
    }
    return c_e;
}

bool ph_dsk_meteo_barra(double min, double max, double gmin, double gmax, double *da, double *a)
{
    if (!noto(min, 150) || !noto(max, 150) || !noto(gmin, 150) || !noto(gmax, 150))
        return false;
    if (max < min) {
        double t = min;
        min = max;
        max = t;
    }
    double span = gmax - gmin;
    if (span <= 0.0) {
        *da = 0.0;
        *a = 1.0;
        return true;
    }
    double x = (min - gmin) / span, y = (max - gmin) / span;
    x = x < 0 ? 0 : (x > 1 ? 1 : x);
    y = y < 0 ? 0 : (y > 1 ? 1 : y);
    const double minima_larghezza = 0.10; /* anche con minima = massima la barra si vede */
    if (y - x < minima_larghezza) {
        double c = (x + y) / 2.0;
        x = c - minima_larghezza / 2.0;
        y = c + minima_larghezza / 2.0;
        if (x < 0) {
            y -= x;
            x = 0;
        }
        if (y > 1) {
            x -= y - 1;
            y = 1;
        }
    }
    *da = x;
    *a = y;
    return true;
}

/* ---- Stati --------------------------------------------------------------------------------------------- */
void ph_dsk_meteo_stato(PhSrvMeteoStato stato, bool ha_dati, const char *frase, int64_t ricevuta_s, int scarto_s,
                        bool luogo_scelto, PhDskMeteoStato *out)
{
    memset(out, 0, sizeof *out);
    char ora[16];
    ph_dsk_meteo_ora_luogo(ricevuta_s, scarto_s, ora, sizeof ora);
    bool vecchi_da_dire = ha_dati && ricevuta_s > 0;
    switch (stato) {
    case PH_SRV_METEO_PRONTO:
        if (ha_dati)
            out->mostra_dati = true;
        else
            scrivi(out->messaggio, sizeof out->messaggio, ph_t("Previsioni non disponibili"));
        break;
    case PH_SRV_METEO_NON_DISPONIBILE:
        scrivi(out->messaggio, sizeof out->messaggio, ph_t("Previsioni non disponibili"));
        out->mostra_dati = ha_dati;
        out->grigio = true;
        if (vecchi_da_dire)
            ph_lingua_componi(out->dettaglio, sizeof out->dettaglio, ph_t("ultimo dato alle {ora}"), "ora", ora,
                              (const char *)NULL);
        break;
    case PH_SRV_METEO_SENZA_POSIZIONE:
        scrivi(out->messaggio, sizeof out->messaggio,
               (frase != NULL && frase[0] != '\0') ? frase : ph_t("Il telefono non dà la sua posizione"));
        out->mostra_dati = ha_dati;
        out->grigio = true;
        out->invito_localita = !luogo_scelto;
        if (vecchi_da_dire)
            ph_lingua_componi(out->dettaglio, sizeof out->dettaglio, ph_t("ultimo dato alle {ora}"), "ora", ora,
                              (const char *)NULL);
        break;
    case PH_SRV_METEO_NON_COLLEGATO:
        scrivi(out->messaggio, sizeof out->messaggio, ph_t("Telefono non collegato"));
        out->mostra_dati = ha_dati;
        out->grigio = true;
        out->invito_localita = !luogo_scelto;
        if (vecchi_da_dire)
            ph_lingua_componi(out->dettaglio, sizeof out->dettaglio, ph_t("ultimo dato alle {ora}"), "ora", ora,
                              (const char *)NULL);
        break;
    case PH_SRV_METEO_IN_ATTESA:
    case PH_SRV_METEO_SPENTO:
    default:
        /* Aspettando la prima risposta: con dati precedenti si vedono (non grigi), senza
         * si dice che si sta aggiornando. */
        if (ha_dati)
            out->mostra_dati = true;
        else
            scrivi(out->messaggio, sizeof out->messaggio, ph_t("Aggiornamento…"));
        break;
    }
}

/* ---- Citta' scelta a mano nel profilo --------------------------------------------------------------------- */
void ph_dsk_tronca_utf8(const char *src, char *out, size_t cap)
{
    if (cap == 0)
        return;
    out[0] = '\0';
    if (src == NULL)
        return;
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80)
            n--; /* il byte che resterebbe fuori e' una continuazione: si arretra al carattere intero */
    }
    memcpy(out, src, n);
    out[n] = '\0';
    /* Testo non UTF-8 valido (dato ostile): si taglia fino al primo punto valido. */
    while (n > 0 && !g_utf8_validate(out, -1, NULL))
        out[--n] = '\0';
}

bool ph_dsk_luogo_scrivi(PhCfgTelefono *p, const char *nome, const char *dettaglio, int32_t lat, int32_t lon)
{
    if (p == NULL || nome == NULL || lat < -9000 || lat > 9000 || lon < -18000 || lon > 18000)
        return false;
    char n[sizeof p->meteo_luogo_nome], d[sizeof p->meteo_luogo_dettaglio];
    ph_dsk_tronca_utf8(nome, n, sizeof n);
    ph_dsk_tronca_utf8(dettaglio, d, sizeof d);
    if (n[0] == '\0')
        return false;
    p->meteo_luogo_scelto = true;
    memcpy(p->meteo_luogo_nome, n, sizeof n);
    memcpy(p->meteo_luogo_dettaglio, d, sizeof d);
    p->meteo_luogo_lat_centesimi = lat;
    p->meteo_luogo_lon_centesimi = lon;
    return true;
}

void ph_dsk_luogo_azzera(PhCfgTelefono *p)
{
    if (p == NULL)
        return;
    p->meteo_luogo_scelto = false;
    p->meteo_luogo_nome[0] = '\0';
    p->meteo_luogo_dettaglio[0] = '\0';
    p->meteo_luogo_lat_centesimi = 0;
    p->meteo_luogo_lon_centesimi = 0;
}

bool ph_dsk_meteo_valore_valido(double v)
{
    return noto(v, 150);
}
