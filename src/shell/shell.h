/* Il contesto della shell: cio' che i pezzi si offrono l'un l'altro senza
 * includere gli header interni degli altri (src/shell/CONTRATTI.md).
 * Realizzato una volta sola in src/shell/comune/ (di chi coordina).
 *
 * Contiene:
 *  - la consegna al thread GTK (l'UNICA via con cui un thread passa qualcosa
 *    al thread GTK);
 *  - il PhShell: la radice con gli strati, il gestore dei popup (uno solo
 *    aperto alla volta), gli avvisi della shell, lo stato della finestra host;
 *  - i "gestori": funzioni che un pezzo serve agli altri (apri l'app X, elenco
 *    delle finestre, avviso a comparsa, ...), con le chiamate che li usano;
 *  - stile, icone del mockup, trascinamento delle app, registro delle decisioni.
 *
 * Tutto si usa SOLO dal thread GTK, tranne ph_shell_consegna,
 * ph_shell_nel_thread_gtk, ph_shell_decisione e ph_shell_ora_ms. */
#ifndef PH_SHELL_SHELL_H
#define PH_SHELL_SHELL_H

#include <gtk/gtk.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shell/servizi.h"

/* ---- Consegna al thread GTK -------------------------------------------- */
typedef void (*PhShellFn)(void *dati);
/* Da QUALUNQUE thread (anche da quello GTK): fn(dati) girera' sul thread GTK in
 * un giro successivo del ciclo principale, mai dentro questa chiamata. Poi, se
 * libera != NULL, libera(dati). L'ordine delle consegne fatte dallo stesso
 * thread e' rispettato. dati non deve puntare a widget senza riferimento:
 * tenere un g_object_ref (o un proprio contatore) fino alla consegna. */
void ph_shell_consegna(PhShellFn fn, void *dati, GDestroyNotify libera);
/* true se il thread che chiama e' quello GTK (quello che ha creato il primo
 * PhShell o chiamato ph_shell_segna_thread_gtk). */
bool ph_shell_nel_thread_gtk(void);
void ph_shell_segna_thread_gtk(void);

/* Millisecondi monotoni (per src/finestre/finestre.h e per i tempi). */
uint64_t ph_shell_ora_ms(void);

/* ---- Registro delle decisioni automatiche (4.12, 4.14) ------------------
 * Ogni decisione presa dal programma da solo (pausa dei flussi, frequenza
 * ridotta, flusso ripreso, ripiego da DMA-BUF a memoria, ...) si scrive qui,
 * con la causa e, se c'e', il numero misurato che l'ha causata. Una riga su
 * stderr: "[decisione] HH:MM:SS.mmm <che_cosa> - causa: <causa>". Mai dati
 * personali (seriali, indirizzi, nomi di file dell'utente). Qualunque thread. */
void ph_shell_decisione(const char *che_cosa, const char *causa_formato, ...)
    __attribute__((format(printf, 2, 3)));

/* ---- Il contesto -------------------------------------------------------- */
typedef struct PhShell PhShell;

/* Crea il contesto sopra i servizi (che restano di chi li ha creati) e carica
 * lo stile di base. La radice va messa nella finestra host da chi crea. */
PhShell *ph_shell_nuova(PhServizi *servizi);
/* Toglie gestori e ascoltatori; non libera i servizi. La radice va gia' tolta
 * dalla finestra (o distrutta con essa).
 * ORDINE DI DISTRUZIONE (garantito): ph_shell_libera prima stacca la radice
 * dal PhShell (ph_shell_radice_viva diventa false), poi lascia il proprio
 * riferimento alla radice; nel dispose della radice i figli degli strati
 * vengono STACCATI PRIMA che la radice emetta "destroy". Quindi un pezzo che
 * nel proprio gestore di "destroy" della radice tocca i suoi widget deve
 * tenerne un riferimento proprio (g_object_ref) finche' esistono i suoi
 * ascolti, e lasciarlo li'. Nessun avviso della shell arriva dopo che
 * ph_shell_libera e' iniziata. */
