/* Dialogo «Località…» del widget Meteo (CONTRATTI.md §18): la citta' scelta a mano vale SOLO
 * quando il telefono non da' una posizione. La ricerca parte SOLO con «Cerca» o Invio (mai a ogni
 * tasto), una nuova ricerca annulla la precedente, chiudere il dialogo annulla. Il testo cercato,
 * i nomi dei luoghi e le coordinate non vanno MAI in un log. La citta' scelta si salva nel profilo
 * (e' l'unico dato di luogo su disco: una scelta dell'utente) e si comunica ai servizi. */
#include <string.h>

#include "interno.h"

struct PhDskLuogo {
    PhDesktop *d;
    GtkWidget *dlg, *campo, *cerca, *scorri, *elenco, *stato, *nessuna;
    GCancellable *annulla_ricerca, *annulla_uri;
    PhMeteoLuogo ris[PH_METEO_LUOGHI_MAX];
    size_t n_ris;
    bool in_corso;
};

static GtkWidget *etichetta(const char *testo, const char *classe)
{
    GtkWidget *l = gtk_label_new(testo);
    gtk_widget_add_css_class(l, classe);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    return l;
}

/* ---- Profilo e servizi -------------------------------------------------------------------------- */
void ph_dsk_luogo_comunica(PhDesktop *d)
{
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p == NULL || !p->meteo_luogo_scelto) {
        ph_srv_meteo_localita(d->srv, NULL);
        return;
    }
    PhMeteoLuogo l;
    memset(&l, 0, sizeof l);
    g_strlcpy(l.nome, p->meteo_luogo_nome, sizeof l.nome);
    g_strlcpy(l.dettaglio, p->meteo_luogo_dettaglio, sizeof l.dettaglio);
    l.lat_centesimi = p->meteo_luogo_lat_centesimi;
    l.lon_centesimi = p->meteo_luogo_lon_centesimi;
    ph_srv_meteo_localita(d->srv, &l);
}

static void scegli(PhDskLuogo *l, size_t i)
{
    PhDesktop *d = l->d;
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p == NULL || i >= l->n_ris)
        return;
    const PhMeteoLuogo *r = &l->ris[i];
    /* Nel profilo va SOLO la citta' scelta a mano (nome e dettaglio troncati su un carattere intero). */
    if (!ph_dsk_luogo_scrivi(p, r->nome, r->dettaglio, r->lat_centesimi, r->lon_centesimi))
        return;
    ph_dsk_salva(d);
    ph_dsk_luogo_comunica(d);
    ph_shell_popup_chiudi(d->sh);
}

static void su_risultato(GtkButton *b, gpointer dati)
{
    scegli(dati, GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(b), "ph-desktop-voce")));
}

static void su_nessuna(GtkButton *b, gpointer dati)
{
    (void)b;
    PhDskLuogo *l = dati;
    PhDesktop *d = l->d;
    PhCfgTelefono *p = ph_dsk_profilo(d);
    if (p != NULL) {
        ph_dsk_luogo_azzera(p);
        ph_dsk_salva(d);
    }
    ph_dsk_luogo_comunica(d);
    ph_shell_popup_chiudi(d->sh);
}

/* ---- Ricerca ------------------------------------------------------------------------------------------ */
static void elenco_svuota(PhDskLuogo *l)
{
    l->n_ris = 0;
    ph_dsk_wx_svuota(l->elenco);
    gtk_widget_set_visible(l->scorri, FALSE);
}

static void stato_testo(PhDskLuogo *l, const char *t)
{
    gtk_label_set_text(GTK_LABEL(l->stato), t != NULL ? t : "");
    gtk_widget_set_visible(l->stato, t != NULL && t[0] != '\0');
}

