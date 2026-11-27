/* Pezzo shell-ossatura (F-01, 4.8, 4.14): applicazione GTK a istanza unica,
 * finestra principale, servizi veri, chiusura ordinata. */
#include <glib-unix.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "config/lingua.h"
#include "logica.h"
#include "ossatura.h"
#include "pezzi_presenti.h"
#include "shell/pezzi.h"
#include "shell/servizi_backend.h"

/* Generata da glib-compile-resources (--manual-register): il CSS del pezzo. */
void ph_risorse_ossatura_register_resource(void);

void ph_ossatura_installa_pezzi(PhShell *sh)
{
    static gsize risorse = 0;
    if (g_once_init_enter(&risorse)) {
        ph_risorse_ossatura_register_resource();
        g_once_init_leave(&risorse, 1);
    }
    ph_shell_stile_carica("/it/yasde/shell/ossatura.css");
#if PH_SHELL_HA_DESKTOP
    ph_desktop_installa(sh);
#endif
#if PH_SHELL_HA_FINESTRA_APP
    ph_finestra_app_installa(sh);
#endif
#if PH_SHELL_HA_SISTEMA
    ph_sistema_installa(sh);
#endif
#if PH_SHELL_HA_LAUNCHER
    ph_launcher_installa(sh);
#endif
#if PH_SHELL_HA_TRASFERIMENTO
    ph_trasferimento_installa(sh);
#endif
#if PH_SHELL_HA_BARRA
    ph_barra_installa(sh);
#endif
    (void)sh;
}

/* ---- Applicazione ------------------------------------------------------------- */
typedef struct {
    GtkApplication *app;
    PhCfgPreferenze pref;
    bool pref_salvabile;
    PhServizi *servizi;
    PhShell *sh;
    GtkWindow *win;
    bool chiuso;
    int uscita;
} Ossatura;


static void svuota_coda_chiusura(void)
{
    while (g_main_context_pending(NULL))
        g_main_context_iteration(NULL, FALSE);
}

void ph_ossatura_chiudi(GtkWindow *win, PhShell *sh, PhOssaturaLiberaServizi libera_servizi, void *utente)
{
    if (win != NULL) {
        gtk_widget_set_visible(GTK_WIDGET(win), FALSE);
        ph_finestra_distacca(win);
        gtk_window_destroy(win);
        svuota_coda_chiusura();
    }
    if (sh != NULL) {
        ph_shell_libera(sh);
        svuota_coda_chiusura();
    }
    if (libera_servizi != NULL) {
        libera_servizi(utente);
        svuota_coda_chiusura();
    }
}

static void libera_servizi_app(void *utente);

/* Chiusura ordinata (una volta sola): la finestra si distrugge (i pezzi
 * agganciano la pulizia alla distruzione della radice e smettono di ascoltare
 * i servizi), POI si libera PhShell (i flussi si chiudono), POI i servizi (il
 * collegamento chiude e il telefono torna com'era grazie ai guardiani). Qui il
 * thread GTK puo' aspettare qualche secondo: la finestra non c'e' piu'. */
static void chiudi_tutto(Ossatura *o)
{
    if (o->chiuso)
        return;
    o->chiuso = true;
    GtkWindow *win = o->win; /* il puntatore debole lo azzera alla distruzione */
    PhShell *sh = o->sh;
    o->sh = NULL;
    ph_ossatura_chiudi(win, sh, libera_servizi_app, o);
}

static void libera_servizi_app(void *utente)
{
    Ossatura *o = utente;
    if (o->servizi != NULL) {
        ph_servizi_libera(o->servizi);
        o->servizi = NULL;
    }
}

static gboolean su_chiusura_finestra(GtkWindow *w, gpointer dati)
{
    (void)w;
    chiudi_tutto(dati);
    return TRUE; /* la finestra l'ha gia' distrutta chiudi_tutto */
}

/* SIGTERM e SIGINT: passano da g_application_quit, quindi da chiudi_tutto, e il
 * telefono torna com'era subito (i guardiani restano la rete di sicurezza). */
static gboolean su_segnale(gpointer app)
{
    fprintf(stderr, "YASDE 2.2: segnale di chiusura ricevuto\n");
    g_application_quit(G_APPLICATION(app));
    return G_SOURCE_REMOVE;
}

