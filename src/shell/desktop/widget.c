/* Widget del desktop (F-02c): Orologio, Calendario, Stato del telefono, Notifiche
 * recenti, Memoria del telefono. Il contenuto si calcola in logica.c (PhDskVista,
 * provato senza schermo); qui ci sono le schede, il trascinamento a passi di
 * 8 px, le dimensioni e il collegamento ai servizi.
 *
 * Riservatezza: i titoli delle notifiche e degli eventi vivono solo nelle
 * etichette a schermo: mai su disco, mai in un log. */
#include <string.h>
#include <time.h>

#include "interno.h"

/* Il catalogo iniziale dei widget (decisione del committente, CONTRATTI.md):
 * aggiungere un tipo e' una riga qui, piu' la sua vista in logica.c. */
const PhDskCatalogoVoce PH_DSK_CATALOGO[] = {
    {PH_CFG_WIDGET_OROLOGIO, PH_T_("Orologio"),
     PH_T_("Ora e data.")},
    {PH_CFG_WIDGET_CALENDARIO, PH_T_("Calendario"),
     PH_T_("I prossimi eventi di oggi, letti dal telefono.")},
    {PH_CFG_WIDGET_STATO, PH_T_("Stato del telefono"),
     PH_T_("Batteria, rete e stato del pannello fisico.")},
    {PH_CFG_WIDGET_NOTIFICHE, PH_T_("Notifiche recenti"),
     PH_T_("Le ultime notifiche del telefono, senza aprire il pannello.")},
    {PH_CFG_WIDGET_MEMORIA, PH_T_("Memoria del telefono"),
     PH_T_("Spazio usato e suddivisione per tipo di contenuto.")},
    {PH_CFG_WIDGET_METEO, PH_T_("Meteo"),
     PH_T_("Il tempo di adesso e le previsioni, in vari stili. Dati da Open-Meteo.com.")},
    {PH_CFG_WIDGET_CALENDARIO_MESE, PH_T_("Calendario del mese"),
     PH_T_("Il mese con i giorni degli impegni; clic su un giorno per vederli. Anche in settimana o in agenda.")},
    {PH_CFG_WIDGET_SVEGLIA, PH_T_("Prossima sveglia"), PH_T_("La prossima sveglia del telefono: giorno e ora.")},
    {PH_CFG_WIDGET_BATTERIA, PH_T_("Batteria in dettaglio"), PH_T_("Livello, carica e temperatura del telefono.")},
    {PH_CFG_WIDGET_INTERRUTTORI, PH_T_("Interruttori rapidi"),
     PH_T_("Suoneria, Non disturbare e torcia del telefono, con lo stato vero.")},
#if PH_DSK_TROVA_OFFERTO
    {PH_CFG_WIDGET_TROVA, PH_T_("Trova il telefono"), PH_T_("Un pulsante per far squillare il telefono e ritrovarlo.")},
#endif
    {PH_CFG_WIDGET_APP_FREQUENTI, PH_T_("App frequenti"), PH_T_("Le app che apri più spesso, a portata di clic.")},
    {PH_CFG_WIDGET_TRASFERIMENTI, PH_T_("Trasferimenti recenti"),
     PH_T_("Gli ultimi file inviati o ricevuti in questa sessione.")},
};
const size_t PH_DSK_CATALOGO_N = sizeof PH_DSK_CATALOGO / sizeof PH_DSK_CATALOGO[0];

const char *ph_dsk_widget_nome(PhCfgWidgetTipo tipo)
{
    for (size_t i = 0; i < PH_DSK_CATALOGO_N; i++)
        if (PH_DSK_CATALOGO[i].tipo == tipo)
            return ph_tv(PH_DSK_CATALOGO[i].nome);
    return NULL;
}

/* La struttura vive quanto la sua scheda (dato dell'oggetto, liberato alla
 * finalizzazione): i gesti della scheda non la trovano mai liberata. */
static void widget_libera(gpointer p)
{
    PhDskWidget *w = p;
    /* Richieste annullate e osservazioni tolte; nel "destroy" della radice (d->vivo falso) non si
     * tocca nessun servizio: vale solo il cancellare i propri GCancellable (CONTRATTI.md, ordine
     * di distruzione). */
    ph_dsk_wx_ferma(w);
    if (w->scheda != NULL && gtk_widget_get_parent(w->scheda) != NULL)
        gtk_widget_unparent(w->scheda);
}

/* Per g_ptr_array_new_with_free_func in desktop.c. */
void ph_dsk_widget_libera_elemento(gpointer p)
{
    widget_libera(p);
}

