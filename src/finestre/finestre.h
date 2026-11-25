/* Regole delle finestre interne del desktop (spec 4.4, 4.5, 4.6, 4.10, 5.1,
 * 5.3, 5.5, 6.2). Pura logica: nessuna grafica, nessun telefono, nessun
 * orologio interno (il tempo arriva come parametro, in millisecondi, e deve
 * essere non decrescente). Da usare da un solo thread (quello dell'interfaccia).
 * L'unica allocazione e' quella del desktop (ph_fin_crea); finestre al massimo
 * PH_FIN_MAX.
 *
 * Coordinate: tutte "della shell", in unita' logiche (pixel fisici / scala
 * della shell), con l'origine nell'angolo in alto a sinistra dell'AREA UTILE
 * (l'area sopra la barra in basso di 48). I puntatori sono double (un mouse con
 * scala frazionaria), i rettangoli sono interi.
 *
 * Convenzione dei ritorni come nel resto del progetto: int 0 riuscito / -1
 * errore con PhErrore compilato (puo' essere NULL); puntatore o id 0 = errore.
 *
 * Il comportamento dove la specifica tace e' quello del mockup (Main.dc.html);
 * le scelte sono annotate nei commenti qui sotto con "MOCKUP:" e le
 * estensioni necessarie con "SCELTA:".
 *
 * Scostamenti dal mockup in attesa di decisione (comportamenti NON presenti
 * ne' nella specifica ne' nel mockup, messi per far valere i limiti chiesti;
 * si tolgono o si cambiano senza toccare il resto):
 *   1. limiti di trascinamento: la finestra sta sopra la barra (y+h <= area) e
 *      restano visibili almeno 96 px in orizzontale (il mockup limita solo y >= 0);
 *   2. massimo del ridimensionamento: fino al bordo destro e alla barra
 *      (il mockup ha solo il minimo 288x180);
 *   3. finestre ridotte e spostate per restare intere quando l'area si restringe;
 *   4. finestra portata dentro l'area e minimo 288x180 gia' all'apertura;
 *   5. stato "agganciata" persistente, che segue l'area (il mockup non lo ricorda:
 *      cambia solo x, w, h);
 *   6. lo stacco di una agganciata riprende la misura di prima;
 *   7. "ripristina" da massimizzata torna alla disposizione di prima, anche se
 *      era agganciata (il mockup ha un flag separato: effetto simile);
 *   8. anteprima dell'aggancio durante il movimento (il mockup non la mostra);
 *   9. il tetto del flusso e' proporzionale (entrambi i lati scalati); il mockup
 *      limita ogni lato per conto suo (la specifica dice solo "2560 per lato");
 *  10. un clic fermo (senza movimento) non stacca ne' aggancia (il mockup aggancia
 *      anche senza movimento se il puntatore e' vicino al bordo);
 *  11. "Ruota" rifiutata per le solo-verticali e per le agganciate; registrazione:
 *      massimizza/aggancio rifiutati invece che "senza effetto sulla misura".
 */
#ifndef PH_FINESTRE_H
#define PH_FINESTRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comune/errore.h"

/* ---- Misure del mockup (6.2), in unita' della shell ---- */
#define PH_FIN_MAX 64              /* massimo di finestre aperte insieme */
#define PH_FIN_BARRA_H 48          /* barra in basso */
#define PH_FIN_TITOLO_H 50         /* barra del titolo della finestra (Vetro liquido, 2.1) */
#define PH_FIN_PIE_H 20            /* piede tecnico (4.4 / 5.5): NASCOSTO di default, vedi ph_fin_imposta_pie */
#define PH_FIN_CROM_H (PH_FIN_TITOLO_H + PH_FIN_PIE_H) /* 70: tolti dal contenuto SOLO con il piede acceso (altrimenti 50) */
#define PH_FIN_MIN_W 288           /* misura minima in ridimensionamento. 288 = 22 (margine sx) + 7 pulsanti da 30 + 6 distanze da 4 + 14 (margine dx) = 270, piu' 18 px: a 288 il nome NON ci sta (restano 14 px) ma i sette pulsanti si, senza sovrapporsi; era 240 con il titolo da 32 */
#define PH_FIN_MIN_H 180
#define PH_FIN_ANGOLO 18           /* maniglia di ridimensionamento in basso a destra */
#define PH_FIN_PULS 30             /* pulsanti tondi del titolo 30x30, centrati in verticale nel titolo */
#define PH_FIN_PULS_DIST 4         /* distanza fra due pulsanti vicini */
#define PH_FIN_PULS_MARG_DX 14     /* dal bordo destro della finestra al gruppo di pulsanti di destra */
#define PH_FIN_PULS_MARG_SX 22     /* dal bordo sinistro della finestra al primo elemento di sinistra (indietro) */
#define PH_FIN_AGG_LATO 14         /* MOCKUP: aggancio sinistra/destra entro 14 px dal bordo */
#define PH_FIN_AGG_ALTO 10         /* MOCKUP: aggancio in alto (massimizza) entro 10 px */
#define PH_FIN_VISIBILE_MIN 96     /* SCELTA: almeno 96 px di finestra restano dentro l'area in orizzontale (titolo sempre raggiungibile) */
#define PH_FIN_ASSESTA_MS 300      /* 4.5: attesa senza cambi di misura prima di chiedere al telefono */
#define PH_FIN_DENS_PASSO 20       /* la densita' effettiva automatica cambia a scalini di 20 dpi */
#define PH_FIN_TABLET_LATO 600     /* PhFinInfo.tablet: lato corto del contenuto >= 600 px logici (solo informazione) */
#define PH_FIN_FLUSSO_MIN 64       /* lato minimo del flusso */
#define PH_FIN_FLUSSO_TETTO 2560   /* tetto di default per lato (4.4) */
#define PH_FIN_DENSITA_RIF 160.0   /* dpi di una shell a scala 1 (px logici = dp): e' solo il RIFERIMENTO */
#define PH_FIN_DENSITA_MIN 50.0    /* densita' accettate (app e riferimento) */
#define PH_FIN_DENSITA_MAX 2000.0
#define PH_FIN_TEL_W 296           /* forma di telefono predefinita per ph_fin_imposta_solo_verticale: a scala 1 e piede spento il contenuto e' 296x440, multiplo di 8 */
#define PH_FIN_TEL_H (PH_FIN_TITOLO_H + 440) /* 490: contenuto 296x440 */
#define PH_FIN_MARGINE_AREA 16     /* margine lasciato per lato quando la misura predefinita va ridotta per stare nell'area */
#define PH_FIN_GENERICA_W 384      /* finestra generica (app sconosciuta): contenuto 384x392 a scala 1, piede spento */
#define PH_FIN_GENERICA_H (PH_FIN_TITOLO_H + 392) /* 442: contenuto 384x392 */
#define PH_FIN_APP_MAX 48          /* lunghezza massima (con il nul) del nome app */
#define PH_FIN_TITOLO_MAX 64

