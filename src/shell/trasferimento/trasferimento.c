/* Pezzo shell-trasferimento (F-09): i pannelli "Invia al telefono" e "Scarica
 * dal telefono", il selettore di cartelle, il trascinamento di file sulla
 * finestra e gli avvisi di fine trasferimento. Vedi COMPITO.md e CONTRATTI.md.
 *
 * Tutto gira sul thread GTK. L'unico altro thread e' quello di GTask che
 * controlla i file trascinati o scelti (stat): non tocca widget ne' servizi. */
#include <string.h>
#include <sys/stat.h>

#include "config/lingua.h"
#include "shell/pezzi.h"
#include "shell/trasferimento/apk.h"
#include "shell/trasferimento/elenco.h"
#include "shell/trasferimento/navigatore.h"
#include "shell/trasferimento/percorsi.h"

#include "shell/trasferimento/interno.h"

/* Generata da glib-compile-resources (--manual-register): il CSS del pezzo. */
void ph_risorse_trasferimento_register_resource(void);

/* Cartelle usate spesso (mockup upDests), sotto la memoria interna. */
static const struct {
    const char *sotto;
    const char *testo;
} RAPIDI[N_RAPIDI] = {
    {"/Download", PH_T_("Download")},
    {"/Documents", PH_T_("Documenti")},
    {"/Pictures", PH_T_("Immagini")},
    {"/Music", PH_T_("Musica")},
};



static void trf_pulisci(gpointer p)
{
    Trf *t = p;
    ph_trf_foto_libera(t->foto);
    g_free(t->dest_invio);
    g_free(t->dest_ric);
    if (t->apk != NULL) {
        g_queue_free_full(t->apk, g_free);
    }
}

/* ---- Pezzi di interfaccia -------------------------------------------------------------------- */

static GtkWidget *bottone(const char *testo, const char *classe1, const char *classe2)
{
    GtkWidget *b = gtk_button_new_with_label(testo);
    if (classe1 != NULL)
        gtk_widget_add_css_class(b, classe1);
    if (classe2 != NULL)
        gtk_widget_add_css_class(b, classe2);
    return b;
}

static GtkWidget *testo_etichetta(const char *testo, const char *c1, const char *c2)
{
    GtkWidget *l = gtk_label_new(testo);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    if (c1 != NULL)
        gtk_widget_add_css_class(l, c1);
    if (c2 != NULL)
        gtk_widget_add_css_class(l, c2);
    return l;
}

/* Il pannello vero e proprio: titolo, sottotitolo e un contenuto che scorre se
 * supera l'altezza massima. Torna la colonna del contenuto (da riempire). */
static GtkWidget *pannello_nuovo(Trf *t, const char *titolo, const char *sotto, const char *etichetta,
                                 int altezza_max, int spazio, GtkWidget **pannello)
{
    GtkWidget *p = g_object_new(GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL, "accessible-role",
                                GTK_ACCESSIBLE_ROLE_DIALOG, NULL);
    gtk_widget_add_css_class(p, "ph-pannello");
    gtk_widget_add_css_class(p, "ph-trasferimento-pannello");
    /* Superficie di vetro chiaro; raggio 14 = quello di ph-pannello. */
    ph_shell_vetro_registra(t->sh, p, 14, PH_VETRO_CHIARO);
    gtk_widget_set_halign(p, GTK_ALIGN_END);
    gtk_widget_set_valign(p, GTK_ALIGN_END);
    gtk_widget_set_margin_end(p, 200);
    gtk_widget_set_margin_bottom(p, 56);
    gtk_widget_set_size_request(p, 380, -1);
    ph_shell_etichetta(p, etichetta, NULL);

    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, spazio);
    GtkWidget *testa = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_append(GTK_BOX(testa), testo_etichetta(titolo, "ph-titolo-pannello", NULL));
    gtk_box_append(GTK_BOX(testa), testo_etichetta(sotto, "ph-piccolo", "ph-testo-3"));
    gtk_box_append(GTK_BOX(col), testa);

    GtkWidget *scorri = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scorri), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    /* Altezza massima del mockup meno imbottitura (2 x 14) e bordo (2). */
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scorri), altezza_max - 30);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scorri), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scorri), col);
    gtk_box_append(GTK_BOX(p), scorri);
    gtk_widget_set_visible(p, FALSE);
    ph_shell_strato_aggiungi(t->sh, PH_STRATO_PANNELLI, p);
    *pannello = p;
    return col;
}

/* ---- Profilo e destinazioni ------------------------------------------------------------------- */

static bool percorso_pc_valido(const char *p)
{
    return p != NULL && p[0] == '/' && strlen(p) < PH_TRF_PERCORSO_MAX && g_utf8_validate(p, -1, NULL);
}

