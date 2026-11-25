/* Interno dei servizi veri (non e' un contratto: lo includono solo i file
 * sv_*.c e servizi_veri.c di questa cartella).
 *
 * MODELLO. Il collegamento (collegamento.h) da' una "sessione" per ogni
 * connessione riuscita. Qui ogni sessione ha UN prestito (SvSessione), preso
 * da un thread proprio, il "gestore" (manager), mai da un avviso del
 * collegamento: gli avvisi si limitano a pubblicare lo stato e a mettere un
 * evento in coda al gestore. Chi lavora (thread dei pool) non prende prestiti
 * suoi: "usa" la sessione (sv_sessione_usa/lascia, un contatore) e il gestore,
 * quando la sessione cade o l'applicazione si chiude, la marca non piu' attiva,
 * aspetta che nessuno la usi, chiude tutto cio' che si era aperto con gli
 * oggetti prestati (ricerche, appunti, canale audio, ascolto del pannello,
 * coda dei trasferimenti) e solo allora restituisce il prestito (regole 3, 4
 * e 5 dei prestiti).
 *
 * LUCCHETTI (ordine: sv->m, poi sf->m del flusso o s->m della sessione; mai
 * il contrario; nessuno si tiene durante una chiamata che puo' aspettare un
 * altro thread o la rete, salvo i brevi sf->m / s->m dove detto):
 *   sv->m   sessione corrente, elenco dei flussi, contatore d'uso del collegamento
 *   s->m    attiva e contatore d'uso della sessione, elenco delle ricerche
 *   sf->m   stato e puntatori di un flusso
 *   sv->am  audio, volume e muto
 *   sv->tm  stato del telefono (batteria, rete, pannello) */
#ifndef PH_OSSATURA_SV_INTERNO_H
#define PH_OSSATURA_SV_INTERNO_H

#include "logica.h"
#include "ossatura.h"
#include "shell/servizi_backend.h"

#if PH_OSSATURA_SERVIZI_VERI
#include <stdatomic.h>

#include "audio/audio.h"
#include "collegamento/collegamento.h"
#include "config/config.h"
#include "config/lingua.h"
#include "dati/dati.h"
#include "file/file.h"
#include "trasferimenti/installa.h"
#include "trasferimenti/trasferimenti.h"
#include "video/cattura.h"
#include "video/decodifica.h"
#include "meteo/meteo.h"
#include "video/esporta.h"

#define SV_FLUSSI_MAX 16
#define SV_RICERCHE_MAX 8

typedef struct Sv Sv;

/* Meteo (sv_meteo.c). Un solo lucchetto m; niente chiamate a pezzi sotto o alla
 * shell che possano aspettare mentre e' preso, salvo le pubblicazioni
 * (consegna asincrona) e le chiamate non bloccanti a PhMeteo. */
typedef struct {
    GMutex m;
    GCond cv;                  /* uso == 0 */
    PhMeteo *pm;               /* UNO per processo: creato alla prima osservazione, chiuso da sv_meteo_chiudi */
    int uso;                   /* chi sta chiamando ph_meteo_* adesso */
    bool chiuso;
    bool osserva;
    guint64 gen;               /* cresce a ogni true/false: i giri e le risposte vecchi si scartano */
    bool in_giro, rifai;       /* un solo giro alla volta; ne e' stato chiesto un altro nel frattempo */
    bool ha_luogo;             /* citta' scelta a mano */
    PhMeteoLuogo luogo;
    PhSrvMeteoFonte fonte_in_uso;
    bool ha_dati;              /* ultimi dati buoni, solo in memoria */
    PhMeteoPrevisione dati;
    char loc_dati[128];
    PhSrvMeteoFonte fonte_dati;
    PhSrvMeteoStato stato_pub; /* ultimo stato pubblicato */
    struct SvRq *rq;           /* la richiesta di previsioni in volo (il contesto che le sta dietro) */
    GList *rq_vecchie;         /* contesti sostituiti o annullati, ancora vivi (sv_meteo.c: quando si liberano) */
    struct SvCerca *cerca;     /* ricerca dei luoghi in corso (al massimo una) */
    GList *cerca_vecchie;
    char seriale[PH_CFG_LUN_SERIALE]; /* telefono dei dati in memoria ("" = nessuno), mai nei log */
    gint64 permesso_us;        /* ultima volta che la richiesta di ricerca e' stata permessa al telefono */
    GSource *timer;            /* il prossimo giro (5 minuti o i 15 s dopo ricerca_avviata) */
    bool attesa15, rip15;      /* la rilettura dopo ricerca_avviata e' in programma / e' questa */
    GSource *sonda;            /* controlla se chi ha chiesto ha rinunciato */
} SvMeteo;

