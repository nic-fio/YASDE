/* Prova dell'ordine dei tentativi della via automatica: VA-API prima di
 * Vulkan (che copia il fotogramma), software sempre ultimo. */
#include "prova/aiuti.h"
#include "video/decodifica.h"

int main(void)
{
    PhVia s[3];
    size_t n = ph_decodifica_ordine_automatico(true, s);
    CONTROLLA(n == 3);
    CONTROLLA(s[0] == PH_VIA_VAAPI);
    CONTROLLA(s[1] == PH_VIA_VULKAN);
    CONTROLLA(s[2] == PH_VIA_SOFTWARE);
    n = ph_decodifica_ordine_automatico(false, s);
    CONTROLLA(n == 2);
    CONTROLLA(s[0] == PH_VIA_VAAPI);
    CONTROLLA(s[1] == PH_VIA_SOFTWARE);
    /* La via reale tiene conto del ripiego (FFmpeg passato al software a meta'). */
    CONTROLLA(ph_via_reale(PH_VIA_VAAPI, false) == PH_VIA_VAAPI);
    CONTROLLA(ph_via_reale(PH_VIA_VULKAN, false) == PH_VIA_VULKAN);
    CONTROLLA(ph_via_reale(PH_VIA_VAAPI, true) == PH_VIA_SOFTWARE);
    CONTROLLA(ph_via_reale(PH_VIA_VULKAN, true) == PH_VIA_SOFTWARE);
    CONTROLLA(ph_via_reale(PH_VIA_SOFTWARE, false) == PH_VIA_SOFTWARE);
    /* Un decodificatore creato col software lo dichiara. */
    PhErrore e = {{0}};
    PhDecodifica *d = ph_decodifica_crea(PH_VIA_SOFTWARE, &e);
    CONTROLLA(d != NULL);
    if (d != NULL) {
        CONTROLLA(ph_decodifica_via(d) == PH_VIA_SOFTWARE);
        ph_decodifica_libera(d);
    }
    return 0;
}