/* ---- Contenuto ------------------------------------------------------------------- */
static void vista_applica(PhDskWidget *w, const PhDskVista *v)
{
    gtk_widget_set_visible(w->titolo, v->titolo[0] != '\0');
    gtk_label_set_text(GTK_LABEL(w->titolo), v->titolo);
    /* Lo stile testuale va a capo; gli altri tagliano con i puntini. */
    bool a_capo = w->tipo == PH_CFG_WIDGET_OROLOGIO && w->stile == PH_DSK_OROLOGIO_TESTUALE;
    gtk_label_set_wrap(GTK_LABEL(w->testa), a_capo);
    gtk_label_set_ellipsize(GTK_LABEL(w->testa), a_capo ? PANGO_ELLIPSIZE_NONE : PANGO_ELLIPSIZE_END);
    gtk_widget_set_visible(w->testa, v->testa[0] != '\0');
    gtk_label_set_text(GTK_LABEL(w->testa), v->testa);
    if (v->testa_grande)
        gtk_widget_add_css_class(w->testa, "ph-desktop-w-grande");
    else
        gtk_widget_remove_css_class(w->testa, "ph-desktop-w-grande");
    gtk_widget_set_visible(w->sotto, v->sotto[0] != '\0');
    gtk_label_set_text(GTK_LABEL(w->sotto), v->sotto);
    gtk_widget_set_visible(w->barra, v->percento >= 0);
    if (v->percento >= 0) {
        /* La scheda ha larghezza fissa: dentro i bordi e l'imbottitura (16 + 1). */
        int utile = ph_dsk_larghezza_widget(w->dim) - 34;
        gtk_widget_set_size_request(w->pieno, utile * v->percento / 100, 6);
        gtk_widget_set_visible(w->pieno, v->percento > 0);
    }
    for (size_t i = 0; i < PH_DSK_RIGHE_MAX; i++) {
        bool c_e = i < v->n_righe;
        gtk_widget_set_visible(w->riga[i], c_e);
        if (c_e) {
            gtk_label_set_text(GTK_LABEL(w->riga_sx[i]), v->righe[i].sinistra);
            gtk_label_set_text(GTK_LABEL(w->riga_dx[i]), v->righe[i].destra);
            gtk_widget_set_visible(w->riga_dx[i], v->righe[i].destra[0] != '\0');
        }
    }
}

/* Ora congelata per le prove e le foto del demo (secondi dalla mezzanotte, -1 = nessuna). */
static int g_ora_fissa = -1;

void ph_dsk_orologio_ora_fissa(int secondi_dal_mezzanotte)
{
    g_ora_fissa = secondi_dal_mezzanotte >= 0 && secondi_dal_mezzanotte < 86400 ? secondi_dal_mezzanotte : -1;
}

static GDateTime *adesso_dt(void)
{
    GDateTime *dt = g_date_time_new_now_local();
    if (g_ora_fissa >= 0) {
        GDateTime *f = g_date_time_new_local(g_date_time_get_year(dt), g_date_time_get_month(dt),
                                             g_date_time_get_day_of_month(dt), g_ora_fissa / 3600,
                                             (g_ora_fissa / 60) % 60, g_ora_fissa % 60);
        g_date_time_unref(dt);
        dt = f;
    }
    return dt;
}

static void adesso(uint64_t *ms, int *scarto, char *ora, size_t cap_ora, char *data, size_t cap_data)
{
    GDateTime *dt = adesso_dt();
    *ms = (uint64_t)(g_date_time_to_unix(dt)) * 1000u + (uint64_t)(g_date_time_get_microsecond(dt) / 1000);
    *scarto = (int)(g_date_time_get_utc_offset(dt) / G_USEC_PER_SEC);
    char *o = g_date_time_format(dt, "%H:%M");
    g_strlcpy(ora, o != NULL ? o : "", cap_ora);
    g_free(o);
    ph_dsk_data_lunga(g_date_time_get_day_of_week(dt), g_date_time_get_day_of_month(dt),
                      g_date_time_get_month(dt), data, cap_data);
    g_date_time_unref(dt);
}

/* L'orologio nello stile scelto. */
static void orologio_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    GDateTime *dt = adesso_dt();
    uint64_t ms = (uint64_t)g_date_time_to_unix(dt) * 1000u;
    int scarto = (int)(g_date_time_get_utc_offset(dt) / G_USEC_PER_SEC);
    int ore = g_date_time_get_hour(dt), minuti = g_date_time_get_minute(dt),
        secondi = g_date_time_get_second(dt);
    char ora[16], data[96];
    snprintf(ora, sizeof ora, "%02d:%02d", ore, minuti);
    ph_dsk_data_lunga(g_date_time_get_day_of_week(dt), g_date_time_get_day_of_month(dt),
                      g_date_time_get_month(dt), data, sizeof data);
    g_date_time_unref(dt);
    bool disegnato = ph_dsk_stile_disegnato(w->stile);
    gtk_widget_set_visible(w->quadrante, disegnato);
    if (disegnato) {
        PhDskOrologioDati dd;
        memset(&dd, 0, sizeof dd);
        dd.stile = w->stile;
        dd.dim = (int)w->dim;
        dd.ore = ore;
        dd.minuti = minuti;
        dd.secondi = ph_dsk_stile_con_secondi(w->stile) ? secondi : 0;
        g_strlcpy(dd.data, data, sizeof dd.data);
        ph_dsk_orologio_imposta(w->quadrante, &dd);
        memset(v, 0, sizeof *v); /* niente etichette: lo disegna il quadrante */
        v->percento = -1;
        return;
    }
    switch (w->stile) {
    case PH_DSK_OROLOGIO_TESTUALE: {
        char frase[160];
        ph_dsk_ora_testo(ore, minuti, frase, sizeof frase);
        ph_dsk_vista_orologio_testo(w->dim, frase, data, v);
        break;
    }
    case PH_DSK_OROLOGIO_DOPPIO_FUSO: {
        int o2 = 0, m2 = 0, gd = 0;
        bool ok = ph_dsk_ora_in_citta(ms, scarto, w->opzione, &o2, &m2, &gd);
        ph_dsk_vista_orologio_fuso(w->dim, ora, data, ph_dsk_citta_nome(w->opzione), ok, o2, m2, gd, v);
        break;
    }
    default:
        ph_dsk_vista_orologio(w->dim, ora, data, v);
        break;
    }
}

