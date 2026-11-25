/* Logica pura della barra (F-03): niente widget, quindi si prova senza
 * schermo (prova_barra.c). Il resto del pezzo (barra.c) la usa e basta. */
#ifndef PH_SHELL_BARRA_LOGICA_H
#define PH_SHELL_BARRA_LOGICA_H

#include <stdbool.h>
#include <stddef.h>

#include "shell/shell.h"

/* Indicatore sotto la voce di una finestra (mockup, taskItems): una finestra
 * minimizzata non e' mai "attiva", anche se il gestore la segna ancora tale. */
typedef enum {
    PH_BARRA_IND_APERTA = 0, /* barretta grigia piena */
    PH_BARRA_IND_ATTIVA,     /* barretta color accento */
    PH_BARRA_IND_MINIMIZZATA /* solo il bordo */
} PhBarraIndicatore;

PhBarraIndicatore ph_barra_indicatore(const PhShellFinestra *f);

/* Etichetta accessibile: "{nome} (aperta)" o "{nome} (in pausa)". Se non ci sta
 * viene troncata (mai fuori dal buffer). */
void ph_barra_etichetta(const PhShellFinestra *f, char *out, size_t capacita);
/* Suggerimento: il nome, o "{nome} . in pausa, stream sospeso" se minimizzata. */
void ph_barra_suggerimento(const PhShellFinestra *f, char *out, size_t capacita);

/* Millisecondi da adesso al prossimo minuto esatto, dati i secondi (0-59) e i
 * microsecondi (0-999999) dell'ora locale. Sempre tra 1 e 60000: a minuto
 * esatto torna 60000, mai 0 (un timer a 0 ms girerebbe a vuoto). Valori fuori
 * campo (secondo intercalare) si riportano nei limiti. */
unsigned ph_barra_ms_al_minuto(int secondi, int microsecondi);

/* "HH:MM" con due cifre; valori fuori campo si riportano nei limiti. */
void ph_barra_ora_testo(int ore, int minuti, char *out, size_t capacita);

/* Etichetta accessibile dell'orologio: ora e data per esteso (gia' nella lingua
 * del sistema), "HH:MM, data". Troncata in sicurezza, sempre UTF-8 valido. */
void ph_barra_etichetta_orologio(const char *ora, const char *data, char *out, size_t capacita);

/* ---- App fissate e ordine delle voci (23 nov) --------------------------- */

/* Un elemento della barra: la finestra (indice nell'elenco di
 * ph_shell_finestre, -1 = app non aperta) e la posizione fra le fissate (-1 =
 * non fissata). */
typedef struct {
    int finestra;
    int fissata;
} PhBarraElemento;

/* Ordine delle voci: prima le FISSATE nell'ordine dell'elenco (con la loro
 * finestra se e' aperta, altrimenti sola icona), poi le finestre aperte non
 * fissate nell'ordine di apertura (lo specchio compreso). Una fissata si
 * abbina alla prima finestra del suo pacchetto; le altre finestre dello stesso
 * pacchetto restano fra le non fissate. Pacchetti vuoti fra le fissate si
 * ignorano. Scrive al massimo `max` elementi e torna quanti. */
size_t ph_barra_ordina(const char (*fissate)[PH_CFG_LUN_APP], size_t n_fissate,
                       const PhShellFinestra *finestre, size_t n_finestre, PhBarraElemento *out,
                       size_t max);

/* Testi di una voce fissata NON aperta: etichetta = il nome, suggerimento =
 * "Apri {nome}" (mockup). */
void ph_barra_etichetta_chiusa(const char *nome, char *out, size_t capacita);
void ph_barra_suggerimento_chiusa(const char *nome, char *out, size_t capacita);

/* Menu del clic destro su un'icona. */
typedef enum {
    PH_BARRA_MENU_CHIUDI = 0, /* "Chiudi app": solo se l'app ha una finestra */
    PH_BARRA_MENU_FISSA,      /* "Fissa sulla barra": se non e' fissata */
    PH_BARRA_MENU_TOGLI       /* "Togli dalla barra": se e' fissata */
} PhBarraMenuVoce;
/* Scrive le voci del menu (al massimo 2) secondo lo stato; torna quante. */
size_t ph_barra_menu(bool aperta, bool fissata, PhBarraMenuVoce out[2]);
const char *ph_barra_menu_testo(PhBarraMenuVoce v);

/* Data per esteso nella lingua dell'app ("Lunedi' 23 novembre 2026" /
 * "Monday, November 23, 2026"). giorno_settimana 1 = lunedi' .. 7 = domenica;
 * valori fuori campo: stringa vuota. */
void ph_barra_data_lunga(int giorno_settimana, int giorno, int mese, int anno, char *out,
                         size_t capacita);

/* ---- Calendario del PC (disegnato dalla barra, nella lingua dell'app) ----- */

/* Nome del mese (1-12) e abbreviazione del giorno della colonna (0 = lunedi'
 * .. 6 = domenica), gia' tradotti; fuori campo: stringa vuota. */
const char *ph_barra_nome_mese(int mese);
const char *ph_barra_giorno_breve(int colonna);
/* Colonna del giorno 1 (0 = lunedi') e numero di giorni del mese; fuori campo:
 * 0 e 0. */
void ph_barra_mese(int anno, int mese, int *colonna_primo, int *giorni);
/* Sposta (anno, mese) di delta_mesi, restando fra l'anno 1 e il 9999. */
void ph_barra_mese_sposta(int *anno, int *mese, int delta_mesi);

#endif
