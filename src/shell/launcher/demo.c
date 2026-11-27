/* demo-launcher: il solo pezzo launcher sopra il telefono finto. Gli altri pezzi
 * non ci sono: qui due gestori finti, uno per le finestre (scrive su stderr
 * l'app aperta) e uno per il desktop (un insieme di pacchetti in memoria), e
 * il launcher si apre da solo all'avvio. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell/finto/finto.h"
#include "prova_demo.h"
#include "shell/pezzi.h"

typedef struct {
    PhShell *sh;
    GHashTable *sul_desktop; /* pacchetto -> 1 */
    GHashTable *fissate;     /* pacchetto -> 1 */
} Finti;

static void finti_apri_app(void *io, const char *pacchetto)
{
    (void)io;
    fprintf(stderr, "demo: apri app %s\n", pacchetto);
}

static bool finti_sul_desktop(void *io, const char *pacchetto)
{
    Finti *f = io;
    return g_hash_table_contains(f->sul_desktop, pacchetto);
}

static void finti_aggiungi(void *io, const char *pacchetto)
{
    Finti *f = io;
    g_hash_table_add(f->sul_desktop, g_strdup(pacchetto));
    fprintf(stderr, "demo: aggiunta al desktop %s\n", pacchetto);
}

static void finti_rimuovi(void *io, const char *pacchetto)
{
    Finti *f = io;
    g_hash_table_remove(f->sul_desktop, pacchetto);
    fprintf(stderr, "demo: rimossa dal desktop %s\n", pacchetto);
}

static bool finti_fissata(void *io, const char *pacchetto)
{
    Finti *f = io;
    return g_hash_table_contains(f->fissate, pacchetto);
}

static void finti_fissa(void *io, const char *pacchetto, bool fissa)
{
    Finti *f = io;
    if (fissa)
        g_hash_table_add(f->fissate, g_strdup(pacchetto));
    else
        g_hash_table_remove(f->fissate, pacchetto);
    fprintf(stderr, "demo: %s %s\n", fissa ? "fissata sulla barra" : "tolta dalla barra", pacchetto);
    ph_shell_emetti(f->sh, PH_SHELL_EV_FISSATE);
}

static void finti_distrutti(GtkWidget *w, gpointer dati)
{
    (void)w;
    Finti *f = dati;
    g_hash_table_unref(f->sul_desktop);
    g_hash_table_unref(f->fissate);
    g_free(f);
}

/* Opzioni del demo per le foto (lo schermo invisibile non ha mouse ne'
 * tastiera): --cerca TESTO scrive nel campo, --menu apre il menu di una app. */
static const char *opz_cerca;
static bool opz_menu;
static bool opz_prova;
static int esito_prova; /* controlli a schermo falliti */

static void apri_launcher(void *dati)
{
    ph_shell_launcher_alterna(dati);
}

static void dopo_apertura(void *dati)
{
    if (opz_cerca != NULL)
        ph_launcher_demo_cerca(dati, opz_cerca);
    if (opz_menu)
        ph_launcher_demo_menu(dati);
}

static gboolean dopo_apertura_a_tempo(gpointer dati)
{
    dopo_apertura(dati);
    return G_SOURCE_REMOVE;
}

/* Con --prova: misure e clic fuori a launcher aperto e disposto, e reazione alla
 * scossa del telefono finto.
 *
 * Perche' non basta un controllo a tempo fisso: il demo apre il launcher prima
 * che la finestra abbia avuto la prima disposizione; ph_shell_area vale 0 finche'
 * la radice non e' disposta, quindi le misure calcolate all'apertura sono quelle
 * con area 0 (riquadro 234x144 con margine 24). L'avviso di area parte da
 * strati_allocate con consegna differita e le misure giuste arrivano un po'
 * dopo. Non e' un difetto del launcher. Si controlla quindi di nuovo,
 * ogni 100 ms (senza dormire), fino a una scadenza ASSOLUTA (ora monotona,
 * l'85 % della vita del demo, --prova MS). Un tick callback sul fotogramma non
 * e' affidabile qui: con la scena ferma non arrivano fotogrammi.
 *
 * La scossa del telefono finto (finto/comandi.c: notifica, blocco, perdita
 * della connessione per 1 s, schermo spento e riacceso) serve a far reagire
 * il launcher a PH_SRV_EV_COLLEGAMENTO (elenco_rileggi). I tempi non sono
 * fissi: sotto carico ritardano, e il ritorno della connessione parte da un
 * timer del telefono finto. Quindi NON si chiude a un'ora data: si ascolta
 * PH_SRV_EV_COLLEGAMENTO e si aspetta di aver visto PERSO e poi di nuovo
 * COLLEGATO (e' l'ultimo evento visibile ai servizi: lo schermo spento e
 * riacceso del finto cade di norma DENTRO la perdita, dove `schermo_spento` vale
 * false perche' ha_telefono e' false, e a riconnessione e' gia' false, quindi
 * non si puo' osservare da qui senza toccare finto/); SOLO DOPO l'ultimo evento
 * la verifica delle misure deve riuscire (vale anche dopo la rilettura
 * dell'elenco) e allora si chiude. Se alla scadenza qualcosa manca, la prova
 * FALLISCE e dice che cosa. Il fallimento visto una volta in `tutto` resta
 * un'IPOTESI (scadenza corta sotto carico); servizi.c non svuota l'elenco
 * delle app alla perdita della connessione ne' a schermo spento. Se
 * ricomparisse, guardare i valori stampati alla scadenza. */
static int vita_ms = 2000;
static bool verificato;
static gint64 scadenza_us;