static void widget_aggiorna(PhDskWidget *w)
{
    PhDesktop *d = w->d;
    PhDskVista v;
    switch (w->tipo) {
    case PH_CFG_WIDGET_OROLOGIO:
        orologio_aggiorna(w, &v);
        break;
    case PH_CFG_WIDGET_CALENDARIO: {
        uint64_t ms;
        int scarto;
        char ora[16], data[96];
        adesso(&ms, &scarto, ora, sizeof ora, data, sizeof data);
        PhDskEvento ev[PH_SRV_CALENDARIO_MAX];
        size_t n = d->n_eventi < PH_SRV_CALENDARIO_MAX ? d->n_eventi : PH_SRV_CALENDARIO_MAX;
        for (size_t i = 0; i < n; i++) {
            ev[i].titolo = d->eventi[i].titolo;
            ev[i].inizio_ms = d->eventi[i].inizio_ms;
            ev[i].fine_ms = d->eventi[i].fine_ms;
            ev[i].tutto_il_giorno = d->eventi[i].tutto_il_giorno;
        }
        ph_dsk_vista_calendario(w->dim, d->eventi_noti, ev, n, ms, scarto, data, &v);
        break;
    }
    case PH_CFG_WIDGET_STATO: {
        PhSrvTelefono t;
        PhSrvCollegamento c;
        ph_srv_telefono(d->srv, &t);
        ph_srv_collegamento(d->srv, &c);
        ph_dsk_vista_stato(w->dim, &t, &c, &v);
        break;
    }
    case PH_CFG_WIDGET_NOTIFICHE: {
        size_t n = 0;
        const PhSrvNotifica *tutte = ph_srv_notifiche(d->srv, &n, NULL);
        PhDskNotifica *vis = g_new0(PhDskNotifica, n + 1);
        size_t k = 0;
        for (size_t i = 0; i < n; i++) {
            if (ph_shell_notifica_nascosta(d->sh, tutte[i].chiave, tutte[i].ora_ms))
                continue; /* contano solo quelle non nascoste */
            vis[k].titolo = tutte[i].titolo;
            vis[k].testo = tutte[i].testo;
            k++;
        }
        ph_dsk_vista_notifiche(w->dim, vis, k, &v);
        g_free(vis);
        break;
    }
    case PH_CFG_WIDGET_MEMORIA:
        ph_dsk_vista_memoria(w->dim, d->memoria_nota, d->memoria_totale, d->memoria_libera,
                             d->dettaglio_noto ? &d->dettaglio : NULL, &v);
        break;
    default:
        if (ph_dsk_wx_vt(w->tipo) == NULL)
            return;
        ph_dsk_wx_aggiorna(w, &v);
        break;
    }
    vista_applica(w, &v);
}

void ph_dsk_widget_aggiorna_uno(PhDskWidget *w)
{
    if (w->d->vivo && w->scheda != NULL)
        widget_aggiorna(w);
}

void ph_dsk_widget_aggiorna_tutti(PhDesktop *d)
{
    for (size_t i = 0; i < d->widget->len; i++)
        widget_aggiorna(g_ptr_array_index(d->widget, i));
}

/* ---- Posizione ------------------------------------------------------------------- */
static void widget_colloca(PhDskWidget *w)
{
    PhDesktop *d = w->d;
    int x = d->area_l - w->destra - ph_dsk_larghezza_widget(w->dim);
    ph_shell_libero_sposta(d->libero, w->scheda, x, w->alto, ph_dsk_larghezza_widget(w->dim), -1);
}

void ph_dsk_widget_riposiziona(PhDesktop *d)
{
    for (size_t i = 0; i < d->widget->len; i++)
        widget_colloca(g_ptr_array_index(d->widget, i));
}

/* ---- Trascinamento -------------------------------------------------------------------- */
static void trascina_inizio(GtkGestureDrag *g, double x, double y, gpointer dati)
{
    (void)g;
    PhDskWidget *w = dati;
    ph_dsk_in_radice(w->d, w->scheda, x, y, &w->inizio_x, &w->inizio_y);
    w->destra0 = w->destra;
    w->alto0 = w->alto;
}

