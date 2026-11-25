/* I SERVIZI: l'UNICA porta della shell verso il telefono (e verso audio,
 * trasferimenti e persistenza). Nessun pezzo della shell include
 * collegamento.h, dati.h, componente.h, decodifica.h, audio.h o
 * trasferimenti.h: passa da qui. Dietro ci sono due realizzazioni:
 *  - quella vera, scritta dall'ossatura sopra i pezzi sotto (servizi_backend.h);
 *  - quella finta (src/shell/finto/), per provare e guardare ogni pezzo senza
 *    telefono.
 * Il pezzo comune (src/shell/comune/servizi.c) fa da tramite e garantisce le
 * regole sotto per entrambe.
 *
 * REGOLE DI THREAD (vincolanti, vedi src/shell/CONTRATTI.md §2)
 *  1. Tutte le funzioni ph_srv_* si chiamano SOLO dal thread GTK, tranne:
 *     ph_srv_flusso_leggi (solo dal thread di lettura del flusso),
 *     ph_srv_flusso_interrompi e ph_srv_flusso_preferisci_memoria (qualunque
 *     thread).
 *  2. Nessuna funzione chiamata dal thread GTK blocca: le richieste al
 *     telefono partono su thread del backend e il risultato torna con una
 *     funzione di ritorno.
 *  3. OGNI funzione di ritorno e ogni avviso arriva sul thread GTK, mai
 *     durante la chiamata che l'ha chiesto (sempre da un giro successivo del
 *     ciclo principale).
 *  4. Annullamento: le richieste prendono un GCancellable facoltativo. Se lo
 *     si annulla dal thread GTK, la funzione di ritorno NON arriva piu' (anche
 *     se il risultato e' gia' pronto in coda).
 *  5. Stringhe in ingresso (percorsi, testo cercato, nomi, testo degli
 *     appunti e dell'input): i servizi le COPIANO prima di tornare; chi chiama
 *     puo' liberarle o riusarle subito dopo la chiamata. (Il backend riceve
 *     puntatori validi solo durante la chiamata e copia cio' che gli serve:
 *     servizi_backend.h.)
 *
 * FOTOGRAFIE. Lo stato (collegamento, telefono, app, notifiche, trasferimenti,
 * volume) si legge con funzioni che non bloccano e restituiscono l'ultima
 * fotografia ricevuta. Quando cambia arriva l'avviso del suo tipo
 * (ph_srv_ascolta): chi ascolta rilegge. Un puntatore restituito da una
 * lettura vale finche' il thread GTK non torna al ciclo principale (la
 * fotografia nuova si sostituisce solo li'): cio' che serve dopo si copia.
 *
 * DATI DAL TELEFONO. Le stringhe con suffisso _schermo (e tutti i nomi, i
 * titoli e i testi) sono gia' sicure da mostrare: UTF-8 valido, senza
 * caratteri di controllo. I campi "percorso" grezzi servono solo a navigare e
 * a richiedere, mai a mostrare. */
#ifndef PH_SHELL_SERVIZI_H
#define PH_SHELL_SERVIZI_H

#include <gtk/gtk.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comune/errore.h"
#include "config/config.h"
#include "meteo/meteo.h"
#include "shell/fotogramma.h"

typedef struct PhServizi PhServizi;

/* ---- Fotografie dello stato -------------------------------------------- */

/* Stati del collegamento (gli stessi di collegamento.h, specifica 3.3). */
typedef enum {
    PH_SRV_CERCO = 0,
    PH_SRV_COLLEGATO,
    PH_SRV_BLOCCATO,      /* il telefono e' bloccato: la shell mostra il velo (4.8) */
    PH_SRV_PERSO,         /* connessione o componente caduti: si riprova da soli */
    PH_SRV_CHIUSO,        /* chiusura chiesta: stato finale */
    PH_SRV_NON_ABBINATO,
} PhSrvStato;

typedef struct {
    PhSrvStato stato;
    char motivo[192];     /* frase per l'utente (gia' tradotta), mai seriale o indirizzo */
    bool ha_telefono;     /* true in COLLEGATO e BLOCCATO */
    char nome[128];       /* modello del telefono */
    int api;              /* livello API Android (34 = Android 14) */
    int schermo_w, schermo_h; /* schermo reale in pixel */
    int densita;          /* dpi del telefono: e' la densita' predefinita delle app (4.4) */
    /* Aggiunto il 23 nov (in coda, compatibile): schermo del telefono spento
     * dal sistema o dal tasto di accensione, distinto dal blocco. Vale in
     * COLLEGATO e BLOCCATO; i flussi sono fermi e la shell lo dice con il velo
     * come per il blocco (Decisioni di chi coordina). false se non noto. */
    bool schermo_spento;
    /* Aggiunti in coda per la prima connessione (23 nov; chi riempie con
     * memset/{0} li lascia a zero = «non si sa»): */
    int telefoni_trovati;   /* quanti telefoni ha visto la scoperta all'ultima scelta (0 = non usata) */
    bool scelta_arbitraria; /* piu' d'uno, nessuno conosciuto: il collegamento ha preso il primo */
    int telefoni_noti;      /* telefoni gia' collegati in passato (telefoni.toml): 0 = mai */
} PhSrvCollegamento;

typedef enum {
    PH_SRV_RETE_NESSUNA = 0,
    PH_SRV_RETE_WIFI,
    PH_SRV_RETE_CELLULARE,
} PhSrvRete;

typedef struct {
    bool noto;            /* false finche' il telefono non ha mandato lo stato */
    int batteria;         /* 0-100 */
    bool in_carica;
    PhSrvRete rete;
    int segnale;          /* 0-4 */
    bool pannello_noto;
    bool pannello_acceso; /* pannello fisico (4.7) */
    bool in_mano;         /* regola del "telefono in mano" */
} PhSrvTelefono;

typedef struct {
    char *pacchetto;      /* identificatore (grezzo, ASCII per Android) */
    char *nome;           /* nome da mostrare */
    bool di_sistema;
    bool solo_verticale;  /* finestra 9:16 fissa (4.5) */
} PhSrvApp;

typedef struct {
    char *chiave;         /* identifica la notifica (resta uguale quando si aggiorna) */
    char *pacchetto;      /* app che l'ha mandata */
    char *titolo;
    char *testo;          /* puo' essere "" */
    uint64_t ora_ms;      /* millisecondi dal 1970: cambia quando la notifica si aggiorna */
} PhSrvNotifica;

typedef enum { PH_SRV_INVIO = 0, PH_SRV_RICEZIONE } PhSrvVerso;
typedef enum {
    PH_SRV_TR_ATTESA = 0,
    PH_SRV_TR_CORSO,
    PH_SRV_TR_COMPLETATO,
    PH_SRV_TR_FALLITO,
    PH_SRV_TR_ANNULLATO,
} PhSrvTrStato;