typedef struct {
    int x, y, w, h;
} PhFinRect;

/* Disposizione della finestra. La minimizzazione e' un flag a parte (MOCKUP:
 * `min` indipendente da `max`): ripristinando si torna alla disposizione di prima. */
typedef enum {
    PH_FIN_NORMALE = 0,
    PH_FIN_MASSIMIZZATA,
    PH_FIN_AGG_SX,
    PH_FIN_AGG_DX,
} PhFinStato;

/* Che cosa e' una finestra di app: misura iniziale e se si puo' ridimensionare.
 * solo_verticale (decisione del committente): la finestra NON cambia dimensioni, resta a forma di
 * telefono: niente ridimensionamento, niente ingrandimento, niente aggancio; si puo' spostare,
 * minimizzare, ripristinare e chiudere. Vedi PhFinInfo.puo_*.
 * specchio: forma FISSA come le solo-verticali (stessi comandi disattivati, stessi rifiuti), con la
 * proporzione dello schermo del telefono; anche creato con solo_verticale = false. */
typedef struct {
    int w, h;                /* misura iniziale della finestra intera (titolo e piede compresi) */
    bool solo_verticale;
    /* Specchio dello schermo reale (spec 4.7, app "phone"): nessuna misura del
     * flusso calcolata dalla finestra e nessuna richiesta di ridimensionamento;
     * la misura (tetto 1920) la decide il telefono. ph_fin_misura_flusso da' errore,
     * info.flusso_w/h valgono 0, ph_fin_avanza non lo nomina mai. */
    bool specchio;
} PhFinSpec;

/* Misure di esempio del mockup (SPEC: msg 296x470 verticale, web 540x340, note
 * 400x290, calc 260x390 verticale, photo 460x330, map 480x330, music 300x430
 * verticale, set 360x400, phone 280x500 verticale; altro 384x442 ridimensionabile). */
PhFinSpec ph_fin_spec_mockup(const char *app);
/* Misura PREDEFINITA (quella da usare nel prodotto): w = h = 0, la sceglie ph_fin_apri dallo schermo del telefono (vedi
 * ph_fin_imposta_telefono). solo_verticale: forma di telefono; specchio: schermo reale; entrambi falsi: finestra
 * generica ridimensionabile. ph_fin_apri con spec NULL equivale a (false, false). */
PhFinSpec ph_fin_spec_predefinita(bool solo_verticale, bool specchio);

