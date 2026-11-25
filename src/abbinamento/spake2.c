#include "abbinamento/spake2.h"

#include <stdlib.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

/* Punti fissi del protocollo: generati dai semi
 * "edwards25519 point generation seed (M)" e "... (N)" (il test lo riverifica). */
const uint8_t PH_SPAKE2_M[32] = {
    0x5a, 0xda, 0x7e, 0x4b, 0xf6, 0xdd, 0xd9, 0xad, 0xb6, 0x62, 0x6d, 0x32, 0x13, 0x1c, 0x6b, 0x5c,
    0x51, 0xa1, 0xe3, 0x47, 0xa3, 0x47, 0x8f, 0x53, 0xcf, 0xcf, 0x44, 0x1b, 0x88, 0xee, 0xd1, 0x2e};
const uint8_t PH_SPAKE2_N[32] = {
    0x10, 0xe3, 0xdf, 0x0a, 0xe3, 0x7d, 0x8e, 0x7a, 0x99, 0xb5, 0xfe, 0x74, 0xb4, 0x46, 0x72, 0x10,
    0x3d, 0xbd, 0xdc, 0xbd, 0x06, 0xaf, 0x68, 0x0d, 0x71, 0x32, 0x9a, 0x11, 0x69, 0x3b, 0xc7, 0x78};

typedef struct {
    BIGNUM *x, *y;
} Punto;

/* La curva: -x^2 + y^2 = 1 + d x^2 y^2 modulo p = 2^255 - 19. */
typedef struct {
    BN_CTX *ctx;
    BIGNUM *p, *d, *l; /* l = ordine del sottogruppo primo */
    Punto base;
} Curva;

static int punto_crea(Punto *a)
{
    a->x = BN_new();
    a->y = BN_new();
    return (a->x != NULL && a->y != NULL) ? 0 : -1;
}

static void punto_libera(Punto *a)
{
    BN_clear_free(a->x);
    BN_clear_free(a->y);
    a->x = a->y = NULL;
}

static int punto_copia(Punto *r, const Punto *a)
{
    return (BN_copy(r->x, a->x) != NULL && BN_copy(r->y, a->y) != NULL) ? 0 : -1;
}

/* r = a + b. La formula e' completa su questa curva: vale anche per a == b e
 * per il punto neutro, quindi non servono casi speciali. r puo' coincidere
 * con a o b. */
static int somma(const Curva *c, Punto *r, const Punto *a, const Punto *b)
{
    int esito = -1;
    BN_CTX *ctx = c->ctx;
    BN_CTX_start(ctx);
    BIGNUM *t = BN_CTX_get(ctx), *xn = BN_CTX_get(ctx), *yn = BN_CTX_get(ctx);
    BIGNUM *u = BN_CTX_get(ctx), *v = BN_CTX_get(ctx), *uno = BN_CTX_get(ctx);
    if (uno == NULL || !BN_one(uno))
        goto fine;
    /* t = d * x1 x2 y1 y2 */
    if (!BN_mod_mul(u, a->x, b->x, c->p, ctx) || !BN_mod_mul(v, a->y, b->y, c->p, ctx) ||
        !BN_mod_mul(t, u, v, c->p, ctx) || !BN_mod_mul(t, t, c->d, c->p, ctx))
        goto fine;
    /* yn = y1 y2 + x1 x2 (v + u);  xn = x1 y2 + y1 x2 */
    if (!BN_mod_add(yn, v, u, c->p, ctx))
        goto fine;
    if (!BN_mod_mul(u, a->x, b->y, c->p, ctx) || !BN_mod_mul(v, a->y, b->x, c->p, ctx) ||
        !BN_mod_add(xn, u, v, c->p, ctx))
        goto fine;
    /* divisori: 1 + t e 1 - t (mai zero su questa curva) */
    if (!BN_mod_add(u, uno, t, c->p, ctx) || !BN_mod_sub(v, uno, t, c->p, ctx))
        goto fine;
    if (BN_mod_inverse(u, u, c->p, ctx) == NULL || BN_mod_inverse(v, v, c->p, ctx) == NULL)
        goto fine;
    if (!BN_mod_mul(r->x, xn, u, c->p, ctx) || !BN_mod_mul(r->y, yn, v, c->p, ctx))
        goto fine;
    esito = 0;
fine:
    BN_CTX_end(ctx);
    return esito;
}

