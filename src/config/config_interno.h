/* Funzioni condivise dentro il pezzo `config` (config.c e profilo.c); non fanno
 * parte del contratto verso gli altri pezzi. */
#ifndef PH_CONFIG_INTERNO_H
#define PH_CONFIG_INTERNO_H

#include <dirent.h>
#include <stddef.h>

#include "comune/errore.h"

typedef int (*DaTesto)(const char *testo, size_t lunghezza, void *dst, PhErrore *e);

/* Rinomina il file in "<file>.illeggibile[.N]" senza sostituirne uno esistente
 * (se i 10 nomi sono presi sostituisce il piu' vecchio); scrive il nome scelto
 * in `nuovo`. 0 riuscito, -1 errore. */
int ph_cfg_metti_da_parte(const char *percorso, char *nuovo, size_t capacita);

/* Legge un file (al massimo `massimo` byte) e lo interpreta con da_testo, che
 * torna 0 riuscito, -1 per un file rovinato da mettere da parte, -2 per un file
 * da NON toccare (es. versione futura): esito -1 con predefiniti, nessun
 * rename. Il motivo e' in PhErrore. Esiti come *_carica in config.h:
 * 0 letto o assente, 1 messo da parte (predefiniti), -1 errore vero. */
int ph_cfg_carica_file(const char *percorso, const char *etichetta, size_t massimo, DaTesto da_testo, void *dst,
                       void (*predefinito)(void *), PhErrore *e);

/* Solo per le prove: se non e' NULL sostituisce readdir nella pulizia degli
 * sfondi, cosi' un test puo' simulare un errore di lettura della cartella. */
extern struct dirent *(*ph_cfg_readdir_prova)(DIR *);

#endif
