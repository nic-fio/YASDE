/* Finestrella dei comandi del telefono finto e programma dimostrativo
 * (finto.h). I testi qui sono per chi sviluppa, non per l'utente: niente
 * traduzioni. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interno.h"

/* ---- Finestrella dei comandi ------------------------------------------------ */
typedef struct {
    PhFinto *f;
    bool bloccato, illeggibili;
    int meteo;
} Comandi;

/* Interruttori delle aggiunte del 23 nov: il pulsante alterna lo stato. */
typedef struct {
    Comandi *c;
    void (*fn)(PhFinto *f, bool si);
    const char *spento, *acceso;
    bool si;
} Interruttore;

static void c_interruttore(GtkButton *b, gpointer p)
{
    Interruttore *i = p;
    i->si = !i->si;
    i->fn(i->c->f, i->si);
    gtk_button_set_label(b, i->si ? i->acceso : i->spento);
}

static void pannello_chiamata(PhFinto *f, bool si)
{
    ph_finto_pannello_risposta(f, si ? PH_SRV_PAN_CHIAMATA : PH_SRV_PAN_FATTO);
}

static void modo_voce(PhFinto *f, PhFintoVoce v, bool si, PhFintoModo modo)
{
    ph_finto_voce_modo(f, v, si ? modo : PH_FINTO_MODO_NORMALE);
}
static void cal_non_disp(PhFinto *f, bool si) { modo_voce(f, PH_FINTO_VOCE_CALENDARIO_MESE, si, PH_FINTO_MODO_NON_DISPONIBILE); }
static void sv_non_disp(PhFinto *f, bool si) { modo_voce(f, PH_FINTO_VOCE_SVEGLIA, si, PH_FINTO_MODO_NON_DISPONIBILE); }
static void sv_errore(PhFinto *f, bool si) { modo_voce(f, PH_FINTO_VOCE_SVEGLIA, si, PH_FINTO_MODO_ERRORE); }
static void bat_non_disp(PhFinto *f, bool si) { modo_voce(f, PH_FINTO_VOCE_BATTERIA, si, PH_FINTO_MODO_NON_DISPONIBILE); }
static void bat_errore(PhFinto *f, bool si) { modo_voce(f, PH_FINTO_VOCE_BATTERIA, si, PH_FINTO_MODO_ERRORE); }
static void ctl_non_disp(PhFinto *f, bool si) { modo_voce(f, PH_FINTO_VOCE_CONTROLLI, si, PH_FINTO_MODO_NON_DISPONIBILE); }
static void ctl_rifiuta(PhFinto *f, bool si) { modo_voce(f, PH_FINTO_VOCE_CONTROLLI, si, PH_FINTO_MODO_ERRORE); }
static void torcia_manca(PhFinto *f, bool si) { ph_finto_torcia_disponibile(f, !si); }
static void tel_silenzioso(PhFinto *f, bool si)
{
    ph_finto_controllo_dal_telefono(f, PH_SRV_CTL_SUONERIA, si ? PH_SRV_SUONERIA_SILENZIOSO : PH_SRV_SUONERIA_SUONA);
}
static void tel_non_disturbare(PhFinto *f, bool si) { ph_finto_controllo_dal_telefono(f, PH_SRV_CTL_NON_DISTURBARE, si); }
static void tel_torcia(PhFinto *f, bool si) { ph_finto_controllo_dal_telefono(f, PH_SRV_CTL_TORCIA, si); }

