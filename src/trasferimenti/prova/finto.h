/* Telefono finto per provare la coda dei trasferimenti senza ADB e senza il
 * pezzo "file". Sostituisce ph_file_invia, ph_file_ricevi, ph_file_info,
 * ph_shell_esegui e ph_adb_viva. Il "telefono" e' una cartella del PC: il
 * percorso /storage/emulated/0/x del telefono e' <radice>/storage/emulated/0/x.
 * Le copie sono lente a comando (pausa per ogni blocco da 4 KiB) e falliscono
 * in base al nome del file:
 *   - nome con "rotto": la copia fallisce dopo 8 KiB (il parziale sparisce);
 *   - nome con "muori": dopo 8 KiB la connessione cade (ph_adb_viva = false). */
#ifndef PH_TR_FINTO_H
#define PH_TR_FINTO_H

#include <stddef.h>

#include "adb/adb.h"

#define FINTO_BLOCCO 4096

/* Cartella che fa da telefono (deve esistere). */
void finto_radice(const char *dir);
/* Pausa per ogni blocco copiato, in microsecondi (0 = nessuna). */
void finto_ritardo_us(int us);
/* true: ph_shell_esegui fallisce sempre. */
void finto_shell_fallisce(int si);
/* Pausa in ogni ph_shell_esegui (uno scanner lento), in millisecondi. */
void finto_shell_ritardo_ms(int ms);
/* Se si', la prossima ricezione di un file con "corsa" nel nome trova, appena
 * partita, un "<destinazione>.parziale" altrui (ph_file_ricevi vero lo crea in
 * modo esclusivo: se c'e' gia', fallisce senza toccarlo). */
void finto_corsa_parziale(int si);
/* Se si', la prossima ricezione di un file con "sosti" nel nome trova, a copia
 * finita, un file dell'utente (non vuoto) al posto della prenotazione. */
void finto_sostituisci_prenotazione(int si);
/* Azzera comandi registrati, contatori e opzioni. */
void finto_reset(void);

PhAdb *finto_adb_nuovo(void);
void finto_adb_libera(PhAdb *a);
void finto_adb_uccidi(PhAdb *a);

/* Quante copie sono andate insieme al massimo (deve essere 1). */
int finto_copie_contemporanee_max(void);
/* Numero di comandi di shell eseguiti; finto_comando(i) ne da' una copia
 * (da liberare con free), NULL se i e' fuori limite. */
size_t finto_comandi_numero(void);
char *finto_comando(size_t i);
/* Percorso reale sul PC di un percorso del telefono (buffer di PATH_MAX). */
void finto_percorso_reale(const char *telefono, char *out, size_t cap);

#endif
