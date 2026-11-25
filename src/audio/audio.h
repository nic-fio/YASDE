/* Audio sul PC: riceve dal telefono i pacchetti Opus del canale "audio"
 * (docs/protocollo.md §2.5), li decodifica e li riproduce con PipeWire.
 * Specifica: 4.9 (audio) e 5.8 (volume).
 *
 * Modello di concorrenza (vincolante):
 *  - un PhAudio ha un thread interno che legge il canale, decodifica e riempie
 *    una coda; la riproduzione e' un'altra funzione chiamata da PipeWire
 *    (thread suo). Tutte le funzioni qui sotto si possono chiamare da un thread
 *    qualunque mentre il riproduttore e' in funzione, TRANNE ph_audio_avvia,
 *    ph_audio_ferma e ph_audio_distruggi, che non vanno chiamate insieme a
 *    nessun'altra funzione dello stesso PhAudio e non vanno mai chiamate dentro la funzione di avviso;
 *  - il canale passato a ph_audio_crea diventa del PhAudio: lo chiude lui (anche
 *    se la creazione fallisce);
 *  - il muto NON ferma la lettura del canale: i pacchetti si ricevono e si
 *    decodificano sempre, si abbassa solo il guadagno;
 *  - la persistenza del volume non e' qui: la tiene chi chiama (pezzo config).
 */
#ifndef PH_AUDIO_H
#define PH_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#include "adb/adb.h"
#include "comune/errore.h"

typedef struct PhAudio PhAudio;

/* Motivo dell'avviso. */
#define PH_AUDIO_FINITO 0   /* il telefono ha chiuso il canale (fine normale) */
#define PH_AUDIO_ERRORE (-1) /* canale caduto, dati non validi o uscita audio rotta */

/* Chiamata UNA volta sola, dal thread interno (o da quello di PipeWire), quando
 * il flusso finisce o si rompe. 'testo' e' una frase in italiano, valida solo
 * durante la chiamata. Non e' chiamata se l'arresto l'ha chiesto chi usa il
 * riproduttore con ph_audio_ferma. */
typedef void (*PhAudioAvviso)(void *ctx, int esito, const char *testo);

typedef struct {
    uint64_t ricevuti;          /* pacchetti letti dal canale e accettati */
    uint64_t persi;             /* pacchetti mancanti (salti del campo tempo), occultati */
    uint64_t in_ritardo;        /* pacchetti arrivati fuori ordine o doppi, scartati */
    uint64_t non_validi;        /* pacchetti con dati Opus non decodificabili, scartati */
    uint64_t sottoalimentazioni;/* volte che l'uscita ha trovato la coda vuota */
    uint64_t frame_scartati;    /* frame (non campioni da due canali) buttati per recuperare un ritardo eccessivo */
    int margine_ms;             /* audio pronto in coda adesso */
    int obiettivo_ms;           /* margine che la coda cerca di tenere (adattivo) */
    /* Solo diagnostica (ultimi valori di pw_time letti da PipeWire; -1 se non noti,
     * anche con un'uscita che non li ha): non servono a chi usa il riproduttore. */
    int64_t diag_delay_tick;    /* ritardo del dispositivo, in tick da 1/48000 s */
    int64_t diag_buffered;      /* frame nel convertitore dello stream */
    int64_t diag_queued;        /* frame in coda nello stream */
    int64_t diag_latenza_us;    /* latenza complessiva usata per l'orologio audio */
} PhAudioStat;

/* Prende il canale (gia' aperto, vedi ph_componente_canale(c, "audio", e)) e
 * prepara il riproduttore, ancora fermo. Volume iniziale 100, non muto.
 * NULL in caso di errore (e il canale e' comunque chiuso). 'avviso' puo' essere
 * NULL. */
PhAudio *ph_audio_crea(PhCanale *canale, PhAudioAvviso avviso, void *ctx, PhErrore *e);

/* Apre l'uscita PipeWire (nome dell'app "YASDE", 48 kHz stereo) e fa
 * partire il thread di lettura. 0 / -1. Si puo' chiamare una volta sola. */
int ph_audio_avvia(PhAudio *a, PhErrore *e);

/* Ferma lettura e uscita; il canale resta aperto fino a ph_audio_distruggi.
 * Si puo' chiamare piu' volte e anche senza ph_audio_avvia. */
void ph_audio_ferma(PhAudio *a);

/* Ferma (se serve), chiude il canale e libera tutto. NULL e' ammesso. */
void ph_audio_distruggi(PhAudio *a);

/* Volume 0-100 (fuori scala viene limitato) e muto: guadagno applicato sul PC ai
 * campioni, con curva cubica (v/100)^3 come i cursori di PipeWire/PulseAudio:
 * 0 = silenzio, 100 = guadagno 1. Rampa di 10 ms per non fare "clic". */
void ph_audio_volume(PhAudio *a, int percento);
int ph_audio_volume_leggi(PhAudio *a);
void ph_audio_muto(PhAudio *a, bool muto);
bool ph_audio_muto_leggi(PhAudio *a);

/* Orologio audio per la sincronia col video: tempo del telefono (microsecondi
 * del campo "tempo" di §2.5) del campione che sta uscendo ora dagli altoparlanti,
 * stimato con la latenza dichiarata da PipeWire. -1 se non noto (nessun audio
 * ancora riprodotto, o la coda e' in riempimento dopo un buco). */
int64_t ph_audio_tempo_telefono_us(PhAudio *a);

void ph_audio_statistiche(PhAudio *a, PhAudioStat *s);

#endif
