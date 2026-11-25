/* Connessione "cifrata" FINTA per le prove: in chiaro su 127.0.0.1, porta
 * decisa dalla prova. Realizza tls.h al posto di tls.c. Le manopole servono a
 * far arrivare il pezzo in una fase precisa (e a fermarlo li') senza pause. */
#ifndef PH_METEO_TLS_FINTO_H
#define PH_METEO_TLS_FINTO_H

#include <semaphore.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

extern atomic_int ph_tls_finto_porta;      /* porta del server finto */
extern atomic_int ph_tls_finto_rifiuta;    /* 1: apri torna «certificato non valido» */
extern atomic_int ph_tls_finto_trattieni;  /* 1: apri aspetta (annullabile, con scadenza) finche' torna 0 */
extern sem_t ph_tls_finto_in_apri;         /* postato quando apri comincia ad aspettare */
extern atomic_int ph_tls_finto_aperture;   /* quante volte e' stato chiamato apri */
extern atomic_int ph_tls_finto_connessioni; /* quante volte apri e' arrivata a fare connect() */
extern atomic_int ph_tls_finto_ignora_annulla; /* 1: il flag di annullamento non si guarda (simula una risposta che arriva comunque) */
/* Modalita' «memoria»: apri non usa la rete, leggi consegna `dati` a pezzi di
 * lunghezza casuale (da `seme`) e poi la fine pulita; scrivi ingoia tutto.
 * dati NULL = torna alla rete. Per provare http.c con ogni sequenza di byte. */
void ph_tls_finto_memoria(const char *dati, size_t n, uint32_t seme);
void ph_tls_finto_ultimo(char *host, size_t cap, int *porta); /* host e porta dell'ultima apri */

#endif
