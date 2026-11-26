/* Logica pura del desktop (F-02): nessun widget, nessun servizio. Sta qui perche'
 * si prova senza schermo (prova_desktop.c): griglia delle icone, posto dei
 * widget, aggancio a passi di 8 px, posizione dei menu, scelta dei "Recenti",
 * rettangoli dello sfondo immagine, lettura sicura delle misure di un'immagine
 * e contenuto (testi e numeri) dei widget. Le misure sono quelle del mockup
 * (Main.dc.html: cellInfo, cellAt, freeCell, addWidget, widgetData). */
#ifndef PH_SHELL_DESKTOP_LOGICA_H
#define PH_SHELL_DESKTOP_LOGICA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config/config.h"
#include "shell/servizi.h"

/* ---- Griglia delle icone (5.2.2) --------------------------------------- */
#define PH_DSK_CELLA_L 92       /* larghezza di una cella */
#define PH_DSK_CELLA_A 96       /* altezza di una cella */
#define PH_DSK_MARGINE 16       /* dal bordo sinistro e dall'alto */
#define PH_DSK_BARRA 48         /* altezza della barra: l'area utile e' la radice meno questa */
#define PH_DSK_RISERVA_WIDGET 240 /* colonna lasciata ai widget a destra, se visibili */
#define PH_DSK_ICONA_L 84       /* larghezza del pulsante dell'icona */
#define PH_DSK_ICONA_A 88       /* altezza del riquadro tratteggiato */
#define PH_DSK_GRIGLIA_MAX 512  /* tetto per colonne e righe: protegge dalle misure assurde */

typedef struct {
    int colonne, righe;
} PhDskGriglia;

typedef struct {
    int c, r;
} PhDskCella;

/* cellInfo: colonne = max(1, (larghezza - 16 - (widget visibili ? 240 : 16)) / 92),
 * righe = max(1, (altezza_radice - 48 - 16) / 96). Mai sopra PH_DSK_GRIGLIA_MAX. */
PhDskGriglia ph_dsk_griglia(int larghezza, int altezza_radice, bool widget_visibili);

/* cellAt: la cella sotto il punto (x, y), in coordinate del desktop, ristretta
 * alla griglia. */
PhDskCella ph_dsk_cella_al_punto(PhDskGriglia g, double x, double y);

/* Angolo in alto a sinistra della cella. */
void ph_dsk_cella_posizione(PhDskCella c, int *x, int *y);

/* freeCell: la cella libera piu' vicina a `voluta` (distanza di Manhattan; a
 * parita' la prima per colonna e poi per riga, come il mockup). `occupate` sono
 * le altre icone (quella che si sposta NON c'e'). false (e *out intatto) se la
 * griglia e' piena. Celle di `occupate` fuori dalla griglia si ignorano. */
bool ph_dsk_cella_libera(PhDskGriglia g, const PhDskCella *occupate, size_t n, PhDskCella voluta,
                         PhDskCella *out);

/* Dove si mostrano le icone del profilo: una cella dentro la griglia e non gia'
 * presa resta dov'e'; le altre (fuori griglia dopo un ridimensionamento, o doppie)
 * vanno nella libera piu' vicina, SOLO a schermo (il profilo non si riscrive).
 * visibile[i] = false se non c'e' piu' posto per l'icona i. */
void ph_dsk_disponi(PhDskGriglia g, const PhDskCella *volute, size_t n, PhDskCella *out,
                    bool *visibile);

/* ---- Widget (5.2.3) ------------------------------------------------------ */
#define PH_DSK_PASSO 8 /* aggancio del trascinamento */

int ph_dsk_larghezza_widget(PhCfgWidgetDim d);  /* 200 / 250 / 320 */

typedef struct {
    int larghezza, altezza;
} PhDskMisura;

typedef struct {
    int destra, alto;         /* posto dal bordo destro e dall'alto */
    int larghezza, altezza;   /* misura reale (ph_dsk_misura_da_profilo) */
} PhDskWidgetPos;

/* Il primo posto libero per un widget nuovo di misura `nuovo`: si provano i posti su una
 * griglia a passi di PH_DSK_PASSO, per colonne dal bordo destro (partendo da 16) e dall'alto
 * (partendo da 16); vale il primo in cui il rettangolo sta dentro l'area (larghezza_area x
 * altezza_utile, con 8 px di respiro) e dista almeno 12 px da ogni widget esistente. Se non
 * c'e' posto, a cascata (come prima). altezza_utile = radice - 48. */
