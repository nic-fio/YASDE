/* demo-desktop: il solo pezzo desktop sopra il telefono finto. Gli altri pezzi
 * non ci sono: qui si registrano dei gestori finti (finestre, sistema, avvisi) e
 * un pulsante trascinabile che fa da sorgente di PH_TIPO_APP_TRASCINATA (il
 * launcher).
 * Per guardarlo senza mouse (strumenti/foto-demo.sh):
 *   --mostra cosa@ms[,cosa@ms...]   es. "widget3@500,menu-desktop@1500,chiudi@2500,sfondo@3000"
 * cosa: widget3, menu-desktop, menu-icona, menu-widget, sfondo, sfondo-solido,
 * sfondo-sfumato, sfondo-immagine, sfondo-scegli-prima, catalogo, chiudi.
 *   --ora HH:MM:SS   congela l'ora degli orologi (per le foto)
 * Il resto delle opzioni va a ph_finto_demo (--misura, --prova, ...). */
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interno.h"
#include "shell/finto/finto.h"

/* Ultima app e pagina chieste al gestore (le legge la prova d'insieme in demo_hook.c). */
char g_demo_aperta[256];
PhShellPagina g_demo_pagina = PH_SHELL_PAGINA_NESSUNA;

static void apri_app_finto(void *io, const char *pacchetto)
{
    (void)io;
    fprintf(stderr, "demo: apri app %s\n", pacchetto);
    g_strlcpy(g_demo_aperta, pacchetto, sizeof g_demo_aperta);
    g_demo_pagina = PH_SHELL_PAGINA_NESSUNA;
}

static void apri_pagina_finta(void *io, const char *pacchetto, PhShellPagina pagina)
{
    (void)io;
    fprintf(stderr, "demo: apri app %s sulla pagina %d\n", pacchetto, (int)pagina);
    g_strlcpy(g_demo_aperta, pacchetto, sizeof g_demo_aperta);
    g_demo_pagina = pagina;
}

static size_t elenco_finto(void *io, PhShellFinestra *out, size_t max)
{
    (void)io;
    (void)out;
    (void)max;
    return 0;
}

static void clic_barra_finto(void *io, unsigned id)
{
    (void)io;
    (void)id;
}

static void specchio_finto(void *io, bool mostra)
{
    (void)io;
    (void)mostra;
}

static bool specchio_aperto_finto(void *io)
{
    (void)io;
    return false;
}

static void avviso_finto(void *io, const char *titolo, const char *testo)
{
    (void)io;
    fprintf(stderr, "demo: avviso \"%s\" / \"%s\"\n", titolo, testo);
}

static bool nascosta_finta(void *io, const char *chiave, uint64_t ora_ms)
{
    (void)io;
    (void)chiave;
    (void)ora_ms;
    return false; /* il demo non nasconde nulla */
}

static GtkWidget *pulsante_finto(void *io, PhShellPopup tipo)
{
    (void)io;
    (void)tipo;
    return NULL;
}

void ph_demo_mostra(PhShell *sh, const char *cosa); /* demo_hook.c */

static void finto_comando(const char *c);

typedef struct {
    PhShell *sh;
    char cosa[48];
} Passo;

static gboolean passo_scatta(gpointer dati)
{
    Passo *p = dati;
    if (g_str_has_prefix(p->cosa, "finto:"))
        finto_comando(p->cosa + 6);
    else
        ph_demo_mostra(p->sh, p->cosa);
    g_free(p);
    return G_SOURCE_REMOVE;
}

static const char *mostra = NULL;
static PhFinto *g_finto = NULL;

/* Comandi del telefono finto per gli stati difficili dei widget nuovi. */
static void finto_comando(const char *c);

/* Per la prova d'insieme (demo_hook.c). */
PhFinto *ph_demo_finto(void)
{
    return g_finto;
}

void ph_demo_finto_comando(const char *c)
{
    finto_comando(c);
}

