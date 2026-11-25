/* Lato PC dei dati del telefono (docs/protocollo.md 2.6, 2.7, 2.8): elenco
 * delle app e icone (specifica 5.4), stato (5.7), notifiche (5.6), miniature
 * e memoria (5.9.2), ricerca di file (5.9.4), appunti condivisi (4.11).
 * Usa solo src/componente/componente.h (richieste 0x70-0x75, eventi 0x7e-0x7f,
 * canali "cerca" e "appunti") e i ph_canale_* di adb.h sui canali aperti.
 * Non decodifica PNG e JPEG: li consegna come byte.
 *
 * Ogni risposta del telefono e' trattata come ostile: lunghezze e conteggi
 * si controllano contro il buffer ricevuto e contro i tetti qui sotto, nessuna
 * allocazione dipende da un numero scritto dal telefono oltre i tetti, ogni
 * stringa torna UTF-8 valido (le sequenze non valide diventano U+FFFD,
 * i caratteri di controllo si sostituiscono: i nomi finiscono a schermo e in
 * file di configurazione) e viene troncata al limite del suo campo.
 *
 * Concorrenza:
 *  - Le funzioni di richiesta (app, icona, stato, notifiche, miniatura,
 *    memoria, memoria per categoria, calendario) si possono chiamare da thread diversi: le serializza il
 *    componente. Non vanno chiamate da dentro un avviso.
 *  - Gli avvisi di stato e notifiche arrivano dal thread interno del
 *    componente: tornare in fretta e NON chiamare le richieste. Per le
 *    notifiche l'avviso dice solo "sono cambiate": chi lo riceve deve
 *    rifare ph_dati_notifiche da un altro thread.
 *  - Gli avvisi di ricerca e di appunti arrivano da un thread di questo
 *    pezzo (uno per ricerca, uno per gli appunti): da dentro si possono
 *    chiamare le richieste, ph_dati_cerca_annulla (anche della ricerca a cui
 *    appartiene l'avviso: e' lecito, va chiamata sempre; in quel caso torna
 *    subito e il thread libera la ricerca quando esce) e ph_dati_appunti_chiudi
 *    (stessa cosa).
 *  - Da un avviso non annullare un'altra ricerca, ne' chiudere gli appunti, se
 *    il suo avviso puo' a sua volta annullare questa: i due thread si
 *    aspettano a vicenda e si bloccano.
 *  - ph_dati_chiudi da dentro un avviso (qualunque) e' vietata: la funzione lo
 *    riconosce, scrive un messaggio su stderr, torna -1 e non fa nulla.
 *  - Dopo che ph_dati_chiudi, ph_dati_cerca_annulla o ph_dati_appunti_chiudi
 *    sono tornate (se non chiamate dal proprio avviso) non arriva piu' nessun
 *    avviso. */
#ifndef PH_DATI_H
#define PH_DATI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comune/buffer.h"
#include "comune/errore.h"
#include "componente/componente.h"

/* Tetti e limiti (byte, salvo dove detto). */
#define PH_DATI_APP_MAX 2000             /* app in un elenco */
#define PH_DATI_NOTIFICHE_MAX 500        /* notifiche in un elenco */
#define PH_DATI_NOME_MAX 1024            /* pacchetto, nome app, titolo (troncati) */
#define PH_DATI_CHIAVE_MAX 4096          /* chiave di una notifica (il telefono ne manda fino a 4096) */
#define PH_DATI_TESTO_NOTIFICA_MAX 8192  /* testo di una notifica (troncato) */
#define PH_DATI_PERCORSO_MAX 4096        /* percorso di un file (PATH_MAX di Android) */
#define PH_DATI_CERCA_TESTO_MAX 1024     /* testo cercato */
#define PH_DATI_CERCA_MAX 5000           /* massimo di risultati di una ricerca */
#define PH_DATI_APPUNTI_MAX 200000       /* specifica 4.11, in entrambi i versi */

typedef struct PhDati PhDati;

/* ---- Stato (0x72, evento 0x7e) e notifiche (0x73, evento 0x7f) -----------*/
typedef enum {
    PH_DATI_RETE_NESSUNA = 0,
    PH_DATI_RETE_WIFI = 1,
    PH_DATI_RETE_CELLULARE = 2,
} PhDatiRete;