void ph_dsk_posto_widget_nuovo(int larghezza_area, int altezza_utile, const PhDskWidgetPos *esistenti,
                               size_t n, PhDskMisura nuovo, int *destra, int *alto);

/* Widget predefiniti di un profilo nuovo (orologio, stato, notifiche: destra 16, alto 16 / 120 /
 * 240, Medio, senza misure salvate): le posizioni sono pensate per le altezze vecchie e i widget si
 * toccano. Se `w` e' esattamente quel profilo, scrive in alti[0..2] le ordinate di partenza giuste
 * (16, poi sotto ogni widget con PH_DSK_DISTANZA_WIDGET px di respiro, secondo la misura vera) e
 * torna true; altrimenti false e `alti` intatto. Serve solo a schermo: il profilo non si riscrive
 * finche' l'utente non sposta o cambia qualcosa. `alti` ha almeno 3 posti. */
#define PH_DSK_DISTANZA_WIDGET 12
bool ph_dsk_predefiniti_alti(const PhCfgWidget *w, size_t n, int *alti);

/* Aggancio a passi di 8 px (onDeskMove): destra = round((iniziale - dx) / 8) * 8,
 * alto = round((iniziale + dy) / 8) * 8, mai negativi. */
int ph_dsk_aggancia_destra(int destra_iniziale, double dx);
int ph_dsk_aggancia_alto(int alto_iniziale, double dy);

/* ---- Forma e misura libera (PIANO.md 8.1) ----------------------------------------
 * Ogni tipo (e, per l'orologio, ogni stile) ha una forma: mattonella (angoli molto
 * morbidi), tondo (lato uguale) o capsula (raggio = meta' dell'altezza). La misura e'
 * libera tra un minimo e un massimo del tipo, a scatti di PH_DSK_PASSO; il livello
 * di contenuto (piccolo / medio / grande) si ricava dalla misura, cosi' i disegni
 * esistenti restano validi finche' non vengono rifatti. */
typedef enum { PH_DSK_FORMA_MATTONELLA = 0, PH_DSK_FORMA_TONDO, PH_DSK_FORMA_CAPSULA } PhDskForma;
#define PH_DSK_RAGGIO_MATTONELLA 28

typedef struct {
    PhDskMisura min, max;
} PhDskLimiti;

/* Forma dal tipo e dallo stile (lo stile fuori elenco vale 0). Tondo: orologio analogico
 * (classico, essenziale) e batteria. Capsula: sveglia e interruttori. Mattonella: il resto. */
PhDskForma ph_dsk_forma(PhCfgWidgetTipo tipo, unsigned stile);
/* Raggio degli angoli per una misura: mattonella 28 (mai oltre la meta' del lato corto),
 * tondo = lato / 2, capsula = altezza / 2. */
int ph_dsk_raggio(PhDskForma forma, int larghezza, int altezza);
/* Imbottitura (px) dentro la scheda, per lato orizzontale e verticale: piu' larga dove
 * l'angolo e' molto curvo, perche' il testo non finisca sotto la curva. */
void ph_dsk_imbottitura(PhDskForma forma, int *orizzontale, int *verticale);

/* Misura equivalente a un livello: quella che tiene il contenuto di oggi. */
PhDskMisura ph_dsk_misura_equivalente(PhCfgWidgetTipo tipo, unsigned stile, PhCfgWidgetDim dim);
/* Minimo (la misura equivalente al Piccolo) e massimo del tipo. Il tondo ha limiti quadrati. */
PhDskLimiti ph_dsk_misura_limiti(PhCfgWidgetTipo tipo, unsigned stile);
/* Porta una misura dentro i limiti; il tondo diventa quadrato (vale il lato corto). */
PhDskMisura ph_dsk_misura_vincola(PhCfgWidgetTipo tipo, unsigned stile, PhDskMisura m);
/* Dal profilo: larghezza e altezza salvate (entrambe diverse da 0) vincolate ai limiti;
 * altrimenti (0/0, profili scritti prima delle misure libere) la misura equivalente a `dim`. */
PhDskMisura ph_dsk_misura_da_profilo(PhCfgWidgetTipo tipo, unsigned stile, PhCfgWidgetDim dim,
                                     int larghezza, int altezza);
/* Cambio di stile: se la forma resta la stessa la misura si tiene (vincolata ai limiti del nuovo
 * stile); se la forma cambia (per esempio digitale -> analogico) la misura si perde e torna {0, 0}
 * = "equivalente al livello", che il chiamante ricava. */