static void finto_comando(const char *c)
{
    static const struct {
        const char *nome;
        PhFintoVoce voce;
    } voci[] = {{"calendario", PH_FINTO_VOCE_CALENDARIO_MESE},
                {"sveglia", PH_FINTO_VOCE_SVEGLIA},
                {"batteria", PH_FINTO_VOCE_BATTERIA},
                {"controlli", PH_FINTO_VOCE_CONTROLLI}};
    if (g_finto == NULL)
        return;
    if (strcmp(c, "sveglia-assente") == 0)
        ph_finto_sveglia_assente(g_finto, true);
    else if (strcmp(c, "trasf") == 0) {
        /* Tre trasferimenti finti: due inviati e uno ricevuto (nessun file viene scritto). */
        PhErrore e = {{0}};
        PhServizi *s = ph_finto_servizi(g_finto);
        char *cartella = g_build_filename(g_get_user_cache_dir(), "yasde-sviluppo", NULL);
        g_mkdir_with_parents(cartella, 0700);
        /* File veri ma piccoli nella cache di sviluppo: il finto fa avanzare un file di pochi byte
         * troppo piano per completarlo. */
        char *f1 = g_build_filename(cartella, "invio-prova.txt", NULL);
        char *f2 = g_build_filename(cartella, "elenco-prova.csv", NULL);
        char contenuto[4096];
        memset(contenuto, 'x', sizeof contenuto);
        g_file_set_contents(f1, contenuto, sizeof contenuto, NULL);
        g_file_set_contents(f2, contenuto, sizeof contenuto, NULL);
        if (ph_srv_tr_accoda(s, PH_SRV_INVIO, f1, "/sdcard/Download", &e) != 0)
            fprintf(stderr, "demo: trasferimento rifiutato: %s\n", e.testo);
        if (ph_srv_tr_accoda(s, PH_SRV_RICEZIONE, "/storage/emulated/0/Pictures/Screenshots/Screenshot_1120.png", cartella, &e) != 0)
            fprintf(stderr, "demo: trasferimento rifiutato: %s\n", e.testo);
        if (ph_srv_tr_accoda(s, PH_SRV_INVIO, f2, "/sdcard/Download", &e) != 0)
            fprintf(stderr, "demo: trasferimento rifiutato: %s\n", e.testo);
        g_free(f1);
        g_free(f2);
        g_free(cartella);
        fprintf(stderr, "demo: trasferimenti accodati\n");
    } else if (strcmp(c, "carica") == 0)
        ph_finto_in_carica(g_finto, true);
    else if (strcmp(c, "tstato") == 0) {
        size_t n = 0, nr = 0;
        const PhSrvTrasferimento *t = ph_srv_trasferimenti(ph_finto_servizi(g_finto), &n);
        ph_srv_trasferimenti_recenti(ph_finto_servizi(g_finto), &nr);
        fprintf(stderr, "demo: coda %zu, recenti %zu, primo stato %d\n", n, nr, n > 0 ? (int)t[0].stato : -1);
    } else if (g_str_has_prefix(c, "meteo-modo:")) {
        ph_finto_meteo_modo(g_finto, (PhFintoMeteoModo)atoi(c + 11));
    } else if (g_str_has_prefix(c, "meteo-cond:")) {
        ph_finto_meteo_condizioni(g_finto, atoi(c + 11));
    } else if (g_str_has_prefix(c, "meteo-intervallo:")) {
        ph_finto_meteo_intervallo(g_finto, atoi(c + 17));
    } else if (strcmp(c, "meteo-cerca-errore") == 0) {
        ph_finto_meteo_ricerca_errore(g_finto, true);
    } else if (strcmp(c, "meteo-nuovi") == 0) {
        ph_finto_meteo_nuovi_dati(g_finto);
    } else if (strcmp(c, "ricerca-lenta:1") == 0 || strcmp(c, "ricerca-lenta:0") == 0) {
        ph_finto_meteo_ricerca_lenta(g_finto, c[14] == '1');
    } else if (strcmp(c, "ricerca-errore:1") == 0 || strcmp(c, "ricerca-errore:0") == 0) {
        ph_finto_meteo_ricerca_errore(g_finto, c[15] == '1');
    } else if (g_str_has_prefix(c, "batt:")) {
        ph_finto_batteria(g_finto, atoi(c + 5));
    } else if (strcmp(c, "notifica") == 0) {
        ph_finto_notifica_nuova(g_finto);
    } else if (strcmp(c, "notifica-agg") == 0) {
        ph_finto_notifica_aggiorna(g_finto);
    } else if (strcmp(c, "notifiche-ill:1") == 0 || strcmp(c, "notifiche-ill:0") == 0) {
        ph_finto_notifiche_illeggibili(g_finto, c[14] == '1');
    } else if (g_str_has_prefix(c, "perdi:")) {
        ph_finto_perdi(g_finto, atoi(c + 6));
    } else if (strcmp(c, "meteo-lento") == 0) {
        ph_finto_meteo_lento(g_finto, true);
    } else if (strcmp(c, "cal-max") == 0) {
        ph_finto_calendario_massimo(g_finto, 3);
    } else if (strcmp(c, "torcia-nd") == 0)
        ph_finto_torcia_disponibile(g_finto, false);
    else if (strcmp(c, "suoneria-silenzioso") == 0)
        ph_finto_controllo_dal_telefono(g_finto, PH_SRV_CTL_SUONERIA, PH_SRV_SUONERIA_SILENZIOSO);
    else if (strcmp(c, "dnd") == 0)
        ph_finto_controllo_dal_telefono(g_finto, PH_SRV_CTL_NON_DISTURBARE, 1);
    else if (strcmp(c, "torcia") == 0)
        ph_finto_controllo_dal_telefono(g_finto, PH_SRV_CTL_TORCIA, 1);
    else if (g_str_has_prefix(c, "nd:") || g_str_has_prefix(c, "err:") || g_str_has_prefix(c, "ok:")) {
        const char *due = strchr(c, ':') + 1;
        PhFintoModo modo = c[0] == 'n' ? PH_FINTO_MODO_NON_DISPONIBILE : (c[0] == 'e' ? PH_FINTO_MODO_ERRORE : PH_FINTO_MODO_NORMALE);
        for (size_t i = 0; i < G_N_ELEMENTS(voci); i++)
            if (strcmp(voci[i].nome, due) == 0)
                ph_finto_voce_modo(g_finto, voci[i].voce, modo);
    }
}