static void trascina_aggiorna(GtkGestureDrag *g, double ox, double oy, gpointer dati)
{
    PhDskWidget *w = dati;
    PhDesktop *d = w->d;
    /* La scheda si muove sotto il puntatore, e le coordinate del gesto sono quelle
     * della scheda: il punto attuale si riporta nelle coordinate della radice. */
    double sx, sy, rx, ry;
    gtk_gesture_drag_get_start_point(g, &sx, &sy);
    ph_dsk_in_radice(d, w->scheda, sx + ox, sy + oy, &rx, &ry);
    double dx = rx - w->inizio_x, dy = ry - w->inizio_y;
    int destra = ph_dsk_aggancia_destra(w->destra0, dx);
    int alto = ph_dsk_aggancia_alto(w->alto0, dy);
    /* Mai tutta fuori dall'area: ne resta in vista una parte per riprenderla. */
    int max_destra = d->area_l - 40;
    int max_alto = d->area_a - PH_DSK_BARRA - 24;
    if (max_destra < 0)
        max_destra = 0;
    if (max_alto < 0)
        max_alto = 0;
    if (destra > max_destra && destra > w->destra0)
        destra = w->destra0 > max_destra ? w->destra0 : max_destra;
    if (alto > max_alto && alto > w->alto0)
        alto = w->alto0 > max_alto ? w->alto0 : max_alto;
    if (destra == w->destra && alto == w->alto)
        return;
    w->destra = destra;
    w->alto = alto;
    widget_colloca(w);
}

static void trascina_fine(GtkGestureDrag *g, double ox, double oy, gpointer dati)
{
    (void)g;
    (void)ox;
    (void)oy;
    PhDskWidget *w = dati;
    PhDesktop *d = w->d;
    if (w->destra == w->destra0 && w->alto == w->alto0)
        return;
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p == NULL || w->indice >= p->n_widget || p->widget[w->indice].tipo != w->tipo)
        return;
    p->widget[w->indice].destra = w->destra;
    p->widget[w->indice].alto = w->alto;
    ph_dsk_salva(d);
}

/* ---- Menu ----------------------------------------------------------------------------- */
static void menu_pulsante(GtkGestureClick *g, int n, double x, double y, gpointer dati)
{
    (void)n;
    PhDskWidget *w = dati;
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
    double rx, ry;
    ph_dsk_in_radice(w->d, w->scheda, x, y, &rx, &ry);
    ph_dsk_menu_widget(w->d, w->id, rx, ry);
}

static void menu_tasto(GtkWidget *s, gpointer dati)
{
    PhDskWidget *w = dati;
    double rx, ry;
    ph_dsk_in_radice(w->d, s, 16, 16, &rx, &ry);
    ph_dsk_menu_widget(w->d, w->id, rx, ry);
}

/* ---- Creazione ---------------------------------------------------------------------------- */
static GtkWidget *etichetta_nuova(const char *classe, bool ellissi)
{
    GtkWidget *l = gtk_label_new("");
    gtk_widget_add_css_class(l, classe);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    if (ellissi) {
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(l), 1); /* nessuna larghezza minima */
    }
    return l;
}