typedef struct {
    uint64_t id;          /* da 1, crescente, mai riusato */
    PhSrvVerso verso;
    PhSrvTrStato stato;
    char *nome;           /* nome finale del file */
    char *origine;        /* invio: percorso sul PC; ricezione: percorso sul telefono */
    char *destinazione;   /* percorso completo di arrivo */
    char *errore;         /* frase da mostrare se FALLITO, altrimenti "" */
    uint64_t dimensione;  /* byte (0 = ancora ignota) */
    uint64_t fatti;
} PhSrvTrasferimento;

typedef struct {
    int volume;           /* 0-100: guadagno lato PC (5.8) */
    bool muto;
} PhSrvVolume;

void ph_srv_collegamento(PhServizi *s, PhSrvCollegamento *out);
void ph_srv_telefono(PhServizi *s, PhSrvTelefono *out);
void ph_srv_volume(PhServizi *s, PhSrvVolume *out);

/* Elenco delle app (ordinato come lo manda il telefono). Vuoto (n = 0) finche'
 * non e' arrivato un elenco COMPLETO (regola: valido solo se termina
 * con "fine"). */
const PhSrvApp *ph_srv_app(PhServizi *s, size_t *n);
const PhSrvApp *ph_srv_app_cerca(PhServizi *s, const char *pacchetto); /* NULL se non c'e' */

/* Notifiche attuali del telefono (TUTTE: il "nascondi" e' della shell, 5.6).
 * problema != NULL se il telefono non le puo' leggere (frase da mostrare). */
const PhSrvNotifica *ph_srv_notifiche(PhServizi *s, size_t *n, const char **problema);

/* Coda dei trasferimenti, nell'ordine di accodamento. */
const PhSrvTrasferimento *ph_srv_trasferimenti(PhServizi *s, size_t *n);
/* "Puntino colorato" della barra (5.3): c'e' una voce del verso in corso o in
 * attesa e la coda non e' ferma. */
bool ph_srv_trasferimenti_attivi(PhServizi *s, PhSrvVerso verso);
/* La coda si e' fermata per la connessione caduta: riparte da sola al ritorno. */
bool ph_srv_coda_ferma(PhServizi *s);

/* Icona dell'app (lato PH_SRV_ICONA_LATO pixel). Solo dal thread GTK.
 * 1) Se per il pacchetto c'e' una riga nella tabella delle icone Kora
 *    (tabella.txt, "pacchetto nome-kora") E il file <nome-kora>.png si carica,
 *    si restituisce quella texture (anche di lato diverso da 128, se e' un PNG
 *    valido: chi la mostra la scala) e al telefono non si chiede nulla.
 *    Cartella: $YASDE_ICONE_KORA se impostata e non vuota, altrimenti
 *    $APPDIR/usr/share/yasde/icone-kora, altrimenti nessuna. La tabella si legge
 *    una volta al primo uso (tetti: 256 KiB, 2000 righe; righe malformate
 *    saltate), i PNG al primo uso di ciascun pacchetto e si tengono.
 * 2) Altrimenti (nessuna cartella, tabella o PNG mancante/illeggibile, pacchetto
 *    non in tabella): l'icona del telefono. NULL se non e' ancora arrivata:
 *    la richiesta parte da sola e all'arrivo si riceve PH_SRV_EV_ICONA con il
 *    pacchetto come argomento. La texture e' della cache: g_object_ref per
 *    tenerla. Un'icona che non arriva resta NULL (si mostra il segnaposto con le
 *    iniziali, come nel mockup). Nessun errore a schermo se i file Kora mancano. */
#define PH_SRV_ICONA_LATO 128
GdkTexture *ph_srv_icona(PhServizi *s, const char *pacchetto);

/* Preferenze generali e profilo del telefono collegato (persistenza, 5.10).
 * Sono modificabili SUL THREAD GTK: dopo una modifica chiamare *_salva, che
 * scrive su disco in modo differito (circa un secondo) su un thread del
 * backend. Prima del primo collegamento il profilo ha i valori predefiniti.
 * Quando si collega un telefono diverso arriva PH_SRV_EV_PROFILO: rileggere
 * tutto (i puntatori vecchi non valgono piu'). */
PhCfgPreferenze *ph_srv_preferenze(PhServizi *s);
void ph_srv_preferenze_salva(PhServizi *s);
PhCfgTelefono *ph_srv_profilo(PhServizi *s);
void ph_srv_profilo_salva(PhServizi *s);

/* ---- Avvisi ------------------------------------------------------------ */
typedef enum {
    PH_SRV_EV_COLLEGAMENTO = 0, /* stato del collegamento (anche blocco) */
    PH_SRV_EV_TELEFONO,         /* batteria, rete, pannello */
    PH_SRV_EV_APP,              /* elenco delle app */
    PH_SRV_EV_ICONA,            /* argomento = pacchetto dell'icona arrivata */
    PH_SRV_EV_NOTIFICHE,
    PH_SRV_EV_TRASFERIMENTI,
    PH_SRV_EV_VOLUME,
    PH_SRV_EV_APPUNTI,          /* argomento = testo arrivato dagli appunti del telefono */
    PH_SRV_EV_PROFILO,          /* profilo di un altro telefono caricato */
    PH_SRV_EV_CONTROLLI,        /* interruttori rapidi / trova il telefono cambiati (23 nov) */
    PH_SRV_EV_METEO,            /* previsioni del tempo cambiate (23 nov sera) */
    PH_SRV_EV_DECODIFICA,       /* il video si decodifica col processore: avviso da mostrare (24 nov);
                                   al massimo UNO per sessione di collegamento, decide il backend */
    PH_SRV_EV_NUMERO
} PhSrvEvento;

/* argomento vale solo durante la chiamata (NULL per gli eventi senza). */
typedef void (*PhSrvAvviso)(PhServizi *s, PhSrvEvento ev, const char *argomento, void *utente);

/* Torna un id > 0. Da dentro un avviso si puo' ascoltare e smettere. */
guint ph_srv_ascolta(PhServizi *s, PhSrvEvento ev, PhSrvAvviso fn, void *utente);
void ph_srv_smetti(PhServizi *s, guint id);

/* ---- Comandi senza risposta ------------------------------------------- */
void ph_srv_riconnetti_ora(PhServizi *s);       /* "Riconnetti ora" (4.8, 5.5) */
void ph_srv_pannello(PhServizi *s, bool acceso); /* pannello fisico (5.7) */
void ph_srv_imposta_volume(PhServizi *s, int volume); /* 0-100, guadagno lato PC */
void ph_srv_imposta_muto(PhServizi *s, bool muto);    /* non ferma la cattura (5.8) */
void ph_srv_appunti_imposta(PhServizi *s, const char *testo); /* appunti PC -> telefono (4.11) */

