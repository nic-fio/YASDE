/* Coda dell'audio decodificato, con margine adattivo.
 *
 * Idea: tra il pacchetto che arriva e il campione che esce c'e' un margine di
 * pochi ms che assorbe i ritardi della rete.
 *  - Quanto margine serve lo dice la rete: per ogni pacchetto si misura quanto
 *    e' in ritardo rispetto al piu' puntuale degli ultimi 10 secondi (tempo di
 *    arrivo meno campo "tempo"; gli orologi dei due lati non sono confrontabili,
 *    ma la loro differenza si'). Il ritardo massimo di quella finestra, piu' un
 *    minimo fisso, e' il margine ideale. Sale subito, scende di 8 ms al secondo:
 *    cosi' si restringe anche durante la riproduzione, senza aspettare silenzi.
 *  - Quanta audio c'e' davvero in coda si controlla ogni mezzo secondo (minimo
 *    visto prima di ogni consegna). Se e' molto sopra il margine si consuma un
 *    po' piu' in fretta, se e' molto sotto un po' piu' piano, rileggendo i
 *    campioni con interpolazione lineare (al massimo 2%: non si sente ne' come
 *    clic ne' come cambio di tono). Un eccesso enorme (dopo uno stallo e una
 *    raffica) si taglia di colpo con una breve dissolvenza.
 *  - Coda vuota durante la riproduzione = sottoalimentazione: si dissolve il
 *    suono, si torna in "riempimento" e si riparte solo col margine pieno. */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio/interno.h"

#define CAPACITA (PH_AUDIO_RATE * 2)         /* 2 secondi */
#define FRAME_PER_MS (PH_AUDIO_RATE / 1000)
#define FINESTRE 10                          /* secondi di storia per la stima */
#define CAMPIONI_MIN_STIMA 25                /* mezzo secondo di pacchetti */
#define MARGINE_FISSO_MS 40
#define FINESTRA_CONTROLLO (PH_AUDIO_RATE / 2)
#define SOGLIA_RECUPERO_MS 40                /* scarto tollerato dal margine */
#define SOGLIA_TAGLIO_MS 250
#define RESTO_DOPO_TAGLIO_MS 40
#define RAPPORTO_MAX 0.02
#define DISSOLVENZA 240                      /* 5 ms */
#define SALITA_DOPO_BUCO_MS 20
#define SCENDI_MS_AL_SECONDO 8
#define SOGLIA_FINE_RECUPERO_MS 8           /* il recupero continua finche' non si e' a tanto dal margine */
#define SOGLIA_RIPRESA_MS 60                /* dopo uno stallo, arrivi cosi' puntuali tornano nella stima */
#define STALLO_MAX_US 5000000                /* si ignora al piu' per tanto: oltre e' l'orologio che e' cambiato */
#define DISCONTINUITA_MAX 16
#define SOGLIA_TEMPO_US 10000

typedef struct {
    uint64_t pos;       /* posizione assoluta (in frame) del primo frame del nuovo tratto */
    double tempo_us;    /* tempo del telefono di quel frame */
} Discontinuita;

typedef struct {
    int64_t sec;       /* -1 = vuota */
    int64_t min, max;  /* scostamento tempo di arrivo - tempo del telefono */
} Finestra;

struct PhCodaAudio {
    pthread_mutex_t m;
    float *anello;
    size_t testa, pieno;
    double tempo_testa_us;
    bool tempo_noto;
    int64_t ora_estrazione_us;
    double durata_consegna_us;   /* durata dell'ultima consegna: oltre non si estrapola */
    int64_t ultimo_tempo_us;     /* ultimo valore dell'orologio dato a chi chiede (-1 = nessuno) */
    bool in_riproduzione;
    bool fine;
    double frac;                 /* parte frazionaria della posizione di lettura */
    int obiettivo_ms;

    Finestra fin[FINESTRE];
    int64_t sec_corrente;
    int campioni;

