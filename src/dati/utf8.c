/* Pulizia delle stringhe che arrivano dal telefono. */
#include <stdlib.h>
#include <string.h>

#include "dati/interno.h"

/* Lunghezza della sequenza UTF-8 ben formata all'inizio di s (0 = non valida).
 * Tabella di Unicode 3.9 (Table 3-7): niente forme troppo lunghe, niente
 * surrogati, niente oltre U+10FFFF. */
static size_t sequenza_valida(const uint8_t *s, size_t n)
{
    uint8_t b = s[0];
    if (b >= 0xc2 && b <= 0xdf)
        return (n >= 2 && (s[1] & 0xc0) == 0x80) ? 2 : 0;
    if (b >= 0xe0 && b <= 0xef) {
        if (n < 3)
            return 0;
        uint8_t lo = 0x80, hi = 0xbf;
        if (b == 0xe0)
            lo = 0xa0;
        else if (b == 0xed)
            hi = 0x9f;
        if (s[1] < lo || s[1] > hi || (s[2] & 0xc0) != 0x80)
            return 0;
        return 3;
    }
    if (b >= 0xf0 && b <= 0xf4) {
        if (n < 4)
            return 0;
        uint8_t lo = 0x80, hi = 0xbf;
        if (b == 0xf0)
            lo = 0x90;
        else if (b == 0xf4)
            hi = 0x8f;
        if (s[1] < lo || s[1] > hi || (s[2] & 0xc0) != 0x80 || (s[3] & 0xc0) != 0x80)
            return 0;
        return 4;
    }
    return 0;
}

bool ph_dati_utf8_valido(const uint8_t *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        if (s[i] == 0)
            return false;
        if (s[i] < 0x80) {
            i++;
            continue;
        }
        size_t v = sequenza_valida(s + i, n - i);
        if (v == 0)
            return false;
        i += v;
    }
    return true;
}

/* Controlli C1 (U+0080-U+009F) e separatori di riga U+2028/U+2029. */
static bool controllo_unicode(const uint8_t *s, size_t v)
{
    if (v == 2)
        return s[0] == 0xc2 && s[1] < 0xa0;
    return v == 3 && s[0] == 0xe2 && s[1] == 0x80 && (s[2] == 0xa8 || s[2] == 0xa9);
}

char *ph_dati_pulisci(const uint8_t *s, size_t n, size_t massimo, int modo)
{
    /* Nel caso peggiore ogni byte non valido diventa 3 byte (U+FFFD). */
    size_t tetto = n <= SIZE_MAX / 3 - 1 ? n * 3 : SIZE_MAX / 3 - 1;
    if (massimo < tetto)
        tetto = massimo;
    char *u = malloc(tetto + 1);
    if (u == NULL)
        return NULL;
    static const char sostituto[3] = {(char)0xef, (char)0xbf, (char)0xbd};
    size_t o = 0, i = 0;
    while (i < n) {
        uint8_t b = s[i];
        const char *pezzo = sostituto;
        size_t lp = 3, avanza = 1;
        char uno = 0;
        if (b < 0x80) {
            bool controllo = b < 0x20 || b == 0x7f;
            bool tieni = true;
            if (controllo) {
                if (modo == PH_PUL_RIGA) {
                    uno = ' ';
                    tieni = false;
                } else if (modo == PH_PUL_APPUNTI) {
                    tieni = b != 0;
                } else {
                    tieni = b == '\n' || b == '\r' || b == '\t';
                }
            }
            if (tieni && uno == 0) {
                uno = (char)b;
                pezzo = &uno;
                lp = 1;
            } else if (uno == ' ') {
                pezzo = &uno;
                lp = 1;
            }
        } else {
            size_t v = sequenza_valida(s + i, n - i);
            if (v != 0) {
                if (modo == PH_PUL_RIGA && controllo_unicode(s + i, v)) {
                    uno = ' ';
                    pezzo = &uno;
                    lp = 1;
                } else {
                    pezzo = (const char *)s + i;
                    lp = v;
                }
                avanza = v;
            }
        }
        if (lp > tetto - o)
            break; /* il carattere non ci sta: si tronca prima, mai a meta' */
        memcpy(u + o, pezzo, lp);
        o += lp;
        i += avanza;
    }
    u[o] = '\0';
    return u;
}
