/* Telefono finto: vedi finto.h. */
#include "finto.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "adb/adb.h"
#include "file/file.h"

/* ---- Tubi ----------------------------------------------------------------*/
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t cv;
    uint8_t *d;
    size_t n, cap;
    bool chiuso;  /* fine normale: i dati rimasti si leggono, poi 0 */
    bool errore;  /* caduta: tutto torna errore subito */
} Tubo;

static void tubo_init(Tubo *t)
{
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_mutex_init(&t->m, NULL);
    pthread_cond_init(&t->cv, &ca);
    pthread_condattr_destroy(&ca);
    t->d = NULL;
    t->n = t->cap = 0;
    t->chiuso = t->errore = false;
}

static void tubo_libera(Tubo *t)
{
    free(t->d);
    pthread_mutex_destroy(&t->m);
    pthread_cond_destroy(&t->cv);
}

static void tubo_chiudi(Tubo *t, bool errore)
{
    pthread_mutex_lock(&t->m);
    t->chiuso = true;
    if (errore)
        t->errore = true;
    pthread_cond_broadcast(&t->cv);
    pthread_mutex_unlock(&t->m);
}

static int tubo_scrivi(Tubo *t, const void *dati, size_t n)
{
    pthread_mutex_lock(&t->m);
    if (t->chiuso) {
        pthread_mutex_unlock(&t->m);
        return -1;
    }
    if (t->n + n > t->cap) {
        size_t nc = (t->n + n) * 2 + 64;
        uint8_t *p = realloc(t->d, nc);
        if (p == NULL) {
            pthread_mutex_unlock(&t->m);
            return -1;
        }
        t->d = p;
        t->cap = nc;
    }
    memcpy(t->d + t->n, dati, n);
    t->n += n;
    pthread_cond_broadcast(&t->cv);
    pthread_mutex_unlock(&t->m);
    return 0;
}

/* >0 byte letti, 0 chiuso, -1 errore, -2 tempo scaduto. */
static ssize_t tubo_leggi(Tubo *t, void *dst, size_t n, int timeout_ms)
{
    struct timespec lim;
    if (timeout_ms > 0) {
        clock_gettime(CLOCK_MONOTONIC, &lim);
        lim.tv_sec += timeout_ms / 1000;
        lim.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (lim.tv_nsec >= 1000000000L) {
            lim.tv_sec++;
            lim.tv_nsec -= 1000000000L;
        }
    }
    pthread_mutex_lock(&t->m);
    for (;;) {
        if (t->errore) {
            pthread_mutex_unlock(&t->m);
            return -1;
        }
        if (t->n > 0) {
            size_t k = t->n < n ? t->n : n;
            memcpy(dst, t->d, k);
            memmove(t->d, t->d + k, t->n - k);
            t->n -= k;
            pthread_mutex_unlock(&t->m);
            return (ssize_t)k;
        }
        if (t->chiuso) {
            pthread_mutex_unlock(&t->m);
            return 0;
        }
        if (timeout_ms == 0) {
            pthread_mutex_unlock(&t->m);
            return -2;
        }
        if (timeout_ms < 0) {
            pthread_cond_wait(&t->cv, &t->m);
        } else if (pthread_cond_timedwait(&t->cv, &t->m, &lim) == ETIMEDOUT && t->n == 0 &&
                   !t->chiuso && !t->errore) {
            pthread_mutex_unlock(&t->m);
            return -2;
        }
    }
}

/* ---- Coppie e finto ADB --------------------------------------------------*/
typedef struct {
    Tubo verso_pc;  /* il servizio scrive, il PC legge */
    Tubo verso_tel; /* il PC scrive, il servizio legge */
    PhAdb *a;
    char servizio[200];
    char tipo[48]; /* riga di apertura: "comandi", "input", "video:3" */
} Coppia;

struct PhCanale {
    Coppia *k;
};

typedef struct {
    uint8_t tipo;
    uint8_t *corpo;
    size_t n;
} Registrato;

typedef struct {
    Registrato *v;
    size_t n, cap;
} Registro;

typedef struct {
    uint32_t id;
    uint16_t larghezza, altezza;
    bool viva;
    bool pausa;
} Sessione;

