/* Logica pura di shell-finestra-app: vedi logica.h. */
#include "logica.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- Tasti -------------------------------------------------------------- */

/* Codici di android.view.KeyEvent. */
enum {
    AK_0 = 7,
    AK_DPAD_UP = 19,
    AK_DPAD_DOWN = 20,
    AK_DPAD_LEFT = 21,
    AK_DPAD_RIGHT = 22,
    AK_A = 29,
    AK_COMMA = 55,
    AK_PERIOD = 56,
    AK_TAB = 61,
    AK_SPACE = 62,
    AK_ENTER = 66,
    AK_DEL = 67, /* "Canc indietro" */
    AK_GRAVE = 68,
    AK_MINUS = 69,
    AK_EQUALS = 70,
    AK_LEFT_BRACKET = 71,
    AK_RIGHT_BRACKET = 72,
    AK_BACKSLASH = 73,
    AK_SEMICOLON = 74,
    AK_APOSTROPHE = 75,
    AK_SLASH = 76,
    AK_PAGE_UP = 92,
    AK_PAGE_DOWN = 93,
    AK_ESCAPE = 111,
    AK_FORWARD_DEL = 112, /* "Canc" */
    AK_MOVE_HOME = 122,
    AK_MOVE_END = 123,
    AK_F1 = 131,
    AK_NUMPAD_0 = 144,
};

#define META_SHIFT_ON 0x1u
#define META_ALT_ON 0x2u
#define META_CTRL_ON 0x1000u

unsigned ph_fa_tasto_android(guint k)
{
    if (k >= GDK_KEY_a && k <= GDK_KEY_z)
        return AK_A + (k - GDK_KEY_a);
    if (k >= GDK_KEY_A && k <= GDK_KEY_Z)
        return AK_A + (k - GDK_KEY_A);
    if (k >= GDK_KEY_0 && k <= GDK_KEY_9)
        return AK_0 + (k - GDK_KEY_0);
    if (k >= GDK_KEY_KP_0 && k <= GDK_KEY_KP_9)
        return AK_NUMPAD_0 + (k - GDK_KEY_KP_0);
    if (k >= GDK_KEY_F1 && k <= GDK_KEY_F12)
        return AK_F1 + (k - GDK_KEY_F1);
    switch (k) {
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_ISO_Enter:
        return AK_ENTER;
    case GDK_KEY_BackSpace: return AK_DEL;
    case GDK_KEY_Tab:
    case GDK_KEY_ISO_Left_Tab:
        return AK_TAB;
    case GDK_KEY_Escape: return AK_ESCAPE;
    case GDK_KEY_Up:
    case GDK_KEY_KP_Up:
        return AK_DPAD_UP;
    case GDK_KEY_Down:
    case GDK_KEY_KP_Down:
        return AK_DPAD_DOWN;
    case GDK_KEY_Left:
    case GDK_KEY_KP_Left:
        return AK_DPAD_LEFT;
    case GDK_KEY_Right:
    case GDK_KEY_KP_Right:
        return AK_DPAD_RIGHT;
    case GDK_KEY_space: return AK_SPACE;
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
        return AK_FORWARD_DEL;
    case GDK_KEY_Home:
    case GDK_KEY_KP_Home:
        return AK_MOVE_HOME;
    case GDK_KEY_End:
    case GDK_KEY_KP_End:
        return AK_MOVE_END;
    case GDK_KEY_Page_Up:
    case GDK_KEY_KP_Page_Up:
        return AK_PAGE_UP;
    case GDK_KEY_Page_Down:
    case GDK_KEY_KP_Page_Down:
        return AK_PAGE_DOWN;
    /* Punteggiatura: serve alle scorciatoie con Ctrl (per esempio Ctrl+-). */
    case GDK_KEY_minus: return AK_MINUS;
    case GDK_KEY_equal: return AK_EQUALS;
    case GDK_KEY_bracketleft: return AK_LEFT_BRACKET;
    case GDK_KEY_bracketright: return AK_RIGHT_BRACKET;
    case GDK_KEY_backslash: return AK_BACKSLASH;
    case GDK_KEY_semicolon: return AK_SEMICOLON;
    case GDK_KEY_apostrophe: return AK_APOSTROPHE;
    case GDK_KEY_slash: return AK_SLASH;
    case GDK_KEY_comma: return AK_COMMA;
    case GDK_KEY_period: return AK_PERIOD;
    case GDK_KEY_grave: return AK_GRAVE;
    default: return 0;
    }
}

