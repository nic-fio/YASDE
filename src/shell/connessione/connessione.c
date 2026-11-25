/* Pezzo shell-connessione (prima connessione): la schermata che l'ossatura
 * mette in PH_STRATO_VELO quando non c'e' ancora un telefono collegato
 * (attesa, istruzioni, abbinamento con il codice, errori). Tutta la logica di
 * «che cosa si mostra» sta in logica_connessione.c (provata senza schermo);
 * qui ci sono solo i widget e i collegamenti con i servizi. Vedi COMPITO.md.
 *
 * RISERVATEZZA: il codice si legge dal campo al clic in un buffer locale, si
 * passa a ph_srv_abbina (che lo copia) e il buffer si cancella subito; il
 * campo si svuota nello stesso momento. Ne' il codice ne' l'indirizzo vanno
 * in un log, in un file o negli appunti. */
#include "connessione.h"

#include <string.h>

#include "config/lingua.h"
#include "logica_connessione.h"

/* Generata da glib-compile-resources (--manual-register): il CSS del pezzo. */
void ph_risorse_connessione_register_resource(void);

typedef struct {
    PhShell *sh;
    PhServizi *servizi;
    GtkWidget *radice;
    GtkWidget *titolo, *testo, *motivo, *arbitraria;
    GtkWidget *passi[PH_CNX_PASSI_MAX];
    GtkWidget *campo_codice, *errore, *espansore, *campo_indirizzo;
    GtkWidget *riga_attesa, *spinner, *attesa;
    GtkWidget *pulsante_abbina, *pulsante_cerca;
    guint id_ascolto;
    guint id_attesa;       /* controllo periodico finche' il servizio sta ancora abbinando (nessun evento lo dice) */
    GtkEditable *delegato_codice; /* dove sta il filtro del campo */
    gulong id_inserisci; /* gestore del filtro del campo (si blocca mentre inserisce le cifre filtrate) */
    GCancellable *annulla; /* richiesta di abbinamento in corso, NULL se nessuna */
    PhCnxFase fase;
    char errore_frase[256]; /* solo frasi per l'utente, mai il codice */
    int vista_prec;         /* per portare il fuoco al campo quando entra la vista del codice */
    bool comandi_attivi;
    bool campo_visibile;
    bool fuoco_dopo_esito; /* dopo un errore il fuoco torna al campo UNA volta */
    bool morto;             /* destroy gia' passato: nessuna callback deve piu' toccare i widget */
} Cnx;

/* ---- Aggiornamento ------------------------------------------------------ */

static void aggiorna(Cnx *d);

/* Controllo periodico: finche' il servizio sta ancora abbinando si
 * ricontrolla; appena finisce la schermata torna normale. */
static gboolean su_attesa_servizio(gpointer u)
{
    Cnx *d = u;
    if (d->morto) {
        d->id_attesa = 0;
        return G_SOURCE_REMOVE;
    }
    if (d->annulla == NULL && ph_srv_abbinamento_in_corso(d->servizi))
        return G_SOURCE_CONTINUE;
    d->id_attesa = 0;
    aggiorna(d);
    return G_SOURCE_REMOVE;
}

/* «Abbina» e' attivo solo con il codice di 6 cifre, l'indirizzo vuoto o
 * valido e nessun tentativo in corso. Si guarda il testo del campo senza
 * copiarlo. */
static void aggiorna_abbina(Cnx *d)
{
    bool ok = false;
    if (d->campo_visibile && d->comandi_attivi) {
        PhCnxIndirizzo ind = ph_cnx_leggi_indirizzo(gtk_editable_get_text(GTK_EDITABLE(d->campo_indirizzo)), NULL, 0, NULL);
        ok = ph_cnx_abbina_ammesso(gtk_editable_get_text(GTK_EDITABLE(d->campo_codice)), ind, false);
    }
    gtk_widget_set_sensitive(d->pulsante_abbina, ok);
}

static void etichetta_imposta(GtkWidget *w, const char *testo)
{
    gtk_label_set_text(GTK_LABEL(w), testo);
    gtk_widget_set_visible(w, testo != NULL && testo[0] != '\0');
}

