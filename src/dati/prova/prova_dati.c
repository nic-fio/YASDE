/* Prove del pezzo "dati" con un telefono finto (finto.c): byte attesi di ogni
 * richiesta e risposta (docs/protocollo.md 2.6-2.8), risposte malformate e
 * troncate a ogni byte, ricerca e appunti con raffiche, annullamenti e
 * canali che cadono. Con l'argomento "concorrenza" gira solo la parte
 * multi-thread (per ThreadSanitizer). */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dati/dati.h"
#include "dati/interno.h"
#include "finto.h"

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "%s:%d: controllo fallito: %s\n", __FILE__, __LINE__, #cond);  \
            exit(1);                                                                       \
        }                                                                                  \
    } while (0)

#define ATTENDI(cond, ms)                                                                  \
    do {                                                                                   \
        int t_ = 0;                                                                        \
        while (!(cond) && t_ < (ms)) {                                                     \
            usleep(1000);                                                                  \
            t_++;                                                                          \
        }                                                                                  \
        CHECK(cond);                                                                       \
    } while (0)

/* ---- Scrittura di byte attesi (indipendente da ph_msg_*) --------------------*/
static void p8(PhBuffer *b, unsigned v)
{
    uint8_t x = (uint8_t)v;
    CHECK(ph_buffer_aggiungi(b, &x, 1) == 0);
}
static void p16(PhBuffer *b, unsigned v)
{
    p8(b, v >> 8);
    p8(b, v);
}
static void p32(PhBuffer *b, uint32_t v)
{
    p16(b, v >> 16);
    p16(b, v & 0xffff);
}
static void p64(PhBuffer *b, uint64_t v)
{
    p32(b, (uint32_t)(v >> 32));
    p32(b, (uint32_t)v);
}
static void pbytes(PhBuffer *b, const void *s, size_t n)
{
    CHECK(ph_buffer_aggiungi(b, s, n) == 0);
}
static void pstr(PhBuffer *b, const char *s)
{
    p16(b, (unsigned)strlen(s));
    pbytes(b, s, strlen(s));
}
/* Messaggio completo: tipo, lunghezza u32, corpo. */
static void pmsg(PhBuffer *b, unsigned tipo, const PhBuffer *corpo)
{
    p8(b, tipo);
    p32(b, (uint32_t)corpo->lunghezza);
    if (corpo->lunghezza)
        pbytes(b, corpo->dati, corpo->lunghezza);
}

static PhErrore err;

/* ---- Pulizia UTF-8 -------------------------------------------------------------*/
/* Controllo indipendente: UTF-8 ben formato, senza NUL. */
static bool utf8_valido(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    while (*p) {
        int n;
        uint32_t cp;
        if (*p < 0x80) {
            p++;
            continue;
        } else if ((*p & 0xe0) == 0xc0) {
            n = 1;
            cp = *p & 0x1f;
        } else if ((*p & 0xf0) == 0xe0) {
            n = 2;
            cp = *p & 0x0f;
        } else if ((*p & 0xf8) == 0xf0) {
            n = 3;
            cp = *p & 0x07;
        } else {
            return false;
        }
        for (int i = 1; i <= n; i++) {
            if ((p[i] & 0xc0) != 0x80)
                return false;
            cp = (cp << 6) | (p[i] & 0x3f);
        }
        if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) ||
            cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return false;
        p += n + 1;
    }
    return true;
}

static void prova_utf8(void)
{
    const char *buono = "caff\xc3\xa8 \xe2\x82\xac \xf0\x9f\x98\x80";
    char *u = ph_dati_pulisci((const uint8_t *)buono, strlen(buono), 1000, PH_PUL_RIGA);
    CHECK(u && strcmp(u, buono) == 0);
    free(u);

    struct {
        const char *in;
        size_t n;
        const char *atteso;
    } casi[] = {
        {"A\xff" "B", 3, "A\xef\xbf\xbd" "B"},
        {"\xc0\x80", 2, "\xef\xbf\xbd\xef\xbf\xbd"},                  /* forma troppo lunga */
        {"\xed\xa0\x80", 3, "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"},  /* surrogato */
        {"x\xe2\x82", 3, "x\xef\xbf\xbd\xef\xbf\xbd"},                /* troncata */
        {"\xf4\x90\x80\x80", 4, "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"}, /* oltre U+10FFFF */
        {"a\0b", 3, "a\xef\xbf\xbd" "b"},
    };
    for (size_t i = 0; i < sizeof casi / sizeof *casi; i++) {
        u = ph_dati_pulisci((const uint8_t *)casi[i].in, casi[i].n, 1000, PH_PUL_TESTO);
        CHECK(u && strcmp(u, casi[i].atteso) == 0);
        free(u);
    }
    /* A capo: restano nel testo, diventano spazio nelle righe. */
    u = ph_dati_pulisci((const uint8_t *)"a\nb\tc\x01", 6, 100, PH_PUL_TESTO);
    CHECK(u && strcmp(u, "a\nb\tc\xef\xbf\xbd") == 0);
    free(u);
    u = ph_dati_pulisci((const uint8_t *)"a\nb\tc\x01", 6, 100, PH_PUL_RIGA);
    CHECK(u && strcmp(u, "a b c ") == 0);
    free(u);
    /* C1 e separatori di riga: spazio nei nomi, intatti nei testi. */
    u = ph_dati_pulisci((const uint8_t *)"a\xc2\x85" "b\xe2\x80\xa8" "c\xe2\x80\xa9", 11, 100, PH_PUL_RIGA);
    CHECK(u && strcmp(u, "a b c ") == 0);
    free(u);
    u = ph_dati_pulisci((const uint8_t *)"a\xc2\x85" "b", 4, 100, PH_PUL_TESTO);
    CHECK(u && strcmp(u, "a\xc2\x85" "b") == 0);
    free(u);
    /* Appunti: solo UTF-8 non valido e NUL sono sostituiti. */
    u = ph_dati_pulisci((const uint8_t *)"a\x01\x1b\n\xff" "b\0c", 8, 100, PH_PUL_APPUNTI);
    CHECK(u && strcmp(u, "a\x01\x1b\n\xef\xbf\xbd" "b\xef\xbf\xbd" "c") == 0);
    free(u);
    /* Validazione e copia per lo schermo. */
    CHECK(ph_dati_utf8_valido((const uint8_t *)"a\nb\xc3\xa8", 5));
    CHECK(!ph_dati_utf8_valido((const uint8_t *)"a\xff", 2));
    CHECK(!ph_dati_utf8_valido((const uint8_t *)"a\0b", 3));
    CHECK(ph_dati_utf8_valido(NULL, 0));
    u = ph_dati_per_schermo("a\nb\xc2\x85" "c\xff");
    CHECK(u && strcmp(u, "a b c\xef\xbf\xbd") == 0);
    free(u);
    /* Troncamento su un confine di carattere. */
    u = ph_dati_pulisci((const uint8_t *)"a\xc3\xa8", 3, 2, PH_PUL_RIGA);
    CHECK(u && strcmp(u, "a") == 0);
    free(u);
    u = ph_dati_pulisci((const uint8_t *)"abc", 3, 0, PH_PUL_RIGA);
    CHECK(u && u[0] == '\0');
    free(u);
    u = ph_dati_pulisci(NULL, 0, 10, PH_PUL_RIGA);
    CHECK(u && u[0] == '\0');
    free(u);
    /* Byte casuali: sempre valido e mai oltre il massimo. */
    srand(12345);
    for (int i = 0; i < 20000; i++) {
        uint8_t b[64];
        size_t n = (size_t)(rand() % 64);
        for (size_t k = 0; k < n; k++)
            b[k] = (uint8_t)(rand() % 4 == 0 ? rand() : 0x80 + rand() % 0x80);
        size_t max = (size_t)(rand() % 200);
        u = ph_dati_pulisci(b, n, max, i % 3);
        CHECK(u && strlen(u) <= max && utf8_valido(u));
        free(u);
    }
}

/* ---- Avvisi di stato e notifiche ---------------------------------------------*/
static atomic_int n_stato, n_notifiche;
static PhDatiStato ultimo_stato;
static void av_stato(const PhDatiStato *s, void *u)
{
    (void)u;
    ultimo_stato = *s;
    atomic_fetch_add(&n_stato, 1);
}
static void av_notifiche(void *u)
{
    (void)u;
    atomic_fetch_add(&n_notifiche, 1);
}

static PhDati *apri_dati(void)
{
    finto_azzera();
    PhDatiAvvisi av = {av_stato, av_notifiche, NULL};
    PhDati *d = ph_dati_apri(finto_componente(), &av, &err);
    CHECK(d != NULL);
    return d;
}

/* Chiede con una risposta troncata a ogni lunghezza: solo quella intera riesce. */
#define PROVA_TRONCATE(corpo, chiamata)                                                    \
    do {                                                                                   \
        for (size_t k_ = 0; k_ <= (corpo).lunghezza; k_++) {                               \
            finto_risposta(TIPO_, (corpo).dati, k_);                                       \
            int r_ = (chiamata);                                                           \
            CHECK((r_ == 0) == (k_ == (corpo).lunghezza));                                 \
        }                                                                                  \
        /* un byte in piu': dati in eccesso */                                             \
        PhBuffer ecc_ = PH_BUFFER_VUOTO;                                                   \
        pbytes(&ecc_, (corpo).dati, (corpo).lunghezza);                                    \
        p8(&ecc_, 0);                                                                      \
        finto_risposta(TIPO_, ecc_.dati, ecc_.lunghezza);                                  \
        CHECK((chiamata) != 0);                                                            \
        ph_buffer_libera(&ecc_);                                                           \
    } while (0)

