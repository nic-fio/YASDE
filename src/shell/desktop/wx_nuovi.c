/* Widget «Chiamate recenti», «Rete» e «In riproduzione» (PIANO.md 9). I dati arrivano dai servizi
 * (ph_srv_chiamate, ph_srv_rete, ph_srv_media): una fotografia dell'ultimo valore letto + un
 * evento a ogni lettura nuova; qui non si chiede nulla al telefono. Ogni widget dichiara il suo
 * interesse a quando nasce (osserva true) e lo toglie quando muore (osserva false, solo a servizi
 * ancora vivi: nel "destroy" della radice non si toccano). Che cosa mostrare lo decide la logica
 * pura di logica_nuovi.c.
 *
 * Riservatezza: nomi, numeri, titoli e nomi di reti si mostrano e basta. Mai in un log, in un
 * messaggio d'errore, su disco, ne' come formato di stringa: sempre come testo di un'etichetta,
 * gia' reso sicuro dalla logica, tagliato con i puntini. */
#include <string.h>

#include "interno.h"
#include "logica_nuovi.h"

/* ---- Comuni ---------------------------------------------------------------------------------- */
static GtkWidget *etichetta(const char *classe)
{
    GtkWidget *l = gtk_label_new("");
    gtk_widget_add_css_class(l, classe);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 1); /* nessuna larghezza minima: il testo si taglia */
    return l;
}

static void testo_imposta(GtkWidget *l, const char *t)
{
    gtk_widget_set_visible(l, t != NULL && t[0] != '\0');
    gtk_label_set_text(GTK_LABEL(l), t != NULL ? t : "");
}

/* ---- Chiamate recenti -------------------------------------------------------------------------- */
typedef struct {
    bool osserva;
} Chiamate;

/* Il primo pacchetto dell'app Telefono che il telefono ha davvero; NULL = nessun clic. */
static const char *g_telefono_prova = NULL;

/* Solo per le prove del demo: un altro pacchetto al posto dei noti (il telefono finto non ha
 * l'app Telefono). NULL toglie la sostituzione. La stringa deve vivere piu' dei widget. */
void ph_dsk_telefono_pacchetto_prova(const char *pacchetto)
{
    g_telefono_prova = pacchetto;
}

const char *ph_dsk_telefono_pacchetto(PhDesktop *d)
{
    if (!d->vivo)
        return NULL;
    if (g_telefono_prova != NULL)
        return ph_srv_app_cerca(d->srv, g_telefono_prova) != NULL ? g_telefono_prova : NULL;
    for (size_t i = 0; i < PH_DSK_PACCHETTI_TELEFONO_N; i++)
        if (ph_srv_app_cerca(d->srv, PH_DSK_PACCHETTI_TELEFONO[i]) != NULL)
            return PH_DSK_PACCHETTI_TELEFONO[i];
    return NULL;
}

static void chiamata_clic(void *dati)
{
    PhDskWidget *w = dati;
    const char *pk = w->scheda != NULL ? ph_dsk_telefono_pacchetto(w->d) : NULL;
    if (pk != NULL)
        ph_shell_apri_app(w->d->sh, pk);
}

