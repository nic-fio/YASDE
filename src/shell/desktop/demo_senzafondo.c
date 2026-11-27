/* Solo per il demo (compilato in demo-desktop, mai nel prodotto): lo stile «Senza fondo» di orologio
 * e meteo, con i widget veri sul telefono finto.
 *   senzafondo          foto: un orologio e un meteo senza fondo, fermi (ora 16:08), a destra un
 *                       orologio digitale con vetro per confronto. Nessun controllo, nessuna uscita.
 *   senzafondo-attendi  se all'uscita la prova non e' arrivata in fondo, il processo esce con errore
 *   senzafondo-avvia    la prova: una sequenza sola, ogni attesa a condizione con scadenza (mai una
 *                       pausa fissa):
 *     1. orologio e meteo senza fondo: niente vetro registrato (ne' chiaro ne' scuro), classe del
 *        loro stile, testo bianco, ora grande con la data sotto; il meteo con luogo, condizione,
 *        temperatura e l'attribuzione dei dati, senza curva ne' giorni; l'orologio digitale di
 *        confronto ha il suo vetro e nessuna classe «senza fondo»;
 *     2. la maniglia (frecce) ridimensiona anche loro e il vetro continua a mancare;
 *     3. il menu del clic destro offre lo stile come ultima voce; scegliendo un altro stile il vetro
 *        torna (e la misura si tiene), scegliendo di nuovo «Senza fondo» sparisce; lo stesso per il
 *        meteo cambiato a codice. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "interno.h"
#include "logica_meteo.h"
#include "shell/finto/finto.h"
#include "shell/servizi_backend.h"

void ph_demo_senzafondo(PhShell *sh, const char *cosa);

#define ORA_FISSA (16 * 3600 + 8 * 60) /* 16:08 */
/* Le attese (profilo 3 s, dati del meteo 4 s) sommano meno dei 10 s della prova: se scade, lo dice lei. */
#define SCADENZA_US (4 * G_USEC_PER_SEC)
#define FALLISCI(...) g_error("senzafondo: " __VA_ARGS__)
#define VERIFICA(cond, msg)                                                                  \
    do {                                                                                     \
        if (!(cond))                                                                         \
            FALLISCI("%s", msg);                                                             \
    } while (0)

static bool sequenza_finita;

static void esci_senza_prova(void)
{
    if (!sequenza_finita) {
        fprintf(stderr, "senzafondo: la prova non e' arrivata in fondo\n");
        _exit(1);
    }
}

typedef struct {
    GtkWidget *radice;
    PhDesktop *d;
    bool solo_foto;
    int fase;
    gint64 scadenza; /* della fase in corso */
} Prova;

/* Il primo discendente con la classe CSS data (NULL se non c'e'). */
static GtkWidget *trova(GtkWidget *w, const char *classe)
{
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (gtk_widget_has_css_class(c, classe))
            return c;
        GtkWidget *r = trova(c, classe);
        if (r != NULL)
            return r;
    }
    return NULL;
}

/* Il n-esimo (in ordine di profondita') discendente con la classe CSS data, per il menu. */
static void raccogli(GtkWidget *w, const char *classe, GtkWidget **out, int max, int *n)
{
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (gtk_widget_has_css_class(c, classe) && *n < max)
            out[(*n)++] = c;
        raccogli(c, classe, out, max, n);
    }
}

static const char *testo_di(GtkWidget *w)
{
    return w != NULL && GTK_IS_LABEL(w) ? gtk_label_get_text(GTK_LABEL(w)) : "";
}

/* Il widget n-esimo del desktop (nell'ordine del profilo). */
static PhDskWidget *widget_n(Prova *v, guint n)
{
    VERIFICA(n < v->d->widget->len, "widget del desktop mancante");
    return g_ptr_array_index(v->d->widget, n);
}

