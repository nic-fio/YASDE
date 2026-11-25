/* Notifiche (F-06): pannello, contatore della campanella, "Nascondi" e
 * "Nascondi tutte" (valgono solo nella shell), toast per le notifiche davvero
 * nuove. La logica (nascoste, nuove, testi) e' in logica.c.
 *
 * Titoli e testi vivono solo in memoria: mai su disco, mai nei log. */
#include <string.h>

#include "shell/sistema/interno.h"

#define MAX_VOCI 100  /* righe disegnate al massimo (il contatore dice il numero vero) */
#define MAX_TITOLO 80
#define MAX_TESTO 200
#define MAX_APP 60

/* Una voce del pannello: i dati che servono al clic, copiati (le fotografie dei
 * servizi valgono solo fino al ritorno al ciclo principale). */
typedef struct {
    PhSistema *s;
    char *chiave;
    char *pacchetto;
    uint64_t ora_ms;
    size_t indice; /* posizione nell'elenco (per il fuoco dopo "Nascondi") */
    GtkWidget *tile; /* contenitore 30x30 dell'icona */
} Voce;

static void voce_libera(gpointer p)
{
    Voce *v = p;
    g_free(v->chiave);
    g_free(v->pacchetto);
    g_free(v);
}

static GtkWidget *etichetta_contenuta(const char *classe, int righe)
{
    GtkWidget *l = gtk_label_new("");
    if (classe != NULL)
        gtk_widget_add_css_class(l, classe);
    gtk_widget_set_halign(l, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(l, TRUE);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(l), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    gtk_label_set_lines(GTK_LABEL(l), righe);
    gtk_label_set_width_chars(GTK_LABEL(l), 1);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 1);
    return l;
}

/* ---- Servizi verso il resto della shell ---------------------------------- */

bool ph_sistema_notifica_nascosta(void *io, const char *chiave, uint64_t ora_ms)
{
    PhSistema *s = io;
    return ph_nascoste_e_nascosta(s->nascoste, chiave, ora_ms);
}

void ph_sistema_notifica_nascondi(PhSistema *s, const char *chiave, uint64_t ora_ms)
{
    if (ph_nascoste_nascondi(s->nascoste, chiave, ora_ms))
        ph_shell_emetti(s->sh, PH_SHELL_EV_NASCOSTE);
    ph_sistema_notifiche_ridisegna(s);
}

/* ---- Voci ---------------------------------------------------------------------- */

static void icona_nella_tile(PhSistema *s, GtkWidget *tile, const char *pacchetto, const char *nome)
{
    GtkWidget *vecchio;
    while ((vecchio = gtk_widget_get_first_child(tile)) != NULL)
        gtk_box_remove(GTK_BOX(tile), vecchio);
    GdkTexture *t = pacchetto != NULL ? ph_srv_icona(s->srv, pacchetto) : NULL;
    GtkWidget *figlio;
    if (t != NULL) {
        /* Misura fissa: una texture da 128 px non deve dettare la misura. */
        figlio = gtk_image_new_from_paintable(GDK_PAINTABLE(t));
        gtk_image_set_pixel_size(GTK_IMAGE(figlio), 30);
    } else {
        /* Come nel mockup: quadrato colorato con le iniziali. */
        figlio = ph_shell_segnaposto_nuovo(pacchetto != NULL ? pacchetto : "", nome, 30, 8);
    }
    gtk_box_append(GTK_BOX(tile), figlio);
}

static void voce_apri(GtkButton *b, gpointer dati)
{
    (void)b;
    Voce *v = dati;
    /* Copie: nascondere ridisegna l'elenco e libera questa voce. */
    PhSistema *s = v->s;
    char *chiave = g_strdup(v->chiave);
    char *pacchetto = g_strdup(v->pacchetto);
    uint64_t ora = v->ora_ms;
    ph_sistema_notifica_nascondi(s, chiave, ora);
    if (pacchetto != NULL && pacchetto[0] != '\0')
        ph_shell_apri_app(s->sh, pacchetto);
    ph_shell_popup_chiudi(s->sh);
    g_free(chiave);
    g_free(pacchetto);
}