/* Legge dal profilo le due cartelle (all'avvio e quando si collega un altro telefono). */
static void leggi_destinazioni(Trf *t)
{
    const PhCfgTelefono *prof = ph_srv_profilo(t->srv);
    const PhCfgPreferenze *pref = ph_srv_preferenze(t->srv);
    char norm[PH_TRF_PERCORSO_MAX + 1];

    g_free(t->dest_invio);
    if (prof != NULL && prof->cartella_invio[0] != '\0' &&
        ph_trf_normalizza(prof->cartella_invio, norm, sizeof norm))
        t->dest_invio = g_strdup(norm);
    else
        t->dest_invio = g_strdup(PH_TRF_MEMORIA_INTERNA "/Download");

    g_free(t->dest_ric);
    t->dest_ric = NULL;
    const char *candidati[] = {prof != NULL ? prof->cartella_ricezione : NULL,
                               pref != NULL ? pref->cartella_ricevuti : NULL,
                               g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD), g_get_home_dir()};
    for (size_t i = 0; i < G_N_ELEMENTS(candidati) && t->dest_ric == NULL; i++) {
        if (percorso_pc_valido(candidati[i]))
            t->dest_ric = g_strdup(candidati[i]);
    }
    if (t->dest_ric == NULL)
        t->dest_ric = g_strdup("/");
}

static void salva_invio(Trf *t)
{
    PhCfgTelefono *prof = ph_srv_profilo(t->srv);
    if (prof == NULL || strlen(t->dest_invio) >= sizeof prof->cartella_invio)
        return; /* troppo lungo per il file del profilo: vale per questa sessione */
    g_strlcpy(prof->cartella_invio, t->dest_invio, sizeof prof->cartella_invio);
    ph_srv_profilo_salva(t->srv);
}

static void salva_ricezione(Trf *t)
{
    PhCfgTelefono *prof = ph_srv_profilo(t->srv);
    if (prof == NULL || strlen(t->dest_ric) >= sizeof prof->cartella_ricezione)
        return;
    g_strlcpy(prof->cartella_ricezione, t->dest_ric, sizeof prof->cartella_ricezione);
    ph_srv_profilo_salva(t->srv);
}

/* ---- Aggiornare l'aspetto --------------------------------------------------------------------- */

void ph_trf_aggiorna_invio(Trf *t)
{
    char *sicuro = ph_trf_testo_sicuro(t->dest_invio, PH_TRF_PERCORSO_MAX);
    gtk_label_set_text(GTK_LABEL(t->dest_invio_et), sicuro);
    gtk_widget_set_tooltip_text(t->dest_invio_et, sicuro);
    g_free(sicuro);
    gtk_button_set_label(GTK_BUTTON(t->cambia_invio), t->scelta_invio ? ph_t("Annulla") : ph_t("Cambia…"));
    gtk_widget_set_visible(t->zona, !t->scelta_invio);
    gtk_widget_set_visible(t->rapidi_gruppo, !t->scelta_invio);
    gtk_widget_set_visible(ph_nav_widget(t->nav_invio), t->scelta_invio);
    for (size_t i = 0; i < N_RAPIDI; i++) {
        char pieno[64];
        g_snprintf(pieno, sizeof pieno, "%s%s", PH_TRF_MEMORIA_INTERNA, RAPIDI[i].sotto);
        bool attuale = strcmp(t->dest_invio, pieno) == 0;
        if (attuale)
            gtk_widget_add_css_class(t->rapidi[i], "ph-trasferimento-rapido-attuale");
        else
            gtk_widget_remove_css_class(t->rapidi[i], "ph-trasferimento-rapido-attuale");
        ph_shell_premuto(t->rapidi[i], attuale);
    }
}

static void aggiorna_scarica(Trf *t)
{
    size_t n = ph_nav_scelti_n(t->nav_ric);
    if (n == 0) {
        gtk_button_set_label(GTK_BUTTON(t->scarica), ph_t("Seleziona i file da scaricare"));
        gtk_widget_set_sensitive(t->scarica, FALSE);
    } else {
        char num[32], testo[128];
        g_snprintf(num, sizeof num, "%zu", n);
        ph_lingua_componi(testo, sizeof testo, ph_t("Scarica {numero} file"), "numero", num, NULL);
        gtk_button_set_label(GTK_BUTTON(t->scarica), testo);
        gtk_widget_set_sensitive(t->scarica, TRUE);
    }
}

void ph_trf_aggiorna_ricezione(Trf *t)
{
    char *sicuro = ph_trf_testo_sicuro(t->dest_ric, PH_TRF_PERCORSO_MAX);
    char *testo = g_malloc(2 * PH_TRF_PERCORSO_MAX);
    ph_lingua_componi(testo, 2 * PH_TRF_PERCORSO_MAX, ph_t("PC: {percorso}"), "percorso", sicuro, NULL);
    gtk_label_set_text(GTK_LABEL(t->dest_ric_et), testo);
    gtk_widget_set_tooltip_text(t->dest_ric_et, testo);
    g_free(testo);
    g_free(sicuro);
    gtk_button_set_label(GTK_BUTTON(t->cambia_ric), t->scelta_pc ? ph_t("Annulla") : ph_t("Cambia…"));
    gtk_widget_set_visible(ph_nav_widget(t->nav_pc), t->scelta_pc);
    /* Mentre si sceglie la cartella del PC l'elenco del telefono si nasconde. */
    gtk_widget_set_visible(ph_nav_widget(t->nav_ric), !t->scelta_pc);
    aggiorna_scarica(t);
}

