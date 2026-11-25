/* CONTRATTO del pezzo shell-connessione (prima connessione): la schermata che
 * l'utente vede quando non c'e' ancora un telefono abbinato o collegato.
 * Header di contratto: lo scrive chi coordina, lo include l'ossatura; chi
 * scrive il pezzo lo realizza e non lo modifica. Vedi COMPITO.md (stessa
 * cartella) e src/shell/CONTRATTI.md §15.
 *
 * Quando mostrarla lo decide l'ossatura con ph_shell_connessione_da_mostrare
 * (shell.h). Tutto dal thread GTK. */
#ifndef PH_SHELL_CONNESSIONE_H
#define PH_SHELL_CONNESSIONE_H

#include <gtk/gtk.h>

#include "shell/shell.h"

/* Crea la schermata: un widget a tutta area (halign/valign FILL), opaco, da
 * aggiungere dall'ossatura a PH_STRATO_VELO (al posto del velo, mai insieme).
 * Si aggiorna da sola ascoltando PH_SRV_EV_COLLEGAMENTO; carica il proprio CSS.
 * Ogni ascolto e ogni richiesta in corso (ph_srv_abbina) si chiudono nel suo
 * dispose: dopo, nessuna funzione di ritorno la tocca piu'. Non chiama mai
 * gtk_widget_set_visible su se' stessa: mostrarla o nasconderla e' dell'ossatura.
 * Quando torna visibile riparte dallo stato attuale (campo del codice vuoto). */
GtkWidget *ph_connessione_nuova(PhShell *sh);

#endif