void ph_shell_libera(PhShell *sh);
/* false se `radice` non appartiene (piu') a un PhShell vivo: ph_shell_libera
 * e' iniziata o e' finita. Per i pezzi che nel "destroy" della radice devono
 * sapere se la shell intera sta chiudendo (invece di leggere dati interni
 * della radice). Solo thread GTK; radice puo' essere NULL (false). */
bool ph_shell_radice_viva(GtkWidget *radice);
GtkWidget *ph_shell_radice(PhShell *sh);
PhServizi *ph_shell_servizi(PhShell *sh);
/* La finestra host che contiene la radice (NULL se non ancora messa). */
GtkWindow *ph_shell_finestra(PhShell *sh);

/* ---- Prima connessione (23 nov) -------------------------------------------
 * Regola UNICA (la usa l'ossatura) per decidere se al posto del desktop va
 * la schermata della prima connessione (pezzo shell-connessione,
 * src/shell/connessione/connessione.h) invece del velo di §4.8:
 *  - NON_ABBINATO: sempre (serve il codice);
 *  - CERCO e PERSO: solo se in questa esecuzione non c'e' mai stato un
 *    collegamento (dopo il primo collegamento vale il velo di 4.8);
 *  - COLLEGATO, BLOCCATO, CHIUSO: mai.
 * Funzione pura, qualunque thread. */
bool ph_shell_connessione_da_mostrare(PhSrvStato stato, bool collegato_in_sessione);

/* ---- Strati (dal basso in alto; z del mockup tra parentesi) ------------- */
typedef enum {
    PH_STRATO_DESKTOP = 0,  /* sfondo, icone, widget (mockup: z auto) - desktop */
    PH_STRATO_FINESTRE,     /* finestre interne (11..) - finestra-app */
    PH_STRATO_BARRA,        /* barra in basso (4000) - barra */
    PH_STRATO_PANNELLI,     /* velo-clic dei popup (4500), launcher, pannelli, popup,
                               menu, dialoghi (5000-5100) - vari */
    PH_STRATO_TOAST,        /* avvisi a comparsa (5200) - sistema */
    PH_STRATO_RILASCIO,     /* riquadro "Rilascia per inviare al telefono" (5400) - trasferimento */
    PH_STRATO_VELO,         /* velo di blocco / connessione (9000) - ossatura */
    PH_STRATO_TITOLO_HOST,  /* striscia di 6 px in schermo intero (9500) - ossatura */
    PH_STRATO_NUMERO
} PhShellStrato;

/* Aggiunge w allo strato. Ogni figlio riceve TUTTA l'area della radice e si
 * colloca con halign/valign e margini (come i figli di un GtkOverlay): un
 * figlio con halign/valign diversi da FILL occupa solo la sua misura naturale
 * e il resto dell'area resta cliccabile per gli strati sotto. Dentro lo stesso
 * strato vale l'ordine di aggiunta (l'ultimo sopra). */
void ph_shell_strato_aggiungi(PhShell *sh, PhShellStrato strato, GtkWidget *w);
void ph_shell_strato_togli(PhShell *sh, GtkWidget *w);
/* Misura attuale della radice in unita' logiche (pixel CSS). */
void ph_shell_area(PhShell *sh, int *larghezza, int *altezza);