typedef struct {
    unsigned id;
    char app[PH_FIN_APP_MAX];
    char titolo[PH_FIN_TITOLO_MAX];
    PhFinRect rect;          /* rettangolo attuale (anche da minimizzata: l'ultimo) */
    PhFinRect normale;       /* rettangolo a cui si torna ripristinando */
    PhFinStato stato;
    bool minimizzata;
    bool attiva;
    bool solo_verticale;
    bool specchio;
    bool registrando;
    /* Che cosa la shell deve lasciare ATTIVO: i comandi si mostrano DISATTIVATI (non senza effetto) quando
     * sono falsi. Valgono ora: false per solo-verticali e SPECCHIO (forma fissa), minimizzate, in registrazione (e per
     * puo_ridimensionare anche da massimizzata). Chiamare comunque la funzione corrispondente torna -1
     * senza cambiare nulla. Spostare, minimizzare, ripristinare e chiudere sono sempre possibili. */
    bool puo_ridimensionare; /* ph_fin_gesto_inizia(RIDIMENSIONA) */
    bool puo_ingrandire;     /* ph_fin_massimizza_ripristina */
    bool puo_agganciare;     /* aggancio ai bordi a fine trascinamento (altrimenti il gesto finisce senza aggancio) */
    int video_w, video_h;    /* misura DICHIARATA dal flusso (0 = nessuna ancora) */
    int flusso_w, flusso_h;  /* misura che il flusso dovrebbe avere ora (multipli di 8) */
    int densita;             /* densita' EFFETTIVA dello schermo virtuale (dpi), 0 per lo specchio */
    /* Riproporzione (regola automatica, telefono noto): fattore = min(1, w / Wt) con w = larghezza utile in pixel logici e Wt =
     * larghezza dello schermo del telefono in punti (verticale). Sotto Wt la densita' scende di quanto serve perche' l'app
     * veda sempre Wt punti di larghezza; 1 negli altri casi (densita' fissa, specchio, telefono non noto, w >= Wt). */
    double fattore;
    int punti_w, punti_h;    /* punti visti dal telefono (flusso * 160 / densita', TRONCATI come Android); 0 per lo specchio */
    /* Contenuto UTILE in modalita' automatica (0 se densita' fissa o specchio): pixel FISICI, multipli di 8 = misura del
     * flusso sotto il tetto (sopra il tetto il flusso e' la stessa cosa ridotta in proporzione). La shell disegna
     * l'immagine 1:1 nell'angolo in alto a sinistra del contenuto (ph_fin_contenuto), grande utile_w/scala per
     * utile_h/scala unita' logiche, e riempie il resto (margine a destra e in basso, fino a 7 px fisici) con il
     * colore di fondo della finestra. In ph_fin_trova un punto nel margine e' CONTENUTO con valido=false.
     * Anche l'ORIGINE del contenuto deve cadere su un pixel fisico intero per il vero 1:1: per le finestre normali la
     * posizione si allinea a fine operazione (spostamento di pochi px logici, solo se la scala lo permette: 1, 1,25,
     * 1,5, 2... non per scale generiche); per le ingrandite/agganciate (rettangolo imposto) e per le scale che non lo
     * permettono la SHELL deve arrotondare al pixel fisico intero l'origine con cui disegna l'immagine. */
    int utile_w, utile_h;
    bool tablet;             /* solo informazione: lato corto del contenuto >= 600 px logici (senza isteresi); non cambia nessun calcolo */
} PhFinInfo;

typedef struct PhFinDesktop PhFinDesktop;

/* ---- Desktop e area utile ---- */

/* larg/alt: l'area del DESKTOP in pixel FISICI, barra in basso (48) compresa
 * (e' la zona client della finestra host, non la finestra col suo bordo);
 * scala: scala della shell, ammessa da 0,1 a 64; l'area logica non puo'
 * superare 32768 per lato (altrimenti errore).
 * Area utile = (larg/scala) x (alt/scala - 48), almeno 1x1.
 * Qualita' iniziale: densita' app 160 = SEGNAPOSTO: il chiamante DEVE impostare
 * la densita' del telefono con ph_fin_imposta_qualita PRIMA di aprire finestre
 * (160 e' solo il riferimento della shell, non una densita' da telefono).
 * Densita' accettate: 50-2000. */
PhFinDesktop *ph_fin_crea(int larg, int alt, double scala, PhErrore *e);
void ph_fin_libera(PhFinDesktop *d);

/* Se la misura dello schermo del telefono cambia a specchio aperto (il telefono ruota: la shell richiama
 * ph_fin_imposta_telefono), la finestra dello specchio RESTA com'e' (l'immagine appare con bande): la funzione
 * non tocca le finestre aperte. Decisione di oggi, da rivedere. */
/* Schermo del telefono (pixel e densita' in dpi), per le misure predefinite delle finestre aperte DOPO la chiamata
 * (non cambia quelle gia' aperte). Chi la chiama: la shell/collegamento, non appena il componente sul telefono dichiara
 * misura e densita' dello schermo reale (e di nuovo se cambiano). Il contenuto predefinito e' lo schermo in PUNTI
 * (px * 160 / densita': 1080x2340 a 450 dpi = 384x832; 1 punto = 1 pixel logico, come DeX):
 *  - forma di telefono (solo-verticali) e finestra generica: sempre in verticale (lato corto = larghezza) anche con il
 *    telefono in orizzontale o un tablet (dove la forma verticale e' comunque ridotta se non sta nell'area);
 *  - specchio: proporzione dello schermo cosi' com'e' (orizzontale compresa);
 *  - se non sta nell'area (margine PH_FIN_MARGINE_AREA per lato) si riduce IN PROPORZIONE, con la larghezza del contenuto
 *    mai sotto min(288, area); poi si assesta ai multipli di 8 pixel fisici;
 *  - telefono non noto: ripiego PH_FIN_TEL_W x PH_FIN_TEL_H (forma di telefono e specchio) e PH_FIN_GENERICA_W x _H.
 * Errore (nulla cambia) per misure o densita' fuori da 1..20000 px e 50..2000 dpi. Si usa con ph_fin_spec_predefinita. */
int ph_fin_imposta_telefono(PhFinDesktop *d, int w_px, int h_px, double densita, uint64_t t_ms, PhErrore *e);

