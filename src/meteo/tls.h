/* Connessione cifrata (TLS) per il pezzo meteo: INTERFACCIA INTERNA al pezzo.
 * Esistono due realizzazioni con le stesse funzioni:
 *  - tls.c: quella VERA (oggi uno scheletro che torna sempre errore: la scrive
 *    chi coordina, e' codice di sicurezza);
 *  - prova/tls_finto.c: quella FINTA delle prove, in chiaro su 127.0.0.1.
 * Tutto il resto del pezzo (HTTP, JSON, frequenza) parla solo con queste
 * funzioni, cosi' si prova senza rete.
 *
 * Convenzioni comuni:
 *  - `scadenza_ms` e' un istante ASSOLUTO dell'orologio monotono
 *    (CLOCK_MONOTONIC, in millisecondi): vale per tutta l'operazione;
 *  - `annulla` (anche NULL) e' un flag: se diventa diverso da 0 l'operazione
 *    in corso deve tornare errore entro 250 ms, in ogni fase (risoluzione del
 *    nome, collegamento, handshake, scrittura, lettura);
 *  - gli errori scrivono in `e` una frase SENZA host, indirizzi o coordinate
 *    («il servizio meteo non risponde», «certificato non valido», ...);
 *  - nessuna funzione tiene lucchetti durante la rete; un PhTls e' usato da un
 *    thread alla volta. */
#ifndef PH_METEO_TLS_H
#define PH_METEO_TLS_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "comune/errore.h"

typedef struct PhTls PhTls;

/* Risolve `host`, si collega a host:porta e completa l'handshake TLS entro
 * `scadenza_ms`. NULL = errore (nulla da chiudere). */
PhTls *ph_tls_apri(const char *host, uint16_t porta, int64_t scadenza_ms, const atomic_int *annulla, PhErrore *e);

/* Scrive TUTTI gli `n` byte (o torna errore). 0 / -1. */
int ph_tls_scrivi(PhTls *t, const void *dati, size_t n, int64_t scadenza_ms, const atomic_int *annulla, PhErrore *e);

/* Legge da 1 a `max` byte (max > 0). Valore di ritorno: > 0 byte letti; 0 =
 * fine PULITA del flusso (con TLS: close_notify ricevuta; una chiusura senza
 * close_notify e' un errore, cosi' una risposta troncata non passa per
 * completa); -1 = errore, scadenza o annullamento. */
ssize_t ph_tls_leggi(PhTls *t, void *buf, size_t max, int64_t scadenza_ms, const atomic_int *annulla, PhErrore *e);

/* Chiude (invia close_notify se puo', senza aspettare) e libera. NULL ammesso. */
void ph_tls_chiudi(PhTls *t);

/* Dopo un -1 di scrivi o leggi il PhTls si puo' solo chiudere.
 *
 * SOLO PER LE PROVE della realizzazione vera: da qui in poi le connessioni
 * verificano il certificato contro il solo file PEM dato (una autorita' di
 * prova) invece che contro quelle del sistema. La stringa deve restare valida;
 * NULL torna al comportamento normale. Va chiamata prima di aprire connessioni,
 * non in parallelo con ph_tls_apri. Il programma vero non la chiama mai; la
 * realizzazione finta delle prove la definisce vuota. */
void ph_tls_prova_ancore(const char *file_pem);

#endif