static void chiamate_disegna(PhDskWidget *w, PhDskVista *v)
{
    PhDesktop *d = w->d;
    ph_dsk_wx_svuota(w->corpo);
    gtk_widget_set_visible(w->corpo, TRUE);
    snprintf(v->titolo, sizeof v->titolo, "%s", ph_t("Chiamate recenti"));
    const PhSrvChiamate *c = ph_srv_chiamate(d->srv);
    char stato[96];
    ph_dsk_chiamate_stato_testo(c, stato, sizeof stato);
    size_t n = ph_dsk_chiamate_da_mostrare(c, w->larghezza, w->altezza);
    if (n == 0) {
        if (stato[0] != '\0') {
            GtkWidget *l = etichetta("ph-desktop-w-sotto");
            gtk_label_set_text(GTK_LABEL(l), stato);
            gtk_box_append(GTK_BOX(w->corpo), l);
        }
        return;
    }
    gint64 adesso = g_get_real_time() / 1000;
    bool clic = ph_dsk_telefono_pacchetto(d) != NULL;
    GtkWidget *elenco = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    for (size_t i = 0; i < n; i++) {
        const PhSrvChiamata *ch = &c->v[i];
        GtkWidget *riga = ph_dsk_riga_nuova(clic ? chiamata_clic : NULL, w);
        char titolo[256], ora[32];
        ph_dsk_chiamata_titolo(ch, titolo, sizeof titolo);
        ph_dsk_chiamata_ora(ch->ora_ms, (uint64_t)adesso, NULL, ora, sizeof ora);
        ph_dsk_riga_imposta(riga, NULL, ph_dsk_chiamata_accento(ch->tipo), true, titolo,
                            ph_dsk_chiamata_tipo_testo(ch->tipo), ora);
        int tipo = ph_dsk_chiamata_tipo_valido(ch->tipo);
        if (tipo != PH_SRV_CHIAMATA_ALTRO) {
            GtkWidget *ico = ph_shell_icona_nuova(&PH_DSK_ICONA_CHIAMATA[tipo]);
            gtk_widget_add_css_class(ico, "ph-desktop-chiamata-icona"); /* colore del testo secondario */
            if (tipo == PH_SRV_CHIAMATA_PERSA)
                gtk_widget_add_css_class(ico, "ph-desktop-chiamata-persa");
            else if (tipo == PH_SRV_CHIAMATA_RIFIUTATA)
                gtk_widget_add_css_class(ico, "ph-desktop-chiamata-rifiutata");
            ph_dsk_riga_icona(riga, ico);
        }
        gtk_box_append(GTK_BOX(elenco), riga);
    }
    gtk_box_append(GTK_BOX(w->corpo), elenco);
}

static void chiamate_inizia(PhDskWidget *w)
{
    Chiamate *x = g_new0(Chiamate, 1);
    w->x = x;
    ph_srv_chiamate_osserva(w->d->srv, true);
    x->osserva = true;
}

static void chiamate_ferma(PhDskWidget *w)
{
    Chiamate *x = w->x;
    if (x->osserva && w->d->vivo) /* nel "destroy" della radice i servizi non si toccano */
        ph_srv_chiamate_osserva(w->d->srv, false);
    g_free(x);
    w->x = NULL;
}

static void chiamate_evento(PhDskWidget *w, int ev)
{
    if (ev == PH_SRV_EV_CHIAMATE || ev == PH_SRV_EV_APP || ev == PH_SRV_EV_PROFILO)
        ph_dsk_widget_aggiorna_uno(w);
}

const PhDskWxVt PH_DSK_WX_CHIAMATE = {
    .inizia = chiamate_inizia,
    .ferma = chiamate_ferma,
    .aggiorna = chiamate_disegna,
    .evento = chiamate_evento,
};

/* ---- Rete ---------------------------------------------------------------------------------------- */
typedef struct {
    bool osserva;
    GtkWidget *stato;            /* «Not available on this phone» */
    GtkWidget *blocco[2];        /* 0 Wi-Fi, 1 Mobile */
    GtkWidget *ico[2];
    GtkWidget *nome[2], *riga[2], *nota[2];
    GtkWidget *aereo;
} Rete;

