/* Dal fotogramma decodificato (AVFrame, quello di ph_decodifica_prendi) al
 * PhFotogramma neutro di src/comune/fotogramma.h, che chi disegna puo' usare
 * senza conoscere FFmpeg. Questo header non include nulla di FFmpeg. */
#ifndef PH_ESPORTA_H
#define PH_ESPORTA_H

#include <stdint.h>

#include "comune/errore.h"
#include "comune/fotogramma.h"

struct AVFrame;

typedef enum {
    PH_ESPORTA_DMABUF = 0,  /* via diretta: fotogramma VA-API mappato in DMA-BUF, senza copie */
    PH_ESPORTA_MEMORIA = 1, /* fotogramma gia' in memoria: nessuna copia (formato RGB gia' previsto)
                               oppure la sola conversione YUV -> RGB che serve */
    PH_ESPORTA_RIPIEGO = 2, /* un fotogramma nella memoria della scheda che non si e' potuto mappare
                               in DMA-BUF (Vulkan, VA-API rifiutato...) e' stato copiato in memoria:
                               chi chiama lo scrive nel registro delle decisioni */
} PhEsportaVia;

/* Esporta *fotogramma in *uscita. L'AVFrame PASSA a questa funzione in ogni
 * caso (riuscita o errore): chi chiama non lo libera e *fotogramma torna NULL.
 * UNICA ECCEZIONE: con `uscita` o `fotogramma` assenti (NULL) torna -1 e l'AVFrame
 * NON viene consumato (resta di chi chiama).
 * Il tempo di *uscita e' il campo pts del fotogramma (= tempo_us del
 * pacchetto, vedi decodifica.h).
 *
 * Torna PhEsportaVia (>= 0) o -1 (PhErrore riempito). Con -1 *uscita e'
 * azzerata, nessun descrittore resta aperto e nessuna memoria resta allocata.
 * Alla riuscita *uscita segue il contratto di VITA di fotogramma.h: chi
 * riceve chiama ph_fotogramma_rilascia una volta, da qualunque thread; solo
 * allora si chiudono i descrittori / si libera la memoria. I descrittori di
 * piani che stanno nello stesso oggetto DMA-BUF compaiono piu' volte con lo
 * stesso numero ma sono chiusi una volta sola (da rilascia).
 *
 * Forme prodotte:
 *  - DMABUF: fourcc e modificatore dal descrittore DRM (NV12 / P010 anche con
 *    i piani su livelli separati), piani, offset e passi; colore: BT.709 o
 *    BT.601 a seconda di quanto dichiarato dal fotogramma (non dichiarato:
 *    BT.709 da 720 righe in su, BT.601 sotto), intervallo limitato se non
 *    dichiarato; pieno -> BT709_PIENO o BT601_PIENO; altre matrici ->
 *    PREDEFINITO. Offset o passo oltre 32 bit -> errore.
 *  - MEMORIA: B8G8R8A8_PREMOLT (alfa 255 nelle conversioni) di norma; i fotogrammi gia'
 *    BGRA, RGBA o RGB24 con riga positiva non si copiano (dati = il buffer
 *    del decodificatore, tenuto vivo fino al rilascio; il loro alfa e' quello
 *    del fotogramma, ma dalla decodifica H.264 non ne arriva mai). Per le conversioni
 *    vale la stessa regola dei colori di cattura.h.
 * La funzione puo' essere chiamata da piu' thread su fotogrammi diversi. */
int ph_esporta(struct AVFrame **fotogramma, PhFotogramma *uscita, PhErrore *e);

#endif
