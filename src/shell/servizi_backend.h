/* Come si scrive una realizzazione dei servizi (servizi.h). Lo includono SOLO
 * il backend finto (src/shell/finto/) e quello vero (src/shell/ossatura/):
 * nessun altro pezzo.
 *
 * Il backend da' una tabella di funzioni (PhServiziBackend); il pezzo comune
 * (src/shell/comune/servizi.c) la avvolge in un PhServizi e pensa a: avvisi
 * sul thread GTK, fotografie dello stato, cache delle icone, annullamenti,
 * salvataggio differito del profilo, vita dei flussi.
 *
 * THREAD
 *  - Le funzioni della tabella sono chiamate dal thread GTK (tranne
 *    flusso_leggi, dal thread di lettura, e flusso_interrompi /
 *    flusso_memoria, da qualunque thread) e NON devono bloccare: il lavoro va
 *    su thread del backend.
 *  - Le funzioni ph_servizi_* qui sotto si chiamano da QUALUNQUE thread del
 *    backend (anche dal thread GTK): sono loro a consegnare al thread GTK.
 *  - Mai chiamare una ph_servizi_* tenendo un lucchetto che il thread GTK puo'
 *    aspettare (la consegna e' asincrona, ma restare prudenti).
 *
 * LAVORI. Ogni richiesta con risposta arriva con un PhSrvLavoro*: il backend
 * lo chiude con ESATTAMENTE una chiamata finale (ph_servizi_lavoro_*; per la
 * ricerca: zero o piu' ph_servizi_lavoro_trovati e poi una
 * ph_servizi_lavoro_cerca_fine). Dopo la chiamata finale il lavoro non si
 * tocca piu'. ph_servizi_lavoro_annullato dice se chi ha chiesto ha
 * rinunciato (si puo' smettere prima, ma la chiamata finale va fatta lo
 * stesso). */
#ifndef PH_SHELL_SERVIZI_BACKEND_H
#define PH_SHELL_SERVIZI_BACKEND_H

#include <stdatomic.h>

#include "shell/servizi.h"

typedef struct PhSrvLavoro PhSrvLavoro;

typedef enum {
    PH_SRV_IN_TOCCO = 0,
    PH_SRV_IN_ROTELLA,
    PH_SRV_IN_TASTO,
    PH_SRV_IN_TESTO,
    PH_SRV_IN_INDIETRO,
} PhSrvInputTipo;

/* Un evento di input gia' controllato dal pezzo comune (coordinate dentro il
 * video, testo UTF-8 valido non vuoto). testo vale solo durante la chiamata. */
typedef struct {
    PhSrvInputTipo tipo;
    int video_w, video_h;
    int azione;           /* PH_SRV_TOCCO_* */
    int x, y;
    unsigned pulsanti;
    int orizzontale, verticale;
    bool giu;
    unsigned codice, meta;
    const char *testo;
} PhSrvInput;

