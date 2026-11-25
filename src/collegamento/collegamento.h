/* Il collegamento: la macchina a stati che tiene in vita il rapporto con UN
 * telefono (specifica 1.5, 3.3 "Stati della connessione" e "Samsung", 4.1,
 * 4.8, 5.1, 5.5). Senza grafica, con un thread proprio. Usa solo i pezzi
 * sotto: scoperta, adb, abbinamento, componente, dati, config.
 *
 * STATI
 *   CERCO         cerca il telefono (prima l'ultimo indirizzo del telefono
 *                 conosciuto piu' recente, poi la scoperta in rete) e si collega.
 *   COLLEGATO     connessione ADB viva, componente avviato, saluto fatto.
 *   BLOCCATO      collegato ma lo schermo del telefono e' bloccato: la shell
 *                 mostra il velo (4.8). Si torna a COLLEGATO allo sblocco.
 *   PERSO         la connessione o il componente sono caduti: si riprova da
 *                 soli a intervalli crescenti (con un tetto), e subito con
 *                 ph_col_riconnetti_ora. Vale anche per i telefoni (Samsung)
 *                 il cui debug wireless cade al blocco: "bloccato -> perso ->
 *                 sblocco -> collegato" avviene da solo, senza intervento.
 *   CHIUSO        chiusura chiesta (ph_col_chiudi): tutto rilasciato in ordine,
 *                 il telefono ripristinato dal componente. Stato finale.
 *   NON_ABBINATO  il telefono non riconosce la chiave di questo PC. NON e' un
 *                 errore di rete (si arriva qui solo se nessun candidato, tra
 *                 ultimo indirizzo e scoperta, riesce a collegarsi): il
 *                 collegamento si ferma qui e aspetta che
 *                 chi sta sopra chiami ph_col_abbina (o ph_col_riconnetti_ora
 *                 per riprovare senza abbinare).
 *
 * TELEFONO UNICO. Alla prima ricerca, se la scoperta ne trova piu' d'uno si
 * sceglie il conosciuto visto per ultimo, altrimenti il primo; l'avviso lo
 * dice (telefoni_trovati, scelta_arbitraria). Dopo il primo collegamento, in
 * ricollegamento si accetta solo lo stesso telefono (stesso numero di serie):
 * uno diverso trovato allo stesso indirizzo viene scartato.
 * Ad ogni collegamento riuscito si aggiorna telefoni.toml (seriale, nome,
 * indirizzo "host:porta" o "[host]:porta" per IPv6, visto) tramite config.h; un errore di salvataggio
 * non e' mai fatale.
 *
 * NUMERO DI SERIE. Si legge con ph_shell_esegui("getprop ro.serialno"). Se e'
 * vuoto o "unknown" si usa "aid-<android_id>" (settings get secure android_id:
 * stabile fino al ripristino di fabbrica, leggibile dall'utente shell); se
 * manca anche quello, "modello-<modello del banner>" (non unico se si hanno due
 * telefoni identici: vale come ultimo ripiego). E' un dato personale: arriva
 * solo nell'avviso e in telefoni.toml, mai nei log; il pezzo non scrive mai
 * indirizzi o seriali su stderr.
 *
 * CONCORRENZA
 *  - Un thread proprio fa tutto: ricerca, collegamento, controllo periodico
 *    (ph_adb_viva, ph_componente_vivo, stato "bloccato" di dati), caduta,
 *    chiusura. Gli avvisi arrivano da questo thread, uno alla volta, nell'ordine.
 *  - Da dentro un avviso si puo': ph_col_presta/restituisci, ph_col_stato,
 *    ph_col_riconnetti_ora, ph_col_abbina (non bloccano). NON si puo'
 *    ph_col_chiudi: la funzione se ne accorge, scrive un messaggio generico su
 *    stderr, torna -1 e non fa nulla (chiuderlo dal thread che lo chiude
 *    sarebbe uno stallo). Va chiamato da un altro thread.
 *  - Tutte le altre funzioni si chiamano da qualunque thread.
 *  - Le attese del thread (pausa tra i tentativi, controllo) sono interrompibili:
 *    ph_col_chiudi non aspetta mai un intervallo di riprova. Resta il tempo
 *    massimo di una chiamata bloccante dei pezzi sotto (ph_adb_collega,
 *    ph_componente_avvia, ph_abbinamento_esegui, una scoperta), ognuna con il
 *    suo timeout dalle opzioni: la chiusura a meta' tentativo dura al piu'
 *    quanto quella chiamata, poi rilascia tutto e finisce senza thread appesi.
 *
 * PRESTITI (regola per usare gli oggetti vivi)
 *  Gli oggetti PhAdb, PhComponente e PhDati vivono solo dentro una
 *  "sessione" (un collegamento riuscito). Per usarli si PRENDE un prestito:
 *      PhColPrestito *p = ph_col_presta(c, &e);   // NULL = non c'e' sessione
 *      ... ph_col_prestito_componente(p) ...      // validi finche' il prestito e' tuo
 *      ph_col_restituisci(p);
 *  1. Un prestito si ottiene solo in COLLEGATO o BLOCCATO.
 *  2. Alla caduta (o alla chiusura) il collegamento, PRIMA di distruggere,
 *     smette di concedere prestiti, chiama l'avviso in_perdita ("sto per
 *     perdere la connessione: rilascia"), poi aspetta che tutti i prestiti
 *     tornino, al massimo attesa_prestiti_ms.
 *  3. Chi ha un prestito non ha mai un oggetto distrutto sotto i piedi: se
 *     allo scadere dell'attesa qualcuno non ha restituito, la sessione resta
 *     "orfana" (la memoria degli oggetti e' valida; dopo una caduta le
 *     chiamate falliscono perche' la connessione e' morta, mentre un'orfana
 *     nata alla chiusura puo' avere la connessione ancora viva: va solo
 *     restituita) e viene distrutta, in ordine, da chi restituisce per
 *     ultimo. Quella restituzione puo' durare (chiude dati, componente e
 *     connessione, fino a ~2 s): ogni altra restituzione e' immediata, perche'
 *     la ritentata dei residui la fa il thread del collegamento. Il collegamento intanto prosegue con una
 *     sessione nuova. Finche' un'orfana vive il telefono resta "in uso" (il
 *     componente non e' chiuso: il ripristino lo fa la distruzione): chi sta
 *     sopra deve restituire presto. Il "telefono torna com'era" e' comunque
 *     garantito dai guardiani sul telefono (docs/protocollo.md), anche se il
 *     PC non chiude mai la sessione.
 *  4. PRIMA di restituire un prestito si chiude tutto cio' che si e' aperto con
 *     gli oggetti prestati: canali di ph_componente_canale, ricerche e appunti
 *     di dati.h. I flussi video si possono chiudere anche dopo la restituzione
 *     (il flusso e' del componente: lo chiude comunque ph_componente_chiudi,
 *     ma vanno poi passati a ph_video_chiudi). Se una ricerca di dati resta
 *     aperta ph_dati_chiudi rifiuta (-1): il collegamento NON chiude il
 *     componente ne' la connessione, scrive un messaggio generico su stderr e
 *     tiene la sessione da parte (memoria valida). La distruzione si ritenta a
 *     ogni giro del thread del collegamento, ph_col_avvia e ph_col_chiudi; se alla fine del
 *     processo resta ancora li' non e' un errore di memoria (i guardiani sul
 *     telefono ripristinano comunque). Chi vuole un rilascio completo annulla le
 *     ricerche prima di restituire.
 *  5. ph_col_restituisci non va chiamata da dentro un callback del componente o
 *     di dati (notifiche, evento_video, stato_telefono): puo' distruggere la
 *     sessione e aspettare quel callback stesso (stallo). Lo stesso vale per
 *     ph_col_chiudi. Una restituzione sola per prestito.
 *  6. ph_col_prestito_attivo(p) dice se la sessione e' ancora buona (false
 *     = in perdita: smettere e restituire).
 *  7. Consiglio: restituire i propri prestiti PRIMA di chiamare ph_col_chiudi
 *     (altrimenti ph_col_chiudi aspetta attesa_prestiti_ms e torna 1).
 *  Gli oggetti NON si usano mai senza prestito, ne' si conservano i puntatori.
 *  I callback notifiche, evento_video e stato_telefono passano da funzioni della
 *  sessione con un flag "muta" e un contatore delle chiamate in corso. Ogni
 *  orfana (da caduta o da chiusura) e ogni sessione passata ai residui viene
 *  silenziata appena nasce: non inoltra nulla di nuovo e il collegamento aspetta
 *  la chiamata eventualmente gia' in corso. Quando ph_col_chiudi e' tornata non
 *  e' in corso ne' arrivera' nessuna chiamata: chi sta sopra puo' liberare i
 *  propri dati. Queste funzioni non devono aspettare il thread che chiama
 *  ph_col_chiudi (o ph_col_restituisci dell'ultimo prestito di un'orfana),
 *  altrimenti i due si bloccano a vicenda. L'attesa NON ha limite di tempo: una
 *  funzione di avviso che non torna ferma la chiusura (ne' ph_col_chiudi ne'
 *  la restituzione dell'ultimo prestito di un'orfana tornano), e non deve
 *  aspettare il thread che sta chiudendo (per esempio con una invocazione
 *  sincrona sul thread grafico che chiama ph_col_chiudi). Da dentro una di
 *  quelle funzioni si possono chiamare ph_col_prestito_attivo e le letture.
 *
 * DATI. Il collegamento e' l'unico a registrarsi sugli eventi 0x7e-0x7f del
 * componente (ne esiste un solo ascoltatore), quindi apre lui PhDati e lo
 * presta insieme agli altri; gli avvisi di notifiche e di stato del telefono
 * (batteria, rete, segnale) si passano nelle opzioni e vengono inoltrati dal
 * thread interno del componente (tornare in fretta, non fare richieste).
 */
