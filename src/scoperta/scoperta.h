/* Scoperta dei telefoni in rete con mDNS (DNS-SD), scritta da zero: nessun
 * avahi, nessuna libreria. Cerca i servizi che Android annuncia con il debug
 * wireless acceso:
 *   _adb-tls-connect._tcp.local   porta a cui collegarsi
 *   _adb-tls-pairing._tcp.local   porta dell'abbinamento con codice (esiste
 *                                 solo mentre il telefono mostra il codice)
 * Specifica 1.5 e 2.4. Non dipende dagli altri pezzi (solo src/comune). */
#ifndef PH_SCOPERTA_H
#define PH_SCOPERTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comune/errore.h"

typedef struct {
    char nome[128];   /* nome dell'istanza, es. "adb-R5CT0000000-AbCdEf" */
    char indirizzo[64]; /* IPv4 o IPv6 in testo */
    uint16_t porta;
    bool abbinamento; /* true = servizio di abbinamento, false = collegamento */
} PhTelefonoTrovato;

/* Manda le domande su tutte le interfacce di rete attive (IPv4; IPv6 se
 * semplice), raccoglie le risposte per durata_ms e riempie trovati[] (al
 * massimo `massimo` voci, senza doppioni). Ripete la domanda a intervalli
 * crescenti: i telefoni con il Wi-Fi in risparmio rispondono tardi.
 * Torna il numero di voci (0 = nessun telefono: non e' un errore), -1 errore. */
int ph_scoperta_cerca(int durata_ms, PhTelefonoTrovato *trovati, size_t massimo, PhErrore *e);

/* Parte pura, provabile senza rete: legge un pacchetto DNS di risposta e ne
 * estrae i telefoni (record PTR + SRV + A/AAAA, anche compressi). Torna il
 * numero di voci aggiunte a trovati[] partendo da *numero. Un pacchetto
 * malformato o ostile non deve mai far leggere fuori dal buffer: torna 0. */
size_t ph_scoperta_leggi_risposta(const uint8_t *pacchetto, size_t lunghezza,
                                  PhTelefonoTrovato *trovati, size_t *numero, size_t massimo);

/* Costruisce la domanda mDNS (QU al primo invio) per i due servizi.
 * Torna la lunghezza scritta in pacchetto, 0 se capacita insufficiente. */
size_t ph_scoperta_domanda(uint8_t *pacchetto, size_t capacita, bool prima);

#endif