static void aggiorna_elenchi(Trf *t)
{
    size_t n = 0;
    const PhSrvTrasferimento *v = ph_srv_trasferimenti(t->srv, &n);
    ph_trf_elenco_aggiorna(t->el_invio, v, n);
    ph_trf_elenco_aggiorna(t->el_ric, v, n);
}

/* ---- Eventi dei servizi -------------------------------------------------------------------------- */

static void avvisa_completati(Trf *t, const PhSrvTrasferimento *v, GArray *idx, PhSrvVerso verso)
{
    unsigned quanti = 0;
    const PhSrvTrasferimento *primo = NULL;
    for (guint i = 0; i < idx->len; i++) {
        const PhSrvTrasferimento *x = &v[g_array_index(idx, guint, i)];
        if (x->verso != verso)
            continue;
        if (primo == NULL)
            primo = x;
        quanti++;
    }
    if (primo == NULL)
        return;
    char *nome = ph_trf_testo_sicuro(primo->nome, 255);
    char testo[600];
    if (quanti > 1) {
        char num[16];
        g_snprintf(num, sizeof num, "%u", quanti - 1);
        ph_lingua_componi(testo, sizeof testo, ph_t("{nome} e altri {numero}"), "nome", nome, "numero", num, NULL);
    } else {
        g_strlcpy(testo, nome, sizeof testo);
    }
    ph_shell_avviso(t->sh, verso == PH_SRV_INVIO ? ph_t("Inviato al telefono") : ph_t("Scaricato sul PC"), testo);
    g_free(nome);
}

static void tr_cambiati(PhServizi *s, PhSrvEvento ev, const char *arg, void *utente)
{
    (void)s;
    (void)ev;
    (void)arg;
    Trf *t = utente;
    size_t n = 0;
    const PhSrvTrasferimento *v = ph_srv_trasferimenti(t->srv, &n);
    ph_trf_elenco_aggiorna(t->el_invio, v, n);
    ph_trf_elenco_aggiorna(t->el_ric, v, n);
    /* Solo l'avviso: nessuna altra azione dopo un trasferimento (decisione di chi coordina). */
    GArray *fatti = ph_trf_foto_completati(t->foto, v, n);
    if (fatti->len > 0) {
        avvisa_completati(t, v, fatti, PH_SRV_INVIO);
        avvisa_completati(t, v, fatti, PH_SRV_RICEZIONE);
    }
    g_array_free(fatti, TRUE);
}

static void profilo_cambiato(PhServizi *s, PhSrvEvento ev, const char *arg, void *utente)
{
    (void)s;
    (void)ev;
    (void)arg;
    Trf *t = utente;
    leggi_destinazioni(t);
    /* Telefono diverso: i file scelti erano di un altro telefono. */
    ph_nav_scelti_svuota(t->nav_ric);
    size_t n = 0;
    const PhSrvTrasferimento *v = ph_srv_trasferimenti(t->srv, &n);
    ph_trf_foto_riprendi(t->foto, v, n); /* telefono diverso: niente avvisi per cio' che e' gia' finito */
    aggiorna_elenchi(t);
    ph_trf_aggiorna_invio(t);
    ph_trf_aggiorna_ricezione(t);
}

/* ---- Accodare file --------------------------------------------------------------------------------- */

typedef struct {
    Trf *t;               /* con un riferimento */
    GPtrArray *percorsi;  /* char *: file del PC da controllare */
    char *dest;           /* cartella del telefono al momento del gesto */
    GPtrArray *file;      /* risultato: file normali */
    GPtrArray *apk;       /* risultato: file .apk */
    guint saltati;        /* cartelle e cose non leggibili */
} Invio;

static void invio_libera(gpointer p)
{
    Invio *i = p;
    g_ptr_array_unref(i->percorsi);
    g_ptr_array_unref(i->file);
    g_ptr_array_unref(i->apk);
    g_free(i->dest);
    g_rc_box_release_full(i->t, trf_pulisci);
    g_free(i);
}

static bool finisce_per_apk(const char *p)
{
    size_t n = strlen(p);
    return n >= 4 && g_ascii_strcasecmp(p + n - 4, ".apk") == 0;
}

/* Thread di GTask: solo stat dei percorsi (puo' bloccarsi su un disco di rete). */
static void invio_filtra(GTask *task, gpointer src, gpointer dati, GCancellable *c)
{
    (void)src;
    (void)c;
    Invio *i = dati;
    for (guint k = 0; k < i->percorsi->len; k++) {
        const char *p = g_ptr_array_index(i->percorsi, k);
        struct stat st;
        if (stat(p, &st) != 0 || !S_ISREG(st.st_mode)) {
            i->saltati++;
            continue;
        }
        g_ptr_array_add(finisce_per_apk(p) ? i->apk : i->file, g_strdup(p));
    }
    g_task_return_boolean(task, TRUE);
}

