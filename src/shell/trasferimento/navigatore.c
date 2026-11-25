/* Navigatore di cartelle e file (navigatore.h). */
#include "shell/trasferimento/navigatore.h"

#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

#include "config/lingua.h"
#include "shell/trasferimento/percorsi.h"

/* Una voce dell'elenco (cartella o file) o un risultato di ricerca. */
typedef struct {
    char *testo;      /* da mostrare (nome; nel selettore di cartelle una ricerca mostra il percorso intero) */
    char *base;       /* ultimo pezzo del percorso, per le etichette accessibili */
    char *percorso;   /* intero, grezzo: serve a navigare e a richiedere */
    char *dove;       /* risultati di ricerca: cartella che la contiene (da mostrare); altrimenti NULL */
    bool cartella;
    uint64_t dimensione;
    bool ha_dim;      /* dimensione nota (anche dai risultati di ricerca) */
    GtkWidget *bottone, *casella; /* validi solo finche' la riga e' mostrata */
} Riga;

struct PhNav;
typedef struct PcLavoro PcLavoro;

struct PhNav {
    PhShell *sh;
    PhNavModo modo;
    PhNavGestori g;
    void *utente;
    bool morto;

    char *percorso;       /* cartella mostrata, intera */
    char *messaggio;      /* al posto di "vuoto" quando la lettura e' fallita, o NULL */
    bool caricato;        /* l'elenco di `percorso` e' arrivato */
    bool negato;          /* accesso non consentito dal sistema */
    GPtrArray *voci;      /* Riga *: l'elenco (nascosti e, per i selettori, file esclusi) */
    GPtrArray *nomi;      /* char *: tutti i nomi grezzi della cartella, per "Nuova cartella" */
    GPtrArray *trovati;   /* Riga *: risultati della ricerca */
    bool in_ricerca, ricerca_finita;
    char *ricerca_errore;
    size_t pagine, resa;  /* pagine di PH_TRF_PAGINA mostrate; righe disegnate */

    GPtrArray *scelti;    /* char *: file scelti (percorsi interi) */
    GHashTable *scelti_set; /* stesse stringhe, per cercare */

    GtkWidget *radice, *su, *campo, *cerca, *scorri, *righe, *vuoto, *altri, *nuova, *usa;
    GCancellable *canc_elenco, *canc_cerca, *canc_crea;
    char *att_percorso;   /* cartella richiesta e non ancora arrivata */
    bool att_iniziale;    /* e' la richiesta di ph_nav_apri (non una scelta dell'utente) */
    guint timer_cerca;
    int blocca;           /* > 0: i campi di testo cambiano da soli, niente reazioni */
    bool creando;
    bool in_corso;        /* una richiesta di elenco e' partita e non e' ancora arrivata */
    bool campo_toccato;   /* l'utente ha scritto nel campo del percorso dopo l'ultima richiesta */
    char *att_digitato;   /* testo digitato che ha chiesto la richiesta in corso, per l'avviso */
    char *digitato_prossimo;
    char *nuova_percorso; /* cartella telefono in creazione */
    PcLavoro *pc;         /* elenco del PC in corso */
};

static void riga_libera(gpointer p)
{
    Riga *r = p;
    g_free(r->testo);
    g_free(r->base);
    g_free(r->percorso);
    g_free(r->dove);
    g_free(r);
}

static void nav_pulisci(gpointer p)
{
    PhNav *n = p;
    if (n->timer_cerca != 0)
        g_source_remove(n->timer_cerca);
    g_clear_pointer(&n->voci, g_ptr_array_unref);
    g_clear_pointer(&n->nomi, g_ptr_array_unref);
    g_clear_pointer(&n->trovati, g_ptr_array_unref);
    g_clear_pointer(&n->scelti_set, g_hash_table_unref);
    g_clear_pointer(&n->scelti, g_ptr_array_unref);
    g_clear_object(&n->canc_elenco);
    g_clear_object(&n->canc_cerca);
    g_clear_object(&n->canc_crea);
    g_free(n->percorso);
    g_free(n->messaggio);
    g_free(n->ricerca_errore);
    g_free(n->att_percorso);
    g_free(n->nuova_percorso);
    g_free(n->att_digitato);
    g_free(n->digitato_prossimo);
}

GtkWidget *ph_trf_etichetta_ellissi(const char *classe)
{
    GtkWidget *l = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    /* Larghezza naturale minima: il testo lungo non allarga il pannello. */
    gtk_label_set_max_width_chars(GTK_LABEL(l), 1);
    gtk_widget_set_hexpand(l, TRUE);
    if (classe != NULL)
        gtk_widget_add_css_class(l, classe);
    return l;
}

static PhServizi *servizi(PhNav *n)
{
    return ph_shell_servizi(n->sh);
}

static bool e_telefono(const PhNav *n)
{
    return n->modo != PH_NAV_PC_CARTELLE;
}

static bool vuole_file(const PhNav *n)
{
    return n->modo == PH_NAV_TEL_FILE;
}

static GPtrArray *vista(PhNav *n)
{
    return n->in_ricerca ? n->trovati : n->voci;
}

/* ---- Selezione ----------------------------------------------------------- */

size_t ph_nav_scelti_n(PhNav *n)
{
    return n->scelti->len;
}

const char *ph_nav_scelto(PhNav *n, size_t i)
{
    return i < n->scelti->len ? g_ptr_array_index(n->scelti, i) : NULL;
}

static void scelta_notifica(PhNav *n)
{
    if (n->g.selezione != NULL)
        n->g.selezione(n->utente, n->scelti->len);
}

static void scelti_togli_silenzioso(PhNav *n, const char *percorso)
{
    if (!g_hash_table_remove(n->scelti_set, percorso))
        return;
    for (guint i = 0; i < n->scelti->len; i++) {
        if (strcmp(g_ptr_array_index(n->scelti, i), percorso) == 0) {
            g_ptr_array_remove_index(n->scelti, i);
            break;
        }
    }
}

static void riga_aggiorna_scelta(PhNav *n, Riga *r);

void ph_nav_scelti_togli(PhNav *n, const char *percorso_dato)
{
    /* `percorso_dato` puo' essere la stringa dell'elenco dei scelti, che si libera qui sotto. */
    char *percorso = g_strdup(percorso_dato);
    scelti_togli_silenzioso(n, percorso);
    for (guint k = 0; k < 2; k++) {
        GPtrArray *a = k == 0 ? n->voci : n->trovati;
        for (guint i = 0; i < a->len; i++) {
            Riga *r = g_ptr_array_index(a, i);
            if (!r->cartella && strcmp(r->percorso, percorso) == 0)
                riga_aggiorna_scelta(n, r);
        }
    }
    g_free(percorso);
    scelta_notifica(n);
}