struct PhAdb {
    pthread_mutex_t m;
    pthread_cond_t cv;
    FintoConfig cfg;
    Coppia **coppie;
    size_t ncoppie;
    pthread_t *thread;
    size_t nthread;
    Registro comandi, input;
    Sessione sessioni[64];
    size_t nsessioni;
    char jar_locale[256], jar_remoto[256], lanciato[256], pulito[256];
    bool forma_ok;
    bool chiudi_ricevuto;
    uint8_t trattieni_tipo; /* cancello: le richieste di questo tipo restano senza risposta... */
    bool cancello_aperto;   /* ...finche' il test non lo apre */
    bool trattenuta;        /* una richiesta e' ferma al cancello */
    bool spento;
    int video_attivi;
    char aperti[64][48];
    size_t naperti;
};

PhAdb *finto_crea(void)
{
    PhAdb *a = calloc(1, sizeof *a);
    if (a == NULL)
        return NULL;
    pthread_mutex_init(&a->m, NULL);
    pthread_cond_init(&a->cv, NULL);
    a->cfg.versione = PH_PROTOCOLLO_VERSIONE;
    a->cfg.pacchetti = 6;
    return a;
}

FintoConfig *finto_cfg(PhAdb *a)
{
    return &a->cfg;
}

static void chiudi_tutto(PhAdb *a, bool errore)
{
    pthread_mutex_lock(&a->m);
    a->spento = true; /* sveglia anche chi e' fermo al cancello */
    pthread_cond_broadcast(&a->cv);
    for (size_t i = 0; i < a->ncoppie; i++) {
        tubo_chiudi(&a->coppie[i]->verso_pc, errore);
        tubo_chiudi(&a->coppie[i]->verso_tel, errore);
    }
    pthread_mutex_unlock(&a->m);
}

void finto_cade(PhAdb *a)
{
    chiudi_tutto(a, true);
}

static void registro_libera(Registro *r)
{
    for (size_t i = 0; i < r->n; i++)
        free(r->v[i].corpo);
    free(r->v);
}

void finto_distruggi(PhAdb *a)
{
    if (a == NULL)
        return;
    chiudi_tutto(a, false);
    for (size_t i = 0; i < a->nthread; i++)
        pthread_join(a->thread[i], NULL);
    for (size_t i = 0; i < a->ncoppie; i++) {
        tubo_libera(&a->coppie[i]->verso_pc);
        tubo_libera(&a->coppie[i]->verso_tel);
        free(a->coppie[i]);
    }
    free(a->coppie);
    free(a->thread);
    registro_libera(&a->comandi);
    registro_libera(&a->input);
    pthread_mutex_destroy(&a->m);
    pthread_cond_destroy(&a->cv);
    free(a);
}

/* ---- Il servizio finto ---------------------------------------------------*/
static void scrivi_pc(Coppia *k, const void *d, size_t n)
{
    (void)tubo_scrivi(&k->verso_pc, d, n);
}

/* Legge esattamente n byte dal PC, aspettando senza limite. 0 riuscito, -1 chiuso. */
static int leggi_esatto(Coppia *k, void *dst, size_t n)
{
    size_t fatti = 0;
    while (fatti < n) {
        ssize_t r = tubo_leggi(&k->verso_tel, (uint8_t *)dst + fatti, n - fatti, -1);
        if (r <= 0)
            return -1;
        fatti += (size_t)r;
    }
    return 0;
}

static void msg_testa(uint8_t *h, uint8_t tipo, uint32_t n)
{
    h[0] = tipo;
    h[1] = (uint8_t)(n >> 24);
    h[2] = (uint8_t)(n >> 16);
    h[3] = (uint8_t)(n >> 8);
    h[4] = (uint8_t)n;
}

static void manda_msg(Coppia *k, uint8_t tipo, const uint8_t *corpo, size_t n)
{
    uint8_t *b = malloc(5 + n);
    if (b == NULL)
        return;
    msg_testa(b, tipo, (uint32_t)n);
    if (n > 0)
        memcpy(b + 5, corpo, n);
    scrivi_pc(k, b, 5 + n);
    free(b);
}

static void manda_errore(Coppia *k, const char *frase)
{
    uint8_t b[300];
    size_t n = strlen(frase);
    b[0] = (uint8_t)(n >> 8);
    b[1] = (uint8_t)n;
    memcpy(b + 2, frase, n);
    manda_msg(k, PH_MSG_ERRORE, b, 2 + n);
}

