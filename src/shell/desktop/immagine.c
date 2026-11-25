/* Immagini dello sfondo: decodifica, riduzione e copia nella cache del PC.
 * Le immagini arrivano dal telefono, quindi sono dato ostile: la misura si legge
 * dalle intestazioni PRIMA di decodificare (logica.c), con un tetto sul lato e sui
 * pixel; ogni scrittura e' dentro buffer dimensionati dalla misura gia' controllata.
 * Queste funzioni girano su un thread di lavoro (mai sul thread GTK): non toccano
 * widget ne' servizi, solo GdkTexture, cairo e i file della cache. */
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "interno.h"

/* Un file della cache non e' mai piu' grande di questo (e' gia' ridotto da noi). */
#define CACHE_BYTE_MAX (48u * 1024u * 1024u)

static GdkTexture *decodifica(GBytes *byte, PhErrore *e)
{
    gsize n = 0;
    const uint8_t *dati = g_bytes_get_data(byte, &n);
    uint32_t w = 0, h = 0;
    if (dati == NULL || n == 0 || n > PH_DSK_IMMAGINE_BYTE_MAX) {
        ph_errore(e, "immagine dello sfondo: dimensione dei dati non valida");
        return NULL;
    }
    if (!ph_dsk_immagine_misura(dati, n, &w, &h) || !ph_dsk_immagine_ammessa(w, h)) {
        ph_errore(e, "immagine dello sfondo: formato o misura non ammessi");
        return NULL;
    }
    GError *ge = NULL;
    GdkTexture *t = gdk_texture_new_from_bytes(byte, &ge);
    if (t == NULL) {
        ph_errore(e, "immagine dello sfondo: decodifica non riuscita");
        g_clear_error(&ge);
        return NULL;
    }
    /* Il decodificatore ha l'ultima parola sulla misura: deve restare nei tetti. */
    if (gdk_texture_get_width(t) < 1 || gdk_texture_get_height(t) < 1 ||
        !ph_dsk_immagine_ammessa((uint32_t)gdk_texture_get_width(t),
                                 (uint32_t)gdk_texture_get_height(t))) {
        ph_errore(e, "immagine dello sfondo: misura decodificata non ammessa");
        g_object_unref(t);
        return NULL;
    }
    return t;
}

GdkTexture *ph_dsk_immagine_da_cache(const char *nome)
{
    char percorso[PH_CFG_LUN_PERCORSO];
    PhErrore e = {{0}};
    if (nome == NULL || !ph_cfg_nome_cache_valido(nome) ||
        ph_cfg_percorso_cache(nome, percorso, sizeof percorso, &e) != 0)
        return NULL;
    PhBuffer b = PH_BUFFER_VUOTO;
    int r = ph_cfg_leggi_file(percorso, CACHE_BYTE_MAX, &b, &e);
    if (r != 0 || b.dati == NULL || b.lunghezza == 0) {
        ph_buffer_libera(&b);
        return NULL;
    }
    GBytes *byte = g_bytes_new(b.dati, b.lunghezza);
    ph_buffer_libera(&b);
    GdkTexture *t = decodifica(byte, &e);
    g_bytes_unref(byte);
    return t;
}

typedef struct {
    uint32_t W, H;
    bool troppo; /* il decodificatore dichiara una misura oltre i tetti */
} Utile;

/* Dimensione scelta quando il decodificatore dice la misura vera: la risoluzione
 * utile, cosi' una foto grande non viene mai tenuta intera in memoria (dove il
 * formato lo permette). L'orientamento EXIF si applica dopo e puo' scambiare i lati:
 * si tiene la misura che copre l'area in entrambi i casi. */
static void su_misura(GdkPixbufLoader *l, int w, int h, gpointer dati)
{
    Utile *u = dati;
    if (w < 1 || h < 1 || !ph_dsk_immagine_ammessa((uint32_t)w, (uint32_t)h)) {
        /* Rifiuto subito: nessuna memoria per un'immagine oltre i tetti. */
        u->troppo = true;
        gdk_pixbuf_loader_set_size(l, 1, 1);
        return;
    }
    uint32_t a, b, c, d;
    ph_dsk_immagine_ridotta((uint32_t)w, (uint32_t)h, u->W, u->H, &a, &b);
    ph_dsk_immagine_ridotta((uint32_t)h, (uint32_t)w, u->W, u->H, &c, &d); /* ruotata di 90 gradi */
    uint32_t nw = a, nh = b;
    if ((uint64_t)c * d > (uint64_t)a * b) {
        nw = d;
        nh = c;
    }
    if (nw != (uint32_t)w || nh != (uint32_t)h)
        gdk_pixbuf_loader_set_size(l, (int)nw, (int)nh);
}