    int64_t min_prima;           /* minimo di 'pieno' prima delle consegne, finestra in corso */
    size_t frame_finestra;
    int64_t ultimo_min_ms;       /* risultato dell'ultima finestra chiusa */
    bool ultimo_valido;
    size_t da_scendere;
    size_t dissolvenza_in;       /* frame ancora da dissolvere in entrata */

    uint64_t sottoalimentazioni, scartati;

    /* Posizioni assolute in frame (non si azzerano mai tranne in svuota): servono
     * per sapere quando la testa raggiunge un salto del tempo del telefono. */
    uint64_t testa_ass, inseriti_ass;
    Discontinuita disc[DISCONTINUITA_MAX];
    int n_disc;
    int recupero;                /* isteresi: 0 niente, +1 si consuma di piu', -1 di meno */
    int64_t ultima_ora_us;
    bool ignora;                 /* dentro uno stallo: gli arrivi non entrano nella stima */
    int64_t ignora_da_us;
};

int64_t ph_audio_ora_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static void azzera_stima(PhCodaAudio *c)
{
    for (int i = 0; i < FINESTRE; i++)
        c->fin[i].sec = -1;
    c->sec_corrente = -1;
    c->campioni = 0;
}

PhCodaAudio *ph_coda_crea(void)
{
    PhCodaAudio *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->anello = calloc((size_t)CAPACITA * PH_AUDIO_CANALI, sizeof(float));
    if (!c->anello) {
        free(c);
        return NULL;
    }
    pthread_mutex_init(&c->m, NULL);
    c->obiettivo_ms = PH_CODA_OBIETTIVO_INIZIALE_MS;
    c->min_prima = INT64_MAX;
    c->ultimo_tempo_us = -1;
    azzera_stima(c);
    return c;
}

void ph_coda_libera(PhCodaAudio *c)
{
    if (!c)
        return;
    pthread_mutex_destroy(&c->m);
    free(c->anello);
    free(c);
}

static float *frame_a(PhCodaAudio *c, size_t k)
{
    return c->anello + ((c->testa + k) % CAPACITA) * PH_AUDIO_CANALI;
}

static void avanza(PhCodaAudio *c, size_t k)
{
    c->testa = (c->testa + k) % CAPACITA;
    c->pieno -= k;
    c->testa_ass += k;
    c->tempo_testa_us += (double)k * 1000000.0 / PH_AUDIO_RATE;
    /* Se la testa ha raggiunto un salto del tempo (buco piu' lungo di quel che
     * si occulta), da qui il tempo riparte dal valore dichiarato dal telefono. */
    int j = 0;
    while (j < c->n_disc && c->disc[j].pos <= c->testa_ass) {
        double nuovo = c->disc[j].tempo_us +
                       (double)(c->testa_ass - c->disc[j].pos) * 1000000.0 / PH_AUDIO_RATE;
        if (nuovo < c->tempo_testa_us)
            c->ultimo_tempo_us = -1;   /* salto all'indietro: l'orologio riparte, non si puo' tenere fermo */
        c->tempo_testa_us = nuovo;
        j++;
    }
    if (j > 0) {
        c->n_disc -= j;
        memmove(c->disc, c->disc + j, (size_t)c->n_disc * sizeof c->disc[0]);
    }
}

static void accoda(PhCodaAudio *c, const float *frames, size_t n)
{
    if (n > CAPACITA)
        n = CAPACITA;
    if (c->pieno + n > CAPACITA) {
        /* Coda piena (raffica enorme): si butta il piu' vecchio. */
        size_t extra = c->pieno + n - CAPACITA;
        avanza(c, extra);
        c->scartati += extra;
        c->dissolvenza_in = DISSOLVENZA;
    }
    size_t fine = (c->testa + c->pieno) % CAPACITA;
    size_t primo = CAPACITA - fine < n ? CAPACITA - fine : n;
    memcpy(c->anello + fine * PH_AUDIO_CANALI, frames, primo * PH_AUDIO_CANALI * sizeof(float));
    if (n > primo)
        memcpy(c->anello, frames + primo * PH_AUDIO_CANALI,
               (n - primo) * PH_AUDIO_CANALI * sizeof(float));
    c->pieno += n;
    c->inseriti_ass += n;
}