static int registra(PhAdb *a, Registro *r, uint8_t tipo, const uint8_t *corpo, size_t n)
{
    uint8_t *copia = malloc(n ? n : 1);
    if (copia == NULL)
        return -1;
    if (n > 0)
        memcpy(copia, corpo, n);
    pthread_mutex_lock(&a->m);
    if (r->n == r->cap) {
        size_t nc = r->cap ? r->cap * 2 : 64;
        Registrato *p = realloc(r->v, nc * sizeof *p);
        if (p == NULL) {
            pthread_mutex_unlock(&a->m);
            free(copia);
            return -1;
        }
        r->v = p;
        r->cap = nc;
    }
    r->v[r->n++] = (Registrato){tipo, copia, n};
    pthread_cond_broadcast(&a->cv);
    pthread_mutex_unlock(&a->m);
    return 0;
}

static void nanopausa_ms(int ms)
{
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

size_t finto_dati_pacchetto(int i, uint8_t *uscita, size_t capacita)
{
    if (i < 0) {
        static const uint8_t sps[] = {0, 0, 0, 1, 0x67, 0x42, 0, 0x1f, 0, 0, 0, 1, 0x68, 0xce};
        size_t n = sizeof sps < capacita ? sizeof sps : capacita;
        memcpy(uscita, sps, n);
        return sizeof sps;
    }
    size_t n = 100 + (size_t)i * 37;
    for (size_t j = 0; j < n && j < capacita; j++)
        uscita[j] = j < 4 ? (uint8_t)(j == 3 ? 1 : 0) : (uint8_t)(i * 7 + (int)j);
    return n;
}

uint64_t finto_tempo_pacchetto(int i)
{
    return 1000000u + (uint64_t)(i + 1) * 16666u;
}

static void manda_pacchetto(Coppia *k, FintoVideoModo modo, uint8_t genere, uint8_t segni,
                            uint16_t l, uint16_t h, uint64_t tempo, const uint8_t *dati, size_t n)
{
    uint8_t *b = malloc(18 + n);
    if (b == NULL)
        return;
    b[0] = genere;
    b[1] = segni;
    b[2] = (uint8_t)(l >> 8);
    b[3] = (uint8_t)l;
    b[4] = (uint8_t)(h >> 8);
    b[5] = (uint8_t)h;
    for (int j = 0; j < 8; j++)
        b[6 + j] = (uint8_t)(tempo >> (56 - 8 * j));
    b[14] = (uint8_t)(n >> 24);
    b[15] = (uint8_t)(n >> 16);
    b[16] = (uint8_t)(n >> 8);
    b[17] = (uint8_t)n;
    memcpy(b + 18, dati, n);
    if (modo == VM_LENTO) {
        for (size_t o = 0; o < 18 + n; o += 7) {
            size_t k2 = 18 + n - o < 7 ? 18 + n - o : 7;
            if (tubo_scrivi(&k->verso_pc, b + o, k2) != 0)
                break;
            nanopausa_ms(4);
        }
    } else {
        scrivi_pc(k, b, 18 + n);
    }
    free(b);
}

static bool sessione_in_pausa(PhAdb *a, uint32_t id)
{
    bool p = false;
    pthread_mutex_lock(&a->m);
    for (size_t i = 0; i < a->nsessioni; i++)
        if (a->sessioni[i].id == id && a->sessioni[i].viva)
            p = a->sessioni[i].pausa;
    pthread_mutex_unlock(&a->m);
    return p;
}

/* true se la sessione esiste. */
static bool imposta_pausa(PhAdb *a, uint32_t id, bool pausa)
{
    bool trovata = false;
    pthread_mutex_lock(&a->m);
    for (size_t i = 0; i < a->nsessioni; i++)
        if (a->sessioni[i].id == id && a->sessioni[i].viva) {
            a->sessioni[i].pausa = pausa;
            trovata = true;
        }
    pthread_mutex_unlock(&a->m);
    return trovata;
}

static void servi_video(Coppia *k, uint32_t id)
{
    PhAdb *a = k->a;
    uint16_t l = 0, h = 0;
    bool trovata = false;
    pthread_mutex_lock(&a->m);
    for (size_t i = 0; i < a->nsessioni; i++)
        if (a->sessioni[i].id == id && a->sessioni[i].viva) {
            l = a->sessioni[i].larghezza;
            h = a->sessioni[i].altezza;
            trovata = true;
        }
    FintoConfig cfg = a->cfg;
    if (trovata)
        a->video_attivi++;
    pthread_mutex_unlock(&a->m);
    if (!trovata)
        return; /* sessione sconosciuta: il servizio chiude il canale */

    uint8_t buf[100 + 64 * 37 + 8];
    int totale = cfg.pacchetti;
    if (totale > 64)
        totale = 64;
    switch (cfg.video_modo) {
    case VM_ENORME: {
        uint8_t testa[18] = {2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x90, 0x00, 0x00};
        scrivi_pc(k, testa, sizeof testa); /* 9 MiB */
        break;
    }
    case VM_GENERE: {
        uint8_t testa[18] = {9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4};
        scrivi_pc(k, testa, sizeof testa);
        break;
    }
    case VM_TRONCATO: {
        uint8_t testa[18] = {2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x03, 0xe8};
        scrivi_pc(k, testa, sizeof testa);
        memset(buf, 0xab, 100);
        scrivi_pc(k, buf, 100);
        break;
    }
    case VM_GRANDE: {
        /* un solo fotogramma chiave da 1,5 MiB: il buffer del PC deve crescere a pezzi */
        size_t n = 1536 * 1024;
        uint8_t *g = malloc(n);
        if (g != NULL) {
            for (size_t j = 0; j < n; j++)
                g[j] = (uint8_t)(j * 31 + (j >> 8));
            manda_pacchetto(k, VM_GRANDE, 2, 1, l, h, 5555, g, n);
            free(g);
        }
        break;
    }
    case VM_LUNGO_FALSO: {
        uint8_t testa[18] = {2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x86, 0xa0};
        scrivi_pc(k, testa, sizeof testa);
        memset(buf, 0xcd, 10);
        scrivi_pc(k, buf, 10);
        nanopausa_ms(400);
        break;
    }
    default:
        for (int i = -1; i < totale - 1; i++) {
            size_t n = finto_dati_pacchetto(i, buf, sizeof buf);
            manda_pacchetto(k, cfg.video_modo, i < 0 ? 1 : 2, i == 0 ? 1 : 0, l, h,
                            finto_tempo_pacchetto(i), buf, n);
        }
        break;
    }
    if (cfg.video_continuo && cfg.video_modo == VM_NORMALE) {
        /* Un fotogramma ogni pochi ms (byte 0xdd); in pausa tace; alla ripresa il
         * primo e' un fotogramma chiave (byte 0xee). Finisce quando il PC chiude. */
        uint8_t x;
        bool era_in_pausa = false;
        while (tubo_leggi(&k->verso_tel, &x, 1, 0) == -2) {
            if (sessione_in_pausa(a, id)) {
                era_in_pausa = true;
            } else {
                uint8_t dati[20];
                memset(dati, era_in_pausa ? 0xee : 0xdd, sizeof dati);
                manda_pacchetto(k, VM_NORMALE, 2, era_in_pausa ? 1 : 0, l, h, 7777, dati, sizeof dati);
                era_in_pausa = false;
            }
            nanopausa_ms(3);
        }
    } else if (cfg.video_resta_aperto && (cfg.video_modo == VM_NORMALE || cfg.video_modo == VM_LENTO)) {
        uint8_t x;
        while (tubo_leggi(&k->verso_tel, &x, 1, -1) > 0) {
        }
    }
    pthread_mutex_lock(&a->m);
    a->video_attivi--;
    pthread_mutex_unlock(&a->m);
}

static void servi_input(Coppia *k)
{
    uint8_t h[5];
    while (leggi_esatto(k, h, 5) == 0) {
        uint32_t n = ((uint32_t)h[1] << 24) | ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 8) | h[4];
        if (n > (1u << 20))
            return;
        uint8_t *corpo = malloc(n ? n : 1);
        if (corpo == NULL)
            return;
        if (n > 0 && leggi_esatto(k, corpo, n) != 0) {
            free(corpo);
            return;
        }
        registra(k->a, &k->a->input, h[0], corpo, n);
        free(corpo);
    }
}

