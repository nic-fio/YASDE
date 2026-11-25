/* Dettagli interni del pezzo "componente": non fanno parte del contratto.
 * Li vedono solo i .c di questa cartella (e un gancio per i test). */
#ifndef PH_COMPONENTE_INTERNO_H
#define PH_COMPONENTE_INTERNO_H

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#include "componente/componente.h"

#define PH_MSG_MAX_CORPO (1u << 20)           /* docs/protocollo.md, 2 */
#define PH_VIDEO_MAX_PACCHETTO (8u << 20)     /* docs/protocollo.md, 2.3 */
#define PH_FETTA_MS 50                        /* ogni lettura bloccante si spezza in fette: cosi' chi chiude non aspetta mai a lungo */
#define PH_TIMEOUT_PREDEFINITO_MS 10000
#define PH_TIMEOUT_RICHIESTA_MS 15000         /* VIDEO_APRI avvia un'app: puo' volerci un po' */
#define PH_JAR_PREFISSO "/data/local/tmp/yasde-"
#define PH_JAR_REMOTO_MAX 80

struct PhComponente {
    PhAdb *adb;
    char jar_remoto[PH_JAR_REMOTO_MAX]; /* /data/local/tmp/yasde-<8 cifre esadecimali>.jar */
    PhCanale *shell;
    PhCanale *comandi;
    PhCanale *input;
    PhEvento evento;
    void *utente;

    atomic_int rif;        /* il componente e ogni flusso video ne tengono uno */
    atomic_bool morto;     /* servizio o connessione caduti (o componente chiuso) */
    atomic_bool chiuso;    /* ph_componente_chiudi chiamata */
    atomic_bool fermo;     /* chiede ai thread interni di uscire */
    atomic_bool chiudendo; /* ph_componente_chiudi e' partita: nessuna PRESENZA nuova */
    atomic_uint prossimo_id;
    int timeout_ms;        /* tempo massimo di una richiesta */

    /* m protegge lo stato della risposta in arrivo; cv sveglia chi aspetta. */
    pthread_mutex_t m;
    pthread_cond_t cv;
    bool in_attesa;        /* una richiesta aspetta la risposta */
    bool pronta;           /* il thread ha depositato la risposta */
    uint8_t risp_tipo;     /* PH_MSG_OK, PH_MSG_ERRORE, 0 = memoria finita */
    PhBuffer risp_corpo;

    bool rich_occupata;     /* una richiesta alla volta sul canale comandi (protetto da m; flag e non mutex: l'attesa e' a tempo, sveglia alla morte) */
    pthread_mutex_t m_in;   /* un messaggio alla volta sul canale input */
    PhBuffer buf_in;

    /* Ascoltatori di eventi per intervalli di tipi. m_asc e' ricorsivo e tenuto
     * anche durante la chiamata: tolto un ascoltatore, la sua funzione non
     * viene piu' chiamata (un ascoltatore puo' togliere se stesso). */
    pthread_mutex_t m_asc;
    struct {
        bool usato;
        uint8_t da, a;
        PhEventoGrezzo f;
        void *utente;
    } asc[8];

    /* Presenza: thread che manda PRESENZA se da presenza_ms non parte nessuna
     * richiesta sul canale comandi. ultima_ms = ora (monotona) dell'ultima richiesta. */
    atomic_llong ultima_ms;
    int presenza_ms;            /* protetto da m */
    pthread_t th_presenza;
    bool th_presenza_ok;

    pthread_t th_comandi;
    pthread_t th_shell;
    bool th_comandi_ok;
    bool th_shell_ok;
};

static inline int64_t ph_ora_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Tempo che resta fino alla scadenza (scadenza < 0 = nessun limite -> -1). */
static inline int ph_resta_ms(int64_t scadenza)
{
    if (scadenza < 0)
        return -1;
    int64_t r = scadenza - ph_ora_ms();
    return r < 0 ? 0 : (r > 0x7fffffff ? 0x7fffffff : (int)r);
}

/* Lettura riprendibile: legge fino a `totale` byte dentro dest, ricordando in
 * *fatti quanti ne ha gia'. Una scadenza della fetta non perde nulla: si
 * richiama e si prosegue. Torna 1 completo, 0 canale chiuso (a *fatti
 * invariato), -1 errore, PH_CANALE_TEMPO fetta scaduta. */
int ph_rip_leggi(PhCanale *c, uint8_t *dest, size_t *fatti, size_t totale, int fetta_ms,
                 PhErrore *e);

void ph_componente_riferimento(PhComponente *c);
void ph_componente_rilascia(PhComponente *c);

/* Una richiesta sul canale comandi: manda `messaggio` (gia' finito con
 * ph_msg_finisci), aspetta OK/ERRORE. Il corpo dell'OK finisce in corpo_ok
 * (facoltativo, viene svuotato prima). Un ERRORE del telefono diventa
 * PhErrore con la sua frase. Timeout o caduta: -1 e componente morto. */
int ph_componente_richiedi(PhComponente *c, const PhBuffer *messaggio, PhBuffer *corpo_ok,
                           int timeout_ms, PhErrore *e);
/* Come sopra, ma l'attesa del turno (turno) dura al massimo coda_ms (< 0 = senza limite). */
int ph_componente_richiedi_coda(PhComponente *c, const PhBuffer *messaggio, PhBuffer *corpo_ok,
                                int timeout_ms, int coda_ms, PhErrore *e);

/* Orologio della presenza: CLOCK_BOOTTIME (avanza anche con il PC sospeso, cosi'
 * al risveglio la PRESENZA parte subito). Sostituibile dai test. */
extern int64_t (*ph_boot_hook)(void);
extern int ph_presenza_fetta_max_ms; /* le attese si spezzano in fette di questa durata (30 s) */
int64_t ph_boot_ms(void);
/* Quanto manca alla prossima PRESENZA (0 = e' ora), data l'ultima richiesta e l'ora. */
int ph_presenza_resta_ms(int64_t ultima, int intervallo_ms, int64_t ora);
int ph_presenza_fetta_ms(int resta);

/* Gancio per i test: cambia l'intervallo della PRESENZA automatica (ms) e
 * sveglia subito il thread che la manda. */
void ph_componente_prova_presenza(PhComponente *c, int intervallo_ms);

/* Gancio per i test: cambia il tempo massimo di una richiesta. */
void ph_componente_prova_timeout(PhComponente *c, int timeout_ms);

#endif