/* r = k * a (raddoppia e somma). k puo' superare l (lo scalare della
 * password arriva fino a 8l). r non deve coincidere con a. */
static int moltiplica(const Curva *c, Punto *r, const BIGNUM *k, const Punto *a)
{
    Punto tmp = {NULL, NULL};
    int esito = -1;
    if (punto_crea(&tmp) != 0)
        goto fine;
    BN_zero(r->x);
    if (!BN_one(r->y))
        goto fine;
    for (int i = BN_num_bits(k) - 1; i >= 0; i--) {
        if (somma(c, r, r, r) != 0 || somma(c, &tmp, r, a) != 0)
            goto fine;
        if (BN_is_bit_set(k, i) && punto_copia(r, &tmp) != 0)
            goto fine;
    }
    esito = 0;
fine:
    punto_libera(&tmp);
    return esito;
}

static int nega(const Curva *c, Punto *r, const Punto *a)
{
    if (BN_copy(r->y, a->y) == NULL)
        return -1;
    if (BN_is_zero(a->x)) {
        BN_zero(r->x);
        return 0;
    }
    return BN_sub(r->x, c->p, a->x) ? 0 : -1;
}

/* Dai 32 byte (y in little endian, bit piu' alto = parita' di x) al punto.
 * -1 se non e' un punto della curva. */
static int decodifica(const Curva *c, Punto *r, const uint8_t in[32])
{
    int esito = -1;
    uint8_t b[32];
    memcpy(b, in, 32);
    int segno = b[31] >> 7;
    b[31] &= 0x7f;
    BN_CTX *ctx = c->ctx;
    BN_CTX_start(ctx);
    BIGNUM *y2 = BN_CTX_get(ctx), *u = BN_CTX_get(ctx), *v = BN_CTX_get(ctx);
    BIGNUM *x2 = BN_CTX_get(ctx), *uno = BN_CTX_get(ctx), *prova = BN_CTX_get(ctx);
    if (prova == NULL || !BN_one(uno) || BN_lebin2bn(b, 32, r->y) == NULL)
        goto fine;
    if (BN_cmp(r->y, c->p) >= 0) /* codifica non canonica: non la accettiamo */
        goto fine;
    /* x^2 = (y^2 - 1) / (d y^2 + 1) */
    if (!BN_mod_sqr(y2, r->y, c->p, ctx) || !BN_mod_sub(u, y2, uno, c->p, ctx) ||
        !BN_mod_mul(v, y2, c->d, c->p, ctx) || !BN_mod_add(v, v, uno, c->p, ctx))
        goto fine;
    if (BN_mod_inverse(v, v, c->p, ctx) == NULL || !BN_mod_mul(x2, u, v, c->p, ctx))
        goto fine;
    if (BN_is_zero(x2)) {
        if (segno)
            goto fine;
        BN_zero(r->x);
        esito = 0;
        goto fine;
    }
    if (BN_mod_sqrt(r->x, x2, c->p, ctx) == NULL) {
        ERR_clear_error(); /* non e' un quadrato: il punto non esiste */
        goto fine;
    }
    if (!BN_mod_sqr(prova, r->x, c->p, ctx) || BN_cmp(prova, x2) != 0)
        goto fine;
    if (BN_is_odd(r->x) != segno && !BN_sub(r->x, c->p, r->x))
        goto fine;
    esito = 0;
fine:
    BN_CTX_end(ctx);
    OPENSSL_cleanse(b, sizeof b);
    return esito;
}

static int codifica(const Punto *a, uint8_t out[32])
{
    if (BN_bn2lebinpad(a->y, out, 32) != 32)
        return -1;
    if (BN_is_odd(a->x))
        out[31] |= 0x80;
    return 0;
}