/* REGOLA (cambio d'area: finestra host, schermo intero, scala). Ogni finestra normale ricorda la misura VOLUTA (quella
 * dell'apertura o l'ultima scelta dall'utente con il ridimensionamento, la rotazione o il ritorno a ridimensionabile; NON le
 * riduzioni automatiche): a ogni cambio la misura effettiva e' la voluta, ridotta solo quanto serve per stare nell'area (le forme
 * fisse tornano alla loro misura, ridotta in proporzione), quindi una finestra rimpicciolita da un'area stretta si riallarga
 * quando l'area ricresce; poi l'assestamento a multipli di 8 e, a movimento assestato, una sola richiesta al telefono.
 * Posizione: una finestra che PRIMA del cambio era tutta dentro l'area viene riportata tutta dentro (se la nuova area puo'
 * contenerla; altrimenti si riduce e sta con il titolo in alto a sinistra); una che l'utente aveva messo a sporgere si sposta
 * solo quanto basta perche' restino la barra del titolo e 96 px. Massimizzate e agganciate seguono l'area; un gesto in corso
 * resta. Le finestre in registrazione non tornano alla misura voluta (la dimensione non cambia).
 */
int ph_fin_imposta_host(PhFinDesktop *d, int larg, int alt, double scala, uint64_t t_ms, PhErrore *e);

PhFinRect ph_fin_area(const PhFinDesktop *d); /* area utile sopra la barra: x=y=0, w e h utili */
double ph_fin_scala(const PhFinDesktop *d);

/* Le tre manopole (4.4). densita_app: dpi del display virtuale (di default
 * quella del telefono); densita_rif: dpi di riferimento della shell a scala 1
 * (160); tetto: lato massimo del flusso in pixel (arrotondato per difetto a un
 * multiplo di 8, almeno 64). */
int ph_fin_imposta_qualita(PhFinDesktop *d, double densita_app, double densita_rif, int tetto,
                           uint64_t t_ms, PhErrore *e);

/* Piede tecnico di 20 px (dimensione del flusso, codec, bitrate: spec 5.5 "facoltativo,
 * utile in sviluppo"). DECISIONE DEL COMMITTENTE: nascosto nel prodotto, spento di
 * default. Spento: il contenuto usa tutta l'altezza sotto il titolo (si tolgono 50 px),
 * ph_fin_trova non conosce la zona PIE e la maniglia di ridimensionamento sta
 * nell'angolo in basso a destra della finestra. Acceso: si tolgono 70 px e sotto il
 * contenuto c'e' la zona PIE. Cambiare l'impostazione cambia la misura del flusso
 * (assestamento normale). */
int ph_fin_imposta_pie(PhFinDesktop *d, bool attivo, uint64_t t_ms, PhErrore *e);

/* "Riapri dove eri" (4.6): se attivo, ph_fin_apri usa posizione e misura
 * ricordate per l'app. Spento di default (e' opzionale). */
void ph_fin_riapri_dove_eri(PhFinDesktop *d, bool attivo);

/* ---- Ciclo di vita ---- */

/* Apre la finestra dell'app. MOCKUP: se l'app ha gia' una finestra, la
 * ripristina (se minimizzata), la porta davanti e la rende attiva, e ritorna il
 * suo id (*nuova=false). Altrimenti ne crea una: posizione a cascata
 * x=70+n*40, y=30+n*30 con n = (numero finestre aperte, escluse quelle dell'app
 * "phone") % 6, misura dalla spec (ridimensionabile: almeno 288x180), poi
 * portata dentro l'area; davanti e attiva. L'app "phone" (specchio dello
 * schermo reale) parte in x=640,y=20 senza cascata. Con "riapri dove eri" attivo
 * e un ricordo per l'app, posizione e misura sono quelle ricordate.
 * Ritorna l'id (> 0, mai riusato) o 0 (troppe finestre, argomenti non validi).
 * nuova puo' essere NULL. */
unsigned ph_fin_apri(PhFinDesktop *d, const char *app, const char *titolo, const PhFinSpec *spec,
                     uint64_t t_ms, bool *nuova, PhErrore *e);
/* MOCKUP: chiudendo, nessuna finestra diventa attiva. Annulla un gesto su di essa.
 * Memorizza il rettangolo normale per "riapri dove eri". */
int ph_fin_chiudi(PhFinDesktop *d, unsigned id, uint64_t t_ms, PhErrore *e);
/* MOCKUP: minimizzando, se era attiva nessuna lo diventa. Il flusso va in pausa
 * (lo vede il chiamante da info.minimizzata). Annulla un gesto su di essa. */
int ph_fin_minimizza(PhFinDesktop *d, unsigned id, uint64_t t_ms, PhErrore *e);
/* Da minimizzata: torna visibile, davanti e attiva. Non minimizzata: -1. */
int ph_fin_ripristina(PhFinDesktop *d, unsigned id, uint64_t t_ms, PhErrore *e);
/* Pulsante "Ingrandisci/Ripristina". MOCKUP: normale o agganciata -> massimizzata
 * (si ricorda la disposizione di prima); massimizzata -> la disposizione di prima.
 * Una finestra a forma fissa (solo-verticale o SPECCHIO) NON si ingrandisce (decisione del committente): torna -1 senza cambiare nulla,
 * come per minimizzata o in registrazione. Non cambia attiva/ordine (come nel mockup). */
