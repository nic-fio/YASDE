/* Logica pura dei widget «Chiamate recenti», «Rete» e «In riproduzione» (PIANO.md 9): quante
 * righe mostrare, che testo scrivere per ogni stato, che icona per ogni livello, quali pulsanti
 * sono attivi. Niente widget: si prova senza schermo (prova_nuovi.c).
 * Nomi, numeri, titoli e nomi di reti arrivano dal telefono (dato ostile): qui si rendono sicuri
 * (UTF-8 valido, niente controlli) e NESSUNA funzione li scrive in un log o in un messaggio.
 * Puntatori NULL: si trattano come «nessun dato». */
#ifndef PH_SHELL_DESKTOP_LOGICA_NUOVI_H
#define PH_SHELL_DESKTOP_LOGICA_NUOVI_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "logica_comp.h"
#include "shell/servizi.h"

/* Copia un campo di dimensione fissa del telefono (puo' non avere il NUL): legge al massimo
 * n_campo byte, mai oltre; dest e' sempre chiuso (cap >= 1; cap 0 o dest NULL: niente). */
void ph_dsk_copia_campo(char *dest, size_t cap, const char *campo, size_t n_campo);

/* ---- Chiamate recenti ---------------------------------------------------------------------- */
/* Pacchetti dell'app Telefono, nell'ordine di preferenza: il widget apre il primo che il telefono
 * ha tra le sue app (non c'e' modo di chiedere l'app predefinita: se nessuno c'e', nessun clic). */
extern const char *const PH_DSK_PACCHETTI_TELEFONO[];
extern const size_t PH_DSK_PACCHETTI_TELEFONO_N;

/* Quante righe ci stanno nella misura (almeno 1). */
size_t ph_dsk_chiamate_righe_max(int larghezza, int altezza);
/* Quante righe mostrare: 0 se la lettura non e' ok; mai oltre il massimo, n e PH_SRV_CHIAMATE_MAX. */
size_t ph_dsk_chiamate_da_mostrare(const PhSrvChiamate *c, int larghezza, int altezza);
/* Testo al posto dell'elenco: «No recent calls» (ok senza righe), «Not available on this phone»
 * (assente o negato); vuoto se ci sono righe o la lettura non e' ancora arrivata. */
void ph_dsk_chiamate_stato_testo(const PhSrvChiamate *c, char *out, size_t cap);
/* Tipo noto (entrante, uscente, persa, rifiutata) o ALTRO. */
int ph_dsk_chiamata_tipo_valido(uint8_t tipo);
/* «Incoming», «Outgoing», «Missed», «Declined»; vuoto per ALTRO. Tradotto. */
const char *ph_dsk_chiamata_tipo_testo(uint8_t tipo);
/* Il nome; se e' vuoto il numero; se anche quello «Hidden number». Sempre testo sicuro. */
void ph_dsk_chiamata_titolo(const PhSrvChiamata *c, char *out, size_t cap);
/* Ora breve: «HH:MM» se la chiamata e' di oggi (giorno civile nel fuso tz, NULL = locale),
 * altrimenti l'etichetta del giorno («Mer 25»); vuoto se l'ora non e' nota o e' assurda. */
void ph_dsk_chiamata_ora(uint64_t ora_ms, uint64_t adesso_ms, GTimeZone *tz, char *out, size_t cap);
/* Accento della riga: corallo per la persa, ardesia per le altre. */
PhDskAccento ph_dsk_chiamata_accento(uint8_t tipo);

/* ---- Rete ---------------------------------------------------------------------------------- */
/* Livello portato in 0..4. */
int ph_dsk_livello_valido(int livello);
/* Testo al posto dei blocchi: «Not available on this phone» se assente o negato; altrimenti vuoto. */
void ph_dsk_rete_stato_testo(const PhSrvReteDettaglio *r, char *out, size_t cap);
/* Wi-Fi: livello (0 se non connesso) e riga di stato: nome della rete, «Connected» se il nome non
 * c'e', «Not connected» (acceso senza rete), «Off». */
void ph_dsk_rete_wifi(const PhSrvReteDettaglio *r, int *livello, char *testo, size_t cap);
/* Mobile: livello (0 se non in servizio), riga di stato («Operatore · 5G», «5G», «Operatore»,
 * «No service», «No SIM») e nota («Mobile data off» solo se in servizio con i dati spenti, valore 0; ignoto (2) = nessuna nota). */
void ph_dsk_rete_mobile(const PhSrvReteDettaglio *r, int *livello, char *testo, size_t cap, char *nota, size_t cap_nota);
/* «5G», «4G», «3G», «2G»; vuoto per una generazione non nota. */
const char *ph_dsk_rete_generazione(uint8_t g);
/* Modo aereo attivo (solo a lettura ok, valore 1; 0 = no, 2 = ignoto: nessuna scritta). */
bool ph_dsk_rete_aereo(const PhSrvReteDettaglio *r);

/* ---- In riproduzione ------------------------------------------------------------------------ */
/* C'e' un brano (lettura ok, stato diverso da «nessuna»). */
bool ph_dsk_media_attivo(const PhSrvMedia *m);
/* In riproduzione adesso (e non in pausa): il pulsante centrale e' «pausa». */
bool ph_dsk_media_suona(const PhSrvMedia *m);
/* Testo al posto del brano: «Nothing playing» (ok senza brano), «Not available on this phone»;
 * vuoto se c'e' un brano o la lettura non e' ancora arrivata. */
void ph_dsk_media_stato_testo(const PhSrvMedia *m, char *out, size_t cap);
/* Titolo («Untitled» se manca) e artista (vuoto se manca), sicuri. */
void ph_dsk_media_titolo(const PhSrvMedia *m, char *out, size_t cap);
void ph_dsk_media_artista(const PhSrvMedia *m, char *out, size_t cap);
/* Il pulsante per `comando` (PH_SRV_MEDIA_CMD_*) e' attivo: c'e' un brano e il bit lo permette. */
bool ph_dsk_media_puo(const PhSrvMedia *m, int comando);
/* Pacchetto dell'app che suona, valido per un clic; NULL se non c'e' o non e' accettabile. */
const char *ph_dsk_media_pacchetto(const PhSrvMedia *m);

#endif