static bool stima_base(PhCodaAudio *c, int64_t *base, int64_t *massimo)
{
    int64_t b = INT64_MAX, m = INT64_MIN;
    for (int i = 0; i < FINESTRE; i++) {
        const Finestra *f = &c->fin[i];
        if (f->sec < 0 || f->sec <= c->sec_corrente - FINESTRE)
            continue;
        if (f->min < b)
            b = f->min;
        if (f->max > m)
            m = f->max;
    }
    *base = b;
    *massimo = m;
    return b != INT64_MAX;
}

static int obiettivo_ideale(PhCodaAudio *c)
{
    int64_t base, massimo;
    if (!stima_base(c, &base, &massimo))
        return PH_CODA_OBIETTIVO_INIZIALE_MS;
    int64_t ritardo_ms = (massimo - base) / 1000;
    int64_t ideale = MARGINE_FISSO_MS + ritardo_ms + (ritardo_ms / 4 < 20 ? ritardo_ms / 4 : 20);
    if (ideale < PH_CODA_OBIETTIVO_MIN_MS)
        ideale = PH_CODA_OBIETTIVO_MIN_MS;
    if (ideale > PH_CODA_OBIETTIVO_MAX_MS)
        ideale = PH_CODA_OBIETTIVO_MAX_MS;
    return (int)ideale;
}

static void stima_aggiorna(PhCodaAudio *c, int64_t tempo_us, int64_t ora_us)
{
    int64_t off = ora_us - tempo_us;
    int64_t base, massimo;
    if (c->campioni > 0 && stima_base(c, &base, &massimo)) {
        int64_t ritardo = off - base;
        bool stallo = ora_us - c->ultima_ora_us > (int64_t)PH_CODA_OBIETTIVO_MAX_MS * 1000;
        if (!c->ignora && (stallo || ritardo > (int64_t)PH_CODA_OBIETTIVO_MAX_MS * 1000)) {
            c->ignora = true;
            c->ignora_da_us = ora_us;
        }
        /* Uno stallo piu' lungo di quanto la coda puo' assorbire non dice nulla sulla
         * rete di tutti i giorni: gli arrivi in ritardo che ne seguono (la raffica di
         * recupero) non entrano nella stima finche' non tornano puntuali. */
        if (c->ignora) {
            if (ritardo < SOGLIA_RIPRESA_MS * 1000) {
                c->ignora = false;
            } else if (ora_us - c->ignora_da_us >= STALLO_MAX_US) {
                azzera_stima(c);       /* l'orologio del telefono e' saltato: si riparte */
                c->ignora = false;
            } else {
                c->ultima_ora_us = ora_us;
                return;
            }
        }
    }
    c->ultima_ora_us = ora_us;
    int64_t s = ora_us < 0 ? 0 : ora_us / 1000000;
    if (s > c->sec_corrente)
        c->sec_corrente = s;
    Finestra *f = &c->fin[s % FINESTRE];
    if (f->sec != s) {
        f->sec = s;
        f->min = f->max = off;
    } else {
        if (off < f->min)
            f->min = off;
        if (off > f->max)
            f->max = off;
    }
    c->campioni++;
    if (c->campioni >= CAMPIONI_MIN_STIMA) {
        int ideale = obiettivo_ideale(c);
        if (ideale > c->obiettivo_ms)
            c->obiettivo_ms = ideale;   /* sale subito */
    }
}

