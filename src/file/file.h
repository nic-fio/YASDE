/* Copia di file tra PC e telefono ed elenco delle cartelle, con il servizio
 * "sync:" di ADB. Specifica 5.9 (trasferimento file) e 5.9.4 (cartelle).
 * Usa solo src/adb/adb.h (canali). Ogni operazione apre il suo canale "sync:",
 * quindi piu' operazioni possono girare insieme da thread diversi. */
#ifndef PH_FILE_H
#define PH_FILE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adb/adb.h"
#include "comune/errore.h"

/* Chiamata durante la copia, dal thread che copia. */
typedef void (*PhFileProgresso)(uint64_t fatti, uint64_t totale, void *utente);

typedef enum {
    PH_FILE_RIUSCITO = 0,
    PH_FILE_ERRORE = -1,
    PH_FILE_ANNULLATO = -2,     /* *annulla e' diventato true */
    PH_FILE_NON_TROVATO = -3,   /* file o cartella inesistente sul telefono */
    PH_FILE_NON_CONSENTITO = -4, /* Android nega l'accesso (5.9.4: "Accesso non consentito dal sistema") */
    PH_FILE_ESISTE = -5         /* il file di destinazione o il suo ".parziale" esiste gia' sul PC: nulla e' stato toccato */
} PhFileEsito;

/* PC -> telefono. Il file remoto viene creato o sovrascritto: scegliere un
 * nome libero e' compito di chi chiama (con ph_file_info). La data di
 * modifica del file locale viene conservata. annulla e progresso facoltativi.
 * Se annullato o fallito a meta', il file parziale sul telefono viene tolto. */
PhFileEsito ph_file_invia(PhAdb *a, const char *locale, const char *remoto,
                          PhFileProgresso progresso, void *utente,
                          const atomic_bool *annulla, PhErrore *e);

/* Telefono -> PC. Mai sovrascrivere un file dell'utente: "<locale>.parziale"
 * si crea in modo esclusivo (se esiste gia': PH_FILE_ESISTE) e il rename finale
 * non sostituisce un "<locale>" esistente, salvo un file normale vuoto messo
 * da chi chiama come prenotazione del nome.
 * Scrive su "<locale>.parziale" e rinomina alla fine; al file
 * ricevuto da' la data di modifica che ha sul telefono. Se annullato o
 * fallito, il parziale viene cancellato. */
PhFileEsito ph_file_ricevi(PhAdb *a, const char *remoto, const char *locale,
                           PhFileProgresso progresso, void *utente,
                           const atomic_bool *annulla, PhErrore *e);

typedef struct {
    char *nome;          /* solo il nome, senza cartella */
    bool cartella;
    bool collegamento;   /* link simbolico */
    uint64_t dimensione;
    int64_t modificato;  /* secondi dal 1970 */
} PhFileVoce;

typedef struct {
    PhFileVoce *voci;
    size_t numero;
} PhFileElenco;

/* Elenco di una cartella del telefono, senza "." e "..", ordinato: prima le
 * cartelle, poi i file, per nome senza distinguere maiuscole. I file nascosti
 * (nome che inizia con '.') ci sono: li filtra chi mostra. */
PhFileEsito ph_file_elenca(PhAdb *a, const char *cartella, PhFileElenco *elenco, PhErrore *e);
void ph_file_elenco_libera(PhFileElenco *elenco);

/* Informazioni su un percorso del telefono (voce->nome resta NULL). */
PhFileEsito ph_file_info(PhAdb *a, const char *percorso, PhFileVoce *voce, PhErrore *e);

#endif
