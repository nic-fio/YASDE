/* Prove della parte pura della scoperta: pacchetti costruiti a mano. Girano
 * con AddressSanitizer; i pacchetti passati al lettore stanno in buffer
 * allocati della lunghezza esatta, cosi' ogni lettura oltre la fine viene
 * segnalata. */
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scoperta/scoperta.h"
#include "scoperta/scoperta_interno.h"

static int errori;

#define CONTROLLA(cond)                                                              \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: fallito: %s\n", __FILE__, __LINE__, #cond);     \
            errori++;                                                                \
        }                                                                            \
    } while (0)

typedef struct {
    uint8_t d[1024];
    size_t n;
} Pkt;

static void p8(Pkt *p, unsigned v) { p->d[p->n++] = (uint8_t)v; }
static void p16(Pkt *p, unsigned v) { p8(p, v >> 8); p8(p, v & 0xFF); }
static void p32(Pkt *p, uint32_t v) { p16(p, v >> 16); p16(p, v & 0xFFFF); }
static void pstr(Pkt *p, const char *s) /* un'etichetta */
{
    size_t l = strlen(s);
    p8(p, (unsigned)l);
    memcpy(p->d + p->n, s, l);
    p->n += l;
}
static void pptr(Pkt *p, size_t off) { p16(p, 0xC000 | (unsigned)off); }
static void intestazione(Pkt *p, unsigned flags, unsigned qd, unsigned an, unsigned ns, unsigned ar)
{
    p->n = 0;
    p16(p, 0);
    p16(p, flags);
    p16(p, qd);
    p16(p, an);
    p16(p, ns);
    p16(p, ar);
}
static void record(Pkt *p, unsigned tipo, uint32_t ttl, size_t *pos_rdlen)
{
    p16(p, tipo);
    p16(p, 0x8001); /* classe IN con il bit "cache flush" */
    p32(p, ttl);
    *pos_rdlen = p->n;
    p16(p, 0);
}
static void chiudi_rdata(Pkt *p, size_t pos_rdlen)
{
    size_t l = p->n - pos_rdlen - 2;
    p->d[pos_rdlen] = (uint8_t)(l >> 8);
    p->d[pos_rdlen + 1] = (uint8_t)l;
}

/* Risposta completa con nomi compressi:
 *   PTR  _adb-tls-<tipo>._tcp.local -> adb-R5CT0000000-AbCdEf.<servizio>
 *   SRV  istanza -> telefono-prova.local porta
 *   A    telefono-prova.local -> 192.168.1.50
 * parti: 1 = solo PTR+SRV, 2 = solo A, 3 = tutto. */
static uint32_t g_ttl_srv = 120; /* 0 = pacchetto di "addio" */

static void risposta(Pkt *p, bool abbinamento, int parti, uint16_t porta)
{
    const char *tipo = abbinamento ? "_adb-tls-pairing" : "_adb-tls-connect";
    size_t pos;
    if (parti == 2) {
        intestazione(p, 0x8400, 0, 1, 0, 0);
        pstr(p, "telefono-prova");
        pstr(p, "local");
        p8(p, 0);
        record(p, 1, 120, &pos);
        p8(p, 192); p8(p, 168); p8(p, 1); p8(p, 50);
        chiudi_rdata(p, pos);
        return;
    }
    intestazione(p, 0x8400, 0, 1, 0, parti == 3 ? 2 : 1);
    size_t nome_servizio = p->n;
    pstr(p, tipo);
    pstr(p, "_tcp");
    size_t off_local = p->n;
    pstr(p, "local");
    p8(p, 0);
    record(p, 12, 4500, &pos);
    size_t istanza = p->n;
    pstr(p, "adb-R5CT0000000-AbCdEf");
    pptr(p, nome_servizio);
    chiudi_rdata(p, pos);

    pptr(p, istanza);
    record(p, 33, g_ttl_srv, &pos);
    p16(p, 0);
    p16(p, 0);
    p16(p, porta);
    size_t bersaglio = p->n;
    pstr(p, "telefono-prova");
    pptr(p, off_local);
    chiudi_rdata(p, pos);

    if (parti == 3) {
        pptr(p, bersaglio);
        record(p, 1, 120, &pos);
        p8(p, 192); p8(p, 168); p8(p, 1); p8(p, 50);
        chiudi_rdata(p, pos);
    }
}