void ph_nav_scelti_svuota(PhNav *n)
{
    g_hash_table_remove_all(n->scelti_set);
    g_ptr_array_set_size(n->scelti, 0);
    for (guint k = 0; k < 2; k++) {
        GPtrArray *a = k == 0 ? n->voci : n->trovati;
        for (guint i = 0; i < a->len; i++) {
            Riga *r = g_ptr_array_index(a, i);
            if (!r->cartella)
                riga_aggiorna_scelta(n, r);
        }
    }
    scelta_notifica(n);
}

/* ---- Righe --------------------------------------------------------------- */

static char *etichetta_riga(const Riga *r, bool scelta)
{
    char buf[1200];
    if (r->cartella)
        ph_lingua_componi(buf, sizeof buf, ph_t("Apri la cartella {nome}"), "nome", r->base, NULL);
    else if (scelta)
        ph_lingua_componi(buf, sizeof buf, ph_t("Deseleziona {nome}"), "nome", r->base, NULL);
    else
        ph_lingua_componi(buf, sizeof buf, ph_t("Seleziona {nome}"), "nome", r->base, NULL);
    return g_strdup(buf);
}

/* Aspetto e etichetta di un file secondo che sia scelto o no. */
static void riga_aggiorna_scelta(PhNav *n, Riga *r)
{
    if (r->bottone == NULL || r->casella == NULL)
        return;
    bool scelta = g_hash_table_contains(n->scelti_set, r->percorso);
    if (scelta)
        gtk_widget_add_css_class(r->casella, "ph-trasferimento-casella-scelta");
    else
        gtk_widget_remove_css_class(r->casella, "ph-trasferimento-casella-scelta");
    char *et = etichetta_riga(r, scelta);
    ph_shell_etichetta(r->bottone, et, NULL);
    g_free(et);
}

static void vai(PhNav *n, const char *percorso, bool iniziale);

static void clic_riga(GtkButton *b, gpointer dati)
{
    (void)b;
    Riga *r = dati;
    PhNav *n = g_object_get_data(G_OBJECT(b), "ph-nav");
    if (n == NULL || n->morto)
        return;
    if (r->cartella) {
        /* La riga puo' sparire con la navigazione: si usa una copia. */
        char *p = g_strdup(r->percorso);
        vai(n, p, false);
        g_free(p);
        return;
    }
    if (g_hash_table_contains(n->scelti_set, r->percorso)) {
        scelti_togli_silenzioso(n, r->percorso);
    } else {
        char *p = g_strdup(r->percorso);
        g_ptr_array_add(n->scelti, p);
        g_hash_table_add(n->scelti_set, p);
    }
    riga_aggiorna_scelta(n, r);
    scelta_notifica(n);
}

static GtkWidget *riga_widget(PhNav *n, Riga *r)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "ph-riga");
    g_object_set_data(G_OBJECT(b), "ph-nav", n);
    GtkWidget *scatola = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    if (r->cartella) {
        GtkWidget *f = gtk_label_new(">");
        gtk_widget_add_css_class(f, "ph-testo-3");
        gtk_widget_set_size_request(f, 16, -1);
        gtk_box_append(GTK_BOX(scatola), f);
    } else {
        r->casella = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(r->casella, "ph-trasferimento-casella");
        gtk_widget_set_valign(r->casella, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(scatola), r->casella);
    }
    GtkWidget *nome = ph_trf_etichetta_ellissi(NULL);
    gtk_label_set_text(GTK_LABEL(nome), r->testo);
    gtk_box_append(GTK_BOX(scatola), nome);

    /* A destra: "Cartella", la dimensione o, cercando, la cartella che contiene. */
    char *meta = NULL;
    if (vuole_file(n)) {
        if (r->dove != NULL && !r->cartella && r->ha_dim) {
            /* Ricerca: "{cartella} · {dimensione}" come il mockup. */
            char dim[64], buf[PH_TRF_PERCORSO_MAX + 128];
            ph_trf_dimensione(r->dimensione, dim, sizeof dim);
            ph_lingua_componi(buf, sizeof buf, ph_t("{cartella} · {dimensione}"), "cartella", r->dove,
                              "dimensione", dim, NULL);
            meta = g_strdup(buf);
        } else if (r->dove != NULL) {
            meta = g_strdup(r->dove);
        } else if (r->cartella) {
            meta = g_strdup(ph_t("Cartella"));
        } else {
            char dim[64];
            ph_trf_dimensione(r->dimensione, dim, sizeof dim);
            meta = g_strdup(dim);
        }
    }
    if (meta != NULL) {
        GtkWidget *m = gtk_label_new(meta);
        gtk_widget_add_css_class(m, "ph-minimo");
        gtk_widget_add_css_class(m, "ph-testo-3");
        gtk_label_set_ellipsize(GTK_LABEL(m), PANGO_ELLIPSIZE_START);
        gtk_label_set_max_width_chars(GTK_LABEL(m), 28);
        gtk_widget_set_tooltip_text(m, meta);
        gtk_box_append(GTK_BOX(scatola), m);
        g_free(meta);
    }
    gtk_button_set_child(GTK_BUTTON(b), scatola);
    r->bottone = b;
    if (r->cartella) {
        char *et = etichetta_riga(r, false);
        ph_shell_etichetta(b, et, NULL);
        g_free(et);
    } else {
        riga_aggiorna_scelta(n, r);
    }
    g_signal_connect(b, "clicked", G_CALLBACK(clic_riga), r);
    return b;
}

static void aggiorna_altri(PhNav *n)
{
    GPtrArray *a = vista(n);
    size_t altri = ph_trf_pagina_prossime(a->len, n->resa);
    if (altri == 0) {
        gtk_widget_set_visible(n->altri, FALSE);
        return;
    }
    char num[32], testo[128];
    g_snprintf(num, sizeof num, "%zu", altri);
    ph_lingua_componi(testo, sizeof testo, ph_t("Mostra altri {numero}"), "numero", num, NULL);
    gtk_button_set_label(GTK_BUTTON(n->altri), testo);
    gtk_widget_set_visible(n->altri, TRUE);
}