typedef struct {
    /* Comandi (thread GTK, non bloccano). */
    void (*riconnetti_ora)(void *b);
    void (*pannello)(void *b, bool acceso);
    void (*volume)(void *b, int volume);
    void (*muto)(void *b, bool muto);
    void (*appunti_imposta)(void *b, const char *testo);

    /* Icona: rispondere (anche molto dopo, o mai) con ph_servizi_pubblica_icona. */
    void (*icona_chiedi)(void *b, const char *pacchetto, int lato);

    /* Richieste con lavoro (thread GTK, non bloccano). Le stringhe valgono
     * solo durante la chiamata. */
    void (*tel_elenca)(void *b, PhSrvLavoro *l, const char *cartella);
    void (*tel_cerca)(void *b, PhSrvLavoro *l, const char *cartella, const char *testo,
                      bool solo_cartelle, unsigned massimo);
    void (*tel_crea_cartella)(void *b, PhSrvLavoro *l, const char *percorso);
    void (*tel_miniatura)(void *b, PhSrvLavoro *l, const char *percorso, int lato);
    void (*tel_leggi)(void *b, PhSrvLavoro *l, const char *percorso, size_t massimo);
    void (*tel_memoria)(void *b, PhSrvLavoro *l);

    /* Trasferimenti (thread GTK, non bloccano). */
    int (*tr_accoda)(void *b, PhSrvVerso verso, const char *origine, const char *cartella,
                     PhErrore *e);
    void (*tr_annulla)(void *b, uint64_t id);
    void (*tr_annulla_tutto)(void *b);
    void (*tr_rimuovi_finiti)(void *b);

    /* Persistenza: la copia e' del backend (la libera lui, con
     * ph_cfg_telefono_libera / free), che la scrive su un suo thread. */
    void (*profilo_salva)(void *b, PhCfgTelefono *copia);
    void (*preferenze_salva)(void *b, PhCfgPreferenze *copia);

    /* Flussi. flusso_apri torna lo stato del backend per quel flusso (NULL =
     * errore con la frase in e); la richiesta vale solo durante la chiamata.
     * flusso_chiudi lo libera (dopo flusso_interrompi e la fine del thread di
     * lettura) e da li' in poi il backend non chiama piu' ph_servizi_flusso_avviso
     * per quel flusso. */
    void *(*flusso_apri)(void *b, PhSrvFlusso *f, const PhSrvFlussoRichiesta *r, PhErrore *e);
    int (*flusso_leggi)(void *b, void *fl, PhFotogramma *out, int timeout_ms);
    void (*flusso_interrompi)(void *b, void *fl);
    void (*flusso_memoria)(void *b, void *fl);
    void (*flusso_pausa)(void *b, void *fl, bool pausa);
    void (*flusso_ridimensiona)(void *b, void *fl, int larghezza, int altezza);
    void (*flusso_input)(void *b, void *fl, const PhSrvInput *in);
    void (*flusso_chiudi)(void *b, void *fl, bool togli_task);

    /* Fine: ferma ogni thread del backend e libera b. Chiamata dal thread GTK da
     * ph_servizi_libera, dopo che tutti i flussi sono stati chiusi. Dopo il
     * ritorno il backend non chiama piu' nessuna ph_servizi_*. */
    void (*chiudi)(void *b);

    /* ---- AGGIUNTE del 23 nov 2026: tutte FACOLTATIVE (NULL = il pezzo
     * comune risponde da se', vedi servizi.h). Stesse regole sopra: dal
     * thread GTK, non bloccano. ---- */
    /* (1) Lavori di installazione: avanzamento con ph_servizi_lavoro_avanzamento,
     * fine con ph_servizi_lavoro_inst. Il flag d'annullamento da passare a
     * ph_inst_installa e' ph_servizi_lavoro_flag_annulla(l). */
    void (*apk_installa)(void *b, PhSrvLavoro *l, const char *apk_pc);
    void (*app_disinstalla)(void *b, PhSrvLavoro *l, const char *pacchetto);
    /* (3) */
    void (*tr_annulla_verso)(void *b, PhSrvVerso verso);
    /* (4) fine con ph_servizi_lavoro_pannello */
    void (*pannello_chiedi)(void *b, PhSrvLavoro *l, bool acceso);
    /* (5) fine con ph_servizi_lavoro_dettaglio / ph_servizi_lavoro_calendario;
     * massimo gia' limitato a 1-PH_SRV_CALENDARIO_MAX. */
    void (*tel_memoria_dettaglio)(void *b, PhSrvLavoro *l);
    void (*tel_calendario)(void *b, PhSrvLavoro *l, unsigned massimo);
    /* (7) Screenshot: fine con ph_servizi_lavoro_screenshot (dove = PH_SRV_FOTO_*).
     * Registrazione: avvia = true (cartella, app, con_audio validi) / false
     * (ferma: gli altri argomenti NULL); gli avvisi con
     * ph_servizi_flusso_registrazione. */
    void (*flusso_screenshot)(void *b, void *fl, PhSrvLavoro *l, unsigned dove, const char *cartella_pc,
                              const char *nome_app);
    void (*flusso_registra)(void *b, void *fl, bool avvia, const char *cartella_pc, const char *nome_app,
                            bool con_audio);
    /* (8) */
    void (*flusso_chiave)(void *b, void *fl);
    /* Densita' nel ridimensionamento (facoltativa: NULL = flusso_ridimensiona,
     * densita' ignorata). densita' 0 = invariata, altrimenti gia' controllata
     * (50-2000); misura gia' controllata come per flusso_ridimensiona. */
    void (*flusso_ridimensiona_densita)(void *b, void *fl, int larghezza, int altezza, int densita);
    /* Prima connessione (facoltativa: NULL = PH_SRV_ABB_NON_DISPONIBILE).
     * codice (6 cifre, gia' controllato) e host (NULL o gia' controllato)
     * valgono SOLO durante la chiamata: il backend li copia, li usa una volta,
     * li cancella (explicit_bzero) e non li scrive mai nei log. Fine con
     * ph_servizi_lavoro_abbina, al piu' entro PH_SRV_ABB_LIMITE_S secondi
     * (scaduto: PH_SRV_ABB_SCADUTA). */
    void (*abbina)(void *b, PhSrvLavoro *l, const char *codice, const char *host, int porta);
    /* Widget del 23 nov pomeriggio (tutte facoltative). Fine con
     * ph_servizi_lavoro_calendario_intervallo / _sveglia / _batteria;
     * controllo_imposta e trova_avvia con ph_servizi_lavoro_fatto (frase del
     * telefono per l'utente). controlli_osserva: true = comincia a leggere lo
     * stato (CONTROLLI_STATO + ascolto dell'evento 0x8e) e a pubblicarlo con
     * ph_servizi_pubblica_controlli; false = smetti. Chiamata solo ai passaggi
     * 0 -> 1 e 1 -> 0 del contatore degli osservatori. Argomenti gia'
     * controllati dal pezzo comune. */
    void (*tel_calendario_intervallo)(void *b, PhSrvLavoro *l, uint64_t da_ms, uint64_t a_ms);
    void (*tel_sveglia)(void *b, PhSrvLavoro *l);
    void (*tel_batteria)(void *b, PhSrvLavoro *l);
    void (*controlli_osserva)(void *b, bool osserva);
    void (*controllo_imposta)(void *b, PhSrvLavoro *l, PhSrvControllo controllo, int valore);
    void (*trova_avvia)(void *b, PhSrvLavoro *l, int secondi);
    void (*trova_ferma)(void *b);
    /* Pizzico (facoltativa: NULL = scartato). Argomenti gia' controllati:
     * coordinate dentro il video, fase 0-3, passo -1200..1200. Non blocca
     * (come flusso_input). */
    void (*flusso_pizzico)(void *b, void *fl, int video_w, int video_h, int fase, int x, int y, int passo);
    /* Meteo (facoltativa: NULL = il pezzo comune pubblica NON_DISPONIBILE).
     * Chiamata solo ai passaggi 0 -> 1 (true) e 1 -> 0 (false) degli
     * osservatori. Con true: posizione dal telefono (ph_dati_posizione: torna
     * subito; con ricerca_avviata richiederla UNA volta dopo ~15 s), poi
     * ph_meteo_chiedi, poi ph_servizi_pubblica_meteo; finche' osservato
     * richiamare ph_meteo_chiedi ogni 5 minuti (il pezzo meteo non ritenta da
     * solo: e' lui a decidere se collegarsi davvero, al piu' ogni
     * PH_METEO_INTERVALLO_S, e quando riprovare dopo un errore). UN SOLO
     * PhMeteo per processo (i suoi tetti valgono per oggetto); con false: smettere (annullare
     * la richiesta in volo, nessun collegamento finche' non torna true). */
    void (*meteo_osserva)(void *b, bool osserva);
    /* Citta' scelta a mano (facoltativa; 23 nov notte): copia gia' controllata
     * (nome non vuoto, coordinate nei limiti) o NULL = nessuna; valida solo
     * durante la chiamata. Non blocca. Il backend la usa SOLO quando il
     * telefono non da' una posizione e allora pubblica fonte A_MANO e
     * localita = l->nome (ph_servizi_pubblica_meteo_fonte). Mai nei log. */
    void (*meteo_localita)(void *b, const PhMeteoLuogo *l);
    /* Ricerca dei luoghi (facoltativa: NULL = ERRORE «non disponibile»).
     * Testo gia' ripulito (2..PH_METEO_RICERCA_TESTO_MAX byte), lingua "it"
     * o "en"; chiamata solo mentre il meteo e' osservato. Chiude il lavoro con ph_servizi_lavoro_luoghi o
     * ph_servizi_lavoro_fatto (errore). Testo mai nei log. */
    void (*meteo_cerca)(void *b, PhSrvLavoro *l, const char *testo, const char *lingua);
} PhServiziBackend;