static void aggiorna(Cnx *d)
{
    if (d->morto)
        return;
    PhSrvCollegamento c;
    ph_srv_collegamento(d->servizi, &c);
    d->fase = ph_cnx_fase_con_stato(d->fase, c.stato);
    /* Se la schermata e' stata rimostrata mentre il servizio sta ancora
     * abbinando (richiesta annullata ma non finita), si comporta come durante
     * un tentativo: campo e «Abbina» inattivi. Il servizio non da' un evento
     * quando finisce: si guarda a intervalli, solo finche' serve. */
    PhCnxFase fase = d->fase;
    bool servizio_occupato = d->annulla == NULL && ph_srv_abbinamento_in_corso(d->servizi);
    if (servizio_occupato && fase == PH_CNX_FASE_RIPOSO)
        fase = PH_CNX_FASE_IN_CORSO;
    if (servizio_occupato && d->id_attesa == 0 && fase == PH_CNX_FASE_IN_CORSO)
        d->id_attesa = g_timeout_add(200, su_attesa_servizio, d);
    PhCnxContenuto k;
    if (!ph_cnx_contenuto_per(&c, fase, &k)) {
        /* Nulla da mostrare in questo stato (l'ossatura la nasconde): se
         * resta visibile un istante, ultimo contenuto senza comandi attivi. */
        d->comandi_attivi = false;
        gtk_widget_set_sensitive(d->campo_codice, FALSE);
        gtk_widget_set_sensitive(d->espansore, FALSE);
        gtk_widget_set_sensitive(d->pulsante_cerca, FALSE);
        gtk_widget_set_sensitive(d->pulsante_abbina, FALSE);
        return;
    }
    d->comandi_attivi = k.comandi_attivi;
    d->campo_visibile = k.campo_codice;

    gtk_label_set_text(GTK_LABEL(d->titolo), k.titolo);
    etichetta_imposta(d->testo, k.testo);
    for (int i = 0; i < PH_CNX_PASSI_MAX; i++)
        etichetta_imposta(d->passi[i], i < k.n_passi ? k.passi[i] : NULL);
    etichetta_imposta(d->motivo, k.mostra_motivo ? c.motivo : NULL);
    gtk_widget_set_visible(d->arbitraria, k.riga_arbitraria);
    etichetta_imposta(d->errore, k.mostra_errore ? d->errore_frase : NULL);

    gtk_widget_set_visible(d->campo_codice, k.campo_codice);
    gtk_widget_set_visible(d->espansore, k.campo_codice);
    gtk_widget_set_visible(d->pulsante_abbina, k.campo_codice);
    gtk_widget_set_sensitive(d->campo_codice, k.comandi_attivi);
    gtk_widget_set_sensitive(d->espansore, k.comandi_attivi);

    gtk_widget_set_visible(d->riga_attesa, k.attesa != NULL);
    if (k.attesa != NULL)
        gtk_label_set_text(GTK_LABEL(d->attesa), k.attesa);
    gtk_widget_set_visible(d->spinner, k.spinner);
    gtk_spinner_set_spinning(GTK_SPINNER(d->spinner), k.spinner);

    gtk_widget_set_visible(d->pulsante_cerca, k.cerca_di_nuovo);
    gtk_widget_set_sensitive(d->pulsante_cerca, k.comandi_attivi);
    aggiorna_abbina(d);

    /* Il fuoco va al campo quando entra la vista del codice, e torna li' dopo
     * un errore (il campo era inattivo e l'aveva perso). */
    if (k.campo_codice && k.comandi_attivi && (d->vista_prec != (int)k.vista || d->fuoco_dopo_esito)) {
        gtk_widget_grab_focus(d->campo_codice);
        d->fuoco_dopo_esito = false;
    }
    d->vista_prec = (int)k.vista;
}

static void su_collegamento(PhServizi *s, PhSrvEvento ev, const char *arg, void *u)
{
    (void)s;
    (void)ev;
    (void)arg;
    aggiorna(u);
}

/* ---- Abbinamento -------------------------------------------------------- */

