/* Conferma dell'installazione di un APK trascinato (specifica 5.9.1). */
#include "shell/trasferimento/apk.h"

#include "config/lingua.h"
#include "shell/trasferimento/percorsi.h"

typedef struct {
    PhTrfApkFatto fatto;
    void *utente;
    char *percorso;
} Chiesta;

static void scelta(GObject *src, GAsyncResult *res, gpointer dati)
{
    Chiesta *c = dati;
    GError *err = NULL;
    int b = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(src), res, &err);
    g_clear_error(&err);
    /* Pulsanti: 0 = Annulla, 1 = Installa; chiusura o errore = niente. */
    c->fatto(c->utente, b == 1, c->percorso);
    g_free(c->percorso);
    g_free(c);
}

void ph_trf_apk_chiedi(GtkWindow *finestra, const char *percorso, PhTrfApkFatto fatto, void *utente)
{
    Chiesta *c = g_new0(Chiesta, 1);
    c->fatto = fatto;
    c->utente = utente;
    c->percorso = g_strdup(percorso);

    char *base = g_path_get_basename(percorso);
    char *nome = ph_trf_testo_sicuro(base, 255);
    char titolo[600];
    ph_lingua_componi(titolo, sizeof titolo, ph_t("Installare l'app {nome} sul telefono?"), "nome", nome, NULL);
    g_free(base);
    g_free(nome);

    GtkAlertDialog *d = gtk_alert_dialog_new("%s", titolo);
    gtk_alert_dialog_set_detail(d, ph_t("Il file è un'app: non lo invio come file ma la installo. Se l'app è già sul telefono viene sostituita e conserva i suoi dati."));
    const char *pulsanti[] = {ph_t("Annulla"), ph_t("Installa l'app"), NULL};
    gtk_alert_dialog_set_buttons(d, pulsanti);
    gtk_alert_dialog_set_cancel_button(d, 0);
    gtk_alert_dialog_set_default_button(d, 1);
    gtk_alert_dialog_set_modal(d, TRUE);
    gtk_alert_dialog_choose(d, finestra, NULL, scelta, c);
    g_object_unref(d);
}
