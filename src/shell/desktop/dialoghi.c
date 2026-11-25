/* I due dialoghi del desktop (F-02): «Sfondo del desktop» (§5.2.1) e «Aggiungi
 * widget» (§5.2.3). Si creano una volta, nascosti, nello strato dei pannelli e si
 * aprono come popup (uno solo alla volta, come tutto il resto).
 *
 * Ogni cambio dello sfondo si applica subito al desktop e al profilo, come nel
 * mockup. La galleria arriva dal telefono: elenco delle cartelle, miniature e,
 * alla scelta, il file (tetto 32 MiB) che desktop.c decodifica e riduce in un
 * thread di lavoro. Mai lavoro pesante qui, sul thread GTK. */
#include <string.h>

#include "interno.h"

/* Cartelle della galleria "Recenti" (5.2.1). */
static const char *const CARTELLE_RECENTI[] = {"/storage/emulated/0/DCIM/Camera",
                                               "/storage/emulated/0/Pictures/Screenshots"};
#define N_CARTELLE_RECENTI 2
#define MINIATURA_LATO 192      /* lato delle miniature chieste al telefono */
#define N_PAL PH_DSK_PALETTE_N
#define PARTENZA_CARTELLE PH_DSK_TEL_MEMORIA_INTERNA

typedef struct {
    PhDskDialoghi *dl;
    GtkWidget *bottone, *immagine;
    char *percorso;
} Mini;

/* Una voce di una cartella del telefono (copia: l'elenco dei servizi vale solo
 * durante la chiamata). */
typedef struct {
    char *nome;         /* grezzo: serve a comporre il percorso */
    char *nome_schermo; /* da mostrare */
} VoceCartella;

static void voce_libera(gpointer p)
{
    VoceCartella *v = p;
    g_free(v->nome);
    g_free(v->nome_schermo);
    g_free(v);
}

struct PhDskDialoghi {
    PhDesktop *d;
    bool aggiorna; /* si sta riallineando l'interfaccia: i segnali non scrivono il profilo */

    /* Sfondo */
    GtkWidget *sfondo_dlg, *sfondo_scorri, *anteprima;
    GtkWidget *scheda[3];          /* solido, sfumato, immagine */
    GtkWidget *pannello[3];
    GtkWidget *sel_solido;
    GtkWidget *pal[3][N_PAL];      /* tavolozze: solido, Da, A */
    GtkWidget *sel_da, *sel_a, *direzione[4];
    GtkWidget *griglia, *adatt[4];
    GCancellable *annulla_gal, *annulla_scelta;
    GPtrArray *mini;               /* Mini* */
    PhDskRecente rec[PH_DSK_RECENTI_MAX]; /* le piu' recenti viste finora (memoria fissa) */
    size_t n_rec;
    bool vista_immagine;           /* scheda «Immagine» aperta ma tipo del profilo invariato */
    int in_attesa;
    bool gal_caricata;
    const Mini *scelta;

    /* Cartelle del telefono */
    GtkWidget *sotto[2];           /* «Recenti», «Cartelle del telefono» */
    GtkWidget *vista_recenti, *vista_cartelle;
    bool sotto_cartelle;
    GtkWidget *cart_su, *cart_percorso, *cart_scorri, *cart_lista, *cart_stato, *cart_griglia,
        *cart_altre;
    char cart_corrente[PH_DSK_TEL_PERCORSO_MAX];
    bool cart_caricata;
    GCancellable *annulla_cart;
    GPtrArray *cart_mini;          /* Mini* delle miniature della cartella */
    GPtrArray *cart_sub, *cart_img; /* VoceCartella* */
    size_t sub_pagine, img_pagine, img_creati;

    /* Aggiungi widget */
    GtkWidget *widget_dlg, *widget_scorri;
};

/* ---- Utilita' --------------------------------------------------------------------- */
static GtkWidget *scatola(GtkOrientation o, int spazio)
{
    return gtk_box_new(o, spazio);
}

/* Griglia di miniature a 4 colonne uguali: i segnaposto vuoti (larghezza zero)
 * tengono le 4 colonne anche con una sola miniatura. */
static GtkWidget *griglia_miniature(void)
{
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 8);
    gtk_grid_set_column_spacing(GTK_GRID(g), 8);
    gtk_grid_set_column_homogeneous(GTK_GRID(g), TRUE);
    for (int c = 0; c < 4; c++) {
        GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_hexpand(v, TRUE);
        gtk_widget_add_css_class(v, "ph-desktop-segnaposto");
        gtk_grid_attach(GTK_GRID(g), v, c, 0, 1, 1);
    }
    return g;
}

/* Toglie le miniature, non i segnaposto. */
static void griglia_svuota(GtkWidget *g)
{
    GtkWidget *f = gtk_widget_get_first_child(g);
    while (f != NULL) {
        GtkWidget *prossimo = gtk_widget_get_next_sibling(f);
        if (gtk_widget_has_css_class(f, "ph-desktop-miniatura"))
            gtk_grid_remove(GTK_GRID(g), f);
        f = prossimo;
    }
}

static GtkWidget *gruppo(const char *etichetta, int spazio, GtkOrientation o)
{
    GtkWidget *b = g_object_new(GTK_TYPE_BOX, "orientation", o, "spacing", spazio, "accessible-role",
                                GTK_ACCESSIBLE_ROLE_GROUP, NULL);
    ph_shell_etichetta(b, etichetta, NULL);
    return b;
}

static GtkWidget *testo_etichetta(const char *testo, const char *classe)
{
    GtkWidget *l = gtk_label_new(testo);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    if (classe != NULL)
        gtk_widget_add_css_class(l, classe);
    return l;
}

static GtkWidget *sezione(const char *testo)
{
    /* Il maiuscolo del mockup lo da' la classe ph-sezione (base.css). */
    return testo_etichetta(testo, "ph-sezione");
}

static GtkWidget *bottone(const char *testo, const char *classe, GCallback cb, gpointer dati)
{
    GtkWidget *b = gtk_button_new_with_label(testo);
    gtk_widget_add_css_class(b, classe);
    g_signal_connect(b, "clicked", cb, dati);
    return b;
}

static void scelto(GtkWidget *b, bool si)
{
    if (si)
        gtk_widget_add_css_class(b, "ph-desktop-scelto");
    else
        gtk_widget_remove_css_class(b, "ph-desktop-scelto");
    ph_shell_premuto(b, si);
}

static uint32_t rgb_da_rgba(const GdkRGBA *c)
{
    uint32_t r = (uint32_t)(c->red * 255.0f + 0.5f), g = (uint32_t)(c->green * 255.0f + 0.5f),
             b = (uint32_t)(c->blue * 255.0f + 0.5f);
    if (r > 255)
        r = 255;
    if (g > 255)
        g = 255;
    if (b > 255)
        b = 255;
    return (r << 16) | (g << 8) | b;
}