/* ---- Vetro ---------------------------------------------------------------
 * Una superficie di vetro (barra, launcher, pannello, menu, dialogo, avviso,
 * widget del desktop) e' un widget che il pezzo REGISTRA: la shell gli mette
 * la classe CSS ph-vetro-chiaro o ph-vetro-scuro (velo, bordo, ombra e colori
 * del testo: base.css) e, quando disegna la scheda grafica, disegna dietro di
 * lui - dentro il suo rettangolo con angoli di `raggio` px, lo stesso del suo
 * CSS - cio' che sta negli strati SOTTO il suo, sfocato. Senza scheda grafica
 * non c'e' sfocatura e il CSS da' un fondo quasi opaco (la radice ha la classe
 * ph-vetro-attivo solo quando la sfocatura c'e').
 * Il widget deve stare (anche annidato) in uno strato della radice; si puo'
 * registrare prima o dopo averlo aggiunto, mostrare, nascondere e spostare
 * liberamente. Si toglie da solo quando viene distrutto; ph_shell_vetro_togli
 * serve solo per smettere prima (innocuo se non e' registrato o se la shell e'
 * gia' stata liberata). Vale la PRIMA registrazione: registrare di nuovo lo
 * stesso widget (anche con l'altro tipo, o dopo ph_shell_vetro_proprio) non fa
 * nulla. Tranne _togli, tutte vogliono una shell viva (non ancora passata a
 * ph_shell_libera). L'opacita' CSS della superficie non vale per la sfocatura
 * dietro di lei. Una superficie che sta dietro un'altra si vede con il suo
 * velo, bordo e ombra (come con un vero backdrop-filter).
 * ph_shell_vetro_proprio: per un widget che la sfocatura se la disegna DA SE'
 * (la cornice delle finestre interne): non riceve classi ne' sfocatura, ma
 * quando sta dietro una superficie viene ritratto dai suoi figli, per non
 * sfocare una sfocatura. Solo thread GTK; nessuna callback. */
typedef enum { PH_VETRO_CHIARO = 0, PH_VETRO_SCURO } PhShellVetro;
void ph_shell_vetro_registra(PhShell *sh, GtkWidget *w, int raggio, PhShellVetro tipo);
void ph_shell_vetro_proprio(PhShell *sh, GtkWidget *w);
void ph_shell_vetro_togli(PhShell *sh, GtkWidget *w);
/* true se la sfocatura c'e' (deciso quando la radice viene realizzata). */
bool ph_shell_vetro_attivo(PhShell *sh);
/* La regola, unica per tutta la shell: la sfocatura c'e' solo se a disegnare e'
 * la scheda grafica, cioe' con i renderer GskGLRenderer, GskNglRenderer,
 * GskVulkanRenderer; con ogni altro nome (o NULL) no. Funzione pura.
 * _decidi: la variabile d'ambiente YASDE_SFOCATURA (0 o 1) se c'e', altrimenti
 * la regola applicata al renderer della finestra di w (w gia' realizzato). */
bool ph_shell_vetro_per_renderer(const char *nome_tipo);
bool ph_shell_vetro_decidi(GtkWidget *w);

/* Contenitore "libero": figli in posizione assoluta (x, y in unita' logiche,
 * misura data o naturale con -1). E' TRASPARENTE AI CLIC fuori dai figli:
 * pensato per gli strati a tutta area (finestre interne, menu contestuali,
 * icone). L'ordine di disegno e' quello dei figli (l'ultimo sopra). */
GtkWidget *ph_shell_libero_nuovo(void);
void ph_shell_libero_metti(GtkWidget *libero, GtkWidget *figlio, int x, int y, int larghezza,
                           int altezza);
void ph_shell_libero_sposta(GtkWidget *libero, GtkWidget *figlio, int x, int y, int larghezza,
                            int altezza);
void ph_shell_libero_togli(GtkWidget *libero, GtkWidget *figlio);
void ph_shell_libero_in_cima(GtkWidget *libero, GtkWidget *figlio);

/* ---- Popup: uno solo aperto alla volta (mockup: toggle* e closePopups) ---
 * Sono popup: launcher, pannelli notifiche / telefono / volume / invio /
 * ricezione, menu contestuali, dialoghi sfondo e widget, menu "altri comandi"
 * della finestra. Il pezzo crea il suo widget, lo aggiunge (nascosto) allo
 * strato PANNELLI, e lo apre con ph_shell_popup_apri. Aprire un popup chiude
 * quello aperto (chiamandone la funzione di chiusura). Con un popup aperto un
 * velo trasparente copre tutta l'area sotto i popup (barra compresa, come il
 * mockup a z 4500): un clic fuori chiude il popup e NON passa sotto. Esc
 * chiude il popup aperto (proposta, vedi CONTRATTI.md §5). Il velo di blocco
 * chiude il popup aperto. */
