/* Widget nuovi (CONTRATTI.md §16): tabella dei tipi, condizioni comuni per chiedere al
 * telefono, icone nuove (disegnate qui nello stile di quelle del mockup: tracciati
 * vettoriali nel colore del testo, nessuna immagine) e piccole utilita'. */
#include "interno.h"

/* ---- Icone nuove (viewBox 16x16, tratto 1,5: come quelle di stile.c) ------------------------- */
const PhShellIcona PH_DSK_ICONA_PREC = {"M9.5 3.5 5 8l4.5 4.5", NULL, 16, 16, 1.6f, 14, 14, false};
const PhShellIcona PH_DSK_ICONA_SUCC = {"M6.5 3.5 11 8l-4.5 4.5", NULL, 16, 16, 1.6f, 14, 14, false};
/* Suoneria che suona: campanella con due onde. */
const PhShellIcona PH_DSK_ICONA_SUONA = {
    "M4.5 11V7.5a3.5 3.5 0 0 1 7 0V11l1 1.5h-9z M6.5 14a1.5 1.5 0 0 0 3 0 M1.8 5.2A7 7 0 0 1 3.4 2.8 M14.2 5.2a7 7 0 0 0-1.6-2.4",
    NULL, 16, 16, 1.4f, 18, 18, false};
/* Vibrazione: telefono con trattini ai lati. */
const PhShellIcona PH_DSK_ICONA_VIBRA = {
    "M5.5 2.5h5a1 1 0 0 1 1 1v9a1 1 0 0 1-1 1h-5a1 1 0 0 1-1-1v-9a1 1 0 0 1 1-1z M2 5.5v5 M14 5.5v5 M0.5 7v2 M15.5 7v2",
    NULL, 16, 16, 1.4f, 18, 18, false};
/* Silenzioso: campanella barrata. */
const PhShellIcona PH_DSK_ICONA_SILENZIOSO = {
    "M4.5 11V7.5a3.5 3.5 0 0 1 7 0V11l1 1.5h-9z M6.5 14a1.5 1.5 0 0 0 3 0 M2.5 2.5l11 11",
    NULL, 16, 16, 1.4f, 18, 18, false};
/* Non disturbare: falce di luna. */
const PhShellIcona PH_DSK_ICONA_LUNA = {"M12.8 9.8A5.6 5.6 0 0 1 6.2 3.2a5.6 5.6 0 1 0 6.6 6.6z", NULL, 16, 16, 1.4f, 18, 18, false};
/* Torcia: corpo con la testa larga e il fascio. */
const PhShellIcona PH_DSK_ICONA_TORCIA = {
    "M5.5 1.8h5l.8 3h-6.6z M4.7 4.8h6.6V6.6L10 8v5.2a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1V8L4.7 6.6z", NULL, 16, 16, 1.4f, 18, 18, false};
/* Trova il telefono: telefono con onde di richiamo. */
const PhShellIcona PH_DSK_ICONA_TROVA = {
    "M5.5 3h5a1 1 0 0 1 1 1v8a1 1 0 0 1-1 1h-5a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z M2.3 5a4.5 4.5 0 0 0 0 6 M13.7 5a4.5 4.5 0 0 1 0 6",
    NULL, 16, 16, 1.4f, 18, 18, false};

/* ---- Icone di «Chiamate recenti», «Rete» e «In riproduzione» ---------------------------------------- */
/* Tipo di chiamata (indice = PH_SRV_CHIAMATA_*; 0 = altro, mai disegnata): freccia entrante,
 * uscente, persa (freccia in su a sinistra) e rifiutata (cerchio barrato). */
