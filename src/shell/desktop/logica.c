/* Logica pura del desktop (logica.h): nessun widget, si prova senza schermo. */
#include "logica.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/lingua.h"

/* ---- Griglia -------------------------------------------------------------- */
static int ristretto(long v, int minimo, int massimo)
{
    if (v < minimo)
        return minimo;
    if (v > massimo)
        return massimo;
    return (int)v;
}

PhDskGriglia ph_dsk_griglia(int larghezza, int altezza_radice, bool widget_visibili)
{
    PhDskGriglia g;
    long c = ((long)larghezza - PH_DSK_MARGINE -
              (widget_visibili ? PH_DSK_RISERVA_WIDGET : PH_DSK_MARGINE)) / PH_DSK_CELLA_L;
    long r = ((long)altezza_radice - PH_DSK_BARRA - PH_DSK_MARGINE) / PH_DSK_CELLA_A;
    g.colonne = ristretto(c, 1, PH_DSK_GRIGLIA_MAX);
    g.righe = ristretto(r, 1, PH_DSK_GRIGLIA_MAX);
    return g;
}

PhDskCella ph_dsk_cella_al_punto(PhDskGriglia g, double x, double y)
{
    PhDskCella c;
    /* floor, non troncamento: a sinistra del margine la cella e' la 0 comunque. */
    double fc = floor((x - PH_DSK_MARGINE) / PH_DSK_CELLA_L);
    double fr = floor((y - PH_DSK_MARGINE) / PH_DSK_CELLA_A);
    if (!(fc == fc))
        fc = 0; /* NaN */
    if (!(fr == fr))
        fr = 0;
    c.c = fc < 0 ? 0 : (fc > g.colonne - 1 ? g.colonne - 1 : (int)fc);
    c.r = fr < 0 ? 0 : (fr > g.righe - 1 ? g.righe - 1 : (int)fr);
    return c;
}

void ph_dsk_cella_posizione(PhDskCella c, int *x, int *y)
{
    if (x != NULL)
        *x = PH_DSK_MARGINE + c.c * PH_DSK_CELLA_L;
    if (y != NULL)
        *y = PH_DSK_MARGINE + c.r * PH_DSK_CELLA_A;
}

static bool in_griglia(PhDskGriglia g, PhDskCella c)
{
    return c.c >= 0 && c.c < g.colonne && c.r >= 0 && c.r < g.righe;
}

/* Cella libera piu' vicina in una mappa di occupazione (colonna * righe + riga). */
static bool vicina(PhDskGriglia g, const unsigned char *presa, PhDskCella voluta, PhDskCella *out)
{
    long migliore = -1;
    for (int c = 0; c < g.colonne; c++) {
        for (int r = 0; r < g.righe; r++) {
            if (presa[(size_t)c * (size_t)g.righe + (size_t)r])
                continue;
            long d = labs((long)c - voluta.c) + labs((long)r - voluta.r);
            if (migliore < 0 || d < migliore) {
                migliore = d;
                out->c = c;
                out->r = r;
            }
        }
    }
    return migliore >= 0;
}

bool ph_dsk_cella_libera(PhDskGriglia g, const PhDskCella *occupate, size_t n, PhDskCella voluta,
                         PhDskCella *out)
{
    if (g.colonne < 1 || g.righe < 1 || out == NULL)
        return false;
    unsigned char *presa = calloc((size_t)g.colonne * (size_t)g.righe, 1);
    if (presa == NULL)
        return false;
    for (size_t i = 0; occupate != NULL && i < n; i++)
        if (in_griglia(g, occupate[i]))
            presa[(size_t)occupate[i].c * (size_t)g.righe + (size_t)occupate[i].r] = 1;
    PhDskCella ris = {0, 0};
    bool ok = vicina(g, presa, voluta, &ris);
    free(presa);
    if (ok)
        *out = ris;
    return ok;
}

void ph_dsk_disponi(PhDskGriglia g, const PhDskCella *volute, size_t n, PhDskCella *out,
                    bool *visibile)
{
    for (size_t i = 0; i < n; i++) {
        out[i] = volute[i];
        visibile[i] = false;
    }
    if (g.colonne < 1 || g.righe < 1)
        return;
    unsigned char *presa = calloc((size_t)g.colonne * (size_t)g.righe, 1);
    if (presa == NULL)
        return;
    /* Prima chi sta gia' bene (nell'ordine del profilo: in caso di doppioni vince
     * il primo), poi gli altri nella libera piu' vicina alla cella voluta. */
    for (size_t i = 0; i < n; i++) {
        if (!in_griglia(g, volute[i]))
            continue;
        unsigned char *p = &presa[(size_t)volute[i].c * (size_t)g.righe + (size_t)volute[i].r];
        if (*p)
            continue;
        *p = 1;
        visibile[i] = true;
    }
    for (size_t i = 0; i < n; i++) {
        if (visibile[i])
            continue;
        PhDskCella v = volute[i];
        v.c = v.c < 0 ? 0 : (v.c > g.colonne - 1 ? g.colonne - 1 : v.c);
        v.r = v.r < 0 ? 0 : (v.r > g.righe - 1 ? g.righe - 1 : v.r);
        PhDskCella ris;
        if (!vicina(g, presa, v, &ris))
            continue;
        presa[(size_t)ris.c * (size_t)g.righe + (size_t)ris.r] = 1;
        out[i] = ris;
        visibile[i] = true;
    }
    free(presa);
}

/* ---- Widget --------------------------------------------------------------- */
int ph_dsk_larghezza_widget(PhCfgWidgetDim d)
{
    return d == PH_CFG_WIDGET_PICCOLO ? 200 : (d == PH_CFG_WIDGET_GRANDE ? 320 : 250);
}

/* Math.round del mockup: la meta' sale. */
static long arrotonda(double v)
{
    return (long)floor(v + 0.5);
}

/* Due rettangoli (posto + misura, nelle coordinate dal bordo destro e dall'alto) si toccano
 * se, allargati di `margine`, si sovrappongono sui due assi. */
static bool si_toccano(int d1, int a1, PhDskMisura m1, int d2, int a2, PhDskMisura m2, int margine)
{
    return d1 < d2 + m2.larghezza + margine && d2 < d1 + m1.larghezza + margine &&
           a1 < a2 + m2.altezza + margine && a2 < a1 + m1.altezza + margine;
}

void ph_dsk_posto_widget_nuovo(int larghezza_area, int altezza_utile, const PhDskWidgetPos *esistenti,
                               size_t n, PhDskMisura nuovo, int *destra, int *alto)
{
    for (int d = PH_DSK_MARGINE; d + nuovo.larghezza <= larghezza_area - 8; d += PH_DSK_PASSO)
        for (int a = PH_DSK_MARGINE; a + nuovo.altezza <= altezza_utile - 8; a += PH_DSK_PASSO) {
            bool libero = true;
            for (size_t i = 0; i < n && libero; i++)
                libero = !si_toccano(d, a, nuovo, esistenti[i].destra, esistenti[i].alto,
                                     (PhDskMisura){esistenti[i].larghezza, esistenti[i].altezza}, 12);
            if (libero) {
                *destra = d;
                *alto = a;
                return;
            }
        }
    *destra = 16 + (int)(n % 4) * 24;
    *alto = 16 + (int)(n % 4) * 24;
}