static bool ha_vetro(const PhDskWidget *w)
{
    return gtk_widget_has_css_class(w->scheda, "ph-vetro-scuro") || gtk_widget_has_css_class(w->scheda, "ph-vetro-chiaro");
}

static bool senza_fondo(const PhDskWidget *w)
{
    return gtk_widget_has_css_class(w->scheda, "ph-desktop-senza-fondo");
}

/* Il profilo: orologio senza fondo, meteo senza fondo, orologio digitale con vetro (confronto). */
static void imposta(Prova *v, PhCfgTelefono *p)
{
    memset(p->widget, 0, sizeof p->widget);
    p->widget[0] = (PhCfgWidget){.tipo = PH_CFG_WIDGET_OROLOGIO, .stile = PH_DSK_OROLOGIO_SENZA_FONDO, .destra = 120,
                                 .alto = 16, .larghezza = 250, .altezza = 128, .dimensione = PH_CFG_WIDGET_MEDIO};
    p->widget[1] = (PhCfgWidget){.tipo = PH_CFG_WIDGET_METEO, .stile = PH_DSK_METEO_SENZA_FONDO, .destra = 120,
                                 .alto = 168, .larghezza = 320, .altezza = 180, .dimensione = PH_CFG_WIDGET_GRANDE};
    p->widget[2] = (PhCfgWidget){.tipo = PH_CFG_WIDGET_OROLOGIO, .stile = PH_DSK_OROLOGIO_DIGITALE, .destra = 480,
                                 .alto = 16, .larghezza = 250, .altezza = 100, .dimensione = PH_CFG_WIDGET_MEDIO};
    p->n_widget = 3;
    p->meteo_luogo_scelto = true; /* niente invito a scegliere la localita' */
    ph_dsk_widget_ricostruisci(v->d);
}

/* Premi una freccia sulla maniglia VERA (come da tastiera): stessa strada del trascinamento. */
static void maniglia(Prova *v, guint n, guint tasto, int volte)
{
    for (int i = 0; i < volte; i++) {
        PhDskWidget *w = widget_n(v, n); /* a ogni passo: puo' essere ricostruito */
        GListModel *m = gtk_widget_observe_controllers(w->maniglia);
        GObject *k = NULL;
        for (guint j = 0; j < g_list_model_get_n_items(m) && k == NULL; j++) {
            GObject *c = g_list_model_get_item(m, j);
            if (GTK_IS_EVENT_CONTROLLER_KEY(c))
                k = c;
            g_object_unref(c);
        }
        g_object_unref(m);
        VERIFICA(k != NULL, "la maniglia non ha il controllo dei tasti");
        gboolean preso = FALSE;
        g_signal_emit_by_name(k, "key-pressed", tasto, 0u, (GdkModifierType)0, &preso);
        VERIFICA(preso, "la maniglia non ha preso la freccia");
    }
}

/* Le voci del menu aperto (pulsanti «ph-riga») in ordine. */
static int voci_menu(Prova *v, GtkWidget **out, int max)
{
    int n = 0;
    raccogli(v->d->menu_voci, "ph-riga", out, max, &n);
    return n;
}

