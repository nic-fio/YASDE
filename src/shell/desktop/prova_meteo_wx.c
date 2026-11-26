/* Prove di logica dello stile «Adesso» del widget Meteo che cresce con la misura vera (passo 2b):
 * soglie in una funzione pura, mai un elemento in meno al crescere di una misura, misure ostili,
 * minimo del tipo abbastanza alto per l'attribuzione e gli stati. Senza schermo. */
#include <limits.h>
#include <stdio.h>

#include "interno.h"

static int errori = 0;
#define VERIFICA(cond)                                                                       \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            fprintf(stderr, "%s:%d: fallita: %s\n", __FILE__, __LINE__, #cond);              \
            errori++;                                                                        \
        }                                                                                    \
    } while (0)

static void prova_soglie(void)
{
    /* Piccolo: solo icona e temperatura. */
    PhDskMeteoSezioni s = ph_dsk_meteo_sezioni(200, 200);
    VERIFICA(!s.testo && !s.curva && s.n_ore == 0 && s.n_giorni == 0 && s.altezza_curva == 0 && s.corpo_temp == 1);
    /* Un po' piu' largo: anche luogo, condizione, massima/minima (alla soglia esatta, non prima). */
    s = ph_dsk_meteo_sezioni(PH_DSK_METEO_ADESSO_LARG_TESTO - 1, 190);
    VERIFICA(!s.testo);
    s = ph_dsk_meteo_sezioni(PH_DSK_METEO_ADESSO_LARG_TESTO, 190);
    VERIFICA(s.testo && !s.curva && s.corpo_temp == 1);
    /* Piu' alto: la curva (alla soglia esatta). */
    s = ph_dsk_meteo_sezioni(240, PH_DSK_METEO_ADESSO_ALT_CURVA - 1);
    VERIFICA(!s.curva && s.n_ore == 0 && s.altezza_curva == 0);
    s = ph_dsk_meteo_sezioni(240, PH_DSK_METEO_ADESSO_ALT_CURVA);
    VERIFICA(s.curva && s.n_ore == 8 && s.n_giorni == 0 && s.altezza_curva == PH_DSK_METEO_ADESSO_ALT_CURVA_BASE);
    s = ph_dsk_meteo_sezioni(PH_DSK_METEO_ADESSO_LARG_CURVA - 1, 600); /* stretto: niente curva ne' giorni */
    VERIFICA(!s.curva && s.n_giorni == 0);
    /* Ancora piu' grande: i giorni, da due in su (uno solo non si mostra). */
    s = ph_dsk_meteo_sezioni(320, PH_DSK_METEO_ADESSO_ALT_CURVA + PH_DSK_METEO_ADESSO_ALT_GIORNO);
    VERIFICA(s.curva && s.n_giorni == 0);
    s = ph_dsk_meteo_sezioni(320, PH_DSK_METEO_ADESSO_ALT_CURVA + 2 * PH_DSK_METEO_ADESSO_ALT_GIORNO);
    VERIFICA(s.n_giorni == 2 && s.n_ore == 12 && s.altezza_curva == PH_DSK_METEO_ADESSO_ALT_CURVA_BASE);
    s = ph_dsk_meteo_sezioni(320, 300);
    VERIFICA(s.n_giorni == 2 && s.corpo_temp == 2 && s.lato_icona > ph_dsk_meteo_sezioni(240, 190).lato_icona);
    /* Il massimo: sette giorni, la curva prende lo spazio che avanza ma non oltre il tetto. */
    s = ph_dsk_meteo_sezioni(480, 640);
    VERIFICA(s.n_giorni == PH_METEO_GIORNI_MAX && s.n_ore == 16 &&
             s.altezza_curva == PH_DSK_METEO_ADESSO_ALT_CURVA_BASE + PH_DSK_METEO_ADESSO_ALT_CURVA_EXTRA);
    /* Il riquadro dell'avviso dei dati vecchi toglie spazio: a misura limite la curva sparisce. */
    int alt = PH_DSK_METEO_ADESSO_ALT_CURVA + PH_DSK_METEO_ADESSO_ALT_AVVISO - 1;
    VERIFICA(ph_dsk_meteo_sezioni(320, alt).curva);
    VERIFICA(!ph_dsk_meteo_sezioni(320, alt - PH_DSK_METEO_ADESSO_ALT_AVVISO).curva);
}