int ph_fin_massimizza_ripristina(PhFinDesktop *d, unsigned id, uint64_t t_ms, PhErrore *e);
/* Segno "in registrazione" (spec 5.5: durante la registrazione la dimensione non
 * cambia): il gesto di ridimensionamento e lo stacco di una agganciata sono
 * rifiutati, massimizza e aggancio non hanno effetto (rifiutati), "ruota" rifiutata,
 * e ph_fin_avanza non chiede nulla al telefono finche' registra (alla fine i 300 ms
 * ripartono). Uno spostamento di una finestra normale e' permesso (non cambia la misura). */
int ph_fin_imposta_registrazione(PhFinDesktop *d, unsigned id, bool registra, uint64_t t_ms, PhErrore *e);
/* "Ruota" (spec 5.5, menu altri comandi; Ctrl+R): scambia larghezza e altezza del
 * CONTENUTO (poi minimo 288x180, sempre entro l'area) (titolo e piede, se acceso, restano), tenendo la finestra dentro l'area. Errore (nessun
 * effetto) se massimizzata, agganciata, minimizzata, in registrazione, solo-verticale
 * (misura fissa) o specchio (forma fissa). Il menu e' aperto dal pulsante PH_FIN_PULS_ALTRI. */
int ph_fin_ruota(PhFinDesktop *d, unsigned id, uint64_t t_ms, PhErrore *e);
/* L'informazione "solo verticale" puo' arrivare dopo l'apertura. Passando a vero una finestra ingrandita,
 * agganciata o larga torna alla forma di telefono predefinita (schermo del telefono in punti se noto, altrimenti
 * PH_FIN_TEL_W x PH_FIN_TEL_H; ridotta se non sta
 * nell'area) come finestra normale, nello stesso punto (portata dentro l'area); un gesto in corso su di essa
 * finisce. Il flusso cambia misura una sola volta, con la richiesta a movimento assestato. Passando a falso la
 * finestra resta com'e' ma si puo' di nuovo ridimensionare. Stesso valore: nessun effetto. Specchio: rifiutato
 * (-1, nulla cambia) in entrambi i sensi. Finestra MINIMIZZATA: la forma cambia subito, la richiesta parte 300 ms
 * dopo il ripristino. Finestra IN REGISTRAZIONE: la forma cambia subito (e' la shell che decide di chiamarla), ma al
 * telefono non va nulla finche' registra; a registrazione finita, una richiesta dopo 300 ms. */
int ph_fin_imposta_solo_verticale(PhFinDesktop *d, unsigned id, bool solo_verticale, uint64_t t_ms, PhErrore *e);
/* Porta davanti e rende attiva (clic su una finestra). Minimizzata: -1. */
int ph_fin_porta_davanti(PhFinDesktop *d, unsigned id, PhErrore *e);

typedef enum {
    PH_FIN_AZ_NESSUNA = 0,
    PH_FIN_AZ_MINIMIZZATA,   /* era attiva */
    PH_FIN_AZ_RIPRISTINATA,  /* era minimizzata */
    PH_FIN_AZ_DAVANTI,       /* altrimenti */
} PhFinAzione;
/* Clic sulla voce della barra (5.3 punto 3). Se l'app non ha finestra il
 * chiamante usa ph_fin_cerca_app e ph_fin_apri. */
int ph_fin_clic_barra(PhFinDesktop *d, unsigned id, uint64_t t_ms, PhFinAzione *azione, PhErrore *e);

/* ---- Interrogazioni ---- */
unsigned ph_fin_cerca_app(const PhFinDesktop *d, const char *app); /* 0 = nessuna */
/* Per una finestra MINIMIZZATA flusso_w/h e densita' sono ricalcolati dal suo ultimo rettangolo e
 * dalle impostazioni attuali, ma ph_fin_avanza non li chiede al telefono finche' non viene ripristinata:
 * possono quindi differire da quanto il telefono ha davvero. `tablet` e' calcolato dal rettangolo attuale. */
int ph_fin_info(const PhFinDesktop *d, unsigned id, PhFinInfo *out, PhErrore *e);
size_t ph_fin_conta(const PhFinDesktop *d);
/* Id in ordine di profondita', dal fondo alla cima (minimizzate comprese).
 * Ritorna quanti ne ha scritti (al massimo `max`). */
size_t ph_fin_ordine(const PhFinDesktop *d, unsigned *ids, size_t max);
unsigned ph_fin_attiva(const PhFinDesktop *d); /* 0 = nessuna */
/* Rettangolo del contenuto (dove si disegna il video) in coordinate della shell. */
int ph_fin_contenuto(const PhFinDesktop *d, unsigned id, PhFinRect *out, PhErrore *e);

/* ---- Gesti: trascinamento dal titolo, ridimensionamento dall'angolo ---- */
typedef enum { PH_FIN_GESTO_SPOSTA = 1, PH_FIN_GESTO_RIDIMENSIONA = 2 } PhFinGesto;
typedef enum {
    PH_FIN_AGG_NESSUNO = 0,
    PH_FIN_AGG_A_SINISTRA,   /* metà sinistra */
    PH_FIN_AGG_A_DESTRA,     /* metà destra */
    PH_FIN_AGG_IN_ALTO,      /* massimizza */
} PhFinAggancio;

/* Un solo gesto alla volta. Porta davanti la finestra. Errore se: gesto gia' in
 * corso, finestra minimizzata o massimizzata (MOCKUP: non si trascina una
 * massimizzata), ridimensiona su solo-verticale o specchio (forma fissa). (px,py) = puntatore. */