/* Crea i servizi sopra un backend (thread GTK). La tabella si copia. Il
 * profilo parte coi valori predefiniti, le preferenze pure finche' il backend
 * non pubblica quelle lette dal disco. */
PhServizi *ph_servizi_nuovi(const PhServiziBackend *tabella, void *b);
/* Thread GTK, alla fine: chiama tabella->chiudi e poi libera (gli avvisi
 * ancora in coda vengono scartati). I flussi vanno chiusi prima. */
void ph_servizi_libera(PhServizi *s);

/* ---- Pubblicazione delle fotografie (qualunque thread) ----------------- */
void ph_servizi_pubblica_collegamento(PhServizi *s, const PhSrvCollegamento *c);
void ph_servizi_pubblica_telefono(PhServizi *s, const PhSrvTelefono *t);
void ph_servizi_pubblica_volume(PhServizi *s, const PhSrvVolume *v);
/* Prende la proprieta' dell'array e delle stringhe (malloc/strdup). */
void ph_servizi_pubblica_app(PhServizi *s, PhSrvApp *app, size_t n);
/* problema: NULL o frase (copiata). Prende la proprieta' dell'array. */
void ph_servizi_pubblica_notifiche(PhServizi *s, PhSrvNotifica *v, size_t n,
                                   const char *problema);
