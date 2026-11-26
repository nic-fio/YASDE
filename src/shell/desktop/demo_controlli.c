/* Solo per il demo (compilato in demo-desktop, mai nel prodotto): prova a schermo del widget
 * Interruttori rapidi (agente D). Quattro capsule di misure diverse; si controlla la disposizione
 * vera che GTK ha dato ai tasti: tondi soli o con nome e stato sotto, nessuna etichetta tagliata
 * con i puntini, il blocco centrato con almeno 8 px liberi sopra e sotto dal bordo della capsula.
 * Poi la misura cambia a caldo come fa la maniglia (che non ricostruisce il widget) e nome e stato
 * devono sparire e ricomparire da soli.
 *   controlli-attendi   se all'uscita la verifica non e' stata eseguita, il processo esce con errore
 *   controlli-misure    mette le capsule nel profilo (aspetta il profilo, con scadenza)
 *   controlli-verifica  controlla (a fotogrammi, con scadenza) e chiude la sequenza */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "interno.h"
#include "logica_controlli.h"

void ph_demo_controlli(PhShell *sh, const char *cosa);

#define CASI 4
/* larghezza, altezza, testo atteso (1 si, 0 no, -1 dipende dal carattere: non si afferma) */
static const struct {
    int l, a, testo;
} CASO[CASI] = {{200, 72, 0}, {250, 96, -1}, {250, 112, 1}, {320, 112, 1}};
#define CASO_CALDO 3 /* la capsula che si ridimensiona a caldo */

static bool verifica_fatta;

static void esci_senza_verifica(void)
{
    if (!verifica_fatta) {
        fprintf(stderr, "controlli: la verifica non e' stata eseguita\n");
        _exit(1);
    }
}

/* Raccoglie in ordine i widget con la classe data. */
static void raccogli(GtkWidget *radice, const char *classe, GtkWidget **out, int max, int *n)
{
    for (GtkWidget *c = gtk_widget_get_first_child(radice); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (gtk_widget_has_css_class(c, classe) && *n < max)
            out[(*n)++] = c;
        raccogli(c, classe, out, max, n);
    }
}

typedef struct {
    GtkWidget *radice;
    GtkWidget *scheda[CASI];
    int fase;
    gint64 scadenza;
    char dettaglio[160];
    const char *motivo; /* perche' l'ultimo controllo non e' passato (per il messaggio di errore) */
} Verifica;

/* Cerca le capsule per misura (l'ordine nell'albero non e' garantito). */
static bool trova_schede(Verifica *v)
{
    GtkWidget *tutte[16];
    int n = 0;
    raccogli(v->radice, "ph-desktop-widget", tutte, 16, &n);
    for (int i = 0; i < n; i++) {
        PhDskWidget *w = g_object_get_data(G_OBJECT(tutte[i]), "ph-desktop-widget");
        if (w == NULL || w->tipo != PH_CFG_WIDGET_INTERRUTTORI)
            continue;
        for (int k = 0; k < CASI; k++)
            if (w->larghezza == CASO[k].l && w->altezza == CASO[k].a)
                v->scheda[k] = tutte[i];
    }
    for (int k = 0; k < CASI; k++)
        if (v->scheda[k] == NULL)
            return false;
    return true;
}

/* Bordo alto e basso di un figlio nelle coordinate della scheda. */
static bool bordi(GtkWidget *scheda, GtkWidget *f, float *alto, float *basso)
{
    graphene_rect_t r;
    if (!gtk_widget_compute_bounds(f, scheda, &r))
        return false;
    *alto = r.origin.y;
    *basso = r.origin.y + r.size.height;
    return true;
}

/* true se la scheda e' disposta come deve; false = non ancora o no (v->motivo dice perche': si
 * riprova fino alla scadenza, poi g_error). */
