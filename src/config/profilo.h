/* Profilo per telefono (specifica 2.2, 4.12 e 4.13): che cosa si e' misurato o
 * osservato su un telefono, per scegliere i valori giusti la volta dopo e per
 * alimentare la "tabella dei telefoni testati" pubblicata con il progetto.
 * Parte pura (nessuna grafica, nessun telefono); dipende da config.h e comune.
 *
 * DOVE. Un file per telefono accanto agli altri file di configurazione:
 *   ~/.config/YASDE/profilo-<seriale ripulito>.toml
 * Il nome del file usa la stessa pulizia del numero di seriale di config.h
 * (ph_cfg_seriale_pulito, con l'impronta se cambia qualcosa): il seriale NON
 * compare mai dentro il file, ne' l'indirizzo. Stesse regole di scrittura di
 * config.h (temporaneo + fsync + rename, 0600, cartelle 0700) e di lettura.
 *
 * FORMATO (sottoinsieme di TOML di toml.h). Una chiave assente = "non
 * misurato": mai valori inventati. Esempio con valori finti:
 *
 *   [profilo]
 *   versione = 1
 *   modello = "Modello-Finto 5G"
 *   api = 34                       # livello API (Android 14 = 34)
 *   [encoder]
 *   scelto = "c2.esempio.avc.encoder"
 *   ripiego = "c2.android.avc.encoder"   # ripiego usato, se c'e' stato
 *   repeat_previous_frame_after_ignorato = true   # true: serve il ridisegno forzato
 *   [misure]
 *   max_flussi = 3                 # flussi attivi in parallelo senza degrado
 *   [[sintomo]]
 *   nome = "frame-fermi"
 *   nota = "dopo 10 s senza cambiamenti l'immagine si ferma"
 *   data = "2026-11-24"
 *   [[workaround]]
 *   nome = "ridisegno-forzato"
 *   nota = "ogni 500 ms"
 *   data = "2026-11-24"
 *
 * ROBUSTEZZA. Come config.h: valori di tipo sbagliato, fuori intervallo, troppo
 * lunghi o con caratteri non ammessi tornano a "non misurato" (le voci di
 * sintomo/workaround con nome non valido o doppio sono scartate); chiavi e
 * sezioni sconosciute sono ignorate. Un file con sintassi sbagliata o troppo
 * grande (oltre PH_PROF_MAX_FILE) viene messo da parte come
 * "<nome>.illeggibile[.N]": ph_profilo_carica torna 1 con il profilo vuoto.
 * Un file di VERSIONE FUTURA (versione > PH_PROF_VERSIONE), scritto da un
 * programma piu' nuovo, NON si tocca e non si rinomina (alternando programmi
 * vecchi e nuovi non si accumulerebbero copie): ph_profilo_carica torna -1 con
 * il profilo vuoto e un messaggio. Esiti: 0 letto o assente, 1 messo da parte,
 * -1 errore vero o versione futura; DOPO -1 NON SI DEVE SALVARE (si lavora col
 * profilo vuoto in memoria), altrimenti si sovrascrive il profilo piu' nuovo.
 * Il modello e' la sigla commerciale di Build.MODEL e MAI il nome del
 * dispositivo scelto dall'utente (che puo' contenere il suo nome).
 * Le stringhe del modello e dei codificatori ammettono solo [A-Za-z0-9 ._+()/,-]
 * (niente ':', per non formare indirizzi web che la tabella pubblica
 * trasformerebbe in collegamenti):
 * escono cosi' tali e quali nella tabella pubblica senza rischi di dati personali
 * o di Markdown rotto. Le note dei sintomi sono testo libero ma NON vanno mai
 * nella tabella.
 *
 * Valore di ritorno: 0 riuscito, -1 errore (dove non e' detto diversamente). */
#ifndef PH_PROFILO_H
#define PH_PROFILO_H

#include <stdbool.h>
#include <stddef.h>

#include "comune/buffer.h"
#include "comune/errore.h"

#define PH_PROF_VERSIONE 1
#define PH_PROF_LUN_MODELLO 64
#define PH_PROF_LUN_ENCODER 96
#define PH_PROF_LUN_NOME 32  /* nome breve: [A-Za-z0-9_.-], 1-31 caratteri */
#define PH_PROF_LUN_NOTA 128
#define PH_PROF_LUN_DATA 11  /* "AAAA-MM-GG" */
#define PH_PROF_MAX_VOCI 32  /* per i sintomi e per i workaround */
#define PH_PROF_MAX_FLUSSI 64
#define PH_PROF_MAX_FILE (256u * 1024u)

