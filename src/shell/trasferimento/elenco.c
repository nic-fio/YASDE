/* Elenco dei trasferimenti di un verso (elenco.h). */
#include "shell/trasferimento/elenco.h"

#include <string.h>

#include "config/lingua.h"
#include "shell/trasferimento/navigatore.h"
#include "shell/trasferimento/percorsi.h"

typedef struct {
    PhTrfElenco *e;
    uint64_t id;
    bool inst;           /* installazione di un APK (id del servizio), non un trasferimento */
    GtkWidget *riga, *nome, *stato, *barra, *meta, *annulla;
} Voce;

struct PhTrfElenco {
    PhShell *sh;
    PhSrvVerso verso;
    GtkWidget *radice;      /* colonna: elenco che scorre + "Annulla tutto" */
    GtkWidget *righe;       /* colonna delle voci */
    GtkWidget *annulla_tutto, *resto;
    GHashTable *voci;       /* id (guint64 *) -> Voce * */
    GQueue *inst_righe;     /* Voce * delle installazioni in corso, la piu' vecchia prima */
    GHashTable *inst;       /* chiave (guint) -> Voce * */
    /* Id delle voci attive, per "Annulla tutto" (solo quelle di questo verso). */
    GArray *attivi;
};

static void voce_libera(gpointer p)
{
    g_free(p);
}

static void elenco_libera(gpointer p)
{
    PhTrfElenco *e = p;
    g_hash_table_unref(e->voci);
    g_hash_table_unref(e->inst);
    g_queue_free(e->inst_righe);
    g_array_free(e->attivi, TRUE);
    g_free(e);
}

static void clic_annulla(GtkButton *b, gpointer dati)
{
    (void)b;
    Voce *v = dati;
    if (v->inst)
        ph_srv_installazione_annulla(ph_shell_servizi(v->e->sh), (guint)v->id);
    else
        ph_srv_tr_annulla(ph_shell_servizi(v->e->sh), v->id);
}

static void clic_annulla_tutto(GtkButton *b, gpointer dati)
{
    (void)b;
    PhTrfElenco *e = dati;
    /* Tutta la coda di questo verso (anche le voci che l'elenco non mostra). */
    ph_srv_tr_annulla_verso(ph_shell_servizi(e->sh), e->verso);
}

static Voce *voce_nuova(PhTrfElenco *e, uint64_t id)
{
    Voce *v = g_new0(Voce, 1);
    v->e = e;
    v->id = id;
    v->riga = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(v->riga, "ph-voce");
    gtk_widget_add_css_class(v->riga, "ph-trasferimento-voce");

    GtkWidget *alto = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    v->nome = ph_trf_etichetta_ellissi("ph-trasferimento-nome");
    v->stato = gtk_label_new("");
    gtk_widget_add_css_class(v->stato, "ph-testo-2");
    gtk_widget_set_halign(v->stato, GTK_ALIGN_END);
    gtk_widget_set_valign(v->stato, GTK_ALIGN_CENTER);
    v->annulla = gtk_button_new_with_label("×");
    gtk_widget_add_css_class(v->annulla, "ph-ghost");
    gtk_widget_add_css_class(v->annulla, "ph-trasferimento-x");
    gtk_widget_set_valign(v->annulla, GTK_ALIGN_CENTER);
    g_signal_connect(v->annulla, "clicked", G_CALLBACK(clic_annulla), v);
    gtk_box_append(GTK_BOX(alto), v->nome);
    gtk_box_append(GTK_BOX(alto), v->stato);
    gtk_box_append(GTK_BOX(alto), v->annulla);

    v->barra = gtk_progress_bar_new();
    gtk_widget_add_css_class(v->barra, "ph-trasferimento-barra");
    gtk_widget_set_margin_top(v->barra, 6);
    v->meta = ph_trf_etichetta_ellissi("ph-testo-3");
    /* La destinazione si mostra intera, a capo (mockup, 5.9.1). */
    gtk_label_set_ellipsize(GTK_LABEL(v->meta), PANGO_ELLIPSIZE_NONE);
    gtk_label_set_wrap(GTK_LABEL(v->meta), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(v->meta), PANGO_WRAP_WORD_CHAR);
    gtk_widget_set_margin_top(v->meta, 4);

    gtk_box_append(GTK_BOX(v->riga), alto);
    gtk_box_append(GTK_BOX(v->riga), v->barra);
    gtk_box_append(GTK_BOX(v->riga), v->meta);
    return v;
}

