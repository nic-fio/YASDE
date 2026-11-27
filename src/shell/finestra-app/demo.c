/* demo-finestra-app: il solo pezzo finestra-app sopra il telefono finto. All'arrivo
 * dell'elenco delle app apre "Note" e "Messaggi" (come il mockup).
 *
 * Opzioni (tolte dalla riga di comando prima di passarla al demo comune):
 *   --scena             sequenza a tempo per le fotografie (schermo senza input,
 *                       strumenti/foto-demo.sh finestra-app "2 4 6 8"): Note,
 *                       Messaggi (solo verticale) e Browser; dopo ~1 s Note
 *                       agganciata a sinistra (Messaggi, solo verticale, non cambia
 *                       dimensioni: il suo pulsante di ingrandimento e' disattivato); dopo ~2 s REC su Note (registrazione
 *                       simulata dal finto); dopo ~4 s il menu dei tre punti di
 *                       Messaggi (voce Ruota disattivata); dopo ~6 s REC si spegne
 *                       e il Browser si ingrandisce. Foto: "3 5 8".
 *   --compat            prova della voce «Compatibilita'» del menu (CONTRATTI.md §19): spunta
 *                       e toglie la voce e controlla misura e densita' chieste al finto
 *                       (esce con 1 se qualcosa non torna; si usa con --prova MS).
 *   --mouse             prova del tasto destro e centrale nel contenuto delle finestre (stessi gesti
 *                       del mouse, senza mouse: registro dei tocchi mandati alla superficie;
 *                       esce con 1 se qualcosa non torna; si usa con --prova MS).
 *   --vetro             prove del vetro (sfocatura dietro il titolo, fase 2): modalita',
 *                       albero dei nodi, paintable tenuti, chiusure, ridisegni a riposo
 *                       (esce con 1 se qualcosa non torna; si usa con --prova MS).
 *   --vetro-menu        prova del menu «altri comandi» come superficie di vetro chiaro con la sfocatura della
 *                       shell ACCESA (YASDE_SFOCATURA=1 fin dall'avvio, anche con il renderer di Broadway):
 *                       il blur del menu c'e' e non ne contiene altri (si usa con --prova MS).
 *   --vetro-diag        come --vetro, piu' una spia che scrive su stderr chi si ridisegna dopo la pausa (ritratti
 *                       e figli propri delle cornici, chiamate di sincronizza): serve a spiegare i disegni
 *                       dopo una pausa, non fa parte delle prove.
 *   --tasti-al-telefono i tasti funzione vanno TUTTI al telefono (come nel
 *                       prodotto). Senza l'opzione F2-F4, F9 e F5-F8 sono
 *                       comandi del demo (al posto della barra e dell'ossatura):
 *   F2  minimizza la finestra attiva (clic sulla sua voce della barra)
 *   F3  ripristina la prima finestra minimizzata
 *   F4  finestra host "non visibile" / di nuovo visibile (4.14)
 *   F5-F8  comandi del telefono finto (blocco, caduta, notifica, app chiusa)
 *   F9  apre l'app successiva dell'elenco */
#include <math.h>
#include <stdio.h>

#include <string.h>

#include "finestra_app_demo.h"
#include "finestre/finestre.h"
#include "shell/comune/interno.h" /* solo per ph_vetro_radice_snapshot, come fa la prova di comune */
#include "shell/finto/finto.h"
#include "shell/pezzi.h"

#define DEMO_FINESTRE_MAX 64

/* Esito della prova --compat: 1 se qualche controllo e' fallito. */
static int g_fallito;

static void controllo(bool ok, const char *che)
{
    if (!ok) {
        fprintf(stderr, "demo: CONTROLLO FALLITO: %s\n", che);
        g_fallito = 1;
    }
}

static gboolean passo_compat(gpointer u);
static gboolean passo_vetro(gpointer u);
static gboolean passo_mouse(gpointer u);

typedef struct {
    int w, h, dens;
    bool in_fin, spunta, in_profilo;
    unsigned salvataggi;
    unsigned avvisi;
} Stato;

typedef struct {
    PhShell *sh;
    PhFinto *finto;
    guint id;
    size_t prossima;
    bool host_visibile;
    bool bloccato;
    bool scena, tasti_al_telefono, compat, vetro, spia, menu_mode, mouse;
    Stato s0, s1;       /* stati di riferimento della prova --compat */
    unsigned avvisi;    /* avvisi a comparsa ricevuti (gestore finto) */
    gint64 attesa_fino; /* scadenza (us) dell'attesa dell'effetto di un'azione, 0 = non in attesa */
    unsigned passo;
    /* Stato delle prove --vetro */
    GPtrArray *tenuti;  /* paintable tenuti con un riferimento in piu' (GdkPaintable *) */
    unsigned conta0, fase, n_on;
    unsigned frames;    /* fotogrammi contati dal tick sulla radice (--vetro) */
    unsigned quiete;     /* fotogrammi consecutivi senza disegni di cornici */
    unsigned ultimo_disegno; /* numero del fotogramma dell'ultimo disegno di una cornice */
    unsigned fr0;        /* fotogrammi contati quando e' partita l'azione del passo */
    unsigned morti;      /* ritratti di widget morti visti subito dopo una chiusura */
    unsigned inval_fig, sync_prima;
    char inval_fig_tipo[96];
    unsigned inval;      /* invalidate-contents visti dalla spia dall'ultimo fotogramma */
    char inval_tipo[96];
    bool diag_attivo;
    unsigned snap_prima; /* ultimo valore del contatore dei disegni visto dal tick */
    gint64 t0;       /* ora (us) dell'ultima azione di un passo */
    gint64 t1;       /* uso interno dei passi con attese */
    bool esito_a, esito_b;
    gpointer dati_gestore; /* la cornice dato del gestore invalidate-contents (solo confronto di puntatori) */
    gpointer cornice_viva; /* cornice di una finestra chiusa, azzerata quando e' finalizzata */
} Demo;

/* Un passo della sequenza a tempo. */
static const struct {
    unsigned ms;               /* tempo dal passo precedente */
    const char *azione;        /* NULL = chiudi il popup aperto */
    unsigned indice;
} SCENA[] = {
    {1000, "aggancia-sinistra", 0},   /* Messaggi (solo verticale) resta com'e' */
    {0, "specchio", 0},     /* finestra dello schermo del telefono (4.7), alla misura nuova */
    {1000, "registra", 0},  /* REC sulla finestra agganciata (agganciare DOPO sarebbe rifiutato) */
    {2000, "menu", 1},
    {2000, NULL, 0},
    {0, "registra", 0},     /* ferma: REC si spegne */
    {0, "sotto", 1},        /* Messaggi a cavallo del bordo inferiore: tagliata, barra intatta */
    {0, "massimizza", 2},
    {0, "zoom", 2},         /* Ctrl + rotella simulata: 3 passi, FINE dopo 250 ms */
    {1000, "pizzichi", 0},  /* scrive quanti passi ha accettato il telefono finto */
    {1000, "chiudi", 2},    /* "Chiudi app" della barra: ph_shell_chiudi_finestra */
};

static gboolean passo_scena(gpointer u)
{
    Demo *d = u;
    if (d->passo >= G_N_ELEMENTS(SCENA))
        return G_SOURCE_REMOVE;
    do {
        if (SCENA[d->passo].azione == NULL) {
            ph_shell_popup_chiudi(d->sh);
        } else if (strcmp(SCENA[d->passo].azione, "pizzichi") == 0) {
            fprintf(stderr, "demo: passi di pizzico accettati dal finto: %u\n", ph_finto_pizzichi(d->finto));
        } else if (strcmp(SCENA[d->passo].azione, "specchio") == 0) {
            ph_shell_specchio(d->sh, true);
        } else if (strcmp(SCENA[d->passo].azione, "chiudi") == 0) {
            PhShellFinestra f[DEMO_FINESTRE_MAX];
            size_t n = ph_shell_finestre(d->sh, f, DEMO_FINESTRE_MAX);
            if (SCENA[d->passo].indice < n)
                ph_shell_chiudi_finestra(d->sh, f[SCENA[d->passo].indice].id);
        } else if (!ph_finestra_app_demo_azione(d->sh, SCENA[d->passo].azione, SCENA[d->passo].indice))
            fprintf(stderr, "demo: azione %s %u rifiutata\n", SCENA[d->passo].azione,
                    SCENA[d->passo].indice);
        d->passo++;
    } while (d->passo < G_N_ELEMENTS(SCENA) && SCENA[d->passo].ms == 0);
    if (d->passo < G_N_ELEMENTS(SCENA))
        g_timeout_add(SCENA[d->passo].ms, passo_scena, d);
    return G_SOURCE_REMOVE;
}

/* --compat: prova con schermo della voce "Compatibilita'" (CONTRATTI.md §19), contro il
 * telefono finto (densita' del finto: 420 dpi). Ogni passo AGISCE una volta e poi
 * VERIFICA con attesa a scadenza (2 s: l'effetto di un clic esce dal ciclo dei
 * fotogrammi e sotto carico puo' tardare). Esce con 1 se qualcosa non torna. */
