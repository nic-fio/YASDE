/* Widget che leggono il telefono con una sola richiesta periodica: «Prossima sveglia» (capsula) e
 * «Batteria in dettaglio» (tonda), CONTRATTI.md §16 e PIANO.md 8.3. Che cosa mostrare lo decidono
 * le funzioni pure di logica_telefono.c sulla misura VERA del widget; qui ci sono i disegni (nel
 * corpo della scheda), le richieste, gli annullamenti, le scadenze e i clic. */
#include <math.h>
#include <string.h>

#include "interno.h"
#include "logica_telefono.h"

#define SVEGLIA_OGNI_US (5 * 60 * G_USEC_PER_SEC)
#define SVEGLIA_PASSATA_OGNI_US (60 * G_USEC_PER_SEC) /* sveglia suonata: la prossima si chiede prima */
#define BATTERIA_OGNI_US (60 * G_USEC_PER_SEC)

/* ---- Comuni -------------------------------------------------------------------------------------- */
static GtkWidget *etichetta_tel(const char *classe)
{
    GtkWidget *l = gtk_label_new("");
    gtk_widget_add_css_class(l, classe);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(l), 0.5f);
    return l;
}

/* Apre un'app come dal launcher, ma solo se il telefono la conosce: un clic che non sa che cosa
 * aprire non fa nulla (mai un errore a schermo). La pagina la decide il tipo del widget
 * (ph_dsk_widget_pagina: la batteria apre la pagina Batteria, la sveglia nessuna). */
static const char *apri_se_nota(PhDskWidget *w, const char *pacchetto)
{
    if (pacchetto == NULL || pacchetto[0] == '\0' || ph_srv_app_cerca(w->d->srv, pacchetto) == NULL)
        return NULL;
    ph_shell_apri_app_pagina(w->d->sh, pacchetto, ph_dsk_widget_pagina(w->tipo));
    return pacchetto;
}

/* Clic (senza trascinare) o Invio / Spazio sulla scheda. Il gesto di trascinamento della scheda
 * annulla il clic se il puntatore si muove. */
static void clic_aggancia(PhDskWidget *w, GCallback clic, GCallback tasto)
{
    GtkGesture *c = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(c), GDK_BUTTON_PRIMARY);
    g_signal_connect(c, "released", clic, w);
    gtk_widget_add_controller(w->scheda, GTK_EVENT_CONTROLLER(c));
    GtkEventController *k = gtk_event_controller_key_new();
    g_signal_connect(k, "key-pressed", tasto, w);
    gtk_widget_add_controller(w->scheda, k);
}

static bool tasto_attiva(guint tasto)
{
    return tasto == GDK_KEY_Return || tasto == GDK_KEY_KP_Enter || tasto == GDK_KEY_space;
}

/* ---- Sveglia ------------------------------------------------------------------------------------- */
/* Icona: orologio con le due campanelle. */
static const PhShellIcona ICONA_SVEGLIA = {
    "M8 3.2a5 5 0 1 0 0 10 5 5 0 0 0 0-10z M8 5.6V8.2l1.8 1.1 M2.2 4.2 4.4 2.2 M13.8 4.2 11.6 2.2", NULL,
    16, 16, 1.5f, 20, 20, false};

typedef struct {
    PhDskDato stato;
    char frase[192];
    bool presente;
    uint64_t ora_ms;
    char pacchetto[PH_CFG_LUN_APP]; /* app della sveglia ("" = non nota) */
    GCancellable *annulla;
    int64_t t_us;
    int64_t minuti_visti; /* minuti che mancavano all'ultimo disegno (-1 = nessuna sveglia) */
    GtkWidget *tondo, *ora, *assente, *giorno, *fra; /* contenuto, creato una volta in `inizia` */
    gulong misura_id[2];  /* gestori di cambio misura della scheda */
} Sveglia;

static uint64_t adesso_ms(void)
{
    return (uint64_t)(g_get_real_time() / 1000);
}

static int64_t sveglia_chiave(const Sveglia *x, uint64_t adesso)
{
    return x->stato == PH_DSK_DATO_OK && x->presente ? ph_dsk_sve_minuti(x->ora_ms, adesso) : -1;
}