static PhDskWidget *widget_nuovo(PhDesktop *d, size_t indice, const PhCfgWidget *cfg)
{
    PhDskWidget *w = g_new0(PhDskWidget, 1);
    w->d = d;
    w->id = ++d->prossimo_id;
    w->indice = indice;
    w->tipo = cfg->tipo;
    w->dim = cfg->dimensione;
    w->stile = ph_dsk_stile_valido_di(cfg->tipo, (unsigned)cfg->stile);
    w->opzione = ph_dsk_citta_valida((unsigned)cfg->opzione);
    w->destra = cfg->destra;
    w->alto = cfg->alto;

    w->scheda = g_object_new(GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL,
                             "accessible-role", GTK_ACCESSIBLE_ROLE_GROUP, NULL);
    g_object_set_data_full(G_OBJECT(w->scheda), "ph-desktop-widget", w, g_free);
    g_object_set_data(G_OBJECT(w->scheda), "ph-desktop-id", GUINT_TO_POINTER(w->id));
    gtk_widget_add_css_class(w->scheda, "ph-desktop-widget");
    /* Vetro chiaro, raggio 14 come il CSS; si toglie da solo alla distruzione. */
    ph_shell_vetro_registra(d->sh, w->scheda, 14, PH_VETRO_CHIARO);
    gtk_widget_set_size_request(w->scheda, ph_dsk_larghezza_widget(w->dim), -1);
    gtk_widget_set_cursor_from_name(w->scheda, "move");
    gtk_widget_set_focusable(w->scheda, TRUE); /* per il tasto Menu da tastiera */
    ph_shell_etichetta(w->scheda, ph_dsk_widget_nome(w->tipo), NULL);

    w->titolo = etichetta_nuova("ph-desktop-w-titolo", true);
    w->testa = etichetta_nuova("ph-desktop-w-testa", true);
    w->sotto = etichetta_nuova("ph-desktop-w-sotto", true);
    w->barra = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(w->barra, "ph-desktop-w-barra");
    gtk_widget_set_overflow(w->barra, GTK_OVERFLOW_HIDDEN);
    w->pieno = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(w->pieno, "ph-desktop-w-pieno");
    gtk_widget_set_halign(w->pieno, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(w->barra), w->pieno);
    w->quadrante = ph_dsk_orologio_nuovo();
    gtk_widget_set_visible(w->quadrante, FALSE);
    gtk_box_append(GTK_BOX(w->scheda), w->titolo);
    gtk_box_append(GTK_BOX(w->scheda), w->quadrante);
    w->corpo = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_visible(w->corpo, FALSE);
    gtk_box_append(GTK_BOX(w->scheda), w->corpo);
    gtk_box_append(GTK_BOX(w->scheda), w->testa);
    gtk_box_append(GTK_BOX(w->scheda), w->sotto);
    gtk_box_append(GTK_BOX(w->scheda), w->barra);
    for (size_t i = 0; i < PH_DSK_RIGHE_MAX; i++) {
        w->riga[i] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_add_css_class(w->riga[i], "ph-desktop-w-riga");
        w->riga_sx[i] = etichetta_nuova("ph-desktop-w-riga-sx", true);
        gtk_widget_set_hexpand(w->riga_sx[i], TRUE);
        w->riga_dx[i] = etichetta_nuova("ph-desktop-w-riga-dx", false);
        gtk_box_append(GTK_BOX(w->riga[i]), w->riga_sx[i]);
        gtk_box_append(GTK_BOX(w->riga[i]), w->riga_dx[i]);
        gtk_box_append(GTK_BOX(w->scheda), w->riga[i]);
    }

    GtkGesture *dr = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dr), GDK_BUTTON_PRIMARY);
    g_signal_connect(dr, "drag-begin", G_CALLBACK(trascina_inizio), w);
    g_signal_connect(dr, "drag-update", G_CALLBACK(trascina_aggiorna), w);
    g_signal_connect(dr, "drag-end", G_CALLBACK(trascina_fine), w);
    gtk_widget_add_controller(w->scheda, GTK_EVENT_CONTROLLER(dr));
    GtkGesture *dx = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dx), GDK_BUTTON_SECONDARY);
    g_signal_connect(dx, "pressed", G_CALLBACK(menu_pulsante), w);
    gtk_widget_add_controller(w->scheda, GTK_EVENT_CONTROLLER(dx));
    ph_dsk_tasto_menu_aggancia(w->scheda, G_CALLBACK(menu_tasto), w);

    ph_shell_libero_metti(d->libero, w->scheda, 0, 0, ph_dsk_larghezza_widget(w->dim), -1);
    widget_colloca(w);
    if (ph_dsk_wx_vt(w->tipo) != NULL) {
        ph_dsk_wx_inizia(w);
        /* Si chiede al telefono solo a widget mappato (e collegato): al "map" e poi dal timer. */
        g_signal_connect_data(w->scheda, "map", G_CALLBACK(ph_dsk_wx_richiedi), w, NULL,
                              G_CONNECT_SWAPPED | G_CONNECT_AFTER); /* dopo: allora il widget risulta mappato */
        if (gtk_widget_get_mapped(w->scheda))
            ph_dsk_wx_richiedi(w); /* ricostruzione a finestra gia' aperta: il "map" e' gia' passato */
    }
    gtk_widget_set_visible(w->scheda, ph_dsk_widget_visibili(d));
    widget_aggiorna(w);
    return w;
}

/* ---- Timer e memoria ------------------------------------------------------------------ */
static bool serve_il_timer(PhDesktop *d)
{
    for (size_t i = 0; i < d->widget->len; i++) {
        PhDskWidget *w = g_ptr_array_index(d->widget, i);
        if (w->tipo == PH_CFG_WIDGET_OROLOGIO || w->tipo == PH_CFG_WIDGET_CALENDARIO ||
            w->tipo == PH_CFG_WIDGET_MEMORIA)
            return true;
        /* I widget nuovi tengono acceso il timer solo se hanno qualcosa da fare nel tempo
         * (richieste periodiche, conto alla rovescia): App frequenti e Trasferimenti no. */
        const PhDskWxVt *vt = ph_dsk_wx_vt(w->tipo);
        if (vt != NULL && (vt->tick != NULL || vt->secondi != NULL))
            return true;
    }
    return false;
}

static bool c_e_tipo(PhDesktop *d, PhCfgWidgetTipo tipo)
{
    for (size_t i = 0; i < d->widget->len; i++)
        if (((PhDskWidget *)g_ptr_array_index(d->widget, i))->tipo == tipo)
            return true;
    return false;
}

/* Si chiede al telefono solo se serve: il widget c'e', i widget non sono nascosti
 * (preferenza) e il telefono e' collegato. */
static bool serve_il_telefono(PhDesktop *d, PhCfgWidgetTipo tipo)
{
    if (!d->vivo || !ph_dsk_widget_visibili(d) || !c_e_tipo(d, tipo))
        return false;
    PhSrvCollegamento c;
    ph_srv_collegamento(d->srv, &c);
    return c.ha_telefono;
}