static void aggiorna_vuoto(PhNav *n)
{
    GPtrArray *a = vista(n);
    const char *testo = NULL;
    if (a->len == 0) {
        if (n->in_ricerca) {
            if (n->ricerca_finita)
                testo = n->ricerca_errore != NULL ? n->ricerca_errore : ph_t("Nessun risultato");
        } else if (n->messaggio != NULL) {
            testo = n->messaggio;
        } else if (n->caricato) {
            if (n->negato)
                testo = ph_t("Accesso non consentito dal sistema");
            else
                testo = vuole_file(n) ? ph_t("Cartella vuota") : ph_t("Nessuna sottocartella");
        }
    }
    if (testo != NULL)
        gtk_label_set_text(GTK_LABEL(n->vuoto), testo);
    gtk_widget_set_visible(n->vuoto, testo != NULL);
}

/* Disegna le righe che mancano fino alla pagina corrente. */
static void aggiungi_righe(PhNav *n)
{
    GPtrArray *a = vista(n);
    size_t fino = ph_trf_pagina_visibili(a->len, n->pagine);
    for (; n->resa < fino; n->resa++)
        gtk_box_append(GTK_BOX(n->righe), riga_widget(n, g_ptr_array_index(a, n->resa)));
    aggiorna_altri(n);
    aggiorna_vuoto(n);
}

static void scollega_righe(GPtrArray *a)
{
    for (guint i = 0; i < a->len; i++) {
        Riga *r = g_ptr_array_index(a, i);
        r->bottone = NULL;
        r->casella = NULL;
    }
}

/* Svuota le righe mostrate (non i dati). */
static void togli_righe(PhNav *n)
{
    scollega_righe(n->voci);
    scollega_righe(n->trovati);
    GtkWidget *f;
    while ((f = gtk_widget_get_first_child(n->righe)) != NULL)
        gtk_box_remove(GTK_BOX(n->righe), f);
    n->resa = 0;
}

static void ridisegna(PhNav *n)
{
    togli_righe(n);
    n->pagine = 1;
    aggiungi_righe(n);
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(n->scorri));
    gtk_adjustment_set_value(adj, 0);
}

static void clic_altri(GtkButton *b, gpointer dati)
{
    (void)b;
    PhNav *n = dati;
    size_t prima = n->resa;
    n->pagine++;
    aggiungi_righe(n);
    GPtrArray *a = vista(n);
    if (prima < n->resa) {
        Riga *r = g_ptr_array_index(a, prima);
        if (r->bottone != NULL)
            gtk_widget_grab_focus(r->bottone);
    }
}

/* ---- Cartella mostrata ----------------------------------------------------- */

/* "Nuova cartella" solo con un elenco arrivato e nessuna richiesta o creazione in corso. */
static void aggiorna_nuova(PhNav *n)
{
    if (n->nuova != NULL)
        gtk_widget_set_sensitive(n->nuova, n->caricato && !n->in_corso && !n->creando);
}

/* Aggiorna il percorso scritto (se l'utente non ci sta scrivendo) e il pulsante "Su". */
static void mostra_percorso(PhNav *n)
{
    if (!n->campo_toccato) {
        char *sicuro = ph_trf_testo_sicuro(n->percorso, PH_TRF_PERCORSO_MAX);
        n->blocca++;
        gtk_editable_set_text(GTK_EDITABLE(n->campo), sicuro);
        n->blocca--;
        g_free(sicuro);
    }
    gtk_widget_set_sensitive(n->su, !ph_trf_e_radice(n->percorso));
}

static void ferma_ricerca(PhNav *n)
{
    if (n->timer_cerca != 0) {
        g_source_remove(n->timer_cerca);
        n->timer_cerca = 0;
    }
    if (n->canc_cerca != NULL) {
        g_cancellable_cancel(n->canc_cerca);
        g_clear_object(&n->canc_cerca);
    }
    g_ptr_array_set_size(n->trovati, 0);
    n->ricerca_finita = false;
    g_clear_pointer(&n->ricerca_errore, g_free);
}

/* Esce dalla ricerca: campo vuoto, risultati via. */
static void chiudi_ricerca(PhNav *n)
{
    ferma_ricerca(n);
    n->in_ricerca = false;
    if (n->cerca != NULL) {
        n->blocca++;
        gtk_editable_set_text(GTK_EDITABLE(n->cerca), "");
        n->blocca--;
    }
}

/* Un elenco e' arrivato: diventa la cartella mostrata. Prende possesso di
 * voci e nomi. */
static void applica(PhNav *n, const char *percorso, bool negato, GPtrArray *voci, GPtrArray *nomi)
{
    togli_righe(n);
    g_ptr_array_unref(n->voci);
    g_ptr_array_unref(n->nomi);
    n->voci = voci;
    n->nomi = nomi;
    g_free(n->percorso);
    n->percorso = g_strdup(percorso);
    g_clear_pointer(&n->messaggio, g_free);
    n->caricato = true;
    n->negato = negato;
    n->in_corso = false;
    mostra_percorso(n);
    ridisegna(n);
    aggiorna_nuova(n);
}

/* Lettura fallita (non "accesso negato"). */
static void fallita(PhNav *n, const char *percorso, PhSrvEsito esito, const char *frase)
{
    char *sicuro = ph_trf_testo_sicuro(percorso, 512);
    const char *titolo;
    const char *testo;
    char *frase_sicura = ph_trf_testo_sicuro(frase, 256);
    char *digitato = n->att_digitato != NULL ? ph_trf_testo_sicuro(n->att_digitato, 512) : NULL;
    n->in_corso = false;
    if (esito == PH_SRV_ESITO_NON_TROVATO) {
        titolo = ph_t("Cartella non trovata");
        testo = digitato != NULL ? digitato : sicuro;
    } else {
        titolo = ph_t("Non riuscito");
        testo = frase_sicura;
    }
    if (n->att_iniziale) {
        /* All'apertura: nessun avviso, il motivo sta dove ci sarebbe l'elenco. */
        togli_righe(n);
        g_ptr_array_set_size(n->voci, 0);
        g_ptr_array_set_size(n->nomi, 0);
        g_free(n->messaggio);
        n->messaggio = g_strdup(esito == PH_SRV_ESITO_NON_TROVATO ? titolo : (frase_sicura[0] != '\0' ? frase_sicura : titolo));
        n->caricato = true;
        n->negato = false;
        mostra_percorso(n);
        ridisegna(n);
    } else {
        ph_shell_avviso(n->sh, titolo, testo);
        mostra_percorso(n); /* il campo torna alla cartella che c'e' davvero */
    }
    aggiorna_nuova(n);
    g_free(sicuro);
    g_free(digitato);
    g_free(frase_sicura);
}