int ph_fin_gesto_inizia(PhFinDesktop *d, unsigned id, PhFinGesto tipo, double px, double py,
                        uint64_t t_ms, PhErrore *e);
/* Muove. SPOSTA: la finestra segue il puntatore (MOCKUP: y >= 0); SCELTA: y
 * resta tale che la finestra stia sopra la barra (y + h <= area), x tale che
 * restino visibili almeno 96 px (titolo raggiungibile). Se era agganciata, al
 * primo movimento si stacca e riprende la misura di prima, tenendo il
 * puntatore alla stessa proporzione orizzontale del titolo. RIDIMENSIONA:
 * misura = originale + spostamento, minimo 288x180 (PH_FIN_MIN_W), massimo fino
 * al bordo destro e alla barra (SCELTA). Non genera richieste al telefono. */
int ph_fin_gesto_muovi(PhFinDesktop *d, double px, double py, uint64_t t_ms, PhErrore *e);
/* Anteprima dell'aggancio durante lo spostamento (solo ridimensionabili):
 * puntatore a meno di 14 px dal bordo sinistro / destro, a meno di 10 dall'alto
 * (MOCKUP, in quest'ordine). *r e' il rettangolo che avra' la finestra
 * (alto: l'intera area). Fuori da un gesto di spostamento: NESSUNO. r puo' essere NULL. */
PhFinAggancio ph_fin_gesto_anteprima(const PhFinDesktop *d, PhFinRect *r);
/* Fine: l'ultimo punto vale come ultimo movimento; poi, se c'e' un aggancio, lo
 * applica (la disposizione di prima e' ricordata per staccarla). *agganciata
 * dice quale (puo' essere NULL). Un clic fermo (stesso punto dell'inizio) non
 * stacca una agganciata, non sposta e non aggancia. La richiesta al telefono parte da
 * ph_fin_avanza dopo l'assestamento. */
int ph_fin_gesto_finisci(PhFinDesktop *d, double px, double py, uint64_t t_ms,
                         PhFinAggancio *agganciata, PhErrore *e);
/* Gesto interrotto (il puntatore esce dal desktop, perdita del grab): la
 * finestra resta dove e', nessun aggancio. */
void ph_fin_gesto_annulla(PhFinDesktop *d, uint64_t t_ms);
unsigned ph_fin_gesto_finestra(const PhFinDesktop *d); /* id della finestra in gesto, 0 = nessun gesto */

/* ---- Misura del flusso video (4.4) ----
 * Funzione pura. larg_fis/alt_fis: contenuto della finestra in pixel fisici.
 *   densita_shell = densita_rif * scala
 *   px = fisici * (densita_app / densita_shell)          (per lato)
 *   se max(px) > tetto: entrambi i lati moltiplicati per tetto/max (proporzioni conservate)
 *   arrotondato al multiplo di 8 piu' vicino, poi limitato a [64, tetto]
 * Poiche' fisici = logici * scala e densita_shell = rif * scala, la scala si
 * semplifica: flusso = logici * densita_app / rif. Questa e' la regola della densita'
 * FISSA (densita' scelta dall'utente, ph_fin_imposta_densita_automatica(false)): la
 * densita' resta quella data e a ridursi sono i pixel. Esempio: monitor 1920x1200,
 * finestra a tutto schermo (contenuto 1920x1100), densita' 450: 5400x3094 -> tetto ->
 * 2560x1464 (910x520 dp). Mockup: 400x238 a 420/160 -> 1048x624.
 * La regola AUTOMATICA predefinita (densita' effettiva per finestra, tipo DeX: flusso 1:1 coi
 * pixel fisici, densita' = riferimento * scala) e' in coda a questo file
 * (ph_fin_misura_flusso_densita) e in finestre.c (misura_di); questa funzione resta
 * il calcolo puro dei pixel.
 * Ritorna 0 o -1 se un argomento non e' valido. */
int ph_fin_calcola_flusso(double larg_fis, double alt_fis, double densita_app, double densita_shell,
                          int tetto, int *w, int *h, PhErrore *e);
/* Misura attuale per una finestra (dal suo contenuto e dalle tre manopole). */
int ph_fin_misura_flusso(const PhFinDesktop *d, unsigned id, int *w, int *h, PhErrore *e);

/* ---- Regola dell'"assestato" (4.5) ---- */
typedef struct {
    unsigned id;
    int w, h;  /* nuova misura del flusso da chiedere al telefono */
    int densita; /* densita' effettiva (dpi) con cui il telefono deve disporre l'app: va chiesta insieme alla misura */
} PhFinRichiesta;
/* Da chiamare a ogni giro dell'interfaccia. Scrive in out (al massimo max) le
 * richieste per le finestre che: non sono in gesto, non sono minimizzate, hanno
 * una misura del flusso diversa dall'ultima chiesta (o data all'apertura) e
 * ferma da almeno 300 ms. Ogni cambio di misura azzera l'attesa. Una finestra
 * non compare piu' finche' la misura non cambia di nuovo. Ritorna quante. */
size_t ph_fin_avanza(PhFinDesktop *d, uint64_t t_ms, PhFinRichiesta *out, size_t max);
/* La misura che il flusso ha DICHIARATO ora (da usare per l'input). */
int ph_fin_dichiara_video(PhFinDesktop *d, unsigned id, int video_w, int video_h, PhErrore *e);

