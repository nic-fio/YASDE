/* Logica pura dei widget nuovi (logica_wx.h): nessun widget, si prova senza schermo. */
#include "logica_wx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/lingua.h"

/* ---- Utilita' ------------------------------------------------------------------------ */
static void azzera(PhDskVista *v)
{
    memset(v, 0, sizeof *v);
    v->percento = -1;
}

static void copia(char *dst, size_t cap, const char *src)
{
    if (cap > 0)
        snprintf(dst, cap, "%s", src != NULL ? src : "");
}

static void riga(PhDskVista *v, const char *sx, const char *dx)
{
    if (v->n_righe >= PH_DSK_RIGHE_MAX)
        return;
    PhDskRiga *r = &v->righe[v->n_righe++];
    copia(r->sinistra, sizeof r->sinistra, sx);
    copia(r->destra, sizeof r->destra, dx);
}

static void maiuscola(const char *in, char *out, size_t cap)
{
    if (cap == 0)
        return;
    out[0] = '\0';
    if (in == NULL || in[0] == '\0')
        return;
    char m[8] = {0};
    int n = g_unichar_to_utf8(g_unichar_toupper(g_utf8_get_char(in)), m);
    m[n] = '\0';
    snprintf(out, cap, "%s%s", m, g_utf8_next_char(in));
}

static int64_t div_pavimento(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b < 0) ? q - 1 : q;
}

/* Il fuso: NULL = quello locale (si crea e si libera dentro la funzione chiamante). */
static GTimeZone *fuso(GTimeZone *tz)
{
    return tz != NULL ? g_time_zone_ref(tz) : g_time_zone_new_local();
}

/* ---- Stili --------------------------------------------------------------------------------- */
size_t ph_dsk_stili_n(PhCfgWidgetTipo tipo)
{
    if (tipo == PH_CFG_WIDGET_OROLOGIO)
        return PH_DSK_OROLOGIO_STILI;
    if (tipo == PH_CFG_WIDGET_CALENDARIO_MESE)
        return PH_DSK_CAL_STILI;
    if (tipo == PH_CFG_WIDGET_METEO)
        return PH_DSK_METEO_STILI;
    return 1;
}

unsigned ph_dsk_stile_valido_di(PhCfgWidgetTipo tipo, unsigned stile)
{
    return stile < ph_dsk_stili_n(tipo) ? stile : 0;
}

const char *ph_dsk_stile_nome_di(PhCfgWidgetTipo tipo, unsigned stile)
{
    if (tipo == PH_CFG_WIDGET_OROLOGIO)
        return ph_dsk_stile_nome(stile);
    if (tipo == PH_CFG_WIDGET_METEO)
        return ph_dsk_meteo_stile_nome(stile);
    if (tipo == PH_CFG_WIDGET_CALENDARIO_MESE) {
        static const char *const nomi[PH_DSK_CAL_STILI] = {PH_T_("Mese"), PH_T_("Settimana"), PH_T_("Agenda")};
        return stile < PH_DSK_CAL_STILI ? ph_tv(nomi[stile]) : NULL;
    }
    return NULL;
}

bool ph_dsk_senza_fondo(PhCfgWidgetTipo tipo, unsigned stile)
{
    return (tipo == PH_CFG_WIDGET_OROLOGIO && stile == PH_DSK_OROLOGIO_SENZA_FONDO) ||
           (tipo == PH_CFG_WIDGET_METEO && stile == PH_DSK_METEO_SENZA_FONDO);
}

PhDskDato ph_dsk_dato_da_esito(PhSrvEsito esito, const char *frase)
{
    if (esito == PH_SRV_ESITO_OK)
        return PH_DSK_DATO_OK;
    return (frase != NULL && frase[0] != '\0') ? PH_DSK_DATO_ERRORE : PH_DSK_DATO_NON_DISPONIBILE;
}

/* Vista per i dati non pronti: true se ha gia' detto tutto. */
static bool vista_dato(PhDskVista *v, PhDskDato s, const char *frase)
{
    if (s == PH_DSK_DATO_OK)
        return false;
    if (s == PH_DSK_DATO_NON_DISPONIBILE) {
        copia(v->testa, sizeof v->testa, ph_t("Non disponibile"));
    } else if (s == PH_DSK_DATO_ERRORE) {
        copia(v->testa, sizeof v->testa, ph_t("Errore"));
        copia(v->sotto, sizeof v->sotto, frase);
    }
    return true; /* ATTESA: niente (non si inventa nulla) */
}

