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
/* Riempimento della barra: proporzionale alla larghezza di adesso (dentro l'imbottitura della
 * forma). Si rifa' a ogni cambio di misura, non solo quando arrivano dati. */
static void barra_applica(PhDskWidget *w)
{
    gtk_widget_set_visible(w->barra, w->percento >= 0);
    if (w->percento < 0)
        return;
    int pad_o, pad_v;
    ph_dsk_imbottitura(w->forma, &pad_o, &pad_v);
    int utile = w->larghezza - 2 * pad_o;
    if (utile < 0)
        utile = 0;
    gtk_widget_set_size_request(w->pieno, utile * w->percento / 100, 6);
    gtk_widget_set_visible(w->pieno, w->percento > 0);
}

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
    w->percento = v->percento;
    barra_applica(w);
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

/* L'«adesso» dei widget (quello congelato da ph_dsk_orologio_ora_fissa, se c'e'): serve alle prove
 * del demo, per ragionare sullo stesso istante dei widget. */
uint64_t ph_dsk_widget_adesso_ms(int *scarto_s)
{
    uint64_t ms;
    int scarto;
    char ora[16], data[96];
    adesso(&ms, &scarto, ora, sizeof ora, data, sizeof data);
    if (scarto_s != NULL)
        *scarto_s = scarto;
    return ms;
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

/* ---- Memoria, notifiche e calendario del giorno (passo 2b, agente A) ---------------------------
 * Contenuto per misura vera: ogni funzione chiede alla logica pura (logica_vecchi.h) che cosa ci
 * sta in larghezza x altezza e costruisce solo quello dentro w->corpo. Si rifanno a ogni aggiornamento
 * dei dati e a ogni cambio di misura. Valori assenti dal telefono: mai inventati.
 * Riservatezza: titoli e testi vivono solo nelle etichette a schermo. */
static void vista_vuota(PhDskVista *v)
{
    memset(v, 0, sizeof *v);
    v->percento = -1;
}

/* Svuota il corpo e lo mostra (o lo nasconde). */
static void corpo_prepara(PhDskWidget *w, bool visibile)
{
    ph_dsk_wx_svuota(w->corpo);
    gtk_widget_set_visible(w->corpo, visibile);
}

static GtkWidget *etichetta_corpo(const char *testo, const char *classe, bool ellissi)
{
    char *ok = g_utf8_make_valid(testo != NULL ? testo : "", -1); /* dato del telefono */
    GtkWidget *l = gtk_label_new(ok);
    g_free(ok);
    gtk_widget_add_css_class(l, classe);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    if (ellissi) {
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(l), 1); /* nessuna larghezza minima */
    }
    return l;
}

static int scarto_locale_s(void)
{
    GDateTime *dt = adesso_dt();
    int s = (int)(g_date_time_get_utc_offset(dt) / G_USEC_PER_SEC);
    g_date_time_unref(dt);
    return s;
}

/* L'app Impostazioni, se il telefono ce l'ha tra le sue app; altrimenti NULL (nessun clic). */
static const char *g_impostazioni_prova = NULL;

/* Solo per le prove del demo: un altro pacchetto al posto di com.android.settings (il telefono finto
 * non ha quell'app). NULL toglie la sostituzione. La stringa deve vivere piu' dei widget. */
void ph_dsk_impostazioni_pacchetto_prova(const char *pacchetto)
{
    g_impostazioni_prova = pacchetto;
}

static const char *impostazioni_nota(PhDesktop *d)
{
    const char *pk = g_impostazioni_prova != NULL ? g_impostazioni_prova : ph_dsk_pacchetto_impostazioni();
    return d->vivo && ph_srv_app_cerca(d->srv, pk) != NULL ? pk : NULL;
}

/* Memoria: clic sul contenuto = app Impostazioni. */
static void memoria_clic(GtkGestureClick *g, int n, double x, double y, gpointer dati)
{
    (void)g;
    (void)n;
    (void)x;
    (void)y;
    PhDskWidget *w = dati;
    const char *pk = w->scheda != NULL ? impostazioni_nota(w->d) : NULL;
    if (pk != NULL)
        ph_shell_apri_app(w->d->sh, pk);
}

