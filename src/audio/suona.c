/* Strumento a mano (non e' un test automatico): riproduce davvero con PipeWire
 * un flusso audio nel formato di §2.5. Senza file genera un tono a 440 Hz e lo
 * codifica Opus al volo. Un thread fa da "telefono": scrive i pacchetti su un
 * canale finto a ritmo reale, con arrivi regolari o irregolari. Ogni secondo
 * stampa le statistiche del riproduttore.
 *
 *   suona [-s secondi] [-v volume] [-j regolare|raffiche|buchi|casuale] [-f file]
 *
 * -j raffiche: i pacchetti arrivano a gruppi di 5 ogni 100 ms
 * -j buchi:    ogni 3 secondi uno stallo di 300 ms
 * -j casuale:  ogni pacchetto in ritardo di 0-40 ms (mai fuori ordine) */
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "audio/audio.h"
#include "audio/interno.h"
#include "audio/prova/aiuti.h"
#include "audio/prova/finto_canale.h"

typedef enum { J_REGOLARE, J_RAFFICHE, J_BUCHI, J_CASUALE } Jitter;

typedef struct {
    PhCanale *canale;
    Jitter jitter;
    const uint8_t *file;   /* NULL = tono */
    size_t file_n;
    _Atomic bool stop;
} Telefono;

static int64_t ora_us(void)
{
    return ph_audio_ora_us();
}

static void dormi_fino(int64_t t_us)
{
    struct timespec ts = {.tv_sec = t_us / 1000000, .tv_nsec = (t_us % 1000000) * 1000};
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
}

static void *thread_telefono(void *arg)
{
    Telefono *t = arg;
    Tono *tono = t->file ? NULL : tono_crea(0.3);
    int64_t inizio = ora_us(), ultimo = inizio, primo_tempo = -1;
    size_t pos = 0;
    uint8_t pk[1500], w[1600];
    srand(1);
    while (!atomic_load(&t->stop)) {
        uint64_t tempo;
        const uint8_t *dati;
        size_t n;
        if (tono) {
            n = (size_t)tono_pacchetto(tono, pk, &tempo);
            dati = pk;
        } else {
            if (pos + PH_AUDIO_INTESTAZIONE > t->file_n)
                break;
            const uint8_t *h = t->file + pos;
            tempo = 0;
            for (int i = 0; i < 8; i++)
                tempo = (tempo << 8) | h[i];
            n = ((size_t)h[8] << 8) | h[9];
            if (pos + PH_AUDIO_INTESTAZIONE + n > t->file_n || n > PH_AUDIO_PACCHETTO_MAX)
                break;
            dati = h + PH_AUDIO_INTESTAZIONE;
            pos += PH_AUDIO_INTESTAZIONE + n;
        }
        if (primo_tempo < 0)
            primo_tempo = (int64_t)tempo;
        int64_t rel = (int64_t)tempo - primo_tempo;
        int64_t due = inizio + rel;
        switch (t->jitter) {
        case J_REGOLARE:
            break;
        case J_RAFFICHE:
            due = inizio + (rel + 99999) / 100000 * 100000;
            break;
        case J_BUCHI: {
            int64_t k = rel / 3000000;
            if (k >= 1 && rel < k * 3000000 + 300000)
                due = inizio + k * 3000000 + 300000;
            break;
        }
        case J_CASUALE:
            due += rand() % 40000;
            break;
        }
        if (due < ultimo)
            due = ultimo;
        ultimo = due;
        dormi_fino(due);
        size_t k = fc_impacchetta(w, tempo, dati, n);
        fc_scrivi(t->canale, w, k);
    }
    if (!atomic_load(&t->stop))
        fc_fine(t->canale);
    if (tono)
        tono_libera(tono);
    return NULL;
}

static volatile sig_atomic_t finito;
static void avviso(void *ctx, int esito, const char *testo)
{
    (void)ctx;
    fprintf(stderr, "avviso: %s (%s)\n", testo, esito == PH_AUDIO_FINITO ? "fine" : "errore");
    finito = 1;
}

int main(int argc, char **argv)
{
    int secondi = 3, volume = 100;
    Jitter jitter = J_REGOLARE;
    const char *percorso = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            secondi = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-v") && i + 1 < argc) {
            volume = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-f") && i + 1 < argc) {
            percorso = argv[++i];
        } else if (!strcmp(argv[i], "-j") && i + 1 < argc) {
            const char *j = argv[++i];
            jitter = !strcmp(j, "raffiche") ? J_RAFFICHE
                   : !strcmp(j, "buchi")    ? J_BUCHI
                   : !strcmp(j, "casuale")  ? J_CASUALE
                   : !strcmp(j, "regolare") ? J_REGOLARE
                                            : (fprintf(stderr, "jitter sconosciuto: %s\n", j), exit(2), J_REGOLARE);
        } else {
            fprintf(stderr, "uso: suona [-s secondi] [-v volume] [-j regolare|raffiche|buchi|casuale] [-f file]\n");
            return 2;
        }
    }
    Telefono tel = {.jitter = jitter};
    atomic_init(&tel.stop, false);
    uint8_t *contenuto = NULL;
    if (percorso) {
        FILE *f = fopen(percorso, "rb");
        if (!f) {
            perror(percorso);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long dim = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (dim < 0 || dim > (64L << 20)) {
            fprintf(stderr, "file troppo grande o illeggibile\n");
            fclose(f);
            return 1;
        }
        contenuto = malloc((size_t)dim + 1);
        if (!contenuto || fread(contenuto, 1, (size_t)dim, f) != (size_t)dim) {
            fprintf(stderr, "lettura del file fallita\n");
            fclose(f);
            return 1;
        }
        fclose(f);
        tel.file = contenuto;
        tel.file_n = (size_t)dim;
    }
    tel.canale = fc_crea();
    PhErrore e = {{0}};
    PhAudio *a = ph_audio_crea(tel.canale, avviso, NULL, &e);
    if (!a) {
        fprintf(stderr, "audio: %s\n", e.testo);
        return 1;
    }
    ph_audio_volume(a, volume);
    if (ph_audio_avvia(a, &e) < 0) {
        fprintf(stderr, "audio: %s\n", e.testo);
        ph_audio_distruggi(a);
        return 1;
    }
    pthread_t th;
    pthread_create(&th, NULL, thread_telefono, &tel);
    for (int s = 0; s < secondi && !finito; s++) {
        sleep(1);
        PhAudioStat st;
        ph_audio_statistiche(a, &st);
        printf("%ds: ricevuti %llu persi %llu ritardo %llu margine %d ms (obiettivo %d) sotto %llu scartati %llu "
               "orologio %lld us | pw delay %lld tick buffered %lld queued %lld latenza %lld us\n",
               s + 1, (unsigned long long)st.ricevuti, (unsigned long long)st.persi,
               (unsigned long long)st.in_ritardo, st.margine_ms, st.obiettivo_ms,
               (unsigned long long)st.sottoalimentazioni, (unsigned long long)st.frame_scartati,
               (long long)ph_audio_tempo_telefono_us(a), (long long)st.diag_delay_tick,
               (long long)st.diag_buffered, (long long)st.diag_queued, (long long)st.diag_latenza_us);
        fflush(stdout);
    }
    atomic_store(&tel.stop, true);
    pthread_join(th, NULL);
    ph_audio_distruggi(a);
    free(contenuto);
    return 0;
}