/* ---- Giorni civili ------------------------------------------------------------------------------ */
int64_t ph_dsk_giorno_civile(int anno, int mese, int giorno)
{
    int64_t y = anno - (mese <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (mese > 2 ? mese - 3 : mese + 9) + 2) / 5 + giorno - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void ph_dsk_civile_da_giorno(int64_t g, int *anno, int *mese, int *giorno)
{
    int64_t z = g + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
    *anno = (int)y;
    *mese = (int)m;
    *giorno = (int)d;
}

int ph_dsk_giorni_nel_mese(int anno, int mese)
{
    static const int g[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mese < 1 || mese > 12)
        return 0;
    bool bisestile = (anno % 4 == 0 && anno % 100 != 0) || anno % 400 == 0;
    return g[mese - 1] + (mese == 2 && bisestile ? 1 : 0);
}

int ph_dsk_giorno_settimana_di(int64_t g)
{
    return (int)(((g % 7) + 7 + 3) % 7); /* il 1970-01-01 era un giovedi' (3) */
}

int ph_dsk_giorno_settimana(int anno, int mese, int giorno)
{
    return ph_dsk_giorno_settimana_di(ph_dsk_giorno_civile(anno, mese, giorno));
}

int64_t ph_dsk_inizio_settimana(int64_t g)
{
    return g - ph_dsk_giorno_settimana_di(g);
}

bool ph_dsk_mese(int anno, int mese, PhDskMese *m)
{
    if (anno < 1900 || anno > 2200 || mese < 1 || mese > 12)
        return false;
    m->anno = anno;
    m->mese = mese;
    m->giorni = ph_dsk_giorni_nel_mese(anno, mese);
    m->primo = ph_dsk_giorno_settimana(anno, mese, 1);
    m->righe = (m->primo + m->giorni + 6) / 7;
    return true;
}

int ph_dsk_mese_cella(const PhDskMese *m, int cella)
{
    int g = cella - m->primo + 1;
    return (cella >= 0 && g >= 1 && g <= m->giorni) ? g : 0;
}

void ph_dsk_mese_sposta(int *anno, int *mese, int delta)
{
    long tot = (long)*anno * 12 + (*mese - 1) + delta;
    long a = tot / 12;
    if (a < 1970)
        a = 1970, tot = a * 12;
    if (a > 2100)
        a = 2100, tot = a * 12 + 11;
    *anno = (int)(tot / 12);
    *mese = (int)(tot % 12) + 1;
}

const char *ph_dsk_nome_giorno(int g)
{
    static const char *const nomi[7] = {PH_T_("Lunedì"),  PH_T_("Martedì"), PH_T_("Mercoledì"), PH_T_("Giovedì"),
                                        PH_T_("Venerdì"), PH_T_("Sabato"),  PH_T_("Domenica")};
    return g >= 1 && g <= 7 ? ph_tv(nomi[g - 1]) : "";
}

const char *ph_dsk_nome_mese(int m)
{
    static const char *const nomi[12] = {
        PH_T_("gennaio"), PH_T_("febbraio"), PH_T_("marzo"),    PH_T_("aprile"),   PH_T_("maggio"),   PH_T_("giugno"),
        PH_T_("luglio"),  PH_T_("agosto"),   PH_T_("settembre"), PH_T_("ottobre"), PH_T_("novembre"), PH_T_("dicembre")};
    return m >= 1 && m <= 12 ? ph_tv(nomi[m - 1]) : "";
}

const char *ph_dsk_giorno_breve(int g)
{
    static const char *const nomi[7] = {PH_T_("Lun"), PH_T_("Mar"), PH_T_("Mer"), PH_T_("Gio"),
                                        PH_T_("Ven"), PH_T_("Sab"), PH_T_("Dom")};
    return g >= 0 && g <= 6 ? ph_tv(nomi[g]) : "";
}

void ph_dsk_mese_titolo(int anno, int mese, char *out, size_t cap)
{
    if (cap == 0)
        return;
    out[0] = '\0';
    if (mese < 1 || mese > 12)
        return;
    char nome[48], num[16];
    maiuscola(ph_dsk_nome_mese(mese), nome, sizeof nome);
    snprintf(num, sizeof num, "%d", anno);
    ph_lingua_componi(out, cap, ph_t("{mese} {anno}"), "mese", nome, "anno", num, (const char *)NULL);
}

/* ---- Intervalli e giorni locali ----------------------------------------------------------------- */
static bool mezzanotte_locale(GTimeZone *tz, int64_t g, uint64_t *ms)
{
    int a, m, d;
    ph_dsk_civile_da_giorno(g, &a, &m, &d);
    if (a < 1970 || a > 2200)
        return false;
    GDateTime *dt = g_date_time_new(tz, a, m, d, 0, 0, 0);
    if (dt == NULL)
        return false;
    *ms = (uint64_t)g_date_time_to_unix(dt) * 1000u;
    g_date_time_unref(dt);
    return true;
}

bool ph_dsk_giorni_intervallo(GTimeZone *tz0, int64_t g, int giorni, uint64_t *da_ms, uint64_t *a_ms)
{
    if (giorni < 1 || giorni > PH_SRV_CALENDARIO_GIORNI_MAX)
        return false;
    GTimeZone *tz = fuso(tz0);
    bool ok = mezzanotte_locale(tz, g, da_ms) && mezzanotte_locale(tz, g + giorni, a_ms);
    g_time_zone_unref(tz);
    return ok;
}

bool ph_dsk_mese_intervallo(GTimeZone *tz, int anno, int mese, uint64_t *da_ms, uint64_t *a_ms)
{
    PhDskMese m;
    if (!ph_dsk_mese(anno, mese, &m))
        return false;
    return ph_dsk_giorni_intervallo(tz, ph_dsk_giorno_civile(anno, mese, 1), m.giorni, da_ms, a_ms);
}

int64_t ph_dsk_giorno_locale(GTimeZone *tz0, uint64_t ms)
{
    if (ms > (uint64_t)INT64_MAX / 1000)
        return 0;
    GTimeZone *tz = fuso(tz0);
    GDateTime *utc = g_date_time_new_from_unix_utc((gint64)(ms / 1000));
    GDateTime *loc = utc != NULL ? g_date_time_to_timezone(utc, tz) : NULL;
    int64_t g = 0;
    if (loc != NULL)
        g = ph_dsk_giorno_civile(g_date_time_get_year(loc), g_date_time_get_month(loc),
                                 g_date_time_get_day_of_month(loc));
    if (loc != NULL)
        g_date_time_unref(loc);
    if (utc != NULL)
        g_date_time_unref(utc);
    g_time_zone_unref(tz);
    return g;
}

/* ---- Eventi sui giorni ------------------------------------------------------------------------------ */
static bool evento_giorni_tz(const PhDskEvento *e, GTimeZone *tz, int64_t *primo, int64_t *ultimo)
{
    const uint64_t limite = 253402300800000ull; /* anno 9999 */
    if (e->fine_ms < e->inizio_ms || e->inizio_ms > limite || e->fine_ms > limite)
        return false;
    if (e->tutto_il_giorno) {
        /* In UTC, mai con l'ora locale: da quello di inizio a quello di fine ESCLUSO. */
        int64_t s = div_pavimento((int64_t)(e->inizio_ms / 1000), 86400);
        int64_t f = div_pavimento((int64_t)(e->fine_ms / 1000), 86400);
        if (f <= s)
            f = s + 1;
        *primo = s;
        *ultimo = f - 1;
        return true;
    }
    *primo = ph_dsk_giorno_locale(tz, e->inizio_ms);
    /* Finire esattamente a mezzanotte non conta nel giorno dopo: si guarda l'ultimo ms. */
    *ultimo = e->fine_ms == e->inizio_ms ? *primo : ph_dsk_giorno_locale(tz, e->fine_ms - 1);
    if (*ultimo < *primo)
        *ultimo = *primo;
    return true;
}

bool ph_dsk_evento_giorni(const PhDskEvento *e, GTimeZone *tz0, int64_t *primo, int64_t *ultimo)
{
    GTimeZone *tz = fuso(tz0);
    bool ok = evento_giorni_tz(e, tz, primo, ultimo);
    g_time_zone_unref(tz);
    return ok;
}

size_t ph_dsk_mese_con_impegni(const PhDskEvento *ev, size_t n, GTimeZone *tz0, int anno, int mese,
                               uint32_t *maschera)
{
    *maschera = 0;
    PhDskMese m;
    if (!ph_dsk_mese(anno, mese, &m) || ev == NULL)
        return 0;
    GTimeZone *tz = fuso(tz0);
    int64_t g1 = ph_dsk_giorno_civile(anno, mese, 1), g2 = g1 + m.giorni - 1;
    size_t toccano = 0;
    for (size_t i = 0; i < n; i++) {
        int64_t p, u;
        if (!evento_giorni_tz(&ev[i], tz, &p, &u) || u < g1 || p > g2)
            continue; /* nessuno dei suoi giorni cade nel mese: si scarta */
        toccano++;
        for (int64_t g = p > g1 ? p : g1; g <= (u < g2 ? u : g2); g++)
            *maschera |= 1u << (g - g1);
    }
    g_time_zone_unref(tz);
    return toccano;
}

size_t ph_dsk_eventi_del_giorno(const PhDskEvento *ev, size_t n, GTimeZone *tz0, int64_t g, size_t *indici,
                                size_t max)
{
    if (ev == NULL)
        return 0;
    GTimeZone *tz = fuso(tz0);
    size_t k = 0;
    for (size_t i = 0; i < n && k < max; i++) {
        int64_t p, u;
        if (evento_giorni_tz(&ev[i], tz, &p, &u) && g >= p && g <= u)
            indici[k++] = i;
    }
    g_time_zone_unref(tz);
    return k;
}

void ph_dsk_conta_per_giorno(const PhDskEvento *ev, size_t n, GTimeZone *tz0, int64_t g0, int giorni,
                             uint8_t *conteggi)
{
    if (giorni < 0)
        return;
    memset(conteggi, 0, (size_t)giorni);
    if (ev == NULL)
        return;
    GTimeZone *tz = fuso(tz0);
    for (size_t i = 0; i < n; i++) {
        int64_t p, u;
        if (!evento_giorni_tz(&ev[i], tz, &p, &u))
            continue;
        for (int64_t g = p > g0 ? p : g0; g <= u && g < g0 + giorni; g++)
            if (conteggi[g - g0] < 255)
                conteggi[g - g0]++;
    }
    g_time_zone_unref(tz);
}

void ph_dsk_evento_orario(const PhDskEvento *e, GTimeZone *tz0, char *out, size_t cap)
{
    if (cap == 0)
        return;
    out[0] = '\0';
    if (e->tutto_il_giorno) {
        copia(out, cap, ph_t("Tutto il giorno"));
        return;
    }
    if (e->inizio_ms > (uint64_t)INT64_MAX / 1000 || e->fine_ms < e->inizio_ms || e->fine_ms > (uint64_t)INT64_MAX / 1000)
        return;
    GTimeZone *tz = fuso(tz0);
    GDateTime *u1 = g_date_time_new_from_unix_utc((gint64)(e->inizio_ms / 1000));
    GDateTime *u2 = g_date_time_new_from_unix_utc((gint64)(e->fine_ms / 1000));
    GDateTime *a = u1 != NULL ? g_date_time_to_timezone(u1, tz) : NULL;
    GDateTime *b = u2 != NULL ? g_date_time_to_timezone(u2, tz) : NULL;
    if (a != NULL && b != NULL) {
        if (e->fine_ms == e->inizio_ms)
            snprintf(out, cap, "%02d:%02d", g_date_time_get_hour(a), g_date_time_get_minute(a));
        else
            snprintf(out, cap, "%02d:%02d\xE2\x80\x93%02d:%02d", g_date_time_get_hour(a), g_date_time_get_minute(a),
                     g_date_time_get_hour(b), g_date_time_get_minute(b));
    }
    if (a != NULL)
        g_date_time_unref(a);
    if (b != NULL)
        g_date_time_unref(b);
    if (u1 != NULL)
        g_date_time_unref(u1);
    if (u2 != NULL)
        g_date_time_unref(u2);
    g_time_zone_unref(tz);
}

void ph_dsk_giorno_etichetta(int64_t g, int64_t oggi, char *out, size_t cap)
{
    if (cap == 0)
        return;
    if (g == oggi) {
        copia(out, cap, ph_t("Oggi"));
    } else if (g == oggi + 1) {
        copia(out, cap, ph_t("Domani"));
    } else {
        int a, m, d;
        char num[16];
        ph_dsk_civile_da_giorno(g, &a, &m, &d);
        snprintf(num, sizeof num, "%d", d);
        ph_lingua_componi(out, cap, ph_t("{giorno} {numero}"), "giorno",
                          ph_dsk_giorno_breve(ph_dsk_giorno_settimana_di(g)), "numero", num, (const char *)NULL);
    }
}

size_t ph_dsk_agenda(const PhDskEvento *ev, size_t n, GTimeZone *tz, int64_t oggi, int giorni,
                     PhDskAgendaRiga *out, size_t max)
{
    size_t k = 0;
    for (int d = 0; d < giorni && k + 2 <= max; d++) {
        size_t idx[64];
        size_t ne = ph_dsk_eventi_del_giorno(ev, n, tz, oggi + d, idx, 64);
        if (ne == 0)
            continue;
        out[k].giorno = oggi + d;
        out[k].evento = (size_t)-1;
        k++;
        for (size_t i = 0; i < ne && k < max; i++) {
            out[k].giorno = oggi + d;
            out[k].evento = idx[i];
            k++;
        }
    }
    return k;
}

void ph_dsk_altri_testo(size_t n, char *out, size_t cap)
{
    if (cap == 0)
        return;
    out[0] = '\0';
    if (n == 1) {
        copia(out, cap, ph_t("e un altro"));
    } else if (n > 1) {
        char num[24];
        snprintf(num, sizeof num, "%zu", n);
        ph_lingua_componi(out, cap, ph_t("e altri {numero}"), "numero", num, (const char *)NULL);
    }
}

void ph_dsk_vista_calendario_mese_piccolo(PhDskDato stato, const char *frase, const PhDskEvento *ev, size_t n,
                                          GTimeZone *tz, int64_t oggi, PhDskVista *v)
{
    azzera(v);
    copia(v->titolo, sizeof v->titolo, ph_t("Calendario"));
    if (vista_dato(v, stato, frase))
        return;
    size_t idx[64];
    size_t k = ph_dsk_eventi_del_giorno(ev, n, tz, oggi, idx, 64);
    char num[24];
    snprintf(num, sizeof num, "%zu", k);
    if (k == 0)
        copia(v->testa, sizeof v->testa, ph_t("Nessun impegno"));
    else if (k == 1)
        copia(v->testa, sizeof v->testa, ph_t("1 impegno"));
    else
        ph_lingua_componi(v->testa, sizeof v->testa, ph_t("{numero} impegni"), "numero", num, (const char *)NULL);
    copia(v->sotto, sizeof v->sotto, ph_t("Oggi"));
}

/* ---- Sveglia ------------------------------------------------------------------------------------------ */
void ph_dsk_fra_testo(int64_t minuti, char *out, size_t cap)
{
    if (cap == 0)
        return;
    out[0] = '\0';
    if (minuti < 0)
        minuti = 0;
    char a[24], b[24];
    if (minuti < 1) {
        copia(out, cap, ph_t("fra meno di un minuto"));
    } else if (minuti == 1) {
        copia(out, cap, ph_t("fra un minuto"));
    } else if (minuti < 60) {
        snprintf(a, sizeof a, "%lld", (long long)minuti);
        ph_lingua_componi(out, cap, ph_t("fra {numero} minuti"), "numero", a, (const char *)NULL);
    } else if (minuti < 24 * 60) {
        int64_t h = minuti / 60, r = minuti % 60;
        snprintf(a, sizeof a, "%lld", (long long)h);
        snprintf(b, sizeof b, "%lld", (long long)r);
        if (r == 0 && h == 1)
            copia(out, cap, ph_t("fra un'ora"));
        else if (r == 0)
            ph_lingua_componi(out, cap, ph_t("fra {numero} ore"), "numero", a, (const char *)NULL);
        else
            ph_lingua_componi(out, cap, ph_t("fra {ore} h {minuti} min"), "ore", a, "minuti", b, (const char *)NULL);
    } else {
        int64_t g = minuti / (24 * 60);
        snprintf(a, sizeof a, "%lld", (long long)g);
        if (g == 1)
            copia(out, cap, ph_t("fra un giorno"));
        else
            ph_lingua_componi(out, cap, ph_t("fra {numero} giorni"), "numero", a, (const char *)NULL);
    }
}

void ph_dsk_vista_sveglia(PhCfgWidgetDim d, PhDskDato stato, const char *frase, bool presente, uint64_t ora_ms,
                          uint64_t adesso_ms, GTimeZone *tz0, PhDskVista *v)
{
    azzera(v);
    copia(v->titolo, sizeof v->titolo, ph_t("Prossima sveglia"));
    if (vista_dato(v, stato, frase))
        return;
    if (!presente) {
        copia(v->testa, sizeof v->testa, ph_t("Nessuna sveglia"));
        return;
    }
    GTimeZone *tz = fuso(tz0);
    GDateTime *u = ora_ms <= (uint64_t)INT64_MAX / 1000 ? g_date_time_new_from_unix_utc((gint64)(ora_ms / 1000)) : NULL;
    GDateTime *l = u != NULL ? g_date_time_to_timezone(u, tz) : NULL;
    if (l != NULL) {
        snprintf(v->testa, sizeof v->testa, "%02d:%02d", g_date_time_get_hour(l), g_date_time_get_minute(l));
        v->testa_grande = true;
        ph_dsk_giorno_etichetta(ph_dsk_giorno_locale(tz, ora_ms), ph_dsk_giorno_locale(tz, adesso_ms), v->sotto,
                                sizeof v->sotto);
        if (d == PH_CFG_WIDGET_GRANDE) {
            int64_t min = ora_ms > adesso_ms ? (int64_t)((ora_ms - adesso_ms) / 60000) : 0;
            char fra[96];
            ph_dsk_fra_testo(min, fra, sizeof fra);
            riga(v, fra, "");
        }
    }
    if (l != NULL)
        g_date_time_unref(l);
    if (u != NULL)
        g_date_time_unref(u);
    g_time_zone_unref(tz);
}

/* ---- Batteria ------------------------------------------------------------------------------------------ */
static void decimale(char *out, size_t cap, long milli_unita, int cifre, const char *unita_chiave)
{
    /* milli_unita: valore * 10^cifre (intero). */
    char sep = ph_lingua_attiva() == PH_LINGUA_IT ? ',' : '.';
    long div = cifre == 1 ? 10 : 100;
    long v = milli_unita < 0 ? -milli_unita : milli_unita;
    char num[32];
    snprintf(num, sizeof num, "%s%ld%c%0*ld", milli_unita < 0 ? "-" : "", v / div, sep, cifre, v % div);
    ph_lingua_componi(out, cap, unita_chiave, "numero", num, (const char *)NULL);
}

void ph_dsk_testo_temperatura(int decimi, char *out, size_t cap)
{
    decimale(out, cap, decimi, 1, ph_t("{numero} °C"));
}

void ph_dsk_testo_tensione(int millivolt, char *out, size_t cap)
{
    decimale(out, cap, (millivolt + 5) / 10, 2, ph_t("{numero} V"));
}

void ph_dsk_testo_durata(int minuti, char *out, size_t cap)
{
    if (cap == 0)
        return;
    char a[16], b[16];
    if (minuti < 0)
        minuti = 0;
    snprintf(a, sizeof a, "%d", minuti / 60);
    snprintf(b, sizeof b, "%d", minuti % 60);
    if (minuti >= 60 && minuti % 60 != 0)
        ph_lingua_componi(out, cap, ph_t("{ore} h {minuti} min"), "ore", a, "minuti", b, (const char *)NULL);
    else if (minuti >= 60)
        ph_lingua_componi(out, cap, ph_t("{numero} h"), "numero", a, (const char *)NULL);
    else
        ph_lingua_componi(out, cap, ph_t("{numero} min"), "numero", b, (const char *)NULL);
}

static const char *testo_salute(PhSrvSalute s)
{
    switch (s) {
    case PH_SRV_SALUTE_BUONA: return ph_t("Buona");
    case PH_SRV_SALUTE_SURRISCALDATA: return ph_t("Surriscaldata");
    case PH_SRV_SALUTE_GUASTA: return ph_t("Guasta");
    case PH_SRV_SALUTE_SOVRATENSIONE: return ph_t("Sovratensione");
    case PH_SRV_SALUTE_FREDDA: return ph_t("Fredda");
    case PH_SRV_SALUTE_ERRORE: return ph_t("Errore");
    default: return NULL;
    }
}

void ph_dsk_vista_batteria(PhCfgWidgetDim d, PhDskDato stato, const char *frase, const PhSrvBatteria *b,
                           PhDskVista *v)
{
    azzera(v);
    copia(v->titolo, sizeof v->titolo, ph_t("Batteria"));
    if (vista_dato(v, stato, frase) || b == NULL)
        return;
    int liv = b->livello < 0 ? 0 : (b->livello > 100 ? 100 : b->livello);
    snprintf(v->testa, sizeof v->testa, "%d%%", liv);
    v->testa_grande = true;
    v->percento = liv;
    switch (b->stato) {
    case PH_SRV_BAT_IN_CARICA: {
        const char *fonte = (b->alimentazione & PH_SRV_ALIM_RETE)         ? ph_t("rete")
                            : (b->alimentazione & PH_SRV_ALIM_USB)        ? ph_t("USB")
                            : (b->alimentazione & PH_SRV_ALIM_SENZA_FILI) ? ph_t("senza fili")
                            : (b->alimentazione & PH_SRV_ALIM_BASE)       ? ph_t("base")
                                                                           : NULL;
        if (fonte != NULL)
            ph_lingua_componi(v->sotto, sizeof v->sotto, ph_t("In carica · {fonte}"), "fonte", fonte,
                              (const char *)NULL);
        else
            copia(v->sotto, sizeof v->sotto, ph_t("In carica"));
        break;
    }
    case PH_SRV_BAT_IN_SCARICA: copia(v->sotto, sizeof v->sotto, ph_t("In scarica")); break;
    case PH_SRV_BAT_NON_IN_CARICA: copia(v->sotto, sizeof v->sotto, ph_t("Non in carica")); break;
    case PH_SRV_BAT_PIENA: copia(v->sotto, sizeof v->sotto, ph_t("Carica completa")); break;
    default: break;
    }
    if (d == PH_CFG_WIDGET_PICCOLO)
        return;
    char t[64];
    if (b->temperatura_nota) {
        ph_dsk_testo_temperatura(b->temperatura_decimi, t, sizeof t);
        riga(v, ph_t("Temperatura"), t);
    }
    if (d != PH_CFG_WIDGET_GRANDE)
        return;
    const char *sal = testo_salute(b->salute);
    if (sal != NULL)
        riga(v, ph_t("Salute"), sal);
    if (b->tensione_mv > 0) {
        ph_dsk_testo_tensione(b->tensione_mv, t, sizeof t);
        riga(v, ph_t("Tensione"), t);
    }
    /* Solo il tempo alla carica completa quando il telefono lo da'; mai l'autonomia
     * (il telefono non la da' e non la stimiamo noi). */
    if (b->stato == PH_SRV_BAT_IN_CARICA && b->minuti_carica >= 0) {
        ph_dsk_testo_durata(b->minuti_carica, t, sizeof t);
        riga(v, ph_t("Carica completa tra"), t);
    }
}

/* ---- Interruttori e trova -------------------------------------------------------------------------------- */
void ph_dsk_interruttore(int controllo, int valore, bool puo, bool in_corso, PhDskInterruttore *out)
{
    memset(out, 0, sizeof *out);
    copia(out->etichetta, sizeof out->etichetta,
          controllo == 0 ? ph_t("Suoneria") : (controllo == 1 ? ph_t("Non disturbare") : ph_t("Torcia")));
    out->in_corso = in_corso;
    if (!puo || valore == PH_SRV_NON_DISPONIBILE || valore < 0) {
        copia(out->stato, sizeof out->stato, ph_t("Non disponibile"));
        return; /* disponibile = false, attivo = false */
    }
    out->disponibile = true;
    if (controllo == 0) {
        out->attivo = valore != PH_SRV_SUONERIA_SUONA;
        copia(out->stato, sizeof out->stato,
              valore == PH_SRV_SUONERIA_SUONA ? ph_t("Suona") : (valore == PH_SRV_SUONERIA_VIBRA ? ph_t("Vibra") : ph_t("Silenzioso")));
    } else {
        out->attivo = valore != 0;
        copia(out->stato, sizeof out->stato, valore != 0 ? ph_t("Acceso") : ph_t("Spento"));
    }
    if (in_corso)
        copia(out->stato, sizeof out->stato, ph_t("In corso"));
}

int ph_dsk_prossimo_valore(int controllo, int valore)
{
    if (controllo == 0) {
        if (valore == PH_SRV_SUONERIA_SUONA)
            return PH_SRV_SUONERIA_VIBRA;
        if (valore == PH_SRV_SUONERIA_VIBRA)
            return PH_SRV_SUONERIA_SILENZIOSO;
        return PH_SRV_SUONERIA_SUONA;
    }
    return valore == 0 ? 1 : 0;
}

int ph_dsk_trova_rimasti(int durata_s, int64_t trascorsi_ms)
{
    if (durata_s <= 0)
        return 0;
    if (trascorsi_ms < 0)
        trascorsi_ms = 0;
    int64_t resto_ms = (int64_t)durata_s * 1000 - trascorsi_ms;
    if (resto_ms <= 0)
        return 0;
    return (int)((resto_ms + 999) / 1000);
}

void ph_dsk_trova_testo(int secondi, char *out, size_t cap)
{
    if (cap == 0)
        return;
    if (secondi < 0)
        secondi = 0;
    char t[24];
    snprintf(t, sizeof t, "%d:%02d", secondi / 60, secondi % 60);
    ph_lingua_componi(out, cap, ph_t("Resta {tempo}"), "tempo", t, (const char *)NULL);
}

/* ---- App frequenti e trasferimenti ----------------------------------------------------------------------- */
size_t ph_dsk_app_frequenti_max(PhCfgWidgetDim d)
{
    return d == PH_CFG_WIDGET_PICCOLO ? 4 : (d == PH_CFG_WIDGET_GRANDE ? 12 : 8);
}

size_t ph_dsk_trasferimenti_max(PhCfgWidgetDim d)
{
    return d == PH_CFG_WIDGET_PICCOLO ? 3 : (d == PH_CFG_WIDGET_GRANDE ? 8 : 5);
}

size_t ph_dsk_app_frequenti(const PhCfgApertura *a, size_t n, size_t max, size_t *indici)
{
    if (a == NULL || n == 0 || max == 0)
        return 0;
    if (n > PH_CFG_MAX_APERTURE)
        n = PH_CFG_MAX_APERTURE;
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (a[i].volte == 0 || a[i].app[0] == '\0')
            continue;
        /* Inserzione ordinata: piu' aperture, poi la piu' recente, poi l'ordine del profilo. */
        size_t pos = k;
        while (pos > 0 && (a[indici[pos - 1]].volte < a[i].volte ||
                           (a[indici[pos - 1]].volte == a[i].volte && a[indici[pos - 1]].ultima < a[i].ultima)))
            pos--;
        if (pos >= max)
            continue;
        size_t fine = k < max ? k : max - 1;
        for (size_t j = fine; j > pos; j--)
            indici[j] = indici[j - 1];
        indici[pos] = i;
        if (k < max)
            k++;
    }
    return k;
}

void ph_dsk_nome_sicuro(const char *grezzo, char *out, size_t cap)
{
    if (cap == 0)
        return;
    size_t o = 0;
    out[0] = '\0';
    const char *p = grezzo != NULL ? grezzo : "";
    while (*p != '\0') {
        gunichar c;
        const char *prossimo;
        if (g_utf8_validate(p, g_utf8_skip[(unsigned char)*p], NULL)) {
            c = g_utf8_get_char(p);
            prossimo = g_utf8_next_char(p);
        } else {
            c = '?';
            prossimo = p + 1;
        }
        bool vietato = c < 0x20 || c == 0x7f || (c >= 0x80 && c < 0xa0) || (c >= 0x200b && c <= 0x200f) ||
                       (c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069) || c == 0xfeff;
        if (!vietato) {
            char m[8];
            int len = g_unichar_to_utf8(c, m);
            if (o + (size_t)len + 1 > cap)
                break; /* si taglia su un confine di carattere */
            memcpy(out + o, m, (size_t)len);
            o += (size_t)len;
        }
        p = prossimo;
    }
    out[o] = '\0';
}