static void voce_nascondi(GtkButton *b, gpointer dati)
{
    (void)b;
    Voce *v = dati;
    PhSistema *s = v->s;
    char *chiave = g_strdup(v->chiave);
    uint64_t ora = v->ora_ms;
    size_t indice = v->indice;
    ph_sistema_notifica_nascondi(s, chiave, ora);
    g_free(chiave);
    /* La voce sparisce con il suo pulsante: il fuoco va alla voce che ne
     * prende il posto (o all'ultima), cosi' la tastiera non si perde. */
    GtkWidget *riga = gtk_widget_get_first_child(s->elenco_notifiche);
    for (size_t i = 0; riga != NULL && i < indice; i++) {
        GtkWidget *prossima = gtk_widget_get_next_sibling(riga);
        if (prossima == NULL)
            break;
        riga = prossima;
    }
    GtkWidget *pulsante = riga != NULL ? gtk_widget_get_last_child(riga) : NULL;
    if (pulsante != NULL && GTK_IS_BUTTON(pulsante))
        gtk_widget_grab_focus(pulsante);
    else if (gtk_widget_get_sensitive(s->btn_nascondi_tutte))
        gtk_widget_grab_focus(s->btn_nascondi_tutte);
}

static GtkWidget *voce_nuova(PhSistema *s, const PhSrvNotifica *n, size_t indice)
{
    Voce *v = g_new0(Voce, 1);
    v->s = s;
    v->chiave = g_strdup(n->chiave);
    v->pacchetto = g_strdup(n->pacchetto);
    v->ora_ms = n->ora_ms;
    v->indice = indice;

    const PhSrvApp *app = n->pacchetto != NULL ? ph_srv_app_cerca(s->srv, n->pacchetto) : NULL;
    char nome[4 * MAX_APP + 8];
    ph_sistema_testo_sicuro(app != NULL && app->nome != NULL ? app->nome : n->pacchetto, MAX_APP,
                            nome, sizeof nome);

    GtkWidget *riga = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(riga, "ph-voce");
    gtk_widget_set_valign(riga, GTK_ALIGN_START);
    g_object_set_data_full(G_OBJECT(riga), "ph-sistema-voce", v, voce_libera);

    v->tile = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(v->tile, "ph-sistema-tile");
    gtk_widget_set_valign(v->tile, GTK_ALIGN_START);
    gtk_widget_set_overflow(v->tile, GTK_OVERFLOW_HIDDEN);
    icona_nella_tile(s, v->tile, n->pacchetto, nome);
    gtk_box_append(GTK_BOX(riga), v->tile);

    /* Pulsante largo che apre l'app: titolo, testo, "{app} · {ora}". */
    GtkWidget *apri = gtk_button_new();
    gtk_widget_add_css_class(apri, "ph-sistema-notifica");
    gtk_widget_set_hexpand(apri, TRUE);
    gtk_widget_set_halign(apri, GTK_ALIGN_FILL);
    gtk_widget_set_valign(apri, GTK_ALIGN_START);
    GtkWidget *colonna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    char t[4 * MAX_TITOLO + 8], x[4 * MAX_TESTO + 8], sotto[512];
    ph_sistema_testo_sicuro(n->titolo, MAX_TITOLO, t, sizeof t);
    ph_sistema_testo_sicuro(n->testo, MAX_TESTO, x, sizeof x);
    GTimeZone *fuso = g_time_zone_new_local();
    ph_sistema_app_ora(nome, n->ora_ms, fuso, sotto, sizeof sotto);
    g_time_zone_unref(fuso);
    GtkWidget *l_titolo = etichetta_contenuta("ph-sistema-titolo", 2);
    gtk_label_set_text(GTK_LABEL(l_titolo), t);
    gtk_box_append(GTK_BOX(colonna), l_titolo);
    if (x[0] != '\0') {
        GtkWidget *l_testo = etichetta_contenuta("ph-sistema-testo", 3);
        gtk_label_set_text(GTK_LABEL(l_testo), x);
        gtk_box_append(GTK_BOX(colonna), l_testo);
    }
    GtkWidget *l_app = etichetta_contenuta("ph-sistema-app", 1);
    gtk_label_set_text(GTK_LABEL(l_app), sotto);
    gtk_box_append(GTK_BOX(colonna), l_app);
    gtk_button_set_child(GTK_BUTTON(apri), colonna);
    char etichetta[512];
    if (ph_lingua_componi(etichetta, sizeof etichetta, ph_t("Apri {nome}"), "nome", nome,
                          (const char *)NULL) == (size_t)-1)
        g_strlcpy(etichetta, nome, sizeof etichetta);
    ph_shell_etichetta(apri, etichetta, NULL);
    g_signal_connect(apri, "clicked", G_CALLBACK(voce_apri), v);
    gtk_box_append(GTK_BOX(riga), apri);

    GtkWidget *nascondi = gtk_button_new_with_label(ph_t("Nascondi"));
    gtk_widget_add_css_class(nascondi, "ph-ghost");
    gtk_widget_set_valign(nascondi, GTK_ALIGN_START);
    ph_shell_etichetta(nascondi, ph_t("Nascondi la notifica"), NULL);
    g_signal_connect(nascondi, "clicked", G_CALLBACK(voce_nascondi), v);
    gtk_box_append(GTK_BOX(riga), nascondi);
    return riga;
}