static void prova_monotona(void)
{
    /* Su tutta la griglia delle misure possibili: crescendo in larghezza o in altezza non si perde
     * nulla (testo, curva, punti, giorni, corpo del numero). */
    for (int a = 0; a <= 700; a += 8) {
        PhDskMeteoSezioni prec = ph_dsk_meteo_sezioni(0, a);
        for (int l = 8; l <= 520; l += 8) {
            PhDskMeteoSezioni s = ph_dsk_meteo_sezioni(l, a);
            VERIFICA(s.testo >= prec.testo && s.curva >= prec.curva && s.n_ore >= prec.n_ore &&
                     s.n_giorni >= prec.n_giorni && s.corpo_temp >= prec.corpo_temp &&
                     s.lato_icona >= prec.lato_icona);
            prec = s;
        }
    }
    for (int l = 0; l <= 520; l += 8) {
        PhDskMeteoSezioni prec = ph_dsk_meteo_sezioni(l, 0);
        for (int a = 8; a <= 700; a += 8) {
            PhDskMeteoSezioni s = ph_dsk_meteo_sezioni(l, a);
            VERIFICA(s.testo >= prec.testo && s.curva >= prec.curva && s.n_ore >= prec.n_ore &&
                     s.n_giorni >= prec.n_giorni && s.corpo_temp >= prec.corpo_temp);
            prec = s;
        }
    }
}

static void prova_ostili(void)
{
    static const int v[] = {INT_MIN, -100000, -1, 0, 1, 239, 100000, INT_MAX};
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++)
        for (size_t j = 0; j < sizeof v / sizeof v[0]; j++) {
            PhDskMeteoSezioni s = ph_dsk_meteo_sezioni(v[i], v[j]);
            VERIFICA(s.n_giorni <= PH_METEO_GIORNI_MAX && s.n_ore <= 16);
            VERIFICA(s.curva ? (s.altezza_curva >= PH_DSK_METEO_ADESSO_ALT_CURVA_BASE &&
                                s.altezza_curva <= PH_DSK_METEO_ADESSO_ALT_CURVA_BASE + PH_DSK_METEO_ADESSO_ALT_CURVA_EXTRA)
                             : (s.altezza_curva == 0 && s.n_ore == 0 && s.n_giorni == 0));
            VERIFICA(s.corpo_temp >= 1 && s.corpo_temp <= 2 && s.lato_icona > 0);
        }
    PhDskMeteoSezioni zero = ph_dsk_meteo_sezioni(INT_MIN, INT_MIN);
    VERIFICA(!zero.testo && !zero.curva);
}

static void prova_uguali(void)
{
    PhDskMeteoSezioni a = ph_dsk_meteo_sezioni(320, 300), b = ph_dsk_meteo_sezioni(320, 300);
    VERIFICA(ph_dsk_meteo_sezioni_uguali(&a, &b));
    b = ph_dsk_meteo_sezioni(320, 300 + PH_DSK_METEO_ADESSO_ALT_GIORNO);
    VERIFICA(!ph_dsk_meteo_sezioni_uguali(&a, &b));
    b = ph_dsk_meteo_sezioni(240, 300);
    VERIFICA(!ph_dsk_meteo_sezioni_uguali(&a, &b));
}

