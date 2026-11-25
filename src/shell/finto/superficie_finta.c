/* Superficie FINTA (superficie.h): entra nella compilazione SOLO quando il
 * pezzo superficie non c'e' (per esempio con -Dpezzo=shell-finestra-app),
 * cosi' finestra-app si prova e si guarda da sola. Semplice ma onesta: un
 * thread di lettura per flusso, una sola consegna in volo (vince l'ultimo
 * fotogramma), DMA-BUF o memoria, stesura su tutta l'area. Non e' il
 * modello per la superficie vera (che ha requisiti in piu': COMPITO.md). */
#include <stdatomic.h>
#include <string.h>

#include "config/lingua.h"
#include "shell/superficie.h"

typedef struct {
    atomic_int rif;
    GMutex m;
    PhFotogramma ultimo;
    bool c_e, in_volo;
    PhSrvFlusso *f;
    GtkWidget *w; /* il thread lo usa solo per g_object_ref durante la sua vita */
} Lettore;

#define PH_TIPO_SUP_FINTA (ph_sup_finta_get_type())
G_DECLARE_FINAL_TYPE(PhSupFinta, ph_sup_finta, PH, SUP_FINTA, GtkWidget)

struct _PhSupFinta {
    GtkWidget genitore;
    PhShell *sh;
    Lettore *l;
    GThread *thread;
    GtkWidget *immagine, *velo, *messaggio;
    GdkTexture *ultima;
    PhSuperficieAvviso avviso;
    void *utente;
    int vw, vh;
    bool chiusa, perso, protetto, primo_piano;
    /* Aggiunte del 23 nov: screenshot in attesa (Scatto*) e registrazione. */
    GList *scatti;
    PhSuperficieRegAvviso reg_avviso;
    void *reg_utente;
};

G_DEFINE_FINAL_TYPE(PhSupFinta, ph_sup_finta, GTK_TYPE_WIDGET)

static void lettore_lascia(Lettore *l)
{
    if (atomic_fetch_sub(&l->rif, 1) != 1)
        return;
    if (l->c_e)
        ph_fotogramma_rilascia(&l->ultimo);
    g_mutex_clear(&l->m);
    g_free(l);
}

typedef struct {
    PhFotogramma f;
} Tenuto;

static void tenuto_rilascia(gpointer p)
{
    Tenuto *t = p;
    ph_fotogramma_rilascia(&t->f);
    g_free(t);
}

static GdkTexture *texture_da(PhFotogramma *fr)
{
    Tenuto *t = g_new(Tenuto, 1);
    t->f = *fr;
    *fr = (PhFotogramma){0};
    if (t->f.tipo == PH_FOTO_DMABUF) {
        GdkDmabufTextureBuilder *b = gdk_dmabuf_texture_builder_new();
        gdk_dmabuf_texture_builder_set_display(b, gdk_display_get_default());
        gdk_dmabuf_texture_builder_set_width(b, (unsigned)t->f.larghezza);
        gdk_dmabuf_texture_builder_set_height(b, (unsigned)t->f.altezza);
        gdk_dmabuf_texture_builder_set_fourcc(b, t->f.fourcc);
        gdk_dmabuf_texture_builder_set_modifier(b, t->f.modificatore);
        gdk_dmabuf_texture_builder_set_n_planes(b, (unsigned)t->f.piani);
        for (int i = 0; i < t->f.piani && i < PH_FOTO_PIANI_MAX; i++) {
            gdk_dmabuf_texture_builder_set_fd(b, (unsigned)i, t->f.fd[i]);
            gdk_dmabuf_texture_builder_set_offset(b, (unsigned)i, t->f.scostamento[i]);
            gdk_dmabuf_texture_builder_set_stride(b, (unsigned)i, t->f.passo_piano[i]);
        }
        GError *err = NULL;
        GdkTexture *tx = gdk_dmabuf_texture_builder_build(b, tenuto_rilascia, t, &err);
        g_object_unref(b);
        if (tx == NULL) {
            /* Come il prototipo: non si sa se GTK abbia chiamato il rilascio;
             * meglio perdere un fotogramma che rilasciarlo due volte. */
            g_clear_error(&err);
        }
        return tx;
    }
    GdkMemoryFormat fm = t->f.formato == PH_FOTO_R8G8B8A8 ? GDK_MEMORY_R8G8B8A8
                       : t->f.formato == PH_FOTO_R8G8B8   ? GDK_MEMORY_R8G8B8
                                                          : GDK_MEMORY_B8G8R8A8_PREMULTIPLIED;
    GBytes *by = g_bytes_new_with_free_func(t->f.dati, t->f.passo * (size_t)t->f.altezza, tenuto_rilascia, t);
    GdkTexture *tx = gdk_memory_texture_new(t->f.larghezza, t->f.altezza, fm, by, t->f.passo);
    g_bytes_unref(by);
    return tx;
}

