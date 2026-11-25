/* Telefono finto per provare installa.c: sostituisce ph_file_invia e
 * ph_shell_esegui. Tiene l'elenco dei file "sul telefono" e dei comandi
 * ricevuti, e risponde ai comandi pm con testi decisi dalla prova. */
#ifndef PH_FINTO_INSTALLA_H
#define PH_FINTO_INSTALLA_H

#include <stddef.h>

#include "adb/adb.h"
#include "file/file.h"

typedef struct {
    PhFileEsito copia_esito;   /* esito di ph_file_invia (RIUSCITO, ERRORE, ...) */
    const char *pm_uscita;     /* stdout di pm install/uninstall */
    const char *pm_errori;     /* stderr */
    int pm_scade;              /* 1: ph_shell_esegui di pm torna -1 (nessuna risposta) */
    int rm_codice;             /* codice di uscita di "rm -f" (0 = riuscito) */
    int connessione_caduta;    /* 1: dopo la copia ogni comando di shell torna -1 */
    int annulla_al_blocco;     /* numero del blocco di copia dopo cui si ferma con ANNULLATO se *annulla */
} FintoInst;

void finto_inst_reset(void);
void finto_inst_config(const FintoInst *c);
PhAdb *finto_inst_adb(void);
void finto_inst_adb_libera(PhAdb *a);

size_t finto_inst_comandi(void);
char *finto_inst_comando(size_t i);            /* copia da liberare, NULL se fuori limite */
size_t finto_inst_copie(void);
char *finto_inst_copia_remoto(size_t i);       /* percorsi remoti delle copie, nell'ordine */
int finto_inst_remoto_esiste(const char *p);   /* il file e' ancora "sul telefono" */
size_t finto_inst_file_sul_telefono(void);     /* quanti file nostri ci sono ancora */

#endif