unsigned ph_fa_meta_android(GdkModifierType m)
{
    unsigned r = 0;
    if ((m & GDK_SHIFT_MASK) != 0)
        r |= META_SHIFT_ON;
    if ((m & GDK_ALT_MASK) != 0)
        r |= META_ALT_ON;
    if ((m & GDK_CONTROL_MASK) != 0)
        r |= META_CTRL_ON;
    return r;
}

bool ph_fa_tasto_e_testo(guint keyval, GdkModifierType m)
{
    if ((m & (GDK_CONTROL_MASK | GDK_ALT_MASK)) != 0)
        return false;
    gunichar c = gdk_keyval_to_unicode(keyval);
    /* Lo spazio e' un carattere stampabile ma e' anche un tasto (Android lo
     * vuole come codice per i pulsanti); come testo andrebbe due volte. */
    return c != 0 && c != ' ' && !g_unichar_iscntrl(c);
}

unsigned ph_fa_pulsante_tocco(unsigned bottone_gdk)
{
    switch (bottone_gdk) {
    case GDK_BUTTON_PRIMARY:
        return 1;
    case GDK_BUTTON_SECONDARY:
        return 2;
    case GDK_BUTTON_MIDDLE:
        return 4;
    default:
        return 0;
    }
}

PhFaRiservato ph_fa_tasto_riservato(guint keyval, GdkModifierType m)
{
    if (keyval == GDK_KEY_F11)
        return PH_FA_RISERVATO_SCHERMO_INTERO;
    GdkModifierType uso = m & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_ALT_MASK);
    guint minuscolo = gdk_keyval_to_lower(keyval);
    if (uso == GDK_CONTROL_MASK && minuscolo == GDK_KEY_r)
        return PH_FA_RISERVATO_RUOTA;
    if (uso == GDK_CONTROL_MASK && minuscolo == GDK_KEY_w)
        return PH_FA_RISERVATO_CHIUDI_APP;
    if (uso == (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && minuscolo == GDK_KEY_c)
        return PH_FA_RISERVATO_COPIA_SCREENSHOT;
    return PH_FA_RISERVATO_NESSUNO;
}

/* ---- Chiavi corte ------------------------------------------------------- */

struct PhFaChiavi {
    GPtrArray *pacchetti; /* la chiave "a<n>" e' l'indice + 1 */
};

static const char CHIAVE_SPECCHIO[] = "phone";

PhFaChiavi *ph_fa_chiavi_nuove(void)
{
    PhFaChiavi *t = g_new0(PhFaChiavi, 1);
    t->pacchetti = g_ptr_array_new_with_free_func(g_free);
    return t;
}

void ph_fa_chiavi_libera(PhFaChiavi *t)
{
    if (t == NULL)
        return;
    g_ptr_array_free(t->pacchetti, TRUE);
    g_free(t);
}

bool ph_fa_chiave_di(PhFaChiavi *t, const char *pacchetto, char out[PH_FIN_APP_MAX])
{
    out[0] = '\0';
    if (pacchetto == NULL || strlen(pacchetto) >= PH_CFG_LUN_APP)
        return false;
    if (pacchetto[0] == '\0') {
        g_strlcpy(out, CHIAVE_SPECCHIO, PH_FIN_APP_MAX);
        return true;
    }
    for (guint i = 0; i < t->pacchetti->len; i++) {
        if (strcmp(g_ptr_array_index(t->pacchetti, i), pacchetto) == 0) {
            snprintf(out, PH_FIN_APP_MAX, "a%u", i + 1);
            return true;
        }
    }
    g_ptr_array_add(t->pacchetti, g_strdup(pacchetto));
    snprintf(out, PH_FIN_APP_MAX, "a%u", t->pacchetti->len);
    return true;
}

const char *ph_fa_pacchetto_di(const PhFaChiavi *t, const char *chiave)
{
    if (chiave == NULL)
        return NULL;
    if (strcmp(chiave, CHIAVE_SPECCHIO) == 0)
        return "";
    /* Solo "a" + cifre senza zeri davanti: una chiave in qualunque altra forma
     * non e' una nostra (il profilo e' un file e puo' contenere di tutto). */
    if (chiave[0] != 'a' || chiave[1] < '1' || chiave[1] > '9')
        return NULL;
    guint64 n = 0;
    for (const char *p = chiave + 1; *p != '\0'; p++) {
        if (*p < '0' || *p > '9')
            return NULL;
        n = n * 10 + (guint64)(*p - '0');
        if (n > t->pacchetti->len)
            return NULL;
    }
    return g_ptr_array_index(t->pacchetti, (guint)(n - 1));
}

