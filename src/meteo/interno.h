/* Interfaccia interna del pezzo meteo (non e' contratto verso gli altri pezzi). */
#ifndef PH_METEO_INTERNO_H
#define PH_METEO_INTERNO_H

#include <stdatomic.h>

#include "meteo.h"
#include "tls.h"

/* Orologio monotono in millisecondi (sempre quello vero: serve alle scadenze
 * di rete, che non devono dipendere dall'orologio finto delle prove). */
int64_t ph_meteo_ms(void);

/* Orologi della regola di frequenza e dell'«ora adesso»: secondi monotoni e
 * secondi Unix. Per le prove si sostituiscono (NULL = quelli veri); si
 * impostano quando nessun PhMeteo e' attivo. */
int64_t ph_meteo_mono_s(void);
int64_t ph_meteo_unix_s(void);
void ph_meteo_prova_orologio(int64_t (*mono_s)(void), int64_t (*unix_s)(void));
/* Intervallo minimo tra l'avvio di due collegamenti (millisecondi reali). */
#define PH_METEO_AVVII_MIN_MS 5000
/* Per le prove: < 0 = predefinito, 0 = nessun intervallo. */
void ph_meteo_prova_avvii_ms(int64_t ms);
/* Limite totale di una richiesta (default PH_METEO_LIMITE_S): per le prove. 0 = default. */
void ph_meteo_prova_limite_ms(int64_t ms);
int64_t ph_meteo_limite_ms(void);

/* HTTP/1.1 minimo: GET `percorso` verso `host` (porta PH_METEO_PORTA), corpo al
 * massimo `max_corpo` byte. Su 0 *corpo e' un buffer malloc (NUL finale non
 * contato in *n) da liberare; su -1 nulla da liberare. */
int ph_meteo_http_scarica(const char *host, const char *percorso, size_t max_corpo, const atomic_int *annulla,
                          int64_t scadenza_ms, char **corpo, size_t *n, PhErrore *e);

/* Testo di ricerca di un luogo: toglie gli spazi ai lati e controlla (da 2 a
 * PH_METEO_RICERCA_TESTO_MAX byte, UTF-8 valido, nessun carattere di
 * controllo). out: almeno PH_METEO_RICERCA_TESTO_MAX + 1 byte. 0 / -1. Gli
 * errori NON contengono il testo. */
int ph_meteo_testo_ricerca(const char *testo, char *out, PhErrore *e);

/* Per le prove: quante volte il thread della ricerca si e' messo ad aspettare
 * la pausa minima tra due richieste. */
int ph_meteo_prova_attese_ricerca(PhMeteo *m);
/* Pausa tra due richieste di ricerca in millisecondi reali: < 0 = predefinita, 0 = nessuna. */
void ph_meteo_prova_pausa_ricerca_ms(int64_t ms);

#endif
