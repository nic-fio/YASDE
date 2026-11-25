/* Telefono finto per provare il componente senza telefono e senza il vero ADB.
 * Sostituisce ph_canale_*, ph_shell_esegui e ph_file_invia: ogni canale e' una
 * coppia di tubi in memoria e dietro ogni canale aperto c'e' un thread che si
 * comporta come il servizio Java (docs/protocollo.md). Registra i messaggi
 * ricevuti (comandi e input) per poterli confrontare byte per byte. */
#ifndef PH_FINTO_H
#define PH_FINTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "componente/componente.h"

typedef enum {
    VM_NORMALE = 0,
    VM_ENORME,      /* lunghezza dichiarata oltre 8 MiB */
    VM_TRONCATO,    /* dati a meta' poi il canale si chiude */
    VM_GENERE,      /* genere sconosciuto */
    VM_LUNGO_FALSO, /* dichiara 100000 byte, ne manda 10, poi tace; dopo 400 ms chiude */
    VM_LENTO,       /* pacchetti validi ma a pezzettini con pause */
    VM_GRANDE       /* un fotogramma da 1,5 MiB: byte (j*31 + (j>>8)) */
} FintoVideoModo;

typedef struct {
    bool niente_pronto;          /* il servizio parte ma non dice mai PRONTO */
    bool shell_muore;            /* scrive un errore e chiude prima di PRONTO */
    bool copia_fallisce;         /* ph_file_invia fallisce */
    uint16_t versione;           /* versione nel saluto (predefinito 1) */
    bool saluto_rifiuta;         /* risponde ERRORE al saluto */
    bool jar_vecchio;            /* ai tipi 0x40-0x4f risponde "tipo sconosciuto" */
    bool chiudi_resta_vivo;      /* risponde OK a CHIUDI ma non chiude nessun canale */
    bool silenzio;               /* non risponde piu' a nessuna richiesta */
    bool evento_prima_di_ok;     /* manda un VIDEO_EVENTO prima di ogni OK */
    int risposta_rotta;          /* 1 = risposta troncata e canale chiuso, 2 = lunghezza enorme */
    int pacchetti;               /* fotogrammi per sessione video, compresa la configurazione (predefinito 6) */
    FintoVideoModo video_modo;
    bool video_continuo;         /* dopo i pacchetti continua a mandare un fotogramma ogni pochi ms (si ferma in pausa) */
    bool presenza_errore;        /* PRESENZA -> ERRORE "tipo sconosciuto" (jar vecchio) */
    int ritardo_chiudi_ms;       /* la risposta a CHIUDI arriva con questo ritardo */
    bool pausa_rifiuta;          /* VIDEO_PAUSA/RIPRENDI -> ERRORE "pausa non riuscita" */
    bool video_resta_aperto;     /* dopo i pacchetti non chiude il canale */
} FintoConfig;

PhAdb *finto_crea(void);
FintoConfig *finto_cfg(PhAdb *a); /* si cambia solo a servizio fermo (tra una richiesta e l'altra) */
/* Spegne tutto e libera: il componente va chiuso prima. */
void finto_distruggi(PhAdb *a);
/* Connessione che cade di colpo: letture e scritture in corso tornano errore. */
void finto_cade(PhAdb *a);

/* Manda byte grezzi sul canale comandi (un evento, o spazzatura). */
bool finto_manda_comandi(PhAdb *a, const uint8_t *dati, size_t n);

int finto_conta_comandi(PhAdb *a, uint8_t tipo); /* richieste ricevute di quel tipo */

/* Cancello: le richieste di questo tipo vengono registrate ma restano senza
 * risposta finche' il test non apre il cancello. tipo 0 = nessuna. */
void finto_trattieni(PhAdb *a, uint8_t tipo);
void finto_apri_cancello(PhAdb *a);
/* Aspetta che una richiesta sia ferma al cancello. */
bool finto_attendi_trattenuta(PhAdb *a, int timeout_ms);

/* Messaggi ricevuti. Gli indici partono da 0. */
size_t finto_numero_input(PhAdb *a);
bool finto_attendi_input(PhAdb *a, size_t n, int timeout_ms);
size_t finto_input(PhAdb *a, size_t i, uint8_t *tipo, uint8_t *corpo, size_t capacita);
size_t finto_numero_comandi(PhAdb *a);
size_t finto_comando(PhAdb *a, size_t i, uint8_t *tipo, uint8_t *corpo, size_t capacita);

/* Cosa e' successo. */
const char *finto_jar_locale(PhAdb *a);
const char *finto_jar_remoto(PhAdb *a);
const char *finto_servizio_lanciato(PhAdb *a);
const char *finto_comando_pulizia(PhAdb *a); /* ultimo comando passato a ph_shell_esegui */
bool finto_ricevuto_chiudi(PhAdb *a);
int finto_video_attivi(PhAdb *a);
int finto_canali_aperti(PhAdb *a, const char *tipo); /* quante volte e' stato aperto "comandi", "input", "video:" ... */

/* Contenuto del pacchetto video numero i (i = -1: configurazione). Torna la lunghezza. */
size_t finto_dati_pacchetto(int i, uint8_t *uscita, size_t capacita);
uint64_t finto_tempo_pacchetto(int i);

#endif
