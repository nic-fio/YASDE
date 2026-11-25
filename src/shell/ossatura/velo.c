/* Velo di blocco e di connessione (specifica 4.8, mockup righe 511-518):
 * copre tutta l'area della shell in PH_STRATO_VELO secondo lo stato del
 * collegamento (PH_SRV_EV_COLLEGAMENTO). Le finestre interne restano dove sono
 * sotto il velo: al ritorno "tornano dove erano". */
#include "config/lingua.h"
#include "logica.h"
#include "ossatura.h"
#include "pezzi_presenti.h"
#if PH_SHELL_HA_CONNESSIONE
#include "shell/connessione/connessione.h"
#endif

typedef struct {
    PhShell *sh;
    GtkWidget *radice;
    GtkWidget *lucchetto, *titolo, *testo, *motivo, *pulsante;
    GtkWidget *connessione;   /* schermata della prima connessione (NULL se il pezzo manca) */
    bool collegato_in_sessione; /* in questa esecuzione c'e' stato un collegamento */
    guint id_ascolto;
    PhServizi *servizi;
} Velo;

/* "destroy" della radice: ph_shell_libera la distrugge mentre i servizi sono
 * ancora vivi. Tutto cio' che tocca servizi o shell si fa qui, non alla
 * finalizzazione (che puo' arrivare dopo la liberazione dei servizi). */
static void velo_distrutto(GtkWidget *radice, gpointer p)
{
    (void)radice;
    Velo *v = p;
    if (v->id_ascolto != 0)
        ph_srv_smetti(v->servizi, v->id_ascolto);
    v->id_ascolto = 0;
    v->sh = NULL;
}

/* Finalizzazione: solo memoria. */
static void velo_libera(gpointer p)
{
    g_free(p);
}

static void velo_aggiorna(Velo *v)
{
    PhSrvCollegamento c;
    PhVeloContenuto k;
    ph_srv_collegamento(v->servizi, &c);
    if (c.stato == PH_SRV_COLLEGATO || c.stato == PH_SRV_BLOCCATO)
        v->collegato_in_sessione = true;
    /* Una sola cosa copre la shell: la schermata della prima connessione o il velo. */
    PhVista vista = ph_ossatura_vista(&c, v->collegato_in_sessione, v->connessione != NULL);
    if (v->connessione != NULL) {
        bool era = gtk_widget_get_visible(v->connessione);
        gtk_widget_set_visible(v->connessione, vista == PH_VISTA_CONNESSIONE);
        if (vista == PH_VISTA_CONNESSIONE && !era && v->sh != NULL)
            ph_shell_popup_chiudi(v->sh); /* mostrarla chiude il popup aperto */
    }
    if (vista != PH_VISTA_VELO || !ph_velo_contenuto_per(&c, &k)) {
        gtk_widget_set_visible(v->radice, FALSE);
        return;
    }
    bool era_visibile = gtk_widget_get_visible(v->radice);
    gtk_widget_set_visible(v->lucchetto, k.lucchetto);
    gtk_label_set_text(GTK_LABEL(v->titolo), k.titolo);
    gtk_widget_set_visible(v->testo, k.testo != NULL);
    if (k.testo != NULL)
        gtk_label_set_text(GTK_LABEL(v->testo), k.testo);
    /* La frase del collegamento e' gia' per l'utente (e puo' mancare). */
    bool motivo = k.mostra_motivo && c.motivo[0] != '\0';
    gtk_widget_set_visible(v->motivo, motivo);
    if (motivo)
        gtk_label_set_text(GTK_LABEL(v->motivo), c.motivo);
    gtk_widget_set_visible(v->pulsante, k.pulsante);
    gtk_widget_set_visible(v->radice, TRUE);
    if (!era_visibile) {
        /* Mostrare il velo chiude il popup aperto (mockup toggleLock) e
         * toglie il fuoco alle finestre sotto: i tasti non devono arrivare
         * al telefono mentre il desktop e' fermo. */
        if (v->sh != NULL)
            ph_shell_popup_chiudi(v->sh);
        gtk_widget_grab_focus(k.pulsante ? v->pulsante : v->radice);
    }
}

