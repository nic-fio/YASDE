/* Lato PC del protocollo con il componente sul telefono (docs/protocollo.md):
 * avvio del servizio, canale comandi, sessioni video (ricezione dei pacchetti
 * H.264), invio dell'input. Usa solo src/adb/adb.h (canali e shell) e
 * src/file/file.h (copia del jar). Non decodifica il video: consegna i
 * pacchetti a chi sta sopra.
 *
 * Concorrenza: un thread interno legge il canale comandi e separa risposte ed
 * eventi. Le richieste (ph_video_apri, ...) si possono chiamare da thread
 * diversi: sono serializzate. Ogni PhVideoFlusso si legge da un solo thread.
 * Le funzioni ph_input_* si possono chiamare da un thread qualunque. */
#ifndef PH_COMPONENTE_H
#define PH_COMPONENTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adb/adb.h"
#include "comune/buffer.h"
#include "comune/errore.h"

#define PH_PROTOCOLLO_VERSIONE 2

/* ---- Codifica dei messaggi (parte pura, provata con i byte attesi) -------*/
enum {
    PH_MSG_SALUTO = 0x01,
    PH_MSG_OK = 0x02,
    PH_MSG_ERRORE = 0x03,
    PH_MSG_CHIUDI = 0x04,
    PH_MSG_VIDEO_APRI = 0x40,
    PH_MSG_VIDEO_CHIUDI = 0x41,
    PH_MSG_VIDEO_RIDIMENSIONA = 0x42,
    PH_MSG_VIDEO_CHIAVE = 0x43,
    PH_MSG_VIDEO_PAUSA = 0x44,
    PH_MSG_VIDEO_RIPRENDI = 0x45,
    PH_MSG_VIDEO_PAGINA = 0x46, /* versione 2 */
    PH_MSG_VIDEO_EVENTO = 0x48,
    PH_MSG_TOCCO = 0x50,
    PH_MSG_ROTELLA = 0x51,
    PH_MSG_TASTO = 0x52,
    PH_MSG_TESTO = 0x53,
    PH_MSG_INDIETRO = 0x54,
    PH_MSG_PIZZICO = 0x55, /* aggiunto il 23 nov: Ctrl + rotella = zoom (docs/protocollo.md §2.4) */
};

/* Scrittura di un messaggio dentro un PhBuffer: ph_msg_inizia mette tipo e
 * lunghezza provvisoria, le ph_msg_* aggiungono i campi, ph_msg_finisci
 * corregge la lunghezza. Tutte tornano 0 o -1 (memoria finita / stringa
 * troppo lunga). */
int ph_msg_inizia(PhBuffer *b, uint8_t tipo);
int ph_msg_u8(PhBuffer *b, uint8_t v);
int ph_msg_u16(PhBuffer *b, uint16_t v);
int ph_msg_i16(PhBuffer *b, int16_t v);
int ph_msg_u32(PhBuffer *b, uint32_t v);
int ph_msg_stringa(PhBuffer *b, const char *s);
int ph_msg_finisci(PhBuffer *b);

/* Lettura del corpo di un messaggio: un cursore che non esce mai dal buffer.
 * Dopo un errore (corpo troppo corto) rotto resta true e le letture danno 0. */
typedef struct {
    const uint8_t *dati;
    size_t lunghezza;
    size_t posizione;
    bool rotto;
} PhLettore;
uint8_t ph_leggi_u8(PhLettore *l);
uint16_t ph_leggi_u16(PhLettore *l);
uint32_t ph_leggi_u32(PhLettore *l);
uint64_t ph_leggi_u64(PhLettore *l);
/* Copia la stringa in uscita (sempre terminata, troncata se serve). */
void ph_leggi_stringa(PhLettore *l, char *uscita, size_t capacita);

/* ---- Componente ----------------------------------------------------------*/
typedef struct PhComponente PhComponente;

