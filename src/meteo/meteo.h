/* Previsioni del tempo da Open-Meteo (decisione del committente del 23 nov
 * sera, PIANO.md): pezzo "meteo" del PC. CONTRATTO scritto da chi coordina:
 * chi scrive il pezzo lo realizza (COMPITO.md) e non lo modifica.
 *
 * E' il SOLO punto in cui YASDE si collega a Internet. Regole:
 *  - si manda SOLO: le coordinate arrotondate a 0,005 gradi (circa 500 m,
 *    gia' arrotondate dal telefono; quelle della citta' scelta a mano sono
 *    al centesimo) e i parametri fissi della richiesta (PH_METEO_*). Nessun
 *    identificativo, nessun cookie, User-Agent generico;
 *  - HTTPS con OpenSSL (gia' una dipendenza del progetto per ADB): certificato
 *    verificato (catena + nome dell'host), SNI; nessuna nuova libreria;
 *  - posizione mai nei log ne' su disco, nemmeno nei messaggi d'errore;
 *  - ogni attesa ha un limite (risoluzione del nome, collegamento, TLS,
 *    risposta: PH_METEO_LIMITE_S in tutto); annullabile;
 *  - dati dalla rete trattati come ostili: risposta al massimo
 *    PH_METEO_RISPOSTA_MAX byte, JSON letto da un analizzatore nostro che non
 *    legge ne' scrive fuori dai buffer; valori assenti = non noti;
 *  - frequenza: al massimo una richiesta riuscita ogni PH_METEO_INTERVALLO_S
 *    per la stessa posizione (risposta dalla cache in memoria), dopo un
 *    fallimento si riprova dopo 5, 15, 30 minuti; nessuna raffica.
 * Attribuzione obbligatoria (dati CC BY 4.0): chi mostra i dati mostra
 * PH_METEO_ATTRIBUZIONE con il collegamento PH_METEO_SITO.
 * Ricerca dei luoghi per nome (decisione del committente del 23 nov notte,
 * in fondo al file): stesso servizio, stesse regole di tempi, dimensioni,
 * certificati e riservatezza; parte SOLO per un'azione esplicita
 * dell'utente (pulsante o Invio, mai a ogni tasto).
 * Riserva futura annotata, NON da realizzare ora: MET Norway
 * (api.met.no/weatherapi/locationforecast/2.0/compact, richiede un
 * User-Agent con un contatto). */
#ifndef PH_METEO_H
#define PH_METEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comune/errore.h"

#define PH_METEO_HOST "api.open-meteo.com"
#define PH_METEO_PORTA 443
#define PH_METEO_PERCORSO "/v1/forecast"
#define PH_METEO_SITO "https://open-meteo.com/"
#define PH_METEO_ATTRIBUZIONE "Dati meteo di Open-Meteo.com"
#define PH_METEO_USER_AGENT "YASDE/2.2"
#define PH_METEO_LIMITE_S 15
#define PH_METEO_INTERVALLO_S (30 * 60)
#define PH_METEO_RISPOSTA_MAX (512u * 1024u)
#define PH_METEO_ORE_MAX 48
#define PH_METEO_GIORNI_MAX 7

/* Valori non noti: temperature e altri numeri = NAN (isnan); codici = -1. */
typedef struct {
    int64_t ora;            /* secondi Unix (UTC) */
    double temperatura;     /* gradi C */
    double percepita;       /* gradi C */
    double umidita;         /* % */
    double precipitazione;  /* mm */
    double vento;           /* km/h */
    int codice;             /* codice WMO (0-99) o -1 */
    int giorno;             /* 1 di giorno, 0 di notte, -1 non noto */
} PhMeteoAdesso;

typedef struct {
    int64_t ora;            /* inizio dell'ora, secondi Unix */
    double temperatura;
    double prob_pioggia;    /* % */
    int codice;
    int giorno;
} PhMeteoOra;

typedef struct {
    int64_t giorno;         /* mezzanotte LOCALE del luogo, in secondi Unix */
    double minima, massima;
    double prob_pioggia;    /* % massima del giorno */
    int codice;
    int64_t alba, tramonto; /* secondi Unix, 0 se non noti */
} PhMeteoGiorno;

