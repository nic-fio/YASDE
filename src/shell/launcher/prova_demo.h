/* Funzioni del solo demo (vedi prova_demo.c). */
#ifndef PH_SHELL_LAUNCHER_PROVA_DEMO_H
#define PH_SHELL_LAUNCHER_PROVA_DEMO_H

#include <stdbool.h>

#include "shell/shell.h"

void ph_launcher_demo_cerca(PhShell *sh, const char *testo);
void ph_launcher_demo_menu(PhShell *sh);
/* Prova a schermo (launcher aperto e disegnato): misure del riquadro e clic
 * nell'area scoperta. Torna il numero di controlli falliti (`scorre`: ci si aspetta che la griglia scorra; con `stampa`, messaggi su stderr). */
/* Aggiunge una cella col segnaposto in un angolo (solo per la prova a schermo). */
void ph_launcher_demo_sonda(PhShell *sh);
int ph_launcher_demo_verifica(PhShell *sh, bool stampa, bool scorre);

#endif