static Stato leggi_stato(Demo *d)
{
    Stato st = {0};
    if (!ph_finestra_app_demo_richiesta(d->sh, 0, &st.w, &st.h, &st.dens))
        controllo(false, "finestra 0 presente");
    ph_finestra_app_demo_stato(d->sh, 0, &st.in_fin, &st.spunta, &st.salvataggi);
    const PhCfgTelefono *pf = ph_srv_profilo(ph_shell_servizi(d->sh));
    st.in_profilo = pf != NULL && ph_cfg_telefono_in_compatibilita(pf, "it.finto.note");
    st.avvisi = d->avvisi;
    return st;
}

static PhCfgTelefono *profilo(Demo *d) { return ph_srv_profilo(ph_shell_servizi(d->sh)); }

/* Riempie o svuota l'elenco con app finte (solo in memoria, senza salvare). */
static void riempi_elenco(Demo *d, bool pieno)
{
    char nome[64];
    for (int k = 0; k < PH_CFG_MAX_COMPAT; k++) {
        snprintf(nome, sizeof nome, "it.finto.riempi%d", k);
        ph_cfg_telefono_imposta_compatibilita(profilo(d), nome, pieno, NULL);
    }
}

static void a_normale(Demo *d) { (void)d; }
static bool v_normale(Demo *d, const Stato *s, const char **che)
{
    d->s0 = *s;
    *che = "regola automatica, nessuna compatibilita'";
    return s->dens > 0 && s->dens < 420 && !s->in_fin && !s->in_profilo;
}

/* Geometria della cornice: i figli non sono spostati (niente border CSS sulla cornice). */
static void a_geometria(Demo *d) { (void)d; }
static bool v_geometria(Demo *d, const Stato *s, const char **che)
{
    (void)s;
    *che = "geometria: pulsanti e contenuto esattamente dove dicono costanti e finestre.h";
    return ph_finestra_app_demo_geometria(d->sh, 0);
}

static void a_attiva(Demo *d) { ph_finestra_app_demo_azione(d->sh, "compat-si", 0); }
static bool v_attiva(Demo *d, const Stato *s, const char **che)
{
    *che = "attiva: densita' 420, flusso piu' grande, voce nel profilo e in finestre";
    return s->dens == 420 && s->w > d->s0.w && s->in_profilo && s->in_fin && s->spunta;
}
static void a_spegni(Demo *d) { ph_finestra_app_demo_azione(d->sh, "compat-no", 0); }
static bool v_spenta(Demo *d, const Stato *s, const char **che)
{
    *che = "spenta: densita' e misura di prima, voce tolta";
    return s->dens == d->s0.dens && s->w == d->s0.w && !s->in_profilo && !s->in_fin && !s->spunta;
}

/* Rifiuto 1: elenco pieno nel profilo -> la spunta torna com'era, compare l'avviso, nulla cambia. */
static void a_elenco_pieno(Demo *d)
{
    riempi_elenco(d, true);
    d->s1 = leggi_stato(d);
    ph_finestra_app_demo_azione(d->sh, "compat-si", 0);
}
static bool v_elenco_pieno(Demo *d, const Stato *s, const char **che)
{
    *che = "elenco pieno: spunta com'era, un avviso, nessun salvataggio, nulla cambia";
    return !s->spunta && !s->in_fin && !s->in_profilo && s->avvisi == d->s1.avvisi + 1 &&
           s->salvataggi == d->s1.salvataggi && s->dens == d->s1.dens && s->w == d->s1.w;
}
static void a_elenco_svuota(Demo *d) { riempi_elenco(d, false); }

/* Rifiuto 2: finestre.h rifiuta (registrazione in corso) -> profilo annullato e salvato di nuovo,
 * la spunta torna. */
static void a_registra(Demo *d)
{
    ph_finestra_app_demo_azione(d->sh, "registra", 0);
    d->s1 = leggi_stato(d);
    ph_finestra_app_demo_azione(d->sh, "compat-si", 0);
}
static bool v_rifiutata(Demo *d, const Stato *s, const char **che)
{
    *che = "rifiutata da finestre.h: profilo com'era e salvato due volte, spunta com'era";
    return !s->spunta && !s->in_fin && !s->in_profilo && s->salvataggi == d->s1.salvataggi + 2 &&
           s->dens == d->s1.dens && s->w == d->s1.w;
}
static void a_ferma_registra(Demo *d) { ph_finestra_app_demo_azione(d->sh, "registra", 0); }

/* Cambio di profilo: la finestra aperta rilegge l'elenco del profilo NUOVO e allinea finestre.h,
 * senza salvare. */
static void a_profilo_con_voce(Demo *d)
{
    d->s1 = leggi_stato(d);
    ph_cfg_telefono_imposta_compatibilita(profilo(d), "it.finto.note", true, NULL);
    ph_finestra_app_demo_profilo_ricaricato(d->sh);
}
static bool v_profilo_con_voce(Demo *d, const Stato *s, const char **che)
{
    *che = "profilo nuovo con la voce: finestre.h allineato, densita' 420, nessun salvataggio";
    return s->in_fin && s->dens == 420 && s->w > d->s0.w && s->salvataggi == d->s1.salvataggi;
}
static void a_profilo_senza_voce(Demo *d)
{
    ph_cfg_telefono_imposta_compatibilita(profilo(d), "it.finto.note", false, NULL);
    ph_finestra_app_demo_profilo_ricaricato(d->sh);
}

static const struct {
    void (*agisci)(Demo *);
    bool (*verifica)(Demo *, const Stato *, const char **);
} PASSI_COMPAT[] = {
    {a_normale, v_normale},
    {a_geometria, v_geometria},
    {a_attiva, v_attiva},
    {a_spegni, v_spenta},
    {a_elenco_pieno, v_elenco_pieno},
    {a_elenco_svuota, v_spenta},
    {a_registra, v_rifiutata},
    {a_ferma_registra, v_spenta},
    {a_profilo_con_voce, v_profilo_con_voce},
    {a_profilo_senza_voce, v_spenta},
};

static gboolean passo_compat(gpointer u)
{
    Demo *d = u;
    if (d->passo >= G_N_ELEMENTS(PASSI_COMPAT)) {
        fprintf(stderr, "demo: prova compatibilita' %s\n", g_fallito ? "FALLITA" : "riuscita");
        return G_SOURCE_REMOVE;
    }
    gint64 ora_us = g_get_monotonic_time();
    if (d->attesa_fino == 0) { /* appena arrivati al passo: agisce e si mette in attesa */
        PASSI_COMPAT[d->passo].agisci(d);
        d->attesa_fino = ora_us + 2 * G_USEC_PER_SEC;
        g_timeout_add(100, passo_compat, d);
        return G_SOURCE_REMOVE;
    }
    Stato st = leggi_stato(d);
    const char *che = "";
    bool fatto = PASSI_COMPAT[d->passo].verifica(d, &st, &che);
    if (!fatto && ora_us < d->attesa_fino) {
        g_timeout_add(100, passo_compat, d);
        return G_SOURCE_REMOVE;
    }
    controllo(fatto, che);
    d->attesa_fino = 0;
    d->passo++;
    g_timeout_add(50, passo_compat, d);
    return G_SOURCE_REMOVE;
}


/* ======================================================================
 * --vetro: prove del vetro (finestra_app.c, blocco "Vetro"). Ogni passo AGISCE una volta e poi
 * VERIFICA; la verifica torna 1 = a posto, 0 = non ancora, -1 = fallito subito. Si aspettano SEGNALI
 * (una posizione reale, un disegno che avviene, fotogrammi del frame clock), mai tempi fissi; ogni
 * passo ha solo una scadenza (SCAD_S) oltre la quale e' fallito.
 *
 * I fotogrammi: una tick callback sulla radice tiene acceso il frame clock e li conta (senza, "nessun
 * disegno" sarebbe vero banalmente). Nel backend Broadway di prova il frame clock a riposo va a circa
 * un fotogramma al secondo: per questo le attese sono in fotogrammi e le scadenze larghe.
 *
 * Limiti noti, dichiarati:
 *  - un ciclo lento guidato da un timer con periodo oltre QUIETE_K + RIPOSO_F fotogrammi non verrebbe visto
 *    (nel codice del vetro non ci sono timer);
 *  - senza prova: le due guardie (`dove > *usati` in vetro_metti, `c->dietro == NULL` in cornice_snapshot),
 *    `vetro_catena == 0` in cornice_realize, il ramo child_visible di vetro_metti (child_visible falso implica
 *    widget non mappato), lo strato lontano sotto la radice (in questa shell non ce n'e'), e un renderer su
 *    scheda grafica vero (si prova solo la funzione pura vetro_per_renderer). */
#define CASC 4        /* finestre in cascata */
#define QUIETE_K 2    /* fotogrammi consecutivi senza disegni di cornici = quiete */
#define RIPOSO_F 3    /* fotogrammi a riposo che non devono vedere nessun disegno */
#define SCAD_S 40     /* scadenza di un passo, in secondi */