#ifndef PH_COLLEGAMENTO_H
#define PH_COLLEGAMENTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adb/adb.h"
#include "comune/errore.h"
#include "componente/componente.h"
#include "config/config.h"
#include "dati/dati.h"

typedef enum {
    PH_COL_CERCO = 0,
    PH_COL_COLLEGATO,
    PH_COL_BLOCCATO,
    PH_COL_PERSO,
    PH_COL_CHIUSO,
    PH_COL_NON_ABBINATO,
} PhColStato;

/* Telefono collegato (valido in COLLEGATO e BLOCCATO). */
typedef struct {
    char seriale[PH_CFG_LUN_SERIALE];     /* vedi "NUMERO DI SERIE" */
    char nome[PH_CFG_LUN_NOME];           /* modello */
    char indirizzo[64];                   /* host in testo */
    uint16_t porta;
    bool conosciuto;                      /* era gia' in telefoni.toml */
    PhTelefonoInfo info;                  /* api, schermo, densita', modello */
} PhColTelefono;

typedef enum {
    PH_COL_ABB_NESSUNO = 0,
    PH_COL_ABB_RIUSCITO,
    PH_COL_ABB_FALLITO,
} PhColEsitoAbbinamento;

typedef struct {
    PhColStato stato;
    char motivo[192];       /* una frase per l'utente, mai seriale o indirizzo */
    bool ha_telefono;       /* true in COLLEGATO e BLOCCATO: telefono e' valido */
    PhColTelefono telefono;
    int telefoni_trovati;   /* quanti ne ha visti la scoperta all'ultima scelta (0 = non usata) */
    bool scelta_arbitraria; /* piu' di uno, nessuno conosciuto: preso il primo */
    /* In CODA, aggiunto dopo: esito di un abbinamento chiesto con ph_col_abbina /
     * ph_col_abbina_a. E' valorizzato SOLO nell'avviso che conclude quella richiesta:
     * uno e un solo avviso per ogni richiesta accettata (RIUSCITO = e' lo stesso
     * avviso che porta a CERCO; FALLITO = resta NON_ABBINATO, il motivo dice perche').
     * Se la chiusura arriva prima della fine della richiesta, nessun avviso la
     * conclude (arriva solo CHIUSO). In tutti gli altri avvisi vale NESSUNO: chi sta
     * sopra non deve dedurre l'esito dal "primo avviso che segue". ph_col_stato, che e'
     * una fotografia e non un avviso, lo restituisce SEMPRE a NESSUNO. */
    PhColEsitoAbbinamento esito_abbinamento;
} PhColAvviso;