static void apk_avanti(Trf *t);

static void invio_filtrato(GObject *src, GAsyncResult *res, gpointer dati)
{
    (void)src;
    (void)dati;
    Invio *i = g_task_get_task_data(G_TASK(res));
    Trf *t = i->t;
    if (t->morto)
        return;
    unsigned falliti = 0;
    char *primo_errore = NULL;
    for (guint k = 0; k < i->file->len; k++) {
        PhErrore e = {{0}};
        if (ph_srv_tr_accoda(t->srv, PH_SRV_INVIO, g_ptr_array_index(i->file, k), i->dest, &e) != 0) {
            falliti++;
            if (primo_errore == NULL)
                primo_errore = ph_trf_testo_sicuro(e.testo, 256);
        }
    }
    if (falliti > 0)
        ph_shell_avviso(t->sh, ph_t("Non riuscito"), primo_errore != NULL ? primo_errore : "");
    g_free(primo_errore);
    if (i->saltati > 0)
        ph_shell_avviso(t->sh, ph_t("Alcuni elementi non sono stati inviati"),
                        ph_t("Si inviano solo i file normali, non le cartelle."));
    /* Gli APK non si inviano come file: chiedono conferma e si installano. */
    for (guint k = 0; k < i->apk->len; k++)
        g_queue_push_tail(t->apk, g_strdup(g_ptr_array_index(i->apk, k)));
    apk_avanti(t);
}

/* Prende possesso di `percorsi` (GPtrArray di char *). */
static void invia_percorsi(Trf *t, GPtrArray *percorsi)
{
    if (percorsi->len == 0) {
        g_ptr_array_unref(percorsi);
        return;
    }
    Invio *i = g_new0(Invio, 1);
    i->t = g_rc_box_acquire(t);
    i->percorsi = percorsi;
    i->dest = g_strdup(t->dest_invio);
    i->file = g_ptr_array_new_with_free_func(g_free);
    i->apk = g_ptr_array_new_with_free_func(g_free);
    GTask *task = g_task_new(NULL, NULL, invio_filtrato, NULL);
    g_task_set_task_data(task, i, invio_libera);
    g_task_run_in_thread(task, invio_filtra);
    g_object_unref(task);
}

/* ---- APK ---------------------------------------------------------------------------------------------- */

/* Installazione di un APK confermato: la riga con l'avanzamento sta nell'elenco
 * dell'invio; alla fine un avviso con l'esito in parole semplici. */
typedef struct {
    Trf *t; /* con un riferimento */
    guint chiave;
    char *nome;
} Inst;

static void inst_avanza(PhServizi *s, uint64_t fatti, uint64_t totale, void *utente)
{
    (void)s;
    Inst *i = utente;
    if (!i->t->morto)
        ph_trf_elenco_inst_avanza(i->t->el_invio, i->chiave, fatti, totale);
}

static void inst_fatto(PhServizi *s, const PhSrvInstRisultato *r, void *utente)
{
    (void)s;
    Inst *i = utente;
    Trf *t = i->t;
    if (!t->morto) {
        ph_trf_elenco_inst_fine(t->el_invio, i->chiave);
        char *msg = ph_trf_testo_sicuro(r->messaggio, 768);
        switch (r->esito) {
        case PH_SRV_INST_RIUSCITO:
            ph_shell_avviso(t->sh, ph_t("App installata sul telefono."), i->nome);
            break;
        case PH_SRV_INST_ANNULLATO:
            ph_shell_avviso(t->sh, ph_t("Installazione annullata."), "");
            break;
        case PH_SRV_INST_RIFIUTATO:
        case PH_SRV_INST_ERRORE:
        default:
            ph_shell_avviso(t->sh, ph_t("Installazione non possibile."), msg);
            break;
        }
        g_free(msg);
    }
    g_free(i->nome);
    g_rc_box_release_full(t, trf_pulisci);
    g_free(i);
}

guint ph_trf_installa_apk(Trf *t, const char *percorso)
{
    Inst *i = g_new0(Inst, 1);
    i->t = g_rc_box_acquire(t);
    i->chiave = ++t->inst_prossima;
    char *base = g_path_get_basename(percorso);
    i->nome = ph_trf_testo_sicuro(base, 255);
    g_free(base);
    /* Le risposte arrivano sempre dopo questa chiamata (mai dentro): la riga si
     * crea qui e si toglie in inst_fatto, anche con id 0. */
    guint id = ph_srv_apk_installa(t->srv, percorso, inst_avanza, inst_fatto, i);
    ph_trf_elenco_inst_nuova(t->el_invio, i->chiave, id, i->nome);
    return id;
}

