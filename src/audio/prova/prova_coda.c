/* Coda con margine adattivo, in tempo finto: gli arrivi e le consegne
 * dell'uscita sono simulati senza aspettare, quindi minuti di audio in un lampo. */
#include <string.h>

#include "audio/interno.h"
#include "aiuti.h"

#define N_MAX 20000
#define CHUNK 1024

typedef enum { REGOLARE, RAFFICHE, BUCO, BUCO_LUNGO, PERSI } Scenario;

typedef struct {
    PhCodaAudio *c;
    int64_t ora_cb;        /* tempo del prossimo "callback" dell'uscita, us */
    double ora_cb_f;
    int k;                 /* prossimo pacchetto da far arrivare */
    int64_t arrivo[N_MAX];
    bool perso[N_MAX];
    float pk[960 * 2];
    float out[CHUNK * 2];
} Sim;

static void sim_inizia(Sim *s, Scenario sc, int pacchetti, int64_t inizio_us)
{
    memset(s, 0, sizeof *s);
    s->c = ph_coda_crea();
    CONTROLLA(s->c);
    for (int i = 0; i < 960 * 2; i++)
        s->pk[i] = 0.25f;
    for (int k = 0; k < pacchetti; k++) {
        int64_t nominale = inizio_us + (int64_t)k * 20000;
        int64_t a = nominale;
        if (sc == RAFFICHE)
            a = (nominale + 99999) / 100000 * 100000;     /* a gruppi di 5 ogni 100 ms */
        if (sc == BUCO && nominale >= inizio_us + 10000000 && nominale < inizio_us + 10300000)
            a = inizio_us + 10300000;                     /* stallo di 300 ms */
        if (sc == BUCO_LUNGO && nominale >= inizio_us + 10000000 && nominale < inizio_us + 13000000)
            a = inizio_us + 13000000;                     /* stallo di 3 s */
        if (sc == PERSI && k % 10 == 5)
            s->perso[k] = true;
        s->arrivo[k] = a;
    }
    s->ora_cb = inizio_us;
    s->ora_cb_f = (double)inizio_us;
}

/* Avanza il tempo finto fino a 'fino_us', consegnando arrivi e callback in ordine. */
static void sim_corri(Sim *s, int pacchetti, int64_t fino_us, Scenario sc)
{
    (void)sc;
    for (;;) {
        bool arrivo_vicino = s->k < pacchetti && s->arrivo[s->k] <= s->ora_cb;
        if (arrivo_vicino) {
            if (s->arrivo[s->k] > fino_us)
                break;
            if (s->perso[s->k])
                ph_coda_inserisci_occultato(s->c, s->pk, 960);
            else
                ph_coda_inserisci(s->c, s->pk, 960, (int64_t)s->k * 20000, s->arrivo[s->k]);
            s->k++;
        } else {
            if (s->ora_cb > fino_us)
                break;
            ph_coda_estrai(s->c, s->out, CHUNK, s->ora_cb);
            s->ora_cb_f += CHUNK * 1000000.0 / 48000;
            s->ora_cb = (int64_t)s->ora_cb_f;
        }
    }
}

#define SEC 1000000LL

static void regolare(void)
{
    static Sim s;
    sim_inizia(&s, REGOLARE, 15000, 5 * SEC);
    PhCodaStat st;
    sim_corri(&s, 15000, 5 * SEC + 3 * SEC, REGOLARE);
    ph_coda_stat(s.c, &st);
    CONTROLLA(st.in_riproduzione);
    CONTROLLA(st.obiettivo_ms <= PH_CODA_OBIETTIVO_INIZIALE_MS);
    sim_corri(&s, 15000, 5 * SEC + 280 * SEC, REGOLARE);
    ph_coda_stat(s.c, &st);
    fprintf(stderr, "regolare: obiettivo %d ms, margine %d ms, sottoalimentazioni %llu\n", st.obiettivo_ms,
            st.margine_ms, (unsigned long long)st.sottoalimentazioni);
    CONTROLLA(st.sottoalimentazioni == 0);
    CONTROLLA(st.obiettivo_ms == PH_CODA_OBIETTIVO_MIN_MS);   /* si e' ristretto durante la riproduzione */
    CONTROLLA(st.margine_ms > 10 && st.margine_ms < PH_CODA_OBIETTIVO_MIN_MS + 30);
    CONTROLLA(st.scartati == 0);
    ph_coda_libera(s.c);
}