static void manda_ok(Coppia *k, const uint8_t *corpo, size_t n, bool evento_prima)
{
    if (evento_prima) {
        const uint8_t ev[] = {0, 0, 0, 7, 2, 0, 1, 0, 0}; /* id 7, schermo protetto acceso */
        manda_msg(k, PH_MSG_VIDEO_EVENTO, ev, sizeof ev);
    }
    manda_msg(k, PH_MSG_OK, corpo, n);
}

static void servi_comandi(Coppia *k)
{
    PhAdb *a = k->a;
    uint8_t h[5];
    while (leggi_esatto(k, h, 5) == 0) {
        uint32_t n = ((uint32_t)h[1] << 24) | ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 8) | h[4];
        if (n > (1u << 20))
            return;
        uint8_t *corpo = malloc(n ? n : 1);
        if (corpo == NULL)
            return;
        if (n > 0 && leggi_esatto(k, corpo, n) != 0) {
            free(corpo);
            return;
        }
        registra(a, &a->comandi, h[0], corpo, n);
        pthread_mutex_lock(&a->m);
        FintoConfig cfg = a->cfg;
        if (a->trattieni_tipo == h[0]) {
            a->trattenuta = true;
            pthread_cond_broadcast(&a->cv);
            while (!a->cancello_aperto && !a->spento)
                pthread_cond_wait(&a->cv, &a->m);
            a->trattenuta = false;
        }
        pthread_mutex_unlock(&a->m);

        if (cfg.silenzio) {
            free(corpo);
            continue;
        }
        if (cfg.risposta_rotta == 1) {
            const uint8_t t[] = {PH_MSG_OK, 0, 0, 0, 50, 1, 2, 3};
            scrivi_pc(k, t, sizeof t);
            free(corpo);
            tubo_chiudi(&k->verso_pc, false);
            return;
        }
        if (cfg.risposta_rotta == 2) {
            const uint8_t t[] = {PH_MSG_OK, 0x7f, 0xff, 0xff, 0xff};
            scrivi_pc(k, t, sizeof t);
            free(corpo);
            continue;
        }

        PhLettore l = {corpo, n, 0, false};
        switch (h[0]) {
        case PH_MSG_SALUTO: {
            (void)ph_leggi_u16(&l);
            if (cfg.saluto_rifiuta) {
                manda_errore(k, "versione del protocollo non supportata");
                break;
            }
            uint8_t r[80];
            size_t o = 0;
            const uint16_t campi[5] = {cfg.versione, 34, 1080, 2340, 420};
            for (int i = 0; i < 5; i++) {
                r[o++] = (uint8_t)(campi[i] >> 8);
                r[o++] = (uint8_t)campi[i];
            }
            const char *modello = "Telefono finto";
            r[o++] = 0;
            r[o++] = (uint8_t)strlen(modello);
            memcpy(r + o, modello, strlen(modello));
            o += strlen(modello);
            manda_ok(k, r, o, cfg.evento_prima_di_ok);
            break;
        }
        case PH_MSG_CHIUDI:
            pthread_mutex_lock(&a->m);
            a->chiudi_ricevuto = true;
            pthread_cond_broadcast(&a->cv);
            pthread_mutex_unlock(&a->m);
            if (cfg.ritardo_chiudi_ms > 0)
                nanopausa_ms(cfg.ritardo_chiudi_ms);
            manda_ok(k, NULL, 0, false);
            if (cfg.chiudi_resta_vivo)
                break; /* risponde OK ma non chiude nulla (free in fondo al ciclo) */
            free(corpo);
            chiudi_tutto(a, false); /* il servizio termina e chiude tutto */
            return;
        case PH_MSG_VIDEO_APRI:
        case PH_MSG_VIDEO_CHIUDI:
        case PH_MSG_VIDEO_RIDIMENSIONA:
        case PH_MSG_VIDEO_PAUSA:
        case PH_MSG_VIDEO_RIPRENDI:
        case PH_MSG_VIDEO_CHIAVE: {
            if (cfg.jar_vecchio) {
                manda_errore(k, "tipo sconosciuto");
                break;
            }
            uint32_t id = ph_leggi_u32(&l);
            if (h[0] == PH_MSG_VIDEO_APRI) {
                uint16_t w = ph_leggi_u16(&l), alt = ph_leggi_u16(&l);
                (void)ph_leggi_u16(&l);
                (void)ph_leggi_u32(&l);
                (void)ph_leggi_u8(&l);
                char pac[100];
                ph_leggi_stringa(&l, pac, sizeof pac);
                if (l.rotto) {
                    manda_errore(k, "richiesta malformata");
                } else if (strcmp(pac, "com.vietato") == 0) {
                    manda_errore(k, "App non trovata sul telefono");
                } else {
                    pthread_mutex_lock(&a->m);
                    if (a->nsessioni < 64)
                        a->sessioni[a->nsessioni++] = (Sessione){id, w, alt, true, false};
                    pthread_mutex_unlock(&a->m);
                    manda_ok(k, NULL, 0, cfg.evento_prima_di_ok);
                }
            } else if (h[0] == PH_MSG_VIDEO_PAUSA || h[0] == PH_MSG_VIDEO_RIPRENDI) {
                if (cfg.pausa_rifiuta)
                    manda_errore(k, "pausa non riuscita");
                else if (!imposta_pausa(a, id, h[0] == PH_MSG_VIDEO_PAUSA))
                    manda_errore(k, "sessione sconosciuta");
                else
                    manda_ok(k, NULL, 0, cfg.evento_prima_di_ok);
            } else if ((h[0] == PH_MSG_VIDEO_RIDIMENSIONA || h[0] == PH_MSG_VIDEO_CHIAVE) &&
                       sessione_in_pausa(a, id)) {
                manda_errore(k, "sessione in pausa");
            } else if (h[0] == PH_MSG_VIDEO_RIDIMENSIONA) {
                uint16_t w = ph_leggi_u16(&l), alt = ph_leggi_u16(&l);
                (void)ph_leggi_u16(&l);
                uint8_t r[4] = {(uint8_t)(w >> 8), (uint8_t)w, (uint8_t)(alt >> 8), (uint8_t)alt};
                manda_ok(k, r, sizeof r, cfg.evento_prima_di_ok);
            } else {
                if (h[0] == PH_MSG_VIDEO_CHIUDI) {
                    pthread_mutex_lock(&a->m);
                    for (size_t i = 0; i < a->nsessioni; i++)
                        if (a->sessioni[i].id == id)
                            a->sessioni[i].viva = false;
                    pthread_mutex_unlock(&a->m);
                }
                manda_ok(k, NULL, 0, cfg.evento_prima_di_ok);
            }
            break;
        }
        case PH_MSG_PRESENZA:
            if (cfg.presenza_errore)
                manda_errore(k, "tipo sconosciuto");
            else
                manda_ok(k, NULL, 0, false);
            break;
        case 0x70: /* richiesta generica: risponde OK con lo stesso corpo */
            manda_ok(k, corpo, n, false);
            break;
        case 0x71: /* richiesta generica rifiutata */
            manda_errore(k, "dato non disponibile");
            break;
        default:
            manda_errore(k, "tipo sconosciuto");
            break;
        }
        free(corpo);
    }
}

