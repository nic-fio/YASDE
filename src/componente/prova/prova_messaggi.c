/* Prova della parte pura: byte attesi per ogni messaggio del protocollo e
 * lettore che non esce mai dal buffer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "componente/componente.h"

#define VERIFICA(x)                                                                      \
    do {                                                                                 \
        if (!(x)) {                                                                      \
            fprintf(stderr, "%s:%d: verifica fallita: %s\n", __FILE__, __LINE__, #x);    \
            exit(1);                                                                     \
        }                                                                                \
    } while (0)

static void uguali(const PhBuffer *b, const uint8_t *atteso, size_t n)
{
    VERIFICA(b->lunghezza == n);
    VERIFICA(memcmp(b->dati, atteso, n) == 0);
}

static void prova_scrittura(void)
{
    PhBuffer b = PH_BUFFER_VUOTO;

    VERIFICA(ph_msg_inizia(&b, PH_MSG_SALUTO) == 0 && ph_msg_u16(&b, 1) == 0 && ph_msg_finisci(&b) == 0);
    uguali(&b, (const uint8_t[]){0x01, 0, 0, 0, 2, 0, 1}, 7);

    VERIFICA(ph_msg_inizia(&b, PH_MSG_CHIUDI) == 0 && ph_msg_finisci(&b) == 0);
    uguali(&b, (const uint8_t[]){0x04, 0, 0, 0, 0}, 5);

    VERIFICA(ph_msg_inizia(&b, PH_MSG_VIDEO_APRI) == 0);
    VERIFICA(ph_msg_u32(&b, 0x01020304) == 0 && ph_msg_u16(&b, 1080) == 0 && ph_msg_u16(&b, 2336) == 0 &&
             ph_msg_u16(&b, 420) == 0 && ph_msg_u32(&b, 8000000) == 0 && ph_msg_u8(&b, 60) == 0 &&
             ph_msg_stringa(&b, "com.a") == 0 && ph_msg_finisci(&b) == 0);
    uguali(&b,
           (const uint8_t[]){0x40, 0, 0, 0, 22, 1, 2, 3, 4, 0x04, 0x38, 0x09, 0x20, 0x01, 0xa4, 0x00, 0x7a,
                             0x12, 0x00, 60, 0, 5, 'c', 'o', 'm', '.', 'a'},
           27);

    VERIFICA(ph_msg_inizia(&b, PH_MSG_VIDEO_CHIUDI) == 0 && ph_msg_u32(&b, 7) == 0 && ph_msg_u8(&b, 1) == 0 &&
             ph_msg_finisci(&b) == 0);
    uguali(&b, (const uint8_t[]){0x41, 0, 0, 0, 5, 0, 0, 0, 7, 1}, 10);

    VERIFICA(ph_msg_inizia(&b, PH_MSG_VIDEO_RIDIMENSIONA) == 0 && ph_msg_u32(&b, 7) == 0 &&
             ph_msg_u16(&b, 640) == 0 && ph_msg_u16(&b, 480) == 0 && ph_msg_u16(&b, 320) == 0 &&
             ph_msg_finisci(&b) == 0);
    uguali(&b, (const uint8_t[]){0x42, 0, 0, 0, 10, 0, 0, 0, 7, 0x02, 0x80, 0x01, 0xe0, 0x01, 0x40}, 15);

    VERIFICA(ph_msg_inizia(&b, PH_MSG_VIDEO_CHIAVE) == 0 && ph_msg_u32(&b, 9) == 0 && ph_msg_finisci(&b) == 0);
    uguali(&b, (const uint8_t[]){0x43, 0, 0, 0, 4, 0, 0, 0, 9}, 9);

    /* TOCCO: id, larghezza, altezza, azione, x, y, pulsanti */
    VERIFICA(ph_msg_inizia(&b, PH_MSG_TOCCO) == 0 && ph_msg_u32(&b, 3) == 0 && ph_msg_u16(&b, 720) == 0 &&
             ph_msg_u16(&b, 1280) == 0 && ph_msg_u8(&b, PH_TOCCO_MUOVI) == 0 && ph_msg_u16(&b, 100) == 0 &&
             ph_msg_u16(&b, 200) == 0 && ph_msg_u8(&b, 5) == 0 && ph_msg_finisci(&b) == 0);
    uguali(&b,
           (const uint8_t[]){0x50, 0, 0, 0, 14, 0, 0, 0, 3, 0x02, 0xd0, 0x05, 0x00, 1, 0, 100, 0, 200, 5}, 19);

    /* ROTELLA con valori negativi: complemento a due, big endian */
    VERIFICA(ph_msg_inizia(&b, PH_MSG_ROTELLA) == 0 && ph_msg_u32(&b, 3) == 0 && ph_msg_u16(&b, 720) == 0 &&
             ph_msg_u16(&b, 1280) == 0 && ph_msg_u16(&b, 10) == 0 && ph_msg_u16(&b, 20) == 0 &&
             ph_msg_i16(&b, -120) == 0 && ph_msg_i16(&b, 240) == 0 && ph_msg_finisci(&b) == 0);
    uguali(&b,
           (const uint8_t[]){0x51, 0, 0, 0, 16, 0, 0, 0, 3, 0x02, 0xd0, 0x05, 0x00, 0, 10, 0, 20, 0xff, 0x88,
                             0x00, 0xf0},
           21);

    VERIFICA(ph_msg_inizia(&b, PH_MSG_TASTO) == 0 && ph_msg_u32(&b, 3) == 0 && ph_msg_u8(&b, 1) == 0 &&
             ph_msg_u32(&b, 66) == 0 && ph_msg_u32(&b, 0x1000) == 0 && ph_msg_finisci(&b) == 0);
    uguali(&b, (const uint8_t[]){0x52, 0, 0, 0, 13, 0, 0, 0, 3, 1, 0, 0, 0, 66, 0, 0, 0x10, 0}, 18);

    VERIFICA(ph_msg_inizia(&b, PH_MSG_TESTO) == 0 && ph_msg_u32(&b, 3) == 0 && ph_msg_stringa(&b, "\xc3\xa8!") == 0 &&
             ph_msg_finisci(&b) == 0);
    uguali(&b, (const uint8_t[]){0x53, 0, 0, 0, 9, 0, 0, 0, 3, 0, 3, 0xc3, 0xa8, '!'}, 14);

    VERIFICA(ph_msg_inizia(&b, PH_MSG_INDIETRO) == 0 && ph_msg_u32(&b, 3) == 0 && ph_msg_finisci(&b) == 0);
    uguali(&b, (const uint8_t[]){0x54, 0, 0, 0, 4, 0, 0, 0, 3}, 9);

    /* PIZZICO: id, larghezza, altezza, fase, x, y, passo (negativo in complemento a due) */
    VERIFICA(ph_msg_inizia(&b, PH_MSG_PIZZICO) == 0 && ph_msg_u32(&b, 3) == 0 && ph_msg_u16(&b, 720) == 0 &&
             ph_msg_u16(&b, 1280) == 0 && ph_msg_u8(&b, PH_PIZZICO_CONTINUA) == 0 && ph_msg_u16(&b, 300) == 0 &&
             ph_msg_u16(&b, 400) == 0 && ph_msg_i16(&b, -1200) == 0 && ph_msg_finisci(&b) == 0);
    uguali(&b,
           (const uint8_t[]){0x55, 0, 0, 0, 15, 0, 0, 0, 3, 0x02, 0xd0, 0x05, 0x00, 1, 0x01, 0x2c, 0x01, 0x90,
                             0xfb, 0x50},
           20);

    /* stringa vuota e NULL */
    VERIFICA(ph_msg_inizia(&b, PH_MSG_TESTO) == 0 && ph_msg_stringa(&b, "") == 0 && ph_msg_stringa(&b, NULL) == 0 &&
             ph_msg_finisci(&b) == 0);
    uguali(&b, (const uint8_t[]){0x53, 0, 0, 0, 4, 0, 0, 0, 0}, 9);

    /* limiti: stringa oltre 65535 byte, corpo oltre 1 MiB */
    char *lunga = malloc(70000);
    VERIFICA(lunga != NULL);
    memset(lunga, 'a', 69999);
    lunga[69999] = '\0';
    VERIFICA(ph_msg_inizia(&b, PH_MSG_TESTO) == 0);
    VERIFICA(ph_msg_stringa(&b, lunga) == -1);
    free(lunga);
    VERIFICA(ph_msg_inizia(&b, PH_MSG_TESTO) == 0);
    for (int i = 0; i < 16; i++) {
        uint8_t blocco[65536];
        memset(blocco, 'x', sizeof blocco);
        VERIFICA(ph_buffer_aggiungi(&b, blocco, sizeof blocco) == 0);
    }
    VERIFICA(ph_msg_finisci(&b) == 0); /* esattamente 1 MiB: ammesso */
    VERIFICA(ph_msg_u8(&b, 1) == 0);
    VERIFICA(ph_msg_finisci(&b) == -1); /* 1 MiB + 1: no */
    ph_buffer_libera(&b);
}