/* ---- Richieste con risposta (telefono) -------------------------------- */
typedef enum {
    PH_SRV_ESITO_OK = 0,
    PH_SRV_ESITO_NON_TROVATO,     /* "Cartella non trovata" */
    PH_SRV_ESITO_NON_CONSENTITO,  /* "Accesso non consentito dal sistema" (5.9.4) */
    PH_SRV_ESITO_NON_COLLEGATO,   /* nessun telefono ora */
    PH_SRV_ESITO_ERRORE,          /* altro: la frase lo dice */
} PhSrvEsito;

typedef struct {
    char *nome;           /* grezzo: per costruire il percorso */
    char *nome_schermo;   /* da mostrare */
    bool cartella;
    uint64_t dimensione;
    int64_t modificato;   /* secondi dal 1970 */
} PhSrvVoce;

/* Elenco di una cartella del telefono, senza "." e "..", prima le cartelle e
 * poi i file, per nome; i nascosti (nome con il punto) CI SONO: li filtra chi
 * mostra (5.9). frase: NULL se OK. voci valide solo durante la chiamata. */
typedef void (*PhSrvElencoFatto)(PhServizi *s, PhSrvEsito esito, const char *frase,
                                 const char *cartella, const PhSrvVoce *voci, size_t n,
                                 void *utente);
void ph_srv_tel_elenca(PhServizi *s, const char *cartella, GCancellable *annulla,
                       PhSrvElencoFatto fatto, void *utente);

/* Ricerca ricorsiva sul telefono (5.9.2, 5.9.4). I risultati arrivano a
 * gruppi (trovati), poi una sola volta fine. massimo 0 = 30 (come il mockup).
 * Per l'utente che digita: annullare la ricerca precedente, poi avviarne una
 * nuova. */
typedef struct {
    const char *percorso;         /* grezzo */
    const char *percorso_schermo; /* da mostrare */
    bool cartella;
} PhSrvTrovato; /* la dimensione dei file: ph_srv_trovato_dimensione (in fondo) */
typedef void (*PhSrvCercaTrovati)(PhServizi *s, const PhSrvTrovato *v, size_t n, void *utente);
typedef void (*PhSrvCercaFine)(PhServizi *s, PhSrvEsito esito, const char *frase, bool massimo,
                               void *utente);
void ph_srv_tel_cerca(PhServizi *s, const char *cartella, const char *testo, bool solo_cartelle,
                      unsigned massimo, GCancellable *annulla, PhSrvCercaTrovati trovati,
                      PhSrvCercaFine fine, void *utente);

/* Esito semplice (crea cartella). */
typedef void (*PhSrvFatto)(PhServizi *s, PhSrvEsito esito, const char *frase, void *utente);
void ph_srv_tel_crea_cartella(PhServizi *s, const char *percorso, GCancellable *annulla,
                              PhSrvFatto fatto, void *utente);

/* Miniatura di un'immagine o di un video del telefono (lato massimo 32-512).
 * t (NULL se esito != OK) vale durante la chiamata: g_object_ref per tenerla. */
typedef void (*PhSrvImmagineFatta)(PhServizi *s, PhSrvEsito esito, const char *frase,
                                   GdkTexture *t, void *utente);
void ph_srv_tel_miniatura(PhServizi *s, const char *percorso, int lato, GCancellable *annulla,
                          PhSrvImmagineFatta fatto, void *utente);

/* Legge un intero file del telefono in memoria (al massimo `massimo` byte,
 * altrimenti ERRORE): serve a copiare l'immagine dello sfondo sul PC (5.2.1).
 * byte (NULL se esito != OK) vale durante la chiamata: g_bytes_ref per tenerli. */
typedef void (*PhSrvByteFatti)(PhServizi *s, PhSrvEsito esito, const char *frase, GBytes *byte,
                               void *utente);
void ph_srv_tel_leggi(PhServizi *s, const char *percorso, size_t massimo, GCancellable *annulla,
                      PhSrvByteFatti fatto, void *utente);

/* Memoria interna del telefono (widget "Memoria del telefono"). */
typedef void (*PhSrvMemoriaFatta)(PhServizi *s, PhSrvEsito esito, uint64_t totale,
                                  uint64_t libera, void *utente);
void ph_srv_tel_memoria(PhServizi *s, GCancellable *annulla, PhSrvMemoriaFatta fatto,
                        void *utente);

/* ---- Trasferimenti (5.9.3) --------------------------------------------- */
/* Accoda (non blocca). INVIO: origine = file del PC, cartella = cartella del
 * telefono (qualunque, "/sdcard" vale "/storage/emulated/0"). RICEZIONE:
 * origine = file del telefono, cartella = cartella del PC che esiste. La voce
 * compare con il prossimo PH_SRV_EV_TRASFERIMENTI. 0 / -1 con la frase. */
int ph_srv_tr_accoda(PhServizi *s, PhSrvVerso verso, const char *origine, const char *cartella,
                     PhErrore *e);
void ph_srv_tr_annulla(PhServizi *s, uint64_t id);
void ph_srv_tr_annulla_tutto(PhServizi *s);
void ph_srv_tr_rimuovi_finiti(PhServizi *s);

/* ---- Flussi video (4.2, 4.5, 4.6, 4.10) -------------------------------- */
typedef struct PhSrvFlusso PhSrvFlusso;

typedef struct {
    const char *pacchetto; /* NULL o "" = specchio dello schermo reale (4.7) */
    int larghezza, altezza;/* misura del flusso in pixel del telefono: multipli di 8, 64-2560
                              (ph_fin_misura_flusso); ignorata per lo specchio */
    int densita;           /* dpi; 0 = quella del telefono */
} PhSrvFlussoRichiesta;

typedef enum {
    PH_SRV_FL_APERTO = 0, /* la sessione esiste; larghezza/altezza = misura */
    PH_SRV_FL_MISURA,     /* il flusso ha dichiarato una misura nuova (dopo un ridimensionamento
                             o da solo): l'input si calcola su questa (4.10) */
    PH_SRV_FL_PROTETTO,   /* schermo protetto acceso/spento (protetto): messaggio al posto del nero */
    PH_SRV_FL_APP_CHIUSA, /* l'app si e' chiusa da sola: la finestra va chiusa */
    PH_SRV_FL_PERSO,      /* connessione persa: ultima immagine sfocata + "Riconnetti ora" (5.5) */
    PH_SRV_FL_RIPRESO,    /* connessione tornata: la sessione e' ripartita da sola */
    PH_SRV_FL_ERRORE,     /* il flusso e' morto e non riparte (frase) */
} PhSrvFlussoEvento;

typedef struct {
    PhSrvFlussoEvento evento;
    int larghezza, altezza;   /* APERTO, MISURA */
    bool protetto;            /* PROTETTO */
    const char *frase;        /* ERRORE, PERSO (puo' essere NULL) */
} PhSrvFlussoDati;

