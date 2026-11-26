/* Logica pura dei widget «Batteria in dettaglio» e «Prossima sveglia» (logica_telefono.h). */
#include "logica_telefono.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config/lingua.h"

#define BAT_RAPPORTO_NUM 18 /* quadrato inscritto del tondo: 18/25 = 0,72 del lato */
#define BAT_RAPPORTO_DEN 25
#define BAT_ANELLO_MIN 72
#define BAT_STATO_ALTO 20
#define BAT_GETTONE_ALTO 22
#define MISURA_MAX 4096 /* limite alle misure per i conti interi (il widget e' molto piu' piccolo) */

static int limita_misura(int v)
{
    return v < 0 ? 0 : (v > MISURA_MAX ? MISURA_MAX : v);
}

static void copia(char *dst, size_t cap, const char *src)
{
    if (cap > 0)
        snprintf(dst, cap, "%s", src != NULL ? src : "");
}

/* ---- Batteria ---------------------------------------------------------------------------------- */
bool ph_dsk_bat_bassa(int livello)
{
    return livello <= PH_DSK_BAT_SOGLIA_BASSA;
}

PhDskAccento ph_dsk_bat_accento(int livello, bool in_carica)
{
    return !in_carica && ph_dsk_bat_bassa(livello) ? PH_DSK_ACC_CORALLO : PH_DSK_ACC_VERDE;
}

void ph_dsk_bat_gettoni_noti(const PhSrvBatteria *b, bool noti[PH_DSK_BAT_G_N])
{
    for (int i = 0; i < PH_DSK_BAT_G_N; i++)
        noti[i] = false;
    if (b == NULL)
        return;
    noti[PH_DSK_BAT_G_TEMPERATURA] = b->temperatura_nota && b->temperatura_decimi >= -500 &&
                                     b->temperatura_decimi <= 1500;
    noti[PH_DSK_BAT_G_SALUTE] = b->salute >= PH_SRV_SALUTE_BUONA && b->salute <= PH_SRV_SALUTE_ERRORE;
    noti[PH_DSK_BAT_G_TENSIONE] = b->tensione_mv > 0 && b->tensione_mv <= 20000;
}

PhDskBatDisp ph_dsk_bat_disposizione(int larghezza, int altezza, const bool noti[PH_DSK_BAT_G_N])
{
    PhDskBatDisp d;
    memset(&d, 0, sizeof d);
    int lato = limita_misura(larghezza < altezza ? larghezza : altezza);
    int area = lato * BAT_RAPPORTO_NUM / BAT_RAPPORTO_DEN;
    d.lato_anello = area;
    d.larghezza_utile = area;
    d.caratteri_max = area / 8 < 4 ? 4 : area / 8;
    if (area - BAT_STATO_ALTO < BAT_ANELLO_MIN)
        return d; /* non c'e' posto per altro: solo l'anello */
    d.stato = true;
    int liberi = (area - BAT_STATO_ALTO - BAT_ANELLO_MIN) / BAT_GETTONE_ALTO;
    int k = 0;
    for (int i = 0; i < PH_DSK_BAT_G_N && noti != NULL; i++) {
        if (noti[i] && k < liberi) {
            d.gettone[i] = true;
            k++;
        }
    }
    d.lato_anello = area - BAT_STATO_ALTO - k * BAT_GETTONE_ALTO;
    return d;
}

void ph_dsk_bat_stato_testo(PhDskDato dato, const PhSrvBatteria *b, char *out, size_t cap)
{
    copia(out, cap, "");
    if (dato == PH_DSK_DATO_NON_DISPONIBILE) {
        copia(out, cap, ph_t("Non disponibile"));
        return;
    }
    if (dato == PH_DSK_DATO_ERRORE) {
        copia(out, cap, ph_t("Errore"));
        return;
    }
    if (dato != PH_DSK_DATO_OK || b == NULL)
        return;
    switch (b->stato) {
    case PH_SRV_BAT_IN_CARICA: copia(out, cap, ph_t("In carica")); break;
    case PH_SRV_BAT_IN_SCARICA: copia(out, cap, ph_t("In scarica")); break;
    case PH_SRV_BAT_NON_IN_CARICA: copia(out, cap, ph_t("Non in carica")); break;
    case PH_SRV_BAT_PIENA: copia(out, cap, ph_t("Carica completa")); break;
    default: break; /* sconosciuto: niente */
    }
}

static const char *salute_testo(PhSrvSalute s)
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