typedef struct {
    uint8_t batteria;  /* 0-100 */
    bool in_carica;
    PhDatiRete rete;
    uint8_t segnale;   /* 0-4 */
    bool bloccato;     /* schermo bloccato */
} PhDatiStato;

/* Evento 0x7e: il corpo e' valido (altrimenti l'evento si ignora). */
typedef void (*PhDatiAvvisoStato)(const PhDatiStato *stato, void *utente);
/* Evento 0x7f: le notifiche sono cambiate; rifare ph_dati_notifiche da un
 * altro thread (vedi sopra). */
typedef void (*PhDatiAvvisoNotifiche)(void *utente);

typedef struct {
    PhDatiAvvisoStato stato;          /* facoltativo (NULL = ignora) */
    PhDatiAvvisoNotifiche notifiche;  /* facoltativo */
    void *utente;
} PhDatiAvvisi;

/* Copia di un testo pronta per lo schermo: i caratteri di controllo (anche C1
 * e i separatori U+2028/U+2029, a capo compresi) diventano spazi e l'UTF-8 non
 * valido U+FFFD. Serve a chi mostra un percorso della ricerca, che il pezzo
 * consegna identico. Da liberare con free(); NULL se la memoria e' finita. */
char *ph_dati_per_schermo(const char *testo);

/* Registra l'ascolto degli eventi 0x7e-0x7f sul componente (ne prende
 * l'intervallo: uno solo per componente). Il componente deve restare vivo
 * fino a ph_dati_chiudi. */
PhDati *ph_dati_apri(PhComponente *c, const PhDatiAvvisi *avvisi, PhErrore *e);
/* Chiude gli appunti se aperti, toglie l'ascolto (aspetta un avviso in corso)
 * e libera. Le ricerche vanno annullate PRIMA (ph_dati_cerca_annulla), e tutto
 * va chiuso prima di ph_componente_chiudi. Non da dentro un avviso (vedi
 * sopra). Con una ricerca non ancora annullata non libera nulla e scrive un
 * messaggio su stderr: annullarla e richiamare. Una ricerca annullata dal
 * proprio avviso e ancora in uscita si aspetta.
 * Torna 0 = chiuso e liberato; -1 = RIFIUTATA (da dentro un avviso, con
 * ricerche non annullate, o se il componente non riesce a togliere un
 * ascolto). Gli ascolti da togliere sono due: prima quello dei controlli
 * (0x8e), poi 0x7e-0x7f. Se fallisce il primo nulla e' stato toccato. Se
 * fallisce il secondo si tenta di rimettere l'ascolto dei controlli; nel caso
 * estremo in cui anche questo fallisce l'ascolto dei controlli resta tolto e
 * ph_dati_ascolta_controlli si puo' richiamare. In ogni -1 d resta valido, il
 * motivo e' in e (facoltativo). Chi riceve -1 NON deve chiudere il componente.
 * Quando torna -1 d resta com'era (salvo il caso estremo sopra), ma durante il tentativo (mentre aspetta le
 * chiamate in corso) le chiamate di altri thread possono aver ricevuto
 * l'errore "dati: in chiusura". Torna -1 anche se un'altra chiusura e' in
 * corso. Dopo un ritorno 0 ph_dati_chiudi non si richiama: d non esiste piu'. */
int ph_dati_chiudi(PhDati *d, PhErrore *e);

/* ---- App (0x70, 0x71) -----------------------------------------------------*/
typedef struct {
    char *pacchetto;
    char *nome;
    bool di_sistema;
    bool solo_verticale;
} PhDatiApp;

/* Elenco ordinato come lo manda il telefono. Piu' di PH_DATI_APP_MAX app,
 * risposta troncata o con byte in eccesso: errore. Con 0 app *app e' NULL.
 * Liberare con ph_dati_app_libera. */
int ph_dati_app_elenco(PhDati *d, PhDatiApp **app, size_t *quante, PhErrore *e);
void ph_dati_app_libera(PhDatiApp *app, size_t quante);

/* Icona PNG lato x lato (16-512) dell'app: i byte vanno in *png (da
 * inizializzare con PH_BUFFER_VUOTO; il contenuto precedente e' sostituito
 * solo se riesce). Si controlla soltanto la firma PNG: la decodifica e' di chi
 * riceve, che deve trattare i byte come ostili. */
int ph_dati_app_icona(PhDati *d, const char *pacchetto, uint16_t lato, PhBuffer *png,
                      PhErrore *e);