/* ---- Telefono: elenco ------------------------------------------------------- */

static Riga *riga_da_elenco(const char *cartella, const PhSrvVoce *v)
{
    char pieno[PH_TRF_PERCORSO_MAX + 1];
    if (!ph_trf_unisci(cartella, v->nome, pieno, sizeof pieno))
        return NULL;
    Riga *r = g_new0(Riga, 1);
    r->testo = ph_trf_testo_sicuro(v->nome_schermo != NULL ? v->nome_schermo : v->nome, 255);
    r->base = g_strdup(r->testo);
    r->percorso = g_strdup(pieno);
    r->cartella = v->cartella;
    r->dimensione = v->dimensione;
    return r;
}

static void tel_elenco_fatto(PhServizi *s, PhSrvEsito esito, const char *frase, const char *cartella,
                             const PhSrvVoce *voci, size_t nv, void *utente)
{
    (void)s;
    (void)cartella;
    PhNav *n = utente;
    if (n->morto || n->att_percorso == NULL)
        return;
    char *percorso = n->att_percorso;
    n->att_percorso = NULL;
    if (esito != PH_SRV_ESITO_OK && esito != PH_SRV_ESITO_NON_CONSENTITO) {
        fallita(n, percorso, esito, frase);
        g_free(percorso);
        return;
    }
    GPtrArray *nuove = g_ptr_array_new_with_free_func(riga_libera);
    GPtrArray *nomi = g_ptr_array_new_with_free_func(g_free);
    for (size_t i = 0; esito == PH_SRV_ESITO_OK && voci != NULL && i < nv; i++) {
        if (voci[i].nome == NULL || !ph_trf_nome_valido(voci[i].nome))
            continue;
        g_ptr_array_add(nomi, g_strdup(voci[i].nome));
        if (ph_trf_nascosto(voci[i].nome))
            continue;
        if (!vuole_file(n) && !voci[i].cartella)
            continue;
        Riga *r = riga_da_elenco(percorso, &voci[i]);
        if (r != NULL)
            g_ptr_array_add(nuove, r);
    }
    applica(n, percorso, esito == PH_SRV_ESITO_NON_CONSENTITO, nuove, nomi);
    g_free(percorso);
}

/* ---- PC: elenco con GIO ------------------------------------------------------ */

struct PcLavoro {
    PhNav *n;           /* con un riferimento (g_rc_box_acquire) */
    char *percorso;
    GCancellable *c;
    GFileEnumerator *en; /* tenuto dal lavoro finche' dura */
    GPtrArray *voci;
    GPtrArray *nomi;
    bool iniziale;      /* richiesta di ph_nav_apri: se fallisce si ripiega sulla home */
};

static void pc_libera(PcLavoro *l)
{
    g_free(l->percorso);
    g_clear_object(&l->c);
    g_clear_object(&l->en);
    g_ptr_array_unref(l->voci);
    g_ptr_array_unref(l->nomi);
    PhNav *n = l->n;
    g_free(l);
    g_rc_box_release_full(n, nav_pulisci);
}

static void pc_elenca(PhNav *n, const char *percorso, bool iniziale);

static gint confronta_righe(gconstpointer a, gconstpointer b)
{
    const Riga *x = *(Riga *const *)a, *y = *(Riga *const *)b;
    char *cx = g_utf8_casefold(x->testo, -1), *cy = g_utf8_casefold(y->testo, -1);
    int r = strcmp(cx, cy);
    g_free(cx);
    g_free(cy);
    return r != 0 ? r : strcmp(x->percorso, y->percorso);
}

/* Fine del lavoro: err != NULL = fallito (NULL = tutto letto). Libera il lavoro. */
static void pc_fine(PcLavoro *l, GError *err)
{
    PhNav *n = l->n;
    bool vivo = !n->morto && n->pc == l;
    if (vivo && err != NULL && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        vivo = false;
    if (vivo)
        n->pc = NULL;
    if (!vivo) {
        pc_libera(l);
        return;
    }
    if (err == NULL) {
        g_ptr_array_sort(l->voci, confronta_righe);
        GPtrArray *v = l->voci, *nm = l->nomi;
        l->voci = g_ptr_array_new_with_free_func(riga_libera);
        l->nomi = g_ptr_array_new_with_free_func(g_free);
        applica(n, l->percorso, false, v, nm);
    } else if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED)) {
        applica(n, l->percorso, true, g_ptr_array_new_with_free_func(riga_libera),
                g_ptr_array_new_with_free_func(g_free));
    } else if (l->iniziale && strcmp(l->percorso, g_get_home_dir()) != 0) {
        /* La destinazione non c'e' piu': si parte dalla cartella personale. */
        char *casa = g_strdup(g_get_home_dir());
        pc_elenca(n, casa, true);
        g_free(casa);
    } else {
        PhSrvEsito es = g_error_matches(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
                                g_error_matches(err, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY)
                            ? PH_SRV_ESITO_NON_TROVATO
                            : PH_SRV_ESITO_ERRORE;
        /* Il testo di GIO e' nella lingua del PC e tecnico: nel log; a schermo una frase tradotta. */
        fprintf(stderr, "trasferimento: lettura della cartella del PC non riuscita: %s\n", err->message);
        fallita(n, l->percorso, es, ph_t("Non riesco a leggere questa cartella del PC."));
    }
    pc_libera(l);
}

/* La chiusura dell'enumeratore non blocca il thread GTK; il riferimento passato
 * come dato la tiene viva fino alla fine. */
static void pc_chiuso(GObject *src, GAsyncResult *res, gpointer dati)
{
    g_file_enumerator_close_finish(G_FILE_ENUMERATOR(src), res, NULL);
    g_object_unref(dati);
}

static void pc_altri(GObject *src, GAsyncResult *res, gpointer dati);

static void pc_batch_avanti(PcLavoro *l)
{
    g_file_enumerator_next_files_async(l->en, 128, G_PRIORITY_DEFAULT, l->c, pc_altri, l);
}