typedef struct {
    PhMeteoAdesso adesso;
    PhMeteoOra ore[PH_METEO_ORE_MAX];      /* da adesso in avanti */
    size_t n_ore;
    PhMeteoGiorno giorni[PH_METEO_GIORNI_MAX]; /* da oggi */
    size_t n_giorni;
    int scarto_utc_s;       /* scarto del fuso del luogo (utc_offset_seconds) */
    int64_t ricevuta;       /* secondi Unix della risposta (per «ultimo dato alle ...») */
} PhMeteoPrevisione;

/* ---- Parti pure (provabili senza rete) ---------------------------------- */
/* Percorso + query per le coordinate in MILLESIMI di grado (23 nov notte:
 * prima centesimi), nei limiti -90000..90000 e -180000..180000 e multipli di
 * 5 (altrimenti -1: piu' precisione di 0,005 gradi non deve uscire), scritte
 * con 3 decimali: 41895 -> "41.895", -5 -> "-0.005". I numeri si
 * scrivono SENZA dipendere dalla lingua del sistema (sempre il punto
 * decimale: niente printf con %f). Parametri: current (temperature_2m,
 * apparent_temperature, relative_humidity_2m, precipitation, weather_code,
 * is_day, wind_speed_10m), hourly (temperature_2m, weather_code, is_day,
 * precipitation_probability), daily (weather_code, temperature_2m_max,
 * temperature_2m_min, precipitation_probability_max, sunrise, sunset),
 * timezone=auto, timeformat=unixtime, forecast_days=7. Nient'altro. 0 / -1. */
int ph_meteo_percorso(int32_t lat_millesimi, int32_t lon_millesimi, char *out, size_t capacita, PhErrore *e);
/* Analizza la risposta JSON (al massimo PH_METEO_RISPOSTA_MAX byte, profondita'
 * massima 16, stringhe e numeri controllati) in *out; `adesso` (secondi Unix)
 * per tenere solo le ore da adesso in avanti. Un campo mancante o di tipo
 * sbagliato = non noto (non e' un errore); JSON non valido, troppo grande o
 * senza nessuna sezione utile = -1 con *out azzerato. */
int ph_meteo_analizza(const char *json, size_t n, int64_t adesso, PhMeteoPrevisione *out, PhErrore *e);

/* ---- Richieste ------------------------------------------------------------ */
typedef struct PhMeteo PhMeteo;

/* p NULL = errore con frase (per l'utente, senza coordinate); valido solo
 * durante la chiamata. Chiamata dal thread del pezzo: tornare in fretta; da
 * dentro si puo' chiamare solo ph_meteo_chiedi. NON si possono chiamare
 * ph_meteo_annulla ne' ph_meteo_chiudi: annulla torna subito senza aspettare
 * (ma e' sbagliato usarla li'), chiudi e' RIFIUTATA (non fa nulla, l'oggetto
 * resta vivo). */
typedef void (*PhMeteoFatto)(const PhMeteoPrevisione *p, const char *frase, void *utente);

/* Crea il pezzo e il suo thread (nessun collegamento finche' non si chiede). */
PhMeteo *ph_meteo_nuovo(PhErrore *e);
/* Chiede le previsioni (non blocca). IL PEZZO NON RITENTA DA SOLO: chi lo
 * usa richiama ph_meteo_chiedi periodicamente (ogni 5 minuti finche' un widget
 * Meteo e' visibile); sono le regole sotto a decidere se serve un collegamento.
 *  - Se c'e' una risposta valida in cache per la stessa posizione piu' giovane
 *    di PH_METEO_INTERVALLO_S, risponde con quella (la cache tiene le ultime 4
 *    posizioni; la risposta arriva sempre dal thread del pezzo, mai dentro
 *    questa chiamata).
 *  - Ogni collegamento AVVIATO conta come tentativo per quella posizione,
 *    anche se poi viene interrotto (annulla, nuova chiedi per un'altra
 *    posizione): dopo il primo tentativo senza successo non ne parte un altro
 *    per la stessa posizione prima di 5 minuti, poi 15, poi 30; nel frattempo
 *    chi chiede riceve subito l'errore precedente, senza collegarsi. Un
 *    successo azzera il conto. Una risposta riuscita arrivata dopo annulla o
 *    dopo un cambio di posizione va comunque in cache.
 *  - Fra l'avvio di due collegamenti (di qualunque posizione) passano almeno
 *    PH_METEO_AVVII_MIN_MS millisecondi (l'attesa non e' un errore).
 *  - Se una richiesta e' in volo, la nuova sostituisce il destinatario della
 *    risposta; se la posizione e' diversa la richiesta in volo si interrompe e
 *    CONTA COME TENTATIVO. Il destinatario sostituito non riceve nulla.
 * 0 / -1 (argomenti non validi, oggetto chiuso). */