static void rinnova(GCancellable **c)
{
    if (*c != NULL) {
        g_cancellable_cancel(*c);
        g_object_unref(*c);
    }
    *c = g_cancellable_new();
}

static void memoria_fatta(PhServizi *s, PhSrvEsito esito, uint64_t totale, uint64_t libera,
                          void *utente)
{
    (void)s;
    PhDesktop *d = utente;
    if (!d->vivo)
        return;
    g_clear_object(&d->annulla_memoria);
    d->memoria_nota = esito == PH_SRV_ESITO_OK && totale > 0;
    d->memoria_totale = d->memoria_nota ? totale : 0;
    d->memoria_libera = d->memoria_nota ? libera : 0;
    ph_dsk_widget_aggiorna_tutti(d);
}

static void dettaglio_fatto(PhServizi *s, PhSrvEsito esito, const char *frase,
                            const PhSrvMemoriaDettaglio *det, void *utente)
{
    (void)s;
    (void)frase;
    PhDesktop *d = utente;
    if (!d->vivo)
        return;
    g_clear_object(&d->annulla_dettaglio);
    d->dettaglio_noto = esito == PH_SRV_ESITO_OK && det != NULL;
    if (d->dettaglio_noto)
        d->dettaglio = *det;
    ph_dsk_widget_aggiorna_tutti(d);
}

void ph_dsk_widget_memoria_chiedi(PhDesktop *d)
{
    if (!serve_il_telefono(d, PH_CFG_WIDGET_MEMORIA))
        return;
    d->t_memoria = g_get_monotonic_time();
    rinnova(&d->annulla_memoria);
    ph_srv_tel_memoria(d->srv, d->annulla_memoria, memoria_fatta, d);
    rinnova(&d->annulla_dettaglio);
    ph_srv_tel_memoria_dettaglio(d->srv, d->annulla_dettaglio, dettaglio_fatto, d);
}

/* ---- Calendario ----------------------------------------------------------------------------
 * RISERVATEZZA: gli eventi (titoli compresi) restano solo in questa memoria, per il
 * widget a schermo; mai su disco, mai in un log. Si chiedono SOLO se c'e' un widget
 * Calendario (e i widget non sono nascosti); si buttano appena non servono. */
static void eventi_libera(PhDesktop *d)
{
    for (size_t i = 0; i < d->n_eventi; i++) {
        g_free(d->eventi[i].titolo);
        d->eventi[i].titolo = NULL;
    }
    d->n_eventi = 0;
    d->eventi_noti = false;
}

static void calendario_fatto(PhServizi *s, PhSrvEsito esito, const char *frase,
                             const PhSrvEventoCalendario *v, size_t n, void *utente)
{
    (void)s;
    (void)frase;
    PhDesktop *d = utente;
    if (!d->vivo)
        return;
    g_clear_object(&d->annulla_calendario);
    eventi_libera(d);
    if (esito == PH_SRV_ESITO_OK && v != NULL) {
        if (n > PH_SRV_CALENDARIO_MAX)
            n = PH_SRV_CALENDARIO_MAX;
        for (size_t i = 0; i < n; i++) {
            d->eventi[i].titolo = g_strdup(v[i].titolo != NULL ? v[i].titolo : "");
            d->eventi[i].inizio_ms = v[i].inizio_ms;
            d->eventi[i].fine_ms = v[i].fine_ms;
            d->eventi[i].tutto_il_giorno = v[i].tutto_il_giorno;
        }
        d->n_eventi = n;
        d->eventi_noti = true; /* elenco vuoto vero: "nessun evento", non "dati assenti" */
    }
    ph_dsk_widget_aggiorna_tutti(d);
}

void ph_dsk_widget_calendario_chiedi(PhDesktop *d)
{
    if (!serve_il_telefono(d, PH_CFG_WIDGET_CALENDARIO))
        return;
    d->t_calendario = g_get_monotonic_time();
    rinnova(&d->annulla_calendario);
    ph_srv_tel_calendario(d->srv, PH_SRV_CALENDARIO_MAX, d->annulla_calendario, calendario_fatto, d);
}

void ph_dsk_widget_dati_azzera(PhDesktop *d)
{
    /* Un altro telefono: i dati dell'altro non devono restare. */
    if (d->annulla_memoria != NULL) {
        g_cancellable_cancel(d->annulla_memoria);
        g_clear_object(&d->annulla_memoria);
    }
    if (d->annulla_dettaglio != NULL) {
        g_cancellable_cancel(d->annulla_dettaglio);
        g_clear_object(&d->annulla_dettaglio);
    }
    if (d->annulla_calendario != NULL) {
        g_cancellable_cancel(d->annulla_calendario);
        g_clear_object(&d->annulla_calendario);
    }
    d->memoria_nota = false;
    d->dettaglio_noto = false;
    eventi_libera(d);
}

/* Un solo timer condiviso. Scatta quando serve: ogni secondo se c'e' una lancetta dei
 * secondi VISIBILE, altrimenti al cambio di minuto; con la finestra host nascosta o
 * minimizzata si limita a controllare ogni 30 s (e non ridisegna nulla). */