static void memoria_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    PhDesktop *d = w->d;
    vista_vuota(v);
    if (!d->memoria_nota || d->memoria_totale == 0) {
        corpo_prepara(w, false); /* i dati non ci sono: solo il titolo, nessun numero */
        snprintf(v->titolo, sizeof v->titolo, "%s", ph_t("Memoria del telefono"));
        return;
    }
    corpo_prepara(w, true);
    gtk_widget_set_vexpand(w->corpo, TRUE);
    gtk_widget_set_cursor_from_name(w->corpo, impostazioni_nota(d) != NULL ? "pointer" : NULL);
    uint64_t libera = d->memoria_libera > d->memoria_totale ? d->memoria_totale : d->memoria_libera;
    uint64_t usata = d->memoria_totale - libera;

    PhDskMemFetta f[PH_DSK_MEM_N];
    size_t nf = ph_dsk_memoria_fette(d->dettaglio_noto ? &d->dettaglio : NULL, usata, f);
    size_t n_voci = nf; /* voci della legenda: 0 se il telefono non ha dato nessuna categoria */

    /* Prima la barra: la sua legenda dice da sola quante colonne usera' (voce piu' larga vera), e da
     * questo dipende quanta altezza serve. */
    PhDskFetta fette[PH_DSK_MEM_N];
    char valori[PH_DSK_MEM_N][48];
    if (nf == 0) { /* senza categorie lette la barra mostra solo quanto e' usato, senza legenda */
        fette[0] = (PhDskFetta){"", "", (int64_t)(usata > (uint64_t)INT64_MAX ? INT64_MAX : usata), PH_DSK_ACC_TEMA};
        nf = 1;
    } else {
        for (size_t i = 0; i < nf; i++) {
            char g[32];
            ph_dsk_formato_gb(f[i].byte, g, sizeof g);
            ph_lingua_componi(valori[i], sizeof valori[i], ph_t("{numero} GB"), "numero", g, (const char *)NULL);
            const char *nome = f[i].categoria == PH_DSK_MEM_IMMAGINI ? ph_t("Immagini")
                               : f[i].categoria == PH_DSK_MEM_VIDEO  ? ph_t("Video")
                               : f[i].categoria == PH_DSK_MEM_AUDIO  ? ph_t("Audio")
                               : f[i].categoria == PH_DSK_MEM_APP    ? ph_t("App")
                                                                     : ph_t("Altro");
            fette[i] = (PhDskFetta){nome, valori[i], (int64_t)(f[i].byte > (uint64_t)INT64_MAX ? INT64_MAX : f[i].byte),
                                    (PhDskAccento)ph_dsk_memoria_accento(f[i].categoria)};
        }
    }
    GtkWidget *fe = ph_dsk_fette_nuove();
    g_object_ref_sink(fe); /* si attacca solo se la barra ci sta */
    ph_dsk_fette_imposta(fe, fette, nf,
                         (int64_t)(d->memoria_totale > (uint64_t)INT64_MAX ? INT64_MAX : d->memoria_totale));
    int ul, ua;
    ph_dsk_vecchi_utile(w->larghezza, w->altezza, &ul, &ua);
    (void)ua;
    PhDskMemoriaDisp disp = ph_dsk_memoria_disp(w->larghezza, w->altezza, n_voci, ph_dsk_fette_legenda_colonne(fe, ul));

    char n1[32], n2[32], q1[48], q2[48], nota[96];
    ph_dsk_formato_gb(libera, n1, sizeof n1);
    ph_dsk_formato_gb(d->memoria_totale, n2, sizeof n2);
    ph_lingua_componi(q1, sizeof q1, ph_t("{numero} GB"), "numero", n1, (const char *)NULL);
    ph_lingua_componi(q2, sizeof q2, ph_t("{numero} GB"), "numero", n2, (const char *)NULL);
    ph_lingua_componi(nota, sizeof nota, ph_t("libero su {quantita}"), "quantita", q2, (const char *)NULL);
    /* Il numero e' il dato principale: grande, alto quanto la logica dice (senza vuoti verso la barra).
     * La dicitura sta accanto se c'e' larghezza, altrimenti sotto, a capo se serve: si legge per intero. */
    GtkWidget *num = ph_dsk_numero_nuovo();
    gtk_widget_set_size_request(num, -1, disp.numero_alto);
    ph_dsk_numero_imposta(num, ph_t("Memoria del telefono"), q1, disp.nota_accanto ? nota : "");
    gtk_box_append(GTK_BOX(w->corpo), num);
    if (!disp.nota_accanto) {
        GtkWidget *l = etichetta_corpo(nota, "ph-desktop-w-sotto-nom", false);
        gtk_label_set_wrap(GTK_LABEL(l), TRUE);
        gtk_box_append(GTK_BOX(w->corpo), l);
    }
    if (disp.barra) {
        ph_dsk_fette_legenda_visibile(fe, disp.legenda);
        gtk_widget_set_margin_top(fe, 10);
        gtk_widget_set_valign(fe, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(w->corpo), fe);
    }
    g_object_unref(fe);
}

/* Notifiche: dati del clic di una riga. Vivono quanto la riga (g_object_set_data_full). */
typedef struct {
    PhDskWidget *w;
    char pacchetto[]; /* app che ha mandato la notifica */
} ClicApp;

static void notifica_clic(void *dati)
{
    ClicApp *c = dati;
    if (c->w->scheda != NULL && c->w->d->vivo)
        ph_shell_apri_app(c->w->d->sh, c->pacchetto);
}