typedef enum {
    PH_POPUP_NESSUNO = 0,
    PH_POPUP_LAUNCHER,
    PH_POPUP_NOTIFICHE,
    PH_POPUP_TELEFONO,
    PH_POPUP_VOLUME,
    PH_POPUP_INVIO,
    PH_POPUP_RICEZIONE,
    PH_POPUP_MENU,             /* menu contestuale (desktop, icona, widget, launcher) */
    PH_POPUP_DIALOGO_SFONDO,
    PH_POPUP_DIALOGO_WIDGET,
    PH_POPUP_ALTRI_COMANDI,    /* menu ⋮ di una finestra */
    /* Aggiunti in coda (23 nov, richieste del committente in PIANO.md): */
    PH_POPUP_MENU_BARRA,       /* clic destro su un'icona della barra: Chiudi app, fissa/togli */
    PH_POPUP_CALENDARIO,       /* clic sull'orologio della barra: calendario */
} PhShellPopup;

/* Chiamata quando il popup si chiude per qualunque motivo (altro popup,
 * clic fuori, Esc, velo, ph_shell_popup_chiudi): il pezzo lo nasconde qui se
 * vuole fare altro (il gestore lo ha gia' nascosto). */
typedef void (*PhShellPopupChiuso)(PhShell *sh, PhShellPopup tipo, GtkWidget *w, void *utente);

/* Mostra w (gia' nello strato PANNELLI) come popup `tipo`; il fuoco va a w. */
void ph_shell_popup_apri(PhShell *sh, PhShellPopup tipo, GtkWidget *w, PhShellPopupChiuso chiuso,
                         void *utente);
/* Se `tipo` e' aperto lo chiude, altrimenti lo apre (i pulsanti della barra). */
void ph_shell_popup_alterna(PhShell *sh, PhShellPopup tipo, GtkWidget *w,
                            PhShellPopupChiuso chiuso, void *utente);
void ph_shell_popup_chiudi(PhShell *sh);
PhShellPopup ph_shell_popup_aperto(PhShell *sh);

/* ---- Avvisi della shell ------------------------------------------------ */
typedef enum {
    PH_SHELL_EV_FINESTRE = 0, /* finestre interne aperte / minimizzate / attiva cambiate */
    PH_SHELL_EV_POPUP,        /* popup aperto cambiato (per aria-expanded della barra) */
    PH_SHELL_EV_HOST,         /* finestra host: visibile/minimizzata, schermo intero */
    PH_SHELL_EV_AREA,         /* misura della radice cambiata */
    PH_SHELL_EV_DESKTOP,      /* icone sul desktop cambiate */
    PH_SHELL_EV_NASCOSTE,     /* notifiche nascoste nella shell cambiate (le emette sistema) */
    PH_SHELL_EV_FISSATE,      /* app fissate sulla barra cambiate (le emette barra; aggiunto il 23 nov) */
    PH_SHELL_EV_NUMERO
} PhShellEvento;

typedef void (*PhShellAvviso)(PhShell *sh, PhShellEvento ev, void *utente);
guint ph_shell_ascolta(PhShell *sh, PhShellEvento ev, PhShellAvviso fn, void *utente);
void ph_shell_smetti(PhShell *sh, guint id);
/* Per i pezzi che servono un gestore: l'avviso parte subito (sincrono). */
void ph_shell_emetti(PhShell *sh, PhShellEvento ev);

/* Stato della finestra host: lo imposta l'ossatura (nei demo resta visibile). */
void ph_shell_imposta_host(PhShell *sh, bool visibile, bool schermo_intero);
/* false = finestra host minimizzata o non visibile (4.14): tutti i flussi in
 * pausa. */
