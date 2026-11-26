/* Logica pura dei componenti di disegno (logica_comp.h): geometria senza widget. */
#include "logica_comp.h"

#include <glib.h>
#include <math.h>
#include <string.h>

/* Tetto dei valori che arrivano da fuori: con numeri finiti ma enormi (1e308) le differenze e le
 * somme traboccano e danno inf o NaN. Tutto cio' che si disegna sta ben dentro +-PH_COMP_LIMITE. */
#define PH_COMP_LIMITE 1e6

static double finito_o(double v, double altrimenti)
{
    return isfinite(v) ? v : altrimenti;
}

/* Valore finito riportato in +-PH_COMP_LIMITE (non finito: `altrimenti`). */
static double limita(double v, double altrimenti)
{
    v = finito_o(v, altrimenti);
    return v > PH_COMP_LIMITE ? PH_COMP_LIMITE : (v < -PH_COMP_LIMITE ? -PH_COMP_LIMITE : v);
}

/* ---- Anello --------------------------------------------------------------------------------- */
static double percento_stretto(double p)
{
    if (!isfinite(p) || p < 0)
        return 0;
    return p > 100 ? 100 : p;
}

int ph_dsk_percento_valido(double percento)
{
    return (int)lround(percento_stretto(percento));
}

void ph_dsk_anello_angoli(double percento, double *inizio, double *fine)
{
    double p = percento_stretto(percento);
    *inizio = -G_PI / 2;
    *fine = *inizio + 2 * G_PI * p / 100.0;
}

void ph_dsk_anello_geometria(double lato, double *raggio, double *spessore)
{
    if (!isfinite(lato) || lato <= 0) {
        *raggio = 0;
        *spessore = 0;
        return;
    }
    double s = lato * 0.094;
    if (s < 3)
        s = 3;
    if (s > 14)
        s = 14;
    if (s > lato / 4)
        s = lato / 4; /* lato minuscolo: il tratto non mangia tutto il disco */
    *spessore = s;
    *raggio = (lato - s) / 2.0;
}

/* ---- Fette ------------------------------------------------------------------------------------ */
size_t ph_dsk_fette_bordi(const int64_t *valori, size_t n, int64_t totale, int larghezza, int *bordi)
{
    if (n > PH_DSK_FETTE_MAX)
        n = PH_DSK_FETTE_MAX;
    if (valori == NULL)
        n = 0;
    bordi[0] = 0;
    double somma = 0;
    for (size_t i = 0; i < n; i++)
        if (valori[i] > 0)
            somma += (double)valori[i];
    double tot = totale > 0 ? (double)totale : 0;
    if (somma > tot)
        tot = somma; /* la somma maggiore del totale: le fette riempiono e basta */
    if (larghezza <= 0 || tot <= 0) {
        for (size_t i = 1; i <= n; i++)
            bordi[i] = 0;
        return n;
    }
    /* Bordi dalla somma cumulata (non dalle larghezze una a una): la loro somma non puo'
     * superare la barra e l'arrotondamento non si accumula. */
    double cum = 0;
    for (size_t i = 0; i < n; i++) {
        if (valori[i] > 0)
            cum += (double)valori[i];
        double r = cum / tot;
        if (r > 1)
            r = 1;
        int b = (int)floor(r * larghezza);
        if (b < bordi[i])
            b = bordi[i];
        if (b > larghezza)
            b = larghezza;
        bordi[i + 1] = b;
    }
    return n;
}