static void notifiche_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    PhDesktop *d = w->d;
    vista_vuota(v);
    snprintf(v->titolo, sizeof v->titolo, "%s", ph_t("Notifiche recenti"));
    corpo_prepara(w, true);
    size_t n = 0;
    const PhSrvNotifica *tutte = ph_srv_notifiche(d->srv, &n, NULL);
    size_t max = ph_dsk_notifiche_righe_max(w->larghezza, w->altezza), k = 0;
    int scarto = scarto_locale_s();
    GtkWidget *elenco = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    for (size_t i = 0; i < n && k < max; i++) {
        if (ph_shell_notifica_nascosta(d->sh, tutte[i].chiave, tutte[i].ora_ms))
            continue; /* contano solo quelle non nascoste */
        const char *pk = tutte[i].pacchetto != NULL ? tutte[i].pacchetto : "";
        const char *titolo = tutte[i].titolo != NULL ? tutte[i].titolo : "";
        const PhSrvApp *app = pk[0] != '\0' ? ph_srv_app_cerca(d->srv, pk) : NULL;
        const char *nome_app = app != NULL && app->nome != NULL ? app->nome : "";
        ClicApp *c = NULL;
        if (pk[0] != '\0') { /* senza pacchetto il clic non fa nulla e la riga non e' un pulsante */
            c = g_malloc(sizeof *c + strlen(pk) + 1);
            c->w = w;
            strcpy(c->pacchetto, pk);
        }
        GtkWidget *riga = ph_dsk_riga_nuova(c != NULL ? notifica_clic : NULL, c);
        if (c != NULL)
            g_object_set_data_full(G_OBJECT(riga), "ph-desktop-clic", c, g_free);
        char sigla[16], ora[16] = "";
        ph_dsk_sigla(nome_app[0] != '\0' ? nome_app : titolo, sigla, sizeof sigla);
        if (tutte[i].ora_ms != 0)
            ph_dsk_ora_evento(tutte[i].ora_ms, scarto, ora, sizeof ora);
        ph_dsk_riga_imposta(riga, sigla, (PhDskAccento)ph_dsk_accento_pacchetto(pk), false, titolo,
                            tutte[i].testo != NULL ? tutte[i].testo : "", ora);
        GdkTexture *tx = pk[0] != '\0' ? ph_srv_icona(d->srv, pk) : NULL; /* icona vera; senza, la sigla */
        if (tx != NULL) {
            GtkWidget *im = gtk_image_new_from_paintable(GDK_PAINTABLE(tx));
            gtk_image_set_pixel_size(GTK_IMAGE(im), 32);
            ph_dsk_riga_icona(riga, im);
        }
        gtk_box_append(GTK_BOX(elenco), riga);
        k++;
    }
    if (k == 0) {
        g_object_ref_sink(elenco); /* mai attaccato: si libera subito */
        g_object_unref(elenco);
        gtk_box_append(GTK_BOX(w->corpo), etichetta_corpo(ph_t("Nessuna notifica"), "ph-desktop-w-sotto", true));
        return;
    }
    gtk_box_append(GTK_BOX(w->corpo), elenco);
}

/* Calendario del giorno: un impegno e' una riga con la barretta colorata, il titolo (con «ora»
 * se e' in corso) e l'orario. Con l'app del calendario nota e' un pulsante, altrimenti solo da
 * guardare (il trascinamento del widget passa). */
static void impegno_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    PhDskWidget *w = dati;
    if (w->scheda != NULL && w->d->vivo && w->d->app_calendario != NULL)
        ph_shell_apri_app(w->d->sh, w->d->app_calendario);
}

static GtkWidget *impegno_nuovo(PhDskWidget *w, const char *titolo, const char *orario, int acc, bool in_corso,
                                bool cliccabile)
{
    GtkWidget *dentro = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 9);
    GtkWidget *barra = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    char classe[48];
    gtk_widget_add_css_class(barra, "ph-desktop-giorno-impegno-barra");
    snprintf(classe, sizeof classe, "ph-desktop-tinta-%s", ph_dsk_accento_nome(acc));
    gtk_widget_add_css_class(barra, classe);
    gtk_box_append(GTK_BOX(dentro), barra);
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(col, TRUE);
    GtkWidget *r1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *t = etichetta_corpo(titolo, "ph-desktop-giorno-impegno-titolo", true);
    gtk_widget_set_hexpand(t, TRUE);
    gtk_box_append(GTK_BOX(r1), t);
    if (in_corso) {
        char *ora = g_utf8_strup(ph_t("Ora"), -1);
        gtk_box_append(GTK_BOX(r1), etichetta_corpo(ora, "ph-desktop-giorno-impegno-ora", false));
        g_free(ora);
    }
    gtk_box_append(GTK_BOX(col), r1);
    if (orario[0] != '\0')
        gtk_box_append(GTK_BOX(col), etichetta_corpo(orario, "ph-desktop-giorno-impegno-orario", true));
    gtk_box_append(GTK_BOX(dentro), col);
    GtkWidget *r;
    if (cliccabile) {
        r = gtk_button_new();
        gtk_button_set_child(GTK_BUTTON(r), dentro);
        g_signal_connect(r, "clicked", G_CALLBACK(impegno_clic), w);
        char *tv = g_utf8_make_valid(titolo, -1);
        char *et = orario[0] != '\0' ? g_strjoin(", ", tv, orario, NULL) : g_strdup(tv);
        ph_shell_etichetta(r, et, NULL);
        g_free(et);
        g_free(tv);
    } else {
        r = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_append(GTK_BOX(r), dentro);
        gtk_widget_set_can_target(r, FALSE);
    }
    gtk_widget_add_css_class(r, "ph-desktop-giorno-impegno");
    gtk_widget_set_hexpand(r, TRUE);
    return r;
}