static GtkWidget *cn(Demo *d, unsigned i) { return ph_finestra_app_demo_cornice(d->sh, i); }

static size_t n_fin(Demo *d)
{
    PhShellFinestra f[DEMO_FINESTRE_MAX];
    return ph_shell_finestre(d->sh, f, DEMO_FINESTRE_MAX);
}

static void chiudi_indice(Demo *d, unsigned i)
{
    PhShellFinestra f[DEMO_FINESTRE_MAX];
    size_t n = ph_shell_finestre(d->sh, f, DEMO_FINESTRE_MAX);
    if (i < n)
        ph_shell_chiudi_finestra(d->sh, f[i].id);
}

static bool pronta(Demo *d, unsigned i)
{
    GtkWidget *c = cn(d, i);
    return c != NULL && gtk_widget_get_realized(c) && gtk_widget_get_mapped(c);
}

static const char *renderer_di(GtkWidget *c)
{
    GtkNative *nat = gtk_widget_get_native(c);
    GskRenderer *r = nat != NULL ? gtk_native_get_renderer(nat) : NULL;
    return r != NULL ? G_OBJECT_TYPE_NAME(r) : "";
}

/* Almeno n fotogrammi dall'azione del passo: dopo un cambio di layout il disegno a mano dei figli
 * (il snapshot di prova) ha bisogno dell'allocazione nuova, che arriva con un fotogramma. */
static bool fotogrammi_da(Demo *d, unsigned n) { return d->frames - d->fr0 >= n; }

/* Quiete: nessun disegno di cornici da QUIETE_K fotogrammi e assestamento della finestra-app finito. */
static bool quieta(Demo *d) { return d->quiete >= QUIETE_K && ph_finestra_app_demo_assestato(d->sh); }

/* Spia (solo --vetro-diag): chi si ridisegna e quando. */
static void spia_dietro_cb(GdkPaintable *p, gpointer u)
{
    Demo *d = u;
    GtkWidget *w = gtk_widget_paintable_get_widget(GTK_WIDGET_PAINTABLE(p));
    if (w == NULL)
        return; /* widget in distruzione */
    d->inval++;
    snprintf(d->inval_tipo, sizeof d->inval_tipo, "%s %dx%d", G_OBJECT_TYPE_NAME(w), gtk_widget_get_width(w),
             gtk_widget_get_height(w));
}

static void spia_figlio_cb(GdkPaintable *p, gpointer u)
{
    Demo *d = u;
    GtkWidget *w = gtk_widget_paintable_get_widget(GTK_WIDGET_PAINTABLE(p));
    if (w == NULL)
        return;
    d->inval_fig++;
    snprintf(d->inval_fig_tipo, sizeof d->inval_fig_tipo, "%s %dx%d", G_OBJECT_TYPE_NAME(w), gtk_widget_get_width(w),
             gtk_widget_get_height(w));
}

static gboolean tick_fotogrammi(GtkWidget *w, GdkFrameClock *fc, gpointer u)
{
    (void)w;
    (void)fc;
    Demo *d = u;
    d->frames++;
    unsigned c = ph_finestra_app_demo_snapshot_conta(d->sh);
    if (c != d->snap_prima) {
        d->quiete = 0;
        d->ultimo_disegno = d->frames;
    } else {
        d->quiete++;
    }
    if (d->spia) {
        for (unsigned i = 0; i < 16; i++) {
            GdkPaintable *p[64];
            guint n = ph_finestra_app_demo_dietro(d->sh, i, p, 64);
            for (guint k = 0; k < n && k < 64; k++) {
                if (g_object_get_data(G_OBJECT(p[k]), "ph-spia") == NULL) {
                    g_object_set_data(G_OBJECT(p[k]), "ph-spia", GINT_TO_POINTER(1));
                    g_signal_connect(p[k], "invalidate-contents", G_CALLBACK(spia_dietro_cb), d);
                }
            }
        }
        /* anche i figli PROPRI di ogni cornice (non solo cio' che sta nel dietro di un'altra) */
        for (unsigned i = 0; i < 16 && cn(d, i) != NULL; i++) {
            for (GtkWidget *k = gtk_widget_get_first_child(cn(d, i)); k != NULL; k = gtk_widget_get_next_sibling(k)) {
                if (g_object_get_data(G_OBJECT(k), "ph-spia-fig") != NULL)
                    continue;
                g_object_set_data(G_OBJECT(k), "ph-spia-fig", GINT_TO_POINTER(1));
                GdkPaintable *sp = gtk_widget_paintable_new(k); /* della spia: resta (solo diagnosi) */
                g_signal_connect(sp, "invalidate-contents", G_CALLBACK(spia_figlio_cb), d);
            }
        }
        unsigned sy = ph_finestra_app_demo_sincronizza_conta(d->sh);
        if (d->diag_attivo && (c != d->snap_prima || d->inval > 0 || d->inval_fig > 0 || sy != d->sync_prima))
            fprintf(stderr,
                    "demo: [diag] fotogramma %u: disegni delle cornici +%u, invalidate dei ritratti %u (ultimo: %s), "
                    "dei figli propri %u (ultimo: %s), chiamate di sincronizza +%u\n",
                    d->frames, c - d->snap_prima, d->inval, d->inval_tipo, d->inval_fig, d->inval_fig_tipo,
                    sy - d->sync_prima);
        d->sync_prima = sy;
        d->inval = 0;
        d->inval_fig = 0;
    }
    d->snap_prima = c;
    return G_SOURCE_CONTINUE;
}

/* Quanti paintable di `dietro` di B ritraggono un widget che e' FIGLIO della cornice di A. */
static unsigned figli_di(Demo *d, unsigned b, unsigned a, unsigned *nulli, unsigned *cornici)
{
    GdkPaintable *p[64];
    guint n = ph_finestra_app_demo_dietro(d->sh, b, p, 64);
    unsigned k_a = 0, nu = 0, co = 0;
    for (guint k = 0; k < n && k < 64; k++) {
        GtkWidget *w = gtk_widget_paintable_get_widget(GTK_WIDGET_PAINTABLE(p[k]));
        if (w == NULL) {
            nu++;
            continue;
        }
        if (ph_finestra_app_demo_e_cornice(w))
            co++;
        if (a != (unsigned)-1 && gtk_widget_get_parent(w) == cn(d, a))
            k_a++;
    }
    if (nulli != NULL)
        *nulli = nu;
    if (cornici != NULL)
        *cornici = co;
    return k_a;
}

static guint sig_invalida(void) { return g_signal_lookup("invalidate-contents", GDK_TYPE_PAINTABLE); }
/* C'e' ancora il gestore della cornice `dati` (confronto fra puntatori: sicuro anche a cornice distrutta)?
 * Un gestore della spia (--vetro-diag) non conta. */
static bool ha_gestore(GdkPaintable *p, gpointer dati)
{
    return g_signal_handler_find(p, G_SIGNAL_MATCH_ID | G_SIGNAL_MATCH_DATA, sig_invalida(), 0, NULL, NULL, dati) != 0;
}

/* --- 1: modalita' alla realizzazione --- */
static void av_nulla(Demo *d) { (void)d; }

/* La scelta per nome del renderer (funzione pura del prodotto): elenco ammesso. */
static int vv_renderer_nomi(Demo *d, const char **che)
{
    (void)d;
    *che = "vetro_per_renderer: vero solo per GskGLRenderer, GskNglRenderer, GskVulkanRenderer";
    static const char *si[] = {"GskGLRenderer", "GskNglRenderer", "GskVulkanRenderer"};
    static const char *no[] = {"GskCairoRenderer", "GskBroadwayRenderer", "", "GskNuovoRenderer", "gskglrenderer", NULL};
    for (size_t k = 0; k < G_N_ELEMENTS(si); k++)
        if (!ph_finestra_app_demo_vetro_per_renderer(si[k]))
            return -1;
    for (size_t k = 0; k < G_N_ELEMENTS(no); k++)
        if (ph_finestra_app_demo_vetro_per_renderer(no[k]))
            return -1;
    return 1;
}

/* Senza variabile (tolta dal main prima di aprire qualunque finestra): Broadway non e' nell'elenco, il
 * vetro e' SPENTO e la classe CSS assente. */
static int vv_senza_var(Demo *d, const char **che)
{
    *che = "senza YASDE_SFOCATURA, con il renderer di Broadway: vetro spento e classe CSS assente";
    if (g_getenv("YASDE_SFOCATURA") != NULL)
        return -1; /* la prova parte con la variabile assente */
    if (!pronta(d, 0))
        return 0;
    bool att, cl;
    ph_finestra_app_demo_vetro(d->sh, 0, &att, &cl);
    const char *r = renderer_di(cn(d, 0));
    fprintf(stderr, "demo: renderer %s, vetro %s\n", r, att ? "acceso" : "spento");
    return !att && !cl && !ph_finestra_app_demo_vetro_per_renderer(r);
}