static void rgba_da_rgb(GdkRGBA *c, uint32_t rgb)
{
    c->red = ((rgb >> 16) & 0xff) / 255.0f;
    c->green = ((rgb >> 8) & 0xff) / 255.0f;
    c->blue = (rgb & 0xff) / 255.0f;
    c->alpha = 1.0f;
}

static GtkWidget *selettore_colore(const char *etichetta, GCallback cb, gpointer dati)
{
    GtkColorDialog *dlg = gtk_color_dialog_new();
    gtk_color_dialog_set_with_alpha(dlg, FALSE);
    gtk_color_dialog_set_title(dlg, etichetta);
    GtkWidget *b = gtk_color_dialog_button_new(dlg); /* prende il riferimento */
    gtk_widget_add_css_class(b, "ph-desktop-selettore");
    gtk_widget_set_size_request(b, 48, 30);
    ph_shell_etichetta(b, etichetta, NULL);
    g_signal_connect(b, "notify::rgba", cb, dati);
    return b;
}

/* ---- Sfondo: scrittura del profilo -------------------------------------------------------- */
static bool sfondo_corrente(PhDskDialoghi *dl, PhCfgSfondo *out)
{
    PhCfgTelefono *p = ph_dsk_profilo(dl->d);
    if (p == NULL)
        return false;
    *out = p->sfondo;
    return true;
}

static void sfondo_tipo(PhDskDialoghi *dl, PhCfgSfondoTipo t)
{
    PhCfgSfondo s;
    if (!sfondo_corrente(dl, &s))
        return;
    s.tipo = t;
    ph_dsk_sfondo_scegli(dl->d, &s);
}

static void sfondo_riallinea(PhDskDialoghi *dl);
static void cart_vai(PhDskDialoghi *dl, const char *percorso);

static void scheda_cliccata(GtkButton *b, gpointer dati)
{
    PhDskDialoghi *dl = dati;
    for (int i = 0; i < 3; i++) {
        if (GTK_WIDGET(b) != dl->scheda[i])
            continue;
        if (i == (int)PH_CFG_SFONDO_IMMAGINE) {
            /* Il tipo nel profilo cambia solo DOPO la scelta di un'immagine: un clic
             * sulla scheda non deve oscurare il desktop. */
            dl->vista_immagine = true;
            sfondo_riallinea(dl);
        } else {
            dl->vista_immagine = false;
            sfondo_tipo(dl, (PhCfgSfondoTipo)i);
        }
    }
}

static void solido_cambiato(GObject *o, GParamSpec *ps, gpointer dati)
{
    (void)ps;
    PhDskDialoghi *dl = dati;
    PhCfgSfondo s;
    if (dl->aggiorna || !sfondo_corrente(dl, &s))
        return;
    s.colore_solido = rgb_da_rgba(gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(o)));
    ph_dsk_sfondo_scegli(dl->d, &s);
}

/* Tavolozza: il pulsante porta indice e uso (0 solido, 1 Da, 2 A). */
static void palette_cliccata(GtkButton *b, gpointer dati)
{
    PhDskDialoghi *dl = dati;
    PhCfgSfondo s;
    if (!sfondo_corrente(dl, &s))
        return;
    size_t i = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(b), "ph-desktop-pal-i"));
    int uso = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "ph-desktop-pal-uso"));
    uint32_t c = ph_dsk_palette(i);
    if (uso == 0)
        s.colore_solido = c;
    else if (uso == 1)
        s.colore_da = c;
    else
        s.colore_a = c;
    ph_dsk_sfondo_scegli(dl->d, &s);
}

static GtkWidget *palette_nuova(PhDskDialoghi *dl, int uso, const char *etichetta)
{
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 4);
    gtk_grid_set_column_spacing(GTK_GRID(g), 4);
    gtk_accessible_update_property(GTK_ACCESSIBLE(g), GTK_ACCESSIBLE_PROPERTY_LABEL, etichetta, -1);
    for (size_t i = 0; i < N_PAL; i++) {
        GtkWidget *b = gtk_button_new();
        char classe[32], cod[16], et[48];
        g_snprintf(classe, sizeof classe, "ph-desktop-pal-%zu", i);
        g_snprintf(cod, sizeof cod, "#%06x", ph_dsk_palette(i));
        gtk_widget_add_css_class(b, "ph-desktop-pal");
        gtk_widget_add_css_class(b, classe);
        ph_lingua_componi(et, sizeof et, ph_t("Colore {valore}"), "valore", cod, (const char *)NULL);
        ph_shell_etichetta(b, et, NULL);
        ph_shell_premuto(b, false);
        g_object_set_data(G_OBJECT(b), "ph-desktop-pal-i", GSIZE_TO_POINTER(i));
        g_object_set_data(G_OBJECT(b), "ph-desktop-pal-uso", GINT_TO_POINTER(uso));
        g_signal_connect(b, "clicked", G_CALLBACK(palette_cliccata), dl);
        gtk_grid_attach(GTK_GRID(g), b, (int)(i % PH_DSK_PALETTE_COLONNE),
                        (int)(i / PH_DSK_PALETTE_COLONNE), 1, 1);
        dl->pal[uso][i] = b;
    }
    return g;
}

static void palette_segna(PhDskDialoghi *dl, int uso, uint32_t colore)
{
    for (size_t i = 0; i < N_PAL; i++)
        scelto(dl->pal[uso][i], ph_dsk_palette(i) == colore);
}

static void da_cambiato(GObject *o, GParamSpec *ps, gpointer dati)
{
    (void)ps;
    PhDskDialoghi *dl = dati;
    PhCfgSfondo s;
    if (dl->aggiorna || !sfondo_corrente(dl, &s))
        return;
    s.colore_da = rgb_da_rgba(gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(o)));
    ph_dsk_sfondo_scegli(dl->d, &s);
}

static void a_cambiato(GObject *o, GParamSpec *ps, gpointer dati)
{
    (void)ps;
    PhDskDialoghi *dl = dati;
    PhCfgSfondo s;
    if (dl->aggiorna || !sfondo_corrente(dl, &s))
        return;
    s.colore_a = rgb_da_rgba(gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(o)));
    ph_dsk_sfondo_scegli(dl->d, &s);
}

static void direzione_cliccata(GtkButton *b, gpointer dati)
{
    PhDskDialoghi *dl = dati;
    PhCfgSfondo s;
    if (!sfondo_corrente(dl, &s))
        return;
    for (int i = 0; i < 4; i++)
        if (GTK_WIDGET(b) == dl->direzione[i])
            s.direzione = (PhCfgDirezione)i;
    ph_dsk_sfondo_scegli(dl->d, &s);
}

static void adatt_cliccato(GtkButton *b, gpointer dati)
{
    PhDskDialoghi *dl = dati;
    PhCfgSfondo s;
    if (!sfondo_corrente(dl, &s))
        return;
    for (int i = 0; i < 4; i++)
        if (GTK_WIDGET(b) == dl->adatt[i])
            s.adattamento = (PhCfgAdattamento)i;
    ph_dsk_sfondo_scegli(dl->d, &s);
}

