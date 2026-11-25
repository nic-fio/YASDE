/* Logica pura della schermata di prima connessione (nessun widget, nessun
 * thread, nessun telefono): si prova senza schermo (prova_connessione.c).
 * Interno al pezzo shell-connessione. RISERVATEZZA: nessuna funzione qui
 * scrive il codice o l'indirizzo in un log; quelle che li toccano lavorano su
 * buffer del chiamante, che li cancella con ph_cnx_cancella. */
#ifndef PH_CONNESSIONE_LOGICA_H
#define PH_CONNESSIONE_LOGICA_H

#include <stdbool.h>
#include <stddef.h>

#include "shell/servizi.h"

#define PH_CNX_CIFRE 6        /* cifre del codice di abbinamento */
#define PH_CNX_HOST_MAX 64    /* host con il terminatore (servizi.h: al massimo 63 caratteri) */

/* Che cosa sta facendo la schermata (a parte lo stato del collegamento). */
typedef enum {
    PH_CNX_FASE_RIPOSO = 0, /* niente in corso */
    PH_CNX_FASE_IN_CORSO,   /* ph_srv_abbina partita, esito atteso */
    PH_CNX_FASE_ERRORE,     /* ultimo tentativo fallito: si mostra la frase */
    PH_CNX_FASE_RIUSCITO,   /* telefono abbinato: il collegamento sta ripartendo */
    PH_CNX_FASE_RIUSCITO_CERCATO, /* come RIUSCITO, ma il collegamento e' gia' passato per CERCO:
                                     un NON_ABBINATO adesso e' un fatto nuovo, non l'attesa */
} PhCnxFase;

typedef enum {
    PH_CNX_VISTA_ISTRUZIONI = 0, /* CERCO, nessun telefono mai abbinato */
    PH_CNX_VISTA_CERCO_NOTO,     /* CERCO, telefono gia' abbinato ma non raggiungibile */
    PH_CNX_VISTA_ABBINA,         /* NON_ABBINATO: codice */
    PH_CNX_VISTA_PERSO,          /* PERSO prima di ogni collegamento */
} PhCnxVista;

#define PH_CNX_PASSI_MAX 3

/* Che cosa mostrare (stringhe gia' tradotte, valide per sempre o fino alla
 * prossima chiamata di ph_cnx_errore_per sullo stesso buffer). */
typedef struct {
    PhCnxVista vista;
    const char *titolo;
    const char *testo;                 /* NULL: nessuno */
    const char *passi[PH_CNX_PASSI_MAX]; /* passi numerati (solo ISTRUZIONI) */
    int n_passi;
    bool mostra_motivo;                /* sotto: la frase del collegamento (se non vuota) */
    bool riga_arbitraria;              /* «Ci sono piu' telefoni in rete: uso il primo trovato.» */
    const char *attesa;                /* NULL: nessuna riga di attesa */
    bool spinner;                      /* la riga di attesa ha il cerchio che gira */
    bool campo_codice;                 /* campo del codice, «Abbina» ed espansore dell'indirizzo */
    bool mostra_errore;                /* riga d'errore sotto il campo */
    bool cerca_di_nuovo;               /* pulsante «Cerca di nuovo» */
    bool comandi_attivi;               /* false durante un tentativo: tutto inattivo */
} PhCnxContenuto;

/* Dallo stato del collegamento, dalla fase e dal fatto che ci sia gia' stato
 * un errore: che cosa mostrare. false = in questo stato la schermata non ha
 * nulla da mostrare (COLLEGATO, BLOCCATO, CHIUSO): il chiamante lascia
 * l'ultimo contenuto con i comandi inattivi. */
bool ph_cnx_contenuto_per(const PhSrvCollegamento *c, PhCnxFase fase, PhCnxContenuto *k);

/* Fase dopo l'arrivo di un nuovo stato del collegamento: l'errore appartiene
 * alla vista del codice, «riuscito» dura finche' il collegamento riparte.
 * «Riuscito» tollera NON_ABBINATO solo finche' non si e' visto CERCO: dopo,
 * un NON_ABBINATO riporta a riposo (campo vuoto e attivo), senza vicoli ciechi. */
PhCnxFase ph_cnx_fase_con_stato(PhCnxFase fase, PhSrvStato stato);

/* Fase dopo l'esito di ph_srv_abbina; `stato` e' lo stato del collegamento
 * al momento dell'esito (il collegamento puo' aver pubblicato CERCO prima). */
PhCnxFase ph_cnx_fase_dopo_esito(PhSrvAbbEsito esito, PhSrvStato stato);

/* true per RIUSCITO e RIUSCITO_CERCATO. */
bool ph_cnx_fase_riuscita(PhCnxFase fase);

/* Frase d'errore (in `out`) per un esito: usa `frase` dell'esito se c'e', poi
 * `motivo` del collegamento, poi una frase fissa. false per RIUSCITO (nessun
 * errore). Mai il codice ne' indirizzi: sono solo frasi gia' per l'utente. */
bool ph_cnx_errore_per(PhSrvAbbEsito esito, const char *frase, const char *motivo, char *out, size_t cap);

/* Filtro del campo del codice: copia in `out` le sole cifre ASCII di `in`, al
 * massimo PH_CNX_CIFRE (la prima parte), con il terminatore. Torna quante. */
size_t ph_cnx_filtra_codice(const char *in, char *out, size_t cap);

typedef enum {
    PH_CNX_IND_VUOTO = 0, /* niente scritto: si usa la scoperta in rete */
    PH_CNX_IND_VALIDO,
    PH_CNX_IND_NON_VALIDO,
} PhCnxIndirizzo;

/* Legge «indirizzo:porta» (spazi ai bordi ammessi; IPv4 `a.b.c.d:porta`,
 * IPv6 `[a::b]:porta`; porta 1-65535). Con VALIDO riempie host (senza
 * parentesi, al massimo 63 caratteri) e porta. */
PhCnxIndirizzo ph_cnx_leggi_indirizzo(const char *testo, char *host, size_t cap, int *porta);

/* «Abbina» e' attivo: codice di 6 cifre, indirizzo vuoto o valido, nessun
 * tentativo in corso. */
bool ph_cnx_abbina_ammesso(const char *codice, PhCnxIndirizzo indirizzo, bool in_corso);

/* Cancella un buffer (non ottimizzabile via): per codice e indirizzo. */
void ph_cnx_cancella(void *p, size_t n);

#endif
