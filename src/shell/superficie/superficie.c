/* Pezzo shell-superficie (4.2, 4.3, 4.5, 5.5): il widget che mostra UN flusso.
 *
 * Un thread di lettura per flusso (legge e decodifica dentro
 * ph_srv_flusso_leggi: mai lavoro pesante sul thread GTK) e consegna i
 * fotogrammi al thread GTK con ph_shell_consegna, accorpando: un solo
 * fotogramma in attesa (il nuovo rilascia il vecchio) e una sola consegna in
 * volo. Sul thread GTK si costruisce la texture (DMA-BUF senza copie, o
 * memoria avvolta senza copie) e la si mette in un GtkPicture dentro un
 * GtkGraphicsOffload, come nel prototipo misurato (strumenti/prototipo-gtk.c).
 *
 * VITA. Il thread NON conosce il widget: conosce solo il Lettore, un oggetto
 * a conteggio di riferimenti (widget + thread + una per consegna in volo).
 * Il widget si scollega dal Lettore (sup = NULL) alla chiusura, sul thread GTK,
 * prima di fermare il flusso: una consegna che arriva dopo trova sup NULL, non
 * tocca ne' il widget ne' il flusso e rilascia il fotogramma. Cosi' nessuna
 * callback usa memoria liberata e nessun riferimento al widget esce dal thread
 * GTK. DESCRITTORI: il fotogramma e' sempre rilasciato esattamente una volta
 * (ph_fotogramma_rilascia), qualunque strada prenda: scartato dallo scambio,
 * invalido, rifiutato da GTK (vedi texture_da), o alla distruzione della texture. */
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/lingua.h"
#include "logica.h"
#include "shell/superficie.h"

/* Generata da glib-compile-resources (--manual-register): il CSS del pezzo. */
void ph_risorse_superficie_register_resource(void);

#define SFOCATURA_PX 12.0f
#define ASSESTA_MS 300       /* area ferma per tanto: "a riposo", niente piu' stiramento */
#define LEGGI_TIMEOUT_MS 100 /* come COMPITO: la chiusura aspetta al massimo questo */
#define STAT_OGNI_US (5 * G_USEC_PER_SEC)

/* ---- Il Lettore: cio' che il thread di lettura e le consegne condividono -- */
typedef struct Lettore {
    atomic_int rif;
    PhSupScambio sc;
    PhSrvFlusso *f;   /* valido finche' il thread vive, e per il thread GTK finche' sup != NULL */
    GtkWidget *sup;   /* SOLO thread GTK; NULL dopo la chiusura */
    atomic_uint ricevuti, mostrati, scartati, rifiuti;
    atomic_bool non_valido_noto, formato_noto, rifiuto_noto;
    atomic_bool primo_piano;  /* scritto dal thread GTK, letto dal thread di lettura */
    int indice;       /* numero del flusso, per i conteggi */
    bool stat;        /* PH_SUPERFICIE_STAT=1 */
} Lettore;

static void lettore_prendi(Lettore *l) { atomic_fetch_add(&l->rif, 1); }

static void lettore_lascia(Lettore *l)
{
    if (atomic_fetch_sub(&l->rif, 1) != 1)
        return;
    ph_sup_scambio_libera(&l->sc);
    g_free(l);
}

static void lettore_lascia_g(gpointer p) { lettore_lascia(p); }

/* ---- Il widget ---------------------------------------------------------- */
#define PH_TIPO_SUPERFICIE (ph_superficie_get_type())
G_DECLARE_FINAL_TYPE(PhSuperficie, ph_superficie, PH, SUPERFICIE, GtkWidget)

struct _PhSuperficie {
    GtkWidget genitore;
    PhShell *sh;
    Lettore *l;                 /* NULL dopo la chiusura */
    GThread *thread;
    GtkWidget *offload, *immagine; /* l'immagine (PhImmagine) */
    double es_l, es_a;          /* misura esatta richiesta (0 = nessuna) */
    GtkWidget *protetto;        /* messaggio dello schermo protetto */
    GtkWidget *perso;           /* "Riconnetti ora" / "Chiudi" */
    GdkTexture *ultima;
    PhSuperficieAvviso avviso;
    void *utente;
    int vw, vh;                 /* misura dichiarata dal flusso */
    int tw, th;                 /* misura dell'ultima texture mostrata */
    int aw, ah;                 /* ultima area ricevuta */
    guint assesta;              /* sorgente "area ferma" */
    bool a_riposo;
    bool chiusa, in_perso, in_protetto, in_pausa;
    GList *scatti;              /* Scatto in attesa (thread GTK) */
    GList *reg_ctx;             /* RegCtx delle registrazioni chieste, liberati alla chiusura */
    double rx, ry, rl, ra;      /* rettangolo dell'immagine nell'ultima disposizione */
};

G_DEFINE_FINAL_TYPE(PhSuperficie, ph_superficie, GTK_TYPE_WIDGET)

/* ---- L'immagine: un widget che disegna la texture in un rettangolo anche non
 * intero, con il filtro giusto. Al posto di GtkPicture, che riempie sempre la sua
 * allocazione (intera). Sta dentro il GtkGraphicsOffload come prima. */
#define PH_TIPO_IMMAGINE (ph_immagine_get_type())
G_DECLARE_FINAL_TYPE(PhImmagine, ph_immagine, PH, IMMAGINE, GtkWidget)

struct _PhImmagine {
    GtkWidget genitore;
    GdkTexture *texture;
    bool esatta;               /* true: dest e' il rettangolo (relativo a questo widget) */
    graphene_rect_t dest;
};
G_DEFINE_FINAL_TYPE(PhImmagine, ph_immagine, GTK_TYPE_WIDGET)

static void imm_measure(GtkWidget *w, GtkOrientation o, int per, int *min, int *nat, int *minb, int *natb)
{
    (void)w; (void)o; (void)per;
    *min = 0; *nat = 0; *minb = -1; *natb = -1;
}

