/* Client ADB scritto da zero: connessione cifrata al telefono via Wi-Fi,
 * canali multipli sulla stessa connessione, comandi shell.
 * Specifica: 1.5, 2.4 ("ADB nostro"). Non sa nulla di video, audio o file.
 *
 * Modello di concorrenza (vincolante):
 *  - una PhAdb ha UN thread lettore interno che legge i pacchetti dal socket
 *    e li smista ai canali; le scritture sul socket sono serializzate da un
 *    mutex (un pacchetto ADB non viene mai spezzato da un altro);
 *  - ogni PhCanale ha la sua coda di dati in arrivo; ph_canale_leggi blocca
 *    (con timeout) solo chi legge quel canale;
 *  - tutte le funzioni qui sotto si possono chiamare da thread diversi, anche
 *    insieme sullo stesso PhAdb. Sullo stesso PhCanale: un lettore e uno
 *    scrittore insieme si', due lettori o due scrittori no;
 *  - il controllo di flusso di ADB va rispettato: dopo un WRTE si aspetta
 *    l'OKAY prima del WRTE successivo sullo stesso canale; in ricezione
 *    l'OKAY si manda quando i dati sono stati messi in coda, ma si TRATTIENE
 *    finche' la coda del canale supera una soglia (due volte il massimo di
 *    un pacchetto) e riparte quando chi legge l'ha svuotata sotto la soglia:
 *    cosi' un lettore lento frena il telefono invece di riempire la memoria;
 *  - un collegamento morto senza chiusura (telefono uscito dal Wi-Fi) va
 *    riconosciuto entro circa 10 secondi (keepalive e TCP_USER_TIMEOUT);
 *  - la libreria ignora SIGPIPE per tutto il processo (OpenSSL scrive con
 *    write()): chi la usa non deve contare su quel segnale;
 *  - se la connessione cade, tutte le letture e scritture in corso tornano
 *    errore subito (nessun thread resta bloccato) e ph_adb_viva diventa false.
 */
#ifndef PH_ADB_H
#define PH_ADB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "comune/buffer.h"
#include "comune/errore.h"

/* ---- Chiave del PC -------------------------------------------------------
 * Chiave RSA 2048 del PC, in un file PEM (PKCS#8, lo stesso formato del file
 * "adbkey" dello strumento adb standard: cosi' in sviluppo si puo' usare un
 * telefono gia' abbinato). Serve per il certificato TLS e per l'abbinamento. */
typedef struct PhChiave PhChiave;

/* Carica la chiave dal file; se il file non esiste la genera e la salva con
 * permessi 0600 (creando la cartella). */
PhChiave *ph_chiave_carica_o_crea(const char *percorso, PhErrore *e);
void ph_chiave_libera(PhChiave *c);
/* Chiave pubblica nel formato ADB (struttura RSAPublicKey di Android in
 * base64, seguita da " nome@host"), terminata da '\0' dentro il buffer:
 * e' cio' che l'abbinamento manda al telefono. */
int ph_chiave_pubblica_adb(const PhChiave *c, PhBuffer *uscita, PhErrore *e);
/* Accesso alla chiave e al certificato autofirmato per chi fa TLS
 * (connessione e abbinamento). I puntatori sono EVP_PKEY* e X509* di OpenSSL,
 * restano di proprieta' della PhChiave. */
void *ph_chiave_evp(const PhChiave *c);
void *ph_chiave_x509(const PhChiave *c);

/* ---- Connessione ---------------------------------------------------------*/
typedef struct PhAdb PhAdb;

/* Si collega a host:porta (porta "debug wireless" del telefono), fa lo
 * scambio CNXN/STLS, passa a TLS 1.3 con il certificato della chiave e
 * completa il saluto. timeout_ms vale per tutta la procedura.
 * Se il telefono non riconosce la chiave (PC non abbinato) l'errore lo dice
 * in modo riconoscibile: *non_abbinato = true (puntatore facoltativo). */
PhAdb *ph_adb_collega(const char *host, uint16_t porta, const PhChiave *chiave,
                      int timeout_ms, bool *non_abbinato, PhErrore *e);
/* Chiude tutto: i canali ancora aperti diventano inutilizzabili ma vanno
 * comunque passati a ph_canale_chiudi. Aspetta la fine del thread lettore. */
void ph_adb_chiudi(PhAdb *a);
bool ph_adb_viva(PhAdb *a);
/* Stringa di presentazione del telefono (il "banner" del CNXN: contiene
 * ro.product.model ecc.). Valida finche' vive la PhAdb. */
const char *ph_adb_presentazione(PhAdb *a);

/* ---- Canali --------------------------------------------------------------*/
typedef struct PhCanale PhCanale;

/* Apre un servizio ADB, per esempio "shell,v2,raw:ls", "sync:",
 * "localabstract:yasde". Errore se il telefono rifiuta. */
PhCanale *ph_canale_apri(PhAdb *a, const char *servizio, int timeout_ms, PhErrore *e);

#define PH_CANALE_CHIUSO 0     /* l'altro lato ha chiuso e la coda e' vuota */
#define PH_CANALE_ERRORE (-1)  /* connessione caduta o errore: PhErrore riempito */
#define PH_CANALE_TEMPO (-2)   /* timeout: nessun dato; non e' un errore */

/* Legge da 1 a n byte. timeout_ms < 0 = aspetta senza limite. */
ssize_t ph_canale_leggi(PhCanale *c, void *dati, size_t n, int timeout_ms, PhErrore *e);
/* Legge esattamente n byte o fallisce (-1). Chiusura prima di n byte = errore. */
int ph_canale_leggi_tutto(PhCanale *c, void *dati, size_t n, int timeout_ms, PhErrore *e);
/* Scrive tutti gli n byte (spezzandoli nei pacchetti ADB) o fallisce (-1). */
int ph_canale_scrivi(PhCanale *c, const void *dati, size_t n, PhErrore *e);
/* Chiude il canale e libera la struttura. Sblocca un'eventuale lettura in
 * corso in un altro thread (che torna PH_CANALE_ERRORE). */
void ph_canale_chiudi(PhCanale *c);

/* ---- Comandi shell -------------------------------------------------------*/
/* Esegue un comando con il protocollo shell v2 e aspetta la fine.
 * uscita/errori (facoltativi, si puo' passare NULL) ricevono stdout e stderr;
 * *codice (facoltativo) il codice di uscita del comando.
 * Torna -1 se il comando non si e' potuto eseguire o e' scaduto il tempo;
 * un codice di uscita diverso da zero NON e' un errore della funzione. */
int ph_shell_esegui(PhAdb *a, const char *comando, PhBuffer *uscita, PhBuffer *errori,
                    int *codice, int timeout_ms, PhErrore *e);

#endif