/* ---- Stato, notifiche ------------------------------------------------------*/
int ph_dati_stato(PhDati *d, PhDatiStato *stato, PhErrore *e);

typedef struct {
    char *chiave;
    char *pacchetto;
    char *titolo;
    char *testo;       /* puo' essere vuoto */
    uint64_t ora_ms;   /* millisecondi dal 1970 */
} PhDatiNotifica;

/* Piu' di PH_DATI_NOTIFICHE_MAX notifiche o risposta malformata: errore.
 * Se il telefono non puo' leggerle risponde ERRORE: l'errore porta la sua
 * frase (mai un elenco vuoto finto). Con 0 notifiche *notifiche e' NULL. */
int ph_dati_notifiche(PhDati *d, PhDatiNotifica **notifiche, size_t *quante, PhErrore *e);
void ph_dati_notifiche_libera(PhDatiNotifica *notifiche, size_t quante);

/* ---- Miniature e memoria (0x74, 0x75) -----------------------------------------*/
/* Miniatura JPEG di un file (lato massimo 32-512): byte in *jpeg, come per
 * l'icona (si controlla solo la firma JPEG). Percorso al massimo
 * PH_DATI_PERCORSO_MAX byte. */
int ph_dati_miniatura(PhDati *d, const char *percorso, uint16_t lato, PhBuffer *jpeg,
                      PhErrore *e);

typedef struct {
    uint64_t totale;  /* byte della memoria interna */
    uint64_t libera;  /* mai piu' di totale */
} PhDatiMemoria;
int ph_dati_memoria(PhDati *d, PhDatiMemoria *memoria, PhErrore *e);

/* ---- Widget della shell: memoria per categoria e calendario (0x76, 0x77) ----------*/
/* Una voce della suddivisione: se il telefono non riesce a leggerla
 * (0xFFFFFFFFFFFFFFFF sul filo) leggibile e' false e byte vale 0. */
typedef struct {
    uint64_t byte;
    bool leggibile;
} PhDatiVoceMemoria;

typedef struct {
    PhDatiVoceMemoria immagini, video, audio, app;  /* app = codice, dati e cache */
} PhDatiMemoriaDettaglio;
int ph_dati_memoria_dettaglio(PhDati *d, PhDatiMemoriaDettaglio *dettaglio, PhErrore *e);

#define PH_DATI_CALENDARIO_MAX 16  /* eventi chiesti al massimo in una richiesta */

typedef struct {
    char *titolo;          /* UTF-8 valido su una riga, troncato a PH_DATI_NOME_MAX */
    uint64_t inizio_ms;    /* millisecondi dal 1970 */
    uint64_t fine_ms;      /* mai prima di inizio_ms */
    bool tutto_il_giorno;
} PhDatiEvento;

/* I prossimi eventi del telefono (al massimo "massimo", 1..PH_DATI_CALENDARIO_MAX,
 * validato prima di mandare), in ordine di inizio come li manda il telefono.
 * Piu' eventi del massimo, un numero dichiarato maggiore dei byte presenti,
 * campi troncati, flag non 0/1 o fine prima dell'inizio: errore. Con 0 eventi
 * *eventi e' NULL. Se il telefono non puo' leggere il calendario risponde ERRORE:
 * l'errore porta la sua frase (mai un elenco vuoto finto). Liberare con
 * ph_dati_calendario_libera; con errore *eventi e *quanti restano NULL/0.
 * PRIVACY: i titoli sono dati personali. Questo pezzo non li scrive in nessun
 * log ne' messaggio d'errore e chi chiama NON deve salvarli su disco (ne' in
 * configurazione, ne' in cache, ne' nei log); il PC li chiede solo se l'utente
 * ha messo il widget Calendario sul desktop. */
int ph_dati_calendario(PhDati *d, uint8_t massimo, PhDatiEvento **eventi, size_t *quanti,
                       PhErrore *e);
void ph_dati_calendario_libera(PhDatiEvento *eventi, size_t quanti);

/* ---- Widget aggiunti il 23 nov: calendario del mese, sveglia, batteria
 * (0x7b-0x7d, docs/protocollo.md §2.6). Stesse regole di concorrenza delle
 * altre richieste (non da dentro un avviso). Stesse regole di ostilita'.
 * PRIVACY: come ph_dati_calendario (titoli mai nei log, mai su disco). */
