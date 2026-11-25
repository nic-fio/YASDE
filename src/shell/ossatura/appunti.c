/* Appunti condivisi (specifica 4.11): tiene allineati gli appunti del PC
 * (GdkClipboard della finestra) con quelli del telefono. Solo testo. Senza
 * rimbalzi: un testo appena ricevuto dal telefono non si rimanda indietro, e
 * un testo messo negli appunti dalla shell stessa (gdk_clipboard_is_local)
 * non e' una copia dell'utente. I testi non si scrivono mai nei log. */
#include <string.h>

#include "ossatura.h"

typedef struct {
    PhServizi *servizi;
    GdkClipboard *cb;
    guint id_ascolto;
    gulong id_cambio;
    GCancellable *annulla;
    bool morto;   /* la radice e' stata distrutta: niente piu' chiamate ai servizi */
    char *ultimo; /* ultimo testo passato di qua (dal telefono o verso il telefono) */
} Appunti;

/* "destroy" della radice (ph_shell_libera la distrugge con i servizi ancora
 * vivi): qui si smette di ascoltare e si scollega il segnale degli appunti di
 * sistema, che altrimenti chiamerebbe i servizi quando sono gia' liberati. */
static void appunti_distrutti(GtkWidget *radice, gpointer p)
{
    (void)radice;
    Appunti *a = p;
    a->morto = true;
    g_cancellable_cancel(a->annulla);
    if (a->id_cambio != 0)
        g_signal_handler_disconnect(a->cb, a->id_cambio);
    a->id_cambio = 0;
    if (a->id_ascolto != 0)
        ph_srv_smetti(a->servizi, a->id_ascolto);
    a->id_ascolto = 0;
}

/* Finalizzazione: solo memoria. */
static void appunti_libera(gpointer p)
{
    Appunti *a = p;
    g_object_unref(a->annulla);
    g_object_unref(a->cb);
    g_free(a->ultimo);
    g_free(a);
}

/* Dal telefono: negli appunti del PC. */
static void su_dal_telefono(PhServizi *s, PhSrvEvento ev, const char *arg, void *u)
{
    (void)s;
    (void)ev;
    Appunti *a = u;
    if (a->morto || arg == NULL || *arg == '\0')
        return;
    if (a->ultimo != NULL && strcmp(a->ultimo, arg) == 0)
        return;
    g_free(a->ultimo);
    a->ultimo = g_strdup(arg);
    gdk_clipboard_set_text(a->cb, arg);
}

static void letto(GObject *o, GAsyncResult *r, gpointer dati)
{
    GError *err = NULL;
    char *testo = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(o), r, &err);
    if (testo == NULL) {
        /* Annullato (la shell si chiude) o contenuto non testuale: niente. */
        g_clear_error(&err);
        return;
    }
    Appunti *a = dati; /* valido: l'annullamento arriva prima della liberazione */
    if (!a->morto && *testo != '\0' && (a->ultimo == NULL || strcmp(a->ultimo, testo) != 0)) {
        g_free(a->ultimo);
        a->ultimo = g_strdup(testo);
        /* Controlla UTF-8 e lunghezza (200000 byte, 4.11) e non blocca. */
        ph_srv_appunti_imposta(a->servizi, testo);
    }
    g_free(testo);
}

static void su_cambio(GdkClipboard *cb, gpointer dati)
{
    Appunti *a = dati;
    if (a->morto)
        return;
    /* Il cambio l'ha fatto la shell (testo arrivato dal telefono): niente eco. */
    if (gdk_clipboard_is_local(cb))
        return;
    GdkContentFormats *f = gdk_clipboard_get_formats(cb);
    if (!gdk_content_formats_contain_gtype(f, G_TYPE_STRING))
        return;
    /* Copie marcate come sensibili da un gestore di password (convenzione
     * comune dei gestori sul PC): non passano mai al telefono. */
    if (gdk_content_formats_contain_mime_type(f, "x-kde-passwordManagerHint"))
        return;
    gdk_clipboard_read_text_async(cb, a->annulla, letto, a);
}

void ph_appunti_installa(PhShell *sh)
{
    GdkDisplay *d = gdk_display_get_default();
    if (d == NULL)
        return;
    Appunti *a = g_new0(Appunti, 1);
    a->servizi = ph_shell_servizi(sh);
    a->cb = g_object_ref(gdk_display_get_clipboard(d));
    a->annulla = g_cancellable_new();
    g_object_set_data_full(G_OBJECT(ph_shell_radice(sh)), "ph-ossatura-appunti", a, appunti_libera);
    a->id_ascolto = ph_srv_ascolta(a->servizi, PH_SRV_EV_APPUNTI, su_dal_telefono, a);
    a->id_cambio = g_signal_connect(a->cb, "changed", G_CALLBACK(su_cambio), a);
    g_signal_connect(ph_shell_radice(sh), "destroy", G_CALLBACK(appunti_distrutti), a);
}