static void voce_aggiorna(Voce *v, const PhSrvTrasferimento *t)
{
    char *nome = ph_trf_testo_sicuro(t->nome, 255);
    gtk_label_set_text(GTK_LABEL(v->nome), nome);
    unsigned pct = ph_trf_percento(t->fatti, t->dimensione);
    if (t->stato == PH_SRV_TR_COMPLETATO)
        pct = 100;
    char stato[64];
    ph_trf_stato_testo(t->stato, pct, stato, sizeof stato);
    gtk_label_set_text(GTK_LABEL(v->stato), stato);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(v->barra), pct / 100.0);
    char meta[2048];
    ph_trf_meta_testo(t, meta, sizeof meta);
    gtk_label_set_text(GTK_LABEL(v->meta), meta);
    gtk_widget_set_tooltip_text(v->meta, meta);
    gtk_widget_set_visible(v->annulla, ph_trf_attivo(t->stato));
    char etichetta[600];
    ph_lingua_componi(etichetta, sizeof etichetta, ph_t("Annulla il trasferimento di {nome}"), "nome",
                      nome, NULL);
    ph_shell_etichetta(v->annulla, etichetta, ph_t("Annulla"));
    ph_shell_etichetta(v->barra, nome, NULL);
    g_free(nome);
}

PhTrfElenco *ph_trf_elenco_nuovo(PhShell *sh, PhSrvVerso verso, int altezza_max)
{
    PhTrfElenco *e = g_new0(PhTrfElenco, 1);
    e->sh = sh;
    e->verso = verso;
    e->voci = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, voce_libera);
    e->inst = g_hash_table_new(g_direct_hash, g_direct_equal);
    e->inst_righe = g_queue_new();
    e->attivi = g_array_new(FALSE, FALSE, sizeof(uint64_t));
    e->radice = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    e->righe = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *scorri = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scorri), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scorri), altezza_max);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scorri), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scorri), e->righe);
    e->annulla_tutto = gtk_button_new_with_label(ph_t("Annulla tutti i trasferimenti"));
    gtk_widget_add_css_class(e->annulla_tutto, "ph-ghost");
    gtk_widget_add_css_class(e->annulla_tutto, "ph-bordato");
    gtk_widget_set_halign(e->annulla_tutto, GTK_ALIGN_END);
    gtk_widget_set_visible(e->annulla_tutto, FALSE);
    g_signal_connect(e->annulla_tutto, "clicked", G_CALLBACK(clic_annulla_tutto), e);
    e->resto = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(e->resto), 0.0f);
    gtk_widget_add_css_class(e->resto, "ph-piccolo");
    gtk_widget_add_css_class(e->resto, "ph-testo-3");
    gtk_widget_set_visible(e->resto, FALSE);
    gtk_box_append(GTK_BOX(e->radice), scorri);
    gtk_box_append(GTK_BOX(e->radice), e->resto);
    gtk_box_append(GTK_BOX(e->radice), e->annulla_tutto);
    gtk_widget_set_visible(e->radice, FALSE);
    g_object_set_data_full(G_OBJECT(e->radice), "ph-trf-elenco", e, elenco_libera);
    return e;
}

GtkWidget *ph_trf_elenco_widget(PhTrfElenco *e)
{
    return e->radice;
}

void ph_trf_elenco_aggiorna(PhTrfElenco *e, const PhSrvTrasferimento *v, size_t n)
{
    /* "Annulla tutti" copre TUTTA la coda del verso, non solo le righe mostrate. */
    g_array_set_size(e->attivi, 0);
    size_t del_verso = 0;
    for (size_t i = 0; i < n; i++) {
        if (v[i].verso != e->verso)
            continue;
        del_verso++;
        if (ph_trf_attivo(v[i].stato))
            g_array_append_val(e->attivi, v[i].id);
    }

    size_t scelte[PH_TRF_ELENCO_MAX], nascoste = 0;
    size_t ns = ph_trf_scegli_voci(v, n, e->verso, PH_TRF_ELENCO_MAX, scelte, &nascoste);

    /* Via le righe che non servono piu'. */
    GHashTable *tenere = g_hash_table_new(g_int64_hash, g_int64_equal);
    gint64 ids[PH_TRF_ELENCO_MAX];
    for (size_t k = 0; k < ns; k++) {
        ids[k] = (gint64)v[scelte[k]].id;
        g_hash_table_add(tenere, &ids[k]);
    }
    GHashTableIter it;
    gpointer chiave, valore;
    GPtrArray *da_togliere = g_ptr_array_new();
    g_hash_table_iter_init(&it, e->voci);
    while (g_hash_table_iter_next(&it, &chiave, &valore)) {
        if (!g_hash_table_contains(tenere, chiave))
            g_ptr_array_add(da_togliere, valore);
    }
    for (guint i = 0; i < da_togliere->len; i++) {
        Voce *x = g_ptr_array_index(da_togliere, i);
        gint64 id = (gint64)x->id;
        gtk_box_remove(GTK_BOX(e->righe), x->riga);
        g_hash_table_remove(e->voci, &id);
    }
    g_ptr_array_unref(da_togliere);
    g_hash_table_unref(tenere);

    /* Righe nuove e righe vecchie si aggiornano sul posto e si mettono nell'ordine scelto. */
    GtkWidget *prima = NULL;
    for (size_t k = 0; k < ns; k++) {
        const PhSrvTrasferimento *t = &v[scelte[k]];
        gint64 id = (gint64)t->id;
        Voce *x = g_hash_table_lookup(e->voci, &id);
        if (x == NULL) {
            x = voce_nuova(e, t->id);
            gint64 *chiave_nuova = g_new(gint64, 1);
            *chiave_nuova = id;
            g_hash_table_insert(e->voci, chiave_nuova, x);
            gtk_box_append(GTK_BOX(e->righe), x->riga);
        }
        voce_aggiorna(x, t);
        gtk_box_reorder_child_after(GTK_BOX(e->righe), x->riga, prima);
        prima = x->riga;
    }

    /* Voci ancora da fare che non ci stanno. */
    if (nascoste > 0) {
        char num[32], testo[128];
        g_snprintf(num, sizeof num, "%zu", nascoste);
        ph_lingua_componi(testo, sizeof testo, ph_t("e altri {numero} in attesa"), "numero", num, NULL);
        gtk_label_set_text(GTK_LABEL(e->resto), testo);
    }
    gtk_widget_set_visible(e->resto, nascoste > 0);
    gtk_widget_set_visible(e->annulla_tutto, e->attivi->len >= 2);
    /* Le installazioni in corso stanno sempre in cima. */
    prima = NULL;
    for (GList *l = e->inst_righe->head; l != NULL; l = l->next) {
        Voce *x = l->data;
        gtk_box_reorder_child_after(GTK_BOX(e->righe), x->riga, prima);
        prima = x->riga;
    }
    gtk_widget_set_visible(e->radice, del_verso > 0 || !g_queue_is_empty(e->inst_righe));
}