static void su_esito(PhServizi *s, PhSrvAbbEsito esito, const char *frase, void *u)
{
    (void)s;
    Cnx *d = u;
    /* Arriva solo se la richiesta non e' stata annullata: il cancellable e'
     * finito, lo lascio. */
    g_clear_object(&d->annulla);
    PhSrvCollegamento c;
    ph_srv_collegamento(d->servizi, &c);
    ph_cnx_errore_per(esito, frase, c.motivo, d->errore_frase, sizeof d->errore_frase);
    d->fase = ph_cnx_fase_dopo_esito(esito, c.stato);
    d->fuoco_dopo_esito = d->fase == PH_CNX_FASE_ERRORE;
    /* Il campo del codice e' gia' vuoto da quando si e' cliccato. */
    aggiorna(d);
}

static void abbina_ora(Cnx *d)
{
    if (d->morto || d->annulla != NULL || !d->comandi_attivi || !d->campo_visibile)
        return;
    char codice[PH_CNX_CIFRE + 2];
    ph_cnx_filtra_codice(gtk_editable_get_text(GTK_EDITABLE(d->campo_codice)), codice, sizeof codice);
    char host[PH_CNX_HOST_MAX];
    int porta = 0;
    PhCnxIndirizzo ind = ph_cnx_leggi_indirizzo(gtk_editable_get_text(GTK_EDITABLE(d->campo_indirizzo)), host,
                                                sizeof host, &porta);
    if (!ph_cnx_abbina_ammesso(codice, ind, false)) {
        ph_cnx_cancella(codice, sizeof codice);
        return;
    }
    d->annulla = g_cancellable_new();
    d->fase = PH_CNX_FASE_IN_CORSO;
    d->errore_frase[0] = '\0';
    /* Il campo si svuota subito: il codice non resta nel widget. */
    gtk_editable_set_text(GTK_EDITABLE(d->campo_codice), "");
    aggiorna(d);
    /* Dopo questa chiamata non si scrive piu' nello stato (l'esito potrebbe
     * essere arrivato di li' a poco, ma sempre dal ciclo principale). */
    ph_srv_abbina(d->servizi, codice, ind == PH_CNX_IND_VALIDO ? host : NULL, ind == PH_CNX_IND_VALIDO ? porta : 0,
                  d->annulla, su_esito, d);
    ph_cnx_cancella(codice, sizeof codice);
    ph_cnx_cancella(host, sizeof host);
}

static void su_pulsante_abbina(GtkButton *b, gpointer u)
{
    (void)b;
    abbina_ora(u);
}

static void su_invio(GtkEntry *e, gpointer u)
{
    (void)e;
    Cnx *d = u;
    /* Invio = «Abbina», ma solo se il pulsante sarebbe attivo. */
    if (gtk_widget_get_sensitive(d->pulsante_abbina))
        abbina_ora(d);
}

static void su_cerca_di_nuovo(GtkButton *b, gpointer u)
{
    (void)b;
    Cnx *d = u;
    if (d->morto || !d->comandi_attivi)
        return;
    ph_srv_riconnetti_ora(d->servizi);
}

static void su_campo_cambiato(GtkEditable *e, gpointer u)
{
    (void)e;
    aggiorna_abbina(u);
}

/* Il campo accetta solo cifre, al massimo 6 (anche con «incolla»). */
static void su_inserisci(GtkEditable *e, char *testo, int lunghezza, int *pos, gpointer u)
{
    gsize n = lunghezza < 0 ? strlen(testo) : (gsize)lunghezza;
    char *copia = g_strndup(testo, n);
    char filtrato[PH_CNX_CIFRE + 1];
    ph_cnx_filtra_codice(copia, filtrato, sizeof filtrato);
    ph_cnx_cancella(copia, n);
    g_free(copia);
    g_signal_stop_emission_by_name(e, "insert-text");
    if (filtrato[0] != '\0') {
        g_signal_handler_block(e, ((Cnx *)u)->id_inserisci);
        gtk_editable_insert_text(e, filtrato, -1, pos);
        g_signal_handler_unblock(e, ((Cnx *)u)->id_inserisci);
    }
    ph_cnx_cancella(filtrato, sizeof filtrato);
}

/* ---- Vita --------------------------------------------------------------- */

static void richiesta_annulla(Cnx *d)
{
    if (d->annulla != NULL) {
        /* Annullata dal thread GTK: la risposta non arrivera' piu'. */
        g_cancellable_cancel(d->annulla);
        g_clear_object(&d->annulla);
    }
}

