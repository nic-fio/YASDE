/* Parte pura del protocollo: scrittura e lettura dei messaggi (docs/protocollo.md, 2). */
#include <string.h>

#include "componente/interno.h"

/* Il messaggio occupa tutto il buffer: ph_msg_inizia lo svuota, cosi' la
 * lunghezza si corregge sempre al byte 1 senza dover ricordare dove si era. */
int ph_msg_inizia(PhBuffer *b, uint8_t tipo)
{
    ph_buffer_svuota(b);
    const uint8_t testa[5] = {tipo, 0, 0, 0, 0};
    return ph_buffer_aggiungi(b, testa, sizeof testa);
}

int ph_msg_u8(PhBuffer *b, uint8_t v)
{
    return ph_buffer_aggiungi(b, &v, 1);
}

int ph_msg_u16(PhBuffer *b, uint16_t v)
{
    const uint8_t d[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    return ph_buffer_aggiungi(b, d, sizeof d);
}

int ph_msg_i16(PhBuffer *b, int16_t v)
{
    return ph_msg_u16(b, (uint16_t)v);
}

int ph_msg_u32(PhBuffer *b, uint32_t v)
{
    const uint8_t d[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    return ph_buffer_aggiungi(b, d, sizeof d);
}

int ph_msg_stringa(PhBuffer *b, const char *s)
{
    if (s == NULL)
        s = "";
    size_t n = strlen(s);
    if (n > 0xffff)
        return -1;
    if (ph_msg_u16(b, (uint16_t)n) != 0)
        return -1;
    return ph_buffer_aggiungi(b, s, n);
}

int ph_msg_finisci(PhBuffer *b)
{
    if (b->lunghezza < 5 || b->dati == NULL)
        return -1;
    size_t corpo = b->lunghezza - 5;
    if (corpo > PH_MSG_MAX_CORPO)
        return -1;
    b->dati[1] = (uint8_t)(corpo >> 24);
    b->dati[2] = (uint8_t)(corpo >> 16);
    b->dati[3] = (uint8_t)(corpo >> 8);
    b->dati[4] = (uint8_t)corpo;
    return 0;
}

/* Prende n byte dal cursore; se non ci sono il lettore si rompe e non avanza piu'. */
static const uint8_t *prendi(PhLettore *l, size_t n)
{
    if (l->rotto || l->dati == NULL || l->posizione > l->lunghezza || n > l->lunghezza - l->posizione) {
        l->rotto = true;
        return NULL;
    }
    const uint8_t *p = l->dati + l->posizione;
    l->posizione += n;
    return p;
}

uint8_t ph_leggi_u8(PhLettore *l)
{
    const uint8_t *p = prendi(l, 1);
    return p ? p[0] : 0;
}

uint16_t ph_leggi_u16(PhLettore *l)
{
    const uint8_t *p = prendi(l, 2);
    return p ? (uint16_t)((p[0] << 8) | p[1]) : 0;
}

uint32_t ph_leggi_u32(PhLettore *l)
{
    const uint8_t *p = prendi(l, 4);
    if (p == NULL)
        return 0;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

uint64_t ph_leggi_u64(PhLettore *l)
{
    const uint8_t *p = prendi(l, 8);
    if (p == NULL)
        return 0;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v = (v << 8) | p[i];
    return v;
}

void ph_leggi_stringa(PhLettore *l, char *uscita, size_t capacita)
{
    if (uscita != NULL && capacita > 0)
        uscita[0] = '\0';
    size_t n = ph_leggi_u16(l);
    const uint8_t *p = prendi(l, n);
    if (p == NULL || uscita == NULL || capacita == 0)
        return;
    size_t copia = n < capacita - 1 ? n : capacita - 1;
    memcpy(uscita, p, copia);
    uscita[copia] = '\0';
}