static double scala_di(GtkWidget *w)
{
    GtkNative *n = gtk_widget_get_native(w);
    GdkSurface *su = n != NULL ? gtk_native_get_surface(n) : NULL;
    return su != NULL ? gdk_surface_get_scale(su) : (double)gtk_widget_get_scale_factor(w);
}

static void imm_snapshot(GtkWidget *w, GtkSnapshot *sn)
{
    PhImmagine *im = PH_IMMAGINE(w);
    if (im->texture == NULL)
        return;
    graphene_rect_t r = im->dest;
    if (!im->esatta)
        graphene_rect_init(&r, 0, 0, (float)gtk_widget_get_width(w), (float)gtk_widget_get_height(w));
    /* Nodo texture SEMPLICE: GTK 4.18 non fa l'offload di un nodo "scaled texture"
     * (GskTextureScaleNode) con nessun filtro. Con un rettangolo grande quanto la
     * texture in pixel fisici il disegno e' gia' punto per punto. */
    gtk_snapshot_append_texture(sn, im->texture, &r);
}

static void imm_dispose(GObject *o)
{
    g_clear_object(&PH_IMMAGINE(o)->texture);
    G_OBJECT_CLASS(ph_immagine_parent_class)->dispose(o);
}

static void ph_immagine_class_init(PhImmagineClass *k)
{
    G_OBJECT_CLASS(k)->dispose = imm_dispose;
    GTK_WIDGET_CLASS(k)->measure = imm_measure;
    GTK_WIDGET_CLASS(k)->snapshot = imm_snapshot;
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(k), "ph-immagine");
}

static void ph_immagine_init(PhImmagine *im) { (void)im; }

static void immagine_imposta(GtkWidget *w, GdkTexture *t)
{
    PhImmagine *im = PH_IMMAGINE(w);
    if (g_set_object(&im->texture, t))
        gtk_widget_queue_draw(w);
}

/* esatta = false: riempie l'allocazione. Altrimenti il rettangolo (x, y, l, a), relativo al widget. */
static void immagine_destinazione(GtkWidget *w, bool esatta, double x, double y, double l, double a)
{
    PhImmagine *im = PH_IMMAGINE(w);
    graphene_rect_t d = GRAPHENE_RECT_INIT((float)x, (float)y, (float)l, (float)a);
    if (im->esatta != esatta || (esatta && !graphene_rect_equal(&im->dest, &d))) {
        im->esatta = esatta;
        im->dest = d;
        gtk_widget_queue_draw(w);
    }
}

/* ---- Texture ------------------------------------------------------------ */
/* La vita del fotogramma dentro la texture e' in logica.c (PhSupTenuto). */

