/* Navigatore di cartelle e file: la parte comune dei tre elenchi del pezzo.
 *  - PH_NAV_TEL_CARTELLE: selettore di cartelle del telefono (5.9.4), dentro il
 *    pannello di invio: Su, percorso digitabile, ricerca di cartelle, elenco
 *    delle sottocartelle, "Nuova cartella", "Usa questa cartella";
 *  - PH_NAV_TEL_FILE: elenco di scarica (5.9.2): Su, percorso, ricerca di file
 *    e cartelle, selezione multipla che resta cambiando cartella;
 *  - PH_NAV_PC_CARTELLE: selettore di cartelle del PC (5.9.4): come il primo
 *    senza ricerca, con il percorso mostrato (non digitabile, come il mockup),
 *    letto con GIO in modo asincrono.
 * Solo thread GTK. Il navigatore e' contato in riferimenti perche' le richieste
 * GIO tornano sempre, anche dopo la chiusura: chi lo crea lo libera con
 * ph_nav_distruggi (annulla tutto, poi nessuna funzione di ritorno lo tocca).
 * Per i servizi vale la regola di servizi.h: una richiesta annullata non
 * risponde piu'. I testi di ricerca e i percorsi passati ai servizi restano
 * validi solo durante la chiamata (il backend li copia). */
#ifndef PH_SHELL_TRASFERIMENTO_NAVIGATORE_H
#define PH_SHELL_TRASFERIMENTO_NAVIGATORE_H

#include <gtk/gtk.h>
#include <stdbool.h>

#include "shell/shell.h"

typedef enum { PH_NAV_TEL_CARTELLE = 0, PH_NAV_TEL_FILE, PH_NAV_PC_CARTELLE } PhNavModo;

typedef struct PhNav PhNav;

typedef struct {
    /* "Usa questa cartella": percorso intero (gia' pulito). */
    void (*usa)(void *utente, const char *percorso);
    /* Cambia il numero di file scelti (solo PH_NAV_TEL_FILE). */
    void (*selezione)(void *utente, size_t scelti);
} PhNavGestori;

PhNav *ph_nav_nuovo(PhShell *sh, PhNavModo modo, const PhNavGestori *g, void *utente);
GtkWidget *ph_nav_widget(PhNav *n);

/* Va a `percorso` e lo elenca (di nuovo ogni volta che il pannello o il
 * selettore si apre). Telefono: percorso gia' pulito. PC: se la cartella non
 * esiste o non si legge si ripiega sulla cartella personale. Svuota la ricerca. */
void ph_nav_apri(PhNav *n, const char *percorso);
/* Annulla ricerche e richieste in corso (il pannello si chiude). */
void ph_nav_ferma(PhNav *n);
/* Cartella mostrata ora (percorso intero, mai NULL). */
const char *ph_nav_percorso(PhNav *n);

/* Selezione dei file (PH_NAV_TEL_FILE): percorsi interi nell'ordine di scelta. */
size_t ph_nav_scelti_n(PhNav *n);
const char *ph_nav_scelto(PhNav *n, size_t i);
void ph_nav_scelti_togli(PhNav *n, const char *percorso);
void ph_nav_scelti_svuota(PhNav *n);

void ph_nav_distruggi(PhNav *n);

/* Solo per il demo: scrive nel campo di ricerca (come se l'utente digitasse) e
 * sceglie un file come con un clic. */
void ph_nav_prova_cerca(PhNav *n, const char *testo);
void ph_nav_prova_scegli(PhNav *n, const char *percorso);

/* Etichetta a una riga che si accorcia con i puntini senza allargare chi la
 * contiene (usata anche dagli altri file del pezzo). classe puo' essere NULL. */
GtkWidget *ph_trf_etichetta_ellissi(const char *classe);

#endif