static void controlla_orologio_senza_fondo(PhDskWidget *w, const char *dove)
{
    VERIFICA(w->tipo == PH_CFG_WIDGET_OROLOGIO && w->stile == PH_DSK_OROLOGIO_SENZA_FONDO && w->senza_fondo, dove);
    VERIFICA(!ha_vetro(w), "l'orologio senza fondo ha un vetro registrato");
    VERIFICA(senza_fondo(w), "l'orologio senza fondo non ha la classe del suo stile");
    /* L'ora grande e la data sotto, come il digitale ma senza mattonella. */
    VERIFICA(strcmp(testo_di(w->testa), "16:08") == 0 && gtk_widget_has_css_class(w->testa, "ph-desktop-w-grande"),
             "l'orologio senza fondo non mostra l'ora grande");
    VERIFICA(gtk_widget_get_visible(w->sotto) && testo_di(w->sotto)[0] != '\0', "l'orologio senza fondo non mostra la data");
    /* Cio' che fa comparire (o no) il fondo e' il CSS del suo stile: se non si applica, l'ora resta a 34 px
     * (qui 48 px, quindi un'etichetta piu' alta di 50) e l'ombra della scheda torna. */
    int minimo = 0;
    gtk_widget_measure(w->testa, GTK_ORIENTATION_VERTICAL, -1, &minimo, NULL, NULL, NULL); /* non dipende dall'assegnazione */
    VERIFICA(minimo >= 50, "il CSS senza fondo non si applica all'orologio (ora non a 48 px)");
    GdkRGBA c;
    gtk_widget_get_color(w->testa, &c);
    VERIFICA(c.red > 0.99f && c.green > 0.99f && c.blue > 0.99f, "il testo senza fondo non e' bianco");
}

static void controlla_meteo_senza_fondo(PhDskWidget *w)
{
    VERIFICA(w->tipo == PH_CFG_WIDGET_METEO && w->stile == PH_DSK_METEO_SENZA_FONDO && w->senza_fondo,
             "il meteo senza fondo non ha lo stile giusto");
    VERIFICA(!ha_vetro(w), "il meteo senza fondo ha un vetro registrato");
    VERIFICA(senza_fondo(w), "il meteo senza fondo non ha la classe del suo stile");
    GtkWidget *luogo = trova(w->scheda, "ph-desktop-meteo-luogo");
    VERIFICA(luogo != NULL && testo_di(luogo)[0] != '\0', "il meteo senza fondo non mostra il luogo");
    GtkWidget *cond = trova(w->scheda, "ph-desktop-w-sotto-nom");
    VERIFICA(cond != NULL && testo_di(cond)[0] != '\0', "il meteo senza fondo non mostra la condizione");
    VERIFICA(trova(w->scheda, "ph-desktop-meteo-t-m") != NULL || trova(w->scheda, "ph-desktop-meteo-t-g") != NULL,
             "il meteo senza fondo non mostra la temperatura");
    VERIFICA(trova(w->scheda, "ph-desktop-meteo-curva") == NULL && trova(w->scheda, "ph-desktop-meteo-giorni") == NULL,
             "il meteo senza fondo mostra la curva o i giorni");
    VERIFICA(trova(w->scheda, "ph-desktop-meteo-mm-riga") == NULL, "il meteo senza fondo mostra massima e minima");
    /* L'attribuzione dei dati resta visibile, come negli altri stili. */
    GtkWidget *att = trova(w->scheda, "ph-desktop-meteo-attrib");
    VERIFICA(att != NULL && gtk_widget_is_visible(att), "attribuzione dei dati non visibile");
    GtkWidget *att_testo = trova(att, "ph-desktop-meteo-attrib-testo");
    VERIFICA(att_testo != NULL && strstr(testo_di(att_testo), "Open-Meteo.com") != NULL, "testo dell'attribuzione sbagliato");
    GdkRGBA c;
    gtk_widget_get_color(luogo, &c);
    VERIFICA(c.red > 0.99f && c.green > 0.99f && c.blue > 0.99f, "il testo del meteo senza fondo non e' bianco");
}

static void controlla_con_vetro(PhDskWidget *w, const char *dove)
{
    VERIFICA(ha_vetro(w), dove);
    VERIFICA(gtk_widget_has_css_class(w->scheda, "ph-vetro-scuro") && !senza_fondo(w) && !w->senza_fondo, dove);
}