static void prova_lettura(void)
{
    const uint8_t d[] = {1, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x00, 0x03, 'a', 'b', 'c'};
    PhLettore l = {d, sizeof d, 0, false};
    VERIFICA(ph_leggi_u8(&l) == 1);
    VERIFICA(ph_leggi_u16(&l) == 0x0203);
    VERIFICA(ph_leggi_u32(&l) == 0x04050607);
    char s[8];
    /* u64 su 8 byte: 08 09 0a 00 03 'a' 'b' 'c' */
    PhLettore l2 = {d + 7, 8, 0, false};
    VERIFICA(ph_leggi_u64(&l2) == 0x08090a0003616263ULL && !l2.rotto);
    VERIFICA(ph_leggi_u8(&l2) == 0 && l2.rotto);

    PhLettore l3 = {d + 10, 5, 0, false};
    ph_leggi_stringa(&l3, s, sizeof s);
    VERIFICA(strcmp(s, "abc") == 0 && !l3.rotto && l3.posizione == 5);

    /* troncamento: la stringa si copia a pezzi ma il cursore salta tutta */
    PhLettore l4 = {d + 10, 5, 0, false};
    char piccolo[3];
    ph_leggi_stringa(&l4, piccolo, sizeof piccolo);
    VERIFICA(strcmp(piccolo, "ab") == 0 && !l4.rotto && l4.posizione == 5);

    /* lunghezza falsa: dichiara 3 byte, ne restano 2 */
    PhLettore l5 = {d + 10, 4, 0, false};
    s[0] = 'x';
    ph_leggi_stringa(&l5, s, sizeof s);
    VERIFICA(s[0] == '\0' && l5.rotto);

    /* lettore rotto: tutto torna 0 e il cursore non si muove */
    size_t pos = l5.posizione;
    VERIFICA(ph_leggi_u8(&l5) == 0 && ph_leggi_u32(&l5) == 0 && l5.posizione == pos);

    /* buffer vuoto e NULL */
    PhLettore l6 = {NULL, 0, 0, false};
    VERIFICA(ph_leggi_u8(&l6) == 0 && l6.rotto);
    PhLettore l7 = {NULL, 5, 0, false};
    VERIFICA(ph_leggi_u32(&l7) == 0 && l7.rotto);
    ph_leggi_stringa(&l7, NULL, 0);

    /* capacita' zero: non scrive nulla */
    PhLettore l8 = {d + 10, 5, 0, false};
    ph_leggi_stringa(&l8, s, 0);
    VERIFICA(!l8.rotto);
}