static void raffiche(void)
{
    static Sim s;
    sim_inizia(&s, RAFFICHE, 15000, 5 * SEC);
    PhCodaStat st, st2;
    sim_corri(&s, 15000, 5 * SEC + 15 * SEC, RAFFICHE);
    ph_coda_stat(s.c, &st);
    CONTROLLA(st.obiettivo_ms >= 100);
    sim_corri(&s, 15000, 5 * SEC + 200 * SEC, RAFFICHE);
    ph_coda_stat(s.c, &st2);
    fprintf(stderr, "raffiche: obiettivo %d ms, margine %d ms, sottoalimentazioni %llu -> %llu\n", st2.obiettivo_ms,
            st2.margine_ms, (unsigned long long)st.sottoalimentazioni, (unsigned long long)st2.sottoalimentazioni);
    CONTROLLA(st2.sottoalimentazioni == st.sottoalimentazioni);   /* a regime nessun buco */
    CONTROLLA(st2.obiettivo_ms >= 100 && st2.obiettivo_ms <= 200);
    ph_coda_libera(s.c);
}

/* Dopo uno stallo: quanti secondi ci vogliono perche' l'obiettivo torni a (minimo + 5 ms). */
static int rientro(Sim *s, Scenario sc, int pacchetti, int64_t da_us, int max_s)
{
    for (int sec = 1; sec <= max_s; sec++) {
        PhCodaStat st;
        sim_corri(s, pacchetti, da_us + sec * SEC, sc);
        ph_coda_stat(s->c, &st);
        if (st.obiettivo_ms <= PH_CODA_OBIETTIVO_MIN_MS + 5)
            return sec;
    }
    return -1;
}

static void buco(void)
{
    static Sim s;
    sim_inizia(&s, BUCO, 15000, 5 * SEC);
    PhCodaStat st, st2;
    sim_corri(&s, 15000, 5 * SEC + 9 * SEC, BUCO);
    ph_coda_stat(s.c, &st);
    CONTROLLA(st.sottoalimentazioni == 0);
    sim_corri(&s, 15000, 5 * SEC + 12 * SEC, BUCO);
    ph_coda_stat(s.c, &st2);
    CONTROLLA(st2.sottoalimentazioni >= 1);          /* lo stallo di 300 ms ha svuotato la coda */
    CONTROLLA(st2.obiettivo_ms >= 300);              /* e il margine e' cresciuto */
    int sec = rientro(&s, BUCO, 15000, 5 * SEC + 12 * SEC, 200);
    ph_coda_stat(s.c, &st);
    fprintf(stderr, "buco 300 ms: obiettivo %d ms dopo lo stallo, rientro a %d ms in %d s\n", st2.obiettivo_ms,
            st.obiettivo_ms, sec);
    CONTROLLA(sec > 0 && sec <= 90);
    sim_corri(&s, 15000, 5 * SEC + 290 * SEC, BUCO);
    ph_coda_stat(s.c, &st);
    fprintf(stderr, "buco: obiettivo %d ms, margine %d ms, sottoalimentazioni %llu, scartati %llu\n", st.obiettivo_ms,
            st.margine_ms, (unsigned long long)st.sottoalimentazioni, (unsigned long long)st.scartati);
    CONTROLLA(st.margine_ms < st.obiettivo_ms + 30);               /* l'eccesso e' stato recuperato */
    CONTROLLA(st.sottoalimentazioni == st2.sottoalimentazioni);
    ph_coda_libera(s.c);
}

/* Stallo piu' lungo di quel che la coda puo' assorbire: non entra nella stima. */
static void buco_lungo(void)
{
    static Sim s;
    sim_inizia(&s, BUCO_LUNGO, 15000, 5 * SEC);
    PhCodaStat st;
    sim_corri(&s, 15000, 5 * SEC + 16 * SEC, BUCO_LUNGO);
    ph_coda_stat(s.c, &st);
    fprintf(stderr, "buco 3 s: obiettivo %d ms subito dopo, sottoalimentazioni %llu\n", st.obiettivo_ms,
            (unsigned long long)st.sottoalimentazioni);
    CONTROLLA(st.sottoalimentazioni >= 1);
    CONTROLLA(st.obiettivo_ms < 150);               /* senza l'esclusione sarebbe 600 */
    int sec = rientro(&s, BUCO_LUNGO, 15000, 5 * SEC + 16 * SEC, 200);
    ph_coda_stat(s.c, &st);
    fprintf(stderr, "buco 3 s: rientro in %d s\n", sec);
    CONTROLLA(sec > 0 && sec <= 30);
    sim_corri(&s, 15000, 5 * SEC + 120 * SEC, BUCO_LUNGO);
    ph_coda_stat(s.c, &st);
    CONTROLLA(st.margine_ms < st.obiettivo_ms + 30);
    ph_coda_libera(s.c);
}

