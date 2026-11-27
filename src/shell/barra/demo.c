/* demo-barra: il solo pezzo barra sopra il telefono finto. Gli altri pezzi
 * non ci sono: qui si registrano dei gestori FINTI (solo per provare la barra)
 * per finestre, launcher, trasferimento e sistema.
 *
 * Tasti (oltre a F5-F8 del telefono finto):
 *   F9          accoda un invio finto (il puntino di "Invia" compare e sparisce)
 *   Maiusc+F9   accoda una ricezione finta (puntino di "Scarica")
 *   F10         apre la prossima app non ancora aperta
 *   Ctrl+F10    chiude l'ultima finestra
 * Sulle icone: clic destro, tasto Menu o Maiusc+F10 aprono il menu.
 * Opzioni del demo: --en (testi in inglese), --mostra molte (16 fissate) | calendario | menu:N (menu dell'N-esima icona,
 * da 0), per le fotografie. Con --prova la sequenza gira da sola. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/lingua.h"
#include "shell/finto/finto.h"
#include "shell/pezzi.h"

#define MAX_FIN 16

typedef struct {
    const char *pacchetto;
    const char *nome;
} AppDemo;

/* Nomi del telefono finto (src/shell/finto): dati di prova, non testi della shell. */
static const AppDemo APP[] = {
    {"it.finto.messaggi", "Messaggi"}, {"it.finto.browser", "Browser"},
    {"it.finto.note", "Note"},         {"it.finto.foto", "Foto"},
    {"it.finto.calcolatrice", "Calcolatrice"}, {"it.finto.mappe", "Mappe"},
};

typedef struct {
    PhShell *sh;
    PhShellFinestra v[MAX_FIN];
    size_t n;
    unsigned prossimo_id;
    GtkWidget *popup[3 + 3]; /* finti: launcher, invio, ricezione, volume, telefono, notifiche */
    int passo_prova;
} Demo;

static Demo D;
static bool INGLESE;
static const char *MOSTRA; /* --mostra calendario (tolto prima di ph_finto_demo) */

/* ---- Gestore finestre finto --------------------------------------------- */

static void cambiato(void)
{
    ph_shell_emetti(D.sh, PH_SHELL_EV_FINESTRE);
}

static void fin_attiva(size_t i)
{
    for (size_t k = 0; k < D.n; k++)
        D.v[k].attiva = (k == i);
    D.v[i].minimizzata = false;
}

static size_t fin_aggiungi(const char *pacchetto, const char *titolo, bool specchio)
{
    if (D.n >= MAX_FIN)
        return D.n - 1;
    PhShellFinestra *f = &D.v[D.n];
    memset(f, 0, sizeof *f);
    f->id = ++D.prossimo_id;
    snprintf(f->pacchetto, sizeof f->pacchetto, "%s", pacchetto);
    snprintf(f->titolo, sizeof f->titolo, "%s", titolo);
    f->specchio = specchio;
    D.n++;
    return D.n - 1;
}

static void fin_apri_app(void *io, const char *pacchetto)
{
    (void)io;
    for (size_t i = 0; i < D.n; i++) {
        if (strcmp(D.v[i].pacchetto, pacchetto) == 0) {
            fin_attiva(i);
            cambiato();
            return;
        }
    }
    for (size_t a = 0; a < G_N_ELEMENTS(APP); a++) {
        if (strcmp(APP[a].pacchetto, pacchetto) == 0) {
            fin_attiva(fin_aggiungi(APP[a].pacchetto, APP[a].nome, false));
            cambiato();
            return;
        }
    }
}

static size_t fin_elenco(void *io, PhShellFinestra *out, size_t max)
{
    (void)io;
    size_t n = D.n < max ? D.n : max;
    memcpy(out, D.v, n * sizeof *out);
    return n;
}

/* Come dice la specifica (5.3 punto 3): in primo piano -> minimizza,
 * minimizzata -> ripristina, altrimenti porta davanti. */
