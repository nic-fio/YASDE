/* Logica pura di shell-finestra-app (F-05): niente widget, niente servizi,
 * niente schermo. Sta in un file a parte per poterla provare senza schermo
 * (prova_finestra_app.c). Le regole di geometria delle finestre NON sono qui:
 * sono di src/finestre/finestre.h, che finestra_app.c usa cosi' com'e'.
 * Qui c'e' solo cio' che finestre.h non sa: i tasti, le chiavi corte delle
 * app, la scelta della misura iniziale, il nome dello screenshot, le decisioni
 * di pausa. Tutto dal thread GTK. */
#ifndef PH_SHELL_FINESTRA_APP_LOGICA_H
#define PH_SHELL_FINESTRA_APP_LOGICA_H

#include <gtk/gtk.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config/config.h"
#include "finestre/finestre.h"
#include "shell/servizi.h"

/* ---- Tasti -------------------------------------------------------------- */

/* Codice KeyEvent.KEYCODE_* di Android per un tasto GDK; 0 se il tasto non ha
 * una corrispondenza (modificatori da soli, tasti multimediali, ...). Lettere
 * maiuscole e minuscole danno lo stesso codice. */
unsigned ph_fa_tasto_android(guint keyval);

/* KeyEvent.META_* di Android (SHIFT 1, ALT 2, CTRL 0x1000) dai modificatori GDK. */
unsigned ph_fa_meta_android(GdkModifierType m);

/* Un tasto con questi modificatori manda un testo (cioe' lo compone il metodo
 * di input) o un codice tasto? I caratteri stampabili senza Ctrl e Alt sono
 * testo (cosi' maiuscole, accenti e layout stranieri arrivano giusti); tutto
 * il resto (Invio, frecce, scorciatoie con Ctrl o Alt) e' un codice tasto. */
bool ph_fa_tasto_e_testo(guint keyval, GdkModifierType m);

typedef enum {
    PH_FA_RISERVATO_NESSUNO = 0,
    PH_FA_RISERVATO_RUOTA,           /* Ctrl+R */
    PH_FA_RISERVATO_CHIUDI_APP,      /* Ctrl+W */
    PH_FA_RISERVATO_COPIA_SCREENSHOT,/* Ctrl+Maiusc+C */
    PH_FA_RISERVATO_SCHERMO_INTERO,  /* F11: lo gestisce l'ossatura, non va al telefono */
} PhFaRiservato;

/* Tasti riservati alla shell: non vanno mai al telefono (5.5, CONTRATTI.md §5). */
PhFaRiservato ph_fa_tasto_riservato(guint keyval, GdkModifierType m);

/* ---- Pulsanti del mouse ------------------------------------------------- */

/* Bit del campo `pulsanti` di TOCCO (docs/protocollo.md 2.4) per un pulsante GDK:
 * sinistro (GDK_BUTTON_PRIMARY) 1, destro (GDK_BUTTON_SECONDARY) 2, centrale
 * (GDK_BUTTON_MIDDLE) 4. 0 = pulsante che non va al telefono (indietro, avanti,
 * altri): il gesto non parte. */
unsigned ph_fa_pulsante_tocco(unsigned bottone_gdk);

/* ---- Chiavi corte delle app --------------------------------------------- */
/* finestre.h chiama l'app con un nome di al massimo PH_FIN_APP_MAX - 1 byte, un
 * pacchetto Android puo' essere piu' lungo (fino a PH_CFG_LUN_APP - 1). Tabella
 * pacchetto <-> chiave ("a1", "a2", ...). Il pacchetto vuoto (specchio, 4.7)
 * ha la chiave "phone", quella che finestre.h tratta come specchio. */
typedef struct PhFaChiavi PhFaChiavi;
PhFaChiavi *ph_fa_chiavi_nuove(void);
void ph_fa_chiavi_libera(PhFaChiavi *t);
/* Chiave del pacchetto (la crea se manca). false se il pacchetto e' NULL o piu'
 * lungo di PH_CFG_LUN_APP - 1 (out resta vuoto). */
bool ph_fa_chiave_di(PhFaChiavi *t, const char *pacchetto, char out[PH_FIN_APP_MAX]);
/* Pacchetto di una chiave ("phone" -> ""); NULL se la chiave non e' nota. */
const char *ph_fa_pacchetto_di(const PhFaChiavi *t, const char *chiave);
/* Ricordi del profilo -> ricordi di finestre.h (crea le chiavi). Scarta le voci
 * senza app. Torna quanti ne ha scritti (al massimo max). */
size_t ph_fa_ricordi_da_profilo(PhFaChiavi *t, const PhCfgFinestra *in, size_t n,
                                PhFinRicordo *out, size_t max);
/* Ricordi di finestre.h -> profilo. Scarta chiavi sconosciute e lo specchio
 * (la sua posizione e' fissa). Torna quanti ne ha scritti. */
size_t ph_fa_ricordi_a_profilo(const PhFaChiavi *t, const PhFinRicordo *in, size_t n,
                               PhCfgFinestra *out, size_t max);