/* Solo l'istanza principale, una volta, prima dei thread: un secondo avvio non
 * deve pulire i temporanei dell'istanza gia' aperta. */
static void su_avvio(GApplication *a, gpointer dati)
{
    (void)dati;
    g_unix_signal_add(SIGTERM, su_segnale, a);
    g_unix_signal_add(SIGINT, su_segnale, a);
    (void)ph_cfg_pulisci_temporanei(3600, NULL);
    int r = ph_cfg_pulisci_sfondi(NULL);
    if (r == -2)
        fprintf(stderr, "YASDE 2.2: un profilo illeggibile: nessuna immagine di sfondo tolta\n");
    else if (r < 0)
        fprintf(stderr, "YASDE 2.2: pulizia delle immagini di sfondo non riuscita\n");
}

static void su_attiva(GApplication *a, gpointer dati)
{
    (void)a;
    Ossatura *o = dati;
    /* Secondo avvio: la finestra esiste, si porta in primo piano (e nessuna
     * seconda connessione). */
    if (o->win != NULL) {
        gtk_window_present(o->win);
        return;
    }
    PhErrore e = {{0}};
    o->servizi = ph_ossatura_servizi_veri(&o->pref, o->pref_salvabile, &e);
    if (o->servizi == NULL) {
        /* Il dettaglio puo' contenere percorsi della cartella personale: non si stampa. */
        fprintf(stderr, "YASDE 2.2: i servizi non si sono avviati\n");
        o->uscita = 1;
        g_application_quit(a);
        return;
    }
    o->sh = ph_shell_nuova(o->servizi);
    o->win = ph_finestra_nuova(o->app, o->sh);
    /* Se la finestra viene distrutta prima della chiusura ordinata (uscita
     * dell'applicazione) il puntatore si azzera da solo. */
    g_object_add_weak_pointer(G_OBJECT(o->win), (gpointer *)&o->win);
    g_signal_connect(o->win, "close-request", G_CALLBACK(su_chiusura_finestra), o);
    ph_ossatura_installa_pezzi(o->sh);
    ph_appunti_installa(o->sh);
    gtk_window_present(o->win);
}

static void su_fine(GApplication *a, gpointer dati)
{
    (void)a;
    /* Chiusura senza passare dal pulsante (segnale di sistema, quit). */
    chiudi_tutto(dati);
}

int ph_ossatura_esegui(int argc, char **argv)
{
    Ossatura o = {0};

    /* Prima di creare thread: preferenze, lingua, temporanei vecchi. */
    PhErrore e = {{0}};
    ph_cfg_preferenze_predefinite(&o.pref);
    int r = ph_cfg_preferenze_carica(&o.pref, &e);
    o.pref_salvabile = r != -1;
    if (r == 1)
        fprintf(stderr, "YASDE 2.2: preferenze illeggibili messe da parte, uso i valori predefiniti\n");
    else if (r == -1)
        fprintf(stderr, "YASDE 2.2: preferenze non lette, uso i valori predefiniti e non li salvo\n");
    if (r == -1)
        ph_cfg_preferenze_predefinite(&o.pref);
    ph_lingua_inizializza(o.pref.lingua);
    /* Scala della shell scelta dall'utente: GTK4 non ha una scala frazionaria
     * dei widget, si applica ai caratteri (GDK_DPI_SCALE, prima di gtk_init). */
    if (!o.pref.scala_automatica) {
        char s[32];
        ph_ossatura_scala_testo(o.pref.scala, s, sizeof s);
        g_setenv("GDK_DPI_SCALE", s, TRUE);
    }

    o.app = gtk_application_new("it.yasde.YASDE", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(o.app, "startup", G_CALLBACK(su_avvio), &o);
    g_signal_connect(o.app, "activate", G_CALLBACK(su_attiva), &o);
    g_signal_connect(o.app, "shutdown", G_CALLBACK(su_fine), &o);
    int stato = g_application_run(G_APPLICATION(o.app), argc, argv);
    chiudi_tutto(&o);
    g_object_unref(o.app);
    fprintf(stderr, "YASDE 2.2: chiusura pulita\n");
    return stato != 0 ? stato : o.uscita;
}
