/* Il riproduttore intero, con un'uscita finta che "consuma" i campioni quando
 * lo decide il test, e un canale finto al posto di quello ADB. */
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "audio/interno.h"
#include "aiuti.h"
#include "finto_canale.h"

/* ---- Uscita finta -------------------------------------------------------- */
typedef struct {
    PhUscita base;
    PhUscitaCb cb;
    int64_t latenza;
    bool fallisci_avvio;
} Finta;

static Finta *g_finta;
static bool g_fabbrica_fallisce, g_avvio_fallisce;

static int finta_avvia(PhUscita *u, PhErrore *e)
{
    return ((Finta *)u)->fallisci_avvio ? ph_errore(e, "uscita finta: avvio rifiutato") : 0;
}
static void finta_ferma(PhUscita *u) { (void)u; }
static int64_t finta_latenza(PhUscita *u) { return ((Finta *)u)->latenza; }
static void finta_libera(PhUscita *u)
{
    if (g_finta == (Finta *)u)
        g_finta = NULL;
    free(u);
}
static PhUscita *finta_crea(const PhUscitaCb *cb, PhErrore *e)
{
    if (g_fabbrica_fallisce) {
        ph_errore(e, "uscita finta: non creata");
        return NULL;
    }
    Finta *f = calloc(1, sizeof *f);
    f->cb = *cb;
    f->latenza = 30000;
    f->fallisci_avvio = g_avvio_fallisce;
    f->base.avvia = finta_avvia;
    f->base.ferma = finta_ferma;
    f->base.latenza_us = finta_latenza;
    f->base.libera = finta_libera;
    g_finta = f;
    return &f->base;
}
static void consuma(float *dest, size_t n)
{
    g_finta->cb.riempi(g_finta->cb.ctx, dest, n);
}

/* ---- Avvisi -------------------------------------------------------------- */
static _Atomic int g_avvisi, g_esito;
static void avviso(void *ctx, int esito, const char *testo)
{
    (void)ctx;
    CONTROLLA(testo && testo[0]);
    atomic_store(&g_esito, esito);
    atomic_fetch_add(&g_avvisi, 1);
}

static void attendi_avvisi(int n)
{
    for (int i = 0; i < 300 && atomic_load(&g_avvisi) < n; i++)
        usleep(10000);
}

/* Spedisce 'n' pacchetti a passo di 20 ms, consumando 960 frame dopo ognuno,
 * e accumula l'uscita in 'reg' (se non NULL). Salta i pacchetti il cui indice
 * e' in 'saltati' (-1 termina l'elenco). */
typedef struct {
    Tono *tono;
    PhCanale *canale;
    int scritti;                       /* pacchetti spediti (compresi i saltati) */
    int64_t consegnati;                /* frame di uscita in tutto */
    int64_t ora_consuma;               /* ph_audio_ora_us() subito prima dell'ultima consuma */
} Pista;

static void corri(PhAudio *a, Pista *p, int n, float *reg, size_t *reg_n, const int *saltati)
{
    (void)a;
    uint8_t pk[1500], w[1600];
    float out[960 * 2];
    for (int i = 0; i < n; i++) {
        uint64_t tempo;
        int len = tono_pacchetto(p->tono, pk, &tempo);
        bool salta = false;
        for (const int *s = saltati; s && *s >= 0; s++)
            if (*s == p->scritti)
                salta = true;
        if (!salta) {
            size_t k = fc_impacchetta(w, tempo, pk, (size_t)len);
            fc_scrivi(p->canale, w, k);
        }
        p->scritti++;
        usleep(20000);
        p->ora_consuma = ph_audio_ora_us();
        consuma(out, 960);
        p->consegnati += 960;
        if (reg) {
            memcpy(reg + *reg_n * 2, out, sizeof out);
            *reg_n += 960;
        }
    }
}

static double energia(const float *x, size_t frame)
{
    double e = 0;
    for (size_t i = 0; i < frame * 2; i++)
        e += (double)x[i] * x[i];
    return e / (double)(frame * 2);
}

