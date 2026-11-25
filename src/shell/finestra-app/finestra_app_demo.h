/* Solo per il demo di shell-finestra-app: riproduce a comando gli stati che
 * l'utente ottiene con mouse e tastiera (il demo per le fotografie gira in uno
 * schermo senza input). Non fa parte del contratto della shell. */
#ifndef PH_SHELL_FINESTRA_APP_DEMO_H
#define PH_SHELL_FINESTRA_APP_DEMO_H

#include <stdbool.h>

#include "shell/shell.h"

/* Un tocco mandato alla superficie (registro delle prove del mouse). */
#define PH_FA_DEMO_TOCCHI_MAX 64
typedef struct {
    int azione;         /* PH_SRV_TOCCO_* */
    unsigned pulsanti;  /* bit di TOCCO: 1 sinistro, 2 destro, 4 centrale */
} PhFaDemoTocco;

/* azione: "massimizza", "minimizza", "registra", "menu" (apre il menu dei tre
 * punti), "compat-si" / "compat-no" (la spunta «Compatibilita'»), "zoom", "sotto",
 * "aggancia-sinistra". indice = posizione della finestra in ordine di
 * apertura (0 = la prima). false se non esiste o l'azione e' rifiutata. */
bool ph_finestra_app_demo_azione(PhShell *sh, const char *azione, unsigned indice);
/* Ultima misura e densita' chieste al telefono per la finestra (per le prove). */
bool ph_finestra_app_demo_richiesta(PhShell *sh, unsigned indice, int *w, int *h, int *densita);

/* Stato della compatibilita': in finestre.h, spunta del menu, salvataggi chiesti dalla voce. */
bool ph_finestra_app_demo_stato(PhShell *sh, unsigned indice, bool *in_finestre, bool *spunta,
                                unsigned *salvataggi);
/* true se i figli della cornice (pulsanti, contenuto) stanno dove dicono le costanti e finestre.h. */
bool ph_finestra_app_demo_geometria(PhShell *sh, unsigned indice);
/* ---- Vetro (prove) ---- */
/* Esito della visita dei nodi di una cornice (ph_finestra_app_demo_albero). */
typedef struct {
    int n_blur;         /* nodi blur in tutto l'albero */
    int blur_annidati;  /* blur dentro un altro blur */
    int clip_zona;      /* clip esattamente (0, 0, larghezza, PH_FIN_TITOLO_H) */
    int catena_ok;      /* di questi: clip -> color matrix -> blur con raggio 2 x 26 */
} PhFaDemoAlbero;
bool ph_finestra_app_demo_e_cornice(GtkWidget *w);
GtkWidget *ph_finestra_app_demo_cornice(PhShell *sh, unsigned indice);
/* attivo = la modalita' decisa alla realizzazione; classe = ha la classe CSS ph-finestra-app-vetro. */
bool ph_finestra_app_demo_vetro(PhShell *sh, unsigned indice, bool *attivo, bool *classe);
/* I paintable tenuti dalla cornice per cio' che sta dietro il titolo (senza riferimento in piu'); torna quanti sono. */
guint ph_finestra_app_demo_dietro(PhShell *sh, unsigned indice, GdkPaintable **p, guint max);
/* La scelta della sfocatura per il nome del tipo del renderer (funzione pura del prodotto). */
bool ph_finestra_app_demo_vetro_per_renderer(const char *nome_tipo);
/* Quante volte e' stata disegnata la cornice (senza i giri di prova). */
unsigned ph_finestra_app_demo_snapshot_cornice(PhShell *sh, unsigned indice);
/* Quante volte sono state disegnate tutte le cornici (somma). */
unsigned ph_finestra_app_demo_snapshot_conta(PhShell *sh);
/* Disegna la cornice in uno snapshot di prova e ne visita i nodi. */
bool ph_finestra_app_demo_albero(PhShell *sh, unsigned indice, PhFaDemoAlbero *o);
/* Accende/spegne il vetro di tutte le cornici (per confrontare i ridisegni a riposo). */
void ph_finestra_app_demo_vetro_imposta(PhShell *sh, bool acceso);
/* Il menu «altri comandi» (NULL se non e' mai stato aperto). */
GtkWidget *ph_finestra_app_demo_menu(PhShell *sh);
/* Visita i nodi di un albero qualunque (per esempio quello della radice della shell). */
void ph_finestra_app_demo_albero_nodo(GskRenderNode *n, float larg, PhFaDemoAlbero *o);
/* Quante volte e' stata chiamata sincronizza (solo diagnosi). */
unsigned ph_finestra_app_demo_sincronizza_conta(PhShell *sh);
/* L'assestamento e' finito (il ciclo dei fotogrammi della finestra-app si e' fermato). */
bool ph_finestra_app_demo_assestato(PhShell *sh);
/* Chiude subito la finestra (senza l'idle della chiusura normale). */
bool ph_finestra_app_demo_chiudi_ora(PhShell *sh, unsigned indice);
/* Porta la finestra davanti e la sposta con il gesto del titolo: angolo in (x, y). */
bool ph_finestra_app_demo_posiziona(PhShell *sh, unsigned indice, double x, double y);
/* Simula PH_SRV_EV_PROFILO. */
void ph_finestra_app_demo_profilo_ricaricato(PhShell *sh);

/* ---- Mouse (prove) ---- */
/* Il contenuto della finestra ha gia' un'immagine su cui tradurre il punto. */
bool ph_finestra_app_demo_mouse_pronto(PhShell *sh, unsigned indice);
/* Come il gesto del mouse nel punto `zona` ("contenuto", "titolo", "pulsante", "angolo") della finestra:
 * azione "giu" (pulsante GDK `bottone`), "muovi", "su", "annulla" (perdita del puntatore).
 * false se la finestra o la zona non esistono. Prende lo stesso percorso del gesto di GTK. */
bool ph_finestra_app_demo_mouse(PhShell *sh, unsigned indice, const char *zona, const char *azione,
                                unsigned bottone);
/* Registro dei tocchi mandati alla superficie dall'ultimo azzeramento. */
unsigned ph_finestra_app_demo_tocchi(PhShell *sh, PhFaDemoTocco *out, unsigned max);
void ph_finestra_app_demo_tocchi_azzera(PhShell *sh);
bool ph_finestra_app_demo_gesto_in_corso(PhShell *sh);

#endif
