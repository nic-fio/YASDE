/* Logica pura dei widget «vecchi» rifatti nel passo 2b (agente A): Memoria del telefono (A2),
 * Notifiche recenti (A4), Calendario del giorno (A5) e Stato del telefono. Che cosa mostrare si
 * decide qui, sulla larghezza e l'altezza VERE del widget (non sul livello piccolo/medio/grande):
 * niente widget, niente servizi, si prova senza schermo (prova_vecchi.c).
 *
 * Le misure sono stime sul contenuto (altezze di titolo, righe, barra) piu' l'imbottitura della
 * mattonella: dove non ci sta tutto, si toglie per primo cio' che e' meno importante (la legenda,
 * poi la barra) e non si lascia mai un contenuto tagliato a meta'. I numeri in ingresso (misure)
 * possono essere qualunque cosa: negativi o enormi non rompono nulla. */
#ifndef PH_SHELL_DESKTOP_LOGICA_VECCHI_H
#define PH_SHELL_DESKTOP_LOGICA_VECCHI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "logica.h"

/* Imbottitura della mattonella (la stessa di ph_dsk_imbottitura per quella forma: la prova lo
 * controlla): serve a ricavare lo spazio utile dalla misura del widget. */
#define PH_DSK_VECCHI_PAD_O 20
#define PH_DSK_VECCHI_PAD_V 16
/* Spazio utile dentro la scheda (mai negativo). */
void ph_dsk_vecchi_utile(int larghezza, int altezza, int *utile_l, int *utile_a);

/* ---- Memoria del telefono (A2) ------------------------------------------------------------------ */
/* Che cosa si vede: il numero grande (spazio libero) c'e' sempre; la dicitura «libero su N GB» sta
 * accanto al numero se la larghezza e' abbondante, altrimenti sotto (si legge sempre per intero);
 * la barra a fette se ci sta; la legenda solo se ci stanno TUTTE le voci (una legenda a meta' fa
 * credere che manchino categorie). n_voci = voci della legenda (0 = nessuna). colonne = quante ne
 * userebbe il componente per la legenda alla larghezza utile (la decide lui, con la voce piu' larga
 * vera: ph_dsk_fette_legenda_colonne); < 1 vale 1. numero_alto = altezza da dare al numero perche'
 * sia il piu' grande possibile senza lasciare vuoti tra lui e la barra. */
typedef struct {
    bool barra, legenda, nota_accanto;
    int numero_alto;
} PhDskMemoriaDisp;
PhDskMemoriaDisp ph_dsk_memoria_disp(int larghezza, int altezza, size_t n_voci, int colonne);

/* Fette della barra: solo le categorie che il telefono ha saputo leggere (e con almeno un byte),
 * piu' «Altro» = usato meno la somma delle categorie lette, se positivo e se almeno una categoria
 * e' stata letta. Mai fette inventate: senza categorie lette non ce n'e' nessuna. */
typedef enum {
    PH_DSK_MEM_IMMAGINI = 0,
    PH_DSK_MEM_VIDEO,
    PH_DSK_MEM_AUDIO,
    PH_DSK_MEM_APP,
    PH_DSK_MEM_ALTRO,
    PH_DSK_MEM_N
} PhDskMemCategoria;
typedef struct {
    PhDskMemCategoria categoria;
    uint64_t byte;
} PhDskMemFetta;
/* out ha almeno PH_DSK_MEM_N posti; torna quante fette ha scritto. det puo' essere NULL. */
size_t ph_dsk_memoria_fette(const PhSrvMemoriaDettaglio *det, uint64_t usata, PhDskMemFetta *out);
/* Colore (accento) di una categoria; fuori elenco: ardesia. */
int ph_dsk_memoria_accento(PhDskMemCategoria c);
/* Pacchetto dell'app Impostazioni standard di Android (clic di memoria e batteria): il widget
 * lo apre solo se il telefono lo ha tra le sue app. */
const char *ph_dsk_pacchetto_impostazioni(void);

/* ---- Notifiche recenti (A4) ---------------------------------------------------------------------- */
/* Quante righe ci stanno nella misura (almeno 1: con meno spazio si taglia, non si toglie la riga). */
size_t ph_dsk_notifiche_righe_max(int larghezza, int altezza);

/* ---- Calendario del giorno (A5) ------------------------------------------------------------------- */
typedef struct {
    bool colonna_giorno; /* giorno in grande a sinistra; altrimenti la data in una riga in alto */
    size_t max_impegni;  /* almeno 1 */
} PhDskCalDisp;
PhDskCalDisp ph_dsk_calendario_disp(int larghezza, int altezza);
/* L'impegno e' in corso adesso: non di tutto il giorno, iniziato e non finito. Un impegno senza
 * durata (fine = inizio) non e' mai «in corso». */
bool ph_dsk_impegno_in_corso(const PhDskEvento *e, uint64_t adesso_ms);

/* ---- Stato del telefono -------------------------------------------------------------------------- */
/* Righe di dettaglio (rete, pannello, schermo) che stanno nella misura: 0..3. Con 200x100 0, con
 * 250x124 1, con 320x164 3: le stesse di prima alle misure di prima. */
size_t ph_dsk_stato_righe(int larghezza, int altezza);

/* Il nome di un pacchetto arrivato dal telefono (app del calendario) e' accettabile: non vuoto, al
 * massimo 255 byte, UTF-8 valido, senza caratteri di controllo. NULL: no. */
bool ph_dsk_pacchetto_valido(const char *pacchetto);

/* ---- Accenti e testi -------------------------------------------------------------------------------- */
/* Colore di una voce (accento 1..PH_DSK_ACC_N-1, mai quello del tema): lo stesso pacchetto ha sempre
 * lo stesso colore; pacchetto NULL o vuoto: ardesia. */
int ph_dsk_accento_pacchetto(const char *pacchetto);
/* Colore dell'i-esimo impegno del giorno (a rotazione, mai il tema). */
int ph_dsk_accento_impegno(size_t i);
/* Nome dell'accento come nelle classi del CSS («corallo» per ph-desktop-tinta-corallo). */
const char *ph_dsk_accento_nome(int accento);
/* Prima lettera in maiuscolo («novembre» -> «Novembre»); il resto com'e'. UTF-8 non valido: copia
 * com'e'. cap >= 1. */
void ph_dsk_maiuscola_prima(const char *testo, char *out, size_t cap);

#endif