static void fin_clic_barra(void *io, unsigned id)
{
    (void)io;
    for (size_t i = 0; i < D.n; i++) {
        if (D.v[i].id != id)
            continue;
        if (D.v[i].attiva && !D.v[i].minimizzata) {
            D.v[i].minimizzata = true;
            D.v[i].attiva = false;
        } else {
            fin_attiva(i);
        }
        cambiato();
        return;
    }
}

static void fin_specchio(void *io, bool mostra)
{
    (void)io;
    (void)mostra;
}

static void fin_chiudi_id(void *io, unsigned id)
{
    (void)io;
    for (size_t i = 0; i < D.n; i++) {
        if (D.v[i].id != id)
            continue;
        memmove(&D.v[i], &D.v[i + 1], (D.n - i - 1) * sizeof D.v[0]);
        D.n--;
        cambiato();
        return;
    }
}

static bool fin_specchio_aperto(void *io)
{
    (void)io;
    return false;
}

static void fin_chiudi_ultima(void)
{
    if (D.n == 0)
        return;
    D.n--;
    bool c_era_attiva = D.v[D.n].attiva;
    if (c_era_attiva && D.n > 0)
        fin_attiva(D.n - 1);
    cambiato();
}

static void fin_apri_prossima(void)
{
    for (size_t a = 0; a < G_N_ELEMENTS(APP); a++) {
        bool aperta = false;
        for (size_t i = 0; i < D.n; i++)
            if (strcmp(D.v[i].pacchetto, APP[a].pacchetto) == 0)
                aperta = true;
        if (!aperta) {
            fin_apri_app(NULL, APP[a].pacchetto);
            return;
        }
    }
}

/* ---- Popup finti: un pannello con una riga di testo --------------------- */

static void popup_chiuso(PhShell *sh, PhShellPopup tipo, GtkWidget *w, void *utente)
{
    (void)sh;
    (void)tipo;
    (void)utente;
    gtk_widget_set_visible(w, FALSE);
}