PhDskMisura ph_dsk_misura_cambio_stile(PhCfgWidgetTipo tipo, unsigned stile_vecchio,
                                       unsigned stile_nuovo, PhDskMisura attuale);
/* Il livello di contenuto di una misura: il piu' alto le cui misure equivalenti stanno
 * dentro `m` (almeno Piccolo). Per le misure equivalenti restituisce il loro livello. */
PhCfgWidgetDim ph_dsk_livello(PhCfgWidgetTipo tipo, unsigned stile, PhDskMisura m);

/* Maniglia nell'angolo in basso a destra: scatti di PH_DSK_PASSO a partire dalla misura
 * `iniziale`, con l'angolo in alto a sinistra fermo (se il widget si allarga `destra` cala).
 * dx, dy: spostamento del puntatore (o +-PH_DSK_PASSO da tastiera). Risultato dentro i limiti
 * del tipo, mai oltre il bordo destro (destra >= 0) ne' sotto l'area utile (`altezza_utile`
 * = altezza radice - barra); se il widget e' gia' piu' grande di quanto l'area permette non
 * si restringe da solo. Il tondo resta quadrato. true se qualcosa e' cambiato. */
bool ph_dsk_ridimensiona(PhCfgWidgetTipo tipo, unsigned stile, PhDskMisura iniziale, int destra0,
                         int alto0, double dx, double dy, int altezza_utile, PhDskMisura *nuova,
                         int *destra);
/* Margine (px, dal bordo del contenuto) per la maniglia, perche' stia dentro la curva
 * dell'angolo: 0 per angoli poco curvi. */
int ph_dsk_maniglia_margine(int raggio, int imbottitura);

/* ---- Menu ----------------------------------------------------------------- */
/* Posizione ristretta di un menu aperto nel punto (x, y) di un'area larg x alt:
 * x <= larg - largh_menu, y <= alt - alt_menu, mai negative. */
#define PH_DSK_MENU_DESKTOP_L 200
#define PH_DSK_MENU_DESKTOP_A 110
#define PH_DSK_MENU_ICONA_L 210
#define PH_DSK_MENU_ICONA_A 150
#define PH_DSK_MENU_WIDGET_L 210
#define PH_DSK_MENU_WIDGET_A 80 /* solo «Rimuovi widget»: stile e citta' si sommano in desktop.c */
void ph_dsk_menu_posizione(double x, double y, int larg, int alt, int largh_menu, int alt_menu,
                           int *ox, int *oy);

/* ---- Recenti della galleria (5.2.1) ---------------------------------------- */
#define PH_DSK_RECENTI_MAX 40

/* .jpg .jpeg .png .webp (senza badare alle maiuscole); non i nomi nascosti. */
bool ph_dsk_nome_immagine(const char *nome);
/* Nome senza estensione, per le miniature ("IMG_0412.jpg" -> "IMG_0412"). */
void ph_dsk_nome_breve(const char *nome, char *out, size_t cap);

typedef struct {
    char *percorso;      /* completo, sul telefono (proprieta' di chi lo mette) */
    char *nome_schermo;  /* da mostrare */
    int64_t modificato;  /* secondi dal 1970 */
} PhDskRecente;

/* Ordina dalla piu' recente (a parita' per percorso) e tiene al massimo `max`:
 * torna quanti ne restano. Quelli oltre il tetto NON si liberano qui (li libera
 * chi li ha creati: gli elementi restano nell'array oltre il valore tornato). */
size_t ph_dsk_recenti_ordina(PhDskRecente *v, size_t n, size_t max);

/* Mette `nuovo` in v (ordinato, al massimo `max` voci, `*n` quante ce ne sono) tenendo
 * solo le piu' recenti: memoria fissa anche con elenchi enormi, ordinati per nome come
 * li danno i servizi. Torna true se una voce e' stata scartata (o `nuovo` stesso, se
 * piu' vecchio delle `max` gia' presenti): sta in *scartato e la libera chi chiama.
 * Se torna false `nuovo` e' entrato nell'array (e ne e' diventato proprietario). */
bool ph_dsk_recenti_inserisci(PhDskRecente *v, size_t *n, size_t max, const PhDskRecente *nuovo,
                              PhDskRecente *scartato);

/* ---- Sfondo ---------------------------------------------------------------- */
typedef struct {
    float x, y, w, h;
} PhDskRett;