static void meteo_modo(PhFinto *f, PhFintoMeteoModo m, bool si) { ph_finto_meteo_modo(f, si ? m : PH_FINTO_METEO_PRONTO); }
static void meteo_non_disp(PhFinto *f, bool si) { meteo_modo(f, PH_FINTO_METEO_NON_DISPONIBILE, si); }
static void meteo_senza_pos(PhFinto *f, bool si) { meteo_modo(f, PH_FINTO_METEO_SENZA_POSIZIONE, si); }
static void meteo_non_coll(PhFinto *f, bool si) { meteo_modo(f, PH_FINTO_METEO_NON_COLLEGATO, si); }
static void meteo_attesa(PhFinto *f, bool si) { meteo_modo(f, PH_FINTO_METEO_IN_ATTESA, si); }
static void c_meteo_condizioni(GtkButton *b, gpointer p)
{
    Comandi *c = p;
    c->meteo = (c->meteo + 1) % PH_FINTO_METEO_SCENARI;
    ph_finto_meteo_condizioni(c->f, c->meteo);
    char *t = g_strdup_printf("Meteo: condizioni %d di %d (cambia)", c->meteo, PH_FINTO_METEO_SCENARI - 1);
    gtk_button_set_label(b, t);
    g_free(t);
}
static void c_meteo_nuovi(GtkButton *b, gpointer p) { (void)b; ph_finto_meteo_nuovi_dati(((Comandi *)p)->f); }

static void ricerca_errore(PhFinto *f, bool si) { ph_finto_meteo_ricerca_errore(f, si); }

static void interruttore(GtkWidget *box, Comandi *c, void (*fn)(PhFinto *, bool), const char *spento,
                         const char *acceso)
{
    Interruttore *i = g_new0(Interruttore, 1);
    i->c = c;
    i->fn = fn;
    i->spento = spento;
    i->acceso = acceso;
    GtkWidget *b = gtk_button_new_with_label(spento);
    g_object_set_data_full(G_OBJECT(b), "ph-interruttore", i, g_free);
    g_signal_connect(b, "clicked", G_CALLBACK(c_interruttore), i);
    gtk_box_append(GTK_BOX(box), b);
}

static void c_blocca(GtkButton *b, gpointer p)
{
    Comandi *c = p;
    c->bloccato = !c->bloccato;
    ph_finto_blocca(c->f, c->bloccato);
    gtk_button_set_label(b, c->bloccato ? "Sblocca il telefono (F5)" : "Blocca il telefono (F5)");
}

static void c_perdi(GtkButton *b, gpointer p) { (void)b; ph_finto_perdi(((Comandi *)p)->f, 5); }
static void c_notifica(GtkButton *b, gpointer p) { (void)b; ph_finto_notifica_nuova(((Comandi *)p)->f); }
static void c_aggiorna(GtkButton *b, gpointer p) { (void)b; ph_finto_notifica_aggiorna(((Comandi *)p)->f); }
static void c_batteria_bassa(GtkButton *b, gpointer p) { (void)b; ph_finto_batteria(((Comandi *)p)->f, 15); }
static void c_batteria_alta(GtkButton *b, gpointer p) { (void)b; ph_finto_batteria(((Comandi *)p)->f, 78); }
static void c_chiudi_app(GtkButton *b, gpointer p) { (void)b; ph_finto_chiudi_app_da_sola(((Comandi *)p)->f); }
static void c_appunti(GtkButton *b, gpointer p) { (void)b; ph_finto_appunti_dal_telefono(((Comandi *)p)->f, NULL); }

static void c_illeggibili(GtkButton *b, gpointer p)
{
    Comandi *c = p;
    c->illeggibili = !c->illeggibili;
    ph_finto_notifiche_illeggibili(c->f, c->illeggibili);
    gtk_button_set_label(b, c->illeggibili ? "Notifiche di nuovo leggibili" : "Notifiche illeggibili");
}

static void aggiungi(GtkWidget *box, const char *testo, GCallback cb, Comandi *c)
{
    GtkWidget *b = gtk_button_new_with_label(testo);
    g_signal_connect(b, "clicked", cb, c);
    gtk_box_append(GTK_BOX(box), b);
}