static void prova_app(void)
{
    PhDati *d = apri_dati();
    PhBuffer r = PH_BUFFER_VUOTO;
    p16(&r, 2);
    pstr(&r, "com.uno");
    pstr(&r, "Uno");
    p8(&r, 0);
    pstr(&r, "com.due");
    pstr(&r, "Du\xc3\xa8");
    p8(&r, 3);
    finto_risposta(0x70, r.dati, r.lunghezza);
    PhDatiApp *app;
    size_t n;
    CHECK(ph_dati_app_elenco(d, &app, &n, &err) == 0);
    CHECK(finto_ultimo_tipo() == 0x70);
    uint8_t corpo[8];
    CHECK(finto_ultimo_corpo(corpo, sizeof corpo) == 0);
    CHECK(n == 2);
    CHECK(strcmp(app[0].pacchetto, "com.uno") == 0 && strcmp(app[0].nome, "Uno") == 0);
    CHECK(!app[0].di_sistema && !app[0].solo_verticale);
    CHECK(strcmp(app[1].pacchetto, "com.due") == 0 && strcmp(app[1].nome, "Du\xc3\xa8") == 0);
    CHECK(app[1].di_sistema && app[1].solo_verticale);
    ph_dati_app_libera(app, n);

    /* Troncata a ogni byte e con byte in eccesso. */
#define TIPO_ 0x70
    PROVA_TRONCATE(r, ph_dati_app_elenco(d, &app, &n, &err) == 0 ? (ph_dati_app_libera(app, n), 0) : -1);
#undef TIPO_
    ph_buffer_libera(&r);

    /* Zero app. */
    p16(&r, 0);
    finto_risposta(0x70, r.dati, r.lunghezza);
    CHECK(ph_dati_app_elenco(d, &app, &n, &err) == 0 && n == 0 && app == NULL);
    ph_dati_app_libera(app, n);
    ph_buffer_svuota(&r);

    /* Conteggi ostili: enorme con pochi byte, oltre il tetto con i byte giusti. */
    p16(&r, 0xffff);
    p8(&r, 0);
    finto_risposta(0x70, r.dati, r.lunghezza);
    CHECK(ph_dati_app_elenco(d, &app, &n, &err) != 0 && app == NULL && n == 0);
    ph_buffer_svuota(&r);
    p16(&r, PH_DATI_APP_MAX + 1);
    for (int i = 0; i < PH_DATI_APP_MAX + 1; i++) {
        pstr(&r, "p");
        pstr(&r, "n");
        p8(&r, 0);
    }
    finto_risposta(0x70, r.dati, r.lunghezza);
    CHECK(ph_dati_app_elenco(d, &app, &n, &err) != 0);
    CHECK(strstr(err.testo, "troppe") != NULL);
    /* Esattamente il tetto: va bene. */
    ph_buffer_svuota(&r);
    p16(&r, PH_DATI_APP_MAX);
    for (int i = 0; i < PH_DATI_APP_MAX; i++) {
        pstr(&r, "p");
        pstr(&r, "n");
        p8(&r, 0);
    }
    finto_risposta(0x70, r.dati, r.lunghezza);
    CHECK(ph_dati_app_elenco(d, &app, &n, &err) == 0 && n == PH_DATI_APP_MAX);
    ph_dati_app_libera(app, n);
    ph_buffer_svuota(&r);

    /* UTF-8 non valido, controlli, nome troppo lungo. */
    char lungo[5001];
    memset(lungo, 'a', 5000);
    lungo[5000] = '\0';
    p16(&r, 2);
    pstr(&r, "p\xff.q");
    pstr(&r, "N\x01" "ome\n\xc0\xaf");
    p8(&r, 0);
    pstr(&r, "com.lungo");
    pstr(&r, lungo);
    p8(&r, 0);
    finto_risposta(0x70, r.dati, r.lunghezza);
    CHECK(ph_dati_app_elenco(d, &app, &n, &err) == 0 && n == 2);
    CHECK(utf8_valido(app[0].pacchetto) && utf8_valido(app[0].nome));
    CHECK(strcmp(app[0].pacchetto, "p\xef\xbf\xbd.q") == 0);
    CHECK(strcmp(app[0].nome, "N ome \xef\xbf\xbd\xef\xbf\xbd") == 0);
    CHECK(strlen(app[1].nome) == PH_DATI_NOME_MAX);
    ph_dati_app_libera(app, n);
    ph_buffer_libera(&r);

    /* Errore del telefono. */
    finto_risposta_errore(0x70, "elenco troppo grande");
    CHECK(ph_dati_app_elenco(d, &app, &n, &err) != 0 && strcmp(err.testo, "elenco troppo grande") == 0);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

static void prova_icona_miniatura(void)
{
    PhDati *d = apri_dati();
    const uint8_t png[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 1, 2, 3};
    finto_risposta(0x71, png, sizeof png);
    PhBuffer b = PH_BUFFER_VUOTO;
    CHECK(ph_dati_app_icona(d, "com.uno", 96, &b, &err) == 0);
    CHECK(b.lunghezza == sizeof png && memcmp(b.dati, png, sizeof png) == 0);
    PhBuffer att = PH_BUFFER_VUOTO;
    pstr(&att, "com.uno");
    p16(&att, 96);
    uint8_t got[64];
    CHECK(finto_ultimo_tipo() == 0x71);
    CHECK(finto_ultimo_corpo(got, sizeof got) == att.lunghezza);
    CHECK(memcmp(got, att.dati, att.lunghezza) == 0);
    ph_buffer_libera(&att);

    /* Lati fuori misura e nomi non validi: errore senza richiesta. */
    int richieste = finto_richieste();
    CHECK(ph_dati_app_icona(d, "com.uno", 15, &b, &err) != 0);
    CHECK(ph_dati_app_icona(d, "com.uno", 513, &b, &err) != 0);
    CHECK(ph_dati_app_icona(d, "", 96, &b, &err) != 0);
    CHECK(ph_dati_app_icona(d, NULL, 96, &b, &err) != 0);
    CHECK(finto_richieste() == richieste);
    CHECK(ph_dati_app_icona(d, "com.uno", 16, &b, &err) == 0);
    CHECK(ph_dati_app_icona(d, "com.uno", 512, &b, &err) == 0);

    /* Risposte che non sono PNG: vuota, solo firma, firma sbagliata; il buffer resta com'era. */
    size_t prima = b.lunghezza;
    finto_risposta(0x71, NULL, 0);
    CHECK(ph_dati_app_icona(d, "com.uno", 96, &b, &err) != 0);
    finto_risposta(0x71, png, 8);
    CHECK(ph_dati_app_icona(d, "com.uno", 96, &b, &err) != 0);
    uint8_t cattivo[sizeof png];
    memcpy(cattivo, png, sizeof png);
    cattivo[1] = 'X';
    finto_risposta(0x71, cattivo, sizeof cattivo);
    CHECK(ph_dati_app_icona(d, "com.uno", 96, &b, &err) != 0);
    CHECK(b.lunghezza == prima);
    finto_risposta_errore(0x71, "app non trovata");
    CHECK(ph_dati_app_icona(d, "com.uno", 96, &b, &err) != 0 && strcmp(err.testo, "app non trovata") == 0);
    ph_buffer_libera(&b);

    /* Miniatura. */
    const uint8_t jpg[] = {0xff, 0xd8, 0xff, 0xe0, 9, 9};
    finto_risposta(0x74, jpg, sizeof jpg);
    CHECK(ph_dati_miniatura(d, "/storage/emulated/0/DCIM/a.jpg", 128, &b, &err) == 0);
    CHECK(b.lunghezza == sizeof jpg && memcmp(b.dati, jpg, sizeof jpg) == 0);
    att = PH_BUFFER_VUOTO;
    pstr(&att, "/storage/emulated/0/DCIM/a.jpg");
    p16(&att, 128);
    CHECK(finto_ultimo_tipo() == 0x74);
    CHECK(finto_ultimo_corpo(got, sizeof got) == att.lunghezza && memcmp(got, att.dati, att.lunghezza) == 0);
    ph_buffer_libera(&att);
    richieste = finto_richieste();
    CHECK(ph_dati_miniatura(d, "/a", 31, &b, &err) != 0);
    CHECK(ph_dati_miniatura(d, "/a", 513, &b, &err) != 0);
    CHECK(ph_dati_miniatura(d, "", 64, &b, &err) != 0);
    char lungo[PH_DATI_PERCORSO_MAX + 2];
    memset(lungo, 'a', sizeof lungo - 1);
    lungo[sizeof lungo - 1] = '\0';
    CHECK(ph_dati_miniatura(d, lungo, 64, &b, &err) != 0);
    CHECK(finto_richieste() == richieste);
    finto_risposta(0x74, png, sizeof png);
    CHECK(ph_dati_miniatura(d, "/a", 64, &b, &err) != 0);
    finto_risposta(0x74, jpg, 3); /* solo la firma: nessun dato */
    CHECK(ph_dati_miniatura(d, "/a", 64, &b, &err) != 0);
    ph_buffer_libera(&b);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

static void prova_stato_memoria(void)
{
    PhDati *d = apri_dati();
    PhBuffer r = PH_BUFFER_VUOTO;
    p8(&r, 87);
    p8(&r, 1);
    p8(&r, 1);
    p8(&r, 3);
    p8(&r, 0);
    PhDatiStato s;
    finto_risposta(0x72, r.dati, r.lunghezza);
    CHECK(ph_dati_stato(d, &s, &err) == 0);
    CHECK(finto_ultimo_tipo() == 0x72);
    CHECK(s.batteria == 87 && s.in_carica && s.rete == PH_DATI_RETE_WIFI && s.segnale == 3 && !s.bloccato);
#define TIPO_ 0x72
    PROVA_TRONCATE(r, ph_dati_stato(d, &s, &err));
#undef TIPO_
    /* Un campo fuori misura per volta. */
    for (size_t i = 0; i < 5; i++) {
        uint8_t v[5];
        memcpy(v, r.dati, 5);
        v[i] = i == 0 ? 101 : i == 2 ? 3 : i == 3 ? 5 : 2;
        finto_risposta(0x72, v, 5);
        CHECK(ph_dati_stato(d, &s, &err) != 0);
    }
    /* Valori limite validi. */
    const uint8_t lim[5] = {100, 0, 2, 4, 1};
    finto_risposta(0x72, lim, 5);
    CHECK(ph_dati_stato(d, &s, &err) == 0 && s.rete == PH_DATI_RETE_CELLULARE && s.bloccato);

    /* Evento 0x7e: valido sì, malformato no. */
    int prima = atomic_load(&n_stato);
    finto_evento(0x7e, r.dati, r.lunghezza);
    CHECK(atomic_load(&n_stato) == prima + 1 && ultimo_stato.batteria == 87);
    for (size_t k = 0; k < r.lunghezza; k++)
        finto_evento(0x7e, r.dati, k);
    finto_evento(0x7e, NULL, 0);
    uint8_t brutto[5] = {200, 0, 0, 0, 0};
    finto_evento(0x7e, brutto, 5);
    CHECK(atomic_load(&n_stato) == prima + 1);
    prima = atomic_load(&n_notifiche);
    finto_evento(0x7f, NULL, 0);
    CHECK(atomic_load(&n_notifiche) == prima + 1);
    ph_buffer_libera(&r);

    /* Memoria. */
    p64(&r, 128000000000ull);
    p64(&r, 54000000000ull);
    PhDatiMemoria m;
    finto_risposta(0x75, r.dati, r.lunghezza);
    CHECK(ph_dati_memoria(d, &m, &err) == 0 && finto_ultimo_tipo() == 0x75);
    CHECK(m.totale == 128000000000ull && m.libera == 54000000000ull);
#define TIPO_ 0x75
    PROVA_TRONCATE(r, ph_dati_memoria(d, &m, &err));
#undef TIPO_
    ph_buffer_svuota(&r);
    p64(&r, 100);
    p64(&r, 101); /* libera piu' del totale: bugia */
    finto_risposta(0x75, r.dati, r.lunghezza);
    CHECK(ph_dati_memoria(d, &m, &err) != 0);
    ph_buffer_libera(&r);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

static void prova_notifiche(void)
{
    PhDati *d = apri_dati();
    PhBuffer r = PH_BUFFER_VUOTO;
    p16(&r, 2);
    pstr(&r, "0|com.chat|1");
    pstr(&r, "com.chat");
    pstr(&r, "Mario");
    pstr(&r, "Ciao\nci vediamo?");
    p64(&r, 1700000000123ull);
    pstr(&r, "k2");
    pstr(&r, "com.mail");
    pstr(&r, "T\xff" "itolo\n2");
    pstr(&r, "");
    p64(&r, 5);
    finto_risposta(0x73, r.dati, r.lunghezza);
    PhDatiNotifica *nt;
    size_t n;
    CHECK(ph_dati_notifiche(d, &nt, &n, &err) == 0 && n == 2);
    CHECK(finto_ultimo_tipo() == 0x73);
    CHECK(strcmp(nt[0].chiave, "0|com.chat|1") == 0 && strcmp(nt[0].pacchetto, "com.chat") == 0);
    CHECK(strcmp(nt[0].titolo, "Mario") == 0 && strcmp(nt[0].testo, "Ciao\nci vediamo?") == 0);
    CHECK(nt[0].ora_ms == 1700000000123ull);
    CHECK(strcmp(nt[1].titolo, "T\xef\xbf\xbd" "itolo 2") == 0 && nt[1].testo[0] == '\0' && nt[1].ora_ms == 5);
    ph_dati_notifiche_libera(nt, n);
#define TIPO_ 0x73
    PROVA_TRONCATE(r, ph_dati_notifiche(d, &nt, &n, &err) == 0 ? (ph_dati_notifiche_libera(nt, n), 0) : -1);
#undef TIPO_
    ph_buffer_svuota(&r);

    p16(&r, 0);
    finto_risposta(0x73, r.dati, r.lunghezza);
    CHECK(ph_dati_notifiche(d, &nt, &n, &err) == 0 && n == 0 && nt == NULL);
    ph_buffer_svuota(&r);
    p16(&r, 0xffff);
    for (int i = 0; i < 20; i++)
        p8(&r, 0);
    finto_risposta(0x73, r.dati, r.lunghezza);
    CHECK(ph_dati_notifiche(d, &nt, &n, &err) != 0);
    ph_buffer_svuota(&r);
    p16(&r, PH_DATI_NOTIFICHE_MAX + 1);
    for (int i = 0; i < PH_DATI_NOTIFICHE_MAX + 1; i++) {
        for (int k = 0; k < 4; k++)
            pstr(&r, "x");
        p64(&r, 1);
    }
    finto_risposta(0x73, r.dati, r.lunghezza);
    CHECK(ph_dati_notifiche(d, &nt, &n, &err) != 0 && strstr(err.testo, "troppe") != NULL);
    ph_buffer_svuota(&r);
    p16(&r, PH_DATI_NOTIFICHE_MAX);
    for (int i = 0; i < PH_DATI_NOTIFICHE_MAX; i++) {
        for (int k = 0; k < 4; k++)
            pstr(&r, "x");
        p64(&r, 1);
    }
    finto_risposta(0x73, r.dati, r.lunghezza);
    CHECK(ph_dati_notifiche(d, &nt, &n, &err) == 0 && n == PH_DATI_NOTIFICHE_MAX);
    ph_dati_notifiche_libera(nt, n);
    ph_buffer_libera(&r);

    /* La chiave arriva fino a 4096 byte intera; oltre si tronca. */
    {
        char *k = malloc(5001);
        CHECK(k != NULL);
        memset(k, 'k', 5000);
        k[5000] = '\0';
        p16(&r, 1);
        pstr(&r, k);
        pstr(&r, "p");
        pstr(&r, "t");
        pstr(&r, "x");
        p64(&r, 1);
        finto_risposta(0x73, r.dati, r.lunghezza);
        CHECK(ph_dati_notifiche(d, &nt, &n, &err) == 0 && strlen(nt[0].chiave) == PH_DATI_CHIAVE_MAX);
        ph_dati_notifiche_libera(nt, n);
        free(k);
        ph_buffer_libera(&r);
    }
    finto_risposta_errore(0x73, "il sistema non permette di leggere le notifiche");
    CHECK(ph_dati_notifiche(d, &nt, &n, &err) != 0);
    CHECK(strcmp(err.testo, "il sistema non permette di leggere le notifiche") == 0 && nt == NULL);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

/* ---- Ricerca -------------------------------------------------------------------*/
typedef struct {
    pthread_mutex_t m;
    atomic_int trovati;
    atomic_int fine;       /* quanti avvisi finali */
    atomic_int motivo;
    atomic_int ritardo_us; /* ritardo nell'avviso: per allungare le raffiche */
    char percorso[8][160];
    bool cartella[8];
} Ric;

static void ric_init(Ric *c)
{
    memset(c, 0, sizeof *c);
    pthread_mutex_init(&c->m, NULL);
    atomic_init(&c->trovati, 0);
    atomic_init(&c->fine, 0);
    atomic_init(&c->motivo, -1);
    atomic_init(&c->ritardo_us, 0);
}
static void ric_trovato(const char *p, bool cart, void *u)
{
    Ric *c = u;
    int i = atomic_load(&c->trovati);
    if (i < 8) {
        pthread_mutex_lock(&c->m);
        snprintf(c->percorso[i], sizeof c->percorso[i], "%.150s", p);
        c->cartella[i] = cart;
        pthread_mutex_unlock(&c->m);
    }
    int r = atomic_load(&c->ritardo_us);
    if (r > 0)
        usleep((unsigned)r);
    atomic_fetch_add(&c->trovati, 1);
}
static void ric_fine(PhDatiCercaMotivo m, void *u)
{
    Ric *c = u;
    atomic_store(&c->motivo, (int)m);
    atomic_fetch_add(&c->fine, 1);
}

static void msg_trovato(PhBuffer *out, const char *percorso, bool cart)
{
    PhBuffer c = PH_BUFFER_VUOTO;
    pstr(&c, percorso);
    p8(&c, cart);
    pmsg(out, 0x79, &c);
    ph_buffer_libera(&c);
}
static void msg_fine(PhBuffer *out, unsigned motivo)
{
    PhBuffer c = PH_BUFFER_VUOTO;
    p8(&c, motivo);
    pmsg(out, 0x7a, &c);
    ph_buffer_libera(&c);
}

static PhDatiRicerca *avvia(PhDati *d, Ric *c, const char *cart, const char *testo, bool solo, uint16_t max)
{
    PhDatiCerca q = {cart, testo, solo, max};
    PhDatiRicerca *r = ph_dati_cerca_avvia(d, &q, ric_trovato, ric_fine, c, &err);
    CHECK(r != NULL);
    return r;
}

static FintoCanale *canale_n(int n)
{
    ATTENDI(finto_canali_aperti() > n, 3000);
    return finto_canale(n);
}

static void prova_cerca_base(void)
{
    PhDati *d = apri_dati();
    Ric c;
    ric_init(&c);
    PhDatiRicerca *r = avvia(d, &c, "/storage/emulated/0", "foto", true, 30);
    FintoCanale *f = canale_n(0);
    CHECK(strcmp(finto_canale_riga(f), "cerca") == 0);
    /* Byte attesi della richiesta CERCA. */
    PhBuffer corpo = PH_BUFFER_VUOTO, att = PH_BUFFER_VUOTO;
    pstr(&corpo, "/storage/emulated/0");
    pstr(&corpo, "foto");
    p8(&corpo, 1);
    p16(&corpo, 30);
    pmsg(&att, 0x78, &corpo);
    uint8_t got[256];
    ATTENDI(finto_canale_scritto(f, got, sizeof got) == att.lunghezza, 3000);
    finto_canale_scritto(f, got, sizeof got);
    CHECK(memcmp(got, att.dati, att.lunghezza) == 0);
    ph_buffer_libera(&corpo);
    ph_buffer_libera(&att);

    /* Tre risultati (uno e' un file con la cartella-only: scartato) e la fine. */
    PhBuffer s = PH_BUFFER_VUOTO;
    msg_trovato(&s, "/storage/emulated/0/Foto", true);
    msg_trovato(&s, "/storage/emulated/0/foto.jpg", false);
    msg_trovato(&s, "/storage/emulated/0/Altre\nfoto", true);
    msg_trovato(&s, "/storage/emulated/0/non\xffvalido", true);
    msg_trovato(&s, "", true);
    msg_fine(&s, 0);
    finto_canale_a_pezzi(f, 3); /* arriva a pezzi da 3 byte */
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&c.fine) == 1, 3000);
    CHECK(atomic_load(&c.trovati) == 2 && atomic_load(&c.motivo) == PH_DATI_CERCA_FINITA);
    CHECK(strcmp(c.percorso[0], "/storage/emulated/0/Foto") == 0 && c.cartella[0]);
    /* il percorso arriva identico, a capo compreso */
    CHECK(strcmp(c.percorso[1], "/storage/emulated/0/Altre\nfoto") == 0 && c.cartella[1]);
    ph_buffer_libera(&s);
    ph_dati_cerca_annulla(r);
    CHECK(finto_canale_chiuso_dal_pc(f));
    CHECK(atomic_load(&c.fine) == 1);

    /* Parametri non validi. */
    PhDatiCerca q = {"/", "x", false, 0};
    q.testo = "";
    CHECK(ph_dati_cerca_avvia(d, &q, ric_trovato, ric_fine, &c, &err) == NULL);
    q.testo = "x";
    q.cartella = "";
    CHECK(ph_dati_cerca_avvia(d, &q, ric_trovato, ric_fine, &c, &err) == NULL);
    q.cartella = "/";
    q.massimo = PH_DATI_CERCA_MAX + 1;
    CHECK(ph_dati_cerca_avvia(d, &q, ric_trovato, ric_fine, &c, &err) == NULL);
    q.massimo = 0;
    CHECK(ph_dati_cerca_avvia(d, &q, NULL, ric_fine, &c, &err) == NULL);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

/* Una ricerca con un flusso dato; torna il motivo finale (-1 = nessun avviso)
 * e quanti risultati. dopo: 0 niente, 1 chiude il canale, 2 errore di rete. */
static int cerca_con_flusso(PhDati *d, const PhBuffer *flusso, size_t tagliato, int dopo, bool solo,
                            uint16_t max, int *trovati)
{
    Ric c;
    ric_init(&c);
    int base = finto_canali_aperti();
    PhDatiRicerca *r = avvia(d, &c, "/storage", "a", solo, max);
    FintoCanale *f = canale_n(base);
    finto_canale_spingi(f, flusso->dati, tagliato);
    if (dopo == 1)
        finto_canale_chiudi_remoto(f);
    else if (dopo == 2)
        finto_canale_errore_remoto(f);
    if (dopo != 0) {
        ATTENDI(atomic_load(&c.fine) == 1, 3000);
    } else {
        usleep(30000);
    }
    ph_dati_cerca_annulla(r);
    *trovati = atomic_load(&c.trovati);
    return atomic_load(&c.fine) == 1 ? atomic_load(&c.motivo) : -1;
}

static void prova_cerca_malformata(void)
{
    PhDati *d = apri_dati();
    int t;
    PhBuffer s = PH_BUFFER_VUOTO;
    msg_trovato(&s, "/a/uno", false);
    msg_trovato(&s, "/a/due", true);
    msg_fine(&s, 1);
    /* Flusso troncato a ogni byte con il canale che si chiude: sempre ERRORE
     * (anche tra due messaggi, prima di CERCA_FINE); intero: il suo motivo. */
    for (size_t k = 0; k <= s.lunghezza; k++) {
        int m = cerca_con_flusso(d, &s, k, 1, false, 0, &t);
        if (k == s.lunghezza)
            CHECK(m == PH_DATI_CERCA_MASSIMO && t == 2);
        else
            CHECK(m == PH_DATI_CERCA_ERRORE);
    }
    /* Canale che cade con un errore di rete. */
    CHECK(cerca_con_flusso(d, &s, 11, 2, false, 0, &t) == PH_DATI_CERCA_ERRORE);
    ph_buffer_libera(&s);

    /* FINE: motivi 0, 1, 2 passano, 3 no; corpo di lunghezza sbagliata no. */
    for (unsigned mot = 0; mot < 4; mot++) {
        msg_fine(&s, mot);
        int m = cerca_con_flusso(d, &s, s.lunghezza, 0, false, 0, &t);
        CHECK(m == (mot < 3 ? (int)mot : PH_DATI_CERCA_ERRORE));
        ph_buffer_svuota(&s);
    }
    p8(&s, 0x7a);
    p32(&s, 2);
    p8(&s, 0);
    p8(&s, 0);
    CHECK(cerca_con_flusso(d, &s, s.lunghezza, 0, false, 0, &t) == PH_DATI_CERCA_ERRORE);
    ph_buffer_svuota(&s);
    p8(&s, 0x7a);
    p32(&s, 0);
    CHECK(cerca_con_flusso(d, &s, s.lunghezza, 0, false, 0, &t) == PH_DATI_CERCA_ERRORE);
    ph_buffer_svuota(&s);

    /* TROVATO malformato: lunghezza interna maggiore, byte in eccesso, flag 2, vuoto. */
    PhBuffer c = PH_BUFFER_VUOTO;
    p16(&c, 50);
    pbytes(&c, "/a/b", 4);
    p8(&c, 0);
    pmsg(&s, 0x79, &c);
    CHECK(cerca_con_flusso(d, &s, s.lunghezza, 0, false, 0, &t) == PH_DATI_CERCA_ERRORE && t == 0);
    ph_buffer_svuota(&s);
    ph_buffer_svuota(&c);
    pstr(&c, "/a/b");
    p8(&c, 0);
    p8(&c, 0);
    pmsg(&s, 0x79, &c);
    CHECK(cerca_con_flusso(d, &s, s.lunghezza, 0, false, 0, &t) == PH_DATI_CERCA_ERRORE && t == 0);
    ph_buffer_svuota(&s);
    ph_buffer_svuota(&c);
    pstr(&c, "/a/b");
    p8(&c, 2);
    pmsg(&s, 0x79, &c);
    CHECK(cerca_con_flusso(d, &s, s.lunghezza, 0, false, 0, &t) == PH_DATI_CERCA_ERRORE && t == 0);
    ph_buffer_svuota(&s);
    ph_buffer_svuota(&c);
    /* Percorsi non validi (oltre PATH_MAX, non UTF-8, con NUL, vuoto, corpo oltre il
     * tetto): quel solo risultato si salta senza contarlo e la ricerca prosegue. */
    char *lungo = malloc(PH_DATI_PERCORSO_MAX + 2);
    CHECK(lungo != NULL);
    memset(lungo, 'a', PH_DATI_PERCORSO_MAX + 1);
    lungo[PH_DATI_PERCORSO_MAX + 1] = '\0';
    msg_trovato(&s, lungo, false);
    memset(lungo, 'a', PH_DATI_PERCORSO_MAX);
    lungo[PH_DATI_PERCORSO_MAX] = '\0';
    msg_trovato(&s, lungo, false); /* esattamente il tetto: valido */
    free(lungo);
    msg_trovato(&s, "/a/\xff", false);
    msg_trovato(&s, "/a/\xc0\xaf", false);
    msg_trovato(&s, "/a/\xed\xa0\x80", false);
    ph_buffer_svuota(&c);
    p16(&c, 4);
    pbytes(&c, "/a\0b", 4);
    p8(&c, 0);
    pmsg(&s, 0x79, &c);
    msg_trovato(&s, "", false);
    ph_buffer_svuota(&c);
    for (int i = 0; i < 20000; i++)
        p8(&c, 'a');
    pmsg(&s, 0x79, &c); /* corpo oltre il tetto di un percorso: letto e scartato */
    msg_trovato(&s, "/a/valido", false);
    msg_fine(&s, 0);
    CHECK(cerca_con_flusso(d, &s, s.lunghezza, 0, false, 0, &t) == PH_DATI_CERCA_FINITA && t == 2);
    ph_buffer_svuota(&s);
    ph_buffer_svuota(&c);

    /* Messaggio da 1 MiB + 1: errore. Un tipo sconosciuto di 100 byte si ignora. */
    p8(&s, 0x79);
    p32(&s, (1u << 20) + 1);
    CHECK(cerca_con_flusso(d, &s, s.lunghezza, 0, false, 0, &t) == PH_DATI_CERCA_ERRORE);
    ph_buffer_svuota(&s);
    for (int i = 0; i < 100; i++)
        p8(&c, i);
    pmsg(&s, 0x6f, &c);
    msg_trovato(&s, "/a/x", false);
    msg_fine(&s, 0);
    CHECK(cerca_con_flusso(d, &s, s.lunghezza, 0, false, 0, &t) == PH_DATI_CERCA_FINITA && t == 1);
    ph_buffer_svuota(&s);
    ph_buffer_libera(&c);
    ph_buffer_libera(&s);

    /* Il telefono manda piu' risultati del massimo: ne arrivano solo max, poi MASSIMO. */
    for (int i = 0; i < 5; i++)
        msg_trovato(&s, "/a/f", false);
    msg_fine(&s, 0);
    CHECK(cerca_con_flusso(d, &s, s.lunghezza, 0, false, 2, &t) == PH_DATI_CERCA_MASSIMO && t == 2);
    /* Solo cartelle: i file si scartano e non contano. */
    ph_buffer_svuota(&s);
    msg_trovato(&s, "/a/f", false);
    msg_trovato(&s, "/a/d", true);
    msg_trovato(&s, "/a/g", false);
    msg_fine(&s, 0);
    CHECK(cerca_con_flusso(d, &s, s.lunghezza, 0, true, 0, &t) == PH_DATI_CERCA_FINITA && t == 1);
    ph_buffer_libera(&s);

    /* Il canale non si apre. */
    Ric ric;
    ric_init(&ric);
    finto_rifiuta_canali(true);
    PhDatiRicerca *r = avvia(d, &ric, "/", "a", false, 0);
    ATTENDI(atomic_load(&ric.fine) == 1, 3000);
    CHECK(atomic_load(&ric.motivo) == PH_DATI_CERCA_ERRORE);
    ph_dati_cerca_annulla(r);
    finto_rifiuta_canali(false);
    /* La scrittura della richiesta fallisce. */
    ric_init(&ric);
    finto_scritture_falliscono(true);
    r = avvia(d, &ric, "/", "a", false, 0);
    ATTENDI(atomic_load(&ric.fine) == 1, 3000);
    CHECK(atomic_load(&ric.motivo) == PH_DATI_CERCA_ERRORE);
    ph_dati_cerca_annulla(r);
    finto_scritture_falliscono(false);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

/* ---- Ricerca: raffiche e annullamenti (parte concorrente) ---------------------*/
typedef struct {
    FintoCanale *f;
    atomic_bool ferma;
} Produttore;

static void *produci(void *a)
{
    Produttore *p = a;
    PhBuffer s = PH_BUFFER_VUOTO;
    msg_trovato(&s, "/storage/emulated/0/qualcosa/di/lungo.txt", false);
    while (!atomic_load(&p->ferma)) {
        finto_canale_spingi(p->f, s.dati, s.lunghezza);
        usleep(50);
    }
    ph_buffer_libera(&s);
    return NULL;
}

static void prova_cerca_concorrenza(void)
{
    PhDati *d = apri_dati();
    Ric c;
    PhBuffer s = PH_BUFFER_VUOTO;

    /* Raffica: 4999 risultati e la fine; poi 6000 con il massimo del telefono (5000). */
    ric_init(&c);
    for (int i = 0; i < 4999; i++)
        msg_trovato(&s, "/storage/emulated/0/x", i & 1);
    msg_fine(&s, 0);
    PhDatiRicerca *r = avvia(d, &c, "/storage", "x", false, 0);
    finto_canale_spingi(canale_n(0), s.dati, s.lunghezza);
    ATTENDI(atomic_load(&c.fine) == 1, 10000);
    CHECK(atomic_load(&c.trovati) == 4999 && atomic_load(&c.motivo) == PH_DATI_CERCA_FINITA);
    ph_dati_cerca_annulla(r);
    ph_buffer_svuota(&s);
    for (int i = 0; i < 6000; i++)
        msg_trovato(&s, "/storage/emulated/0/x", false);
    ric_init(&c);
    r = avvia(d, &c, "/storage", "x", false, 0);
    finto_canale_spingi(canale_n(1), s.dati, s.lunghezza);
    ATTENDI(atomic_load(&c.fine) == 1, 10000);
    CHECK(atomic_load(&c.trovati) == 5000 && atomic_load(&c.motivo) == PH_DATI_CERCA_MASSIMO);
    ph_dati_cerca_annulla(r);
    ph_buffer_libera(&s);

    /* Annullamento a meta' di una raffica che continua ad arrivare. */
    ric_init(&c);
    atomic_store(&c.ritardo_us, 100);
    r = avvia(d, &c, "/storage", "x", false, 0);
    Produttore p = {canale_n(2), false};
    pthread_t th;
    CHECK(pthread_create(&th, NULL, produci, &p) == 0);
    ATTENDI(atomic_load(&c.trovati) > 10, 5000);
    ph_dati_cerca_annulla(r);
    int dopo = atomic_load(&c.trovati);
    usleep(80000);
    CHECK(atomic_load(&c.trovati) == dopo && atomic_load(&c.fine) == 0);
    CHECK(finto_canale_chiuso_dal_pc(p.f));
    atomic_store(&p.ferma, true);
    pthread_join(th, NULL);

    /* Ricerche in rapida sequenza (l'utente digita): ogni tasto annulla la
     * precedente. Il contesto si distrugge subito: un avviso in ritardo
     * sarebbe una lettura di memoria liberata (AddressSanitizer). */
    PhBuffer un = PH_BUFFER_VUOTO;
    msg_trovato(&un, "/storage/emulated/0/uno", false);
    msg_trovato(&un, "/storage/emulated/0/due", true);
    msg_trovato(&un, "/storage/emulated/0/tre", false);
    PhDatiRicerca *prec = NULL;
    Ric *prec_c = NULL;
    for (int i = 0; i < 300; i++) {
        Ric *n = malloc(sizeof *n);
        CHECK(n != NULL);
        ric_init(n);
        char testo[16];
        snprintf(testo, sizeof testo, "t%d", i);
        if (prec != NULL) {
            ph_dati_cerca_annulla(prec);
            memset(prec_c, 0xaa, sizeof *prec_c);
            free(prec_c);
        }
        /* Dopo l'annullamento nessun altro thread apre canali: l'indice e' sicuro. */
        int base = finto_canali_aperti();
        prec = avvia(d, n, "/storage", testo, i % 3 == 0, 0);
        prec_c = n;
        if (i % 2 == 0) {
            FintoCanale *f = canale_n(base);
            finto_canale_spingi(f, un.dati, un.lunghezza);
            if (i % 4 == 0)
                usleep(300);
        }
    }
    ph_dati_cerca_annulla(prec);
    memset(prec_c, 0xaa, sizeof *prec_c);
    free(prec_c);
    ph_buffer_libera(&un);

    /* Annullamento mentre il canale si sta ancora aprendo. */
    ric_init(&c);
    finto_ritardo_apertura_ms(100);
    r = avvia(d, &c, "/storage", "x", false, 0);
    usleep(10000);
    ph_dati_cerca_annulla(r);
    CHECK(atomic_load(&c.fine) == 0 && atomic_load(&c.trovati) == 0);
    finto_ritardo_apertura_ms(0);

    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

/* ---- Appunti -------------------------------------------------------------------*/
typedef struct {
    pthread_mutex_t m;
    atomic_int n;
    char *ultimo;
    size_t lung_ultimo;
    atomic_int ritardo_us;
    atomic_int caduti; /* avvisi finali (testo NULL) */
} Appunti;
static Appunti app;

static void app_init(void)
{
    free(app.ultimo);
    memset(&app, 0, sizeof app);
    pthread_mutex_init(&app.m, NULL);
    atomic_init(&app.n, 0);
    atomic_init(&app.ritardo_us, 0);
    atomic_init(&app.caduti, 0);
}
static void app_avviso(const char *t, size_t n, void *u)
{
    (void)u;
    if (t == NULL) {
        CHECK(n == 0);
        atomic_fetch_add(&app.caduti, 1);
        return;
    }
    pthread_mutex_lock(&app.m);
    free(app.ultimo);
    app.ultimo = strdup(t);
    app.lung_ultimo = n;
    pthread_mutex_unlock(&app.m);
    int r = atomic_load(&app.ritardo_us);
    if (r > 0)
        usleep((unsigned)r);
    atomic_fetch_add(&app.n, 1);
}

static void msg_appunti(PhBuffer *out, const void *testo, size_t n)
{
    PhBuffer c = PH_BUFFER_VUOTO;
    p32(&c, (uint32_t)n);
    pbytes(&c, testo, n);
    pmsg(out, 0x58, &c);
    ph_buffer_libera(&c);
}

static void prova_appunti(void)
{
    PhDati *d = apri_dati();
    app_init();
    /* Prima dell'apertura: errore. */
    CHECK(ph_dati_appunti_imposta(d, "x", &err) != 0);
    finto_rifiuta_canali(true);
    CHECK(ph_dati_appunti_apri(d, app_avviso, NULL, &err) != 0);
    finto_rifiuta_canali(false);
    CHECK(ph_dati_appunti_apri(d, app_avviso, NULL, &err) == 0);
    CHECK(ph_dati_appunti_apri(d, app_avviso, NULL, &err) != 0); /* gia' aperto */
    FintoCanale *f = canale_n(0);
    CHECK(strcmp(finto_canale_riga(f), "appunti") == 0);

    /* PC -> telefono: imposta e incolla. */
    uint8_t got[512];
    PhBuffer att = PH_BUFFER_VUOTO, c = PH_BUFFER_VUOTO;
    CHECK(ph_dati_appunti_imposta(d, "ciao \xc3\xa8", &err) == 0);
    p32(&c, 7);
    pbytes(&c, "ciao \xc3\xa8", 7);
    pmsg(&att, 0x59, &c);
    CHECK(finto_canale_scritto(f, got, sizeof got) == att.lunghezza);
    finto_canale_scritto(f, got, sizeof got);
    CHECK(memcmp(got, att.dati, att.lunghezza) == 0);
    size_t gia = att.lunghezza;
    ph_buffer_svuota(&c);
    ph_buffer_svuota(&att);
    p32(&c, 42);
    p32(&c, 3);
    pbytes(&c, "xyz", 3);
    pmsg(&att, 0x5a, &c);
    CHECK(ph_dati_appunti_incolla(d, 42, "xyz", &err) == 0);
    CHECK(finto_canale_scritto(f, got, sizeof got) == gia + att.lunghezza);
    finto_canale_scritto(f, got, sizeof got);
    CHECK(memcmp(got + gia, att.dati, att.lunghezza) == 0);
    ph_buffer_libera(&c);
    ph_buffer_libera(&att);
    /* Testo vuoto: rifiutato sul PC (il telefono lo ignora), nulla di scritto. */
    size_t prima_v = finto_canale_scritto(f, got, 0);
    CHECK(ph_dati_appunti_imposta(d, "", &err) != 0);
    CHECK(ph_dati_appunti_incolla(d, 3, "", &err) != 0);
    CHECK(finto_canale_scritto(f, got, 0) == prima_v);
    CHECK(ph_dati_appunti_imposta(d, NULL, &err) != 0);

    /* Limite di 200.000 byte sul PC: al limite passa, oltre no e non scrive nulla. */
    char *grande = malloc(PH_DATI_APPUNTI_MAX + 2);
    CHECK(grande != NULL);
    memset(grande, 'a', PH_DATI_APPUNTI_MAX + 1);
    grande[PH_DATI_APPUNTI_MAX + 1] = '\0';
    size_t prima = finto_canale_scritto(f, got, 0);
    CHECK(ph_dati_appunti_imposta(d, grande, &err) != 0);
    CHECK(ph_dati_appunti_incolla(d, 1, grande, &err) != 0);
    CHECK(finto_canale_scritto(f, got, 0) == prima);
    grande[PH_DATI_APPUNTI_MAX] = '\0';
    CHECK(ph_dati_appunti_imposta(d, grande, &err) == 0);
    CHECK(finto_canale_scritto(f, got, 0) == prima + 5 + 4 + PH_DATI_APPUNTI_MAX);
    CHECK(ph_dati_appunti_incolla(d, 1, grande, &err) == 0);
    CHECK(finto_canale_scritto(f, got, 0) == prima + 5 + 4 + PH_DATI_APPUNTI_MAX + 5 + 8 + PH_DATI_APPUNTI_MAX);

    /* Telefono -> PC. */
    PhBuffer s = PH_BUFFER_VUOTO;
    msg_appunti(&s, "dal telefono \xe2\x82\xac", 16);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&app.n) == 1, 3000);
    CHECK(strcmp(app.ultimo, "dal telefono \xe2\x82\xac") == 0 && app.lung_ultimo == 16);
    /* Al limite: arriva. Oltre: ignorato, ma il messaggio dopo arriva. */
    ph_buffer_svuota(&s);
    memset(grande, 'b', PH_DATI_APPUNTI_MAX);
    msg_appunti(&s, grande, PH_DATI_APPUNTI_MAX);
    finto_canale_a_pezzi(f, 7001);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&app.n) == 2, 5000);
    CHECK(app.lung_ultimo == PH_DATI_APPUNTI_MAX);
    ph_buffer_svuota(&s);
    grande[PH_DATI_APPUNTI_MAX] = 'b';
    msg_appunti(&s, grande, PH_DATI_APPUNTI_MAX + 1);
    msg_appunti(&s, "segue", 5);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&app.n) == 3, 5000);
    CHECK(strcmp(app.ultimo, "segue") == 0);
    free(grande);
    ph_buffer_svuota(&s);

    /* UTF-8 non valido e NUL: sostituiti. Vuoto: ignorato. */
    msg_appunti(&s, "a\xff" "b\0c", 5);
    msg_appunti(&s, "", 0);
    msg_appunti(&s, "fine", 4);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&app.n) == 5, 3000);
    CHECK(strcmp(app.ultimo, "fine") == 0);
    ph_buffer_svuota(&s);

    /* Lunghezza interna incoerente (troppa, poca, vuoto di corpo, tipo sconosciuto): ignorati. */
    PhBuffer cc = PH_BUFFER_VUOTO;
    p32(&cc, 100);
    pbytes(&cc, "abc", 3);
    pmsg(&s, 0x58, &cc);
    ph_buffer_svuota(&cc);
    p32(&cc, 2);
    pbytes(&cc, "abcd", 4);
    pmsg(&s, 0x58, &cc);
    ph_buffer_svuota(&cc);
    p8(&cc, 0);
    p8(&cc, 0);
    pmsg(&s, 0x58, &cc);
    ph_buffer_svuota(&cc);
    p32(&cc, 0xffffffffu);
    pmsg(&s, 0x58, &cc);
    ph_buffer_svuota(&cc);
    pbytes(&cc, "zz", 2);
    pmsg(&s, 0x01, &cc);
    ph_buffer_libera(&cc);
    msg_appunti(&s, "ultimo", 6);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&app.n) == 6, 3000);
    CHECK(strcmp(app.ultimo, "ultimo") == 0);
    ph_buffer_svuota(&s);
    usleep(20000);
    CHECK(atomic_load(&app.n) == 6);

    /* Controlli leciti di un testo copiato restano; se la sostituzione dell'UTF-8
     * non valido porta oltre il limite il messaggio si scarta. */
    int base = atomic_load(&app.n);
    msg_appunti(&s, "x\x01\x1b\ty\n", 6);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&app.n) == base + 1, 3000);
    CHECK(strcmp(app.ultimo, "x\x01\x1b\ty\n") == 0);
    ph_buffer_svuota(&s);
    uint8_t *brutto = malloc(PH_DATI_APPUNTI_MAX);
    CHECK(brutto != NULL);
    memset(brutto, 0xff, PH_DATI_APPUNTI_MAX);
    msg_appunti(&s, brutto, PH_DATI_APPUNTI_MAX);
    free(brutto);
    msg_appunti(&s, "dopo", 4);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&app.n) == base + 2, 5000);
    CHECK(strcmp(app.ultimo, "dopo") == 0);
    ph_buffer_svuota(&s);

    /* Chiusura: nessuna scrittura dopo, nessun avviso dopo. */
    ph_dati_appunti_chiudi(d);
    CHECK(finto_canale_chiuso_dal_pc(f));
    CHECK(atomic_load(&app.caduti) == 0); /* chiusura voluta: nessun avviso finale */
    CHECK(ph_dati_appunti_imposta(d, "x", &err) != 0);
    msg_appunti(&s, "tardi", 5);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    usleep(30000);
    CHECK(atomic_load(&app.n) == base + 2);
    ph_dati_appunti_chiudi(d); /* seconda volta: innocua */
    ph_buffer_libera(&s);

    /* Riapertura dopo la chiusura, poi chiusura di tutto con ph_dati_chiudi. */
    CHECK(ph_dati_appunti_apri(d, app_avviso, NULL, &err) == 0);
    f = canale_n(1);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
    CHECK(finto_canale_chiuso_dal_pc(f));
    CHECK(!finto_ascolto_registrato());
}