void ph_dsk_bat_gettone_testo(int quale, const PhSrvBatteria *b, char *out, size_t cap)
{
    copia(out, cap, "");
    bool noti[PH_DSK_BAT_G_N];
    ph_dsk_bat_gettoni_noti(b, noti);
    if (cap == 0 || quale < 0 || quale >= PH_DSK_BAT_G_N || !noti[quale])
        return;
    switch (quale) {
    case PH_DSK_BAT_G_TEMPERATURA: ph_dsk_testo_temperatura(b->temperatura_decimi, out, cap); break;
    case PH_DSK_BAT_G_SALUTE:
        ph_lingua_componi(out, cap, ph_t("Salute: {valore}"), "valore", salute_testo(b->salute),
                          (const char *)NULL);
        break;
    default: ph_dsk_testo_tensione(b->tensione_mv, out, cap); break;
    }
}

/* ---- Sveglia ------------------------------------------------------------------------------------- */
int ph_dsk_sve_tondo_lato(int larghezza_utile, int altezza_utile)
{
    int lato = larghezza_utile < altezza_utile ? larghezza_utile : altezza_utile;
    if (lato > PH_DSK_SVE_TONDO_MAX)
        lato = PH_DSK_SVE_TONDO_MAX;
    return lato < 24 ? 0 : lato;
}

PhDskSveDisp ph_dsk_sve_disposizione(int larghezza, int altezza, bool presente)
{
    PhDskSveDisp d;
    memset(&d, 0, sizeof d);
    int po, pv;
    ph_dsk_imbottitura(PH_DSK_FORMA_CAPSULA, &po, &pv);
    int cw = limita_misura(larghezza) - 2 * po, ch = limita_misura(altezza) - 2 * pv;
    if (cw <= 0 || ch <= 0)
        return d;
    d.tondo_lato = cw >= 150 ? ph_dsk_sve_tondo_lato(cw, ch) : 0;
    d.tondo = d.tondo_lato > 0;
    d.testo_larghezza = cw - (d.tondo ? d.tondo_lato + 12 : 0);
    int righe = !presente ? 0 : (ch >= 78 ? 2 : (ch >= 44 ? 1 : 0));
    d.giorno = righe >= 1;
    d.fra = righe == 2 || (righe == 1 && d.testo_larghezza >= 190);
    d.unite = righe == 1 && d.fra;
    d.ora_px = (int)(ph_dsk_numero_corpo(d.testo_larghezza, ch - righe * 16, 5, 14, 40) + 0.5);
    return d;
}

bool ph_dsk_sve_passata(uint64_t ora_ms, uint64_t adesso_ms)
{
    return ora_ms <= adesso_ms;
}

int64_t ph_dsk_sve_minuti(uint64_t ora_ms, uint64_t adesso_ms)
{
    if (ph_dsk_sve_passata(ora_ms, adesso_ms))
        return 0;
    uint64_t m = (ora_ms - adesso_ms) / 60000u;
    return m > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)m;
}

void ph_dsk_sve_fra(uint64_t ora_ms, uint64_t adesso_ms, char *out, size_t cap)
{
    if (cap == 0)
        return;
    out[0] = '\0';
    if (ph_dsk_sve_passata(ora_ms, adesso_ms))
        return;
    ph_dsk_fra_testo(ph_dsk_sve_minuti(ora_ms, adesso_ms), out, cap);
}

void ph_dsk_sve_ora_testo(uint64_t ora_ms, GTimeZone *tz0, char *out, size_t cap)
{
    if (cap == 0)
        return;
    out[0] = '\0';
    if (ora_ms > (uint64_t)INT64_MAX / 1000)
        return;
    GTimeZone *tz = tz0 != NULL ? g_time_zone_ref(tz0) : g_time_zone_new_local();
    GDateTime *u = g_date_time_new_from_unix_utc((gint64)(ora_ms / 1000));
    GDateTime *l = u != NULL ? g_date_time_to_timezone(u, tz) : NULL;
    if (l != NULL)
        snprintf(out, cap, "%02d:%02d", g_date_time_get_hour(l), g_date_time_get_minute(l));
    if (l != NULL)
        g_date_time_unref(l);
    if (u != NULL)
        g_date_time_unref(u);
    g_time_zone_unref(tz);
}

void ph_dsk_sve_assente_testo(PhDskDato dato, bool presente, char *out, size_t cap)
{
    copia(out, cap, "");
    if (dato == PH_DSK_DATO_NON_DISPONIBILE)
        copia(out, cap, ph_t("Non disponibile"));
    else if (dato == PH_DSK_DATO_ERRORE)
        copia(out, cap, ph_t("Errore"));
    else if (dato == PH_DSK_DATO_OK && !presente)
        copia(out, cap, ph_t("Nessuna sveglia"));
}

void ph_dsk_sve_giorno_fra(const char *giorno, const char *fra, char *out, size_t cap)
{
    if (fra == NULL || fra[0] == '\0')
        copia(out, cap, giorno);
    else
        ph_lingua_componi(out, cap, ph_t("{giorno} · {fra}"), "giorno", giorno != NULL ? giorno : "", "fra", fra,
                          (const char *)NULL);
}