GtkWidget *ph_finto_comandi_nuovi(PhFinto *f)
{
    Comandi *c = g_new0(Comandi, 1);
    c->f = f;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    g_object_set_data_full(G_OBJECT(box), "ph-comandi", c, g_free);
    aggiungi(box, "Blocca il telefono (F5)", G_CALLBACK(c_blocca), c);
    aggiungi(box, "Connessione persa per 5 s (F6)", G_CALLBACK(c_perdi), c);
    aggiungi(box, "Notifica nuova (F7)", G_CALLBACK(c_notifica), c);
    aggiungi(box, "Aggiorna l'ultima notifica", G_CALLBACK(c_aggiorna), c);
    aggiungi(box, "Notifiche illeggibili", G_CALLBACK(c_illeggibili), c);
    aggiungi(box, "Batteria al 15%", G_CALLBACK(c_batteria_bassa), c);
    aggiungi(box, "Batteria al 78%", G_CALLBACK(c_batteria_alta), c);
    aggiungi(box, "L'ultima app si chiude da sola (F8)", G_CALLBACK(c_chiudi_app), c);
    aggiungi(box, "Testo negli appunti del telefono", G_CALLBACK(c_appunti), c);
    interruttore(box, c, pannello_chiamata, "Pannello: rifiuta (chiamata in corso)", "Pannello: accetta di nuovo");
    interruttore(box, c, ph_finto_installazioni_rifiutate, "Installazioni rifiutate", "Installazioni accettate");
    interruttore(box, c, ph_finto_calendario_illeggibile, "Calendario illeggibile", "Calendario leggibile");
    interruttore(box, c, ph_finto_prima_lettura_vuota, "Prima lettura notifiche vuota",
                 "Prima lettura notifiche normale");
    interruttore(box, c, ph_finto_schermo_spento, "Schermo spento dal sistema", "Schermo riacceso");
    interruttore(box, c, ph_finto_registrazione_guasta, "Registrazione guasta", "Registrazione normale");
    interruttore(box, c, cal_non_disp, "Calendario del mese non disponibile", "Calendario del mese disponibile");
    interruttore(box, c, ph_finto_sveglia_assente, "Nessuna sveglia", "Sveglia domani alle 07:30");
    interruttore(box, c, sv_non_disp, "Sveglia non disponibile", "Sveglia disponibile");
    interruttore(box, c, sv_errore, "Sveglia in errore", "Sveglia senza errore");
    interruttore(box, c, ph_finto_in_carica, "Batteria in carica", "Batteria in scarica");
    interruttore(box, c, bat_non_disp, "Batteria in dettaglio non disponibile", "Batteria in dettaglio disponibile");
    interruttore(box, c, bat_errore, "Batteria in dettaglio in errore", "Batteria in dettaglio senza errore");
    interruttore(box, c, ctl_non_disp, "Controlli non disponibili", "Controlli disponibili");
    interruttore(box, c, torcia_manca, "Torcia non disponibile", "Torcia disponibile");
    interruttore(box, c, ctl_rifiuta, "Rifiuta i controlli", "Accetta i controlli");
    interruttore(box, c, tel_silenzioso, "Dal telefono: silenzioso", "Dal telefono: suoneria attiva");
    interruttore(box, c, tel_non_disturbare, "Dal telefono: Non disturbare acceso", "Dal telefono: Non disturbare spento");
    interruttore(box, c, tel_torcia, "Dal telefono: torcia accesa", "Dal telefono: torcia spenta");
    interruttore(box, c, meteo_non_disp, "Meteo non disponibile", "Meteo disponibile");
    interruttore(box, c, meteo_senza_pos, "Posizione spenta", "Posizione accesa");
    interruttore(box, c, meteo_non_coll, "Meteo: nessun telefono", "Meteo: telefono collegato");
    interruttore(box, c, ph_finto_meteo_lento, "Meteo lento (10 s alla prima risposta)", "Meteo veloce");
    interruttore(box, c, ricerca_errore, "Ricerca luoghi in errore", "Ricerca luoghi normale");
    interruttore(box, c, ph_finto_meteo_ricerca_lenta, "Ricerca luoghi lenta (10 s)", "Ricerca luoghi veloce");
    interruttore(box, c, meteo_attesa, "Meteo in attesa (lento)", "Meteo: risposte normali");
    aggiungi(box, "Meteo: condizioni (cambia)", G_CALLBACK(c_meteo_condizioni), c);
    aggiungi(box, "Meteo: arrivano dati nuovi", G_CALLBACK(c_meteo_nuovi), c);
    interruttore(box, c, ph_finto_telefono_raggiungibile, "Telefono visibile in rete", "Telefono non visibile in rete");
    return box;
}

