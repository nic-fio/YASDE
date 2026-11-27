/* Dettagli interni del pezzo "dati": non fanno parte del contratto. */
#ifndef PH_DATI_INTERNO_H
#define PH_DATI_INTERNO_H

#include <pthread.h>
#include <stdatomic.h>

#include "dati/dati.h"

/* Tipi dei messaggi (docs/protocollo.md 2.6-2.8). */
enum {
    PH_DT_APP_ELENCO = 0x70,
    PH_DT_APP_ICONA = 0x71,
    PH_DT_STATO = 0x72,
    PH_DT_NOTIFICHE = 0x73,
    PH_DT_MINIATURA = 0x74,
    PH_DT_MEMORIA = 0x75,
    PH_DT_MEMORIA_DETTAGLIO = 0x76,
    PH_DT_CALENDARIO = 0x77,
    PH_DT_CALENDARIO_INTERVALLO = 0x7b,
    PH_DT_SVEGLIA = 0x7c,
    PH_DT_BATTERIA = 0x7d,
    PH_DT_CONTROLLI_STATO = 0x80,
    PH_DT_SUONERIA_IMPOSTA = 0x81,
    PH_DT_NON_DISTURBARE_IMPOSTA = 0x82,
    PH_DT_TORCIA_IMPOSTA = 0x83,
    PH_DT_TROVA_AVVIA = 0x84,
    PH_DT_TROVA_FERMA = 0x85,
    PH_DT_CONTROLLI_CAMBIATI = 0x8e,
    PH_DT_POSIZIONE = 0x90,
    PH_DT_CHIAMATE = 0x91,
    PH_DT_RETE = 0x92,
    PH_DT_MEDIA = 0x93,
    PH_DT_MEDIA_COMANDO = 0x94,
    PH_DT_STATO_CAMBIATO = 0x7e,
    PH_DT_NOTIFICHE_CAMBIATE = 0x7f,
    PH_DT_CERCA = 0x78,
    PH_DT_TROVATO = 0x79,
    PH_DT_CERCA_FINE = 0x7a,
    PH_DT_APPUNTI_TESTO = 0x58,
    PH_DT_APPUNTI_IMPOSTA = 0x59,
    PH_DT_APPUNTI_INCOLLA = 0x5a,
};

#define PH_DT_MAX_CORPO (1u << 20)  /* docs/protocollo.md 2 */

/* Modi di pulizia (ph_dati_pulisci). */
enum {
    PH_PUL_RIGA,     /* nomi: ogni controllo (C0, C1, U+2028/9) diventa spazio */
    PH_PUL_TESTO,    /* testi: restano \n \r \t, gli altri C0 diventano U+FFFD */
    PH_PUL_APPUNTI,  /* appunti: solo l'UTF-8 non valido e i NUL diventano U+FFFD */
};

/* Messaggi: dentro un avviso e' vietato ph_dati_chiudi (vedi dati.h). */
extern _Thread_local int ph_dati_in_avviso;

struct PhDati {
    PhComponente *c;
    PhDatiAvvisi avvisi;
    atomic_bool chiuso;
    atomic_int ricerche;        /* ricerche non ancora liberate (anche annullate ma in uscita) */
    atomic_int da_annullare;    /* ricerche per cui ph_dati_cerca_annulla non e' ancora stata chiamata */
    pthread_mutex_t m_ric;      /* protegge il calo di "ricerche" e la sua attesa */
    pthread_cond_t cv_ric;
    /* Avviso dei controlli (evento 0x8e), sotto m_ric. Chi lo chiama ne prende
     * una copia e rilascia il lock prima di chiamare. */
    PhDatiAvvisoControlli ctrl_avviso;
    void *ctrl_utente;
    bool ctrl_registrato;       /* l'ascolto 0x8e e' registrato sul componente */
    /* m_ctrl serializza registrazione e rimozione dell'ascolto dei controlli
     * (e il passo dei controlli di ph_dati_chiudi). Si tiene mentre si chiama
     * ph_componente_ascolta, che aspetta l'avviso in corso: l'avviso quindi non
     * deve mai prendere m_ctrl (prende solo m_ric, per pochi istanti). */
    pthread_mutex_t m_ctrl;
    int in_uso;                 /* funzioni pubbliche in corso (sotto m_ric): ph_dati_chiudi le aspetta */
    bool chiudendo;             /* ph_dati_chiudi e' partita: le nuove chiamate tornano errore (sotto m_ric) */