#define PH_DATI_CALENDARIO_INTERVALLO_MAX 500   /* eventi al massimo in una risposta */
#define PH_DATI_CALENDARIO_GIORNI_MAX 62        /* ampiezza massima dell'intervallo */

typedef struct {
    PhDatiEvento *eventi;  /* NULL se 0 */
    size_t numero;
    bool troncato;         /* il telefono ne aveva piu' del massimo chiesto */
    char *app_calendario;  /* pacchetto dell'app Calendario predefinita, "" se nessuna (mai NULL a riuscita) */
} PhDatiCalendarioIntervallo;

/* Eventi che si sovrappongono a [da_ms, a_ms) (a - da al massimo 62 giorni),
 * al massimo `massimo` (1-PH_DATI_CALENDARIO_INTERVALLO_MAX), validati PRIMA di
 * mandare (argomenti non validi: -1 senza toccare il telefono). Ogni evento
 * come in ph_dati_calendario (fine mai prima dell'inizio). Liberare con
 * ph_dati_calendario_intervallo_libera; a errore *out resta azzerato. */
int ph_dati_calendario_intervallo(PhDati *d, uint64_t da_ms, uint64_t a_ms, uint16_t massimo,
                                  PhDatiCalendarioIntervallo *out, PhErrore *e);
void ph_dati_calendario_intervallo_libera(PhDatiCalendarioIntervallo *c);

typedef struct {
    bool presente;
    uint64_t ora_ms;   /* 0 se non presente */
    char *pacchetto;   /* "" se non noto (mai NULL a riuscita) */
} PhDatiSveglia;
int ph_dati_sveglia(PhDati *d, PhDatiSveglia *out, PhErrore *e);
void ph_dati_sveglia_libera(PhDatiSveglia *s);

typedef enum {
    PH_DATI_BAT_SCONOSCIUTO = 0,
    PH_DATI_BAT_IN_CARICA = 1,
    PH_DATI_BAT_IN_SCARICA = 2,
    PH_DATI_BAT_NON_IN_CARICA = 3,
    PH_DATI_BAT_PIENA = 4,
} PhDatiBatStato;

typedef struct {
    uint8_t livello;          /* 0-100 */
    PhDatiBatStato stato;     /* valori fuori elenco -> SCONOSCIUTO */
    uint8_t alimentazione;    /* bit 0 rete, 1 USB, 2 senza fili, 3 base */
    bool temperatura_nota;
    int temperatura_decimi;   /* decimi di grado C, valida con temperatura_nota (limitata a -500..1500) */
    uint8_t salute;           /* 0-6 come il protocollo; fuori elenco -> 0 */
    uint16_t tensione_mv;     /* 0 = non nota */
    int32_t minuti_carica;    /* -1 = non noto */
    int32_t minuti_autonomia; /* -1 = non noto */
} PhDatiBatteria;
int ph_dati_batteria(PhDati *d, PhDatiBatteria *out, PhErrore *e);

/* ---- Controlli rapidi (0x80-0x85, evento 0x8e; docs/protocollo.md §2.10) ----
 * Gli avvisi dei controlli si registrano a parte (l'intervallo 0x8e e' del
 * pezzo dati come 0x7e-0x7f): ph_dati_ascolta_controlli, chiamata da un
 * thread qualunque fuori dagli avvisi; avviso dal thread interno del
 * componente, da trattare come gli altri (tornare in fretta, niente
 * richieste). Una funzione NULL toglie l'ascolto (aspetta un avviso in
 * corso). ph_dati_chiudi lo toglie comunque. Da dentro un avviso e' rifiutata
 * (-1). Sostituire un avviso gia' registrato: se torna -1 resta quello di prima
 * (avviso e dato). Registrazione e rimozione sono serializzate fra thread. */
#define PH_DATI_NON_DISPONIBILE (-1)
typedef struct {
    int suoneria;        /* 0 suona, 1 vibra, 2 silenzioso, PH_DATI_NON_DISPONIBILE */
    int non_disturbare;  /* 0 / 1 / PH_DATI_NON_DISPONIBILE */
    int torcia;          /* 0 / 1 / PH_DATI_NON_DISPONIBILE */
    bool trova_in_corso;
    bool puo_suoneria, puo_non_disturbare, puo_torcia, puo_trova;
} PhDatiControlli;