/* Coordinate in MILLESIMI (23 nov notte), stesse regole di ph_meteo_percorso
 * (fuori limite o non multiple di 5: -1); "stessa posizione" (cache,
 * tentativi, richiesta in volo) = stessi millesimi. Una citta' scelta a mano
 * (PhMeteoLuogo, in centesimi) si passa moltiplicata per 10. */
int ph_meteo_chiedi(PhMeteo *m, int32_t lat_millesimi, int32_t lon_millesimi, PhMeteoFatto fatto, void *utente,
                    PhErrore *e);
/* Dopo il ritorno nessuna funzione di ritorno arriva piu' (una in corso si
 * aspetta); la richiesta in volo si interrompe entro 1 s. L'oggetto resta
 * usabile. Qualunque thread FUORI dalle funzioni di ritorno (dentro, non
 * aspetta: vietata). */
void ph_meteo_annulla(PhMeteo *m);
/* Annulla, ferma il thread (entro 1 s), svuota la cache, libera. DOPO il
 * ritorno il puntatore non si usa piu'. Va chiamata fuori dalle funzioni di
 * ritorno (dentro e' rifiutata: non fa nulla). Le chiamate di altri thread
 * gia' iniziate nello stesso momento finiscono prima o tornano errore. */
void ph_meteo_chiudi(PhMeteo *m);

/* ---- Ricerca dei luoghi per nome (aggiunta del 23 nov notte) ------------
 * Serve alla citta' scelta a mano dall'utente, usata SOLO quando il telefono
 * non da' una posizione (CONTRATTI.md §18). Servizio di geocodifica di
 * Open-Meteo (https://open-meteo.com/en/docs/geocoding-api, verificato il
 * 23 nov): GET /v1/search?name=&count=&language=&format=json, senza chiave
 * per l'uso non commerciale, stessi termini d'uso e stessi limiti generali
 * del servizio di previsione; la base dei luoghi e' GeoNames (CC BY 4.0):
 * chi mostra i risultati mostra PH_METEO_GEO_ATTRIBUZIONE con il
 * collegamento PH_METEO_GEO_SITO. Esce SOLO il testo scritto dall'utente,
 * la lingua e i parametri fissi: nessuna posizione. Il testo cercato e i
 * nomi dei luoghi non vanno MAI nei log ne' nei messaggi d'errore. */
#define PH_METEO_GEO_HOST "geocoding-api.open-meteo.com"
#define PH_METEO_GEO_PERCORSO "/v1/search"
#define PH_METEO_GEO_ATTRIBUZIONE "Luoghi da GeoNames, tramite Open-Meteo.com"
#define PH_METEO_GEO_SITO "https://www.geonames.org/"
#define PH_METEO_LUOGHI_MAX 10          /* count= nella richiesta */
#define PH_METEO_RICERCA_RISPOSTA_MAX (64u * 1024u)
#define PH_METEO_RICERCA_TESTO_MAX 64   /* byte UTF-8, dopo aver tolto gli spazi ai lati */
#define PH_METEO_RICERCA_PAUSA_S 2      /* tra due richieste di ricerca in rete */
#define PH_METEO_RICERCA_TETTO 10       /* al massimo tante ricerche in rete ... */
#define PH_METEO_RICERCA_FINESTRA_S (10 * 60) /* ... in ogni finestra mobile di tanti secondi */
#define PH_METEO_RICERCA_RIPROVA_S 30   /* la stessa ricerca fallita non si riprova prima */

typedef struct {
    char nome[128];         /* "name" (nella lingua chiesta, se il servizio ce l'ha) */
    char dettaglio[128];    /* "admin1, country" (le parti che ci sono), "" se nessuna */
    /* RESTANO in CENTESIMI (come la citta' salvata nel profilo, il cui formato
     * non cambia): per ph_meteo_chiedi moltiplicare per 10. */
    int32_t lat_centesimi;  /* ARROTONDATE a 2 decimali dal pezzo: -9000..9000 */
    int32_t lon_centesimi;  /* -18000..18000 */
} PhMeteoLuogo;