typedef struct SvSessione SvSessione;
typedef struct SvFlusso SvFlusso;

/* Una sessione di collegamento, dal punto di vista dell'ossatura. */
struct SvSessione {
    uint64_t id;               /* unico, crescente, mai 0 */
    PhColPrestito *p;          /* il prestito: lo restituisce solo il gestore */
    PhComponente *c;
    PhDati *d;
    PhAdb *a;
    int schermo_w, schermo_h, densita; /* del telefono */
    GMutex m;
    GCond cv;
    bool attiva;               /* false da in_perdita in poi: nessun uso nuovo */
    int uso;                   /* chi sta usando c, d, a adesso */
    GList *ricerche;           /* SvRicerca*, sv_file.c */
    bool appunti_aperti, ascolto_pannello;
    bool ascolto_controlli;    /* l'ascolto dell'evento 0x8e e' registrato (s->m) */
    atomic_bool prima_notifiche; /* la prossima lettura delle notifiche e' la prima della sessione */
};

struct Sv {
    atomic_int rif;
    PhServizi *serv;
    PhCollegamento *col;
    int col_uso;               /* chi sta chiamando ph_col_presta (sv->m) */
    bool avvio_finito;         /* ph_col_avvia e' tornata (sv->m) */
    atomic_int chiusura;       /* ph_servizi_libera ha chiamato chiudi */
    char jar[1024];
    char chiave[1024];

    GMutex m;
    GCond cv;
    SvSessione *sess;          /* la sessione attiva, sv->m */
    uint64_t prossima_sessione;/* solo il gestore */
    atomic_uint_least64_t sess_id; /* id della sessione attiva, 0 = nessuna */
    atomic_uint_least64_t decod_sw_sess; /* ultima sessione per cui e' partito l'avviso «decodifica col
                                            processore» (0 = mai): uno per sessione di collegamento */

    /* Stato del telefono (sv->tm). */
    GMutex tm;
    PhSrvTelefono tel;

    /* Profilo e preferenze: solo il thread GTK. */
    char seriale_gtk[PH_CFG_LUN_SERIALE];
    bool profilo_ok_gtk;
    bool pref_ok;

    /* Thread di lavoro. Ogni pool ha un ruolo (sv_core.c, sv_lavoro). */
    GThreadPool *p_lavori;     /* cartelle, ricerche, miniature, ... (piu' thread) */
    GThreadPool *p_stato;      /* app, notifiche, pannello (uno: in ordine) */
    GThreadPool *p_icone;      /* icone (uno: una alla volta) */
    GThreadPool *p_scrittura;  /* profilo e preferenze (uno: in ordine) */
    GThreadPool *p_flussi;     /* apertura, pausa, ridimensionamento, chiusura (uno) */
    GThreadPool *p_input;      /* input verso il telefono (uno: in ordine) */
    GThreadPool *p_installa;   /* installazione e disinstallazione di app (una alla volta) */

    /* Gestore. */
    GThread *manager;
    GAsyncQueue *coda;

    /* Audio e volume (sv->am). */
    GMutex am;
    PhAudio *audio;
    PhSrvVolume vol;
    gint64 audio_scadenza_us;  /* 0 = nessun avvio in programma */
    bool audio_avviato;        /* nella sessione corrente */
    int audio_tentativi;       /* riaperture dopo un'interruzione, nella sessione corrente */

    /* Flussi (sv->m). */
    GPtrArray *flussi;         /* SvFlusso* */

    /* Trasferimenti: creata alla prima sessione, vive fino alla chiusura. */
    PhTrasferimenti *_Atomic tr;

