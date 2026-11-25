/* Logica pura del launcher (F-04): vedi logica.h. */
#include "logica.h"

#include <string.h>

char *ph_launcher_normalizza(const char *testo, bool pulisci)
{
    if (testo == NULL || !g_utf8_validate(testo, -1, NULL))
        return g_strdup("");
    /* NFKD: la lettera accentata diventa lettera + segno, che poi si toglie. */
    char *scomposto = g_utf8_normalize(testo, -1, G_NORMALIZE_ALL);
    if (scomposto == NULL)
        return g_strdup("");
    GString *s = g_string_sized_new(strlen(scomposto));
    for (const char *p = scomposto; *p != '\0'; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        GUnicodeType t = g_unichar_type(c);
        if (t == G_UNICODE_NON_SPACING_MARK || t == G_UNICODE_SPACING_MARK ||
            t == G_UNICODE_ENCLOSING_MARK)
            continue;
        g_string_append_unichar(s, c);
    }
    g_free(scomposto);
    char *sc = g_string_free(s, FALSE);
    char *ridotto = g_utf8_casefold(sc, -1);
    g_free(sc);
    if (ridotto == NULL)
        return g_strdup("");
    if (pulisci)
        g_strstrip(ridotto);
    return ridotto;
}

bool ph_launcher_corrisponde(const char *nome_norm, const char *ricerca_norm)
{
    if (ricerca_norm == NULL || ricerca_norm[0] == '\0')
        return true;
    if (nome_norm == NULL)
        return false;
    return strstr(nome_norm, ricerca_norm) != NULL;
}

long ph_launcher_prima(const char *const *nomi_norm, size_t n, const char *ricerca_norm)
{
    if (nomi_norm == NULL)
        return -1;
    for (size_t i = 0; i < n; i++)
        if (ph_launcher_corrisponde(nomi_norm[i], ricerca_norm))
            return (long)i;
    return -1;
}

int ph_launcher_colonne(int larghezza_interna)
{
    if (larghezza_interna < PH_LAUNCHER_COLONNA)
        return 1; /* anche negativa o zero (radice non ancora misurata) */
    return larghezza_interna / PH_LAUNCHER_COLONNA;
}

void ph_launcher_misure(int larg_area, int alt_area, PhLauncherMisure *m)
{
    if (m == NULL)
        return;
    if (larg_area < 0)
        larg_area = 0;
    if (alt_area < 0)
        alt_area = 0;
    /* Area piu' stretta di minimo + 2 margini: il riquadro sta a 24 px dai bordi. */
    int larg;
    if (larg_area < PH_LAUNCHER_LARGHEZZA_MIN + 2 * PH_LAUNCHER_MARGINE_LATERALE) {
        larg = larg_area - 2 * PH_LAUNCHER_MARGINE_LATERALE;
        if (larg < 0)
            larg = 0;
    } else {
        /* long long: l'area arriva dalla finestra, non ci si fida del prodotto in int */
        long long l = (long long)larg_area * PH_LAUNCHER_LARGHEZZA_PERC / 100;
        larg = l < PH_LAUNCHER_LARGHEZZA_MIN   ? PH_LAUNCHER_LARGHEZZA_MIN
               : l > PH_LAUNCHER_LARGHEZZA_MAX ? PH_LAUNCHER_LARGHEZZA_MAX
                                               : (int)l;
    }
    m->larghezza = larg;
    m->larghezza_interna = larg > PH_LAUNCHER_BORDO_E_IMBOTTITURA
                               ? larg - PH_LAUNCHER_BORDO_E_IMBOTTITURA
                               : 0;
    m->colonne = ph_launcher_colonne(m->larghezza_interna);
    m->larghezza_griglia = m->colonne * PH_LAUNCHER_COLONNA;
    m->larghezza_campo = m->larghezza_interna < PH_LAUNCHER_CAMPO_MAX ? m->larghezza_interna
                                                                      : PH_LAUNCHER_CAMPO_MAX;
    int alto = (int)((long long)alt_area * PH_LAUNCHER_ALTO_PERC / 100);
    m->margine_alto = alto < PH_LAUNCHER_ALTO_MIN ? PH_LAUNCHER_ALTO_MIN : alto;
    int sopra_barra = alt_area - PH_LAUNCHER_ALTEZZA_BARRA;
    m->altezza_max =
        sopra_barra > 0 ? (int)((long long)sopra_barra * PH_LAUNCHER_ALTEZZA_PERC / 100) : 0;
}

void ph_launcher_posizione_menu(int clic_x, int clic_y, int larghezza, int altezza, int *x,
                                int *y)
{
    int mx = larghezza - 210, my = altezza - 150;
    if (mx < 0)
        mx = 0;
    if (my < 0)
        my = 0;
    int px = clic_x < 0 ? 0 : clic_x;
    int py = clic_y < 0 ? 0 : clic_y;
    if (x != NULL)
        *x = px > mx ? mx : px;
    if (y != NULL)
        *y = py > my ? my : py;
}