bool ph_shell_host_visibile(PhShell *sh);
bool ph_shell_schermo_intero(PhShell *sh);

/* ---- Gestori: funzioni che un pezzo serve agli altri ------------------- */

/* Una finestra interna, per la barra (5.3). */
typedef struct {
    unsigned id;
    char pacchetto[PH_CFG_LUN_APP]; /* "" per lo specchio */
    char titolo[128];               /* nome dell'app o "Schermo del telefono" */
    bool minimizzata;
    bool attiva;
    bool specchio;
} PhShellFinestra;

/* Servito da finestra-app. */
typedef struct {
    void *io;
    /* Apre l'app o porta davanti la sua finestra (mockup openApp). */
    void (*apri_app)(void *io, const char *pacchetto);
    /* Finestre aperte in ordine di apertura (al massimo max); torna quante. */
    size_t (*elenco)(void *io, PhShellFinestra *out, size_t max);
    /* Clic sulla voce della barra: in primo piano -> minimizza, minimizzata ->
     * ripristina, altrimenti porta davanti (5.3 punto 3). */
    void (*clic_barra)(void *io, unsigned id);
    /* Finestra dello schermo reale (4.7): mostra o chiude. */
    void (*specchio)(void *io, bool mostra);
    bool (*specchio_aperto)(void *io);
    /* Aggiunta in coda (23 nov; facoltativa, NULL = non fa nulla): chiude la
     * finestra `id` con lo stesso effetto del pulsante Chiudi (app tolta dai
     * recenti, 4.6). id sconosciuto: nulla. */
    void (*chiudi)(void *io, unsigned id);
} PhShellGestoreFinestre;

/* Servito da desktop (icone, 5.2.2). */
typedef struct {
    void *io;
    bool (*sul_desktop)(void *io, const char *pacchetto);
    /* Nel primo posto libero (mockup ctxAddDesk); se e' pieno avvisa da se'. */
    void (*aggiungi)(void *io, const char *pacchetto);
    void (*rimuovi)(void *io, const char *pacchetto);
} PhShellGestoreDesktop;

/* Servito da sistema (toast di informazione: "Inviato al telefono", ...). */
typedef struct {
    void *io;
    void (*avviso)(void *io, const char *titolo, const char *testo);
} PhShellGestoreAvvisi;

/* Servito da launcher. */
typedef struct {
    void *io;
    void (*alterna)(void *io);
} PhShellGestoreLauncher;

/* Servito da trasferimento. */
typedef struct {
    void *io;
    void (*apri)(void *io, PhSrvVerso verso); /* alterna il pannello invia/scarica */
} PhShellGestoreTrasferimento;

/* Servito da sistema (F-06, F-07, F-08): i tre pulsanti dell'area di sistema
 * della barra, che hanno stato e popup del sistema, e il "nascondi" delle
 * notifiche, che vale solo nella shell (5.6) e serve anche al widget
 * "Notifiche recenti" del desktop. */
typedef struct {
    void *io;
    /* Pulsante nuovo (.tb del mockup) per PH_POPUP_VOLUME, PH_POPUP_TELEFONO o
     * PH_POPUP_NOTIFICHE: lo colloca la barra (ne diventa il genitore); il
     * sistema ne aggiorna icona, testo, contatore, etichetta e aria-expanded
     * e al clic apre il suo popup. NULL per gli altri tipi. */
    GtkWidget *(*pulsante)(void *io, PhShellPopup tipo);
    /* true se la notifica (chiave, ora) e' nascosta nella shell. Una notifica
     * nascosta e poi aggiornata (ora diversa) non e' piu' nascosta. */
    bool (*nascosta)(void *io, const char *chiave, uint64_t ora_ms);
} PhShellGestoreSistema;