    bool controlli_osserva;    /* almeno un widget osserva i controlli (sv->tm) */
    GMutex ctl_m;              /* ordina le pubblicazioni dei controlli (evento 0x8e e lettura) */
    guint64 ctl_gen;           /* cresce a ogni pubblicazione di un evento (ctl_m) */
    /* Abbinamento in corso (al massimo uno): chi lo chiude toglie il puntatore sotto abb_m. */
    GMutex abb_m;
    struct SvAbbPend *abb;
    atomic_int telefoni_noti;  /* voci di telefoni.toml (solo il numero) */
    atomic_bool barriera_ferma; /* una barriera di chiusura e' scaduta: le successive non aspettano */
    atomic_bool trova_attivo;  /* "trova il telefono" avviato da noi o visto in corso */
    atomic_int notifiche_in_coda;
    atomic_int pannello_spegnimento;
    atomic_int_least64_t pannello_non_prima_us; /* dopo un rifiuto (chiamata in corso) si riprova piu' tardi */
    atomic_uint cache_n;
    SvMeteo met;
};

/* ---- sv_core.c ------------------------------------------------------------ */
typedef void (*SvFn)(Sv *sv, void *dati);
/* Mette fn(sv, dati) in coda al pool. Dopo, fn gira su un thread del pool e
 * possiede dati. Non blocca. */
void sv_lavoro(Sv *sv, GThreadPool *pool, SvFn fn, void *dati);

void sv_rif_prendi(Sv *sv);
void sv_rif_lascia(Sv *sv);

/* NULL se non c'e' una sessione attiva. Ogni sv_sessione_usa riuscita va
 * chiusa con sv_sessione_lascia. */
SvSessione *sv_sessione_usa(Sv *sv);
void sv_sessione_lascia(SvSessione *s);
uint64_t sv_sessione_id(Sv *sv);

/* Solo per le prove: chiamata dal gestore dopo ogni evento gestito (NULL nel
 * programma). Serve ad aspettare un segnale invece di dormire. */
extern void (*sv_prova_evento_gestito)(void);
/* Idem: chiamata da ridim_lavoro dopo aver deciso (applicato o ricordato). */
extern void (*sv_prova_ridim_fatto)(void);
/* Idem: chiamata da input_lavoro dopo aver aggiornato lo stato dei gesti. */
extern void (*sv_prova_input_fatto)(void);

enum { SV_MG_SESSIONE = 1, SV_MG_PERDITA, SV_MG_AUDIO, SV_MG_FINE };
void sv_manager_invia(Sv *sv, int tipo);

/* Comandi della tabella servita da sv_core.c. */
void sv_riconnetti_ora(void *b);
void sv_profilo_salva(void *b, PhCfgTelefono *copia);
void sv_preferenze_salva(void *b, PhCfgPreferenze *copia);

/* ---- sv_dati.c: stato, notifiche, app, icone, pannello, appunti ------------ */
void sv_dati_sessione_nuova(Sv *sv, SvSessione *s);
void sv_dati_teardown(Sv *sv, SvSessione *s);
void sv_ev_stato(const PhDatiStato *stato, void *utente);
void sv_ev_notifiche(void *utente);
void sv_icona_chiedi(void *b, const char *pacchetto, int lato);
void sv_pannello(void *b, bool acceso);
void sv_appunti_imposta(void *b, const char *testo);
/* Spegne il pannello fisico dal PC (regola del telefono in mano): da un thread
 * del pool p_stato, una richiesta alla volta. */
void sv_pannello_spegni_se_in_mano(Sv *sv);
/* Nessuna funzione riaccende il pannello alla chiusura dell'ultima finestra
 * (regola del committente, PIANO.md): lo riaccende l'utente, una chiamata, o il
 * ripristino lato telefono (guardiano) alla chiusura o caduta di YASDE. */

/* ---- sv_file.c: cartelle, ricerca, miniature, lettura, memoria ------------- */
void sv_tel_elenca(void *b, PhSrvLavoro *l, const char *cartella);
void sv_tel_cerca(void *b, PhSrvLavoro *l, const char *cartella, const char *testo,
                  bool solo_cartelle, unsigned massimo);
void sv_tel_crea_cartella(void *b, PhSrvLavoro *l, const char *percorso);
void sv_tel_miniatura(void *b, PhSrvLavoro *l, const char *percorso, int lato);
void sv_tel_leggi(void *b, PhSrvLavoro *l, const char *percorso, size_t massimo);
void sv_tel_memoria(void *b, PhSrvLavoro *l);
/* Annulla le ricerche rimaste aperte della sessione (prima di restituire il
 * prestito). Dal gestore, senza lucchetti, la sessione non e' piu' usata. */