typedef void (*PhSrvFlussoAvviso)(PhSrvFlusso *f, const PhSrvFlussoDati *d, void *utente);

/* Crea il flusso e fa partire l'apertura della sessione sul telefono (non
 * blocca). Gli avvisi arrivano sul thread GTK fino a ph_srv_flusso_chiudi.
 * NULL = errore (argomenti, troppi flussi). */
PhSrvFlusso *ph_srv_flusso_apri(PhServizi *s, const PhSrvFlussoRichiesta *r,
                                PhSrvFlussoAvviso avviso, void *utente, PhErrore *e);

/* SOLO dal thread di lettura del flusso (uno per flusso, della superficie).
 * Blocca al massimo timeout_ms. 1 = *out riempito (va rilasciato con
 * ph_fotogramma_rilascia), 0 = niente entro il tempo (in apertura, in pausa,
 * connessione persa: riprovare), -1 = flusso finito per sempre (interrotto o
 * chiuso): il thread esce. La decodifica avviene qui, sul thread di lettura. */
int ph_srv_flusso_leggi(PhSrvFlusso *f, PhFotogramma *out, int timeout_ms);

/* Qualunque thread: da qui in poi ph_srv_flusso_leggi torna -1 subito. */
void ph_srv_flusso_interrompi(PhSrvFlusso *f);

/* Qualunque thread: GTK ha rifiutato un DMA-BUF; da qui in poi il flusso
 * consegna fotogrammi in memoria (ripiego del prototipo). */
void ph_srv_flusso_preferisci_memoria(PhSrvFlusso *f);

/* Minimizza = pausa, l'app resta viva (4.6); la ripresa da' un fotogramma
 * chiave in ~0,1 s. Chiamarle due volte di fila non e' un errore. */
void ph_srv_flusso_pausa(PhSrvFlusso *f);
void ph_srv_flusso_riprendi(PhSrvFlusso *f);

/* Nuova misura a movimento assestato (4.5): multipli di 8, 64-2560. La misura
 * vera arriva con PH_SRV_FL_MISURA. Ignorata per lo specchio. */
void ph_srv_flusso_ridimensiona(PhSrvFlusso *f, int larghezza, int altezza);

/* Input (4.10). video_w/video_h = misura del video su cui sono calcolate le
 * coordinate (l'ultima dichiarata): se non coincide con quella del telefono
 * l'evento viene scartato. Coordinate gia' nello spazio del video, intere,
 * 0 <= x < video_w. Valori fuori misura: l'evento si scarta qui. */
enum { PH_SRV_TOCCO_GIU = 0, PH_SRV_TOCCO_MUOVI = 1, PH_SRV_TOCCO_SU = 2, PH_SRV_TOCCO_ANNULLA = 3 };
void ph_srv_flusso_tocco(PhSrvFlusso *f, int video_w, int video_h, int azione, int x, int y,
                         unsigned pulsanti);
/* orizzontale/verticale in 120esimi di scatto (uno scatto = 120), come il
 * protocollo e superficie.h. */
void ph_srv_flusso_rotella(PhSrvFlusso *f, int video_w, int video_h, int x, int y,
                           int orizzontale, int verticale);
/* codice = KeyEvent.KEYCODE_* di Android, meta = KeyEvent.META_* (la
 * traduzione dai tasti del PC e' della finestra). */
void ph_srv_flusso_tasto(PhSrvFlusso *f, bool giu, unsigned codice, unsigned meta);
/* Testo (anche non ASCII: il backend usa "incolla" se serve, 4.10). */
void ph_srv_flusso_testo(PhSrvFlusso *f, const char *testo);
void ph_srv_flusso_indietro(PhSrvFlusso *f);

/* Chiude (non blocca). Prima: ph_srv_flusso_interrompi e attesa della fine
 * del thread di lettura. togli_task = true per "Chiudi" (app tolta dai
 * recenti, 4.6). Dopo il ritorno nessun avviso e f non vale piu'. */
void ph_srv_flusso_chiudi(PhSrvFlusso *f, bool togli_task);

/* ======================================================================
 * AGGIUNTE del 23 nov 2026 (solo in coda: nessuna firma sopra e' cambiata).
 * Valgono le stesse REGOLE DI THREAD dell'inizio del file: tutto dal thread
 * GTK, nessuna chiamata blocca, ogni funzione di ritorno arriva sul thread GTK
 * in un giro successivo, mai dentro la chiamata. Se il backend non realizza
 * ancora una di queste funzioni il pezzo comune risponde da se' come scritto
 * funzione per funzione (mai silenzio: una risposta arriva sempre).
 * ====================================================================== */

/* ---- (1) Installazione e disinstallazione di app (5.9.1, 5.4) ----------
 * La CONFERMA dell'utente la mostra chi chiama, prima (per la disinstallazione
 * deve dire che si perdono anche i dati dell'app). Esito e messaggio in parole
 * semplici, gia' nella lingua attiva (src/trasferimenti/installa.h). */
typedef enum {
    PH_SRV_INST_RIUSCITO = 0,
    PH_SRV_INST_RIFIUTATO, /* il telefono ha detto di no: il messaggio dice perche' */
    PH_SRV_INST_ANNULLATO, /* annullata con ph_srv_installazione_annulla prima che fosse tardi */
    PH_SRV_INST_ERRORE,    /* non e' stato possibile (file, copia, connessione, nessun telefono) */
} PhSrvInstEsito;

typedef struct {
    PhSrvInstEsito esito;
    const char *messaggio;   /* sempre presente, da mostrare */
    const char *codice;      /* codice tecnico (es. INSTALL_FAILED_ALREADY_EXISTS) o "" */
    bool temporaneo_rimasto; /* un file temporaneo e' rimasto sul telefono (il messaggio lo dice) */
} PhSrvInstRisultato;

/* fatti/totale: byte copiati del file APK (totale 0 = ignoto). Al piu' ~10 al
 * secondo. Dopo la copia non arrivano piu' (pm lavora: puo' durare). */
typedef void (*PhSrvInstAvanzamento)(PhServizi *s, uint64_t fatti, uint64_t totale, void *utente);
/* Arriva SEMPRE, una volta sola, anche dopo un annullamento (con l'esito vero:
 * se era troppo tardi per annullare puo' essere RIUSCITO). r vale solo
 * durante la chiamata. */
typedef void (*PhSrvInstFatto)(PhServizi *s, const PhSrvInstRisultato *r, void *utente);

/* Installa un APK del PC (percorso assoluto). Torna un id > 0 per
 * l'annullamento (0 = argomenti non validi: la risposta ERRORE arriva lo
 * stesso). avanzamento puo' essere NULL. */
guint ph_srv_apk_installa(PhServizi *s, const char *apk_pc, PhSrvInstAvanzamento avanzamento,
                          PhSrvInstFatto fatto, void *utente);
