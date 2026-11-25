/* Ganci per il demo: servono a mostrare i pannelli nelle foto di
 * strumenti/foto-demo.sh, che non ha ne' mouse ne' tastiera. Solo thread GTK. */
#ifndef PH_SHELL_TRASFERIMENTO_PROVA_H
#define PH_SHELL_TRASFERIMENTO_PROVA_H

#include "shell/shell.h"

/* 0 invio con due file in coda; 1 selettore in /data (non consentita); 2 ricerca
 * "pippo"; 3 scarica con due file scelti; 4 selettore di cartelle del PC. */
void ph_trasferimento_prova(PhShell *sh, int passo);

#endif