static GtkWidget *rete_blocco(Rete *x, int i, const char *nome)
{
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(b, "ph-desktop-rete-blocco");
    gtk_widget_set_vexpand(b, TRUE); /* i blocchi si dividono l'altezza: nessuna fascia vuota in fondo */
    x->ico[i] = ph_shell_icona_nuova(i == 0 ? &PH_DSK_ICONA_WIFI[0] : &PH_DSK_ICONA_MOBILE[0]);
    gtk_widget_set_valign(x->ico[i], GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(b), x->ico[i]);
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(col, TRUE);
    gtk_widget_set_valign(col, GTK_ALIGN_CENTER);
    x->nome[i] = etichetta("ph-desktop-rete-nome");
    gtk_label_set_text(GTK_LABEL(x->nome[i]), nome);
    x->riga[i] = etichetta("ph-desktop-rete-riga");
    x->nota[i] = etichetta("ph-desktop-rete-nota");
    gtk_box_append(GTK_BOX(col), x->nome[i]);
    gtk_box_append(GTK_BOX(col), x->riga[i]);
    gtk_box_append(GTK_BOX(col), x->nota[i]);
    gtk_box_append(GTK_BOX(b), col);
    return b;
}

static void rete_inizia(PhDskWidget *w)
{
    Rete *x = g_new0(Rete, 1);
    w->x = x;
    gtk_widget_set_vexpand(w->corpo, TRUE);
    x->stato = etichetta("ph-desktop-w-sotto");
    gtk_box_append(GTK_BOX(w->corpo), x->stato);
    x->blocco[0] = rete_blocco(x, 0, "Wi-Fi");
    x->blocco[1] = rete_blocco(x, 1, ph_t("Mobile"));
    gtk_box_append(GTK_BOX(w->corpo), x->blocco[0]);
    gtk_box_append(GTK_BOX(w->corpo), x->blocco[1]);
    x->aereo = etichetta("ph-desktop-w-sotto");
    gtk_label_set_text(GTK_LABEL(x->aereo), ph_t("Modalità aereo"));
    gtk_box_append(GTK_BOX(w->corpo), x->aereo);
    ph_srv_rete_osserva(w->d->srv, true);
    x->osserva = true;
}

static void rete_ferma(PhDskWidget *w)
{
    Rete *x = w->x;
    if (x->osserva && w->d->vivo)
        ph_srv_rete_osserva(w->d->srv, false);
    g_free(x);
    w->x = NULL;
}

static void rete_disegna(PhDskWidget *w, PhDskVista *v)
{
    Rete *x = w->x;
    snprintf(v->titolo, sizeof v->titolo, "%s", ph_t("Rete"));
    gtk_widget_set_visible(w->corpo, TRUE);
    const PhSrvReteDettaglio *r = ph_srv_rete(w->d->srv);
    char stato[96];
    ph_dsk_rete_stato_testo(r, stato, sizeof stato);
    bool dati = r != NULL && r->stato == PH_SRV_LET_OK;
    testo_imposta(x->stato, stato);
    for (int i = 0; i < 2; i++)
        gtk_widget_set_visible(x->blocco[i], dati);
    gtk_widget_set_visible(x->aereo, ph_dsk_rete_aereo(r));
    if (!dati)
        return;
    int lw, lm;
    char tw[256], tm[256], nota[96];
    ph_dsk_rete_wifi(r, &lw, tw, sizeof tw);
    ph_dsk_rete_mobile(r, &lm, tm, sizeof tm, nota, sizeof nota);
    ph_shell_icona_imposta(x->ico[0], &PH_DSK_ICONA_WIFI[lw]);
    ph_shell_icona_imposta(x->ico[1], &PH_DSK_ICONA_MOBILE[lm]);
    testo_imposta(x->riga[0], tw);
    testo_imposta(x->nota[0], "");
    testo_imposta(x->riga[1], tm);
    testo_imposta(x->nota[1], nota);
    for (int i = 0; i < 2; i++) {
        char et[600];
        snprintf(et, sizeof et, "%s, %s%s%s", gtk_label_get_text(GTK_LABEL(x->nome[i])),
                 gtk_label_get_text(GTK_LABEL(x->riga[i])), i == 1 && nota[0] != '\0' ? ", " : "",
                 i == 1 ? nota : "");
        ph_shell_etichetta(x->blocco[i], et, NULL);
    }
}