const PhShellIcona PH_DSK_ICONA_CHIAMATA[5] = {
    {"M3 8h10", NULL, 16, 16, 1.5f, 20, 20, false},
    {"M11.5 4.5 4.5 11.5 M4.5 6.5v5h5", NULL, 16, 16, 1.6f, 20, 20, false},
    {"M4.5 11.5 11.5 4.5 M6.5 4.5h5v5", NULL, 16, 16, 1.6f, 20, 20, false},
    {"M11.5 11.5 4.5 4.5 M4.5 9.5v-5h5", NULL, 16, 16, 1.6f, 20, 20, false},
    {"M8 2.5a5.5 5.5 0 1 0 0 11 5.5 5.5 0 0 0 0-11z M4.1 11.9 11.9 4.1", NULL, 16, 16, 1.5f, 20, 20, false},
};
/* Wi-Fi a livelli 0-4: il puntino e poi un arco in piu' a ogni livello. */
#define WIFI_PUNTO "M8 12.5a.9.9 0 1 0 0 1.8.9.9 0 0 0 0-1.8z"
const PhShellIcona PH_DSK_ICONA_WIFI[5] = {
    {NULL, WIFI_PUNTO, 16, 16, 1.5f, 22, 22, false},
    {"M5.9 11.4A3 3 0 0 1 10.1 11.4", WIFI_PUNTO, 16, 16, 1.5f, 22, 22, false},
    {"M5.9 11.4A3 3 0 0 1 10.1 11.4 M4.3 9.8A5.2 5.2 0 0 1 11.7 9.8", WIFI_PUNTO, 16, 16, 1.5f, 22, 22, false},
    {"M5.9 11.4A3 3 0 0 1 10.1 11.4 M4.3 9.8A5.2 5.2 0 0 1 11.7 9.8 M2.8 8.3A7.4 7.4 0 0 1 13.2 8.3", WIFI_PUNTO,
     16, 16, 1.5f, 22, 22, false},
    {"M5.9 11.4A3 3 0 0 1 10.1 11.4 M4.3 9.8A5.2 5.2 0 0 1 11.7 9.8 M2.8 8.3A7.4 7.4 0 0 1 13.2 8.3 "
     "M1.2 6.7A9.6 9.6 0 0 1 14.8 6.7",
     WIFI_PUNTO, 16, 16, 1.5f, 22, 22, false},
};
/* Rete mobile a livelli 0-4: la base e poi una barra in piu' a ogni livello. */
const PhShellIcona PH_DSK_ICONA_MOBILE[5] = {
    {"M2.5 13.5h11", NULL, 16, 16, 1.5f, 22, 22, false},
    {"M2.5 13.5h11 M3.5 13V11", NULL, 16, 16, 1.7f, 22, 22, false},
    {"M2.5 13.5h11 M3.5 13V11 M6.5 13V8.5", NULL, 16, 16, 1.7f, 22, 22, false},
    {"M2.5 13.5h11 M3.5 13V11 M6.5 13V8.5 M9.5 13V6", NULL, 16, 16, 1.7f, 22, 22, false},
    {"M2.5 13.5h11 M3.5 13V11 M6.5 13V8.5 M9.5 13V6 M12.5 13V3.5", NULL, 16, 16, 1.7f, 22, 22, false},
};
/* Pulsanti del lettore: indietro, avanti, play (pieni) e pausa. */
const PhShellIcona PH_DSK_ICONA_MEDIA_INDIETRO = {NULL, "M12 3.5v9L6.5 8z M3.8 3.5h1.8v9H3.8z", 16, 16, 1, 16, 16, false};
const PhShellIcona PH_DSK_ICONA_MEDIA_AVANTI = {NULL, "M4 3.5v9L9.5 8z M10.4 3.5h1.8v9h-1.8z", 16, 16, 1, 16, 16, false};
const PhShellIcona PH_DSK_ICONA_MEDIA_PLAY = {NULL, "M5 3v10l8-5z", 16, 16, 1, 18, 18, false};
const PhShellIcona PH_DSK_ICONA_MEDIA_PAUSA = {NULL, "M4.5 3h2.6v10H4.5z M8.9 3h2.6v10H8.9z", 16, 16, 1, 18, 18, false};