/* Servito da barra (aggiunto il 23 nov, PIANO.md «Richieste del committente»
 * punti 3-4): le app FISSATE sulla barra, per telefono. L'elenco vive nel
 * profilo del telefono (campo di PhCfgTelefono aggiunto dal pezzo config,
 * massimo 16 pacchetti); lo legge e lo scrive SOLO la barra
 * (ph_srv_profilo + ph_srv_profilo_salva), che a ogni cambio emette
 * PH_SHELL_EV_FISSATE e lo rilegge a PH_SRV_EV_PROFILO. Gli altri pezzi
 * (launcher, menu della barra) passano da qui, cosi' le regole (niente
 * doppioni, tetto di 16, ordine) stanno in un posto solo. */
typedef struct {
    void *io;
    bool (*fissata)(void *io, const char *pacchetto);
    /* fissa = true aggiunge in fondo (gia' fissata o elenco pieno: nulla;
     * pieno: avviso a comparsa della barra), false toglie. */
    void (*fissa)(void *io, const char *pacchetto, bool fissa);
} PhShellGestoreFissate;

/* Registrare (o sostituire) un gestore: la struttura si copia. NULL lo toglie. */
void ph_shell_registra_finestre(PhShell *sh, const PhShellGestoreFinestre *g);
void ph_shell_registra_desktop(PhShell *sh, const PhShellGestoreDesktop *g);
void ph_shell_registra_avvisi(PhShell *sh, const PhShellGestoreAvvisi *g);
void ph_shell_registra_launcher(PhShell *sh, const PhShellGestoreLauncher *g);
void ph_shell_registra_trasferimento(PhShell *sh, const PhShellGestoreTrasferimento *g);
void ph_shell_registra_sistema(PhShell *sh, const PhShellGestoreSistema *g);
void ph_shell_registra_fissate(PhShell *sh, const PhShellGestoreFissate *g); /* 23 nov */

/* Chiamate dei gestori. Senza gestore registrato (demo di un pezzo solo) non
 * fanno nulla e lo scrivono su stderr; le letture tornano 0/false. */
void ph_shell_apri_app(PhShell *sh, const char *pacchetto);
size_t ph_shell_finestre(PhShell *sh, PhShellFinestra *out, size_t max);
void ph_shell_clic_barra(PhShell *sh, unsigned id);
void ph_shell_specchio(PhShell *sh, bool mostra);
bool ph_shell_specchio_aperto(PhShell *sh);
bool ph_shell_sul_desktop(PhShell *sh, const char *pacchetto);
void ph_shell_aggiungi_al_desktop(PhShell *sh, const char *pacchetto);
void ph_shell_rimuovi_dal_desktop(PhShell *sh, const char *pacchetto);
void ph_shell_avviso(PhShell *sh, const char *titolo, const char *testo);
void ph_shell_launcher_alterna(PhShell *sh);
void ph_shell_trasferimento_apri(PhShell *sh, PhSrvVerso verso);
/* NULL senza gestore (la barra lascia il posto vuoto). */
GtkWidget *ph_shell_sistema_pulsante(PhShell *sh, PhShellPopup tipo);
/* false senza gestore: nessuna notifica nascosta. */
bool ph_shell_notifica_nascosta(PhShell *sh, const char *chiave, uint64_t ora_ms);
/* Aggiunte del 23 nov. Senza gestore (o con la voce NULL) non fanno nulla /
 * tornano false. */
void ph_shell_chiudi_finestra(PhShell *sh, unsigned id);           /* gestore finestre */
bool ph_shell_app_fissata(PhShell *sh, const char *pacchetto);     /* gestore fissate */
void ph_shell_fissa_app(PhShell *sh, const char *pacchetto, bool fissa);

/* ---- Trascinamento di un'app (launcher -> desktop, desktop -> desktop) ---
 * Contenuto del GdkContentProvider: un valore di tipo PH_TIPO_APP_TRASCINATA
 * (boxed). Chi trascina dal launcher chiude il launcher appena il trascinamento
 * parte (5.2.2). */