static void servi_shell(Coppia *k)
{
    FintoConfig cfg;
    pthread_mutex_lock(&k->a->m);
    cfg = k->a->cfg;
    pthread_mutex_unlock(&k->a->m);
    if (cfg.shell_muore) {
        const char *t = "Errore: classe it.yasde.Servizio non trovata\r\n";
        scrivi_pc(k, t, strlen(t));
        tubo_chiudi(&k->verso_pc, false);
        tubo_chiudi(&k->verso_tel, false);
        return;
    }
    if (!cfg.niente_pronto) {
        const char *t = "avvio del servizio\r\nPRONTO\r\n";
        scrivi_pc(k, t, strlen(t));
    }
    uint8_t x;
    while (tubo_leggi(&k->verso_tel, &x, 1, -1) > 0) {
    }
    /* Shell chiusa dal PC: il servizio muore e con lui tutti i canali. */
    chiudi_tutto(k->a, false);
}

static void *thread_coppia(void *arg)
{
    Coppia *k = arg;
    if (strncmp(k->servizio, "shell:", 6) == 0) {
        servi_shell(k);
    } else {
        char riga[48];
        size_t n = 0;
        uint8_t c;
        while (n < sizeof riga - 1 && tubo_leggi(&k->verso_tel, &c, 1, -1) > 0 && c != '\n')
            riga[n++] = (char)c;
        riga[n] = '\0';
        pthread_mutex_lock(&k->a->m);
        snprintf(k->tipo, sizeof k->tipo, "%s", riga);
        if (k->a->naperti < 64)
            snprintf(k->a->aperti[k->a->naperti++], sizeof k->a->aperti[0], "%s", riga);
        pthread_cond_broadcast(&k->a->cv);
        pthread_mutex_unlock(&k->a->m);
        if (strcmp(riga, "comandi") == 0)
            servi_comandi(k);
        else if (strcmp(riga, "input") == 0)
            servi_input(k);
        else if (strcmp(riga, "audio") == 0) {
            const uint8_t dati[8] = {1, 2, 3, 4, 5, 6, 7, 8};
            scrivi_pc(k, dati, sizeof dati);
            uint8_t x;
            while (tubo_leggi(&k->verso_tel, &x, 1, -1) > 0) {
            }
        } else if (strncmp(riga, "video:", 6) == 0)
            servi_video(k, (uint32_t)strtoul(riga + 6, NULL, 10));
        /* tipo sconosciuto: il servizio chiude il canale */
    }
    tubo_chiudi(&k->verso_pc, false);
    tubo_chiudi(&k->verso_tel, false);
    return NULL;
}