/* ---- Tabella dei tipi ----------------------------------------------------------------------------- */
const PhDskWxVt *ph_dsk_wx_vt(PhCfgWidgetTipo tipo)
{
    switch (tipo) {
    case PH_CFG_WIDGET_METEO: return &PH_DSK_WX_METEO;
    case PH_CFG_WIDGET_CALENDARIO_MESE: return &PH_DSK_WX_CALENDARIO_MESE;
    case PH_CFG_WIDGET_SVEGLIA: return &PH_DSK_WX_SVEGLIA;
    case PH_CFG_WIDGET_BATTERIA: return &PH_DSK_WX_BATTERIA;
    case PH_CFG_WIDGET_INTERRUTTORI: return &PH_DSK_WX_INTERRUTTORI;
    case PH_CFG_WIDGET_TROVA: return &PH_DSK_WX_TROVA;
    case PH_CFG_WIDGET_APP_FREQUENTI: return &PH_DSK_WX_APP_FREQUENTI;
    case PH_CFG_WIDGET_TRASFERIMENTI: return &PH_DSK_WX_TRASFERIMENTI;
    case PH_CFG_WIDGET_NOTE: return &PH_DSK_WX_NOTE;
    case PH_CFG_WIDGET_CHIAMATE: return &PH_DSK_WX_CHIAMATE;
    case PH_CFG_WIDGET_RETE: return &PH_DSK_WX_RETE;
    case PH_CFG_WIDGET_RIPRODUZIONE: return &PH_DSK_WX_MEDIA;
    default: return NULL;
    }
}

void ph_dsk_wx_inizia(PhDskWidget *w)
{
    const PhDskWxVt *vt = ph_dsk_wx_vt(w->tipo);
    if (vt != NULL && vt->inizia != NULL)
        vt->inizia(w);
}

void ph_dsk_wx_ferma(PhDskWidget *w)
{
    const PhDskWxVt *vt = ph_dsk_wx_vt(w->tipo);
    if (vt != NULL && w->x != NULL && vt->ferma != NULL)
        vt->ferma(w); /* libera w->x e lo azzera */
}

void ph_dsk_wx_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    const PhDskWxVt *vt = ph_dsk_wx_vt(w->tipo);
    memset(v, 0, sizeof *v);
    v->percento = -1;
    if (vt != NULL && vt->aggiorna != NULL && w->x != NULL)
        vt->aggiorna(w, v);
}

bool ph_dsk_wx_puo_chiedere(PhDskWidget *w)
{
    PhDesktop *d = w->d;
    if (!d->vivo || w->scheda == NULL || !ph_dsk_widget_visibili(d) || !gtk_widget_get_mapped(w->scheda))
        return false;
    PhSrvCollegamento c;
    ph_srv_collegamento(d->srv, &c);
    return c.ha_telefono;
}

void ph_dsk_wx_richiedi(PhDskWidget *w)
{
    const PhDskWxVt *vt = ph_dsk_wx_vt(w->tipo);
    if (vt != NULL && vt->richiedi != NULL && w->x != NULL && ph_dsk_wx_puo_chiedere(w))
        vt->richiedi(w);
}

void ph_dsk_wx_tick(PhDskWidget *w, int64_t ora_us)
{
    const PhDskWxVt *vt = ph_dsk_wx_vt(w->tipo);
    if (vt != NULL && vt->tick != NULL && w->x != NULL)
        vt->tick(w, ora_us);
}

bool ph_dsk_wx_secondi(PhDskWidget *w)
{
    const PhDskWxVt *vt = ph_dsk_wx_vt(w->tipo);
    return vt != NULL && vt->secondi != NULL && w->x != NULL && vt->secondi(w);
}

void ph_dsk_wx_evento(PhDskWidget *w, int ev)
{
    const PhDskWxVt *vt = ph_dsk_wx_vt(w->tipo);
    if (vt != NULL && vt->evento != NULL && w->x != NULL)
        vt->evento(w, ev);
}

GCancellable *ph_dsk_wx_rinnova(GCancellable **c)
{
    ph_dsk_wx_annulla(c);
    *c = g_cancellable_new();
    return *c;
}

void ph_dsk_wx_annulla(GCancellable **c)
{
    if (*c != NULL) {
        g_cancellable_cancel(*c);
        g_object_unref(*c);
        *c = NULL;
    }
}

void ph_dsk_wx_svuota(GtkWidget *box)
{
    GtkWidget *f;
    while ((f = gtk_widget_get_first_child(box)) != NULL)
        gtk_box_remove(GTK_BOX(box), f);
}
