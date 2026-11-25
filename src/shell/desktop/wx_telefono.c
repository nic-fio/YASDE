/* Widget che leggono il telefono con una sola richiesta periodica: «Prossima sveglia» e
 * «Batteria in dettaglio» (CONTRATTI.md §16). Il contenuto e' una PhDskVista (logica_wx.c):
 * qui ci sono solo le richieste, gli annullamenti e le scadenze. */
#include <string.h>

#include "interno.h"

#define SVEGLIA_OGNI_US (5 * 60 * G_USEC_PER_SEC)
#define BATTERIA_OGNI_US (60 * G_USEC_PER_SEC)

/* ---- Sveglia ------------------------------------------------------------------------------------- */
typedef struct {
    PhDskDato stato;
    char frase[192];
    bool presente;
    uint64_t ora_ms;
    char pacchetto[PH_CFG_LUN_APP]; /* app della sveglia ("" = non nota) */
    GCancellable *annulla;
    int64_t t_us;
} Sveglia;

static void sveglia_fatta(PhServizi *s, PhSrvEsito esito, const char *frase, bool presente, uint64_t ora_ms,
                          const char *pacchetto, void *utente)
{
    (void)s;
    PhDskWidget *w = utente;
    Sveglia *x = w->x;
    if (x == NULL || !w->d->vivo)
        return;
    g_clear_object(&x->annulla);
    x->stato = ph_dsk_dato_da_esito(esito, frase);
    g_strlcpy(x->frase, x->stato == PH_DSK_DATO_ERRORE ? frase : "", sizeof x->frase);
    x->presente = esito == PH_SRV_ESITO_OK && presente;
    x->ora_ms = x->presente ? ora_ms : 0;
    g_strlcpy(x->pacchetto, pacchetto != NULL ? pacchetto : "", sizeof x->pacchetto);
    ph_dsk_widget_aggiorna_uno(w);
}

static void sveglia_richiedi(PhDskWidget *w)
{
    Sveglia *x = w->x;
    x->t_us = g_get_monotonic_time();
    ph_srv_tel_sveglia(w->d->srv, ph_dsk_wx_rinnova(&x->annulla), sveglia_fatta, w);
}

static void apri_app_sveglia(PhDskWidget *w)
{
    Sveglia *x = w->x;
    if (x != NULL && x->pacchetto[0] != '\0')
        ph_shell_apri_app(w->d->sh, x->pacchetto); /* come dal launcher */
}

static void sveglia_clic(GtkGestureClick *g, int n, double px, double py, gpointer dati)
{
    (void)g;
    (void)n;
    (void)px;
    (void)py;
    apri_app_sveglia(dati);
}

static gboolean sveglia_tasto(GtkEventControllerKey *k, guint tasto, guint codice, GdkModifierType stato, gpointer dati)
{
    (void)k;
    (void)codice;
    (void)stato;
    if (tasto == GDK_KEY_Return || tasto == GDK_KEY_KP_Enter || tasto == GDK_KEY_space) {
        apri_app_sveglia(dati);
        return TRUE;
    }
    return FALSE;
}

static void sveglia_inizia(PhDskWidget *w)
{
    Sveglia *x = g_new0(Sveglia, 1);
    x->stato = PH_DSK_DATO_ATTESA;
    w->x = x;
    /* Clic (senza trascinare) o Invio sulla scheda: apre l'app della sveglia, se nota. Il
     * gesto di trascinamento della scheda annulla il clic se il puntatore si muove. */
    GtkGesture *c = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(c), GDK_BUTTON_PRIMARY);
    g_signal_connect(c, "released", G_CALLBACK(sveglia_clic), w);
    gtk_widget_add_controller(w->scheda, GTK_EVENT_CONTROLLER(c));
    GtkEventController *k = gtk_event_controller_key_new();
    g_signal_connect(k, "key-pressed", G_CALLBACK(sveglia_tasto), w);
    gtk_widget_add_controller(w->scheda, k);
}

static void sveglia_ferma(PhDskWidget *w)
{
    Sveglia *x = w->x;
    ph_dsk_wx_annulla(&x->annulla);
    g_free(x);
    w->x = NULL;
}

static void sveglia_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    Sveglia *x = w->x;
    ph_dsk_vista_sveglia(w->dim, x->stato, x->frase, x->presente, x->ora_ms, (uint64_t)(g_get_real_time() / 1000),
                         NULL, v);
}

static void sveglia_tick(PhDskWidget *w, int64_t ora_us)
{
    Sveglia *x = w->x;
    ph_dsk_widget_aggiorna_uno(w); /* «fra 7 ore» scorre con il tempo */
    if (ora_us - x->t_us >= SVEGLIA_OGNI_US && ph_dsk_wx_puo_chiedere(w))
        sveglia_richiedi(w);
}

const PhDskWxVt PH_DSK_WX_SVEGLIA = {
    .inizia = sveglia_inizia,
    .ferma = sveglia_ferma,
    .aggiorna = sveglia_aggiorna,
    .richiedi = sveglia_richiedi,
    .tick = sveglia_tick,
};

/* ---- Batteria ---------------------------------------------------------------------------------------- */
typedef struct {
    PhDskDato stato;
    char frase[192];
    PhSrvBatteria dati;
    GCancellable *annulla;
    int64_t t_us;
} Batteria;

static void batteria_fatta(PhServizi *s, PhSrvEsito esito, const char *frase, const PhSrvBatteria *b, void *utente)
{
    (void)s;
    PhDskWidget *w = utente;
    Batteria *x = w->x;
    if (x == NULL || !w->d->vivo)
        return;
    g_clear_object(&x->annulla);
    x->stato = ph_dsk_dato_da_esito(esito, frase);
    g_strlcpy(x->frase, x->stato == PH_DSK_DATO_ERRORE ? frase : "", sizeof x->frase);
    if (esito == PH_SRV_ESITO_OK && b != NULL)
        x->dati = *b;
    else
        x->stato = x->stato == PH_DSK_DATO_OK ? PH_DSK_DATO_NON_DISPONIBILE : x->stato;
    ph_dsk_widget_aggiorna_uno(w);
}

static void batteria_richiedi(PhDskWidget *w)
{
    Batteria *x = w->x;
    x->t_us = g_get_monotonic_time();
    ph_srv_tel_batteria(w->d->srv, ph_dsk_wx_rinnova(&x->annulla), batteria_fatta, w);
}

static void batteria_inizia(PhDskWidget *w)
{
    Batteria *x = g_new0(Batteria, 1);
    x->stato = PH_DSK_DATO_ATTESA;
    w->x = x;
}

static void batteria_ferma(PhDskWidget *w)
{
    Batteria *x = w->x;
    ph_dsk_wx_annulla(&x->annulla);
    g_free(x);
    w->x = NULL;
}

static void batteria_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    Batteria *x = w->x;
    ph_dsk_vista_batteria(w->dim, x->stato, x->frase, &x->dati, v);
}

static void batteria_tick(PhDskWidget *w, int64_t ora_us)
{
    Batteria *x = w->x;
    if (ora_us - x->t_us >= BATTERIA_OGNI_US && ph_dsk_wx_puo_chiedere(w))
        batteria_richiedi(w);
}

const PhDskWxVt PH_DSK_WX_BATTERIA = {
    .inizia = batteria_inizia,
    .ferma = batteria_ferma,
    .aggiorna = batteria_aggiorna,
    .richiedi = batteria_richiedi,
    .tick = batteria_tick,
};