static void calendario_giorno_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    PhDesktop *d = w->d;
    vista_vuota(v);
    uint64_t ms;
    int scarto;
    char ora[16], data[96];
    adesso(&ms, &scarto, ora, sizeof ora, data, sizeof data);
    PhDskCalDisp disp = ph_dsk_calendario_disp(w->larghezza, w->altezza);
    corpo_prepara(w, true);
    if (!disp.colonna_giorno)
        g_strlcpy(v->titolo, data, sizeof v->titolo); /* la data lunga in una riga in alto */

    GtkWidget *riga = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    if (disp.colonna_giorno) {
        GDateTime *dt = adesso_dt();
        char num[16], mese[48];
        snprintf(num, sizeof num, "%d", g_date_time_get_day_of_month(dt));
        ph_dsk_maiuscola_prima(ph_dsk_nome_mese(g_date_time_get_month(dt)), mese, sizeof mese);
        GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *nu = ph_dsk_numero_nuovo();
        gtk_widget_set_size_request(nu, 64, 52);
        ph_dsk_numero_imposta(nu, ph_dsk_giorno_breve(g_date_time_get_day_of_week(dt) - 1), num, "");
        gtk_box_append(GTK_BOX(col), nu);
        gtk_box_append(GTK_BOX(col), etichetta_corpo(mese, "ph-desktop-w-sotto-nom", true));
        /* Larghezza FISSA di 64 px. gtk_widget_set_size_request fissa solo il minimo: il numero ha una
         * larghezza naturale di 120 e la colonna la prenderebbe (halign START) lasciando 56 px vuoti tra
         * il giorno e gli impegni. Dentro una finestra che scorre (invisibile, senza clic) la larghezza
         * naturale e' quella minima del contenuto: 64. */
        GtkWidget *fisso = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(fisso), GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
        gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(fisso), 64);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(fisso), col);
        gtk_widget_set_hexpand(fisso, FALSE);
        gtk_widget_set_halign(fisso, GTK_ALIGN_START);
        gtk_widget_set_valign(fisso, GTK_ALIGN_START);
        gtk_widget_set_can_target(fisso, FALSE); /* solo da guardare: il trascinamento del widget passa */
        gtk_widget_add_css_class(fisso, "ph-desktop-giorno-colonna");
        gtk_box_append(GTK_BOX(riga), fisso);
        g_date_time_unref(dt);
    }
    GtkWidget *impegni = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(impegni, TRUE);
    gtk_widget_set_halign(impegni, GTK_ALIGN_FILL); /* tutta la larghezza che resta, subito dopo il giorno */
    gtk_widget_set_valign(impegni, GTK_ALIGN_START);
    gtk_widget_add_css_class(impegni, "ph-desktop-giorno-impegni");
    if (d->eventi_noti) { /* senza dati non si scrive niente: ne' «nessun impegno» ne' impegni finti */
        PhDskEvento ev[PH_SRV_CALENDARIO_MAX];
        size_t n = d->n_eventi < PH_SRV_CALENDARIO_MAX ? d->n_eventi : PH_SRV_CALENDARIO_MAX;
        for (size_t i = 0; i < n; i++) {
            ev[i].titolo = d->eventi[i].titolo;
            ev[i].inizio_ms = d->eventi[i].inizio_ms;
            ev[i].fine_ms = d->eventi[i].fine_ms;
            ev[i].tutto_il_giorno = d->eventi[i].tutto_il_giorno;
        }
        size_t idx[PH_SRV_CALENDARIO_MAX];
        size_t max = disp.max_impegni < PH_SRV_CALENDARIO_MAX ? disp.max_impegni : PH_SRV_CALENDARIO_MAX;
        size_t k = ph_dsk_eventi_di_oggi(ev, n, ms, scarto, max, idx);
        if (k == 0) {
            /* Lo stato vuoto si legge per intero: mai con i puntini, va a capo nello spazio che c'e'. */
            GtkWidget *vuoto = etichetta_corpo(ph_t("Nessun impegno"), "ph-desktop-w-sotto", false);
            gtk_label_set_wrap(GTK_LABEL(vuoto), TRUE);
            gtk_label_set_wrap_mode(GTK_LABEL(vuoto), PANGO_WRAP_WORD_CHAR);
            gtk_widget_set_hexpand(vuoto, TRUE);
            gtk_box_append(GTK_BOX(impegni), vuoto);
        }
        for (size_t i = 0; i < k; i++) {
            char orario[48];
            ph_dsk_evento_orario(&ev[idx[i]], NULL, orario, sizeof orario);
            gtk_box_append(GTK_BOX(impegni),
                           impegno_nuovo(w, ev[idx[i]].titolo != NULL ? ev[idx[i]].titolo : "", orario,
                                         ph_dsk_accento_impegno(i), ph_dsk_impegno_in_corso(&ev[idx[i]], ms),
                                         d->app_calendario != NULL));
        }
    }
    gtk_box_append(GTK_BOX(riga), impegni);
    gtk_box_append(GTK_BOX(w->corpo), riga);
}