static void av_env0(Demo *d)
{
    g_setenv("YASDE_SFOCATURA", "0", TRUE);
    ph_shell_apri_app(d->sh, "it.finto.foto");
}
static int vv_env0(Demo *d, const char **che)
{
    *che = "YASDE_SFOCATURA=0: vetro spento e classe CSS assente";
    if (n_fin(d) < 3 || !pronta(d, 2))
        return 0;
    bool att, cl;
    ph_finestra_app_demo_vetro(d->sh, 2, &att, &cl);
    return !att && !cl;
}

static void av_env1(Demo *d)
{
    g_setenv("YASDE_SFOCATURA", "1", TRUE);
    ph_shell_apri_app(d->sh, "it.finto.browser");
}
static int vv_env1(Demo *d, const char **che)
{
    *che = "YASDE_SFOCATURA=1: vetro acceso e classe CSS presente (anche con il renderer di Broadway)";
    if (n_fin(d) < 4 || !pronta(d, 3))
        return 0;
    bool att, cl;
    ph_finestra_app_demo_vetro(d->sh, 3, &att, &cl);
    return att && cl;
}

/* --- Quattro finestre in cascata, tutte con il vetro (aperte una alla volta, per avere un ordine fisso) --- */
static void av_chiudi_prime(Demo *d)
{
    chiudi_indice(d, 1); /* Messaggi */
    chiudi_indice(d, 0); /* Notes */
}
static bool elenco_e(Demo *d, size_t n, const char *ultimo)
{
    PhShellFinestra f[DEMO_FINESTRE_MAX];
    if (ph_shell_finestre(d->sh, f, DEMO_FINESTRE_MAX) != n)
        return false;
    return strcmp(f[n - 1].pacchetto, ultimo) == 0 && pronta(d, (unsigned)n - 1);
}
static int vv_chiuse(Demo *d, const char **che)
{
    *che = "Notes e Messaggi chiuse: restano Photos e Browser";
    PhShellFinestra f[DEMO_FINESTRE_MAX];
    return ph_shell_finestre(d->sh, f, DEMO_FINESTRE_MAX) == 2 && strcmp(f[0].pacchetto, "it.finto.foto") == 0 &&
           strcmp(f[1].pacchetto, "it.finto.browser") == 0;
}
static void av_apri_mappe(Demo *d) { ph_shell_apri_app(d->sh, "it.finto.mappe"); }
static int vv_mappe(Demo *d, const char **che)
{
    *che = "Maps aperta";
    return elenco_e(d, 3, "it.finto.mappe");
}
static void av_apri_file(Demo *d) { ph_shell_apri_app(d->sh, "it.finto.file"); }
static int vv_file(Demo *d, const char **che)
{
    *che = "File aperta";
    return elenco_e(d, 4, "it.finto.file");
}

/* Ora: 0 Photos (senza vetro), 1 Browser, 2 Maps, 3 File, 4 Weather. */
static void posiziona_cascata(Demo *d)
{
    for (unsigned k = 1; k <= CASC; k++)
        if (!ph_finestra_app_demo_posiziona(d->sh, k, 40.0 + 40.0 * (k - 1), 40.0 + 40.0 * (k - 1)))
            controllo(false, "posiziona_cascata: il gesto e' stato rifiutato");
}
static void av_apri_meteo(Demo *d) { ph_shell_apri_app(d->sh, "it.finto.meteo"); }
static int vv_cascata(Demo *d, const char **che)
{
    *che = "cascata di quattro finestre con il vetro pronta";
    if (!elenco_e(d, 5, "it.finto.meteo"))
        return 0;
    for (unsigned k = 0; k < 5; k++)
        if (!pronta(d, k))
            return 0;
    posiziona_cascata(d);
    return 1;
}

/* Il layout ha dato alla cascata le posizioni volute (posizioni REALI dei widget, non un tempo). */
static bool cascata_a_posto(Demo *d)
{
    for (unsigned k = 1; k <= CASC; k++) {
        graphene_rect_t b;
        if (!gtk_widget_compute_bounds(cn(d, k), gtk_widget_get_parent(cn(d, k)), &b) ||
            fabsf(b.origin.x - (40.0f + 40.0f * (float)(k - 1))) > 0.5f)
            return false;
    }
    return true;
}

/* Flussi in pausa (host non visibile): il telefono finto non manda piu' fotogrammi sotto i titoli; poi si
 * aspetta la QUIETE (segnale). Le immagini gia' in volo al momento della pausa arrivano dopo: sono un
 * ridisegno legittimo (vedi --vetro-diag), non del vetro. */
static void av_pausa(Demo *d)
{
    d->diag_attivo = true;
    ph_shell_imposta_host(d->sh, false, false);
}
static int vv_quiete(Demo *d, const char **che)
{
    *che = "quiete raggiunta dopo la pausa dei flussi";
    if (!cascata_a_posto(d) || !fotogrammi_da(d, 2))
        return 0;
    return quieta(d);
}

/* --- 2 e 3 (parte albero): in ogni cornice al piu' un blur, nessuno dentro un altro --- */
static int vv_alberi(Demo *d, const char **che)
{
    *che = "albero: clip del titolo -> color matrix -> blur(52), un solo blur per cornice, nessuno annidato; senza vetro nessun blur";
    PhFaDemoAlbero o;
    ph_finestra_app_demo_albero(d->sh, 0, &o);
    if (o.n_blur != 0 || o.clip_zona != 0)
        return -1;
    for (unsigned k = 2; k <= CASC; k++) { /* sopra ad almeno un'altra finestra che tocca il titolo */
        ph_finestra_app_demo_albero(d->sh, k, &o);
        fprintf(stderr, "demo: cornice %u: blur %d annidati %d clip %d catena %d\n", k, o.n_blur, o.blur_annidati,
                o.clip_zona, o.catena_ok);
        if (o.n_blur != 1 || o.blur_annidati != 0 || o.clip_zona < 1 || o.catena_ok != 1)
            return -1;
    }
    for (unsigned k = 1; k <= CASC; k++) {
        ph_finestra_app_demo_albero(d->sh, k, &o);
        if (o.n_blur > 1 || o.blur_annidati != 0)
            return -1;
    }
    return 1;
}

/* --- 3: il "dietro" di una finestra: strati sotto e FIGLI delle altre, mai la loro cornice --- */
static int vv_dietro(Demo *d, const char **che)
{
    *che = "dietro: figli delle finestre SOTTO e strati sotto la radice, mai una cornice ne' una finestra sopra";
    PhFaDemoAlbero o;
    ph_finestra_app_demo_albero(d->sh, CASC, &o); /* disegna: riempie il dietro */
    ph_finestra_app_demo_albero(d->sh, CASC - 2, &o);
    unsigned nulli, cornici;
    unsigned figli = figli_di(d, CASC, CASC - 1, &nulli, &cornici);
    fprintf(stderr, "demo: dietro di %u: %u figli della finestra sotto, %u cornici, %u spenti\n", CASC, figli, cornici,
            nulli);
    if (figli == 0 || cornici != 0 || nulli != 0)
        return -1;
    /* Una finestra IN MEZZO (la 2): vede la 1 (sotto) ma mai la 3 ne' la 4, che le stanno SOPRA. */
    unsigned mezzo_sotto = figli_di(d, CASC - 2, CASC - 3, NULL, NULL);
    unsigned mezzo_sopra3 = figli_di(d, CASC - 2, CASC - 1, NULL, NULL);
    unsigned mezzo_sopra4 = figli_di(d, CASC - 2, CASC, NULL, NULL);
    fprintf(stderr, "demo: dietro di %u: %u figli della finestra sotto, %u e %u delle finestre sopra\n", CASC - 2,
            mezzo_sotto, mezzo_sopra3, mezzo_sopra4);
    if (mezzo_sotto == 0 || mezzo_sopra3 != 0 || mezzo_sopra4 != 0)
        return -1;
    /* Ogni widget ritratto sta in mostra (mappato e child_visible). */
    GtkWidget *c = cn(d, CASC);
    GdkPaintable *p[64];
    guint n = ph_finestra_app_demo_dietro(d->sh, CASC, p, 64);
    for (guint j = 0; j < n && j < 64; j++) {
        GtkWidget *w = gtk_widget_paintable_get_widget(GTK_WIDGET_PAINTABLE(p[j]));
        if (!gtk_widget_get_mapped(w) || !gtk_widget_get_child_visible(w))
            return -1;
    }
    /* Gli strati sotto la radice: tutti quelli che toccano la zona attorno al titolo ci sono; quelli che NON la
     * toccano non ci sono. */
    graphene_rect_t zona = GRAPHENE_RECT_INIT(-78.0f, -78.0f, (float)gtk_widget_get_width(c) + 156.0f,
                                              (float)PH_FIN_TITOLO_H + 156.0f);
    unsigned strati = 0, lontani = 0;
    GtkWidget *catena[16];
    int nc = 0;
    for (GtkWidget *x = c; x != NULL && nc < 16; x = gtk_widget_get_parent(x)) {
        catena[nc++] = x;
        if (gtk_widget_has_css_class(x, "ph-shell"))
            break;
    }
    for (int k = nc - 1; k >= 1; k--) {
        for (GtkWidget *x = gtk_widget_get_first_child(catena[k]); x != NULL && x != catena[k - 1];
             x = gtk_widget_get_next_sibling(x)) {
            if (ph_finestra_app_demo_e_cornice(x))
                continue;
            graphene_rect_t b;
            if (!gtk_widget_get_mapped(x) || !gtk_widget_get_child_visible(x) ||
                !gtk_widget_compute_bounds(x, c, &b))
                continue;
            bool tocca = graphene_rect_intersection(&b, &zona, NULL);
            bool trovato = false;
            for (guint j = 0; j < n && j < 64; j++)
                trovato = trovato || gtk_widget_paintable_get_widget(GTK_WIDGET_PAINTABLE(p[j])) == x;
            if (tocca)
                strati++;
            else
                lontani++;
            if (tocca != trovato)
                return -1;
        }
    }
    fprintf(stderr, "demo: strati sotto la radice: %u toccano il titolo, %u no\n", strati, lontani);
    return 1;
}