/* Dispone e riempie il contenuto secondo la misura e i dati di adesso. Costa poco e si puo'
 * richiamare a ogni cambio di misura; il timer lo richiama solo quando cambia il minuto. */
static void sveglia_applica(PhDskWidget *w)
{
    Sveglia *x = w->x;
    uint64_t adesso = adesso_ms();
    bool c_e = x->stato == PH_DSK_DATO_OK && x->presente;
    PhDskSveDisp dp = ph_dsk_sve_disposizione(w->larghezza, w->altezza, c_e);
    x->minuti_visti = sveglia_chiave(x, adesso);

    gtk_widget_set_visible(x->tondo, dp.tondo);
    gtk_widget_set_size_request(x->tondo, dp.tondo_lato, dp.tondo_lato);
    char ora[16] = "", giorno[48] = "", fra[96] = "", assente[48] = "", riga_giorno[160] = "";
    ph_dsk_sve_assente_testo(x->stato, x->presente, assente, sizeof assente);
    if (c_e) {
        ph_dsk_sve_ora_testo(x->ora_ms, NULL, ora, sizeof ora);
        ph_dsk_giorno_etichetta(ph_dsk_giorno_locale(NULL, x->ora_ms), ph_dsk_giorno_locale(NULL, adesso), giorno,
                                sizeof giorno);
        ph_dsk_sve_fra(x->ora_ms, adesso, fra, sizeof fra);
    }
    if (dp.unite)
        ph_dsk_sve_giorno_fra(giorno, fra, riga_giorno, sizeof riga_giorno);
    else
        g_strlcpy(riga_giorno, giorno, sizeof riga_giorno);

    /* L'ora grande: larghezza e altezza del corpo scelto dalla logica. */
    bool ora_visibile = c_e && ora[0] != '\0';
    gtk_widget_set_visible(x->ora, ora_visibile);
    if (ora_visibile) {
        gtk_widget_set_size_request(x->ora, (int)ceil(0.62 * 5 * dp.ora_px) + 4, (int)ceil(dp.ora_px * 1.15) + 2);
        ph_dsk_numero_imposta(x->ora, "", ora, "");
    }
    gtk_widget_set_visible(x->assente, !c_e && assente[0] != '\0');
    gtk_label_set_text(GTK_LABEL(x->assente), assente);
    bool giorno_visibile = ora_visibile && dp.giorno && riga_giorno[0] != '\0';
    gtk_widget_set_visible(x->giorno, giorno_visibile);
    gtk_label_set_text(GTK_LABEL(x->giorno), riga_giorno);
    bool fra_visibile = ora_visibile && dp.fra && !dp.unite && fra[0] != '\0';
    gtk_widget_set_visible(x->fra, fra_visibile);
    gtk_label_set_text(GTK_LABEL(x->fra), fra);

    /* Etichetta accessibile: tutto cio' che si vede. */
    const char *nome = ph_dsk_widget_nome(w->tipo);
    char *a11y = c_e ? g_strdup_printf("%s, %s, %s%s%s", nome != NULL ? nome : "", ora, giorno,
                                       fra[0] != '\0' ? ", " : "", fra)
                     : g_strdup_printf("%s%s%s", nome != NULL ? nome : "", assente[0] != '\0' ? ", " : "", assente);
    ph_shell_etichetta(w->scheda, a11y, NULL);
    g_free(a11y);
}

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

/* L'azione del clic; `forza` (solo per le prove) prende il posto del pacchetto. Torna il pacchetto
 * aperto, NULL se non si e' aperto nulla. */
static const char *sveglia_attiva(PhDskWidget *w, const char *forza)
{
    Sveglia *x = w->x;
    return x != NULL ? apri_se_nota(w, forza != NULL ? forza : x->pacchetto) : NULL;
}

static void sveglia_clic(GtkGestureClick *g, int n, double px, double py, gpointer dati)
{
    (void)g;
    (void)n;
    (void)px;
    (void)py;
    sveglia_attiva(dati, NULL);
}

