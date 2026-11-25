/* demo-trasferimento: il solo pezzo trasferimento sopra il telefono finto. Gli altri pezzi
 * non ci sono: qui un toast finto mostra gli avvisi (ph_shell_avviso) e due
 * pulsanti in basso a destra aprono i pannelli, come farebbe la barra. Il
 * telefono finto ha l'albero del mockup (con "pippo", /data non consentita) e
 * fa avanzare i trasferimenti senza scrivere nulla sul PC. Il trascinamento di
 * file dal gestore file del PC sulla finestra si prova per davvero. */
#include "shell/finto/finto.h"
#include "shell/pezzi.h"
#include "shell/trasferimento/prova.h"

typedef struct {
    GtkWidget *toast, *titolo, *testo;
    guint scade;
} Toast;

static gboolean toast_nascondi(gpointer p)
{
    Toast *t = p;
    t->scade = 0;
    gtk_widget_set_visible(t->toast, FALSE);
    return G_SOURCE_REMOVE;
}

/* Gestore finto degli avvisi: una scheda in alto a destra per 3 secondi. */
static void toast_mostra(void *io, const char *titolo, const char *testo)
{
    Toast *t = io;
    gtk_label_set_text(GTK_LABEL(t->titolo), titolo);
    gtk_label_set_text(GTK_LABEL(t->testo), testo);
    gtk_widget_set_visible(t->toast, TRUE);
    if (t->scade != 0)
        g_source_remove(t->scade);
    t->scade = g_timeout_add(3000, toast_nascondi, t);
}

static void clic_invia(GtkButton *b, gpointer sh)
{
    (void)b;
    ph_shell_trasferimento_apri(sh, PH_SRV_INVIO);
}

static void clic_scarica(GtkButton *b, gpointer sh)
{
    (void)b;
    ph_shell_trasferimento_apri(sh, PH_SRV_RICEZIONE);
}

static GtkWidget *pulsante(PhShell *sh, const PhShellIcona *icona, const char *nome, int margine,
                           GCallback cb)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "ph-tb");
    gtk_button_set_child(GTK_BUTTON(b), ph_shell_icona_nuova(icona));
    ph_shell_etichetta(b, nome, nome);
    gtk_widget_set_halign(b, GTK_ALIGN_END);
    gtk_widget_set_valign(b, GTK_ALIGN_END);
    gtk_widget_set_margin_end(b, margine);
    gtk_widget_set_margin_bottom(b, 4);
    g_signal_connect(b, "clicked", cb, sh);
    ph_shell_strato_aggiungi(sh, PH_STRATO_BARRA, b);
    return b;
}

/* Con PH_DEMO_SEQ=1 il demo mostra da solo, a tempo, i pannelli e gli stati (per le foto). */
static gboolean passo_demo(gpointer sh)
{
    static int passo = 0;
    ph_trasferimento_prova(sh, passo++);
    return passo < 12 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

static void installa(PhShell *sh, PhFinto *f, void *u)
{
    (void)f;
    (void)u;
    ph_trasferimento_installa(sh);
    pulsante(sh, &PH_ICONA_INVIA, "Invia al telefono", 150, G_CALLBACK(clic_invia));
    pulsante(sh, &PH_ICONA_SCARICA, "Scarica dal telefono", 100, G_CALLBACK(clic_scarica));

    static Toast t;
    t.toast = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(t.toast, "ph-pannello");
    gtk_widget_set_halign(t.toast, GTK_ALIGN_END);
    gtk_widget_set_valign(t.toast, GTK_ALIGN_START);
    gtk_widget_set_margin_end(t.toast, 16);
    gtk_widget_set_margin_top(t.toast, 16);
    t.titolo = gtk_label_new("");
    gtk_widget_add_css_class(t.titolo, "ph-titolo-pannello");
    t.testo = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(t.testo), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(t.testo), 40);
    gtk_box_append(GTK_BOX(t.toast), t.titolo);
    gtk_box_append(GTK_BOX(t.toast), t.testo);
    gtk_widget_set_visible(t.toast, FALSE);
    ph_shell_strato_aggiungi(sh, PH_STRATO_TOAST, t.toast);
    PhShellGestoreAvvisi ga = {.io = &t, .avviso = toast_mostra};
    ph_shell_registra_avvisi(sh, &ga);
    if (g_getenv("PH_DEMO_SEQ") != NULL)
        g_timeout_add(g_strcmp0(g_getenv("PH_DEMO_SEQ"), "veloce") == 0 ? 500 : 2000, passo_demo, sh);
}

int main(int argc, char **argv)
{
    return ph_finto_demo(argc, argv, "YASDE 2.1 - demo trasferimento", installa, NULL);
}