static void widget_aggiorna(PhDskWidget *w)
{
    PhDesktop *d = w->d;
    PhDskVista v;
    switch (w->tipo) {
    case PH_CFG_WIDGET_OROLOGIO:
        orologio_aggiorna(w, &v);
        break;
    case PH_CFG_WIDGET_CALENDARIO:
        calendario_giorno_aggiorna(w, &v);
        break;
    case PH_CFG_WIDGET_STATO: {
        PhSrvTelefono t;
        PhSrvCollegamento c;
        ph_srv_telefono(d->srv, &t);
        ph_srv_collegamento(d->srv, &c);
        /* Le righe di dettaglio dipendono dall'altezza vera, non dal livello (logica_vecchi.h). */
        ph_dsk_vista_stato(PH_CFG_WIDGET_GRANDE, &t, &c, &v);
        size_t max = ph_dsk_stato_righe(w->larghezza, w->altezza);
        if (v.n_righe > max)
            v.n_righe = max;
        break;
    }
    case PH_CFG_WIDGET_NOTIFICHE:
        notifiche_aggiorna(w, &v);
        break;
    case PH_CFG_WIDGET_MEMORIA:
        memoria_aggiorna(w, &v);
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
    int x = d->area_l - w->destra - w->larghezza;
    ph_shell_libero_sposta(d->libero, w->scheda, x, w->alto, w->larghezza, w->altezza);
}

/* Misura, forma e maniglia dopo ogni cambio di misura. Il vetro prende il raggio della
 * forma: per tondo e capsula segue la misura, quindi si registra di nuovo (la sfocatura
 * si ritaglia con il raggio dato alla registrazione). Mattonella: il raggio non cambia. */
static void geometria_applica(PhDskWidget *w)
{
    PhDesktop *d = w->d;
    gtk_widget_set_size_request(w->scheda, w->larghezza, w->altezza);
    int raggio = ph_dsk_raggio(w->forma, w->larghezza, w->altezza);
    if (raggio != w->raggio) {
        ph_shell_vetro_togli(d->sh, w->scheda);
        ph_shell_vetro_registra(d->sh, w->scheda, raggio, PH_VETRO_SCURO);
        w->raggio = raggio;
    }
    int pad_o, pad_v;
    ph_dsk_imbottitura(w->forma, &pad_o, &pad_v);
    gtk_widget_set_margin_end(w->maniglia, ph_dsk_maniglia_margine(raggio, pad_o));
    gtk_widget_set_margin_bottom(w->maniglia, ph_dsk_maniglia_margine(raggio, pad_v));
}

void ph_dsk_widget_riposiziona(PhDesktop *d)
{
    for (size_t i = 0; i < d->widget->len; i++)
        widget_colloca(g_ptr_array_index(d->widget, i));
}

/* Scrive nel profilo il posto con cui i widget sono mostrati ora. Serve prima di ogni modifica fatta
 * dall'utente (spostare, ridimensionare, aggiungere, togliere, cambiare stile): i widget
 * predefiniti di un profilo nuovo si mostrano piu' distanziati di come sono scritti
 * (ph_dsk_predefiniti_alti) e, cambiato il profilo, quella correzione a schermo non si
 * riconoscerebbe piu' e gli altri tornerebbero a toccarsi. Con i valori gia' uguali non cambia
 * nulla; il chiamante salva subito dopo. */
static void posizioni_fissa(PhDesktop *d)
{
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p == NULL)
        return;
    for (size_t i = 0; i < d->widget->len; i++) {
        const PhDskWidget *w = g_ptr_array_index(d->widget, i);
        if (w->indice < p->n_widget && p->widget[w->indice].tipo == w->tipo) {
            p->widget[w->indice].destra = w->destra;
            p->widget[w->indice].alto = w->alto;
        }
    }
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
    posizioni_fissa(d); /* anche gli altri: vedi sopra (w ha gia' il posto nuovo) */
    ph_dsk_salva(d);
}

/* ---- Ridimensionamento (maniglia in basso a destra) ------------------------------------ */
/* Applica a schermo una misura calcolata dalla logica; false se non cambia nulla. */
static bool ridim_calcola(PhDskWidget *w, PhDskMisura base, int destra_base, double dx, double dy)
{
    PhDesktop *d = w->d;
    PhDskMisura n;
    int destra;
    ph_dsk_ridimensiona(w->tipo, w->stile, base, destra_base, w->alto, dx, dy, d->area_a - PH_DSK_BARRA,
                        &n, &destra);
    if (n.larghezza == w->larghezza && n.altezza == w->altezza && destra == w->destra)
        return false;
    w->larghezza = n.larghezza;
    w->altezza = n.altezza;
    w->destra = destra;
    geometria_applica(w);
    barra_applica(w);
    widget_colloca(w);
    /* agente A: questi contenuti dipendono dalla misura vera, non solo dal livello: si rifanno. */
    if (w->tipo == PH_CFG_WIDGET_MEMORIA || w->tipo == PH_CFG_WIDGET_NOTIFICHE ||
        w->tipo == PH_CFG_WIDGET_CALENDARIO || w->tipo == PH_CFG_WIDGET_STATO)
        widget_aggiorna(w);
    return true;
}

/* Scrive misura, posto e livello nel profilo. Se il livello di contenuto cambia, i widget si
 * ricostruiscono (il contenuto dipende dal livello): `w` non va piu' toccato. true = ricostruiti. */
static bool ridim_salva(PhDskWidget *w)
{
    PhDesktop *d = w->d;
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p == NULL || w->indice >= p->n_widget || p->widget[w->indice].tipo != w->tipo)
        return false;
    posizioni_fissa(d);
    PhCfgWidget *c = &p->widget[w->indice];
    PhCfgWidgetDim livello = ph_dsk_livello(w->tipo, w->stile, (PhDskMisura){w->larghezza, w->altezza});
    c->larghezza = w->larghezza;
    c->altezza = w->altezza;
    c->destra = w->destra;
    c->dimensione = livello; /* il campo resta scritto: lo leggono anche le versioni senza misure libere */
    ph_dsk_salva(d);
    if (livello == w->dim)
        return false;
    ph_dsk_widget_ricostruisci(d);
    return true;
}

static void ridim_inizio(GtkGestureDrag *g, double x, double y, gpointer dati)
{
    PhDskWidget *w = dati;
    /* La maniglia e' sua: la scheda non deve cominciare a trascinarsi. */
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
    ph_dsk_in_radice(w->d, w->maniglia, x, y, &w->inizio_x, &w->inizio_y);
    w->misura0 = (PhDskMisura){w->larghezza, w->altezza};
    w->destra0 = w->destra;
}

static void ridim_aggiorna(GtkGestureDrag *g, double ox, double oy, gpointer dati)
{
    PhDskWidget *w = dati;
    /* La maniglia si muove con l'angolo: il punto si riporta nelle coordinate della radice. */
    double sx, sy, rx, ry;
    gtk_gesture_drag_get_start_point(g, &sx, &sy);
    ph_dsk_in_radice(w->d, w->maniglia, sx + ox, sy + oy, &rx, &ry);
    ridim_calcola(w, w->misura0, w->destra0, rx - w->inizio_x, ry - w->inizio_y);
}