/* Disinstalla l'app (nome del pacchetto: 1-255 caratteri [A-Za-z0-9_.], il
 * primo una lettera; altrimenti ERRORE senza toccare il telefono). Toglie
 * anche i dati dell'app. Torna un id > 0 (0 = nome non valido). */
guint ph_srv_app_disinstalla(PhServizi *s, const char *pacchetto, PhSrvInstFatto fatto,
                             void *utente);
/* Chiede di fermare l'installazione/disinstallazione `id` (vale durante la
 * copia e subito prima di pm). La risposta finale arriva comunque. id gia'
 * finito o sconosciuto: nulla. */
void ph_srv_installazione_annulla(PhServizi *s, guint id);

/* ---- (2) Dimensione di un file trovato dalla ricerca --------------------
 * Per «{cartella} · {dimensione}» del mockup. PhSrvTrovato resta com'era (i
 * pezzi lo riempiono per posizione): la dimensione si chiede con questa
 * funzione, SOLO dentro la PhSrvCercaTrovati, con un puntatore all'array
 * ricevuto (&v[i]). true e *byte riempito se il telefono l'ha detta; false
 * per le cartelle, se non nota, o se t non e' del gruppo in consegna. */
bool ph_srv_trovato_dimensione(PhServizi *s, const PhSrvTrovato *t, uint64_t *byte);

/* ---- (3) Annullare tutti i trasferimenti di UN verso -------------------
 * Le voci di quel verso in attesa o in corso diventano ANNULLATO (quella in
 * corso quando la copia si ferma); l'altro verso non si tocca. */
void ph_srv_tr_annulla_verso(PhServizi *s, PhSrvVerso verso);

/* ---- (4) Pannello fisico con esito (4.7, docs/protocollo.md §2.9) ------- */
typedef enum {
    PH_SRV_PAN_FATTO = 0,          /* il pannello e' ora come chiesto */
    PH_SRV_PAN_BLOCCATO,           /* telefono bloccato: non si spegne/accende ora */
    PH_SRV_PAN_CHIAMATA,           /* chiamata in arrivo o in corso: spegnere e' rifiutato */
    PH_SRV_PAN_RIPRISTINO_INCERTO, /* il ripristino sul telefono non e' al sicuro: per prudenza non si spegne */
    PH_SRV_PAN_NON_DISPONIBILE,    /* il telefono non lo permette (API mancante) */
    PH_SRV_PAN_NON_COLLEGATO,
    PH_SRV_PAN_ERRORE,             /* altro: la frase lo dice */
    PH_SRV_PAN_SENZA_ESITO,        /* comando mandato ma il backend non sa dire l'esito
                                      (backend che non realizza ancora pannello_chiedi) */
} PhSrvPannelloEsito;

/* frase: NULL o una frase per l'utente. Lo stato vero del pannello arriva
 * comunque con PH_SRV_EV_TELEFONO. */
typedef void (*PhSrvPannelloFatto)(PhServizi *s, PhSrvPannelloEsito esito, const char *frase,
                                   void *utente);
void ph_srv_pannello_chiedi(PhServizi *s, bool acceso, GCancellable *annulla,
                            PhSrvPannelloFatto fatto, void *utente);

/* ---- (5) Dati dei widget: memoria per categoria e calendario -----------
 * RISERVATEZZA (Decisioni di chi coordina): il calendario si chiede SOLO se
 * il widget Calendario e' sul desktop; titoli e orari non si scrivono mai su
 * disco (profilo, cache) ne' in nessun log, nemmeno nei messaggi d'errore. */
typedef struct {
    uint64_t byte;
    bool leggibile;       /* false: il telefono non ha potuto leggerla (byte = 0) */
} PhSrvVoceMemoria;

typedef struct {
    PhSrvVoceMemoria immagini, video, audio, app; /* app = codice, dati e cache */
} PhSrvMemoriaDettaglio;

/* d (NULL se esito != OK) vale solo durante la chiamata. */
typedef void (*PhSrvDettaglioFatto)(PhServizi *s, PhSrvEsito esito, const char *frase,
                                    const PhSrvMemoriaDettaglio *d, void *utente);
void ph_srv_tel_memoria_dettaglio(PhServizi *s, GCancellable *annulla, PhSrvDettaglioFatto fatto,
                                  void *utente);

#define PH_SRV_CALENDARIO_MAX 16
typedef struct {
    const char *titolo;   /* gia' sicuro da mostrare, una riga */
    uint64_t inizio_ms;   /* millisecondi dal 1970 */
    uint64_t fine_ms;     /* mai prima di inizio_ms */
    bool tutto_il_giorno; /* inizio e fine a mezzanotte UTC: si mostrano come giorni, senza fuso */
} PhSrvEventoCalendario;

/* I prossimi eventi (al massimo `massimo`, 1-PH_SRV_CALENDARIO_MAX; fuori
 * intervallo si limita), in ordine di inizio. Telefono che non lo permette:
 * esito ERRORE con la sua frase (mai un elenco vuoto finto). v vale solo
 * durante la chiamata. */
typedef void (*PhSrvCalendarioFatto)(PhServizi *s, PhSrvEsito esito, const char *frase,
                                     const PhSrvEventoCalendario *v, size_t n, void *utente);
void ph_srv_tel_calendario(PhServizi *s, unsigned massimo, GCancellable *annulla,
                           PhSrvCalendarioFatto fatto, void *utente);

/* ---- (6) Prima lettura delle notifiche dopo un collegamento ------------
 * Dopo ogni collegamento (anche una riconnessione) la PRIMA fotografia delle
 * notifiche e' la «base»: per chi mostra i toast quelle notifiche sono «gia'
 * presenti», non nuove. Quando quella prima fotografia e' applicata (anche se
 * vuota, o con un problema) la generazione aumenta di 1 e arriva il solito
 * PH_SRV_EV_NOTIFICHE. Uso: in ascolto di PH_SRV_EV_NOTIFICHE, se la
 * generazione e' cambiata dall'ultima volta questa fotografia e' la base. */
unsigned ph_srv_notifiche_generazione(PhServizi *s); /* 0 = nessuna prima lettura ancora */
/* true se la prima lettura del collegamento ATTUALE e' arrivata (false da
 * quando il telefono non e' collegato fino alla prossima prima lettura). */
bool ph_srv_notifiche_pronte(PhServizi *s);

/* ---- (7) Screenshot e registrazione di un flusso (5.5, src/video/cattura.h) */
/* Dove mettere lo screenshot (si possono unire con |). */
enum { PH_SRV_FOTO_FILE = 1, PH_SRV_FOTO_APPUNTI = 2 };

/* percorso: il file scritto (NULL se non richiesto o errore); png: i byte del
 * PNG per gli appunti (NULL se non richiesto o errore; g_bytes_ref per
 * tenerli). Non arriva se il flusso e' stato chiuso prima. */