static void prova_appunti_cadute(void)
{
    PhBuffer s = PH_BUFFER_VUOTO;
    msg_appunti(&s, "abcdef", 6);
    /* Canale chiuso dal telefono, troncato a ogni byte: il thread finisce,
     * arriva l'avviso finale (testo NULL) e mai un testo. */
    for (size_t k = 0; k < s.lunghezza; k++) {
        PhDati *d = apri_dati();
        app_init();
        CHECK(ph_dati_appunti_apri(d, app_avviso, NULL, &err) == 0);
        FintoCanale *f = canale_n(0);
        finto_canale_spingi(f, s.dati, k);
        finto_canale_chiudi_remoto(f);
        ATTENDI(atomic_load(&app.caduti) == 1, 3000);
        CHECK(atomic_load(&app.n) == 0);
        CHECK(ph_dati_appunti_imposta(d, "x", &err) != 0 && strstr(err.testo, "caduto") != NULL);
        CHECK(ph_dati_chiudi(d, NULL) == 0);
    }
    /* Messaggio intero e poi errore di rete: l'avviso arriva, poi il thread finisce. */
    PhDati *d = apri_dati();
    app_init();
    CHECK(ph_dati_appunti_apri(d, app_avviso, NULL, &err) == 0);
    FintoCanale *f = canale_n(0);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    finto_canale_errore_remoto(f);
    ATTENDI(atomic_load(&app.caduti) == 1, 3000);
    CHECK(atomic_load(&app.n) == 1);
    CHECK(ph_dati_appunti_incolla(d, 1, "x", &err) != 0 && strstr(err.testo, "caduto") != NULL);
    /* Scrivere su un canale caduto: errore del canale, nessun crash. */
    finto_canale_scrittura_fallisce(f, true);
    CHECK(ph_dati_appunti_imposta(d, "x", &err) != 0);
    CHECK(ph_dati_chiudi(d, NULL) == 0);

    /* Messaggio oltre 1 MiB: il thread si ferma. */
    d = apri_dati();
    app_init();
    CHECK(ph_dati_appunti_apri(d, app_avviso, NULL, &err) == 0);
    f = canale_n(0);
    ph_buffer_svuota(&s);
    p8(&s, 0x58);
    p32(&s, (1u << 20) + 1);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&app.caduti) == 1, 3000);
    CHECK(ph_dati_appunti_imposta(d, "x", &err) != 0 && strstr(err.testo, "caduto") != NULL);
    ph_buffer_libera(&s);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