static int aggancia(double v)
{
    long p = arrotonda(v / PH_DSK_PASSO) * PH_DSK_PASSO;
    if (p < 0)
        p = 0;
    if (p > 16384) /* il tetto del profilo (config.h) */
        p = 16384;
    return (int)p;
}

int ph_dsk_aggancia_destra(int destra_iniziale, double dx)
{
    return aggancia((double)destra_iniziale - dx);
}

int ph_dsk_aggancia_alto(int alto_iniziale, double dy)
{
    return aggancia((double)alto_iniziale + dy);
}

/* ---- Forma e misura libera ---------------------------------------------------- */
PhDskForma ph_dsk_forma(PhCfgWidgetTipo tipo, unsigned stile)
{
    switch (tipo) {
    case PH_CFG_WIDGET_OROLOGIO:
        stile = ph_dsk_stile_valido_di(tipo, stile);
        return stile == PH_DSK_OROLOGIO_CLASSICO || stile == PH_DSK_OROLOGIO_ESSENZIALE
                   ? PH_DSK_FORMA_TONDO
                   : PH_DSK_FORMA_MATTONELLA;
    case PH_CFG_WIDGET_BATTERIA: return PH_DSK_FORMA_TONDO;
    case PH_CFG_WIDGET_SVEGLIA:
    case PH_CFG_WIDGET_INTERRUTTORI: return PH_DSK_FORMA_CAPSULA;
    default: return PH_DSK_FORMA_MATTONELLA;
    }
}

int ph_dsk_raggio(PhDskForma forma, int larghezza, int altezza)
{
    int corto = larghezza < altezza ? larghezza : altezza;
    if (corto < 0)
        corto = 0;
    switch (forma) {
    case PH_DSK_FORMA_TONDO: return corto / 2;
    case PH_DSK_FORMA_CAPSULA: return (altezza > 0 ? altezza : 0) / 2;
    default: return corto / 2 < PH_DSK_RAGGIO_MATTONELLA ? corto / 2 : PH_DSK_RAGGIO_MATTONELLA;
    }
}

void ph_dsk_imbottitura(PhDskForma forma, int *orizzontale, int *verticale)
{
    switch (forma) {
    case PH_DSK_FORMA_TONDO: *orizzontale = 12; *verticale = 12; break;
    case PH_DSK_FORMA_CAPSULA: *orizzontale = 24; *verticale = 10; break;
    default: *orizzontale = 20; *verticale = 16; break;
    }
}

/* Misure che tengono il contenuto di oggi (stime sul contenuto piu' l'imbottitura della
 * forma): larghezze come le vecchie 200 / 250 / 320. Valgono come minimo (Piccolo) e come
 * punto di partenza dei profili senza misure. Per ogni tipo le tre misure crescono in
 * larghezza, cosi' ph_dsk_livello ritrova il livello di ogni misura equivalente. */
typedef struct {
    PhDskMisura m[3]; /* Piccolo, Medio, Grande */
} Equivalenti;

static Equivalenti equivalenti(PhCfgWidgetTipo tipo, unsigned stile)
{
    stile = ph_dsk_stile_valido_di(tipo, stile);
    switch (tipo) {
    case PH_CFG_WIDGET_OROLOGIO:
        switch (stile) {
        case PH_DSK_OROLOGIO_CLASSICO: return (Equivalenti){{{144, 144}, {184, 184}, {224, 224}}};
        case PH_DSK_OROLOGIO_ESSENZIALE: return (Equivalenti){{{152, 152}, {184, 184}, {216, 216}}};
        case PH_DSK_OROLOGIO_PALETTE: return (Equivalenti){{{200, 90}, {250, 136}, {320, 156}}};
        case PH_DSK_OROLOGIO_TESTUALE: return (Equivalenti){{{200, 106}, {250, 128}, {320, 128}}};
        case PH_DSK_OROLOGIO_DOPPIO_FUSO: return (Equivalenti){{{200, 100}, {250, 122}, {320, 122}}};
        default: return (Equivalenti){{{200, 80}, {250, 100}, {320, 100}}};
        }
    case PH_CFG_WIDGET_CALENDARIO: return (Equivalenti){{{200, 110}, {250, 150}, {320, 210}}};
    case PH_CFG_WIDGET_STATO: return (Equivalenti){{{200, 100}, {250, 124}, {320, 164}}};
    case PH_CFG_WIDGET_NOTIFICHE: return (Equivalenti){{{200, 104}, {250, 128}, {320, 168}}};
    case PH_CFG_WIDGET_MEMORIA: return (Equivalenti){{{200, 120}, {250, 144}, {320, 208}}};
    case PH_CFG_WIDGET_METEO: return (Equivalenti){{{200, 150}, {250, 210}, {320, 300}}};
    case PH_CFG_WIDGET_CALENDARIO_MESE: return (Equivalenti){{{200, 110}, {250, 290}, {320, 420}}};
    case PH_CFG_WIDGET_SVEGLIA: return (Equivalenti){{{200, 72}, {250, 80}, {320, 104}}};
    case PH_CFG_WIDGET_BATTERIA: return (Equivalenti){{{136, 136}, {176, 176}, {216, 216}}};
    case PH_CFG_WIDGET_INTERRUTTORI: return (Equivalenti){{{200, 72}, {250, 96}, {320, 104}}};
    case PH_CFG_WIDGET_TROVA: return (Equivalenti){{{200, 96}, {250, 120}, {320, 140}}};
    case PH_CFG_WIDGET_APP_FREQUENTI: return (Equivalenti){{{200, 168}, {250, 180}, {320, 260}}};
    case PH_CFG_WIDGET_TRASFERIMENTI: return (Equivalenti){{{200, 146}, {250, 206}, {320, 296}}};
    default: return (Equivalenti){{{200, 110}, {250, 150}, {320, 210}}};
    }
}

PhDskMisura ph_dsk_misura_equivalente(PhCfgWidgetTipo tipo, unsigned stile, PhCfgWidgetDim dim)
{
    int k = dim == PH_CFG_WIDGET_PICCOLO ? 0 : (dim == PH_CFG_WIDGET_GRANDE ? 2 : 1);
    return equivalenti(tipo, stile).m[k];
}

PhDskLimiti ph_dsk_misura_limiti(PhCfgWidgetTipo tipo, unsigned stile)
{
    PhDskLimiti l;
    l.min = equivalenti(tipo, stile).m[0];
    switch (ph_dsk_forma(tipo, stile)) {
    case PH_DSK_FORMA_TONDO: l.max = (PhDskMisura){320, 320}; break;
    case PH_DSK_FORMA_CAPSULA: l.max = (PhDskMisura){480, 128}; break;
    default: l.max = (PhDskMisura){480, 640}; break;
    }
    return l;
}

static int stringi(int v, int min, int max)
{
    return v < min ? min : (v > max ? max : v);
}