typedef void (*PhSrvScreenshotFatto)(PhServizi *s, PhSrvEsito esito, const char *frase,
                                     const char *percorso, GBytes *png, void *utente);
/* Screenshot dell'ultima immagine decodificata del flusso. Con
 * PH_SRV_FOTO_FILE: file NUOVO "<cartella>/<app> AAAA-MM-GG HH.MM.SS.png" (con
 * " (2)", " (3)"... se esiste: mai sovrascrivere; cartella creata se manca,
 * un livello). nome_app: il nome da mostrare dell'app (ripulito dal backend).
 * Nessuna immagine ancora: ERRORE con frase. */
void ph_srv_flusso_screenshot(PhSrvFlusso *f, unsigned dove, const char *cartella_pc,
                              const char *nome_app, GCancellable *annulla,
                              PhSrvScreenshotFatto fatto, void *utente);

typedef enum {
    PH_SRV_REG_INIZIATA = 0, /* file creato (percorso); i dati arrivano dal prossimo fotogramma chiave */
    PH_SRV_REG_FINITA,       /* chiusa: percorso del file completo (frase NULL), oppure
                                incompleta/interrotta (frase lo dice), oppure nessun
                                fotogramma (percorso NULL: file vuoto cancellato) */
    PH_SRV_REG_ERRORE,       /* non e' partita (frase); niente file */
} PhSrvRegEvento;

typedef struct {
    PhSrvRegEvento evento;
    const char *percorso;
    const char *frase;
} PhSrvRegDati;

/* Arriva sul thread GTK; non arriva piu' dopo ph_srv_flusso_chiudi. */
typedef void (*PhSrvRegAvviso)(PhSrvFlusso *f, const PhSrvRegDati *d, void *utente);

/* Avvia la registrazione MP4 del flusso (pacchetti H.264 copiati senza
 * ricodifica; con_audio: anche l'audio di YASDE). File NUOVO
 * "<cartella>/<app> data ora.mp4" (stesse regole dello screenshot). Durante la
 * registrazione la misura del flusso non deve cambiare: un
 * ph_srv_flusso_ridimensiona viene ignorato (scritto su stderr) finche' non si
 * ferma. Gia' in registrazione: ERRORE. La connessione persa o la chiusura del
 * flusso fermano la registrazione (alla chiusura nessun avviso: il file resta
 * completo fino all'ultimo frammento). */
void ph_srv_flusso_registra(PhSrvFlusso *f, const char *cartella_pc, const char *nome_app,
                            bool con_audio, PhSrvRegAvviso avviso, void *utente);
/* Ferma: arriva PH_SRV_REG_FINITA. Non in registrazione: nulla. */
void ph_srv_flusso_registra_ferma(PhSrvFlusso *f);
/* true tra ph_srv_flusso_registra e l'avviso FINITA/ERRORE. */
bool ph_srv_flusso_registrando(PhSrvFlusso *f);

/* ---- (8) Fotogramma chiave a richiesta ---------------------------------
 * Chiede al telefono un fotogramma chiave (~0,1 s), per esempio alla fine
 * dello schermo protetto. In pausa o senza backend: nulla. */
void ph_srv_flusso_chiave(PhSrvFlusso *f);

/* ---- Densita' nel ridimensionamento (23 nov, PIANO.md «Finestre grandi:
 * modalita' tablet») ------------------------------------------------------
 * Come ph_srv_flusso_ridimensiona, con la densita' (dpi) dello schermo
 * virtuale: 0 = invariata (quella attuale del flusso); altrimenti 50-2000
 * (fuori intervallo: scartato, scritto su stderr). Un cambio di sola densita'
 * (stessa misura) e' un ridimensionamento valido: la misura e la densita'
 * vere tornano con PH_SRV_FL_MISURA. Stesse regole di prima: multipli di 8,
 * 64-2560; ignorato durante la registrazione e per lo specchio. Se il
 * backend non realizza la variante con la densita', il pezzo comune usa
 * quella vecchia e la densita' resta quella dell'apertura. */
void ph_srv_flusso_ridimensiona_densita(PhSrvFlusso *f, int larghezza, int altezza, int densita);

/* ---- Prima connessione: abbinamento con il codice (23 nov) -------------
 * (specifica 1.5, 2.4 «abbinamento con codice»; collegamento.h: stato
 * NON_ABBINATO, ph_col_abbina / ph_col_abbina_a.) Il codice e' quello a sei
 * cifre che Android mostra in «Debug wireless > Associa dispositivo con
 * codice». RISERVATEZZA: il codice, l'indirizzo e la porta non si scrivono
 * mai su disco ne' in nessun log (nemmeno nei messaggi d'errore); il pezzo
 * comune li copia, li passa al backend e cancella la propria copia. */
typedef enum {
    PH_SRV_ABB_RIUSCITO = 0,    /* il telefono ha accettato la chiave: il collegamento riparte da solo */
    PH_SRV_ABB_FALLITO,         /* codice sbagliato o scaduto, schermata del codice non aperta sul
                                   telefono, telefono non trovato: la frase lo dice */
    PH_SRV_ABB_RIFIUTATA,       /* richiesta non accettata ora: non si e' in NON_ABBINATO, o un
                                   abbinamento e' gia' in corso, o codice/indirizzo non validi */
    PH_SRV_ABB_SCADUTA,         /* nessun esito entro PH_SRV_ABB_LIMITE_S secondi */
    PH_SRV_ABB_NON_DISPONIBILE, /* il backend non sa abbinare */
} PhSrvAbbEsito;

#define PH_SRV_ABB_LIMITE_S 60 /* attesa massima dell'esito (scoperta + abbinamento) */

/* frase: NULL o una frase per l'utente (gia' tradotta, senza codice ne'
 * indirizzi). Arriva una volta sola, sul thread GTK. Lo stato del
 * collegamento arriva comunque con PH_SRV_EV_COLLEGAMENTO (riuscito: CERCO
 * con il motivo «abbinamento riuscito», poi COLLEGATO). */
typedef void (*PhSrvAbbFatto)(PhServizi *s, PhSrvAbbEsito esito, const char *frase, void *utente);

/* true se `codice` e' esattamente di 6 cifre ASCII (niente spazi). */
bool ph_srv_codice_valido(const char *codice);

/* Chiede l'abbinamento. host == NULL: il servizio di abbinamento lo trova il
 * collegamento con la scoperta in rete (caso normale). host/porta: quelli che
 * il telefono mostra sotto il codice («indirizzo IP e porta»), per quando la
 * scoperta non lo vede; host = IPv4/IPv6 in testo (al massimo 63 caratteri),
 * porta 1-65535. Annullando dal thread GTK la risposta non arriva, ma
 * l'abbinamento gia' partito NON si ferma (collegamento.h non lo permette):
 * il suo effetto si vede dallo stato del collegamento. */