static gboolean sveglia_tasto(GtkEventControllerKey *k, guint tasto, guint codice, GdkModifierType stato, gpointer dati)
{
    (void)k;
    (void)codice;
    (void)stato;
    if (!tasto_attiva(tasto))
        return FALSE;
    sveglia_clic(NULL, 1, 0, 0, dati);
    return TRUE;
}

/* La misura della scheda e' cambiata (maniglia): il contenuto si rifa' alla misura nuova. */
static void sveglia_misura(GObject *o, GParamSpec *p, gpointer dati)
{
    (void)o;
    (void)p;
    PhDskWidget *w = dati;
    if (w->x != NULL && w->d->vivo)
        sveglia_applica(w);
}

static void sveglia_inizia(PhDskWidget *w)
{
    Sveglia *x = g_new0(Sveglia, 1);
    x->stato = PH_DSK_DATO_ATTESA;
    x->minuti_visti = -1;
    w->x = x;

    /* Contenuto: tondo ambra con l'icona, a destra l'ora grande e sotto giorno e tempo che manca. */
    GtkWidget *riga = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_valign(riga, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(riga, TRUE);
    x->tondo = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(x->tondo, "ph-desktop-tel-tondo");
    gtk_widget_add_css_class(x->tondo, "ph-desktop-tinta-ambra");
    gtk_widget_set_valign(x->tondo, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(x->tondo, GTK_ALIGN_CENTER);
    /* Mai espandersi: un figlio che si espande farebbe allungare il tondo in un ovale. */
    gtk_widget_set_hexpand(x->tondo, FALSE);
    gtk_widget_set_vexpand(x->tondo, FALSE);
    GtkWidget *icona = ph_shell_icona_nuova(&ICONA_SVEGLIA);
    /* L'icona si espande dentro il tondo per stare al centro (il tondo, con hexpand esplicito FALSE, no). */
    gtk_widget_set_hexpand(icona, TRUE);
    gtk_widget_set_halign(icona, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icona, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(x->tondo), icona);
    GtkWidget *colonna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(colonna, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(colonna, TRUE);
    x->ora = ph_dsk_numero_nuovo();
    gtk_widget_set_hexpand(x->ora, FALSE);
    gtk_widget_set_halign(x->ora, GTK_ALIGN_START);
    x->assente = etichetta_tel("ph-desktop-tel-assente");
    gtk_label_set_xalign(GTK_LABEL(x->assente), 0.0f);
    x->giorno = etichetta_tel("ph-desktop-tel-sotto");
    gtk_label_set_xalign(GTK_LABEL(x->giorno), 0.0f);
    x->fra = etichetta_tel("ph-desktop-tel-sotto");
    gtk_label_set_xalign(GTK_LABEL(x->fra), 0.0f);
    gtk_box_append(GTK_BOX(colonna), x->ora);
    gtk_box_append(GTK_BOX(colonna), x->assente);
    gtk_box_append(GTK_BOX(colonna), x->giorno);
    gtk_box_append(GTK_BOX(colonna), x->fra);
    gtk_box_append(GTK_BOX(riga), x->tondo);
    gtk_box_append(GTK_BOX(riga), colonna);
    gtk_widget_set_vexpand(w->corpo, TRUE);
    gtk_widget_set_valign(w->corpo, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(w->corpo), riga);
    gtk_widget_set_visible(w->corpo, TRUE);

    clic_aggancia(w, G_CALLBACK(sveglia_clic), G_CALLBACK(sveglia_tasto));
    x->misura_id[0] = g_signal_connect(w->scheda, "notify::width-request", G_CALLBACK(sveglia_misura), w);
    x->misura_id[1] = g_signal_connect(w->scheda, "notify::height-request", G_CALLBACK(sveglia_misura), w);
}

static void sveglia_ferma(PhDskWidget *w)
{
    Sveglia *x = w->x;
    for (int i = 0; w->scheda != NULL && i < 2; i++)
        if (g_signal_handler_is_connected(w->scheda, x->misura_id[i]))
            g_signal_handler_disconnect(w->scheda, x->misura_id[i]);
    ph_dsk_wx_annulla(&x->annulla);
    g_free(x);
    w->x = NULL;
}

static void sveglia_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    (void)v; /* niente etichette della scheda: il contenuto e' nel corpo */
    sveglia_applica(w);
}

static void sveglia_tick(PhDskWidget *w, int64_t ora_us)
{
    Sveglia *x = w->x;
    /* «fra 7 ore» scorre con il tempo: si ridisegna solo quando cambia il minuto (il timer del
     * desktop scatta al cambio di minuto: nessun richiamo a ogni fotogramma). */
    if (sveglia_chiave(x, adesso_ms()) != x->minuti_visti)
        sveglia_applica(w);
    int64_t ogni = x->presente && ph_dsk_sve_passata(x->ora_ms, adesso_ms()) ? SVEGLIA_PASSATA_OGNI_US : SVEGLIA_OGNI_US;
    if (ora_us - x->t_us >= ogni && ph_dsk_wx_puo_chiedere(w))
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
    GtkWidget *anello, *cornice, *testo_stato, *gettoni, *gettone[PH_DSK_BAT_G_N]; /* creati una volta in `inizia` */
    gulong misura_id[2];  /* gestori di cambio misura della scheda */
} Batteria;

/* Dispone e riempie il contenuto secondo la misura e i dati di adesso. */
static void batteria_applica(PhDskWidget *w)
{
    Batteria *x = w->x;
    bool ok = x->stato == PH_DSK_DATO_OK;
    bool noti[PH_DSK_BAT_G_N];
    ph_dsk_bat_gettoni_noti(ok ? &x->dati : NULL, noti);
    PhDskBatDisp dp = ph_dsk_bat_disposizione(w->larghezza, w->altezza, noti);

    /* La cornice da' all'anello il lato esatto: il naturale dell'anello (96 px) altrimenti vincerebbe. */
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(x->cornice), dp.lato_anello);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(x->cornice), dp.lato_anello);
    int livello = x->dati.livello < 0 ? 0 : (x->dati.livello > 100 ? 100 : x->dati.livello);
    bool in_carica = x->dati.stato == PH_SRV_BAT_IN_CARICA;
    /* Non noto (in attesa, non disponibile, errore): anello vuoto con «--», mai un numero inventato. */
    ph_dsk_anello_imposta(x->anello, ok ? (double)livello : NAN, NULL,
                          ok ? ph_dsk_bat_accento(livello, in_carica) : PH_DSK_ACC_VERDE);

    char stato[64];
    ph_dsk_bat_stato_testo(x->stato, ok ? &x->dati : NULL, stato, sizeof stato);
    gtk_widget_set_visible(x->testo_stato, dp.stato && stato[0] != '\0');
    gtk_label_set_text(GTK_LABEL(x->testo_stato), stato);
    gtk_label_set_max_width_chars(GTK_LABEL(x->testo_stato), dp.caratteri_max); /* dentro il cerchio */
    bool qualcuno = false;
    for (int i = 0; i < PH_DSK_BAT_G_N; i++) {
        char t[96];
        ph_dsk_bat_gettone_testo(i, ok ? &x->dati : NULL, t, sizeof t);
        bool vedi = dp.gettone[i] && t[0] != '\0';
        gtk_widget_set_visible(x->gettone[i], vedi);
        gtk_label_set_text(GTK_LABEL(x->gettone[i]), t);
        gtk_label_set_max_width_chars(GTK_LABEL(x->gettone[i]), dp.caratteri_max);
        qualcuno = qualcuno || vedi;
    }
    gtk_widget_set_visible(x->gettoni, qualcuno);

    const char *nome = ph_dsk_widget_nome(w->tipo);
    char *a11y = ok ? g_strdup_printf("%s, %d%%%s%s", nome != NULL ? nome : "", livello, stato[0] != '\0' ? ", " : "", stato)
                    : g_strdup_printf("%s%s%s", nome != NULL ? nome : "", stato[0] != '\0' ? ", " : "", stato);
    ph_shell_etichetta(w->scheda, a11y, NULL);
    g_free(a11y);
}

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