/* ---- Programma dimostrativo -------------------------------------------------- */
typedef struct {
    PhFinto *f;
    GMainLoop *ciclo;
    bool bloccato;
    guint scossa, fine;
    GtkWidget *win;
} Demo;

static gboolean tasto(GtkEventControllerKey *k, guint t, guint codice, GdkModifierType stato, gpointer p)
{
    (void)k;
    (void)codice;
    (void)stato;
    Demo *d = p;
    switch (t) {
    case GDK_KEY_F5:
        d->bloccato = !d->bloccato;
        ph_finto_blocca(d->f, d->bloccato);
        return TRUE;
    case GDK_KEY_F6:
        ph_finto_perdi(d->f, 5);
        return TRUE;
    case GDK_KEY_F7:
        ph_finto_notifica_nuova(d->f);
        return TRUE;
    case GDK_KEY_F8:
        ph_finto_chiudi_app_da_sola(d->f);
        return TRUE;
    default:
        return FALSE;
    }
}

/* Chiusura chiesta dall'utente: si esce dal ciclo e si TIENE la finestra viva (TRUE): la
 * distrugge una volta sola il codice dopo il ciclo. Con FALSE GTK la distruggerebbe
 * qui e poi la si distruggerebbe una seconda volta (critico fatale). */
static gboolean chiusa(GtkWindow *w, gpointer p)
{
    (void)w;
    g_main_loop_quit(((Demo *)p)->ciclo);
    return TRUE;
}

static gboolean fine_prova(gpointer p)
{
    Demo *d = p;
    d->fine = 0;
    /* Come se l'utente chiudesse la finestra: passa dal gestore di close-request (che deve
     * lasciarla viva per la distruzione unica dopo il ciclo). */
    gtk_window_close(GTK_WINDOW(d->win));
    return G_SOURCE_REMOVE;
}

/* Nella prova automatica si fa succedere un po' di tutto, cosi' ogni pezzo
 * reagisce almeno una volta a blocco, caduta e notifiche. */