/* --- 3: il vetro SI AGGIORNA. (i) un figlio della finestra sotto si ridisegna: la cornice sopra si ridisegna
 * (segnale dei paintable), e poi i ridisegni si ESAURISCONO entro CASC fotogrammi (nessuna auto-alimentazione). --- */
static void av_aggiorna(Demo *d)
{
    d->conta0 = ph_finestra_app_demo_snapshot_cornice(d->sh, CASC);
    d->ultimo_disegno = 0;
    gtk_widget_queue_draw(gtk_widget_get_first_child(cn(d, 1))); /* un figlio della finestra PIU' IN BASSO */
}
static int vv_aggiorna(Demo *d, const char **che)
{
    *che = "il disegno di un figlio della finestra sotto fa ridisegnare la cornice sopra, poi i ridisegni si esauriscono";
    if (ph_finestra_app_demo_snapshot_cornice(d->sh, CASC) == d->conta0)
        return 0; /* non e' ancora arrivato il ridisegno della cornice sopra */
    if (!quieta(d))
        return 0;
    fprintf(stderr, "demo: ridisegni esauriti %u fotogrammi dopo la richiesta\n", d->ultimo_disegno - d->fr0);
    return d->ultimo_disegno - d->fr0 <= CASC;
}

/* (ii) la finestra in cima (gia' attiva: nessun cambio di stile) si sposta: cio' che sta dietro il suo titolo cambia
 * posizione RISPETTO A LEI senza che nessun ritratto cambi il proprio disegno, quindi la sua cornice si ridisegna
 * solo per vetro_ridisegna (sincronizza). Isola vetro_ridisegna perche' uno spostamento a dimensione invariata
 * non ridisegna il widget stesso (cambia solo la sua trasformazione) e non cambia lo stile (era gia' attiva);
 * le altre scene (minimizza, cambio d'ordine) invalidano comunque i figli per la riattivazione o lo smontaggio. */
static void av_sposta_cima(Demo *d)
{
    d->conta0 = ph_finestra_app_demo_snapshot_cornice(d->sh, CASC);
    if (!ph_finestra_app_demo_posiziona(d->sh, CASC, 160.0, 300.0))
        controllo(false, "av_sposta_cima: il gesto e' stato rifiutato");
}
static int vv_sposta_cima(Demo *d, const char **che)
{
    *che = "la finestra in cima si sposta: la sua cornice si ridisegna (il vetro segue lo spostamento)";
    graphene_rect_t b;
    if (!gtk_widget_compute_bounds(cn(d, CASC), gtk_widget_get_parent(cn(d, CASC)), &b) ||
        fabsf(b.origin.y - 300.0f) > 0.5f)
        return 0; /* il layout non l'ha ancora spostata */
    return ph_finestra_app_demo_snapshot_cornice(d->sh, CASC) != d->conta0;
}

/* --- Il menu «altri comandi» e' una superficie di vetro CHIARO: ha la classe, e nell'albero dei nodi della RADICE
 * il suo blur non contiene nessun altro blur (la cornice che gli sta sotto e' ritratta dai figli, non intera). --- */
static void av_menu(Demo *d)
{
    if (!ph_finestra_app_demo_azione(d->sh, "menu", CASC))
        controllo(false, "av_menu: l'azione menu e' stata rifiutata");
}
/* Visita l'albero della radice (disegnato a mano via paintable): quanti blur e quanti dentro un altro. */
static PhFaDemoAlbero albero_radice(Demo *d)
{
    GtkWidget *radice = ph_shell_radice(d->sh);
    float larg = (float)gtk_widget_get_width(radice);
    GtkSnapshot *sn = gtk_snapshot_new();
    ph_vetro_radice_snapshot(radice, sn); /* la stessa funzione del disegno vero della radice (comune/vetro.c) */
    GskRenderNode *n = gtk_snapshot_free_to_node(sn);
    PhFaDemoAlbero o;
    ph_finestra_app_demo_albero_nodo(n, larg, &o);
    if (n != NULL)
        gsk_render_node_unref(n);
    return o;
}

static int vv_menu(Demo *d, const char **che)
{
    *che = "menu aperto sopra una finestra: classe ph-vetro-chiaro, nessun blur dentro un altro nell'albero della radice";
    GtkWidget *m = ph_finestra_app_demo_menu(d->sh);
    if (m == NULL || !gtk_widget_get_mapped(m) || !fotogrammi_da(d, 2))
        return 0;
    if (!gtk_widget_has_css_class(m, "ph-vetro-chiaro"))
        return -1;
    PhFaDemoAlbero o = albero_radice(d);
    fprintf(stderr, "demo: radice con il menu aperto: blur %d annidati %d\n", o.n_blur, o.blur_annidati);
    ph_shell_popup_chiudi(d->sh);
    return o.blur_annidati == 0;
}

/* --- Modalita' --vetro-menu: la sfocatura della shell e' ACCESA (YASDE_SFOCATURA=1 dall'avvio): il blur del
 * menu c'e' (uno in piu' rispetto alla radice senza menu) e non contiene nessun altro blur (la cornice sotto e'
 * ritratta dai figli, non intera, perche' segnata con ph_shell_vetro_proprio). --- */
static int vm_pronte(Demo *d, const char **che)
{
    *che = "shell con la sfocatura accesa e due finestre pronte";
    if (!ph_shell_vetro_attivo(d->sh))
        return -1;
    return pronta(d, 0) && pronta(d, 1) && fotogrammi_da(d, 3);
}
static int vm_chiuso(Demo *d, const char **che)
{
    *che = "misura dei blur della radice con il menu chiuso";
    PhFaDemoAlbero o = albero_radice(d);
    d->n_on = (unsigned)o.n_blur;
    fprintf(stderr, "demo: radice con il menu chiuso: blur %d annidati %d\n", o.n_blur, o.blur_annidati);
    return o.blur_annidati == 0;
}
static void vma_apri(Demo *d)
{
    if (!ph_finestra_app_demo_azione(d->sh, "menu", 0))
        controllo(false, "vma_apri: l'azione menu e' stata rifiutata");
}
static int vm_aperto(Demo *d, const char **che)
{
    *che = "menu aperto: un blur in piu' (il suo), nessun blur annidato";
    GtkWidget *m = ph_finestra_app_demo_menu(d->sh);
    if (m == NULL || !gtk_widget_get_mapped(m) || !fotogrammi_da(d, 3))
        return 0;
    if (!gtk_widget_has_css_class(m, "ph-vetro-chiaro"))
        return -1;
    PhFaDemoAlbero o = albero_radice(d);
    fprintf(stderr, "demo: radice con il menu aperto: blur %d annidati %d (chiuso: %u)\n", o.n_blur, o.blur_annidati,
            d->n_on);
    ph_shell_popup_chiudi(d->sh);
    return o.blur_annidati == 0 && (unsigned)o.n_blur == d->n_on + 1;
}

/* --- 6: finestra lontana o minimizzata: non entra nel dietro --- */
static void av_lontana(Demo *d)
{
    /* la 3 (sotto la 4) in questo momento c'e' (controllo di vv_dietro); ora lontana in orizzontale,
     * oltre 3 x 26 px dal titolo della 4 */
    if (!ph_finestra_app_demo_posiziona(d->sh, CASC - 1, 900.0, 160.0))
        controllo(false, "av_lontana: il gesto e' stato rifiutato");
}
static int vv_lontana(Demo *d, const char **che)
{
    *che = "finestra lontana oltre 3 x 26 px dal titolo: non entra nel dietro";
    graphene_rect_t b;
    /* valida solo se la finestra e' davvero fuori dalla zona (posizione REALE dopo il layout) */
    if (!gtk_widget_compute_bounds(cn(d, CASC - 1), cn(d, CASC), &b) ||
        b.origin.x < gtk_widget_get_width(cn(d, CASC)) + 78.0f || !fotogrammi_da(d, 2))
        return 0;
    PhFaDemoAlbero o;
    ph_finestra_app_demo_albero(d->sh, CASC, &o);
    return figli_di(d, CASC, CASC - 1, NULL, NULL) == 0;
}

