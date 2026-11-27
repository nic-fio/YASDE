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