PhDskMisura ph_dsk_misura_vincola(PhCfgWidgetTipo tipo, unsigned stile, PhDskMisura m)
{
    PhDskLimiti l = ph_dsk_misura_limiti(tipo, stile);
    if (ph_dsk_forma(tipo, stile) == PH_DSK_FORMA_TONDO) {
        int lato = stringi(m.larghezza < m.altezza ? m.larghezza : m.altezza, l.min.larghezza,
                           l.max.larghezza);
        return (PhDskMisura){lato, lato};
    }
    return (PhDskMisura){stringi(m.larghezza, l.min.larghezza, l.max.larghezza),
                         stringi(m.altezza, l.min.altezza, l.max.altezza)};
}

bool ph_dsk_predefiniti_alti(const PhCfgWidget *w, size_t n, int *alti)
{
    static const PhCfgWidgetTipo tipi[3] = {PH_CFG_WIDGET_OROLOGIO, PH_CFG_WIDGET_STATO, PH_CFG_WIDGET_NOTIFICHE};
    static const int alti_vecchi[3] = {16, 120, 240};
    if (w == NULL || alti == NULL || n != 3)
        return false;
    for (size_t i = 0; i < 3; i++)
        if (w[i].tipo != tipi[i] || w[i].destra != 16 || w[i].alto != alti_vecchi[i] ||
            w[i].dimensione != PH_CFG_WIDGET_MEDIO || w[i].larghezza != 0 || w[i].altezza != 0 ||
            w[i].stile != 0 || w[i].opzione != 0)
            return false;
    int y = 16;
    for (size_t i = 0; i < 3; i++) {
        alti[i] = y;
        PhDskMisura m = ph_dsk_misura_da_profilo(tipi[i], 0, PH_CFG_WIDGET_MEDIO, 0, 0);
        y += m.altezza + PH_DSK_DISTANZA_WIDGET;
    }
    return true;
}

PhDskMisura ph_dsk_misura_da_profilo(PhCfgWidgetTipo tipo, unsigned stile, PhCfgWidgetDim dim,
                                     int larghezza, int altezza)
{
    if (larghezza <= 0 || altezza <= 0)
        return ph_dsk_misura_equivalente(tipo, stile, dim);
    return ph_dsk_misura_vincola(tipo, stile, (PhDskMisura){larghezza, altezza});
}

PhDskMisura ph_dsk_misura_cambio_stile(PhCfgWidgetTipo tipo, unsigned stile_vecchio,
                                       unsigned stile_nuovo, PhDskMisura attuale)
{
    if (ph_dsk_forma(tipo, stile_vecchio) != ph_dsk_forma(tipo, stile_nuovo))
        return (PhDskMisura){0, 0};
    return ph_dsk_misura_vincola(tipo, stile_nuovo, attuale);
}

PhCfgWidgetDim ph_dsk_livello(PhCfgWidgetTipo tipo, unsigned stile, PhDskMisura m)
{
    Equivalenti e = equivalenti(tipo, stile);
    for (int k = 2; k > 0; k--)
        if (m.larghezza >= e.m[k].larghezza && m.altezza >= e.m[k].altezza)
            return k == 2 ? PH_CFG_WIDGET_GRANDE : PH_CFG_WIDGET_MEDIO;
    return PH_CFG_WIDGET_PICCOLO;
}

bool ph_dsk_ridimensiona(PhCfgWidgetTipo tipo, unsigned stile, PhDskMisura iniziale, int destra0,
                         int alto0, double dx, double dy, int altezza_utile, PhDskMisura *nuova,
                         int *destra)
{
    PhDskLimiti lim = ph_dsk_misura_limiti(tipo, stile);
    bool tondo = ph_dsk_forma(tipo, stile) == PH_DSK_FORMA_TONDO;
    long passo_l = arrotonda(dx / PH_DSK_PASSO) * PH_DSK_PASSO;
    long passo_a = arrotonda(dy / PH_DSK_PASSO) * PH_DSK_PASSO;
    if (tondo)
        passo_l = passo_a = arrotonda((dx + dy) / 2.0 / PH_DSK_PASSO) * PH_DSK_PASSO;
    /* Tetti dell'area: il bordo destro (l'angolo in alto a sinistra resta fermo) e il fondo.
     * Un widget gia' oltre il tetto non si restringe da solo. */
    int tetto_l = destra0 + iniziale.larghezza, tetto_a = altezza_utile - alto0;
    if (tetto_l < iniziale.larghezza)
        tetto_l = iniziale.larghezza;
    if (tetto_a < iniziale.altezza)
        tetto_a = iniziale.altezza;
    if (tetto_l > lim.max.larghezza)
        tetto_l = lim.max.larghezza;
    if (tetto_a > lim.max.altezza)
        tetto_a = lim.max.altezza;
    long l = iniziale.larghezza + passo_l, a = iniziale.altezza + passo_a;
    if (tondo) {
        int tetto = tetto_l < tetto_a ? tetto_l : tetto_a;
        tetto_l = tetto_a = tetto;
    }
    /* Il minimo del tipo vince sul tetto dell'area. */
    l = stringi((int)(l > 100000 ? 100000 : (l < -100000 ? -100000 : l)), lim.min.larghezza,
                tetto_l > lim.min.larghezza ? tetto_l : lim.min.larghezza);
    a = stringi((int)(a > 100000 ? 100000 : (a < -100000 ? -100000 : a)), lim.min.altezza,
                tetto_a > lim.min.altezza ? tetto_a : lim.min.altezza);
    if (tondo)
        a = l;
    int d = destra0 - ((int)l - iniziale.larghezza);
    if (d < 0)
        d = 0;
    bool cambiato = l != iniziale.larghezza || a != iniziale.altezza;
    nuova->larghezza = (int)l;
    nuova->altezza = (int)a;
    *destra = d;
    return cambiato;
}

int ph_dsk_maniglia_margine(int raggio, int imbottitura)
{
    /* La maniglia (16 px) deve stare dentro la curva: il suo centro a 45 gradi, 10 px dentro
     * il bordo. Dall'angolo del widget il centro dista raggio * (1 - cos 45) + 10 * cos 45. */
    int m = (int)(raggio * 0.2929 + 7.07 - 8.0) - imbottitura;
    return m > 0 ? m : 0;
}

/* ---- Menu ----------------------------------------------------------------- */
void ph_dsk_menu_posizione(double x, double y, int larg, int alt, int largh_menu, int alt_menu,
                           int *ox, int *oy)
{
    double mx = larg - largh_menu;
    double my = alt - alt_menu;
    double px = x < mx ? x : mx;
    double py = y < my ? y : my;
    if (!(px == px) || px < 0)
        px = 0;
    if (!(py == py) || py < 0)
        py = 0;
    *ox = (int)px;
    *oy = (int)py;
}

/* ---- Recenti -------------------------------------------------------------- */
bool ph_dsk_nome_immagine(const char *nome)
{
    if (nome == NULL || nome[0] == '\0' || nome[0] == '.' || strchr(nome, '/') != NULL)
        return false;
    const char *p = strrchr(nome, '.');
    if (p == NULL)
        return false;
    p++;
    static const char *const ammesse[] = {"jpg", "jpeg", "png", "webp"};
    for (size_t i = 0; i < sizeof ammesse / sizeof ammesse[0]; i++) {
        const char *a = ammesse[i];
        size_t k = 0;
        while (a[k] != '\0' && p[k] != '\0' && tolower((unsigned char)p[k]) == a[k])
            k++;
        if (a[k] == '\0' && p[k] == '\0')
            return true;
    }
    return false;
}