/* Di nuovo vicina: con la finestra al suo posto il dietro la contiene. */
static void av_vicina(Demo *d) { posiziona_cascata(d); }
static int vv_vicina(Demo *d, const char **che)
{
    *che = "finestra di nuovo vicina: rientra nel dietro";
    graphene_rect_t b;
    if (!cascata_a_posto(d) || !gtk_widget_compute_bounds(cn(d, CASC - 1), cn(d, CASC), &b) || !fotogrammi_da(d, 2))
        return 0;
    PhFaDemoAlbero o;
    ph_finestra_app_demo_albero(d->sh, CASC, &o);
    return figli_di(d, CASC, CASC - 1, NULL, NULL) > 0;
}

/* (ii) la finestra sotto si minimizza: la cornice sopra si ridisegna (vetro_ridisegna) e non la ritrae piu'. */
static void av_minimizzata(Demo *d)
{
    d->conta0 = ph_finestra_app_demo_snapshot_cornice(d->sh, CASC);
    ph_finestra_app_demo_azione(d->sh, "minimizza", CASC - 1);
}
static int vv_minimizzata(Demo *d, const char **che)
{
    *che = "finestra sotto minimizzata: la cornice sopra si ridisegna e non la ritrae piu'";
    if (ph_finestra_app_demo_snapshot_cornice(d->sh, CASC) == d->conta0 || gtk_widget_get_mapped(cn(d, CASC - 1)))
        return 0;
    return figli_di(d, CASC, CASC - 1, NULL, NULL) == 0; /* dal disegno VERO della cornice sopra */
}

/* --- 4 e 5: chiusura di una finestra SOTTO, poi il disegno vero di quella sopra.
 * La finestra chiusa e' la Maps (indice 2): i suoi figli sono ritratti nel `dietro` della Weather (indice 4,
 * che le sta sopra e a 80 px); la File (3) e' minimizzata. Dopo la chiusura Weather e' la 3. --- */
static void av_chiudi_sotto(Demo *d)
{
    d->tenuti = g_ptr_array_new_with_free_func(g_object_unref);
    GdkPaintable *p[64];
    guint n = ph_finestra_app_demo_dietro(d->sh, CASC, p, 64);
    d->esito_a = true;
    for (guint k = 0; k < n && k < 64; k++) {
        GtkWidget *w = gtk_widget_paintable_get_widget(GTK_WIDGET_PAINTABLE(p[k]));
        if (w != NULL && gtk_widget_get_parent(w) == cn(d, 2)) {
            if (!ha_gestore(p[k], cn(d, CASC)))
                d->esito_a = false; /* il gestore c'e' PRIMA della chiusura */
            g_ptr_array_add(d->tenuti, g_object_ref(p[k]));
        }
    }
    d->esito_a = d->esito_a && d->tenuti->len > 0;
    d->dati_gestore = cn(d, CASC); /* la Weather: e' lei il dato del gestore */
    d->conta0 = ph_finestra_app_demo_snapshot_cornice(d->sh, CASC); /* la Weather: dopo la chiusura e' la 3 */
    ph_finestra_app_demo_chiudi_ora(d->sh, 2); /* subito: la cornice e i suoi figli sono distrutti */
    /* SUBITO, senza disegnare: la Weather tiene ritratti di widget morti. */
    unsigned nulli = 0;
    figli_di(d, CASC - 1, (unsigned)-1, &nulli, NULL);
    d->morti = nulli;
}
static int vv_chiudi_sotto(Demo *d, const char **che)
{
    *che = "chiusura della finestra sotto: ritratti morti nel dietro (5), poi scartati dal disegno vero (4, 5)";
    if (!d->esito_a || d->morti == 0)
        return -1;
    if (ph_finestra_app_demo_snapshot_cornice(d->sh, CASC - 1) == d->conta0)
        return 0; /* il disegno vero della Weather non e' ancora avvenuto */
    unsigned nulli = 1, cornici = 0;
    figli_di(d, CASC - 1, (unsigned)-1, &nulli, &cornici);
    fprintf(stderr, "demo: ritratti morti subito dopo la chiusura: %u, dopo il disegno vero: %u\n", d->morti, nulli);
    if (nulli != 0 || cornici != 0)
        return -1;
    /* quelli tenuti nella prova non hanno piu' il segnale della cornice */
    for (guint k = 0; k < d->tenuti->len; k++) {
        GdkPaintable *p = g_ptr_array_index(d->tenuti, k);
        if (ha_gestore(p, d->dati_gestore))
            return -1;
        gdk_paintable_invalidate_contents(p); /* non deve toccare nessuna cornice */
    }
    g_ptr_array_set_size(d->tenuti, 0);
    return 1;
}

static void av_chiudi_sopra(Demo *d)
{
    /* La finestra sopra (la Weather, ora la 3) tiene dei paintable con il gestore: con un riferimento in piu',
     * dopo la sua chiusura devono averlo staccato. */
    GdkPaintable *p[64];
    guint n = ph_finestra_app_demo_dietro(d->sh, CASC - 1, p, 64);
    d->esito_a = true;
    for (guint k = 0; k < n && k < 64; k++) {
        if (!ha_gestore(p[k], cn(d, CASC - 1)))
            d->esito_a = false; /* il gestore c'e' PRIMA della chiusura */
        g_ptr_array_add(d->tenuti, g_object_ref(p[k]));
    }
    d->esito_a = d->esito_a && d->tenuti->len > 0;
    d->dati_gestore = cn(d, CASC - 1);
    d->cornice_viva = cn(d, CASC - 1);
    g_object_add_weak_pointer(G_OBJECT(d->cornice_viva), &d->cornice_viva);
    ph_finestra_app_demo_chiudi_ora(d->sh, CASC - 1);
}
static int vv_chiudi_sopra(Demo *d, const char **che)
{
    *che = "dopo la distruzione della cornice i suoi paintable non hanno piu' il segnale; invalidarli non fa nulla";
    if (!d->esito_a)
        return -1;
    if (d->cornice_viva != NULL) /* aspetta che la cornice sia davvero distrutta */
        return 0;
    for (guint k = 0; k < d->tenuti->len; k++) {
        GdkPaintable *p = g_ptr_array_index(d->tenuti, k);
        if (ha_gestore(p, d->dati_gestore))
            return -1;
        gdk_paintable_invalidate_contents(p);
    }
    g_ptr_array_unref(d->tenuti);
    d->tenuti = NULL;
    return 1;
}

/* --- 7: niente ridisegni a riposo: dopo la quiete (segnale), ZERO disegni delle cornici in RIPOSO_F fotogrammi
 * con il vetro acceso. I fotogrammi girano davvero (li conta la tick callback): senza questo "nessun disegno" sarebbe
 * vero banalmente. --- */
static void av_riposo(Demo *d) { d->fase = 0; }
static int vv_riposo(Demo *d, const char **che)
{
    *che = "a riposo, con il vetro acceso, nessun disegno delle cornici in RIPOSO_F fotogrammi";
    if (d->fase == 0) {
        if (!quieta(d))
            return 0;
        d->fase = 1;
        d->fr0 = d->frames;
        d->conta0 = ph_finestra_app_demo_snapshot_conta(d->sh);
        return 0;
    }
    if (ph_finestra_app_demo_snapshot_conta(d->sh) != d->conta0) {
        fprintf(stderr, "demo: a riposo: %u disegni in %u fotogrammi\n",
                ph_finestra_app_demo_snapshot_conta(d->sh) - d->conta0, d->frames - d->fr0);
        return -1;
    }
    if (d->frames - d->fr0 < RIPOSO_F)
        return 0;
    fprintf(stderr, "demo: a riposo: nessun disegno in %u fotogrammi\n", d->frames - d->fr0);
    ph_shell_imposta_host(d->sh, true, false);
    return 1;
}

typedef struct {
    void (*agisci)(Demo *);
    int (*verifica)(Demo *, const char **);
} PassoVetro;

static const PassoVetro PASSI_MENU[] = {
    {av_nulla, vm_pronte},
    {av_nulla, vm_chiuso},
    {vma_apri, vm_aperto},
};

static const PassoVetro PASSI_VETRO[] = {
    {av_nulla, vv_renderer_nomi},
    {av_nulla, vv_senza_var},
    {av_env0, vv_env0},
    {av_env1, vv_env1},
    {av_chiudi_prime, vv_chiuse},
    {av_apri_mappe, vv_mappe},
    {av_apri_file, vv_file},
    {av_apri_meteo, vv_cascata},
    {av_pausa, vv_quiete},
    {av_nulla, vv_alberi},
    {av_nulla, vv_dietro},
    {av_aggiorna, vv_aggiorna},
    {av_menu, vv_menu},
    {av_sposta_cima, vv_sposta_cima},
    {av_lontana, vv_lontana},
    {av_vicina, vv_vicina},
    {av_minimizzata, vv_minimizzata},
    {av_chiudi_sotto, vv_chiudi_sotto},
    {av_chiudi_sopra, vv_chiudi_sopra},
    {av_riposo, vv_riposo},
};