/* ---- Galleria del telefono --------------------------------------------------------------- */
static void recente_libera(PhDskRecente *r)
{
    g_free(r->percorso);
    g_free(r->nome_schermo);
}

static void cart_pulisci(PhDskDialoghi *dl);

static void galleria_pulisci(PhDskDialoghi *dl)
{
    /* Annullare dal thread GTK garantisce che nessuna funzione di ritorno arrivi
     * piu': i dati delle richieste si possono liberare subito. */
    if (dl->annulla_gal != NULL) {
        g_cancellable_cancel(dl->annulla_gal);
        g_clear_object(&dl->annulla_gal);
    }
    dl->scelta = NULL;
    if (dl->mini != NULL)
        g_ptr_array_set_size(dl->mini, 0);
    if (dl->griglia != NULL)
        griglia_svuota(dl->griglia);
    for (size_t i = 0; i < dl->n_rec; i++)
        recente_libera(&dl->rec[i]);
    dl->n_rec = 0;
    dl->in_attesa = 0;
    dl->gal_caricata = false;
    cart_pulisci(dl);
}

static void mini_libera(gpointer p)
{
    Mini *m = p;
    g_free(m->percorso);
    g_free(m);
}

static void mini_miniatura(PhServizi *s, PhSrvEsito esito, const char *frase, GdkTexture *t,
                           void *utente)
{
    (void)s;
    (void)frase;
    Mini *m = utente;
    if (esito == PH_SRV_ESITO_OK && t != NULL)
        gtk_picture_set_paintable(GTK_PICTURE(m->immagine), GDK_PAINTABLE(t));
}

static void leggi_fatto(PhServizi *s, PhSrvEsito esito, const char *frase, GBytes *byte,
                        void *utente)
{
    (void)s;
    (void)frase;
    PhDskDialoghi *dl = utente;
    if (esito != PH_SRV_ESITO_OK || byte == NULL || dl->d == NULL || !dl->d->vivo)
        return; /* nessun testo inventato: lo sfondo resta com'e' */
    PhCfgSfondo cfg;
    if (!sfondo_corrente(dl, &cfg))
        return;
    ph_dsk_sfondo_da_byte(dl->d, byte, cfg.adattamento);
}

static void mini_segna(PhDskDialoghi *dl, const Mini *scelta)
{
    GPtrArray *insiemi[2] = {dl->mini, dl->cart_mini};
    for (int k = 0; k < 2; k++)
        for (guint i = 0; insiemi[k] != NULL && i < insiemi[k]->len; i++) {
            Mini *x = g_ptr_array_index(insiemi[k], i);
            scelto(x->bottone, x == scelta);
        }
}

static void mini_cliccata(GtkButton *b, gpointer dati)
{
    (void)b;
    Mini *m = dati;
    PhDskDialoghi *dl = m->dl;
    dl->scelta = m;
    mini_segna(dl, m);
    if (dl->annulla_scelta != NULL) {
        g_cancellable_cancel(dl->annulla_scelta);
        g_object_unref(dl->annulla_scelta);
    }
    dl->annulla_scelta = g_cancellable_new();
    /* Il file intero arriva in memoria (tetto 32 MiB); la riduzione e la scrittura
     * nella cache avvengono in un thread di lavoro (ph_dsk_sfondo_da_byte). */
    ph_srv_tel_leggi(dl->d->srv, m->percorso, PH_DSK_IMMAGINE_BYTE_MAX, dl->annulla_scelta,
                     leggi_fatto, dl);
}

/* Una miniatura nella griglia (Recenti o cartella): la richiesta della texture parte
 * subito, con il `annulla` di quell'insieme (mai miniature di una cartella in un'altra). */