/* Copia in un buffer di lunghezza esatta e chiama il lettore. */
static size_t leggi_esatto(const uint8_t *d, size_t n, PhTelefonoTrovato *t, size_t *num, size_t max)
{
    uint8_t *copia = malloc(n > 0 ? n : 1);
    if (copia == NULL)
        exit(2);
    if (n > 0)
        memcpy(copia, d, n);
    size_t r = ph_scoperta_leggi_risposta(copia, n, t, num, max);
    free(copia);
    return r;
}

static void prova_domanda(void)
{
    static const uint8_t atteso_qu[] =
        "\x00\x00\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00"
        "\x10_adb-tls-connect\x04_tcp\x05local\x00\x00\x0c\x80\x01"
        "\x10_adb-tls-pairing\x04_tcp\x05local\x00\x00\x0c\x80\x01";
    uint8_t buf[512];
    size_t n = ph_scoperta_domanda(buf, sizeof buf, true);
    CONTROLLA(n == sizeof atteso_qu - 1);
    CONTROLLA(n == sizeof atteso_qu - 1 && memcmp(buf, atteso_qu, n) == 0);

    size_t m = ph_scoperta_domanda(buf, sizeof buf, false);
    CONTROLLA(m == n);
    /* Senza il bit QU: i due byte di classe diventano 00 01. */
    CONTROLLA(m == n && buf[n - 2] == 0x00 && buf[n - 1] == 0x01);
    CONTROLLA(m == n && buf[n - 4] == 0x00 && buf[n - 3] == 0x0c && buf[n - 2] == 0x00);

    for (size_t cap = 0; cap < n; cap++) {
        uint8_t *piccolo = malloc(cap > 0 ? cap : 1);
        CONTROLLA(piccolo != NULL);
        if (piccolo != NULL) {
            CONTROLLA(ph_scoperta_domanda(piccolo, cap, true) == 0);
            free(piccolo);
        }
    }
}

static void prova_risposta_completa(void)
{
    Pkt p;
    PhTelefonoTrovato t[4];
    size_t num = 0;

    risposta(&p, false, 3, 37123);
    CONTROLLA(leggi_esatto(p.d, p.n, t, &num, 4) == 1);
    CONTROLLA(num == 1);
    CONTROLLA(strcmp(t[0].nome, "adb-R5CT0000000-AbCdEf") == 0);
    CONTROLLA(strcmp(t[0].indirizzo, "192.168.1.50") == 0);
    CONTROLLA(t[0].porta == 37123);
    CONTROLLA(!t[0].abbinamento);

    /* Lo stesso pacchetto una seconda volta: nessun doppione. */
    CONTROLLA(leggi_esatto(p.d, p.n, t, &num, 4) == 0);
    CONTROLLA(num == 1);

    /* Servizio di abbinamento: stessa istanza ma voce diversa. */
    risposta(&p, true, 3, 40001);
    CONTROLLA(leggi_esatto(p.d, p.n, t, &num, 4) == 1);
    CONTROLLA(num == 2 && t[1].abbinamento && t[1].porta == 40001);

    /* massimo rispettato */
    size_t zero = 0;
    CONTROLLA(leggi_esatto(p.d, p.n, t, &zero, 0) == 0);
    size_t uno = 1;
    risposta(&p, false, 3, 1111);
    CONTROLLA(leggi_esatto(p.d, p.n, t, &uno, 1) == 0 && uno == 1);

    /* una domanda (QR = 0) non e' una risposta */
    risposta(&p, false, 3, 37123);
    p.d[2] = 0x00;
    size_t n0 = 0;
    CONTROLLA(leggi_esatto(p.d, p.n, t, &n0, 4) == 0);
}

