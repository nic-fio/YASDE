#include "abbinamento/spake2.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t ALICE[] = "adb pair client";
static const uint8_t BOB[] = "adb pair server";

static int scambio(const char *pw_a, const char *pw_b, uint8_t ka[64], uint8_t kb[64])
{
    PhSpake2 *a = ph_spake2_crea(PH_SPAKE2_ALICE, ALICE, sizeof ALICE, BOB, sizeof BOB);
    PhSpake2 *b = ph_spake2_crea(PH_SPAKE2_BOB, ALICE, sizeof ALICE, BOB, sizeof BOB);
    assert(a != NULL && b != NULL);
    uint8_t ma[32], mb[32];
    assert(ph_spake2_messaggio(a, (const uint8_t *)pw_a, strlen(pw_a), ma) == 0);
    assert(ph_spake2_messaggio(b, (const uint8_t *)pw_b, strlen(pw_b), mb) == 0);
    assert(memcmp(ma, mb, 32) != 0);
    int ra = ph_spake2_chiave(a, mb, ka);
    int rb = ph_spake2_chiave(b, ma, kb);
    /* ordine sbagliato delle chiamate: rifiutato */
    assert(ph_spake2_chiave(a, mb, ka) == -1 || ra != 0);
    ph_spake2_libera(a);
    ph_spake2_libera(b);
    return (ra == 0 && rb == 0) ? 0 : -1;
}

int main(void)
{
    assert(ph_spake2_autoprova() == 0);

    /* M e N si rigenerano dai loro semi: conferma costanti e decodifica. */
    uint8_t p[32];
    assert(ph_spake2_genera_punto("edwards25519 point generation seed (M)", p) == 0);
    assert(memcmp(p, PH_SPAKE2_M, 32) == 0);
    assert(ph_spake2_genera_punto("edwards25519 point generation seed (N)", p) == 0);
    assert(memcmp(p, PH_SPAKE2_N, 32) == 0);

    uint8_t ka[64], kb[64];
    /* stessa password: stessa chiave (piu' giri: lo scalare privato e' casuale) */
    for (int i = 0; i < 8; i++) {
        char pw[32];
        snprintf(pw, sizeof pw, "12345%d-materiale", i);
        assert(scambio(pw, pw, ka, kb) == 0);
        assert(memcmp(ka, kb, 64) == 0);
    }
    /* password diverse: chiavi diverse */
    assert(scambio("123456", "123457", ka, kb) == 0);
    assert(memcmp(ka, kb, 64) != 0);

    /* messaggio che non e' un punto della curva: rifiutato */
    PhSpake2 *a = ph_spake2_crea(PH_SPAKE2_ALICE, ALICE, sizeof ALICE, BOB, sizeof BOB);
    assert(a != NULL);
    uint8_t m[32], falso[32];
    assert(ph_spake2_messaggio(a, (const uint8_t *)"1", 1, m) == 0);
    int rifiutati = 0;
    for (int y = 2; y < 40; y++) {
        memset(falso, 0, 32);
        falso[0] = (uint8_t)y;
        PhSpake2 *t = ph_spake2_crea(PH_SPAKE2_ALICE, ALICE, sizeof ALICE, BOB, sizeof BOB);
        assert(t != NULL && ph_spake2_messaggio(t, (const uint8_t *)"1", 1, m) == 0);
        if (ph_spake2_chiave(t, falso, ka) != 0)
            rifiutati++;
        ph_spake2_libera(t);
    }
    assert(rifiutati > 5 && rifiutati < 38); /* circa meta' dei valori non e' sulla curva */
    memset(falso, 0xff, 32); /* y >= p: non canonico */
    assert(ph_spake2_chiave(a, falso, ka) == -1);
    ph_spake2_libera(a);
    ph_spake2_libera(NULL);
    puts("spake2: tutto bene");
    return 0;
}
