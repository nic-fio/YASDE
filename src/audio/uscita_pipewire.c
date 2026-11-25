/* Uscita audio con PipeWire: un pw_stream di riproduzione, 48 kHz stereo
 * float, nome dell'app "YASDE". Il resto del pezzo la vede solo come
 * PhUscita (interno.h). */
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/interno.h"

#define ATTESA_AVVIO_S 3

typedef struct {
    PhUscita base;
    PhUscitaCb cb;
    struct pw_thread_loop *loop;
    struct pw_stream *stream;
    struct pw_stream_events eventi;
    _Atomic int64_t latenza_us;
    _Atomic int64_t d_delay, d_buffered, d_queued;   /* ultimi valori di pw_time, per la diagnosi */
    int stato;                 /* 0 in attesa, 1 pronto, -1 errore: sotto il lock del loop */
    char errore[200];
    bool pw_iniziato;
} UscitaPw;

static void su_stato(void *dati, enum pw_stream_state vecchio, enum pw_stream_state nuovo,
                     const char *errore)
{
    UscitaPw *u = dati;
    (void)vecchio;
    if (nuovo == PW_STREAM_STATE_ERROR) {
        bool era_avviata = u->stato == 1;
        u->stato = -1;
        snprintf(u->errore, sizeof u->errore, "%s", errore ? errore : "errore di PipeWire");
        if (era_avviata && u->cb.errore)
            u->cb.errore(u->cb.ctx, u->errore);
    } else if (nuovo == PW_STREAM_STATE_PAUSED || nuovo == PW_STREAM_STATE_STREAMING) {
        if (u->stato == 0)
            u->stato = 1;
    }
    pw_thread_loop_signal(u->loop, false);
}

static void su_processo(void *dati)
{
    UscitaPw *u = dati;
    struct pw_buffer *b = pw_stream_dequeue_buffer(u->stream);
    if (!b)
        return;
    struct spa_data *d = &b->buffer->datas[0];
    float *dst = d->data;
    if (!dst) {
        pw_stream_queue_buffer(u->stream, b);
        return;
    }
    const uint32_t passo = sizeof(float) * PH_AUDIO_CANALI;
    uint32_t n = d->maxsize / passo;
    if (b->requested && b->requested < n)
        n = (uint32_t)b->requested;
    u->cb.riempi(u->cb.ctx, dst, n);
    d->chunk->offset = 0;
    d->chunk->stride = (int32_t)passo;
    d->chunk->size = n * passo;
    b->size = n;   /* in frame: e' la somma di questi che PipeWire riporta in pw_time.queued */
    pw_stream_queue_buffer(u->stream, b);

    struct pw_time t;
    if (pw_stream_get_time_n(u->stream, &t, sizeof t) == 0 && t.rate.denom > 0 && t.delay >= 0) {
        /* Dal frame appena consegnato al suo suono: il ritardo del dispositivo
         * (delay, in tick di 'rate') piu' cio' che e' ancora dentro lo stream
         * (buffered e queued, in frame). */
        int64_t us = t.delay * 1000000 * (int64_t)t.rate.num / (int64_t)t.rate.denom;
        us += ((int64_t)t.buffered + (int64_t)t.queued) * 1000000 / PH_AUDIO_RATE;
        atomic_store(&u->latenza_us, us);
        atomic_store(&u->d_delay, t.delay);
        atomic_store(&u->d_buffered, (int64_t)t.buffered);
        atomic_store(&u->d_queued, (int64_t)t.queued);
    }
}