    /* Appunti: m_app protegge canale e scritture; il thread legge soltanto.
     * app_stop e' l'unico modo di fermare il thread: il canale si chiude
     * dopo il join, mai mentre il thread lo usa. */
    pthread_mutex_t m_app;
    PhCanale *app_canale;
    pthread_t app_thread;
    atomic_bool app_stop;
    atomic_bool app_caduto;  /* il lettore e' finito senza che nessuno lo fermasse */
    bool app_distaccato;     /* chiusa dal proprio avviso: il thread chiude il canale (sotto m_app) */
    pthread_cond_t app_cv;   /* segnala canale chiuso */
    PhDatiAvvisoAppunti app_avviso;
    void *app_utente;
};

/* utf8.c: copia ripulita di s[0..n): UTF-8 valido, terminata, al massimo
 * "massimo" byte (troncata su un confine di carattere). Le sequenze non valide
 * (anche i NUL) diventano U+FFFD; per i caratteri di controllo vedi i modi
 * PH_PUL_*. Torna memoria da free(), NULL se la memoria e' finita. */
char *ph_dati_pulisci(const uint8_t *s, size_t n, size_t massimo, int modo);
/* true se s[0..n) e' UTF-8 ben formato e senza NUL (nessuna modifica). */
bool ph_dati_utf8_valido(const uint8_t *s, size_t n);

/* richieste.c: parti comuni usate anche da nuovi.c. */
int ph_dati_chiedi(PhDati *d, uint8_t tipo, const PhBuffer *msg, PhBuffer *risposta, PhErrore *e);
/* 0, -1 se rotto/non c'e' tutta, -2 memoria finita. */
int ph_dati_leggi_pulita(PhLettore *l, char **out, size_t massimo, int modo);
int ph_dati_leggi_eventi(PhLettore *l, size_t n, PhDatiEvento **out, const char **perche);
/* nuovi.c: ascolto dei controlli (0x8e), registrato da ph_dati_ascolta_controlli. */
void ph_dati_ascolto_controlli(uint8_t tipo, const uint8_t *corpo, size_t n, void *utente);

/* appunti.c: chiusura interna, senza contatore d'uso (la chiama ph_dati_chiudi). */
void ph_dati_appunti_chiudi_i(PhDati *d);

/* Contatore d'uso (richieste.c): ogni funzione pubblica che usa d lo prende
 * all'inizio e lo rende alla fine; ph_dati_chiudi aspetta che torni a zero.
 * entra torna -1 (con errore) se la chiusura e' partita. */
int ph_dati_entra(PhDati *d, PhErrore *e);
void ph_dati_esci(PhDati *d);

/* canale.c: lettura di messaggi (formato di docs/protocollo.md 2) da un canale.
 * Il corpo piu' grande di max_corpo non si tiene: si legge e si scarta
 * (*scartato = true, corpo vuoto). Oltre PH_DT_MAX_CORPO e' un errore.
 * Ritorni: 1 messaggio letto, 0 canale chiuso pulito tra due messaggi,
 * -1 errore (canale caduto, chiuso a meta', messaggio troppo grande),
 * -2 *stop diventato true. Legge a fette per accorgersi
 * dello stop (fetta_ms). corpo e' riusato: valido fino alla lettura successiva. */
int ph_dati_leggi_messaggio(PhCanale *c, atomic_bool *stop, int fetta_ms, size_t max_corpo,
                            uint8_t *tipo, PhBuffer *corpo, bool *scartato, PhErrore *e);

/* Lettura di una stringa del protocollo (u16 + byte) senza copiarla: p e n
 * puntano dentro il buffer del lettore. Se non c'e' tutta, il lettore si rompe. */
void ph_dati_leggi_str(PhLettore *l, const uint8_t **p, size_t *n);

#endif