static void su_collegamento(PhServizi *s, PhSrvEvento ev, const char *arg, void *u)
{
    (void)s;
    (void)ev;
    (void)arg;
    velo_aggiorna(u);
}

static void su_riconnetti(GtkButton *b, gpointer p)
{
    (void)b;
    Velo *v = p;
    ph_srv_riconnetti_ora(v->servizi);
}

void ph_velo_installa(PhShell *sh)
{
    Velo *v = g_new0(Velo, 1);
    v->sh = sh;
    v->servizi = ph_shell_servizi(sh);

    /* Ruolo "alert" (4.8, 5.11): proprieta' di costruzione. */
    v->radice = g_object_new(GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL, "spacing", 10,
                             "accessible-role", GTK_ACCESSIBLE_ROLE_ALERT, NULL);
    gtk_widget_add_css_class(v->radice, "ph-ossatura-velo");
    gtk_widget_set_halign(v->radice, GTK_ALIGN_FILL);
    gtk_widget_set_valign(v->radice, GTK_ALIGN_FILL);
    gtk_widget_set_focusable(v->radice, TRUE);

    /* Contenuto centrato in colonna. */
    GtkWidget *colonna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_halign(colonna, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(colonna, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(colonna, TRUE);
    gtk_box_append(GTK_BOX(v->radice), colonna);

    v->lucchetto = ph_shell_icona_nuova(&PH_ICONA_LUCCHETTO);
    gtk_widget_set_halign(v->lucchetto, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(colonna), v->lucchetto);

    v->titolo = gtk_label_new("");
    gtk_widget_add_css_class(v->titolo, "ph-ossatura-velo-titolo");
    gtk_label_set_justify(GTK_LABEL(v->titolo), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(colonna), v->titolo);

    v->testo = gtk_label_new("");
    gtk_widget_add_css_class(v->testo, "ph-ossatura-velo-testo");
    gtk_label_set_wrap(GTK_LABEL(v->testo), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(v->testo), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_justify(GTK_LABEL(v->testo), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars(GTK_LABEL(v->testo), 44);
    gtk_widget_set_size_request(v->testo, 320, -1);
    gtk_box_append(GTK_BOX(colonna), v->testo);

    v->motivo = gtk_label_new("");
    gtk_widget_add_css_class(v->motivo, "ph-ossatura-velo-testo");
    gtk_label_set_wrap(GTK_LABEL(v->motivo), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(v->motivo), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_justify(GTK_LABEL(v->motivo), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars(GTK_LABEL(v->motivo), 44);
    gtk_widget_set_size_request(v->motivo, 320, -1);
    gtk_box_append(GTK_BOX(colonna), v->motivo);

    v->pulsante = gtk_button_new_with_label(ph_t("Riconnetti ora"));
    gtk_widget_add_css_class(v->pulsante, "ph-accento");
    gtk_widget_add_css_class(v->pulsante, "ph-ossatura-velo-pulsante");
    gtk_widget_set_halign(v->pulsante, GTK_ALIGN_CENTER);
    g_signal_connect(v->pulsante, "clicked", G_CALLBACK(su_riconnetti), v);
    gtk_box_append(GTK_BOX(colonna), v->pulsante);

    gtk_widget_set_visible(v->radice, FALSE);
    ph_shell_strato_aggiungi(sh, PH_STRATO_VELO, v->radice);
#if PH_SHELL_HA_CONNESSIONE
    /* Sopra il velo, mai insieme: la mostra o la nasconde velo_aggiorna. Si libera
     * con la radice (ascolti e richieste in corso si chiudono nel suo dispose). */
    v->connessione = ph_connessione_nuova(sh);
    gtk_widget_set_visible(v->connessione, FALSE);
    ph_shell_strato_aggiungi(sh, PH_STRATO_VELO, v->connessione);
#endif

    /* Il velo vive quanto la radice: lo stato interno si libera con lei. */
    g_object_set_data_full(G_OBJECT(ph_shell_radice(sh)), "ph-ossatura-velo", v, velo_libera);
    g_signal_connect(ph_shell_radice(sh), "destroy", G_CALLBACK(velo_distrutto), v);
    v->id_ascolto = ph_srv_ascolta(v->servizi, PH_SRV_EV_COLLEGAMENTO, su_collegamento, v);
    velo_aggiorna(v);
}
