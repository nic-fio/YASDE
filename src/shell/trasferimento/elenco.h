/* Elenco dei trasferimenti di un verso, comune ai due pannelli (5.9.3). Un
 * widget che si aggiorna sul posto (le righe non si ricostruiscono a ogni
 * avanzamento: lo scorrimento e il fuoco restano). Solo thread GTK. */
#ifndef PH_SHELL_TRASFERIMENTO_ELENCO_H
#define PH_SHELL_TRASFERIMENTO_ELENCO_H

#include <gtk/gtk.h>

#include "shell/shell.h"

typedef struct PhTrfElenco PhTrfElenco;

/* altezza_max: 150 per l'invio, 120 per la ricezione. Lo stato lo libera il
 * widget quando viene distrutto. */
PhTrfElenco *ph_trf_elenco_nuovo(PhShell *sh, PhSrvVerso verso, int altezza_max);
GtkWidget *ph_trf_elenco_widget(PhTrfElenco *e);
/* Mostra le voci di v del verso dell'elenco: al piu' PH_TRF_ELENCO_MAX, prima
 * quelle da fare (ph_trf_scegli_voci), poi le ultime finite; se le da fare non
 * ci stanno, "e altri N in attesa". "Annulla tutti" copre tutta la coda del
 * verso. Nascosto se non ci sono voci. */
void ph_trf_elenco_aggiorna(PhTrfElenco *e, const PhSrvTrasferimento *v, size_t n);

/* Installazioni di APK (ph_srv_apk_installa): righe proprie, sempre in cima,
 * con avanzamento e "x" (ph_srv_installazione_annulla con id_servizio, 0 = senza
 * "x"). `chiave` la sceglie chi chiama. La riga sparisce con _inst_fine. */
void ph_trf_elenco_inst_nuova(PhTrfElenco *e, guint chiave, guint id_servizio, const char *nome);
void ph_trf_elenco_inst_avanza(PhTrfElenco *e, guint chiave, uint64_t fatti, uint64_t totale);
void ph_trf_elenco_inst_fine(PhTrfElenco *e, guint chiave);

#define PH_TRF_ELENCO_MAX 12 /* come il mockup (xfers.slice(-12)) */

#endif