static bool controlla(Verifica *v, GtkWidget *scheda, int atteso)
{
    GtkWidget *btn[3], *nomi[3], *stati[3], *tondi[3];
    int nb = 0, nn = 0, ns = 0, nt = 0;
    raccogli(scheda, "ph-desktop-interruttore", btn, 3, &nb);
    raccogli(scheda, "ph-desktop-interruttore-nome", nomi, 3, &nn);
    raccogli(scheda, "ph-desktop-interruttore-stato", stati, 3, &ns);
    raccogli(scheda, "ph-desktop-interruttore-tondo", tondi, 3, &nt);
    if (nb != 3 || nn != 3 || ns != 3 || nt != 3)
        g_error("controlli: attesi 3 tasti, nomi, stati e tondi, visti %d %d %d %d", nb, nn, ns, nt);
    if (gtk_widget_get_width(btn[0]) <= 0 || gtk_widget_get_height(btn[0]) <= 0) {
        v->motivo = "non ancora disposto";
        return false;
    }
    bool testo = gtk_widget_get_child_visible(nomi[0]);
    for (int i = 0; i < 3; i++)
        if (gtk_widget_get_child_visible(nomi[i]) != testo || gtk_widget_get_child_visible(stati[i]) != testo) {
            v->motivo = "nome e stato non sono tutti visibili o tutti nascosti";
            return false;
        }
    if (atteso >= 0 && (testo ? 1 : 0) != atteso) {
        v->motivo = atteso ? "il testo doveva esserci" : "il testo non doveva esserci";
        return false;
    }
    float sopra_min = 1e9f, sotto_min = 1e9f, sopra_max = -1e9f, sotto_max = -1e9f;
    /* Le coordinate dei figli e gtk_widget_get_height partono dal contenuto: l'imbottitura della
     * capsula sta fuori. La capsula vera e' alta quanto la scheda nel suo contenitore. */
    float h_scheda = (float)gtk_widget_get_height(scheda);
    graphene_rect_t rc;
    if (!gtk_widget_compute_bounds(scheda, gtk_widget_get_parent(scheda), &rc)) {
        v->motivo = "misura della capsula non calcolabile";
        return false;
    }
    float h_capsula = rc.size.height;
    float pad = (h_capsula - h_scheda) / 2;
    for (int i = 0; i < 3; i++) {
        int l = gtk_widget_get_width(btn[i]), a = gtk_widget_get_height(btn[i]);
        float alto, basso, a2, b2;
        if (!bordi(scheda, tondi[i], &alto, &basso)) {
            v->motivo = "bordi del tondo non calcolabili";
            return false;
        }
        if (testo) {
            if (l < PH_DSK_CTL_COL_MIN) {
                v->motivo = "colonna troppo stretta per il testo";
                return false;
            }
            /* Nessuna etichetta tagliata con i puntini (D4), nome o stato che sia. */
            GtkWidget *et[2] = {nomi[i], stati[i]};
            for (int k = 0; k < 2; k++) {
                PangoLayout *ly = gtk_label_get_layout(GTK_LABEL(et[k]));
                int lw, lh;
                pango_layout_get_pixel_size(ly, &lw, &lh);
                if (pango_layout_is_ellipsized(ly) || gtk_widget_get_height(et[k]) < lh ||
                    gtk_widget_get_width(et[k]) < lw) {
                    v->motivo = "un'etichetta e' tagliata";
                    return false;
                }
            }
            if (!bordi(scheda, stati[i], &a2, &b2)) {
                v->motivo = "bordi dello stato non calcolabili";
                return false;
            }
            /* Il nome (anche su due righe) non si sovrappone allo stato. */
            float na, nb2;
            if (!bordi(scheda, nomi[i], &na, &nb2) || nb2 > a2 + 0.5f || na < basso) {
                v->motivo = "il nome si sovrappone allo stato o al tondo";
                return false;
            }
            basso = b2;
        } else if (l != a || l < 36) {
            v->motivo = "i tondi non sono tondi";
            return false;
        }
        alto += pad;
        basso += pad;
        if (alto < sopra_min)
            sopra_min = alto;
        if (alto > sopra_max)
            sopra_max = alto;
        float sotto = h_capsula - basso;
        if (sotto < sotto_min)
            sotto_min = sotto;
        if (sotto > sotto_max)
            sotto_max = sotto;
    }
    /* D5: almeno 8 px liberi sopra e sotto, e il blocco centrato (2 px di tolleranza per gli arrotondamenti). */
    if (sopra_min < PH_DSK_CTL_MARGINE_V || sotto_min < PH_DSK_CTL_MARGINE_V) {
        snprintf(v->dettaglio, sizeof v->dettaglio,
                 "meno di 8 px liberi sopra o sotto (%.1f e %.1f, capsula alta %.0f, testo %d)", sopra_min, sotto_min,
                 h_capsula, testo ? 1 : 0);
        v->motivo = v->dettaglio;
        return false;
    }
    if (sopra_max - sopra_min > 1.5f || sotto_max - sotto_min > 1.5f || (sopra_min - sotto_min > 2.5f) ||
        (sotto_min - sopra_min > 2.5f)) {
        snprintf(v->dettaglio, sizeof v->dettaglio, "il blocco non e' centrato in verticale (sopra %.1f-%.1f, sotto %.1f-%.1f)",
                 sopra_min, sopra_max, sotto_min, sotto_max);
        v->motivo = v->dettaglio;
        return false;
    }
    return true;
}

/* Cambia la misura della capsula a caldo come la maniglia (widget.c, geometria_applica): angolo in
 * alto a sinistra fermo, richiesta di misura e posto nel contenitore libero. */
