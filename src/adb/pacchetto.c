#include "adb/interno.h"

static void scrivi32(uint8_t *d, uint32_t v)
{
    d[0] = (uint8_t)v;
    d[1] = (uint8_t)(v >> 8);
    d[2] = (uint8_t)(v >> 16);
    d[3] = (uint8_t)(v >> 24);
}

static uint32_t leggi32(const uint8_t *d)
{
    return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
}

uint32_t ph_pacchetto_checksum(const void *dati, size_t n)
{
    const uint8_t *p = dati;
    uint32_t somma = 0;
    for (size_t i = 0; i < n; i++)
        somma += p[i];
    return somma;
}

void ph_pacchetto_prepara(PhPacchettoAdb *p, uint32_t comando, uint32_t arg0, uint32_t arg1,
                          const void *dati, size_t n)
{
    p->comando = comando;
    p->arg0 = arg0;
    p->arg1 = arg1;
    p->lunghezza = (uint32_t)n;
    p->checksum = ph_pacchetto_checksum(dati, n);
    p->magic = ~comando;
}

void ph_pacchetto_codifica(const PhPacchettoAdb *p, uint8_t fuori[PH_ADB_INTESTAZIONE])
{
    scrivi32(fuori, p->comando);
    scrivi32(fuori + 4, p->arg0);
    scrivi32(fuori + 8, p->arg1);
    scrivi32(fuori + 12, p->lunghezza);
    scrivi32(fuori + 16, p->checksum);
    scrivi32(fuori + 20, p->magic);
}

int ph_pacchetto_decodifica(const uint8_t dentro[PH_ADB_INTESTAZIONE], PhPacchettoAdb *p,
                            PhErrore *e)
{
    p->comando = leggi32(dentro);
    p->arg0 = leggi32(dentro + 4);
    p->arg1 = leggi32(dentro + 8);
    p->lunghezza = leggi32(dentro + 12);
    p->checksum = leggi32(dentro + 16);
    p->magic = leggi32(dentro + 20);
    if (p->magic != ~p->comando)
        return ph_errore(e, "pacchetto ADB non valido (magic errato)");
    if (p->lunghezza > PH_ADB_MAX_DATI)
        return ph_errore(e, "pacchetto ADB troppo grande (%u byte)", (unsigned)p->lunghezza);
    return 0;
}
