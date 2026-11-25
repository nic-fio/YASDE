/* Pezzo shell-sistema (F-06, F-07, F-08): notifiche e avvisi a comparsa, stato
 * del telefono, volume. Qui: installazione, smistamento degli avvisi dei
 * servizi, vita e distruzione. I pezzi veri stanno in notifiche.c, toast.c,
 * telefono.c, volume.c, pulsanti.c; la logica pura in logica.c. */
#include <string.h>

#include "shell/sistema/interno.h"

/* Generata da glib-compile-resources (--manual-register): il CSS del pezzo. */
void ph_risorse_sistema_register_resource(void);

void ph_sistema_popup_colloca(GtkWidget *w, int dal_bordo_destro, int dal_basso, int larghezza)
{
    gtk_widget_set_halign(w, GTK_ALIGN_END);
    gtk_widget_set_valign(w, GTK_ALIGN_END);
    gtk_widget_set_margin_end(w, dal_bordo_destro);
    gtk_widget_set_margin_bottom(w, dal_basso);
    gtk_widget_set_size_request(w, larghezza, -1);
}

GtkWidget *ph_sistema_pannello_nuovo(PhSistema *s, int spaziatura, const char *etichetta)
{
    GtkWidget *p = g_object_new(GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL, "spacing",
                                spaziatura, "accessible-role", GTK_ACCESSIBLE_ROLE_GROUP, NULL);
    gtk_widget_add_css_class(p, "ph-pannello");
    /* Superficie di vetro chiaro; 14 = border-radius di ph-pannello in base.css. */
    ph_shell_vetro_registra(s->sh, p, 14, PH_VETRO_CHIARO);
    ph_shell_etichetta(p, etichetta, NULL);
    gtk_widget_set_visible(p, FALSE);
    ph_shell_strato_aggiungi(s->sh, PH_STRATO_PANNELLI, p);
    return p;
}

static void su_servizi(PhServizi *srv, PhSrvEvento ev, const char *argomento, void *utente)
{
    (void)srv;
    PhSistema *s = utente;
    if (s->chiuso)
        return;
    switch (ev) {
    case PH_SRV_EV_COLLEGAMENTO:
        ph_sistema_telefono_aggiorna(s);
        break;
    case PH_SRV_EV_TELEFONO:
        ph_sistema_telefono_aggiorna(s);
        break;
    case PH_SRV_EV_APP:
        ph_sistema_notifiche_ridisegna(s); /* i nomi delle app */
        break;
    case PH_SRV_EV_ICONA:
        ph_sistema_notifiche_icona(s, argomento);
        break;
    case PH_SRV_EV_NOTIFICHE:
        ph_sistema_notifiche_cambiate(s);
        break;
    case PH_SRV_EV_VOLUME:
        ph_sistema_volume_aggiorna(s);
        break;
    case PH_SRV_EV_PROFILO:
        /* Profilo di un altro telefono: le sue notifiche sono un'altra storia. */
        ph_sistema_notifiche_profilo(s);
        ph_sistema_volume_aggiorna(s);
        break;
    case PH_SRV_EV_DECODIFICA:
        /* Il backend l'ha filtrato: una volta per sessione di collegamento. */
        /* Testi corti: il riquadro e' largo 300 px (titolo su 1 riga, testo su 2). Resta in vista
         * piu' a lungo degli altri toast: contiene l'indicazione su che cosa fare. */
        ph_sistema_toast_info_lungo(s, ph_t("Decodifica video hardware non disponibile"),
                                    ph_t("Il video usa il processore: le app sono meno fluide. "
                                         "Installa i driver video VA-API o Vulkan."));
        break;
    default:
        break;
    }
}

static void su_popup(PhShell *sh, PhShellEvento ev, void *utente)
{
    (void)sh;
    (void)ev;
    PhSistema *s = utente;
    if (!s->chiuso)
        ph_sistema_pulsanti_aggiorna(s); /* aria-expanded */
}

static void su_finestre(PhShell *sh, PhShellEvento ev, void *utente)
{
    (void)sh;
    (void)ev;
    PhSistema *s = utente;
    if (!s->chiuso)
        ph_sistema_telefono_aggiorna(s); /* lo specchio e' stato aperto o chiuso */
}

/* La radice e' in distruzione (i suoi figli sono gia' stati tolti): si smette
 * di ascoltare e si fermano i timer. Lo stato si libera dopo (su_finalizzata),
 * cosi' nessun gestore di un figlio puo' trovarlo gia' liberato. Il contratto
 * (CONTRATTI.md) vuole la radice distrutta prima di ph_shell_libera: il PhShell
 * qui e' ancora valido. */