typedef struct {
    uint16_t api;       /* livello API di Android */
    uint16_t larghezza; /* schermo reale */
    uint16_t altezza;
    uint16_t densita;   /* dpi */
    char modello[64];
} PhTelefonoInfo;

/* Eventi che arrivano dal telefono. Chiamata dal thread interno: deve
 * tornare in fretta e non chiamare le funzioni di richiesta. */
typedef void (*PhEvento)(uint32_t id_video, uint8_t che_cosa, uint16_t a, uint16_t b, void *utente);

/* Copia il jar sul telefono, lancia il servizio, aspetta "PRONTO", apre i
 * canali comandi e input, fa il saluto. */
PhComponente *ph_componente_avvia(PhAdb *a, const char *jar_locale, PhEvento evento,
                                  void *utente, PhTelefonoInfo *info, int timeout_ms,
                                  PhErrore *e);
/* Manda CHIUDI, chiude i canali e libera tutto (anche i flussi ancora aperti
 * diventano inutilizzabili; vanno comunque passati a ph_video_chiudi). */
void ph_componente_chiudi(PhComponente *c);
/* false quando il servizio e' morto o la connessione e' caduta. */
bool ph_componente_vivo(PhComponente *c);

/* ---- Per gli altri pezzi (audio, dati, ricerca) ---------------------------
 * Richiesta generica sul canale comandi: manda tipo+corpo, aspetta l'OK e ne
 * mette il corpo in risposta (facoltativa). ERRORE del telefono -> -1 con la
 * sua frase. Serializzata con le altre richieste; stessi tempi massimi. */
int ph_componente_richiesta(PhComponente *c, uint8_t tipo, const uint8_t *corpo, size_t n,
                            PhBuffer *risposta, PhErrore *e);
/* Apre un canale del protocollo (docs/protocollo.md §1): riga e' il tipo, per
 * esempio "audio" o "cerca", senza l'a capo. Il canale e' di chi chiama
 * (ph_canale_leggi/scrivi/chiudi di adb.h) e va chiuso prima di
 * ph_componente_chiudi. Le righe "comandi", "input" e "video:..." sono
 * del componente e vengono rifiutate. */
PhCanale *ph_componente_canale(PhComponente *c, const char *riga, PhErrore *e);
/* Chi vuole gli eventi di un intervallo di tipi (es. 0x7e-0x7f) registra una
 * funzione: chiamata dal thread interno con il corpo dell'evento, deve tornare
 * in fretta e non chiamare le funzioni di richiesta. Al massimo 8 ascoltatori;
 * registrarne uno per un intervallo gia' preso lo sostituisce; funzione NULL
 * lo toglie. Gli eventi video (0x48) restano alla PhEvento data all'avvio.
 * Un intervallo che ne tocca un altro senza coincidere e' un errore.
 * ph_componente_ascolta aspetta la fine di una chiamata in corso
 * all'ascoltatore che sta sostituendo o togliendo. Da dentro un ascoltatore
 * si puo' solo togliere se' stesso: mai chiamare ph_componente_chiudi, ne'
 * aspettare un altro thread che sta chiamando ph_componente_ascolta. */
typedef void (*PhEventoGrezzo)(uint8_t tipo, const uint8_t *corpo, size_t n, void *utente);
int ph_componente_ascolta(PhComponente *c, uint8_t da_tipo, uint8_t a_tipo, PhEventoGrezzo f,
                          void *utente, PhErrore *e);

/* ---- Video ---------------------------------------------------------------*/
typedef struct PhVideoFlusso PhVideoFlusso;

/* Pagine di un'app che il PC puo' chiedere (docs/protocollo.md §2.2). Elenco
 * fisso: il PC non manda mai un intent libero, il telefono traduce il numero
 * in un'azione standard di Android. */