static void quattro_caratteri(uint32_t fourcc, char out[5])
{
    for (int i = 0; i < 4; i++) {
        unsigned char c = (unsigned char)(fourcc >> (8 * i));
        out[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
    out[4] = '\0';
}

#if GTK_CHECK_VERSION(4, 16, 0)
/* Gli spazi dei colori si costruiscono una volta (thread GTK) e restano. */
static GdkColorState *stato_colore(PhFotoColore c)
{
    static GdkColorState *cache[8];
    static bool fallito[8];
    unsigned pr, tr, ma;
    bool ristretta;
    if (!ph_sup_cicp(c, &pr, &tr, &ma, &ristretta) || (unsigned)c >= G_N_ELEMENTS(cache))
        return NULL;
    if (cache[c] == NULL && !fallito[c]) {
        GdkCicpParams *p = gdk_cicp_params_new();
        gdk_cicp_params_set_color_primaries(p, pr);
        gdk_cicp_params_set_transfer_function(p, tr);
        gdk_cicp_params_set_matrix_coefficients(p, ma);
        gdk_cicp_params_set_range(p, ristretta ? GDK_CICP_RANGE_NARROW : GDK_CICP_RANGE_FULL);
        GError *err = NULL;
        cache[c] = gdk_cicp_params_build_color_state(p, &err);
        if (cache[c] == NULL) {
            /* Si ricorda e si scrive una volta: GTK decide da se' lo spazio. */
            fallito[c] = true;
            ph_shell_decisione("spazio dei colori lasciato a GTK", "GTK non ha costruito lo spazio dei colori %d: %s",
                               (int)c, err != NULL ? err->message : "motivo ignoto");
        }
        g_clear_error(&err);
        g_object_unref(p);
    }
    return cache[c];
}
#endif

/* Costruisce la texture; *fr passa alla texture (o si rilascia qui). NULL =
 * fallita (fotogramma comunque rilasciato). */
static GdkTexture *texture_da(Lettore *l, PhFotogramma *fr)
{
    bool memoria = fr->tipo == PH_FOTO_MEMORIA;
    PhSupTenuto *t = ph_sup_tenuto_nuovo(fr, memoria ? 1 : 2); /* memoria: solo il GBytes; DMA-BUF: GTK + noi */

    if (memoria) {
        int fm = ph_sup_formato_gdk(t->f.formato);
        if (fm < 0) {
            ph_sup_tenuto_lascia(t);
            return NULL;
        }
        GBytes *by = g_bytes_new_with_free_func(t->f.dati, t->f.passo * (size_t)t->f.altezza,
                                                ph_sup_tenuto_da_gtk, t);
        GdkTexture *tx = gdk_memory_texture_new(t->f.larghezza, t->f.altezza, (GdkMemoryFormat)fm, by,
                                                t->f.passo);
        g_bytes_unref(by);
        return tx;
    }

    /* DMA-BUF */
    GdkDisplay *disp = gdk_display_get_default();
    if (!atomic_load(&l->formato_noto)) {
        GdkDmabufFormats *elenco = gdk_display_get_dmabuf_formats(disp);
        if (elenco != NULL && !gdk_dmabuf_formats_contains(elenco, t->f.fourcc, t->f.modificatore) &&
            !atomic_exchange(&l->formato_noto, true)) {
            char fc[5];
            quattro_caratteri(t->f.fourcc, fc);
            ph_shell_decisione("DMA-BUF provato comunque",
                               "GTK non elenca fourcc %s modificatore 0x%llx tra i formati importabili",
                               fc, (unsigned long long)t->f.modificatore);
        }
    }
    GdkDmabufTextureBuilder *b = gdk_dmabuf_texture_builder_new();
    gdk_dmabuf_texture_builder_set_display(b, disp);
    gdk_dmabuf_texture_builder_set_width(b, (unsigned)t->f.larghezza);
    gdk_dmabuf_texture_builder_set_height(b, (unsigned)t->f.altezza);
    gdk_dmabuf_texture_builder_set_fourcc(b, t->f.fourcc);
    gdk_dmabuf_texture_builder_set_modifier(b, t->f.modificatore);
    gdk_dmabuf_texture_builder_set_n_planes(b, (unsigned)t->f.piani);
    for (int i = 0; i < t->f.piani; i++) { /* piani <= 4: l'ha controllato ph_sup_controlla */
        gdk_dmabuf_texture_builder_set_fd(b, (unsigned)i, t->f.fd[i]);
        gdk_dmabuf_texture_builder_set_offset(b, (unsigned)i, t->f.scostamento[i]);
        gdk_dmabuf_texture_builder_set_stride(b, (unsigned)i, t->f.passo_piano[i]);
    }
#if GTK_CHECK_VERSION(4, 16, 0)
    GdkColorState *cs = stato_colore(t->f.colore);
    if (cs != NULL)
        gdk_dmabuf_texture_builder_set_color_state(b, cs);
#endif
    GError *err = NULL;
    GdkTexture *tx = gdk_dmabuf_texture_builder_build(b, ph_sup_tenuto_da_gtk, t, &err);
    g_object_unref(b);
    g_clear_error(&err);
    ph_sup_tenuto_costruito(t, tx != NULL);
    return tx;
}

/* ---- Consegna: gira sul thread GTK -------------------------------------- */
static void mostra(void *dati)
{
    Lettore *l = dati;
    PhFotogramma fr = {0};
    if (!ph_sup_scambio_prendi(&l->sc, &fr))
        return;
    PhSuperficie *s = l->sup == NULL ? NULL : PH_SUPERFICIE(l->sup);
    if (s == NULL || s->chiusa || s->in_protetto) {
        /* Chiusa, o schermo protetto: l'immagine non si mostra e non si tiene. */
        ph_fotogramma_rilascia(&fr);
        atomic_fetch_add(&l->scartati, 1);
        return;
    }
    bool dmabuf = fr.tipo == PH_FOTO_DMABUF;
    int w = fr.larghezza, h = fr.altezza;
    uint32_t fourcc = fr.fourcc;
    uint64_t mod = fr.modificatore;
    GdkTexture *t = texture_da(l, &fr);
    if (t == NULL) {
        atomic_fetch_add(&l->scartati, 1);
        if (dmabuf) {
            atomic_fetch_add(&l->rifiuti, 1);
            ph_srv_flusso_preferisci_memoria(l->f);
            if (!atomic_exchange(&l->rifiuto_noto, true)) {
                char fc[5];
                quattro_caratteri(fourcc, fc);
                ph_shell_decisione("flusso in memoria invece che DMA-BUF",
                                   "GTK ha rifiutato il DMA-BUF (fourcc %s, modificatore 0x%llx)", fc,
                                   (unsigned long long)mod);
            }
        }
        return;
    }
    immagine_imposta(s->immagine, t);
    g_set_object(&s->ultima, t);
    g_object_unref(t);
    if (s->tw != w || s->th != h) {
        s->tw = w;
        s->th = h;
        gtk_widget_queue_allocate(GTK_WIDGET(s)); /* le bande dipendono dal video */
    }
    atomic_fetch_add(&l->mostrati, 1);
}

/* ---- Thread di lettura --------------------------------------------------- */
static void scrivi_stat(Lettore *l, unsigned *prima_ric, unsigned *prima_mos, gint64 dt_us)
{
    unsigned ric = atomic_load(&l->ricevuti), mos = atomic_load(&l->mostrati);
    double s = (double)dt_us / 1e6;
    fprintf(stderr,
            "superficie %d: ricevuti %u (%.1f/s), mostrati %u (%.1f/s), scartati %u, rifiuti DMA-BUF %u\n",
            l->indice, ric, (double)(ric - *prima_ric) / s, mos, (double)(mos - *prima_mos) / s,
            atomic_load(&l->scartati), atomic_load(&l->rifiuti));
    *prima_ric = ric;
    *prima_mos = mos;
}

/* Scrive nel registro un cambio di ritmo (o il ritorno per il primo piano). */
static void scrivi_ritmo(Lettore *l, const PhSupSforzo *s, unsigned causa, bool per_pp)
{
    if (per_pp)
        ph_shell_decisione("frequenza fotogrammi ripristinata",
                           "flusso %d: la superficie e' passata in primo piano", l->indice);
    else
        ph_shell_decisione(s->livello == 0 ? "frequenza fotogrammi ripristinata" : "frequenza fotogrammi ridotta",
                           "flusso %d: %u fotogrammi sostituiti senza essere mostrati nella finestra di "
                           "misura; ora 1 ogni %d",
                           l->indice, causa, 1 << s->livello);
}

/* Controlla e manda un fotogramma allo scambio (*fr ceduto o rilasciato). */
static void manda_allo_scambio(Lettore *l, PhSupRitmo *ritmo, PhFotogramma *fr, unsigned n)
{
    if (!ph_sup_controlla(fr)) {
        ph_fotogramma_rilascia(fr);
        atomic_fetch_add(&l->scartati, 1);
        if (!atomic_exchange(&l->non_valido_noto, true))
            ph_shell_decisione("fotogramma scartato", "fotogramma non valido (il n. %u)", n);
        return;
    }
    bool sostituito = false;
    bool manda = ph_sup_scambio_metti(&l->sc, fr, &sostituito);
    if (sostituito)
        atomic_fetch_add(&l->scartati, 1);
    bool cambiato, per_pp;
    unsigned causa;
    ph_sup_ritmo_dopo_scambio(ritmo, g_get_monotonic_time(), sostituito, atomic_load(&l->primo_piano),
                              &cambiato, &causa, &per_pp);
    if (cambiato)
        scrivi_ritmo(l, &ritmo->sf, causa, per_pp);
    if (manda) {
        lettore_prendi(l);
        ph_shell_consegna(mostra, l, lettore_lascia_g);
    }
}

static gpointer leggi(gpointer p)
{
    Lettore *l = p;
    gint64 ultimo = g_get_monotonic_time();
    unsigned prima_ric = 0, prima_mos = 0;
    PhSupRitmo ritmo = {0};
    for (;;) {
        PhFotogramma fr = {0};
        int r = ph_srv_flusso_leggi(l->f, &fr, LEGGI_TIMEOUT_MS);
        if (r < 0)
            break;
        bool cambiato, per_pp;
        unsigned causa;
        if (r > 0) {
            unsigned n = atomic_fetch_add(&l->ricevuti, 1) + 1;
            /* primo_piano si legge PRIMA del salto: in primo piano non si salta mai. */
            if (ph_sup_ritmo_arriva(&ritmo, &fr, g_get_monotonic_time(), atomic_load(&l->primo_piano),
                                    &cambiato, &causa, &per_pp)) {
                if (cambiato)
                    scrivi_ritmo(l, &ritmo.sf, causa, per_pp);
                manda_allo_scambio(l, &ritmo, &fr, n);
            } else {
                /* Non in primo piano e il thread GTK e' indietro: questo fotogramma
                 * resta da parte (mai la risoluzione); quello che sostituisce, se
                 * c'era, e' contato come scartato. */
                atomic_fetch_add(&l->scartati, 1);
                if (cambiato)
                    scrivi_ritmo(l, &ritmo.sf, causa, per_pp);
            }
        } else if (ph_sup_ritmo_silenzio(&ritmo, &fr, g_get_monotonic_time(), atomic_load(&l->primo_piano),
                                         &cambiato, &causa, &per_pp)) {
            /* Silenzio: l'ultimo fotogramma saltato non resta indietro. Era gia' stato contato come scartato:
             * la riga dei conteggi lo conta due volte solo se poi non viene mostrato. */
            atomic_fetch_sub(&l->scartati, 1);
            if (cambiato)
                scrivi_ritmo(l, &ritmo.sf, causa, per_pp);
            manda_allo_scambio(l, &ritmo, &fr, atomic_load(&l->ricevuti));
        } else if (cambiato) {
            scrivi_ritmo(l, &ritmo.sf, causa, per_pp);
        }
        if (l->stat) {
            gint64 ora = g_get_monotonic_time();
            if (ora - ultimo >= STAT_OGNI_US) {
                scrivi_stat(l, &prima_ric, &prima_mos, ora - ultimo);
                ultimo = ora;
            }
        }
    }
    ph_sup_ritmo_libera(&ritmo);
    lettore_lascia(l);
    return NULL;
}

/* ---- Disposizione e disegno ---------------------------------------------- */
/* Assegna un'area a un figlio senza scendere sotto il suo minimo (altrimenti
 * GTK protesta): se l'area e' piccolissima il figlio sborda e il clip lo taglia. */
static void alloca_figlio(GtkWidget *c, int x, int y, int l, int a)
{
    int minl = 0, mina = 0;
    gtk_widget_measure(c, GTK_ORIENTATION_HORIZONTAL, -1, &minl, NULL, NULL, NULL);
    if (l < minl)
        l = minl;
    gtk_widget_measure(c, GTK_ORIENTATION_VERTICAL, l, &mina, NULL, NULL, NULL);
    if (a < mina)
        a = mina;
    GtkAllocation al = {x, y, l, a};
    gtk_widget_size_allocate(c, &al, -1);
}

static gboolean riposo_scaduto(gpointer u)
{
    PhSuperficie *s = u;
    s->assesta = 0;
    if (!s->a_riposo) {
        s->a_riposo = true;
        gtk_widget_queue_allocate(GTK_WIDGET(s));
    }
    return G_SOURCE_REMOVE;
}

static void sup_measure(GtkWidget *w, GtkOrientation o, int per, int *min, int *nat, int *minb, int *natb)
{
    (void)w; (void)o; (void)per;
    /* Misura la decide chi mette il widget (finestra-app): l'immagine si
     * stende su cio' che riceve e puo' ridursi quanto vuole. */
    *min = 0;
    *nat = 0;
    *minb = -1;
    *natb = -1;
}

static void sup_allocate(GtkWidget *w, int larghezza, int altezza, int baseline)
{
    (void)baseline;
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (s->chiusa)
        return;
    if (larghezza != s->aw || altezza != s->ah) {
        /* L'area si muove: l'immagine si stira e la scala la GPU; a riposo
         * (area ferma per ASSESTA_MS) torna nelle sue proporzioni. */
        s->aw = larghezza;
        s->ah = altezza;
        s->a_riposo = false;
        if (s->assesta != 0)
            g_source_remove(s->assesta);
        s->assesta = g_timeout_add(ASSESTA_MS, riposo_scaduto, s);
    }
    PhSupRett r = ph_sup_rettangolo_esatto(larghezza, altezza, s->es_l, s->es_a, s->tw, s->th,
                                           scala_di(w), s->a_riposo);
    s->rx = r.x; s->ry = r.y; s->rl = r.l; s->ra = r.a;
    /* L'allocazione e' intera: contiene il rettangolo (anche non intero), che
     * l'immagine disegna da se' con la sua origine relativa. */
    int ix = (int)floor(r.x + 1e-6), iy = (int)floor(r.y + 1e-6);
    int il = (int)ceil(r.x + r.l - 1e-6) - ix, ia = (int)ceil(r.y + r.a - 1e-6) - iy;
    if (il < 1) il = 1;
    if (ia < 1) ia = 1;
    if (ix + il > larghezza) il = larghezza - ix;
    if (iy + ia > altezza) ia = altezza - iy;
    bool esatta = s->a_riposo && s->es_l > 0 && s->es_a > 0;
    immagine_destinazione(s->immagine, esatta, r.x - ix, r.y - iy, r.l, r.a);
    alloca_figlio(s->offload, ix, iy, il, ia);
    alloca_figlio(s->protetto, 0, 0, larghezza, altezza);
    int pl = 0, pa = 0;
    gtk_widget_measure(s->perso, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &pl, NULL, NULL);
    if (pl > larghezza)
        pl = larghezza;
    gtk_widget_measure(s->perso, GTK_ORIENTATION_VERTICAL, pl, NULL, &pa, NULL, NULL);
    alloca_figlio(s->perso, (larghezza - pl) / 2, (altezza - pa) / 2, pl, pa);
}

static void sup_snapshot(GtkWidget *w, GtkSnapshot *sn)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    static const GdkRGBA nero = {0, 0, 0, 1}, scuro = {0, 0, 0, 0.45f};
    float lw = (float)gtk_widget_get_width(w), lh = (float)gtk_widget_get_height(w);
    graphene_rect_t area = GRAPHENE_RECT_INIT(0, 0, lw, lh);
    gtk_snapshot_append_color(sn, &nero, &area); /* fondo e bande */
    if (s->in_protetto && !s->in_perso) {
        gtk_widget_snapshot_child(w, s->protetto, sn);
    } else if (s->in_perso) {
        /* Ultima immagine sfocata e scurita (5.5). */
        gtk_snapshot_push_blur(sn, SFOCATURA_PX);
        gtk_widget_snapshot_child(w, s->offload, sn);
        gtk_snapshot_pop(sn);
        gtk_snapshot_append_color(sn, &scuro, &area);
    } else {
        gtk_widget_snapshot_child(w, s->offload, sn);
    }
    gtk_widget_snapshot_child(w, s->perso, sn);
}

static void aggiorna_velo(PhSuperficie *s)
{
    /* Con la connessione persa il messaggio lascia il posto ai pulsanti. */
    gtk_widget_set_visible(s->protetto, s->in_protetto && !s->in_perso);
    gtk_widget_set_visible(s->offload, !s->in_protetto);
    gtk_widget_set_visible(s->perso, s->in_perso);
    gtk_widget_queue_draw(GTK_WIDGET(s));
}

/* ---- Avvisi del flusso (thread GTK, fino a ph_srv_flusso_chiudi) --------- */
static void avviso_flusso(PhSrvFlusso *f, const PhSrvFlussoDati *d, void *utente)
{
    (void)f;
    PhSuperficie *s = utente;
    if (s->chiusa)
        return;
    /* Chi riceve l'avviso puo' chiudere la superficie dentro la propria
     * funzione: il riferimento tiene il widget in vita fino alla fine e dopo
     * la chiamata non si tocca piu' nulla. */
    g_object_ref(s);
    switch (d->evento) {
    case PH_SRV_FL_APERTO:
    case PH_SRV_FL_MISURA:
        s->vw = d->larghezza;
        s->vh = d->altezza;
        if (s->avviso != NULL)
            s->avviso(GTK_WIDGET(s), PH_SUP_EV_MISURA, s->vw, s->vh, s->utente);
        break;
    case PH_SRV_FL_PROTETTO: {
        bool era = s->in_protetto;
        s->in_protetto = d->protetto;
        if (ph_sup_chiave_da_chiedere(era, s->in_protetto) && s->l != NULL)
            ph_srv_flusso_chiave(s->l->f); /* l'immagine torna anche con lo schermo dell'app fermo */
        if (s->in_protetto) {
            /* Non si tiene nessuna immagine del contenuto protetto (5.5). */
            immagine_imposta(s->immagine, NULL);
            g_clear_object(&s->ultima);
            s->tw = s->th = 0;
        }
        aggiorna_velo(s);
        break;
    }
    case PH_SRV_FL_PERSO:
        s->in_perso = true;
        aggiorna_velo(s);
        break;
    case PH_SRV_FL_RIPRESO:
        s->in_perso = false;
        aggiorna_velo(s);
        break;
    case PH_SRV_FL_APP_CHIUSA:
        if (s->avviso != NULL)
            s->avviso(GTK_WIDGET(s), PH_SUP_EV_APP_CHIUSA, 0, 0, s->utente);
        break;
    case PH_SRV_FL_ERRORE:
        if (s->avviso != NULL)
            s->avviso(GTK_WIDGET(s), PH_SUP_EV_ERRORE, 0, 0, s->utente);
        break;
    }
    g_object_unref(s);
}

static void riconnetti_premuto(GtkButton *b, gpointer p)
{
    (void)b;
    PhSuperficie *s = p;
    if (!s->chiusa)
        ph_srv_riconnetti_ora(ph_shell_servizi(s->sh));
}

static void chiudi_premuto(GtkButton *b, gpointer p)
{
    (void)b;
    PhSuperficie *s = p;
    if (s->chiusa || s->avviso == NULL)
        return;
    g_object_ref(s);
    s->avviso(GTK_WIDGET(s), PH_SUP_EV_CHIUDI, 0, 0, s->utente);
    g_object_unref(s);
}

/* ---- Screenshot e registrazione ------------------------------------------ */
/* Una richiesta di screenshot. Vive nella lista del widget (riferimento 1);
 * LIMITE NOTO: se chi l'ha chiesta la annulla con il GCancellable la risposta non
 * arriva mai e la richiesta resta nella lista (pochi byte) fino alla chiusura del
 * widget: toglierla all'annullamento richiederebbe un segnale che puo' scattare
 * da qualunque thread, un rischio non giustificato.
 * la decodifica del PNG ne prende un altro. Il widget la scollega (s = NULL)
 * alla chiusura: quello che arriva dopo non tocca ne' widget ne' utente. */
typedef struct Scatto {
    atomic_int rif;
    PhSuperficie *s; /* solo thread GTK; NULL dopo la chiusura */
    PhSuperficieScreenshotFatto fatto;
    void *utente;
    PhSrvEsito esito;
    char *frase, *percorso;
    GBytes *png;
    GdkTexture *img;
} Scatto;

static void scatto_lascia(Scatto *sc)
{
    if (atomic_fetch_sub(&sc->rif, 1) != 1)
        return;
    g_free(sc->frase);
    g_free(sc->percorso);
    g_clear_pointer(&sc->png, g_bytes_unref);
    g_clear_object(&sc->img);
    g_free(sc);
}

static void scatto_lascia_g(gpointer p) { scatto_lascia(p); }

/* Consegna l'esito a chi l'ha chiesto (thread GTK, widget vivo). */
static void scatto_finito(Scatto *sc)
{
    PhSuperficie *s = sc->s;
    if (s == NULL || s->chiusa)
        return;
    s->scatti = g_list_remove(s->scatti, sc);
    sc->s = NULL;
    g_object_ref(s);
    if (sc->fatto != NULL)
        sc->fatto(GTK_WIDGET(s), sc->esito, sc->frase, sc->percorso, sc->img, sc->utente);
    g_object_unref(s);
    scatto_lascia(sc); /* il riferimento della lista */
}

static void scatto_arrivato(void *dati)
{
    scatto_finito(dati);
}

/* Immagine per gli appunti non disponibile: se il file e' stato salvato l'esito
 * resta OK (con percorso, immagine NULL e la frase), altrimenti e' un errore. */
static void scatto_senza_immagine(Scatto *sc, char *frase, PhSupDecodifica come)
{
    if (!ph_sup_esito_scatto(true, sc->percorso != NULL, come).esito_ok)
        sc->esito = PH_SRV_ESITO_ERRORE;
    g_free(sc->frase);
    sc->frase = frase;
}

/* L'immagine per gli appunti si decodifica dal PNG in memoria (mai dalla
 * texture mostrata) e fuori dal thread GTK. */
static void scatto_decodifica(Scatto *sc)
{
    char *frase = NULL;
    if (!ph_sup_decodifica_png(sc->png, &sc->img, &frase))
        scatto_senza_immagine(sc, frase, PH_SUP_DEC_FALLITA); /* tradotta; il dettaglio e' su stderr */
    g_clear_pointer(&sc->png, g_bytes_unref);
}

static gpointer scatto_thread(gpointer p)
{
    Scatto *sc = p; /* porta il proprio riferimento */
    scatto_decodifica(sc);
    ph_shell_consegna(scatto_arrivato, sc, scatto_lascia_g);
    return NULL;
}

static void scatto_dai_servizi(PhServizi *srv, PhSrvEsito esito, const char *frase, const char *percorso,
                               GBytes *png, void *utente)
{
    (void)srv;
    Scatto *sc = utente;
    if (sc->s == NULL)
        return;
    sc->esito = esito;
    sc->frase = g_strdup(frase);
    sc->percorso = g_strdup(percorso);
    if (png != NULL && esito == PH_SRV_ESITO_OK) {
        sc->png = g_bytes_ref(png);
        atomic_fetch_add(&sc->rif, 1); /* per il thread */
        GThread *t = g_thread_try_new("ph-sup-png", scatto_thread, sc, NULL);
        if (t != NULL) {
            g_thread_unref(t);
            return;
        }
        scatto_lascia(sc);
        /* Niente thread: NON si decodifica sul thread GTK; stesso esito di una decodifica fallita. */
        scatto_senza_immagine(sc, g_strdup(ph_t("Non riesco a leggere l'immagine dello screenshot.")),
                               PH_SUP_DEC_NO_THREAD);
        g_clear_pointer(&sc->png, g_bytes_unref);
    }
    scatto_finito(sc);
}

typedef struct {
    PhSuperficie *s;
    PhSuperficieRegAvviso avviso;
    void *utente;
} RegCtx;

static void reg_dai_servizi(PhSrvFlusso *f, const PhSrvRegDati *d, void *u)
{
    (void)f;
    RegCtx *c = u;
    PhSuperficie *s = c->s;
    if (s->chiusa || c->avviso == NULL)
        return;
    /* L'avviso puo' chiudere la superficie: il riferimento tiene il widget e
     * dopo la chiamata non si tocca c (puo' essere stato liberato). */
    g_object_ref(s);
    c->avviso(GTK_WIDGET(s), d, c->utente);
    g_object_unref(s);
}

/* ---- Chiusura ------------------------------------------------------------- */
void ph_superficie_chiudi(GtkWidget *w, bool togli_task)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (s->chiusa)
        return;
    s->chiusa = true;
    if (s->assesta != 0)
        g_source_remove(s->assesta);
    s->assesta = 0;
    Lettore *l = s->l;
    s->l = NULL;
    if (l != NULL) {
        /* Prima ci si scollega (le consegne in volo non toccheranno piu' ne'
         * widget ne' flusso), poi si ferma il thread: leggi torna entro
         * LEGGI_TIMEOUT_MS (contratto di servizi.h), quindi la join e' limitata. */
        l->sup = NULL;
        /* Una registrazione in corso si ferma PRIMA di chiudere il flusso. */
        if (ph_srv_flusso_registrando(l->f))
            ph_srv_flusso_registra_ferma(l->f);
        ph_sup_scambio_chiudi(&l->sc);
        ph_srv_flusso_interrompi(l->f);
        if (s->thread != NULL) {
            gint64 t0 = g_get_monotonic_time();
            g_thread_join(s->thread);
            gint64 ms = (g_get_monotonic_time() - t0) / 1000;
            if (ms > 150) /* leggi doveva tornare entro 100 ms: il backend non rispetta l'interruzione */
                ph_shell_decisione("chiusura lenta del flusso",
                                   "il thread di lettura ha impiegato %lld ms a fermarsi (attesi al massimo ~100)",
                                   (long long)ms);
        }
        s->thread = NULL;
        ph_srv_flusso_chiudi(l->f, togli_task);
        l->f = NULL;
        lettore_lascia(l);
    }
    /* Dopo flusso_chiudi nessuna risposta dei servizi puo' piu' arrivare: gli
     * screenshot ancora in attesa si scollegano (le consegne gia' partite
     * trovano s NULL e non fanno nulla) e i contesti delle registrazioni si liberano. */
    for (GList *g = s->scatti; g != NULL; g = g->next) {
        Scatto *sc = g->data;
        sc->s = NULL;
        scatto_lascia(sc);
    }
    g_list_free(s->scatti);
    s->scatti = NULL;
    g_list_free_full(s->reg_ctx, g_free);
    s->reg_ctx = NULL;
    immagine_imposta(s->immagine, NULL);
    g_clear_object(&s->ultima);
}