/* ---- Misura iniziale ---------------------------------------------------- */
/* Misura iniziale PREDEFINITA (finestre.h): w = h = 0, la sceglie ph_fin_apri dallo
 * schermo del telefono in punti (ph_fin_imposta_telefono). Solo verticale: forma di
 * telefono; specchio: schermo reale; altrimenti generica ridimensionabile. app NULL =
 * elenco delle app non ancora arrivato: la generica. */
PhFinSpec ph_fa_scegli_spec(const PhSrvApp *app, bool specchio);

/* ---- Screenshot e registrazioni ------------------------------------------ */
/* <immagini>/YASDE: la cartella del PC per screenshot e registrazioni.
 * I nomi dei file li decide src/video/cattura.h. Da liberare con g_free. */
char *ph_fa_cartella_screenshot(const char *immagini);

/* ---- Densita' ------------------------------------------------------------ */
/* Regola della densita' dello schermo virtuale: con una densita_app scelta
 * dall'utente nel profilo (> 0) vale quella, FISSA; con 0 resta la regola
 * automatica di finestre.h (modalita' tablet). */
bool ph_fa_densita_automatica(int densita_profilo);

/* ---- Pausa -------------------------------------------------------------- */
/* Un flusso deve essere in pausa se la finestra e' minimizzata o la finestra
 * host non e' visibile (4.6, 4.14). */
bool ph_fa_pausa_voluta(bool host_visibile, bool minimizzata);
typedef enum { PH_FA_PASSO_NIENTE = 0, PH_FA_PASSO_PAUSA, PH_FA_PASSO_RIPRENDI } PhFaPasso;
/* Che cosa fare perche' lo stato attuale (in_pausa) diventi quello voluto. */
PhFaPasso ph_fa_passo_pausa(bool in_pausa, bool voluta);

/* ---- Geometria del titolo ----------------------------------------------- */
/* x del pulsante p in una finestra larga `larghezza` (come PhFinPulsante di
 * finestre.h: indietro a sinistra, gli altri ancorati a destra, PH_FIN_PULS px,
 * PH_FIN_PULS_DIST fra loro, PH_FIN_PULS_MARG_DX dal bordo destro, indietro a PH_FIN_PULS_MARG_SX).
 * Il pulsante e' alto PH_FIN_PULS e sta a y = (PH_FIN_TITOLO_H - PH_FIN_PULS) / 2. */
int ph_fa_pulsante_x(int larghezza, PhFinPulsante p);

/* ---- Input -------------------------------------------------------------- */
/* Punto (x, y) della shell -> coordinate del video, sul RETTANGOLO DELL'IMMAGINE
 * (immagine: x, y, larghezza, altezza in coordinate della shell; viene da
 * ph_superficie_rettangolo_immagine piu' l'origine del contenuto). A riposo
 * l'immagine ha bande ai lati: la formula di finestre.h (x * video_w /
 * larghezza) va applicata all'immagine, non all'area. Stessa formula, troncata.
 * - fissa = false (inizio di un tocco, rotella): un punto nelle bande o fuori
 *   dall'immagine non e' input: torna false.
 * - fissa = true (tocco gia' iniziato): il punto e' portato dentro l'immagine,
 *   perche' un tocco iniziato in una finestra resta suo fino al rilascio.
 * Torna false anche se una misura non e' valida. Con l'immagine uguale a tutto
 * il contenuto da' lo stesso risultato di ph_fin_trova. */
bool ph_fa_traduci_immagine(double ix, double iy, double iw, double ih, int video_w, int video_h,
                            double x, double y, bool fissa, int *vx, int *vy);

/* Origine con cui disegnare l'immagine 1:1 (vero 1:1: l'origine deve cadere su un
 * pixel FISICO intero, altrimenti il compositore ricampiona). `origine` e'
 * l'origine logica del contenuto nel contenitore; `scostamento` e' la posizione
 * logica del contenitore rispetto alla superficie nativa (barra del titolo
 * dell'host, margine dell'ombra: la radice non parte da (0,0)). Torna l'origine
 * corretta nello stesso sistema di `origine` (cambia di meno di mezzo pixel
 * fisico): (scostamento + corretta) * scala e' un intero. scala > 0. */
double ph_fa_origine_pixel_intero(double origine, double scostamento, double scala);

/* Rotella: accumula `delta` in SCATTI (positivo = giu'/destra) e torna i
 * 120esimi di scatto interi da mandare (uno scatto = 120, come chiede
 * ph_superficie_rotella), tenendo il resto in *accumulo: un colpo di rotella
 * e' +-120, un touchpad da' frazioni che non si perdono. Il segno resta quello
 * di GTK: l'inversione verso Android la fa il chiamante, una volta sola. */
int ph_fa_passi_rotella(double *accumulo, double delta);

/* Il delta di GTK in scatti: con unita' WHEEL (rotella del mouse) e' gia' in
 * scatti; con SURFACE (touchpad, GTK >= 4.8) e' in pixel e si divide per
 * pixel_per_scatto (> 0). */
