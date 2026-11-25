/* Scelte di colore e controlli sui piani condivisi (vedi colore_interno.h). */
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>

#include "video/colore_interno.h"

/* Matrice di colore per libswscale: quella dichiarata dal fotogramma; se non
 * dichiara nulla BT.709 da 720 righe in su e BT.601 sotto. */
int ph_video_matrice_sws(const AVFrame *f)
{
    switch (f->colorspace) {
    case AVCOL_SPC_BT709: return AVCOL_SPC_BT709;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M: return AVCOL_SPC_SMPTE170M;
    case AVCOL_SPC_SMPTE240M: return AVCOL_SPC_SMPTE240M;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL: return AVCOL_SPC_BT2020_NCL;
    default: return f->height >= 720 ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
    }
}

/* Ogni piano che il formato usa deve avere puntatore e riga abbastanza larga. */
bool ph_video_piani_ok(const AVFrame *f)
{
    int np = av_pix_fmt_count_planes((enum AVPixelFormat)f->format);
    if (np < 1)
        return false;
    for (int i = 0; i < np; i++) {
        int minimo = av_image_get_linesize((enum AVPixelFormat)f->format, f->width, i);
        if (minimo < 0 || f->data[i] == NULL || f->linesize[i] < minimo)
            return false;
    }
    return true;
}