/* ---- Installazioni di APK (non sono nella coda dei trasferimenti) ---------------------------------- */
void ph_trf_elenco_inst_nuova(PhTrfElenco *e, guint chiave, guint id_servizio, const char *nome)
{
    Voce *v = voce_nuova(e, id_servizio);
    v->inst = true;
    char *sicuro = ph_trf_testo_sicuro(nome, 255);
    gtk_label_set_text(GTK_LABEL(v->nome), sicuro);
    gtk_label_set_text(GTK_LABEL(v->meta), ph_t("Installazione sul telefono"));
    gtk_label_set_text(GTK_LABEL(v->stato), ph_t("In attesa"));
    char et[600];
    ph_lingua_componi(et, sizeof et, ph_t("Annulla l'installazione di {nome}"), "nome", sicuro, NULL);
    ph_shell_etichetta(v->annulla, et, ph_t("Annulla"));
    ph_shell_etichetta(v->barra, sicuro, NULL);
    gtk_widget_set_visible(v->annulla, id_servizio != 0);
    g_free(sicuro);
    g_hash_table_insert(e->inst, GUINT_TO_POINTER(chiave), v);
    g_queue_push_tail(e->inst_righe, v);
    gtk_box_append(GTK_BOX(e->righe), v->riga);
    GtkWidget *prima = NULL; /* le installazioni stanno in cima, nell'ordine di arrivo */
    for (GList *l = e->inst_righe->head; l != NULL; l = l->next) {
        gtk_box_reorder_child_after(GTK_BOX(e->righe), ((Voce *)l->data)->riga, prima);
        prima = ((Voce *)l->data)->riga;
    }
    gtk_widget_set_visible(e->radice, TRUE);
}

void ph_trf_elenco_inst_avanza(PhTrfElenco *e, guint chiave, uint64_t fatti, uint64_t totale)
{
    Voce *v = g_hash_table_lookup(e->inst, GUINT_TO_POINTER(chiave));
    if (v == NULL)
        return;
    unsigned pct = ph_trf_percento(fatti, totale);
    char stato[64];
    if (totale > 0 && fatti >= totale) {
        /* Copia finita: ora lavora il telefono (puo' durare), niente percentuale. */
        g_strlcpy(stato, ph_t("In installazione"), sizeof stato);
        /* Da qui lavora il telefono: annullare non ha piu' effetto. */
        gtk_widget_set_visible(v->annulla, FALSE);
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(v->barra));
    } else {
        ph_trf_stato_testo(PH_SRV_TR_CORSO, pct, stato, sizeof stato);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(v->barra), pct / 100.0);
    }
    gtk_label_set_text(GTK_LABEL(v->stato), stato);
}

void ph_trf_elenco_inst_fine(PhTrfElenco *e, guint chiave)
{
    Voce *v = g_hash_table_lookup(e->inst, GUINT_TO_POINTER(chiave));
    if (v == NULL)
        return;
    g_queue_remove(e->inst_righe, v);
    g_hash_table_remove(e->inst, GUINT_TO_POINTER(chiave));
    gtk_box_remove(GTK_BOX(e->righe), v->riga);
    g_free(v);
    if (e->voci != NULL && g_hash_table_size(e->voci) == 0 && g_queue_is_empty(e->inst_righe))
        gtk_widget_set_visible(e->radice, FALSE);
}
