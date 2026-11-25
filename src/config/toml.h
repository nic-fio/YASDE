/* Lettore e scrittore del sottoinsieme di TOML usato dai file di
 * configurazione (e dai file delle traduzioni). Scritto da zero, nessuna
 * libreria. Interno al pezzo `config`: gli altri pezzi usano config.h.
 *
 * Sottoinsieme riconosciuto (tutto il resto e' un errore con numero di riga):
 *   # commento             da '#' a fine riga (anche dopo un valore)
 *   [nome]                 tabella; una sola volta per nome
 *   [[nome]]               tabella ripetuta (elenchi); ogni ripetizione e' una voce
 *   chiave = valore        chiave "nuda" [A-Za-z0-9_-]+ oppure tra virgolette
 *   "stringa"              UTF-8 valido, su una sola riga; escape \" \\ \n \t \r
 *                          \b \f \uXXXX \UXXXXXXXX (mai il carattere nullo)
 *   123  -45  +7           interi a 64 bit (fuori intervallo = errore)
 *   1.5  -0.25  2e3  1.5e-3  decimali finiti (niente nan/inf)
 *   true  false            booleani
 * Non esistono: array, tabelle in linea, stringhe su piu' righe o letterali con
 * apici singoli, date, interi esadecimali, trattini bassi nei numeri.
 * Le righe finiscono con \n o \r\n. Il nome di tabella e' [A-Za-z0-9_.-]{1,64}
 * (il punto non ha significato speciale). Chiavi doppie nella stessa tabella,
 * tabella ripetuta con lo stesso nome di una normale, byte nulli, UTF-8 non
 * valido nelle stringhe: errori. Limiti: file 4 MiB, riga 8192 byte, 1024
 * tabelle, 2048 voci per tabella (bastano ai file reali; il controllo dei
 * doppioni e' quadratico e con questi limiti costa pochi millisecondi anche
 * con un file ostile). */
#ifndef PH_TOML_H
#define PH_TOML_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comune/buffer.h"
#include "comune/errore.h"

#define PH_TOML_MAX_FILE (4u * 1024u * 1024u)
#define PH_TOML_MAX_RIGA 8192u
#define PH_TOML_MAX_TABELLE 1024u
#define PH_TOML_MAX_VOCI 2048u

typedef enum { PH_TOML_STRINGA, PH_TOML_INTERO, PH_TOML_DECIMALE, PH_TOML_BOOLEANO } PhTomlTipo;

typedef struct {
    char *chiave;
    PhTomlTipo tipo;
    char *s;   /* per PH_TOML_STRINGA (mai NULL) */
    int64_t i; /* per PH_TOML_INTERO */
    double d;  /* per PH_TOML_DECIMALE */
    bool b;    /* per PH_TOML_BOOLEANO */
    int riga;
} PhTomlVoce;

typedef struct {
    char *nome; /* "" per le chiavi prima della prima intestazione */
    bool ripetuta;
    int riga;
    PhTomlVoce *voci;
    size_t n, capacita;
} PhTomlTabella;

typedef struct {
    PhTomlTabella *tabelle; /* tabelle[0] e' sempre la radice */
    size_t n, capacita;
} PhToml;

/* Legge `lunghezza` byte (non serve il terminatore). 0 riuscito, -1 errore:
 * il messaggio e' "riga N: ..." (N parte da 1). In caso di errore *doc e'
 * vuoto e non va liberato. */
int ph_toml_leggi(const char *testo, size_t lunghezza, PhToml *doc, PhErrore *e);
void ph_toml_libera(PhToml *doc);

/* Voce con quel nome e quel tipo; NULL se manca o ha un altro tipo. */
const PhTomlVoce *ph_toml_voce(const PhTomlTabella *t, const char *chiave, PhTomlTipo tipo);

/* Scrittura: aggiunge testo a un buffer. 0 riuscito, -1 memoria finita. Le
 * chiavi non "nude" sono scritte tra virgolette; i caratteri di controllo
 * nelle stringhe diventano \u00XX, quindi cio' che si scrive si rilegge. */
int ph_toml_scrivi_commento(PhBuffer *b, const char *testo);
int ph_toml_scrivi_tabella(PhBuffer *b, const char *nome, bool ripetuta); /* riga vuota + intestazione */
int ph_toml_scrivi_stringa(PhBuffer *b, const char *chiave, const char *valore);
int ph_toml_scrivi_intero(PhBuffer *b, const char *chiave, int64_t valore);
int ph_toml_scrivi_decimale(PhBuffer *b, const char *chiave, double valore);
int ph_toml_scrivi_booleano(PhBuffer *b, const char *chiave, bool valore);

#endif