static void prova_pacchetti_separati(void)
{
    Pkt p1, p2;
    PhScRaccolta *r = malloc(sizeof *r);
    PhTelefonoTrovato t[4];
    size_t num = 0;
    CONTROLLA(r != NULL);
    if (r == NULL)
        return;

    risposta(&p1, false, 1, 5555); /* PTR + SRV, senza A */
    risposta(&p2, false, 2, 0);    /* solo A */

    /* Da soli non bastano. */
    CONTROLLA(leggi_esatto(p1.d, p1.n, t, &num, 4) == 0);
    CONTROLLA(leggi_esatto(p2.d, p2.n, t, &num, 4) == 0);

    ph_sc_raccolta_inizia(r);
    ph_sc_raccolta_aggiungi(r, p1.d, p1.n, NULL);
    CONTROLLA(ph_sc_raccolta_risolvi(r, t, &num, 4) == 0);
    ph_sc_raccolta_aggiungi(r, p2.d, p2.n, NULL);
    CONTROLLA(ph_sc_raccolta_risolvi(r, t, &num, 4) == 1);
    CONTROLLA(num == 1 && t[0].porta == 5555 && strcmp(t[0].indirizzo, "192.168.1.50") == 0);

    /* In ordine inverso: prima l'A, poi PTR+SRV. */
    num = 0;
    ph_sc_raccolta_inizia(r);
    ph_sc_raccolta_aggiungi(r, p2.d, p2.n, NULL);
    ph_sc_raccolta_aggiungi(r, p1.d, p1.n, NULL);
    CONTROLLA(ph_sc_raccolta_risolvi(r, t, &num, 4) == 1);

    /* Senza A, l'indirizzo di chi ha risposto fa da ripiego. */
    num = 0;
    ph_sc_raccolta_inizia(r);
    ph_sc_raccolta_aggiungi(r, p1.d, p1.n, "10.1.2.3");
    CONTROLLA(ph_sc_raccolta_risolvi(r, t, &num, 4) == 1);
    CONTROLLA(num == 1 && strcmp(t[0].indirizzo, "10.1.2.3") == 0);
    free(r);
}

static void prova_addio(void)
{
    Pkt p, addio;
    PhScRaccolta *r = malloc(sizeof *r);
    PhTelefonoTrovato t[4];
    size_t num = 0;
    CONTROLLA(r != NULL);
    if (r == NULL)
        return;
    /* Il telefono annuncia l'abbinamento e poi lo toglie con TTL 0. */
    risposta(&p, true, 3, 40001);
    g_ttl_srv = 0;
    risposta(&addio, true, 1, 40001);
    g_ttl_srv = 120;

    ph_sc_raccolta_inizia(r);
    ph_sc_raccolta_aggiungi(r, p.d, p.n, NULL);
    CONTROLLA(ph_sc_raccolta_risolvi(r, t, &num, 4) == 1);
    ph_sc_raccolta_aggiungi(r, addio.d, addio.n, NULL);
    num = 0;
    CONTROLLA(ph_sc_raccolta_risolvi(r, t, &num, 4) == 0 && num == 0);
    /* Addio della porta vecchia (40001) dopo l'annuncio di quella nuova (40002):
     * la nuova resta. */
    Pkt nuova;
    risposta(&nuova, true, 3, 40002);
    num = 0;
    ph_sc_raccolta_inizia(r);
    ph_sc_raccolta_aggiungi(r, nuova.d, nuova.n, NULL);
    ph_sc_raccolta_aggiungi(r, addio.d, addio.n, NULL);
    CONTROLLA(ph_sc_raccolta_risolvi(r, t, &num, 4) == 1 && t[0].porta == 40002);
    ph_sc_raccolta_inizia(r);
    ph_sc_raccolta_aggiungi(r, p.d, p.n, NULL);
    /* Un SRV con TTL 0 da solo non crea nulla. */
    num = 0;
    CONTROLLA(leggi_esatto(addio.d, addio.n, t, &num, 4) == 0);
    /* Se il servizio ricompare, torna. */
    ph_sc_raccolta_aggiungi(r, p.d, p.n, NULL);
    num = 0;
    CONTROLLA(ph_sc_raccolta_risolvi(r, t, &num, 4) == 1);
    free(r);
}

