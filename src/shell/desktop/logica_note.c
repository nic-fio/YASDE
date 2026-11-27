/* Logica pura del widget Note rapide (logica_note.h). */
#include "logica_note.h"

#include <glib.h>

size_t ph_dsk_note_posto(size_t attuali)
{
    return attuali >= PH_DSK_NOTE_MAX ? 0 : PH_DSK_NOTE_MAX - attuali;
}

size_t ph_dsk_note_filtra(const char *in, size_t n, size_t posto, char *out, size_t cap)
{
    if (out == NULL || cap == 0)
        return 0;
    out[0] = '\0';
    if (in == NULL)
        return 0;
    if (posto > cap - 1)
        posto = cap - 1; /* il buffer di chi chiama e' il limite vero */
    size_t scritti = 0, i = 0;
    while (i < n) {
        /* Con la lunghezza data la lettura non esce dal buffer, nemmeno su un carattere troncato. */
        gunichar c = g_utf8_get_char_validated(in + i, n - i);
        if (c == (gunichar)-1 || c == (gunichar)-2) {
            i++; /* non e' UTF-8 valido: via il byte e si riparte dal successivo */
            continue;
        }
        size_t len = (size_t)g_utf8_skip[(unsigned char)in[i]];
        if (c == 0 || len == 0 || len > n - i) {
            i++; /* un NUL dentro il testo (o una lunghezza incoerente) non entra */
            continue;
        }
        if ((g_unichar_iscntrl(c) && c != '\n' && c != '\t')) {
            i += len;
            continue;
        }
        if (len > posto - scritti)
            break; /* questo non ci sta: il resto non entra */
        for (size_t k = 0; k < len; k++)
            out[scritti + k] = in[i + k];
        scritti += len;
        i += len;
    }
    out[scritti] = '\0';
    return scritti;
}