/* ---- Curva ------------------------------------------------------------------------------------- */
void ph_dsk_curva_punti(const double *t, size_t n, double larghezza, double margine_x, double y_alto,
                        double y_basso, PhDskCurvaPunti *out)
{
    memset(out, 0, sizeof *out);
    if (t == NULL || n == 0)
        return;
    if (n > PH_DSK_CURVA_MAX)
        n = PH_DSK_CURVA_MAX;
    larghezza = limita(larghezza, 0);
    if (larghezza < 0)
        larghezza = 0;
    margine_x = limita(margine_x, 0);
    if (margine_x < 0)
        margine_x = 0;
    if (margine_x > larghezza / 2)
        margine_x = larghezza / 2;
    double yt = limita(y_alto, 0), yb = limita(y_basso, 0);
    if (yt > yb) {
        double s = yt;
        yt = yb;
        yb = s;
    }
    /* Valori puliti: i non finiti prendono la media dei finiti. */
    double v[PH_DSK_CURVA_MAX], somma = 0;
    size_t buoni = 0;
    for (size_t i = 0; i < n; i++)
        if (isfinite(t[i])) {
            somma += limita(t[i], 0);
            buoni++;
        }
    double media = buoni > 0 ? somma / (double)buoni : 0;
    for (size_t i = 0; i < n; i++)
        v[i] = isfinite(t[i]) ? limita(t[i], 0) : media;
    size_t imin = 0, imax = 0;
    for (size_t i = 1; i < n; i++) {
        if (v[i] < v[imin])
            imin = i;
        if (v[i] > v[imax])
            imax = i;
    }
    out->n = n;
    out->imin = imin;
    out->imax = imax;
    out->tmin = v[imin];
    out->tmax = v[imax];
    double lo = v[imin], hi = v[imax];
    if (hi - lo < PH_DSK_CURVA_SPAN_MIN) {
        double c = (hi + lo) / 2.0;
        lo = c - PH_DSK_CURVA_SPAN_MIN / 2.0;
        hi = c + PH_DSK_CURVA_SPAN_MIN / 2.0;
    }
    double utile = larghezza - 2 * margine_x;
    for (size_t i = 0; i < n; i++) {
        double x = n == 1 ? larghezza / 2.0 : margine_x + utile * (double)i / (double)(n - 1);
        out->x[i] = isfinite(x) ? x : larghezza / 2.0;
        double y = yb - (yb - yt) * (v[i] - lo) / (hi - lo);
        if (!isfinite(y))
            y = (yt + yb) / 2.0; /* ultima difesa: mai un NaN a cairo */
        out->y[i] = y < yt ? yt : (y > yb ? yb : y);
    }
}

size_t ph_dsk_curva_passo_etichette(size_t n, double larghezza, double larg_etichetta)
{
    if (n <= 1)
        return 1;
    if (!isfinite(larghezza) || larghezza <= 0)
        return n;
    if (!isfinite(larg_etichetta) || larg_etichetta <= 0)
        return 1;
    /* Tra due etichette vicine resta un quinto della loro larghezza. */
    double passo = ceil(1.2 * larg_etichetta * (double)(n - 1) / larghezza);
    if (!(passo >= 1))
        return 1;
    return passo > (double)n ? n : (size_t)passo;
}

/* ---- Tratto ---------------------------------------------------------------------------------------- */
bool ph_dsk_tratto_px(double min, double max, double scala_min, double scala_max, double larghezza,
                      double minimo_px, double *x0, double *x1)
{
    *x0 = *x1 = 0;
    if (!isfinite(min) || !isfinite(max) || !isfinite(scala_min) || !isfinite(scala_max) ||
        !isfinite(larghezza) || larghezza <= 0)
        return false;
    min = limita(min, 0);
    max = limita(max, 0);
    scala_min = limita(scala_min, 0);
    scala_max = limita(scala_max, 0);
    larghezza = limita(larghezza, 0);
    if (larghezza <= 0)
        return false;
    if (min > max) {
        double s = min;
        min = max;
        max = s;
    }
    if (scala_min > scala_max) {
        double s = scala_min;
        scala_min = scala_max;
        scala_max = s;
    }
    double a, b;
    if (scala_max - scala_min <= 0) {
        a = 0;
        b = larghezza;
    } else {
        double lo = min < scala_min ? scala_min : (min > scala_max ? scala_max : min);
        double hi = max < scala_min ? scala_min : (max > scala_max ? scala_max : max);
        a = (lo - scala_min) / (scala_max - scala_min) * larghezza;
        b = (hi - scala_min) / (scala_max - scala_min) * larghezza;
    }
    if (!isfinite(a))
        a = 0; /* ultima difesa */
    if (!isfinite(b))
        b = larghezza;
    minimo_px = limita(minimo_px, 0);
    if (minimo_px > larghezza)
        minimo_px = larghezza;
    if (b - a < minimo_px) {
        double c = (a + b) / 2.0;
        a = c - minimo_px / 2.0;
        b = a + minimo_px;
        if (a < 0) {
            b -= a;
            a = 0;
        }
        if (b > larghezza) {
            a -= b - larghezza;
            b = larghezza;
        }
    }
    *x0 = a < 0 ? 0 : a;
    *x1 = b > larghezza ? larghezza : b;
    return true;
}