typedef enum { PH_PROF_NON_MISURATO, PH_PROF_NO, PH_PROF_SI } PhProfTri;

typedef struct {
    char nome[PH_PROF_LUN_NOME];
    char nota[PH_PROF_LUN_NOTA]; /* valore o nota, "" se non c'e' */
    char data[PH_PROF_LUN_DATA]; /* "AAAA-MM-GG", "" se non nota */
} PhProfVoce;

typedef struct {
    char modello[PH_PROF_LUN_MODELLO]; /* "" = non misurato */
    int api;                           /* livello API 1-999, 0 = non misurato */
    char encoder[PH_PROF_LUN_ENCODER]; /* codificatore scelto, "" = non misurato */
    char ripiego[PH_PROF_LUN_ENCODER]; /* ripiego usato, "" = nessuno */
    PhProfTri repeat_ignorato;         /* "repeat-previous-frame-after" ignorato: serve il ridisegno forzato */
    int max_flussi;                    /* 1-64 flussi in parallelo senza degrado, 0 = non misurato */
    PhProfVoce sintomi[PH_PROF_MAX_VOCI];
    size_t n_sintomi;
    PhProfVoce workaround[PH_PROF_MAX_VOCI];
    size_t n_workaround;
} PhProfilo;

/* La struttura e' di decine di KB: meglio nello heap. */
PhProfilo *ph_profilo_nuovo(void); /* vuoto = tutto "non misurato"; NULL se memoria finita */
void ph_profilo_libera(PhProfilo *p);
void ph_profilo_vuoto(PhProfilo *p);

/* Testo <-> struttura (senza toccare il disco). da_testo: -1 con "riga N: ..."
 * per la sintassi, o con il motivo se la versione e' futura; in errore p e' vuoto.
 * Le date devono essere reali (mese 1-12, giorno coerente col mese, bisestili). */
int ph_profilo_da_testo(const char *testo, size_t lunghezza, PhProfilo *p, PhErrore *e);
int ph_profilo_in_testo(const PhProfilo *p, PhBuffer *out);

/* Su disco, per il seriale del telefono (mai scritto nel file). */
int ph_profilo_carica(const char *seriale, PhProfilo *p, PhErrore *e);
int ph_profilo_salva(const char *seriale, const PhProfilo *p, PhErrore *e);

/* Aggiornamento di un campo; valori non validi -> -1 e il profilo non cambia. */
int ph_profilo_imposta_telefono(PhProfilo *p, const char *modello, int api); /* modello "" o api 0: non misurato */
int ph_profilo_imposta_encoder(PhProfilo *p, const char *scelto, const char *ripiego); /* ripiego NULL o "": nessuno */
void ph_profilo_imposta_repeat(PhProfilo *p, PhProfTri ignorato);
int ph_profilo_imposta_max_flussi(PhProfilo *p, int flussi); /* 0 = non misurato, 1-64 */

/* Aggiunge un sintomo osservato / un workaround attivato. `data` "AAAA-MM-GG"
 * oppure NULL per la data di oggi (UTC); `nota` puo' essere NULL. Se il nome
 * c'e' gia' non si crea un doppione: nota e data della voce esistente vengono
 * aggiornate. Torna 0 aggiunta, 1 gia' presente (aggiornata), -1 nome o data non
 * validi, nota troppo lunga o elenco pieno. */
int ph_profilo_aggiungi_sintomo(PhProfilo *p, const char *nome, const char *nota, const char *data, PhErrore *e);
int ph_profilo_aggiungi_workaround(PhProfilo *p, const char *nome, const char *nota, const char *data, PhErrore *e);

/* Tabella dei telefoni testati (Markdown), senza dati personali: solo modello,
 * versione di Android, codificatore e ripiego, comportamento di
 * repeat-previous-frame-after, flussi paralleli, NOMI dei sintomi e dei
 * workaround (mai le note). I campi non misurati dicono "non misurato".
 * L'intestazione (due righe, con a capo) e' una stringa fissa; la riga
 * e' una sola, senza a capo finale. Errore se non entra in `capacita`. */
const char *ph_profilo_tabella_intestazione(void);
int ph_profilo_riga_tabella(const PhProfilo *p, char *out, size_t capacita, PhErrore *e);

#endif