static void persi(void)
{
    static Sim s;
    sim_inizia(&s, PERSI, 15000, 5 * SEC);
    PhCodaStat st;
    sim_corri(&s, 15000, 5 * SEC + 120 * SEC, PERSI);
    ph_coda_stat(s.c, &st);
    CONTROLLA(st.sottoalimentazioni == 0);
    CONTROLLA(st.obiettivo_ms <= 60);
    ph_coda_libera(s.c);
}

/* Ritardo in eccesso: poco = si comprime senza tagli; tanto = si taglia di colpo. */
static void eccesso(int extra_ms, bool taglio)
{
    static Sim s;
    sim_inizia(&s, REGOLARE, 15000, 5 * SEC);
    static float grande[960 * 2 * 25];
    for (size_t i = 0; i < sizeof grande / sizeof grande[0]; i++)
        grande[i] = 0.25f;
    sim_corri(&s, 15000, 5 * SEC + 20 * SEC, REGOLARE);
    PhCodaStat st0, st;
    ph_coda_stat(s.c, &st0);
    ph_coda_inserisci_occultato(s.c, grande, (size_t)extra_ms * 48);
    sim_corri(&s, 15000, 5 * SEC + 21 * SEC, REGOLARE);
    ph_coda_stat(s.c, &st);
    if (taglio) {
        CONTROLLA(st.scartati > 0);
        CONTROLLA(st.margine_ms < st.obiettivo_ms + 150);
    }
    sim_corri(&s, 15000, 5 * SEC + 60 * SEC, REGOLARE);
    ph_coda_stat(s.c, &st);
    fprintf(stderr, "eccesso %d ms: margine %d ms, obiettivo %d, scartati %llu, sottoalimentazioni %llu\n", extra_ms,
            st.margine_ms, st.obiettivo_ms, (unsigned long long)st.scartati, (unsigned long long)st.sottoalimentazioni);
    CONTROLLA(st.margine_ms < st.obiettivo_ms + 25);
    CONTROLLA(st.sottoalimentazioni == 0);
    if (!taglio)
        CONTROLLA(st.scartati == 0);
    ph_coda_libera(s.c);
}

static void orologio(void)
{
    PhCodaAudio *c = ph_coda_crea();
    float pk[960 * 2] = {0}, out[CHUNK * 2];
    CONTROLLA(ph_coda_tempo(c, 1000, 0) == -1);              /* nulla ancora */
    int64_t t0 = 7000000;
    for (int k = 0; k < 20; k++)
        ph_coda_inserisci(c, pk, 960, t0 + (int64_t)k * 20000, 100000 + k * 20000);
    CONTROLLA(ph_coda_tempo(c, 1000, 0) == -1);              /* ancora in riempimento */
    int64_t ora = 5000000;
    CONTROLLA(ph_coda_estrai(c, out, CHUNK, ora) == CHUNK);
    int64_t atteso = t0 + 21333;                             /* 1024 frame consegnati */
    /* L'orologio non torna indietro, quindi si leggono i valori in ordine crescente. */
    int64_t t = ph_coda_tempo(c, ora, 30000);                /* la latenza dell'uscita si toglie */
    CONTROLLA(t >= atteso - 30000 - 2 && t <= atteso - 30000 + 2);
    t = ph_coda_tempo(c, ora, 0);
    CONTROLLA(t >= atteso - 2 && t <= atteso + 2);
    t = ph_coda_tempo(c, ora + 10000, 0);
    CONTROLLA(t >= atteso + 10000 - 2 && t <= atteso + 10000 + 2);
    t = ph_coda_tempo(c, ora, 0);                            /* una lettura "vecchia": non si torna indietro */
    CONTROLLA(t >= atteso + 10000 - 2 && t <= atteso + 10000 + 2);
    t = ph_coda_tempo(c, ora + 5000000, 0);                  /* non si va oltre l'ultima consegna (21,3 ms) */
    CONTROLLA(t >= atteso + 21333 - 2 && t <= atteso + 21333 + 2);
    ph_coda_svuota(c);
    CONTROLLA(ph_coda_tempo(c, ora, 0) == -1);
    ph_coda_libera(c);

    /* Uno stallo (coda vuota) rimette l'orologio su "non noto". */
    c = ph_coda_crea();
    for (int k = 0; k < 10; k++)
        ph_coda_inserisci(c, pk, 960, t0 + (int64_t)k * 20000, 100000 + k * 20000);
    for (int i = 0; i < 12; i++)
        ph_coda_estrai(c, out, CHUNK, ora + i * 21333);
    CONTROLLA(ph_coda_tempo(c, ora + 12 * 21333, 0) == -1);
    PhCodaStat st;
    ph_coda_stat(c, &st);
    CONTROLLA(st.sottoalimentazioni == 1);
    ph_coda_libera(c);
}