static void su_distrutta(GtkWidget *radice, gpointer utente)
{
    PhSistema *s = utente;
    if (s->chiuso)
        return; /* idempotente */
    s->chiuso = true;
    for (int ev = 0; ev < PH_SRV_EV_NUMERO; ev++)
        ph_srv_smetti(s->srv, s->id_srv[ev]);
    ph_shell_smetti(s->sh, s->id_popup);
    ph_shell_smetti(s->sh, s->id_finestre);
    /* Le registrazioni dei gestori non sono coperte dal registro degli oggetti
     * vivi: si toccano solo se la radice e' ancora viva per il PhShell (durante
     * ph_shell_libera i gestori li toglie la shell stessa). */
    if (ph_shell_radice_viva(radice)) {
        ph_shell_registra_avvisi(s->sh, NULL);
        ph_shell_registra_sistema(s->sh, NULL);
    }
    ph_sistema_volume_ferma(s); /* toglie solo il timer: il livello e' gia' nel profilo da quando e' stato mosso */
    ph_sistema_telefono_ferma(s);
    ph_sistema_toast_ferma(s);
    ph_sistema_notifiche_ferma(s);
    ph_sistema_pulsanti_ferma(s);
    g_clear_object(&s->pannello_notifiche);
    g_clear_object(&s->pannello_telefono);
    g_clear_object(&s->pannello_volume);
    g_clear_object(&s->toast);
}

static void su_finalizzata(gpointer utente, GObject *radice)
{
    (void)radice;
    PhSistema *s = utente;
    ph_nascoste_libera(s->nascoste);
    ph_viste_libera(s->viste);
    g_free(s->firma_notifiche);
    g_free(s->toast_chiave);
    g_free(s->toast_pacchetto);
    if (s->pulsanti != NULL)
        g_ptr_array_free(s->pulsanti, TRUE);
    g_free(s);
}

void ph_sistema_installa(PhShell *sh)
{
    static gsize risorse = 0;
    if (g_once_init_enter(&risorse)) {
        ph_risorse_sistema_register_resource();
        g_once_init_leave(&risorse, 1);
    }
    ph_shell_stile_carica("/it/yasde/shell/sistema.css");

    PhSistema *s = g_new0(PhSistema, 1);
    s->sh = sh;
    s->srv = ph_shell_servizi(sh);
    s->nascoste = ph_nascoste_nuove();
    s->viste = ph_viste_nuove();
    s->pulsanti = g_ptr_array_new();

    ph_sistema_notifiche_inizia(s);
    ph_sistema_toast_inizia(s);
    ph_sistema_telefono_inizia(s);
    ph_sistema_volume_inizia(s);
    ph_sistema_pulsanti_inizia(s);

    PhShellGestoreAvvisi ga = {.io = s, .avviso = ph_sistema_toast_info};
    ph_shell_registra_avvisi(sh, &ga);
    PhShellGestoreSistema gs = {
        .io = s, .pulsante = ph_sistema_pulsante, .nascosta = ph_sistema_notifica_nascosta};
    ph_shell_registra_sistema(sh, &gs);

    static const PhSrvEvento ascoltati[] = {PH_SRV_EV_COLLEGAMENTO, PH_SRV_EV_TELEFONO, PH_SRV_EV_APP,
                                            PH_SRV_EV_ICONA,        PH_SRV_EV_NOTIFICHE, PH_SRV_EV_VOLUME,
                                            PH_SRV_EV_PROFILO,      PH_SRV_EV_DECODIFICA};
    for (size_t i = 0; i < G_N_ELEMENTS(ascoltati); i++)
        s->id_srv[ascoltati[i]] = ph_srv_ascolta(s->srv, ascoltati[i], su_servizi, s);
    s->id_popup = ph_shell_ascolta(sh, PH_SHELL_EV_POPUP, su_popup, s);
    s->id_finestre = ph_shell_ascolta(sh, PH_SHELL_EV_FINESTRE, su_finestre, s);

    /* Riferimenti ai widget a cui gli ascolti danno ancora lavoro: la radice, in
     * distruzione, stacca prima i figli. Si rilasciano dopo aver smesso di ascoltare. */
    g_object_ref(s->pannello_notifiche);
    g_object_ref(s->pannello_telefono);
    g_object_ref(s->pannello_volume);
    g_object_ref(s->toast);

    GtkWidget *radice = ph_shell_radice(sh);
    g_signal_connect(radice, "destroy", G_CALLBACK(su_distrutta), s);
    g_object_weak_ref(G_OBJECT(radice), su_finalizzata, s);
}