/* Nascosta dall'ossatura: la richiesta in corso si lascia, e al ritorno la
 * schermata riparte dallo stato attuale con il campo vuoto. */
static void su_nascosta(GtkWidget *w, gpointer u)
{
    (void)w;
    Cnx *d = u;
    if (d->morto)
        return;
    richiesta_annulla(d);
    d->fase = PH_CNX_FASE_RIPOSO;
    d->errore_frase[0] = '\0';
    d->vista_prec = -1;
    d->fuoco_dopo_esito = false;
    gtk_editable_set_text(GTK_EDITABLE(d->campo_codice), "");
    gtk_editable_set_text(GTK_EDITABLE(d->campo_indirizzo), "");
    gtk_expander_set_expanded(GTK_EXPANDER(d->espansore), FALSE);
}

static void su_mostrata(GtkWidget *w, gpointer u)
{
    (void)w;
    aggiorna(u);
}

static void su_distruggi(GtkWidget *w, gpointer u)
{
    (void)w;
    Cnx *d = u;
    d->morto = true;
    if (d->id_attesa != 0)
        g_source_remove(d->id_attesa);
    d->id_attesa = 0;
    richiesta_annulla(d);
    /* I servizi sono ancora vivi (si liberano dopo la shell). */
    if (d->id_ascolto != 0)
        ph_srv_smetti(d->servizi, d->id_ascolto);
    d->id_ascolto = 0;
}

static void cnx_libera(gpointer p)
{
    Cnx *d = p;
    ph_cnx_cancella(d->errore_frase, sizeof d->errore_frase);
    g_free(d);
}

/* ---- Costruzione -------------------------------------------------------- */

static GtkWidget *testo_nuovo(const char *classe)
{
    GtkWidget *l = gtk_label_new("");
    gtk_widget_add_css_class(l, classe);
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(l), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 10); /* natura stretta: la carta ha larghezza fissa e il testo la riempie */
    gtk_widget_set_visible(l, FALSE);
    return l;
}

