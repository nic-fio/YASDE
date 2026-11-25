/* Regole delle finestre interne: vedi finestre.h per il contratto e per le
 * scelte prese dal mockup. Nessun orologio, nessuna grafica. */
#include "finestre/finestre.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned id;
    char app[PH_FIN_APP_MAX];
    char titolo[PH_FIN_TITOLO_MAX];
    PhFinSpec spec;
    PhFinRect rect;
    PhFinRect normale;     /* dove tornare ripristinando (sempre dentro l'area) */
    PhFinStato stato;
    PhFinStato prima;      /* disposizione prima di massimizzare */
    bool min;
    bool compat;           /* voce "Compatibilita'" (CONTRATTI.md 19): densita' VERA del telefono, flusso allargato */
    bool subito;           /* richiesta al telefono senza attesa di assestamento (cambio della compatibilita') */
    bool rec;              /* in registrazione: la misura non cambia (5.5) */
    int video_w, video_h;
    int rich_w, rich_h;    /* ultima misura chiesta al telefono (o data all'apertura) */
    int bers_w, bers_h;    /* misura attuale del flusso, osservata */
    int rich_d, bers_d;    /* densita' effettiva (dpi, arrotondata): chiesta / attuale */
    uint64_t t_cambio;     /* quando e' cambiata l'ultima volta */
    int voluta_w, voluta_h; /* misura VOLUTA (finestre non fisse): l'ultima scelta da apertura o utente, non toccata dalle riduzioni automatiche */
} Fin;

struct PhFinDesktop {
    int W, H;              /* area utile */
    double scala;
    double dapp, drif;
    int tetto;             /* gia' multiplo di 8, >= 64 */
    Fin v[PH_FIN_MAX];     /* dal fondo alla cima */
    size_t n;
    unsigned prossimo;
    unsigned attiva;
    bool riapri;
    bool pie;              /* piede tecnico di 20 px (spento di default) */
    bool automatica;       /* densita' automatica (predefinita) o fissa */
    bool tel_noto;         /* schermo del telefono noto (ph_fin_imposta_telefono) */
    int tel_w, tel_h;      /* pixel dello schermo del telefono */
    double tel_dens;       /* densita' del telefono (dpi) */
    PhFinRicordo mem[PH_FIN_MEM_MAX];
    size_t nmem;
    /* gesto in corso */
    bool g_att;
    unsigned g_id;
    PhFinGesto g_tipo;
    double g_offx, g_offy; /* puntatore - angolo della finestra, durante lo spostamento */
    double g_rx;           /* proporzione orizzontale del punto preso nel titolo */
    double g_sx, g_sy;     /* puntatore all'inizio */
    PhFinRect g_orig;
    PhFinAggancio g_agg;
};

/* Forma fissa: le solo-verticali e lo specchio non cambiano dimensioni (niente ridimensionamento, ingrandimento, aggancio, rotazione). */
#define FISSA(f) ((f)->spec.solo_verticale || (f)->spec.specchio)