typedef struct {
    PhDati *d;
    int n;
    atomic_int fatti;
} Scrittore;

static void *scrivi_molto(void *a)
{
    Scrittore *s = a;
    char testo[64];
    for (int i = 0; i < s->n; i++) {
        snprintf(testo, sizeof testo, "scrittura numero %d", i);
        CHECK(ph_dati_appunti_imposta(s->d, testo, NULL) == 0);
        if (i % 3 == 0)
            CHECK(ph_dati_appunti_incolla(s->d, 7, testo, NULL) == 0);
        atomic_fetch_add(&s->fatti, 1);
    }
    return NULL;
}

static void *produci_appunti(void *a)
{
    Produttore *p = a;
    PhBuffer s = PH_BUFFER_VUOTO;
    msg_appunti(&s, "testo in arrivo", 15);
    while (!atomic_load(&p->ferma)) {
        finto_canale_spingi(p->f, s.dati, s.lunghezza);
        usleep(30);
    }
    ph_buffer_libera(&s);
    return NULL;
}

static void prova_appunti_concorrenza(void)
{
    PhDati *d = apri_dati();
    app_init();
    CHECK(ph_dati_appunti_apri(d, app_avviso, NULL, &err) == 0);
    FintoCanale *f = canale_n(0);

    /* Scrittori in parallelo con la lettura: i messaggi sul canale non si mescolano. */
    Produttore p = {f, false};
    pthread_t pt, st[4];
    CHECK(pthread_create(&pt, NULL, produci_appunti, &p) == 0);
    Scrittore sc[4];
    for (int i = 0; i < 4; i++) {
        sc[i] = (Scrittore){d, 200, 0};
        CHECK(pthread_create(&st[i], NULL, scrivi_molto, &sc[i]) == 0);
    }
    for (int i = 0; i < 4; i++)
        pthread_join(st[i], NULL);
    size_t tot = finto_canale_scritto(f, NULL, 0);
    uint8_t *w = malloc(tot);
    CHECK(w != NULL);
    finto_canale_scritto(f, w, tot);
    size_t pos = 0;
    int imposta = 0, incolla = 0;
    while (pos < tot) {
        CHECK(tot - pos >= 5);
        uint32_t l = ((uint32_t)w[pos + 1] << 24) | ((uint32_t)w[pos + 2] << 16) | ((uint32_t)w[pos + 3] << 8) | w[pos + 4];
        CHECK(tot - pos - 5 >= l);
        if (w[pos] == 0x59) {
            imposta++;
            CHECK(l >= 4 && memcmp(w + pos + 9, "scrittura numero ", 17) == 0);
        } else {
            CHECK(w[pos] == 0x5a);
            incolla++;
            CHECK(l >= 8 && w[pos + 8] == 7);
        }
        pos += 5 + l;
    }
    free(w);
    CHECK(imposta == 800 && incolla == 4 * 67);
    ATTENDI(atomic_load(&app.n) > 20, 5000);

    /* Chiusura mentre arrivano testi: dopo il ritorno non ne arrivano altri. */
    atomic_store(&app.ritardo_us, 200);
    int visti = atomic_load(&app.n);
    ATTENDI(atomic_load(&app.n) > visti + 3, 5000);
    ph_dati_appunti_chiudi(d);
    int dopo = atomic_load(&app.n);
    usleep(80000);
    CHECK(atomic_load(&app.n) == dopo);
    atomic_store(&p.ferma, true);
    pthread_join(pt, NULL);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

/* ---- ph_dati_chiudi con gli eventi in arrivo ------------------------------------*/
typedef struct {
    atomic_bool ferma;
} Eventi;

static void *manda_eventi(void *a)
{
    Eventi *e = a;
    const uint8_t st[5] = {50, 0, 1, 2, 0};
    while (!atomic_load(&e->ferma)) {
        finto_evento(0x7e, st, 5);
        finto_evento(0x7f, NULL, 0);
    }
    return NULL;
}

static void prova_chiudi_con_eventi(void)
{
    for (int i = 0; i < 50; i++) {
        PhDati *d = apri_dati();
        Eventi ev;
        atomic_init(&ev.ferma, false);
        pthread_t t;
        CHECK(pthread_create(&t, NULL, manda_eventi, &ev) == 0);
        int prima = atomic_load(&n_stato);
        ATTENDI(atomic_load(&n_stato) > prima + 2, 3000);
        CHECK(ph_dati_chiudi(d, NULL) == 0);
        int s = atomic_load(&n_stato), n = atomic_load(&n_notifiche);
        usleep(2000);
        CHECK(atomic_load(&n_stato) == s && atomic_load(&n_notifiche) == n);
        atomic_store(&ev.ferma, true);
        pthread_join(t, NULL);
    }
}

/* ---- Annullamento e chiusura dai propri avvisi ----------------------------------*/
static PhDati *g_d;
static PhDatiRicerca *g_r;
static atomic_int g_cb;
static atomic_int g_chiudi_vietata; /* avvisi che hanno tentato ph_dati_chiudi */

static void sc_trovato(const char *p, bool c, void *u)
{
    (void)p;
    (void)c;
    (void)u;
    atomic_fetch_add(&g_cb, 1);
    ph_dati_cerca_annulla(g_r);
}
static void sc_fine(PhDatiCercaMotivo m, void *u)
{
    (void)m;
    (void)u;
    atomic_fetch_add(&g_cb, 1);
    ph_dati_cerca_annulla(g_r);
}
static void sc_fine_doppia(PhDatiCercaMotivo m, void *u)
{
    (void)m;
    (void)u;
    ph_dati_cerca_annulla(g_r);
    ph_dati_cerca_annulla(g_r); /* la seconda e' innocua */
    atomic_fetch_add(&g_cb, 1);
}
static void sc_appunti(const char *t, size_t n, void *u)
{
    (void)n;
    (void)u;
    if (t == NULL)
        return;
    atomic_fetch_add(&g_cb, 1);
    ph_dati_appunti_chiudi(g_d);
}
static void sc_appunti_chiudi_tutto(const char *t, size_t n, void *u)
{
    (void)n;
    (void)u;
    if (t == NULL)
        return;
    CHECK(ph_dati_chiudi(g_d, NULL) == -1); /* vietata: rifiutata, messaggio su stderr */
    atomic_fetch_add(&g_chiudi_vietata, 1);
}
static void sc_stato_chiudi_tutto(const PhDatiStato *s, void *u)
{
    (void)s;
    (void)u;
    CHECK(ph_dati_chiudi(g_d, NULL) == -1);
    atomic_fetch_add(&g_chiudi_vietata, 1);
}

static void prova_autochiusura(void)
{
    PhDati *d = apri_dati();
    Ric c;
    PhBuffer s = PH_BUFFER_VUOTO;
    PhDatiCerca q = {"/storage", "x", false, 0};
    uint8_t got[8];

    /* Annulla dal proprio avviso "trovato": torna subito, il thread libera. */
    ric_init(&c);
    atomic_store(&g_cb, 0);
    int base = finto_canali_aperti();
    g_r = ph_dati_cerca_avvia(d, &q, sc_trovato, ric_fine, &c, &err);
    CHECK(g_r != NULL);
    FintoCanale *f = canale_n(base);
    msg_trovato(&s, "/storage/a", false);
    msg_trovato(&s, "/storage/b", false);
    msg_fine(&s, 0);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&d->ricerche) == 0, 3000);
    CHECK(atomic_load(&g_cb) == 1 && atomic_load(&c.fine) == 0);
    CHECK(finto_canale_chiuso_dal_pc(f));
    ph_buffer_svuota(&s);

    /* Annulla dal proprio avviso finale. */
    ric_init(&c);
    atomic_store(&g_cb, 0);
    base = finto_canali_aperti();
    g_r = ph_dati_cerca_avvia(d, &q, ric_trovato, sc_fine, &c, &err);
    CHECK(g_r != NULL);
    f = canale_n(base);
    msg_trovato(&s, "/storage/a", false);
    msg_fine(&s, 0);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&d->ricerche) == 0, 3000);
    CHECK(atomic_load(&g_cb) == 1 && atomic_load(&c.trovati) == 1);
    CHECK(finto_canale_chiuso_dal_pc(f));
    ph_buffer_svuota(&s);

    /* Annulla dall'avviso finale per errore (canale caduto). */
    ric_init(&c);
    atomic_store(&g_cb, 0);
    base = finto_canali_aperti();
    g_r = ph_dati_cerca_avvia(d, &q, ric_trovato, sc_fine, &c, &err);
    CHECK(g_r != NULL);
    f = canale_n(base);
    finto_canale_errore_remoto(f);
    ATTENDI(atomic_load(&d->ricerche) == 0, 3000);
    CHECK(atomic_load(&g_cb) == 1);

    CHECK(ph_dati_chiudi(d, NULL) == 0);
    /* Annulla dall'avviso finale e subito ph_dati_chiudi dal thread principale:
     * la ricerca in uscita si aspetta e si libera tutto. Molte volte. */
    for (int i = 0; i < 200; i++) {
        d = apri_dati();
        ric_init(&c);
        atomic_store(&g_cb, 0);
        g_r = ph_dati_cerca_avvia(d, &q, ric_trovato, i & 1 ? sc_fine_doppia : sc_fine, &c, &err);
        CHECK(g_r != NULL);
        f = canale_n(0);
        msg_fine(&s, 0);
        finto_canale_spingi(f, s.dati, s.lunghezza);
        ph_buffer_svuota(&s);
        ATTENDI(atomic_load(&d->da_annullare) == 0, 3000);
        CHECK(ph_dati_chiudi(d, NULL) == 0);
        CHECK(!finto_ascolto_registrato());
        CHECK(finto_canale_chiuso_dal_pc(f));
    }
    d = apri_dati();
    /* ph_dati_chiudi con una ricerca non annullata: non fa nulla (messaggio su stderr). */
    ric_init(&c);
    PhDatiRicerca *r = avvia(d, &c, "/storage", "x", false, 0);
    CHECK(ph_dati_chiudi(d, &err) == -1 && strstr(err.testo, "non annullate") != NULL);
    CHECK(finto_ascolto_registrato());
    CHECK(atomic_load(&d->ricerche) == 1);
    ph_dati_cerca_annulla(r);
    CHECK(atomic_load(&d->ricerche) == 0);

    /* Chiusura degli appunti dal loro stesso avviso. */
    g_d = d;
    atomic_store(&g_cb, 0);
    CHECK(ph_dati_appunti_apri(d, sc_appunti, NULL, &err) == 0);
    base = finto_canali_aperti() - 1;
    f = finto_canale(base);
    msg_appunti(&s, "primo", 5);
    msg_appunti(&s, "secondo", 7);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&g_cb) >= 1, 3000);
    ATTENDI(finto_canale_chiuso_dal_pc(f), 3000);
    ph_dati_appunti_chiudi(d); /* aspetta che il thread abbia finito: poi nulla da fare */
    CHECK(atomic_load(&g_cb) == 1);
    CHECK(ph_dati_appunti_imposta(d, "x", &err) != 0);
    ph_buffer_svuota(&s);
    /* Si puo' riaprire. */
    atomic_store(&g_cb, 0);
    CHECK(ph_dati_appunti_apri(d, sc_appunti, NULL, &err) == 0);
    f = finto_canale(finto_canali_aperti() - 1);
    msg_appunti(&s, "terzo", 5);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&g_cb) == 1, 3000);
    ph_dati_appunti_chiudi(d);
    CHECK(finto_canale_chiuso_dal_pc(f));
    ph_buffer_svuota(&s);
    CHECK(ph_dati_chiudi(d, NULL) == 0);

    /* ph_dati_chiudi dall'avviso degli appunti: ignorata, il PhDati resta valido. */
    d = apri_dati();
    g_d = d;
    atomic_store(&g_chiudi_vietata, 0);
    CHECK(ph_dati_appunti_apri(d, sc_appunti_chiudi_tutto, NULL, &err) == 0);
    f = canale_n(0);
    msg_appunti(&s, "uno", 3);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&g_chiudi_vietata) == 1, 3000);
    CHECK(finto_ascolto_registrato());
    CHECK(ph_dati_appunti_imposta(d, "ancora vivo", &err) == 0);
    ph_buffer_libera(&s);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
    CHECK(!finto_ascolto_registrato());
    (void)got;

    /* Idem dall'avviso dello stato (thread del componente). */
    finto_azzera();
    PhDatiAvvisi av = {sc_stato_chiudi_tutto, NULL, NULL};
    d = ph_dati_apri(finto_componente(), &av, &err);
    CHECK(d != NULL);
    g_d = d;
    atomic_store(&g_chiudi_vietata, 0);
    const uint8_t st[5] = {50, 0, 1, 2, 0};
    finto_evento(0x7e, st, 5);
    CHECK(atomic_load(&g_chiudi_vietata) == 1 && finto_ascolto_registrato());
    finto_evento(0x7e, st, 5);
    CHECK(atomic_load(&g_chiudi_vietata) == 2);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
    CHECK(!finto_ascolto_registrato());
}

