/* Logica pura del widget Meteo (CONTRATTI.md §18): icone e testi dai codici WMO, formattazione
 * dei valori (mai numeri inventati: non noto = «—»), quali ore e giorni mostrare per stile e
 * misura, barre della settimana, testi degli stati. Niente widget: si prova senza schermo. */
#ifndef PH_SHELL_DESKTOP_LOGICA_METEO_H
#define PH_SHELL_DESKTOP_LOGICA_METEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "logica.h"

/* Gli stili del widget (campo `stile` del profilo). */
typedef enum {
    PH_DSK_METEO_ADESSO = 0, /* icona grande, temperatura, condizione, massima e minima */
    PH_DSK_METEO_ORE,        /* le prossime ore a colonne */
    PH_DSK_METEO_SETTIMANA,  /* i giorni con minima e massima a barre */
    PH_DSK_METEO_COMPATTO,   /* una riga */
    PH_DSK_METEO_OGGI,       /* percepita, vento, pioggia, umidita' (e le prossime ore nel Grande) */
    PH_DSK_METEO_CURVA,      /* l'andamento della temperatura sulle prossime ore */
    PH_DSK_METEO_SENZA_FONDO, /* icona, temperatura, luogo e condizione sullo sfondo: niente vetro (6) */
    PH_DSK_METEO_STILI
} PhDskMeteoStile;
const char *ph_dsk_meteo_stile_nome(unsigned stile); /* tradotto, NULL oltre l'ultimo */

/* Gruppi di condizioni (icone): codice WMO -> gruppo. Codice ignoto = NESSUNA (nessuna icona). */
typedef enum {
    PH_DSK_COND_NESSUNA = 0,
    PH_DSK_COND_SERENO,        /* 0 */
    PH_DSK_COND_POCO_NUVOLOSO, /* 1-2 */
    PH_DSK_COND_NUVOLOSO,      /* 3 */
    PH_DSK_COND_NEBBIA,        /* 45, 48 */
    PH_DSK_COND_PIOGGERELLA,   /* 51-57 */
    PH_DSK_COND_PIOGGIA,       /* 61-67, 80-82 */
    PH_DSK_COND_NEVE,          /* 71-77, 85-86 */
    PH_DSK_COND_TEMPORALE      /* 95-99 */
} PhDskCond;
PhDskCond ph_dsk_meteo_gruppo(int codice);
/* Testo della condizione nella lingua attiva («Pioggia debole»); "" se il codice e' ignoto. */
const char *ph_dsk_meteo_condizione(int codice);

/* Valori: non noto (NAN, infinito, fuori scala) = «—» (trattino lungo). */
void ph_dsk_meteo_temp(double gradi, char *out, size_t cap);   /* «14°», «-3°» (mai «-0°») */
void ph_dsk_meteo_temp_c(double gradi, char *out, size_t cap); /* «14,5 °C» con il separatore della lingua */
void ph_dsk_meteo_vento(double kmh, char *out, size_t cap);    /* «12 km/h» */
void ph_dsk_meteo_percento(double v, char *out, size_t cap);   /* «63 %», ristretto a 0-100 */
void ph_dsk_meteo_mm(double mm, char *out, size_t cap);        /* «0,4 mm» */
/* Titolo: «Meteo — <localita'>» se c'e' il nome, altrimenti «Meteo». */
void ph_dsk_meteo_titolo(const char *localita, char *out, size_t cap);
/* «HH:MM» dell'istante nel fuso del luogo (scarto in secondi dall'UTC). */
void ph_dsk_meteo_ora_luogo(int64_t ora_s, int scarto_s, char *out, size_t cap);
/* «HH» (ora) dell'istante nel fuso del luogo, per le colonne delle ore. */
void ph_dsk_meteo_ora_breve(int64_t ora_s, int scarto_s, char *out, size_t cap);
/* «Oggi» o il giorno abbreviato («Mer») del giorno di `giorno_s` (mezzanotte locale in secondi
 * Unix); `oggi_s` e' la mezzanotte locale di oggi. */
void ph_dsk_meteo_giorno_nome(int64_t giorno_s, int64_t oggi_s, int scarto_s, char *out, size_t cap);

/* Quante colonne di ore, quanti giorni, quante righe di dettaglio per stile e misura (0 = non usato). */
size_t ph_dsk_meteo_n_ore(unsigned stile, PhCfgWidgetDim d);
size_t ph_dsk_meteo_n_giorni(unsigned stile, PhCfgWidgetDim d);
size_t ph_dsk_meteo_n_dettagli(PhCfgWidgetDim d);

/* agente C: inizio (stile «Adesso» che cresce con la misura VERA, passo 2b) ---------------------------
 * Soglie in px della misura del widget (scheda intera, imbottitura compresa). Piccolo: icona e
 * temperatura; piu' largo: anche luogo, condizione e massima/minima; piu' alto: anche la curva delle
 * prossime ore; ancora piu' alto: anche i prossimi giorni con il tratto minima-massima. Mai un
 * elemento in meno al crescere di una misura (la prova lo controlla su tutta la griglia). */