size_t ph_fa_ricordi_da_profilo(PhFaChiavi *t, const PhCfgFinestra *in, size_t n,
                                PhFinRicordo *out, size_t max)
{
    size_t k = 0;
    for (size_t i = 0; i < n && k < max; i++) {
        if (in[i].app[0] == '\0')
            continue;
        PhFinRicordo r = {0};
        if (!ph_fa_chiave_di(t, in[i].app, r.app))
            continue;
        r.x = in[i].x;
        r.y = in[i].y;
        r.w = in[i].larghezza;
        r.h = in[i].altezza;
        out[k++] = r;
    }
    return k;
}

size_t ph_fa_ricordi_a_profilo(const PhFaChiavi *t, const PhFinRicordo *in, size_t n,
                               PhCfgFinestra *out, size_t max)
{
    size_t k = 0;
    for (size_t i = 0; i < n && k < max; i++) {
        const char *pk = ph_fa_pacchetto_di(t, in[i].app);
        if (pk == NULL || pk[0] == '\0')
            continue;
        PhCfgFinestra f = {0};
        g_strlcpy(f.app, pk, sizeof f.app);
        f.x = in[i].x;
        f.y = in[i].y;
        f.larghezza = in[i].w;
        f.altezza = in[i].h;
        out[k++] = f;
    }
    return k;
}

/* ---- Misura iniziale ---------------------------------------------------- */

PhFinSpec ph_fa_scegli_spec(const PhSrvApp *app, bool specchio)
{
    /* La misura la sceglie ph_fin_apri dallo schermo del telefono (w = h = 0). */
    if (specchio)
        return ph_fin_spec_predefinita(false, true);
    return ph_fin_spec_predefinita(app != NULL && app->solo_verticale, false);
}

/* ---- Screenshot e registrazioni ------------------------------------------ */

char *ph_fa_cartella_screenshot(const char *immagini)
{
    return g_build_filename(immagini, "YASDE", NULL);
}

/* ---- Densita' ------------------------------------------------------------ */

bool ph_fa_densita_automatica(int densita_profilo)
{
    return densita_profilo <= 0;
}

/* ---- Pausa -------------------------------------------------------------- */

bool ph_fa_pausa_voluta(bool host_visibile, bool minimizzata)
{
    return !host_visibile || minimizzata;
}

PhFaPasso ph_fa_passo_pausa(bool in_pausa, bool voluta)
{
    if (voluta && !in_pausa)
        return PH_FA_PASSO_PAUSA;
    if (!voluta && in_pausa)
        return PH_FA_PASSO_RIPRENDI;
    return PH_FA_PASSO_NIENTE;
}

/* ---- Geometria del titolo ----------------------------------------------- */

int ph_fa_pulsante_x(int larghezza, PhFinPulsante p)
{
    const int passo = PH_FIN_PULS + PH_FIN_PULS_DIST;
    const int destra = larghezza - PH_FIN_PULS_MARG_DX - PH_FIN_PULS; /* chiudi */
    switch (p) {
    case PH_FIN_PULS_INDIETRO: return PH_FIN_PULS_MARG_SX;
    case PH_FIN_PULS_CHIUDI: return destra;
    case PH_FIN_PULS_INGRANDISCI: return destra - passo;
    case PH_FIN_PULS_MINIMIZZA: return destra - 2 * passo;
    case PH_FIN_PULS_ALTRI: return destra - 3 * passo;
    case PH_FIN_PULS_REGISTRA: return destra - 4 * passo;
    case PH_FIN_PULS_SCREENSHOT: return destra - 5 * passo;
    default: return 0;
    }
}

/* ---- Input -------------------------------------------------------------- */

bool ph_fa_traduci_immagine(double ix, double iy, double iw, double ih, int video_w, int video_h,
                            double x, double y, bool fissa, int *vx, int *vy)
{
    if (!(iw > 0.0) || !(ih > 0.0) || video_w <= 0 || video_h <= 0 || !isfinite(ix) ||
        !isfinite(iy) || !isfinite(iw) || !isfinite(ih) || !isfinite(x) || !isfinite(y))
        return false;
    double lx = x - ix, ly = y - iy;
    if (fissa) {
        lx = CLAMP(lx, 0.0, iw);
        ly = CLAMP(ly, 0.0, ih);
    } else if (lx < 0.0 || ly < 0.0 || lx >= iw || ly >= ih) {
        return false; /* nelle bande */
    }
    int px = (int)(lx * video_w / iw);
    int py = (int)(ly * video_h / ih);
    *vx = MIN(px, video_w - 1);
    *vy = MIN(py, video_h - 1);
    return true;
}