/* L'app Impostazioni, sulla pagina Batteria; `forza` come per la sveglia. */
static const char *batteria_attiva(PhDskWidget *w, const char *forza)
{
    return w->x != NULL ? apri_se_nota(w, forza != NULL ? forza : PH_DSK_BAT_PACCHETTO_IMPOSTAZIONI) : NULL;
}

static void batteria_clic(GtkGestureClick *g, int n, double px, double py, gpointer dati)
{
    (void)g;
    (void)n;
    (void)px;
    (void)py;
    batteria_attiva(dati, NULL);
}

/* Per la prova d'insieme (demo_hook.c): fa lo stesso del clic su una batteria o una sveglia. */
const char *ph_dsk_wx_telefono_clic_prova(PhDskWidget *w, const char *forza)
{
    if (w->tipo == PH_CFG_WIDGET_BATTERIA)
        return batteria_attiva(w, forza);
    if (w->tipo == PH_CFG_WIDGET_SVEGLIA)
        return sveglia_attiva(w, forza);
    return NULL;
}

static gboolean batteria_tasto(GtkEventControllerKey *k, guint tasto, guint codice, GdkModifierType stato, gpointer dati)
{
    (void)k;
    (void)codice;
    (void)stato;
    if (!tasto_attiva(tasto))
        return FALSE;
    batteria_clic(NULL, 1, 0, 0, dati);
    return TRUE;
}