static void installa(PhShell *sh, PhFinto *f, void *u)
{
    (void)u;
    g_finto = f;
    PhShellGestoreFinestre gf = {.io = NULL,
                                 .apri_app = apri_app_finto,
                                 .apri_pagina = apri_pagina_finta,
                                 .elenco = elenco_finto,
                                 .clic_barra = clic_barra_finto,
                                 .specchio = specchio_finto,
                                 .specchio_aperto = specchio_aperto_finto};
    ph_shell_registra_finestre(sh, &gf);
    PhShellGestoreAvvisi ga = {.io = NULL, .avviso = avviso_finto};
    ph_shell_registra_avvisi(sh, &ga);
    PhShellGestoreSistema gs = {.io = NULL, .pulsante = pulsante_finto, .nascosta = nascosta_finta};
    ph_shell_registra_sistema(sh, &gs);

    ph_desktop_installa(sh);

    /* Sorgente di trascinamento al posto del launcher. */
    GtkWidget *sorgente = gtk_button_new_with_label("Trascina un'app (Mappe)");
    gtk_widget_set_halign(sorgente, GTK_ALIGN_START);
    gtk_widget_set_valign(sorgente, GTK_ALIGN_END);
    gtk_widget_set_margin_start(sorgente, 16);
    gtk_widget_set_margin_bottom(sorgente, 8);
    GtkDragSource *src = gtk_drag_source_new();
    gtk_drag_source_set_actions(src, GDK_ACTION_COPY);
    GdkContentProvider *cp = ph_shell_app_trascinata_contenuto("it.finto.mappe", false);
    gtk_drag_source_set_content(src, cp);
    g_object_unref(cp);
    gtk_widget_add_controller(sorgente, GTK_EVENT_CONTROLLER(src));
    ph_shell_strato_aggiungi(sh, PH_STRATO_BARRA, sorgente);

    /* Sequenza a tempo per le fotografie. */
    if (mostra != NULL) {
        char *copia = g_strdup(mostra);
        for (char *tok = strtok(copia, ","); tok != NULL; tok = strtok(NULL, ",")) {
            char *at = strchr(tok, '@');
            guint ritardo = 1500; /* senza «@ms»: il telefono finto pubblica il profilo a ~300 ms */
            if (at != NULL) {
                *at = '\0';
                ritardo = (guint)atoi(at + 1);
            }
            Passo *p = g_new0(Passo, 1);
            p->sh = sh;
            g_strlcpy(p->cosa, tok, sizeof p->cosa);
            g_timeout_add(ritardo, passo_scatta, p);
        }
        g_free(copia);
    }
}

int main(int argc, char **argv)
{
    /* Le copie ridimensionate dello sfondo vanno nella cache di sviluppo, non in quella dell'utente. */
    /* Non in prova: senza XDG_RUNTIME_DIR GTK cerca il socket di broadway nella cartella di cache, e
     * spostarla gli farebbe perdere lo schermo («nessuno schermo: prova saltata»). Nel container
     * della prova la cache e' comunque usa e getta. */
    bool in_prova = false;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--prova") == 0)
            in_prova = true;
    if (g_getenv("XDG_CACHE_HOME") == NULL && !in_prova) {
        char *c = g_build_filename(g_get_home_dir(), ".cache", "yasde-sviluppo", "cache-desktop", NULL);
        g_setenv("XDG_CACHE_HOME", c, TRUE);
        g_free(c);
    }
    /* Toglie --mostra dalla riga di comando prima di passarla al demo comune. */
    int n = 0;
    char **v = g_new0(char *, (gsize)argc + 1);
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--mostra") == 0 && i + 1 < argc) {
            mostra = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--ora") == 0 && i + 1 < argc) {
            /* Ora congelata per le foto: HH:MM:SS. */
            int h = 0, m = 0, sec = 0;
            if (sscanf(argv[++i], "%d:%d:%d", &h, &m, &sec) >= 2)
                ph_dsk_orologio_ora_fissa(h * 3600 + m * 60 + sec);
            continue;
        }
        v[n++] = argv[i];
    }
    int r = ph_finto_demo(n, v, "YASDE 2.3 - demo desktop", installa, NULL);
    g_free(v);
    return r;
}