static void pc_altri(GObject *src, GAsyncResult *res, gpointer dati)
{
    PcLavoro *l = dati;
    GError *err = NULL;
    GList *lista = g_file_enumerator_next_files_finish(G_FILE_ENUMERATOR(src), res, &err);
    if (err == NULL && lista != NULL) {
        for (GList *p = lista; p != NULL; p = p->next) {
            GFileInfo *info = p->data;
            const char *nome = g_file_info_get_name(info);
            if (nome == NULL || !ph_trf_nome_valido(nome))
                continue;
            g_ptr_array_add(l->nomi, g_strdup(nome));
            if (ph_trf_nascosto(nome) || g_file_info_get_file_type(info) != G_FILE_TYPE_DIRECTORY)
                continue;
            char pieno[PH_TRF_PERCORSO_MAX + 1];
            if (!ph_trf_unisci(l->percorso, nome, pieno, sizeof pieno))
                continue;
            Riga *r = g_new0(Riga, 1);
            const char *vis = g_file_info_get_display_name(info);
            r->testo = ph_trf_testo_sicuro(vis != NULL ? vis : nome, 255);
            r->base = g_strdup(r->testo);
            r->percorso = g_strdup(pieno);
            r->cartella = true;
            g_ptr_array_add(l->voci, r);
        }
        g_list_free_full(lista, g_object_unref);
        pc_batch_avanti(l);
        return;
    }
    g_list_free_full(lista, g_object_unref);
    g_file_enumerator_close_async(l->en, G_PRIORITY_DEFAULT, NULL, pc_chiuso, g_object_ref(l->en));
    pc_fine(l, err);
    g_clear_error(&err);
}

static void pc_aperto(GObject *src, GAsyncResult *res, gpointer dati)
{
    PcLavoro *l = dati;
    GError *err = NULL;
    l->en = g_file_enumerate_children_finish(G_FILE(src), res, &err);
    if (l->en == NULL) {
        pc_fine(l, err);
        g_clear_error(&err);
        return;
    }
    pc_batch_avanti(l);
}

static void pc_elenca(PhNav *n, const char *percorso, bool iniziale)
{
    if (n->canc_elenco != NULL) {
        g_cancellable_cancel(n->canc_elenco);
        g_clear_object(&n->canc_elenco);
    }
    n->canc_elenco = g_cancellable_new();
    PcLavoro *l = g_new0(PcLavoro, 1);
    l->n = g_rc_box_acquire(n);
    l->percorso = g_strdup(percorso);
    l->c = g_object_ref(n->canc_elenco);
    l->voci = g_ptr_array_new_with_free_func(riga_libera);
    l->nomi = g_ptr_array_new_with_free_func(g_free);
    l->iniziale = iniziale;
    n->pc = l;
    n->att_iniziale = iniziale;
    GFile *f = g_file_new_for_path(percorso);
    g_file_enumerate_children_async(f, "standard::name,standard::display-name,standard::type",
                                    G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT, l->c, pc_aperto, l);
    g_object_unref(f);
}

/* ---- Navigare ------------------------------------------------------------------ */

static void vai(PhNav *n, const char *percorso, bool iniziale)
{
    if (n->morto)
        return;
    /* Chi chiede di navigare chiude la ricerca (non l'arrivo dell'elenco: l'utente
     * potrebbe aver gia' ricominciato a scrivere); il campo del percorso torna
     * a essere riscrivibile da noi. */
    if (n->in_ricerca) {
        chiudi_ricerca(n);
        ridisegna(n);
    }
    n->in_corso = true;
    n->campo_toccato = false;
    g_free(n->att_digitato);
    n->att_digitato = n->digitato_prossimo;
    n->digitato_prossimo = NULL;
    aggiorna_nuova(n);
    if (!e_telefono(n)) {
        pc_elenca(n, percorso, iniziale);
        return;
    }
    if (n->canc_elenco != NULL) {
        g_cancellable_cancel(n->canc_elenco);
        g_clear_object(&n->canc_elenco);
    }
    n->canc_elenco = g_cancellable_new();
    g_free(n->att_percorso);
    n->att_percorso = g_strdup(percorso);
    n->att_iniziale = iniziale;
    ph_srv_tel_elenca(servizi(n), percorso, n->canc_elenco, tel_elenco_fatto, n);
}

void ph_nav_apri(PhNav *n, const char *percorso)
{
    if (n->morto)
        return;
    /* `percorso` puo' essere n->percorso stesso (ph_nav_percorso): si copia prima di liberarlo. */
    char *nuovo = g_strdup(percorso != NULL && percorso[0] == '/' ? percorso : "/");
    /* L'elenco vecchio non vale piu': si parte da vuoto e dal percorso nuovo. */
    togli_righe(n);
    g_ptr_array_set_size(n->voci, 0);
    g_ptr_array_set_size(n->nomi, 0);
    g_free(n->percorso);
    n->percorso = nuovo;
    g_clear_pointer(&n->messaggio, g_free);
    n->caricato = false;
    n->negato = false;
    n->campo_toccato = false;
    chiudi_ricerca(n);
    mostra_percorso(n);
    ridisegna(n);
    /* Una creazione rimasta a meta' (selettore chiuso) si annulla. */
    if (n->canc_crea != NULL) {
        g_cancellable_cancel(n->canc_crea);
        g_clear_object(&n->canc_crea);
    }
    g_clear_pointer(&n->nuova_percorso, g_free);
    n->creando = false;
    char *p = g_strdup(n->percorso);
    vai(n, p, true);
    g_free(p);
}

static void ferma_tutto(PhNav *n)
{
    if (n->canc_elenco != NULL) {
        g_cancellable_cancel(n->canc_elenco);
        g_clear_object(&n->canc_elenco);
    }
    if (n->canc_crea != NULL) {
        g_cancellable_cancel(n->canc_crea);
        g_clear_object(&n->canc_crea);
    }
    n->pc = NULL;
    g_clear_pointer(&n->att_percorso, g_free);
    ferma_ricerca(n);
    n->creando = false;
    n->in_corso = false;
    aggiorna_nuova(n);
}

void ph_nav_ferma(PhNav *n)
{
    /* Le righe mostrate puntano ai dati che ferma_tutto libera: via anche quelle. */
    togli_righe(n);
    ferma_tutto(n);
}

const char *ph_nav_percorso(PhNav *n)
{
    return n->percorso;
}

static void clic_su(GtkButton *b, gpointer dati)
{
    (void)b;
    PhNav *n = dati;
    char su[PH_TRF_PERCORSO_MAX + 1];
    if (ph_trf_su(n->percorso, su, sizeof su))
        vai(n, su, false);
}