/* ---- Sigla -------------------------------------------------------------------------------------------- */
void ph_dsk_sigla(const char *testo, char *out, size_t cap)
{
    if (cap == 0)
        return;
    out[0] = '\0';
    const char *fallback = "?";
    if (testo != NULL && g_utf8_validate(testo, -1, NULL)) {
        const char *p = testo;
        while (*p != '\0' && g_unichar_isspace(g_utf8_get_char(p)))
            p = g_utf8_next_char(p);
        if (*p != '\0') {
            gunichar c = g_unichar_toupper(g_utf8_get_char(p));
            if (g_unichar_isgraph(c)) {
                char m[8];
                int len = g_unichar_to_utf8(c, m);
                if (len > 0 && (size_t)len + 1 <= cap) {
                    memcpy(out, m, (size_t)len);
                    out[len] = '\0';
                    return;
                }
            }
        }
    }
    g_strlcpy(out, fallback, cap);
}

/* ---- Numero grande -------------------------------------------------------------------------------------------- */
double ph_dsk_numero_corpo(double larghezza, double altezza, size_t caratteri, double min_px,
                           double max_px)
{
    if (!isfinite(min_px) || min_px < 1)
        min_px = 1;
    if (!isfinite(max_px) || max_px < min_px)
        max_px = min_px;
    if (!isfinite(larghezza) || !isfinite(altezza) || larghezza <= 0 || altezza <= 0 || caratteri == 0)
        return min_px;
    double px = max_px;
    double per_altezza = altezza / 1.15;
    double per_larghezza = larghezza / (0.62 * (double)caratteri);
    if (per_altezza < px)
        px = per_altezza;
    if (per_larghezza < px)
        px = per_larghezza;
    return px < min_px ? min_px : px;
}

/* ---- Disposizione adattiva ----------------------------------------------------------------------------------- */
int ph_dsk_legenda_colonne(double larghezza, double larg_voce_max, double spazio, size_t n)
{
    if (n < 2 || !isfinite(larghezza) || !isfinite(larg_voce_max) || larghezza <= 0)
        return 1;
    spazio = limita(spazio, 0);
    if (spazio < 0)
        spazio = 0;
    return larg_voce_max <= (larghezza - spazio) / 2.0 ? 2 : 1;
}

bool ph_dsk_riga_ora_sotto(double larghezza, double larg_chip, double larg_titolo, double larg_ora,
                           double spazio)
{
    if (!isfinite(larghezza) || larghezza <= 0 || !isfinite(larg_titolo) || !isfinite(larg_ora) ||
        larg_ora <= 0)
        return false; /* niente ora, o misura non ancora nota: resta a destra */
    larg_chip = limita(larg_chip, 0);
    spazio = limita(spazio, 0);
    return larghezza < larg_chip + spazio + larg_titolo + spazio + larg_ora;
}

/* ---- Accenti -------------------------------------------------------------------------------------------------------- */
int ph_dsk_accento_valido(int accento, int n_accenti)
{
    return accento >= 0 && accento < n_accenti ? accento : 0;
}
