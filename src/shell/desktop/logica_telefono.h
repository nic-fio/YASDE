/* Logica pura dei widget «Batteria in dettaglio» (tonda) e «Prossima sveglia» (capsula), passo 2b
 * (PIANO.md 8.3): che cosa mostrare per la larghezza e l'altezza VERE del widget, testi, colori
 * d'accento, tempo che manca alla sveglia. Niente widget: si prova senza schermo.
 * I numeri arrivano dal telefono (dato ostile): valori assurdi non si mostrano (mai inventati) e
 * nessuna funzione divide per zero o va oltre i buffer. */
#ifndef PH_SHELL_DESKTOP_LOGICA_TELEFONO_H
#define PH_SHELL_DESKTOP_LOGICA_TELEFONO_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "logica_comp.h"
#include "logica_wx.h"

/* ---- Batteria ---------------------------------------------------------------------------------- */
/* Sotto questa percentuale (compresa) la batteria e' «bassa»: anello corallo invece che verde. */
#define PH_DSK_BAT_SOGLIA_BASSA 20
bool ph_dsk_bat_bassa(int livello);
/* Colore dell'anello: corallo solo se bassa E non in carica (in carica sale: resta verde). */
PhDskAccento ph_dsk_bat_accento(int livello, bool in_carica);
/* App da aprire al clic: Impostazioni standard di Android (il widget la apre solo se l'elenco
 * delle app del telefono la contiene). */
#define PH_DSK_BAT_PACCHETTO_IMPOSTAZIONI "com.android.settings"

/* I gettoni, nell'ordine di priorita'. */
enum { PH_DSK_BAT_G_TEMPERATURA = 0, PH_DSK_BAT_G_SALUTE, PH_DSK_BAT_G_TENSIONE, PH_DSK_BAT_G_N };
/* Quali misure il telefono ha dato davvero (e in un intervallo plausibile): temperatura nota tra
 * -50 e 150 gradi, salute diversa da «sconosciuta», tensione tra 1 e 20000 mV. */
void ph_dsk_bat_gettoni_noti(const PhSrvBatteria *b, bool noti[PH_DSK_BAT_G_N]);

typedef struct {
    int lato_anello;                /* px, quadrato (0 = niente da disegnare) */
    int larghezza_utile;            /* px: lato del quadrato inscritto; nessun testo va oltre */
    int caratteri_max;              /* tetto ai caratteri di stato e gettoni (poi i puntini) */
    bool stato;                     /* riga dello stato sotto l'anello */
    bool gettone[PH_DSK_BAT_G_N];   /* quali gettoni mostrare, uno per riga sotto lo stato */
} PhDskBatDisp;
/* Decide sulla misura vera: il contenuto sta nel quadrato inscritto del tondo (lato corto * 0,72).
 * L'anello non scende sotto 72 px; lo stato ne prende 20, ogni gettone 22: i gettoni (tra quelli
 * `noti`, nell'ordine temperatura, salute, tensione) compaiono finche' l'anello resta di almeno 72,
 * cio' che avanza va all'anello. Senza posto per lo stato c'e' solo l'anello. Misure non positive:
 * tutto a zero/falso. */
PhDskBatDisp ph_dsk_bat_disposizione(int larghezza, int altezza, const bool noti[PH_DSK_BAT_G_N]);

/* Testo sotto l'anello: lo stato della batteria («In carica», «In scarica», ...); dato non pronto:
 * vuoto (in attesa) o «Non disponibile» / «Errore». Stato sconosciuto: vuoto. */
void ph_dsk_bat_stato_testo(PhDskDato dato, const PhSrvBatteria *b, char *out, size_t cap);
/* Testo di un gettone («31,5 °C», «Salute: Buona», «4,12 V»); vuoto se la misura non e' nota. */
void ph_dsk_bat_gettone_testo(int quale, const PhSrvBatteria *b, char *out, size_t cap);

/* ---- Sveglia ------------------------------------------------------------------------------------- */
typedef struct {
    bool tondo;           /* il tondo ambra con l'icona a sinistra */
    int tondo_lato;       /* px */
    int ora_px;           /* corpo dell'ora grande */
    int testo_larghezza;  /* larghezza che resta al testo */
    bool giorno;          /* riga del giorno */
    bool fra;             /* «fra N h M min» */
    bool unite;           /* giorno e «fra» sulla stessa riga («Domani · fra 7 h») */
} PhDskSveDisp;
/* Lato del tondo ambra: SEMPRE un cerchio, il minore tra larghezza e altezza disponibili, con un
 * tetto di 72 px; sotto i 24 px (o con misure non positive) niente tondo: 0. */
#define PH_DSK_SVE_TONDO_MAX 72
int ph_dsk_sve_tondo_lato(int larghezza_utile, int altezza_utile);
/* Decide sulla misura vera. Senza sveglia (presente = false) conta solo il tondo. */
PhDskSveDisp ph_dsk_sve_disposizione(int larghezza, int altezza, bool presente);

/* Minuti che mancano (arrotondati per difetto), da due istanti assoluti in ms UNIX: il cambio
 * dell'ora legale non conta (la differenza non dipende dall'orologio a muro). Sveglia gia'
 * passata: 0, mai negativo. */
int64_t ph_dsk_sve_minuti(uint64_t ora_ms, uint64_t adesso_ms);
bool ph_dsk_sve_passata(uint64_t ora_ms, uint64_t adesso_ms);
/* «fra 2 h 30 min»; vuoto se la sveglia e' gia' passata. */
void ph_dsk_sve_fra(uint64_t ora_ms, uint64_t adesso_ms, char *out, size_t cap);
/* «07:30» nell'ora locale di tz (NULL = fuso locale); vuoto se l'istante e' assurdo. */
void ph_dsk_sve_ora_testo(uint64_t ora_ms, GTimeZone *tz, char *out, size_t cap);
/* Testo al posto dell'ora: vuoto in attesa; «Non disponibile», «Errore»; «Nessuna sveglia». Dato ok
 * con sveglia: vuoto. */
void ph_dsk_sve_assente_testo(PhDskDato dato, bool presente, char *out, size_t cap);
/* «{giorno} · {fra}» (fra vuoto: il solo giorno). */
void ph_dsk_sve_giorno_fra(const char *giorno, const char *fra, char *out, size_t cap);

#endif
