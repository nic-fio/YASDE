/* Interno dell'ossatura (non e' un contratto: lo includono solo i file di
 * questa cartella). */
#ifndef PH_SHELL_OSSATURA_H
#define PH_SHELL_OSSATURA_H

#include <gtk/gtk.h>

#include "config/config.h"
#include "shell/shell.h"

/* Il programma YASDE 2.1: applicazione GTK a istanza unica, finestra
 * principale F-01, servizi veri, pezzi installati. Torna il codice d'uscita. */
int ph_ossatura_esegui(int argc, char **argv);

/* Installa i pezzi presenti in questa compilazione (pezzi_presenti.h), nell'ordine
 * di pezzi.h. Lo usano il programma e il demo. */
void ph_ossatura_installa_pezzi(PhShell *sh);

/* I servizi veri (servizi_veri.c) sopra collegamento, dati, componente,
 * video, audio, trasferimenti, config. NULL con la frase in e se non sono
 * disponibili in questa compilazione. Thread GTK, dopo che il ciclo
 * principale di GTK e' pronto. pref sono le preferenze lette all'avvio;
 * pref_salvabile = false se il file era illeggibile in modo grave (esito -1 di
 * ph_cfg_preferenze_carica: mai salvare, config.h). */
PhServizi *ph_ossatura_servizi_veri(const PhCfgPreferenze *pref, bool pref_salvabile, PhErrore *e);

/* ---- Finestra principale (finestra.c) ------------------------------------
 * La finestra host F-01 con la radice della shell dentro, il pulsante di
 * schermo intero (F11), la striscia da 6 px in schermo intero, il velo di
 * blocco / connessione (velo.c) e lo stato host (visibile, schermo intero)
 * verso la shell. app puo' essere NULL (demo: finestra semplice). Non la
 * presenta. Thread GTK. */
GtkWindow *ph_finestra_nuova(GtkApplication *app, PhShell *sh);
/* Da chiamare prima di ph_shell_libera: la finestra smette di parlare con la
 * shell (la finestra puo' restare viva ancora un poco). */
void ph_finestra_distacca(GtkWindow *win);

/* Il velo (velo.c): lo mette la finestra in PH_STRATO_VELO. Si libera con la
 * radice. */
void ph_velo_installa(PhShell *sh);

/* LA chiusura ordinata, una funzione sola: la usano il programma (ossatura.c) e
 * demo-ossatura, cosi' il demo prova la sequenza vera. Da chiamare dal thread
 * GTK, anche dentro il segnale close-request:
 *  1. la finestra si nasconde e si distrugge (win puo' essere NULL);
 *  2. ph_shell_libera: distrugge la radice ("destroy" dei pezzi) con shell e
 *     servizi ancora vivi, chiude i flussi;
 *  3. libera_servizi(utente) (puo' essere NULL): i servizi, MAI prima.
 * Dopo ogni passo si svuota la coda dei lavori in sospeso. */
typedef void (*PhOssaturaLiberaServizi)(void *utente);
void ph_ossatura_chiudi(GtkWindow *win, PhShell *sh, PhOssaturaLiberaServizi libera_servizi, void *utente);

/* Allinea gli appunti del PC (GdkClipboard) con quelli del telefono (4.11,
 * appunti.c). Si libera con la radice. */
void ph_appunti_installa(PhShell *sh);

#endif
