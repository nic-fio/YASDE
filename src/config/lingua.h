/* Lingua dell'interfaccia (specifica 5.11, ridotta al solo inglese il 24 nov 2026). I testi nel
 * codice sono in italiano e si scrivono cosi':
 *     ph_t("Inviato al telefono")
 * Con la lingua italiana ph_t() restituisce il testo stesso; con l'inglese la
 * traduzione presa da data/en/ (file .txt), incorporata nell'eseguibile al momento della
 * compilazione (nessun file da installare). Se manca la traduzione si ottiene
 * l'italiano: il test `lingua` (meson test) fallisce prima che succeda.
 * Il test controlla ogni ph_t("...") dei sorgenti: l'argomento deve essere un
 * letterale (o piu' letterali adiacenti); ph_t(variabile) e' un errore del test.
 * Per un letterale messo in una tabella e tradotto piu' tardi si scrive
 *     PH_T_("Testo")      (non traduce, vale il testo stesso)
 * e poi, quando serve, ph_tv(tabella[i]); la scansione riconosce PH_T_ come ph_t
 * e pretende la traduzione inglese del testo, mentre ph_tv non e' controllata.
 *
 * FILE DELLE TRADUZIONI (data/en/ (file .txt)): sottoinsieme di TOML (vedi toml.h),
 * una riga per testo, chiave = italiano, valore = inglese, entrambi tra
 * virgolette con le stesse sequenze di escape del C piu' comuni:
 *     "Inviato al telefono" = "Sent to the phone"
 *     "Copiati {numero} file" = "{numero} files copied"
 *
 * SEGNAPOSTO. Si usa lo stile {nome} (nome = [A-Za-z_][A-Za-z0-9_]*), non
 * printf: l'ordine delle parole cambia da una lingua all'altra e con {nome} la
 * traduzione puo' riordinare. Per scrivere una graffa letterale: {{ e }}. Il
 * carattere '%' non ha alcun significato. I segnaposto dell'italiano e
 * dell'inglese devono essere gli stessi (stessi nomi, stesso numero di volte):
 * lo controllano il generatore e il test.
 *
 * COMPOSIZIONE SICURA. ph_lingua_componi() sostituisce i segnaposto; il modello
 * (anche se arriva da un file) non e' mai una stringa di formato di printf.
 * I valori NON possono essere NULL: sarebbe un errore del chiamante e la
 * funzione scrive una stringa vuota e torna (size_t)-1.
 *     ph_lingua_componi(buf, sizeof buf, ph_t("Copiati {numero} file"),
 *                       "numero", "3", (const char *)NULL);
 * Nel codice si passa sempre ph_t("...") come modello, cosi' il test lo vede.
 *
 * Lingua attiva: dal 24 nov 2026 l'app e' SOLO in inglese. ph_lingua_inizializza()
 * imposta sempre l'inglese, qualunque sia la preferenza nel file (un valore
 * vecchio "it" o "auto" si legge e si ignora) e qualunque sia l'ambiente del PC.
 * Va chiamata all'avvio, prima di
 * creare thread; ph_t() si puo' poi chiamare da qualunque thread. */
#ifndef PH_LINGUA_H
#define PH_LINGUA_H

#include <stdbool.h>
#include <stddef.h>

#include "config/config.h"

typedef enum { PH_LINGUA_IT, PH_LINGUA_EN } PhLingua;

PhLingua ph_lingua_da_ambiente(void);
/* Imposta l'inglese (la preferenza e' ignorata); torna PH_LINGUA_EN. */
PhLingua ph_lingua_inizializza(PhCfgLingua preferenza);
void ph_lingua_imposta(PhLingua lingua);
PhLingua ph_lingua_attiva(void);

/* Marcatore per la scansione dei sorgenti: non traduce. */
#define PH_T_(testo) (testo)

/* Identica a ph_t ma non controllata dalla scansione dei sorgenti: SOLO con
 * testi marcati PH_T_ (non usarla per aggirare il controllo). */
const char *ph_tv(const char *italiano);

/* Traduzione nella lingua attiva; mai NULL (NULL in ingresso da "").
 * Il puntatore restituito vale per tutta la vita del programma. */
const char *ph_t(const char *italiano);

/* Traduzione inglese di un testo italiano, NULL se manca (per il test). */
const char *ph_lingua_traduzione_en(const char *italiano);

/* Scrive in `out` (sempre terminato se capacita > 0) il modello con i
 * segnaposto sostituiti. Le coppie nome, valore terminano con un puntatore
 * NULL (al massimo 16 coppie); un segnaposto senza coppia, o malformato, resta
 * com'e'; un valore NULL e' un errore (vedi sopra). Torna la lunghezza che servirebbe (come
 * snprintf): se >= capacita il testo e' stato troncato, su un confine di
 * carattere UTF-8. */
size_t ph_lingua_componi(char *out, size_t capacita, const char *modello, ...)
    __attribute__((sentinel));

/* Confronta i segnaposto di due testi (stessi nomi e stesso numero di volte,
 * in qualunque ordine). 1 uguali, 0 diversi, -1 se uno dei due e' malformato
 * (graffa isolata o segnaposto non valido) o ne ha piu' di 32. */
int ph_lingua_segnaposto_uguali(const char *a, const char *b);

#endif