static void apk_risposta(void *utente, bool conferma, const char *percorso)
{
    Trf *t = utente;
    if (!t->morto) {
        t->apk_attivo = false;
        if (conferma)
            ph_trf_installa_apk(t, percorso);
        apk_avanti(t);
    }
    g_rc_box_release_full(t, trf_pulisci);
}

static void apk_avanti(Trf *t)
{
    if (t->morto || t->apk_attivo || g_queue_is_empty(t->apk))
        return;
    char *p = g_queue_pop_head(t->apk);
    t->apk_attivo = true;
    ph_trf_apk_chiedi(ph_shell_finestra(t->sh), p, apk_risposta, g_rc_box_acquire(t));
    g_free(p);
}

/* ---- Aprire i pannelli ---------------------------------------------------------------------------------- */

static void pannello_chiuso(PhShell *sh, PhShellPopup tipo, GtkWidget *w, void *utente)
{
    (void)sh;
    (void)w;
    Trf *t = utente;
    if (t->morto)
        return;
    if (tipo == PH_POPUP_INVIO) {
        ph_nav_ferma(t->nav_invio);
        t->scelta_invio = false;
        ph_trf_aggiorna_invio(t);
    } else {
        ph_nav_ferma(t->nav_ric);
        ph_nav_ferma(t->nav_pc);
        t->scelta_pc = false;
        ph_trf_aggiorna_ricezione(t);
    }
}

void ph_trf_apri_pannello(Trf *t, PhSrvVerso verso, bool alterna)
{
    PhShellPopup tipo = verso == PH_SRV_INVIO ? PH_POPUP_INVIO : PH_POPUP_RICEZIONE;
    GtkWidget *w = verso == PH_SRV_INVIO ? t->p_invio : t->p_ric;
    if (ph_shell_popup_aperto(t->sh) == tipo) {
        if (alterna)
            ph_shell_popup_chiudi(t->sh);
        return; /* gia' aperto (un file trascinato su un pannello aperto) */
    }
    aggiorna_elenchi(t);
    if (verso == PH_SRV_INVIO) {
        t->scelta_invio = false;
        ph_trf_aggiorna_invio(t);
    } else {
        t->scelta_pc = false;
        ph_trf_aggiorna_ricezione(t);
        /* L'elenco del telefono si rilegge a ogni apertura; parte dalla memoria interna e poi resta dov'era. */
        ph_nav_apri(t->nav_ric, t->ric_iniziato ? ph_nav_percorso(t->nav_ric) : PH_TRF_MEMORIA_INTERNA);
        t->ric_iniziato = true;
    }
    ph_shell_popup_apri(t->sh, tipo, w, pannello_chiuso, t);
}

static void gestore_apri(void *io, PhSrvVerso verso)
{
    ph_trf_apri_pannello(io, verso, true);
}

/* ---- Invia: reazioni ----------------------------------------------------------------------------------------- */

static void cambia_invio_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    Trf *t = dati;
    t->scelta_invio = !t->scelta_invio;
    if (t->scelta_invio)
        ph_nav_apri(t->nav_invio, t->dest_invio); /* parte dalla destinazione attuale */
    else
        ph_nav_ferma(t->nav_invio);
    ph_trf_aggiorna_invio(t);
}

static void invio_usa(void *utente, const char *percorso)
{
    Trf *t = utente;
    g_free(t->dest_invio);
    t->dest_invio = g_strdup(percorso);
    t->scelta_invio = false;
    ph_nav_ferma(t->nav_invio);
    salva_invio(t);
    ph_trf_aggiorna_invio(t);
}

static void rapido_clic(GtkButton *b, gpointer dati)
{
    Trf *t = dati;
    for (size_t i = 0; i < N_RAPIDI; i++) {
        if (GTK_WIDGET(b) != t->rapidi[i])
            continue;
        g_free(t->dest_invio);
        t->dest_invio = g_strdup_printf("%s%s", PH_TRF_MEMORIA_INTERNA, RAPIDI[i].sotto);
        salva_invio(t);
        ph_trf_aggiorna_invio(t);
        return;
    }
}

static void scelti_file_pc(GObject *src, GAsyncResult *res, gpointer dati)
{
    Trf *t = dati;
    GError *err = NULL;
    GListModel *lista = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(src), res, &err);
    g_clear_error(&err); /* annullato dall'utente: nessun messaggio */
    if (lista != NULL && !t->morto) {
        GPtrArray *percorsi = g_ptr_array_new_with_free_func(g_free);
        guint n = g_list_model_get_n_items(lista);
        for (guint i = 0; i < n; i++) {
            GFile *f = g_list_model_get_item(lista, i);
            char *p = f != NULL ? g_file_get_path(f) : NULL;
            if (p != NULL)
                g_ptr_array_add(percorsi, p);
            g_clear_object(&f);
        }
        invia_percorsi(t, percorsi);
    }
    g_clear_object(&lista);
    g_rc_box_release_full(t, trf_pulisci);
}