static void curva_libera(Curva *c)
{
    BN_free(c->p);
    BN_free(c->d);
    BN_free(c->l);
    punto_libera(&c->base);
    BN_CTX_free(c->ctx);
    memset(c, 0, sizeof *c);
}

static int curva_crea(Curva *c)
{
    memset(c, 0, sizeof *c);
    c->ctx = BN_CTX_new();
    c->p = BN_new();
    c->d = BN_new();
    c->l = BN_new();
    BIGNUM *a = BN_new(), *b = BN_new();
    uint8_t yb[32];
    int esito = -1;
    if (c->ctx == NULL || c->p == NULL || c->d == NULL || c->l == NULL || a == NULL || b == NULL ||
        punto_crea(&c->base) != 0)
        goto fine;
    /* p = 2^255 - 19 */
    if (!BN_one(c->p) || !BN_lshift(c->p, c->p, 255) || !BN_sub_word(c->p, 19))
        goto fine;
    /* l = 2^252 + 27742317777372353535851937790883648493 */
    if (!BN_dec2bn(&a, "27742317777372353535851937790883648493") || !BN_one(c->l) ||
        !BN_lshift(c->l, c->l, 252) || !BN_add(c->l, c->l, a))
        goto fine;
    /* d = -121665 / 121666 */
    if (!BN_set_word(a, 121665) || !BN_sub(a, c->p, a) || !BN_set_word(b, 121666) ||
        BN_mod_inverse(b, b, c->p, c->ctx) == NULL || !BN_mod_mul(c->d, a, b, c->p, c->ctx))
        goto fine;
    /* punto base: y = 4/5, x pari */
    if (!BN_set_word(a, 4) || !BN_set_word(b, 5) || BN_mod_inverse(b, b, c->p, c->ctx) == NULL ||
        !BN_mod_mul(a, a, b, c->p, c->ctx) || BN_bn2lebinpad(a, yb, 32) != 32)
        goto fine;
    if (decodifica(c, &c->base, yb) != 0)
        goto fine;
    esito = 0;
fine:
    BN_free(a);
    BN_free(b);
    if (esito != 0)
        curva_libera(c);
    return esito;
}

struct PhSpake2 {
    PhSpake2Ruolo ruolo;
    Curva curva;
    uint8_t *nome_alice, *nome_bob;
    size_t n_alice, n_bob;
    BIGNUM *privato;         /* 8 * (casuale mod l) */
    BIGNUM *scalare;         /* scalare della password */
    uint8_t hash_password[SHA512_DIGEST_LENGTH];
    uint8_t mio[PH_SPAKE2_MESSAGGIO];
    int stato; /* 0 creato, 1 messaggio fatto, 2 chiave fatta */
};

PhSpake2 *ph_spake2_crea(PhSpake2Ruolo ruolo, const uint8_t *nome_alice, size_t n_alice,
                         const uint8_t *nome_bob, size_t n_bob)
{
    PhSpake2 *s = calloc(1, sizeof *s);
    if (s == NULL)
        return NULL;
    s->ruolo = ruolo;
    if (curva_crea(&s->curva) != 0) {
        free(s);
        return NULL;
    }
    s->nome_alice = malloc(n_alice ? n_alice : 1);
    s->nome_bob = malloc(n_bob ? n_bob : 1);
    s->privato = BN_secure_new();
    s->scalare = BN_secure_new();
    if (s->nome_alice == NULL || s->nome_bob == NULL || s->privato == NULL || s->scalare == NULL) {
        ph_spake2_libera(s);
        return NULL;
    }
    if (n_alice)
        memcpy(s->nome_alice, nome_alice, n_alice);
    if (n_bob)
        memcpy(s->nome_bob, nome_bob, n_bob);
    s->n_alice = n_alice;
    s->n_bob = n_bob;
    return s;
}

void ph_spake2_libera(PhSpake2 *s)
{
    if (s == NULL)
        return;
    BN_clear_free(s->privato);
    BN_clear_free(s->scalare);
    free(s->nome_alice);
    free(s->nome_bob);
    if (s->curva.ctx != NULL)
        curva_libera(&s->curva);
    OPENSSL_cleanse(s, sizeof *s);
    free(s);
}