/* Dove si disegna un'immagine iw x ih in un'area W x H: Riempi copre ritagliando,
 * Adatta sta tutta dentro e centrata, Allunga riempie deformando, Centra ha la
 * misura naturale (iw x ih) al centro. Misure non positive: rettangolo vuoto. */
PhDskRett ph_dsk_rett_immagine(PhCfgAdattamento m, float W, float H, float iw, float ih);

/* Estremi del gradiente lineare come il CSS linear-gradient(<angolo>deg, ...) su
 * un'area w x h (0deg verso l'alto, 90deg verso destra). */
void ph_dsk_gradiente(PhCfgDirezione d, float w, float h, float *x0, float *y0, float *x1,
                      float *y1);
int ph_dsk_angolo(PhCfgDirezione d); /* 135, 180, 90, 45 */

/* ---- Immagini che arrivano dal telefono (dato ostile) ---------------------- */
#define PH_DSK_IMMAGINE_LATO_MAX 16384
#define PH_DSK_IMMAGINE_PIXEL_MAX (64u * 1000u * 1000u) /* 256 MB decodificata */
#define PH_DSK_IMMAGINE_BYTE_MAX (32u * 1024u * 1024u)  /* tetto di ph_srv_tel_leggi */

/* Legge la misura dalle sole intestazioni (PNG, JPEG, WebP) senza decodificare e
 * senza uscire dal buffer. false se il formato non e' riconosciuto o l'intestazione
 * e' troncata o incoerente. */
bool ph_dsk_immagine_misura(const uint8_t *dati, size_t n, uint32_t *w, uint32_t *h);
/* Formato riconosciuto dalla firma: "png", "jpeg", "webp", NULL se altro. */
const char *ph_dsk_immagine_formato(const uint8_t *dati, size_t n);
/* Misura entro i tetti (lato e pixel totali, mai zero). */
bool ph_dsk_immagine_ammessa(uint32_t w, uint32_t h);
/* Misura ridotta alla "risoluzione utile": la piu' piccola che ancora copre
 * un'area W x H in pixel fisici, senza mai ingrandire (e mai sotto 1). */
void ph_dsk_immagine_ridotta(uint32_t w, uint32_t h, uint32_t W, uint32_t H, uint32_t *nw,
                             uint32_t *nh);
/* Nome nuovo valido per ph_cfg_nome_cache_valido ("sfondo-<numero>.png"). */
void ph_dsk_nome_sfondo(uint32_t numero, char *out, size_t cap);

/* ---- Tavolozza dei colori (dialogo dello sfondo) ---------------------------------
 * 6 righe x 8 colonne: la prima di grigi e neri, le altre cinque di otto tinte dal
 * chiaro allo scuro (coerenti con il tema scuro). Le stesse 48 per solido, Da e A. */
#define PH_DSK_PALETTE_COLONNE 8
#define PH_DSK_PALETTE_N 48
uint32_t ph_dsk_palette(size_t i); /* 0xRRGGBB; indice oltre il fondo: 0 */

/* ---- Cartelle del telefono (scelta di un'immagine) --------------------------------
 * Il minimo della logica dei percorsi (le stesse regole del selettore di
 * trasferimento, riscritte qui: nessun pezzo include gli header di un altro). */
#define PH_DSK_TEL_MEMORIA_INTERNA "/storage/emulated/0"
#define PH_DSK_TEL_PERCORSO_MAX 4096
#define PH_DSK_TEL_PAGINA_CARTELLE 200 /* sottocartelle per pagina */
#define PH_DSK_TEL_PAGINA_IMMAGINI 24  /* miniature per pagina: si chiedono a richiesta */
/* `nome` e' un solo pezzo di percorso valido: UTF-8, senza '/', senza controlli,
 * ne' vuoto, ne' "." o "..", al massimo 255 byte. */
bool ph_dsk_tel_nome_valido(const char *nome);
bool ph_dsk_tel_nascosto(const char *nome); /* inizia con il punto */
/* cartella + "/" + nome. false se il nome non e' valido, la cartella non e' un
 * percorso assoluto o il risultato non sta in cap / PH_DSK_TEL_PERCORSO_MAX. */
bool ph_dsk_tel_unisci(const char *cartella, const char *nome, char *out, size_t cap);
/* Cartella che contiene `percorso`. true se si e' saliti; false (out = "/") se era
 * gia' "/" o l'input non e' valido. */
