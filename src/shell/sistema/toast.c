/* Avvisi a comparsa (F-06): toast di una notifica nuova (5,2 s, il clic apre
 * l'app e nasconde la notifica), toast di informazione (3,2 s, il clic lo
 * chiude), e la notifica al desktop del PC quando la finestra non e' in primo
 * piano. Titoli e testi vivono solo in memoria: mai su disco, mai nei log. */
#include <string.h>

#include "shell/sistema/interno.h"

#define DURATA_NOTIFICA_MS 5200
#define DURATA_INFO_MS 3200
#define DURATA_INFO_LUNGO_MS 9000
#define MAX_TITOLO 80
#define MAX_TESTO 200

/* Etichetta di una riga di interfaccia che non puo' allargare il suo
 * contenitore: la larghezza la decide chi la contiene, il testo lungo va a
 * capo e poi finisce con "…" (testi ostili inclusi). */
static GtkWidget *etichetta_contenuta(const char *classe, int righe)
{
    GtkWidget *l = gtk_label_new("");
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

static void toast_chiudi(PhSistema *s)
{
    if (s->toast_timer != 0) {
        g_source_remove(s->toast_timer);
        s->toast_timer = 0;
    }
    g_clear_pointer(&s->toast_chiave, g_free);
    g_clear_pointer(&s->toast_pacchetto, g_free);
    s->toast_ora_ms = 0;
    if (s->toast != NULL)
        gtk_widget_set_visible(s->toast, FALSE);
}

static gboolean toast_scaduto(gpointer dati)
{
    PhSistema *s = dati;
    s->toast_timer = 0; /* la sorgente finisce con questo ritorno */
    toast_chiudi(s);
    return G_SOURCE_REMOVE;
}

static void toast_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    PhSistema *s = dati;
    /* Copie: chiudere il toast libera i valori. */
    char *chiave = g_strdup(s->toast_chiave);
    char *pacchetto = g_strdup(s->toast_pacchetto);
    uint64_t ora = s->toast_ora_ms;
    toast_chiudi(s);
    if (pacchetto != NULL) {
        if (chiave != NULL)
            ph_sistema_notifica_nascondi(s, chiave, ora);
        ph_shell_apri_app(s->sh, pacchetto);
    }
    g_free(chiave);
    g_free(pacchetto);
}

void ph_sistema_toast_inizia(PhSistema *s)
{
    s->toast = gtk_button_new();
    gtk_widget_add_css_class(s->toast, "ph-sistema-toast");
    /* Avviso a comparsa: vetro chiaro; 12 = border-radius di ph-sistema-toast nel CSS. */
    ph_shell_vetro_registra(s->sh, s->toast, 12, PH_VETRO_CHIARO);
    GtkWidget *colonna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    s->toast_titolo = etichetta_contenuta("ph-sistema-titolo", 1);
    s->toast_testo = etichetta_contenuta("ph-sistema-testo", 2);
    gtk_box_append(GTK_BOX(colonna), s->toast_titolo);
    gtk_box_append(GTK_BOX(colonna), s->toast_testo);
    gtk_button_set_child(GTK_BUTTON(s->toast), colonna);
    /* Mockup: a destra 16 px, 60 px dal basso, largo 300. */
    gtk_widget_set_halign(s->toast, GTK_ALIGN_END);
    gtk_widget_set_valign(s->toast, GTK_ALIGN_END);
    gtk_widget_set_margin_end(s->toast, 16);
    gtk_widget_set_margin_bottom(s->toast, 60);
    gtk_widget_set_size_request(s->toast, 300, -1);
    gtk_widget_set_visible(s->toast, FALSE);
    g_signal_connect(s->toast, "clicked", G_CALLBACK(toast_clic), s);
    ph_shell_strato_aggiungi(s->sh, PH_STRATO_TOAST, s->toast);
}

void ph_sistema_toast_ferma(PhSistema *s)
{
    if (s->toast_timer != 0) {
        g_source_remove(s->toast_timer);
        s->toast_timer = 0;
    }
}

/* Un toast nuovo sostituisce quello presente. chiave e pacchetto NULL = toast
 * di informazione. */
static void toast_mostra(PhSistema *s, const char *titolo, const char *testo, const char *pacchetto,
                         const char *chiave, uint64_t ora_ms, guint durata_ms)
{
    if (s->chiuso)
        return;
    char t[4 * MAX_TITOLO + 8], x[4 * MAX_TESTO + 8];
    ph_sistema_testo_sicuro(titolo, MAX_TITOLO, t, sizeof t);
    ph_sistema_testo_sicuro(testo, MAX_TESTO, x, sizeof x);
    gtk_label_set_text(GTK_LABEL(s->toast_titolo), t);
    gtk_label_set_text(GTK_LABEL(s->toast_testo), x);
    gtk_widget_set_visible(s->toast_testo, x[0] != '\0');

    if (s->toast_timer != 0)
        g_source_remove(s->toast_timer);
    g_free(s->toast_chiave);
    g_free(s->toast_pacchetto);
    s->toast_chiave = (pacchetto != NULL && pacchetto[0] != '\0') ? g_strdup(chiave) : NULL;
    s->toast_pacchetto = (pacchetto != NULL && pacchetto[0] != '\0') ? g_strdup(pacchetto) : NULL;
    s->toast_ora_ms = ora_ms;
    s->toast_timer = g_timeout_add(durata_ms, toast_scaduto, s);
    gtk_widget_set_visible(s->toast, TRUE);
}

void ph_sistema_toast_info(void *io, const char *titolo, const char *testo)
{
    toast_mostra(io, titolo, testo, NULL, NULL, 0, DURATA_INFO_MS);
}

void ph_sistema_toast_info_lungo(void *io, const char *titolo, const char *testo)
{
    toast_mostra(io, titolo, testo, NULL, NULL, 0, DURATA_INFO_LUNGO_MS);
}

void ph_sistema_toast_notifica(PhSistema *s, const PhSrvNotifica *n)
{
    toast_mostra(s, n->titolo, n->testo, n->pacchetto, n->chiave, n->ora_ms, DURATA_NOTIFICA_MS);
}

void ph_sistema_notifica_al_desktop(PhSistema *s, const PhSrvNotifica *n)
{
    const PhCfgPreferenze *pref = ph_srv_preferenze(s->srv);
    if (pref == NULL || !pref->avvisi)
        return;
    /* Solo se la finestra non e' sotto gli occhi dell'utente. */
    GtkWindow *finestra = ph_shell_finestra(s->sh);
    if (ph_shell_host_visibile(s->sh) && finestra != NULL && gtk_window_is_active(finestra))
        return;
    /* Nei demo non c'e' un'applicazione registrata: niente da fare. */
    GApplication *app = g_application_get_default();
    if (app == NULL || g_application_get_application_id(app) == NULL ||
        !g_application_get_is_registered(app))
        return;
    char t[4 * MAX_TITOLO + 8], x[4 * MAX_TESTO + 8];
    ph_sistema_testo_sicuro(n->titolo, MAX_TITOLO, t, sizeof t);
    ph_sistema_testo_sicuro(n->testo, MAX_TESTO, x, sizeof x);
    GNotification *g = g_notification_new(t[0] != '\0' ? t : "YASDE");
    if (x[0] != '\0')
        g_notification_set_body(g, x);
    g_application_send_notification(app, NULL, g);
    g_object_unref(g);
}