static void mostra(void *dati)
{
    PhSupFinta *s = PH_SUP_FINTA(dati);
    if (s->l == NULL)
        return;
    PhFotogramma fr = {0};
    g_mutex_lock(&s->l->m);
    bool c = s->l->c_e;
    if (c)
        fr = s->l->ultimo;
    s->l->c_e = false;
    s->l->in_volo = false;
    g_mutex_unlock(&s->l->m);
    if (!c)
        return;
    bool dmabuf = fr.tipo == PH_FOTO_DMABUF;
    GdkTexture *t = texture_da(&fr);
    if (t == NULL) {
        if (dmabuf)
            ph_srv_flusso_preferisci_memoria(s->l->f);
        return;
    }
    gtk_picture_set_paintable(GTK_PICTURE(s->immagine), GDK_PAINTABLE(t));
    g_set_object(&s->ultima, t);
    g_object_unref(t);
}

static gpointer leggi(gpointer p)
{
    Lettore *l = p;
    for (;;) {
        PhFotogramma fr;
        int r = ph_srv_flusso_leggi(l->f, &fr, 100);
        if (r < 0)
            break;
        if (r == 0)
            continue;
        g_mutex_lock(&l->m);
        if (l->c_e)
            ph_fotogramma_rilascia(&l->ultimo); /* non ancora mostrato: vince il nuovo */
        l->ultimo = fr;
        l->c_e = true;
        bool manda = !l->in_volo;
        l->in_volo = true;
        g_mutex_unlock(&l->m);
        if (manda)
            ph_shell_consegna(mostra, g_object_ref(l->w), g_object_unref);
    }
    lettore_lascia(l);
    return NULL;
}

static void aggiorna_velo(PhSupFinta *s)
{
    if (s->perso)
        gtk_label_set_text(GTK_LABEL(s->messaggio), ph_t("Connessione persa"));
    else if (s->protetto)
        gtk_label_set_text(GTK_LABEL(s->messaggio),
                           ph_t("Questa app protegge il suo schermo: il contenuto non si puo' mostrare."));
    gtk_widget_set_visible(s->velo, s->perso || s->protetto);
    gtk_widget_set_opacity(s->immagine, s->perso ? 0.35 : 1.0);
}

static void avviso_flusso(PhSrvFlusso *f, const PhSrvFlussoDati *d, void *utente)
{
    (void)f;
    PhSupFinta *s = utente;
    switch (d->evento) {
    case PH_SRV_FL_APERTO:
    case PH_SRV_FL_MISURA:
        s->vw = d->larghezza;
        s->vh = d->altezza;
        if (s->avviso != NULL)
            s->avviso(GTK_WIDGET(s), PH_SUP_EV_MISURA, s->vw, s->vh, s->utente);
        break;
    case PH_SRV_FL_PROTETTO:
        s->protetto = d->protetto;
        aggiorna_velo(s);
        break;
    case PH_SRV_FL_APP_CHIUSA:
        if (s->avviso != NULL)
            s->avviso(GTK_WIDGET(s), PH_SUP_EV_APP_CHIUSA, 0, 0, s->utente);
        break;
    case PH_SRV_FL_PERSO:
        s->perso = true;
        aggiorna_velo(s);
        break;
    case PH_SRV_FL_RIPRESO:
        s->perso = false;
        aggiorna_velo(s);
        break;
    case PH_SRV_FL_ERRORE:
        if (s->avviso != NULL)
            s->avviso(GTK_WIDGET(s), PH_SUP_EV_ERRORE, 0, 0, s->utente);
        break;
    }
}

