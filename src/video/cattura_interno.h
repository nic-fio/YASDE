/* Parti interne di cattura: condivise dai file di cattura e dai test.
 * Non e' contratto: nessun altro pezzo lo include. */
#ifndef PH_CATTURA_INTERNO_H
#define PH_CATTURA_INTERNO_H

#include <stddef.h>

#include "video/cattura.h"

/* Scrive tutto, riprovando su EINTR e scritture corte. 0 o -1 (errno resta). */
int ph_cattura_scrivi_tutto(int fd, const void *dati, size_t n);

/* Testo di errno senza passare da strerror (non rientrante). */
void ph_cattura_testo_errno(int codice, char *uscita, size_t n);

/* Crea (O_EXCL) il primo file libero "<cartella>/<nome> [(k)].<estensione>".
 * Torna il descrittore (>= 0) e il percorso scelto, oppure -1. Crea la
 * cartella se manca (un livello). */
int ph_cattura_apri_nuovo(const char *cartella, const char *app, const char *estensione,
                          time_t quando, char *percorso, size_t n_percorso, PhErrore *e);

/* Come ph_registrazione_inizia ma su un descrittore gia' aperto (che passa a
 * lei, anche in caso di errore): serve ai test (disco pieno con /dev/full). */
PhRegistrazione *ph_registrazione_inizia_su_fd(int fd, bool con_audio, PhErrore *e);

#endif