/* Percorso + query della ricerca: name = testo (spazi ai lati tolti, da 2 a
 * PH_METEO_RICERCA_TESTO_MAX byte di UTF-8 valido, senza caratteri di
 * controllo; codificato per l'URL con %XX per tutto cio' che non e'
 * lettera/cifra ASCII o -._~), count = PH_METEO_LUOGHI_MAX, language =
 * lingua (2 lettere minuscole, es. "it", "en"; altro = errore), format=json.
 * Nient'altro. 0 / -1. */
int ph_meteo_percorso_ricerca(const char *testo, const char *lingua, char *out, size_t capacita, PhErrore *e);
/* Analizza la risposta della ricerca (al massimo
 * PH_METEO_RICERCA_RISPOSTA_MAX byte, stesse regole di ph_meteo_analizza):
 * legge l'array "results"; per ciascun elemento name (obbligatorio, non
 * vuoto), latitude e longitude (obbligatori, nei limiti; arrotondati al
 * centesimo PIU' VICINO), admin1 e country (facoltativi). Gli elementi senza
 * i campi obbligatori si saltano; stringhe ripulite per lo schermo (UTF-8
 * valido, niente caratteri di controllo) e troncate a un carattere intero.
 * Al massimo `massimo` luoghi in v, quanti in *n. "results" assente = 0
 * luoghi (non e' un errore: il servizio lo omette quando non trova nulla).
 * {"error":true,...}, JSON non valido o troppo grande = -1 con *n = 0. */
int ph_meteo_analizza_ricerca(const char *json, size_t len, PhMeteoLuogo *v, size_t massimo, size_t *n,
                              PhErrore *e);

/* v NULL = errore con frase (per l'utente, MAI con il testo cercato); n puo'
 * essere 0 (nessun risultato: v non NULL). Valido solo durante la chiamata.
 * Chiamata dal thread del pezzo: tornare in fretta; da dentro si possono
 * chiamare solo ph_meteo_chiedi e ph_meteo_cerca. */
typedef void (*PhMeteoCercaFatto)(const PhMeteoLuogo *v, size_t n, const char *frase, void *utente);

/* Cerca i luoghi (non blocca; risposta sempre dal thread del pezzo, mai
 * dentro questa chiamata). Una sola ricerca alla volta: una nuova sostituisce
 * quella in corso (la vecchia si interrompe e la sua funzione di ritorno NON
 * arriva). Fra l'inizio di due richieste in rete passano almeno
 * PH_METEO_RICERCA_PAUSA_S secondi (la nuova aspetta, non fallisce); la
 * stessa ricerca (testo e lingua uguali) entro PH_METEO_INTERVALLO_S risponde
 * dalla memoria. Stesso limite PH_METEO_LIMITE_S, stessi certificati. I
 * tentativi a 5/15/30 minuti delle previsioni NON valgono per la ricerca
 * (la chiede l'utente, che vede l'errore e decide). Indipendente dalle
 * previsioni: puo' correre insieme a una ph_meteo_chiedi.
 * TETTO: al massimo PH_METEO_RICERCA_TETTO ricerche IN RETE in ogni finestra
 * mobile di PH_METEO_RICERCA_FINESTRA_S secondi; conta ogni avvio, anche se
 * poi interrotto (annulla o ricerca sostituita); le risposte dalla memoria non
 * contano. Oltre il tetto la funzione di ritorno arriva SUBITO con l'errore
 * «troppe ricerche: riprova tra qualche minuto», senza rete. La stessa ricerca
 * appena FALLITA non si riprova prima di PH_METEO_RICERCA_RIPROVA_S secondi:
 * nel frattempo la risposta e' subito l'errore di prima, senza rete e senza
 * contare per il tetto. Le frasi d'errore non contengono mai il testo
 * cercato e parlano di «ricerca dei luoghi». 0 / -1 (argomenti
 * non validi, come per ph_meteo_percorso_ricerca; nessuna richiesta parte). */
int ph_meteo_cerca(PhMeteo *m, const char *testo, const char *lingua, PhMeteoCercaFatto fatto, void *utente,
                   PhErrore *e);
/* Annulla SOLO la ricerca: dopo il ritorno la sua funzione di ritorno non
 * arriva piu' (una in corso si aspetta). ph_meteo_annulla e ph_meteo_chiudi
 * annullano anche la ricerca. */
void ph_meteo_cerca_annulla(PhMeteo *m);

#endif