static void rete_evento(PhDskWidget *w, int ev)
{
    if (ev == PH_SRV_EV_RETE || ev == PH_SRV_EV_PROFILO)
        ph_dsk_widget_aggiorna_uno(w);
}

const PhDskWxVt PH_DSK_WX_RETE = {
    .inizia = rete_inizia,
    .ferma = rete_ferma,
    .aggiorna = rete_disegna,
    .evento = rete_evento,
};

/* ---- In riproduzione ------------------------------------------------------------------------------ */
typedef struct {
    PhDskWidget *w;
    bool osserva;
    GtkWidget *stato;             /* «Nothing playing» / «Not available on this phone» */
    GtkWidget *brano;             /* titolo (pulsante), artista e app */
    GtkWidget *titolo_btn, *titolo, *artista, *app;
    GtkWidget *comandi;           /* i tre pulsanti */
    GtkWidget *btn[3];            /* indietro, play-pausa, avanti */
    GtkWidget *ico_centro;
} Media;

static const int MEDIA_CMD[3] = {PH_SRV_MEDIA_CMD_INDIETRO, PH_SRV_MEDIA_CMD_PLAY_PAUSA, PH_SRV_MEDIA_CMD_AVANTI};

static void media_comando_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    PhDskWidget *w = dati;
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "ph-desktop-comando"));
    if (!w->d->vivo || w->x == NULL || i < 0 || i > 2)
        return;
    /* Si ricontrolla qui: il pulsante puo' essere rimasto acceso per l'ultima lettura. */
    if (ph_dsk_media_puo(ph_srv_media(w->d->srv), MEDIA_CMD[i]))
        ph_srv_media_comando(w->d->srv, MEDIA_CMD[i]);
}

static void media_titolo_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    PhDskWidget *w = dati;
    if (!w->d->vivo || w->x == NULL)
        return;
    const char *pk = ph_dsk_media_pacchetto(ph_srv_media(w->d->srv));
    if (pk != NULL && ph_srv_app_cerca(w->d->srv, pk) != NULL)
        ph_shell_apri_app(w->d->sh, pk);
}

static GtkWidget *media_pulsante(Media *x, int i, const PhShellIcona *ico, const char *nome)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "ph-desktop-media-tasto");
    if (i == 1)
        gtk_widget_add_css_class(b, "ph-desktop-media-tasto-centro");
    GtkWidget *im = ph_shell_icona_nuova(ico);
    gtk_widget_set_halign(im, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(im, GTK_ALIGN_CENTER);
    gtk_button_set_child(GTK_BUTTON(b), im);
    ph_shell_etichetta(b, nome, NULL);
    g_object_set_data(G_OBJECT(b), "ph-desktop-comando", GINT_TO_POINTER(i));
    g_signal_connect(b, "clicked", G_CALLBACK(media_comando_clic), x->w);
    if (i == 1)
        x->ico_centro = im;
    return b;
}