double ph_fa_origine_pixel_intero(double origine, double scostamento, double scala)
{
    if (!(scala > 0.0) || !isfinite(origine) || !isfinite(scostamento) || !isfinite(scala))
        return origine;
    return round((scostamento + origine) * scala) / scala - scostamento;
}

int ph_fa_passi_rotella(double *accumulo, double delta)
{
    if (!isfinite(delta))
        return 0;
    *accumulo += delta * 120.0;
    /* Un tetto: un valore enorme non deve diventare migliaia di scatti. */
    *accumulo = CLAMP(*accumulo, -120.0 * 64, 120.0 * 64);
    double passi = trunc(*accumulo);
    *accumulo -= passi;
    return (int)passi;
}

double ph_fa_scatti_da_delta(bool pixel, double pixel_per_scatto, double delta)
{
    if (!pixel)
        return delta;
    return pixel_per_scatto > 0.0 ? delta / pixel_per_scatto : 0.0;
}

void ph_fa_riga_misura(char *out, size_t capacita, const char *motivo, int logici_w, int logici_h,
                       double scala, int w, int h, int densita, double densita_telefono, bool tablet,
                       bool specchio, double fattore, int punti_w, int punti_h)
{
    if (specchio) {
        snprintf(out, capacita, "%s: contenuto %dx%d logici = %.0fx%.0f fisici, scala %.3g, misura decisa dal telefono",
                 motivo, logici_w, logici_h, logici_w * scala, logici_h * scala, scala);
        return;
    }
    snprintf(out, capacita,
             "%s: contenuto %dx%d logici = %.0fx%.0f fisici, scala %.3g, chiesto al telefono %dx%d a %d dpi "
             "(telefono: %.0f dpi), modalita' tablet %s, fattore %.3g, il telefono vede %dx%d punti",
             motivo, logici_w, logici_h, logici_w * scala, logici_h * scala, scala, w, h, densita,
             densita_telefono, tablet ? "si" : "no", fattore, punti_w, punti_h);
}

bool ph_fa_conta_apertura(PhCfgTelefono *profilo, const char *pacchetto, bool nuova, bool specchio,
                          int64_t ora_secondi)
{
    if (profilo == NULL || pacchetto == NULL || pacchetto[0] == '\0' || !nuova || specchio)
        return false;
    /* Un nome non valido non cambia nulla (la funzione di config torna -1). */
    return ph_cfg_telefono_conta_apertura(profilo, pacchetto, ora_secondi, NULL) == 0;
}

/* ---- Pizzico ----------------------------------------------------------------- */

void ph_fa_pz_inizializza(PhFaPizzico *p)
{
    p->aperto = false;
    p->ultimo_ms = 0;
    p->resto = 0.0;
}

PhFaPzAzione ph_fa_pz_passo(PhFaPizzico *p, double scatti, uint64_t t_ms, int *passo)
{
    int n = ph_fa_passi_rotella(&p->resto, scatti);
    if (n == 0)
        return PH_FA_PZ_NIENTE;
    n = CLAMP(n, -PH_FA_PIZZICO_PASSO_MAX, PH_FA_PIZZICO_PASSO_MAX);
    *passo = n;
    PhFaPzAzione az = p->aperto ? PH_FA_PZ_CONTINUA : PH_FA_PZ_INIZIO;
    p->aperto = true;
    p->ultimo_ms = t_ms;
    return az;
}

bool ph_fa_pz_scaduto(const PhFaPizzico *p, uint64_t t_ms)
{
    return p->aperto && t_ms >= p->ultimo_ms && t_ms - p->ultimo_ms >= PH_FA_PIZZICO_FINE_MS;
}

PhFaPzAzione ph_fa_pz_chiudi(PhFaPizzico *p)
{
    bool era = p->aperto;
    p->aperto = false;
    p->resto = 0.0;
    return era ? PH_FA_PZ_FINE : PH_FA_PZ_NIENTE;
}

bool ph_fa_scroll_e_zoom(GdkModifierType m)
{
    return (m & GDK_CONTROL_MASK) != 0 && (m & GDK_ALT_MASK) == 0;
}

double ph_fa_scatti_da_rapporto(double rapporto)
{
    if (!(rapporto > 0.0) || !isfinite(rapporto))
        return 0.0;
    return log(rapporto) / log(1.25);
}

PhFaZoomInizio ph_fa_zoom_inizio(bool tocco_in_corso, bool gesto_finestra_in_corso)
{
    if (gesto_finestra_in_corso)
        return PH_FA_ZOOM_RIFIUTA;
    return tocco_in_corso ? PH_FA_ZOOM_ANNULLA_TOCCO : PH_FA_ZOOM_PROCEDI;
}