static GtkWidget *popup_finto(PhShellPopup tipo, const char *testo)
{
    GtkWidget *p = gtk_label_new(testo);
    gtk_widget_add_css_class(p, "ph-pannello");
    gtk_widget_set_halign(p, GTK_ALIGN_END);
    gtk_widget_set_valign(p, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(p, 56);
    gtk_widget_set_margin_end(p, 8);
    gtk_widget_set_visible(p, FALSE);
    ph_shell_strato_aggiungi(D.sh, PH_STRATO_PANNELLI, p);
    (void)tipo;
    return p;
}

static void alterna(int k, PhShellPopup tipo)
{
    ph_shell_popup_alterna(D.sh, tipo, D.popup[k], popup_chiuso, NULL);
}

static void launcher_alterna(void *io)
{
    (void)io;
    alterna(0, PH_POPUP_LAUNCHER);
}

static void tr_apri(void *io, PhSrvVerso verso)
{
    (void)io;
    if (verso == PH_SRV_INVIO)
        alterna(1, PH_POPUP_INVIO);
    else
        alterna(2, PH_POPUP_RICEZIONE);
}

/* ---- Gestore sistema finto: tre pulsanti semplici ------------------------ */

static void sis_clic(GtkButton *bt, gpointer dati)
{
    (void)bt;
    int k = GPOINTER_TO_INT(dati);
    static const PhShellPopup tipi[] = {PH_POPUP_VOLUME, PH_POPUP_TELEFONO, PH_POPUP_NOTIFICHE};
    alterna(3 + k, tipi[k]);
}

static GtkWidget *sis_pulsante(void *io, PhShellPopup tipo)
{
    (void)io;
    const PhShellIcona *icona;
    const char *nome;
    int k;
    switch (tipo) {
    case PH_POPUP_VOLUME: icona = &PH_ICONA_VOLUME; nome = "Volume"; k = 0; break;
    case PH_POPUP_TELEFONO: icona = &PH_ICONA_WIFI; nome = "Stato del telefono"; k = 1; break;
    case PH_POPUP_NOTIFICHE: icona = &PH_ICONA_CAMPANELLA; nome = "Notifiche"; k = 2; break;
    default: return NULL;
    }
    GtkWidget *bt = gtk_button_new();
    gtk_widget_add_css_class(bt, "ph-tb");
    gtk_button_set_child(GTK_BUTTON(bt), ph_shell_icona_nuova(icona));
    ph_shell_etichetta(bt, nome, NULL);
    g_signal_connect(bt, "clicked", G_CALLBACK(sis_clic), GINT_TO_POINTER(k));
    return bt;
}

static bool sis_nascosta(void *io, const char *chiave, uint64_t ora_ms)
{
    (void)io;
    (void)chiave;
    (void)ora_ms;
    return false;
}

/* ---- Trasferimenti finti e tasti ----------------------------------------- */

static void accoda(PhSrvVerso verso)
{
    PhErrore e;
    /* Un file che esiste di sicuro (il demo stesso); il backend finto non
     * scrive niente sul PC. */
    const char *origine = verso == PH_SRV_INVIO ? "/proc/self/exe" : "/storage/emulated/0/Download/prova.bin";
    const char *cartella = verso == PH_SRV_INVIO ? "/sdcard/Download" : g_get_tmp_dir();
    memset(&e, 0, sizeof e);
    if (ph_srv_tr_accoda(ph_shell_servizi(D.sh), verso, origine, cartella, &e) != 0)
        fprintf(stderr, "demo-barra: trasferimento rifiutato\n");
}

static gboolean tasto(GtkEventControllerKey *k, guint val, guint code, GdkModifierType st,
                      gpointer dati)
{
    (void)k;
    (void)code;
    (void)dati;
    bool maiusc = (st & GDK_SHIFT_MASK) != 0;
    if (val == GDK_KEY_F9) {
        accoda(maiusc ? PH_SRV_RICEZIONE : PH_SRV_INVIO);
        return TRUE;
    }
    if (val == GDK_KEY_F10) {
        if ((st & GDK_CONTROL_MASK) != 0)
            fin_chiudi_ultima();
        else
            fin_apri_prossima();
        return TRUE;
    }
    return FALSE;
}

/* Cerca il pulsante con la classe data (l'orologio) e lo "clicca". */
static GtkWidget *cerca_classe(GtkWidget *w, const char *classe)
{
    if (gtk_widget_has_css_class(w, classe))
        return w;
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        GtkWidget *t = cerca_classe(c, classe);
        if (t != NULL)
            return t;
    }
    return NULL;
}

static void clic_orologio_demo(void)
{
    GtkWidget *bt = cerca_classe(ph_shell_radice(D.sh), "ph-barra-orologio-bottone");
    if (bt != NULL)
        g_signal_emit_by_name(bt, "clicked");
}

/* Sequenza automatica per --prova: ogni pezzo reagisce a aperture, clic,
 * minimizzazioni, chiusure, popup e trasferimenti almeno una volta. */