static void campo_cambiato(GtkEditable *e, gpointer dati)
{
    (void)e;
    PhNav *n = dati;
    if (n->blocca == 0 && !n->morto)
        n->campo_toccato = true;
}

static void campo_invio(GtkEntry *e, gpointer dati)
{
    (void)e;
    PhNav *n = dati;
    const char *digitato = gtk_editable_get_text(GTK_EDITABLE(n->campo));
    /* Se il testo e' quello mostrato (ripulito o tagliato per lo schermo) vale la cartella vera. */
    char *mostrato = ph_trf_testo_sicuro(n->percorso, PH_TRF_PERCORSO_MAX);
    bool uguale = strcmp(digitato, mostrato) == 0;
    g_free(mostrato);
    char norm[PH_TRF_PERCORSO_MAX + 1];
    if (uguale) {
        g_strlcpy(norm, n->percorso, sizeof norm);
    } else {
        bool ok = e_telefono(n) ? ph_trf_normalizza(digitato, norm, sizeof norm)
                                : ph_trf_normalizza_pc(digitato, n->percorso, norm, sizeof norm);
        if (!ok) {
            char *sicuro = ph_trf_testo_sicuro(digitato, 512);
            ph_shell_avviso(n->sh, ph_t("Cartella non trovata"), sicuro);
            g_free(sicuro);
            n->campo_toccato = false;
            mostra_percorso(n);
            return;
        }
        g_free(n->digitato_prossimo);
        n->digitato_prossimo = g_strdup(digitato);
    }
    vai(n, norm, false);
}

/* ---- Telefono: ricerca ------------------------------------------------------------ */

static void cerca_trovati(PhServizi *s, const PhSrvTrovato *v, size_t nv, void *utente)
{
    (void)s;
    PhNav *n = utente;
    if (n->morto || !n->in_ricerca)
        return;
    for (size_t i = 0; v != NULL && i < nv; i++) {
        if (v[i].percorso == NULL || v[i].percorso_schermo == NULL)
            continue;
        if (n->trovati->len >= 500 || strlen(v[i].percorso) > PH_TRF_PERCORSO_MAX)
            continue;
        if (ph_trf_percorso_nascosto(v[i].percorso))
            continue;
        Riga *r = g_new0(Riga, 1);
        char *schermo = ph_trf_testo_sicuro(v[i].percorso_schermo, PH_TRF_PERCORSO_MAX);
        const char *ultimo = strrchr(schermo, '/');
        r->base = g_strdup(ultimo != NULL && ultimo[1] != '\0' ? ultimo + 1 : schermo);
        if (ultimo != NULL && ultimo != schermo)
            r->dove = g_strndup(schermo, (gsize)(ultimo - schermo));
        else
            r->dove = g_strdup("/");
        /* Il selettore di cartelle mostra il percorso intero, l'elenco di scarica il nome. */
        r->testo = vuole_file(n) ? g_strdup(r->base) : g_strdup(schermo);
        r->percorso = g_strdup(v[i].percorso);
        r->cartella = v[i].cartella;
        if (!r->cartella)
            r->ha_dim = ph_srv_trovato_dimensione(servizi(n), &v[i], &r->dimensione);
        g_free(schermo);
        g_ptr_array_add(n->trovati, r);
    }
    aggiungi_righe(n);
}

static void cerca_fine(PhServizi *s, PhSrvEsito esito, const char *frase, bool massimo, void *utente)
{
    (void)s;
    (void)massimo;
    PhNav *n = utente;
    if (n->morto || !n->in_ricerca)
        return;
    n->ricerca_finita = true;
    g_clear_pointer(&n->ricerca_errore, g_free);
    if (esito == PH_SRV_ESITO_NON_CONSENTITO)
        n->ricerca_errore = g_strdup(ph_t("Accesso non consentito dal sistema"));
    else if (esito != PH_SRV_ESITO_OK)
        n->ricerca_errore = ph_trf_testo_sicuro(frase != NULL && frase[0] != '\0' ? frase : ph_t("Non riuscito"), 256);
    aggiorna_vuoto(n);
}

static gboolean cerca_parti(gpointer dati)
{
    PhNav *n = dati;
    n->timer_cerca = 0;
    if (n->morto || !n->in_ricerca)
        return G_SOURCE_REMOVE;
    char *testo = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(n->cerca))));
    if (testo[0] != '\0') {
        if (n->canc_cerca != NULL) {
            g_cancellable_cancel(n->canc_cerca);
            g_clear_object(&n->canc_cerca);
        }
        n->canc_cerca = g_cancellable_new();
        /* Cartelle soltanto nel selettore di destinazione; file e cartelle nell'elenco di scarica. */
        ph_srv_tel_cerca(servizi(n), "/", testo, !vuole_file(n), 30, n->canc_cerca, cerca_trovati,
                         cerca_fine, n);
    }
    g_free(testo);
    return G_SOURCE_REMOVE;
}

static void cerca_cambiata(GtkEditable *e, gpointer dati)
{
    (void)e;
    PhNav *n = dati;
    if (n->blocca > 0 || n->morto)
        return;
    /* Ogni tasto annulla la ricerca di prima e butta via i suoi risultati: dopo
     * una ricerca nuova non si vede mai niente di vecchio. */
    ferma_ricerca(n);
    char *testo = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(n->cerca))));
    n->in_ricerca = testo[0] != '\0';
    g_free(testo);
    ridisegna(n);
    if (n->in_ricerca)
        n->timer_cerca = g_timeout_add(250, cerca_parti, n);
}

/* ---- Nuova cartella e "Usa questa cartella" ---------------------------------------------- */

static void nuova_fine(PhNav *n)
{
    n->creando = false;
    aggiorna_nuova(n);
}

static void tel_creata(PhServizi *s, PhSrvEsito esito, const char *frase, void *utente)
{
    (void)s;
    PhNav *n = utente;
    if (n->morto || !n->creando)
        return;
    nuova_fine(n);
    char *p = n->nuova_percorso;
    n->nuova_percorso = NULL;
    if (esito == PH_SRV_ESITO_OK && p != NULL) {
        vai(n, p, false);
    } else if (esito == PH_SRV_ESITO_NON_CONSENTITO) {
        ph_shell_avviso(n->sh, ph_t("Accesso non consentito dal sistema"), "");
    } else {
        char *f = ph_trf_testo_sicuro(frase, 256);
        ph_shell_avviso(n->sh, ph_t("Non riuscito"), f);
        g_free(f);
    }
    g_free(p);
}