static void mini_nuova(PhDskDialoghi *dl, GtkWidget *griglia, GPtrArray *insieme,
                       GCancellable *annulla, const char *percorso, const char *nome_schermo)
{
    Mini *m = g_new0(Mini, 1);
    m->dl = dl;
    m->percorso = g_strdup(percorso);
    m->bottone = gtk_button_new();
    gtk_widget_add_css_class(m->bottone, "ph-desktop-miniatura");
    GtkWidget *col = scatola(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *cornice = scatola(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(cornice, "ph-desktop-miniatura-cornice");
    gtk_widget_set_overflow(cornice, GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_size_request(cornice, -1, 58);
    m->immagine = gtk_picture_new();
    gtk_picture_set_can_shrink(GTK_PICTURE(m->immagine), TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(m->immagine), GTK_CONTENT_FIT_COVER);
    gtk_widget_set_hexpand(m->immagine, TRUE);
    gtk_widget_set_vexpand(m->immagine, TRUE);
    gtk_box_append(GTK_BOX(cornice), m->immagine);
    char breve[128];
    ph_dsk_nome_breve(nome_schermo, breve, sizeof breve);
    GtkWidget *nome = gtk_label_new(breve);
    gtk_widget_add_css_class(nome, "ph-desktop-miniatura-nome");
    gtk_label_set_xalign(GTK_LABEL(nome), 0);
    gtk_label_set_ellipsize(GTK_LABEL(nome), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(nome), 1);
    gtk_box_append(GTK_BOX(col), cornice);
    gtk_box_append(GTK_BOX(col), nome);
    gtk_button_set_child(GTK_BUTTON(m->bottone), col);
    ph_shell_etichetta(m->bottone, breve, NULL);
    ph_shell_premuto(m->bottone, false);
    g_signal_connect(m->bottone, "clicked", G_CALLBACK(mini_cliccata), m);
    guint pos = insieme->len;
    gtk_grid_attach(GTK_GRID(griglia), m->bottone, (int)(pos % 4), (int)(pos / 4), 1, 1);
    g_ptr_array_add(insieme, m);
    ph_srv_tel_miniatura(dl->d->srv, m->percorso, MINIATURA_LATO, annulla, mini_miniatura, m);
}

static void galleria_costruisci(PhDskDialoghi *dl)
{
    /* Le piu' recenti sono gia' ordinate e al massimo 40 (tenute durante la scansione). */
    for (size_t i = 0; i < dl->n_rec; i++)
        mini_nuova(dl, dl->griglia, dl->mini, dl->annulla_gal, dl->rec[i].percorso,
                   dl->rec[i].nome_schermo);
    /* L'elenco non serve piu': i percorsi sono nei pulsanti. */
    for (size_t i = 0; i < dl->n_rec; i++)
        recente_libera(&dl->rec[i]);
    dl->n_rec = 0;
}

static void elenco_fatto(PhServizi *s, PhSrvEsito esito, const char *frase, const char *cartella,
                         const PhSrvVoce *voci, size_t n, void *utente)
{
    (void)s;
    (void)frase;
    PhDskDialoghi *dl = utente;
    if (esito == PH_SRV_ESITO_OK && voci != NULL && cartella != NULL) {
        for (size_t i = 0; i < n; i++) {
            const PhSrvVoce *v = &voci[i];
            /* Il nome arriva dal telefono: niente '/', niente nomi nascosti o senza
             * l'estensione di un'immagine. */
            if (v->cartella || v->nome == NULL || !ph_dsk_nome_immagine(v->nome))
                continue;
            PhDskRecente r;
            r.percorso = g_strdup_printf("%s%s%s", cartella,
                                         g_str_has_suffix(cartella, "/") ? "" : "/", v->nome);
            r.nome_schermo = g_strdup(v->nome_schermo != NULL ? v->nome_schermo : v->nome);
            r.modificato = v->modificato;
            /* Niente taglio all'arrivo: i servizi danno l'elenco per nome, quindi le
             * piu' recenti possono stare in fondo. Si tengono le 40 piu' recenti. */
            PhDskRecente scartato;
            if (ph_dsk_recenti_inserisci(dl->rec, &dl->n_rec, PH_DSK_RECENTI_MAX, &r, &scartato))
                recente_libera(&scartato);
        }
    }
    /* Errori (anche permessi, punto aperto A6): nessun testo, la griglia resta vuota. */
    if (--dl->in_attesa == 0)
        galleria_costruisci(dl);
}

static void galleria_avvia(PhDskDialoghi *dl)
{
    galleria_pulisci(dl);
    dl->gal_caricata = true;
    dl->in_attesa = N_CARTELLE_RECENTI;
    dl->annulla_gal = g_cancellable_new();
    GCancellable *c = dl->annulla_gal;
    for (int i = 0; i < N_CARTELLE_RECENTI; i++) {
        /* La funzione di ritorno non arriva mai dentro la chiamata (servizi.h). */
        ph_srv_tel_elenca(dl->d->srv, CARTELLE_RECENTI[i], c, elenco_fatto, dl);
    }
}

/* ---- Cartelle del telefono ------------------------------------------------------------------
 * Partenza dalla memoria interna. Ogni cambio di cartella annulla le richieste della
 * precedente (elenco e miniature, che condividono il GCancellable): niente miniature
 * di una cartella dentro un'altra. Le miniature si chiedono a pagine (a richiesta). */
static void cart_pulisci(PhDskDialoghi *dl)
{
    if (dl->annulla_cart != NULL) {
        g_cancellable_cancel(dl->annulla_cart);
        g_clear_object(&dl->annulla_cart);
    }
    dl->scelta = NULL;
    g_ptr_array_set_size(dl->cart_mini, 0);
    g_ptr_array_set_size(dl->cart_sub, 0);
    g_ptr_array_set_size(dl->cart_img, 0);
    dl->sub_pagine = 1;
    dl->img_pagine = 1;
    dl->img_creati = 0;
    GtkWidget *f;
    if (dl->cart_griglia != NULL)
        griglia_svuota(dl->cart_griglia);
    if (dl->cart_lista != NULL)
        while ((f = gtk_widget_get_first_child(dl->cart_lista)) != NULL)
            gtk_box_remove(GTK_BOX(dl->cart_lista), f);
    if (dl->cart_stato != NULL)
        gtk_widget_set_visible(dl->cart_stato, FALSE);
    if (dl->cart_altre != NULL)
        gtk_widget_set_visible(dl->cart_altre, FALSE);
    if (dl->cart_scorri != NULL)
        gtk_widget_set_visible(dl->cart_scorri, FALSE);
    dl->cart_caricata = false;
}

static void cart_entra(GtkButton *b, gpointer dati)
{
    PhDskDialoghi *dl = dati;
    size_t i = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(b), "ph-desktop-voce"));
    if (i >= dl->cart_sub->len)
        return;
    VoceCartella *v = g_ptr_array_index(dl->cart_sub, i);
    char nuovo[PH_DSK_TEL_PERCORSO_MAX];
    if (ph_dsk_tel_unisci(dl->cart_corrente, v->nome, nuovo, sizeof nuovo))
        cart_vai(dl, nuovo);
}

static void cart_altre_sub(GtkButton *b, gpointer dati);

static void cart_ridisegna_sub(PhDskDialoghi *dl)
{
    GtkWidget *f;
    while ((f = gtk_widget_get_first_child(dl->cart_lista)) != NULL)
        gtk_box_remove(GTK_BOX(dl->cart_lista), f);
    size_t tot = dl->cart_sub->len;
    size_t vis = ph_dsk_pagina_visibili(tot, dl->sub_pagine, PH_DSK_TEL_PAGINA_CARTELLE);
    for (size_t i = 0; i < vis; i++) {
        VoceCartella *v = g_ptr_array_index(dl->cart_sub, i);
        GtkWidget *b = gtk_button_new();
        gtk_widget_add_css_class(b, "ph-desktop-cartella");
        GtkWidget *riga = scatola(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *fr = gtk_label_new(">");
        gtk_widget_add_css_class(fr, "ph-desktop-cartella-freccia");
        GtkWidget *nome = gtk_label_new(v->nome_schermo);
        gtk_label_set_xalign(GTK_LABEL(nome), 0);
        gtk_label_set_ellipsize(GTK_LABEL(nome), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(nome), 1);
        gtk_widget_set_hexpand(nome, TRUE);
        gtk_box_append(GTK_BOX(riga), fr);
        gtk_box_append(GTK_BOX(riga), nome);
        gtk_button_set_child(GTK_BUTTON(b), riga);
        char et[400];
        ph_lingua_componi(et, sizeof et, ph_t("Apri la cartella {nome}"), "nome", v->nome_schermo,
                          (const char *)NULL);
        ph_shell_etichetta(b, et, NULL);
        g_object_set_data(G_OBJECT(b), "ph-desktop-voce", GSIZE_TO_POINTER(i));
        g_signal_connect(b, "clicked", G_CALLBACK(cart_entra), dl);
        gtk_box_append(GTK_BOX(dl->cart_lista), b);
    }
    size_t altre = ph_dsk_pagina_prossime(tot, vis, PH_DSK_TEL_PAGINA_CARTELLE);
    if (altre > 0) {
        char num[24], testo[96];
        g_snprintf(num, sizeof num, "%zu", altre);
        ph_lingua_componi(testo, sizeof testo, ph_t("Mostra altri {numero}"), "numero", num,
                          (const char *)NULL);
        GtkWidget *b = bottone(testo, "ph-ghost", G_CALLBACK(cart_altre_sub), dl);
        gtk_widget_add_css_class(b, "ph-bordato");
        gtk_box_append(GTK_BOX(dl->cart_lista), b);
    }
    gtk_widget_set_visible(dl->cart_scorri, vis > 0);
}

static void cart_altre_sub(GtkButton *b, gpointer dati)
{
    (void)b;
    PhDskDialoghi *dl = dati;
    dl->sub_pagine++;
    cart_ridisegna_sub(dl);
}

static void cart_aggiorna_altre(PhDskDialoghi *dl)
{
    size_t altre = ph_dsk_pagina_prossime(dl->cart_img->len, dl->img_creati, PH_DSK_TEL_PAGINA_IMMAGINI);
    gtk_widget_set_visible(dl->cart_altre, altre > 0);
    if (altre > 0) {
        char num[24], testo[96];
        g_snprintf(num, sizeof num, "%zu", altre);
        ph_lingua_componi(testo, sizeof testo, ph_t("Mostra altri {numero}"), "numero", num,
                          (const char *)NULL);
        gtk_button_set_label(GTK_BUTTON(dl->cart_altre), testo);
    }
}

/* Crea le miniature della pagina corrente (solo quelle nuove: a richiesta). */
static void cart_aggiungi_img(PhDskDialoghi *dl)
{
    size_t vis = ph_dsk_pagina_visibili(dl->cart_img->len, dl->img_pagine, PH_DSK_TEL_PAGINA_IMMAGINI);
    for (; dl->img_creati < vis; dl->img_creati++) {
        VoceCartella *v = g_ptr_array_index(dl->cart_img, dl->img_creati);
        char percorso[PH_DSK_TEL_PERCORSO_MAX];
        if (!ph_dsk_tel_unisci(dl->cart_corrente, v->nome, percorso, sizeof percorso))
            continue; /* nome o percorso non validi: non si chiede */
        mini_nuova(dl, dl->cart_griglia, dl->cart_mini, dl->annulla_cart, percorso, v->nome_schermo);
    }
    cart_aggiorna_altre(dl);
}

static void cart_altre_img(GtkButton *b, gpointer dati)
{
    (void)b;
    PhDskDialoghi *dl = dati;
    dl->img_pagine++;
    cart_aggiungi_img(dl);
}

static void cart_stato_testo(PhDskDialoghi *dl, const char *testo)
{
    gtk_label_set_text(GTK_LABEL(dl->cart_stato), testo != NULL ? testo : "");
    gtk_widget_set_visible(dl->cart_stato, testo != NULL && testo[0] != '\0');
}

static void cart_elenco_fatto(PhServizi *s, PhSrvEsito esito, const char *frase,
                              const char *cartella, const PhSrvVoce *voci, size_t n, void *utente)
{
    (void)s;
    PhDskDialoghi *dl = utente;
    /* Difesa: l'elenco e' della cartella chiesta (annullare impedisce gli altri). */
    if (cartella == NULL || strcmp(cartella, dl->cart_corrente) != 0)
        return;
    if (esito != PH_SRV_ESITO_OK || voci == NULL) {
        /* Mai un elenco vuoto finto: si dice perche' non si vede nulla. */
        if (esito == PH_SRV_ESITO_NON_CONSENTITO)
            cart_stato_testo(dl, ph_t("Accesso non consentito dal sistema"));
        else if (esito == PH_SRV_ESITO_NON_TROVATO)
            cart_stato_testo(dl, ph_t("Cartella non trovata"));
        else
            cart_stato_testo(dl, frase != NULL && frase[0] != '\0' ? frase : ph_t("Cartella non trovata"));
        return;
    }
    for (size_t i = 0; i < n; i++) {
        const PhSrvVoce *v = &voci[i];
        /* Nomi dal telefono: ostili. Un solo pezzo valido; i nascosti (punto) no. */
        if (!ph_dsk_tel_nome_valido(v->nome) || ph_dsk_tel_nascosto(v->nome))
            continue;
        if (!v->cartella && !ph_dsk_nome_immagine(v->nome))
            continue;
        VoceCartella *c = g_new0(VoceCartella, 1);
        c->nome = g_strdup(v->nome);
        c->nome_schermo = g_strdup(v->nome_schermo != NULL ? v->nome_schermo : v->nome);
        g_ptr_array_add(v->cartella ? dl->cart_sub : dl->cart_img, c);
    }
    cart_ridisegna_sub(dl);
    cart_aggiungi_img(dl);
    if (dl->cart_sub->len == 0 && dl->cart_img->len == 0)
        cart_stato_testo(dl, ph_t("Cartella vuota"));
    else if (dl->cart_img->len == 0)
        cart_stato_testo(dl, ph_t("Nessuna immagine in questa cartella"));
    else
        cart_stato_testo(dl, NULL);
}

static void cart_vai(PhDskDialoghi *dl, const char *percorso)
{
    cart_pulisci(dl);
    g_strlcpy(dl->cart_corrente, percorso, sizeof dl->cart_corrente);
    dl->cart_caricata = true;
    /* Il percorso si mostra per intero (nomi gia' sicuri: arrivano dai servizi). */
    gtk_label_set_text(GTK_LABEL(dl->cart_percorso), dl->cart_corrente);
    gtk_widget_set_sensitive(dl->cart_su, !ph_dsk_tel_e_radice(dl->cart_corrente));
    dl->annulla_cart = g_cancellable_new();
    ph_srv_tel_elenca(dl->d->srv, dl->cart_corrente, dl->annulla_cart, cart_elenco_fatto, dl);
}

static void cart_su_cliccato(GtkButton *b, gpointer dati)
{
    (void)b;
    PhDskDialoghi *dl = dati;
    char su[PH_DSK_TEL_PERCORSO_MAX];
    if (ph_dsk_tel_su(dl->cart_corrente, su, sizeof su))
        cart_vai(dl, su);
}

static void sotto_cliccato(GtkButton *b, gpointer dati)
{
    PhDskDialoghi *dl = dati;
    dl->sotto_cartelle = GTK_WIDGET(b) == dl->sotto[1];
    sfondo_riallinea(dl);
}

/* ---- Allineamento dell'interfaccia al profilo ------------------------------------------------ */
static void sfondo_riallinea(PhDskDialoghi *dl)
{
    PhCfgSfondo s;
    if (dl->sfondo_dlg == NULL || !sfondo_corrente(dl, &s))
        return;
    PhDesktop *d = dl->d;
    dl->aggiorna = true;
    ph_dsk_sfondo_imposta(dl->anteprima, &s, d->immagine, d->area_l);
    /* Scheda in vista: quella del profilo, salvo «Immagine» aperta ma non ancora scelta. */
    PhCfgSfondoTipo vista = dl->vista_immagine ? PH_CFG_SFONDO_IMMAGINE : s.tipo;
    for (int i = 0; i < 3; i++) {
        scelto(dl->scheda[i], (int)vista == i);
        gtk_widget_set_visible(dl->pannello[i], (int)vista == i);
    }
    GdkRGBA c;
    rgba_da_rgb(&c, s.colore_solido);
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dl->sel_solido), &c);
    palette_segna(dl, 0, s.colore_solido);
    palette_segna(dl, 1, s.colore_da);
    palette_segna(dl, 2, s.colore_a);
    rgba_da_rgb(&c, s.colore_da);
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dl->sel_da), &c);
    rgba_da_rgb(&c, s.colore_a);
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(dl->sel_a), &c);
    for (int i = 0; i < 4; i++) {
        scelto(dl->direzione[i], (int)s.direzione == i);
        scelto(dl->adatt[i], (int)s.adattamento == i);
    }
    for (int i = 0; i < 2; i++)
        scelto(dl->sotto[i], (i == 1) == dl->sotto_cartelle);
    gtk_widget_set_visible(dl->vista_recenti, !dl->sotto_cartelle);
    gtk_widget_set_visible(dl->vista_cartelle, dl->sotto_cartelle);
    dl->aggiorna = false;
    if (vista == PH_CFG_SFONDO_IMMAGINE && gtk_widget_get_visible(dl->sfondo_dlg)) {
        if (!dl->sotto_cartelle && !dl->gal_caricata)
            galleria_avvia(dl);
        else if (dl->sotto_cartelle && !dl->cart_caricata)
            cart_vai(dl, PARTENZA_CARTELLE);
    }
}