static gboolean passo(gpointer dati)
{
    Prova *v = dati;
    PhCfgTelefono *p = ph_dsk_profilo(v->d);
    if (v->fase == 0) { /* aspetta il profilo del telefono finto */
        if (p == NULL) {
            VERIFICA(g_get_monotonic_time() <= v->scadenza, "scaduto: il profilo non e' arrivato");
            return G_SOURCE_CONTINUE;
        }
        ph_dsk_orologio_ora_fissa(ORA_FISSA);
        imposta(v, p);
        if (v->solo_foto)
            return G_SOURCE_REMOVE;
        v->fase = 1;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    /* 1. aspetta i dati del meteo (compare il luogo), poi controlla i tre widget */
    if (v->fase == 1) {
        if (trova(widget_n(v, 1)->scheda, "ph-desktop-meteo-luogo") == NULL) {
            VERIFICA(g_get_monotonic_time() <= v->scadenza, "scaduto: i dati del meteo non sono arrivati");
            return G_SOURCE_CONTINUE;
        }
        controlla_orologio_senza_fondo(widget_n(v, 0), "orologio senza fondo");
        controlla_meteo_senza_fondo(widget_n(v, 1));
        controlla_con_vetro(widget_n(v, 2), "l'orologio digitale di confronto non ha il suo vetro");
        v->fase = 2;
        return G_SOURCE_CONTINUE;
    }
    /* 2. la maniglia: ridimensionano e il vetro continua a mancare */
    if (v->fase == 2) {
        PhDskWidget *o = widget_n(v, 0), *m = widget_n(v, 1);
        int lo = o->larghezza, ao = o->altezza, lm = m->larghezza, am = m->altezza;
        maniglia(v, 0, GDK_KEY_Right, 2);
        maniglia(v, 0, GDK_KEY_Down, 1);
        maniglia(v, 1, GDK_KEY_Right, 5);
        maniglia(v, 1, GDK_KEY_Down, 2);
        o = widget_n(v, 0);
        m = widget_n(v, 1);
        VERIFICA(o->larghezza == lo + 2 * PH_DSK_PASSO && o->altezza == ao + PH_DSK_PASSO, "la maniglia non ha ridimensionato l'orologio");
        VERIFICA(m->larghezza == lm + 5 * PH_DSK_PASSO && m->altezza == am + 2 * PH_DSK_PASSO, "la maniglia non ha ridimensionato il meteo");
        VERIFICA(!ha_vetro(o) && !ha_vetro(m) && senza_fondo(o) && senza_fondo(m), "dopo il ridimensionamento e' tornato un vetro");
        /* Il contenuto segue la misura: il meteo piu' largo non ha ancora curva ne' giorni. */
        controlla_meteo_senza_fondo(m);
        v->fase = 3;
        return G_SOURCE_CONTINUE;
    }
    /* 3. il menu del clic destro: lo stile e' l'ultima voce; un altro stile rimette il vetro */
    if (v->fase == 3) {
        PhDskWidget *o = widget_n(v, 0);
        PhDskMisura prima = {o->larghezza, o->altezza};
        ph_dsk_menu_widget(v->d, o->id, 700, 60);
        GtkWidget *voci[32];
        int n = voci_menu(v, voci, 32);
        VERIFICA(n == (int)ph_dsk_stili_n(PH_CFG_WIDGET_OROLOGIO) + 1, "il menu dell'orologio non ha una voce per stile piu' «Rimuovi»");
        const char *nome = ph_dsk_stile_nome_di(PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_SENZA_FONDO);
        VERIFICA(g_str_has_prefix(testo_di(gtk_button_get_child(GTK_BUTTON(voci[PH_DSK_OROLOGIO_SENZA_FONDO]))), nome),
                 "l'ultima voce di stile non e' «Senza fondo»");
        g_signal_emit_by_name(voci[PH_DSK_OROLOGIO_DIGITALE], "clicked"); /* sceglie il digitale */
        o = widget_n(v, 0);
        VERIFICA(o->stile == PH_DSK_OROLOGIO_DIGITALE, "il menu non ha cambiato lo stile");
        controlla_con_vetro(o, "passando a un altro stile il vetro non e' tornato");
        VERIFICA(o->larghezza == prima.larghezza && o->altezza == prima.altezza, "passando a un altro stile la misura non e' stata tenuta");
        /* ...e di nuovo senza fondo dal menu: il vetro sparisce e la misura si tiene. */
        ph_dsk_menu_widget(v->d, o->id, 700, 60);
        n = voci_menu(v, voci, 32);
        VERIFICA(n == (int)ph_dsk_stili_n(PH_CFG_WIDGET_OROLOGIO) + 1, "il menu dell'orologio e' cambiato");
        g_signal_emit_by_name(voci[PH_DSK_OROLOGIO_SENZA_FONDO], "clicked");
        o = widget_n(v, 0);
        controlla_orologio_senza_fondo(o, "orologio tornato senza fondo");
        VERIFICA(o->larghezza == prima.larghezza && o->altezza == prima.altezza, "tornando senza fondo la misura non e' stata tenuta");
        v->fase = 4;
        return G_SOURCE_CONTINUE;
    }
    /* 4. il meteo: stile cambiato a codice, dal e verso «Senza fondo», con gli stessi controlli. Il
     * profilo scritto dopo il cambio ha lo stile e la misura giusti. */
    if (v->fase == 4) {
        PhDskWidget *m = widget_n(v, 1);
        PhDskMisura prima = {m->larghezza, m->altezza};
        ph_dsk_widget_stile_imposta(v->d, m->id, PH_DSK_METEO_COMPATTO, 0);
        m = widget_n(v, 1);
        VERIFICA(m->stile == PH_DSK_METEO_COMPATTO, "il meteo non ha cambiato stile");
        controlla_con_vetro(m, "il meteo, cambiato stile, non ha riavuto il vetro");
        VERIFICA(m->larghezza == prima.larghezza && m->altezza == prima.altezza, "il meteo ha perso la misura cambiando stile");
        VERIFICA(p != NULL && p->widget[1].stile == PH_DSK_METEO_COMPATTO && p->widget[1].larghezza == prima.larghezza, "profilo del meteo non aggiornato");
        ph_dsk_widget_stile_imposta(v->d, m->id, PH_DSK_METEO_SENZA_FONDO, 0);
        m = widget_n(v, 1);
        VERIFICA(!ha_vetro(m) && senza_fondo(m), "il meteo tornato senza fondo ha un vetro");
        VERIFICA(p->widget[1].stile == PH_DSK_METEO_SENZA_FONDO && m->larghezza == prima.larghezza && m->altezza == prima.altezza,
                 "il meteo tornato senza fondo ha perso misura o stile");
        controlla_meteo_senza_fondo(m);
        sequenza_finita = true;
        g_message("senzafondo: niente vetro ne' fondo, testo bianco, ora e data, meteo con attribuzione, maniglia, menu, "
                  "vetro che torna cambiando stile e misura tenuta");
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_REMOVE;
}

void ph_demo_senzafondo(PhShell *sh, const char *cosa)
{
    GtkWidget *radice = ph_shell_radice(sh);
    if (strcmp(cosa, "senzafondo-attendi") == 0) {
        atexit(esci_senza_prova);
        return;
    }
    if (strcmp(cosa, "senzafondo") != 0 && strcmp(cosa, "senzafondo-avvia") != 0)
        return;
    PhDesktop *d = g_object_get_data(G_OBJECT(radice), "ph-desktop");
    if (d == NULL)
        FALLISCI("desktop non trovato");
    Prova *v = g_new0(Prova, 1); /* vive quanto il processo */
    v->radice = radice;
    v->d = d;
    v->solo_foto = strcmp(cosa, "senzafondo") == 0;
    v->scadenza = g_get_monotonic_time() + 3 * G_USEC_PER_SEC; /* attesa del profilo */
    /* Un controllo ogni 10 ms e non a ogni fotogramma (su uno schermo invisibile sono radi). */
    g_timeout_add(10, passo, v);
}