double ph_fa_scatti_da_delta(bool pixel, double pixel_per_scatto, double delta);

/* ---- Registro delle decisioni ---------------------------------------------- */
/* La riga del registro per una misura chiesta al telefono: contenuto in pixel
 * logici e fisici, scala, misura e densita' chieste, fattore di riproporzione e
 * punti che il telefono vede. Per lo specchio la misura la decide il telefono
 * (quella chiesta e' un segnaposto): niente numeri inventati. */
void ph_fa_riga_misura(char *out, size_t capacita, const char *motivo, int logici_w, int logici_h,
                       double scala, int w, int h, int densita, double densita_telefono, bool tablet,
                       bool specchio, double fattore, int punti_w, int punti_h);

/* ---- Aperture contate (widget "App frequenti") ------------------------------ */
/* Conta un'apertura sul profilo SOLO per una finestra NUOVA di un'app: non per lo
 * specchio, non per un secondo clic che porta in primo piano una finestra gia'
 * aperta (nuova = false), non per il ripristino da minimizzata ne' per la ripresa
 * dopo una caduta del collegamento (la finestra e' la stessa: qui non si passa).
 * Torna true se ha contato. E' uno storico d'uso: mai nel registro delle decisioni. */
bool ph_fa_conta_apertura(PhCfgTelefono *profilo, const char *pacchetto, bool nuova, bool specchio,
                          int64_t ora_secondi);

/* ---- Pizzico: Ctrl + rotella = zoom (CONTRATTI.md §17) ------------------------ */
/* Macchina a stati del gesto, senza widget: il tempo arriva come parametro (ms,
 * non decrescente). Un gesto si apre al primo passo (INIZIO con quel passo), ogni
 * passo successivo e' CONTINUA, e si chiude con FINE dopo PH_FA_PIZZICO_FINE_MS
 * senza passi (ph_fa_pz_scaduto) o su richiesta (rilascio di Ctrl, uscita del
 * puntatore, perdita del fuoco, minimizzazione, prima di un tocco). */
#define PH_FA_PIZZICO_FINE_MS 250
#define PH_FA_PIZZICO_PASSO_MAX 1200 /* limite del contratto: -1200..1200 */
typedef struct {
    bool aperto;
    uint64_t ultimo_ms; /* istante dell'ultimo passo mandato */
    double resto;       /* 120esimi di scatto non ancora mandati */
} PhFaPizzico;
typedef enum {
    PH_FA_PZ_NIENTE = 0, /* niente da mandare */
    PH_FA_PZ_INIZIO,
    PH_FA_PZ_CONTINUA,
    PH_FA_PZ_FINE,
} PhFaPzAzione;
void ph_fa_pz_inizializza(PhFaPizzico *p);
/* Un evento di zoom di `scatti` scatti (positivo = ingrandisci; uno scatto = 120
 * passi). Accumula senza perdere il resto: sotto un passo intero non apre nulla.
 * *passo = passo intero da mandare (limitato a +-1200) quando l'azione non e' NIENTE. */
PhFaPzAzione ph_fa_pz_passo(PhFaPizzico *p, double scatti, uint64_t t_ms, int *passo);
/* true se il gesto e' aperto e sono passati PH_FA_PIZZICO_FINE_MS dall'ultimo passo. */
bool ph_fa_pz_scaduto(const PhFaPizzico *p, uint64_t t_ms);
/* Chiude: FINE se era aperto (azzera anche il resto), altrimenti NIENTE. */
PhFaPzAzione ph_fa_pz_chiudi(PhFaPizzico *p);
/* Con Ctrl (e senza Alt) la rotella e' uno zoom: la rotella normale NON si manda. */
bool ph_fa_scroll_e_zoom(GdkModifierType m);
/* Rapporto di scala del touchpad (GtkGestureZoom) in scatti: il telefono ingrandisce
 * di x1,25 ogni 120 passi, quindi scatti = log(rapporto) / log(1,25). 0 se non valido. */
double ph_fa_scatti_da_rapporto(double rapporto);

/* Pizzico a due dita sul TOUCHSCREEN: il primo dito e' gia' un tocco (gesto di
 * trascinamento) e il secondo fa partire lo zoom. Per avere un pizzico utilizzabile
 * senza tocchi spuri il tocco del primo dito si ANNULLA al telefono quando lo zoom
 * parte (il trascinamento sul contenuto non rivendica la sequenza, cosi' lo zoom puo'
 * riconoscere i due dita). Se invece e' in corso lo spostamento o il ridimensionamento
 * di una finestra, lo zoom si rifiuta. */
typedef enum {
    PH_FA_ZOOM_PROCEDI = 0,          /* nessun gesto in corso */
    PH_FA_ZOOM_ANNULLA_TOCCO,        /* c'e' un tocco: ANNULLA, poi lo zoom parte */
    PH_FA_ZOOM_RIFIUTA,              /* spostamento/ridimensionamento in corso */
} PhFaZoomInizio;
PhFaZoomInizio ph_fa_zoom_inizio(bool tocco_in_corso, bool gesto_finestra_in_corso);

#endif
