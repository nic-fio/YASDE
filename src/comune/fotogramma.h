/* Un fotogramma video pronto da mostrare, descritto senza FFmpeg e senza GTK
 * (C puro): e' cio' che i servizi (src/shell/servizi.h) consegnano alla
 * superficie. Due forme:
 *  - DMABUF: l'immagine e' nella memoria della scheda grafica e si mostra
 *    SENZA COPIE (PIANO.md, proposta di architettura, scelta 2; prototipo
 *    strumenti/prototipo-gtk.c: GdkDmabufTextureBuilder);
 *  - MEMORIA: pixel in memoria normale (ripiego, e i fotogrammi sintetici del
 *    backend finto).
 *
 * VITA. Il fotogramma e' di chi lo riceve, che DEVE chiamare
 * ph_fotogramma_rilascia esattamente una volta, da qualunque thread, quando
 * l'immagine non serve piu' (tipicamente nella funzione di rilascio della
 * GdkTexture). Fino ad allora fd e dati restano validi. Chi riceve non chiude
 * mai i descrittori e non libera mai i dati da se'. */
#ifndef PH_COMUNE_FOTOGRAMMA_H
#define PH_COMUNE_FOTOGRAMMA_H

#include <stddef.h>
#include <stdint.h>

#define PH_FOTO_PIANI_MAX 4

typedef enum {
    PH_FOTO_MEMORIA = 0,
    PH_FOTO_DMABUF = 1,
} PhFotoTipo;

/* Formati in memoria. I nomi seguono l'ordine dei byte in memoria. */
typedef enum {
    PH_FOTO_B8G8R8A8_PREMOLT = 0, /* = cairo ARGB32 su little endian, = GDK_MEMORY_B8G8R8A8_PREMULTIPLIED */
    PH_FOTO_R8G8B8A8,             /* = GDK_MEMORY_R8G8B8A8 (alfa non premoltiplicato) */
    PH_FOTO_R8G8B8,               /* = GDK_MEMORY_R8G8B8 (il ripiego con copia del prototipo) */
} PhFotoFormato;

/* Spazio dei colori dei fotogrammi YUV in DMA-BUF (H.264 del telefono: di
 * solito BT.709 a gamma limitata). PREDEFINITO = lascia decidere a GTK. */
typedef enum {
    PH_FOTO_COLORE_PREDEFINITO = 0,
    PH_FOTO_COLORE_BT709_LIMITATO,
    PH_FOTO_COLORE_BT601_LIMITATO,
    PH_FOTO_COLORE_BT709_PIENO,
    PH_FOTO_COLORE_BT601_PIENO, /* telefoni che dichiarano BT.601 a intervallo pieno */
} PhFotoColore;

typedef struct PhFotogramma {
    PhFotoTipo tipo;
    int larghezza, altezza;   /* pixel del video (= misura dichiarata dal flusso) */
    uint64_t tempo_us;        /* tempo del telefono (campo "tempo" del protocollo) */

    /* PH_FOTO_MEMORIA */
    PhFotoFormato formato;
    const uint8_t *dati;
    size_t passo;             /* byte per riga */

    /* PH_FOTO_DMABUF (stessi significati dei setter di GdkDmabufTextureBuilder) */
    uint32_t fourcc;          /* DRM_FORMAT_* (es. NV12, XRGB8888) */
    uint64_t modificatore;    /* DRM_FORMAT_MOD_* (0 = lineare) */
    int piani;                /* 1..PH_FOTO_PIANI_MAX */
    int fd[PH_FOTO_PIANI_MAX];
    uint32_t scostamento[PH_FOTO_PIANI_MAX];
    uint32_t passo_piano[PH_FOTO_PIANI_MAX];
    PhFotoColore colore;

    /* Vita: chiamata una volta da ph_fotogramma_rilascia (puo' essere NULL). */
    void (*rilascia)(void *dati_rilascio);
    void *dati_rilascio;
} PhFotogramma;

/* Rilascia il fotogramma (chiama rilascia una volta e azzera la struttura).
 * Chiamarla su una struttura gia' rilasciata o azzerata non fa nulla. */
static inline void ph_fotogramma_rilascia(PhFotogramma *f)
{
    if (f == NULL)
        return;
    void (*r)(void *) = f->rilascia;
    void *d = f->dati_rilascio;
    *f = (PhFotogramma){0};
    if (r != NULL)
        r(d);
}

#endif
