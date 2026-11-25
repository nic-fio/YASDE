/* Telefono finto per le prove del pezzo "dati": sostituisce le funzioni di
 * src/componente (richiesta, canale, ascolta) e di src/adb (ph_canale_*). */
#ifndef PH_DATI_PROVA_FINTO_H
#define PH_DATI_PROVA_FINTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adb/adb.h"
#include "componente/componente.h"

/* Riporta tutto allo stato iniziale (libera canali, risposte, registri). */
void finto_azzera(void);
PhComponente *finto_componente(void);

/* Richieste: la risposta all'ultimo tipo programmato; ERRORE se errore != NULL. */
void finto_risposta(uint8_t tipo, const uint8_t *corpo, size_t n);
void finto_risposta_errore(uint8_t tipo, const char *frase);
int finto_richieste(void);                    /* quante richieste arrivate */
uint8_t finto_ultimo_tipo(void);
size_t finto_ultimo_corpo(uint8_t *dest, size_t max); /* byte della richiesta */

/* Cancello: finche' e' chiuso le richieste restano in attesa. Con "attese" si
 * conta (anche tra le richieste appena entrate) quante sono ferme: serve a
 * sapere che una richiesta e' arrivata fin li' senza dormire. */
int finto_violazioni(void); /* ascolta(NULL) chiamata con richieste ancora in corso */
void finto_cancello(bool chiuso);
int finto_cancello_attese(void);

/* Eventi: chiama l'ascoltatore registrato per quel tipo (come il thread del
 * componente: tiene il blocco che ph_componente_ascolta aspetta). */
void finto_evento(uint8_t tipo, const uint8_t *corpo, size_t n);
bool finto_ascolto_registrato(void);
bool finto_ascolto_per(uint8_t tipo);                 /* c'e' un ascoltatore per quel tipo */
void finto_togliere_ascolto_fallisce_alla_n(int n);   /* l'N-esimo ph_componente_ascolta(NULL) da ora fallisce */
void finto_registrare_ascolto_fallisce_alla_n(int n); /* l'N-esima registrazione (funzione non NULL) da ora fallisce */
void finto_ritardo_ascolta_ms(int ms);                /* pausa dentro ph_componente_ascolta (prima di impostarlo, nessun altro thread) */
void finto_togliere_ascolto_fallisce_una_volta(void); /* ph_componente_ascolta(NULL) fallisce una volta */

/* Canali aperti con ph_componente_canale: n-esimo (0 = primo). Le strutture
 * sopravvivono alla chiusura per poter controllare lo stato. */
typedef struct FintoCanale FintoCanale;
void finto_rifiuta_canali(bool rifiuta);
void finto_ritardo_apertura_ms(int ms);
void finto_scritture_falliscono(bool v);       /* ogni ph_canale_scrivi fallisce */
int finto_canali_aperti(void);
FintoCanale *finto_canale(int n);
const char *finto_canale_riga(FintoCanale *f);
void finto_canale_spingi(FintoCanale *f, const void *dati, size_t n);   /* T -> PC */
void finto_canale_chiudi_remoto(FintoCanale *f);                         /* dopo i dati: CHIUSO */
void finto_canale_errore_remoto(FintoCanale *f);                         /* dopo i dati: ERRORE */
void finto_canale_a_pezzi(FintoCanale *f, size_t max_per_lettura);       /* legge a pezzi piccoli */
void finto_canale_scrittura_fallisce(FintoCanale *f, bool fallisce);
size_t finto_canale_scritto(FintoCanale *f, uint8_t *dest, size_t max); /* PC -> T */
bool finto_canale_chiuso_dal_pc(FintoCanale *f);

#endif