int ph_dsk_immagine_prepara(GBytes *byte, uint32_t W, uint32_t H, const char *precedente,
                            char *nome_out, size_t cap, PhErrore *e)
{
    gsize n = 0;
    const uint8_t *dati = g_bytes_get_data(byte, &n);
    uint32_t w = 0, h = 0;
    if (dati == NULL || n == 0 || n > PH_DSK_IMMAGINE_BYTE_MAX)
        return ph_errore(e, "immagine dello sfondo: dimensione dei dati non valida");
    /* La misura dalle sole intestazioni, PRIMA di decodificare: i tetti. */
    if (!ph_dsk_immagine_misura(dati, n, &w, &h) || !ph_dsk_immagine_ammessa(w, h))
        return ph_errore(e, "immagine dello sfondo: formato o misura non ammessi");

    Utile u = {W, H, false};
    /* Il formato e' quello riconosciuto dalla firma: nessun altro decodificatore. */
    const char *formato = ph_dsk_immagine_formato(dati, n);
    GError *ge = NULL;
    GdkPixbufLoader *l = formato != NULL ? gdk_pixbuf_loader_new_with_type(formato, &ge) : NULL;
    if (l == NULL) {
        g_clear_error(&ge);
        return ph_errore(e, "immagine dello sfondo: formato non ammesso");
    }
    g_signal_connect(l, "size-prepared", G_CALLBACK(su_misura), &u);
    gboolean ok = gdk_pixbuf_loader_write(l, dati, n, &ge);
    gboolean chiuso = gdk_pixbuf_loader_close(l, ok ? &ge : NULL);
    GdkPixbuf *pb = ok && chiuso && !u.troppo ? gdk_pixbuf_loader_get_pixbuf(l) : NULL;
    if (pb != NULL)
        g_object_ref(pb);
    g_object_unref(l);
    g_clear_error(&ge);
    if (pb == NULL)
        return ph_errore(e, "immagine dello sfondo: decodifica non riuscita");

    /* Foto verticali del telefono: l'orientamento sta nell'EXIF, non nei pixel. */
    GdkPixbuf *dritto = gdk_pixbuf_apply_embedded_orientation(pb);
    g_object_unref(pb);
    if (dritto == NULL)
        return ph_errore(e, "immagine dello sfondo: memoria insufficiente");
    /* Dopo la rotazione si sa quale lato e' quale: ultimo passo alla misura utile esatta. */
    {
        uint32_t fw = (uint32_t)gdk_pixbuf_get_width(dritto), fh = (uint32_t)gdk_pixbuf_get_height(dritto);
        uint32_t rw, rh;
        ph_dsk_immagine_ridotta(fw, fh, W, H, &rw, &rh);
        if (rw < fw && rh < fh && rw >= 1 && rh >= 1) {
            GdkPixbuf *ridotto = gdk_pixbuf_scale_simple(dritto, (int)rw, (int)rh, GDK_INTERP_BILINEAR);
            if (ridotto != NULL) {
                g_object_unref(dritto);
                dritto = ridotto;
            }
        }
    }
    int ris = -1;
    gchar *png = NULL;
    gsize png_n = 0;
    if (gdk_pixbuf_get_width(dritto) < 1 || gdk_pixbuf_get_height(dritto) < 1 ||
        !ph_dsk_immagine_ammessa((uint32_t)gdk_pixbuf_get_width(dritto),
                                 (uint32_t)gdk_pixbuf_get_height(dritto))) {
        ph_errore(e, "immagine dello sfondo: misura decodificata non ammessa");
        goto fine;
    }
    if (!gdk_pixbuf_save_to_buffer(dritto, &png, &png_n, "png", &ge, NULL) || png_n == 0) {
        g_clear_error(&ge);
        ph_errore(e, "immagine dello sfondo: scrittura del PNG non riuscita");
        goto fine;
    }

    /* Nome nuovo, diverso dal precedente: se la scrittura fallisce il file in uso
     * resta intatto. */
    char nome[PH_CFG_LUN_FILE_CACHE];
    char percorso[PH_CFG_LUN_PERCORSO];
    nome[0] = '\0';
    for (int tentativi = 0; tentativi < 16; tentativi++) {
        ph_dsk_nome_sfondo(g_random_int(), nome, sizeof nome);
        if (ph_cfg_nome_cache_valido(nome) && (precedente == NULL || strcmp(nome, precedente) != 0))
            break;
        nome[0] = '\0';
    }
    if (nome[0] == '\0' || strlen(nome) >= cap) {
        ph_errore(e, "immagine dello sfondo: nome del file non valido");
        goto fine;
    }
    if (ph_cfg_percorso_cache(nome, percorso, sizeof percorso, e) != 0 ||
        ph_cfg_scrivi_file(percorso, png, png_n, e) != 0)
        goto fine;
    snprintf(nome_out, cap, "%s", nome);
    ris = 0;

fine:
    g_free(png);
    g_object_unref(dritto);
    return ris;
}

void ph_dsk_immagine_togli(const char *nome)
{
    /* Solo i file che scriviamo noi: il nome del profilo e' un dato modificabile. */
    if (nome == NULL || !ph_cfg_nome_cache_valido(nome) || !g_str_has_prefix(nome, "sfondo-") ||
        !g_str_has_suffix(nome, ".png"))
        return;
    char percorso[PH_CFG_LUN_PERCORSO];
    if (ph_cfg_percorso_cache(nome, percorso, sizeof percorso, NULL) == 0)
        unlink(percorso);
}