enum PhVideoPagina {
    PH_VIDEO_PAGINA_NESSUNA = 0,  /* avvio normale */
    PH_VIDEO_PAGINA_BATTERIA = 1, /* POWER_USAGE_SUMMARY */
    PH_VIDEO_PAGINA_MEMORIA = 2   /* INTERNAL_STORAGE_SETTINGS */
};

typedef struct {
    uint16_t larghezza, altezza; /* multipli di 8, 64..2560 */
    uint16_t densita;
    uint32_t bit_al_secondo;
    uint8_t fps;
    const char *pacchetto;       /* NULL o "" = specchio dello schermo reale */
    uint8_t pagina;              /* PhVideoPagina; 0 = avvio normale. Fuori elenco:
                                  * ph_video_apri torna NULL senza mandare nulla */
} PhVideoRichiesta;

typedef struct {
    bool configurazione;  /* true = SPS/PPS, false = fotogramma */
    bool chiave;
    uint16_t larghezza, altezza;
    uint64_t tempo_us;
    const uint8_t *dati;  /* H.264 Annex B; valido fino alla lettura successiva */
    size_t lunghezza;
} PhVideoPacchetto;

/* Apre la sessione (VIDEO_APRI) e il canale video:<id>. L'id lo assegna il
 * componente (contatore crescente) e si legge con ph_video_id. */
PhVideoFlusso *ph_video_apri(PhComponente *c, const PhVideoRichiesta *r, PhErrore *e);
uint32_t ph_video_id(const PhVideoFlusso *f);
/* 1 = pacchetto letto, 0 = flusso finito, PH_CANALE_ERRORE, PH_CANALE_TEMPO. */
int ph_video_leggi(PhVideoFlusso *f, PhVideoPacchetto *p, int timeout_ms, PhErrore *e);
int ph_video_ridimensiona(PhVideoFlusso *f, uint16_t larghezza, uint16_t altezza,
                          uint16_t densita, uint16_t *larghezza_vera, uint16_t *altezza_vera,
                          PhErrore *e);
int ph_video_chiave(PhVideoFlusso *f, PhErrore *e);
/* Pausa e ripresa (docs/protocollo.md §2.2, VIDEO_PAUSA / VIDEO_RIPRENDI):
 * finestra minimizzata = flusso in pausa, l'app resta viva sul telefono.
 * In pausa ph_video_leggi torna PH_CANALE_TEMPO (non arrivano pacchetti);
 * dopo la ripresa, passati i pacchetti che erano gia' in viaggio prima della
 * pausa, il primo fotogramma e' un fotogramma chiave. Chiamarle due volte di
 * fila non e' un errore. Per ridimensionare un flusso in pausa prima lo si
 * riprende. Con ph_video_chiudi su un altro thread: le chiamate di pausa,
 * ripresa, chiave e ridimensiona gia' in corso finiscono prima che il flusso
 * sia liberato (e VIDEO_CHIUDI parte dopo di loro); quelle che arrivano mentre
 * la chiusura e' in corso tornano -1. Chi chiama deve garantire che nessuna
 * parta dopo che ph_video_chiudi puo' essere tornata: il flusso non esiste
 * piu'. */
int ph_video_pausa(PhVideoFlusso *f, PhErrore *e);
int ph_video_riprendi(PhVideoFlusso *f, PhErrore *e);
/* Avvia una pagina (PhVideoPagina, mai 0) dell'app della sessione sul suo
 * schermo virtuale (VIDEO_PAGINA, protocollo §2.2). 0 = OK del telefono;
 * altrimenti -1 con PhErrore. In caso d'errore non e' stato toccato nulla: il
 * flusso e la sessione restano com'erano (pagina 0 o fuori elenco: l'errore
 * viene prima di mandare qualunque cosa; sessione ignota, specchio o nessuna
 * attivita' che risponde: lo dice il telefono). Attesa col limite delle altre
 * richieste. Regge ph_video_chiudi su un altro thread come pausa/ripresa:
 * o finisce prima della chiusura, o torna -1. */
