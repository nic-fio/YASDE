/* La superficie: il widget che mostra UN flusso video (pezzo
 * src/shell/superficie/). La usa finestra-app per il contenuto di ogni
 * finestra interna (e per lo specchio). Specifica 4.2, 4.3, 4.5, 5.5.
 *
 * La superficie possiede il suo PhSrvFlusso: lo apre, ha un thread di lettura
 * e decodifica (mai lavoro pesante sul thread GTK), mostra i fotogrammi senza
 * copie (DMA-BUF) o in memoria. Durante trascinamento e ridimensionamento li
 * stende su tutta la sua area e la scala la fa la GPU (4.5); a riposo
 * l'immagine mantiene le proporzioni, con bande ai lati se servono: chi
 * traduce l'input usa ph_superficie_rettangolo_immagine (in fondo).
 * Mostra da se': il messaggio dello schermo protetto, l'ultima immagine
 * sfocata con "Riconnetti ora" e "Chiudi" a connessione persa (5.5).
 *
 * Tutto dal thread GTK. */
#ifndef PH_SHELL_SUPERFICIE_H
#define PH_SHELL_SUPERFICIE_H

#include <gtk/gtk.h>
#include <stdbool.h>

#include "shell/servizi.h"
#include "shell/shell.h"

typedef enum {
    PH_SUP_EV_MISURA = 0,  /* il video ha dichiarato una misura (larghezza, altezza): per l'input */
    PH_SUP_EV_APP_CHIUSA,  /* l'app si e' chiusa da sola: chiudere la finestra */
    PH_SUP_EV_CHIUDI,      /* l'utente ha premuto "Chiudi" sul velo di connessione persa */
    PH_SUP_EV_ERRORE,      /* il flusso e' morto e non riparte */
} PhSuperficieEvento;

typedef void (*PhSuperficieAvviso)(GtkWidget *superficie, PhSuperficieEvento ev, int larghezza,
                                   int altezza, void *utente);

/* Crea il widget e apre il flusso. NULL = errore (frase in e). */
GtkWidget *ph_superficie_nuova(PhShell *sh, const PhSrvFlussoRichiesta *r,
                               PhSuperficieAvviso avviso, void *utente, PhErrore *e);

/* Pausa (finestra minimizzata, host non visibile) e ripresa. */
void ph_superficie_pausa(GtkWidget *sup, bool pausa);
/* Nuova misura del flusso a movimento assestato (ph_fin_avanza). */
void ph_superficie_ridimensiona(GtkWidget *sup, int larghezza, int altezza);
/* Ultima misura dichiarata dal video (0 x 0 se nessuna ancora). */
void ph_superficie_misura_video(GtkWidget *sup, int *larghezza, int *altezza);

/* Input, gia' tradotto nelle coordinate del video (ph_fin_trova). */
void ph_superficie_tocco(GtkWidget *sup, int video_w, int video_h, int azione, int x, int y,
                         unsigned pulsanti);
/* Rotella: orizzontale e verticale in 120esimi di scatto, come nel protocollo
 * (docs/protocollo.md, ROTELLA): uno scatto intero = 120; un touchpad manda
 * valori piu' piccoli, senza perdere il resto. Verso come Android: verticale
 * positivo = la rotella gira in avanti (il contenuto scende). Chi chiama fa la
 * conversione dal verso di GTK; nessun altro la rifa'. */
void ph_superficie_rotella(GtkWidget *sup, int video_w, int video_h, int x, int y,
                           int orizzontale, int verticale);
void ph_superficie_tasto(GtkWidget *sup, bool giu, unsigned codice, unsigned meta);
void ph_superficie_testo(GtkWidget *sup, const char *testo);
void ph_superficie_indietro(GtkWidget *sup);

/* Ultima immagine mostrata (riferimento nuovo, g_object_unref), NULL se
 * nessuna: serve allo screenshot (5.5). */
GdkTexture *ph_superficie_fotografa(GtkWidget *sup);

/* Aggiunte del 23 nov 2026 (decisioni di chi coordina, CONTRATTI.md).
 *
 * A riposo l'immagine mantiene le proporzioni del video, con bande ai lati se
 * l'area ha proporzioni diverse; durante trascinamento e ridimensionamento e'
 * stesa su tutta l'area. Questa funzione da' il rettangolo che l'immagine
 * occupa ADESSO dentro il widget (coordinate del widget, in pixel logici):
 * chi traduce l'input lo usa al posto dell'area intera, e scarta i punti che
 * cadono nelle bande. Torna false se non c'e' ancora un'immagine. Solo dal
 * thread GTK. */
bool ph_superficie_rettangolo_immagine(GtkWidget *sup, double *x, double *y,
                                       double *larghezza, double *altezza);
/* La superficie della finestra attiva e' "in primo piano" (una sola alla
 * volta: la imposta finestra-app a ogni cambio di focus). Quando il thread
 * GTK e' sotto sforzo (i fotogrammi si accumulano), le superfici NON in primo
 * piano rinunciano per prime: mostrano al massimo un fotogramma ogni due
 * (poi uno ogni quattro) finche' la coda non torna vuota; quella in primo
 * piano non rinuncia mai per scelta propria. Mai la risoluzione (4.12). Ogni
 * passaggio a un ritmo ridotto e ogni ritorno si scrive con
 * ph_shell_decisione, con il numero che l'ha causato. Solo dal thread GTK. */
