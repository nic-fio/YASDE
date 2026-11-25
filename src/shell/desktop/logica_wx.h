/* Logica pura dei widget nuovi del 23 nov pomeriggio (CONTRATTI.md §16): calendario
 * del mese, sveglia, batteria, interruttori, trova il telefono, app frequenti,
 * trasferimenti recenti. Niente widget: si prova senza schermo. */
#ifndef PH_SHELL_DESKTOP_LOGICA_WX_H
#define PH_SHELL_DESKTOP_LOGICA_WX_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "logica.h"

/* ---- Stili per tipo di widget (campo `stile` del profilo) ---------------------------- */
typedef enum { PH_DSK_CAL_MESE = 0, PH_DSK_CAL_SETTIMANA, PH_DSK_CAL_AGENDA, PH_DSK_CAL_STILI } PhDskCalStile;
size_t ph_dsk_stili_n(PhCfgWidgetTipo tipo);                 /* quanti stili ha il tipo (>= 1) */
unsigned ph_dsk_stile_valido_di(PhCfgWidgetTipo tipo, unsigned stile); /* fuori elenco = 0 */
const char *ph_dsk_stile_nome_di(PhCfgWidgetTipo tipo, unsigned stile); /* NULL oltre l'ultimo */

/* ---- Stato di un dato chiesto al telefono ---------------------------------------------- */
typedef enum {
    PH_DSK_DATO_ATTESA = 0,     /* non ancora arrivato: niente da mostrare */
    PH_DSK_DATO_OK,
    PH_DSK_DATO_NON_DISPONIBILE,/* il telefono non lo permette (ERRORE senza frase) */
    PH_DSK_DATO_ERRORE          /* errore con una frase del servizio */
} PhDskDato;
/* Dall'esito di una richiesta (frase NULL o vuota con ERRORE = non disponibile). */
PhDskDato ph_dsk_dato_da_esito(PhSrvEsito esito, const char *frase);

/* ---- Giorni civili e griglia del mese ---------------------------------------------------- */
/* Giorni dal 1970-01-01 (calendario gregoriano) e ritorno. mese 1-12, giorno 1-31. */
int64_t ph_dsk_giorno_civile(int anno, int mese, int giorno);
void ph_dsk_civile_da_giorno(int64_t g, int *anno, int *mese, int *giorno);
int ph_dsk_giorni_nel_mese(int anno, int mese); /* 28-31, bisestili compresi; 0 se mese non valido */
int ph_dsk_giorno_settimana(int anno, int mese, int giorno); /* 0 = lunedi' ... 6 = domenica */
int ph_dsk_giorno_settimana_di(int64_t g);
int64_t ph_dsk_inizio_settimana(int64_t g); /* il lunedi' della settimana di g */

typedef struct {
    int anno, mese;
    int giorni; /* nel mese */
    int primo;  /* colonna (0 = lunedi') del giorno 1 */
    int righe;  /* settimane mostrate: 4-6 */
} PhDskMese;
bool ph_dsk_mese(int anno, int mese, PhDskMese *m);
/* Giorno (1-31) nella cella i (0 .. righe*7-1), 0 se la cella e' fuori dal mese. */
int ph_dsk_mese_cella(const PhDskMese *m, int cella);
/* Sposta (anno, mese) di `delta` mesi (anche negativo); anni entro 1970-2100. */
void ph_dsk_mese_sposta(int *anno, int *mese, int delta);
/* "Novembre 2026" (nome del mese nella lingua attiva, iniziale maiuscola). */
void ph_dsk_mese_titolo(int anno, int mese, char *out, size_t cap);

/* Richiesta del mese (CONTRATTI.md §16, aggiunta): da mezzanotte LOCALE del primo giorno
 * a mezzanotte locale del primo del mese dopo, in ms UNIX. tz NULL = fuso locale. false
 * se il mese non e' valido o l'intervallo supera PH_SRV_CALENDARIO_GIORNI_MAX giorni. */
bool ph_dsk_mese_intervallo(GTimeZone *tz, int anno, int mese, uint64_t *da_ms, uint64_t *a_ms);
/* Intervallo di `giorni` giorni da `g` (giorno civile locale): mezzanotte locale a mezzanotte locale. */
bool ph_dsk_giorni_intervallo(GTimeZone *tz, int64_t g, int giorni, uint64_t *da_ms, uint64_t *a_ms);
/* Giorno civile LOCALE (nel fuso tz) di un istante UNIX in ms. */
int64_t ph_dsk_giorno_locale(GTimeZone *tz, uint64_t ms);

/* ---- Eventi sui giorni ------------------------------------------------------------------- */
/* Giorni civili occupati da un evento, primo e ultimo INCLUSI. «Tutto il giorno»: inizio e
 * fine letti in UTC, da quello di inizio a quello di fine ESCLUSO. Con orario: giorni
 * locali; un evento che finisce esattamente a mezzanotte locale non conta nel giorno dopo.
 * false se l'evento non e' valido (fine prima dell'inizio, istanti assurdi). */
bool ph_dsk_evento_giorni(const PhDskEvento *e, GTimeZone *tz, int64_t *primo, int64_t *ultimo);
/* Maschera dei giorni (bit giorno-1) del mese che hanno almeno un evento: eventi che non
 * toccano il mese si scartano. Torna quanti eventi toccano il mese. */
size_t ph_dsk_mese_con_impegni(const PhDskEvento *ev, size_t n, GTimeZone *tz, int anno, int mese,
                               uint32_t *maschera);