static void riconnetti(GtkButton *b, gpointer p)
{
    (void)b;
    ph_srv_riconnetti_ora(ph_shell_servizi(PH_SUP_FINTA(p)->sh));
}

static void chiudi_premuto(GtkButton *b, gpointer p)
{
    (void)b;
    PhSupFinta *s = p;
    if (s->avviso != NULL)
        s->avviso(GTK_WIDGET(s), PH_SUP_EV_CHIUDI, 0, 0, s->utente);
}

void ph_superficie_chiudi(GtkWidget *w, bool togli_task)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    if (s->chiusa)
        return;
    s->chiusa = true;
    if (s->l != NULL) {
        ph_srv_flusso_interrompi(s->l->f);
        if (s->thread != NULL)
            g_thread_join(s->thread);
        s->thread = NULL;
        ph_srv_flusso_chiudi(s->l->f, togli_task); /* da qui nessuna risposta arriva piu' */
        g_list_free_full(s->scatti, g_free);
        s->scatti = NULL;
        s->l->f = NULL;
        lettore_lascia(s->l);
        s->l = NULL;
    }
}

static void sup_dispose(GObject *o)
{
    PhSupFinta *s = PH_SUP_FINTA(o);
    ph_superficie_chiudi(GTK_WIDGET(s), false);
    g_clear_object(&s->ultima);
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(GTK_WIDGET(o))) != NULL)
        gtk_widget_unparent(c);
    G_OBJECT_CLASS(ph_sup_finta_parent_class)->dispose(o);
}

static void ph_sup_finta_class_init(PhSupFintaClass *k)
{
    G_OBJECT_CLASS(k)->dispose = sup_dispose;
    gtk_widget_class_set_layout_manager_type(GTK_WIDGET_CLASS(k), GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(k), "ph-superficie");
}

static void ph_sup_finta_init(PhSupFinta *s)
{
    gtk_widget_set_overflow(GTK_WIDGET(s), GTK_OVERFLOW_HIDDEN);
    s->immagine = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(s->immagine), GTK_CONTENT_FIT_FILL);
    gtk_picture_set_can_shrink(GTK_PICTURE(s->immagine), TRUE);
    gtk_widget_set_parent(s->immagine, GTK_WIDGET(s));
    s->velo = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_halign(s->velo, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(s->velo, GTK_ALIGN_CENTER);
    s->messaggio = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(s->messaggio), TRUE);
    gtk_box_append(GTK_BOX(s->velo), s->messaggio);
    GtkWidget *b1 = gtk_button_new_with_label(ph_t("Riconnetti ora"));
    g_signal_connect(b1, "clicked", G_CALLBACK(riconnetti), s);
    GtkWidget *b2 = gtk_button_new_with_label(ph_t("Chiudi"));
    g_signal_connect(b2, "clicked", G_CALLBACK(chiudi_premuto), s);
    gtk_box_append(GTK_BOX(s->velo), b1);
    gtk_box_append(GTK_BOX(s->velo), b2);
    gtk_widget_set_visible(s->velo, FALSE);
    gtk_widget_set_parent(s->velo, GTK_WIDGET(s));
}