static void scegli_file_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    Trf *t = dati;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, ph_t("Scegli i file dal PC"));
    gtk_file_dialog_set_modal(d, TRUE);
    gtk_file_dialog_open_multiple(d, ph_shell_finestra(t->sh), NULL, scelti_file_pc, g_rc_box_acquire(t));
    g_object_unref(d);
}

/* ---- Scarica: reazioni ------------------------------------------------------------------------------------------ */

static void ric_selezione(void *utente, size_t scelti)
{
    (void)scelti;
    aggiorna_scarica(utente);
}

static void scarica_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    Trf *t = dati;
    size_t n = ph_nav_scelti_n(t->nav_ric);
    if (n == 0)
        return;
    GPtrArray *fatti = g_ptr_array_new_with_free_func(g_free);
    unsigned falliti = 0;
    char *primo_errore = NULL;
    for (size_t i = 0; i < n; i++) {
        const char *p = ph_nav_scelto(t->nav_ric, i);
        PhErrore e = {{0}};
        if (ph_srv_tr_accoda(t->srv, PH_SRV_RICEZIONE, p, t->dest_ric, &e) == 0) {
            g_ptr_array_add(fatti, g_strdup(p));
        } else {
            falliti++;
            if (primo_errore == NULL)
                primo_errore = ph_trf_testo_sicuro(e.testo, 256);
        }
    }
    /* Quelli accodati escono dalla selezione; quelli rifiutati restano per riprovare. */
    for (guint i = 0; i < fatti->len; i++)
        ph_nav_scelti_togli(t->nav_ric, g_ptr_array_index(fatti, i));
    g_ptr_array_unref(fatti);
    if (falliti > 0)
        ph_shell_avviso(t->sh, ph_t("Non riuscito"), primo_errore != NULL ? primo_errore : "");
    g_free(primo_errore);
}

static void cambia_ric_clic(GtkButton *b, gpointer dati)
{
    (void)b;
    Trf *t = dati;
    t->scelta_pc = !t->scelta_pc;
    if (t->scelta_pc)
        ph_nav_apri(t->nav_pc, t->dest_ric); /* se non esiste parte dalla cartella personale */
    else
        ph_nav_ferma(t->nav_pc);
    ph_trf_aggiorna_ricezione(t);
}

static void pc_usa(void *utente, const char *percorso)
{
    Trf *t = utente;
    g_free(t->dest_ric);
    t->dest_ric = g_strdup(percorso);
    t->scelta_pc = false;
    ph_nav_ferma(t->nav_pc);
    salva_ricezione(t);
    ph_trf_aggiorna_ricezione(t);
}

/* ---- Trascinamento di file sulla finestra ------------------------------------------------------------------------ */

static GdkDragAction drop_entra(GtkDropTarget *d, double x, double y, gpointer dati)
{
    (void)d;
    (void)x;
    (void)y;
    Trf *t = dati;
    gtk_widget_set_visible(t->riquadro, TRUE);
    return GDK_ACTION_COPY;
}

static void drop_esce(GtkDropTarget *d, gpointer dati)
{
    (void)d;
    Trf *t = dati;
    gtk_widget_set_visible(t->riquadro, FALSE);
}

static gboolean drop_rilascia(GtkDropTarget *d, const GValue *v, double x, double y, gpointer dati)
{
    (void)d;
    (void)x;
    (void)y;
    Trf *t = dati;
    gtk_widget_set_visible(t->riquadro, FALSE);
    if (!G_VALUE_HOLDS(v, GDK_TYPE_FILE_LIST))
        return FALSE;
    GdkFileList *lista = g_value_get_boxed(v);
    GSList *file = lista != NULL ? gdk_file_list_get_files(lista) : NULL;
    GPtrArray *percorsi = g_ptr_array_new_with_free_func(g_free);
    for (GSList *p = file; p != NULL; p = p->next) {
        char *percorso = g_file_get_path(G_FILE(p->data)); /* NULL se non e' un file locale */
        if (percorso != NULL)
            g_ptr_array_add(percorsi, percorso);
    }
    g_slist_free(file);
    ph_trf_apri_pannello(t, PH_SRV_INVIO, false);
    invia_percorsi(t, percorsi);
    return TRUE;
}

/* ---- Costruzione ---------------------------------------------------------------------------------------------------- */