void sv_file_teardown(SvSessione *s);

/* ---- sv_tr.c: trasferimenti -------------------------------------------------- */
int sv_tr_accoda(void *b, PhSrvVerso verso, const char *origine, const char *cartella, PhErrore *e);
void sv_tr_annulla(void *b, uint64_t id);
void sv_tr_annulla_tutto(void *b);
void sv_tr_rimuovi_finiti(void *b);
/* Rilegge la coda e la pubblica (dopo un'azione che non genera avvisi). */
void sv_tr_pubblica(Sv *sv);
/* Crea la coda (prima sessione) o la riprende con la connessione nuova. */
void sv_tr_sessione_nuova(Sv *sv, SvSessione *s);
/* Prima di restituire il prestito: la coda lascia la connessione (o si chiude
 * se l'applicazione sta chiudendo). */
void sv_tr_teardown(Sv *sv, SvSessione *s, bool chiusura);
/* In chiudi: se la coda esiste ancora (staccata), la chiude. */
void sv_tr_chiudi_finale(Sv *sv);

/* ---- sv_flussi.c: flussi video, input ---------------------------------------- */
void *sv_flusso_apri(void *b, PhSrvFlusso *f, const PhSrvFlussoRichiesta *r, PhErrore *e);
int sv_flusso_leggi(void *b, void *fl, PhFotogramma *out, int timeout_ms);
void sv_flusso_interrompi(void *b, void *fl);
void sv_flusso_memoria(void *b, void *fl);
void sv_flusso_pausa(void *b, void *fl, bool pausa);
void sv_flusso_ridimensiona(void *b, void *fl, int larghezza, int altezza);
void sv_flusso_ridimensiona_densita(void *b, void *fl, int larghezza, int altezza, int densita);
void sv_flusso_input(void *b, void *fl, const PhSrvInput *in);
void sv_flusso_chiudi(void *b, void *fl, bool togli_task);
/* VIDEO_EVENTO dal componente (thread interno: torna subito). */
void sv_ev_video(uint32_t id_video, uint8_t che_cosa, uint16_t a, uint16_t b, void *utente);
/* Dal gestore: la sessione e' caduta -> tutti i flussi PERSO. */
void sv_flussi_sessione_persa(Sv *sv);
/* Dal gestore: c'e' una sessione buona -> riapre i flussi PERSO. */
void sv_flussi_sessione_nuova(Sv *sv);
/* In chiudi, dopo che la shell ha chiuso i suoi flussi: libera i rimasti. */
void sv_flussi_chiudi_finale(Sv *sv);

/* ---- sv_extra.c: installazione app, pannello con esito, widget, chiave ------------ */
void sv_apk_installa(void *b, PhSrvLavoro *l, const char *apk_pc);
void sv_app_disinstalla(void *b, PhSrvLavoro *l, const char *pacchetto);
void sv_tr_annulla_verso(void *b, PhSrvVerso verso);
void sv_pannello_chiedi(void *b, PhSrvLavoro *l, bool acceso);
void sv_tel_memoria_dettaglio(void *b, PhSrvLavoro *l);
void sv_tel_calendario(void *b, PhSrvLavoro *l, unsigned massimo);
void sv_tel_calendario_intervallo(void *b, PhSrvLavoro *l, uint64_t da_ms, uint64_t a_ms);
void sv_tel_sveglia(void *b, PhSrvLavoro *l);
void sv_tel_batteria(void *b, PhSrvLavoro *l);
void sv_controlli_osserva(void *b, bool osserva);
void sv_controllo_imposta(void *b, PhSrvLavoro *l, PhSrvControllo controllo, int valore);
void sv_trova_avvia(void *b, PhSrvLavoro *l, int secondi);
void sv_trova_ferma(void *b);
/* Sessione nuova (gestore): se c'e' un osservatore si riparte e si ripubblica. */
void sv_controlli_sessione_nuova(Sv *sv, SvSessione *s);
/* Prima di restituire il prestito: alla chiusura dell'applicazione ferma
 * "trova il telefono"; toglie l'ascolto; pubblica tutto "non disponibile". */