/* ---- Traduzione dell'input (4.10) ---- */
typedef enum {
    PH_FIN_ZONA_FUORI = 0,   /* nessuna finestra sotto il punto (e' il desktop) */
    PH_FIN_ZONA_TITOLO,
    PH_FIN_ZONA_PULSANTE,
    PH_FIN_ZONA_CONTENUTO,
    PH_FIN_ZONA_PIE,         /* piede tecnico (non e' input); esiste solo con il piede acceso */
    PH_FIN_ZONA_ANGOLO,      /* maniglia di ridimensionamento (solo ridimensionabili non massimizzate) */
} PhFinZona;
/* Disposizione del titolo (per il compositore): i pulsanti sono ANCORATI ai lati della barra
 * del titolo (indietro a sinistra, gli altri a destra) e si disegnano sempre li'; il nome
 * dell'app e l'indicatore REC usano solo lo spazio che avanza in mezzo (tagliati o nascosti se
 * non c'e' posto, mai sopra i pulsanti).
 * Pulsanti del titolo, da SINISTRA a DESTRA: indietro, [nome dell'app e REC al centro],
 * screenshot, registra, altri (⋮), minimizza, ingrandisci, chiudi. Tutti PH_FIN_PULS x PH_FIN_PULS
 * (30x30), verticalmente centrati nei PH_FIN_TITOLO_H (50) px del titolo, con PH_FIN_PULS_MARG_DX (14) px
 * dal bordo destro e PH_FIN_PULS_DIST (4) px fra l'uno e l'altro; indietro a PH_FIN_PULS_MARG_SX (22)
 * px dal bordo sinistro. */
typedef enum {
    PH_FIN_PULS_NESSUNO = 0,
    PH_FIN_PULS_INDIETRO,
    PH_FIN_PULS_SCREENSHOT,
    PH_FIN_PULS_REGISTRA,
    PH_FIN_PULS_ALTRI,       /* "⋮": menu altri comandi (Ruota, Chiudi app) */
    PH_FIN_PULS_MINIMIZZA,
    PH_FIN_PULS_INGRANDISCI,
    PH_FIN_PULS_CHIUDI,
} PhFinPulsante;
typedef struct {
    unsigned id;             /* finestra sotto il punto, 0 se fuori */
    PhFinZona zona;
    PhFinPulsante pulsante;
    /* Solo per il contenuto: il punto nelle coordinate del VIDEO
     * (x * video_w / larghezza disegnata), sempre 0 <= vx < video_w e 0 <= vy < video_h;
     * chi inietta deve TRONCARE a intero. Il compositore deve stendere l'immagine su
     * TUTTO il rettangolo del contenuto (ph_fin_contenuto), senza bande: la formula
     * lo presuppone. Il telefono deve ricevere anche
     * video_w/video_h con cui e' stato calcolato, e scartare l'evento se non
     * coincidono con la misura attuale del suo display. valido=false se il
     * flusso non ha ancora dichiarato una misura. */
    bool valido;
    double vx, vy;
    int video_w, video_h;
} PhFinColpo;
/* Trova la finestra piu' in alto non minimizzata che contiene il punto. */
void ph_fin_trova(const PhFinDesktop *d, double x, double y, PhFinColpo *out);

/* ---- "Riapri dove eri" (4.6): struttura semplice, la scrittura su file e' d'altri ---- */
#define PH_FIN_MEM_MAX 64
typedef struct {
    char app[PH_FIN_APP_MAX];
    int x, y, w, h;          /* rettangolo "normale" (non quello massimizzato/agganciato) */
} PhFinRicordo;
/* Esporta i ricordi: quelli delle finestre aperte (vincono) e quelli di
 * finestre chiuse. Ritorna quanti ne ha scritti (al massimo max). */
size_t ph_fin_esporta(const PhFinDesktop *d, PhFinRicordo *out, size_t max);
/* Sostituisce i ricordi. Valori assurdi (misure <= 0, nome vuoto) sono scartati,
 * non e' un errore; se ce ne sono piu' di PH_FIN_MEM_MAX tiene i primi. Ritorna quanti ne tiene. */
size_t ph_fin_importa(PhFinDesktop *d, const PhFinRicordo *in, size_t n);

/* ---- Densita' effettiva (aggiunta in coda) ----
 * Per ogni finestra la regola automatica (predefinita) sceglie anche la densita' dello schermo virtuale,
 * oltre alla misura del flusso: UNA regola per tutte le finestre di app (piccole, grandi, solo-verticali): flusso = pixel fisici del contenuto (1:1, ridotto in proporzione solo oltre il tetto),
 * densita' = ceil20(max(50, rif*k)) con k = scala (o meno se oltre il tetto), mai sopra quella del telefono,
 * cosi' 1 dp = (quasi) 1 pixel logico (160 dpi a scala 1, come DeX). Nessuna soglia: PhFinInfo.tablet e' solo
 * informazione. La regola e' spiegata in finestre.c (misura_di) e verificata in prova_finestre.c. Chi apre o ridimensiona un
 * flusso chiede al telefono misura E densita' (PhFinRichiesta.densita, PhFinInfo.densita). Lo specchio non ha
 * densita' (0). Se il telefono ha una densita' piu' bassa di rif*scala (scale molto alte) vince la regola
 * "mai sopra la densita' del telefono": l'app appare un po' piu' piccola di 1 dp = 1 pixel logico.
 * RIPROPORZIONE (telefono noto, ph_fin_imposta_telefono): un'app non vede mai meno punti di larghezza di quelli dello schermo
 * del telefono (Wt, es. 384): con w = larghezza utile in pixel logici e f = min(1, w / Wt), la densita' effettiva e'
 * floor(flusso_w * 160 / Wt) (per DIFETTO: i punti, troncati come fa Android, sono sempre >= Wt): 384 -> 160 dpi,
 * 288 -> 120 (il telefono vede 384 punti), 240 -> 100. A f = 1 resta lo scalino di 20 dpi, ma mai sopra lo stesso limite. Vedi PhFinInfo.fattore e punti_w/punti_h. */
