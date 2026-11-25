/* Il riproduttore: tiene insieme lettura del canale, decodifica, coda e uscita. */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "audio/interno.h"

#define TIMEOUT_LETTURA_MS 100   /* quanto al massimo ci mette ph_audio_ferma */
#define RAMPA_FRAME 480          /* 10 ms */

struct PhAudio {
    PhCanale *canale;
    PhAudioAvviso avviso;
    void *ctx_avviso;
    PhUscitaFabbrica fabbrica;
    PhUscita *uscita;
    PhLettore lettore;
    PhCodaAudio *coda;
    PhFlusso *flusso;
    pthread_t thread;
    bool thread_attivo;
    bool avviato;
    _Atomic bool stop;
    _Atomic bool avvisato;
    _Atomic int volume;
    _Atomic bool muto;
    float guadagno;           /* solo dal thread dell'uscita */
    bool guadagno_iniziato;
};

static void avvisa(PhAudio *a, int esito, const char *testo)
{
    /* Una sola volta, e mai dopo un arresto voluto. */
    if (atomic_load(&a->stop) || atomic_exchange(&a->avvisato, true))
        return;
    if (a->avviso)
        a->avviso(a->ctx_avviso, esito, testo);
}

static void uscita_riempi(void *ctx, float *dest, size_t n)
{
    PhAudio *a = ctx;
    ph_coda_estrai(a->coda, dest, n, ph_audio_ora_us());
    /* Curva cubica come i cursori di PipeWire/PulseAudio: il cursore e' uniforme per l'orecchio. */
    float v = (float)atomic_load(&a->volume) / 100.0f;
    float bersaglio = atomic_load(&a->muto) ? 0.0f : v * v * v;
    if (!a->guadagno_iniziato) {
        a->guadagno = bersaglio;
        a->guadagno_iniziato = true;
    }
    /* Rampa lineare: da qualunque guadagno a quello nuovo in 10 ms al massimo. */
    const float passo = 1.0f / RAMPA_FRAME;
    float g = a->guadagno;
    for (size_t i = 0; i < n; i++) {
        if (g < bersaglio) {
            g += passo;
            if (g > bersaglio)
                g = bersaglio;
        } else if (g > bersaglio) {
            g -= passo;
            if (g < bersaglio)
                g = bersaglio;
        }
        dest[i * PH_AUDIO_CANALI] *= g;
        dest[i * PH_AUDIO_CANALI + 1] *= g;
    }
    a->guadagno = g;
}

static void uscita_errore(void *ctx, const char *testo)
{
    avvisa(ctx, PH_AUDIO_ERRORE, testo);
}

static void *thread_lettura(void *arg)
{
    PhAudio *a = arg;
    PhPacchetto p;
    while (!atomic_load(&a->stop)) {
        PhErrore e = {{0}};
        int r = ph_lettore_leggi(&a->lettore, a->canale, TIMEOUT_LETTURA_MS, &p, &e);
        if (r == PH_LETTURA_TEMPO)
            continue;
        if (r == PH_LETTURA_PACCHETTO) {
            if (ph_flusso_pacchetto(a->flusso, p.tempo_us, p.dati, p.lunghezza, ph_audio_ora_us(), &e) == 0)
                continue;
            r = PH_LETTURA_ERRORE;
        }
        if (r == PH_LETTURA_CHIUSO) {
            ph_coda_fine(a->coda);   /* quel che resta si suona fino in fondo */
            avvisa(a, PH_AUDIO_FINITO, "il telefono ha chiuso il canale audio");
        } else {
            avvisa(a, PH_AUDIO_ERRORE, e.testo[0] ? e.testo : "errore sul canale audio");
        }
        break;
    }
    return NULL;
}