static void sup_dispose(GObject *o)
{
    PhSuperficie *s = PH_SUPERFICIE(o);
    ph_superficie_chiudi(GTK_WIDGET(s), false);
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(GTK_WIDGET(o))) != NULL)
        gtk_widget_unparent(c);
    G_OBJECT_CLASS(ph_superficie_parent_class)->dispose(o);
}

static void ph_superficie_class_init(PhSuperficieClass *k)
{
    G_OBJECT_CLASS(k)->dispose = sup_dispose;
    GtkWidgetClass *wk = GTK_WIDGET_CLASS(k);
    wk->measure = sup_measure;
    wk->size_allocate = sup_allocate;
    wk->snapshot = sup_snapshot;
    gtk_widget_class_set_css_name(wk, "ph-superficie");
}

static void ph_superficie_init(PhSuperficie *s)
{
    gtk_widget_set_overflow(GTK_WIDGET(s), GTK_OVERFLOW_HIDDEN);

    /* L'immagine: prototipo, strada 1 di 4.3. */
    s->immagine = g_object_new(PH_TIPO_IMMAGINE, NULL);
    s->offload = gtk_graphics_offload_new(s->immagine);
    gtk_graphics_offload_set_enabled(GTK_GRAPHICS_OFFLOAD(s->offload), GTK_GRAPHICS_OFFLOAD_ENABLED);
#if GTK_CHECK_VERSION(4, 16, 0)
    gtk_graphics_offload_set_black_background(GTK_GRAPHICS_OFFLOAD(s->offload), TRUE);
#endif
    gtk_widget_set_parent(s->offload, GTK_WIDGET(s));

    /* Schermo protetto (5.5). */
    s->protetto = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(s->protetto, "ph-superficie-protetto");
    GtkWidget *msg = gtk_label_new(ph_t("Questa app protegge il suo schermo: il contenuto non si puo' mostrare."));
    gtk_label_set_wrap(GTK_LABEL(msg), TRUE);
    gtk_label_set_justify(GTK_LABEL(msg), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars(GTK_LABEL(msg), 40);
    gtk_widget_set_halign(msg, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(msg, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(msg, TRUE);
    gtk_box_append(GTK_BOX(s->protetto), msg);
    gtk_widget_set_visible(s->protetto, FALSE);
    gtk_widget_set_parent(s->protetto, GTK_WIDGET(s));

    /* Connessione persa (5.5). */
    s->perso = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_accessible_update_property(GTK_ACCESSIBLE(s->perso), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   ph_t("Connessione persa"), -1);
    GtkWidget *b1 = gtk_button_new_with_label(ph_t("Riconnetti ora"));
    gtk_widget_add_css_class(b1, "ph-accento");
    g_signal_connect(b1, "clicked", G_CALLBACK(riconnetti_premuto), s);
    GtkWidget *b2 = gtk_button_new_with_label(ph_t("Chiudi"));
    gtk_widget_add_css_class(b2, "ph-ghost");
    g_signal_connect(b2, "clicked", G_CALLBACK(chiudi_premuto), s);
    gtk_box_append(GTK_BOX(s->perso), b1);
    gtk_box_append(GTK_BOX(s->perso), b2);
    gtk_widget_set_visible(s->perso, FALSE);
    gtk_widget_set_parent(s->perso, GTK_WIDGET(s));
}

/* ---- Funzioni di superficie.h -------------------------------------------- */
GtkWidget *ph_superficie_nuova(PhShell *sh, const PhSrvFlussoRichiesta *r, PhSuperficieAvviso avviso,
                               void *utente, PhErrore *e)
{
    static gsize risorse = 0;
    static atomic_int contatore;
    if (g_once_init_enter(&risorse)) {
        ph_risorse_superficie_register_resource();
        g_once_init_leave(&risorse, 1);
    }
    ph_shell_stile_carica("/it/yasde/shell/superficie.css");

    PhSuperficie *s = g_object_new(PH_TIPO_SUPERFICIE, NULL); /* floating, come ogni widget */
    s->sh = sh;
    s->avviso = avviso;
    s->utente = utente;
    PhSrvFlusso *f = ph_srv_flusso_apri(ph_shell_servizi(sh), r, avviso_flusso, s, e);
    if (f == NULL) {
        s->chiusa = true; /* nessun flusso: niente da chiudere */
        g_object_ref_sink(s);
        g_object_unref(s);
        return NULL;
    }
    Lettore *l = g_new0(Lettore, 1);
    atomic_init(&l->rif, 2); /* widget + thread */
    ph_sup_scambio_inizia(&l->sc);
    l->f = f;
    l->sup = GTK_WIDGET(s);
    l->indice = atomic_fetch_add(&contatore, 1) + 1;
    const char *st = g_getenv("PH_SUPERFICIE_STAT");
    l->stat = st != NULL && st[0] == '1';
    s->l = l;
    s->thread = g_thread_try_new("ph-sup-lettura", leggi, l, NULL);
    if (s->thread == NULL) {
        /* Nessun thread: si chiude il flusso e si rinuncia. */
        s->l = NULL;
        l->sup = NULL;
        ph_srv_flusso_interrompi(f);
        ph_srv_flusso_chiudi(f, false);
        ph_sup_scambio_chiudi(&l->sc);
        lettore_lascia(l);
        lettore_lascia(l);
        s->chiusa = true;
        g_object_ref_sink(s);
        g_object_unref(s);
        ph_errore(e, "superficie: impossibile avviare il thread di lettura");
        return NULL;
    }
    return GTK_WIDGET(s);
}

void ph_superficie_pausa(GtkWidget *w, bool pausa)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (s->chiusa || s->l == NULL)
        return;
    s->in_pausa = pausa;
    if (pausa)
        ph_srv_flusso_pausa(s->l->f);
    else
        ph_srv_flusso_riprendi(s->l->f);
}

void ph_superficie_ridimensiona(GtkWidget *w, int larghezza, int altezza)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (!s->chiusa && s->l != NULL)
        ph_srv_flusso_ridimensiona(s->l->f, larghezza, altezza);
}

void ph_superficie_misura_video(GtkWidget *w, int *larghezza, int *altezza)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    *larghezza = s->vw;
    *altezza = s->vh;
}