void ph_dsk_dialoghi_sfondo_cambiato(PhDesktop *d)
{
    if (d->dlg != NULL)
        sfondo_riallinea(d->dlg);
}

/* Misure dipendenti dalla radice: larghezza al massimo radice - 48, altezza al
 * massimo radice - 96 (il contenuto scorre). */
void ph_dsk_dialoghi_area(PhDesktop *d)
{
    PhDskDialoghi *dl = d->dlg;
    if (dl == NULL)
        return;
    int l = d->area_l > 0 ? d->area_l : 1280, a = d->area_a > 0 ? d->area_a : 800;
    int ls = MIN(520, l - 48), lw = MIN(540, l - 48);
    gtk_widget_set_size_request(dl->sfondo_dlg, ls > 200 ? ls : 200, -1);
    gtk_widget_set_size_request(dl->widget_dlg, lw > 200 ? lw : 200, -1);
    /* Imbottitura 40, titolo ~28, anteprima 96, schede ~34 e gli spazi: il resto
     * e' per il contenuto che scorre. */
    /* Il dialogo e' alto al massimo a - 96: tolte imbottitura (40), titolo (28), anteprima
     * (96), schede (~36) e tre spazi (36) resta il contenuto che scorre; per il
     * catalogo imbottitura, titolo, testo (~34) e due spazi. */
    int hs = a - 96 - 236;
    int hw = a - 96 - 126;
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(dl->sfondo_scorri), hs > 120 ? hs : 120);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(dl->widget_scorri), hw > 120 ? hw : 120);
    sfondo_riallinea(dl); /* l'anteprima "Centra" dipende dalla larghezza del desktop */
}