static void cerca_fatta(PhServizi *s, PhSrvEsito esito, const char *frase, const PhMeteoLuogo *v, size_t n,
                        void *utente)
{
    (void)s;
    PhDskLuogo *l = utente;
    if (!l->d->vivo)
        return;
    g_clear_object(&l->annulla_ricerca);
    l->in_corso = false;
    gtk_widget_set_sensitive(l->cerca, TRUE);
    elenco_svuota(l);
    if (esito != PH_SRV_ESITO_OK || v == NULL) {
        /* La frase del servizio non contiene mai il testo cercato. */
        stato_testo(l, frase != NULL && frase[0] != '\0' ? frase : ph_t("Ricerca non riuscita."));
        return;
    }
    if (n == 0) {
        stato_testo(l, ph_t("Nessun luogo trovato."));
        return;
    }
    stato_testo(l, NULL);
    if (n > PH_METEO_LUOGHI_MAX)
        n = PH_METEO_LUOGHI_MAX;
    for (size_t i = 0; i < n; i++) {
        l->ris[i] = v[i];
        GtkWidget *b = gtk_button_new();
        gtk_widget_add_css_class(b, "ph-desktop-luogo-riga");
        GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
        GtkWidget *nome = etichetta(v[i].nome, "ph-desktop-luogo-nome");
        gtk_label_set_ellipsize(GTK_LABEL(nome), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(nome), 1);
        gtk_box_append(GTK_BOX(col), nome);
        if (v[i].dettaglio[0] != '\0') {
            GtkWidget *det = etichetta(v[i].dettaglio, "ph-desktop-luogo-dettaglio");
            gtk_label_set_ellipsize(GTK_LABEL(det), PANGO_ELLIPSIZE_END);
            gtk_label_set_max_width_chars(GTK_LABEL(det), 1);
            gtk_box_append(GTK_BOX(col), det);
        }
        gtk_button_set_child(GTK_BUTTON(b), col);
        char et[300];
        ph_lingua_componi(et, sizeof et, ph_t("{titolo}: {testo}"), "titolo", v[i].nome, "testo", v[i].dettaglio,
                          (const char *)NULL);
        ph_shell_etichetta(b, v[i].dettaglio[0] != '\0' ? et : v[i].nome, NULL);
        g_object_set_data(G_OBJECT(b), "ph-desktop-voce", GSIZE_TO_POINTER(i));
        g_signal_connect(b, "clicked", G_CALLBACK(su_risultato), l);
        gtk_box_append(GTK_BOX(l->elenco), b);
    }
    l->n_ris = n;
    gtk_widget_set_visible(l->scorri, TRUE);
}

static void avvia_ricerca(PhDskLuogo *l)
{
    if (l->in_corso)
        return;
    const char *grezzo = gtk_editable_get_text(GTK_EDITABLE(l->campo));
    char *testo = g_strstrip(g_strdup(grezzo != NULL ? grezzo : ""));
    elenco_svuota(l);
    if (testo[0] == '\0') {
        g_free(testo);
        return;
    }
    l->in_corso = true;
    gtk_widget_set_sensitive(l->cerca, FALSE); /* durante la ricerca «Cerca» e' spento */
    stato_testo(l, ph_t("Ricerca in corso…"));
    /* Una nuova ricerca annulla la precedente (rinnova); la lingua e' quella della shell. */
    ph_srv_meteo_cerca(l->d->srv, testo, ph_lingua_attiva() == PH_LINGUA_EN ? "en" : "it",
                       ph_dsk_wx_rinnova(&l->annulla_ricerca), cerca_fatta, l);
    g_free(testo);
}

static void su_cerca(GtkWidget *w, gpointer dati)
{
    (void)w;
    avvia_ricerca(dati);
}

/* ---- Apertura e chiusura --------------------------------------------------------------------------------- */
static void chiuso(PhShell *sh, PhShellPopup tipo, GtkWidget *w, void *utente)
{
    (void)sh;
    (void)tipo;
    (void)w;
    PhDskLuogo *l = utente;
    /* Chiudere il dialogo annulla la ricerca in volo. */
    ph_dsk_wx_annulla(&l->annulla_ricerca);
    l->in_corso = false;
    gtk_widget_set_sensitive(l->cerca, TRUE);
    elenco_svuota(l);
    stato_testo(l, NULL);
    gtk_editable_set_text(GTK_EDITABLE(l->campo), "");
}

static void su_annulla(GtkButton *b, gpointer dati)
{
    (void)b;
    PhDskLuogo *l = dati;
    ph_shell_popup_chiudi(l->d->sh);
}

static void su_geonames(GtkButton *b, gpointer dati)
{
    (void)b;
    PhDskLuogo *l = dati;
    ph_dsk_apri_uri(l->d, ph_dsk_wx_rinnova(&l->annulla_uri), PH_METEO_GEO_SITO);
}

void ph_dsk_luogo_area(PhDesktop *d)
{
    PhDskLuogo *l = d->luogo;
    if (l == NULL)
        return;
    int largo = d->area_l > 0 ? d->area_l : 1280, alto = d->area_a > 0 ? d->area_a : 800;
    int w = MIN(440, largo - 48);
    gtk_widget_set_size_request(l->dlg, w > 220 ? w : 220, -1);
    int h = alto - 96 - 250;
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(l->scorri), h > 90 ? h : 90);
}

void ph_dsk_luogo_apri(PhDesktop *d)
{
    PhDskLuogo *l = d->luogo;
    if (l == NULL)
        return;
    ph_dsk_luogo_area(d);
    PhCfgTelefono *p = ph_dsk_profilo(d);
    gtk_widget_set_sensitive(l->nessuna, p != NULL && p->meteo_luogo_scelto);
    ph_shell_popup_apri(d->sh, PH_POPUP_DIALOGO_WIDGET, l->dlg, chiuso, l);
    gtk_widget_grab_focus(l->campo);
}