static void costruisci_invio(Trf *t)
{
    GtkWidget *col = pannello_nuovo(t, ph_t("Invia al telefono"), ph_t("Dal PC al telefono"),
                                    ph_t("Invia file al telefono"), 560, 12, &t->p_invio);

    /* Zona di trascinamento: tratteggio, testo, "oppure" e il selettore del sistema. */
    t->zona = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(t->zona, "ph-trasferimento-zona");
    GtkWidget *z1 = testo_etichetta(ph_t("Trascina i file in qualsiasi punto della finestra"), "ph-trasferimento-zona-testo", NULL);
    gtk_label_set_wrap(GTK_LABEL(z1), TRUE);
    gtk_label_set_justify(GTK_LABEL(z1), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(z1), 0.5f);
    GtkWidget *z2 = testo_etichetta(ph_t("oppure"), "ph-piccolo", "ph-testo-3");
    gtk_label_set_xalign(GTK_LABEL(z2), 0.5f);
    GtkWidget *scegli = bottone(ph_t("Scegli i file dal PC"), "ph-ghost", "ph-bordato");
    gtk_widget_set_halign(scegli, GTK_ALIGN_CENTER);
    ph_shell_etichetta(scegli, ph_t("Scegli i file dal PC"), NULL);
    g_signal_connect(scegli, "clicked", G_CALLBACK(scegli_file_clic), t);
    gtk_box_append(GTK_BOX(t->zona), z1);
    gtk_box_append(GTK_BOX(t->zona), z2);
    gtk_box_append(GTK_BOX(t->zona), scegli);
    gtk_box_append(GTK_BOX(col), t->zona);

    gtk_box_append(GTK_BOX(col), testo_etichetta(ph_t("Cartella di destinazione sul telefono"), "ph-sezione", NULL));
    GtkWidget *riga = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(riga, "ph-trasferimento-dest");
    t->dest_invio_et = ph_trf_etichetta_ellissi("ph-piccolo");
    t->cambia_invio = bottone(ph_t("Cambia…"), "ph-ghost", "ph-bordato");
    g_signal_connect(t->cambia_invio, "clicked", G_CALLBACK(cambia_invio_clic), t);
    gtk_box_append(GTK_BOX(riga), t->dest_invio_et);
    gtk_box_append(GTK_BOX(riga), t->cambia_invio);
    gtk_box_append(GTK_BOX(col), riga);

    /* Cartelle usate spesso (gruppo con etichetta). */
    t->rapidi_gruppo = g_object_new(GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_HORIZONTAL, "spacing", 4,
                                    "accessible-role", GTK_ACCESSIBLE_ROLE_GROUP, NULL);
    ph_shell_etichetta(t->rapidi_gruppo, ph_t("Cartelle usate spesso"), NULL);
    for (size_t i = 0; i < N_RAPIDI; i++) {
        t->rapidi[i] = bottone(ph_tv(RAPIDI[i].testo), "ph-trasferimento-rapido", NULL);
        g_signal_connect(t->rapidi[i], "clicked", G_CALLBACK(rapido_clic), t);
        gtk_box_append(GTK_BOX(t->rapidi_gruppo), t->rapidi[i]);
    }
    gtk_box_append(GTK_BOX(col), t->rapidi_gruppo);

    PhNavGestori g = {.usa = invio_usa, .selezione = NULL};
    t->nav_invio = ph_nav_nuovo(t->sh, PH_NAV_TEL_CARTELLE, &g, t);
    gtk_widget_set_visible(ph_nav_widget(t->nav_invio), FALSE);
    gtk_box_append(GTK_BOX(col), ph_nav_widget(t->nav_invio));

    t->el_invio = ph_trf_elenco_nuovo(t->sh, PH_SRV_INVIO, 150);
    gtk_box_append(GTK_BOX(col), ph_trf_elenco_widget(t->el_invio));
}

static void costruisci_ricezione(Trf *t)
{
    GtkWidget *col = pannello_nuovo(t, ph_t("Scarica dal telefono"), ph_t("Dal telefono al PC"),
                                    ph_t("Scarica file dal telefono"), 580, 10, &t->p_ric);

    PhNavGestori g = {.usa = NULL, .selezione = ric_selezione};
    t->nav_ric = ph_nav_nuovo(t->sh, PH_NAV_TEL_FILE, &g, t);
    gtk_box_append(GTK_BOX(col), ph_nav_widget(t->nav_ric));

    t->scarica = bottone("", "ph-accento", "ph-trasferimento-scarica");
    gtk_widget_add_css_class(t->scarica, "ph-trasferimento-pieno");
    g_signal_connect(t->scarica, "clicked", G_CALLBACK(scarica_clic), t);
    gtk_box_append(GTK_BOX(col), t->scarica);

    gtk_box_append(GTK_BOX(col), testo_etichetta(ph_t("Cartella di destinazione sul PC"), "ph-sezione", NULL));
    GtkWidget *riga = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(riga, "ph-trasferimento-dest");
    t->dest_ric_et = ph_trf_etichetta_ellissi("ph-piccolo");
    t->cambia_ric = bottone(ph_t("Cambia…"), "ph-ghost", "ph-bordato");
    g_signal_connect(t->cambia_ric, "clicked", G_CALLBACK(cambia_ric_clic), t);
    gtk_box_append(GTK_BOX(riga), t->dest_ric_et);
    gtk_box_append(GTK_BOX(riga), t->cambia_ric);
    gtk_box_append(GTK_BOX(col), riga);

    PhNavGestori gp = {.usa = pc_usa, .selezione = NULL};
    t->nav_pc = ph_nav_nuovo(t->sh, PH_NAV_PC_CARTELLE, &gp, t);
    gtk_widget_set_visible(ph_nav_widget(t->nav_pc), FALSE);
    gtk_box_append(GTK_BOX(col), ph_nav_widget(t->nav_pc));

    t->el_ric = ph_trf_elenco_nuovo(t->sh, PH_SRV_RICEZIONE, 120);
    gtk_box_append(GTK_BOX(col), ph_trf_elenco_widget(t->el_ric));
}