static void prova_chiudi_con_ascolto_guasto(void)
{
    PhDati *d = apri_dati();
    app_init();
    CHECK(ph_dati_appunti_apri(d, app_avviso, NULL, &err) == 0);
    FintoCanale *f = canale_n(0);
    finto_togliere_ascolto_fallisce_una_volta();
    CHECK(ph_dati_chiudi(d, &err) == -1 && strstr(err.testo, "ascolto") != NULL);
    /* Nulla toccato: l'ascolto c'e' ancora, gli appunti funzionano. */
    CHECK(finto_ascolto_registrato());
    int prima = atomic_load(&n_notifiche);
    finto_evento(0x7f, NULL, 0);
    CHECK(atomic_load(&n_notifiche) == prima + 1);
    CHECK(!finto_canale_chiuso_dal_pc(f));
    CHECK(ph_dati_appunti_imposta(d, "ancora usabili", &err) == 0);
    PhBuffer s = PH_BUFFER_VUOTO;
    int n0 = atomic_load(&app.n);
    msg_appunti(&s, "dal telefono", 12);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ATTENDI(atomic_load(&app.n) == n0 + 1, 3000);
    ph_buffer_libera(&s);
    /* Senza piu' il guasto: chiude tutto. */
    CHECK(ph_dati_chiudi(d, &err) == 0);
    CHECK(!finto_ascolto_registrato() && finto_canale_chiuso_dal_pc(f));
}