/* ---- Funzioni dell'ADB finto ----------------------------------------------*/
PhCanale *ph_canale_apri(PhAdb *a, const char *servizio, int timeout_ms, PhErrore *e)
{
    (void)timeout_ms;
    bool shell = strncmp(servizio, "shell:", 6) == 0;
    if (!shell && strcmp(servizio, "localabstract:yasde") != 0) {
        ph_errore(e, "servizio '%s' rifiutato dal telefono finto", servizio);
        return NULL;
    }
    Coppia *k = calloc(1, sizeof *k);
    PhCanale *c = calloc(1, sizeof *c);
    if (k == NULL || c == NULL) {
        free(k);
        free(c);
        ph_errore(e, "memoria finita");
        return NULL;
    }
    tubo_init(&k->verso_pc);
    tubo_init(&k->verso_tel);
    k->a = a;
    snprintf(k->servizio, sizeof k->servizio, "%s", servizio);
    c->k = k;

    pthread_mutex_lock(&a->m);
    if (shell)
        snprintf(a->lanciato, sizeof a->lanciato, "%s", servizio + 6);
    Coppia **nc = realloc(a->coppie, (a->ncoppie + 1) * sizeof *nc);
    pthread_t *nt = realloc(a->thread, (a->nthread + 1) * sizeof *nt);
    if (nc != NULL)
        a->coppie = nc;
    if (nt != NULL)
        a->thread = nt;
    if (nc == NULL || nt == NULL || pthread_create(&a->thread[a->nthread], NULL, thread_coppia, k) != 0) {
        pthread_mutex_unlock(&a->m);
        tubo_libera(&k->verso_pc);
        tubo_libera(&k->verso_tel);
        free(k);
        free(c);
        ph_errore(e, "impossibile creare il canale finto");
        return NULL;
    }
    a->nthread++;
    a->coppie[a->ncoppie++] = k;
    pthread_mutex_unlock(&a->m);
    return c;
}