/* La radice sta per essere distrutta: via gestore e ascoltatori, nessuna
 * funzione di ritorno tocca piu' i widget. Il PhShell puo' essere gia' libero
 * (ph_shell_libera prima della radice): lo si riconosce dal dato che libera. */
static void radice_distrutta(GtkWidget *radice, gpointer dati)
{
    Trf *t = dati;
    if (t->morto)
        return;
    t->morto = true;
    if (ph_shell_radice_viva(radice))
        ph_shell_registra_trasferimento(t->sh, NULL);
    ph_srv_smetti(t->srv, t->ev_tr);
    ph_srv_smetti(t->srv, t->ev_profilo);
    ph_nav_distruggi(t->nav_invio);
    ph_nav_distruggi(t->nav_ric);
    ph_nav_distruggi(t->nav_pc);
    for (size_t i = 0; i < G_N_ELEMENTS(t->tenuti); i++)
        g_clear_object(&t->tenuti[i]);
    g_object_set_data(G_OBJECT(radice), "ph-trasferimento", NULL);
    g_rc_box_release_full(t, trf_pulisci);
}

void ph_trasferimento_installa(PhShell *sh)
{
    static gsize risorse = 0;
    if (g_once_init_enter(&risorse)) {
        ph_risorse_trasferimento_register_resource();
        g_once_init_leave(&risorse, 1);
    }
    ph_shell_stile_carica("/it/yasde/shell/trasferimento.css");

    Trf *t = g_rc_box_new0(Trf);
    t->sh = sh;
    t->srv = ph_shell_servizi(sh);
    t->radice = ph_shell_radice(sh);
    t->foto = ph_trf_foto_nuova();
    t->apk = g_queue_new();
    leggi_destinazioni(t);

    costruisci_invio(t);
    costruisci_ricezione(t);

    /* Riquadro "Rilascia per inviare al telefono": non riceve clic. */
    GtkWidget *ril = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(ril, "ph-trasferimento-rilascio");
    gtk_widget_set_margin_start(ril, 8);
    gtk_widget_set_margin_top(ril, 8);
    gtk_widget_set_margin_end(ril, 8);
    gtk_widget_set_margin_bottom(ril, 56);
    GtkWidget *testo = gtk_label_new(ph_t("Rilascia per inviare al telefono"));
    gtk_widget_set_hexpand(testo, TRUE);
    gtk_widget_set_vexpand(testo, TRUE);
    gtk_box_append(GTK_BOX(ril), testo);
    gtk_widget_set_can_target(ril, FALSE);
    gtk_widget_set_visible(ril, FALSE);
    ph_shell_strato_aggiungi(sh, PH_STRATO_RILASCIO, ril);
    t->riquadro = ril;
    t->tenuti[0] = g_object_ref(t->p_invio);
    t->tenuti[1] = g_object_ref(t->p_ric);
    t->tenuti[2] = g_object_ref(ril);
    g_object_set_data(G_OBJECT(t->radice), "ph-trasferimento", t);

    /* Trascinamento di file dal PC su tutta la finestra (le app del launcher
     * hanno un altro tipo: non lo attivano). */
    GtkDropTarget *dt = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(dt, "enter", G_CALLBACK(drop_entra), t);
    g_signal_connect(dt, "leave", G_CALLBACK(drop_esce), t);
    g_signal_connect(dt, "drop", G_CALLBACK(drop_rilascia), t);
    gtk_widget_add_controller(t->radice, GTK_EVENT_CONTROLLER(dt));

    ph_trf_aggiorna_invio(t);
    ph_trf_aggiorna_ricezione(t);
    size_t n = 0;
    const PhSrvTrasferimento *v = ph_srv_trasferimenti(t->srv, &n);
    ph_trf_foto_riprendi(t->foto, v, n); /* cio' che c'e' gia' non genera avvisi */
    ph_trf_elenco_aggiorna(t->el_invio, v, n);
    ph_trf_elenco_aggiorna(t->el_ric, v, n);
    t->ev_tr = ph_srv_ascolta(t->srv, PH_SRV_EV_TRASFERIMENTI, tr_cambiati, t);
    t->ev_profilo = ph_srv_ascolta(t->srv, PH_SRV_EV_PROFILO, profilo_cambiato, t);

    PhShellGestoreTrasferimento g = {.io = t, .apri = gestore_apri};
    ph_shell_registra_trasferimento(sh, &g);
    g_signal_connect(t->radice, "destroy", G_CALLBACK(radice_distrutta), t);
}