/* Buco piu' lungo di quel che si occulta: il pacchetto vero arriva con un tempo
 * molto piu' avanti; l'orologio deve coincidere con il tempo del telefono. */
/* Il margine in ms e' troncato e la coda puo' leggere a rapporto != 1: ~1 ms di tolleranza. */
#define TOLL 1200

static void orologio_buco(int64_t salto_us)
{
    PhCodaAudio *c = ph_coda_crea();
    float pk[960 * 2] = {0}, out[CHUNK * 2];
    int64_t t0 = 2000000, ora = 50000000;
    int inseriti = 0;
    for (int passo = 0; passo < 80; passo++) {
        /* Un pacchetto ogni 20 ms; dal quindicesimo il tempo salta in avanti. */
        int64_t tempo = t0 + (int64_t)inseriti * 20000 + (inseriti >= 15 ? salto_us : 0);
        ph_coda_inserisci(c, pk, 960, tempo, ora);
        inseriti++;
        ph_coda_estrai(c, out, 960, ora);
        PhCodaStat st;
        ph_coda_stat(c, &st);
        if (st.in_riproduzione) {
            int64_t consumati = (int64_t)inseriti * 960 - (int64_t)st.margine_ms * 48;
            int64_t p = consumati / 960, off = consumati % 960;
            int64_t atteso = t0 + p * 20000 + (p >= 15 ? salto_us : 0) + off * 1000000 / 48000;
            int64_t t = ph_coda_tempo(c, ora, 0);
            if (!(t >= atteso - TOLL && t <= atteso + TOLL))
                fprintf(stderr, "passo %d: orologio %lld atteso %lld\n", passo, (long long)t, (long long)atteso);
            CONTROLLA(t >= atteso - TOLL && t <= atteso + TOLL);
        }
        ora += 20000;
    }
    ph_coda_libera(c);
}

static void orologio_monotono(void)
{
    PhCodaAudio *c = ph_coda_crea();
    static float pk[960 * 2], out[2048 * 2];
    int64_t base = 100 * SEC, prossimo_arrivo = base, prossima_estr = base;
    int64_t k = 0, durata_us = 0, ultimo = -1, letture = 0, indietro_ammesso = 0;
    int64_t lat = 20000, ripartenze = 0;
    bool in_pausa = false;
    uint32_t seme = 12345;
    for (int64_t ms = 0; ms < 70000; ms++) {
        int64_t ora = base + ms * 1000;
        if (ms == 15000)
            lat = 45000;     /* la latenza sale... */
        if (ms == 50000)
            lat = 5000;      /* ...e scende */
        /* Fase 20-40 s: arrivi a gruppi ogni 100 ms (il margine sale e la coda rallenta). */
        bool a_gruppi = ms >= 20000 && ms < 40000;
        while (prossimo_arrivo <= ora) {
            if (!a_gruppi || ora % 100000 < 1000) {
                ph_coda_inserisci(c, pk, 960, k * 20000, ora);
                k++;
                prossimo_arrivo += 20000;
            } else {
                break;
            }
        }
        if (ms == 45000)     /* 200 ms in piu' di colpo (la coda accelera per recuperare) */
            for (int j = 0; j < 10; j++)   /* pacchetti veri, col tempo che continua */
                ph_coda_inserisci(c, pk, 960, k++ * 20000, ora);
        if (ora >= prossima_estr) {
            seme = seme * 1664525u + 1013904223u;
            size_t n = 480 + (seme >> 16) % 1000;
            ph_coda_estrai(c, out, n, ora);
            durata_us = (int64_t)n * 1000000 / 48000;
            /* intervallo irregolare: ogni tanto molto in ritardo, ogni tanto in anticipo */
            int64_t jitter = (int64_t)((seme >> 8) % 16000) - 8000;
            if ((seme >> 20) % 10 == 0)
                jitter += 40000;
            prossima_estr = ora + durata_us + jitter;
            if (prossima_estr < ora + 1000)
                prossima_estr = ora + 1000;
        }
        int64_t t = ph_coda_tempo(c, ora, lat);
        if (t < 0) {
            in_pausa = true;     /* in riempimento dopo un buco: 'ultimo' resta quello di prima */
            continue;
        }
        if (in_pausa) {
            if (ultimo >= 0)
                ripartenze++;    /* il primo valore dopo il -1 non e' minore dell'ultimo dato (controllo sotto) */
            in_pausa = false;
        }
        int64_t testa = ph_coda_tempo_testa(c);
        CONTROLLA(testa >= 0);
        if (ultimo >= 0) {
            CONTROLLA(t >= ultimo);
            if (t > ultimo)
                indietro_ammesso++;
        }
        /* Mai oltre l'audio davvero consegnato (se il valore e' tenuto fermo dalla
         * monotonia dopo un aumento di latenza, il limite e' il valore precedente). */
        int64_t limite = testa - lat + durata_us + 1;
        CONTROLLA(t <= limite || t == ultimo);
        ultimo = t;
        letture++;
    }
    fprintf(stderr, "orologio monotono: %lld letture, %lld avanzamenti\n", (long long)letture,
            (long long)indietro_ammesso);
    fprintf(stderr, "orologio monotono: %lld ripartenze dopo coda vuota\n", (long long)ripartenze);
    CONTROLLA(letture > 30000);
    ph_coda_libera(c);
}