bool ph_dsk_tel_su(const char *percorso, char *out, size_t cap);
bool ph_dsk_tel_e_radice(const char *percorso);
/* Quante voci mostrare dopo `pagine` pagine, e quante ne aggiunge il prossimo clic. */
size_t ph_dsk_pagina_visibili(size_t totale, size_t pagine, size_t per_pagina);
size_t ph_dsk_pagina_prossime(size_t totale, size_t visibili, size_t per_pagina);

/* ---- Contenuto dei widget (5.2.3, widgetData) ------------------------------- */
#define PH_DSK_RIGHE_MAX 4

typedef struct {
    char sinistra[256];
    char destra[64];
} PhDskRiga;

typedef struct {
    char titolo[64];     /* "" = nessun titolo */
    char testa[96];      /* riga principale; "" = non nota */
    bool testa_grande;   /* orologio: 34 px invece di 18 */
    char sotto[96];      /* sottotitolo, "" = nessuno */
    int percento;        /* barra: 0-100, -1 = nessuna barra */
    PhDskRiga righe[PH_DSK_RIGHE_MAX];
    size_t n_righe;
} PhDskVista;

/* Quante righe di dettaglio per dimensione (mockup: 'cnt' e le slice). */
size_t ph_dsk_righe_notifiche(PhCfgWidgetDim d);  /* 1 / 2 / 4 */
size_t ph_dsk_righe_stato(PhCfgWidgetDim d);      /* 0 / 1 / 3 */
size_t ph_dsk_righe_calendario(PhCfgWidgetDim d); /* 1 / 2 / 4 */

/* ---- Orologio: stili (5.2.3, richiesta del committente) --------------------------
 * Tutti disegnati da noi sul PC. Il predefinito (0) e' il digitale di sempre. */
typedef enum {
    PH_DSK_OROLOGIO_DIGITALE = 0,
    PH_DSK_OROLOGIO_CLASSICO,    /* analogico con tacche, numeri e lancetta dei secondi */
    PH_DSK_OROLOGIO_ESSENZIALE,  /* solo lancette e quattro tacche, data piccola */
    PH_DSK_OROLOGIO_PALETTE,     /* ore e minuti su tessere separate */
    PH_DSK_OROLOGIO_TESTUALE,    /* l'ora scritta a parole */
    PH_DSK_OROLOGIO_DOPPIO_FUSO, /* ora locale e una seconda citta' */
    PH_DSK_OROLOGIO_STILI
} PhDskOrologioStile;

/* Nome dello stile nella lingua attiva (NULL oltre l'ultimo). */
const char *ph_dsk_stile_nome(unsigned stile);
/* Uno stile fuori elenco (profilo rovinato) vale il digitale. */
unsigned ph_dsk_stile_valido(unsigned stile);
/* Lo stile si disegna con lo snapshot (non con le etichette): classico, essenziale, palette. */
bool ph_dsk_stile_disegnato(unsigned stile);
/* Lo stile mostra la lancetta dei secondi: serve un aggiornamento al secondo. */
bool ph_dsk_stile_con_secondi(unsigned stile);

/* Citta' del doppio fuso: elenco breve con il loro fuso (identificatore IANA). */
size_t ph_dsk_citta_n(void);
const char *ph_dsk_citta_nome(size_t i); /* nella lingua attiva; NULL fuori elenco */
unsigned ph_dsk_citta_valida(unsigned i); /* fuori elenco = la prima */
/* Ora della citta' all'istante ms (millisecondi UNIX UTC). giorno_diff = giorni di
 * differenza dalla data locale (locale = UTC + scarto_locale_s): -1, 0 o +1. false se il
 * fuso non e' disponibile sul sistema (nessun numero inventato). */
bool ph_dsk_ora_in_citta(uint64_t ms, int scarto_locale_s, size_t citta, int *ore, int *minuti,
                         int *giorno_diff);

/* L'ora a parole: «le quattro meno dieci», «mezzogiorno e un quarto», «l'una e mezza»,
 * «mezzanotte»; con la prima lettera maiuscola. ore 0-23, minuti 0-59 (altro: stringa vuota). */
void ph_dsk_ora_testo(int ore, int minuti, char *out, size_t cap);

/* Angoli (gradi, in senso orario da ore 12) delle lancette. */
void ph_dsk_angoli_lancette(int ore, int minuti, double secondi, double *a_ore, double *a_minuti,
                            double *a_secondi);

