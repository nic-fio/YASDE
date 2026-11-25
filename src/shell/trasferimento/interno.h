/* Parti interne del pezzo trasferimento condivise tra trasferimento.c e i
 * ganci di prova del demo (prova.c, compilato solo nel demo). Non e' un contratto. */
#ifndef PH_SHELL_TRASFERIMENTO_INTERNO_H
#define PH_SHELL_TRASFERIMENTO_INTERNO_H

#include <gtk/gtk.h>
#include <stdbool.h>

#include "shell/shell.h"
#include "shell/trasferimento/elenco.h"
#include "shell/trasferimento/navigatore.h"
#include "shell/trasferimento/percorsi.h"

#define N_RAPIDI 4

typedef struct {
    PhShell *sh;
    PhServizi *srv;
    bool morto;
    GtkWidget *radice;
    guint ev_tr, ev_profilo;
    PhTrfFoto *foto;

    /* Invia al telefono */
    GtkWidget *p_invio;
    PhTrfElenco *el_invio;
    GtkWidget *zona, *dest_invio_et, *cambia_invio, *rapidi_gruppo, *rapidi[N_RAPIDI];
    PhNav *nav_invio;
    bool scelta_invio;
    char *dest_invio; /* cartella del telefono, percorso intero pulito */

    /* Scarica dal telefono */
    GtkWidget *p_ric;
    PhTrfElenco *el_ric;
    GtkWidget *scarica, *dest_ric_et, *cambia_ric;
    PhNav *nav_ric, *nav_pc;
    bool scelta_pc, ric_iniziato;
    char *dest_ric; /* cartella del PC */

    /* Trascinamento */
    GtkWidget *riquadro;
    /* Riferimenti ai widget di primo livello: la radice stacca i figli prima del
     * suo "destroy", i nostri ascolti devono poter ancora toccarli. */
    GtkWidget *tenuti[3];

    /* APK in attesa di conferma, uno alla volta */
    GQueue *apk;
    bool apk_attivo;
    guint inst_prova;    /* solo per il demo: installazione da annullare */
    guint inst_prossima; /* chiavi delle righe di installazione */
} Trf;

void ph_trf_apri_pannello(Trf *t, PhSrvVerso verso, bool alterna);
void ph_trf_aggiorna_invio(Trf *t);
guint ph_trf_installa_apk(Trf *t, const char *percorso); /* dopo la conferma; id del servizio */
void ph_trf_aggiorna_ricezione(Trf *t);

#endif