#define PH_DSK_METEO_ADESSO_LARG_TESTO 240 /* da qui: luogo, condizione, massima/minima */
#define PH_DSK_METEO_ADESSO_LARG_CURVA 240 /* la curva ha bisogno di almeno questa larghezza... */
#define PH_DSK_METEO_ADESSO_ALT_CURVA 230  /* ...e di questa altezza (testa + curva + attribuzione) */
#define PH_DSK_METEO_ADESSO_ALT_GIORNO 28  /* altezza di un giorno */
#define PH_DSK_METEO_ADESSO_GIORNI_MIN 2   /* con meno spazio di cosi' non si mostrano giorni */
#define PH_DSK_METEO_ADESSO_ALT_AVVISO 48  /* da togliere all'altezza se sopra c'e' l'avviso dei dati vecchi */
#define PH_DSK_METEO_ADESSO_ALT_CURVA_BASE 96
#define PH_DSK_METEO_ADESSO_ALT_CURVA_EXTRA 64 /* quanto puo' crescere la curva con lo spazio che avanza */
typedef struct {
    bool testo;        /* luogo, condizione, massima/minima accanto alla temperatura */
    bool curva;        /* curva delle prossime ore */
    size_t n_ore;      /* punti della curva (0 se non c'e') */
    size_t n_giorni;   /* giorni con il tratto minima-massima (0 se non ci sono) */
    int altezza_curva; /* px della curva (0 se non c'e') */
    int corpo_temp;    /* 1 medio, 2 grande: corpo del numero della temperatura */
    int lato_icona;    /* px dell'icona della condizione */
} PhDskMeteoSezioni;
/* Funzione PURA: misure non positive o enormi sono ammesse (si limitano). */
PhDskMeteoSezioni ph_dsk_meteo_sezioni(int larghezza, int altezza);
/* Le sezioni dello stile dato: «Senza fondo» mostra solo icona, temperatura e testo (mai curva ne'
 * giorni, l'altezza non conta); gli altri come ph_dsk_meteo_sezioni. */
PhDskMeteoSezioni ph_dsk_meteo_sezioni_stile(unsigned stile, int larghezza, int altezza);
bool ph_dsk_meteo_sezioni_uguali(const PhDskMeteoSezioni *a, const PhDskMeteoSezioni *b);
/* «Max 18° · Min 11°» non ci sta per intero (larghezza_lunga > disponibile, in px): si usa la forma
 * breve «18° / 11°». Misure non positive: false (forma lunga). */
bool ph_dsk_meteo_forma_breve(int disponibile, int larghezza_lunga);
/* agente C: fine */

/* Estremi (minima e massima) dei giorni dati, per le barre; false se nessun valore noto. */
bool ph_dsk_meteo_estremi(const PhMeteoGiorno *g, size_t n, double *minimo, double *massimo);
/* Posizione della barra di un giorno rispetto agli estremi: da e a in 0..1 (larghezza minima
 * garantita). false se minima o massima non sono note (nessuna barra). */
bool ph_dsk_meteo_barra(double min, double max, double gmin, double gmax, double *da, double *a);

/* Testi degli stati. `luogo_scelto`: c'e' una citta' scelta a mano nel profilo. */
typedef struct {
    char messaggio[192];   /* frase principale (vuota se i dati si mostrano e va tutto bene) */
    char dettaglio[96];    /* «ultimo dato alle 14:32» */
    bool mostra_dati;      /* ci sono dati (freschi o vecchi) da disegnare */
    bool grigio;           /* dati vecchi: si disegnano in grigio */
    bool invito_localita;  /* «Scegli una localita'...» */
} PhDskMeteoStato;
void ph_dsk_meteo_stato(PhSrvMeteoStato stato, bool ha_dati, const char *frase, int64_t ricevuta_s, int scarto_s,
                        bool luogo_scelto, PhDskMeteoStato *out);

/* ---- Citta' scelta a mano nel profilo ----------------------------------------------------------------- */
/* Copia `src` in out (cap byte, terminatore compreso) tagliando su un confine di carattere UTF-8:
 * mai un carattere a meta'. Config rifiuta nome e dettaglio da 128 byte o piu'. */
void ph_dsk_tronca_utf8(const char *src, char *out, size_t cap);
/* Scrive nel profilo la citta' scelta dall'utente (nome e dettaglio troncati a 127 byte). false
 * (profilo intatto) se il nome e' vuoto o le coordinate sono fuori limite. */
bool ph_dsk_luogo_scrivi(PhCfgTelefono *p, const char *nome, const char *dettaglio, int32_t lat_centesimi,
                         int32_t lon_centesimi);
void ph_dsk_luogo_azzera(PhCfgTelefono *p);
/* true se il valore e' noto e entro +-150 (gli stessi limiti di ph_dsk_meteo_temp): oltre = non noto. */
bool ph_dsk_meteo_valore_valido(double v);

#endif