static void prova_sottorete(void)
{
    /* 192.168.1.50/24 in ordine di rete (little endian: byte alto in fondo) */
    uint32_t ind[1] = {htonl(0xC0A80132u)}, mask[1] = {htonl(0xFFFFFF00u)};
    CONTROLLA(ph_sc_in_sottorete(htonl(0xC0A80101u), ind, mask, 1));
    CONTROLLA(ph_sc_in_sottorete(htonl(0xC0A801FEu), ind, mask, 1));
    CONTROLLA(!ph_sc_in_sottorete(htonl(0xC0A80201u), ind, mask, 1));
    CONTROLLA(!ph_sc_in_sottorete(htonl(0x08080808u), ind, mask, 1));
    CONTROLLA(!ph_sc_in_sottorete(htonl(0xC0A80101u), ind, mask, 0));
}

static void prova_aaaa(void)
{
    Pkt p;
    size_t pos;
    PhTelefonoTrovato t[2];
    size_t num = 0;
    intestazione(&p, 0x8400, 0, 0, 0, 2);
    size_t o = p.n;
    pstr(&p, "x");
    pstr(&p, "_adb-tls-connect");
    pstr(&p, "_tcp");
    pstr(&p, "local");
    p8(&p, 0);
    record(&p, 33, 120, &pos);
    p16(&p, 0); p16(&p, 0); p16(&p, 7000);
    pstr(&p, "h");
    pstr(&p, "local");
    p8(&p, 0);
    chiudi_rdata(&p, pos);
    pstr(&p, "h");
    pstr(&p, "local");
    p8(&p, 0);
    record(&p, 28, 120, &pos);
    for (int i = 0; i < 16; i++)
        p8(&p, i == 0 ? 0x20 : i == 1 ? 0x01 : i == 15 ? 1 : 0);
    chiudi_rdata(&p, pos);
    (void)o;
    CONTROLLA(leggi_esatto(p.d, p.n, t, &num, 2) == 1);
    CONTROLLA(num == 1 && strcmp(t[0].indirizzo, "2001::1") == 0 && strcmp(t[0].nome, "x") == 0);
}