void ph_superficie_tocco(GtkWidget *w, int vw, int vh, int azione, int x, int y, unsigned pulsanti)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (!s->chiusa && s->l != NULL)
        ph_srv_flusso_tocco(s->l->f, vw, vh, azione, x, y, pulsanti);
}

void ph_superficie_rotella(GtkWidget *w, int vw, int vh, int x, int y, int o, int v)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (!s->chiusa && s->l != NULL)
        ph_srv_flusso_rotella(s->l->f, vw, vh, x, y, ph_sup_satura_i16(o), ph_sup_satura_i16(v));
}

void ph_superficie_tasto(GtkWidget *w, bool giu, unsigned codice, unsigned meta)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (!s->chiusa && s->l != NULL)
        ph_srv_flusso_tasto(s->l->f, giu, codice, meta);
}

void ph_superficie_testo(GtkWidget *w, const char *testo)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (!s->chiusa && s->l != NULL && testo != NULL)
        ph_srv_flusso_testo(s->l->f, testo);
}

void ph_superficie_indietro(GtkWidget *w)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (!s->chiusa && s->l != NULL)
        ph_srv_flusso_indietro(s->l->f);
}

void ph_superficie_pagina(GtkWidget *w, uint8_t pagina)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (!s->chiusa && s->l != NULL)
        ph_srv_flusso_pagina(s->l->f, pagina);
}