typedef void (*PhDatiAvvisoControlli)(const PhDatiControlli *c, void *utente);

int ph_dati_controlli(PhDati *d, PhDatiControlli *out, PhErrore *e);
int ph_dati_ascolta_controlli(PhDati *d, PhDatiAvvisoControlli avviso, void *utente, PhErrore *e);
/* Comandi: 0 / -1 con la frase del telefono (gia' per l'utente) in e.
 * Argomenti fuori intervallo: -1 senza mandare nulla. */
int ph_dati_suoneria_imposta(PhDati *d, int modo, PhErrore *e);       /* 0-2 */
int ph_dati_non_disturbare_imposta(PhDati *d, bool acceso, PhErrore *e);
int ph_dati_torcia_imposta(PhDati *d, bool accesa, PhErrore *e);
/* Trova il telefono: in questa versione il telefono risponde sempre ERRORE
 * («non disponibile su questa versione»; protocollo §2.10). */
int ph_dati_trova_avvia(PhDati *d, uint16_t secondi, PhErrore *e);    /* 5-120 */
int ph_dati_trova_ferma(PhDati *d, PhErrore *e);

/* ---- Posizione approssimativa (0x90, docs/protocollo.md §2.11) ---------
 * Solo per il widget Meteo, mentre e' sul desktop. RISERVATEZZA:
 * posizione e localita' mai nei log ne' nei messaggi d'errore, mai su disco. */
typedef enum {
    PH_DATI_POS_NOTA = 0,
    PH_DATI_POS_SPENTA = 1,      /* posizione del telefono spenta */
    PH_DATI_POS_NEGATA = 2,      /* permesso negato all'utente shell */
    PH_DATI_POS_SCONOSCIUTA = 3, /* nessuna posizione nota */
} PhDatiPosEsito;

typedef struct {
    PhDatiPosEsito esito;
    /* MILLESIMI di grado (gradi * 1000), sempre multipli di 5: il telefono
     * arrotonda a 0,005 gradi, circa 500 m (23 nov notte; prima erano
     * centesimi: nomi cambiati apposta perche' nessuno li usi come prima). */
    int32_t lat_millesimi;       /* -90000..90000 */
    int32_t lon_millesimi;       /* -180000..180000 */
    bool eta_nota;
    uint32_t eta_s;
    uint8_t fornitore;           /* 0 rete, 1 gps, 2 fusa, 3 passivo */
    char localita[128];          /* "" se non nota; UTF-8 ripulito per lo schermo */
    bool ricerca_avviata;        /* (23 nov notte) il telefono ha avviato una ricerca in
                                    sottofondo: richiedere UNA volta dopo ~15 s */
} PhDatiPosizione;

/* richiesta_permessa: vedi il protocollo (il telefono puo' avviare UNA ricerca
 * di rete in sottofondo se manca una posizione recente). 0 / -1. La risposta
 * arriva SUBITO (decine di millisecondi, decisione del 23 nov notte): il
 * telefono non aspetta mai dentro la richiesta ne' la posizione nuova ne' il
 * Geocoder; con ricerca_avviata chi chiama richiede una volta dopo ~15 s.
 * Esito, coordinate (fuori limite o non multiple di 5), fornitore o
 * ricerca_avviata fuori misura, localita' non
 * UTF-8 o oltre 127 byte, corpo troncato o con coda: -1 (messaggio fisso,
 * senza dati), come per le altre letture. Con esito diverso da NOTA
 * coordinate e localita' arrivano a zero (valori diversi = -1). A errore
 * *out e' SCONOSCIUTA con tutto a zero. Nessun dato di posizione nei log. */
int ph_dati_posizione(PhDati *d, bool richiesta_permessa, PhDatiPosizione *out, PhErrore *e);

/* ---- Ricerca (canale "cerca", 0x78-0x7a) --------------------------------------
 * ph_dati_cerca_avvia torna subito: un thread interno apre il canale, manda la
 * richiesta e consegna i risultati man mano. Un errore di apertura o di invio
 * arriva quindi come avviso finale con PH_DATI_CERCA_ERRORE.
 * Per ogni ricerca arrivano zero o piu' avvisi "trovato" e, se non si annulla,
 * esattamente un avviso finale, entrambi dal thread della ricerca. */