void ph_servizi_pubblica_trasferimenti(PhServizi *s, PhSrvTrasferimento *v, size_t n,
                                       bool coda_ferma);
/* Byte PNG dell'icona (ostili: decodificati qui, nel thread che chiama, con un
 * tetto; un PNG non valido si scarta). */
void ph_servizi_pubblica_icona(PhServizi *s, const char *pacchetto, const void *png, size_t n);
void ph_servizi_pubblica_appunti(PhServizi *s, const char *testo);
/* Profilo di un telefono appena collegato (proprieta' passata) e preferenze
 * lette dal disco (copiate). */
void ph_servizi_pubblica_profilo(PhServizi *s, PhCfgTelefono *profilo);
void ph_servizi_pubblica_preferenze(PhServizi *s, const PhCfgPreferenze *p);

/* ---- Chiusura dei lavori (qualunque thread) ---------------------------- */
bool ph_servizi_lavoro_annullato(PhSrvLavoro *l);
/* Prende la proprieta' dell'array di voci e delle stringhe. frase puo' essere NULL. */
void ph_servizi_lavoro_elenco(PhSrvLavoro *l, PhSrvEsito esito, const char *frase,
                              PhSrvVoce *voci, size_t n);
/* Non finale: un gruppo di risultati (copiati). */
void ph_servizi_lavoro_trovati(PhSrvLavoro *l, const PhSrvTrovato *v, size_t n);
void ph_servizi_lavoro_cerca_fine(PhSrvLavoro *l, PhSrvEsito esito, const char *frase,
                                  bool massimo);
void ph_servizi_lavoro_fatto(PhSrvLavoro *l, PhSrvEsito esito, const char *frase);
/* Byte di un'immagine (PNG o JPEG, ostili: decodificati qui con un tetto). */
void ph_servizi_lavoro_immagine(PhSrvLavoro *l, PhSrvEsito esito, const char *frase,
                                const void *dati, size_t n);
/* Prende un riferimento a byte (puo' essere NULL se esito != OK). */
void ph_servizi_lavoro_byte(PhSrvLavoro *l, PhSrvEsito esito, const char *frase, GBytes *byte);
void ph_servizi_lavoro_memoria(PhSrvLavoro *l, PhSrvEsito esito, uint64_t totale,
                               uint64_t libera);

/* ---- Avvisi dei flussi (qualunque thread) ------------------------------ */
void ph_servizi_flusso_avviso(PhSrvFlusso *f, const PhSrvFlussoDati *d);

/* ---- AGGIUNTE del 23 nov 2026 (qualunque thread) ----------------------- */
/* (2) Come ph_servizi_lavoro_trovati, con la dimensione dei file:
 * dimensioni[i] = byte, oppure UINT64_MAX se non nota (e per le cartelle). */
void ph_servizi_lavoro_trovati_dimensioni(PhSrvLavoro *l, const PhSrvTrovato *v, const uint64_t *dimensioni,
                                          size_t n);
/* (1) Non finale: byte copiati dell'APK (accorpato dal pezzo comune). */
void ph_servizi_lavoro_avanzamento(PhSrvLavoro *l, uint64_t fatti, uint64_t totale);
/* (1) Flag che diventa true quando chi ha chiesto annulla
 * (ph_srv_installazione_annulla) o i servizi si chiudono: da passare come
 * `annulla` a ph_inst_installa. Valido fino alla chiamata finale del lavoro. */
const atomic_bool *ph_servizi_lavoro_flag_annulla(PhSrvLavoro *l);
/* (1) Finale. r copiato (messaggio e codice compresi). */
void ph_servizi_lavoro_inst(PhSrvLavoro *l, const PhSrvInstRisultato *r);
/* (4) Finale. */
void ph_servizi_lavoro_pannello(PhSrvLavoro *l, PhSrvPannelloEsito esito, const char *frase);
/* (5) Finali. d copiato (puo' essere NULL se esito != OK). Gli eventi: array e
 * titoli copiati; i titoli si ripuliscono per lo schermo qui (mai scritti nei
 * log). */
