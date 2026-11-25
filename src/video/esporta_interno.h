/* Parti interne di esporta.c condivise con i test. Non e' contratto. */
#ifndef PH_ESPORTA_INTERNO_H
#define PH_ESPORTA_INTERNO_H

#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>

#include "video/esporta.h"

/* Riempie *uscita da un fotogramma DRM_PRIME gia' mappato. `drm` PASSA alla
 * funzione (riuscita: lo libera il rilascio; errore: lo libera lei, e con lui
 * i descrittori). larghezza/altezza e colore vengono dal fotogramma originale. */
int ph_esporta_drm(AVFrame *drm, int larghezza, int altezza, enum AVPixelFormat sw_format,
                   enum AVColorSpace spazio, enum AVColorRange intervallo, uint64_t tempo_us,
                   PhFotogramma *uscita, PhErrore *e);

#endif