static int passo_prova;
static gboolean scossa(gpointer p)
{
    Demo *d = p;
    switch (passo_prova++) {
    case 0: ph_finto_notifica_nuova(d->f); break;
    case 1: ph_finto_blocca(d->f, true); break;
    case 2: ph_finto_blocca(d->f, false); break;
    case 3: ph_finto_perdi(d->f, 1); break;
    case 4: ph_finto_notifica_aggiorna(d->f); break;
    case 5: ph_finto_schermo_spento(d->f, true); break;
    case 6: ph_finto_schermo_spento(d->f, false); break;
    default: d->scossa = 0; return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

int ph_finto_demo(int argc, char **argv, const char *titolo, PhFintoInstalla installa, void *utente)
{
    int larg = 1280, alt = 800, prova_ms = -1;
    bool comandi = false, dmabuf = false;
    int inizio = PH_FINTO_INIZIO_COLLEGATO;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--misura") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &larg, &alt) != 2 || larg < 320 || alt < 240) {
                fprintf(stderr, "--misura: atteso LARGHEZZAxALTEZZA\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--comandi") == 0) {
            comandi = true;
        } else if (strcmp(argv[i], "--dmabuf") == 0) {
            dmabuf = true;
        } else if (strcmp(argv[i], "--stato") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            if (strcmp(v, "collegato") == 0)
                inizio = PH_FINTO_INIZIO_COLLEGATO;
            else if (strcmp(v, "non-abbinato") == 0)
                inizio = PH_FINTO_INIZIO_NON_ABBINATO;
            else if (strcmp(v, "cerca-noto") == 0)
                inizio = PH_FINTO_INIZIO_CERCA_NOTO;
            else if (strcmp(v, "cerca-nuovo") == 0)
                inizio = PH_FINTO_INIZIO_CERCA_NUOVO;
            else {
                fprintf(stderr, "--stato: collegato, non-abbinato, cerca-noto o cerca-nuovo\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--prova") == 0 && i + 1 < argc) {
            prova_ms = atoi(argv[++i]);
        } else {
            fprintf(stderr, "uso: %s [--misura LxA] [--comandi] [--dmabuf] [--stato S] [--prova MS]\n", argv[0]);
            return 2;
        }
    }
    if (!gtk_init_check()) {
        if (prova_ms >= 0) {
            fprintf(stderr, "nessuno schermo: prova saltata\n");
            return 77; /* meson: test saltato */
        }
        fprintf(stderr, "nessuno schermo disponibile\n");
        return 1;
    }
    PhFintoOpzioni o = {.dmabuf = dmabuf, .notifiche_ogni_s = prova_ms >= 0 ? -1 : 0,
                        .ritardo_ms = prova_ms >= 0 ? 20 : 0, .inizio = inizio};
    Demo d = {0};
    d.f = ph_finto_nuovo(&o);
    if (d.f == NULL)
        return 1;
    PhShell *sh = ph_shell_nuova(ph_finto_servizi(d.f));

    /* Sfondo predefinito del mockup sotto a tutto: il desktop, se c'e', mette
     * il suo sopra (stesso strato, aggiunto dopo). */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, ".ph-finto-sfondo { background-image: linear-gradient(135deg, #0e2638, #1a625f); }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    GtkWidget *sfondo = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sfondo, "ph-finto-sfondo");
    gtk_widget_set_hexpand(sfondo, TRUE);
    gtk_widget_set_vexpand(sfondo, TRUE);
    ph_shell_strato_aggiungi(sh, PH_STRATO_DESKTOP, sfondo);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), titolo != NULL ? titolo : "YASDE 2.1 - demo");
    gtk_window_set_default_size(GTK_WINDOW(win), larg, alt);
    gtk_window_set_child(GTK_WINDOW(win), ph_shell_radice(sh));
    GtkEventController *k = gtk_event_controller_key_new();
    g_signal_connect(k, "key-pressed", G_CALLBACK(tasto), &d);
    gtk_widget_add_controller(win, k);
    d.ciclo = g_main_loop_new(NULL, FALSE);
    d.win = win;
    g_signal_connect(win, "close-request", G_CALLBACK(chiusa), &d);

    if (installa != NULL)
        installa(sh, d.f, utente);

    GtkWidget *wc = NULL;
    if (comandi) {
        wc = gtk_window_new();
        gtk_window_set_title(GTK_WINDOW(wc), "Telefono finto - comandi");
        gtk_window_set_child(GTK_WINDOW(wc), ph_finto_comandi_nuovi(d.f));
        g_object_add_weak_pointer(G_OBJECT(wc), (gpointer *)&wc); /* chiusa dall'utente: gia' distrutta */
        gtk_window_present(GTK_WINDOW(wc));
    }
    gtk_window_present(GTK_WINDOW(win));
    if (prova_ms >= 0) {
        d.scossa = g_timeout_add(250, scossa, &d);
        d.fine = g_timeout_add((guint)prova_ms, fine_prova, &d);
    }
    g_main_loop_run(d.ciclo);
    if (d.scossa != 0)
        g_source_remove(d.scossa);
    if (d.fine != 0)
        g_source_remove(d.fine);

    if (wc != NULL)
        gtk_window_destroy(GTK_WINDOW(wc));
    gtk_window_destroy(GTK_WINDOW(win));
    /* Liberando la radice i pezzi chiudono i loro flussi (pezzi.h). */
    ph_shell_libera(sh);
    while (g_main_context_pending(NULL))
        g_main_context_iteration(NULL, FALSE);
    ph_finto_libera(d.f);
    while (g_main_context_pending(NULL))
        g_main_context_iteration(NULL, FALSE);
    g_main_loop_unref(d.ciclo);
    return 0;
}
