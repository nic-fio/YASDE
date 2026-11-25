/* Telefono finto per i test di src/file: implementa le funzioni ph_canale_*
 * di adb.h con un server "sync:" in memoria che lavora su una cartella
 * temporanea (la radice del telefono). Risponde come il telefono vero e, a
 * richiesta, male: risposte troncate, byte corrotti, FAIL, blocchi assurdi.
 * Non ha thread: ogni scrittura del client viene elaborata subito. */
#ifndef PH_FILE_FINTO_CANALE_H
#define PH_FILE_FINTO_CANALE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adb/adb.h"

typedef struct {
    size_t tronca_a;             /* dopo questo numero di byte il canale non produce altro (SIZE_MAX = mai) */
    bool stallo;                 /* dopo il troncamento: resta zitto (TEMPO) invece di chiudere */
    long corrompi;               /* inverte il byte in questa posizione dell'uscita (-1 = nessuno) */
    bool fail_invio;             /* SEND: alla fine FAIL "No space left on device", file parziale lasciato */
    bool fail_ricevi_a_meta;     /* RECV: dopo il primo blocco manda FAIL */
    uint64_t dimensione_finta;   /* se != 0, STA2/LST2/LIS2 dichiarano questa dimensione per i file normali */
    uint32_t lunghezza_assurda;  /* se != 0, RECV manda un DATA con questa lunghezza */
    size_t blocco_ricevi;        /* dimensione dei DATA in RECV (0 = 65536) */
    size_t scrivi_errore_dopo;   /* dopo questi byte scritti dal client, ph_canale_scrivi fallisce (SIZE_MAX = mai) */
    bool rifiuta_apertura;       /* ph_canale_apri fallisce */
} FintoOpzioni;

/* La "connessione": una radice sul disco e le opzioni. */
struct PhAdb {
    char radice[512];
    FintoOpzioni o;
    size_t ultimo_prodotti;  /* byte che l'ultimo canale chiuso aveva prodotto, prima di troncamenti */
    unsigned rm_eseguiti;    /* quanti "rm" via shell sono arrivati */
    unsigned canali_aperti;
    unsigned canali_chiusi;
};

PhAdb *finto_nuovo(void);               /* crea la radice (cartella temporanea) */
void finto_libera(PhAdb *a);            /* cancella tutto */
void finto_opzioni_normali(PhAdb *a);   /* nessun difetto */
/* Percorso reale sul disco di un percorso del telefono. */
void finto_percorso(const PhAdb *a, const char *telefono, char *uscita, size_t n);

#endif
