/* Stato interno del launcher (F-04): condiviso tra launcher.c e il codice del
 * solo demo (prova_demo.c). Non e' un contratto verso gli altri pezzi. */
#ifndef PH_SHELL_LAUNCHER_INTERNO_H
#define PH_SHELL_LAUNCHER_INTERNO_H

#include <stdbool.h>

#include "shell/shell.h"

/* Chiave del dato attaccato al widget di una cella: la PhCella. */
#define PH_LAUNCHER_CHIAVE_CELLA "ph-cella"

typedef struct PhLauncher PhLauncher;
typedef struct PhCella PhCella;

/* Il testo cercato, normalizzato. Sta in una scatola a parte, di proprieta' del
 * filtro (che la usa), cosi' il filtro non deve conoscere lo stato intero. */
typedef struct {
    char *testo;
} PhRicerca;

struct PhLauncher {
    int rif; /* contatore: i widget (celle, fabbrica) tengono un riferimento */
    PhShell *sh;
    PhServizi *srv;
    GtkWidget *riquadro, *campo, *scorrevole, *griglia, *nessuna;
    GtkWidget *velo_menu, *menu, *voce_apri, *voce_aggiungi, *voce_rimuovi;
    GtkWidget *voce_fissa, *voce_togli; /* fissa / togli dalla barra */
    GListStore *deposito;      /* tutte le voci, nell'ordine del telefono */
    GtkCustomFilter *filtro;
    GListModel *filtrato;      /* deposito + filtro */
    GPtrArray *voci;           /* le stesse voci (riferimenti nostri) per confronto e Invio */
    PhRicerca *ricerca;        /* testo cercato (posseduto dal filtro) */
    char *menu_pacchetto;      /* app del menu aperto, NULL se chiuso */
    GHashTable *celle;         /* pacchetto -> PhCella delle celle ora visibili */
    GtkEventController *esc;   /* Esc con il menu aperto, sulla finestra */
    GtkWidget *esc_finestra;
    guint id_srv[4];
    guint id_area, id_fissate;
    bool distrutto;
};

struct PhCella {
    PhLauncher *l;
    GtkWidget *cella, *icona, *nome_etichetta;
    char *pacchetto; /* NULL se non legata a una voce */
    char *nome;
    GdkTexture *texture; /* NULL = segnaposto */
};


/* Apre il menu contestuale per `pacchetto` al punto (x, y) di `riferimento`. */
void ph_launcher_interno_menu_apri(PhLauncher *l, GtkWidget *riferimento, double x, double y,
                                   const char *pacchetto);

#endif
