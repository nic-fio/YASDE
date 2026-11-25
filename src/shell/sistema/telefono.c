/* Stato del telefono (F-07, 4.7): popup con la riga di stato, «Mostra schermo
 * del telefono» (apre o chiude la finestra dello specchio tramite il gestore
 * delle finestre) e «Pannello fisico». La pillola del pannello segue sempre lo
 * stato vero del telefono, mai il clic: il telefono puo' rifiutare lo
 * spegnimento (bloccato, chiamata in corso, ripristino impossibile da mettere
 * al sicuro) e il contratto non da' un esito, quindi il rifiuto si riconosce
 * dal mancato cambio entro PH_PANNELLO_ATTESA_MS. */
#include <string.h>

#include "shell/sistema/interno.h"

static GtkWidget *pillola_nuova(void)
{
    GtkWidget *p = gtk_label_new("");
    gtk_widget_add_css_class(p, "ph-pillola");
    gtk_widget_set_valign(p, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(p, GTK_ALIGN_END);
    return p;
}

static void pillola_imposta(GtkWidget *pillola, GtkWidget *pulsante, bool acceso, const char *testo)
{
    gtk_label_set_text(GTK_LABEL(pillola), testo);
    if (acceso)
        gtk_widget_add_css_class(pillola, "ph-acceso");
    else
        gtk_widget_remove_css_class(pillola, "ph-acceso");
    ph_shell_premuto(pulsante, acceso);
}

/* Chiude l'attesa senza dire niente: timer fermo, richiesta annullata (la sua
 * risposta non arrivera' piu'). */
static void attesa_chiudi(PhSistema *s)
{
    ph_pannello_annulla(&s->attesa);
    s->attesa_senza_esito = false;
    if (s->attesa_timer != 0) {
        g_source_remove(s->attesa_timer);
        s->attesa_timer = 0;
    }
    if (s->attesa_annulla != NULL) {
        g_cancellable_cancel(s->attesa_annulla);
        g_clear_object(&s->attesa_annulla);
    }
}

/* Dice che il telefono non ha eseguito la richiesta; il motivo e' quello vero
 * se i servizi lo danno, altrimenti una frase generica. */
static void rifiuto_avvisa(PhSistema *s, bool richiesto, PhSrvPannelloEsito esito, const char *frase)
{
    const char *titolo = richiesto ? ph_t("Il telefono non ha acceso il pannello fisico")
                                   : ph_t("Il telefono non ha spento il pannello fisico");
    const char *testo;
    if (frase != NULL && frase[0] != '\0')
        testo = frase;
    else
        switch (esito) {
        case PH_SRV_PAN_BLOCCATO: testo = ph_t("Il telefono è bloccato."); break;
        case PH_SRV_PAN_CHIAMATA: testo = ph_t("C'è una chiamata in corso."); break;
        case PH_SRV_PAN_RIPRISTINO_INCERTO:
            testo = ph_t("Non c'è un modo sicuro di ripristinarlo ora.");
            break;
        case PH_SRV_PAN_NON_DISPONIBILE: testo = ph_t("Il telefono non lo permette."); break;
        default:
            /* Nessun esito dal backend: solo il mancato cambio. */
            testo = richiesto ? ph_t("Riprova tra poco.")
                              : ph_t("Può essere bloccato, in chiamata o senza un modo sicuro di ripristinarlo.");
            break;
        }
    ph_sistema_toast_info(s, titolo, testo);
}

/* Esito vero dai servizi: sostituisce l'attesa a tempo (che resta solo per i
 * backend che non sanno dire l'esito). */
static void pannello_esito(PhServizi *srv, PhSrvPannelloEsito esito, const char *frase, void *utente)
{
    (void)srv;
    PhSistema *s = utente;
    if (s->chiuso || !s->attesa.in_attesa)
        return;
    bool richiesto = s->attesa.richiesto;
    if (esito == PH_SRV_PAN_SENZA_ESITO) {
        /* Il backend non sa dire l'esito: vale il mancato cambio entro il tempo. */
        s->attesa_senza_esito = true;
        return;
    }
    attesa_chiudi(s);
    if (esito != PH_SRV_PAN_FATTO && esito != PH_SRV_PAN_NON_COLLEGATO)
        rifiuto_avvisa(s, richiesto, esito, frase);
    ph_sistema_telefono_aggiorna(s);
}

/* Confronta lo stato vero con la richiesta in corso (solo per l'attesa a
 * tempo). Da chiamare a ogni cambio di stato e alla scadenza: l'ordine in cui
 * arrivano non conta. */
static void attesa_controlla(PhSistema *s)
{
    if (!s->attesa.in_attesa)
        return;
    PhSrvTelefono t;
    ph_srv_telefono(s->srv, &t);
    bool richiesto = s->attesa.richiesto;
    bool senza_esito = s->attesa_senza_esito;
    PhPannelloEsito e =
        ph_pannello_controlla(&s->attesa, t.pannello_noto && t.pannello_acceso, ph_shell_ora_ms());
    if (e == PH_PANNELLO_NIENTE)
        return;
    attesa_chiudi(s);
    /* Il rifiuto a tempo vale solo se il backend ha detto di non avere l'esito:
     * altrimenti la risposta e' solo lenta o persa, e non si accusa il telefono. */
    if (e == PH_PANNELLO_RIFIUTATO && senza_esito)
        rifiuto_avvisa(s, richiesto, PH_SRV_PAN_SENZA_ESITO, NULL);
}

static gboolean attesa_scaduta(gpointer dati)
{
    PhSistema *s = dati;
    attesa_controlla(s);
    if (s->attesa.in_attesa)
        return G_SOURCE_CONTINUE; /* il timer e' scattato un attimo prima del tempo */
    s->attesa_timer = 0;
    ph_sistema_telefono_aggiorna(s);
    return G_SOURCE_REMOVE;
}

void ph_sistema_telefono_aggiorna(PhSistema *s)
{
    PhSrvCollegamento c;
    PhSrvTelefono t;
    ph_srv_collegamento(s->srv, &c);
    /* Collegamento caduto durante la richiesta: nessun telefono da accusare. */
    if (!c.ha_telefono)
        attesa_chiudi(s);
    attesa_controlla(s);
    ph_srv_telefono(s->srv, &t);

    char riga[512];
    ph_sistema_riga_telefono(&c, &t, riga, sizeof riga);
    gtk_label_set_text(GTK_LABEL(s->riga_stato), riga);
    gtk_widget_set_visible(s->riga_stato, riga[0] != '\0');

    bool visibile = ph_shell_specchio_aperto(s->sh);
    pillola_imposta(s->pill_schermo, s->btn_schermo, visibile,
                    visibile ? ph_t("Visibile") : ph_t("Nascosto"));

    bool acceso = t.pannello_noto && t.pannello_acceso;
    pillola_imposta(s->pill_pannello, s->btn_pannello, acceso, acceso ? ph_t("Acceso") : ph_t("Spento"));
    gtk_widget_set_sensitive(s->btn_pannello,
                             c.ha_telefono && t.pannello_noto && !s->attesa.in_attesa);

    ph_sistema_pulsanti_aggiorna(s);
}

static void schermo_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    PhSistema *s = dati;
    ph_shell_specchio(s->sh, !ph_shell_specchio_aperto(s->sh));
    ph_shell_popup_chiudi(s->sh); /* come il mockup */
    ph_sistema_telefono_aggiorna(s);
}