/* Misura e densita' effettiva attuali di una finestra (stessa regola di ph_fin_misura_flusso). Errore per lo specchio. */
int ph_fin_misura_flusso_densita(const PhFinDesktop *d, unsigned id, int *w, int *h, int *densita, PhErrore *e);
/* true (predefinito): regola automatica. false: densita' FISSA = quella di ph_fin_imposta_qualita (se l'utente
 * ne ha scelta una propria), regola della versione 1: flusso = contenuto*densita/rif, pixel ridotti oltre il tetto. */
int ph_fin_imposta_densita_automatica(PhFinDesktop *d, bool automatica, uint64_t t_ms, PhErrore *e);

/* ---- Compatibilita' (aggiunta in coda, 23 nov sera; CONTRATTI.md §19) ----
 * Per le app che si disegnano rimpicciolite perche' usano la densita' dello
 * schermo VERO del telefono invece di quella dello schermo virtuale. Scelta
 * dell'utente, per finestra (la shell la ricorda per pacchetto nel profilo):
 * nessun riconoscimento automatico, nessun elenco di app qui.
 * Con la compatibilita' attiva la finestra usa la regola della densita' FISSA
 * (quella di ph_fin_calcola_flusso) con la densita' del TELEFONO, qualunque sia
 * la regola globale (ph_fin_imposta_densita_automatica):
 *   D  = densita' del telefono da ph_fin_imposta_telefono (se non nota: la
 *        densita_app di ph_fin_imposta_qualita);
 *   w, h = contenuto in pixel logici; Wt = larghezza del telefono in punti;
 *   f  = min(1, w / Wt) se il telefono e' noto, altrimenti 1 (riproporzione:
 *        l'app non vede mai meno di Wt punti di larghezza, come nella regola
 *        automatica, ma qui si allarga il flusso invece di abbassare D);
 *   flusso = (w / f, h / f) * D / 160, multipli di 8 (arrotondati al piu'
 *        vicino), minimo 64;
 *   TETTO: se il lato maggiore supera il tetto, entrambi i lati si riducono
 *        in proporzione (multipli di 8) e la densita' RESTA D: l'app vede meno
 *        punti (es. tetto 2560 a 450 dpi: al massimo 910 punti sul lato
 *        lungo) e la superficie INGRANDISCE l'immagine. La densita' non si
 *        abbassa mai: e' proprio la differenza di densita' che rimpicciolisce
 *        queste app.
 *   densita' = D (PhFinInfo.densita e PhFinRichiesta.densita); PhFinInfo.fattore = f;
 *   punti = flusso * 160 / D troncati; utile_w = utile_h = 0 (come la densita'
 *        fissa: l'immagine riempie il contenuto ridotta dalla GPU, NON 1:1, ed
 *        e' giusto cosi'); ph_fin_trova converte le coordinate in proporzione.
 * Esempio: finestra a grandezza di telefono 384x832 punti, D = 450 -> flusso
 * circa 1080x2340 (multipli di 8), cioe' lo schermo vero del telefono.
 * Valgono come per le altre finestre: solo-verticali (forma fissa, il flusso
 * segue il contenuto), ridimensionamento (richieste a fine assestamento, sempre
 * con D), zoom con Ctrl+rotella (il pizzico va in coordinate del video, come
 * sempre), modalita' tablet (PhFinInfo.tablet resta solo informazione:
 * l'app decide la sua disposizione dai punti che vede, cioe' quelli della
 * finestra, ridotti dal tetto). Specchio: la compatibilita' non esiste (-1).
 *
 * Chiamarla subito dopo ph_fin_apri (prima di leggere la misura da mandare con
 * VIDEO_APRI) per le app gia' in compatibilita', e a ogni cambio della voce.
 * Se misura o densita' cambiano, la prossima ph_fin_avanza da' SUBITO (senza
 * l'attesa di assestamento) una PhFinRichiesta con misura e densita' nuove:
 * chi la riceve la manda come un ridimensionamento con densita'
 * (ph_superficie_ridimensiona_densita). Rifiutata (-1, nulla cambia) per lo
 * specchio, id sconosciuto e durante una registrazione (la misura del flusso
 * non puo' cambiare: la voce del menu e' disattivata). Stesso valore di prima:
 * 0 senza richieste. */
int ph_fin_imposta_compatibilita(PhFinDesktop *d, unsigned id, bool attiva, uint64_t t_ms, PhErrore *e);
/* Stato della voce per la finestra (false per id sconosciuto e specchio). */
bool ph_fin_compatibilita(const PhFinDesktop *d, unsigned id);

#endif
