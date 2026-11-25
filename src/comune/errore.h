/* Errori: ogni funzione che puo' fallire riceve un PhErrore* (puo' essere NULL)
 * e in caso di errore ci scrive una frase in italiano che dice che cosa stava
 * facendo. Convenzione dei valori di ritorno, uguale in tutto il progetto:
 *   int      ->  0 riuscito, -1 errore (PhErrore riempito)
 *   puntatore -> NULL errore
 * Nessuna funzione chiama exit() o abort() per un errore che dipende dal
 * telefono o dalla rete. */
#ifndef PH_ERRORE_H
#define PH_ERRORE_H

typedef struct {
    char testo[256];
} PhErrore;

/* Scrive il messaggio (stile printf). Con e == NULL non fa nulla. Torna sempre -1,
 * cosi' si puo' scrivere: return ph_errore(e, "..."); */
int ph_errore(PhErrore *e, const char *formato, ...) __attribute__((format(printf, 2, 3)));

/* Antepone un contesto al messaggio gia' presente: "contesto: messaggio". Torna -1. */
int ph_errore_contesto(PhErrore *e, const char *formato, ...) __attribute__((format(printf, 2, 3)));

#endif