void ph_superficie_primo_piano(GtkWidget *sup, bool primo_piano);

/* Ferma il thread, chiude il flusso (togli_task = "Chiudi", 4.6). Dopo, nessun
 * avviso; il widget resta da togliere dal genitore. */
void ph_superficie_chiudi(GtkWidget *sup, bool togli_task);

/* Aggiunte del 23 nov 2026 (seconda parte): screenshot e registrazione del
 * flusso della superficie (finestra-app non vede il PhSrvFlusso). La
 * superficie inoltra a ph_srv_flusso_screenshot / ph_srv_flusso_registra
 * (servizi.h, (7)); i nomi dei file li decide src/video/cattura.h
 * ("<app> AAAA-MM-GG HH.MM.SS", con " (2)"... : mai sovrascrivere). Solo dal
 * thread GTK; le funzioni di ritorno arrivano sul thread GTK, mai dentro la
 * chiamata, e MAI dopo ph_superficie_chiudi (ne' dopo il dispose). */

/* percorso: file salvato (NULL se non chiesto o errore). immagine: copia IN
 * MEMORIA per gli appunti (mai la texture DMA-BUF mostrata, che terrebbe
 * impegnato un fotogramma del decodificatore); NULL se non chiesta o errore;
 * g_object_ref per tenerla. */
typedef void (*PhSuperficieScreenshotFatto)(GtkWidget *sup, PhSrvEsito esito, const char *frase,
                                            const char *percorso, GdkTexture *immagine, void *utente);
/* dove = PH_SRV_FOTO_FILE e/o PH_SRV_FOTO_APPUNTI (servizi.h). nome_app: il
 * nome da mostrare dell'app. Annullando dal thread GTK la risposta non arriva.
 * L'ordine delle risposte di richieste DIVERSE non e' garantito (una con
 * l'immagine per gli appunti passa da un thread e puo' arrivare dopo una
 * senza): chi chiama distingue le richieste con il proprio `utente`. */
void ph_superficie_screenshot(GtkWidget *sup, unsigned dove, const char *cartella_pc,
                              const char *nome_app, GCancellable *annulla,
                              PhSuperficieScreenshotFatto fatto, void *utente);

/* Avvisi della registrazione (PhSrvRegDati di servizi.h: INIZIATA con il
 * percorso, FINITA, ERRORE). */
typedef void (*PhSuperficieRegAvviso)(GtkWidget *sup, const PhSrvRegDati *d, void *utente);
/* Avvia la registrazione MP4 (con_audio: anche l'audio di YASDE). Gia' in
 * registrazione: ERRORE all'avviso dato qui, quella in corso continua.
 * Durante la registrazione ph_superficie_ridimensiona non cambia la misura. */
void ph_superficie_registra(GtkWidget *sup, const char *cartella_pc, const char *nome_app,
                            bool con_audio, PhSuperficieRegAvviso avviso, void *utente);
/* Ferma: arriva FINITA. Non in registrazione: nulla. */
void ph_superficie_registra_ferma(GtkWidget *sup);
/* true tra ph_superficie_registra e l'avviso FINITA/ERRORE. */
bool ph_superficie_registrando(GtkWidget *sup);

/* Aggiunta del 23 nov (modalita' tablet): come ph_superficie_ridimensiona, con
 * la densita' effettiva dello schermo virtuale (0 = invariata). La superficie
 * la inoltra a ph_srv_flusso_ridimensiona_densita. Solo dal thread GTK. */
void ph_superficie_ridimensiona_densita(GtkWidget *sup, int larghezza, int altezza, int densita);

/* Aggiunta del 23 nov (immagine esatta punto per punto a scala frazionaria):
 * misura ESATTA, in unita' logiche anche non intere, del rettangolo in cui
 * disegnare l'immagine a riposo, a partire dall'angolo in alto a sinistra
 * della superficie (es. 1000 px fisici a scala 1,5 = 666,667; il widget e'
 * allocato a 667). Serve perche' l'allocazione di un widget e' intera e a
 * scala 125% o 150% l'immagine verrebbe ingrandita di una frazione di pixel e
 * ricampionata. 0 x 0 (predefinito) = come prima: l'immagine riempie l'area
 * mantenendo le proporzioni. Durante trascinamento e ridimensionamento resta
 * stesa su tutta l'area. ph_superficie_rettangolo_immagine torna questo
 * rettangolo. Solo dal thread GTK. */
void ph_superficie_misura_esatta(GtkWidget *sup, double larghezza, double altezza);

/* Aggiunta del 23 nov (Ctrl + rotella = zoom): inoltra a ph_srv_flusso_pizzico
 * (stessi argomenti, coordinate del video). Dopo ph_superficie_chiudi nulla.
 * In pausa la superficie non lo inoltra. Solo dal thread GTK. */
void ph_superficie_pizzico(GtkWidget *sup, int video_w, int video_h, int fase, int x, int y, int passo);

#endif