/* Coda che si vuota davvero e riparte: l'orologio non deve tornare sotto l'ultimo
 * valore dato. Prima del vuoto una consegna lunga (2880 frame = 60 ms, che svuota la
 * coda) letta a fine durata, cosi' l'orologio e' avanti di 60 ms sull'audio consegnato;
 * alla ripartenza una consegna corta (480 frame): il calcolo da solo darebbe meno. */
static void orologio_ripartenza(void)
{
    PhCodaAudio *c = ph_coda_crea();
    float pk[960 * 2] = {0}, out[3000 * 2];
    int64_t t0 = 9000000, ora = 70000000;
    for (int k = 0; k < 3; k++)
        ph_coda_inserisci(c, pk, 960, t0 + (int64_t)k * 20000, ora);
    CONTROLLA(ph_coda_estrai(c, out, 2880, ora) == 2880);        /* coda vuota, ancora "in riproduzione" */
    int64_t ultimo = ph_coda_tempo(c, ora + 60000, 0);
    CONTROLLA(ultimo >= t0 + 120000 - 2 && ultimo <= t0 + 120000 + 2);
    ora += 60000;
    CONTROLLA(ph_coda_estrai(c, out, 480, ora) == 0);            /* il vuoto vero */
    CONTROLLA(ph_coda_tempo(c, ora, 0) == -1);
    ora += 10000;
    for (int k = 3; k < 7; k++)                                  /* il tempo continua dove era arrivato */
        ph_coda_inserisci(c, pk, 960, t0 + (int64_t)k * 20000, ora);
    CONTROLLA(ph_coda_estrai(c, out, 480, ora) > 0);             /* riparte */
    int64_t t = ph_coda_tempo(c, ora, 0);
    CONTROLLA(t >= 0);
    CONTROLLA(t >= ultimo);
    ph_coda_libera(c);
}

/* Fine del flusso: quel che resta si suona anche sotto il margine, senza contare buchi. */
static void fine(void)
{
    PhCodaAudio *c = ph_coda_crea();
    float pk[960 * 2], out[CHUNK * 2];
    for (int i = 0; i < 960 * 2; i++)
        pk[i] = 0.25f;
    ph_coda_inserisci(c, pk, 960, 0, 1000);
    CONTROLLA(ph_coda_estrai(c, out, CHUNK, 2000) == 0);     /* 20 ms < 60 ms: in attesa */
    ph_coda_fine(c);
    CONTROLLA(ph_coda_estrai(c, out, CHUNK, 3000) == 960);
    CONTROLLA(out[0] == 0.0f || out[0] < 0.25f);             /* dissolvenza in entrata */
    CONTROLLA(out[500 * 2] == 0.25f);                        /* a meta' nessuna dissolvenza */
    CONTROLLA(out[CHUNK * 2 - 1] == 0.0f);
    PhCodaStat st;
    ph_coda_stat(c, &st);
    CONTROLLA(st.sottoalimentazioni == 0);
    ph_coda_libera(c);
}

int main(void)
{
    regolare();
    raffiche();
    buco();
    buco_lungo();
    persi();
    eccesso(150, false);
    eccesso(500, true);
    orologio();
    orologio_buco(1000000);
    orologio_buco(6000000);
    orologio_monotono();
    orologio_ripartenza();
    fine();
    return 0;
}