void ph_dsk_dialoghi_annulla_scelta(PhDesktop *d)
{
    PhDskDialoghi *dl = d->dlg;
    if (dl != NULL && dl->annulla_scelta != NULL) {
        g_cancellable_cancel(dl->annulla_scelta);
        g_clear_object(&dl->annulla_scelta);
    }
}

/* ---- Apertura e chiusura -------------------------------------------------------------------------- */
static void dialogo_chiuso(PhShell *sh, PhShellPopup tipo, GtkWidget *w, void *utente)
{
    (void)sh;
    (void)w;
    PhDskDialoghi *dl = utente;
    if (tipo == PH_POPUP_DIALOGO_SFONDO)
        galleria_pulisci(dl); /* le richieste in volo non servono piu' */
}

static void fatto_cliccato(GtkButton *b, gpointer dati)
{
    (void)b;
    PhDskDialoghi *dl = dati;
    ph_shell_popup_chiudi(dl->d->sh);
}

void ph_dsk_dialogo_sfondo_apri(PhDesktop *d)
{
    PhDskDialoghi *dl = d->dlg;
    ph_dsk_dialoghi_area(d);
    galleria_pulisci(dl);
    dl->vista_immagine = false;
    dl->sotto_cartelle = false;
    sfondo_riallinea(dl);
    ph_shell_popup_apri(d->sh, PH_POPUP_DIALOGO_SFONDO, dl->sfondo_dlg, dialogo_chiuso, dl);
    /* Il dialogo e' ora visibile: se serve parte la galleria. */
    sfondo_riallinea(dl);
}

void ph_dsk_dialogo_widget_apri(PhDesktop *d)
{
    PhDskDialoghi *dl = d->dlg;
    ph_dsk_dialoghi_area(d);
    ph_shell_popup_apri(d->sh, PH_POPUP_DIALOGO_WIDGET, dl->widget_dlg, dialogo_chiuso, dl);
}