GdkTexture *ph_superficie_fotografa(GtkWidget *w)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    return s->ultima != NULL ? g_object_ref(s->ultima) : NULL;
}

bool ph_superficie_rettangolo_immagine(GtkWidget *w, double *x, double *y, double *larghezza, double *altezza)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (s->chiusa || s->tw <= 0 || s->th <= 0 || s->rl <= 0 || s->ra <= 0)
        return false;
    *x = s->rx;
    *y = s->ry;
    *larghezza = s->rl;
    *altezza = s->ra;
    return true;
}

void ph_superficie_primo_piano(GtkWidget *w, bool primo_piano)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (!s->chiusa && s->l != NULL)
        atomic_store(&s->l->primo_piano, primo_piano);
}

void ph_superficie_screenshot(GtkWidget *w, unsigned dove, const char *cartella_pc, const char *nome_app,
                              GCancellable *annulla, PhSuperficieScreenshotFatto fatto, void *utente)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (s->chiusa || s->l == NULL || fatto == NULL)
        return;
    Scatto *sc = g_new0(Scatto, 1);
    atomic_init(&sc->rif, 1); /* la lista */
    sc->s = s;
    sc->fatto = fatto;
    sc->utente = utente;
    s->scatti = g_list_prepend(s->scatti, sc);
    ph_srv_flusso_screenshot(s->l->f, dove, cartella_pc, nome_app, annulla, scatto_dai_servizi, sc);
}