GtkWidget *ph_connessione_nuova(PhShell *sh)
{
    static gsize risorse = 0;
    if (g_once_init_enter(&risorse)) {
        ph_risorse_connessione_register_resource();
        g_once_init_leave(&risorse, 1);
    }
    ph_shell_stile_carica("/it/yasde/shell/connessione.css");

    Cnx *d = g_new0(Cnx, 1);
    d->sh = sh;
    d->servizi = ph_shell_servizi(sh);
    d->vista_prec = -1;

    d->radice = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(d->radice, "ph-connessione");
    gtk_widget_set_halign(d->radice, GTK_ALIGN_FILL);
    gtk_widget_set_valign(d->radice, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(d->radice, TRUE);
    gtk_widget_set_vexpand(d->radice, TRUE);

    /* La carta: come i dialoghi del mockup, larga 480 px, al centro. */
    GtkWidget *carta = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(carta, "ph-dialogo");
    gtk_widget_set_halign(carta, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(carta, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(carta, TRUE);
    gtk_widget_set_margin_start(carta, 24);
    gtk_widget_set_margin_end(carta, 24);
    gtk_widget_set_size_request(carta, 480, -1);
    /* Superficie di vetro chiaro (raggio 16, come .ph-dialogo): la radice NON lo e'.
     * Limite noto: la carta sta nello stesso strato del fondo sfumato della
     * schermata, che e' opaco e a tutta area; la sfocatura degli strati sotto
     * viene quindi calcolata e subito coperta da quel fondo (lavoro sprecato,
     * nessun difetto a schermo: sotto il velo della carta si vede la sfumatura). */
    ph_shell_vetro_registra(sh, carta, 16, PH_VETRO_CHIARO);
    gtk_box_append(GTK_BOX(d->radice), carta);

    /* Nome del prodotto, sottotitolo e avviso: solo in inglese, non passano da ph_t. */
    static const struct {
        const char *testo, *classe;
    } intestazione[] = {
        {"YASDE", "ph-connessione-nome"},
        {"Yet Another Samsung Dex Emulator", "ph-connessione-sottotitolo"},
        {"Independent project, not affiliated with Samsung", "ph-connessione-piccolo"},
    };
    for (size_t i = 0; i < sizeof intestazione / sizeof intestazione[0]; i++) {
        GtkWidget *l = gtk_label_new(intestazione[i].testo);
        gtk_widget_add_css_class(l, intestazione[i].classe);
        gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(l), TRUE);
        gtk_box_append(GTK_BOX(carta), l);
    }

    d->titolo = gtk_label_new("");
    gtk_widget_add_css_class(d->titolo, "ph-titolo-dialogo");
    gtk_label_set_xalign(GTK_LABEL(d->titolo), 0.0f);
    gtk_box_append(GTK_BOX(carta), d->titolo);

    d->testo = testo_nuovo("ph-connessione-testo");
    gtk_box_append(GTK_BOX(carta), d->testo);
    for (int i = 0; i < PH_CNX_PASSI_MAX; i++) {
        d->passi[i] = testo_nuovo("ph-connessione-testo");
        gtk_box_append(GTK_BOX(carta), d->passi[i]);
    }
    d->motivo = testo_nuovo("ph-connessione-testo");
    gtk_box_append(GTK_BOX(carta), d->motivo);
    d->arbitraria = testo_nuovo("ph-connessione-piccolo");
    gtk_label_set_text(GTK_LABEL(d->arbitraria), ph_t("Ci sono più telefoni in rete: uso il primo trovato."));
    gtk_widget_set_visible(d->arbitraria, FALSE);
    gtk_box_append(GTK_BOX(carta), d->arbitraria);

    /* Campo del codice: 6 cifre, centrato, a spaziatura fissa. */
    d->campo_codice = gtk_entry_new();
    gtk_widget_add_css_class(d->campo_codice, "ph-campo");
    gtk_widget_add_css_class(d->campo_codice, "ph-connessione-codice");
    gtk_entry_set_max_length(GTK_ENTRY(d->campo_codice), PH_CNX_CIFRE);
    gtk_entry_set_input_purpose(GTK_ENTRY(d->campo_codice), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_input_hints(GTK_ENTRY(d->campo_codice), GTK_INPUT_HINT_NO_SPELLCHECK);
    gtk_editable_set_alignment(GTK_EDITABLE(d->campo_codice), 0.5f);
    gtk_widget_set_halign(d->campo_codice, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(d->campo_codice, 200, -1);
    ph_shell_etichetta(d->campo_codice, ph_t("Codice di abbinamento"), NULL);
    /* In GTK4 «insert-text» di una GtkEntry arriva al suo delegate (GtkText): il
     * filtro va li', e li' si blocca mentre inserisce le cifre filtrate. */
    d->delegato_codice = gtk_editable_get_delegate(GTK_EDITABLE(d->campo_codice));
    d->id_inserisci = g_signal_connect(d->delegato_codice, "insert-text", G_CALLBACK(su_inserisci), d);
    g_signal_connect(d->campo_codice, "changed", G_CALLBACK(su_campo_cambiato), d);
    g_signal_connect(d->campo_codice, "activate", G_CALLBACK(su_invio), d);
    gtk_widget_set_visible(d->campo_codice, FALSE);
    gtk_box_append(GTK_BOX(carta), d->campo_codice);

    /* Riga d'errore: ruolo «alert», annunciata dal lettore di schermo. */
    d->errore = g_object_new(GTK_TYPE_LABEL, "accessible-role", GTK_ACCESSIBLE_ROLE_ALERT, NULL);
    gtk_widget_add_css_class(d->errore, "ph-connessione-errore");
    gtk_label_set_wrap(GTK_LABEL(d->errore), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(d->errore), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_xalign(GTK_LABEL(d->errore), 0.5f);
    gtk_label_set_justify(GTK_LABEL(d->errore), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars(GTK_LABEL(d->errore), 10);
    gtk_widget_set_visible(d->errore, FALSE);
    gtk_box_append(GTK_BOX(carta), d->errore);

    /* «Abbina» subito sotto il campo (e sopra l'espansore): cosi' l'ordine di
     * Tab e' campo, «Abbina», espansore, «Cerca di nuovo», come da COMPITO. */
    d->pulsante_abbina = gtk_button_new_with_label(ph_t("Abbina"));
    gtk_widget_add_css_class(d->pulsante_abbina, "ph-accento");
    gtk_widget_set_halign(d->pulsante_abbina, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(d->pulsante_abbina, 200, -1);
    ph_shell_etichetta(d->pulsante_abbina, ph_t("Abbina"), NULL);
    g_signal_connect(d->pulsante_abbina, "clicked", G_CALLBACK(su_pulsante_abbina), d);
    gtk_widget_set_visible(d->pulsante_abbina, FALSE);
    gtk_box_append(GTK_BOX(carta), d->pulsante_abbina);

    /* Indirizzo e porta a mano, in un espansore chiuso. */
    d->espansore = gtk_expander_new(ph_t("Il telefono non compare? Scrivi indirizzo e porta"));
    gtk_widget_add_css_class(d->espansore, "ph-connessione-espansore");
    d->campo_indirizzo = gtk_entry_new();
    gtk_widget_add_css_class(d->campo_indirizzo, "ph-campo");
    gtk_widget_add_css_class(d->campo_indirizzo, "ph-connessione-indirizzo");
    gtk_widget_set_margin_top(d->campo_indirizzo, 8);
    gtk_entry_set_max_length(GTK_ENTRY(d->campo_indirizzo), PH_CNX_HOST_MAX + 7);
    gtk_entry_set_input_hints(GTK_ENTRY(d->campo_indirizzo), GTK_INPUT_HINT_NO_SPELLCHECK);
    gtk_entry_set_placeholder_text(GTK_ENTRY(d->campo_indirizzo), ph_t("Indirizzo IP e porta"));
    ph_shell_etichetta(d->campo_indirizzo, ph_t("Indirizzo IP e porta"), NULL);
    g_signal_connect(d->campo_indirizzo, "changed", G_CALLBACK(su_campo_cambiato), d);
    g_signal_connect(d->campo_indirizzo, "activate", G_CALLBACK(su_invio), d);
    gtk_expander_set_child(GTK_EXPANDER(d->espansore), d->campo_indirizzo);
    gtk_widget_set_visible(d->espansore, FALSE);
    gtk_box_append(GTK_BOX(carta), d->espansore);

    /* Riga di attesa (cerchio che gira + frase). */
    d->riga_attesa = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    d->spinner = gtk_spinner_new();
    gtk_widget_set_size_request(d->spinner, 16, 16);
    gtk_widget_set_valign(d->spinner, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(d->riga_attesa), d->spinner);
    d->attesa = gtk_label_new("");
    gtk_widget_add_css_class(d->attesa, "ph-connessione-piccolo");
    gtk_label_set_xalign(GTK_LABEL(d->attesa), 0.0f);
    gtk_box_append(GTK_BOX(d->riga_attesa), d->attesa);
    gtk_widget_set_visible(d->riga_attesa, FALSE);
    gtk_box_append(GTK_BOX(carta), d->riga_attesa);

    /* In fondo a destra: «Cerca di nuovo». */
    GtkWidget *pulsanti = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(pulsanti, GTK_ALIGN_END);
    gtk_widget_set_margin_top(pulsanti, 4);
    d->pulsante_cerca = gtk_button_new_with_label(ph_t("Cerca di nuovo"));
    gtk_widget_add_css_class(d->pulsante_cerca, "ph-ghost");
    gtk_widget_add_css_class(d->pulsante_cerca, "ph-bordato");
    ph_shell_etichetta(d->pulsante_cerca, ph_t("Cerca di nuovo"), NULL);
    g_signal_connect(d->pulsante_cerca, "clicked", G_CALLBACK(su_cerca_di_nuovo), d);
    gtk_widget_set_visible(d->pulsante_cerca, FALSE);
    gtk_box_append(GTK_BOX(pulsanti), d->pulsante_cerca);
    gtk_box_append(GTK_BOX(carta), pulsanti);

    g_signal_connect(d->radice, "unmap", G_CALLBACK(su_nascosta), d);
    g_signal_connect(d->radice, "map", G_CALLBACK(su_mostrata), d);
    g_signal_connect(d->radice, "destroy", G_CALLBACK(su_distruggi), d);
    g_object_set_data_full(G_OBJECT(d->radice), "ph-connessione", d, cnx_libera);

    d->id_ascolto = ph_srv_ascolta(d->servizi, PH_SRV_EV_COLLEGAMENTO, su_collegamento, d);
    aggiorna(d);
    return d->radice;
}