static void ridimensiona(Verifica *v, int l, int a)
{
    GtkWidget *scheda = v->scheda[CASO_CALDO];
    PhDskWidget *w = g_object_get_data(G_OBJECT(scheda), "ph-desktop-widget");
    gtk_widget_set_size_request(scheda, l, a);
    ph_shell_libero_sposta(w->d->libero, scheda, w->d->area_l - w->destra - CASO[CASO_CALDO].l, w->alto, l, a);
}

static gboolean fotogramma(GtkWidget *radice, GdkFrameClock *clock, gpointer dati)
{
    (void)clock;
    (void)radice;
    Verifica *v = dati;
    if (v->fase == 0 && !trova_schede(v)) {
        v->motivo = "le capsule non sono ancora comparse";
        if (g_get_monotonic_time() > v->scadenza)
            g_error("controlli: %s", v->motivo);
        return G_SOURCE_CONTINUE;
    }
    bool ok = false;
    switch (v->fase) {
    case 0:
        ok = true;
        for (int k = 0; k < CASI && ok; k++)
            ok = controlla(v, v->scheda[k], CASO[k].testo);
        break;
    case 1: ok = controlla(v, v->scheda[CASO_CALDO], 0); break; /* ridotta a 200x72 */
    case 2: ok = controlla(v, v->scheda[CASO_CALDO], 1); break; /* riallargata */
    default: break;
    }
    if (!ok) {
        if (g_get_monotonic_time() > v->scadenza)
            g_error("controlli: fase %d: %s", v->fase, v->motivo != NULL ? v->motivo : "?");
        return G_SOURCE_CONTINUE;
    }
    v->fase++;
    v->scadenza = g_get_monotonic_time() + 4 * G_USEC_PER_SEC;
    if (v->fase == 1)
        ridimensiona(v, 200, 72); /* come la maniglia: nessuna ricostruzione */
    else if (v->fase == 2)
        ridimensiona(v, CASO[CASO_CALDO].l, CASO[CASO_CALDO].a);
    else {
        verifica_fatta = true;
        g_message("controlli: tondi soli o con testo, nessun nome tagliato, blocco centrato con margini, a caldo");
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* Mette le capsule nel profilo; se il profilo non c'e' ancora aspetta, con scadenza. */
typedef struct {
    PhShell *sh;
    gint64 scadenza;
} AttesaMisure;

static gboolean misure_tentativo(gpointer dati)
{
    AttesaMisure *a = dati;
    GtkWidget *radice = ph_shell_radice(a->sh);
    PhDesktop *d = g_object_get_data(G_OBJECT(radice), "ph-desktop");
    PhCfgTelefono *p = d != NULL ? ph_dsk_profilo(d) : NULL;
    if (p == NULL) {
        if (g_get_monotonic_time() > a->scadenza)
            g_error("controlli: dopo 10 s il profilo non e' arrivato");
        return G_SOURCE_CONTINUE;
    }
    memset(p->widget, 0, sizeof p->widget);
    for (int i = 0; i < CASI; i++) {
        p->widget[i].tipo = PH_CFG_WIDGET_INTERRUTTORI;
        p->widget[i].destra = 16 + (i % 2) * 340;
        p->widget[i].alto = 16 + (i / 2) * 128;
        p->widget[i].larghezza = CASO[i].l;
        p->widget[i].altezza = CASO[i].a;
        p->widget[i].dimensione = CASO[i].a >= 112 ? PH_CFG_WIDGET_MEDIO : PH_CFG_WIDGET_PICCOLO;
    }
    p->n_widget = CASI;
    ph_dsk_widget_ricostruisci(d);
    return G_SOURCE_REMOVE;
}

void ph_demo_controlli(PhShell *sh, const char *cosa)
{
    GtkWidget *radice = ph_shell_radice(sh);
    if (strcmp(cosa, "controlli-attendi") == 0) {
        atexit(esci_senza_verifica);
    } else if (strcmp(cosa, "controlli-misure") == 0) {
        AttesaMisure *a = g_new0(AttesaMisure, 1);
        a->sh = sh;
        a->scadenza = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
        if (misure_tentativo(a) == G_SOURCE_REMOVE)
            g_free(a);
        else
            g_timeout_add_full(G_PRIORITY_DEFAULT, 50, misure_tentativo, a, g_free);
    } else if (strcmp(cosa, "controlli-verifica") == 0) {
        Verifica *v = g_new0(Verifica, 1); /* vive quanto il processo */
        v->radice = radice;
        v->scadenza = g_get_monotonic_time() + 10 * G_USEC_PER_SEC; /* include l'attesa del profilo */
        gtk_widget_add_tick_callback(radice, fotogramma, v, NULL);
    }
}