static void batteria_misura(GObject *o, GParamSpec *p, gpointer dati)
{
    (void)o;
    (void)p;
    PhDskWidget *w = dati;
    if (w->x != NULL && w->d->vivo)
        batteria_applica(w);
}

static void batteria_inizia(PhDskWidget *w)
{
    Batteria *x = g_new0(Batteria, 1);
    x->stato = PH_DSK_DATO_ATTESA;
    w->x = x;

    /* Contenuto, al centro del tondo: anello con la percentuale, sotto lo stato, poi i gettoni. */
    GtkWidget *colonna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(colonna, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(colonna, GTK_ALIGN_CENTER);
    x->anello = ph_dsk_anello_nuovo();
    gtk_widget_set_hexpand(x->anello, TRUE);
    gtk_widget_set_vexpand(x->anello, TRUE);
    /* Cornice di misura esatta, che ritaglia e non propaga il naturale del figlio (come in widget.c). */
    x->cornice = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(x->cornice), GTK_POLICY_EXTERNAL, GTK_POLICY_EXTERNAL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(x->cornice), x->anello);
    gtk_widget_set_halign(x->cornice, GTK_ALIGN_CENTER);
    x->testo_stato = etichetta_tel("ph-desktop-tel-stato");
    x->gettoni = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_halign(x->gettoni, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(x->gettoni, 4);
    for (int i = 0; i < PH_DSK_BAT_G_N; i++) {
        x->gettone[i] = etichetta_tel("ph-desktop-tel-gettone");
        gtk_widget_set_halign(x->gettone[i], GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(x->gettoni), x->gettone[i]);
    }
    gtk_box_append(GTK_BOX(colonna), x->cornice);
    gtk_box_append(GTK_BOX(colonna), x->testo_stato);
    gtk_box_append(GTK_BOX(colonna), x->gettoni);
    gtk_widget_set_vexpand(w->corpo, TRUE);
    gtk_widget_set_valign(w->corpo, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(w->corpo, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(w->corpo), colonna);
    gtk_widget_set_visible(w->corpo, TRUE);

    clic_aggancia(w, G_CALLBACK(batteria_clic), G_CALLBACK(batteria_tasto));
    x->misura_id[0] = g_signal_connect(w->scheda, "notify::width-request", G_CALLBACK(batteria_misura), w);
    x->misura_id[1] = g_signal_connect(w->scheda, "notify::height-request", G_CALLBACK(batteria_misura), w);
}

static void batteria_ferma(PhDskWidget *w)
{
    Batteria *x = w->x;
    for (int i = 0; w->scheda != NULL && i < 2; i++)
        if (g_signal_handler_is_connected(w->scheda, x->misura_id[i]))
            g_signal_handler_disconnect(w->scheda, x->misura_id[i]);
    ph_dsk_wx_annulla(&x->annulla);
    g_free(x);
    w->x = NULL;
}

static void batteria_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    (void)v; /* niente etichette della scheda: il contenuto e' nel corpo */
    batteria_applica(w);
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