static bool serve_secondi(PhDesktop *d)
{
    for (size_t i = 0; i < d->widget->len; i++) {
        PhDskWidget *w = g_ptr_array_index(d->widget, i);
        if (w->tipo == PH_CFG_WIDGET_OROLOGIO && ph_dsk_stile_con_secondi(w->stile) &&
            gtk_widget_get_mapped(w->scheda))
            return true;
        if (ph_dsk_wx_vt(w->tipo) != NULL && gtk_widget_get_mapped(w->scheda) && ph_dsk_wx_secondi(w))
            return true;
    }
    return false;
}

static void timer_programma(PhDesktop *d);

static gboolean timer_scatto(gpointer dati)
{
    PhDesktop *d = dati;
    d->timer_ora = 0; /* questa sorgente finisce qui: se ne programma un'altra */
    if (!d->vivo || !serve_il_timer(d))
        return G_SOURCE_REMOVE;
    if (ph_shell_host_visibile(d->sh)) {
        GDateTime *dt = g_date_time_new_now_local();
        int giorno = g_date_time_get_day_of_year(dt);
        g_date_time_unref(dt);
        int64_t ora = g_get_monotonic_time();
        /* Calendario: anche al cambio di giorno e ogni 15 minuti (stesse condizioni di
         * sempre, dentro la chiedi: widget presente, non nascosto, telefono collegato). */
        bool cambio_giorno = d->giorno_visto != 0 && giorno != d->giorno_visto;
        d->giorno_visto = giorno;
        if (cambio_giorno || ora - d->t_calendario >= 15 * 60 * G_USEC_PER_SEC)
            ph_dsk_widget_calendario_chiedi(d);
        if (ora - d->t_memoria >= 60 * G_USEC_PER_SEC)
            ph_dsk_widget_memoria_chiedi(d); /* ogni 60 s (COMPITO.md) */
        for (size_t i = 0; i < d->widget->len; i++) {
            PhDskWidget *w = g_ptr_array_index(d->widget, i);
            if (w->tipo == PH_CFG_WIDGET_OROLOGIO || w->tipo == PH_CFG_WIDGET_CALENDARIO)
                if (gtk_widget_get_mapped(w->scheda))
                    widget_aggiorna(w);
            if (ph_dsk_wx_vt(w->tipo) != NULL && gtk_widget_get_mapped(w->scheda))
                ph_dsk_wx_tick(w, ora); /* richieste periodiche e aggiornamenti propri */
        }
    }
    timer_programma(d);
    return G_SOURCE_REMOVE;
}

static void timer_programma(PhDesktop *d)
{
    if (!d->vivo || d->timer_ora != 0 || !serve_il_timer(d))
        return;
    unsigned ms;
    if (!ph_shell_host_visibile(d->sh)) {
        ms = 30000;
    } else {
        ms = ph_dsk_orologio_attesa_ms((uint64_t)(g_get_real_time() / 1000), serve_secondi(d));
        if (ms > 30000)
            ms = 30000; /* si ricontrolla almeno ogni 30 s (visibilita', nuovi widget) */
    }
    d->timer_ora = g_timeout_add(ms, timer_scatto, d);
}

/* Cambiano le esigenze di precisione (per esempio inizia uno squillo): si riprogramma il timer. */
void ph_dsk_widget_timer_riprogramma(PhDesktop *d)
{
    if (!d->vivo)
        return;
    if (d->timer_ora != 0) {
        g_source_remove(d->timer_ora);
        d->timer_ora = 0;
    }
    timer_programma(d);
}

/* La finestra host torna visibile (o i widget cambiano): ridisegna e riprogramma subito. */
void ph_dsk_widget_timer_riavvia(PhDesktop *d)
{
    if (!d->vivo)
        return;
    if (d->timer_ora != 0) {
        g_source_remove(d->timer_ora);
        d->timer_ora = 0;
    }
    ph_dsk_widget_aggiorna_tutti(d);
    timer_programma(d);
}

/* ---- Insieme dei widget ------------------------------------------------------------------ */
void ph_dsk_widget_ricostruisci(PhDesktop *d)
{
    g_ptr_array_set_size(d->widget, 0);
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p != NULL) {
        for (size_t i = 0; i < p->n_widget && i < PH_CFG_MAX_WIDGET; i++) {
            /* Un tipo fuori catalogo non si mostra e NON si cancella dal profilo. */
            if (ph_dsk_widget_nome(p->widget[i].tipo) == NULL)
                continue;
            g_ptr_array_add(d->widget, widget_nuovo(d, i, &p->widget[i]));
        }
    }
    timer_programma(d);
    if (!c_e_tipo(d, PH_CFG_WIDGET_CALENDARIO))
        eventi_libera(d); /* non servono: non si tengono */
    ph_dsk_widget_memoria_chiedi(d);
    ph_dsk_widget_calendario_chiedi(d);
}

void ph_dsk_widget_libera(PhDesktop *d)
{
    for (size_t i = 0; i < d->widget->len; i++)
        ((PhDskWidget *)g_ptr_array_index(d->widget, i))->scheda = NULL;
    g_ptr_array_set_size(d->widget, 0);
    if (d->timer_ora != 0) {
        g_source_remove(d->timer_ora);
        d->timer_ora = 0;
    }
    ph_dsk_widget_dati_azzera(d);
}