void sv_controlli_teardown(Sv *sv, SvSessione *s);
void sv_flusso_chiave(void *b, void *fl);
void sv_flusso_pizzico(void *b, void *fl, int video_w, int video_h, int fase, int x, int y, int passo);
void sv_flusso_screenshot(void *b, void *fl, PhSrvLavoro *l, unsigned dove, const char *cartella_pc,
                          const char *nome_app);
void sv_flusso_registra(void *b, void *fl, bool avvia, const char *cartella_pc, const char *nome_app,
                        bool con_audio);

/* ---- sv_abbina.c: prima connessione, abbinamento con il codice ---------------------- */
void sv_abbina(void *b, PhSrvLavoro *l, const char *codice, const char *host, int porta);
/* Un avviso del collegamento puo' concludere l'abbinamento in corso (thread del
 * collegamento: non blocca). */
void sv_abbina_avviso(Sv *sv, const PhColAvviso *a);
/* Chiusura dell'applicazione: l'abbinamento in corso si chiude (una risposta sola). */
void sv_abbina_chiudi(Sv *sv);
/* Conta i telefoni gia' noti (telefoni.toml); -1 se non si legge. Nessun dato
 * personale esce da qui: solo il numero. */
int sv_telefoni_noti(void);
/* Solo per le prove: attesa massima in secondi (predefinita PH_SRV_ABB_LIMITE_S). */
extern int sv_prova_abb_limite_s;

/* ---- sv_meteo.c: previsioni e ricerca dei luoghi -------------------------------------- */
void sv_meteo_inizia(Sv *sv);
void sv_meteo_osserva(void *b, bool osserva);
void sv_meteo_localita(void *b, const PhMeteoLuogo *l);
void sv_meteo_cerca(void *b, PhSrvLavoro *l, const char *testo, const char *lingua);
/* Il telefono e' arrivato o e' sparito: rifa' il giro se il meteo e' osservato (gestore). */
void sv_meteo_sessione(Sv *sv);
/* Una sessione nuova sta per partire con questo telefono (gestore, PRIMA di
 * pubblicare il profilo): se e' un altro telefono, dati, posizione e citta'
 * in memoria si azzerano e si riparte da «in attesa». */
void sv_meteo_telefono(Sv *sv, const char *seriale);
/* Primo passo della chiusura (PRIMA di chiudere il collegamento): ferma tutto,
 * chiude la ricerca in corso e ph_meteo_chiudi; dopo, nessuna callback. */
void sv_meteo_chiudi(Sv *sv);
/* Dopo l'ultimo riferimento. */
void sv_meteo_libera(Sv *sv);
/* Solo per le prove: periodo dei giri e attesa dopo ricerca_avviata, in ms. */
extern int sv_prova_meteo_periodo_ms;
extern int sv_prova_meteo_ricerca_ms;
/* Idem: ogni quanti secondi la ricerca di rete del telefono puo' essere permessa (6 ore). */
extern int sv_prova_meteo_permesso_s;
/* Idem: sorgenti GLib del meteo vive (timer del giro e sonda della ricerca): a zero da spento. */
extern atomic_int sv_prova_meteo_sorgenti;
/* Idem: l'ultimo Sv creato, e letture di controllo (contesti vecchi, giro in volo, dati azzerati). */
extern Sv *sv_prova_sv;
int sv_prova_meteo_vecchie(Sv *sv);
bool sv_prova_meteo_in_giro(Sv *sv);
bool sv_prova_meteo_azzerato(Sv *sv);

/* ---- sv_audio.c: volume e canale audio ----------------------------------------- */
void sv_volume(void *b, int volume);
void sv_muto(void *b, bool muto);
/* Programma l'avvio dell'audio 5 s dopo l'apertura del primo flusso video
 * della sessione (4.9, proposta del COMPITO: tutto qui, da misurare). */
void sv_audio_programma(Sv *sv);
/* Microsecondi all'avvio programmato; -1 = niente in programma. */
gint64 sv_audio_attesa_us(Sv *sv);
/* Avvia l'audio se l'ora e' arrivata (gestore). */
void sv_audio_avvia_se_dovuto(Sv *sv);
/* Profilo caricato: imposta il volume dell'utente (gestore). */
void sv_audio_volume_iniziale(Sv *sv, int volume);
/* Prima di restituire il prestito: chiude il canale audio. */
void sv_audio_teardown(Sv *sv);

#endif /* PH_OSSATURA_SERVIZI_VERI */
#endif