void ph_srv_abbina(PhServizi *s, const char *codice, const char *host, int porta, GCancellable *annulla,
                   PhSrvAbbFatto fatto, void *utente);
/* true tra ph_srv_abbina e il suo esito (anche se annullata). */
bool ph_srv_abbinamento_in_corso(PhServizi *s);

/* ---- Widget aggiunti il 23 nov pomeriggio (PIANO.md «Richieste ...
 * pomeriggio»; protocollo §2.6 e §2.10) ------------------------------------
 * Riservatezza: titoli degli eventi, nomi e numeri mai su disco ne' nei log.
 * Se il backend non realizza una voce, la risposta e' ERRORE (frase NULL) e
 * il widget dice «non disponibile». */

/* Calendario del mese: eventi che si sovrappongono a [da_ms, a_ms) (al massimo
 * PH_SRV_CALENDARIO_GIORNI_MAX giorni; oltre: ERRORE senza chiedere). v e
 * app_calendario (pacchetto da aprire con un clic, "" se nessuno) valgono
 * solo durante la chiamata. Chiedere SOLO se il widget e' sul desktop. */
#define PH_SRV_CALENDARIO_GIORNI_MAX 62
#define PH_SRV_CALENDARIO_INTERVALLO_MAX 500
typedef void (*PhSrvCalendarioMeseFatto)(PhServizi *s, PhSrvEsito esito, const char *frase,
                                         const PhSrvEventoCalendario *v, size_t n, bool troncato,
                                         const char *app_calendario, void *utente);
void ph_srv_tel_calendario_intervallo(PhServizi *s, uint64_t da_ms, uint64_t a_ms, GCancellable *annulla,
                                      PhSrvCalendarioMeseFatto fatto, void *utente);

/* Prossima sveglia. presente false: nessuna sveglia; pacchetto "" se non noto. */
typedef void (*PhSrvSvegliaFatta)(PhServizi *s, PhSrvEsito esito, const char *frase, bool presente,
                                  uint64_t ora_ms, const char *pacchetto, void *utente);
void ph_srv_tel_sveglia(PhServizi *s, GCancellable *annulla, PhSrvSvegliaFatta fatto, void *utente);

/* Batteria in dettaglio (stessi significati di BATTERIA nel protocollo). */
typedef enum {
    PH_SRV_BAT_SCONOSCIUTO = 0,
    PH_SRV_BAT_IN_CARICA,
    PH_SRV_BAT_IN_SCARICA,
    PH_SRV_BAT_NON_IN_CARICA,
    PH_SRV_BAT_PIENA,
} PhSrvBatStato;
enum { PH_SRV_ALIM_RETE = 1, PH_SRV_ALIM_USB = 2, PH_SRV_ALIM_SENZA_FILI = 4, PH_SRV_ALIM_BASE = 8 };
typedef enum {
    PH_SRV_SALUTE_SCONOSCIUTA = 0,
    PH_SRV_SALUTE_BUONA,
    PH_SRV_SALUTE_SURRISCALDATA,
    PH_SRV_SALUTE_GUASTA,
    PH_SRV_SALUTE_SOVRATENSIONE,
    PH_SRV_SALUTE_FREDDA,
    PH_SRV_SALUTE_ERRORE,
} PhSrvSalute;
typedef struct {
    int livello;              /* 0-100 */
    PhSrvBatStato stato;
    unsigned alimentazione;   /* PH_SRV_ALIM_* uniti con |, 0 = nessuna */
    bool temperatura_nota;
    int temperatura_decimi;   /* decimi di grado C */
    PhSrvSalute salute;
    int tensione_mv;          /* 0 = non nota */
    int minuti_carica;        /* -1 = non noto */
    int minuti_autonomia;     /* -1 = non noto */
} PhSrvBatteria;
typedef void (*PhSrvBatteriaFatta)(PhServizi *s, PhSrvEsito esito, const char *frase, const PhSrvBatteria *b,
                                   void *utente);
void ph_srv_tel_batteria(PhServizi *s, GCancellable *annulla, PhSrvBatteriaFatta fatto, void *utente);

/* Interruttori rapidi e trova il telefono: fotografia + PH_SRV_EV_CONTROLLI.
 * La fotografia si aggiorna solo mentre qualcuno OSSERVA (il widget e' sul
 * desktop): ph_srv_controlli_osserva(s, true) a ogni widget aggiunto, false a
 * ogni widget tolto (contatore: si smette quando tornano a zero). */
#define PH_SRV_NON_DISPONIBILE (-1)
enum { PH_SRV_SUONERIA_SUONA = 0, PH_SRV_SUONERIA_VIBRA = 1, PH_SRV_SUONERIA_SILENZIOSO = 2 };
typedef struct {
    bool noto;               /* false finche' il telefono non ha risposto */
    int suoneria;            /* PH_SRV_SUONERIA_* o PH_SRV_NON_DISPONIBILE */
    int non_disturbare;      /* 0 / 1 / PH_SRV_NON_DISPONIBILE */
    int torcia;              /* 0 / 1 / PH_SRV_NON_DISPONIBILE */
    bool trova_in_corso;
    bool puo_suoneria, puo_non_disturbare, puo_torcia, puo_trova; /* false: «non disponibile» */
} PhSrvControlli;
void ph_srv_controlli(PhServizi *s, PhSrvControlli *out);
void ph_srv_controlli_osserva(PhServizi *s, bool osserva);

typedef enum {
    PH_SRV_CTL_SUONERIA = 0,   /* valore PH_SRV_SUONERIA_* */
    PH_SRV_CTL_NON_DISTURBARE, /* valore 0/1 */
    PH_SRV_CTL_TORCIA,         /* valore 0/1 */
} PhSrvControllo;
/* Comando dell'utente. Suoneria e non disturbare restano come li mette
 * l'utente anche dopo la chiusura (effetto voluto, nessun ripristino); la
 * torcia accesa da YASDE si spegne alla chiusura. Esito con PhSrvFatto;
 * lo stato vero arriva con PH_SRV_EV_CONTROLLI. Valore fuori intervallo:
 * ERRORE senza mandare nulla. */
void ph_srv_controllo_imposta(PhServizi *s, PhSrvControllo controllo, int valore, GCancellable *annulla,
                              PhSrvFatto fatto, void *utente);
/* Trova il telefono: NON disponibile in questa versione (decisione del
 * committente, protocollo §2.10): il telefono risponde sempre ERRORE «non
 * disponibile su questa versione» e puo_trova e' sempre falso. Le funzioni
 * restano per una versione futura. */
void ph_srv_trova_avvia(PhServizi *s, int secondi, GCancellable *annulla, PhSrvFatto fatto, void *utente);
void ph_srv_trova_ferma(PhServizi *s);