/* Cambio di stato, o stesso stato con motivo nuovo (non ripete avvisi uguali;
 * un abbinamento fallito si segnala sempre). Dal thread del collegamento. */
typedef void (*PhColAvvisoFn)(const PhColAvviso *avviso, void *utente);
/* "Sto per perdere la connessione": restituire subito i prestiti. Dal thread
 * del collegamento, prima di ogni distruzione (caduta o chiusura). */
typedef void (*PhColInPerditaFn)(void *utente);

typedef struct {
    const char *jar_locale;          /* il jar del componente, per ph_componente_avvia */
    const char *percorso_chiave;     /* NULL = ph_cfg_percorso_chiave */

    PhColAvvisoFn avviso;            /* facoltativo */
    PhColInPerditaFn in_perdita;     /* facoltativo */
    void *utente;                    /* per i due avvisi sopra */

    PhEvento evento_video;           /* inoltrato a ph_componente_avvia (facoltativo) */
    void *utente_video;
    PhDatiAvvisoNotifiche notifiche; /* inoltrato a dati (facoltativo) */
    void *utente_notifiche;
    PhDatiAvvisoStato stato_telefono;/* batteria, rete, segnale (5.7), inoltrato a dati */
    void *utente_stato_telefono;

    /* Tempi in millisecondi; 0 = predefinito (tra parentesi). */
    int durata_scoperta_ms;    /* una ricerca in rete (1500) */
    int timeout_collega_ms;    /* ph_adb_collega (8000) */
    int timeout_componente_ms; /* ph_componente_avvia (15000) */
    int timeout_abbinamento_ms;/* ph_abbinamento_esegui (15000) */
    int pausa_cerca_ms;        /* tra due ricerche in CERCO (1000) */
    int riprova_iniziale_ms;   /* primo intervallo in PERSO (1000), raddoppia... */
    int riprova_massimo_ms;    /* ...fino a questo tetto (30000) */
    int riprova_dopo_blocco_ms;/* tetto dopo una caduta avvenuta da BLOCCATO: il debug
                                  wireless si riapre allo sblocco (3000) */
    int controllo_ms;          /* periodo del controllo di vita e di blocco (500) */
    int attesa_prestiti_ms;    /* attesa dei prestiti alla caduta (3000) */
} PhColOpzioni;