ssize_t ph_canale_leggi(PhCanale *c, void *dati, size_t n, int timeout_ms, PhErrore *e)
{
    ssize_t r = tubo_leggi(&c->k->verso_pc, dati, n, timeout_ms);
    if (r == -1)
        ph_errore(e, "connessione con il telefono finto caduta");
    return r;
}

int ph_canale_leggi_tutto(PhCanale *c, void *dati, size_t n, int timeout_ms, PhErrore *e)
{
    size_t fatti = 0;
    while (fatti < n) {
        ssize_t r = ph_canale_leggi(c, (uint8_t *)dati + fatti, n - fatti, timeout_ms, e);
        if (r <= 0)
            return ph_errore(e, "lettura incompleta dal telefono finto");
        fatti += (size_t)r;
    }
    return 0;
}

int ph_canale_scrivi(PhCanale *c, const void *dati, size_t n, PhErrore *e)
{
    if (tubo_scrivi(&c->k->verso_tel, dati, n) != 0)
        return ph_errore(e, "scrittura sul canale finto non riuscita (canale chiuso)");
    return 0;
}

void ph_canale_chiudi(PhCanale *c)
{
    if (c == NULL)
        return;
    tubo_chiudi(&c->k->verso_pc, false);
    tubo_chiudi(&c->k->verso_tel, false);
    free(c);
}

int ph_shell_esegui(PhAdb *a, const char *comando, PhBuffer *uscita, PhBuffer *errori,
                    int *codice, int timeout_ms, PhErrore *e)
{
    (void)uscita; (void)errori; (void)codice; (void)timeout_ms;
    pthread_mutex_lock(&a->m);
    snprintf(a->pulito, sizeof a->pulito, "%s", comando);
    pthread_mutex_unlock(&a->m);
    return ph_errore(e, "ph_shell_esegui non e' simulato");
}

PhFileEsito ph_file_invia(PhAdb *a, const char *locale, const char *remoto,
                          PhFileProgresso progresso, void *utente,
                          const atomic_bool *annulla, PhErrore *e)
{
    (void)progresso; (void)utente; (void)annulla;
    pthread_mutex_lock(&a->m);
    snprintf(a->jar_locale, sizeof a->jar_locale, "%s", locale);
    snprintf(a->jar_remoto, sizeof a->jar_remoto, "%s", remoto);
    bool fallisce = a->cfg.copia_fallisce;
    pthread_mutex_unlock(&a->m);
    if (fallisce) {
        ph_errore(e, "spazio sul telefono esaurito");
        return PH_FILE_ERRORE;
    }
    return PH_FILE_RIUSCITO;
}