void ph_dsk_nome_breve(const char *nome, char *out, size_t cap)
{
    if (cap == 0)
        return;
    if (nome == NULL)
        nome = "";
    const char *p = strrchr(nome, '.');
    size_t n = (p != NULL && p != nome) ? (size_t)(p - nome) : strlen(nome);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, nome, n);
    out[n] = '\0';
}

static int confronta_recenti(const void *a, const void *b)
{
    const PhDskRecente *x = a, *y = b;
    if (x->modificato != y->modificato)
        return x->modificato > y->modificato ? -1 : 1;
    return strcmp(x->percorso != NULL ? x->percorso : "", y->percorso != NULL ? y->percorso : "");
}

size_t ph_dsk_recenti_ordina(PhDskRecente *v, size_t n, size_t max)
{
    if (v != NULL && n > 1)
        qsort(v, n, sizeof *v, confronta_recenti);
    return n < max ? n : max;
}

bool ph_dsk_recenti_inserisci(PhDskRecente *v, size_t *n, size_t max, const PhDskRecente *nuovo,
                              PhDskRecente *scartato)
{
    if (max == 0) {
        *scartato = *nuovo;
        return true;
    }
    size_t pos = 0;
    while (pos < *n && confronta_recenti(&v[pos], nuovo) <= 0)
        pos++;
    if (pos >= max) { /* piu' vecchio di tutti i `max` gia' tenuti */
        *scartato = *nuovo;
        return true;
    }
    bool pieno = *n >= max;
    if (pieno)
        *scartato = v[max - 1]; /* l'ultimo esce */
    else
        (*n)++;
    for (size_t i = (pieno ? max - 1 : *n - 1); i > pos; i--)
        v[i] = v[i - 1];
    v[pos] = *nuovo;
    return pieno;
}

/* ---- Sfondo --------------------------------------------------------------- */
PhDskRett ph_dsk_rett_immagine(PhCfgAdattamento m, float W, float H, float iw, float ih)
{
    PhDskRett r = {0, 0, 0, 0};
    if (!(W > 0) || !(H > 0) || !(iw > 0) || !(ih > 0))
        return r;
    switch (m) {
    case PH_CFG_ADATT_ALLUNGA:
        r.w = W;
        r.h = H;
        return r;
    case PH_CFG_ADATT_CENTRA:
        r.w = iw;
        r.h = ih;
        break;
    case PH_CFG_ADATT_ADATTA:
    case PH_CFG_ADATT_RIEMPI:
    default: {
        float sx = W / iw, sy = H / ih;
        float s = m == PH_CFG_ADATT_ADATTA ? (sx < sy ? sx : sy) : (sx > sy ? sx : sy);
        r.w = iw * s;
        r.h = ih * s;
        break;
    }
    }
    r.x = (W - r.w) / 2;
    r.y = (H - r.h) / 2;
    return r;
}

int ph_dsk_angolo(PhCfgDirezione d)
{
    switch (d) {
    case PH_CFG_DIR_VERTICALE: return 180;
    case PH_CFG_DIR_ORIZZONTALE: return 90;
    case PH_CFG_DIR_DIAGONALE_INVERSA: return 45;
    case PH_CFG_DIR_DIAGONALE:
    default: return 135;
    }
}

void ph_dsk_gradiente(PhCfgDirezione d, float w, float h, float *x0, float *y0, float *x1,
                      float *y1)
{
    double a = ph_dsk_angolo(d) * M_PI / 180.0;
    double dx = sin(a), dy = -cos(a);
    /* Lunghezza della linea del gradiente CSS: tocca gli angoli dell'area. */
    double lun = fabs(w * dx) + fabs(h * dy);
    double cx = w / 2.0, cy = h / 2.0;
    *x0 = (float)(cx - dx * lun / 2);
    *y0 = (float)(cy - dy * lun / 2);
    *x1 = (float)(cx + dx * lun / 2);
    *y1 = (float)(cy + dy * lun / 2);
}