static void ridim_fine(GtkGestureDrag *g, double ox, double oy, gpointer dati)
{
    (void)g;
    (void)ox;
    (void)oy;
    PhDskWidget *w = dati;
    if (w->larghezza == w->misura0.larghezza && w->altezza == w->misura0.altezza)
        return;
    ridim_salva(w);
}

/* Frecce sulla maniglia con il fuoco: un passo (PH_DSK_PASSO) alla volta. Destra e giu'
 * allargano, sinistra e su restringono. */
static gboolean ridim_tasto(GtkEventControllerKey *c, guint tasto, guint codice, GdkModifierType stato,
                            gpointer dati)
{
    (void)c;
    (void)codice;
    (void)stato;
    PhDskWidget *w = dati;
    double dx = 0, dy = 0;
    switch (tasto) {
    case GDK_KEY_Right: case GDK_KEY_KP_Right: dx = PH_DSK_PASSO; break;
    case GDK_KEY_Left: case GDK_KEY_KP_Left: dx = -PH_DSK_PASSO; break;
    case GDK_KEY_Down: case GDK_KEY_KP_Down: dy = PH_DSK_PASSO; break;
    case GDK_KEY_Up: case GDK_KEY_KP_Up: dy = -PH_DSK_PASSO; break;
    default: return FALSE;
    }
    if (w->forma == PH_DSK_FORMA_TONDO) /* il tondo cresce sui due assi insieme */
        dx = dy = dx != 0 ? dx : dy;
    if (!ridim_calcola(w, (PhDskMisura){w->larghezza, w->altezza}, w->destra, dx, dy))
        return TRUE; /* al limite: il tasto e' preso comunque */
    PhDesktop *d = w->d;
    size_t indice = w->indice;
    if (ridim_salva(w)) {
        /* Ricostruita: la maniglia nuova riprende il fuoco, per continuare da tastiera. */
        for (size_t i = 0; i < d->widget->len; i++) {
            PhDskWidget *n = g_ptr_array_index(d->widget, i);
            if (n->indice == indice && n->maniglia != NULL)
                gtk_widget_grab_focus(n->maniglia);
        }
    }
    return TRUE;
}