PhDskLuogo *ph_dsk_luogo_nuovo(PhDesktop *d)
{
    PhDskLuogo *l = g_new0(PhDskLuogo, 1);
    l->d = d;
    l->dlg = g_object_new(GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL, "spacing", 12, "accessible-role",
                          GTK_ACCESSIBLE_ROLE_DIALOG, NULL);
    gtk_widget_add_css_class(l->dlg, "ph-dialogo");
    ph_shell_vetro_registra(d->sh, l->dlg, 16, PH_VETRO_CHIARO); /* raggio di ph-dialogo */
    gtk_widget_set_halign(l->dlg, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(l->dlg, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(l->dlg, FALSE);
    ph_shell_etichetta(l->dlg, ph_t("Località"), NULL);

    GtkWidget *testa = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *titolo = etichetta(ph_t("Località"), "ph-titolo-dialogo");
    gtk_widget_set_hexpand(titolo, TRUE);
    GtkWidget *annulla = gtk_button_new_with_label(ph_t("Annulla"));
    gtk_widget_add_css_class(annulla, "ph-ghost");
    g_signal_connect(annulla, "clicked", G_CALLBACK(su_annulla), l);
    gtk_box_append(GTK_BOX(testa), titolo);
    gtk_box_append(GTK_BOX(testa), annulla);
    gtk_box_append(GTK_BOX(l->dlg), testa);

    GtkWidget *nota = etichetta(ph_t("Usata solo quando il telefono non dà la sua posizione."), "ph-desktop-nota");
    gtk_label_set_wrap(GTK_LABEL(nota), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(nota), 1);
    gtk_box_append(GTK_BOX(l->dlg), nota);

    GtkWidget *riga = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    l->campo = gtk_entry_new();
    gtk_widget_add_css_class(l->campo, "ph-campo");
    gtk_widget_set_hexpand(l->campo, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(l->campo), ph_t("Nome della città"));
    gtk_entry_set_max_length(GTK_ENTRY(l->campo), PH_METEO_RICERCA_TESTO_MAX);
    ph_shell_etichetta(l->campo, ph_t("Nome della città"), NULL);
    g_signal_connect(l->campo, "activate", G_CALLBACK(su_cerca), l); /* Invio: l'unico altro modo di cercare */
    l->cerca = gtk_button_new_with_label(ph_t("Cerca"));
    gtk_widget_add_css_class(l->cerca, "ph-accento");
    g_signal_connect(l->cerca, "clicked", G_CALLBACK(su_cerca), l);
    gtk_box_append(GTK_BOX(riga), l->campo);
    gtk_box_append(GTK_BOX(riga), l->cerca);
    gtk_box_append(GTK_BOX(l->dlg), riga);

    l->stato = etichetta("", "ph-desktop-nota");
    gtk_widget_set_visible(l->stato, FALSE);
    gtk_label_set_wrap(GTK_LABEL(l->stato), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(l->stato), 1);
    gtk_box_append(GTK_BOX(l->dlg), l->stato);

    l->elenco = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    l->scorri = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(l->scorri), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(l->scorri), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(l->scorri), l->elenco);
    gtk_widget_set_visible(l->scorri, FALSE);
    gtk_box_append(GTK_BOX(l->dlg), l->scorri);

    l->nessuna = gtk_button_new_with_label(ph_t("Nessuna"));
    gtk_widget_add_css_class(l->nessuna, "ph-ghost");
    gtk_widget_add_css_class(l->nessuna, "ph-bordato");
    gtk_widget_set_halign(l->nessuna, GTK_ALIGN_START);
    g_signal_connect(l->nessuna, "clicked", G_CALLBACK(su_nessuna), l);
    gtk_box_append(GTK_BOX(l->dlg), l->nessuna);

    /* Sotto l'elenco, sempre: l'attribuzione dei luoghi (GeoNames, CC BY 4.0). */
    GtkWidget *geo = gtk_button_new();
    gtk_widget_add_css_class(geo, "ph-desktop-meteo-attrib");
    GtkWidget *gl = etichetta(ph_t("Luoghi da GeoNames, tramite Open-Meteo.com"), "ph-desktop-meteo-attrib-testo");
    gtk_label_set_wrap(GTK_LABEL(gl), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(gl), 1);
    gtk_button_set_child(GTK_BUTTON(geo), gl);
    gtk_widget_set_halign(geo, GTK_ALIGN_START);
    ph_shell_etichetta(geo, ph_t("Luoghi da GeoNames, tramite Open-Meteo.com"), PH_METEO_GEO_SITO);
    g_signal_connect(geo, "clicked", G_CALLBACK(su_geonames), l);
    gtk_box_append(GTK_BOX(l->dlg), geo);

    ph_shell_strato_aggiungi(d->sh, PH_STRATO_PANNELLI, l->dlg);
    g_object_ref(l->dlg); /* vive finche' esiste il nostro ascolto (CONTRATTI.md: ordine di distruzione) */
    return l;
}

void ph_dsk_luogo_libera(PhDskLuogo *l)
{
    if (l == NULL)
        return;
    ph_dsk_wx_annulla(&l->annulla_ricerca); /* la funzione di ritorno non arrivera' piu' */
    ph_dsk_wx_annulla(&l->annulla_uri);
    g_object_unref(l->dlg);
    g_free(l);
}
