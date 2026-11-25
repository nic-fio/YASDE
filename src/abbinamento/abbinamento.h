/* Abbinamento con codice a sei cifre ("Associa dispositivo con codice").
 * Codice di sicurezza: lo scrive la sessione principale, non un programmatore. */
#ifndef PH_ABBINAMENTO_H
#define PH_ABBINAMENTO_H

#include <stdint.h>

#include "adb/adb.h"
#include "comune/errore.h"

/* host:porta sono quelli del servizio di abbinamento (non del collegamento).
 * Riuscito = il telefono ha registrato la chiave pubblica del PC. */
int ph_abbinamento_esegui(const char *host, uint16_t porta, const char *codice,
                          const PhChiave *chiave, int timeout_ms, PhErrore *e);

#endif
