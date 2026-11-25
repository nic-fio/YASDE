/* Il punto d'ingresso di ogni pezzo della shell. Ogni _installa si chiama una
 * volta, dal thread GTK, dopo ph_shell_nuova: crea i widget del pezzo, li
 * aggiunge agli strati (shell.h), carica il CSS del pezzo, registra i gestori
 * che serve e si mette in ascolto dei servizi. L'ossatura li chiama in questo
 * ordine: desktop, finestra-app, sistema, launcher, trasferimento, barra (la
 * barra per ultima: colloca i pulsanti che le danno gli altri, per esempio
 * ph_shell_sistema_pulsante).
 * La superficie non ha _installa: la usa finestra-app (superficie.h).
 * Ogni pezzo libera le sue cose quando la radice viene distrutta (segnale
 * "destroy" o weak ref sulla radice). */
#ifndef PH_SHELL_PEZZI_H
#define PH_SHELL_PEZZI_H

#include "shell/shell.h"

void ph_desktop_installa(PhShell *sh);       /* F-02 */
void ph_finestra_app_installa(PhShell *sh);  /* F-05 */
void ph_sistema_installa(PhShell *sh);       /* F-06, F-07, F-08 */
void ph_launcher_installa(PhShell *sh);      /* F-04 */
void ph_trasferimento_installa(PhShell *sh); /* F-09 */
void ph_barra_installa(PhShell *sh);         /* F-03 */

#endif