int ph_video_pagina(PhVideoFlusso *f, uint8_t pagina, PhErrore *e);
/* Manda VIDEO_CHIUDI, chiude il canale, libera. Sblocca una lettura in corso. */
void ph_video_chiudi(PhVideoFlusso *f, bool togli_task);

/* ---- Input (nessuna risposta dal telefono) --------------------------------
 * larghezza/altezza = misure del video su cui sono calcolate x e y.
 * Concorrenza: il lock del canale input resta preso durante la scrittura (adb
 * vieta due scrittori sullo stesso canale e un messaggio non deve mescolarsi
 * con un altro): e' l'eccezione nota alla regola «nessun lock durante una
 * scrittura in rete». */
enum { PH_TOCCO_GIU = 0, PH_TOCCO_MUOVI = 1, PH_TOCCO_SU = 2, PH_TOCCO_ANNULLA = 3 };
int ph_input_tocco(PhComponente *c, uint32_t id, uint16_t larghezza, uint16_t altezza,
                   uint8_t azione, uint16_t x, uint16_t y, uint8_t pulsanti, PhErrore *e);
int ph_input_rotella(PhComponente *c, uint32_t id, uint16_t larghezza, uint16_t altezza,
                     uint16_t x, uint16_t y, int16_t orizzontale, int16_t verticale, PhErrore *e);
int ph_input_tasto(PhComponente *c, uint32_t id, bool giu, uint32_t codice, uint32_t meta,
                   PhErrore *e);
int ph_input_testo(PhComponente *c, uint32_t id, const char *testo, PhErrore *e);
int ph_input_indietro(PhComponente *c, uint32_t id, PhErrore *e);
/* PIZZICO (0x55, docs/protocollo.md §2.4): gesto a due dita generato dal
 * telefono attorno a (x, y) nella misura del video larghezza x altezza.
 * fase PH_PIZZICO_*; passo in 120esimi (positivo = ingrandisci), limitato a
 * -1200..1200. Argomenti non validi (fase sconosciuta, x >= larghezza, y >=
 * altezza, misure 0): -1 senza mandare nulla. Il gesto lo chiude comunque il
 * telefono dopo 400 ms senza passi. */
enum { PH_PIZZICO_INIZIO = 0, PH_PIZZICO_CONTINUA = 1, PH_PIZZICO_FINE = 2, PH_PIZZICO_ANNULLA = 3 };
int ph_input_pizzico(PhComponente *c, uint32_t id, uint16_t larghezza, uint16_t altezza, uint8_t fase,
                     uint16_t x, uint16_t y, int16_t passo, PhErrore *e);

/* ---- Presenza (aggiunta in coda, 23 nov sera; protocollo §2.1) -----------
 * Il telefono resta sveglio solo finche' sente il PC vivo (30 minuti senza
 * messaggi sul canale comandi = PC assente). Il componente, da solo, dal
 * saluto alla chiusura: se da PH_PRESENZA_INTERVALLO_S secondi non e' partita
 * nessuna richiesta sul canale comandi, manda PRESENZA (tipo 0x05, corpo
 * vuoto) come una richiesta qualunque (una alla volta, con il limite solito,
 * OK atteso). Su un thread suo (o un timer del thread interno) che aspetta
 * con una scadenza su una condizione, svegliato da ph_componente_chiudi:
 * nessun lucchetto tenuto durante la scrittura in rete, nessuna richiesta
 * degli altri pezzi trattenuta piu' del tempo di una richiesta. Un fallimento
 * non e' un errore per nessuno (una riga generica su stderr; si riprova alla
 * scadenza dopo). Dopo il ritorno di ph_componente_chiudi nessuna PRESENZA e'
 * in corso. Nessuna funzione pubblica nuova. */
#define PH_MSG_PRESENZA 0x05
#define PH_PRESENZA_INTERVALLO_S 300
#define PH_PC_ASSENTE_S 1800 /* lato telefono, per memoria: 30 minuti */

#endif