/* ---- Accesso ai registri -------------------------------------------------*/
bool finto_manda_comandi(PhAdb *a, const uint8_t *dati, size_t n)
{
    pthread_mutex_lock(&a->m);
    Coppia *k = NULL;
    for (size_t i = 0; i < a->ncoppie; i++)
        if (strcmp(a->coppie[i]->tipo, "comandi") == 0) {
            k = a->coppie[i];
            break;
        }
    pthread_mutex_unlock(&a->m);
    if (k == NULL)
        return false;
    return tubo_scrivi(&k->verso_pc, dati, n) == 0;
}

static size_t registro_leggi(PhAdb *a, Registro *r, size_t i, uint8_t *tipo, uint8_t *corpo, size_t cap)
{
    pthread_mutex_lock(&a->m);
    size_t n = 0;
    if (i < r->n) {
        n = r->v[i].n;
        *tipo = r->v[i].tipo;
        memcpy(corpo, r->v[i].corpo, n < cap ? n : cap);
    }
    pthread_mutex_unlock(&a->m);
    return n;
}

size_t finto_numero_input(PhAdb *a)
{
    pthread_mutex_lock(&a->m);
    size_t n = a->input.n;
    pthread_mutex_unlock(&a->m);
    return n;
}

bool finto_attendi_input(PhAdb *a, size_t n, int timeout_ms)
{
    for (int t = 0; t < timeout_ms; t += 5) {
        if (finto_numero_input(a) >= n)
            return true;
        nanopausa_ms(5);
    }
    return finto_numero_input(a) >= n;
}

size_t finto_input(PhAdb *a, size_t i, uint8_t *tipo, uint8_t *corpo, size_t cap)
{
    return registro_leggi(a, &a->input, i, tipo, corpo, cap);
}

size_t finto_numero_comandi(PhAdb *a)
{
    pthread_mutex_lock(&a->m);
    size_t n = a->comandi.n;
    pthread_mutex_unlock(&a->m);
    return n;
}

size_t finto_comando(PhAdb *a, size_t i, uint8_t *tipo, uint8_t *corpo, size_t cap)
{
    return registro_leggi(a, &a->comandi, i, tipo, corpo, cap);
}

const char *finto_jar_locale(PhAdb *a) { return a->jar_locale; }
const char *finto_jar_remoto(PhAdb *a) { return a->jar_remoto; }
const char *finto_servizio_lanciato(PhAdb *a) { return a->lanciato; }
const char *finto_comando_pulizia(PhAdb *a) { return a->pulito; }

bool finto_ricevuto_chiudi(PhAdb *a)
{
    pthread_mutex_lock(&a->m);
    bool r = a->chiudi_ricevuto;
    pthread_mutex_unlock(&a->m);
    return r;
}

int finto_video_attivi(PhAdb *a)
{
    pthread_mutex_lock(&a->m);
    int r = a->video_attivi;
    pthread_mutex_unlock(&a->m);
    return r;
}

int finto_canali_aperti(PhAdb *a, const char *tipo)
{
    int n = 0;
    pthread_mutex_lock(&a->m);
    for (size_t i = 0; i < a->naperti; i++)
        if (strncmp(a->aperti[i], tipo, strlen(tipo)) == 0)
            n++;
    pthread_mutex_unlock(&a->m);
    return n;
}

void finto_trattieni(PhAdb *a, uint8_t tipo)
{
    pthread_mutex_lock(&a->m);
    a->trattieni_tipo = tipo;
    a->cancello_aperto = false;
    pthread_mutex_unlock(&a->m);
}

void finto_apri_cancello(PhAdb *a)
{
    pthread_mutex_lock(&a->m);
    a->cancello_aperto = true;
    pthread_cond_broadcast(&a->cv);
    pthread_mutex_unlock(&a->m);
}

bool finto_attendi_trattenuta(PhAdb *a, int timeout_ms)
{
    for (int t = 0; t < timeout_ms; t += 2) {
        pthread_mutex_lock(&a->m);
        bool r = a->trattenuta;
        pthread_mutex_unlock(&a->m);
        if (r)
            return true;
        nanopausa_ms(2);
    }
    return false;
}

/* Quante richieste di quel tipo ha ricevuto il servizio. */
int finto_conta_comandi(PhAdb *a, uint8_t tipo)
{
    pthread_mutex_lock(&a->m);
    int n = 0;
    for (size_t i = 0; i < a->comandi.n; i++)
        if (a->comandi.v[i].tipo == tipo)
            n++;
    pthread_mutex_unlock(&a->m);
    return n;
}