void ph_coda_inserisci(PhCodaAudio *c, const float *frames, size_t n, int64_t tempo_us, int64_t ora_us)
{
    pthread_mutex_lock(&c->m);
    if (c->pieno == 0) {
        c->tempo_testa_us = (double)tempo_us;
        c->tempo_noto = true;
        c->n_disc = 0;
    } else {
        /* Il pacchetto deve cominciare dove finisce la coda. Se non e' cosi' (buco
         * troppo lungo per l'occultamento) si segna il salto: l'orologio lo applichera'
         * quando la testa ci arriva. */
        double fine = c->n_disc > 0
            ? c->disc[c->n_disc - 1].tempo_us +
                  (double)(c->inseriti_ass - c->disc[c->n_disc - 1].pos) * 1000000.0 / PH_AUDIO_RATE
            : c->tempo_testa_us + (double)c->pieno * 1000000.0 / PH_AUDIO_RATE;
        double d = (double)tempo_us - fine;
        if (d > SOGLIA_TEMPO_US || d < -SOGLIA_TEMPO_US) {
            if (c->n_disc == DISCONTINUITA_MAX)
                c->n_disc--;   /* troppi salti in coda: l'ultimo si sovrascrive */
            c->disc[c->n_disc].pos = c->inseriti_ass;
            c->disc[c->n_disc].tempo_us = (double)tempo_us;
            c->n_disc++;
            /* Dopo un salto del tempo gli scostamenti di prima non valgono piu': senza
             * questo la stima si gonfierebbe fino a 600 ms per 10 secondi. */
            azzera_stima(c);
        }
    }
    stima_aggiorna(c, tempo_us, ora_us);
    accoda(c, frames, n);
    pthread_mutex_unlock(&c->m);
}

void ph_coda_inserisci_occultato(PhCodaAudio *c, const float *frames, size_t n)
{
    pthread_mutex_lock(&c->m);
    accoda(c, frames, n);
    pthread_mutex_unlock(&c->m);
}

void ph_coda_fine(PhCodaAudio *c)
{
    pthread_mutex_lock(&c->m);
    c->fine = true;
    pthread_mutex_unlock(&c->m);
}

void ph_coda_svuota(PhCodaAudio *c)
{
    pthread_mutex_lock(&c->m);
    c->testa = 0;
    c->pieno = 0;
    c->frac = 0;
    c->tempo_noto = false;
    c->in_riproduzione = false;
    c->testa_ass = c->inseriti_ass = 0;
    c->n_disc = 0;
    c->recupero = 0;
    c->ultimo_tempo_us = -1;
    c->ignora = false;
    c->ultimo_valido = false;
    c->min_prima = INT64_MAX;
    c->frame_finestra = 0;
    azzera_stima(c);
    pthread_mutex_unlock(&c->m);
}

/* Passa da un margine all'altro: ogni secondo di audio consegnato il margine
 * scende di un poco verso quello ideale. */
static void scendi(PhCodaAudio *c, size_t n)
{
    c->da_scendere += n;
    while (c->da_scendere >= PH_AUDIO_RATE) {
        c->da_scendere -= PH_AUDIO_RATE;
        if (c->campioni < CAMPIONI_MIN_STIMA)
            continue;
        int ideale = obiettivo_ideale(c);
        if (ideale < c->obiettivo_ms) {
            c->obiettivo_ms -= SCENDI_MS_AL_SECONDO;
            if (c->obiettivo_ms < ideale)
                c->obiettivo_ms = ideale;
        }
    }
}

/* Porta a zero, con rampa lineare, gli ultimi 'd' frame dei 'veri' consegnati. */
static void dissolvi_fine(float *dest, size_t veri, size_t d)
{
    for (size_t j = 0; j < d; j++) {
        float g = (float)(d - j) / (float)d;
        size_t fr = veri - d + j;
        for (int k = 0; k < PH_AUDIO_CANALI; k++)
            dest[fr * PH_AUDIO_CANALI + k] *= g;
    }
}

