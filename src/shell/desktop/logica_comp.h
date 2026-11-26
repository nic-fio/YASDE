/* Logica pura dei componenti di disegno comuni (anello, fette, curva, tratto minima-massima,
 * sigla, numero grande): solo geometria e scelte, niente widget, si prova senza schermo.
 * I numeri arrivano dal telefono (dato ostile): ogni funzione regge valori negativi, zero,
 * non finiti e totali incoerenti, non legge ne' scrive oltre i buffer dati e non divide mai
 * per zero. Un valore non finito conta come "non noto". */
#ifndef PH_SHELL_DESKTOP_LOGICA_COMP_H
#define PH_SHELL_DESKTOP_LOGICA_COMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Anello con la percentuale (batteria) ------------------------------------------------ */
/* Percentuale portata in 0..100 (arrotondata). Non finita o negativa: 0. */
int ph_dsk_percento_valido(double percento);
/* Angoli in radianti (cairo: 0 = ore 3, positivo = senso orario): l'arco parte da ore 12
 * (-pi/2) e arriva a -pi/2 + 2 pi p/100. A 100 l'arco e' il cerchio intero, a 0 e' vuoto
 * (fine = inizio). */
void ph_dsk_anello_angoli(double percento, double *inizio, double *fine);
/* Raggio della linea d'asse e spessore del tratto per un lato `lato` (px): lo spessore e' circa il
 * 9% del lato, tra 3 e 14 px, e mai oltre un quarto del lato; il raggio tiene il tratto dentro
 * il quadrato. lato <= 0: tutto 0. */
void ph_dsk_anello_geometria(double lato, double *raggio, double *spessore);

/* ---- Barra a fette ------------------------------------------------------------------------- */
#define PH_DSK_FETTE_MAX 8
/* Bordi in px delle fette (n + 1 valori, il primo 0, mai in calo, l'ultimo <= larghezza). Le fette
 * occupano la barra in proporzione ai valori (negativi o non noti = 0) rispetto a
 * max(totale, somma dei valori): il totale zero o negativo vale la somma; se la somma e' maggiore
 * del totale le fette riempiono la barra e basta. La fetta i va da bordi[i] a bordi[i+1]. n e'
 * ridotto a PH_DSK_FETTE_MAX; torna quanti n ha usato. Se larghezza <= 0 o nessun valore e'
 * positivo i bordi sono tutti 0. `bordi` deve avere almeno min(n, PH_DSK_FETTE_MAX) + 1 posti. */
size_t ph_dsk_fette_bordi(const int64_t *valori, size_t n, int64_t totale, int larghezza, int *bordi);

/* ---- Curva (temperature) ---------------------------------------------------------------------------- */
#define PH_DSK_CURVA_MAX 48
#define PH_DSK_CURVA_SPAN_MIN 2.0 /* una curva quasi piatta non ingrandisce il rumore */
typedef struct {
    size_t n;                  /* punti validi (<= PH_DSK_CURVA_MAX) */
    double x[PH_DSK_CURVA_MAX], y[PH_DSK_CURVA_MAX];
    size_t imin, imax;         /* indici della minima e della massima (il primo se uguali) */
    double tmin, tmax;         /* valori della minima e della massima */
} PhDskCurvaPunti;
/* Punti di una curva t[0..n) in una larghezza `larghezza` (con `margine_x` ai lati) e tra le
 * ordinate y_alto (valore massimo) e y_basso (minimo). n = 0: nessun punto. n = 1: un punto al
 * centro. Valori tutti uguali, o con un'escursione sotto PH_DSK_CURVA_SPAN_MIN: la curva e'
 * centrata in verticale. I valori non finiti prendono la media di quelli finiti (0 se non ce ne
 * sono). Le x sono equidistanti e crescenti; le y stanno sempre tra y_alto e y_basso (anche se
 * scambiati). */
void ph_dsk_curva_punti(const double *t, size_t n, double larghezza, double margine_x, double y_alto,
                        double y_basso, PhDskCurvaPunti *out);
/* Ogni quanti punti scrivere un'etichetta di larghezza `larg_etichetta` perche' non si tocchino
 * (almeno 1; con una larghezza non utilizzabile solo la prima: passo = n). */
size_t ph_dsk_curva_passo_etichette(size_t n, double larghezza, double larg_etichetta);

/* ---- Tratto minima-massima (giorni) ---------------------------------------------------------------- */
/* Estremi in px (x0 <= x1, dentro 0..larghezza) del tratto tra `min` e `max` su una scala che va
 * da scala_min a scala_max. Minima e massima scambiate si raddrizzano; fuori scala si ritaglia;
 * scala senza ampiezza: tratto su tutta la larghezza; il tratto non e' mai piu' stretto di
 * `minimo_px` (se ci sta). false (x0 = x1 = 0) se un valore non e' finito o larghezza <= 0. */
bool ph_dsk_tratto_px(double min, double max, double scala_min, double scala_max, double larghezza,
                      double minimo_px, double *x0, double *x1);

/* ---- Sigla di una riga d'elenco -------------------------------------------------------------------------- */
/* Prima lettera del testo (UTF-8 valido, maiuscola) per il tondo colorato; testo vuoto, NULL o
 * non UTF-8 valido: "?". Spazi iniziali saltati. cap >= 1. */
void ph_dsk_sigla(const char *testo, char *out, size_t cap);

/* ---- Numero grande ---------------------------------------------------------------------------------------- */
/* Corpo (px) del numero grande perche' stia in larghezza x altezza: al massimo max_px, al minimo
 * min_px, non oltre l'altezza e con `caratteri` cifre larghe circa 0,62 corpi dentro la larghezza.
 * Misure non positive o caratteri 0: min_px. */
double ph_dsk_numero_corpo(double larghezza, double altezza, size_t caratteri, double min_px,
                           double max_px);

/* ---- Disposizione adattiva ---------------------------------------------------------------------------------- */
/* Colonne della legenda delle fette: 2 se la voce piu' larga (chip, nome e valore per intero) sta in
 * una colonna (meta' della larghezza meno lo spazio tra le colonne), altrimenti 1; con meno di due voci
 * o una misura non utilizzabile (non finita, <= 0: ancora non nota) 1. */
int ph_dsk_legenda_colonne(double larghezza, double larg_voce_max, double spazio, size_t n);
/* Nella riga d'elenco l'ora va sotto (sulla riga del sottotitolo, a destra) quando titolo e ora non
 * stanno insieme a fianco del tondo: larghezza < chip + spazio + titolo + spazio + ora. Senza ora
 * (larg_ora <= 0) o con misure non utilizzabili: false (l'ora resta a destra). */
bool ph_dsk_riga_ora_sotto(double larghezza, double larg_chip, double larg_titolo, double larg_ora,
                           double spazio);

/* ---- Accenti ------------------------------------------------------------------------------------------------------ */
/* I colori d'accento dei widget (variabili --ph-desktop-* di desktop.css, scelti per il vetro
 * scuro). TEMA e' l'accento del tema (--ph-accento). */
typedef enum {
    PH_DSK_ACC_TEMA = 0,
    PH_DSK_ACC_VERDE,   /* batteria */
    PH_DSK_ACC_AMBRA,
    PH_DSK_ACC_CORALLO,
    PH_DSK_ACC_INDACO,
    PH_DSK_ACC_VIOLA,
    PH_DSK_ACC_ARDESIA,
    PH_DSK_ACC_N
} PhDskAccento;
/* Accento valido (0 = quello del tema); un valore fuori elenco vale il tema. n_accenti = quanti. */
int ph_dsk_accento_valido(int accento, int n_accenti);

#endif