static void media_inizia(PhDskWidget *w)
{
    Media *x = g_new0(Media, 1);
    x->w = w;
    w->x = x;
    gtk_widget_set_vexpand(w->corpo, TRUE);
    x->stato = etichetta("ph-desktop-w-sotto");
    gtk_box_append(GTK_BOX(w->corpo), x->stato);

    x->brano = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(x->brano, TRUE);
    x->titolo_btn = gtk_button_new();
    gtk_widget_add_css_class(x->titolo_btn, "ph-desktop-media-titolo-btn");
    x->titolo = etichetta("ph-desktop-media-titolo");
    gtk_button_set_child(GTK_BUTTON(x->titolo_btn), x->titolo);
    g_signal_connect(x->titolo_btn, "clicked", G_CALLBACK(media_titolo_clic), w);
    x->artista = etichetta("ph-desktop-media-artista");
    x->app = etichetta("ph-desktop-media-app");
    gtk_box_append(GTK_BOX(x->brano), x->titolo_btn);
    gtk_box_append(GTK_BOX(x->brano), x->artista);
    gtk_box_append(GTK_BOX(x->brano), x->app);
    gtk_box_append(GTK_BOX(w->corpo), x->brano);

    x->comandi = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_set_halign(x->comandi, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(x->comandi, 8);
    x->btn[0] = media_pulsante(x, 0, &PH_DSK_ICONA_MEDIA_INDIETRO, ph_t("Brano precedente"));
    x->btn[1] = media_pulsante(x, 1, &PH_DSK_ICONA_MEDIA_PLAY, ph_t("Riproduci o metti in pausa"));
    x->btn[2] = media_pulsante(x, 2, &PH_DSK_ICONA_MEDIA_AVANTI, ph_t("Brano successivo"));
    for (int i = 0; i < 3; i++)
        gtk_box_append(GTK_BOX(x->comandi), x->btn[i]);
    gtk_box_append(GTK_BOX(w->corpo), x->comandi);

    ph_srv_media_osserva(w->d->srv, true);
    x->osserva = true;
}

static void media_ferma(PhDskWidget *w)
{
    Media *x = w->x;
    if (x->osserva && w->d->vivo)
        ph_srv_media_osserva(w->d->srv, false);
    g_free(x);
    w->x = NULL;
}

static void media_disegna(PhDskWidget *w, PhDskVista *v)
{
    Media *x = w->x;
    snprintf(v->titolo, sizeof v->titolo, "%s", ph_t("In riproduzione"));
    gtk_widget_set_visible(w->corpo, TRUE);
    const PhSrvMedia *m = ph_srv_media(w->d->srv);
    char stato[96];
    ph_dsk_media_stato_testo(m, stato, sizeof stato);
    bool brano = ph_dsk_media_attivo(m);
    testo_imposta(x->stato, stato);
    gtk_widget_set_visible(x->brano, brano);
    /* I pulsanti ci sono sempre quando i dati sono arrivati (spenti senza brano): cosi' il widget
     * non cambia forma da un momento all'altro. */
    gtk_widget_set_visible(x->comandi, m != NULL && m->stato_lettura == PH_SRV_LET_OK);
    for (int i = 0; i < 3; i++)
        gtk_widget_set_sensitive(x->btn[i], ph_dsk_media_puo(m, MEDIA_CMD[i]));
    ph_shell_icona_imposta(x->ico_centro, ph_dsk_media_suona(m) ? &PH_DSK_ICONA_MEDIA_PAUSA : &PH_DSK_ICONA_MEDIA_PLAY);
    if (!brano)
        return;
    char t[256], a[256];
    ph_dsk_media_titolo(m, t, sizeof t);
    ph_dsk_media_artista(m, a, sizeof a);
    gtk_label_set_text(GTK_LABEL(x->titolo), t);
    testo_imposta(x->artista, a);
    const char *pk = ph_dsk_media_pacchetto(m);
    const PhSrvApp *app = pk != NULL ? ph_srv_app_cerca(w->d->srv, pk) : NULL;
    char nome_app[256];
    ph_dsk_nome_sicuro(app != NULL && app->nome != NULL ? app->nome : "", nome_app, sizeof nome_app);
    testo_imposta(x->app, nome_app);
    gtk_widget_set_cursor_from_name(x->titolo_btn, app != NULL ? "pointer" : NULL);
}

static void media_evento(PhDskWidget *w, int ev)
{
    if (ev == PH_SRV_EV_MEDIA || ev == PH_SRV_EV_APP || ev == PH_SRV_EV_PROFILO)
        ph_dsk_widget_aggiorna_uno(w);
}

const PhDskWxVt PH_DSK_WX_MEDIA = {
    .inizia = media_inizia,
    .ferma = media_ferma,
    .aggiorna = media_disegna,
    .evento = media_evento,
};