/* Trasferimenti recenti (widget): gli ultimi PH_SRV_RECENTI_MAX trasferimenti
 * COMPLETATI in questa esecuzione, il piu' recente per primo, tenuti dal pezzo
 * comune anche se la coda li toglie (ph_srv_tr_rimuovi_finiti). Solo in
 * memoria (nomi di file: mai su disco). Cambiano con PH_SRV_EV_TRASFERIMENTI;
 * si svuotano a PH_SRV_EV_PROFILO (un altro telefono). Puntatore valido fino
 * al ritorno al ciclo principale. */
#define PH_SRV_RECENTI_MAX 20
const PhSrvTrasferimento *ph_srv_trasferimenti_recenti(PhServizi *s, size_t *n);

/* ---- Pizzico (23 nov: Ctrl + rotella = zoom, docs/protocollo.md §2.4) -----
 * Gesto a due dita che il TELEFONO genera attorno a (x, y) (coordinate del
 * video, come ph_srv_flusso_tocco). fase PH_SRV_PIZZICO_*; passo in 120esimi
 * (positivo = ingrandisci; uno scatto di rotella = 120), limitato a
 * -1200..1200. Argomenti non validi: scartato qui. Il telefono chiude da solo
 * un gesto senza passi da 400 ms; chi lo apre manda comunque FINE. Se il
 * backend non lo realizza: scartato (scritto una volta su stderr). */
enum { PH_SRV_PIZZICO_INIZIO = 0, PH_SRV_PIZZICO_CONTINUA = 1, PH_SRV_PIZZICO_FINE = 2,
       PH_SRV_PIZZICO_ANNULLA = 3 };
void ph_srv_flusso_pizzico(PhSrvFlusso *f, int video_w, int video_h, int fase, int x, int y, int passo);

/* ---- Meteo (23 nov sera; src/meteo/meteo.h; CONTRATTI.md §18) -----------
 * L'UNICO collegamento a Internet dell'app. Si accende SOLO mentre almeno un
 * widget Meteo e' sul desktop (nessun dialogo di consenso: aggiungere il
 * widget basta, decisione del committente del 23 nov notte): ogni widget
 * Meteo chiama ph_srv_meteo_osserva(true) e, alla rimozione, false
 * (contatore: si spegne a zero). La posizione del telefono non arriva mai
 * alla shell: la usa il backend per chiedere le previsioni. Citta' scelta a
 * mano e ricerca dei luoghi: in fondo al file. */
typedef enum {
    PH_SRV_METEO_SPENTO = 0,     /* nessun osservatore */
    PH_SRV_METEO_IN_ATTESA,      /* prima risposta non ancora arrivata */
    PH_SRV_METEO_PRONTO,         /* dati freschi */
    PH_SRV_METEO_NON_DISPONIBILE,/* il servizio non risponde: dati vecchi se ci sono (ha_dati) */
    PH_SRV_METEO_SENZA_POSIZIONE,/* il telefono non da' una posizione (frase dice perche') */
    PH_SRV_METEO_NON_COLLEGATO,  /* nessun telefono: niente posizione (dati vecchi se ci sono) */
} PhSrvMeteoStato;

typedef struct {
    PhSrvMeteoStato stato;
    char frase[192];             /* per l'utente, MAI con coordinate */
    char localita[128];          /* nome della localita' (dal telefono o dalla citta' scelta), "" se non noto */
    bool ha_dati;                /* dati validi (anche vecchi: dati.ricevuta dice quando) */
    PhMeteoPrevisione dati;
} PhSrvMeteo;

/* Fotografia (thread GTK; cambia con PH_SRV_EV_METEO). */
const PhSrvMeteo *ph_srv_meteo(PhServizi *s);
void ph_srv_meteo_osserva(PhServizi *s, bool osserva);

/* ---- Meteo: citta' scelta a mano e ricerca dei luoghi (23 nov notte) -----
 * Precedenza (CONTRATTI.md §18): la posizione del TELEFONO vince sempre; la
 * citta' scelta vale SOLO quando il telefono non da' una posizione (spenta,
 * permesso negato, nessuna posizione nota, nessun telefono collegato). Con la
 * citta' scelta in uso PhSrvMeteo.localita e' il suo nome; senza posizione
 * ne' citta' lo stato e' SENZA_POSIZIONE (o NON_COLLEGATO). */
typedef enum {
    PH_SRV_METEO_FONTE_NESSUNA = 0, /* nessun dato ancora / nessuna posizione */
    PH_SRV_METEO_FONTE_TELEFONO,    /* previsioni per la posizione del telefono */
    PH_SRV_METEO_FONTE_A_MANO,      /* previsioni per la citta' scelta dall'utente */
} PhSrvMeteoFonte;
/* Da che posizione vengono i dati dell'ultima fotografia (thread GTK; cambia
 * con PH_SRV_EV_METEO). Separata da PhSrvMeteo per non cambiarne la forma. */
PhSrvMeteoFonte ph_srv_meteo_fonte(PhServizi *s);

/* Citta' scelta a mano (thread GTK; NULL = nessuna). La conserva il PROFILO
 * (config, campi meteo_luogo_*: chi la sceglie li scrive e chiama
 * ph_srv_profilo_salva); questa funzione la comunica al backend e va chiamata
 * dal desktop dopo la scelta, dopo la rimozione e a ogni profilo caricato
 * (PH_SRV_EV_PROFILO e all'avvio). Coordinate fuori limite o nome vuoto =
 * ignorata (come NULL). Copiata. Se il meteo e' osservato e la citta' e' ora
 * quella in uso, si aggiorna subito (sempre con le regole di frequenza). */
void ph_srv_meteo_localita(PhServizi *s, const PhMeteoLuogo *l);

/* Ricerca dei luoghi per nome (menu «Località…» del widget): SOLO per
 * un'azione esplicita dell'utente (pulsante o Invio), mai a ogni tasto.
 * lingua: "it" o "en" (la lingua della shell). Una sola chiamata finale:
 * esito OK con v[0..n) (n puo' essere 0: «nessun luogo trovato»), oppure
 * ERRORE con frase (mai il testo cercato) e v NULL, oppure niente se
 * annullata. v vale durante la chiamata. Testo non valido (meno di 2 o piu'
 * di PH_METEO_RICERCA_TESTO_MAX byte dopo aver tolto gli spazi) o meteo
 * non osservato (nessun widget Meteo: §18) = ERRORE senza rete e senza
 * chiamare il backend. */
typedef void (*PhSrvMeteoCercaFatta)(PhServizi *s, PhSrvEsito esito, const char *frase, const PhMeteoLuogo *v,
                                     size_t n, void *utente);
void ph_srv_meteo_cerca(PhServizi *s, const char *testo, const char *lingua, GCancellable *annulla,
                        PhSrvMeteoCercaFatta fatto, void *utente);

#endif