static void prova_ostili(void)
{
    Pkt p;
    PhTelefonoTrovato t[4];
    size_t num;

    /* Puntatore che rimanda a se stesso come nome del record. */
    intestazione(&p, 0x8400, 0, 1, 0, 0);
    pptr(&p, 12);
    p16(&p, 1); p16(&p, 1); p32(&p, 100); p16(&p, 4);
    p32(&p, 0);
    num = 0;
    CONTROLLA(leggi_esatto(p.d, p.n, t, &num, 4) == 0);

    /* Ciclo tra due puntatori. */
    intestazione(&p, 0x8400, 1, 0, 0, 0);
    pptr(&p, 14);
    pptr(&p, 12);
    p16(&p, 12); p16(&p, 1);
    num = 0;
    CONTROLLA(leggi_esatto(p.d, p.n, t, &num, 4) == 0);

    /* Puntatore fuori dal pacchetto. */
    intestazione(&p, 0x8400, 0, 1, 0, 0);
    p16(&p, 0xC000 | 0x3FFF);
    p16(&p, 1); p16(&p, 1); p32(&p, 100); p16(&p, 4);
    p32(&p, 0);
    num = 0;
    CONTROLLA(leggi_esatto(p.d, p.n, t, &num, 4) == 0);

    /* Catena lunghissima di puntatori in avanti: oltre il tetto dei salti. */
    intestazione(&p, 0x8400, 0, 1, 0, 0);
    for (int i = 0; i < 40; i++)
        pptr(&p, 12 + 2 * (size_t)(i + 1));
    p8(&p, 0);
    p16(&p, 1); p16(&p, 1); p32(&p, 100); p16(&p, 4);
    p32(&p, 0);
    num = 0;
    CONTROLLA(leggi_esatto(p.d, p.n, t, &num, 4) == 0);

    /* Lunghezza di etichetta oltre la fine. */
    intestazione(&p, 0x8400, 0, 1, 0, 0);
    p8(&p, 60);
    pstr(&p, "poco");
    num = 0;
    CONTROLLA(leggi_esatto(p.d, p.n, t, &num, 4) == 0);

    /* Etichetta riservata (bit 0x40). */
    intestazione(&p, 0x8400, 0, 1, 0, 0);
    p8(&p, 0x41);
    p8(&p, 'a');
    p8(&p, 0);
    num = 0;
    CONTROLLA(leggi_esatto(p.d, p.n, t, &num, 4) == 0);

    /* Conteggi enormi con un pacchetto corto. */
    intestazione(&p, 0x8400, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF);
    num = 0;
    CONTROLLA(leggi_esatto(p.d, p.n, t, &num, 4) == 0);

    /* rdlength falsa nel SRV: dichiara molto piu' di quanto resta. */
    risposta(&p, false, 3, 37123);
    static const uint8_t testa_srv[] = {0x00, 0x21, 0x80, 0x01};
    for (size_t i = 12; i + 10 < p.n; i++) {
        if (memcmp(p.d + i, testa_srv, sizeof testa_srv) == 0) {
            p.d[i + 8] = 0xFF;
            p.d[i + 9] = 0xFF;
            break;
        }
    }
    num = 0;
    CONTROLLA(leggi_esatto(p.d, p.n, t, &num, 4) == 0);

    /* Nome senza fine: etichette da 63 ripetute oltre il limite di un nome. */
    intestazione(&p, 0x8400, 1, 0, 0, 0);
    for (int i = 0; i < 8; i++) {
        p8(&p, 63);
        memset(p.d + p.n, 'a', 63);
        p.n += 63;
    }
    p8(&p, 0);
    p16(&p, 1); p16(&p, 1);
    num = 0;
    CONTROLLA(leggi_esatto(p.d, p.n, t, &num, 4) == 0);
}

/* Pacchetto troncato in ogni punto e con ogni byte alterato: niente letture
 * fuori dal buffer (lo dice AddressSanitizer) e nessun ciclo infinito (lo dice
 * il timeout di meson). */
static void prova_troncato_e_alterato(void)
{
    Pkt p;
    PhTelefonoTrovato t[4];
    for (int parti = 1; parti <= 3; parti++) {
        risposta(&p, parti == 2, parti, 37123);
        for (size_t n = 0; n <= p.n; n++) {
            size_t num = 0;
            leggi_esatto(p.d, n, t, &num, 4);
        }
        static const uint8_t valori[] = {0x00, 0x3F, 0x40, 0xC0, 0xFF, 0x01};
        for (size_t i = 0; i < p.n; i++) {
            for (size_t v = 0; v < sizeof valori; v++) {
                Pkt q = p;
                q.d[i] = valori[v];
                size_t num = 0;
                leggi_esatto(q.d, q.n, t, &num, 4);
            }
        }
    }
    size_t num = 0;
    CONTROLLA(ph_scoperta_leggi_risposta(NULL, 10, t, &num, 4) == 0);
}

int main(void)
{
    prova_domanda();
    prova_risposta_completa();
    prova_pacchetti_separati();
    prova_addio();
    prova_sottorete();
    prova_aaaa();
    prova_ostili();
    prova_troncato_e_alterato();
    if (errori > 0) {
        fprintf(stderr, "scoperta: %d controlli falliti\n", errori);
        return 1;
    }
    printf("scoperta: tutte le prove riuscite\n");
    return 0;
}