static gboolean passo_vetro(gpointer u)
{
    Demo *d = u;
    const PassoVetro *tab = d->menu_mode ? PASSI_MENU : PASSI_VETRO;
    const guint n_passi = d->menu_mode ? G_N_ELEMENTS(PASSI_MENU) : G_N_ELEMENTS(PASSI_VETRO);
    if (d->passo >= n_passi) {
        fprintf(stderr, "demo: prova vetro %s\n", g_fallito ? "FALLITA" : "riuscita");
        /* Finita: non si aspetta la scadenza di --prova (come se l'utente chiudesse la finestra). */
        GtkRoot *r = gtk_widget_get_root(ph_shell_radice(d->sh));
        if (r != NULL)
            gtk_window_close(GTK_WINDOW(r));
        return G_SOURCE_REMOVE;
    }
    gint64 ora_us = g_get_monotonic_time();
    if (d->attesa_fino == 0) {
        d->fr0 = d->frames;
        tab[d->passo].agisci(d);
        d->attesa_fino = ora_us + SCAD_S * G_USEC_PER_SEC;
        g_timeout_add(100, passo_vetro, d);
        return G_SOURCE_REMOVE;
    }
    const char *che = "";
    int r = tab[d->passo].verifica(d, &che);
    if (r == 0 && ora_us < d->attesa_fino) {
        g_timeout_add(100, passo_vetro, d);
        return G_SOURCE_REMOVE;
    }
    controllo(r == 1, che);
    d->attesa_fino = 0;
    d->passo++;
    g_timeout_add(50, passo_vetro, d);
    return G_SOURCE_REMOVE;
}

/* ======================================================================
 * --mouse: destro e centrale sul contenuto di una finestra app (finestra_app.c, gesto_inizia_pulsante).
 * I gesti si danno con gli stessi gestori di GTK (ph_finestra_app_demo_mouse); si legge il registro dei
 * tocchi che la finestra manda alla superficie. Tutto sincrono: l'unica attesa e' che il contenuto abbia
 * un'immagine (segnale: ph_finestra_app_demo_mouse_pronto), con scadenza. Si prova sulla finestra 1
 * (Messaggi) e, per la maniglia, sulla 2 (Browser, l'unica ridimensionabile); sono affiancate. */
#define BTN_SX 1
#define BTN_CE 2
#define BTN_DX 3

static bool tocchi_sono(Demo *d, unsigned n, const int *azioni, const unsigned *pulsanti)
{
    PhFaDemoTocco t[PH_FA_DEMO_TOCCHI_MAX];
    if (ph_finestra_app_demo_tocchi(d->sh, t, PH_FA_DEMO_TOCCHI_MAX) != n)
        return false;
    for (unsigned i = 0; i < n; i++)
        if (t[i].azione != azioni[i] || t[i].pulsanti != pulsanti[i])
            return false;
    return true;
}

#define MOUSE(z, a, b) ph_finestra_app_demo_mouse(d->sh, 1, z, a, b)
#define MOUSE0(z, a, b) ph_finestra_app_demo_mouse(d->sh, 0, z, a, b)
#define MOUSE2(z, a, b) ph_finestra_app_demo_mouse(d->sh, 2, z, a, b)
#define PULITO ph_finestra_app_demo_tocchi_azzera(d->sh)

/* La finestra attiva e' quella in posizione i dell'elenco (come il demo legge F2). */
static bool attiva_e(Demo *d, size_t i)
{
    PhShellFinestra f[DEMO_FINESTRE_MAX];
    size_t n = ph_shell_finestre(d->sh, f, DEMO_FINESTRE_MAX);
    return i < n && f[i].attiva;
}

static void prova_mouse(Demo *d)
{
    enum { G = PH_SRV_TOCCO_GIU, M = PH_SRV_TOCCO_MUOVI, S = PH_SRV_TOCCO_SU, A = PH_SRV_TOCCO_ANNULLA };

    /* Sinistro: come prima (giu e muovi con 1, su con 0). */
    PULITO;
    controllo(MOUSE("contenuto", "giu", BTN_SX) && MOUSE("contenuto", "muovi", 0) && MOUSE("contenuto", "su", 0),
              "mouse: sinistro, gesti accettati");
    controllo(tocchi_sono(d, 3, (int[]){G, M, S}, (unsigned[]){1, 1, 0}), "mouse: sinistro = giu 1, muovi 1, su 0");

    /* Destro: bit 2. */
    PULITO;
    MOUSE("contenuto", "giu", BTN_DX);
    controllo(ph_finestra_app_demo_gesto_in_corso(d->sh), "mouse: destro giu apre il gesto");
    MOUSE("contenuto", "muovi", 0);
    MOUSE("contenuto", "su", 0);
    controllo(tocchi_sono(d, 3, (int[]){G, M, S}, (unsigned[]){2, 2, 0}), "mouse: destro = giu 2, muovi 2, su 0");
    controllo(!ph_finestra_app_demo_gesto_in_corso(d->sh), "mouse: destro su chiude il gesto");

    /* Centrale: bit 4. */
    PULITO;
    MOUSE("contenuto", "giu", BTN_CE);
    MOUSE("contenuto", "muovi", 0);
    MOUSE("contenuto", "su", 0);
    controllo(tocchi_sono(d, 3, (int[]){G, M, S}, (unsigned[]){4, 4, 0}), "mouse: centrale = giu 4, muovi 4, su 0");

    /* Barra del titolo e bordo: nessun tocco, nessun gesto (il destro non sposta ne' ridimensiona). */
    PULITO;
    controllo(MOUSE("titolo", "giu", BTN_DX), "mouse: punto del titolo (finestra 1)");
    controllo(!ph_finestra_app_demo_gesto_in_corso(d->sh), "mouse: destro sul titolo non apre gesti");
    controllo(MOUSE("pulsante", "giu", BTN_CE), "mouse: punto del pulsante (finestra 1)");
    controllo(!ph_finestra_app_demo_gesto_in_corso(d->sh), "mouse: centrale sul pulsante non apre gesti");
    controllo(MOUSE2("angolo", "giu", BTN_DX), "mouse: punto della maniglia (finestra 2, Browser)");
    controllo(!ph_finestra_app_demo_gesto_in_corso(d->sh), "mouse: destro sulla maniglia non apre gesti");
    controllo(tocchi_sono(d, 0, NULL, NULL), "mouse: titolo, pulsante e maniglia, nessun tocco");

    /* Destro e centrale fuori dal contenuto non cambiano la finestra attiva; sul contenuto la portano davanti. */
    MOUSE0("contenuto", "giu", BTN_SX);
    MOUSE0("contenuto", "su", 0);
    controllo(attiva_e(d, 0), "mouse: il sinistro sul contenuto della finestra 0 la rende attiva");
    MOUSE("titolo", "giu", BTN_DX);
    MOUSE("pulsante", "giu", BTN_CE);
    MOUSE2("angolo", "giu", BTN_DX);
    controllo(attiva_e(d, 0), "mouse: destro e centrale su titolo, pulsante e maniglia non cambiano la finestra attiva");
    PULITO;
    MOUSE("contenuto", "giu", BTN_DX);
    controllo(attiva_e(d, 1), "mouse: destro sul contenuto porta la finestra davanti");
    MOUSE("contenuto", "su", 0);
    PULITO;

    /* Pulsanti che non vanno al telefono (indietro, avanti). */
    MOUSE("contenuto", "giu", 8);
    MOUSE("contenuto", "giu", 9);
    controllo(!ph_finestra_app_demo_gesto_in_corso(d->sh) && tocchi_sono(d, 0, NULL, NULL),
              "mouse: pulsanti 8 e 9 ignorati");

    /* Un pulsante alla volta: il secondo si ignora, il gesto resta del primo. */
    PULITO;
    MOUSE("contenuto", "giu", BTN_SX);
    MOUSE("contenuto", "giu", BTN_DX);
    MOUSE("contenuto", "muovi", 0);
    MOUSE("contenuto", "su", 0);
    controllo(tocchi_sono(d, 3, (int[]){G, M, S}, (unsigned[]){1, 1, 0}), "mouse: (difesa) destro durante il sinistro ignorato");
    PULITO;
    MOUSE("contenuto", "giu", BTN_DX);
    MOUSE("contenuto", "giu", BTN_SX);
    MOUSE("contenuto", "giu", BTN_CE);
    MOUSE("contenuto", "muovi", 0);
    MOUSE("contenuto", "su", 0);
    controllo(tocchi_sono(d, 3, (int[]){G, M, S}, (unsigned[]){2, 2, 0}), "mouse: (difesa) sinistro e centrale durante il destro ignorati");

    /* Perdita del puntatore a pulsante giu: annulla (pulsanti 0), mai un giu senza chiusura. */
    PULITO;
    MOUSE("contenuto", "giu", BTN_DX);
    MOUSE("contenuto", "annulla", 0);
    controllo(tocchi_sono(d, 2, (int[]){G, A}, (unsigned[]){2, 0}) && !ph_finestra_app_demo_gesto_in_corso(d->sh),
              "mouse: annullamento a destro giu = annulla");
    /* ...e dopo l'annullamento la fine arrivata dal toolkit non manda altro. */
    MOUSE("contenuto", "su", 0);
    controllo(tocchi_sono(d, 2, (int[]){G, A}, (unsigned[]){2, 0}), "mouse: dopo l'annullamento nessun su");

    /* Pausa (finestra minimizzata) a pulsante giu: annulla. Finestra 0, le altre servono dopo. */
    PULITO;
    MOUSE0("contenuto", "giu", BTN_DX);
    ph_finestra_app_demo_azione(d->sh, "minimizza", 0);
    controllo(tocchi_sono(d, 2, (int[]){G, A}, (unsigned[]){2, 0}) && !ph_finestra_app_demo_gesto_in_corso(d->sh),
              "mouse: pausa a destro giu = annulla");

    /* Finestra chiusa a pulsante giu: annulla. */
    PULITO;
    MOUSE("contenuto", "giu", BTN_CE);
    ph_finestra_app_demo_chiudi_ora(d->sh, 1);
    controllo(tocchi_sono(d, 2, (int[]){G, A}, (unsigned[]){4, 0}) && !ph_finestra_app_demo_gesto_in_corso(d->sh),
              "mouse: finestra chiusa a centrale giu = annulla");
}