typedef struct PhCollegamento PhCollegamento;

/* Riempie le opzioni con i predefiniti (puntatori NULL, tempi 0). */
void ph_col_opzioni_predefinite(PhColOpzioni *o);

/* Avvia il thread: parte subito in CERCO (il primo avviso arriva da li').
 * Le opzioni si copiano; jar_locale e percorso_chiave pure. NULL = errore. */
PhCollegamento *ph_col_avvia(const PhColOpzioni *o, PhErrore *e);

/* Chiusura ordinata da qualunque stato, anche a meta' tentativo: ferma il
 * thread, rilascia tutto in ordine (dati, componente: ripristina il telefono,
 * adb), manda l'avviso CHIUSO e libera il collegamento. Torna 0, oppure 1 se
 * qualche prestito della sessione viva alla chiusura non e' stato restituito in
 * tempo (conta solo l'orfana nata ora, non quelle di cadute precedenti: la
 * sessione orfana sara' distrutta da chi restituisce; il collegamento e'
 * comunque libero), oppure -1
 * se chiamata da dentro un avviso (non fa nulla). Dopo 0/1 il puntatore non
 * e' piu' valido. */
int ph_col_chiudi(PhCollegamento *c);

/* Fotografia dell'ultimo avviso (stato corrente). */
void ph_col_stato(PhCollegamento *c, PhColAvviso *uscita);

/* "Riconnetti ora": in PERSO salta l'attesa e riprova subito (e riparte dal
 * primo intervallo); in CERCO ripete subito la ricerca; in NON_ABBINATO
 * riprova a collegarsi senza abbinare. Negli altri stati -1 (nulla da fare); -1 anche
 * in NON_ABBINATO se un abbinamento e' stato accettato o e' in corso (si aspetta il
 * suo esito). Un "riconnetti ora" arrivato dopo che il thread ha concluso un tentativo
 * ma prima che NON_ABBINATO sia pubblicato e' accettato e porta a un nuovo tentativo
 * subito. */
int ph_col_riconnetti_ora(PhCollegamento *c, PhErrore *e);

/* Solo in NON_ABBINATO: chi sta sopra da' SOLO il codice a sei cifre; la porta
 * di abbinamento la trova il collegamento con la scoperta (il servizio con
 * abbinamento = true dello stesso telefono, o l'unico presente). Non blocca: il
 * thread cerca il servizio ed esegue ph_abbinamento_esegui. Esito per avviso:
 * riuscito = CERCO ("abbinamento riuscito") e il collegamento riprende; fallito
 * = ancora NON_ABBINATO con il motivo (frase propria, senza indirizzi); servizio
 * non trovato = NON_ABBINATO con la frase "apri sul telefono la schermata
 * Associa dispositivo con codice e riprova". Il codice non si conserva oltre
 * l'uso. 0 = richiesta accettata, -1 = non in NON_ABBINATO, gia' in corso, con un
 * "riconnetti ora" accettato e non ancora pubblicato come CERCO, o
 * argomenti non validi. */
int ph_col_abbina(PhCollegamento *c, const char *codice, PhErrore *e);
/* Variante con host e porta del servizio di abbinamento dati da chi sta sopra
 * (per il caso in cui la scoperta non lo veda). Stesse regole. */
int ph_col_abbina_a(PhCollegamento *c, const char *host, uint16_t porta, const char *codice,
                    PhErrore *e);

/* ---- Prestiti ---------------------------------------------------------------*/
typedef struct PhColPrestito PhColPrestito;

/* NULL (con la frase in e) se non c'e' una sessione buona. */
PhColPrestito *ph_col_presta(PhCollegamento *c, PhErrore *e);
PhAdb *ph_col_prestito_adb(const PhColPrestito *p);
PhComponente *ph_col_prestito_componente(const PhColPrestito *p);
PhDati *ph_col_prestito_dati(const PhColPrestito *p);
/* Telefono di questa sessione (copia stabile finche' il prestito e' tuo). */
const PhColTelefono *ph_col_prestito_telefono(const PhColPrestito *p);
bool ph_col_prestito_attivo(const PhColPrestito *p);
void ph_col_restituisci(PhColPrestito *p);

#endif