static void pannello_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    PhSistema *s = dati;
    PhSrvTelefono t;
    ph_srv_telefono(s->srv, &t);
    if (s->attesa.in_attesa || !t.pannello_noto)
        return;
    bool richiesto = !t.pannello_acceso;
    ph_pannello_richiedi(&s->attesa, richiesto, ph_shell_ora_ms());
    if (s->attesa_timer != 0)
        g_source_remove(s->attesa_timer);
    s->attesa_timer = g_timeout_add(PH_PANNELLO_ATTESA_MS + 50, attesa_scaduta, s);
    g_clear_object(&s->attesa_annulla);
    s->attesa_annulla = g_cancellable_new();
    ph_srv_pannello_chiedi(s->srv, richiesto, s->attesa_annulla, pannello_esito, s);
    ph_sistema_telefono_aggiorna(s); /* il pulsante resta fermo finche' il telefono non risponde */
}

void ph_sistema_telefono_inizia(PhSistema *s)
{
    GtkWidget *p = ph_sistema_pannello_nuovo(s, 8, ph_t("Telefono"));
    s->pannello_telefono = p;
    ph_sistema_popup_colloca(p, 8, 56, 300);

    GtkWidget *titolo = gtk_label_new(ph_t("Telefono"));
    gtk_widget_add_css_class(titolo, "ph-titolo-pannello");
    gtk_widget_set_halign(titolo, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(p), titolo);

    s->riga_stato = gtk_label_new("");
    gtk_widget_add_css_class(s->riga_stato, "ph-piccolo");
    gtk_widget_add_css_class(s->riga_stato, "ph-testo-2");
    gtk_widget_set_halign(s->riga_stato, GTK_ALIGN_FILL);
    gtk_widget_set_margin_bottom(s->riga_stato, 4);
    gtk_label_set_xalign(GTK_LABEL(s->riga_stato), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(s->riga_stato), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(s->riga_stato), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_width_chars(GTK_LABEL(s->riga_stato), 1);
    gtk_label_set_max_width_chars(GTK_LABEL(s->riga_stato), 1);
    gtk_box_append(GTK_BOX(p), s->riga_stato);

    /* «Mostra schermo del telefono» + pillola. */
    s->btn_schermo = gtk_button_new();
    gtk_widget_add_css_class(s->btn_schermo, "ph-pbtn");
    gtk_widget_add_css_class(s->btn_schermo, "ph-sistema-pbtn");
    GtkWidget *r1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *t1 = gtk_label_new(ph_t("Mostra schermo del telefono"));
    gtk_widget_set_hexpand(t1, TRUE);
    gtk_widget_set_halign(t1, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(t1), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(t1), TRUE);
    gtk_box_append(GTK_BOX(r1), t1);
    s->pill_schermo = pillola_nuova();
    gtk_box_append(GTK_BOX(r1), s->pill_schermo);
    gtk_button_set_child(GTK_BUTTON(s->btn_schermo), r1);
    g_signal_connect(s->btn_schermo, "clicked", G_CALLBACK(schermo_clic), s);
    gtk_box_append(GTK_BOX(p), s->btn_schermo);

    /* «Pannello fisico» con la nota sotto + pillola. */
    s->btn_pannello = gtk_button_new();
    gtk_widget_add_css_class(s->btn_pannello, "ph-pbtn");
    gtk_widget_add_css_class(s->btn_pannello, "ph-sistema-pbtn");
    GtkWidget *r2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(col, TRUE);
    gtk_widget_set_halign(col, GTK_ALIGN_START);
    GtkWidget *t2 = gtk_label_new(ph_t("Pannello fisico"));
    gtk_widget_set_halign(t2, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(col), t2);
    GtkWidget *nota = gtk_label_new(ph_t("Si spegne da solo mentre usi il PC"));
    gtk_widget_add_css_class(nota, "ph-minimo");
    gtk_widget_add_css_class(nota, "ph-testo-3");
    gtk_widget_set_halign(nota, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(nota), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(nota), TRUE);
    gtk_box_append(GTK_BOX(col), nota);
    gtk_box_append(GTK_BOX(r2), col);
    s->pill_pannello = pillola_nuova();
    gtk_box_append(GTK_BOX(r2), s->pill_pannello);
    gtk_button_set_child(GTK_BUTTON(s->btn_pannello), r2);
    g_signal_connect(s->btn_pannello, "clicked", G_CALLBACK(pannello_clic), s);
    gtk_box_append(GTK_BOX(p), s->btn_pannello);

    ph_sistema_telefono_aggiorna(s);
}

void ph_sistema_telefono_ferma(PhSistema *s) { attesa_chiudi(s); }

void ph_sistema_telefono_alterna(PhSistema *s)
{
    ph_sistema_telefono_aggiorna(s);
    ph_shell_popup_alterna(s->sh, PH_POPUP_TELEFONO, s->pannello_telefono, NULL, NULL);
}