typedef enum {
    PH_DATI_CERCA_FINITA = 0,
    PH_DATI_CERCA_MASSIMO = 1,   /* raggiunto il massimo (anche i tetti del telefono) */
    PH_DATI_CERCA_ERRORE = 2,    /* cartella non cercabile, canale caduto, risposta ostile */
} PhDatiCercaMotivo;

typedef struct {
    const char *cartella;  /* percorso assoluto di partenza, non vuoto (copiato) */
    const char *testo;     /* cercato nei nomi, non vuoto (copiato) */
    bool solo_cartelle;
    uint16_t massimo;      /* 0 = il massimo del telefono; al piu' PH_DATI_CERCA_MAX */
} PhDatiCerca;

typedef struct PhDatiRicerca PhDatiRicerca;
/* percorso: UTF-8 valido senza NUL, consegnato IDENTICO ai byte del telefono
 * (caratteri di controllo compresi: per mostrarlo a schermo usare
 * ph_dati_per_schermo), valido solo durante la chiamata. Un risultato con
 * percorso non UTF-8, con NUL, vuoto o oltre PH_DATI_PERCORSO_MAX si salta,
 * senza contarlo e senza fermare la ricerca. Se si e' chiesto "solo cartelle"
 * i file non arrivano; dopo il massimo non arriva altro. */
typedef void (*PhDatiTrovato)(const char *percorso, bool cartella, void *utente);
typedef void (*PhDatiCercaFine)(PhDatiCercaMotivo motivo, void *utente);

PhDatiRicerca *ph_dati_cerca_avvia(PhDati *d, const PhDatiCerca *r, PhDatiTrovato trovato,
                                   PhDatiCercaFine fine, void *utente, PhErrore *e);
/* Interrompe subito (chiude il canale: il telefono si ferma) e libera. Dopo
 * il ritorno non arriva piu' nessun avviso. Va chiamata SEMPRE, una volta
 * sola, anche dopo l'avviso finale, e anche da dentro i suoi avvisi (torna
 * subito: la ricerca si libera quando il thread esce e dopo il ritorno non
 * si tocca piu'; una seconda chiamata dallo stesso avviso e' innocua). Puo' aspettare la fine di un avviso in corso e
 * dell'apertura del canale. Per le ricerche in rapida sequenza (l'utente
 * digita): annullare la precedente, poi avviare la nuova. */
void ph_dati_cerca_annulla(PhDatiRicerca *r);

/* ---- Appunti (canale "appunti", 0x58-0x5a) -----------------------------------
 * Solo testo, al massimo PH_DATI_APPUNTI_MAX byte in entrambi i versi: dal
 * telefono un testo piu' lungo viene ignorato, dal PC e' rifiutato prima
 * dell'invio. Il testo vuoto non si manda (il telefono lo ignora): errore.
 * Un solo canale appunti per PhDati.
 * Dal telefono si sostituiscono solo UTF-8 non valido e NUL; se dopo la
 * sostituzione il testo supera il limite il messaggio si scarta. */
/* testo: UTF-8 valido terminato da '\0' (n byte), valido solo durante la
 * chiamata. Con testo == NULL (n == 0) e' l'avviso finale: il lettore e'
 * finito senza che nessuno lo fermasse (canale caduto, messaggio oltre 1 MiB);
 * da allora imposta e incolla danno l'errore "canale appunti caduto" e non
 * arriva altro. Nessun avviso se si chiude con ph_dati_appunti_chiudi. */
typedef void (*PhDatiAvvisoAppunti)(const char *testo, size_t n, void *utente);

int ph_dati_appunti_apri(PhDati *d, PhDatiAvvisoAppunti avviso, void *utente, PhErrore *e);
/* "Imposta": mette il testo negli appunti del telefono. */
int ph_dati_appunti_imposta(PhDati *d, const char *testo, PhErrore *e);
/* "Incolla nella sessione N": mette il testo negli appunti e manda «incolla»
 * all'app della sessione video id_sessione (ph_video_id). */
int ph_dati_appunti_incolla(PhDati *d, uint32_t id_sessione, const char *testo, PhErrore *e);
/* Ferma il thread e chiude il canale; dopo il ritorno nessun avviso. Sicura
 * anche mentre arrivano testi (puo' aspettare fino a 150 ms) e dall'avviso
 * degli appunti stesso (torna subito, il thread chiude il canale uscendo). */
void ph_dati_appunti_chiudi(PhDati *d);

#endif