GtkWidget *ph_superficie_nuova(PhShell *sh, const PhSrvFlussoRichiesta *r, PhSuperficieAvviso avviso,
                               void *utente, PhErrore *e)
{
    PhSupFinta *s = g_object_new(PH_TIPO_SUP_FINTA, NULL);
    s->sh = sh;
    s->avviso = avviso;
    s->utente = utente;
    PhSrvFlusso *f = ph_srv_flusso_apri(ph_shell_servizi(sh), r, avviso_flusso, s, e);
    if (f == NULL) {
        g_object_ref_sink(s);
        g_object_unref(s);
        return NULL;
    }
    Lettore *l = g_new0(Lettore, 1);
    atomic_init(&l->rif, 2); /* superficie + thread */
    g_mutex_init(&l->m);
    l->f = f;
    l->w = GTK_WIDGET(s);
    s->l = l;
    s->thread = g_thread_new("ph-sup-finta", leggi, l);
    return GTK_WIDGET(s);
}

void ph_superficie_pausa(GtkWidget *w, bool pausa)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    if (s->l == NULL)
        return;
    if (pausa)
        ph_srv_flusso_pausa(s->l->f);
    else
        ph_srv_flusso_riprendi(s->l->f);
}

void ph_superficie_ridimensiona(GtkWidget *w, int larghezza, int altezza)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    if (s->l != NULL)
        ph_srv_flusso_ridimensiona(s->l->f, larghezza, altezza);
}

void ph_superficie_misura_video(GtkWidget *w, int *larghezza, int *altezza)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    *larghezza = s->vw;
    *altezza = s->vh;
}

void ph_superficie_tocco(GtkWidget *w, int vw, int vh, int azione, int x, int y, unsigned pulsanti)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    if (s->l != NULL)
        ph_srv_flusso_tocco(s->l->f, vw, vh, azione, x, y, pulsanti);
}

void ph_superficie_rotella(GtkWidget *w, int vw, int vh, int x, int y, int o, int v)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    if (s->l != NULL)
        ph_srv_flusso_rotella(s->l->f, vw, vh, x, y, o, v);
}

void ph_superficie_tasto(GtkWidget *w, bool giu, unsigned codice, unsigned meta)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    if (s->l != NULL)
        ph_srv_flusso_tasto(s->l->f, giu, codice, meta);
}

void ph_superficie_testo(GtkWidget *w, const char *testo)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    if (s->l != NULL)
        ph_srv_flusso_testo(s->l->f, testo);
}

void ph_superficie_indietro(GtkWidget *w)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    if (s->l != NULL)
        ph_srv_flusso_indietro(s->l->f);
}

GdkTexture *ph_superficie_fotografa(GtkWidget *w)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    return s->ultima != NULL ? g_object_ref(s->ultima) : NULL;
}

/* La finta stende l'immagine su tutta l'area (nessuna banda): il rettangolo e'
 * l'area intera, appena c'e' un'immagine. */
bool ph_superficie_rettangolo_immagine(GtkWidget *w, double *x, double *y, double *larghezza, double *altezza)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    int lw = gtk_widget_get_width(w), lh = gtk_widget_get_height(w);
    if (s->ultima == NULL || lw <= 0 || lh <= 0)
        return false;
    *x = 0;
    *y = 0;
    *larghezza = lw;
    *altezza = lh;
    return true;
}

/* La finta non rinuncia mai ai fotogrammi: annota soltanto il valore. */
void ph_superficie_primo_piano(GtkWidget *w, bool primo_piano)
{
    PH_SUP_FINTA(w)->primo_piano = primo_piano;
}

/* ---- Aggiunte del 23 nov: screenshot e registrazione (superficie.h) ---------- */
typedef struct {
    PhSupFinta *s;
    PhSuperficieScreenshotFatto fatto;
    void *utente;
} Scatto;