void ph_superficie_registra(GtkWidget *w, const char *cartella_pc, const char *nome_app, bool con_audio,
                            PhSuperficieRegAvviso avviso, void *utente)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (s->chiusa || s->l == NULL)
        return;
    /* Un contesto per ogni richiesta (anche quella rifiutata perche' gia' in
     * registrazione: il suo ERRORE va a chi l'ha chiesta); si liberano tutti
     * alla chiusura, dopo la quale non arriva piu' nulla. */
    RegCtx *c = g_new0(RegCtx, 1);
    c->s = s;
    c->avviso = avviso;
    c->utente = utente;
    s->reg_ctx = g_list_prepend(s->reg_ctx, c);
    ph_srv_flusso_registra(s->l->f, cartella_pc, nome_app, con_audio, reg_dai_servizi, c);
}

void ph_superficie_registra_ferma(GtkWidget *w)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (!s->chiusa && s->l != NULL)
        ph_srv_flusso_registra_ferma(s->l->f);
}

bool ph_superficie_registrando(GtkWidget *w)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    return !s->chiusa && s->l != NULL && ph_srv_flusso_registrando(s->l->f);
}

void ph_superficie_ridimensiona_densita(GtkWidget *w, int larghezza, int altezza, int densita)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    /* Come ph_superficie_ridimensiona: dopo la chiusura nulla; la pausa, la
     * registrazione e i limiti li gestiscono i servizi. */
    if (!s->chiusa && s->l != NULL)
        ph_srv_flusso_ridimensiona_densita(s->l->f, larghezza, altezza, densita);
}

void ph_superficie_misura_esatta(GtkWidget *w, double larghezza, double altezza)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (s->chiusa)
        return;
    if (!(larghezza > 0) || !(altezza > 0))
        larghezza = altezza = 0;
    if (s->es_l == larghezza && s->es_a == altezza)
        return;
    s->es_l = larghezza;
    s->es_a = altezza;
    gtk_widget_queue_allocate(w);
}

/* Il pizzico passa ai servizi come tocco, rotella e tasti (anche con il velo), tranne in pausa. La superficie NON tiene i
 * tocchi in corso e non ne chiude nessuno: l'ordine dei gesti (chiudere il pizzico
 * prima di un tocco, e viceversa) e' di finestra-app (CONTRATTI.md 17); il telefono
 * chiude comunque da solo un pizzico senza passi dopo 400 ms. */
void ph_superficie_pizzico(GtkWidget *w, int video_w, int video_h, int fase, int x, int y, int passo)
{
    PhSuperficie *s = PH_SUPERFICIE(w);
    if (s->chiusa || s->l == NULL ||
        !ph_sup_pizzico_ammesso(fase, s->in_pausa))
        return;
    ph_srv_flusso_pizzico(s->l->f, video_w, video_h, fase, x, y, passo);
}
