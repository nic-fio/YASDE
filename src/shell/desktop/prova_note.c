/* Prove di logica delle Note rapide (logica_note.h), SENZA schermo: che cosa si accetta del testo
 * scritto o incollato e quanto ne resta posto. */
#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "logica_note.h"

static int errori = 0;
#define VERIFICA(cond)                                                                       \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            fprintf(stderr, "%s:%d: fallita: %s\n", __FILE__, __LINE__, #cond);              \
            errori++;                                                                        \
        }                                                                                    \
    } while (0)

/* Filtra in un buffer abbastanza grande e torna il risultato come stringa. */
static char g_out[8192];
static const char *filtra(const char *in, size_t n, size_t posto)
{
    size_t k = ph_dsk_note_filtra(in, n, posto, g_out, sizeof g_out);
    VERIFICA(k == strlen(g_out));
    return g_out;
}

static void prova_posto(void)
{
    VERIFICA(PH_DSK_NOTE_MAX == PH_CFG_LUN_NOTE - 1);
    VERIFICA(ph_dsk_note_posto(0) == PH_DSK_NOTE_MAX);
    VERIFICA(ph_dsk_note_posto(10) == PH_DSK_NOTE_MAX - 10);
    VERIFICA(ph_dsk_note_posto(PH_DSK_NOTE_MAX) == 0);
    VERIFICA(ph_dsk_note_posto(PH_DSK_NOTE_MAX + 1) == 0);
    VERIFICA(ph_dsk_note_posto((size_t)-1) == 0);
}

static void prova_caratteri(void)
{
    /* Il testo normale passa com'e', con a capo e tabulazione. */
    VERIFICA(strcmp(filtra("ciao", 4, 100), "ciao") == 0);
    VERIFICA(strcmp(filtra("a\nb\tc", 5, 100), "a\nb\tc") == 0);
    VERIFICA(strcmp(filtra("", 0, 100), "") == 0);
    /* \r\n diventa \n; un \r da solo si toglie. */
    VERIFICA(strcmp(filtra("a\r\nb\r\nc", 7, 100), "a\nb\nc") == 0);
    VERIFICA(strcmp(filtra("a\rb", 3, 100), "ab") == 0);
    /* Gli altri controlli: NUL dentro il testo, ESC, DEL, campanello, C1 (U+0085). */
    VERIFICA(strcmp(filtra("a\0b", 3, 100), "ab") == 0);
    VERIFICA(strcmp(filtra("a\x1b[0mb", 6, 100), "a[0mb") == 0);
    VERIFICA(strcmp(filtra("a\x7f" "b\x07" "c", 5, 100), "abc") == 0);
    VERIFICA(strcmp(filtra("a\xc2\x85" "b", 4, 100), "ab") == 0);
    /* Caratteri a piu' byte non toccati. */
    VERIFICA(strcmp(filtra("\xc3\xa8 \xe2\x82\xac \xf0\x9f\x98\x80", 11, 100), "\xc3\xa8 \xe2\x82\xac \xf0\x9f\x98\x80") == 0);
    /* UTF-8 non valido: via un byte alla volta, il resto resta. */
    VERIFICA(strcmp(filtra("a\xff" "b", 3, 100), "ab") == 0);
    VERIFICA(strcmp(filtra("a\x80" "b", 3, 100), "ab") == 0);            /* continuazione senza testa */
    VERIFICA(strcmp(filtra("a\xc3" "b", 3, 100), "ab") == 0);            /* testa senza continuazione */
    VERIFICA(strcmp(filtra("a\xc0\xaf" "b", 4, 100), "ab") == 0);        /* sovralunga */
    VERIFICA(strcmp(filtra("a\xed\xa0\x80" "b", 5, 100), "ab") == 0);    /* surrogato */
    VERIFICA(strcmp(filtra("ab\xe2\x82", 4, 100), "ab") == 0);           /* carattere troncato in fondo */
    VERIFICA(strcmp(filtra("ab\xf4\x90\x80\x80", 6, 100), "ab") == 0);   /* oltre U+10FFFF */
    /* La lunghezza data comanda, non il NUL: il resto oltre n non si legge. */
    VERIFICA(strcmp(filtra("abcdef", 3, 100), "abc") == 0);
    /* Il risultato e' sempre UTF-8 valido. */
    const char *sporco = "x\xc3\xa8\xe2\x82\xff\xf0\x9f\x98\x80\xc3";
    filtra(sporco, strlen(sporco), 100);
    VERIFICA(g_utf8_validate(g_out, -1, NULL));
}