size_t ph_coda_estrai(PhCodaAudio *c, float *dest, size_t n, int64_t ora_us)
{
    pthread_mutex_lock(&c->m);
    c->ora_estrazione_us = ora_us;
    c->durata_consegna_us = (double)n * 1000000.0 / PH_AUDIO_RATE;
    size_t veri = 0;

    /* Eccesso enorme: si tagliera' alla fine di questo giro, dopo una dissolvenza
     * in uscita (tagliare subito farebbe un clic). */
    bool taglia = c->in_riproduzione &&
        c->pieno > (size_t)(c->obiettivo_ms + SOGLIA_TAGLIO_MS) * FRAME_PER_MS;

    if (!c->in_riproduzione) {
        size_t obiettivo = (size_t)c->obiettivo_ms * FRAME_PER_MS;
        if (c->pieno > 0 && (c->pieno >= obiettivo || c->fine)) {
            c->in_riproduzione = true;
            c->recupero = 0;
            c->dissolvenza_in = DISSOLVENZA;
            c->min_prima = INT64_MAX;
            c->frame_finestra = 0;
            c->ultimo_valido = false;
        }
    }
    if (!c->in_riproduzione) {
        memset(dest, 0, n * PH_AUDIO_CANALI * sizeof(float));
        scendi(c, n);
        pthread_mutex_unlock(&c->m);
        return 0;
    }

    /* Controllo ogni mezzo secondo di quanto c'e' davvero in coda. */
    if ((int64_t)c->pieno < c->min_prima)
        c->min_prima = (int64_t)c->pieno;
    c->frame_finestra += n;
    if (c->frame_finestra >= FINESTRA_CONTROLLO) {
        c->ultimo_min_ms = c->min_prima / FRAME_PER_MS;
        c->ultimo_valido = true;
        c->min_prima = (int64_t)c->pieno;
        c->frame_finestra = 0;
    }
    double rapporto = 1.0;
    if (c->ultimo_valido) {
        int64_t scarto = c->ultimo_min_ms - c->obiettivo_ms;
        /* Isteresi: il recupero parte oltre la soglia larga e continua finche' il
         * margine non e' tornato vicino all'obiettivo (se no si fermerebbe a meta'). */
        if (c->recupero == 0) {
            if (scarto > SOGLIA_RECUPERO_MS)
                c->recupero = 1;
            else if (scarto < -SOGLIA_RECUPERO_MS)
                c->recupero = -1;
        } else if (c->recupero > 0 && scarto <= SOGLIA_FINE_RECUPERO_MS) {
            c->recupero = 0;
        } else if (c->recupero < 0 && scarto >= -SOGLIA_FINE_RECUPERO_MS) {
            c->recupero = 0;
        }
        if (c->recupero > 0)
            rapporto = 1.0 + 0.005 + (double)scarto * 0.0001;
        else if (c->recupero < 0)
            rapporto = 1.0 - 0.005 - (double)(-scarto) * 0.0001;
        if (rapporto > 1.0 + RAPPORTO_MAX)
            rapporto = 1.0 + RAPPORTO_MAX;
        if (rapporto < 1.0 - RAPPORTO_MAX)
            rapporto = 1.0 - RAPPORTO_MAX;
    }

    /* Lettura con interpolazione lineare (rapporto 1 e frac 0 = copia esatta). */
    double p = c->frac;
    size_t i = 0;
    for (; i < n; i++) {
        size_t ip = (size_t)p;
        double f = p - (double)ip;
        if (ip >= c->pieno || (f > 0.0 && ip + 1 >= c->pieno))
            break;
        const float *a = frame_a(c, ip);
        float *o = dest + i * PH_AUDIO_CANALI;
        if (f > 0.0) {
            const float *b = frame_a(c, ip + 1);
            for (int k = 0; k < PH_AUDIO_CANALI; k++)
                o[k] = a[k] + (b[k] - a[k]) * (float)f;
        } else {
            for (int k = 0; k < PH_AUDIO_CANALI; k++)
                o[k] = a[k];
        }
        p += rapporto;
    }
    veri = i;
    size_t consumati = (size_t)p;
    if (consumati > c->pieno)
        consumati = c->pieno;

    if (c->dissolvenza_in > 0) {
        for (size_t j = 0; j < veri && c->dissolvenza_in > 0; j++, c->dissolvenza_in--) {
            float g = (float)(DISSOLVENZA - c->dissolvenza_in) / DISSOLVENZA;
            for (int k = 0; k < PH_AUDIO_CANALI; k++)
                dest[j * PH_AUDIO_CANALI + k] *= g;
        }
    }

    if (i < n) {
        /* Coda vuota: si dissolve l'ultimo pezzetto ed e' sottoalimentazione. */
        size_t d = veri < DISSOLVENZA / 2 ? veri : DISSOLVENZA / 2;
        dissolvi_fine(dest, veri, d);
        memset(dest + veri * PH_AUDIO_CANALI, 0, (n - veri) * PH_AUDIO_CANALI * sizeof(float));
        avanza(c, c->pieno);
        c->frac = 0;
        c->in_riproduzione = false;
        c->recupero = 0;
        c->ultimo_valido = false;
        c->min_prima = INT64_MAX;
        c->frame_finestra = 0;
        if (!c->fine) {
            c->sottoalimentazioni++;
            c->obiettivo_ms += SALITA_DOPO_BUCO_MS;
            if (c->obiettivo_ms > PH_CODA_OBIETTIVO_MAX_MS)
                c->obiettivo_ms = PH_CODA_OBIETTIVO_MAX_MS;
        }
    } else {
        avanza(c, consumati);
        c->frac = p - (double)consumati;
        size_t d = veri < DISSOLVENZA / 2 ? veri : DISSOLVENZA / 2;
        if (taglia) {
            size_t resto = (size_t)(c->obiettivo_ms + RESTO_DOPO_TAGLIO_MS) * FRAME_PER_MS;
            if (c->pieno > resto) {
                dissolvi_fine(dest, veri, d);
                size_t via = c->pieno - resto;
                avanza(c, via);
                c->scartati += via;
                c->frac = 0;
                c->dissolvenza_in = DISSOLVENZA;
                c->recupero = 0;
                c->ultimo_valido = false;
                c->min_prima = INT64_MAX;
                c->frame_finestra = 0;
            }
        } else if (c->pieno == 0) {
            /* La coda si e' vuotata proprio alla fine del giro: il prossimo trovera'
             * silenzio, quindi il suono finisce ora con una dissolvenza. */
            dissolvi_fine(dest, veri, d);
            c->dissolvenza_in = DISSOLVENZA;   /* se un pacchetto arriva prima del giro dopo */
        }
    }
    scendi(c, n);
    pthread_mutex_unlock(&c->m);
    return veri;
}