static void prova_minimo(void)
{
    /* Il minimo del Meteo e' quello che tiene attribuzione e stati (190) e regge i livelli: le tre
     * misure equivalenti restano crescenti e ognuna ritrova il proprio livello. */
    PhDskLimiti l = ph_dsk_misura_limiti(PH_CFG_WIDGET_METEO, 0);
    VERIFICA(l.min.altezza == 200);
    /* Medio mostra la curva (e solo quella), Grande anche i giorni: le misure equivalenti sono
     * quelle che il contenuto riempie. */
    PhDskMeteoSezioni sm = ph_dsk_meteo_sezioni(ph_dsk_misura_equivalente(PH_CFG_WIDGET_METEO, 0, PH_CFG_WIDGET_MEDIO).larghezza,
                                                ph_dsk_misura_equivalente(PH_CFG_WIDGET_METEO, 0, PH_CFG_WIDGET_MEDIO).altezza);
    VERIFICA(sm.testo && sm.curva && sm.n_giorni == 0);
    PhDskMeteoSezioni sg = ph_dsk_meteo_sezioni(ph_dsk_misura_equivalente(PH_CFG_WIDGET_METEO, 0, PH_CFG_WIDGET_GRANDE).larghezza,
                                                ph_dsk_misura_equivalente(PH_CFG_WIDGET_METEO, 0, PH_CFG_WIDGET_GRANDE).altezza);
    VERIFICA(sg.testo && sg.curva && sg.n_giorni >= 2);
    /* Gli altri stili tengono il minimo di prima. */
    VERIFICA(ph_dsk_misura_limiti(PH_CFG_WIDGET_METEO, 3).min.altezza == 150);
    PhDskMisura p = ph_dsk_misura_equivalente(PH_CFG_WIDGET_METEO, 0, PH_CFG_WIDGET_PICCOLO);
    PhDskMisura m = ph_dsk_misura_equivalente(PH_CFG_WIDGET_METEO, 0, PH_CFG_WIDGET_MEDIO);
    PhDskMisura g = ph_dsk_misura_equivalente(PH_CFG_WIDGET_METEO, 0, PH_CFG_WIDGET_GRANDE);
    VERIFICA(p.larghezza == l.min.larghezza && p.altezza == l.min.altezza);
    VERIFICA(p.larghezza < m.larghezza && m.larghezza < g.larghezza && p.altezza < m.altezza && m.altezza < g.altezza);
    VERIFICA(ph_dsk_livello(PH_CFG_WIDGET_METEO, 0, p) == PH_CFG_WIDGET_PICCOLO);
    VERIFICA(ph_dsk_livello(PH_CFG_WIDGET_METEO, 0, m) == PH_CFG_WIDGET_MEDIO);
    VERIFICA(ph_dsk_livello(PH_CFG_WIDGET_METEO, 0, g) == PH_CFG_WIDGET_GRANDE);
    /* Il livello di ogni misura equivalente ritrova se stesso anche per gli altri stili. */
    VERIFICA(ph_dsk_livello(PH_CFG_WIDGET_METEO, 3, ph_dsk_misura_equivalente(PH_CFG_WIDGET_METEO, 3, PH_CFG_WIDGET_MEDIO)) ==
             PH_CFG_WIDGET_MEDIO);
    /* Un profilo salvato prima (200 x 150) si porta al nuovo minimo, senza uscire dai limiti. */
    PhDskMisura v = ph_dsk_misura_da_profilo(PH_CFG_WIDGET_METEO, 0, PH_CFG_WIDGET_PICCOLO, 200, 150);
    VERIFICA(v.larghezza == 200 && v.altezza == l.min.altezza);
}

static void prova_forma_breve(void)
{
    /* Ci sta per intero (anche al pixel): forma lunga; oltre: breve. */
    VERIFICA(!ph_dsk_meteo_forma_breve(120, 100));
    VERIFICA(!ph_dsk_meteo_forma_breve(100, 100));
    VERIFICA(ph_dsk_meteo_forma_breve(99, 100));
    /* Misure non utilizzabili (non ancora note): forma lunga. */
    VERIFICA(!ph_dsk_meteo_forma_breve(0, 100));
    VERIFICA(!ph_dsk_meteo_forma_breve(-5, 100));
    VERIFICA(!ph_dsk_meteo_forma_breve(50, 0));
    VERIFICA(ph_dsk_meteo_forma_breve(1, INT_MAX));
}

int main(void)
{
    prova_forma_breve();
    prova_soglie();
    prova_monotona();
    prova_ostili();
    prova_uguali();
    prova_minimo();
    if (errori == 0)
        printf("prova_meteo_wx: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