typedef struct {
    PhNav *n; /* con un riferimento */
    char *nome;
    char *cartella; /* dove si crea: quella di quando si e' premuto il pulsante */
    int tentativi;
} PcNuova;

static void pc_nuova_libera(PcNuova *p)
{
    PhNav *n = p->n;
    g_free(p->nome);
    g_free(p->cartella);
    g_free(p);
    g_rc_box_release_full(n, nav_pulisci);
}

static void pc_nuova_prova(PhNav *n, PcNuova *p);

static void pc_nuova_fatta(GObject *src, GAsyncResult *res, gpointer dati)
{
    PcNuova *p = dati;
    PhNav *n = p->n;
    GError *err = NULL;
    bool ok = g_file_make_directory_finish(G_FILE(src), res, &err);
    if (n->morto || (err != NULL && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))) {
        g_clear_error(&err);
        pc_nuova_libera(p);
        return;
    }
    if (ok) {
        char pieno[PH_TRF_PERCORSO_MAX + 1];
        bool intero = ph_trf_unisci(p->cartella, p->nome, pieno, sizeof pieno);
        nuova_fine(n);
        if (intero)
            vai(n, pieno, false);
    } else if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_EXISTS) && p->tentativi < 20) {
        /* Un file o una cartella con quel nome c'era e non l'avevamo visto: si passa al prossimo. */
        g_ptr_array_add(n->nomi, g_strdup(p->nome));
        p->tentativi++;
        pc_nuova_prova(n, p);
        g_clear_error(&err);
        return;
    } else {
        nuova_fine(n);
        /* Il testo di GIO e' nella lingua del PC e tecnico: nel log; a schermo una frase tradotta. */
        fprintf(stderr, "trasferimento: creazione della cartella sul PC non riuscita: %s\n",
                err != NULL ? err->message : "");
        ph_shell_avviso(n->sh, ph_t("Non riuscito"), ph_t("Non riesco a creare la cartella sul PC."));
    }
    g_clear_error(&err);
    pc_nuova_libera(p);
}

static void pc_nuova_prova(PhNav *n, PcNuova *p)
{
    char nome[PH_TRF_PERCORSO_MAX];
    if (!ph_trf_nome_libero(ph_t("Nuova cartella"), (const char *const *)(void *)n->nomi->pdata, n->nomi->len,
                            nome, sizeof nome)) {
        nuova_fine(n);
        ph_shell_avviso(n->sh, ph_t("Non riuscito"), "");
        pc_nuova_libera(p);
        return;
    }
    g_free(p->nome);
    p->nome = g_strdup(nome);
    char pieno[PH_TRF_PERCORSO_MAX + 1];
    if (!ph_trf_unisci(p->cartella, nome, pieno, sizeof pieno)) {
        nuova_fine(n);
        ph_shell_avviso(n->sh, ph_t("Non riuscito"), "");
        pc_nuova_libera(p);
        return;
    }
    if (n->canc_crea == NULL)
        n->canc_crea = g_cancellable_new();
    GFile *f = g_file_new_for_path(pieno);
    g_file_make_directory_async(f, G_PRIORITY_DEFAULT, n->canc_crea, pc_nuova_fatta, p);
    g_object_unref(f);
}

static void clic_nuova(GtkButton *b, gpointer dati)
{
    (void)b;
    PhNav *n = dati;
    if (n->creando || n->morto)
        return;
    n->creando = true;
    aggiorna_nuova(n);
    if (!e_telefono(n)) {
        PcNuova *p = g_new0(PcNuova, 1);
        p->n = g_rc_box_acquire(n);
        p->cartella = g_strdup(n->percorso);
        pc_nuova_prova(n, p);
        return;
    }
    char nome[PH_TRF_PERCORSO_MAX], pieno[PH_TRF_PERCORSO_MAX + 1];
    if (!ph_trf_nome_libero(ph_t("Nuova cartella"), (const char *const *)(void *)n->nomi->pdata, n->nomi->len,
                            nome, sizeof nome) ||
        !ph_trf_unisci(n->percorso, nome, pieno, sizeof pieno)) {
        nuova_fine(n);
        ph_shell_avviso(n->sh, ph_t("Non riuscito"), "");
        return;
    }
    g_free(n->nuova_percorso);
    n->nuova_percorso = g_strdup(pieno);
    if (n->canc_crea != NULL) {
        g_cancellable_cancel(n->canc_crea);
        g_clear_object(&n->canc_crea);
    }
    n->canc_crea = g_cancellable_new();
    ph_srv_tel_crea_cartella(servizi(n), pieno, n->canc_crea, tel_creata, n);
}

static void clic_usa(GtkButton *b, gpointer dati)
{
    (void)b;
    PhNav *n = dati;
    if (n->g.usa != NULL)
        n->g.usa(n->utente, n->percorso);
}

/* ---- Costruzione ------------------------------------------------------------------------- */