PhAudio *ph_audio_crea_con_uscita(PhCanale *canale, PhAudioAvviso avviso, void *ctx,
                                  PhUscitaFabbrica fabbrica, PhErrore *e)
{
    if (!canale) {
        ph_errore(e, "audio: canale mancante");
        return NULL;
    }
    PhAudio *a = calloc(1, sizeof *a);
    if (!a) {
        ph_errore(e, "memoria esaurita creando il riproduttore audio");
        ph_canale_chiudi(canale);
        return NULL;
    }
    a->canale = canale;
    a->avviso = avviso;
    a->ctx_avviso = ctx;
    a->fabbrica = fabbrica;
    atomic_init(&a->stop, false);
    atomic_init(&a->avvisato, false);
    atomic_init(&a->volume, 100);
    atomic_init(&a->muto, false);
    ph_lettore_inizia(&a->lettore);
    a->coda = ph_coda_crea();
    if (!a->coda) {
        ph_errore(e, "memoria esaurita creando la coda audio");
        ph_audio_distruggi(a);
        return NULL;
    }
    a->flusso = ph_flusso_crea(a->coda, e);
    if (!a->flusso) {
        ph_audio_distruggi(a);
        return NULL;
    }
    return a;
}

PhAudio *ph_audio_crea(PhCanale *canale, PhAudioAvviso avviso, void *ctx, PhErrore *e)
{
    return ph_audio_crea_con_uscita(canale, avviso, ctx, ph_uscita_pipewire, e);
}

int ph_audio_avvia(PhAudio *a, PhErrore *e)
{
    if (a->avviato)
        return ph_errore(e, "audio gia' avviato");
    PhUscitaCb cb = {.riempi = uscita_riempi, .errore = uscita_errore, .ctx = a};
    a->uscita = a->fabbrica(&cb, e);
    if (!a->uscita)
        return -1;
    if (a->uscita->avvia(a->uscita, e) < 0) {
        a->uscita->libera(a->uscita);
        a->uscita = NULL;
        return -1;
    }
    if (pthread_create(&a->thread, NULL, thread_lettura, a) != 0) {
        a->uscita->ferma(a->uscita);
        a->uscita->libera(a->uscita);
        a->uscita = NULL;
        return ph_errore(e, "thread di lettura audio non creato");
    }
    a->thread_attivo = true;
    a->avviato = true;
    return 0;
}

void ph_audio_ferma(PhAudio *a)
{
    atomic_store(&a->stop, true);
    if (a->thread_attivo) {
        pthread_join(a->thread, NULL);
        a->thread_attivo = false;
    }
    if (a->uscita) {
        a->uscita->ferma(a->uscita);
        a->uscita->libera(a->uscita);
        a->uscita = NULL;
    }
}

void ph_audio_distruggi(PhAudio *a)
{
    if (!a)
        return;
    ph_audio_ferma(a);
    if (a->canale)
        ph_canale_chiudi(a->canale);
    ph_flusso_libera(a->flusso);
    ph_coda_libera(a->coda);
    free(a);
}

void ph_audio_volume(PhAudio *a, int percento)
{
    if (percento < 0)
        percento = 0;
    if (percento > 100)
        percento = 100;
    atomic_store(&a->volume, percento);
}

int ph_audio_volume_leggi(PhAudio *a)
{
    return atomic_load(&a->volume);
}

void ph_audio_muto(PhAudio *a, bool muto)
{
    atomic_store(&a->muto, muto);
}

bool ph_audio_muto_leggi(PhAudio *a)
{
    return atomic_load(&a->muto);
}

int64_t ph_audio_tempo_telefono_us(PhAudio *a)
{
    PhUscita *u = a->uscita;
    int64_t lat = u ? u->latenza_us(u) : -1;
    if (lat < 0)
        lat = 0;   /* latenza non nota: meglio una stima che niente */
    return ph_coda_tempo(a->coda, ph_audio_ora_us(), lat);
}

void ph_audio_statistiche(PhAudio *a, PhAudioStat *s)
{
    memset(s, 0, sizeof *s);
    ph_flusso_contatori(a->flusso, s);
    PhCodaStat cs;
    ph_coda_stat(a->coda, &cs);
    s->sottoalimentazioni = cs.sottoalimentazioni;
    s->frame_scartati = cs.scartati;
    s->margine_ms = cs.margine_ms;
    s->obiettivo_ms = cs.obiettivo_ms;
    s->diag_delay_tick = s->diag_buffered = s->diag_queued = s->diag_latenza_us = -1;
    PhUscita *u = a->uscita;
    if (u) {
        s->diag_latenza_us = u->latenza_us(u);
        if (u->diagnosi)
            u->diagnosi(u, &s->diag_delay_tick, &s->diag_buffered, &s->diag_queued);
    }
}