int64_t ph_coda_tempo(PhCodaAudio *c, int64_t ora_us, int64_t latenza_us)
{
    pthread_mutex_lock(&c->m);
    int64_t r = -1;
    if (c->tempo_noto && c->in_riproduzione) {
        int64_t da = ora_us - c->ora_estrazione_us;
        if (da < 0)
            da = 0;
        if ((double)da > c->durata_consegna_us)
            da = (int64_t)c->durata_consegna_us;   /* non si va oltre l'audio davvero consegnato */
        double t = c->tempo_testa_us - (double)latenza_us + (double)da;
        r = t < 0 ? -1 : (int64_t)t;
        /* Mai indietro: callback in ritardo, recupero che rallenta o latenza che cambia
         * non devono far tornare l'orologio (i salti in avanti per i buchi restano). */
        if (r >= 0 && r < c->ultimo_tempo_us)
            r = c->ultimo_tempo_us;
        if (r >= 0)
            c->ultimo_tempo_us = r;
    }
    pthread_mutex_unlock(&c->m);
    return r;
}

int64_t ph_coda_tempo_testa(PhCodaAudio *c)
{
    pthread_mutex_lock(&c->m);
    int64_t r = c->tempo_noto && c->in_riproduzione ? (int64_t)c->tempo_testa_us : -1;
    pthread_mutex_unlock(&c->m);
    return r;
}

void ph_coda_stat(PhCodaAudio *c, PhCodaStat *s)
{
    pthread_mutex_lock(&c->m);
    s->sottoalimentazioni = c->sottoalimentazioni;
    s->scartati = c->scartati;
    s->margine_ms = (int)(c->pieno / FRAME_PER_MS);
    s->obiettivo_ms = c->obiettivo_ms;
    s->in_riproduzione = c->in_riproduzione;
    pthread_mutex_unlock(&c->m);
}
