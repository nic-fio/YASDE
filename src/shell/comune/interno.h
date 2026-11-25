/* Parti interne del pezzo comune della shell (non e' un contratto: nessun
 * altro pezzo lo include nel PRODOTTO). Unica eccezione: le PROVE degli altri
 * pezzi possono includerlo per le funzioni d'ispezione del vetro
 * (ph_vetro_radice_*), che servono a guardare l'albero dei nodi della radice. */
#ifndef PH_SHELL_COMUNE_INTERNO_H
#define PH_SHELL_COMUNE_INTERNO_H

#include <gtk/gtk.h>
#include <stdbool.h>

/* Elenco di ascoltatori con id, sicuro se durante un avviso qualcuno si
 * aggiunge o si toglie (gli aggiunti non ricevono l'avviso in corso). Solo
 * thread GTK. La funzione e' conservata come void (*)(void) e riconvertita da
 * chi chiama (conversione ammessa per i puntatori a funzione). */
typedef void (*PhFnGenerica)(void);

typedef struct {
    guint id;
    int evento;
    PhFnGenerica fn;
    void *utente;
    bool tolto;
} PhAscoltatore;

typedef struct {
    GArray *voci; /* di PhAscoltatore */
    guint prossimo;
    int in_corso;
} PhAscoltatori;

void ph_ascoltatori_inizia(PhAscoltatori *a);
void ph_ascoltatori_finisci(PhAscoltatori *a);
guint ph_ascoltatori_aggiungi(PhAscoltatori *a, int evento, PhFnGenerica fn, void *utente);
void ph_ascoltatori_togli(PhAscoltatori *a, guint id);
/* Per l'emissione: ph_ascoltatori_apri, poi per i < n ph_ascoltatori_voce(a, i)
 * (NULL se tolta o di un altro evento), poi ph_ascoltatori_chiudi. */
guint ph_ascoltatori_apri(PhAscoltatori *a);
const PhAscoltatore *ph_ascoltatori_voce(PhAscoltatori *a, guint i, int evento);
void ph_ascoltatori_chiudi(PhAscoltatori *a);

/* Registro degli oggetti vivi (PhServizi, PhShell): ph_srv_smetti e
 * ph_shell_smetti lo consultano, cosi' una chiamata arrivata dopo la
 * liberazione (per esempio dal "destroy" di una radice che muore tardi) non
 * tocca memoria liberata. Qualunque thread. Due oggetti non vivi insieme hanno
 * indirizzi diversi: un indirizzo riusato dopo la liberazione per un oggetto
 * nuovo risulterebbe vivo (rischio accettato: i pezzi smettono comunque
 * di ascoltare nel proprio "destroy"). */
void ph_vivi_aggiungi(const void *p);
void ph_vivi_togli(const void *p);
bool ph_vivi_e(const void *p);

/* Vetro (vetro.c): lo stato sta sulla radice a strati. _inizia alla creazione
 * della radice, _finisci nel suo dispose PRIMA di staccare i figli (da li' in
 * poi nessun ritratto la chiama piu'), _snapshot e' il suo disegno: i figli in
 * ordine, e prima di ogni figlio la sfocatura dietro le superfici registrate
 * che contiene. _disegni e _ritratti: quante sfocature e quanti ritratti
 * nell'ultimo disegno (per le prove). */
void ph_vetro_radice_inizia(GtkWidget *radice);
void ph_vetro_radice_finisci(GtkWidget *radice);
void ph_vetro_radice_snapshot(GtkWidget *radice, GtkSnapshot *s);
unsigned ph_vetro_radice_disegni(GtkWidget *radice);
unsigned ph_vetro_radice_ritratti(GtkWidget *radice);
/* Solo per le prove (vetro.c): disegni della radice, voci registrate, ritratto k-esimo. */
unsigned ph_vetro_radice_snapshot_conta(GtkWidget *radice);
unsigned ph_vetro_radice_voci(GtkWidget *radice);
GdkPaintable *ph_vetro_radice_ritratto(GtkWidget *radice, unsigned k);

/* Registra le risorse del pezzo comune (stile di base). */
void ph_shell_comune_risorse(void);

#endif