/* Indici (in ordine) degli eventi che toccano il giorno civile g; al massimo max. */
size_t ph_dsk_eventi_del_giorno(const PhDskEvento *ev, size_t n, GTimeZone *tz, int64_t g,
                                size_t *indici, size_t max);
/* Quanti eventi toccano ciascun giorno di [g0, g0+giorni): conteggi[i] (saturati a 255). */
void ph_dsk_conta_per_giorno(const PhDskEvento *ev, size_t n, GTimeZone *tz, int64_t g0, int giorni,
                             uint8_t *conteggi);
/* «Tutto il giorno» oppure «HH:MM–HH:MM» (ora locale; evento senza durata: «HH:MM»). */
void ph_dsk_evento_orario(const PhDskEvento *e, GTimeZone *tz, char *out, size_t cap);
/* «Oggi», «Domani» o «Mer 25» (giorno abbreviato e numero). */
void ph_dsk_giorno_etichetta(int64_t g, int64_t oggi, char *out, size_t cap);
const char *ph_dsk_giorno_breve(int giorno_settimana0); /* «Lun»..«Dom» (0 = lunedi'), tradotto */
const char *ph_dsk_nome_giorno(int giorno_settimana1);  /* 1 = lunedi' ... 7, «Lunedì» */
const char *ph_dsk_nome_mese(int mese);                 /* 1-12, «gennaio» */

/* Agenda: giorni con impegni a partire da `oggi` entro `giorni` giorni, con le righe da
 * mostrare: una intestazione per giorno (evento = SIZE_MAX) e i suoi eventi. */
typedef struct {
    int64_t giorno;
    size_t evento; /* indice, oppure (size_t)-1 per l'intestazione del giorno */
} PhDskAgendaRiga;
size_t ph_dsk_agenda(const PhDskEvento *ev, size_t n, GTimeZone *tz, int64_t oggi, int giorni,
                     PhDskAgendaRiga *out, size_t max);

/* «e un altro» (1) oppure «e altri N»; vuoto per 0. */
void ph_dsk_altri_testo(size_t n, char *out, size_t cap);

/* Calendario piccolo: «N impegni oggi». */
void ph_dsk_vista_calendario_mese_piccolo(PhDskDato stato, const char *frase, const PhDskEvento *ev,
                                          size_t n, GTimeZone *tz, int64_t oggi, PhDskVista *v);

/* ---- Sveglia -------------------------------------------------------------------------------- */
/* «fra 7 ore», «fra un'ora», «fra 25 minuti», «fra 2 h 30 min», «fra 3 giorni». */
void ph_dsk_fra_testo(int64_t minuti, char *out, size_t cap);
void ph_dsk_vista_sveglia(PhCfgWidgetDim d, PhDskDato stato, const char *frase, bool presente,
                          uint64_t ora_ms, uint64_t adesso_ms, GTimeZone *tz, PhDskVista *v);

/* ---- Batteria ---------------------------------------------------------------------------------- */
void ph_dsk_vista_batteria(PhCfgWidgetDim d, PhDskDato stato, const char *frase, const PhSrvBatteria *b,
                           PhDskVista *v);
/* «31,5 °C», «4,12 V» con il separatore della lingua. */
void ph_dsk_testo_temperatura(int decimi, char *out, size_t cap);
void ph_dsk_testo_tensione(int millivolt, char *out, size_t cap);
/* «1 h 20 min», «45 min». */
void ph_dsk_testo_durata(int minuti, char *out, size_t cap);

/* ---- Interruttori e «Trova il telefono» ------------------------------------------------------ */
typedef struct {
    bool disponibile; /* false: inattivo, «Non disponibile» */
    bool attivo;      /* evidenziato (suoneria non normale, DND acceso, torcia accesa) */
    bool in_corso;    /* comando mandato, stato nuovo non ancora arrivato */
    char etichetta[48]; /* «Suoneria» */
    char stato[48];     /* «Vibra», «Acceso», «In corso», «Non disponibile» */
} PhDskInterruttore;
/* controllo: 0 suoneria, 1 non disturbare, 2 torcia (come PhSrvControllo). */
void ph_dsk_interruttore(int controllo, int valore, bool puo, bool in_corso, PhDskInterruttore *out);
/* Valore da mandare al prossimo clic: suoneria suona -> vibra -> silenzioso -> suona; gli altri 0 <-> 1. */
int ph_dsk_prossimo_valore(int controllo, int valore);
#define PH_DSK_TROVA_SECONDI 60
/* Secondi che restano (arrotondati per eccesso, mai sotto 0). */
int ph_dsk_trova_rimasti(int durata_s, int64_t trascorsi_ms);
void ph_dsk_trova_testo(int secondi, char *out, size_t cap); /* «Resta 0:42» */

/* ---- App frequenti e trasferimenti ---------------------------------------------------------------- */
/* Le app piu' aperte: piu' aperture prima, a pari merito la piu' recente (poi l'ordine nel profilo). */
size_t ph_dsk_app_frequenti(const PhCfgApertura *a, size_t n, size_t max, size_t *indici);
size_t ph_dsk_app_frequenti_max(PhCfgWidgetDim d);     /* 4 / 8 / 12 */
size_t ph_dsk_trasferimenti_max(PhCfgWidgetDim d);     /* 3 / 5 / 8 */
/* Nome di file da mostrare: UTF-8 valido, senza controlli ne' invisibili di direzione. */
void ph_dsk_nome_sicuro(const char *grezzo, char *out, size_t cap);

#endif