/* ---- Misura delle immagini ostili ----------------------------------------- */
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t le16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t le24(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static bool misura_png(const uint8_t *d, size_t n, uint32_t *w, uint32_t *h)
{
    static const uint8_t firma[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (n < 24 || memcmp(d, firma, 8) != 0 || memcmp(d + 12, "IHDR", 4) != 0)
        return false;
    *w = be32(d + 16);
    *h = be32(d + 20);
    return true;
}

static bool misura_jpeg(const uint8_t *d, size_t n, uint32_t *w, uint32_t *h)
{
    if (n < 4 || d[0] != 0xff || d[1] != 0xd8)
        return false;
    size_t i = 2;
    while (i + 4 <= n) {
        if (d[i] != 0xff) {
            return false; /* fuori sincronia: non e' un JPEG che sappiamo leggere */
        }
        uint8_t m = d[i + 1];
        if (m == 0xff) { /* riempimento */
            i++;
            continue;
        }
        if (m == 0x01 || (m >= 0xd0 && m <= 0xd8)) { /* marcatori senza lunghezza */
            i += 2;
            continue;
        }
        if (m == 0xd9 || m == 0xda)
            return false; /* fine o dati: la misura non e' comparsa */
        size_t lun = ((size_t)d[i + 2] << 8) | d[i + 3];
        if (lun < 2)
            return false;
        bool sof = m >= 0xc0 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc;
        if (sof) {
            if (lun < 8 || i + 9 > n)
                return false;
            *h = ((uint32_t)d[i + 5] << 8) | d[i + 6];
            *w = ((uint32_t)d[i + 7] << 8) | d[i + 8];
            return true;
        }
        if (lun > n - i - 2)
            return false; /* il segmento esce dal buffer */
        i += 2 + lun;
    }
    return false;
}

static bool misura_webp(const uint8_t *d, size_t n, uint32_t *w, uint32_t *h)
{
    if (n < 30 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WEBP", 4) != 0)
        return false;
    if (memcmp(d + 12, "VP8 ", 4) == 0) {
        if (d[23] != 0x9d || d[24] != 0x01 || d[25] != 0x2a)
            return false;
        *w = le16(d + 26) & 0x3fff;
        *h = le16(d + 28) & 0x3fff;
        return true;
    }
    if (memcmp(d + 12, "VP8L", 4) == 0) {
        if (d[20] != 0x2f)
            return false;
        uint32_t bit = (uint32_t)d[21] | ((uint32_t)d[22] << 8) | ((uint32_t)d[23] << 16) |
                       ((uint32_t)d[24] << 24);
        *w = (bit & 0x3fff) + 1;
        *h = ((bit >> 14) & 0x3fff) + 1;
        return true;
    }
    if (memcmp(d + 12, "VP8X", 4) == 0) {
        *w = le24(d + 24) + 1;
        *h = le24(d + 27) + 1;
        return true;
    }
    return false;
}

bool ph_dsk_immagine_misura(const uint8_t *dati, size_t n, uint32_t *w, uint32_t *h)
{
    if (dati == NULL || w == NULL || h == NULL)
        return false;
    uint32_t a = 0, b = 0;
    if (!(misura_png(dati, n, &a, &b) || misura_jpeg(dati, n, &a, &b) ||
          misura_webp(dati, n, &a, &b)))
        return false;
    *w = a;
    *h = b;
    return true;
}

const char *ph_dsk_immagine_formato(const uint8_t *dati, size_t n)
{
    uint32_t w, h;
    if (dati == NULL)
        return NULL;
    if (misura_png(dati, n, &w, &h))
        return "png";
    if (misura_jpeg(dati, n, &w, &h))
        return "jpeg";
    if (misura_webp(dati, n, &w, &h))
        return "webp";
    return NULL;
}

bool ph_dsk_immagine_ammessa(uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0 || w > PH_DSK_IMMAGINE_LATO_MAX || h > PH_DSK_IMMAGINE_LATO_MAX)
        return false;
    return (uint64_t)w * h <= PH_DSK_IMMAGINE_PIXEL_MAX;
}

void ph_dsk_immagine_ridotta(uint32_t w, uint32_t h, uint32_t W, uint32_t H, uint32_t *nw,
                             uint32_t *nh)
{
    *nw = w;
    *nh = h;
    if (w == 0 || h == 0 || W == 0 || H == 0)
        return;
    double s = fmax((double)W / w, (double)H / h);
    if (s >= 1.0)
        return;
    double a = ceil(w * s), b = ceil(h * s);
    *nw = a < 1 ? 1 : (uint32_t)a;
    *nh = b < 1 ? 1 : (uint32_t)b;
}

void ph_dsk_nome_sfondo(uint32_t numero, char *out, size_t cap)
{
    if (cap > 0)
        snprintf(out, cap, "sfondo-%u.png", numero);
}

/* ---- Contenuto dei widget ------------------------------------------------- */
static size_t per_dim(PhCfgWidgetDim d, size_t p, size_t m, size_t g)
{
    return d == PH_CFG_WIDGET_PICCOLO ? p : (d == PH_CFG_WIDGET_GRANDE ? g : m);
}

size_t ph_dsk_righe_notifiche(PhCfgWidgetDim d) { return per_dim(d, 1, 2, 4); }
size_t ph_dsk_righe_stato(PhCfgWidgetDim d) { return per_dim(d, 0, 1, 3); }
size_t ph_dsk_righe_calendario(PhCfgWidgetDim d) { return per_dim(d, 1, 2, 4); }

static void vista_azzera(PhDskVista *v)
{
    memset(v, 0, sizeof *v);
    v->percento = -1;
}

static void copia(char *dst, size_t cap, const char *src)
{
    if (cap == 0)
        return;
    snprintf(dst, cap, "%s", src != NULL ? src : "");
}

static void aggiungi_riga(PhDskVista *v, const char *sinistra, const char *destra)
{
    if (v->n_righe >= PH_DSK_RIGHE_MAX)
        return;
    PhDskRiga *r = &v->righe[v->n_righe++];
    copia(r->sinistra, sizeof r->sinistra, sinistra);
    copia(r->destra, sizeof r->destra, destra);
}

void ph_dsk_data_lunga(int giorno_settimana, int giorno, int mese, char *out, size_t cap)
{
    if (cap == 0)
        return;
    if (giorno_settimana < 1 || giorno_settimana > 7 || mese < 1 || mese > 12 || giorno < 1 ||
        giorno > 31) {
        out[0] = '\0';
        return;
    }
    char num[16];
    snprintf(num, sizeof num, "%d", giorno);
    ph_lingua_componi(out, cap, ph_t("{giorno} {numero} {mese}"), "giorno",
                      ph_dsk_nome_giorno(giorno_settimana), "numero", num, "mese",
                      ph_dsk_nome_mese(mese), (const char *)NULL);
}

void ph_dsk_vista_orologio(PhCfgWidgetDim d, const char *ora, const char *data, PhDskVista *v)
{
    vista_azzera(v);
    copia(v->testa, sizeof v->testa, ora);
    v->testa_grande = true;
    /* Il Grande resta come il Medio: il secondo fuso non e' deciso (COMPITO.md). */
    if (d != PH_CFG_WIDGET_PICCOLO)
        copia(v->sotto, sizeof v->sotto, data);
}

void ph_dsk_vista_stato(PhCfgWidgetDim d, const PhSrvTelefono *t, const PhSrvCollegamento *c,
                        PhDskVista *v)
{
    vista_azzera(v);
    copia(v->titolo, sizeof v->titolo, ph_t("Telefono"));
    if (t != NULL && t->noto) {
        int b = t->batteria < 0 ? 0 : (t->batteria > 100 ? 100 : t->batteria);
        char num[16];
        snprintf(num, sizeof num, "%d", b);
        ph_lingua_componi(v->testa, sizeof v->testa, ph_t("Batteria {percento}%"), "percento", num,
                          (const char *)NULL);
        v->percento = b;
    }
    size_t max = ph_dsk_righe_stato(d);
    PhDskVista tutte;
    vista_azzera(&tutte);
    if (t != NULL && t->noto) {
        const char *rete = t->rete == PH_SRV_RETE_WIFI
                               ? ph_t("Wi-Fi connesso")
                               : (t->rete == PH_SRV_RETE_CELLULARE ? ph_t("Rete cellulare")
                                                                   : ph_t("Nessuna rete"));
        aggiungi_riga(&tutte, ph_t("Rete"), rete);
        if (t->pannello_noto)
            aggiungi_riga(&tutte, ph_t("Pannello fisico"),
                          t->pannello_acceso ? ph_t("acceso") : ph_t("spento"));
    }
    if (c != NULL && c->ha_telefono)
        aggiungi_riga(&tutte, ph_t("Schermo"),
                      c->stato == PH_SRV_BLOCCATO ? ph_t("bloccato") : ph_t("sbloccato"));
    for (size_t i = 0; i < tutte.n_righe && i < max; i++)
        v->righe[v->n_righe++] = tutte.righe[i];
}

void ph_dsk_vista_notifiche(PhCfgWidgetDim d, const PhDskNotifica *n, size_t quante,
                            PhDskVista *v)
{
    vista_azzera(v);
    copia(v->titolo, sizeof v->titolo, ph_t("Notifiche recenti"));
    if (quante == 0) {
        copia(v->testa, sizeof v->testa, ph_t("Nessuna notifica"));
        return;
    }
    char num[24];
    snprintf(num, sizeof num, "%zu", quante);
    ph_lingua_componi(v->testa, sizeof v->testa,
                      quante == 1 ? ph_t("{numero} nuova") : ph_t("{numero} nuove"), "numero", num,
                      (const char *)NULL);
    size_t max = ph_dsk_righe_notifiche(d);
    for (size_t i = 0; i < quante && i < max; i++) {
        const char *ti = n[i].titolo != NULL ? n[i].titolo : "";
        const char *te = n[i].testo != NULL ? n[i].testo : "";
        char riga[256];
        if (te[0] == '\0')
            copia(riga, sizeof riga, ti);
        else
            ph_lingua_componi(riga, sizeof riga, ph_t("{titolo}: {testo}"), "titolo", ti, "testo",
                              te, (const char *)NULL);
        aggiungi_riga(v, riga, "");
    }
}

void ph_dsk_formato_gb(uint64_t byte, char *out, size_t cap)
{
    if (cap == 0)
        return;
    double g = (double)byte / 1073741824.0;
    long long dec = (long long)floor(g * 10.0 + 0.5);
    char sep = ph_lingua_attiva() == PH_LINGUA_IT ? ',' : '.';
    snprintf(out, cap, "%lld%c%lld", dec / 10, sep, dec % 10);
}

size_t ph_dsk_righe_memoria(PhCfgWidgetDim d) { return per_dim(d, 0, 1, 4); }

static void riga_memoria(PhDskVista *v, size_t max, const char *nome, const PhSrvVoceMemoria *m)
{
    if (!m->leggibile || v->n_righe >= max)
        return; /* non leggibile: non si inventa */
    char n[32], q[48];
    ph_dsk_formato_gb(m->byte, n, sizeof n);
    ph_lingua_componi(q, sizeof q, ph_t("{numero} GB"), "numero", n, (const char *)NULL);
    aggiungi_riga(v, nome, q);
}

void ph_dsk_vista_memoria(PhCfgWidgetDim d, bool noto, uint64_t totale, uint64_t libera,
                          const PhSrvMemoriaDettaglio *det, PhDskVista *v)
{
    vista_azzera(v);
    copia(v->titolo, sizeof v->titolo, ph_t("Memoria del telefono"));
    if (!noto || totale == 0)
        return;
    uint64_t usata = totale > libera ? totale - libera : 0;
    char n1[32], n2[32], q1[48], q2[48];
    ph_dsk_formato_gb(usata, n1, sizeof n1);
    ph_dsk_formato_gb(totale, n2, sizeof n2);
    ph_lingua_componi(q1, sizeof q1, ph_t("{numero} GB"), "numero", n1, (const char *)NULL);
    ph_lingua_componi(q2, sizeof q2, ph_t("{numero} GB"), "numero", n2, (const char *)NULL);
    ph_lingua_componi(v->testa, sizeof v->testa, ph_t("{quantita} usati"), "quantita", q1,
                      (const char *)NULL);
    ph_lingua_componi(v->sotto, sizeof v->sotto, ph_t("su {quantita}"), "quantita", q2,
                      (const char *)NULL);
    v->percento = (int)((usata * 100u + totale / 2) / totale);
    if (v->percento > 100)
        v->percento = 100;
    if (det != NULL) {
        size_t max = ph_dsk_righe_memoria(d);
        riga_memoria(v, max, ph_t("Immagini"), &det->immagini);
        riga_memoria(v, max, ph_t("Video"), &det->video);
        riga_memoria(v, max, ph_t("Audio"), &det->audio);
        riga_memoria(v, max, ph_t("App"), &det->app);
    }
}

/* Divisione per eccesso verso -infinito (i giorni prima del 1970 non esistono
 * per noi, ma lo scarto di fuso puo' rendere negativo un istante vicino a 0). */
static int64_t div_giorno(int64_t s)
{
    int64_t q = s / 86400;
    if (s % 86400 < 0)
        q--;
    return q;
}

size_t ph_dsk_eventi_di_oggi(const PhDskEvento *ev, size_t n, uint64_t adesso_ms, int scarto_s,
                             size_t max, size_t *indici)
{
    size_t k = 0;
    int64_t oggi = div_giorno((int64_t)(adesso_ms / 1000) + scarto_s);
    for (size_t i = 0; ev != NULL && i < n && k < max; i++) {
        bool dentro;
        if (ev[i].tutto_il_giorno) {
            int64_t sd = div_giorno((int64_t)(ev[i].inizio_ms / 1000));
            int64_t ed = div_giorno((int64_t)(ev[i].fine_ms / 1000));
            if (ed <= sd)
                ed = sd + 1;
            dentro = sd <= oggi && oggi < ed;
        } else {
            int64_t sd = div_giorno((int64_t)(ev[i].inizio_ms / 1000) + scarto_s);
            bool non_finito = ev[i].fine_ms > adesso_ms ||
                              (ev[i].fine_ms == ev[i].inizio_ms && ev[i].inizio_ms >= adesso_ms);
            dentro = sd <= oggi && non_finito;
        }
        if (dentro)
            indici[k++] = i;
    }
    return k;
}

void ph_dsk_ora_evento(uint64_t ms, int scarto_s, char *out, size_t cap)
{
    if (cap == 0)
        return;
    int64_t t = (int64_t)(ms / 1000) + scarto_s;
    int64_t s = t - div_giorno(t) * 86400;
    snprintf(out, cap, "%02d:%02d", (int)(s / 3600), (int)((s % 3600) / 60));
}

void ph_dsk_vista_calendario(PhCfgWidgetDim d, bool noto, const PhDskEvento *ev, size_t n,
                             uint64_t adesso_ms, int scarto_s, const char *data, PhDskVista *v)
{
    vista_azzera(v);
    copia(v->titolo, sizeof v->titolo, ph_t("Calendario"));
    copia(v->testa, sizeof v->testa, ph_t("Oggi"));
    copia(v->sotto, sizeof v->sotto, data);
    if (!noto)
        return;
    size_t max = ph_dsk_righe_calendario(d);
    size_t idx[PH_DSK_RIGHE_MAX];
    if (max > PH_DSK_RIGHE_MAX)
        max = PH_DSK_RIGHE_MAX;
    size_t k = ph_dsk_eventi_di_oggi(ev, n, adesso_ms, scarto_s, max, idx);
    for (size_t i = 0; i < k; i++) {
        char ora[8] = "";
        if (!ev[idx[i]].tutto_il_giorno)
            ph_dsk_ora_evento(ev[idx[i]].inizio_ms, scarto_s, ora, sizeof ora);
        aggiungi_riga(v, ev[idx[i]].titolo, ora);
    }
}

/* ---- Tavolozza ------------------------------------------------------------------- */
static const uint32_t PALETTE[PH_DSK_PALETTE_N] = {
    0x000000, 0x0b1220, 0x1f2430, 0x374151, 0x6b7280, 0x9ca3af, 0xd1d5db, 0xffffff,
    0xe46767, 0xe49b67, 0xe4c567, 0x67e491, 0x67e4da, 0x67b0e4, 0x6772e4, 0xc567e4,
    0xd22d2d, 0xd2722d, 0xd2a92d, 0x2dd264, 0x2dd2c5, 0x2d8dd2, 0x2d3ad2, 0xa92dd2,
    0x8f2424, 0x8f5024, 0x8f7424, 0x248f47, 0x248f86, 0x24628f, 0x242d8f, 0x74248f,
    0x571919, 0x573319, 0x574819, 0x19572e, 0x195752, 0x193d57, 0x191e57, 0x481957,
    0x2e0f0f, 0x2e1c0f, 0x2e260f, 0x0f2e1a, 0x0f2e2b, 0x0f212e, 0x0f122e, 0x260f2e};

uint32_t ph_dsk_palette(size_t i)
{
    return i < PH_DSK_PALETTE_N ? PALETTE[i] : 0;
}

/* ---- Cartelle del telefono ---------------------------------------------------------- */
bool ph_dsk_tel_nome_valido(const char *nome)
{
    if (nome == NULL || nome[0] == '\0' || strlen(nome) > 255 || strchr(nome, '/') != NULL ||
        strcmp(nome, ".") == 0 || strcmp(nome, "..") == 0 || !g_utf8_validate(nome, -1, NULL))
        return false;
    for (const unsigned char *p = (const unsigned char *)nome; *p != '\0'; p++)
        if (*p < 0x20 || *p == 0x7f)
            return false;
    return true;
}

bool ph_dsk_tel_nascosto(const char *nome)
{
    return nome != NULL && nome[0] == '.';
}

bool ph_dsk_tel_e_radice(const char *percorso)
{
    return percorso != NULL && strcmp(percorso, "/") == 0;
}

bool ph_dsk_tel_unisci(const char *cartella, const char *nome, char *out, size_t cap)
{
    if (cap == 0)
        return false;
    out[0] = '\0';
    if (cartella == NULL || cartella[0] != '/' || !ph_dsk_tel_nome_valido(nome))
        return false;
    size_t lc = strlen(cartella), ln = strlen(nome);
    bool radice = lc == 1;
    size_t tot = lc + (radice ? 0 : 1) + ln;
    if (tot >= cap || tot > PH_DSK_TEL_PERCORSO_MAX)
        return false;
    snprintf(out, cap, "%s%s%s", cartella, radice ? "" : "/", nome);
    return true;
}

bool ph_dsk_tel_su(const char *percorso, char *out, size_t cap)
{
    if (cap < 2)
        return false;
    snprintf(out, cap, "/");
    if (percorso == NULL || percorso[0] != '/')
        return false;
    size_t n = strlen(percorso);
    while (n > 1 && percorso[n - 1] == '/')
        n--;
    if (n <= 1)
        return false; /* era gia' la radice */
    size_t i = n;
    while (i > 1 && percorso[i - 1] != '/')
        i--;
    size_t len = i > 1 ? i - 1 : 1; /* senza la barra finale, tranne la radice */
    if (len >= cap)
        return false;
    memcpy(out, percorso, len);
    out[len] = '\0';
    return true;
}

size_t ph_dsk_pagina_visibili(size_t totale, size_t pagine, size_t per_pagina)
{
    if (per_pagina == 0)
        return 0;
    if (pagine > totale / per_pagina + 1)
        return totale;
    size_t v = pagine * per_pagina;
    return v < totale ? v : totale;
}

size_t ph_dsk_pagina_prossime(size_t totale, size_t visibili, size_t per_pagina)
{
    if (visibili >= totale)
        return 0;
    size_t resto = totale - visibili;
    return resto < per_pagina ? resto : per_pagina;
}

/* ---- Orologio: stili ----------------------------------------------------------------------- */
const char *ph_dsk_stile_nome(unsigned stile)
{
    static const char *const nomi[PH_DSK_OROLOGIO_STILI] = {
        PH_T_("Digitale"),         PH_T_("Analogico classico"), PH_T_("Analogico essenziale"),
        PH_T_("A palette"),        PH_T_("Testuale"),           PH_T_("Doppio fuso")};
    return stile < PH_DSK_OROLOGIO_STILI ? ph_tv(nomi[stile]) : NULL;
}

unsigned ph_dsk_stile_valido(unsigned stile)
{
    return stile < PH_DSK_OROLOGIO_STILI ? stile : PH_DSK_OROLOGIO_DIGITALE;
}

bool ph_dsk_stile_disegnato(unsigned stile)
{
    return stile == PH_DSK_OROLOGIO_CLASSICO || stile == PH_DSK_OROLOGIO_ESSENZIALE ||
           stile == PH_DSK_OROLOGIO_PALETTE;
}

bool ph_dsk_stile_con_secondi(unsigned stile)
{
    return stile == PH_DSK_OROLOGIO_CLASSICO;
}

static const struct {
    const char *nome;
    const char *fuso;
} CITTA[] = {{PH_T_("Londra"), "Europe/London"},      {PH_T_("New York"), "America/New_York"},
             {PH_T_("Los Angeles"), "America/Los_Angeles"}, {PH_T_("Mosca"), "Europe/Moscow"},
             {PH_T_("Dubai"), "Asia/Dubai"},           {PH_T_("Pechino"), "Asia/Shanghai"},
             {PH_T_("Tokyo"), "Asia/Tokyo"},           {PH_T_("Sydney"), "Australia/Sydney"}};

size_t ph_dsk_citta_n(void)
{
    return sizeof CITTA / sizeof CITTA[0];
}

const char *ph_dsk_citta_nome(size_t i)
{
    return i < ph_dsk_citta_n() ? ph_tv(CITTA[i].nome) : NULL;
}

unsigned ph_dsk_citta_valida(unsigned i)
{
    return i < ph_dsk_citta_n() ? i : 0;
}

bool ph_dsk_ora_in_citta(uint64_t ms, int scarto_locale_s, size_t citta, int *ore, int *minuti,
                         int *giorno_diff)
{
    if (citta >= ph_dsk_citta_n() || ms > (uint64_t)INT64_MAX / 1000)
        return false;
    GTimeZone *tz = g_time_zone_new_identifier(CITTA[citta].fuso);
    if (tz == NULL)
        return false; /* nessun database dei fusi: niente numeri inventati */
    GDateTime *utc = g_date_time_new_from_unix_utc((gint64)(ms / 1000));
    GDateTime *loc = utc != NULL ? g_date_time_to_timezone(utc, tz) : NULL;
    bool ok = loc != NULL;
    if (ok) {
        *ore = g_date_time_get_hour(loc);
        *minuti = g_date_time_get_minute(loc);
        /* Giorno della citta' meno giorno locale (locale = UTC + scarto). */
        int64_t t_loc = (int64_t)(ms / 1000) + scarto_locale_s;
        int64_t g_loc = t_loc / 86400 - (t_loc % 86400 < 0 ? 1 : 0);
        int64_t t_cit = (int64_t)(ms / 1000) + g_time_zone_get_offset(tz, g_time_zone_find_interval(tz, G_TIME_TYPE_UNIVERSAL, (gint64)(ms / 1000)));
        int64_t g_cit = t_cit / 86400 - (t_cit % 86400 < 0 ? 1 : 0);
        *giorno_diff = (int)(g_cit - g_loc);
    }
    if (loc != NULL)
        g_date_time_unref(loc);
    if (utc != NULL)
        g_date_time_unref(utc);
    g_time_zone_unref(tz);
    return ok;
}

void ph_dsk_ora_testo(int ore, int minuti, char *out, size_t cap)
{
    static const char *const ORE_PAROLE[11] = {
        PH_T_("l'una"),    PH_T_("le due"),   PH_T_("le tre"),   PH_T_("le quattro"),
        PH_T_("le cinque"), PH_T_("le sei"),   PH_T_("le sette"), PH_T_("le otto"),
        PH_T_("le nove"),  PH_T_("le dieci"), PH_T_("le undici")};
    static const char *const MIN_PAROLE[30] = {
        NULL,
        NULL,
        PH_T_("due"),          PH_T_("tre"),          PH_T_("quattro"),     PH_T_("cinque"),
        PH_T_("sei"),          PH_T_("sette"),        PH_T_("otto"),        PH_T_("nove"),
        PH_T_("dieci"),        PH_T_("undici"),       PH_T_("dodici"),      PH_T_("tredici"),
        PH_T_("quattordici"),  NULL,                  PH_T_("sedici"),      PH_T_("diciassette"),
        PH_T_("diciotto"),     PH_T_("diciannove"),   PH_T_("venti"),       PH_T_("ventuno"),
        PH_T_("ventidue"),     PH_T_("ventitré"),     PH_T_("ventiquattro"), PH_T_("venticinque"),
        PH_T_("ventisei"),     PH_T_("ventisette"),   PH_T_("ventotto"),    PH_T_("ventinove")};
    /* Dopo la mezza, fino a :34, si continua con «e» (le quattro e trentuno); «meno» solo
     * da :35 (le cinque meno venticinque), come si dice davvero. */
    static const char *const OLTRE_PAROLE[4] = {PH_T_("trentuno"), PH_T_("trentadue"),
                                                PH_T_("trentatré"), PH_T_("trentaquattro")};
    if (cap == 0)
        return;
    out[0] = '\0';
    if (ore < 0 || ore > 23 || minuti < 0 || minuti > 59)
        return;
    /* L'ora a cui ci si riferisce: da :35 si conta «meno» verso la prossima. */
    int rif = minuti <= 34 ? ore : (ore + 1) % 24;
    char nome_ora[48];
    if (rif == 0)
        g_strlcpy(nome_ora, ph_t("mezzanotte"), sizeof nome_ora);
    else if (rif == 12)
        g_strlcpy(nome_ora, ph_t("mezzogiorno"), sizeof nome_ora);
    else
        g_strlcpy(nome_ora, ph_tv(ORE_PAROLE[rif % 12 - 1]), sizeof nome_ora);

    char frase[160];
    if (minuti == 0) {
        if (rif == 0 || rif == 12)
            g_strlcpy(frase, nome_ora, sizeof frase);
        else
            ph_lingua_componi(frase, sizeof frase, ph_t("{ora} in punto"), "ora", nome_ora,
                              (const char *)NULL);
    } else if (minuti == 15) {
        ph_lingua_componi(frase, sizeof frase, ph_t("{ora} e un quarto"), "ora", nome_ora, (const char *)NULL);
    } else if (minuti == 30) {
        ph_lingua_componi(frase, sizeof frase, ph_t("{ora} e mezza"), "ora", nome_ora, (const char *)NULL);
    } else if (minuti == 45) {
        ph_lingua_componi(frase, sizeof frase, ph_t("{ora} meno un quarto"), "ora", nome_ora, (const char *)NULL);
    } else if (minuti > 30 && minuti <= 34) {
        ph_lingua_componi(frase, sizeof frase, ph_t("{ora} e {oltre}"), "ora", nome_ora, "oltre",
                          ph_tv(OLTRE_PAROLE[minuti - 31]), (const char *)NULL);
    } else {
        int k = minuti < 30 ? minuti : 60 - minuti; /* 1-29 e 1-25 */
        const char *parola = k == 1 ? ph_t("un minuto") : ph_tv(MIN_PAROLE[k]);
        ph_lingua_componi(frase, sizeof frase,
                          minuti < 30 ? ph_t("{ora} e {minuti}") : ph_t("{ora} meno {minuti}"), "ora",
                          nome_ora, "minuti", parola, (const char *)NULL);
    }
    /* Prima lettera maiuscola (UTF-8). */
    if (frase[0] != '\0') {
        gunichar u = g_unichar_toupper(g_utf8_get_char(frase));
        char m[8] = {0};
        int n = g_unichar_to_utf8(u, m);
        m[n] = '\0';
        snprintf(out, cap, "%s%s", m, g_utf8_next_char(frase));
    }
}

void ph_dsk_angoli_lancette(int ore, int minuti, double secondi, double *a_ore, double *a_minuti,
                            double *a_secondi)
{
    if (ore < 0 || ore > 23)
        ore = 0;
    if (minuti < 0 || minuti > 59)
        minuti = 0;
    if (!(secondi >= 0.0) || secondi >= 60.0)
        secondi = 0.0;
    *a_secondi = secondi * 6.0;
    *a_minuti = (minuti + secondi / 60.0) * 6.0;
    *a_ore = ((ore % 12) + (minuti + secondi / 60.0) / 60.0) * 30.0;
}

unsigned ph_dsk_orologio_attesa_ms(uint64_t ms, bool secondi)
{
    uint64_t periodo = secondi ? 1000 : 60000;
    uint64_t resto = ms % periodo;
    uint64_t attesa = periodo - resto + 5; /* un soffio dopo il cambio */
    return attesa < 20 ? 20 : (unsigned)attesa;
}

int ph_dsk_quadrante_diametro(unsigned stile, PhCfgWidgetDim d)
{
    if (stile == PH_DSK_OROLOGIO_CLASSICO)
        return d == PH_CFG_WIDGET_PICCOLO ? 120 : (d == PH_CFG_WIDGET_GRANDE ? 200 : 160);
    if (stile == PH_DSK_OROLOGIO_ESSENZIALE)
        return d == PH_CFG_WIDGET_PICCOLO ? 100 : (d == PH_CFG_WIDGET_GRANDE ? 160 : 130);
    return 0;
}

int ph_dsk_orologio_altezza(unsigned stile, PhCfgWidgetDim d)
{
    switch (stile) {
    case PH_DSK_OROLOGIO_CLASSICO: return ph_dsk_quadrante_diametro(stile, d);
    case PH_DSK_OROLOGIO_ESSENZIALE: return ph_dsk_quadrante_diametro(stile, d) + 24; /* + data */
    case PH_DSK_OROLOGIO_PALETTE:
        return (d == PH_CFG_WIDGET_PICCOLO ? 56 : (d == PH_CFG_WIDGET_GRANDE ? 96 : 76)) +
               (d == PH_CFG_WIDGET_PICCOLO ? 0 : 26);
    default: return 0;
    }
}

void ph_dsk_vista_orologio_testo(PhCfgWidgetDim d, const char *frase, const char *data, PhDskVista *v)
{
    vista_azzera(v);
    copia(v->testa, sizeof v->testa, frase);
    if (d != PH_CFG_WIDGET_PICCOLO)
        copia(v->sotto, sizeof v->sotto, data);
}

void ph_dsk_vista_orologio_fuso(PhCfgWidgetDim d, const char *ora, const char *data,
                                const char *citta, bool noto2, int ore2, int minuti2,
                                int giorno_diff, PhDskVista *v)
{
    ph_dsk_vista_orologio(d, ora, data, v);
    char hm[32];
    if (noto2)
        snprintf(hm, sizeof hm, "%02d:%02d%s", ore2, minuti2,
                 giorno_diff > 0 ? " +1" : (giorno_diff < 0 ? " -1" : ""));
    else
        snprintf(hm, sizeof hm, "--:--");
    aggiungi_riga(v, citta, hm);
}
