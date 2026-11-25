/* SPAKE2 su edwards25519, nella variante che usa Android per l'abbinamento
 * (quella di BoringSSL, con le sue particolarita', riprodotte apposta: senza,
 * il telefono rifiuta). Scritto da zero dalla descrizione del protocollo.
 *
 * Aritmetica con i BIGNUM di OpenSSL: corretta ma NON a tempo costante.
 * Accettabile qui: il codice a sei cifre vale una volta sola, per pochi
 * secondi, sulla rete di casa. */
#ifndef PH_SPAKE2_H
#define PH_SPAKE2_H

#include <stddef.h>
#include <stdint.h>

#define PH_SPAKE2_MESSAGGIO 32
#define PH_SPAKE2_CHIAVE 64

typedef enum { PH_SPAKE2_ALICE, PH_SPAKE2_BOB } PhSpake2Ruolo; /* alice = PC, bob = telefono */
typedef struct PhSpake2 PhSpake2;

extern const uint8_t PH_SPAKE2_M[32];
extern const uint8_t PH_SPAKE2_N[32];

/* I nomi entrano nel riassunto finale cosi' come sono (con le loro lunghezze). */
PhSpake2 *ph_spake2_crea(PhSpake2Ruolo ruolo, const uint8_t *nome_alice, size_t n_alice,
                         const uint8_t *nome_bob, size_t n_bob);
/* Primo passo: produce il messaggio da mandare. 0 riuscito, -1 errore. */
int ph_spake2_messaggio(PhSpake2 *s, const uint8_t *password, size_t n,
                        uint8_t messaggio[PH_SPAKE2_MESSAGGIO]);
/* Secondo passo: dal messaggio dell'altro ricava la chiave comune.
 * -1 se il messaggio non e' un punto valido della curva. */
int ph_spake2_chiave(PhSpake2 *s, const uint8_t altrui[PH_SPAKE2_MESSAGGIO],
                     uint8_t chiave[PH_SPAKE2_CHIAVE]);
void ph_spake2_libera(PhSpake2 *s);

/* Per i test: genera un punto dal seme come sono stati generati M e N. */
int ph_spake2_genera_punto(const char *seme, uint8_t punto[32]);
/* Per i test: controlli interni dell'aritmetica (punto base, ordine del gruppo). */
int ph_spake2_autoprova(void);

#endif