static int minimo(int a, int b) { return a < b ? a : b; }
static int massimo(int a, int b) { return a > b ? a : b; }
static int limita(int v, int lo, int hi) /* se lo > hi vince lo */
{
    if (v > hi) v = hi;
    if (v < lo) v = lo;
    return v;
}
static int arrotonda(double v) { return (int)lround(v); }
/* Il puntatore arriva come double dal sistema: lo si tiene vicino all'area prima di arrotondarlo. */
static double lim_d(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
#define PUNTATORE_MARGINE 4096.0

/* ------------------------------------------------------------------ */
/* Misura del flusso                                                   */
/* ------------------------------------------------------------------ */

/* Pixel grezzi -> misura del flusso: se il lato maggiore supera il tetto si riducono entrambi i lati
 * in proporzione (ultima risorsa: la densita' qui non si tocca), poi multipli di 8 in [64, tetto]. */
static void riduci_flusso(double rw, double rh, int tetto, int *w, int *h)
{
    int cap = (tetto / 8) * 8;
    if (cap < PH_FIN_FLUSSO_MIN) cap = PH_FIN_FLUSSO_MIN;
    double m = rw > rh ? rw : rh;
    if (m > cap) {
        rw = rw * cap / m;
        rh = rh * cap / m;
    }
    *w = limita(arrotonda(rw / 8.0) * 8, PH_FIN_FLUSSO_MIN, cap);
    *h = limita(arrotonda(rh / 8.0) * 8, PH_FIN_FLUSSO_MIN, cap);
}

int ph_fin_calcola_flusso(double larg_fis, double alt_fis, double densita_app, double densita_shell,
                          int tetto, int *w, int *h, PhErrore *e)
{
    if (!w || !h) return ph_errore(e, "misura del flusso: manca dove scrivere il risultato");
    if (!(larg_fis > 0) || !(alt_fis > 0) || !(densita_app >= PH_FIN_DENSITA_MIN) ||
        !(densita_app <= PH_FIN_DENSITA_MAX) || !(densita_shell > 0) ||
        !isfinite(larg_fis) || !isfinite(alt_fis) || !isfinite(densita_app) || !isfinite(densita_shell))
        return ph_errore(e, "misura del flusso: dimensioni o densita' non valide");
    riduci_flusso(larg_fis * densita_app / densita_shell, alt_fis * densita_app / densita_shell, tetto, w, h);
    return 0;
}

static int crom(const PhFinDesktop *d) { return PH_FIN_TITOLO_H + (d->pie ? PH_FIN_PIE_H : 0); }

/* Caso limite NON gestito: una finestra alta meno di 33 px (area minuscola) ha il titolo che
 * la riempie tutta e il contenuto (forzato a 1 px) esce dal suo rettangolo; ph_fin_trova
 * in quel caso risponde sempre "titolo". Non si corregge: succede solo con aree < 33 px. */
static PhFinRect contenuto_di(const PhFinDesktop *d, const PhFinRect *r)
{
    PhFinRect c = { r->x, r->y + PH_FIN_TITOLO_H, r->w, massimo(1, r->h - crom(d)) };
    return c;
}

/* Densita' effettiva e misura del flusso di una finestra (spec 4.4).
 * Notazione: L = contenuto in pixel LOGICI della shell (c.w x c.h), s = scala, rif = densita'
 * di riferimento (160), dapp = densita' del telefono, cap = tetto (multiplo di 8), dp = px * 160 / densita'.
 *
 * Densita' FISSA (impostata dall'utente): D = dapp; flusso = L * D / rif (la scala si
 * semplifica), ridotto in proporzione se supera il tetto. E' la regola della versione 1.
 *
 * Densita' AUTOMATICA (predefinita), UNA SOLA regola per ogni finestra di app (piccola, grande, solo-verticale), come in DeX (1 dp = 1 pixel logico, 160 dpi a scala 1):
 *  - flusso = pixel FISICI del contenuto (L*s, 1:1, mai ingrandito), multipli di 8, ridotto in proporzione
 *    solo oltre il tetto;
 *  - k = pixel di flusso per pixel logico, senza l'arrotondamento a 8 (che lo gonfierebbe di poco e farebbe
 *    saltare lo scalino): s, ridotto in proporzione se si e' superato il tetto;
 *  - D = ceil20(max(50, rif*k)): cosi' un dp vale (quasi) un pixel logico e il telefono vede L dp;
 *  - D non supera mai la densita' del telefono. Se il telefono ha una densita' piu' bassa di rif*k (scale molto
 *    alte) "1 dp >= 1 pixel logico" e "mai sopra la densita' del telefono" si contraddicono: vince la seconda
 *    (l'app appare un po' piu' piccola).
 * Riproporzione (telefono noto): f = min(1, w/Wt), con Wt larghezza dello schermo del telefono in punti; per f < 1 la
 *    densita' e' floor(flusso_w*160/Wt) (per DIFETTO, cosi' i punti troncati sono sempre >= Wt); per f = 1 vale quanto sopra,
 *    ma mai oltre lo stesso limite (continuo a w = Wt, anche alle scale dove 160*scala non e' multiplo di 20).
 *    Lo scalino di 20 dpi resta solo con f = 1: serve a non cambiare densita' a ogni pixel quando k varia (oltre il tetto);
 *    con f < 1 la densita' varia a ogni pixel di larghezza ma si applica solo a movimento assestato insieme alla misura,
 *    e uno scalino romperebbe la proporzione (punti visti diversi da Wt).
 * Perche' a scalini di 20 dpi: ogni cambio di densita' fa ridisporre l'app sul telefono; senza scalini un pixel
 * di ridimensionamento la cambierebbe a ogni passo (con k = s la densita' cambia solo cambiando scala o oltre il
 * tetto). Densita' e misura cambiano solo insieme, a movimento assestato (stessa regola dei 300 ms).
 * Perche' 1:1 e non L*dapp/rif: sotto il tetto i dp visti sarebbero gli stessi, ma si disegnerebbero (dapp/rif)
 * volte piu' pixel per lato, poi rimpiccioliti dal PC: lavoro inutile per il telefono.
 * Lo specchio (4.7) non ha ne' misura ne' densita'. */
static double passo_su(double v) { return ceil(v / PH_FIN_DENS_PASSO) * PH_FIN_DENS_PASSO; }

/* Contenuto "utile": in modalita' automatica il flusso e' 1:1 coi pixel dello schermo, quindi il contenuto in
 * pixel fisici si porta a un multiplo di 8 (tolleranza di mezzo pixel per le scale frazionarie); il resto, fino
 * a 7 px fisici, e' un margine a destra e in basso. */
static void utile_di(const PhFinDesktop *d, const Fin *f, int *uw, int *uh)
{
    PhFinRect c = contenuto_di(d, &f->rect);
    *uw = massimo(8, 8 * (int)floor((c.w * d->scala + 0.5) / 8.0));
    *uh = massimo(8, 8 * (int)floor((c.h * d->scala + 0.5) / 8.0));
}

static bool lato_tablet(const PhFinDesktop *d, const Fin *f)
{
    if (f->spec.specchio) return false;
    PhFinRect c = contenuto_di(d, &f->rect);
    return minimo(c.w, c.h) >= PH_FIN_TABLET_LATO;
}

/* Larghezza dello schermo del telefono in punti, in verticale (lato corto); 0 se non nota. */
static double larghezza_telefono(const PhFinDesktop *d)
{
    if (!d->tel_noto) return 0.0;
    double pw = d->tel_w * 160.0 / d->tel_dens, ph = d->tel_h * 160.0 / d->tel_dens;
    return pw < ph ? pw : ph;
}

static void misura_di_ex(const PhFinDesktop *d, const Fin *f, int *w, int *h, double *dens, double *fatt)
{
    if (fatt) *fatt = 1.0;
    if (f->spec.specchio) { /* la misura la decide il telefono (4.7): non si calcola */
        *w = *h = 0;
        *dens = 0;
        return;
    }
    PhFinRect c = contenuto_di(d, &f->rect);
    if (f->compat) {
        /* COMPATIBILITA': densita' = quella VERA del telefono (se non nota: densita_app di ph_fin_imposta_qualita), qualunque
         * sia la regola globale. Il flusso e' (punti / f) * D / 160, con f = min(1, w / Wt): l'app non vede mai meno di Wt punti
         * di larghezza e per ottenerlo si ALLARGA il flusso, non si abbassa la densita'. Oltre il tetto si riducono i pixel in
         * proporzione e la densita' RESTA D. Immagine non 1:1 (utile = 0): la riduce la GPU. */
        double Dc = d->tel_noto ? d->tel_dens : d->dapp;
        double Wt = larghezza_telefono(d), fc = 1.0;
        if (Wt > 0 && c.w < Wt) fc = c.w / Wt;
        riduci_flusso(c.w / fc * Dc / 160.0, c.h / fc * Dc / 160.0, d->tetto, w, h);
        *dens = Dc;
        if (fatt) *fatt = fc;
        return;
    }
    double pw = c.w * d->scala, ph = c.h * d->scala; /* pixel fisici */
    double dsh = d->drif * d->scala;                 /* densita' della shell */
    int cap = massimo(PH_FIN_FLUSSO_MIN, (d->tetto / 8) * 8);
    double D = d->dapp;
    if (!d->automatica) {
        riduci_flusso(pw * D / dsh, ph * D / dsh, d->tetto, w, h);
    } else {
        int uw, uh;
        utile_di(d, f, &uw, &uh);
        riduci_flusso(uw, uh, d->tetto, w, h); /* sotto il tetto: esattamente il contenuto utile */
        double m = uw > uh ? uw : uh;
        double k = m > cap ? d->scala * cap / m : d->scala;
        /* Fattore di riproporzione f = min(1, w / Wt): un'app non vede mai meno punti di larghezza di quelli del telefono (Wt).
         * Con f = 1 la densita' e' a scalini di 20 dpi (cambia solo con la scala o oltre il tetto, dove k varia con la misura);
         * con f < 1 e' l'intero piu' vicino a rif*k*f, senza scalino che romperebbe la proporzione: cambia a ogni pixel di
         * larghezza ma solo a movimento assestato, insieme alla misura. Oltre il tetto k e' gia' ridotto e i punti visti
         * w/f restano comunque >= Wt. */
        double Wt = larghezza_telefono(d), fa = 1.0;
        if (Wt > 0 && uw / d->scala < Wt) fa = (uw / d->scala) / Wt;
        if (fatt) *fatt = fa;
        double Dscalino = passo_su(fmax(PH_FIN_DENSITA_MIN, d->drif * k - 1e-6));
        if (Wt > 0) {
            /* Densita' per DIFETTO: i punti visti (flusso * 160 / densita', che Android tronca) sono sempre >= Wt. Si parte dal
             * flusso vero (non da w*k) cosi' vale anche oltre il tetto, dove il flusso e' arrotondato a 8. Con f = 1 lo
             * scalino di 20 non puo' superare questo limite (alle scale dove 160*scala non e' multiplo di 20 farebbe salire
             * la densita' sopra 160*k e scendere i punti sotto Wt); a w = Wt i due coincidono: continuo. */
            double Dmax = floor(*w * 160.0 / Wt + 1e-9);
            D = fa < 1.0 ? Dmax : fmin(Dscalino, Dmax);
        } else {
            D = Dscalino;
        }
        if (D < PH_FIN_DENSITA_MIN) D = PH_FIN_DENSITA_MIN;
        if (D > d->dapp) D = d->dapp;
    }
    *dens = D;
}

static void misura_di(const PhFinDesktop *d, const Fin *f, int *w, int *h, double *dens)
{
    misura_di_ex(d, f, w, h, dens, NULL);
}

static void flusso_di(const PhFinDesktop *d, const Fin *f, int *w, int *h)
{
    double dens;
    misura_di(d, f, w, h, &dens);
}

/* Osserva la misura del flusso; se cambia riparte l'attesa dell'assestamento. */
static void aggiorna_flusso(PhFinDesktop *d, uint64_t t)
{
    for (size_t i = 0; i < d->n; i++) {
        Fin *f = &d->v[i];
        if (f->min || f->spec.specchio) continue;
        int w, h;
        double dens;
        misura_di(d, f, &w, &h, &dens);
        int di = arrotonda(dens);
        if (w != f->bers_w || h != f->bers_h || di != f->bers_d) {
            f->bers_w = w;
            f->bers_h = h;
            f->bers_d = di;
            f->t_cambio = t;
        }
    }
}

int ph_fin_misura_flusso(const PhFinDesktop *d, unsigned id, int *w, int *h, PhErrore *e)
{
    if (!d || !w || !h) return ph_errore(e, "misura del flusso: argomenti mancanti");
    for (size_t i = 0; i < d->n; i++)
        if (d->v[i].id == id) {
            if (d->v[i].spec.specchio)
                return ph_errore(e, "misura del flusso: la finestra %u e' lo specchio, la misura la decide il telefono", id);
            flusso_di(d, &d->v[i], w, h);
            return 0;
        }
    return ph_errore(e, "misura del flusso: la finestra %u non esiste", id);
}

int ph_fin_misura_flusso_densita(const PhFinDesktop *d, unsigned id, int *w, int *h, int *densita, PhErrore *e)
{
    if (!d || !w || !h || !densita) return ph_errore(e, "misura del flusso: argomenti mancanti");
    for (size_t i = 0; i < d->n; i++)
        if (d->v[i].id == id) {
            if (d->v[i].spec.specchio)
                return ph_errore(e, "misura del flusso: la finestra %u e' lo specchio, la misura la decide il telefono", id);
            double dens;
            misura_di(d, &d->v[i], w, h, &dens);
            *densita = arrotonda(dens);
            return 0;
        }
    return ph_errore(e, "misura del flusso: la finestra %u non esiste", id);
}

static void assesta_tutte(PhFinDesktop *d);

int ph_fin_imposta_densita_automatica(PhFinDesktop *d, bool automatica, uint64_t t_ms, PhErrore *e)
{
    if (!d) return ph_errore(e, "desktop assente");
    d->automatica = automatica;
    assesta_tutte(d);
    aggiorna_flusso(d, t_ms);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Geometria                                                           */
/* ------------------------------------------------------------------ */

/* L'unico posto dove si calcola il rettangolo di un aggancio (anteprima compresa). */
static PhFinRect rect_agg(const PhFinDesktop *d, PhFinAggancio a)
{
    PhFinRect z = { 0, 0, 0, 0 };
    if (a == PH_FIN_AGG_A_SINISTRA) { z.w = massimo(1, d->W / 2); z.h = d->H; }
    else if (a == PH_FIN_AGG_A_DESTRA) { z.x = d->W / 2; z.w = d->W - z.x; z.h = d->H; }
    else if (a == PH_FIN_AGG_IN_ALTO) { z.w = d->W; z.h = d->H; }
    return z;
}

static PhFinRect rect_stato(const PhFinDesktop *d, const Fin *f)
{
    switch (f->stato) {
    case PH_FIN_MASSIMIZZATA:
        return rect_agg(d, PH_FIN_AGG_IN_ALTO);
    case PH_FIN_AGG_SX:
        return rect_agg(d, PH_FIN_AGG_A_SINISTRA);
    case PH_FIN_AGG_DX:
        return rect_agg(d, PH_FIN_AGG_A_DESTRA);
    case PH_FIN_NORMALE:
    default:
        return f->normale;
    }
}

static void applica_stato(const PhFinDesktop *d, Fin *f) { f->rect = rect_stato(d, f); }

/* Misura del rettangolo "normale": mai piu' grande dell'area. Le forme fisse (solo-verticali, specchio)
 * tornano alla loro misura (ridotta in proporzione se non ci sta). */
static void adatta_misura(const PhFinDesktop *d, Fin *f)
{
    PhFinRect *r = &f->normale;
    if (FISSA(f)) {
        double k = 1.0;
        if (f->spec.w > d->W) k = (double)d->W / f->spec.w;
        if (f->spec.h * k > d->H) k = (double)d->H / f->spec.h;
        r->w = massimo(1, (int)(f->spec.w * k));
        r->h = massimo(1, (int)(f->spec.h * k));
    } else {
        r->w = massimo(1, minimo(r->w, d->W));
        r->h = massimo(1, minimo(r->h, d->H));
    }
}

/* Posizione permessa a una finestra che si sposta: in alto y >= 0; in basso resta visibile la barra del titolo sopra il
 * bordo inferiore dell'area (il resto puo' sporgere: finisce sotto la barra della shell); ai lati restano visibili
 * PH_FIN_VISIBILE_MIN px. */
static void limiti_posizione(const PhFinDesktop *d, const PhFinRect *r, int *xlo, int *xhi, int *ylo, int *yhi)
{
    int vis = minimo(PH_FIN_VISIBILE_MIN, r->w);
    *xlo = vis - r->w;
    *xhi = d->W - vis;
    *ylo = 0;
    *yhi = massimo(0, d->H - minimo(PH_FIN_TITOLO_H, r->h));
}

/* Cambio di area (o di forma): riporta la finestra solo quanto basta perche' titolo e 96 px restino visibili. */
static void adatta_normale(const PhFinDesktop *d, Fin *f)
{
    PhFinRect *r = &f->normale;
    adatta_misura(d, f);
    int xlo, xhi, ylo, yhi;
    limiti_posizione(d, r, &xlo, &xhi, &ylo, &yhi);
    r->x = limita(r->x, xlo, xhi);
    r->y = limita(r->y, ylo, yhi);
}

/* La misura attuale diventa quella voluta (apertura, fine ridimensionamento, rotazione, ritorno a ridimensionabile). */
static void memorizza_voluta(Fin *f)
{
    if (FISSA(f)) return;
    f->voluta_w = f->normale.w;
    f->voluta_h = f->normale.h;
}

/* Cio' che si salva per "riapri dove eri": posizione normale e misura VOLUTA (non quella ridotta da un'area stretta). */
static PhFinRect da_ricordare(const Fin *f)
{
    PhFinRect r = f->normale;
    if (!FISSA(f) && f->voluta_w > 0) {
        r.w = f->voluta_w;
        r.h = f->voluta_h;
    }
    return r;
}

/* Apertura: la finestra nuova sta INTERA dentro l'area. */
static void adatta_dentro(const PhFinDesktop *d, Fin *f)
{
    PhFinRect *r = &f->normale;
    adatta_misura(d, f);
    r->x = limita(r->x, 0, massimo(0, d->W - r->w));
    r->y = limita(r->y, 0, massimo(0, d->H - r->h));
}

/* Un intero in [lo, hi] vicino a x (al massimo 4 passi) tale che pos_fisica(v) = (v + extra) * s sia un intero: cosi' l'origine
 * del contenuto cade su un pixel fisico intero. Se non c'e' (scale frazionarie generiche) resta x. */
static int allinea_intero(int x, int extra, double s, int lo, int hi)
{
    for (int passo = 0; passo <= 4; passo++) {
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            int v = x + sgn * passo;
            if (v < lo || v > hi) continue;
            double p = (v + extra) * s;
            if (fabs(p - floor(p + 0.5)) < 1e-6) return v;
            if (passo == 0) break; /* un solo candidato a passo 0 */
        }
    }
    return x;
}

/* "Assestamento": una finestra normale di app in regola automatica porta il contenuto al multiplo di 8 pixel
 * fisici piu' vicino a (larghezza * scala) e (altezza del contenuto * scala): il bersaglio e' univoco, quindi una
 * seconda chiamata non cambia nulla; poi si sceglie l'intero logico che lo avvicina di piu' (scostamento al massimo
 * ~4 px fisici piu' mezzo pixel logico: a scala 1,5 fino a ~6). Cambiano solo w e h (con i limiti del ridimensionamento:
 * bordo destro, barra, 96 px visibili, minimi) e l'origine si allinea a un pixel fisico intero (spostamento di pochi px
 * logici al massimo, solo se la scala lo permette); nessun rientro nell'area: una finestra a meta' fuori resta li'.
 * Si chiama a fine operazione (apertura, fine ridimensionamento, rotazione, cambio di scala o di area, piede, passaggio ad
 * automatica), mai durante un gesto. Le solo-verticali (forma fissa), lo specchio, le ingrandite/agganciate
 * (rettangolo imposto) e quelle in registrazione non cambiano. */
/* Intero logico in [lo, hi] la cui misura fisica e' un multiplo di 8, il piu' vicino a w0*s; se il bersaglio cade fuori
 * dai limiti si scende (o sale) di multipli di 8 finche' sta dentro, cosi' il risultato e' un punto fisso (idempotente).
 * Se nell'intervallo non c'e' alcun multiplo di 8 si limita e basta. */
static int dim8(int w0, double s, int lo, int hi)
{
    int T = 8 * (int)floor(w0 * s / 8.0 + 0.5);
    int v = (int)lround(T / s);
    for (int k = 0; k < 256 && v > hi && T > 8; k++) {
        T -= 8;
        v = (int)lround(T / s);
    }
    for (int k = 0; k < 256 && v < lo; k++) {
        T += 8;
        v = (int)lround(T / s);
    }
    return limita(v, lo, hi);
}

/* Misura predefinita della finestra intera per una finestra di app (spec.w = spec.h = 0): lo schermo del telefono IN PUNTI
 * (px * 160 / densita': 1080x2340 a 450 dpi = 384x832), cioe' 1 punto = 1 pixel logico come in DeX, come contenuto.
 *  - forma di telefono (solo-verticali) e finestra generica: sempre in verticale (lato corto = larghezza), anche se il
 *    telefono e' in orizzontale o e' un tablet;
 *  - specchio dello schermo: la proporzione resta quella dello schermo cosi' com'e';
 *  - se non sta nell'area (con PH_FIN_MARGINE_AREA px per lato) si riduce IN PROPORZIONE, ma la larghezza del contenuto non
 *    scende sotto min(288, area utile); poi i lati del contenuto si assestano sui multipli di 8 pixel fisici;
 *  - telefono non ancora noto: il ripiego (forma di telefono 296x490, specchio uguale, generica 384x442 (contenuto 296x440 e 384x392)). */
static void misura_predefinita(const PhFinDesktop *d, const PhFinSpec *s, int *ow, int *oh)
{
    if (!d->tel_noto) {
        *ow = (s->solo_verticale || s->specchio) ? PH_FIN_TEL_W : PH_FIN_GENERICA_W;
        *oh = (s->solo_verticale || s->specchio) ? PH_FIN_TEL_H : PH_FIN_GENERICA_H;
        return;
    }
    double pw = d->tel_w * 160.0 / d->tel_dens, ph = d->tel_h * 160.0 / d->tel_dens;
    double a = pw, b = ph;
    if (!s->specchio) { /* forma di telefono e generica: sempre verticale */
        a = pw < ph ? pw : ph;
        b = pw < ph ? ph : pw;
    }
    int cr = crom(d);
    double aw = d->W - 2.0 * PH_FIN_MARGINE_AREA, ah = d->H - 2.0 * PH_FIN_MARGINE_AREA - cr;
    double k = 1.0;
    if (aw / a < k) k = aw / a;
    if (ah / b < k) k = ah / b;
    double kmin = fmin(PH_FIN_MIN_W, aw > 1 ? aw : 1) / a;
    if (k < kmin) k = kmin < 1.0 ? kmin : 1.0;
    if (k <= 0) k = 1.0;
    *ow = dim8(arrotonda(a * k), d->scala, 1, massimo(1, d->W));
    *oh = dim8(arrotonda(b * k), d->scala, 1, massimo(1, d->H - cr)) + cr;
}

int ph_fin_imposta_telefono(PhFinDesktop *d, int w_px, int h_px, double densita, uint64_t t_ms, PhErrore *e)
{
    (void)t_ms; /* non cambia le finestre gia' aperte */
    if (!d) return ph_errore(e, "desktop assente");
    if (w_px <= 0 || h_px <= 0 || w_px > 20000 || h_px > 20000 || !(densita >= PH_FIN_DENSITA_MIN) ||
        !(densita <= PH_FIN_DENSITA_MAX))
        return ph_errore(e, "schermo del telefono non valido: %dx%d a %g dpi", w_px, h_px, densita);
    d->tel_noto = true;
    d->tel_w = w_px;
    d->tel_h = h_px;
    d->tel_dens = densita;
    return 0;
}

PhFinSpec ph_fin_spec_predefinita(bool solo_verticale, bool specchio)
{
    PhFinSpec s = { 0, 0, solo_verticale, specchio };
    return s;
}

static void assesta(PhFinDesktop *d, Fin *f)
{
    if (!d->automatica || f->compat || f->spec.specchio || f->spec.solo_verticale || f->rec || f->stato != PH_FIN_NORMALE) return;
    double s = d->scala;
    int cr = crom(d);
    PhFinRect *r = &f->normale;
    /* Prima la posizione (origine del contenuto su un pixel fisico intero), poi la misura con i limiti della posizione
     * definitiva: cosi' l'ordine non conta e una seconda chiamata non cambia nulla. */
    bool dentro_x = r->x >= 0 && r->x + r->w <= d->W;
    int vis = minimo(PH_FIN_VISIBILE_MIN, r->w);
    r->x = allinea_intero(r->x, 0, s, dentro_x ? 0 : vis - r->w, dentro_x ? d->W - r->w : d->W - vis);
    bool dentro_y0 = r->y >= 0 && r->y + r->h <= d->H;
    r->y = allinea_intero(r->y, PH_FIN_TITOLO_H, s, 0, dentro_y0 ? massimo(0, d->H - r->h) : massimo(0, d->H - minimo(PH_FIN_TITOLO_H, r->h)));
    /* Una finestra a meta' fuori non si accorcia: basta che stia nella larghezza dell'area; una finestra tutta dentro
     * (bordo destro compreso) resta tutta dentro. */
    bool dentro = r->x >= 0 && r->x + r->w <= d->W;
    int maxw = massimo(1, dentro ? d->W - r->x : d->W);
    int minw = massimo(minimo(PH_FIN_MIN_W, maxw), minimo(PH_FIN_VISIBILE_MIN - r->x, maxw));
    r->w = dim8(r->w, s, minw, maxw);
    /* Una finestra che sporge in basso mantiene misura e posizione; una tutta dentro resta tutta dentro. */
    bool dentro_y = r->y >= 0 && r->y + r->h <= d->H;
    int maxh = massimo(1, dentro_y ? d->H - r->y : d->H);
    int hi = maxh - cr;
    if (hi >= 1) {
        int lo = massimo(1, minimo(PH_FIN_MIN_H, maxh) - cr);
        int ch = r->h - cr;
        r->h = dim8(ch, s, lo, hi) + cr;
    }
}

static void assesta_tutte(PhFinDesktop *d)
{
    for (size_t i = 0; i < d->n; i++) {
        assesta(d, &d->v[i]);
        applica_stato(d, &d->v[i]);
    }
}

static int indice(const PhFinDesktop *d, unsigned id)
{
    if (id == 0) return -1;
    for (size_t i = 0; i < d->n; i++)
        if (d->v[i].id == id) return (int)i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Memoria "riapri dove eri"                                           */
/* ------------------------------------------------------------------ */

static void mem_scrivi(PhFinDesktop *d, const char *app, PhFinRect r)
{
    size_t i;
    for (i = 0; i < d->nmem; i++)
        if (strcmp(d->mem[i].app, app) == 0) break;
    if (i == d->nmem) {
        if (d->nmem == PH_FIN_MEM_MAX) { /* pieno: si dimentica il piu' vecchio */
            memmove(&d->mem[0], &d->mem[1], (PH_FIN_MEM_MAX - 1) * sizeof d->mem[0]);
            i = PH_FIN_MEM_MAX - 1;
        } else {
            d->nmem++;
        }
        memset(&d->mem[i], 0, sizeof d->mem[i]);
        snprintf(d->mem[i].app, sizeof d->mem[i].app, "%s", app);
    }
    d->mem[i].x = r.x; d->mem[i].y = r.y; d->mem[i].w = r.w; d->mem[i].h = r.h;
}

static const PhFinRicordo *mem_cerca(const PhFinDesktop *d, const char *app)
{
    for (size_t i = 0; i < d->nmem; i++)
        if (strcmp(d->mem[i].app, app) == 0) return &d->mem[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Desktop                                                             */
/* ------------------------------------------------------------------ */

PhFinSpec ph_fin_spec_mockup(const char *app)
{
    static const struct { const char *app; int w, h; bool vert; } t[] = {
        { "msg", 296, 470, true },  { "web", 540, 340, false }, { "note", 400, 290, false },
        { "calc", 260, 390, true }, { "photo", 460, 330, false }, { "map", 480, 330, false },
        { "music", 300, 430, true }, { "set", 360, 400, false },  { "phone", 280, 500, true },
    };
    if (app)
        for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
            if (strcmp(app, t[i].app) == 0) {
                PhFinSpec s = { t[i].w, t[i].h, t[i].vert, strcmp(app, "phone") == 0 };
                return s;
            }
    PhFinSpec s = { PH_FIN_GENERICA_W, PH_FIN_GENERICA_H, false, false };
    return s;
}

static int area_da_host(int larg, int alt, double scala, int *W, int *H, PhErrore *e)
{
    if (larg <= 0 || alt <= 0 || !(scala >= 0.1) || !isfinite(scala) || scala > 64.0)
        return ph_errore(e, "desktop non valido: %dx%d, scala %g (ammessa 0,1 - 64)", larg, alt, scala);
    double lw = larg / scala, lh = alt / scala;
    if (lw > 32768.0 || lh > 32768.0)
        return ph_errore(e, "desktop troppo grande: %.0fx%.0f unita' (massimo 32768 per lato)", lw, lh);
    *W = massimo(1, arrotonda(lw));
    *H = massimo(1, arrotonda(lh) - PH_FIN_BARRA_H);
    return 0;
}

PhFinDesktop *ph_fin_crea(int larg, int alt, double scala, PhErrore *e)
{
    int W = 1, H = 1; /* area_da_host li assegna sempre quando riesce */
    if (area_da_host(larg, alt, scala, &W, &H, e) != 0) return NULL;
    PhFinDesktop *d = calloc(1, sizeof *d);
    if (!d) {
        ph_errore(e, "desktop delle finestre: memoria esaurita");
        return NULL;
    }
    d->W = W;
    d->H = H;
    d->scala = scala;
    d->dapp = PH_FIN_DENSITA_RIF;
    d->drif = PH_FIN_DENSITA_RIF;
    d->tetto = PH_FIN_FLUSSO_TETTO;
    d->prossimo = 1;
    d->automatica = true;
    return d;
}

void ph_fin_libera(PhFinDesktop *d) { free(d); }

PhFinRect ph_fin_area(const PhFinDesktop *d)
{
    PhFinRect r = { 0, 0, d ? d->W : 0, d ? d->H : 0 };
    return r;
}

double ph_fin_scala(const PhFinDesktop *d) { return d ? d->scala : 1.0; }

int ph_fin_imposta_host(PhFinDesktop *d, int larg, int alt, double scala, uint64_t t_ms, PhErrore *e)
{
    int W = 1, H = 1; /* area_da_host li assegna sempre quando riesce */
    if (!d) return ph_errore(e, "desktop assente");
    if (area_da_host(larg, alt, scala, &W, &H, e) != 0) return -1;
    int vecchia_w = d->W, vecchia_h = d->H;
    d->W = W;
    d->H = H;
    d->scala = scala;
    for (size_t i = 0; i < d->n; i++) {
        Fin *f = &d->v[i];
        /* Chi era tutto dentro l'area resta tutto dentro (il pulsante Chiudi non finisce fuori); chi l'utente aveva messo a
         * sporgere si sposta solo quanto basta perche' restino il titolo e i 96 px. */
        const PhFinRect *n = &f->normale;
        bool era_dentro = n->x >= 0 && n->y >= 0 && n->x + n->w <= vecchia_w && n->y + n->h <= vecchia_h;
        /* La misura torna a quella voluta (ridotta solo quanto serve per l'area): una finestra rimpicciolita da un'area stretta
         * si riallarga con l'area. Le forme fisse tornano alla loro spec; le finestre in registrazione non cambiano misura. */
        if (!FISSA(f) && !f->rec && f->voluta_w > 0) {
            f->normale.w = f->voluta_w;
            f->normale.h = f->voluta_h;
        }
        if (era_dentro) adatta_dentro(d, f);
        else adatta_normale(d, f);
        assesta(d, f);
        applica_stato(d, f);
    }
    /* Un gesto in corso: l'originale puo' non stare piu' nell'area; i limiti
     * si riapplicano al prossimo movimento. */
    aggiorna_flusso(d, t_ms);
    return 0;
}

int ph_fin_imposta_qualita(PhFinDesktop *d, double densita_app, double densita_rif, int tetto,
                           uint64_t t_ms, PhErrore *e)
{
    if (!d) return ph_errore(e, "desktop assente");
    if (!(densita_app >= PH_FIN_DENSITA_MIN && densita_app <= PH_FIN_DENSITA_MAX) ||
        !(densita_rif >= PH_FIN_DENSITA_MIN && densita_rif <= PH_FIN_DENSITA_MAX) || tetto < PH_FIN_FLUSSO_MIN)
        return ph_errore(e, "qualita' del flusso non valida: densita' %g, riferimento %g, tetto %d",
                         densita_app, densita_rif, tetto);
    d->dapp = densita_app;
    d->drif = densita_rif;
    d->tetto = (tetto / 8) * 8;
    aggiorna_flusso(d, t_ms);
    return 0;
}

int ph_fin_imposta_pie(PhFinDesktop *d, bool attivo, uint64_t t_ms, PhErrore *e)
{
    if (!d) return ph_errore(e, "desktop assente");
    d->pie = attivo;
    assesta_tutte(d);
    aggiorna_flusso(d, t_ms);
    return 0;
}

void ph_fin_riapri_dove_eri(PhFinDesktop *d, bool attivo)
{
    if (d) d->riapri = attivo;
}

/* ------------------------------------------------------------------ */
/* Ciclo di vita                                                       */
/* ------------------------------------------------------------------ */

static void in_cima(PhFinDesktop *d, size_t i)
{
    if (i + 1 < d->n) {
        Fin tmp = d->v[i];
        memmove(&d->v[i], &d->v[i + 1], (d->n - i - 1) * sizeof d->v[0]);
        d->v[d->n - 1] = tmp;
    }
}

unsigned ph_fin_apri(PhFinDesktop *d, const char *app, const char *titolo, const PhFinSpec *spec,
                     uint64_t t_ms, bool *nuova, PhErrore *e)
{
    if (nuova) *nuova = false;
    if (!d || !app || !app[0] || strlen(app) >= PH_FIN_APP_MAX) {
        ph_errore(e, "apertura finestra: nome dell'app mancante o troppo lungo");
        return 0;
    }
    int ix = -1;
    for (size_t i = 0; i < d->n; i++)
        if (strcmp(d->v[i].app, app) == 0) { ix = (int)i; break; }
    if (ix >= 0) { /* MOCKUP: app gia' aperta -> ripristina e porta davanti */
        Fin *f = &d->v[ix];
        f->min = false;
        unsigned id = f->id;
        in_cima(d, (size_t)ix);
        d->attiva = id;
        aggiorna_flusso(d, t_ms);
        return id;
    }
    if (d->n >= PH_FIN_MAX) {
        ph_errore(e, "apertura finestra: troppe finestre aperte (%d)", PH_FIN_MAX);
        return 0;
    }
    PhFinSpec s = spec ? *spec : ph_fin_spec_predefinita(false, false);
    if (s.w == 0 && s.h == 0) misura_predefinita(d, &s, &s.w, &s.h);
    if (s.w <= 0 || s.h <= 0) {
        ph_errore(e, "apertura finestra: misura iniziale non valida %dx%d", s.w, s.h);
        return 0;
    }
    Fin *f = &d->v[d->n];
    memset(f, 0, sizeof *f);
    f->id = d->prossimo++;
    snprintf(f->app, sizeof f->app, "%s", app);
    snprintf(f->titolo, sizeof f->titolo, "%s", titolo ? titolo : app);
    f->spec = s;
    f->stato = PH_FIN_NORMALE;
    f->prima = PH_FIN_NORMALE;

    int w = s.w, h = s.h;
    if (!s.solo_verticale && !s.specchio) {
        w = massimo(w, PH_FIN_MIN_W);
        h = massimo(h, PH_FIN_MIN_H);
    }
    int x, y;
    if (strcmp(app, "phone") == 0) { /* MOCKUP: lo specchio dello schermo parte in 640,20 */
        x = 640; y = 20;
    } else {
        int n = 0;
        for (size_t i = 0; i < d->n; i++)
            if (strcmp(d->v[i].app, "phone") != 0) n++;
        n %= 6;
        x = 70 + n * 40;
        y = 30 + n * 30;
    }
    const PhFinRicordo *m = d->riapri ? mem_cerca(d, app) : NULL;
    if (m && m->w > 0 && m->h > 0) {
        x = m->x; y = m->y;
        if (!s.solo_verticale && !s.specchio) { w = massimo(m->w, PH_FIN_MIN_W); h = massimo(m->h, PH_FIN_MIN_H); } /* i minimi valgono anche per un ricordo importato */
    }
    f->normale.x = x; f->normale.y = y; f->normale.w = w; f->normale.h = h;
    /* La misura voluta e' quella CHIESTA (spec o ricordo, con i minimi), prima di qualunque riduzione per l'area. */
    f->voluta_w = w;
    f->voluta_h = h;
    adatta_dentro(d, f);
    assesta(d, f);
    applica_stato(d, f);
    {
        double dens;
        misura_di(d, f, &f->bers_w, &f->bers_h, &dens);
        f->bers_d = arrotonda(dens);
    }
    f->rich_w = f->bers_w;
    f->rich_h = f->bers_h;
    f->rich_d = f->bers_d;
    f->t_cambio = t_ms;
    d->n++;
    d->attiva = f->id; /* in cima: e' l'ultima */
    if (nuova) *nuova = true;
    return f->id;
}

static void ferma_gesto_se(PhFinDesktop *d, unsigned id)
{
    if (d->g_att && d->g_id == id) {
        d->g_att = false;
        d->g_agg = PH_FIN_AGG_NESSUNO;
    }
}

int ph_fin_chiudi(PhFinDesktop *d, unsigned id, uint64_t t_ms, PhErrore *e)
{
    int i = d ? indice(d, id) : -1;
    if (i < 0) return ph_errore(e, "chiusura: la finestra %u non esiste", id);
    mem_scrivi(d, d->v[i].app, da_ricordare(&d->v[i]));
    ferma_gesto_se(d, id);
    memmove(&d->v[i], &d->v[i + 1], (d->n - (size_t)i - 1) * sizeof d->v[0]);
    d->n--;
    if (d->attiva == id) d->attiva = 0;
    aggiorna_flusso(d, t_ms);
    return 0;
}

int ph_fin_minimizza(PhFinDesktop *d, unsigned id, uint64_t t_ms, PhErrore *e)
{
    int i = d ? indice(d, id) : -1;
    if (i < 0) return ph_errore(e, "minimizza: la finestra %u non esiste", id);
    if (d->v[i].min) return ph_errore(e, "minimizza: la finestra %u e' gia' minimizzata", id);
    d->v[i].min = true;
    ferma_gesto_se(d, id);
    if (d->attiva == id) d->attiva = 0;
    aggiorna_flusso(d, t_ms);
    return 0;
}

int ph_fin_ripristina(PhFinDesktop *d, unsigned id, uint64_t t_ms, PhErrore *e)
{
    int i = d ? indice(d, id) : -1;
    if (i < 0) return ph_errore(e, "ripristina: la finestra %u non esiste", id);
    if (!d->v[i].min) return ph_errore(e, "ripristina: la finestra %u non e' minimizzata", id);
    d->v[i].min = false;
    in_cima(d, (size_t)i);
    d->attiva = id;
    aggiorna_flusso(d, t_ms);
    return 0;
}

int ph_fin_massimizza_ripristina(PhFinDesktop *d, unsigned id, uint64_t t_ms, PhErrore *e)
{
    int i = d ? indice(d, id) : -1;
    if (i < 0) return ph_errore(e, "massimizza: la finestra %u non esiste", id);
    Fin *f = &d->v[i];
    if (FISSA(f)) /* decisione del committente: forma fissa (solo verticale o specchio); nulla cambia */
        return ph_errore(e, "massimizza: la finestra %u ha forma fissa (solo verticale o specchio), non si ingrandisce", id);
    if (f->min) return ph_errore(e, "massimizza: la finestra %u e' minimizzata", id);
    if (f->rec) return ph_errore(e, "massimizza: la finestra %u sta registrando, la misura non cambia", id);
    if (f->stato == PH_FIN_MASSIMIZZATA) {
        f->stato = f->prima;
    } else {
        f->prima = f->stato;
        f->stato = PH_FIN_MASSIMIZZATA;
    }
    ferma_gesto_se(d, id);
    applica_stato(d, f);
    aggiorna_flusso(d, t_ms);
    return 0;
}

int ph_fin_imposta_registrazione(PhFinDesktop *d, unsigned id, bool registra, uint64_t t_ms, PhErrore *e)
{
    int i = d ? indice(d, id) : -1;
    if (i < 0) return ph_errore(e, "registrazione: la finestra %u non esiste", id);
    Fin *f = &d->v[i];
    if (f->rec && !registra) f->t_cambio = t_ms; /* i 300 ms dell'assestamento ripartono dalla fine */
    if (registra && d->g_att && d->g_id == id && d->g_tipo == PH_FIN_GESTO_RIDIMENSIONA)
        d->g_att = false; /* registrare ferma un ridimensionamento in corso: la misura resta com'e' */
    f->rec = registra;
    return 0;
}

int ph_fin_imposta_compatibilita(PhFinDesktop *d, unsigned id, bool attiva, uint64_t t_ms, PhErrore *e)
{
    int i = d ? indice(d, id) : -1;
    if (i < 0) return ph_errore(e, "compatibilita': la finestra %u non esiste", id);
    Fin *f = &d->v[i];
    if (f->spec.specchio) return ph_errore(e, "compatibilita': la finestra %u e' lo specchio, non ha compatibilita'", id);
    if (f->rec) return ph_errore(e, "compatibilita': la finestra %u sta registrando, la misura non cambia", id);
    if (f->compat == attiva) return 0;
    f->compat = attiva;
    if (!attiva) { /* torna alla regola globale: assestamento a multipli di 8 pixel fisici come le altre */
        assesta(d, f);
        applica_stato(d, f);
    }
    int w0 = f->bers_w, h0 = f->bers_h, d0 = f->bers_d;
    aggiorna_flusso(d, t_ms);
    /* se misura o densita' cambiano la richiesta parte alla prossima ph_fin_avanza, senza l'attesa di assestamento; da
     * minimizzata il cambio si vede al ripristino (aggiorna_flusso salta le minimizzate) ma la richiesta e' comunque immediata. */
    if (f->min || f->bers_w != w0 || f->bers_h != h0 || f->bers_d != d0) f->subito = true;
    return 0;
}

bool ph_fin_compatibilita(const PhFinDesktop *d, unsigned id)
{
    int i = d ? indice(d, id) : -1;
    return i >= 0 && !d->v[i].spec.specchio && d->v[i].compat;
}

int ph_fin_ruota(PhFinDesktop *d, unsigned id, uint64_t t_ms, PhErrore *e)
{
    int i = d ? indice(d, id) : -1;
    if (i < 0) return ph_errore(e, "ruota: la finestra %u non esiste", id);
    Fin *f = &d->v[i];
    if (f->min) return ph_errore(e, "ruota: la finestra %u e' minimizzata", id);
    if (f->stato != PH_FIN_NORMALE) return ph_errore(e, "ruota: la finestra %u e' massimizzata o agganciata", id);
    if (f->rec) return ph_errore(e, "ruota: la finestra %u sta registrando", id);
    if (f->spec.specchio) return ph_errore(e, "ruota: la finestra %u e' lo specchio, la misura la decide il telefono", id);
    if (f->spec.solo_verticale) return ph_errore(e, "ruota: la finestra %u e' solo verticale, misura fissa", id); /* lo specchio e' gia' escluso sopra */
    PhFinRect c = contenuto_di(d, &f->rect);
    /* si scambiano larghezza e altezza del CONTENUTO; titolo e piede restano. */
    /* dopo lo scambio valgono i minimi (altrimenti i pulsanti del titolo si sovrappongono),
     * sempre entro l'area. */
    f->normale.w = limita(c.h, minimo(PH_FIN_MIN_W, d->W), d->W);
    f->normale.h = limita(c.w + crom(d), minimo(PH_FIN_MIN_H, d->H), d->H);
    adatta_dentro(d, f); /* non e' un movimento dell'utente: la finestra rientra tutta nell'area */
    assesta(d, f);
    applica_stato(d, f);
    memorizza_voluta(f);
    ferma_gesto_se(d, id);
    aggiorna_flusso(d, t_ms);
    return 0;
}

int ph_fin_imposta_solo_verticale(PhFinDesktop *d, unsigned id, bool solo_verticale, uint64_t t_ms, PhErrore *e)
{
    int i = d ? indice(d, id) : -1;
    if (i < 0) return ph_errore(e, "solo verticale: la finestra %u non esiste", id);
    Fin *f = &d->v[i];
    if (f->spec.specchio)
        return ph_errore(e, "solo verticale: la finestra %u e' lo specchio, la sua forma non cambia", id);
    if (f->spec.solo_verticale == solo_verticale) return 0;
    ferma_gesto_se(d, id);
    f->spec.solo_verticale = solo_verticale;
    if (solo_verticale) { /* torna alla forma di telefono predefinita, come finestra normale */
        PhFinSpec sp = f->spec;
        misura_predefinita(d, &sp, &f->spec.w, &f->spec.h);
        f->stato = PH_FIN_NORMALE;
        f->prima = PH_FIN_NORMALE;
        adatta_dentro(d, f); /* tutta nell'area */
        applica_stato(d, f);
    } else {
        memorizza_voluta(f); /* torna ridimensionabile: la misura attuale e' quella voluta */
    }
    aggiorna_flusso(d, t_ms);
    return 0;
}

int ph_fin_porta_davanti(PhFinDesktop *d, unsigned id, PhErrore *e)
{
    int i = d ? indice(d, id) : -1;
    if (i < 0) return ph_errore(e, "porta davanti: la finestra %u non esiste", id);
    if (d->v[i].min) return ph_errore(e, "porta davanti: la finestra %u e' minimizzata", id);
    in_cima(d, (size_t)i);
    d->attiva = id;
    return 0;
}

int ph_fin_clic_barra(PhFinDesktop *d, unsigned id, uint64_t t_ms, PhFinAzione *azione, PhErrore *e)
{
    int i = d ? indice(d, id) : -1;
    PhFinAzione az;
    if (i < 0) return ph_errore(e, "clic sulla barra: la finestra %u non esiste", id);
    if (d->v[i].min) {
        az = PH_FIN_AZ_RIPRISTINATA;
        if (ph_fin_ripristina(d, id, t_ms, e) != 0) return -1;
    } else if (d->attiva == id) {
        az = PH_FIN_AZ_MINIMIZZATA;
        if (ph_fin_minimizza(d, id, t_ms, e) != 0) return -1;
    } else {
        az = PH_FIN_AZ_DAVANTI;
        if (ph_fin_porta_davanti(d, id, e) != 0) return -1;
    }
    if (azione) *azione = az;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Interrogazioni                                                      */
/* ------------------------------------------------------------------ */

unsigned ph_fin_cerca_app(const PhFinDesktop *d, const char *app)
{
    if (!d || !app) return 0;
    for (size_t i = 0; i < d->n; i++)
        if (strcmp(d->v[i].app, app) == 0) return d->v[i].id;
    return 0;
}

int ph_fin_info(const PhFinDesktop *d, unsigned id, PhFinInfo *out, PhErrore *e)
{
    int i = d ? indice(d, id) : -1;
    if (i < 0 || !out) return ph_errore(e, "info: la finestra %u non esiste", id);
    const Fin *f = &d->v[i];
    memset(out, 0, sizeof *out);
    out->id = f->id;
    memcpy(out->app, f->app, sizeof out->app);
    memcpy(out->titolo, f->titolo, sizeof out->titolo);
    out->rect = f->rect;
    out->normale = f->normale;
    out->stato = f->stato;
    out->minimizzata = f->min;
    out->attiva = d->attiva == f->id;
    out->solo_verticale = f->spec.solo_verticale;
    out->puo_ridimensionare = !FISSA(f) && !f->min && !f->rec && f->stato != PH_FIN_MASSIMIZZATA;
    out->puo_ingrandire = !FISSA(f) && !f->min && !f->rec;
    out->puo_agganciare = !FISSA(f) && !f->min && !f->rec;
    out->specchio = f->spec.specchio;
    out->registrando = f->rec;
    out->video_w = f->video_w;
    out->video_h = f->video_h;
    {
        double dens;
        misura_di_ex(d, f, &out->flusso_w, &out->flusso_h, &dens, &out->fattore);
        out->densita = arrotonda(dens);
        if (!f->spec.specchio && out->densita > 0) { /* punti visti dal telefono: pixel del flusso * 160 / densita' */
            out->punti_w = (int)floor(out->flusso_w * 160.0 / out->densita + 1e-9); /* come Android: troncati */
            out->punti_h = (int)floor(out->flusso_h * 160.0 / out->densita + 1e-9);
        }
    }
    out->tablet = lato_tablet(d, f);
    if (d->automatica && !f->spec.specchio && !f->compat) utile_di(d, f, &out->utile_w, &out->utile_h);
    return 0;
}

size_t ph_fin_conta(const PhFinDesktop *d) { return d ? d->n : 0; }

size_t ph_fin_ordine(const PhFinDesktop *d, unsigned *ids, size_t max)
{
    if (!d || !ids) return 0;
    size_t n = d->n < max ? d->n : max;
    for (size_t i = 0; i < n; i++) ids[i] = d->v[i].id;
    return n;
}

unsigned ph_fin_attiva(const PhFinDesktop *d) { return d ? d->attiva : 0; }

int ph_fin_contenuto(const PhFinDesktop *d, unsigned id, PhFinRect *out, PhErrore *e)
{
    int i = d ? indice(d, id) : -1;
    if (i < 0 || !out) return ph_errore(e, "contenuto: la finestra %u non esiste", id);
    *out = contenuto_di(d, &d->v[i].rect);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Gesti                                                               */
/* ------------------------------------------------------------------ */

int ph_fin_gesto_inizia(PhFinDesktop *d, unsigned id, PhFinGesto tipo, double px, double py,
                        uint64_t t_ms, PhErrore *e)
{
    (void)t_ms;
    int i = d ? indice(d, id) : -1;
    if (i < 0) return ph_errore(e, "gesto: la finestra %u non esiste", id);
    if (tipo != PH_FIN_GESTO_SPOSTA && tipo != PH_FIN_GESTO_RIDIMENSIONA)
        return ph_errore(e, "gesto: tipo non valido");
    if (!isfinite(px) || !isfinite(py)) return ph_errore(e, "gesto: punto non valido");
    px = lim_d(px, -PUNTATORE_MARGINE, d->W + PUNTATORE_MARGINE);
    py = lim_d(py, -PUNTATORE_MARGINE, d->H + PUNTATORE_MARGINE);
    if (d->g_att) return ph_errore(e, "gesto: un altro gesto e' gia' in corso");
    Fin *f = &d->v[i];
    if (f->min) return ph_errore(e, "gesto: la finestra %u e' minimizzata", id);
    if (f->stato == PH_FIN_MASSIMIZZATA) /* MOCKUP: una massimizzata non si trascina ne' si ridimensiona */
        return ph_errore(e, "gesto: la finestra %u e' massimizzata", id);
    if (tipo == PH_FIN_GESTO_SPOSTA && f->rec && f->stato != PH_FIN_NORMALE)
        return ph_errore(e, "gesto: la finestra %u sta registrando, staccarla ne cambierebbe la misura", id);
    if (tipo == PH_FIN_GESTO_RIDIMENSIONA) {
        if (FISSA(f))
            return ph_errore(e, "gesto: la finestra %u ha forma fissa (solo verticale o specchio), misura fissa", id);
        if (f->rec)
            return ph_errore(e, "gesto: la finestra %u sta registrando, la misura non cambia", id);
        if (f->stato != PH_FIN_NORMALE) { /* da agganciata si ridimensiona partendo dalla misura attuale */
            f->stato = PH_FIN_NORMALE;
            f->normale = f->rect;
        }
    }
    PhFinRect r = f->rect;
    in_cima(d, (size_t)i);
    d->attiva = id;
    d->g_att = true;
    d->g_id = id;
    d->g_tipo = tipo;
    d->g_sx = px;
    d->g_sy = py;
    d->g_orig = r;
    d->g_offx = px - r.x;
    d->g_offy = py - r.y;
    d->g_rx = r.w > 0 ? (px - r.x) / r.w : 0.0;
    d->g_agg = PH_FIN_AGG_NESSUNO;
    return 0;
}

static PhFinAggancio aggancio_per(const PhFinDesktop *d, double px, double py, PhFinRect *r)
{
    PhFinAggancio a = PH_FIN_AGG_NESSUNO;
    if (px < PH_FIN_AGG_LATO) a = PH_FIN_AGG_A_SINISTRA;
    else if (px > d->W - PH_FIN_AGG_LATO) a = PH_FIN_AGG_A_DESTRA;
    else if (py < PH_FIN_AGG_ALTO) a = PH_FIN_AGG_IN_ALTO;
    if (r) *r = rect_agg(d, a);
    return a;
}

int ph_fin_gesto_muovi(PhFinDesktop *d, double px, double py, uint64_t t_ms, PhErrore *e)
{
    if (!d || !d->g_att) return ph_errore(e, "gesto: nessun gesto in corso");
    if (!isfinite(px) || !isfinite(py)) return ph_errore(e, "gesto: punto non valido");
    px = lim_d(px, -PUNTATORE_MARGINE, d->W + PUNTATORE_MARGINE);
    py = lim_d(py, -PUNTATORE_MARGINE, d->H + PUNTATORE_MARGINE);
    int i = indice(d, d->g_id);
    if (i < 0) {
        d->g_att = false;
        return ph_errore(e, "gesto: la finestra non esiste piu'");
    }
    Fin *f = &d->v[i];
    if (d->g_tipo == PH_FIN_GESTO_SPOSTA) {
        bool fermo = px == d->g_sx && py == d->g_sy; /* un semplice clic non muove ne' stacca ne' aggancia */
        if (f->stato != PH_FIN_NORMALE && fermo) {
            d->g_agg = PH_FIN_AGG_NESSUNO;
            aggiorna_flusso(d, t_ms);
            return 0;
        }
        if (f->stato != PH_FIN_NORMALE) { /* stacco: torna la misura di prima */
            f->stato = PH_FIN_NORMALE;
            f->rect.w = f->normale.w;
            f->rect.h = f->normale.h;
            d->g_offx = d->g_rx * f->rect.w;
        }
        int w = f->rect.w, h = f->rect.h;
        int vis = minimo(PH_FIN_VISIBILE_MIN, w);
        f->rect.x = limita(arrotonda(px - d->g_offx), vis - w, d->W - vis);
        f->rect.y = limita(arrotonda(py - d->g_offy), 0, massimo(0, d->H - minimo(PH_FIN_TITOLO_H, h))); /* puo' sporgere in basso */
        f->normale = f->rect;
        d->g_agg = (FISSA(f) || f->rec || fermo) ? PH_FIN_AGG_NESSUNO : aggancio_per(d, px, py, NULL);
    } else {
        int maxw = massimo(1, minimo(d->W, d->W - f->rect.x));
        int maxh = massimo(1, d->H - f->rect.y);
        if (f->rect.y + f->rect.h > d->H) maxh = massimo(maxh, minimo(d->g_orig.h, d->H)); /* a cavallo del bordo inferiore: nessun salto */
        int w = arrotonda(d->g_orig.w + (px - d->g_sx));
        int h = arrotonda(d->g_orig.h + (py - d->g_sy));
        /* una finestra a cavallo del bordo sinistro non puo' restringersi fino a perdere il titolo: dopo il
         * ridimensionamento ne restano visibili almeno PH_FIN_VISIBILE_MIN px. */
        int lo = massimo(minimo(PH_FIN_MIN_W, maxw), minimo(PH_FIN_VISIBILE_MIN - f->rect.x, maxw));
        f->rect.w = limita(w, lo, maxw);
        f->rect.h = limita(h, minimo(PH_FIN_MIN_H, maxh), maxh);
        f->normale = f->rect;
    }
    aggiorna_flusso(d, t_ms);
    return 0;
}

PhFinAggancio ph_fin_gesto_anteprima(const PhFinDesktop *d, PhFinRect *r)
{
    if (r) memset(r, 0, sizeof *r);
    if (!d || !d->g_att || d->g_tipo != PH_FIN_GESTO_SPOSTA || d->g_agg == PH_FIN_AGG_NESSUNO)
        return PH_FIN_AGG_NESSUNO;
    if (r) *r = rect_agg(d, d->g_agg);
    return d->g_agg;
}

int ph_fin_gesto_finisci(PhFinDesktop *d, double px, double py, uint64_t t_ms,
                         PhFinAggancio *agganciata, PhErrore *e)
{
    if (agganciata) *agganciata = PH_FIN_AGG_NESSUNO;
    if (!d || !d->g_att) return ph_errore(e, "gesto: nessun gesto in corso");
    unsigned id = d->g_id;
    if (ph_fin_gesto_muovi(d, px, py, t_ms, e) != 0) return -1;
    PhFinAggancio a = d->g_agg;
    d->g_att = false;
    d->g_agg = PH_FIN_AGG_NESSUNO;
    int i = indice(d, id);
    if (i < 0) return ph_errore(e, "gesto: la finestra non esiste piu'");
    Fin *f = &d->v[i];
    f->t_cambio = t_ms; /* la fine del gesto conta come ultimo cambio: i 300 ms partono da qui */
    if (d->g_tipo == PH_FIN_GESTO_RIDIMENSIONA) { /* a fine ridimensionamento il contenuto si assesta sul multiplo di 8 */
        assesta(d, f);
        applica_stato(d, f);
        memorizza_voluta(f); /* la misura scelta dall'utente diventa la voluta */
    }
    if (a != PH_FIN_AGG_NESSUNO && !FISSA(f) && !f->rec && d->g_tipo == PH_FIN_GESTO_SPOSTA) {
        /* `normale` e' gia' la posizione di fine trascinamento: e' la disposizione di prima. */
        if (a == PH_FIN_AGG_A_SINISTRA) f->stato = PH_FIN_AGG_SX;
        else if (a == PH_FIN_AGG_A_DESTRA) f->stato = PH_FIN_AGG_DX;
        else { f->prima = PH_FIN_NORMALE; f->stato = PH_FIN_MASSIMIZZATA; }
        applica_stato(d, f);
        if (agganciata) *agganciata = a;
    }
    aggiorna_flusso(d, t_ms);
    return 0;
}

void ph_fin_gesto_annulla(PhFinDesktop *d, uint64_t t_ms)
{
    if (!d) return;
    int i = d->g_att ? indice(d, d->g_id) : -1;
    if (i >= 0) {
        d->v[i].t_cambio = t_ms;
        if (d->g_tipo == PH_FIN_GESTO_RIDIMENSIONA && d->v[i].stato == PH_FIN_NORMALE) memorizza_voluta(&d->v[i]); /* la finestra resta com'e' */
    }
    d->g_att = false;
    d->g_agg = PH_FIN_AGG_NESSUNO;
    aggiorna_flusso(d, t_ms);
}

unsigned ph_fin_gesto_finestra(const PhFinDesktop *d) { return d && d->g_att ? d->g_id : 0; }

/* ------------------------------------------------------------------ */
/* Assestato                                                           */
/* ------------------------------------------------------------------ */

size_t ph_fin_avanza(PhFinDesktop *d, uint64_t t_ms, PhFinRichiesta *out, size_t max)
{
    size_t n = 0;
    if (!d || !out) return 0;
    for (size_t i = 0; i < d->n && n < max; i++) {
        Fin *f = &d->v[i];
        if (f->min || f->rec) continue;
        if (d->g_att && d->g_id == f->id) continue; /* durante il gesto nessuna richiesta */
        if (f->bers_w == f->rich_w && f->bers_h == f->rich_h && f->bers_d == f->rich_d) {
            f->subito = false; /* niente di nuovo da chiedere */
            continue;
        }
        if (!f->subito && (t_ms < f->t_cambio || t_ms - f->t_cambio < PH_FIN_ASSESTA_MS)) continue;
        out[n].id = f->id;
        out[n].w = f->bers_w;
        out[n].h = f->bers_h;
        out[n].densita = f->bers_d;
        n++;
        f->rich_w = f->bers_w;
        f->rich_h = f->bers_h;
        f->rich_d = f->bers_d;
        f->subito = false;
    }
    return n;
}

int ph_fin_dichiara_video(PhFinDesktop *d, unsigned id, int video_w, int video_h, PhErrore *e)
{
    int i = d ? indice(d, id) : -1;
    if (i < 0) return ph_errore(e, "video: la finestra %u non esiste", id);
    if (video_w < 0 || video_h < 0 || (video_w == 0) != (video_h == 0))
        return ph_errore(e, "video: misura dichiarata non valida %dx%d", video_w, video_h);
    d->v[i].video_w = video_w;
    d->v[i].video_h = video_h;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Traduzione dell'input                                               */
/* ------------------------------------------------------------------ */

static bool dentro(const PhFinRect *r, double x, double y)
{
    return x >= r->x && x < (double)r->x + r->w && y >= r->y && y < (double)r->y + r->h;
}

static PhFinPulsante pulsante_a(const PhFinRect *r, double x, double y)
{
    static const PhFinPulsante da_destra[] = { PH_FIN_PULS_CHIUDI, PH_FIN_PULS_INGRANDISCI,
                                               PH_FIN_PULS_MINIMIZZA, PH_FIN_PULS_ALTRI,
                                               PH_FIN_PULS_REGISTRA, PH_FIN_PULS_SCREENSHOT };
    double top = r->y + (PH_FIN_TITOLO_H - PH_FIN_PULS) / 2;
    if (y < top || y >= top + PH_FIN_PULS) return PH_FIN_PULS_NESSUNO;
    if (x >= r->x + PH_FIN_PULS_MARG_SX && x < r->x + PH_FIN_PULS_MARG_SX + PH_FIN_PULS) return PH_FIN_PULS_INDIETRO;
    for (int k = 0; k < 6; k++) {
        double dx = r->x + r->w - PH_FIN_PULS_MARG_DX - k * (PH_FIN_PULS + PH_FIN_PULS_DIST); /* bordo destro del pulsante k */
        if (x >= dx - PH_FIN_PULS && x < dx) return da_destra[k];
    }
    return PH_FIN_PULS_NESSUNO;
}

void ph_fin_trova(const PhFinDesktop *d, double x, double y, PhFinColpo *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!d || !isfinite(x) || !isfinite(y)) return;
    /* I punti fuori dall'area (compresa la parte di finestra che sporge sotto la barra della shell) non sono di nessuna finestra. */
    if (x < 0 || y < 0 || x >= d->W || y >= d->H) return;
    for (size_t k = d->n; k-- > 0;) {
        const Fin *f = &d->v[k];
        if (f->min || !dentro(&f->rect, x, y)) continue;
        const PhFinRect *r = &f->rect;
        out->id = f->id;
        bool ridim = !FISSA(f) && f->stato != PH_FIN_MASSIMIZZATA;
        if (ridim && x >= (double)r->x + r->w - PH_FIN_ANGOLO && y >= (double)r->y + r->h - PH_FIN_ANGOLO) {
            out->zona = PH_FIN_ZONA_ANGOLO;
            return;
        }
        if (y < r->y + PH_FIN_TITOLO_H) {
            out->pulsante = pulsante_a(r, x, y);
            out->zona = out->pulsante != PH_FIN_PULS_NESSUNO ? PH_FIN_ZONA_PULSANTE : PH_FIN_ZONA_TITOLO;
            return;
        }
        PhFinRect c = contenuto_di(d, r);
        if (!dentro(&c, x, y)) {
            if (!d->pie) { /* senza piede non resta nulla sotto il contenuto */
                out->id = 0;
                continue;
            }
            out->zona = PH_FIN_ZONA_PIE;
            return;
        }
        out->zona = PH_FIN_ZONA_CONTENUTO;
        out->video_w = f->video_w;
        out->video_h = f->video_h;
        /* In automatica l'immagine e' disegnata solo nel contenuto utile (multiplo di 8 px fisici); il resto e' margine. */
        double dw = c.w, dh = c.h;
        if (d->automatica && !f->spec.specchio && !f->compat) {
            int uw, uh;
            utile_di(d, f, &uw, &uh);
            dw = uw / d->scala;
            dh = uh / d->scala;
        }
        if (f->video_w > 0 && f->video_h > 0 && x - c.x < dw && y - c.y < dh) {
            out->valido = true;
            out->vx = (x - c.x) * f->video_w / dw;
            out->vy = (y - c.y) * f->video_h / dh;
            /* l'arrotondamento in virgola mobile non deve mai uscire dal video */
            if (out->vx > f->video_w - 1e-6) out->vx = f->video_w - 1e-6;
            if (out->vy > f->video_h - 1e-6) out->vy = f->video_h - 1e-6;
            if (out->vx < 0) out->vx = 0;
            if (out->vy < 0) out->vy = 0;
        }
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Riapri dove eri                                                     */
/* ------------------------------------------------------------------ */

size_t ph_fin_esporta(const PhFinDesktop *d, PhFinRicordo *out, size_t max)
{
    size_t n = 0;
    if (!d || !out) return 0;
    for (size_t i = 0; i < d->n && n < max; i++) {
        PhFinRicordo *o = &out[n++];
        memset(o, 0, sizeof *o);
        memcpy(o->app, d->v[i].app, sizeof o->app);
        PhFinRect rr = da_ricordare(&d->v[i]);
        o->x = rr.x; o->y = rr.y;
        o->w = rr.w; o->h = rr.h;
    }
    for (size_t i = 0; i < d->nmem && n < max; i++) {
        bool aperta = false;
        for (size_t k = 0; k < d->n; k++)
            if (strcmp(d->v[k].app, d->mem[i].app) == 0) { aperta = true; break; }
        if (!aperta) out[n++] = d->mem[i];
    }
    return n;
}

size_t ph_fin_importa(PhFinDesktop *d, const PhFinRicordo *in, size_t n)
{
    if (!d) return 0;
    d->nmem = 0;
    if (!in) return 0;
    for (size_t i = 0; i < n && d->nmem < PH_FIN_MEM_MAX; i++) {
        if (memchr(in[i].app, '\0', sizeof in[i].app) == NULL || in[i].app[0] == '\0') continue;
        if (in[i].w <= 0 || in[i].h <= 0) continue;
        PhFinRect r = { in[i].x, in[i].y, in[i].w, in[i].h };
        mem_scrivi(d, in[i].app, r);
    }
    return d->nmem;
}