/* La maschera di chi parla: alice usa M, bob usa N (mia = 1);
 * per togliere quella dell'altro si usa il punto opposto (mia = 0). */
static int maschera(const PhSpake2 *s, Punto *r, int mia)
{
    int usa_m = (s->ruolo == PH_SPAKE2_ALICE) == (mia != 0);
    Punto fisso = {NULL, NULL};
    int esito = -1;
    if (punto_crea(&fisso) != 0)
        goto fine;
    if (decodifica(&s->curva, &fisso, usa_m ? PH_SPAKE2_M : PH_SPAKE2_N) != 0)
        goto fine;
    esito = moltiplica(&s->curva, r, s->scalare, &fisso);
fine:
    punto_libera(&fisso);
    return esito;
}

int ph_spake2_messaggio(PhSpake2 *s, const uint8_t *password, size_t n,
                        uint8_t messaggio[PH_SPAKE2_MESSAGGIO])
{
    if (s == NULL || s->stato != 0)
        return -1;
    const Curva *c = &s->curva;
    uint8_t casuale[64];
    Punto pubblico = {NULL, NULL}, m = {NULL, NULL};
    BIGNUM *t = BN_new();
    int esito = -1;
    if (t == NULL || punto_crea(&pubblico) != 0 || punto_crea(&m) != 0)
        goto fine;

    /* Scalare privato: 64 byte casuali ridotti modulo l, poi per 8 (cosi'
     * il risultato finale cade sempre nel sottogruppo primo). */
    if (RAND_bytes(casuale, sizeof casuale) != 1 || BN_lebin2bn(casuale, 64, s->privato) == NULL ||
        !BN_mod(s->privato, s->privato, c->l, c->ctx) || !BN_lshift(s->privato, s->privato, 3))
        goto fine;
    if (moltiplica(c, &pubblico, s->privato, &c->base) != 0)
        goto fine;

    /* Scalare della password: SHA-512 ridotto modulo l. Poi, come fa
     * BoringSSL, si aggiungono multipli di l finche' i tre bit bassi sono
     * zero: il valore modulo l non cambia, ma M e N non stanno nel
     * sottogruppo primo e senza questo passo la loro parte di ordine piccolo
     * finirebbe nel messaggio, rivelando a chi ascolta i tre bit bassi dello
     * scalare. */
    SHA512(password, n, s->hash_password);
    if (BN_lebin2bn(s->hash_password, 64, s->scalare) == NULL ||
        !BN_mod(s->scalare, s->scalare, c->l, c->ctx))
        goto fine;
    for (int bit = 0; bit < 3; bit++) {
        if (BN_is_bit_set(s->scalare, bit)) {
            if (!BN_lshift(t, c->l, bit) || !BN_add(s->scalare, s->scalare, t))
                goto fine;
        }
    }

    if (maschera(s, &m, 1) != 0 || somma(c, &pubblico, &pubblico, &m) != 0 ||
        codifica(&pubblico, s->mio) != 0)
        goto fine;
    memcpy(messaggio, s->mio, PH_SPAKE2_MESSAGGIO);
    s->stato = 1;
    esito = 0;
fine:
    OPENSSL_cleanse(casuale, sizeof casuale);
    BN_clear_free(t);
    punto_libera(&pubblico);
    punto_libera(&m);
    return esito;
}

static int riassunto_aggiungi(SHA512_CTX *h, const uint8_t *dati, size_t n)
{
    /* Ogni voce e' preceduta dalla sua lunghezza su 8 byte, little endian. */
    uint8_t lunghezza[8];
    for (int i = 0; i < 8; i++)
        lunghezza[i] = (uint8_t)((uint64_t)n >> (8 * i));
    return (SHA512_Update(h, lunghezza, 8) == 1 && SHA512_Update(h, dati, n) == 1) ? 0 : -1;
}