static void scatto_fatto(PhServizi *srv, PhSrvEsito esito, const char *frase, const char *percorso, GBytes *png,
                         void *u)
{
    (void)srv;
    Scatto *sc = u;
    PhSupFinta *s = sc->s;
    s->scatti = g_list_remove(s->scatti, sc);
    /* Copia in memoria dal PNG (mai la texture mostrata). */
    GdkTexture *t = png != NULL ? gdk_texture_new_from_bytes(png, NULL) : NULL;
    if (sc->fatto != NULL)
        sc->fatto(GTK_WIDGET(s), t == NULL && png != NULL ? PH_SRV_ESITO_ERRORE : esito, frase, percorso, t,
                  sc->utente);
    if (t != NULL)
        g_object_unref(t);
    g_free(sc);
}

void ph_superficie_screenshot(GtkWidget *w, unsigned dove, const char *cartella_pc, const char *nome_app,
                              GCancellable *annulla, PhSuperficieScreenshotFatto fatto, void *utente)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    if (s->l == NULL || fatto == NULL)
        return;
    Scatto *sc = g_new0(Scatto, 1);
    sc->s = s;
    sc->fatto = fatto;
    sc->utente = utente;
    s->scatti = g_list_prepend(s->scatti, sc);
    ph_srv_flusso_screenshot(s->l->f, dove, cartella_pc, nome_app, annulla, scatto_fatto, sc);
}

static void reg_avviso(PhSrvFlusso *f, const PhSrvRegDati *d, void *u)
{
    (void)f;
    PhSupFinta *s = u;
    if (s->reg_avviso != NULL)
        s->reg_avviso(GTK_WIDGET(s), d, s->reg_utente);
}

typedef struct {
    PhSuperficieRegAvviso avviso;
    void *utente;
    PhSupFinta *s;
} Rifiuto;

static void rifiuto_avviso(PhSrvFlusso *f, const PhSrvRegDati *d, void *u)
{
    (void)f;
    Rifiuto *r = u;
    if (r->avviso != NULL)
        r->avviso(GTK_WIDGET(r->s), d, r->utente);
}

void ph_superficie_registra(GtkWidget *w, const char *cartella_pc, const char *nome_app, bool con_audio,
                            PhSuperficieRegAvviso avviso, void *utente)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    if (s->l == NULL)
        return;
    if (ph_srv_flusso_registrando(s->l->f)) {
        /* Il rifiuto va a chi l'ha chiesto; quella in corso continua. Il
         * Rifiuto vive quanto la superficie (uno solo alla volta basta). */
        Rifiuto *r = g_object_get_data(G_OBJECT(s), "ph-rifiuto");
        if (r == NULL) {
            r = g_new0(Rifiuto, 1);
            g_object_set_data_full(G_OBJECT(s), "ph-rifiuto", r, g_free);
        }
        r->avviso = avviso;
        r->utente = utente;
        r->s = s;
        ph_srv_flusso_registra(s->l->f, cartella_pc, nome_app, con_audio, rifiuto_avviso, r);
        return;
    }
    s->reg_avviso = avviso;
    s->reg_utente = utente;
    ph_srv_flusso_registra(s->l->f, cartella_pc, nome_app, con_audio, reg_avviso, s);
}

void ph_superficie_registra_ferma(GtkWidget *w)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    if (s->l != NULL)
        ph_srv_flusso_registra_ferma(s->l->f);
}

bool ph_superficie_registrando(GtkWidget *w)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    return s->l != NULL && ph_srv_flusso_registrando(s->l->f);
}

void ph_superficie_ridimensiona_densita(GtkWidget *w, int larghezza, int altezza, int densita)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    if (s->l != NULL)
        ph_srv_flusso_ridimensiona_densita(s->l->f, larghezza, altezza, densita);
}

void ph_superficie_pizzico(GtkWidget *w, int video_w, int video_h, int fase, int x, int y, int passo)
{
    PhSupFinta *s = PH_SUP_FINTA(w);
    if (s->l != NULL)
        ph_srv_flusso_pizzico(s->l->f, video_w, video_h, fase, x, y, passo);
}