static PhDskWidget *cerca(PhDesktop *d, unsigned id)
{
    for (size_t i = 0; i < d->widget->len; i++) {
        PhDskWidget *w = g_ptr_array_index(d->widget, i);
        if (w->id == id)
            return w;
    }
    return NULL;
}

bool ph_dsk_widget_dimensione_di(PhDesktop *d, unsigned id, PhCfgWidgetDim *dim)
{
    PhDskWidget *w = cerca(d, id);
    if (w == NULL)
        return false;
    *dim = w->dim;
    return true;
}

void ph_dsk_widget_aggiungi(PhDesktop *d, PhCfgWidgetTipo tipo)
{
    PhCfgTelefono *p = ph_dsk_profilo(d);
    const char *nome = ph_dsk_widget_nome(tipo);
    if (p == NULL || nome == NULL || p->n_widget >= PH_CFG_MAX_WIDGET)
        return;
    PhDskWidgetPos pos[PH_CFG_MAX_WIDGET];
    size_t np = 0;
    for (size_t i = 0; i < p->n_widget; i++) {
        if (ph_dsk_widget_nome(p->widget[i].tipo) == NULL)
            continue; /* i tipi fuori catalogo non si vedono: non occupano posto */
        pos[np].destra = p->widget[i].destra;
        pos[np].alto = p->widget[i].alto;
        pos[np].dimensione = p->widget[i].dimensione;
        np++;
    }
    int destra, alto;
    ph_dsk_posto_widget_nuovo(d->area_a - PH_DSK_BARRA, pos, np, &destra, &alto);
    PhCfgWidget *n = &p->widget[p->n_widget++];
    n->tipo = tipo;
    n->destra = destra;
    n->alto = alto;
    n->dimensione = PH_CFG_WIDGET_MEDIO;
    ph_dsk_salva(d);
    ph_dsk_widget_ricostruisci(d);
    ph_dsk_avviso(d, ph_t("Widget aggiunto"), nome);
}

void ph_dsk_widget_dimensione(PhDesktop *d, unsigned id, PhCfgWidgetDim dim)
{
    PhDskWidget *w = cerca(d, id);
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (w == NULL || p == NULL || w->indice >= p->n_widget || p->widget[w->indice].tipo != w->tipo)
        return;
    p->widget[w->indice].dimensione = dim;
    ph_dsk_salva(d);
    ph_dsk_widget_ricostruisci(d);
}

void ph_dsk_widget_rimuovi(PhDesktop *d, unsigned id)
{
    PhDskWidget *w = cerca(d, id);
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (w == NULL || p == NULL || w->indice >= p->n_widget || p->widget[w->indice].tipo != w->tipo)
        return;
    size_t i = w->indice;
    memmove(&p->widget[i], &p->widget[i + 1], (p->n_widget - i - 1) * sizeof p->widget[0]);
    p->n_widget--;
    ph_dsk_salva(d);
    ph_dsk_widget_ricostruisci(d);
}

bool ph_dsk_widget_stile_di(PhDesktop *d, unsigned id, PhCfgWidgetTipo *tipo, unsigned *stile, unsigned *opzione)
{
    PhDskWidget *w = cerca(d, id);
    if (w == NULL)
        return false;
    *tipo = w->tipo;
    *stile = w->stile;
    *opzione = w->opzione;
    return true;
}

void ph_dsk_widget_stile_imposta(PhDesktop *d, unsigned id, unsigned stile, unsigned opzione)
{
    PhDskWidget *w = cerca(d, id);
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (w == NULL || p == NULL || w->indice >= p->n_widget || p->widget[w->indice].tipo != w->tipo)
        return;
    p->widget[w->indice].stile = (int)ph_dsk_stile_valido_di(w->tipo, stile);
    p->widget[w->indice].opzione = (int)(w->tipo == PH_CFG_WIDGET_OROLOGIO ? ph_dsk_citta_valida(opzione) : 0);
    ph_dsk_salva(d);
    ph_dsk_widget_ricostruisci(d);
}

/* Evento dei servizi o della shell per i widget nuovi (interruttori, trasferimenti...). */
void ph_dsk_widget_evento(PhDesktop *d, int ev)
{
    for (size_t i = 0; i < d->widget->len; i++) {
        PhDskWidget *w = g_ptr_array_index(d->widget, i);
        if (ph_dsk_wx_vt(w->tipo) != NULL)
            ph_dsk_wx_evento(w, ev);
    }
}

/* Il telefono si e' collegato: i widget nuovi chiedono i loro dati. */
void ph_dsk_widget_richiedi_nuovi(PhDesktop *d)
{
    for (size_t i = 0; i < d->widget->len; i++) {
        PhDskWidget *w = g_ptr_array_index(d->widget, i);
        if (ph_dsk_wx_vt(w->tipo) != NULL)
            ph_dsk_wx_richiedi(w);
    }
}