/* ---- Costruzione: dialogo dello sfondo -------------------------------------------------------------- */
static GtkWidget *intestazione(const char *titolo, PhDskDialoghi *dl)
{
    GtkWidget *r = scatola(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *t = testo_etichetta(titolo, "ph-titolo-dialogo");
    gtk_widget_set_hexpand(t, TRUE);
    GtkWidget *f = bottone(ph_t("Fatto"), "ph-ghost", G_CALLBACK(fatto_cliccato), dl);
    gtk_box_append(GTK_BOX(r), t);
    gtk_box_append(GTK_BOX(r), f);
    return r;
}

static GtkWidget *dialogo_nuovo(PhShell *sh, const char *titolo)
{
    GtkWidget *w = g_object_new(GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL, "spacing",
                                12, "accessible-role", GTK_ACCESSIBLE_ROLE_DIALOG, NULL);
    gtk_widget_add_css_class(w, "ph-dialogo");
    ph_shell_vetro_registra(sh, w, 16, PH_VETRO_CHIARO); /* raggio di ph-dialogo */
    gtk_widget_set_halign(w, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(w, FALSE);
    ph_shell_etichetta(w, titolo, NULL);
    return w;
}

static GtkWidget *scorrevole_nuovo(GtkWidget *contenuto)
{
    GtkWidget *s = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(s), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(s), contenuto);
    gtk_widget_add_css_class(s, "ph-desktop-scorrevole");
    return s;
}

static void costruisci_sfondo(PhDskDialoghi *dl)
{
    dl->sfondo_dlg = dialogo_nuovo(dl->d->sh, ph_t("Sfondo del desktop"));
    gtk_box_append(GTK_BOX(dl->sfondo_dlg), intestazione(ph_t("Sfondo del desktop"), dl));

    /* Anteprima: 96 px, raggio 10, bordo 1 px. La cornice ritaglia lo sfondo. */
    GtkWidget *cornice = scatola(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(cornice, "ph-desktop-anteprima");
    gtk_widget_set_overflow(cornice, GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_size_request(cornice, -1, 96);
    dl->anteprima = ph_dsk_sfondo_nuovo();
    gtk_box_append(GTK_BOX(cornice), dl->anteprima);
    gtk_box_append(GTK_BOX(dl->sfondo_dlg), cornice);

    /* Tipo di sfondo: tre schede a larghezza uguale. */
    GtkWidget *tipi = gruppo(ph_t("Tipo di sfondo"), 4, GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_add_css_class(tipi, "ph-desktop-schede");
    gtk_box_set_homogeneous(GTK_BOX(tipi), TRUE);
    const char *nomi[3] = {ph_t("Colore solido"), ph_t("Colore sfumato"), ph_t("Immagine dal telefono")};
    for (int i = 0; i < 3; i++) {
        dl->scheda[i] = bottone(nomi[i], "ph-desktop-scheda", G_CALLBACK(scheda_cliccata), dl);
        gtk_box_append(GTK_BOX(tipi), dl->scheda[i]);
    }
    gtk_box_append(GTK_BOX(dl->sfondo_dlg), tipi);

    GtkWidget *tutti = scatola(GTK_ORIENTATION_VERTICAL, 0);

    /* Solido: la tavolozza 6x8 e, sotto, il selettore libero con la sua etichetta. */
    dl->pannello[0] = scatola(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(dl->pannello[0]), palette_nuova(dl, 0, ph_t("Colore dello sfondo")));
    GtkWidget *riga = scatola(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(riga), testo_etichetta(ph_t("Altro colore…"), "ph-desktop-campo"));
    dl->sel_solido = selettore_colore(ph_t("Colore dello sfondo"), G_CALLBACK(solido_cambiato), dl);
    gtk_box_append(GTK_BOX(riga), dl->sel_solido);
    gtk_box_append(GTK_BOX(dl->pannello[0]), riga);

    /* Sfumato: Da e A, ciascuno con la tavolozza e il selettore libero. */
    dl->pannello[1] = scatola(GTK_ORIENTATION_VERTICAL, 10);
    /* Una sotto l'altra: affiancate allargherebbero il dialogo oltre i 520 px. */
    GtkWidget *colori = scatola(GTK_ORIENTATION_VERTICAL, 14);
    for (int k = 0; k < 2; k++) {
        GtkWidget *col = scatola(GTK_ORIENTATION_VERTICAL, 8);
        gtk_box_append(GTK_BOX(col), testo_etichetta(k == 0 ? ph_t("Da") : ph_t("A"), "ph-desktop-campo"));
        gtk_box_append(GTK_BOX(col), palette_nuova(dl, k + 1, k == 0 ? ph_t("Primo colore") : ph_t("Secondo colore")));
        GtkWidget *r = scatola(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_append(GTK_BOX(r), testo_etichetta(ph_t("Altro colore…"), "ph-desktop-campo"));
        GtkWidget *sel = selettore_colore(k == 0 ? ph_t("Primo colore") : ph_t("Secondo colore"),
                                          k == 0 ? G_CALLBACK(da_cambiato) : G_CALLBACK(a_cambiato), dl);
        if (k == 0)
            dl->sel_da = sel;
        else
            dl->sel_a = sel;
        gtk_box_append(GTK_BOX(r), sel);
        gtk_box_append(GTK_BOX(col), r);
        gtk_box_append(GTK_BOX(colori), col);
    }
    gtk_box_append(GTK_BOX(dl->pannello[1]), colori);
    GtkWidget *dir = gruppo(ph_t("Direzione della sfumatura"), 4, GTK_ORIENTATION_HORIZONTAL);
    /* Nell'ordine dell'enum PhCfgDirezione: diagonale, verticale, orizzontale, inversa. */
    const char *nd[4] = {ph_t("Diagonale"), ph_t("Verticale"), ph_t("Orizzontale"),
                         ph_t("Diagonale inversa")};
    for (int i = 0; i < 4; i++) {
        dl->direzione[i] = bottone(nd[i], "ph-desktop-opzione", G_CALLBACK(direzione_cliccata), dl);
        gtk_box_append(GTK_BOX(dir), dl->direzione[i]);
    }
    gtk_box_append(GTK_BOX(dl->pannello[1]), dir);

    /* Immagine: «Recenti» oppure le cartelle del telefono. */
    dl->pannello[2] = scatola(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *sotto = gruppo(ph_t("Galleria del telefono"), 4, GTK_ORIENTATION_HORIZONTAL);
    dl->sotto[0] = bottone(ph_t("Recenti"), "ph-desktop-opzione", G_CALLBACK(sotto_cliccato), dl);
    dl->sotto[1] = bottone(ph_t("Cartelle del telefono"), "ph-desktop-opzione", G_CALLBACK(sotto_cliccato), dl);
    gtk_box_append(GTK_BOX(sotto), dl->sotto[0]);
    gtk_box_append(GTK_BOX(sotto), dl->sotto[1]);
    gtk_box_append(GTK_BOX(dl->pannello[2]), sotto);

    dl->vista_recenti = scatola(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(dl->vista_recenti), sezione(ph_t("Galleria del telefono · Recenti")));
    dl->griglia = griglia_miniature();
    gtk_box_append(GTK_BOX(dl->vista_recenti), dl->griglia);
    gtk_box_append(GTK_BOX(dl->pannello[2]), dl->vista_recenti);

    dl->vista_cartelle = scatola(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *rp = scatola(GTK_ORIENTATION_HORIZONTAL, 8);
    dl->cart_su = bottone(ph_t("Su"), "ph-desktop-su", G_CALLBACK(cart_su_cliccato), dl);
    ph_shell_etichetta(dl->cart_su, ph_t("Cartella superiore"), ph_t("Cartella superiore"));
    dl->cart_percorso = gtk_label_new("");
    gtk_widget_add_css_class(dl->cart_percorso, "ph-desktop-percorso");
    gtk_label_set_xalign(GTK_LABEL(dl->cart_percorso), 0);
    gtk_label_set_ellipsize(GTK_LABEL(dl->cart_percorso), PANGO_ELLIPSIZE_START);
    gtk_label_set_max_width_chars(GTK_LABEL(dl->cart_percorso), 1);
    gtk_widget_set_hexpand(dl->cart_percorso, TRUE);
    ph_shell_etichetta(dl->cart_percorso, ph_t("Cartelle del telefono"), NULL);
    gtk_box_append(GTK_BOX(rp), dl->cart_su);
    gtk_box_append(GTK_BOX(rp), dl->cart_percorso);
    gtk_box_append(GTK_BOX(dl->vista_cartelle), rp);
    dl->cart_lista = scatola(GTK_ORIENTATION_VERTICAL, 0);
    dl->cart_scorri = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(dl->cart_scorri), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(dl->cart_scorri), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(dl->cart_scorri), 170);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(dl->cart_scorri), dl->cart_lista);
    gtk_widget_set_visible(dl->cart_scorri, FALSE);
    gtk_box_append(GTK_BOX(dl->vista_cartelle), dl->cart_scorri);
    dl->cart_stato = testo_etichetta("", "ph-desktop-vuoto");
    gtk_widget_set_visible(dl->cart_stato, FALSE);
    gtk_box_append(GTK_BOX(dl->vista_cartelle), dl->cart_stato);
    dl->cart_griglia = griglia_miniature();
    gtk_box_append(GTK_BOX(dl->vista_cartelle), dl->cart_griglia);
    dl->cart_altre = bottone("", "ph-ghost", G_CALLBACK(cart_altre_img), dl);
    gtk_widget_add_css_class(dl->cart_altre, "ph-bordato");
    gtk_widget_set_halign(dl->cart_altre, GTK_ALIGN_START);
    gtk_widget_set_visible(dl->cart_altre, FALSE);
    gtk_box_append(GTK_BOX(dl->vista_cartelle), dl->cart_altre);
    gtk_widget_set_visible(dl->vista_cartelle, FALSE);
    gtk_box_append(GTK_BOX(dl->pannello[2]), dl->vista_cartelle);
    GtkWidget *ad = gruppo(ph_t("Adattamento dell'immagine"), 4, GTK_ORIENTATION_HORIZONTAL);
    /* Nell'ordine dell'enum PhCfgAdattamento: riempi, adatta, allunga, centra. */
    const char *na[4] = {ph_t("Riempi"), ph_t("Adatta"), ph_t("Allunga"), ph_t("Centra")};
    for (int i = 0; i < 4; i++) {
        dl->adatt[i] = bottone(na[i], "ph-desktop-opzione", G_CALLBACK(adatt_cliccato), dl);
        gtk_box_append(GTK_BOX(ad), dl->adatt[i]);
    }
    gtk_box_append(GTK_BOX(dl->pannello[2]), ad);
    GtkWidget *nota = testo_etichetta(
        ph_t("Le anteprime arrivano dal telefono. L'immagine scelta viene copiata sul PC, così lo "
             "sfondo resta anche a telefono scollegato e non pesa sullo stream."),
        "ph-desktop-nota");
    gtk_label_set_wrap(GTK_LABEL(nota), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(nota), 60);
    gtk_box_append(GTK_BOX(dl->pannello[2]), nota);

    for (int i = 0; i < 3; i++)
        gtk_box_append(GTK_BOX(tutti), dl->pannello[i]);
    dl->sfondo_scorri = scorrevole_nuovo(tutti);
    gtk_box_append(GTK_BOX(dl->sfondo_dlg), dl->sfondo_scorri);
    ph_shell_strato_aggiungi(dl->d->sh, PH_STRATO_PANNELLI, dl->sfondo_dlg);
}

/* ---- Costruzione: dialogo dei widget ------------------------------------------------------------------ */
static void aggiungi_cliccato(GtkButton *b, gpointer dati)
{
    PhDskDialoghi *dl = dati;
    gpointer tipo = g_object_get_data(G_OBJECT(b), "ph-desktop-tipo");
    ph_dsk_widget_aggiungi(dl->d, (PhCfgWidgetTipo)GPOINTER_TO_INT(tipo));
}

static void costruisci_widget(PhDskDialoghi *dl)
{
    dl->widget_dlg = dialogo_nuovo(dl->d->sh, ph_t("Aggiungi widget"));
    gtk_box_append(GTK_BOX(dl->widget_dlg), intestazione(ph_t("Aggiungi widget"), dl));
    GtkWidget *info = testo_etichetta(
        ph_t("Widget del desktop: li disegna YASDE sul PC, usando i dati del telefono. Dopo "
             "averlo aggiunto lo trascini dove vuoi; clic destro per cambiare dimensione o "
             "rimuoverlo."),
        "ph-desktop-nota");
    gtk_label_set_wrap(GTK_LABEL(info), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(info), 70);
    gtk_box_append(GTK_BOX(dl->widget_dlg), info);

    GtkWidget *griglia = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(griglia), 10);
    gtk_grid_set_column_spacing(GTK_GRID(griglia), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(griglia), TRUE);
    for (size_t i = 0; i < PH_DSK_CATALOGO_N; i++) {
        const PhDskCatalogoVoce *v = &PH_DSK_CATALOGO[i];
        GtkWidget *scheda = scatola(GTK_ORIENTATION_VERTICAL, 6);
        gtk_widget_add_css_class(scheda, "ph-desktop-voce-catalogo");
        gtk_box_append(GTK_BOX(scheda), testo_etichetta(ph_tv(v->nome), "ph-desktop-catalogo-nome"));
        GtkWidget *desc = testo_etichetta(ph_tv(v->descrizione), "ph-desktop-catalogo-desc");
        gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(desc), 32);
        gtk_widget_set_vexpand(desc, TRUE);
        gtk_box_append(GTK_BOX(scheda), desc);
        GtkWidget *ag = bottone(ph_t("Aggiungi"), "ph-desktop-aggiungi", G_CALLBACK(aggiungi_cliccato), dl);
        gtk_widget_set_halign(ag, GTK_ALIGN_START);
        g_object_set_data(G_OBJECT(ag), "ph-desktop-tipo", GINT_TO_POINTER((int)v->tipo));
        /* L'etichetta accessibile dice anche di quale widget si tratta. */
        char et[160];
        ph_lingua_componi(et, sizeof et, ph_t("Aggiungi {widget}"), "widget", ph_tv(v->nome),
                          (const char *)NULL);
        ph_shell_etichetta(ag, et, NULL);
        gtk_box_append(GTK_BOX(scheda), ag);
        gtk_grid_attach(GTK_GRID(griglia), scheda, (int)(i % 2), (int)(i / 2), 1, 1);
    }
    dl->widget_scorri = scorrevole_nuovo(griglia);
    gtk_box_append(GTK_BOX(dl->widget_dlg), dl->widget_scorri);
    ph_shell_strato_aggiungi(dl->d->sh, PH_STRATO_PANNELLI, dl->widget_dlg);
}

/* ---- Vita --------------------------------------------------------------------------------------------- */
PhDskDialoghi *ph_dsk_dialoghi_nuovi(PhDesktop *d)
{
    PhDskDialoghi *dl = g_new0(PhDskDialoghi, 1);
    dl->d = d;
    dl->mini = g_ptr_array_new_with_free_func(mini_libera);
    dl->cart_mini = g_ptr_array_new_with_free_func(mini_libera);
    dl->cart_sub = g_ptr_array_new_with_free_func(voce_libera);
    dl->cart_img = g_ptr_array_new_with_free_func(voce_libera);
    dl->sub_pagine = dl->img_pagine = 1;
    costruisci_sfondo(dl);
    costruisci_widget(dl);
    g_object_ref(dl->sfondo_dlg); /* vedi desktop.c: i widget vivono finche' ci sono gli ascolti */
    g_object_ref(dl->widget_dlg);
    d->dlg = dl; /* le funzioni sopra lo cercano da d */
    ph_dsk_dialoghi_area(d);
    return dl;
}

void ph_dsk_dialoghi_libera(PhDskDialoghi *dl)
{
    if (dl == NULL)
        return;
    /* I widget stanno morendo con la radice: la galleria si ferma senza toccarli. */
    dl->griglia = NULL;
    dl->cart_griglia = dl->cart_lista = dl->cart_stato = dl->cart_altre = dl->cart_scorri = NULL;
    galleria_pulisci(dl);
    if (dl->annulla_scelta != NULL) {
        g_cancellable_cancel(dl->annulla_scelta);
        g_clear_object(&dl->annulla_scelta);
    }
    g_ptr_array_unref(dl->mini);
    g_ptr_array_unref(dl->cart_mini);
    g_ptr_array_unref(dl->cart_sub);
    g_ptr_array_unref(dl->cart_img);
    g_object_unref(dl->sfondo_dlg);
    g_object_unref(dl->widget_dlg);
    dl->d = NULL;
    g_free(dl);
}

