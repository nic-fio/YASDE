/* Parti interne del client ADB: pacchetti del protocollo. Non fa parte del
 * contratto (adb.h); lo includono solo i file di questa cartella e i test. */
#ifndef PH_ADB_INTERNO_H
#define PH_ADB_INTERNO_H

#include <stddef.h>
#include <stdint.h>

#include "comune/errore.h"

/* Comandi: le quattro lettere ASCII lette come intero little endian. */
#define PH_ADB_CNXN 0x4e584e43u
#define PH_ADB_AUTH 0x48545541u
#define PH_ADB_STLS 0x534c5453u
#define PH_ADB_OPEN 0x4e45504fu
#define PH_ADB_OKAY 0x59414b4fu
#define PH_ADB_WRTE 0x45545257u
#define PH_ADB_CLSE 0x45534c43u

/* Versione 0x01000001: il checksum dei dati non e' piu' controllato (la
 * integrita' la da' il TLS); lo calcoliamo comunque, costa poco. */
#define PH_ADB_VERSIONE 0x01000001u
#define PH_ADB_VERSIONE_STLS 0x01000000u
#define PH_ADB_MAX_DATI (1024u * 1024u)
#define PH_ADB_INTESTAZIONE 24

typedef struct {
    uint32_t comando;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t lunghezza;
    uint32_t checksum;
    uint32_t magic;
} PhPacchettoAdb;

/* Somma dei byte, come richiede il protocollo. */
uint32_t ph_pacchetto_checksum(const void *dati, size_t n);
/* Riempie p (checksum e magic compresi) per un pacchetto con n byte di dati. */
void ph_pacchetto_prepara(PhPacchettoAdb *p, uint32_t comando, uint32_t arg0, uint32_t arg1,
                          const void *dati, size_t n);
/* Intestazione da 24 byte, little endian. */
void ph_pacchetto_codifica(const PhPacchettoAdb *p, uint8_t fuori[PH_ADB_INTESTAZIONE]);
/* Decodifica; -1 se il magic non e' il complemento del comando o se la
 * lunghezza supera PH_ADB_MAX_DATI (un telefono non deve poterci far
 * allocare o leggere di piu'). */
int ph_pacchetto_decodifica(const uint8_t dentro[PH_ADB_INTESTAZIONE], PhPacchettoAdb *p,
                            PhErrore *e);

#endif