void ph_servizi_lavoro_dettaglio(PhSrvLavoro *l, PhSrvEsito esito, const char *frase,
                                 const PhSrvMemoriaDettaglio *d);
void ph_servizi_lavoro_calendario(PhSrvLavoro *l, PhSrvEsito esito, const char *frase,
                                  const PhSrvEventoCalendario *v, size_t n);
/* (7) Finale. percorso e png facoltativi (png: si prende un riferimento). */
void ph_servizi_lavoro_screenshot(PhSrvLavoro *l, PhSrvEsito esito, const char *frase,
                                  const char *percorso, GBytes *png);
/* (7) Avviso della registrazione del flusso. Come per ph_servizi_flusso_avviso:
 * mai dopo il ritorno di flusso_chiudi del backend. FINITA ed ERRORE chiudono
 * lo stato "registrando". */
void ph_servizi_flusso_registrazione(PhSrvFlusso *f, const PhSrvRegDati *d);
/* Widget del 23 nov pomeriggio. Finali; dati copiati (titoli ripuliti qui). */
void ph_servizi_lavoro_calendario_intervallo(PhSrvLavoro *l, PhSrvEsito esito, const char *frase,
                                             const PhSrvEventoCalendario *v, size_t n, bool troncato,
                                             const char *app_calendario);
void ph_servizi_lavoro_sveglia(PhSrvLavoro *l, PhSrvEsito esito, const char *frase, bool presente,
                               uint64_t ora_ms, const char *pacchetto);
void ph_servizi_lavoro_batteria(PhSrvLavoro *l, PhSrvEsito esito, const char *frase, const PhSrvBatteria *b);
/* Fotografia del meteo (qualunque thread; copiata; frase senza coordinate). */
void ph_servizi_pubblica_meteo(PhServizi *s, const PhSrvMeteo *m);
/* Come ph_servizi_pubblica_meteo, con la fonte dei dati (23 nov notte); la
 * forma senza fonte pubblica FONTE_TELEFONO se ha_dati, NESSUNA altrimenti. */
void ph_servizi_pubblica_meteo_fonte(PhServizi *s, const PhSrvMeteo *m, PhSrvMeteoFonte fonte);
/* Risultati della ricerca dei luoghi (chiamata finale; copiati e ripuliti:
 * nome non vuoto, coordinate nei limiti, al massimo PH_METEO_LUOGHI_MAX). */
void ph_servizi_lavoro_luoghi(PhSrvLavoro *l, PhSrvEsito esito, const char *frase, const PhMeteoLuogo *v,
                              size_t n);
/* Fotografia dei controlli (qualunque thread). */
void ph_servizi_pubblica_controlli(PhServizi *s, const PhSrvControlli *c);
/* Da QUALUNQUE thread (anche quello di lettura di un flusso): un decodificatore
 * video e' passato al processore (nessuna via hardware, GTK ha rifiutato il
 * DMA-BUF o la via hardware ha rinunciato). Arriva sul thread GTK come
 * PH_SRV_EV_DECODIFICA; il pezzo comune non la filtra: «una volta per sessione
 * di collegamento» e' compito del backend. Mai chiamarla con un lock che chi
 * ascolta potrebbe prendere (la consegna e' comunque sempre differita). */
void ph_servizi_pubblica_decodifica_software(PhServizi *s);
/* Prima connessione: finale dell'abbinamento. frase senza codice ne' indirizzi. */
void ph_servizi_lavoro_abbina(PhSrvLavoro *l, PhSrvAbbEsito esito, const char *frase);
/* (6) Come ph_servizi_pubblica_notifiche, ma dice esplicitamente se questa e'
 * la PRIMA lettura dopo un collegamento/riconnessione (anche vuota o con
 * problema). Il backend che usa ancora ph_servizi_pubblica_notifiche: il pezzo
 * comune tratta come prima la prima pubblicazione applicata dopo il passaggio
 * a COLLEGATO/BLOCCATO (ordine delle consegne: pubblicare il collegamento
 * PRIMA delle notifiche, dallo stesso thread). */
void ph_servizi_pubblica_notifiche_lettura(PhServizi *s, PhSrvNotifica *v, size_t n,
                                           const char *problema, bool prima_lettura);

/* Utili per i backend: liberano gli array come li vuole il pezzo comune. */
void ph_srv_app_libera(PhSrvApp *v, size_t n);
void ph_srv_notifiche_libera(PhSrvNotifica *v, size_t n);
void ph_srv_trasferimenti_libera(PhSrvTrasferimento *v, size_t n);
void ph_srv_voci_libera(PhSrvVoce *v, size_t n);

#endif
