/* Codifica e decodifica dei pacchetti ADB, con i byte attesi scritti a mano. */
#include "adb/interno.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    /* CNXN version=0x01000001 maxdata=0x00100000 con payload "host\0" */
    const char payload[] = "host";
    PhPacchettoAdb p;
    ph_pacchetto_prepara(&p, PH_ADB_CNXN, 0x01000001u, 0x00100000u, payload, 5);
    assert(p.lunghezza == 5);
    /* 'h'+'o'+'s'+'t' = 0x68+0x6f+0x73+0x74 = 0x1be */
    assert(p.checksum == 0x1be);
    uint8_t b[24];
    ph_pacchetto_codifica(&p, b);
    const uint8_t atteso[24] = {
        0x43, 0x4e, 0x58, 0x4e,       /* "CNXN" */
        0x01, 0x00, 0x00, 0x01,       /* arg0 = 0x01000001 */
        0x00, 0x00, 0x10, 0x00,       /* arg1 = 0x00100000 */
        0x05, 0x00, 0x00, 0x00,       /* lunghezza */
        0xbe, 0x01, 0x00, 0x00,       /* checksum */
        0xbc, 0xb1, 0xa7, 0xb1,       /* magic = ~"CNXN" */
    };
    assert(memcmp(b, atteso, 24) == 0);

    PhPacchettoAdb q;
    PhErrore e;
    assert(ph_pacchetto_decodifica(atteso, &q, &e) == 0);
    assert(q.comando == PH_ADB_CNXN && q.arg0 == 0x01000001u && q.arg1 == 0x00100000u);
    assert(q.lunghezza == 5 && q.checksum == 0x1be && q.magic == 0xb1a7b1bcu);

    /* gli altri comandi: le quattro lettere in ordine */
    const struct { uint32_t v; const char *s; } nomi[] = {
        {PH_ADB_AUTH, "AUTH"}, {PH_ADB_STLS, "STLS"}, {PH_ADB_OPEN, "OPEN"},
        {PH_ADB_OKAY, "OKAY"}, {PH_ADB_WRTE, "WRTE"}, {PH_ADB_CLSE, "CLSE"},
    };
    for (size_t i = 0; i < sizeof nomi / sizeof nomi[0]; i++) {
        ph_pacchetto_prepara(&p, nomi[i].v, 7, 9, NULL, 0);
        ph_pacchetto_codifica(&p, b);
        assert(memcmp(b, nomi[i].s, 4) == 0);
        assert(p.checksum == 0);
        assert(ph_pacchetto_decodifica(b, &q, &e) == 0);
        assert(q.comando == nomi[i].v && q.arg0 == 7 && q.arg1 == 9);
    }

    /* magic sbagliato */
    uint8_t cattivo[24];
    memcpy(cattivo, atteso, 24);
    cattivo[20] ^= 1;
    assert(ph_pacchetto_decodifica(cattivo, &q, &e) == -1);
    assert(e.testo[0] != '\0');

    /* lunghezza oltre il massimo: rifiutata (nessuna allocazione dal telefono) */
    memcpy(cattivo, atteso, 24);
    cattivo[12] = 0x01;
    cattivo[13] = 0x00;
    cattivo[14] = 0x10; /* 0x00100001 = 1 MiB + 1 */
    cattivo[15] = 0x00;
    assert(ph_pacchetto_decodifica(cattivo, &q, &e) == -1);
    cattivo[12] = 0x00; /* esattamente 1 MiB: ammessa */
    assert(ph_pacchetto_decodifica(cattivo, &q, &e) == 0);
    cattivo[15] = 0x80; /* 2 GiB */
    assert(ph_pacchetto_decodifica(cattivo, &q, &e) == -1);

    assert(ph_pacchetto_checksum("\xff\xff", 2) == 0x1fe);
    assert(ph_pacchetto_checksum(NULL, 0) == 0);
    return 0;
}