static int pw_avvia(PhUscita *base, PhErrore *e)
{
    UscitaPw *u = (UscitaPw *)base;
    u->loop = pw_thread_loop_new("yasde-audio", NULL);
    if (!u->loop)
        return ph_errore(e, "PipeWire: thread di servizio non creato");
    struct pw_properties *prop = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Movie",
        PW_KEY_APP_NAME, "YASDE", PW_KEY_NODE_NAME, "YASDE", PW_KEY_NODE_LATENCY, "1024/48000",
        NULL);
    if (!prop)
        return ph_errore(e, "PipeWire: memoria esaurita");
    u->eventi.version = PW_VERSION_STREAM_EVENTS;
    u->eventi.state_changed = su_stato;
    u->eventi.process = su_processo;
    u->stream = pw_stream_new_simple(pw_thread_loop_get_loop(u->loop), "YASDE", prop, &u->eventi, u);
    if (!u->stream)
        return ph_errore(e, "PipeWire: flusso audio non creato");
    if (pw_thread_loop_start(u->loop) < 0)
        return ph_errore(e, "PipeWire: thread di servizio non partito");

    pw_thread_loop_lock(u->loop);
    uint8_t buf[1024];
    struct spa_pod_builder pb = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    struct spa_audio_info_raw info = SPA_AUDIO_INFO_RAW_INIT(
        .format = SPA_AUDIO_FORMAT_F32, .channels = PH_AUDIO_CANALI, .rate = PH_AUDIO_RATE,
        .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR});
    const struct spa_pod *params[1];
    params[0] = spa_format_audio_raw_build(&pb, SPA_PARAM_EnumFormat, &info);
    int r = pw_stream_connect(u->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                              PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                  PW_STREAM_FLAG_RT_PROCESS,
                              params, 1);
    if (r < 0) {
        pw_thread_loop_unlock(u->loop);
        return ph_errore(e, "PipeWire: collegamento del flusso fallito (%s)", strerror(-r));
    }
    struct timespec limite;
    pw_thread_loop_get_time(u->loop, &limite, (int64_t)ATTESA_AVVIO_S * SPA_NSEC_PER_SEC);
    while (u->stato == 0) {
        if (pw_thread_loop_timed_wait_full(u->loop, &limite) < 0)
            break;
    }
    int stato = u->stato;
    char errore[sizeof u->errore];
    snprintf(errore, sizeof errore, "%s", u->errore);
    pw_thread_loop_unlock(u->loop);
    if (stato < 0)
        return ph_errore(e, "PipeWire: %s", errore);
    if (stato == 0)
        return ph_errore(e, "PipeWire non risponde (daemon assente?)");
    return 0;
}

static void pw_ferma(PhUscita *base)
{
    UscitaPw *u = (UscitaPw *)base;
    if (u->loop)
        pw_thread_loop_stop(u->loop);
    if (u->stream) {
        pw_stream_destroy(u->stream);
        u->stream = NULL;
    }
    if (u->loop) {
        pw_thread_loop_destroy(u->loop);
        u->loop = NULL;
    }
}

static int64_t pw_latenza(PhUscita *base)
{
    UscitaPw *u = (UscitaPw *)base;
    return atomic_load(&u->latenza_us);
}

static void pw_diagnosi(PhUscita *base, int64_t *delay, int64_t *buffered, int64_t *queued)
{
    UscitaPw *u = (UscitaPw *)base;
    *delay = atomic_load(&u->d_delay);
    *buffered = atomic_load(&u->d_buffered);
    *queued = atomic_load(&u->d_queued);
}

static void pw_libera(PhUscita *base)
{
    UscitaPw *u = (UscitaPw *)base;
    pw_ferma(base);
    if (u->pw_iniziato)
        pw_deinit();
    free(u);
}

PhUscita *ph_uscita_pipewire(const PhUscitaCb *cb, PhErrore *e)
{
    UscitaPw *u = calloc(1, sizeof *u);
    if (!u) {
        ph_errore(e, "memoria esaurita creando l'uscita audio");
        return NULL;
    }
    u->cb = *cb;
    atomic_init(&u->latenza_us, -1);
    u->base.avvia = pw_avvia;
    u->base.ferma = pw_ferma;
    u->base.latenza_us = pw_latenza;
    u->base.libera = pw_libera;
    u->base.diagnosi = pw_diagnosi;
    atomic_init(&u->d_delay, -1);
    atomic_init(&u->d_buffered, -1);
    atomic_init(&u->d_queued, -1);
    pw_init(NULL, NULL);
    u->pw_iniziato = true;
    return &u->base;
}
