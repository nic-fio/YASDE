/* Volume (F-08, 5.8): guadagno dell'audio di YASDE sul PC. Non tocca il
 * volume del telefono e il muto non ferma la cattura: e' solo il guadagno che
 * il backend applica. Lo stato vero torna con PH_SRV_EV_VOLUME; mentre
 * l'utente muove il cursore gli avvisi non lo spostano (a cursore fermo ci si
 * riallinea) e l'ultimo livello va subito nel profilo, il muto no. */
#include <math.h>
#include <string.h>

#include "shell/sistema/interno.h"

/* Silenzio del cursore prima di riallinearsi allo stato vero. */
#define VOLUME_QUIETE_MS 600

static void volume_applica(PhSistema *s)
{
    char testo[64];
    ph_sistema_volume_testo(s->volume, s->muto, testo, sizeof testo);
    gtk_label_set_text(GTK_LABEL(s->volume_valore), testo);

    s->in_aggiornamento = true;
    gtk_range_set_value(GTK_RANGE(s->volume_cursore), s->volume);
    s->in_aggiornamento = false;

    /* Come il mockup: la pillola dice "Attivo" solo con il muto vero, il
     * pulsante risulta premuto anche a volume 0. */
    gtk_label_set_text(GTK_LABEL(s->pill_muto), s->muto ? ph_t("Attivo") : ph_t("Disattivo"));
    if (s->muto)
        gtk_widget_add_css_class(s->pill_muto, "ph-acceso");
    else
        gtk_widget_remove_css_class(s->pill_muto, "ph-acceso");
    ph_shell_premuto(s->btn_muto, ph_sistema_volume_muto_mostrato(s->volume, s->muto));

    ph_sistema_pulsanti_aggiorna(s);
}

static void volume_dai_servizi(PhSistema *s)
{
    PhSrvVolume v;
    ph_srv_volume(s->srv, &v);
    s->volume = ph_sistema_volume_limita(v.volume);
    s->muto = v.muto;
    volume_applica(s);
}

static gboolean volume_quiete(gpointer dati)
{
    PhSistema *s = dati;
    s->volume_timer = 0;
    volume_dai_servizi(s); /* da qui lo stato vero torna a comandare */
    return G_SOURCE_REMOVE;
}

static void volume_arma(PhSistema *s)
{
    if (s->volume_timer != 0)
        g_source_remove(s->volume_timer);
    s->volume_timer = g_timeout_add(VOLUME_QUIETE_MS, volume_quiete, s);
}

void ph_sistema_volume_aggiorna(PhSistema *s)
{
    /* Mentre l'utente agisce lo stato che arriva puo' essere vecchio: si
     * aspetta la quiete (volume_quiete rilegge). */
    if (s->volume_timer != 0)
        return;
    volume_dai_servizi(s);
}

static void cursore_mosso(GtkRange *r, gpointer dati)
{
    PhSistema *s = dati;
    if (s->in_aggiornamento)
        return;
    int v = ph_sistema_volume_limita((int)lround(gtk_range_get_value(r)));
    s->volume = v;
    /* Muovere il cursore toglie il muto (mockup onVol). */
    if (s->muto) {
        s->muto = false;
        ph_srv_imposta_muto(s->srv, false);
    }
    ph_srv_imposta_volume(s->srv, v);
    /* L'ultimo livello va subito nel profilo: ph_srv_profilo_salva raggruppa i
     * salvataggi (su disco una volta sola) e la chiusura dei servizi scrive cio'
     * che resta in sospeso. Cosi' alla chiusura di questo pezzo non serve toccare
     * i servizi. */
    PhCfgTelefono *profilo = ph_srv_profilo(s->srv);
    if (profilo != NULL && ph_sistema_volume_nel_profilo(profilo, v))
        ph_srv_profilo_salva(s->srv);
    volume_arma(s);
    volume_applica(s);
}

static void muto_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    PhSistema *s = dati;
    s->muto = !s->muto;
    ph_srv_imposta_muto(s->srv, s->muto);
    volume_arma(s);
    volume_applica(s);
}

void ph_sistema_volume_inizia(PhSistema *s)
{
    GtkWidget *p = ph_sistema_pannello_nuovo(s, 10, ph_t("Volume"));
    s->pannello_volume = p;
    ph_sistema_popup_colloca(p, 135, 56, 250);

    GtkWidget *testa = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *titolo = gtk_label_new(ph_t("Volume"));
    gtk_widget_add_css_class(titolo, "ph-titolo-pannello");
    gtk_widget_set_hexpand(titolo, TRUE);
    gtk_widget_set_halign(titolo, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(testa), titolo);
    s->volume_valore = gtk_label_new("");
    gtk_widget_add_css_class(s->volume_valore, "ph-piccolo");
    gtk_widget_add_css_class(s->volume_valore, "ph-testo-2");
    gtk_box_append(GTK_BOX(testa), s->volume_valore);
    gtk_box_append(GTK_BOX(p), testa);

    s->volume_cursore = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_widget_add_css_class(s->volume_cursore, "ph-sistema-cursore");
    gtk_scale_set_draw_value(GTK_SCALE(s->volume_cursore), FALSE);
    gtk_widget_set_hexpand(s->volume_cursore, TRUE);
    ph_shell_etichetta(s->volume_cursore, ph_t("Volume di YASDE"), NULL);
    g_signal_connect(s->volume_cursore, "value-changed", G_CALLBACK(cursore_mosso), s);
    gtk_box_append(GTK_BOX(p), s->volume_cursore);

    s->btn_muto = gtk_button_new();
    gtk_widget_add_css_class(s->btn_muto, "ph-pbtn");
    gtk_widget_add_css_class(s->btn_muto, "ph-sistema-pbtn");
    GtkWidget *riga = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *t = gtk_label_new(ph_t("Muto"));
    gtk_widget_set_hexpand(t, TRUE);
    gtk_widget_set_halign(t, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(riga), t);
    s->pill_muto = gtk_label_new("");
    gtk_widget_add_css_class(s->pill_muto, "ph-pillola");
    gtk_widget_set_valign(s->pill_muto, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(riga), s->pill_muto);
    gtk_button_set_child(GTK_BUTTON(s->btn_muto), riga);
    g_signal_connect(s->btn_muto, "clicked", G_CALLBACK(muto_clic), s);
    gtk_box_append(GTK_BOX(p), s->btn_muto);

    GtkWidget *nota = gtk_label_new(
        ph_t("Regola l'audio di YASDE sul PC. Il volume del telefono non cambia."));
    gtk_widget_add_css_class(nota, "ph-sistema-nota");
    gtk_widget_set_halign(nota, GTK_ALIGN_FILL);
    gtk_label_set_xalign(GTK_LABEL(nota), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(nota), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(nota), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_width_chars(GTK_LABEL(nota), 1);
    gtk_label_set_max_width_chars(GTK_LABEL(nota), 1);
    gtk_box_append(GTK_BOX(p), nota);

    volume_dai_servizi(s);
}

void ph_sistema_volume_ferma(PhSistema *s)
{
    /* Nel "destroy" della radice solo timer: niente servizi (CONTRATTI.md). */
    if (s->volume_timer != 0) {
        g_source_remove(s->volume_timer);
        s->volume_timer = 0;
    }
}

void ph_sistema_volume_alterna(PhSistema *s)
{
    ph_shell_popup_alterna(s->sh, PH_POPUP_VOLUME, s->pannello_volume, NULL, NULL);
}