int ph_spake2_chiave(PhSpake2 *s, const uint8_t altrui[PH_SPAKE2_MESSAGGIO],
                     uint8_t chiave[PH_SPAKE2_CHIAVE])
{
    if (s == NULL || s->stato != 1)
        return -1;
    const Curva *c = &s->curva;
    Punto q = {NULL, NULL}, m = {NULL, NULL}, k = {NULL, NULL};
    uint8_t kb[32];
    int esito = -1;
    if (punto_crea(&q) != 0 || punto_crea(&m) != 0 || punto_crea(&k) != 0)
        goto fine;
    if (decodifica(c, &q, altrui) != 0)
        goto fine;
    /* Toglie la maschera dell'altro e moltiplica per il nostro segreto. */
    if (maschera(s, &m, 0) != 0 || nega(c, &m, &m) != 0 || somma(c, &q, &q, &m) != 0 ||
        moltiplica(c, &k, s->privato, &q) != 0 || codifica(&k, kb) != 0)
        goto fine;

    SHA512_CTX h;
    const uint8_t *di_alice = s->ruolo == PH_SPAKE2_ALICE ? s->mio : altrui;
    const uint8_t *di_bob = s->ruolo == PH_SPAKE2_ALICE ? altrui : s->mio;
    if (SHA512_Init(&h) != 1 || riassunto_aggiungi(&h, s->nome_alice, s->n_alice) != 0 ||
        riassunto_aggiungi(&h, s->nome_bob, s->n_bob) != 0 ||
        riassunto_aggiungi(&h, di_alice, 32) != 0 || riassunto_aggiungi(&h, di_bob, 32) != 0 ||
        riassunto_aggiungi(&h, kb, 32) != 0 ||
        riassunto_aggiungi(&h, s->hash_password, sizeof s->hash_password) != 0 ||
        SHA512_Final(chiave, &h) != 1)
        goto fine;
    s->stato = 2;
    esito = 0;
fine:
    OPENSSL_cleanse(kb, sizeof kb);
    punto_libera(&q);
    punto_libera(&m);
    punto_libera(&k);
    return esito;
}

int ph_spake2_genera_punto(const char *seme, uint8_t punto[32])
{
    Curva c;
    Punto p = {NULL, NULL};
    uint8_t v[SHA256_DIGEST_LENGTH];
    int esito = -1;
    if (curva_crea(&c) != 0)
        return -1;
    if (punto_crea(&p) != 0)
        goto fine;
    SHA256((const uint8_t *)seme, strlen(seme), v);
    /* Si ripete l'hash finche' i 32 byte sono la codifica di un punto. */
    for (int giri = 0; giri < 1000; giri++) {
        if (decodifica(&c, &p, v) == 0) {
            memcpy(punto, v, 32);
            esito = 0;
            break;
        }
        SHA256(v, sizeof v, v);
    }
fine:
    punto_libera(&p);
    curva_libera(&c);
    return esito;
}

int ph_spake2_autoprova(void)
{
    Curva c;
    Punto r = {NULL, NULL};
    uint8_t b[32], atteso[32];
    int esito = -1;
    if (curva_crea(&c) != 0)
        return -1;
    if (punto_crea(&r) != 0)
        goto fine;
    /* Il punto base di ed25519 si codifica come 58 66 66 ... 66. */
    memset(atteso, 0x66, 32);
    atteso[0] = 0x58;
    if (codifica(&c.base, b) != 0 || memcmp(b, atteso, 32) != 0)
        goto fine;
    /* l * base = punto neutro (0, 1). */
    if (moltiplica(&c, &r, c.l, &c.base) != 0 || !BN_is_zero(r.x) || !BN_is_one(r.y))
        goto fine;
    /* 2*base calcolato in due modi. */
    BIGNUM *due = BN_new();
    Punto s = {NULL, NULL};
    if (due != NULL && punto_crea(&s) == 0 && BN_set_word(due, 2) &&
        moltiplica(&c, &r, due, &c.base) == 0 && somma(&c, &s, &c.base, &c.base) == 0 &&
        BN_cmp(r.x, s.x) == 0 && BN_cmp(r.y, s.y) == 0)
        esito = 0;
    BN_free(due);
    punto_libera(&s);
fine:
    punto_libera(&r);
    curva_libera(&c);
    return esito;
}