/* ---- Elenco ------------------------------------------------------------------------ */

static void elenco_svuota(PhSistema *s)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(s->elenco_notifiche)) != NULL)
        gtk_box_remove(GTK_BOX(s->elenco_notifiche), c);
}

static GtkWidget *messaggio_nuovo(const char *testo)
{
    char sicuro[4 * MAX_TESTO + 8];
    ph_sistema_testo_sicuro(testo, MAX_TESTO, sicuro, sizeof sicuro);
    GtkWidget *l = etichetta_contenuta("ph-sistema-vuoto", 4);
    gtk_label_set_text(GTK_LABEL(l), sicuro);
    return l;
}

/* Cio' che l'elenco mostrerebbe, in una stringa: se non e' cambiata non si
 * ricostruisce (il fuoco e i clic in corso restano dove sono). */
static char *firma_calcola(PhSistema *s, const PhSrvNotifica *v, size_t n, const char *problema)
{
    GString *g = g_string_new(problema != NULL ? problema : "");
    for (size_t i = 0; i < n; i++) {
        if (ph_nascoste_e_nascosta(s->nascoste, v[i].chiave, v[i].ora_ms))
            continue;
        const PhSrvApp *app = v[i].pacchetto != NULL ? ph_srv_app_cerca(s->srv, v[i].pacchetto) : NULL;
        g_string_append_printf(g, "\x1f%s\x1e%s\x1e%s\x1e%s\x1e%s\x1e%" G_GUINT64_FORMAT,
                               v[i].chiave != NULL ? v[i].chiave : "",
                               v[i].pacchetto != NULL ? v[i].pacchetto : "",
                               v[i].titolo != NULL ? v[i].titolo : "", v[i].testo != NULL ? v[i].testo : "",
                               app != NULL && app->nome != NULL ? app->nome : "", v[i].ora_ms);
    }
    return g_string_free(g, FALSE);
}

static void ridisegna(PhSistema *s, bool forza)
{
    size_t n = 0;
    const char *problema = NULL;
    const PhSrvNotifica *v = ph_srv_notifiche(s->srv, &n, &problema);
    size_t visibili = ph_nascoste_visibili(s->nascoste, v, n);
    bool contatore_cambiato = visibili != s->visibili;
    s->visibili = visibili;

    if (!forza && !gtk_widget_get_visible(s->pannello_notifiche)) {
        /* Il pannello e' chiuso: si ricostruisce quando si apre. */
        g_clear_pointer(&s->firma_notifiche, g_free);
    } else {
        char *firma = firma_calcola(s, v, n, problema);
        if (s->firma_notifiche != NULL && strcmp(firma, s->firma_notifiche) == 0) {
            g_free(firma);
        } else {
            g_free(s->firma_notifiche);
            s->firma_notifiche = firma;
            elenco_svuota(s);
            if (problema != NULL) {
                gtk_box_append(GTK_BOX(s->elenco_notifiche), messaggio_nuovo(problema));
            } else if (visibili == 0) {
                gtk_box_append(GTK_BOX(s->elenco_notifiche), messaggio_nuovo(ph_t("Nessuna notifica")));
            } else {
                size_t disegnate = 0;
                for (size_t i = 0; i < n && disegnate < MAX_VOCI; i++) {
                    if (ph_nascoste_e_nascosta(s->nascoste, v[i].chiave, v[i].ora_ms))
                        continue;
                    gtk_box_append(GTK_BOX(s->elenco_notifiche), voce_nuova(s, &v[i], disegnate));
                    disegnate++;
                }
            }
        }
    }
    gtk_widget_set_sensitive(s->btn_nascondi_tutte, visibili > 0);
    if (contatore_cambiato)
        ph_sistema_pulsanti_aggiorna(s);
}

void ph_sistema_notifiche_ridisegna(PhSistema *s) { ridisegna(s, false); }

void ph_sistema_notifiche_icona(PhSistema *s, const char *pacchetto)
{
    if (pacchetto == NULL)
        return;
    if (!gtk_widget_get_visible(s->pannello_notifiche)) {
        /* Pannello chiuso: all'apertura si ricostruisce con l'icona arrivata. */
        g_clear_pointer(&s->firma_notifiche, g_free);
        return;
    }
    for (GtkWidget *r = gtk_widget_get_first_child(s->elenco_notifiche); r != NULL;
         r = gtk_widget_get_next_sibling(r)) {
        Voce *v = g_object_get_data(G_OBJECT(r), "ph-sistema-voce");
        if (v == NULL || v->pacchetto == NULL || strcmp(v->pacchetto, pacchetto) != 0)
            continue;
        const PhSrvApp *app = ph_srv_app_cerca(s->srv, pacchetto);
        char nome[4 * MAX_APP + 8];
        ph_sistema_testo_sicuro(app != NULL && app->nome != NULL ? app->nome : pacchetto, MAX_APP, nome,
                                sizeof nome);
        icona_nella_tile(s, v->tile, pacchetto, nome);
    }
}