static gboolean scossa(gpointer p)
{
    (void)p;
    switch (D.passo_prova++) {
    case 0: fin_apri_prossima(); break;
    case 1: fin_apri_prossima(); break;
    case 2: fin_clic_barra(NULL, D.v[D.n - 1].id); break; /* attiva -> minimizza */
    case 3: fin_clic_barra(NULL, D.v[D.n - 1].id); break; /* minimizzata -> ripristina */
    case 4: fin_clic_barra(NULL, D.v[0].id); break;       /* altrimenti -> davanti */
    case 5: launcher_alterna(NULL); break;
    case 6: tr_apri(NULL, PH_SRV_INVIO); break;           /* chiude il launcher, apre invio */
    case 7: ph_shell_popup_chiudi(D.sh); break;
    case 8: accoda(PH_SRV_INVIO); break;
    case 9: fin_chiudi_ultima(); break;
    case 10: fin_chiudi_ultima(); break;
    case 11: clic_orologio_demo(); break; /* apre il calendario */
    case 12: clic_orologio_demo(); break; /* lo richiude */
    default: return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static GtkWidget *cerca_ennesimo(GtkWidget *w, const char *classe, int *n)
{
    if (gtk_widget_has_css_class(w, classe) && (*n)-- == 0)
        return w;
    for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        GtkWidget *t = cerca_ennesimo(c, classe, n);
        if (t != NULL)
            return t;
    }
    return NULL;
}

/* Per le fotografie: apre il menu dell'N-esima icona simulando il tasto Menu. */
static void menu_icona_demo(int n)
{
    GtkWidget *bt = cerca_ennesimo(ph_shell_radice(D.sh), "ph-barra-voce-bottone", &n);
    if (bt == NULL)
        return;
    GListModel *m = gtk_widget_observe_controllers(bt);
    for (guint i = 0; i < g_list_model_get_n_items(m); i++) {
        GObject *c = g_list_model_get_item(m, i);
        if (GTK_IS_EVENT_CONTROLLER_KEY(c)) {
            gboolean r;
            g_signal_emit_by_name(c, "key-pressed", (guint)GDK_KEY_Menu, 0u, 0, &r);
        }
        g_object_unref(c);
    }
    g_object_unref(m);
}

/* 16 app fissate (il tetto) e qualche finestra aperta in piu': la barra deve
 * scorrere e lasciare sempre visibile la parte destra. Dopo il
 * collegamento, perche' il telefono finto rimette allora il suo profilo. */
static gboolean molte_demo(gpointer p)
{
    (void)p;
    static const char *const TUTTE[] = {
        "it.finto.messaggi", "it.finto.browser", "it.finto.note", "it.finto.foto",
        "it.finto.calcolatrice", "it.finto.mappe", "it.finto.musica", "it.finto.impostazioni",
        "it.finto.fotocamera", "it.finto.calendario", "it.finto.orologio", "it.finto.contatti",
        "it.finto.file", "it.finto.meteo", "it.finto.video", "it.finto.chiamate"};
    PhCfgTelefono *prof = ph_srv_profilo(ph_shell_servizi(D.sh));
    prof->n_fissate = 0;
    for (size_t i = 0; i < G_N_ELEMENTS(TUTTE); i++)
        ph_cfg_telefono_fissa_app(prof, TUTTE[i], NULL);
    fin_aggiungi("it.finto.banca", "Banca", false);
    fin_aggiungi("it.finto.chiamate", "Chiamate", false);
    ph_shell_emetti(D.sh, PH_SHELL_EV_FISSATE);
    return G_SOURCE_REMOVE;
}

static gboolean mostra_demo(gpointer p)
{
    (void)p;
    if (strncmp(MOSTRA, "molte", 5) == 0) {
        molte_demo(NULL);
        return G_SOURCE_REMOVE;
    }
    if (strcmp(MOSTRA, "calendario") == 0)
        clic_orologio_demo();
    else if (strncmp(MOSTRA, "menu:", 5) == 0)
        menu_icona_demo(atoi(MOSTRA + 5));
    return G_SOURCE_REMOVE;
}

/* Prova del vetro (fase 5): le tre superfici della barra hanno la classe giusta.
 * Il menu e il calendario sono costruiti con la barra; il popup si apre dopo. */
static int vetro_errori;

static GtkWidget *superficie_con(GtkWidget *radice, const char *classe)
{
    int n = 0;
    return cerca_ennesimo(radice, classe, &n);
}

static void controlla_vetro(PhShell *sh)
{
    GtkWidget *radice = ph_shell_radice(sh);
    static const struct {
        const char *classe;
        const char *vetro;
    } attese[] = {{"ph-barra-radice", "ph-vetro-scuro"},
                  {"ph-barra-calendario", "ph-vetro-chiaro"},
                  {"ph-menu", "ph-vetro-chiaro"}};
    for (size_t i = 0; i < G_N_ELEMENTS(attese); i++) {
        GtkWidget *w = superficie_con(radice, attese[i].classe);
        if (w == NULL || !gtk_widget_has_css_class(w, attese[i].vetro)) {
            fprintf(stderr, "demo-barra: %s senza la classe %s\n", attese[i].classe,
                    attese[i].vetro);
            vetro_errori++;
        }
    }
}

static void installa(PhShell *sh, PhFinto *f, void *u)
{
    (void)f;
    int argc_prova = GPOINTER_TO_INT(u);
    memset(&D, 0, sizeof D);
    D.sh = sh;

    /* Tre finestre di partenza (attiva, minimizzata, aperta) e lo specchio. */
    fin_aggiungi(APP[0].pacchetto, APP[0].nome, false);
    fin_aggiungi(APP[1].pacchetto, APP[1].nome, false);
    fin_aggiungi(APP[2].pacchetto, APP[2].nome, false);
    fin_aggiungi("", "Schermo del telefono", true);
    D.v[0].attiva = true;
    D.v[1].minimizzata = true;

    PhShellGestoreFinestre gf = {.io = NULL,
                                 .apri_app = fin_apri_app,
                                 .elenco = fin_elenco,
                                 .clic_barra = fin_clic_barra,
                                 .specchio = fin_specchio,
                                 .specchio_aperto = fin_specchio_aperto,
                                 .chiudi = fin_chiudi_id};
    ph_shell_registra_finestre(sh, &gf);
    PhShellGestoreLauncher gl = {.io = NULL, .alterna = launcher_alterna};
    ph_shell_registra_launcher(sh, &gl);
    PhShellGestoreTrasferimento gt = {.io = NULL, .apri = tr_apri};
    ph_shell_registra_trasferimento(sh, &gt);
    PhShellGestoreSistema gs = {.io = NULL, .pulsante = sis_pulsante, .nascosta = sis_nascosta};
    ph_shell_registra_sistema(sh, &gs);

    static const char *testi[] = {"Launcher (finto)", "Invio (finto)", "Ricezione (finta)",
                                  "Volume (finto)", "Telefono (finto)", "Notifiche (finte)"};
    static const PhShellPopup tipi[] = {PH_POPUP_LAUNCHER, PH_POPUP_INVIO, PH_POPUP_RICEZIONE,
                                        PH_POPUP_VOLUME, PH_POPUP_TELEFONO, PH_POPUP_NOTIFICHE};
    for (int i = 0; i < 6; i++)
        D.popup[i] = popup_finto(tipi[i], testi[i]);

    if (INGLESE)
        ph_lingua_imposta(PH_LINGUA_EN);
    ph_barra_installa(sh);
    if (argc_prova)
        controlla_vetro(sh);

    GtkEventController *k = gtk_event_controller_key_new();
    g_signal_connect(k, "key-pressed", G_CALLBACK(tasto), NULL);
    gtk_widget_add_controller(ph_shell_radice(sh), k);
    if (argc_prova)
        g_timeout_add(100, scossa, NULL);
    if (MOSTRA != NULL)
        g_timeout_add(400, mostra_demo, NULL);
}

int main(int argc, char **argv)
{
    int prova = 0;
    /* Opzione del solo demo: si toglie dagli argomenti prima di darli a
     * ph_finto_demo, che non la conosce. */
    char *args[64];
    int n = 0;
    for (int i = 0; i < argc && n < 63; i++) {
        if (strcmp(argv[i], "--mostra") == 0 && i + 1 < argc) {
            MOSTRA = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--en") == 0) {
            INGLESE = true;
            continue;
        }
        if (strcmp(argv[i], "--prova") == 0)
            prova = 1;
        args[n++] = argv[i];
    }
    args[n] = NULL;
    int r = ph_finto_demo(n, args, "YASDE 2.3 - demo barra", installa, GINT_TO_POINTER(prova));
    return r != 0 ? r : (vetro_errori != 0 ? 1 : 0);
}