PhNav *ph_nav_nuovo(PhShell *sh, PhNavModo modo, const PhNavGestori *g, void *utente)
{
    PhNav *n = g_rc_box_new0(PhNav);
    n->sh = sh;
    n->modo = modo;
    if (g != NULL)
        n->g = *g;
    n->utente = utente;
    n->percorso = g_strdup("/");
    n->voci = g_ptr_array_new_with_free_func(riga_libera);
    n->nomi = g_ptr_array_new_with_free_func(g_free);
    n->trovati = g_ptr_array_new_with_free_func(riga_libera);
    n->scelti = g_ptr_array_new_with_free_func(g_free);
    n->scelti_set = g_hash_table_new(g_str_hash, g_str_equal);
    n->pagine = 1;

    bool selettore = modo != PH_NAV_TEL_FILE;
    n->radice = gtk_box_new(GTK_ORIENTATION_VERTICAL, selettore ? 6 : 10);
    if (selettore)
        gtk_widget_add_css_class(n->radice, "ph-trasferimento-selettore");

    /* Riga "Su" + percorso. */
    GtkWidget *alto = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    n->su = gtk_button_new_with_label(ph_t("Su"));
    gtk_widget_add_css_class(n->su, "ph-ghost");
    gtk_widget_add_css_class(n->su, "ph-bordato");
    gtk_widget_add_css_class(n->su, "ph-trasferimento-su");
    ph_shell_etichetta(n->su, ph_t("Cartella superiore"), NULL);
    g_signal_connect(n->su, "clicked", G_CALLBACK(clic_su), n);
    gtk_box_append(GTK_BOX(alto), n->su);
    n->campo = gtk_entry_new();
    gtk_widget_add_css_class(n->campo, "ph-campo");
    gtk_widget_set_hexpand(n->campo, TRUE);
    gtk_editable_set_width_chars(GTK_EDITABLE(n->campo), 1);
    gtk_entry_set_max_length(GTK_ENTRY(n->campo), PH_TRF_PERCORSO_MAX);
    if (e_telefono(n))
        ph_shell_etichetta(n->campo, ph_t("Percorso sul telefono (Invio per andare)"), NULL);
    else
        ph_shell_etichetta(n->campo, ph_t("Percorso sul PC (Invio per andare)"), NULL);
    g_signal_connect(n->campo, "activate", G_CALLBACK(campo_invio), n);
    g_signal_connect(n->campo, "changed", G_CALLBACK(campo_cambiato), n);
    gtk_box_append(GTK_BOX(alto), n->campo);
    gtk_box_append(GTK_BOX(n->radice), alto);

    /* Ricerca: solo sul telefono. */
    if (e_telefono(n)) {
        n->cerca = gtk_entry_new();
        gtk_widget_add_css_class(n->cerca, "ph-campo");
        gtk_editable_set_width_chars(GTK_EDITABLE(n->cerca), 1);
        gtk_entry_set_max_length(GTK_ENTRY(n->cerca), 255);
        if (modo == PH_NAV_TEL_CARTELLE) {
            gtk_entry_set_placeholder_text(GTK_ENTRY(n->cerca), ph_t("Cerca una cartella in tutto il telefono"));
            ph_shell_etichetta(n->cerca, ph_t("Cerca una cartella nel telefono"), NULL);
        } else {
            gtk_entry_set_placeholder_text(GTK_ENTRY(n->cerca),
                                           ph_t("Cerca file o cartelle in tutto il telefono"));
            ph_shell_etichetta(n->cerca, ph_t("Cerca nel telefono"), NULL);
        }
        g_signal_connect(n->cerca, "changed", G_CALLBACK(cerca_cambiata), n);
        gtk_box_append(GTK_BOX(n->radice), n->cerca);
    }

    /* Elenco che scorre: righe, messaggio, "Mostra altri". */
    GtkWidget *contenuto = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    n->righe = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    n->vuoto = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(n->vuoto), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(n->vuoto), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(n->vuoto), PANGO_WRAP_WORD_CHAR);
    gtk_widget_add_css_class(n->vuoto, "ph-piccolo");
    gtk_widget_add_css_class(n->vuoto, "ph-testo-3");
    gtk_widget_add_css_class(n->vuoto, selettore ? "ph-trasferimento-vuoto-s" : "ph-trasferimento-vuoto");
    gtk_widget_set_visible(n->vuoto, FALSE);
    n->altri = gtk_button_new();
    gtk_widget_add_css_class(n->altri, "ph-ghost");
    gtk_widget_add_css_class(n->altri, "ph-bordato");
    gtk_widget_set_halign(n->altri, GTK_ALIGN_START);
    gtk_widget_set_visible(n->altri, FALSE);
    g_signal_connect(n->altri, "clicked", G_CALLBACK(clic_altri), n);
    gtk_box_append(GTK_BOX(contenuto), n->righe);
    gtk_box_append(GTK_BOX(contenuto), n->vuoto);
    gtk_box_append(GTK_BOX(contenuto), n->altri);
    n->scorri = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(n->scorri), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(n->scorri), selettore ? 170 : 230);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(n->scorri), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(n->scorri), contenuto);
    gtk_box_append(GTK_BOX(n->radice), n->scorri);

    /* In fondo ai selettori: "Nuova cartella" e "Usa questa cartella". */
    if (selettore) {
        GtkWidget *basso = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        n->nuova = gtk_button_new_with_label(ph_t("Nuova cartella"));
        gtk_widget_add_css_class(n->nuova, "ph-ghost");
        gtk_widget_add_css_class(n->nuova, "ph-bordato");
        g_signal_connect(n->nuova, "clicked", G_CALLBACK(clic_nuova), n);
        n->usa = gtk_button_new_with_label(ph_t("Usa questa cartella"));
        gtk_widget_add_css_class(n->usa, "ph-accento");
        gtk_widget_add_css_class(n->usa, "ph-trasferimento-pieno");
        gtk_widget_set_hexpand(n->usa, TRUE);
        g_signal_connect(n->usa, "clicked", G_CALLBACK(clic_usa), n);
        gtk_box_append(GTK_BOX(basso), n->nuova);
        gtk_box_append(GTK_BOX(basso), n->usa);
        gtk_box_append(GTK_BOX(n->radice), basso);
    }
    mostra_percorso(n);
    aggiorna_nuova(n);
    return n;
}

GtkWidget *ph_nav_widget(PhNav *n)
{
    return n->radice;
}

void ph_nav_distruggi(PhNav *n)
{
    if (n == NULL || n->morto)
        return;
    ferma_tutto(n);
    n->morto = true;
    /* I widget possono vivere ancora un attimo: niente piu' funzioni di ritorno verso `n`. */
    GtkWidget *w[] = {n->su, n->campo, n->cerca, n->altri, n->nuova, n->usa};
    for (size_t i = 0; i < G_N_ELEMENTS(w); i++)
        if (w[i] != NULL)
            g_signal_handlers_disconnect_by_data(w[i], n);
    g_rc_box_release_full(n, nav_pulisci);
}

void ph_nav_prova_cerca(PhNav *n, const char *testo)
{
    if (n->cerca != NULL)
        gtk_editable_set_text(GTK_EDITABLE(n->cerca), testo);
}

void ph_nav_prova_scegli(PhNav *n, const char *percorso)
{
    if (g_hash_table_contains(n->scelti_set, percorso))
        return;
    char *p = g_strdup(percorso);
    g_ptr_array_add(n->scelti, p);
    g_hash_table_add(n->scelti_set, p);
    for (guint i = 0; i < n->voci->len; i++) {
        Riga *r = g_ptr_array_index(n->voci, i);
        if (!r->cartella && strcmp(r->percorso, percorso) == 0)
            riga_aggiorna_scelta(n, r);
    }
    scelta_notifica(n);
}