/* ---- Eventi ------------------------------------------------------------------------------ */

void ph_sistema_notifiche_profilo(PhSistema *s)
{
    /* Il profilo arriva prima delle notifiche nuove: non e' un evento delle
     * viste (non azzera, non registra). Le nascoste di notifiche sparite le
     * dimentica la sincronizzazione al prossimo elenco. */
    ph_sistema_notifiche_ridisegna(s);
}

void ph_sistema_notifiche_cambiate(PhSistema *s)
{
    size_t n = 0;
    const char *problema = NULL;
    const PhSrvNotifica *v = ph_srv_notifiche(s->srv, &n, &problema);
    bool leggibile = problema == NULL;

    /* Una nascosta che il telefono ha aggiornato (ora diversa) torna visibile;
     * le nascoste di notifiche sparite si dimenticano. */
    if (ph_nascoste_sincronizza(s->nascoste, v, n, leggibile))
        ph_shell_emetti(s->sh, PH_SHELL_EV_NASCOSTE);

    size_t indice = SIZE_MAX;
    size_t nuove = ph_viste_registra(s->viste, v, n, leggibile, ph_srv_notifiche_generazione(s->srv),
                                    ph_srv_notifiche_pronte(s->srv), &indice);

    ridisegna(s, false);

    if (nuove > 0 && indice < n) {
        ph_sistema_toast_notifica(s, &v[indice]);
        ph_sistema_notifica_al_desktop(s, &v[indice]);
    }
}

/* ---- Pannello ------------------------------------------------------------------------------- */

static void nascondi_tutte(GtkButton *b, gpointer dati)
{
    (void)b;
    PhSistema *s = dati;
    size_t n = 0;
    const PhSrvNotifica *v = ph_srv_notifiche(s->srv, &n, NULL);
    if (ph_nascoste_nascondi_tutte(s->nascoste, v, n))
        ph_shell_emetti(s->sh, PH_SHELL_EV_NASCOSTE);
    ridisegna(s, false);
}

void ph_sistema_notifiche_inizia(PhSistema *s)
{
    GtkWidget *p = ph_sistema_pannello_nuovo(s, 10, ph_t("Notifiche"));
    s->pannello_notifiche = p;
    ph_sistema_popup_colloca(p, 8, 56, 340);

    GtkWidget *testa = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *titolo = gtk_label_new(ph_t("Notifiche"));
    gtk_widget_add_css_class(titolo, "ph-titolo-pannello");
    gtk_widget_set_hexpand(titolo, TRUE);
    gtk_widget_set_halign(titolo, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(testa), titolo);
    s->btn_nascondi_tutte = gtk_button_new_with_label(ph_t("Nascondi tutte"));
    gtk_widget_add_css_class(s->btn_nascondi_tutte, "ph-ghost");
    g_signal_connect(s->btn_nascondi_tutte, "clicked", G_CALLBACK(nascondi_tutte), s);
    gtk_box_append(GTK_BOX(testa), s->btn_nascondi_tutte);
    gtk_box_append(GTK_BOX(p), testa);

    /* Mockup: altezza massima 460 con scorrimento; 388 = 460 meno bordi,
     * imbottitura, intestazione e spazio. */
    s->elenco_notifiche = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *scorri = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scorri), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scorri), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scorri), 388);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scorri), s->elenco_notifiche);
    gtk_box_append(GTK_BOX(p), scorri);


    /* Apertura con la prima lettura gia' fatta: la fotografia della generazione
     * attuale fa da base (niente toast per le notifiche gia' presenti). */
    if (ph_srv_notifiche_pronte(s->srv)) {
        size_t n = 0;
        const char *problema = NULL;
        const PhSrvNotifica *v = ph_srv_notifiche(s->srv, &n, &problema);
        ph_viste_registra(s->viste, v, n, problema == NULL, ph_srv_notifiche_generazione(s->srv), true,
                          NULL);
    }
    ridisegna(s, false);
}

void ph_sistema_notifiche_ferma(PhSistema *s) { (void)s; }

void ph_sistema_notifiche_alterna(PhSistema *s)
{
    ridisegna(s, true);
    ph_shell_popup_alterna(s->sh, PH_POPUP_NOTIFICHE, s->pannello_notifiche, NULL, NULL);
}
