/* Parti interne del pezzo audio. Ognuna si prova da sola, senza scheda audio:
 *   pacchetti.c  lettura dei pacchetti dal canale (formato §2.5)
 *   decodifica.c Opus 48 kHz stereo con occultamento dei pacchetti mancanti
 *   flusso.c     decodifica + riconoscimento dei pacchetti persi/in ritardo
 *   coda.c       coda con margine adattivo e recupero del ritardo
 *   uscita*.c    uscita audio dietro una piccola interfaccia (PipeWire o finta)
 *   audio.c      il riproduttore che mette insieme le parti */
#ifndef PH_AUDIO_INTERNO_H
#define PH_AUDIO_INTERNO_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio/audio.h"

#define PH_AUDIO_RATE 48000
#define PH_AUDIO_CANALI 2
#define PH_AUDIO_FRAME_MAX 5760        /* 120 ms: il massimo di un pacchetto Opus */
#define PH_AUDIO_PACCHETTO_MAX 4000    /* oltre: il canale e' rotto */
#define PH_AUDIO_INTESTAZIONE 10       /* tempo (8) + lunghezza (2) */

/* ---- 1. Lettura dei pacchetti -------------------------------------------- */
typedef struct {
    uint8_t buf[PH_AUDIO_INTESTAZIONE + PH_AUDIO_PACCHETTO_MAX];
    size_t ha;          /* byte gia' arrivati del pacchetto in corso */
    bool consegnato;    /* l'ultimo pacchetto e' stato dato a chi chiama */
} PhLettore;

typedef struct {
    int64_t tempo_us;
    const uint8_t *dati;   /* valido fino alla chiamata successiva */
    size_t lunghezza;
} PhPacchetto;

#define PH_LETTURA_ERRORE (-1)
#define PH_LETTURA_TEMPO 0      /* nessun pacchetto completo entro il timeout */
#define PH_LETTURA_PACCHETTO 1
#define PH_LETTURA_CHIUSO 2     /* chiuso a un confine di pacchetto */

void ph_lettore_inizia(PhLettore *l);
/* Legge dal canale (ph_canale_leggi) al massimo i byte che mancano al pacchetto
 * in corso: non va mai oltre i propri buffer. Se il timeout scade a meta' di un
 * pacchetto lo stato resta e la chiamata dopo continua. */
int ph_lettore_leggi(PhLettore *l, PhCanale *c, int timeout_ms, PhPacchetto *p, PhErrore *e);

/* ---- 2. Decodifica Opus -------------------------------------------------- */
typedef struct PhDecoder PhDecoder;
PhDecoder *ph_decoder_crea(PhErrore *e);
void ph_decoder_libera(PhDecoder *d);
void ph_decoder_azzera(PhDecoder *d);
/* Frame decodificati (per canale) in 'out' (almeno PH_AUDIO_FRAME_MAX*2 float
 * intercalati) o -1 se il pacchetto non e' Opus valido. */
int ph_decoder_decodifica(PhDecoder *d, const uint8_t *dati, size_t n, float *out, PhErrore *e);
/* Occulta un pacchetto mancante di 'frame' frame (multiplo di 120). */
int ph_decoder_occulta(PhDecoder *d, int frame, float *out, PhErrore *e);

/* ---- 3. Coda con margine adattivo ---------------------------------------- */
typedef struct PhCodaAudio PhCodaAudio;

typedef struct {
    uint64_t sottoalimentazioni;
    uint64_t scartati;          /* frame buttati */
    int margine_ms;
    int obiettivo_ms;
    bool in_riproduzione;
} PhCodaStat;

#define PH_CODA_OBIETTIVO_INIZIALE_MS 60
#define PH_CODA_OBIETTIVO_MIN_MS 40
#define PH_CODA_OBIETTIVO_MAX_MS 600

PhCodaAudio *ph_coda_crea(void);
void ph_coda_libera(PhCodaAudio *c);
/* Tutto cio' che tocca la coda prende il suo mutex; i tempi (ora_us) li passa
 * chi chiama (orologio monotono), cosi' i test girano in tempo finto. */
void ph_coda_inserisci(PhCodaAudio *c, const float *frames, size_t n, int64_t tempo_us, int64_t ora_us);
void ph_coda_inserisci_occultato(PhCodaAudio *c, const float *frames, size_t n);
/* Riempie sempre 'n' frame di 'dest' (silenzio se manca audio). Torna quelli veri. */
size_t ph_coda_estrai(PhCodaAudio *c, float *dest, size_t n, int64_t ora_us);
/* Il flusso e' finito: si suona quel che resta senza aspettare il margine. */
void ph_coda_fine(PhCodaAudio *c);
void ph_coda_svuota(PhCodaAudio *c);
int64_t ph_coda_tempo(PhCodaAudio *c, int64_t ora_us, int64_t latenza_us);
/* Tempo del telefono del prossimo frame da consegnare (-1 se non noto): per i test. */
int64_t ph_coda_tempo_testa(PhCodaAudio *c);
void ph_coda_stat(PhCodaAudio *c, PhCodaStat *s);

int64_t ph_audio_ora_us(void);   /* orologio monotono */

/* ---- 3b. Flusso: decodifica e riconoscimento dei buchi -------------------- */
typedef struct PhFlusso PhFlusso;
PhFlusso *ph_flusso_crea(PhCodaAudio *coda, PhErrore *e);
void ph_flusso_libera(PhFlusso *f);
/* 0 se il pacchetto e' stato gestito (anche se scartato), -1 se il flusso e'
 * irrecuperabile (troppi pacchetti non validi di fila). */
int ph_flusso_pacchetto(PhFlusso *f, int64_t tempo_us, const uint8_t *dati, size_t n,
                        int64_t ora_us, PhErrore *e);
void ph_flusso_contatori(PhFlusso *f, PhAudioStat *s);

/* ---- 4. Uscita ------------------------------------------------------------ */
typedef struct {
    /* Riempie esattamente 'frame' frame float stereo intercalati. Puo' essere
     * chiamata da un thread in tempo reale: niente attese lunghe. */
    void (*riempi)(void *ctx, float *dest, size_t frame);
    /* Errore dell'uscita dopo l'avvio (esempio: PipeWire si e' fermato). */
    void (*errore)(void *ctx, const char *testo);
    void *ctx;
} PhUscitaCb;

typedef struct PhUscita PhUscita;
struct PhUscita {
    int (*avvia)(PhUscita *u, PhErrore *e);
    void (*ferma)(PhUscita *u);
    /* Microsecondi tra la consegna dell'ultimo frame e il momento in cui suona
     * (per PipeWire: delay + buffered + queued/stride), -1 se non noto. */
    int64_t (*latenza_us)(PhUscita *u);
    void (*libera)(PhUscita *u);
    /* Facoltativa (puo' essere NULL): gli ultimi valori letti dall'uscita, solo
     * per la diagnosi. -1 se non noti. */
    void (*diagnosi)(PhUscita *u, int64_t *delay, int64_t *buffered, int64_t *queued);
};
typedef PhUscita *(*PhUscitaFabbrica)(const PhUscitaCb *cb, PhErrore *e);

PhUscita *ph_uscita_pipewire(const PhUscitaCb *cb, PhErrore *e);

/* Come ph_audio_crea ma con un'uscita a scelta (i test usano una finta). */
PhAudio *ph_audio_crea_con_uscita(PhCanale *canale, PhAudioAvviso avviso, void *ctx,
                                  PhUscitaFabbrica fabbrica, PhErrore *e);

#endif