static gboolean passo_mouse(gpointer u)
{
    Demo *d = u;
    /* Finestre una accanto all'altra (appena ci sono tutte e tre): nessuna copre il contenuto, il
     * titolo o la maniglia delle altre. Poi si aspetta l'immagine della finestra 1. */
    if (!d->esito_a)
        d->esito_a = ph_finestra_app_demo_posiziona(d->sh, 0, 40.0, 40.0) &&
                     ph_finestra_app_demo_posiziona(d->sh, 1, 400.0, 40.0) &&
                     ph_finestra_app_demo_posiziona(d->sh, 2, 700.0, 5.0);
    if (!d->esito_a || !ph_finestra_app_demo_mouse_pronto(d->sh, 1)) {
        if (g_get_monotonic_time() - d->t0 > 30 * G_USEC_PER_SEC) {
            controllo(false, "mouse: la finestra non ha mai avuto un'immagine");
            d->passo = 1;
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }
    prova_mouse(d);
    d->passo = 1;
    fprintf(stderr, "demo: prova mouse %s\n", g_fallito ? "FALLITA" : "riuscita");
    return G_SOURCE_REMOVE;
}


static void avviso_finto(void *io, const char *titolo, const char *testo)
{
    (void)titolo;
    (void)testo;
    ((Demo *)io)->avvisi++;
}

static void app_arrivate(PhServizi *s, PhSrvEvento ev, const char *arg, void *u)
{
    (void)ev;
    (void)arg;
    Demo *d = u;
    size_t n;
    ph_srv_app(s, &n);
    if (n == 0)
        return;
    ph_srv_smetti(s, d->id);
    ph_shell_apri_app(d->sh, "it.finto.note");
    ph_shell_apri_app(d->sh, "it.finto.messaggi");
    if (d->compat) {
        d->passo = 0;
        g_timeout_add(500, passo_compat, d);
    }
    if (d->vetro) {
        d->passo = 0;
        g_timeout_add(500, passo_vetro, d);
    }
    if (d->mouse) {
        ph_shell_apri_app(d->sh, "it.finto.browser"); /* ridimensionabile: ha la maniglia */
        d->passo = 0;
        d->t0 = g_get_monotonic_time();
        g_timeout_add(500, passo_mouse, d);
    }
    if (d->scena) {
        ph_shell_apri_app(d->sh, "it.finto.browser");
        g_timeout_add(SCENA[0].ms, passo_scena, d);
    }
}

/* In cattura sulla radice: i comandi del demo passano prima della finestra
 * attiva (che altrimenti manderebbe i tasti funzione al telefono). */
static gboolean tasto(GtkEventControllerKey *k, guint t, guint c, GdkModifierType m, gpointer u)
{
    (void)k;
    (void)c;
    (void)m;
    Demo *d = u;
    PhShellFinestra f[DEMO_FINESTRE_MAX];
    switch (t) {
    case GDK_KEY_F2:
    case GDK_KEY_F3: {
        size_t n = ph_shell_finestre(d->sh, f, DEMO_FINESTRE_MAX);
        for (size_t i = 0; i < n; i++) {
            if ((t == GDK_KEY_F2 && f[i].attiva && !f[i].minimizzata) ||
                (t == GDK_KEY_F3 && f[i].minimizzata)) {
                ph_shell_clic_barra(d->sh, f[i].id);
                break;
            }
        }
        return TRUE;
    }
    case GDK_KEY_F4:
        d->host_visibile = !d->host_visibile;
        ph_shell_imposta_host(d->sh, d->host_visibile, false);
        return TRUE;
    case GDK_KEY_F5:
        d->bloccato = !d->bloccato;
        ph_finto_blocca(d->finto, d->bloccato);
        return TRUE;
    case GDK_KEY_F6:
        ph_finto_perdi(d->finto, 5);
        return TRUE;
    case GDK_KEY_F7:
        ph_finto_notifica_nuova(d->finto);
        return TRUE;
    case GDK_KEY_F8:
        ph_finto_chiudi_app_da_sola(d->finto);
        return TRUE;
    case GDK_KEY_F9: {
        size_t n;
        const PhSrvApp *a = ph_srv_app(ph_shell_servizi(d->sh), &n);
        if (n > 0)
            ph_shell_apri_app(d->sh, a[d->prossima++ % n].pacchetto);
        return TRUE;
    }
    default:
        return FALSE;
    }
}

static void installa(PhShell *sh, PhFinto *f, void *u)
{
    Demo *d = u;
    d->sh = sh;
    d->finto = f;
    PhShellGestoreAvvisi ga = {.io = d, .avviso = avviso_finto};
    ph_shell_registra_avvisi(sh, &ga);
    ph_finestra_app_installa(sh);
    d->id = ph_srv_ascolta(ph_shell_servizi(sh), PH_SRV_EV_APP, app_arrivate, d);
    if (d->vetro)
        gtk_widget_add_tick_callback(ph_shell_radice(sh), tick_fotogrammi, d, NULL);
    if (!d->tasti_al_telefono) {
        GtkEventController *k = gtk_event_controller_key_new();
        gtk_event_controller_set_propagation_phase(k, GTK_PHASE_CAPTURE);
        g_signal_connect(k, "key-pressed", G_CALLBACK(tasto), d);
        gtk_widget_add_controller(ph_shell_radice(sh), k);
    }
}

int main(int argc, char **argv)
{
    Demo d = {0};
    d.host_visibile = true;
    /* Le opzioni del demo si tolgono: il demo comune non le conosce. */
    int n = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scena") == 0)
            d.scena = true;
        else if (strcmp(argv[i], "--compat") == 0)
            d.compat = true;
        else if (strcmp(argv[i], "--mouse") == 0)
            d.mouse = true;
        else if (strcmp(argv[i], "--vetro") == 0)
            d.vetro = true;
        else if (strcmp(argv[i], "--vetro-menu") == 0)
            d.vetro = d.menu_mode = true;
        else if (strcmp(argv[i], "--vetro-diag") == 0)
            d.vetro = d.spia = true;
        else if (strcmp(argv[i], "--tasti-al-telefono") == 0)
            d.tasti_al_telefono = true;
        else
            argv[n++] = argv[i];
    }
    argc = n;
    if (d.vetro) {
        if (d.menu_mode)
            g_setenv("YASDE_SFOCATURA", "1", TRUE); /* sfocatura della shell accesa fin dalla realizzazione */
        else
            g_unsetenv("YASDE_SFOCATURA"); /* la prova 1 parte con la variabile assente */
        /* Nessun avviso di GTK (per esempio "snapshot without a current allocation") deve comparire. */
        g_log_set_always_fatal((GLogLevelFlags)(G_LOG_FATAL_MASK | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL));
    }
    int rc = ph_finto_demo(argc, argv, "YASDE 2.2 - demo finestra-app", installa, &d);
    /* La prova --compat deve aver finito tutti i passi nel tempo dato (--prova MS). */
    if (d.compat && d.passo < G_N_ELEMENTS(PASSI_COMPAT)) {
        fprintf(stderr, "demo: prova compatibilita' NON FINITA (passo %u)\n", d.passo);
        g_fallito = 1;
    }
    if (d.mouse && d.passo < 1) {
        fprintf(stderr, "demo: prova mouse NON FINITA\n");
        g_fallito = 1;
    }
    if (d.vetro && d.passo < (d.menu_mode ? G_N_ELEMENTS(PASSI_MENU) : G_N_ELEMENTS(PASSI_VETRO))) {
        fprintf(stderr, "demo: prova vetro NON FINITA (passo %u)\n", d.passo);
        g_fallito = 1;
    }
    return rc != 0 ? rc : g_fallito;
}
