/* Logica pura dell'ossatura (nessun widget, nessun thread, nessun telefono):
 * si prova senza schermo (prova_ossatura.c). Interno al pezzo shell-ossatura. */
#ifndef PH_OSSATURA_LOGICA_H
#define PH_OSSATURA_LOGICA_H

#include <gdk/gdk.h>
#include <stdbool.h>
#include <stddef.h>

#include "shell/servizi.h"

/* 1 se in questa compilazione ci sono tutti i pezzi sotto (meson.build). */
#ifndef PH_OSSATURA_SERVIZI_VERI
#define PH_OSSATURA_SERVIZI_VERI 0
#endif

/* ---- Velo (4.8) ---------------------------------------------------------- */
typedef struct {
    bool lucchetto;      /* icona del lucchetto sopra il titolo */
    const char *titolo;  /* gia' tradotto */
    const char *testo;   /* testo fisso (solo BLOCCATO), altrimenti NULL */
    bool mostra_motivo;  /* sotto il titolo la frase del collegamento */
    bool pulsante;       /* "Riconnetti ora" */
} PhVeloContenuto;

/* true se in questo stato il velo e' visibile (e riempie *c). COLLEGATO e
 * CHIUSO: nascosto. */
bool ph_velo_contenuto(PhSrvStato stato, PhVeloContenuto *c);
/* Come sopra dalla fotografia intera: con schermo_spento (COLLEGATO o
 * BLOCCATO) il velo e' quello del blocco. */
bool ph_velo_contenuto_per(const PhSrvCollegamento *col, PhVeloContenuto *c);

/* ---- Che cosa copre la shell secondo lo stato (prima connessione, velo) ------
 * Una sola cosa alla volta in PH_STRATO_VELO: la schermata della prima
 * connessione (ph_shell_connessione_da_mostrare: NON_ABBINATO sempre; CERCO e
 * PERSO solo se in questa esecuzione non c'e' mai stato un collegamento),
 * altrimenti il velo di 4.8 (blocco, connessione persa DOPO il primo collegamento,
 * e senza la schermata anche CERCO e NON_ABBINATO), altrimenti niente.
 * COLLEGAMENTO e PERSO dopo il primo collegamento convivono cosi': la schermata
 * e' solo per l'inizio e per l'abbinamento perso/revocato, il velo per tutto il resto. */
typedef enum { PH_VISTA_NESSUNA = 0, PH_VISTA_VELO, PH_VISTA_CONNESSIONE } PhVista;
PhVista ph_ossatura_vista(const PhSrvCollegamento *col, bool collegato_in_sessione, bool ha_connessione);

/* ---- Finestra host (4.14) ------------------------------------------------ */
/* Lo stato della GdkToplevel (GdkToplevelState) dice se lo schermo del PC
 * mostra la finestra: minimizzata o sospesa = non visibile (su Wayland la
 * sospensione e' l'unico segnale). */
bool ph_host_visibile_da_stato(unsigned stato_toplevel);

/* ---- Comando di shell per creare una cartella sul telefono ---------------
 * "mkdir '<percorso>'" con il percorso tra apici singoli: un apice dentro il
 * percorso diventa '\'' , quindi nessun carattere puo' uscire dagli apici.
 * 0 riuscito; -1 se il percorso e' vuoto o non entra in capacita. */
int ph_ossatura_comando_mkdir(const char *percorso, char *out, size_t capacita);

/* ---- Macchina di un flusso video ------------------------------------------
 * Tiene la regola «un flusso persa la connessione diventa PERSO, al ritorno
 * si riapre da solo e si dice RIPRESO». Funzione pura: chi la usa (sv_flussi.c)
 * la chiama con il lucchetto del flusso preso. */
typedef enum {
    PH_FL_APRENDO = 0, /* un lavoro di apertura e' in coda o in corso */
    PH_FL_ATTIVO,
    PH_FL_PERSO,       /* senza connessione: aspetta la sessione nuova */
    PH_FL_MORTO,       /* errore del codificatore o apertura fallita: non riparte */
} PhFlStato;

typedef enum {
    PH_FL_EV_SESSIONE_PERSA = 0, /* la sessione e' caduta (o si chiude) */
    PH_FL_EV_SESSIONE_NUOVA,     /* c'e' una sessione buona */
    PH_FL_EV_APERTO,             /* il lavoro ha aperto la sessione video */
    PH_FL_EV_APERTO_SENZA_SESSIONE, /* il lavoro non ha trovato (o ha perso) la sessione */
    PH_FL_EV_APERTO_FALLITO,     /* il telefono ha rifiutato l'apertura con la connessione viva */
    PH_FL_EV_CODIFICATORE,       /* VIDEO_EVENTO 4: la sessione e' morta */
} PhFlEvento;

typedef struct {
    PhFlStato stato;         /* stato dopo l'evento */
    bool ha_avviso;
    PhSrvFlussoEvento avviso;/* avviso per la shell, se ha_avviso */
    bool riapri;             /* mettere in coda un lavoro di apertura */
} PhFlPasso;

/* gia_aperto: il flusso e' gia' stato aperto almeno una volta (la seconda e'
 * una ripresa: RIPRESO invece di APERTO). */
PhFlPasso ph_fl_passo(PhFlStato stato, PhFlEvento ev, bool gia_aperto);

/* Misura del flusso per lo specchio (nessuna misura dalla shell): schermo del
 * telefono portato a multipli di 8 dentro 64..2560, senza deformarlo. */
void ph_ossatura_misura_specchio(int schermo_w, int schermo_h, int *w, int *h);

#if PH_OSSATURA_SERVIZI_VERI
#include "collegamento/collegamento.h"
#include "dati/dati.h"
#include "file/file.h"

/* Esito di file.h -> esito dei servizi. */
PhSrvEsito ph_ossatura_esito_file(PhFileEsito e);
/* Stato del collegamento -> stato dei servizi. */
PhSrvStato ph_ossatura_stato_da_collegamento(PhColStato s);
/* Avviso del collegamento -> fotografia dei servizi (nessun seriale, nessun
 * indirizzo: solo modello, misure e densita'). */
void ph_ossatura_collegamento_da_avviso(const PhColAvviso *a, PhSrvCollegamento *c);
/* Stato del telefono (dati.h) -> fotografia dei servizi (resta com'e' il
 * pannello). */
void ph_ossatura_telefono_da_stato(const PhDatiStato *s, PhSrvTelefono *t);
/* Motivo del rifiuto di PANNELLO_IMPOSTA dalla frase del telefono (il
 * protocollo 2.9 non ha un codice: le frasi sono quelle di
 * telefono/pannello/it/yasde/Pannello.java e NascostePannello.java; il
 * riconoscimento sta in UN punto solo, ph_ossatura_esito_pannello). */
PhSrvPannelloEsito ph_ossatura_esito_pannello(const char *frase);
/* Corpo di PANNELLO_EVENTO (0x6e): aggiorna acceso e in_mano. false se il
 * corpo e' troppo corto. */
bool ph_ossatura_pannello_da_evento(const unsigned char *corpo, size_t n, PhSrvTelefono *t);
/* Corpo dell'OK di PANNELLO_STATO (0x61). */
bool ph_ossatura_pannello_da_stato(const unsigned char *corpo, size_t n, PhSrvTelefono *t);
#endif

/* Scala per GDK_DPI_SCALE: sempre col punto decimale, qualunque sia la lingua
 * del PC (con la virgola GDK leggerebbe «1,25» come 1). */
void ph_ossatura_scala_testo(double scala, char *out, size_t cap);

#endif
