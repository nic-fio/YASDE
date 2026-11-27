/* Logica pura del widget Note rapide: che cosa si accetta del testo che l'utente scrive o incolla
 * e quanto ne resta posto. Niente widget: si prova senza schermo (prova_note.c).
 * Il testo e' un dato dell'utente: nessuna funzione qui lo scrive in un log o in un messaggio. */
#ifndef PH_SHELL_DESKTOP_LOGICA_NOTE_H
#define PH_SHELL_DESKTOP_LOGICA_NOTE_H

#include <stddef.h>

#include "config/config.h"

/* Quanti byte di testo (senza il terminatore) ci stanno nel profilo. */
#define PH_DSK_NOTE_MAX (PH_CFG_LUN_NOTE - 1)

/* Posto che resta a un testo che ha gia' `attuali` byte: mai negativo. */
size_t ph_dsk_note_posto(size_t attuali);

/* Filtra e taglia il testo che sta per entrare nel campo. `in` ha `n` byte (puo' avere dei NUL
 * dentro e non e' detto che sia UTF-8 valido). Si tengono solo i caratteri validi; si scartano:
 *   - i byte che non formano UTF-8 valido (uno alla volta);
 *   - ogni carattere di controllo tranne '\n' e '\t', quindi anche '\r' (un "\r\n" diventa "\n"),
 *     NUL, ESC, DEL e i controlli C1.
 * Il risultato non supera `posto` byte: e' un prefisso del testo filtrato, tagliato a un confine
 * di carattere (si ferma al primo carattere che non ci sta, non salta per metterne uno piu'
 * corto). `out` e' sempre terminato da NUL (cap >= 1) e riceve al massimo cap - 1 byte.
 * Torna i byte scritti (senza il NUL). Mai scritture fuori da `out`, qualunque sia `in`. */
size_t ph_dsk_note_filtra(const char *in, size_t n, size_t posto, char *out, size_t cap);

#endif