/* Dopo quanti ms serve il prossimo aggiornamento: al secondo con la lancetta dei
 * secondi, altrimenti al minuto; mai meno di 20 ms. `ms` = millisecondi UNIX. */
unsigned ph_dsk_orologio_attesa_ms(uint64_t ms, bool secondi);

/* Altezza (px) del quadrante disegnato; 0 per gli stili non disegnati. */
int ph_dsk_orologio_altezza(unsigned stile, PhCfgWidgetDim d);
/* Diametro del quadrante analogico. */
int ph_dsk_quadrante_diametro(unsigned stile, PhCfgWidgetDim d);

/* Data lunga ("Lunedi' 23 novembre"): giorno_settimana 1 = lunedi' .. 7 = domenica,
 * giorno 1-31, mese 1-12. Testi nella lingua attiva, con l'iniziale maiuscola. */
void ph_dsk_data_lunga(int giorno_settimana, int giorno, int mese, char *out, size_t cap);

void ph_dsk_vista_orologio(PhCfgWidgetDim d, const char *ora, const char *data, PhDskVista *v);
/* Stile testuale: la frase al posto dell'ora, la data sotto (non nel Piccolo). */
void ph_dsk_vista_orologio_testo(PhCfgWidgetDim d, const char *frase, const char *data, PhDskVista *v);
/* Doppio fuso: ora locale grande, data, e una riga con la citta' e la sua ora
 * («HH:MM», con «+1»/«-1» se il giorno e' un altro). noto2 = false: ora «--:--». */
void ph_dsk_vista_orologio_fuso(PhCfgWidgetDim d, const char *ora, const char *data,
                                const char *citta, bool noto2, int ore2, int minuti2,
                                int giorno_diff, PhDskVista *v);
/* t / c: stato del telefono e del collegamento. Valori non noti non si inventano. */
void ph_dsk_vista_stato(PhCfgWidgetDim d, const PhSrvTelefono *t, const PhSrvCollegamento *c,
                        PhDskVista *v);
typedef struct {
    const char *titolo;
    const char *testo;
} PhDskNotifica;
/* Solo le notifiche NON nascoste (le filtra chi chiama). */
void ph_dsk_vista_notifiche(PhCfgWidgetDim d, const PhDskNotifica *n, size_t quante,
                            PhDskVista *v);
/* "GB" con una cifra decimale e il separatore della lingua attiva ("160,0"). */
void ph_dsk_formato_gb(uint64_t byte, char *out, size_t cap);
/* noto = false: la lettura non e' ancora arrivata (o e' fallita): niente numeri.
 * det: la suddivisione (immagini, video, audio, app), NULL se non nota; le voci
 * non leggibili non si mostrano. Righe: Medio 1, Grande 4. */
void ph_dsk_vista_memoria(PhCfgWidgetDim d, bool noto, uint64_t totale, uint64_t libera,
                          const PhSrvMemoriaDettaglio *det, PhDskVista *v);
size_t ph_dsk_righe_memoria(PhCfgWidgetDim d);    /* 0 / 1 / 4 */

typedef struct {
    const char *titolo;   /* dato personale: mai su disco, mai in un log */
    uint64_t inizio_ms, fine_ms;
    bool tutto_il_giorno; /* inizio e fine a mezzanotte UTC: sono giorni, senza fuso */
} PhDskEvento;
/* Sceglie gli eventi che riguardano "oggi" (giorno locale: adesso_ms + scarto_s):
 * non finiti, che iniziano oggi o sono in corso, o tutto-il-giorno che coprono
 * oggi. Scrive gli indici in ordine, al massimo `max`; torna quanti. */
size_t ph_dsk_eventi_di_oggi(const PhDskEvento *ev, size_t n, uint64_t adesso_ms, int scarto_s,
                             size_t max, size_t *indici);
/* "HH:MM" dell'inizio in ora locale. */
void ph_dsk_ora_evento(uint64_t ms, int scarto_s, char *out, size_t cap);
/* ev puo' essere NULL con n = 0 ("nessun evento"). giorno_*: la data lunga di
 * oggi. noto = false: i dati non ci sono (non si inventa nulla). */
void ph_dsk_vista_calendario(PhCfgWidgetDim d, bool noto, const PhDskEvento *ev, size_t n,
                             uint64_t adesso_ms, int scarto_s, const char *data, PhDskVista *v);


#include "logica_wx.h"
#include "logica_meteo.h"
#include "logica_comp.h"

#endif