static void prova_limite(void)
{
    /* Taglio a un confine di carattere: due byte per «è», tre per «€», quattro per la faccina. */
    VERIFICA(strcmp(filtra("\xc3\xa8\xc3\xa8\xc3\xa8", 6, 5), "\xc3\xa8\xc3\xa8") == 0);        /* 4 byte */
    VERIFICA(strcmp(filtra("\xc3\xa8\xc3\xa8", 4, 4), "\xc3\xa8\xc3\xa8") == 0);                /* esatto */
    VERIFICA(strcmp(filtra("\xe2\x82\xac\xe2\x82\xac", 6, 5), "\xe2\x82\xac") == 0);            /* 3 byte */
    VERIFICA(strcmp(filtra("\xf0\x9f\x98\x80", 4, 3), "") == 0);                                 /* non ci sta */
    VERIFICA(strcmp(filtra("ab\xf0\x9f\x98\x80" "c", 7, 5), "ab") == 0);                         /* niente salti */
    VERIFICA(strcmp(filtra("abc", 3, 0), "") == 0);                                              /* nessun posto */
    VERIFICA(ph_dsk_note_filtra("abc", 3, 2, g_out, sizeof g_out) == 2 && strcmp(g_out, "ab") == 0);
    /* Si conta il testo che resta DOPO il filtro: gli scartati non usano posto. */
    VERIFICA(strcmp(filtra("a\r\nb\r\nc", 7, 3), "a\nb") == 0);
    VERIFICA(strcmp(filtra("\x01\x01\x01" "ab", 5, 2), "ab") == 0);

    /* Incolla enorme: si ferma al limite del profilo, senza uscire dal buffer. */
    char *enorme = g_malloc(200000);
    memset(enorme, 'a', 199999);
    enorme[199999] = '\0';
    char out[PH_CFG_LUN_NOTE];
    size_t k = ph_dsk_note_filtra(enorme, 199999, PH_DSK_NOTE_MAX, out, sizeof out);
    VERIFICA(k == PH_DSK_NOTE_MAX && strlen(out) == PH_DSK_NOTE_MAX);
    /* Enorme di caratteri a due byte: il limite e' dispari, quindi un byte resta vuoto. */
    for (int i = 0; i < 100000; i++)
        memcpy(enorme + 2 * i, "\xc3\xa8", 2);
    k = ph_dsk_note_filtra(enorme, 200000, PH_DSK_NOTE_MAX, out, sizeof out);
    VERIFICA(k == PH_DSK_NOTE_MAX - 1 && g_utf8_validate(out, -1, NULL));
    g_free(enorme);

    /* Il buffer di chi chiama e' il limite vero, anche se `posto` e' piu' grande. */
    char piccolo[6];
    k = ph_dsk_note_filtra("abcdefghij", 10, PH_DSK_NOTE_MAX, piccolo, sizeof piccolo);
    VERIFICA(k == 5 && strcmp(piccolo, "abcde") == 0);
    char uno[1] = {'x'};
    VERIFICA(ph_dsk_note_filtra("abc", 3, 100, uno, sizeof uno) == 0 && uno[0] == '\0');
    VERIFICA(ph_dsk_note_filtra("abc", 3, 100, NULL, 10) == 0);
    VERIFICA(ph_dsk_note_filtra("abc", 3, 100, g_out, 0) == 0);
    VERIFICA(ph_dsk_note_filtra(NULL, 3, 100, g_out, sizeof g_out) == 0 && g_out[0] == '\0');
    /* Dopo il taglio il resto del buffer non e' toccato e c'e' sempre il terminatore. */
    char guardia[8];
    memset(guardia, 'Z', sizeof guardia);
    ph_dsk_note_filtra("abcdef", 6, 3, guardia, 4);
    VERIFICA(memcmp(guardia, "abc\0ZZZZ", 8) == 0);

    /* Idempotente: rifiltrare il risultato non cambia nulla (serve alla ricorsione dell'inserimento). */
    const char *misto = "a\r\nb\x01\xc3\xa8\t\xff" "z";
    char una[64], due[64];
    ph_dsk_note_filtra(misto, strlen(misto), 100, una, sizeof una);
    ph_dsk_note_filtra(una, strlen(una), 100, due, sizeof due);
    VERIFICA(strcmp(una, due) == 0);
}

int main(void)
{
    prova_posto();
    prova_caratteri();
    prova_limite();
    if (errori == 0)
        printf("prova_note: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