typedef struct {
    PhServizi *srv;
    GtkWidget *radice;
    guint id_avviso, id_giro; /* 0 = non attivo */
    bool perso, ricollegato;
} Prova;
static Prova prova;

static bool eventi_completi(void)
{
    return prova.perso && prova.ricollegato;
}

static void avviso_collegamento(PhServizi *s, PhSrvEvento ev, const char *arg, void *utente)
{
    (void)ev;
    (void)arg;
    (void)utente;
    PhSrvCollegamento c;
    ph_srv_collegamento(s, &c);
    if (c.stato == PH_SRV_PERSO)
        prova.perso = true;
    else if (c.stato == PH_SRV_COLLEGATO && prova.perso)
        prova.ricollegato = true;
}

static void prova_distrutta(GtkWidget *w, gpointer dati)
{
    (void)w;
    (void)dati;
    /* La radice muore (fine della prova o chiusura a mano): niente piu' ascolti
     * ne' giri, non devono toccare una shell liberata. */
    if (prova.id_avviso != 0)
        ph_srv_smetti(prova.srv, prova.id_avviso);
    if (prova.id_giro != 0)
        g_source_remove(prova.id_giro);
    prova.id_avviso = prova.id_giro = 0;
}

static gboolean verifica_periodica(gpointer dati)
{
    int larg = 0;
    ph_shell_area(dati, &larg, NULL);
    bool scaduta = g_get_monotonic_time() >= scadenza_us;
    if (!scaduta && (larg <= 0 || !eventi_completi()))
        return G_SOURCE_CONTINUE; /* area non nota o scossa non finita */
    if (scaduta && !eventi_completi()) {
        fprintf(stderr, "prova a schermo: la scossa non e' finita: perso %d, ricollegato %d\n",
                prova.perso, prova.ricollegato);
        esito_prova = 1;
        prova.id_giro = 0;
        return G_SOURCE_REMOVE;
    }
    /* L'area e' piccola apposta (--misura nel meson.build): le app non ci
     * stanno e la griglia deve scorrere. */
    int e = ph_launcher_demo_verifica(dati, scaduta, true);
    if (e == 0) {
        verificato = true;
        prova.id_giro = 0;
        GtkRoot *finestra = gtk_widget_get_root(prova.radice);
        if (GTK_IS_WINDOW(finestra))
            gtk_window_close(GTK_WINDOW(finestra));
        return G_SOURCE_REMOVE;
    }
    if (scaduta) {
        esito_prova = e;
        prova.id_giro = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void installa(PhShell *sh, PhFinto *fin, void *u)
{
    (void)fin;
    (void)u;
    Finti *f = g_new0(Finti, 1);
    f->sh = sh;
    f->sul_desktop = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    f->fissate = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    PhShellGestoreFissate gx = {.io = f, .fissata = finti_fissata, .fissa = finti_fissa};
    ph_shell_registra_fissate(sh, &gx);
    PhShellGestoreFinestre gf = {.io = f, .apri_app = finti_apri_app};
    ph_shell_registra_finestre(sh, &gf);
    PhShellGestoreDesktop gd = {.io = f, .sul_desktop = finti_sul_desktop,
                                .aggiungi = finti_aggiungi, .rimuovi = finti_rimuovi};
    ph_shell_registra_desktop(sh, &gd);
    g_signal_connect(ph_shell_radice(sh), "destroy", G_CALLBACK(finti_distrutti), f);

    ph_launcher_installa(sh);
    /* Aperto all'avvio, dopo che la finestra e' stata mostrata. */
    ph_shell_consegna(apri_launcher, sh, NULL);
    /* Le celle compaiono dopo l'elenco e il primo disegno. */
    if (opz_cerca != NULL || opz_menu)
        g_timeout_add(900, dopo_apertura_a_tempo, sh);
    if (opz_prova) {
        ph_launcher_demo_sonda(sh);
        prova.srv = ph_shell_servizi(sh);
        prova.radice = ph_shell_radice(sh);
        /* Dopo il launcher (installato sopra): il suo avviso gira per primo. */
        prova.id_avviso = ph_srv_ascolta(prova.srv, PH_SRV_EV_COLLEGAMENTO, avviso_collegamento, NULL);
        scadenza_us = g_get_monotonic_time() + (gint64)vita_ms * 850;
        prova.id_giro = g_timeout_add(100, verifica_periodica, sh);
        g_signal_connect(prova.radice, "destroy", G_CALLBACK(prova_distrutta), NULL);
    }
}

int main(int argc, char **argv)
{
    int n = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cerca") == 0 && i + 1 < argc)
            opz_cerca = argv[++i];
        else if (strcmp(argv[i], "--menu") == 0)
            opz_menu = true;
        else {
            if (strcmp(argv[i], "--prova") == 0) {
                opz_prova = true;
                if (i + 1 < argc && atoi(argv[i + 1]) > 0)
                    vita_ms = atoi(argv[i + 1]);
            }
            argv[n++] = argv[i];
        }
    }
    argc = n;
    if (opz_prova) /* nessun avviso di GTK (per esempio di misura della griglia) deve comparire */
        g_log_set_always_fatal(
            (GLogLevelFlags)(G_LOG_FATAL_MASK | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL));
    int r = ph_finto_demo(argc, argv, "YASDE 2.3 - demo launcher", installa, NULL);
    if (r == 0 && opz_prova && !verificato) {
        fprintf(stderr, "prova a schermo: la verifica non e' mai riuscita (%d controlli falliti)\n",
                esito_prova);
        return 1;
    }
    return r;
}
