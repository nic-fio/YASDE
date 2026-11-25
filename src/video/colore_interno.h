/* Scelte di colore e controlli sui piani condivisi da cattura_png.c e
 * esporta.c (libswscale). Non e' contratto. */
#ifndef PH_COLORE_INTERNO_H
#define PH_COLORE_INTERNO_H

#include <stdbool.h>

#include <libavutil/frame.h>

/* Matrice (valore AVCOL_SPC_* per sws_getCoefficients): quella dichiarata dal
 * fotogramma; se non dichiara nulla BT.709 da 720 righe in su, BT.601 sotto. */
int ph_video_matrice_sws(const AVFrame *f);

/* Ogni piano che il formato usa ha puntatore e riga abbastanza larga. */
bool ph_video_piani_ok(const AVFrame *f);

#endif