int main(void)
{
    PhErrore e = {{0}};
    PhAudioStat s;
    static float reg[960 * 2 * 400];
    size_t nreg = 0;

    /* Argomenti sbagliati. */
    CONTROLLA(ph_audio_crea_con_uscita(NULL, avviso, NULL, finta_crea, &e) == NULL);
    ph_audio_distruggi(NULL);

    /* ---- Tono: decodifica e riproduzione, volume, muto, orologio ---- */
    int chiuse0 = fc_chiusure();
    PhCanale *c = fc_crea();
    PhAudio *a = ph_audio_crea_con_uscita(c, avviso, NULL, finta_crea, &e);
    CONTROLLA(a);
    CONTROLLA(ph_audio_volume_leggi(a) == 100 && !ph_audio_muto_leggi(a));
    CONTROLLA(ph_audio_tempo_telefono_us(a) == -1);
    CONTROLLA(ph_audio_avvia(a, &e) == 0);
    CONTROLLA(ph_audio_avvia(a, NULL) == -1);       /* una volta sola */
    Pista p = {tono_crea(0.5), c, 0, 0, 0};
    int saltati[] = {40, 41, -1};
    corri(a, &p, 80, reg, &nreg, saltati);
    ph_audio_statistiche(a, &s);
    CONTROLLA(s.ricevuti == 78 && s.persi == 2 && s.in_ritardo == 0 && s.non_validi == 0);
    CONTROLLA(s.sottoalimentazioni == 0);
    {
        /* Tono a ~440 Hz (la coda in tempo reale puo' comprimere o allungare di
         * qualche percento): si guardano finestre di 5 pacchetti (100 ms), dove
         * il rapporto di lettura e' costante, e l'energia sul tratto lungo. */
        double en;
        for (int w = 12; w + 5 <= 37; w += 5)
            CONTROLLA(picco_frequenza(reg + (size_t)w * 960 * 2, 5 * 960, 2, NULL) > 0.8);
        picco_frequenza(reg + 12 * 960 * 2, 25 * 960, 2, &en);
        CONTROLLA(en > 0.10 && en < 0.15);
        /* E anche dopo il buco occultato. */
        for (int w = 52; w + 5 <= 77; w += 5)
            CONTROLLA(picco_frequenza(reg + (size_t)w * 960 * 2, 5 * 960, 2, NULL) > 0.8);
    }
    /* Orologio: tempo del telefono dell'ultimo campione consegnato meno la latenza (30 ms). */
    {
        PhAudioStat st;
        ph_audio_statistiche(a, &st);
        int64_t consumati = (int64_t)p.scritti * 960 - (int64_t)st.margine_ms * 48;
        int64_t atteso = consumati * 1000000 / 48000 - 30000;
        int64_t t = ph_audio_tempo_telefono_us(a);
        CONTROLLA(t > 0);
        /* La stima avanza col tempo reale dall'ultima consegna: il limite alto e' quello
         * piu' il tempo passato davvero dalla consegna alla lettura. */
        int64_t passato = ph_audio_ora_us() - p.ora_consuma;
        CONTROLLA(t > atteso - 15000 && t < atteso + 15000 + passato);
    }
    /* Volume 50: guadagno cubico (0.5^3 = 0.125), energia 0.125^2 = 1/64. */
    ph_audio_volume(a, 50);
    CONTROLLA(ph_audio_volume_leggi(a) == 50);
    nreg = 0;
    corri(a, &p, 40, reg, &nreg, NULL);
    {
        double en = energia(reg + 10 * 960 * 2, 20 * 960);
        CONTROLLA(en > 0.125 / 64 * 0.8 && en < 0.125 / 64 * 1.2);
    }
    /* Torna a 100: guadagno 1. */
    ph_audio_volume(a, 100);
    nreg = 0;
    corri(a, &p, 25, reg, &nreg, NULL);
    CONTROLLA(energia(reg + 10 * 960 * 2, 14 * 960) > 0.10);
    ph_audio_volume(a, 150);
    CONTROLLA(ph_audio_volume_leggi(a) == 100);
    ph_audio_volume(a, 100);
    ph_audio_volume(a, -5);
    CONTROLLA(ph_audio_volume_leggi(a) == 0);
    /* Volume 0 in un colpo solo: la rampa deve scendere piano (nessun clic) e arrivare a zero. */
    nreg = 0;
    corri(a, &p, 1, reg, &nreg, NULL);
    {
        double salto = 0;
        for (size_t i = 1; i < 960; i++) {
            double d = fabs((double)reg[i * 2] - (double)reg[(i - 1) * 2]);
            if (d > salto)
                salto = d;
        }
        CONTROLLA(salto < 0.05);
        CONTROLLA(reg[600 * 2] == 0.0f && reg[959 * 2] == 0.0f);
        CONTROLLA(fabsf(reg[100 * 2]) > 0.0f || fabsf(reg[101 * 2]) > 0.0f);   /* a meta' rampa suona ancora */
    }
    /* Muto con volume 100: silenzio, ma i pacchetti si ricevono comunque. */
    ph_audio_volume(a, 100);
    ph_audio_muto(a, true);
    CONTROLLA(ph_audio_muto_leggi(a));
    ph_audio_statistiche(a, &s);
    uint64_t prima = s.ricevuti;
    nreg = 0;
    corri(a, &p, 20, reg, &nreg, NULL);
    ph_audio_statistiche(a, &s);
    CONTROLLA(s.ricevuti == prima + 20);
    CONTROLLA(energia(reg + 5 * 960 * 2, 14 * 960) == 0.0);
    ph_audio_muto(a, false);
    nreg = 0;
    corri(a, &p, 20, reg, &nreg, NULL);
    CONTROLLA(energia(reg + 5 * 960 * 2, 14 * 960) > 0.10);

    /* Il telefono chiude: avviso FINITO una volta; quel che c'e' in coda si suona. */
    CONTROLLA(atomic_load(&g_avvisi) == 0);
    fc_fine(c);
    attendi_avvisi(1);
    CONTROLLA(atomic_load(&g_avvisi) == 1 && atomic_load(&g_esito) == PH_AUDIO_FINITO);
    {
        float out[960 * 2];
        int64_t veri = 0;
        for (int i = 0; i < 20; i++) {
            consuma(out, 960);
            if (energia(out, 960) > 0.001)
                veri += 960;
        }
        CONTROLLA(veri > 0);
    }
    usleep(100000);
    CONTROLLA(atomic_load(&g_avvisi) == 1);
    ph_audio_ferma(a);
    ph_audio_ferma(a);                                  /* ripetibile */
    CONTROLLA(fc_chiusure() == chiuse0);                /* il canale resta aperto finche' non si distrugge */
    ph_audio_distruggi(a);
    CONTROLLA(fc_chiusure() == chiuse0 + 1);
    tono_libera(p.tono);

    /* ---- Errore del canale: avviso ERRORE ---- */
    atomic_store(&g_avvisi, 0);
    c = fc_crea();
    a = ph_audio_crea_con_uscita(c, avviso, NULL, finta_crea, &e);
    CONTROLLA(a && ph_audio_avvia(a, &e) == 0);
    fc_errore(c);
    attendi_avvisi(1);
    CONTROLLA(atomic_load(&g_avvisi) == 1 && atomic_load(&g_esito) == PH_AUDIO_ERRORE);
    ph_audio_distruggi(a);

    /* ---- Spazzatura sul canale: lunghezza falsa -> ERRORE, senza uscire dai buffer ---- */
    atomic_store(&g_avvisi, 0);
    c = fc_crea();
    a = ph_audio_crea_con_uscita(c, avviso, NULL, finta_crea, &e);
    CONTROLLA(a && ph_audio_avvia(a, &e) == 0);
    {
        uint8_t h[10] = {0, 0, 0, 0, 0, 0, 0, 5, 0x7f, 0xff};
        fc_scrivi(c, h, sizeof h);
    }
    attendi_avvisi(1);
    CONTROLLA(atomic_load(&g_avvisi) == 1 && atomic_load(&g_esito) == PH_AUDIO_ERRORE);
    ph_audio_distruggi(a);

    /* ---- Pacchetti con framing giusto ma dati non Opus: scartati, poi flusso rotto ---- */
    atomic_store(&g_avvisi, 0);
    c = fc_crea();
    a = ph_audio_crea_con_uscita(c, avviso, NULL, finta_crea, &e);
    CONTROLLA(a && ph_audio_avvia(a, &e) == 0);
    for (int i = 0; i < 150; i++) {
        uint8_t w[16], sp = 0x03;
        size_t k = fc_impacchetta(w, (uint64_t)i * 20000, &sp, 1);
        fc_scrivi(c, w, k);
    }
    attendi_avvisi(1);
    CONTROLLA(atomic_load(&g_avvisi) == 1 && atomic_load(&g_esito) == PH_AUDIO_ERRORE);
    ph_audio_distruggi(a);

    /* ---- Arresto voluto: nessun avviso ---- */
    atomic_store(&g_avvisi, 0);
    c = fc_crea();
    a = ph_audio_crea_con_uscita(c, avviso, NULL, finta_crea, &e);
    CONTROLLA(a && ph_audio_avvia(a, &e) == 0);
    usleep(150000);
    ph_audio_ferma(a);
    fc_fine(c);
    usleep(150000);
    CONTROLLA(atomic_load(&g_avvisi) == 0);
    ph_audio_distruggi(a);

    /* ---- Uscita che non parte: errore, canale comunque chiuso alla distruzione ---- */
    g_avvio_fallisce = true;
    c = fc_crea();
    chiuse0 = fc_chiusure();
    a = ph_audio_crea_con_uscita(c, avviso, NULL, finta_crea, &e);
    CONTROLLA(a);
    e.testo[0] = 0;
    CONTROLLA(ph_audio_avvia(a, &e) == -1 && e.testo[0]);
    ph_audio_distruggi(a);
    CONTROLLA(fc_chiusure() == chiuse0 + 1);
    g_avvio_fallisce = false;
    g_fabbrica_fallisce = true;
    c = fc_crea();
    a = ph_audio_crea_con_uscita(c, avviso, NULL, finta_crea, &e);
    CONTROLLA(a && ph_audio_avvia(a, &e) == -1);
    ph_audio_distruggi(a);
    g_fabbrica_fallisce = false;
    return 0;
}