/* ---- Widget: memoria per categoria e calendario ------------------------------------*/
static void prova_widget(void)
{
    PhDati *d = apri_dati();
    PhBuffer r = PH_BUFFER_VUOTO;
    PhDatiMemoriaDettaglio md;
    p64(&r, 1000);
    p64(&r, 2000);
    p64(&r, 3000);
    p64(&r, 4000);
    finto_risposta(0x76, r.dati, r.lunghezza);
    CHECK(ph_dati_memoria_dettaglio(d, &md, &err) == 0);
    uint8_t got[16];
    CHECK(finto_ultimo_tipo() == 0x76 && finto_ultimo_corpo(got, sizeof got) == 0);
    CHECK(md.immagini.byte == 1000 && md.video.byte == 2000 && md.audio.byte == 3000 && md.app.byte == 4000);
    CHECK(md.immagini.leggibile && md.video.leggibile && md.audio.leggibile && md.app.leggibile);
#define TIPO_ 0x76
    PROVA_TRONCATE(r, ph_dati_memoria_dettaglio(d, &md, &err));
#undef TIPO_
    /* Non leggibile: esposto con il campo, non con il numero magico; un valore
     * che e' "quasi" il massimo resta un numero. */
    ph_buffer_svuota(&r);
    p64(&r, UINT64_MAX);
    p64(&r, 0);
    p64(&r, UINT64_MAX - 1);
    p64(&r, UINT64_MAX);
    finto_risposta(0x76, r.dati, r.lunghezza);
    CHECK(ph_dati_memoria_dettaglio(d, &md, &err) == 0);
    CHECK(!md.immagini.leggibile && md.immagini.byte == 0);
    CHECK(md.video.leggibile && md.video.byte == 0);
    CHECK(md.audio.leggibile && md.audio.byte == UINT64_MAX - 1);
    CHECK(!md.app.leggibile && md.app.byte == 0);
    finto_risposta_errore(0x76, "suddivisione non disponibile");
    CHECK(ph_dati_memoria_dettaglio(d, &md, &err) != 0 && strcmp(err.testo, "suddivisione non disponibile") == 0);

    /* Calendario. */
    ph_buffer_svuota(&r);
    p16(&r, 2);
    pstr(&r, "TITOLO-SEGRETO riunione");
    p64(&r, 1700000000000ull);
    p64(&r, 1700003600000ull);
    p8(&r, 0);
    pstr(&r, "Comp\xff" "leanno\n");
    p64(&r, 1700100000000ull);
    p64(&r, 1700100000000ull);
    p8(&r, 1);
    finto_risposta(0x77, r.dati, r.lunghezza);
    PhDatiEvento *ev;
    size_t n;
    CHECK(ph_dati_calendario(d, 5, &ev, &n, &err) == 0 && n == 2);
    CHECK(finto_ultimo_tipo() == 0x77 && finto_ultimo_corpo(got, sizeof got) == 1 && got[0] == 5);
    CHECK(strcmp(ev[0].titolo, "TITOLO-SEGRETO riunione") == 0 && ev[0].inizio_ms == 1700000000000ull);
    CHECK(ev[0].fine_ms == 1700003600000ull && !ev[0].tutto_il_giorno);
    CHECK(strcmp(ev[1].titolo, "Comp\xef\xbf\xbd" "leanno ") == 0 && ev[1].tutto_il_giorno);
    ph_dati_calendario_libera(ev, n);
#define TIPO_ 0x77
    PROVA_TRONCATE(r, ph_dati_calendario(d, 5, &ev, &n, &err) == 0 ? (ph_dati_calendario_libera(ev, n), 0) : -1);
#undef TIPO_
    /* In nessun errore compare un titolo. */
    for (size_t k = 0; k < r.lunghezza; k++) {
        finto_risposta(0x77, r.dati, k);
        CHECK(ph_dati_calendario(d, 5, &ev, &n, &err) != 0 && ev == NULL && n == 0);
        CHECK(strstr(err.testo, "SEGRETO") == NULL && strstr(err.testo, "leanno") == NULL);
    }
    /* massimo validato prima di mandare: nessuna richiesta. */
    int richieste = finto_richieste();
    finto_risposta(0x77, r.dati, r.lunghezza);
    CHECK(ph_dati_calendario(d, 0, &ev, &n, &err) != 0);
    CHECK(ph_dati_calendario(d, 17, &ev, &n, &err) != 0);
    CHECK(ph_dati_calendario(d, 255, &ev, &n, &err) != 0);
    CHECK(finto_richieste() == richieste);
    CHECK(ph_dati_calendario(d, 1, &ev, &n, &err) != 0 && strstr(err.testo, "massimo") != NULL); /* 2 eventi su massimo 1 */
    CHECK(ph_dati_calendario(d, 16, &ev, &n, &err) == 0);
    ph_dati_calendario_libera(ev, n);
    CHECK(ph_dati_calendario(d, 2, &ev, &n, &err) == 0 && n == 2); /* esattamente il massimo */
    ph_dati_calendario_libera(ev, n);
    /* Numero dichiarato maggiore del vero (3 su 2 presenti) e conteggio enorme. */
    r.dati[1] = 3;
    finto_risposta(0x77, r.dati, r.lunghezza);
    CHECK(ph_dati_calendario(d, 16, &ev, &n, &err) != 0 && ev == NULL);
    r.dati[0] = 0xff;
    r.dati[1] = 0xff;
    finto_risposta(0x77, r.dati, r.lunghezza);
    CHECK(ph_dati_calendario(d, 16, &ev, &n, &err) != 0);
    /* Flag non 0/1; fine prima dell'inizio. */
    ph_buffer_svuota(&r);
    p16(&r, 1);
    pstr(&r, "x");
    p64(&r, 10);
    p64(&r, 20);
    p8(&r, 2);
    finto_risposta(0x77, r.dati, r.lunghezza);
    CHECK(ph_dati_calendario(d, 3, &ev, &n, &err) != 0);
    ph_buffer_svuota(&r);
    p16(&r, 1);
    pstr(&r, "x");
    p64(&r, 20);
    p64(&r, 10);
    p8(&r, 0);
    finto_risposta(0x77, r.dati, r.lunghezza);
    CHECK(ph_dati_calendario(d, 3, &ev, &n, &err) != 0);
    /* Zero eventi; titolo lungo troncato; ERRORE del telefono. */
    ph_buffer_svuota(&r);
    p16(&r, 0);
    finto_risposta(0x77, r.dati, r.lunghezza);
    CHECK(ph_dati_calendario(d, 3, &ev, &n, &err) == 0 && n == 0 && ev == NULL);
    char *lungo = malloc(3001);
    CHECK(lungo != NULL);
    memset(lungo, 'a', 3000);
    lungo[3000] = '\0';
    ph_buffer_svuota(&r);
    p16(&r, 1);
    pstr(&r, lungo);
    p64(&r, 1);
    p64(&r, 2);
    p8(&r, 0);
    free(lungo);
    finto_risposta(0x77, r.dati, r.lunghezza);
    CHECK(ph_dati_calendario(d, 3, &ev, &n, &err) == 0 && strlen(ev[0].titolo) == PH_DATI_NOME_MAX);
    ph_dati_calendario_libera(ev, n);
    finto_risposta_errore(0x77, "calendario non leggibile");
    CHECK(ph_dati_calendario(d, 3, &ev, &n, &err) != 0 && strcmp(err.testo, "calendario non leggibile") == 0 && ev == NULL);
    ph_buffer_libera(&r);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

/* ---- ph_dati_chiudi con una chiamata in corso -----------------------------------------*/
typedef struct {
    PhDati *d;
    int esito;
    atomic_bool fatto;
} Giro;

static void *giro_memoria(void *a)
{
    Giro *g = a;
    PhDatiMemoria m;
    g->esito = ph_dati_memoria(g->d, &m, NULL);
    atomic_store(&g->fatto, true);
    return NULL;
}
static void *giro_chiudi(void *a)
{
    Giro *g = a;
    g->esito = ph_dati_chiudi(g->d, NULL);
    atomic_store(&g->fatto, true);
    return NULL;
}
static bool chiudendo(PhDati *d)
{
    pthread_mutex_lock(&d->m_ric);
    bool r = d->chiudendo;
    pthread_mutex_unlock(&d->m_ric);
    return r;
}

static int in_uso(PhDati *d)
{
    pthread_mutex_lock(&d->m_ric);
    int n = d->in_uso;
    pthread_mutex_unlock(&d->m_ric);
    return n;
}

/* Aspetta al massimo ms che cond diventi vera: serve a controllare un'ASSENZA
 * (qualcosa che non deve accadere) con una scadenza esplicita. */
#define ASSENZA_FINO_A(cond, ms)                                                           \
    do {                                                                                   \
        for (int t_ = 0; t_ < (ms) && !(cond); t_++)                                       \
            usleep(1000);                                                                  \
    } while (0)

static void prova_chiudi_con_chiamata_in_corso(void)
{
    PhDati *d = apri_dati();
    PhBuffer r = PH_BUFFER_VUOTO;
    p64(&r, 100);
    p64(&r, 50);
    finto_risposta(0x75, r.dati, r.lunghezza);
    ph_buffer_libera(&r);
    finto_cancello(true);
    Giro a = {d, -9, false}, b = {d, -9, false};
    pthread_t ta, tb;
    CHECK(pthread_create(&ta, NULL, giro_memoria, &a) == 0);
    ATTENDI(finto_cancello_attese() == 1, 3000);
    CHECK(pthread_create(&tb, NULL, giro_chiudi, &b) == 0);
    ATTENDI(chiudendo(d), 3000);
    /* La chiusura non deve togliere l'ascolto finche' la richiesta e' ferma nel
     * cancello: se lo fa, il finto registra la violazione (assenza controllata
     * con scadenza di 150 ms). */
    ASSENZA_FINO_A(finto_violazioni() > 0 || atomic_load(&b.fatto), 150);
    CHECK(finto_violazioni() == 0 && !atomic_load(&b.fatto));
    /* Le chiamate nuove sono rifiutate. */
    PhDatiMemoria m;
    CHECK(ph_dati_memoria(d, &m, &err) == -1 && strstr(err.testo, "chiusura") != NULL);
    CHECK(ph_dati_appunti_imposta(d, "x", &err) == -1 && strstr(err.testo, "chiusura") != NULL);
    finto_cancello(false);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    CHECK(a.esito == 0 && b.esito == 0);
    CHECK(!finto_ascolto_registrato());
    CHECK(finto_violazioni() == 0);
}

/* Una ricerca non annullata fa rifiutare la chiusura anche se e' stata avviata
 * mentre la chiusura aspettava: -1 e tutto torna come prima. */
static void prova_chiudi_rifiutata_con_ricerca(void)
{
    PhDati *d = apri_dati();
    PhBuffer r = PH_BUFFER_VUOTO;
    p64(&r, 100);
    p64(&r, 50);
    finto_risposta(0x75, r.dati, r.lunghezza);
    ph_buffer_libera(&r);
    finto_cancello(true);
    Giro a = {d, -9, false}, b = {d, -9, false};
    pthread_t ta, tb;
    CHECK(pthread_create(&ta, NULL, giro_memoria, &a) == 0);
    ATTENDI(finto_cancello_attese() == 1, 3000);
    Ric c;
    ric_init(&c);
    PhDatiRicerca *rc = avvia(d, &c, "/storage", "x", false, 0); /* entra prima della chiusura */
    CHECK(pthread_create(&tb, NULL, giro_chiudi, &b) == 0);
    ATTENDI(chiudendo(d), 3000);
    /* La chiusura aspetta la richiesta ferma: il rifiuto non arriva prima. */
    ASSENZA_FINO_A(atomic_load(&b.fatto), 150);
    CHECK(!atomic_load(&b.fatto));
    finto_cancello(false);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    CHECK(a.esito == 0 && b.esito == -1);
    CHECK(!chiudendo(d) && finto_ascolto_registrato());
    /* d e' intatto: si annulla e si richiude. */
    ph_dati_cerca_annulla(rc);
    PhDatiMemoria m;
    CHECK(ph_dati_memoria(d, &m, &err) == 0);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

/* ph_dati_appunti_chiudi (pubblica) in corso mentre arriva ph_dati_chiudi. */
static pthread_mutex_t cb_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cb_cv = PTHREAD_COND_INITIALIZER;
static bool cb_dentro, cb_libera;
static void cb_appunti_bloccato(const char *t, size_t n, void *u)
{
    (void)n;
    (void)u;
    if (t == NULL)
        return;
    pthread_mutex_lock(&cb_m);
    cb_dentro = true;
    pthread_cond_broadcast(&cb_cv);
    while (!cb_libera)
        pthread_cond_wait(&cb_cv, &cb_m);
    pthread_mutex_unlock(&cb_m);
}
static void *giro_appunti_chiudi(void *a)
{
    Giro *g = a;
    ph_dati_appunti_chiudi(g->d);
    g->esito = 0;
    atomic_store(&g->fatto, true);
    return NULL;
}

static void prova_chiudi_con_appunti_chiudi_in_corso(void)
{
    PhDati *d = apri_dati();
    cb_dentro = false;
    cb_libera = false;
    CHECK(ph_dati_appunti_apri(d, cb_appunti_bloccato, NULL, &err) == 0);
    FintoCanale *f = canale_n(0);
    PhBuffer s = PH_BUFFER_VUOTO;
    msg_appunti(&s, "x", 1);
    finto_canale_spingi(f, s.dati, s.lunghezza);
    ph_buffer_libera(&s);
    pthread_mutex_lock(&cb_m);
    while (!cb_dentro)
        pthread_cond_wait(&cb_cv, &cb_m); /* il thread degli appunti e' fermo nell'avviso */
    pthread_mutex_unlock(&cb_m);
    Giro x = {d, -9, false}, y = {d, -9, false};
    pthread_t tx, ty;
    CHECK(pthread_create(&tx, NULL, giro_appunti_chiudi, &x) == 0); /* aspetta il join */
    ATTENDI(in_uso(d) == 1, 3000);
    CHECK(pthread_create(&ty, NULL, giro_chiudi, &y) == 0);
    ATTENDI(chiudendo(d), 3000);
    ASSENZA_FINO_A(atomic_load(&y.fatto), 150);
    CHECK(!atomic_load(&y.fatto) && !atomic_load(&x.fatto));
    pthread_mutex_lock(&cb_m);
    cb_libera = true;
    pthread_cond_broadcast(&cb_cv);
    pthread_mutex_unlock(&cb_m);
    pthread_join(tx, NULL);
    pthread_join(ty, NULL);
    CHECK(y.esito == 0 && !finto_ascolto_registrato());
}

/* ---- Calendario per intervallo, sveglia, batteria, controlli rapidi -------------------*/
static void ev(PhBuffer *b, const char *titolo, uint64_t ini, uint64_t fin, unsigned giorno)
{
    pstr(b, titolo);
    p64(b, ini);
    p64(b, fin);
    p8(b, giorno);
}

static void prova_calendario_intervallo(void)
{
    PhDati *d = apri_dati();
    PhBuffer r = PH_BUFFER_VUOTO;
    const uint64_t da = 1700000000000ull, g = 86400000ull;
    p8(&r, 1);
    pstr(&r, "com.calendario");
    p16(&r, 2);
    ev(&r, "TITOLO-SEGRETO uno", da + 1000, da + 2000, 0);
    ev(&r, "Due\xff", da + g, da + 2 * g, 1);
    finto_risposta(0x7b, r.dati, r.lunghezza);
    PhDatiCalendarioIntervallo out;
    CHECK(ph_dati_calendario_intervallo(d, da, da + 7 * g, 100, &out, &err) == 0);
    /* Byte attesi della richiesta: u64 da, u64 a, u16 massimo. */
    PhBuffer att = PH_BUFFER_VUOTO;
    p64(&att, da);
    p64(&att, da + 7 * g);
    p16(&att, 100);
    uint8_t got[64];
    CHECK(finto_ultimo_tipo() == 0x7b && finto_ultimo_corpo(got, sizeof got) == att.lunghezza);
    CHECK(memcmp(got, att.dati, att.lunghezza) == 0);
    ph_buffer_libera(&att);
    CHECK(out.numero == 2 && out.troncato && strcmp(out.app_calendario, "com.calendario") == 0);
    CHECK(strcmp(out.eventi[0].titolo, "TITOLO-SEGRETO uno") == 0 && out.eventi[0].inizio_ms == da + 1000);
    CHECK(strcmp(out.eventi[1].titolo, "Due\xef\xbf\xbd") == 0 && out.eventi[1].tutto_il_giorno);
    ph_dati_calendario_intervallo_libera(&out);
    CHECK(out.eventi == NULL && out.app_calendario == NULL && out.numero == 0);
#define TIPO_ 0x7b
    PROVA_TRONCATE(r, ph_dati_calendario_intervallo(d, da, da + g, 10, &out, &err) == 0
                          ? (ph_dati_calendario_intervallo_libera(&out), 0) : -1);
#undef TIPO_
    /* In nessun errore compare un titolo; a errore *out e' azzerato. */
    for (size_t k = 0; k < r.lunghezza; k++) {
        finto_risposta(0x7b, r.dati, k);
        CHECK(ph_dati_calendario_intervallo(d, da, da + g, 10, &out, &err) != 0);
        CHECK(out.eventi == NULL && out.app_calendario == NULL && out.numero == 0);
        CHECK(strstr(err.testo, "SEGRETO") == NULL && strstr(err.testo, "calendario") == NULL);
    }
    finto_risposta(0x7b, r.dati, r.lunghezza);
    /* Argomenti: validati prima di mandare. */
    int richieste = finto_richieste();
    CHECK(ph_dati_calendario_intervallo(d, da + 1, da, 10, &out, &err) != 0);                /* fine prima */
    CHECK(ph_dati_calendario_intervallo(d, da, da + 62 * g + 1, 10, &out, &err) != 0);       /* oltre 62 giorni, in ms */
    CHECK(ph_dati_calendario_intervallo(d, 0, UINT64_MAX, 10, &out, &err) != 0);
    CHECK(ph_dati_calendario_intervallo(d, da, da + g, 0, &out, &err) != 0);
    CHECK(ph_dati_calendario_intervallo(d, da, da + g, 501, &out, &err) != 0);
    CHECK(ph_dati_calendario_intervallo(d, da, da + g, 10, NULL, &err) != 0);
    CHECK(finto_richieste() == richieste);
    CHECK(ph_dati_calendario_intervallo(d, da, da + 62 * g, 500, &out, &err) == 0);          /* limiti esatti */
    ph_dati_calendario_intervallo_libera(&out);
    CHECK(ph_dati_calendario_intervallo(d, UINT64_MAX - 5, UINT64_MAX, 2, &out, &err) == 0); /* niente giri di intero */
    ph_dati_calendario_intervallo_libera(&out);
    CHECK(ph_dati_calendario_intervallo(d, da, da + g, 1, &out, &err) != 0 && strstr(err.testo, "massimo") != NULL);
    /* Numero dichiarato maggiore del vero, enorme; flag sbagliati. */
    r.dati[1 + 2 + 14 + 1] = 3; /* u8 troncato + stringa(2+14) + u16: byte basso del numero */
    finto_risposta(0x7b, r.dati, r.lunghezza);
    CHECK(ph_dati_calendario_intervallo(d, da, da + g, 10, &out, &err) != 0);
    r.dati[1 + 2 + 14] = 0xff;
    r.dati[1 + 2 + 14 + 1] = 0xff;
    finto_risposta(0x7b, r.dati, r.lunghezza);
    CHECK(ph_dati_calendario_intervallo(d, da, da + g, 500, &out, &err) != 0 && out.eventi == NULL);
    ph_buffer_svuota(&r);
    p8(&r, 2); /* troncato non 0/1 */
    pstr(&r, "");
    p16(&r, 0);
    finto_risposta(0x7b, r.dati, r.lunghezza);
    CHECK(ph_dati_calendario_intervallo(d, da, da + g, 10, &out, &err) != 0);
    ph_buffer_svuota(&r);
    p8(&r, 0);
    pstr(&r, "");
    p16(&r, 0);
    finto_risposta(0x7b, r.dati, r.lunghezza);
    CHECK(ph_dati_calendario_intervallo(d, da, da + g, 10, &out, &err) == 0);
    CHECK(out.numero == 0 && out.eventi == NULL && !out.troncato && out.app_calendario != NULL && out.app_calendario[0] == '\0');
    ph_dati_calendario_intervallo_libera(&out);
    ph_buffer_svuota(&r);
    p8(&r, 0);
    pstr(&r, "com.\xffx");
    p16(&r, 1);
    ev(&r, "x", 20, 10, 0); /* fine prima dell'inizio */
    finto_risposta(0x7b, r.dati, r.lunghezza);
    CHECK(ph_dati_calendario_intervallo(d, da, da + g, 10, &out, &err) != 0);
    ph_buffer_svuota(&r);
    p8(&r, 0);
    pstr(&r, "com.\xffx");
    p16(&r, 0);
    finto_risposta(0x7b, r.dati, r.lunghezza);
    CHECK(ph_dati_calendario_intervallo(d, da, da + g, 10, &out, &err) == 0 && strcmp(out.app_calendario, "com.\xef\xbf\xbd" "x") == 0);
    ph_dati_calendario_intervallo_libera(&out);
    finto_risposta_errore(0x7b, "calendario non leggibile");
    CHECK(ph_dati_calendario_intervallo(d, da, da + g, 10, &out, &err) != 0 && strcmp(err.testo, "calendario non leggibile") == 0);
    CHECK(out.eventi == NULL && out.app_calendario == NULL);
    ph_buffer_libera(&r);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

static void prova_sveglia_batteria(void)
{
    PhDati *d = apri_dati();
    PhBuffer r = PH_BUFFER_VUOTO;
    PhDatiSveglia sv;
    p8(&r, 1);
    p64(&r, 1700000123000ull);
    pstr(&r, "com.orologio");
    finto_risposta(0x7c, r.dati, r.lunghezza);
    CHECK(ph_dati_sveglia(d, &sv, &err) == 0 && finto_ultimo_tipo() == 0x7c);
    uint8_t got[8];
    CHECK(finto_ultimo_corpo(got, sizeof got) == 0);
    CHECK(sv.presente && sv.ora_ms == 1700000123000ull && strcmp(sv.pacchetto, "com.orologio") == 0);
    ph_dati_sveglia_libera(&sv);
    CHECK(sv.pacchetto == NULL);
#define TIPO_ 0x7c
    PROVA_TRONCATE(r, ph_dati_sveglia(d, &sv, &err) == 0 ? (ph_dati_sveglia_libera(&sv), 0) : -1);
#undef TIPO_
    ph_buffer_svuota(&r);
    p8(&r, 0);
    p64(&r, 0);
    pstr(&r, "");
    finto_risposta(0x7c, r.dati, r.lunghezza);
    CHECK(ph_dati_sveglia(d, &sv, &err) == 0 && !sv.presente && sv.ora_ms == 0 && sv.pacchetto != NULL && sv.pacchetto[0] == '\0');
    ph_dati_sveglia_libera(&sv);
    /* Incoerenze: flag 2, presente senza ora, assente con ora. Pacchetto non valido ripulito. */
    unsigned flag[3] = {2, 1, 0};
    uint64_t ora[3] = {5, 0, 5};
    for (int i = 0; i < 3; i++) {
        ph_buffer_svuota(&r);
        p8(&r, flag[i]);
        p64(&r, ora[i]);
        pstr(&r, "");
        finto_risposta(0x7c, r.dati, r.lunghezza);
        CHECK(ph_dati_sveglia(d, &sv, &err) != 0 && sv.pacchetto == NULL);
    }
    ph_buffer_svuota(&r);
    p8(&r, 1);
    p64(&r, 9);
    pstr(&r, "a\xff" "b");
    finto_risposta(0x7c, r.dati, r.lunghezza);
    CHECK(ph_dati_sveglia(d, &sv, &err) == 0 && strcmp(sv.pacchetto, "a\xef\xbf\xbd" "b") == 0);
    ph_dati_sveglia_libera(&sv);
    finto_risposta_errore(0x7c, "sveglia non leggibile");
    CHECK(ph_dati_sveglia(d, &sv, &err) != 0 && strcmp(err.testo, "sveglia non leggibile") == 0);

    /* Batteria. */
    PhDatiBatteria b;
    ph_buffer_svuota(&r);
    p8(&r, 87);
    p8(&r, 1);
    p8(&r, 0x05);
    p16(&r, 312);
    p8(&r, 1);
    p16(&r, 4100);
    p32(&r, 42);
    p32(&r, 600);
    finto_risposta(0x7d, r.dati, r.lunghezza);
    CHECK(ph_dati_batteria(d, &b, &err) == 0 && finto_ultimo_tipo() == 0x7d && finto_ultimo_corpo(got, sizeof got) == 0);
    CHECK(b.livello == 87 && b.stato == PH_DATI_BAT_IN_CARICA && b.alimentazione == 5);
    CHECK(b.temperatura_nota && b.temperatura_decimi == 312 && b.salute == 1 && b.tensione_mv == 4100);
    CHECK(b.minuti_carica == 42 && b.minuti_autonomia == 600);
#define TIPO_ 0x7d
    PROVA_TRONCATE(r, ph_dati_batteria(d, &b, &err));
#undef TIPO_
    /* Non noto, fuori elenco, fuori misura. */
    ph_buffer_svuota(&r);
    p8(&r, 100);
    p8(&r, 9);          /* stato fuori elenco */
    p8(&r, 0xff);       /* alimentazione: solo i 4 bit noti */
    p16(&r, 0x7fff);    /* temperatura non nota */
    p8(&r, 9);          /* salute fuori elenco */
    p16(&r, 0);
    p32(&r, 0xffffffffu); /* -1: non noto */
    p32(&r, 0x80000000u); /* molto negativo: non noto */
    finto_risposta(0x7d, r.dati, r.lunghezza);
    CHECK(ph_dati_batteria(d, &b, &err) == 0);
    CHECK(b.stato == PH_DATI_BAT_SCONOSCIUTO && b.alimentazione == 0x0f && !b.temperatura_nota && b.temperatura_decimi == 0);
    CHECK(b.salute == 0 && b.tensione_mv == 0 && b.minuti_carica == -1 && b.minuti_autonomia == -1);
    uint16_t temp[3] = {2000, (uint16_t)-600, (uint16_t)-40};
    int atteso[3] = {1500, -500, -40};
    for (int i = 0; i < 3; i++) {
        ph_buffer_svuota(&r);
        p8(&r, 5);
        p8(&r, 4);
        p8(&r, 0);
        p16(&r, temp[i]);
        p8(&r, 0);
        p16(&r, 3000);
        p32(&r, 0);
        p32(&r, 0);
        finto_risposta(0x7d, r.dati, r.lunghezza);
        CHECK(ph_dati_batteria(d, &b, &err) == 0 && b.temperatura_nota && b.temperatura_decimi == atteso[i] && b.stato == PH_DATI_BAT_PIENA);
    }
    r.dati[0] = 101; /* livello oltre 100 */
    finto_risposta(0x7d, r.dati, r.lunghezza);
    CHECK(ph_dati_batteria(d, &b, &err) != 0);
    finto_risposta_errore(0x7d, "batteria non leggibile");
    CHECK(ph_dati_batteria(d, &b, &err) != 0 && strcmp(err.testo, "batteria non leggibile") == 0);
    ph_buffer_libera(&r);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

static atomic_int n_ctrl;
static PhDatiControlli ultimo_ctrl;
static void av_ctrl(const PhDatiControlli *c, void *u)
{
    (void)u;
    ultimo_ctrl = *c;
    atomic_fetch_add(&n_ctrl, 1);
}

static void prova_controlli(void)
{
    PhDati *d = apri_dati();
    const uint8_t ok[5] = {1, 0, 0xff, 1, 0x0b};
    PhDatiControlli c;
    finto_risposta(0x80, ok, 5);
    CHECK(ph_dati_controlli(d, &c, &err) == 0 && finto_ultimo_tipo() == 0x80);
    uint8_t got[8];
    CHECK(finto_ultimo_corpo(got, sizeof got) == 0);
    CHECK(c.suoneria == 1 && c.non_disturbare == 0 && c.torcia == PH_DATI_NON_DISPONIBILE && c.trova_in_corso);
    CHECK(c.puo_suoneria && c.puo_non_disturbare && !c.puo_torcia && c.puo_trova);
    PhBuffer r = PH_BUFFER_VUOTO;
    pbytes(&r, ok, 5);
#define TIPO_ 0x80
    PROVA_TRONCATE(r, ph_dati_controlli(d, &c, &err));
#undef TIPO_
    ph_buffer_libera(&r);
    /* Un campo fuori intervallo per volta; 0xff dove e' ammesso; bit sconosciuti dei comandi ignorati. */
    const uint8_t brutti[4][5] = {{3, 0, 0, 0, 0}, {0, 2, 0, 0, 0}, {0, 0, 2, 0, 0}, {0, 0, 0, 2, 0}};
    for (int i = 0; i < 4; i++) {
        finto_risposta(0x80, brutti[i], 5);
        CHECK(ph_dati_controlli(d, &c, &err) != 0);
    }
    const uint8_t tutti[5] = {0xff, 0xff, 0xff, 0, 0xf0};
    finto_risposta(0x80, tutti, 5);
    CHECK(ph_dati_controlli(d, &c, &err) == 0 && c.suoneria == PH_DATI_NON_DISPONIBILE && c.non_disturbare == PH_DATI_NON_DISPONIBILE);
    CHECK(!c.puo_suoneria && !c.puo_non_disturbare && !c.puo_torcia && !c.puo_trova && !c.trova_in_corso);

    /* Comandi: byte attesi e argomenti validati prima di mandare. */
    finto_risposta(0x81, NULL, 0);
    finto_risposta(0x82, NULL, 0);
    finto_risposta(0x83, NULL, 0);
    finto_risposta(0x84, NULL, 0);
    finto_risposta(0x85, NULL, 0);
    CHECK(ph_dati_suoneria_imposta(d, 2, &err) == 0 && finto_ultimo_tipo() == 0x81);
    CHECK(finto_ultimo_corpo(got, sizeof got) == 1 && got[0] == 2);
    CHECK(ph_dati_non_disturbare_imposta(d, true, &err) == 0 && finto_ultimo_tipo() == 0x82);
    CHECK(finto_ultimo_corpo(got, sizeof got) == 1 && got[0] == 1);
    CHECK(ph_dati_non_disturbare_imposta(d, false, &err) == 0 && finto_ultimo_corpo(got, sizeof got) == 1 && got[0] == 0);
    CHECK(ph_dati_torcia_imposta(d, true, &err) == 0 && finto_ultimo_tipo() == 0x83);
    CHECK(finto_ultimo_corpo(got, sizeof got) == 1 && got[0] == 1);
    CHECK(ph_dati_torcia_imposta(d, false, &err) == 0 && finto_ultimo_corpo(got, sizeof got) == 1 && got[0] == 0);
    CHECK(ph_dati_trova_avvia(d, 30, &err) == 0 && finto_ultimo_tipo() == 0x84);
    CHECK(finto_ultimo_corpo(got, sizeof got) == 2 && got[0] == 0 && got[1] == 30);
    CHECK(ph_dati_trova_avvia(d, 120, &err) == 0 && ph_dati_trova_avvia(d, 5, &err) == 0);
    CHECK(ph_dati_trova_ferma(d, &err) == 0 && finto_ultimo_tipo() == 0x85 && finto_ultimo_corpo(got, sizeof got) == 0);
    int richieste = finto_richieste();
    CHECK(ph_dati_suoneria_imposta(d, -1, &err) != 0);
    CHECK(ph_dati_suoneria_imposta(d, 3, &err) != 0);
    CHECK(ph_dati_trova_avvia(d, 4, &err) != 0);
    CHECK(ph_dati_trova_avvia(d, 121, &err) != 0);
    CHECK(ph_dati_trova_avvia(d, 0, &err) != 0);
    CHECK(finto_richieste() == richieste);
    finto_risposta_errore(0x83, "la torcia non e' disponibile");
    CHECK(ph_dati_torcia_imposta(d, true, &err) != 0 && strcmp(err.testo, "la torcia non e' disponibile") == 0);
    finto_risposta_errore(0x80, "controlli non disponibili");
    CHECK(ph_dati_controlli(d, &c, &err) != 0 && strcmp(err.testo, "controlli non disponibili") == 0);

    /* Avviso 0x8e: valido si', malformato no; e' separato da 0x7e/0x7f. */
    CHECK(!finto_ascolto_per(0x8e) && finto_ascolto_per(0x7e));
    CHECK(ph_dati_ascolta_controlli(d, av_ctrl, NULL, &err) == 0 && finto_ascolto_per(0x8e));
    int prima = atomic_load(&n_ctrl);
    finto_evento(0x8e, ok, 5);
    CHECK(atomic_load(&n_ctrl) == prima + 1 && ultimo_ctrl.suoneria == 1 && ultimo_ctrl.puo_trova);
    for (size_t k = 0; k < 5; k++)
        finto_evento(0x8e, ok, k);
    finto_evento(0x8e, NULL, 0);
    finto_evento(0x8e, brutti[0], 5);
    CHECK(atomic_load(&n_ctrl) == prima + 1);
    /* Sostituzione e rimozione. */
    CHECK(ph_dati_ascolta_controlli(d, av_ctrl, NULL, &err) == 0);
    finto_evento(0x8e, ok, 5);
    CHECK(atomic_load(&n_ctrl) == prima + 2);
    CHECK(ph_dati_ascolta_controlli(d, NULL, NULL, &err) == 0 && !finto_ascolto_per(0x8e));
    finto_evento(0x8e, ok, 5);
    CHECK(atomic_load(&n_ctrl) == prima + 2);
    CHECK(ph_dati_ascolta_controlli(d, NULL, NULL, &err) == 0); /* innocua */
    CHECK(ph_dati_ascolta_controlli(d, av_ctrl, NULL, &err) == 0);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
    CHECK(!finto_ascolto_per(0x8e) && !finto_ascolto_per(0x7e));

    /* Chiusura rifiutata per un guasto nel togliere l'ascolto: i due ascolti restano. */
    d = apri_dati();
    CHECK(ph_dati_ascolta_controlli(d, av_ctrl, NULL, &err) == 0);
    finto_togliere_ascolto_fallisce_alla_n(1); /* fallisce 0x8e: nulla toccato */
    CHECK(ph_dati_chiudi(d, &err) == -1);
    CHECK(finto_ascolto_per(0x8e) && finto_ascolto_per(0x7e));
    prima = atomic_load(&n_ctrl);
    finto_evento(0x8e, ok, 5);
    CHECK(atomic_load(&n_ctrl) == prima + 1);
    finto_togliere_ascolto_fallisce_alla_n(2); /* 0x8e tolto, poi fallisce 0x7e: 0x8e rimesso */
    CHECK(ph_dati_chiudi(d, &err) == -1);
    CHECK(finto_ascolto_per(0x8e) && finto_ascolto_per(0x7e));
    finto_evento(0x8e, ok, 5);
    CHECK(atomic_load(&n_ctrl) == prima + 2);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
    CHECK(!finto_ascolto_per(0x8e) && !finto_ascolto_per(0x7e));
}

/* ---- Controlli: eventi durante e dopo la chiusura, chiamate nuove in corso ---------*/
static void *manda_eventi_ctrl(void *a)
{
    Eventi *e = a;
    const uint8_t st[5] = {0, 1, 1, 0, 0x0f};
    while (!atomic_load(&e->ferma))
        finto_evento(0x8e, st, 5);
    return NULL;
}

static void prova_controlli_chiusura(void)
{
    for (int i = 0; i < 30; i++) {
        PhDati *d = apri_dati();
        CHECK(ph_dati_ascolta_controlli(d, av_ctrl, NULL, &err) == 0);
        Eventi ev;
        atomic_init(&ev.ferma, false);
        pthread_t t;
        CHECK(pthread_create(&t, NULL, manda_eventi_ctrl, &ev) == 0);
        int prima = atomic_load(&n_ctrl);
        ATTENDI(atomic_load(&n_ctrl) > prima + 2, 3000);
        CHECK(ph_dati_chiudi(d, NULL) == 0);
        int n = atomic_load(&n_ctrl);
        /* nessun avviso dopo il ritorno: il finto non ha piu' l'ascoltatore */
        CHECK(!finto_ascolto_per(0x8e));
        ASSENZA_FINO_A(atomic_load(&n_ctrl) != n, 5);
        CHECK(atomic_load(&n_ctrl) == n);
        atomic_store(&ev.ferma, true);
        pthread_join(t, NULL);
    }
}

/* Togliere l'ascolto dei controlli mentre un avviso e' in corso: aspetta la sua fine. */
static pthread_mutex_t cc_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cc_cv = PTHREAD_COND_INITIALIZER;
static bool cc_dentro, cc_libera;
static void av_ctrl_bloccato(const PhDatiControlli *c, void *u)
{
    (void)c;
    (void)u;
    pthread_mutex_lock(&cc_m);
    cc_dentro = true;
    pthread_cond_broadcast(&cc_cv);
    while (!cc_libera)
        pthread_cond_wait(&cc_cv, &cc_m);
    pthread_mutex_unlock(&cc_m);
    atomic_fetch_add(&n_ctrl, 1);
}
static void *evento_unico(void *a)
{
    (void)a;
    const uint8_t st[5] = {0, 0, 0, 0, 0};
    finto_evento(0x8e, st, 5);
    return NULL;
}
static void *giro_toglie_ascolto(void *a)
{
    Giro *g = a;
    g->esito = ph_dati_ascolta_controlli(g->d, NULL, NULL, NULL);
    atomic_store(&g->fatto, true);
    return NULL;
}

static void prova_controlli_avviso_in_corso(void)
{
    PhDati *d = apri_dati();
    cc_dentro = false;
    cc_libera = false;
    CHECK(ph_dati_ascolta_controlli(d, av_ctrl_bloccato, NULL, &err) == 0);
    int prima = atomic_load(&n_ctrl);
    pthread_t te, tx;
    CHECK(pthread_create(&te, NULL, evento_unico, NULL) == 0);
    pthread_mutex_lock(&cc_m);
    while (!cc_dentro)
        pthread_cond_wait(&cc_cv, &cc_m);
    pthread_mutex_unlock(&cc_m);
    Giro x = {d, -9, false};
    CHECK(pthread_create(&tx, NULL, giro_toglie_ascolto, &x) == 0);
    /* Il componente (qui il finto) fa aspettare chi toglie l'ascolto finche' l'avviso non finisce. */
    ASSENZA_FINO_A(atomic_load(&x.fatto), 150);
    CHECK(!atomic_load(&x.fatto));
    pthread_mutex_lock(&cc_m);
    cc_libera = true;
    pthread_cond_broadcast(&cc_cv);
    pthread_mutex_unlock(&cc_m);
    pthread_join(te, NULL);
    pthread_join(tx, NULL);
    CHECK(x.esito == 0 && atomic_load(&n_ctrl) == prima + 1);
    const uint8_t st[5] = {0, 0, 0, 0, 0};
    finto_evento(0x8e, st, 5);
    CHECK(atomic_load(&n_ctrl) == prima + 1);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

static void *giro_controlli(void *a)
{
    Giro *g = a;
    PhDatiControlli c;
    g->esito = ph_dati_controlli(g->d, &c, NULL);
    atomic_store(&g->fatto, true);
    return NULL;
}
static void *giro_intervallo(void *a)
{
    Giro *g = a;
    PhDatiCalendarioIntervallo c;
    g->esito = ph_dati_calendario_intervallo(g->d, 1000, 2000, 10, &c, NULL);
    if (g->esito == 0)
        ph_dati_calendario_intervallo_libera(&c);
    atomic_store(&g->fatto, true);
    return NULL;
}

static void prova_chiudi_con_chiamate_nuove_in_corso(void)
{
    PhDati *d = apri_dati();
    const uint8_t st[5] = {0, 0, 0, 0, 0};
    finto_risposta(0x80, st, 5);
    PhBuffer r = PH_BUFFER_VUOTO;
    p8(&r, 0);
    pstr(&r, "");
    p16(&r, 0);
    finto_risposta(0x7b, r.dati, r.lunghezza);
    ph_buffer_libera(&r);
    CHECK(ph_dati_ascolta_controlli(d, av_ctrl, NULL, &err) == 0);
    finto_cancello(true);
    Giro a = {d, -9, false}, b = {d, -9, false}, c = {d, -9, false};
    pthread_t ta, tb, tc;
    CHECK(pthread_create(&ta, NULL, giro_controlli, &a) == 0);
    CHECK(pthread_create(&tb, NULL, giro_intervallo, &b) == 0);
    ATTENDI(finto_cancello_attese() == 2, 3000);
    CHECK(pthread_create(&tc, NULL, giro_chiudi, &c) == 0);
    ATTENDI(chiudendo(d), 3000);
    /* Ne' l'ascolto dei controlli ne' gli altri si tolgono finche' le richieste sono ferme. */
    ASSENZA_FINO_A(finto_violazioni() > 0 || atomic_load(&c.fatto), 150);
    CHECK(finto_violazioni() == 0 && !atomic_load(&c.fatto) && finto_ascolto_per(0x8e));
    finto_cancello(false);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    pthread_join(tc, NULL);
    CHECK(a.esito == 0 && b.esito == 0 && c.esito == 0);
    CHECK(!finto_ascolto_registrato() && finto_violazioni() == 0);
}

/* ---- Ascolto dei controlli: sostituzione fallita, gara, caso estremo, da un avviso ---*/
static atomic_int n_ctrl_b;
static void av_ctrl_b(const PhDatiControlli *c, void *u)
{
    (void)c;
    (void)u;
    atomic_fetch_add(&n_ctrl_b, 1);
}

static bool ctrl_registrato(PhDati *d)
{
    pthread_mutex_lock(&d->m_ric);
    bool r = d->ctrl_registrato;
    pthread_mutex_unlock(&d->m_ric);
    return r;
}

static void *giro_registra_ascolto(void *a)
{
    Giro *g = a;
    g->esito = ph_dati_ascolta_controlli(g->d, av_ctrl, NULL, NULL);
    atomic_store(&g->fatto, true);
    return NULL;
}

static atomic_int g_ctrl_esito;
static void av_ctrl_riascolta(const PhDatiControlli *c, void *u)
{
    (void)c;
    (void)u;
    /* Da dentro un avviso: rifiutata (il componente si bloccherebbe). */
    atomic_store(&g_ctrl_esito, ph_dati_ascolta_controlli(g_d, av_ctrl, NULL, NULL) == -1 ? 1 : 2);
}

static void prova_controlli_ascolto(void)
{
    const uint8_t ok[5] = {1, 0, 0, 0, 0x0f};
    /* (1) Sostituzione fallita: restano avviso e dato di prima. */
    PhDati *d = apri_dati();
    CHECK(ph_dati_ascolta_controlli(d, av_ctrl, NULL, &err) == 0);
    finto_registrare_ascolto_fallisce_alla_n(1);
    CHECK(ph_dati_ascolta_controlli(d, av_ctrl_b, NULL, &err) == -1);
    int a0 = atomic_load(&n_ctrl), b0 = atomic_load(&n_ctrl_b);
    finto_evento(0x8e, ok, 5);
    CHECK(atomic_load(&n_ctrl) == a0 + 1 && atomic_load(&n_ctrl_b) == b0);
    /* senza ascolto precedente, una registrazione fallita non lascia nulla */
    CHECK(ph_dati_ascolta_controlli(d, NULL, NULL, &err) == 0);
    finto_registrare_ascolto_fallisce_alla_n(1);
    CHECK(ph_dati_ascolta_controlli(d, av_ctrl_b, NULL, &err) == -1 && !ctrl_registrato(d) && d->ctrl_avviso == NULL);
    CHECK(ph_dati_chiudi(d, NULL) == 0);

    /* (2) Registra e toglie in parallelo: lo stato finale e' coerente con il componente. */
    d = apri_dati();
    finto_ritardo_ascolta_ms(1);
    CHECK(ph_dati_ascolta_controlli(d, av_ctrl, NULL, &err) == 0);
    for (int i = 0; i < 150; i++) {
        Giro r = {d, -9, false}, x = {d, -9, false};
        pthread_t tr, tx;
        CHECK(pthread_create(&tr, NULL, giro_registra_ascolto, &r) == 0);
        CHECK(pthread_create(&tx, NULL, giro_toglie_ascolto, &x) == 0);
        pthread_join(tr, NULL);
        pthread_join(tx, NULL);
        CHECK(r.esito == 0 && x.esito == 0);
        CHECK(ctrl_registrato(d) == finto_ascolto_per(0x8e));
        if (!ctrl_registrato(d)) /* si riparte da registrato: e' la situazione che si contende */
            CHECK(ph_dati_ascolta_controlli(d, av_ctrl, NULL, &err) == 0);
    }
    finto_ritardo_ascolta_ms(0);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
    CHECK(!finto_ascolto_per(0x8e) && !finto_ascolto_per(0x7e));

    /* (3) Caso estremo di ph_dati_chiudi: fallisce la rimozione di 0x7e e anche il
     * ripristino di 0x8e. -1, l'ascolto dei controlli resta tolto e si puo' riregistrare. */
    d = apri_dati();
    CHECK(ph_dati_ascolta_controlli(d, av_ctrl, NULL, &err) == 0);
    finto_togliere_ascolto_fallisce_alla_n(2);
    finto_registrare_ascolto_fallisce_alla_n(1);
    CHECK(ph_dati_chiudi(d, &err) == -1);
    CHECK(!ctrl_registrato(d) && !finto_ascolto_per(0x8e) && finto_ascolto_per(0x7e));
    CHECK(ph_dati_ascolta_controlli(d, av_ctrl, NULL, &err) == 0 && finto_ascolto_per(0x8e));
    a0 = atomic_load(&n_ctrl);
    finto_evento(0x8e, ok, 5);
    CHECK(atomic_load(&n_ctrl) == a0 + 1);
    CHECK(ph_dati_chiudi(d, NULL) == 0);

    /* Da dentro un avviso ph_dati_ascolta_controlli e' rifiutata. */
    d = apri_dati();
    g_d = d;
    atomic_store(&g_ctrl_esito, 0);
    CHECK(ph_dati_ascolta_controlli(d, av_ctrl_riascolta, NULL, &err) == 0);
    finto_evento(0x8e, ok, 5);
    CHECK(atomic_load(&g_ctrl_esito) == 1);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

/* ---- Posizione approssimativa (0x90) --------------------------------------------------*/
/* Corpo dell'OK: esito, lat, lon, eta, fornitore, localita (stringa: u16 + byte), ricerca avviata. */
static void pos(PhBuffer *b, unsigned esito, int32_t lat, int32_t lon, uint32_t eta, unsigned forn,
                const void *loc, size_t n, unsigned ricerca)
{
    p8(b, esito);
    p32(b, (uint32_t)lat);
    p32(b, (uint32_t)lon);
    p32(b, eta);
    p8(b, forn);
    p16(b, (unsigned)n); /* n puo' superare 16 bit solo nelle prove di lunghezze false: si tronca come sul filo */
    pbytes(b, loc, n);
    p8(b, ricerca);
}
static void poss(PhBuffer *b, unsigned esito, int32_t lat, int32_t lon, uint32_t eta, unsigned forn,
                 const char *loc, unsigned ricerca)
{
    pos(b, esito, lat, lon, eta, forn, loc, strlen(loc), ricerca);
}

/* Una risposta data: torna il risultato di ph_dati_posizione. */
static int posizione_con(PhDati *d, const PhBuffer *r, PhDatiPosizione *p)
{
    finto_risposta(0x90, r->dati, r->lunghezza);
    return ph_dati_posizione(d, true, p, &err);
}

static void prova_posizione(void)
{
    PhDati *d = apri_dati();
    PhBuffer r = PH_BUFFER_VUOTO;
    poss(&r, 0, 45005, 7650, 3600, 0, "Localita-segreta", 1);
    PhDatiPosizione p;
    CHECK(r.lunghezza == 17 + 16);
    CHECK(posizione_con(d, &r, &p) == 0);
    uint8_t got[4];
    CHECK(finto_ultimo_tipo() == 0x90 && finto_ultimo_corpo(got, sizeof got) == 1 && got[0] == 1);
    CHECK(p.esito == PH_DATI_POS_NOTA && p.lat_millesimi == 45005 && p.lon_millesimi == 7650);
    CHECK(p.eta_nota && p.eta_s == 3600 && p.fornitore == 0 && strcmp(p.localita, "Localita-segreta") == 0);
    CHECK(p.ricerca_avviata);
    CHECK(ph_dati_posizione(d, false, &p, &err) == 0 && finto_ultimo_corpo(got, sizeof got) == 1 && got[0] == 0);
#define TIPO_ 0x90
    PROVA_TRONCATE(r, ph_dati_posizione(d, true, &p, &err));
#undef TIPO_
    /* Il messaggio d'errore non contiene mai dati di posizione; a errore *out e' sconosciuta e azzerata. */
    for (size_t k = 0; k < r.lunghezza; k++) {
        finto_risposta(0x90, r.dati, k);
        CHECK(ph_dati_posizione(d, true, &p, &err) != 0);
        CHECK(p.esito == PH_DATI_POS_SCONOSCIUTA && p.lat_millesimi == 0 && p.localita[0] == '\0' && !p.ricerca_avviata);
        CHECK(strstr(err.testo, "segreta") == NULL && strstr(err.testo, "45005") == NULL);
    }
    /* Esiti 1-3 senza coordinate ne' localita': validi. Fuori elenco: errore. */
    for (unsigned es = 1; es <= 5; es++) {
        ph_buffer_svuota(&r);
        poss(&r, es, 0, 0, 0xffffffffu, 1, "", 0);
        CHECK((posizione_con(d, &r, &p) == 0) == (es <= 3));
        if (es <= 3)
            CHECK(p.esito == (PhDatiPosEsito)es && !p.eta_nota && p.localita[0] == '\0' && !p.ricerca_avviata);
    }
    /* Coordinate o localita' con esito diverso da "nota": errore. */
    for (unsigned es = 1; es <= 3; es++) {
        ph_buffer_svuota(&r);
        poss(&r, es, 1, 0, 0, 0, "", 0);
        CHECK(posizione_con(d, &r, &p) != 0);
        ph_buffer_svuota(&r);
        poss(&r, es, 0, -1, 0, 0, "", 0);
        CHECK(posizione_con(d, &r, &p) != 0);
        ph_buffer_svuota(&r);
        poss(&r, es, 0, 0, 0, 0, "x", 0);
        CHECK(posizione_con(d, &r, &p) != 0);
    }
    /* Limiti delle coordinate (millesimi): esatti passano, di 5 oltre e' errore; i valori
     * che non sono multipli di 5 (il telefono non ha arrotondato) sono un errore. */
    struct { int32_t lat, lon; bool ok; } c[] = {
        {90000, 180000, true}, {-90000, -180000, true}, {90005, 0, false}, {-90005, 0, false},
        {0, 180005, false}, {0, -180005, false}, {INT32_MAX, 0, false}, {0, INT32_MIN, false},
        {45457, 0, false}, {0, 7651, false}, {1, 0, false}, {0, -4, false}, {45455, 7655, true},
        {5, -5, true}, {-45455, 7655, true}, {45456, 7655, false}};
    for (size_t i = 0; i < sizeof c / sizeof *c; i++) {
        ph_buffer_svuota(&r);
        poss(&r, 0, c[i].lat, c[i].lon, 0xffffffffu, 3, "x", 0);
        CHECK((posizione_con(d, &r, &p) == 0) == c[i].ok);
        if (c[i].ok)
            CHECK(p.lat_millesimi == c[i].lat && p.lon_millesimi == c[i].lon && !p.eta_nota && p.eta_s == 0);
    }
    /* Fornitore e "ricerca avviata" fuori elenco. */
    ph_buffer_svuota(&r);
    poss(&r, 0, 5, 5, 5, 4, "x", 0);
    CHECK(posizione_con(d, &r, &p) != 0);
    ph_buffer_svuota(&r);
    poss(&r, 0, 5, 5, 5, 3, "x", 2);
    CHECK(posizione_con(d, &r, &p) != 0);
    ph_buffer_svuota(&r);
    poss(&r, 0, 5, 5, 5, 3, "x", 0);
    CHECK(posizione_con(d, &r, &p) == 0 && !p.ricerca_avviata && p.fornitore == 3);
    /* Localita': UTF-8 non valido o con NUL = errore; controlli in UTF-8 valido ripuliti. */
    const char *brutte[] = {"A\xff", "\xc0\xaf", "\xed\xa0\x80", "x\xe2\x82", "\xf4\x90\x80\x80"};
    for (size_t i = 0; i < sizeof brutte / sizeof *brutte; i++) {
        ph_buffer_svuota(&r);
        poss(&r, 0, 5, 5, 5, 2, brutte[i], 0);
        CHECK(posizione_con(d, &r, &p) != 0 && p.localita[0] == '\0');
    }
    ph_buffer_svuota(&r);
    pos(&r, 0, 5, 5, 5, 2, "a\0b", 3, 0);
    CHECK(posizione_con(d, &r, &p) != 0);
    ph_buffer_svuota(&r);
    poss(&r, 0, 5, 5, 5, 2, "Ro\xc3\xa8" "ma\nTer\x01mini\xc2\x85", 0);
    CHECK(posizione_con(d, &r, &p) == 0 && utf8_valido(p.localita) && strcmp(p.localita, "Ro\xc3\xa8" "ma Ter mini ") == 0);
    /* Lunghezza della localita': 127 passa, 128 e oltre no (anche enorme o falsa). */
    char lunga[400];
    memset(lunga, 'a', sizeof lunga - 1);
    lunga[sizeof lunga - 1] = '\0';
    ph_buffer_svuota(&r);
    pos(&r, 0, 5, 5, 5, 2, lunga, 127, 0);
    CHECK(posizione_con(d, &r, &p) == 0 && strlen(p.localita) == 127);
    ph_buffer_svuota(&r);
    pos(&r, 0, 5, 5, 5, 2, lunga, 128, 0);
    CHECK(posizione_con(d, &r, &p) != 0);
    ph_buffer_svuota(&r);
    pos(&r, 0, 5, 5, 5, 2, lunga, 300, 0);
    CHECK(posizione_con(d, &r, &p) != 0);
    unsigned falsi[] = {1, 5, 0xffff, 0x8000};
    for (size_t i = 0; i < sizeof falsi / sizeof *falsi; i++) {
        ph_buffer_svuota(&r);
        p8(&r, 0);
        p32(&r, 5);
        p32(&r, 5);
        p32(&r, 5);
        p8(&r, 2);
        p16(&r, falsi[i]); /* dichiara piu' byte di quelli che seguono */
        pbytes(&r, "abc", 3);
        p8(&r, 0);
        CHECK(posizione_con(d, &r, &p) != 0);
    }
    /* Mancano il byte "ricerca avviata" (formato vecchio): non e' valido. */
    ph_buffer_svuota(&r);
    p8(&r, 0);
    p32(&r, 5);
    p32(&r, 5);
    p32(&r, 5);
    p8(&r, 2);
    pstr(&r, "Roma");
    CHECK(posizione_con(d, &r, &p) != 0);
    /* Lunghezza della localita' a 32 bit (19 + n byte): non e' il protocollo. */
    {
        const uint8_t u32[] = {0, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 2, 0, 0, 0, 1, 'x', 0};
        finto_risposta(0x90, u32, sizeof u32);
        CHECK(ph_dati_posizione(d, true, &p, &err) != 0);
    }
    /* Byte scritti a mano dal protocollo (non prodotti dal codificatore delle prove):
     * esito 0 | lat 45005 (0xafcd) | lon 7650 (0x1de2) | eta 3600 | fornitore 1 | stringa "Roma" (u16 4) | ricerca 1:
     * 17 + 4 = 21 byte. Una risposta vuota di localita' e senza posizione: 17 byte. */
    {
        const uint8_t a[] = {0x00, 0x00, 0x00, 0xaf, 0xcd, 0x00, 0x00, 0x1d, 0xe2, 0x00, 0x00, 0x0e,
                             0x10, 0x01, 0x00, 0x04, 0x52, 0x6f, 0x6d, 0x61, 0x01};
        CHECK(sizeof a == 17 + 4);
        finto_risposta(0x90, a, sizeof a);
        CHECK(ph_dati_posizione(d, true, &p, &err) == 0);
        CHECK(p.esito == PH_DATI_POS_NOTA && p.lat_millesimi == 45005 && p.lon_millesimi == 7650);
        CHECK(p.eta_nota && p.eta_s == 3600 && p.fornitore == 1 && strcmp(p.localita, "Roma") == 0 && p.ricerca_avviata);
        const uint8_t b[] = {0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00};
        CHECK(sizeof b == 17);
        finto_risposta(0x90, b, sizeof b);
        CHECK(ph_dati_posizione(d, false, &p, &err) == 0 && p.esito == PH_DATI_POS_SCONOSCIUTA);
        CHECK(!p.eta_nota && p.localita[0] == '\0' && !p.ricerca_avviata && p.lat_millesimi == 0);
    }
    /* ERRORE del telefono e argomento mancante. */
    finto_risposta_errore(0x90, "posizione non disponibile");
    CHECK(ph_dati_posizione(d, true, &p, &err) != 0 && strcmp(err.testo, "posizione non disponibile") == 0);
    CHECK(p.esito == PH_DATI_POS_SCONOSCIUTA && p.localita[0] == '\0');
    CHECK(ph_dati_posizione(d, true, NULL, &err) != 0);
    /* Corpi casuali: mai fuori da un buffer; meta' hanno la struttura giusta con campi
     * qualunque (localita' da 0 a 140 byte, validi e no). Se riesce, i valori rispettano i limiti. */
    srand(777);
    int riusciti = 0;
    static const char *pezzi[] = {"a", "Z", " ", "\xc3\xa8", "\xe2\x82\xac", "\xf0\x9f\x98\x80", "\n", "\x01", "\xc2\x85", "\xff", "\xc0"};
    for (int i = 0; i < 40000; i++) {
        PhBuffer f = PH_BUFFER_VUOTO;
        if (rand() % 2) {
            PhBuffer s = PH_BUFFER_VUOTO;
            size_t voglio = (size_t)(rand() % 141);
            bool pulita = rand() % 4 != 0; /* senza byte non validi */
            while (s.lunghezza < voglio) {
                const char *z = pezzi[rand() % (pulita ? 9 : 11)];
                pbytes(&s, z, strlen(z));
            }
            unsigned es = rand() % 3 ? 0 : (unsigned)(rand() % 6);
            bool coord = es == 0 || rand() % 5 == 0;
            int32_t la = coord ? (int32_t)(rand() % 6 ? 5 * (rand() % 36001 - 18000) + (rand() % 12 == 0) : (int32_t)rand()) : 0;
            int32_t lo = coord ? (int32_t)(rand() % 6 ? 5 * (rand() % 72001 - 36000) + (rand() % 12 == 0) : (int32_t)rand()) : 0;
            size_t nn = s.lunghezza;
            if (rand() % 12 == 0)
                nn = (size_t)rand() % 200; /* lunghezza dichiarata falsa */
            pos(&f, es, la, lo, (uint32_t)rand(), (unsigned)(rand() % 9 == 0 ? rand() % 6 : rand() % 4), s.dati ? s.dati : (const uint8_t *)"", nn <= s.lunghezza ? nn : s.lunghezza, (unsigned)(rand() % 4 ? rand() % 2 : rand() % 4));
            if (nn > s.lunghezza) { /* dichiarata piu' lunga di quella scritta */
                f.dati[14 + 1] = (uint8_t)nn;
            }
            if (rand() % 10 == 0)
                p8(&f, 1); /* coda */
            if (rand() % 10 == 0 && f.lunghezza > 1)
                f.lunghezza -= 1; /* troncata */
            ph_buffer_libera(&s);
        } else {
            size_t n = (size_t)(rand() % 40);
            for (size_t k = 0; k < n; k++)
                p8(&f, (unsigned)rand());
        }
        finto_risposta(0x90, f.dati, f.lunghezza);
        ph_buffer_libera(&f);
        if (ph_dati_posizione(d, rand() % 2, &p, &err) == 0) {
            riusciti++;
            CHECK(p.esito <= PH_DATI_POS_SCONOSCIUTA && p.fornitore <= 3 && strlen(p.localita) <= 127 && utf8_valido(p.localita));
            CHECK(p.lat_millesimi >= -90000 && p.lat_millesimi <= 90000 && p.lon_millesimi >= -180000 && p.lon_millesimi <= 180000);
            CHECK(p.lat_millesimi % 5 == 0 && p.lon_millesimi % 5 == 0);
            CHECK(p.esito == PH_DATI_POS_NOTA || (p.lat_millesimi == 0 && p.lon_millesimi == 0 && p.localita[0] == '\0'));
        } else {
            CHECK(p.esito == PH_DATI_POS_SCONOSCIUTA && p.lat_millesimi == 0 && p.localita[0] == '\0' && !p.ricerca_avviata);
        }
    }
    CHECK(riusciti > 3000);
    ph_buffer_libera(&r);
    CHECK(ph_dati_chiudi(d, NULL) == 0);
}

static void *giro_posizione(void *a)
{
    Giro *g = a;
    PhDatiPosizione p;
    g->esito = ph_dati_posizione(g->d, true, &p, NULL);
    atomic_store(&g->fatto, true);
    return NULL;
}

static void prova_posizione_chiusura(void)
{
    PhDati *d = apri_dati();
    PhBuffer r = PH_BUFFER_VUOTO;
    poss(&r, 0, 5, 5, 5, 2, "x", 0);
    finto_risposta(0x90, r.dati, r.lunghezza);
    ph_buffer_libera(&r);
    finto_cancello(true);
    Giro a = {d, -9, false}, b = {d, -9, false};
    pthread_t ta, tb;
    CHECK(pthread_create(&ta, NULL, giro_posizione, &a) == 0);
    ATTENDI(finto_cancello_attese() == 1, 3000);
    CHECK(pthread_create(&tb, NULL, giro_chiudi, &b) == 0);
    ATTENDI(chiudendo(d), 3000);
    ASSENZA_FINO_A(finto_violazioni() > 0 || atomic_load(&b.fatto), 150);
    CHECK(finto_violazioni() == 0 && !atomic_load(&b.fatto));
    PhDatiPosizione p;
    CHECK(ph_dati_posizione(d, false, &p, &err) == -1 && strstr(err.testo, "chiusura") != NULL);
    finto_cancello(false);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    CHECK(a.esito == 0 && b.esito == 0 && finto_violazioni() == 0);
}

int main(int argc, char **argv)
{
    bool solo_concorrenza = argc > 1 && strcmp(argv[1], "concorrenza") == 0;
    if (!solo_concorrenza) {
        prova_utf8();
        prova_app();
        prova_icona_miniatura();
        prova_stato_memoria();
        prova_notifiche();
        prova_cerca_base();
        prova_cerca_malformata();
        prova_appunti();
        prova_appunti_cadute();
        prova_widget();
        prova_calendario_intervallo();
        prova_sveglia_batteria();
        prova_controlli();
        prova_posizione();
    }
    prova_autochiusura();
    prova_chiudi_con_chiamata_in_corso();
    prova_controlli_ascolto();
    prova_posizione_chiusura();
    prova_controlli_chiusura();
    prova_controlli_avviso_in_corso();
    prova_chiudi_con_chiamate_nuove_in_corso();
    prova_chiudi_rifiutata_con_ricerca();
    prova_chiudi_con_appunti_chiudi_in_corso();
    prova_chiudi_con_ascolto_guasto();
    prova_cerca_concorrenza();
    prova_appunti_concorrenza();
    prova_chiudi_con_eventi();
    finto_azzera();
    free(app.ultimo);
    printf("prova dati: ok\n");
    return 0;
}
