/* Solo per il demo (compilato in demo-ossatura, mai nel prodotto): la disposizione fissa dei widget
 * per la schermata «vetrina» del manuale e del sito (docs/sources/img/desktop-widgets.png).
 * Undici widget in quattro colonne, senza sovrapposizioni, con i dati del telefono finto (tutti
 * inventati: «Alex Sample», «SampleNet», «Sample Song», «Sample City»). Si chiama dal passo
 * «vetrina» di demo-ossatura; carica il profilo fisso e ricostruisce i widget. */
#include <string.h>

#include "interno.h"

void ph_demo_vetrina(PhShell *sh);

typedef struct {
    PhCfgWidgetTipo tipo;
    unsigned stile;
    int destra, alto, l, a;
} Posto;

/* Colonne (da destra): 320 / 250 / 250 / 250, distanza 16. Altezze: la barra in basso resta libera. */
#define C1 16
#define C2 352
#define C3 618
#define C4 884
static const Posto POSTI[] = {
    /* colonna 1: meteo con curva e giorni, chiamate */
    {PH_CFG_WIDGET_METEO, PH_DSK_METEO_ADESSO, C1, 16, 320, 300},
    {PH_CFG_WIDGET_CHIAMATE, 0, C1, 332, 320, 254},
    /* colonna 2: calendario del mese, memoria, prossima sveglia */
    {PH_CFG_WIDGET_CALENDARIO_MESE, 0, C2, 16, 250, 290},
    {PH_CFG_WIDGET_MEMORIA, 0, C2, 322, 250, 144},
    {PH_CFG_WIDGET_SVEGLIA, 0, C2, 482, 250, 80},
    /* colonna 3: in riproduzione, rete, note rapide */
    {PH_CFG_WIDGET_RIPRODUZIONE, 0, C3, 16, 250, 170},
    {PH_CFG_WIDGET_RETE, 0, C3, 202, 250, 170},
    {PH_CFG_WIDGET_NOTE, 0, C3, 388, 250, 180},
    /* colonna 4: orologio tondo, batteria tonda, interruttori */
    {PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_CLASSICO, C4 + 33, 16, 184, 184},
    {PH_CFG_WIDGET_BATTERIA, 0, C4 + 37, 216, 176, 176},
    {PH_CFG_WIDGET_INTERRUTTORI, 0, C4, 408, 250, 112},
};

void ph_demo_vetrina(PhShell *sh)
{
    PhDesktop *d = g_object_get_data(G_OBJECT(ph_shell_radice(sh)), "ph-desktop");
    PhCfgTelefono *p = d != NULL ? ph_dsk_profilo(d) : NULL;
    if (p == NULL) {
        g_printerr("vetrina: il profilo del telefono finto non e' arrivato\n");
        return;
    }
    memset(p->widget, 0, sizeof p->widget);
    size_t n = sizeof POSTI / sizeof POSTI[0];
    for (size_t i = 0; i < n; i++) {
        p->widget[i].tipo = POSTI[i].tipo;
        p->widget[i].stile = POSTI[i].stile;
        p->widget[i].destra = POSTI[i].destra;
        p->widget[i].alto = POSTI[i].alto;
        p->widget[i].larghezza = POSTI[i].l;
        p->widget[i].altezza = POSTI[i].a;
        p->widget[i].dimensione = PH_CFG_WIDGET_MEDIO;
    }
    p->n_widget = n;
    g_strlcpy(p->note, "Buy tickets for Friday\nCall Alex about the trip", sizeof p->note);
    ph_dsk_widget_ricostruisci(d);
}