typedef struct {
    char pacchetto[PH_CFG_LUN_APP];
    bool dal_desktop; /* false = dal launcher */
} PhShellAppTrascinata;
GType ph_shell_app_trascinata_get_type(void);
#define PH_TIPO_APP_TRASCINATA (ph_shell_app_trascinata_get_type())
/* Crea il contenuto da dare a gtk_drag_source_set_content. */
GdkContentProvider *ph_shell_app_trascinata_contenuto(const char *pacchetto, bool dal_desktop);

/* ---- Stile -------------------------------------------------------------
 * Lo stile di base (colori e misure del mockup, src/shell/ossatura/base.css)
 * lo carica ph_shell_nuova. Ogni pezzo carica il SUO file CSS, compilato come
 * risorsa, con questa funzione (una volta, nel proprio _installa). */
void ph_shell_stile_carica(const char *percorso_risorsa);
/* Il CSS di un pezzo e' valido? (per i test: carica senza schermo e torna il
 * numero di errori di sintassi, scritti su stderr). */
int ph_shell_stile_controlla(const char *percorso_risorsa);

/* ---- Icone del mockup ----------------------------------------------------
 * Disegnate con i tracciati SVG del mockup (gsk_path_parse) nel colore del
 * testo del widget (currentColor): una sola fonte per tutti i pezzi. */
typedef struct {
    const char *tratto;   /* tracciato da tracciare (puo' essere NULL) */
    const char *pieno;    /* tracciato da riempire (puo' essere NULL) */
    float vista_l, vista_a; /* viewBox del mockup */
    float spessore;       /* stroke-width */
    int larghezza, altezza; /* misura disegnata in px */
    bool spigoli_vivi;    /* tratto con estremita' dritte e giunzioni ad angolo (stile "solo glifi" delle finestre
                           * interne); false = estremita' e giunzioni tonde (tutte le altre icone) */
} PhShellIcona;

extern const PhShellIcona PH_ICONA_SCHERMO_INTERO, PH_ICONA_RIDUCI, PH_ICONA_INGRANDISCI_HOST,
    PH_ICONA_RIPRISTINA_HOST, PH_ICONA_CHIUDI_HOST, PH_ICONA_INDIETRO, PH_ICONA_SCREENSHOT,
    PH_ICONA_REGISTRA, PH_ICONA_ALTRI, PH_ICONA_RIDUCI_FINESTRA, PH_ICONA_INGRANDISCI_FINESTRA,
    PH_ICONA_CHIUDI_FINESTRA, PH_ICONA_LAUNCHER, PH_ICONA_INVIA, PH_ICONA_SCARICA,
    PH_ICONA_VOLUME, PH_ICONA_VOLUME_MUTO, PH_ICONA_WIFI, PH_ICONA_BATTERIA,
    PH_ICONA_CAMPANELLA, PH_ICONA_LUCCHETTO;

GtkWidget *ph_shell_icona_nuova(const PhShellIcona *icona);
void ph_shell_icona_imposta(GtkWidget *w, const PhShellIcona *icona);

/* ---- Accessibilita' (5.11) ----------------------------------------------
 * Etichetta accessibile (aria-label del mockup) e suggerimento (title del
 * mockup, NULL = nessuno). Testi gia' tradotti. */
void ph_shell_etichetta(GtkWidget *w, const char *etichetta, const char *suggerimento);
/* aria-expanded / aria-pressed del mockup. */
void ph_shell_espanso(GtkWidget *w, bool espanso);
void ph_shell_premuto(GtkWidget *w, bool premuto);

/* Segnaposto del mockup al posto di un'icona mancante: quadrato arrotondato
 * colorato con le iniziali (lato e raggio in px). Il colore deriva dal
 * pacchetto (stabile). */
GtkWidget *ph_shell_segnaposto_nuovo(const char *pacchetto, const char *nome, int lato,
                                     int raggio);

#endif