/* Sequenze casuali di letture su buffer grandi esattamente quanto i dati:
 * AddressSanitizer deve restare zitto. */
static void prova_a_caso(void)
{
    unsigned seme = 12345;
    for (int giro = 0; giro < 5000; giro++) {
        seme = seme * 1103515245u + 12345u;
        size_t n = (seme >> 16) % 40;
        uint8_t *d = malloc(n ? n : 1);
        VERIFICA(d != NULL);
        for (size_t i = 0; i < n; i++) {
            seme = seme * 1103515245u + 12345u;
            d[i] = (uint8_t)(seme >> 16);
        }
        PhLettore l = {n ? d : NULL, n, 0, false};
        char s[5];
        for (int op = 0; op < 12; op++) {
            seme = seme * 1103515245u + 12345u;
            switch ((seme >> 16) % 5) {
            case 0: (void)ph_leggi_u8(&l); break;
            case 1: (void)ph_leggi_u16(&l); break;
            case 2: (void)ph_leggi_u32(&l); break;
            case 3: (void)ph_leggi_u64(&l); break;
            default: ph_leggi_stringa(&l, s, sizeof s); break;
            }
            VERIFICA(l.posizione <= l.lunghezza);
        }
        free(d);
    }
}

int main(void)
{
    prova_scrittura();
    prova_lettura();
    prova_a_caso();
    return 0;
}
