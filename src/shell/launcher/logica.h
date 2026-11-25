/* Logica pura del launcher (F-04): niente widget, quindi si prova senza
 * schermo (prova_launcher.c). Usa solo GLib. */
#ifndef PH_SHELL_LAUNCHER_LOGICA_H
#define PH_SHELL_LAUNCHER_LOGICA_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>

/* Forma "da confronto" di un testo: Unicode scomposto (NFKD), senza i segni
 * diacritici (accenti), con le maiuscole ridotte (casefold). Per la ricerca
 * l'utente non deve dover indovinare maiuscole e accenti ("e" trova "È").
 * Il testo non valido in UTF-8 si tratta come vuoto. Con `pulisci` toglie gli
 * spazi a inizio e fine (la ricerca digitata, come il trim del mockup).
 * Torna una stringa nuova (g_free). */
char *ph_launcher_normalizza(const char *testo, bool pulisci);

/* Il nome (gia' normalizzato) contiene la ricerca (gia' normalizzata)? Una
 * ricerca vuota trova tutto. */
bool ph_launcher_corrisponde(const char *nome_norm, const char *ricerca_norm);

/* Indice della prima app che corrisponde, nell'ordine dato (Invio apre la
 * prima trovata), o -1 se nessuna. nomi_norm ha n voci. */
long ph_launcher_prima(const char *const *nomi_norm, size_t n, const char *ricerca_norm);

/* Misure del riquadro e della griglia (decisione del committente, fase 4 di
 * 2.1: riquadro centrato al posto di quello a 24 px dai bordi). Tutte in px. */
enum {
    PH_LAUNCHER_MARGINE_LATERALE = 24,  /* dai bordi, se l'area e' stretta */
    PH_LAUNCHER_LARGHEZZA_PERC = 60,    /* % della larghezza dell'area */
    PH_LAUNCHER_LARGHEZZA_MIN = 480,
    PH_LAUNCHER_LARGHEZZA_MAX = 960,
    PH_LAUNCHER_ALTEZZA_BARRA = 48,     /* la barra in basso, fuori dal riquadro */
    PH_LAUNCHER_ALTEZZA_PERC = 76,      /* % dell'area sopra la barra: massimo */
    PH_LAUNCHER_ALTO_PERC = 6,          /* % dell'altezza dell'area: distanza dall'alto */
    PH_LAUNCHER_ALTO_MIN = 24,
    PH_LAUNCHER_BORDO_E_IMBOTTITURA = 48, /* imbottitura 24+24 (css); il bordo del vetro e' un'ombra interna: 0 px */
    PH_LAUNCHER_COLONNA = 112,          /* larghezza di una colonna della griglia */
    PH_LAUNCHER_CAMPO_MAX = 420,        /* larghezza massima del campo di ricerca */
};

typedef struct {
    int larghezza;         /* del riquadro (bordo compreso) */
    int margine_alto;      /* tra il bordo alto dell'area e il riquadro */
    int altezza_max;       /* massimo del riquadro (bordo compreso); 0 se non c'e' spazio */
    int larghezza_interna; /* dentro bordo e imbottitura */
    int colonne;           /* della griglia, almeno 1 */
    int larghezza_griglia; /* colonne x PH_LAUNCHER_COLONNA: la griglia si centra */
    int larghezza_campo;   /* campo di ricerca: l'interna, al massimo CAMPO_MAX */
} PhLauncherMisure;

/* Misure per un'area di larghezza x altezza (px; radice della shell). Area
 * non misurata o negativa: tutto a valori minimi senza mai uscire dai limiti
 * (larghezza 0 = "non dare una misura"). */
void ph_launcher_misure(int larg_area, int alt_area, PhLauncherMisure *m);

/* Colonne di una griglia larga `larghezza_interna` px: colonne da 112, almeno 1. */
int ph_launcher_colonne(int larghezza_interna);

/* Posizione del menu contestuale dato il punto del clic (coordinate della
 * radice) e la misura della radice: spostata per restare dentro (mockup:
 * x <= larghezza - 210, y <= altezza - 150, mai sotto 0). */
void ph_launcher_posizione_menu(int clic_x, int clic_y, int larghezza, int altezza, int *x,
                                int *y);

#endif
