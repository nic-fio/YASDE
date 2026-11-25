/* Parti interne della scoperta, visibili solo ai test e a cerca.c.
 * Non fa parte del contratto (scoperta.h). */
#ifndef PH_SCOPERTA_INTERNO_H
#define PH_SCOPERTA_INTERNO_H

#include <stdio.h>

#include "scoperta/scoperta.h"

#define PH_SC_MAX_SRV 64
#define PH_SC_MAX_ADDR 128
#define PH_SC_MAX_PTR 64
#define PH_SC_NOME 256

typedef struct {
    char proprietario[PH_SC_NOME]; /* istanza.servizio.local, senza punto finale */
    char bersaglio[PH_SC_NOME];    /* nome dell'host dentro l'SRV */
    char origine[64];              /* chi ha spedito il pacchetto: ripiego se manca l'A */
    uint16_t porta;
} PhScSrv;

typedef struct {
    char nome[PH_SC_NOME];
    char indirizzo[64];
    bool v6;
} PhScAddr;

/* Record raccolti da uno o piu' pacchetti: SRV, A e AAAA possono arrivare
 * in pacchetti diversi e si combinano solo alla fine. */
typedef struct {
    PhScSrv srv[PH_SC_MAX_SRV];
    size_t n_srv;
    PhScAddr addr[PH_SC_MAX_ADDR];
    size_t n_addr;
    char ptr[PH_SC_MAX_PTR][PH_SC_NOME]; /* istanze annunciate da un PTR */
    size_t n_ptr;
} PhScRaccolta;

void ph_sc_raccolta_inizia(PhScRaccolta *r);

/* Legge un pacchetto e ne aggiunge i record alla raccolta. origine (puo' essere
 * NULL) e' l'indirizzo di chi l'ha spedito. Pacchetto malformato: si ferma al
 * primo errore, tenendo cio' che aveva gia' letto di valido. */
void ph_sc_raccolta_aggiungi(PhScRaccolta *r, const uint8_t *pacchetto, size_t lunghezza,
                             const char *origine);

/* Combina i record e aggiunge a trovati[] (da *numero) le voci nuove. Torna
 * quante ne ha aggiunte. */
size_t ph_sc_raccolta_risolvi(const PhScRaccolta *r, PhTelefonoTrovato *trovati, size_t *numero,
                              size_t massimo);

/* Vero se `sorgente` (ordine di rete) sta nella sottorete di una delle n
 * interfacce (indirizzi e maschere in ordine di rete). */
bool ph_sc_in_sottorete(uint32_t sorgente, const uint32_t *indirizzi, const uint32_t *maschere,
                        size_t n);

/* Come ph_scoperta_cerca, ma se traccia != NULL scrive su di esso i pacchetti
 * ricevuti e le voci trovate, con il tempo trascorso. */
int ph_scoperta_cerca_traccia(int durata_ms, PhTelefonoTrovato *trovati, size_t massimo,
                              PhErrore *e, FILE *traccia);

#endif