/* La rotella sopra il widget non scorre il contenuto (che e' solo ritagliato). */
static gboolean ridim_rotella(GtkEventControllerScroll *c, double dx, double dy, gpointer dati)
{
    (void)c;
    (void)dx;
    (void)dy;
    (void)dati;
    return TRUE;
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

/* `alto`: l'ordinata con cui si mostra (di norma quella del profilo, vedi ph_dsk_predefiniti_alti). */
static PhDskWidget *widget_nuovo(PhDesktop *d, size_t indice, const PhCfgWidget *cfg, int alto)
{
    PhDskWidget *w = g_new0(PhDskWidget, 1);
    w->d = d;
    w->id = ++d->prossimo_id;
    w->indice = indice;
    w->tipo = cfg->tipo;
    w->stile = ph_dsk_stile_valido_di(cfg->tipo, (unsigned)cfg->stile);
    w->opzione = ph_dsk_citta_valida((unsigned)cfg->opzione);
    w->destra = cfg->destra;
    w->alto = alto;
    /* Misura dal profilo (0/0 = quella equivalente al livello salvato); il livello di contenuto
     * si ricava sempre dalla misura. */
    PhDskMisura mis = ph_dsk_misura_da_profilo(cfg->tipo, w->stile, cfg->dimensione, cfg->larghezza,
                                               cfg->altezza);
    w->larghezza = mis.larghezza;
    w->altezza = mis.altezza;
    w->dim = ph_dsk_livello(cfg->tipo, w->stile, mis);
    w->forma = ph_dsk_forma(cfg->tipo, w->stile);
    w->percento = -1; /* la barra compare con la prima vista */

    w->scheda = g_object_new(GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL,
                             "accessible-role", GTK_ACCESSIBLE_ROLE_GROUP, NULL);
    g_object_set_data_full(G_OBJECT(w->scheda), "ph-desktop-widget", w, g_free);
    g_object_set_data(G_OBJECT(w->scheda), "ph-desktop-id", GUINT_TO_POINTER(w->id));
    gtk_widget_add_css_class(w->scheda, "ph-desktop-widget");
    gtk_widget_add_css_class(w->scheda, w->forma == PH_DSK_FORMA_TONDO ? "ph-desktop-forma-tondo"
                                         : (w->forma == PH_DSK_FORMA_CAPSULA ? "ph-desktop-forma-capsula"
                                                                             : "ph-desktop-forma-mattonella"));
    /* Vetro scuro (come la barra). Il raggio dipende dalla forma e dalla misura. */
    w->raggio = ph_dsk_raggio(w->forma, w->larghezza, w->altezza);
    ph_shell_vetro_registra(d->sh, w->scheda, w->raggio, PH_VETRO_SCURO);
    gtk_widget_set_cursor_from_name(w->scheda, "move");
    gtk_widget_set_focusable(w->scheda, TRUE); /* per il tasto Menu da tastiera */
    ph_shell_etichetta(w->scheda, ph_dsk_widget_nome(w->tipo), NULL);

    /* Il contenuto sta in una finestra che lo ritaglia e non detta nessun minimo: la misura
     * della scheda e' sempre quella scelta (anche del tondo), il contenuto che non ci sta si
     * taglia. */
    w->interno = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *finestra = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(finestra), GTK_POLICY_EXTERNAL, GTK_POLICY_EXTERNAL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(finestra), w->interno);
    gtk_widget_set_hexpand(finestra, TRUE);
    gtk_widget_set_vexpand(finestra, TRUE);
    GtkEventController *rot = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    gtk_event_controller_set_propagation_phase(rot, GTK_PHASE_CAPTURE);
    g_signal_connect(rot, "scroll", G_CALLBACK(ridim_rotella), w);
    gtk_widget_add_controller(finestra, rot);

    /* Maniglia: visibile al passaggio del mouse e con il fuoco da tastiera (CSS). */
    w->maniglia = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(w->maniglia, "ph-desktop-maniglia");
    gtk_widget_set_halign(w->maniglia, GTK_ALIGN_END);
    gtk_widget_set_valign(w->maniglia, GTK_ALIGN_END);
    gtk_widget_set_focusable(w->maniglia, TRUE);
    gtk_widget_set_cursor_from_name(w->maniglia, "se-resize");
    ph_shell_etichetta(w->maniglia, ph_t("Ridimensiona widget"), NULL);
    GtkGesture *rd = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rd), GDK_BUTTON_PRIMARY);
    g_signal_connect(rd, "drag-begin", G_CALLBACK(ridim_inizio), w);
    g_signal_connect(rd, "drag-update", G_CALLBACK(ridim_aggiorna), w);
    g_signal_connect(rd, "drag-end", G_CALLBACK(ridim_fine), w);
    gtk_widget_add_controller(w->maniglia, GTK_EVENT_CONTROLLER(rd));
    GtkEventController *tasti = gtk_event_controller_key_new();
    g_signal_connect(tasti, "key-pressed", G_CALLBACK(ridim_tasto), w);
    gtk_widget_add_controller(w->maniglia, tasti);
    GtkWidget *sovra = gtk_overlay_new();
    gtk_widget_set_hexpand(sovra, TRUE);
    gtk_widget_set_vexpand(sovra, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(sovra), finestra);
    gtk_overlay_add_overlay(GTK_OVERLAY(sovra), w->maniglia);
    gtk_box_append(GTK_BOX(w->scheda), sovra);
    geometria_applica(w);

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
    gtk_widget_set_vexpand(w->quadrante, TRUE); /* il quadrante si centra nello spazio che resta */
    gtk_box_append(GTK_BOX(w->interno), w->titolo);
    gtk_box_append(GTK_BOX(w->interno), w->quadrante);
    w->corpo = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_visible(w->corpo, FALSE);
    if (w->tipo == PH_CFG_WIDGET_MEMORIA) {
        /* agente A: un clic sul contenuto apre l'app Impostazioni (se il telefono ce l'ha). Il clic
         * non reclama la sequenza: il trascinamento del widget funziona come sempre, e dopo un
         * trascinamento GTK non lo conta come clic. */
        GtkGesture *cl = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(cl), GDK_BUTTON_PRIMARY);
        g_signal_connect(cl, "released", G_CALLBACK(memoria_clic), w);
        gtk_widget_add_controller(w->corpo, GTK_EVENT_CONTROLLER(cl));
    }
    gtk_box_append(GTK_BOX(w->interno), w->corpo);
    gtk_box_append(GTK_BOX(w->interno), w->testa);
    gtk_box_append(GTK_BOX(w->interno), w->sotto);
    gtk_box_append(GTK_BOX(w->interno), w->barra);
    for (size_t i = 0; i < PH_DSK_RIGHE_MAX; i++) {
        w->riga[i] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_add_css_class(w->riga[i], "ph-desktop-w-riga");
        w->riga_sx[i] = etichetta_nuova("ph-desktop-w-riga-sx", true);
        gtk_widget_set_hexpand(w->riga_sx[i], TRUE);
        w->riga_dx[i] = etichetta_nuova("ph-desktop-w-riga-dx", false);
        gtk_box_append(GTK_BOX(w->riga[i]), w->riga_sx[i]);
        gtk_box_append(GTK_BOX(w->riga[i]), w->riga_dx[i]);
        gtk_box_append(GTK_BOX(w->interno), w->riga[i]);
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

    ph_shell_libero_metti(d->libero, w->scheda, 0, 0, w->larghezza, w->altezza);
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

/* agente A: l'app del calendario (per il clic su un impegno) la dice solo la lettura a intervallo.
 * Si chiede una volta, sulle prossime 24 ore, e solo finche' non e' nota; degli eventi si butta
 * tutto (riservatezza): conta solo il nome del pacchetto. */
static void app_calendario_fatta(PhServizi *s, PhSrvEsito esito, const char *frase,
                                 const PhSrvEventoCalendario *v, size_t n, bool troncato,
                                 const char *app_calendario, void *utente)
{
    (void)s;
    (void)frase;
    (void)v;
    (void)n;
    (void)troncato;
    PhDesktop *d = utente;
    if (!d->vivo)
        return;
    g_clear_object(&d->annulla_app_cal);
    /* Il pacchetto arriva dal telefono: si controlla (logica pura, con prova). */
    if (esito != PH_SRV_ESITO_OK || !ph_dsk_pacchetto_valido(app_calendario))
        return;
    if (d->app_calendario != NULL && strcmp(d->app_calendario, app_calendario) == 0)
        return;
    g_free(d->app_calendario);
    d->app_calendario = g_strdup(app_calendario);
    ph_dsk_widget_aggiorna_tutti(d); /* gli impegni diventano cliccabili */
}

void ph_dsk_widget_calendario_chiedi(PhDesktop *d)
{
    if (!serve_il_telefono(d, PH_CFG_WIDGET_CALENDARIO))
        return;
    d->t_calendario = g_get_monotonic_time();
    rinnova(&d->annulla_calendario);
    ph_srv_tel_calendario(d->srv, PH_SRV_CALENDARIO_MAX, d->annulla_calendario, calendario_fatto, d);
    if (d->app_calendario == NULL) {
        uint64_t ora = (uint64_t)(g_get_real_time() / 1000);
        rinnova(&d->annulla_app_cal);
        ph_srv_tel_calendario_intervallo(d->srv, ora, ora + 86400000u, d->annulla_app_cal, app_calendario_fatta,
                                         d);
    }
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
    if (d->annulla_app_cal != NULL) { /* agente A */
        g_cancellable_cancel(d->annulla_app_cal);
        g_clear_object(&d->annulla_app_cal);
    }
    g_clear_pointer(&d->app_calendario, g_free); /* l'app del calendario e' quella di un altro telefono */
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
        /* Profilo nuovo mai toccato: i tre predefiniti si mostrano a distanza giusta. */
        int alti[3];
        bool distanzia = ph_dsk_predefiniti_alti(p->widget, p->n_widget, alti);
        for (size_t i = 0; i < p->n_widget && i < PH_CFG_MAX_WIDGET; i++) {
            /* Un tipo fuori catalogo non si mostra e NON si cancella dal profilo. */
            if (ph_dsk_widget_nome(p->widget[i].tipo) == NULL)
                continue;
            g_ptr_array_add(d->widget, widget_nuovo(d, i, &p->widget[i], distanzia ? alti[i] : p->widget[i].alto));
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

void ph_dsk_widget_aggiungi(PhDesktop *d, PhCfgWidgetTipo tipo)
{
    PhCfgTelefono *p = ph_dsk_profilo(d);
    const char *nome = ph_dsk_widget_nome(tipo);
    if (p == NULL || nome == NULL || p->n_widget >= PH_CFG_MAX_WIDGET)
        return;
    posizioni_fissa(d); /* il posto del nuovo si calcola sui posti con cui si vedono gli altri */
    PhDskWidgetPos pos[PH_CFG_MAX_WIDGET];
    size_t np = 0;
    for (size_t i = 0; i < p->n_widget; i++) {
        if (ph_dsk_widget_nome(p->widget[i].tipo) == NULL)
            continue; /* i tipi fuori catalogo non si vedono: non occupano posto */
        const PhCfgWidget *e = &p->widget[i];
        PhDskMisura m = ph_dsk_misura_da_profilo(e->tipo, ph_dsk_stile_valido_di(e->tipo, (unsigned)e->stile),
                                                 e->dimensione, e->larghezza, e->altezza);
        pos[np].destra = e->destra;
        pos[np].alto = e->alto;
        pos[np].larghezza = m.larghezza;
        pos[np].altezza = m.altezza;
        np++;
    }
    int destra, alto;
    ph_dsk_posto_widget_nuovo(d->area_l, d->area_a - PH_DSK_BARRA, pos, np,
                              ph_dsk_misura_equivalente(tipo, 0, PH_CFG_WIDGET_MEDIO), &destra, &alto);
    PhCfgWidget *n = &p->widget[p->n_widget++];
    *n = (PhCfgWidget){0}; /* dopo una rimozione la voce in coda puo' avere i valori di un'altra */
    n->tipo = tipo;
    n->destra = destra;
    n->alto = alto;
    n->dimensione = PH_CFG_WIDGET_MEDIO; /* larghezza e altezza 0: la misura equivalente al Medio */
    ph_dsk_salva(d);
    ph_dsk_widget_ricostruisci(d);
    ph_dsk_avviso(d, ph_t("Widget aggiunto"), nome);
}

void ph_dsk_widget_rimuovi(PhDesktop *d, unsigned id)
{
    PhDskWidget *w = cerca(d, id);
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (w == NULL || p == NULL || w->indice >= p->n_widget || p->widget[w->indice].tipo != w->tipo)
        return;
    posizioni_fissa(d);
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
    posizioni_fissa(d);
    PhCfgWidget *c = &p->widget[w->indice];
    unsigned nuovo = ph_dsk_stile_valido_di(w->tipo, stile);
    if (nuovo != w->stile) {
        /* La misura si perde solo se cambia la forma (ogni forma ha i suoi limiti): allora si riparte
         * dall'equivalente al livello di contenuto di adesso. Altrimenti si tiene, entro i limiti
         * del nuovo stile. */
        PhDskMisura m = ph_dsk_misura_cambio_stile(w->tipo, w->stile, nuovo, (PhDskMisura){w->larghezza, w->altezza});
        c->larghezza = m.larghezza;
        c->altezza = m.altezza;
        c->dimensione = m.larghezza > 0 ? ph_dsk_livello(w->tipo, nuovo, m) : w->dim;
    }
    c->stile = (int)nuovo;
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
        /* agente A: icone e nomi delle app arrivano dopo: le notifiche (icona vera) e la memoria
         * (l'app Impostazioni c'e' o no) si rifanno. */
        else if ((w->tipo == PH_CFG_WIDGET_NOTIFICHE && (ev == PH_SRV_EV_ICONA || ev == PH_SRV_EV_APP)) ||
                 (w->tipo == PH_CFG_WIDGET_MEMORIA && ev == PH_SRV_EV_APP))
            widget_aggiorna(w);
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
