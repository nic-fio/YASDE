/* demo-ossatura: la shell INTERA sopra il telefono finto, con la finestra
 * principale VERA (barra del titolo, schermo intero con la striscia, velo) e i
 * pezzi presenti in questa compilazione (con -Dpezzo=shell-ossatura nessuno;
 * con pezzo=tutto quelli approvati). Serve all'integrazione.
 * Opzioni (come ph_finto_demo): --misura LxA, --comandi, --dmabuf, --prova MS
 * (torna 77 se non c'e' uno schermo). Tasti: F5 blocca/sblocca, F6 connessione
 * persa per 5 s, F7 notifica nuova, F8 l'ultima app si chiude da sola, F11
 * schermo intero. I testi qui sono per chi sviluppa: niente traduzioni.
 * Per le schermate del manuale (nello schermo invisibile non c'e' ne' mouse ne'
 * tastiera): --app-neutra (finestre con un'immagine "da app" a rettangoli
 * invece delle barre), --passi "p1,p2,..." (un passo ogni 1,2 s, il primo dopo
 * 2,5 s): app:<pacchetto> apre una finestra; clic:<sel> preme un pulsante, con
 * <sel> = css=<classe>[#n] | tip=<suggerimento>[#n] | dato=<chiave>[#n] | testo=<etichetta>[#n]
 * (n-esimo in ordine di albero, da 0); hook:<azione>:<n> e' un'azione di
 * finestra_app_demo.h; invia:<nome> (accoda l'invio di un file vuoto da 40 MB, tolto all'uscita);
 * decodifica-sw (l'avviso «decodifica col processore»);
 * destro:X:Y (clic destro nel punto, coordinate della
 * finestra, per i menu); notifica; chiudi (chiude il popup); elenca (stampa i
 * pulsanti su stderr, per scegliere i selettori). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "config/lingua.h"
#include "ossatura.h"
#include "pezzi_presenti.h"
#include "shell/finto/finto.h"
#include "shell/servizi_backend.h"
#if PH_SHELL_HA_FINESTRA_APP
#include "shell/finestra-app/finestra_app_demo.h"
#endif

typedef struct {
    PhFinto *f;
    PhShell *sh;
    GtkWindow *win;
    GMainLoop *ciclo;
    bool bloccato;
    guint scossa, fine;
    int passo;
    int errori;
    const char *mostra; /* blocco | persa | intero */
    bool apri_app;      /* --apri-app: apre una finestra di app (ph_shell_apri_app) */
    bool app_aperta;
    guint id_app;
    int chiudi_dopo_s;  /* --chiudi-dopo S: gtk_window_close dopo S secondi */
    bool chiuso;
    char **passi;       /* --passi: la sequenza (NULL-terminata) */
    int passo_n;
    guint id_passi;
    GSList *file_invio; /* file creati dal passo invia: si cancellano all'uscita */
    GtkWidget *wc;      /* finestrella dei comandi */
    GtkWidget *radice_tenuta; /* riferimento di troppo alla radice, finche' i servizi non si liberano */
} Demo;

/* Appena arriva l'elenco delle app apre una finestra di app (come fa il demo di
 * finestra-app): con il pezzo finestra-app c'e' un flusso del telefono finto. */
static void app_arrivate(PhServizi *s, PhSrvEvento ev, const char *arg, void *u)
{
    (void)ev;
    (void)arg;
    Demo *d = u;
    size_t n = 0;
    ph_srv_app(s, &n);
    if (d->apri_app && !d->app_aperta && n > 0) {
        d->app_aperta = true;
        ph_shell_apri_app(d->sh, "it.finto.note");
    }
}

/* Dopo ph_shell_libera, prima dei servizi (ph_ossatura_chiudi): il segnale
 * "changed" degli appunti di sistema deve essere gia' scollegato (il "destroy"
 * della radice e' avvenuto) anche se la radice e' ancora tenuta viva da un
 * riferimento di troppo, che qui il demo simula. */
static void libera_finto(void *utente)
{
    Demo *d = utente;
    GdkClipboard *cb = gdk_display_get_clipboard(gdk_display_get_default());
    if (g_signal_has_handler_pending(cb, g_signal_lookup("changed", GDK_TYPE_CLIPBOARD), 0, TRUE)) {
        fprintf(stderr, "demo: il segnale degli appunti di sistema e' ancora collegato dopo ph_shell_libera\n");
        d->errori++;
    }
    g_clear_object(&d->radice_tenuta);
    ph_finto_libera(d->f);
}

/* Chiusura come nel programma: la stessa funzione, ph_ossatura_chiudi. Dal
 * pulsante di chiusura gira DENTRO il segnale close-request. Una volta sola. */
static void chiudi(Demo *d)
{
    if (d->chiuso)
        return;
    d->chiuso = true;
    if (d->id_app != 0)
        ph_srv_smetti(ph_shell_servizi(d->sh), d->id_app);
    if (d->wc != NULL)
        gtk_window_destroy(GTK_WINDOW(d->wc));
    d->radice_tenuta = g_object_ref(ph_shell_radice(d->sh));
    ph_ossatura_chiudi(d->win, d->sh, libera_finto, d);
}


/* ---- Passi per le schermate del manuale --------------------------------- */
static GtkWidget *cerca_pulsante(GtkWidget *w, const char *tipo, const char *val, int *n)
{
    for (GtkWidget *c = w; c != NULL; c = gtk_widget_get_next_sibling(c)) {
        bool ok = false;
        if (strcmp(tipo, "css") == 0)
            ok = gtk_widget_has_css_class(c, val);
        else if (strcmp(tipo, "tip") == 0)
            ok = gtk_widget_get_tooltip_text(c) != NULL && strcmp(gtk_widget_get_tooltip_text(c), val) == 0;
        else if (strcmp(tipo, "dato") == 0)
            ok = g_object_get_data(G_OBJECT(c), val) != NULL;
        else if (strcmp(tipo, "testo") == 0 && GTK_IS_BUTTON(c)) {
            GtkWidget *f = gtk_button_get_child(GTK_BUTTON(c));
            ok = f != NULL && GTK_IS_LABEL(f) && strcmp(gtk_label_get_text(GTK_LABEL(f)), val) == 0;
        } else if (strcmp(tipo, "testo") == 0 && GTK_IS_CHECK_BUTTON(c)) {
            const char *l = gtk_check_button_get_label(GTK_CHECK_BUTTON(c));
            ok = l != NULL && strcmp(l, val) == 0;
        }
        if (ok && (GTK_IS_BUTTON(c) || GTK_IS_CHECK_BUTTON(c)) && (*n)-- == 0)
            return c;
        GtkWidget *r = cerca_pulsante(gtk_widget_get_first_child(c), tipo, val, n);
        if (r != NULL)
            return r;
    }
    return NULL;
}

static void elenca(GtkWidget *w, int livello)
{
    for (GtkWidget *c = w; c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (GTK_IS_BUTTON(c) || GTK_IS_CHECK_BUTTON(c)) {
            GtkWidget *f = GTK_IS_BUTTON(c) ? gtk_button_get_child(GTK_BUTTON(c)) : NULL;
            char *cl = g_strjoinv(" ", gtk_widget_get_css_classes(c));
            fprintf(stderr, "pulsante: tipo=%s classi=[%s] tip=%s testo=%s vis=%d\n", G_OBJECT_TYPE_NAME(c), cl,
                    gtk_widget_get_tooltip_text(c) ? gtk_widget_get_tooltip_text(c) : "-",
                    f != NULL && GTK_IS_LABEL(f) ? gtk_label_get_text(GTK_LABEL(f)) : "-", gtk_widget_get_mapped(c));
            g_free(cl);
        }
        elenca(gtk_widget_get_first_child(c), livello + 1);
    }
}

/* Clic destro in (x, y) della finestra: emette "pressed" del gesto del tasto secondario
 * del primo widget sotto il punto che ne abbia uno (come il clic vero, per i menu). */
static bool clic_destro(GtkWidget *finestra, double x, double y)
{
    GtkWidget *w = gtk_widget_pick(finestra, x, y, GTK_PICK_DEFAULT);
    for (; w != NULL; w = gtk_widget_get_parent(w)) {
        GListModel *ctl = gtk_widget_observe_controllers(w);
        for (guint i = 0; i < g_list_model_get_n_items(ctl); i++) {
            GtkEventController *c = g_list_model_get_item(ctl, i);
            bool ok = GTK_IS_GESTURE_CLICK(c) && gtk_gesture_single_get_button(GTK_GESTURE_SINGLE(c)) == GDK_BUTTON_SECONDARY;
            if (ok) {
                graphene_point_t pf = GRAPHENE_POINT_INIT((float)x, (float)y), pw;
                if (!gtk_widget_compute_point(finestra, w, &pf, &pw))
                    pw = pf;
                g_signal_emit_by_name(c, "pressed", 1, (double)pw.x, (double)pw.y);
                g_object_unref(c);
                g_object_unref(ctl);
                return true;
            }
            g_object_unref(c);
        }
        g_object_unref(ctl);
    }
    return false;
}

static gboolean passo(gpointer p)
{
    Demo *d = p;
    if (d->passi == NULL || d->passi[d->passo_n] == NULL) {
        d->id_passi = 0;
        return G_SOURCE_REMOVE;
    }
    const char *st = d->passi[d->passo_n++];
    fprintf(stderr, "passo: %s\n", st);
    if (strncmp(st, "app:", 4) == 0) {
        ph_shell_apri_app(d->sh, st + 4);
    } else if (strcmp(st, "notifica") == 0) {
        ph_finto_notifica_nuova(d->f);
    } else if (strcmp(st, "decodifica-sw") == 0) {
        /* Come se un flusso passasse al processore: l'avviso a schermo (solo per le foto del manuale). */
        ph_servizi_pubblica_decodifica_software(ph_shell_servizi(d->sh));
    } else if (strcmp(st, "chiudi") == 0) {
        ph_shell_popup_chiudi(d->sh);
    } else if (strncmp(st, "destro:", 7) == 0) {
        double x = 0, y = 0;
        if (sscanf(st + 7, "%lf:%lf", &x, &y) != 2 || !clic_destro(GTK_WIDGET(d->win), x, y)) {
            fprintf(stderr, "passo: clic destro non riuscito (%s)\n", st);
            d->errori++;
        }
    } else if (strncmp(st, "invia:", 6) == 0) {
        /* Accoda l'invio di un file di 40 MB (vuoto) col nome dato, creato nella cache. */
        char *f = g_build_filename(g_get_user_cache_dir(), st + 6, NULL);
        FILE *fp = fopen(f, "wb");
        PhErrore e;
        memset(&e, 0, sizeof e);
        if (fp == NULL || ftruncate(fileno(fp), 40 << 20) != 0 ||
            ph_srv_tr_accoda(ph_shell_servizi(d->sh), PH_SRV_INVIO, f, "/sdcard/Download", &e) != 0) {
            fprintf(stderr, "passo: invio non riuscito (%s)\n", st);
            d->errori++;
        }
        if (fp != NULL)
            fclose(fp);
        if (fp != NULL)
            d->file_invio = g_slist_prepend(d->file_invio, f); /* tolto in uscita */
        else
            g_free(f);
    } else if (strcmp(st, "elenca") == 0) {
        elenca(GTK_WIDGET(d->win), 0);
    } else if (strncmp(st, "clic:", 5) == 0) {
        char *sel = g_strdup(st + 5);
        int n = 0;
        char *diesis = strrchr(sel, '#');
        if (diesis != NULL) {
            *diesis = '\0';
            n = atoi(diesis + 1);
        }
        char *uguale = strchr(sel, '=');
        GtkWidget *b = NULL;
        if (uguale != NULL) {
            *uguale = '\0';
            b = cerca_pulsante(GTK_WIDGET(d->win), sel, uguale + 1, &n);
        }
        if (b == NULL) {
            fprintf(stderr, "passo: pulsante non trovato (%s)\n", st);
            d->errori++;
        } else if (GTK_IS_CHECK_BUTTON(b)) {
            gtk_check_button_set_active(GTK_CHECK_BUTTON(b), !gtk_check_button_get_active(GTK_CHECK_BUTTON(b)));
        } else {
            gtk_widget_activate(b);
        }
        g_free(sel);
#if PH_SHELL_HA_FINESTRA_APP
    } else if (strncmp(st, "hook:", 5) == 0) {
        char *az = g_strdup(st + 5);
        char *due = strrchr(az, ':');
        unsigned idx = 0;
        if (due != NULL) {
            *due = '\0';
            idx = (unsigned)atoi(due + 1);
        }
        if (!ph_finestra_app_demo_azione(d->sh, az, idx)) {
            fprintf(stderr, "passo: azione rifiutata (%s)\n", st);
            d->errori++;
        }
        g_free(az);
#endif
    } else {
        fprintf(stderr, "passo sconosciuto: %s\n", st);
        d->errori++;
    }
    return G_SOURCE_CONTINUE;
}

static gboolean avvia_passi(gpointer p)
{
    Demo *d = p;
    d->id_passi = g_timeout_add(1200, passo, d);
    passo(d);
    return G_SOURCE_REMOVE;
}

static gboolean chiudi_ora(gpointer p)
{
    Demo *d = p;
    if (!d->chiuso)
        gtk_window_close(d->win); /* stessa strada del pulsante: emette close-request */
    return G_SOURCE_REMOVE;
}

static gboolean su_tasto(GtkEventControllerKey *k, guint t, guint codice, GdkModifierType stato, gpointer p)
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

static gboolean su_chiusura(GtkWindow *w, gpointer p)
{
    (void)w;
    Demo *d = p;
    chiudi(d);
    g_main_loop_quit(d->ciclo);
    return TRUE; /* la finestra l'ha gia' distrutta chiudi() */
}

static gboolean fine_prova(gpointer p)
{
    ((Demo *)p)->fine = 0;
    g_main_loop_quit(((Demo *)p)->ciclo);
    return G_SOURCE_REMOVE;
}

/* Nella prova automatica succede un po' di tutto: ogni pezzo e il velo
 * reagiscono a notifica, blocco, caduta e schermo intero. */
static gboolean scossa(gpointer p)
{
    Demo *d = p;
    switch (d->passo++) {
    case 0: ph_finto_notifica_nuova(d->f); break;
    case 1: ph_finto_blocca(d->f, true); break;
    case 2: ph_finto_blocca(d->f, false); break;
    case 3: ph_finto_perdi(d->f, 1); break;
    case 4: gtk_window_fullscreen(d->win); break;
    case 5: gtk_window_unfullscreen(d->win); break;
    case 6: ph_finto_notifica_aggiorna(d->f); break;
    default: d->scossa = 0; return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* --mostra: porta il demo in uno stato che senza mouse non si vedrebbe. */
static gboolean mostra_stato(gpointer p)
{
    Demo *d = p;
    if (strcmp(d->mostra, "blocco") == 0)
        ph_finto_blocca(d->f, true);
    else if (strcmp(d->mostra, "persa") == 0)
        ph_finto_perdi(d->f, 600);
    else if (strcmp(d->mostra, "intero") == 0)
        gtk_window_fullscreen(d->win);
    return G_SOURCE_REMOVE;
}

int main(int argc, char **argv)
{
    int larg = 0, alt = 0, prova_ms = -1;
    bool comandi = false, dmabuf = false;
    const char *mostra = NULL;
    bool apri_app = false, app_neutra = false;
    char **passi = NULL;
    int chiudi_dopo = 0;
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
        } else if (strcmp(argv[i], "--apri-app") == 0) {
            apri_app = true;
        } else if (strcmp(argv[i], "--app-neutra") == 0) {
            app_neutra = true;
        } else if (strcmp(argv[i], "--passi") == 0 && i + 1 < argc) {
            passi = g_strsplit(argv[++i], ",", -1);
        } else if (strcmp(argv[i], "--chiudi-dopo") == 0 && i + 1 < argc) {
            chiudi_dopo = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--mostra") == 0 && i + 1 < argc) {
            mostra = argv[++i];
        } else if (strcmp(argv[i], "--prova") == 0 && i + 1 < argc) {
            prova_ms = atoi(argv[++i]);
        } else {
            fprintf(stderr, "uso: %s [--misura LxA] [--comandi] [--dmabuf] [--mostra blocco|persa|intero] [--apri-app] [--app-neutra] [--passi P,P,...] [--chiudi-dopo S] [--prova MS]\n", argv[0]);
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

    /* Come il programma vero: l'app e' solo in inglese. */
    ph_lingua_inizializza(PH_CFG_LINGUA_AUTO);
    Demo d = {0};
    d.mostra = mostra;
    d.apri_app = apri_app;
    d.chiudi_dopo_s = chiudi_dopo;
    d.passi = passi;
    PhFintoOpzioni o = {.dmabuf = dmabuf, .notifiche_ogni_s = prova_ms >= 0 ? -1 : 0,
                        .ritardo_ms = prova_ms >= 0 ? 20 : 0, .aspetto_app = app_neutra};
    d.f = ph_finto_nuovo(&o);
    if (d.f == NULL)
        return 1;
    d.sh = ph_shell_nuova(ph_finto_servizi(d.f));
    d.win = ph_finestra_nuova(NULL, d.sh);
    if (larg > 0)
        gtk_window_set_default_size(d.win, larg, alt);
    d.ciclo = g_main_loop_new(NULL, FALSE);
    g_signal_connect(d.win, "close-request", G_CALLBACK(su_chiusura), &d);
    GtkEventController *k = gtk_event_controller_key_new();
    g_signal_connect(k, "key-pressed", G_CALLBACK(su_tasto), &d);
    gtk_widget_add_controller(GTK_WIDGET(d.win), k);

#if !PH_SHELL_HA_DESKTOP
    /* Senza il pezzo desktop: lo sfondo predefinito del mockup. */
    GtkWidget *sfondo = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sfondo, "ph-ossatura-demo-sfondo");
    gtk_widget_set_hexpand(sfondo, TRUE);
    gtk_widget_set_vexpand(sfondo, TRUE);
    ph_shell_strato_aggiungi(d.sh, PH_STRATO_DESKTOP, sfondo);
#endif
    ph_ossatura_installa_pezzi(d.sh);
    ph_appunti_installa(d.sh);
    if (apri_app)
        d.id_app = ph_srv_ascolta(ph_shell_servizi(d.sh), PH_SRV_EV_APP, app_arrivate, &d);

    GtkWidget *wc = NULL;
    if (comandi) {
        wc = gtk_window_new();
        gtk_window_set_title(GTK_WINDOW(wc), "Telefono finto - comandi");
        gtk_window_set_child(GTK_WINDOW(wc), ph_finto_comandi_nuovi(d.f));
        gtk_window_present(GTK_WINDOW(wc));
        d.wc = wc;
    }
    gtk_window_present(d.win);
    if (mostra != NULL)
        g_timeout_add(700, mostra_stato, &d);
    if (passi != NULL)
        g_timeout_add(2500, avvia_passi, &d);
    if (chiudi_dopo > 0)
        g_timeout_add_seconds((guint)chiudi_dopo, chiudi_ora, &d);
    if (prova_ms >= 0) {
        d.scossa = g_timeout_add(250, scossa, &d);
        d.fine = g_timeout_add((guint)prova_ms, fine_prova, &d);
    }
    g_main_loop_run(d.ciclo);
    if (d.scossa != 0)
        g_source_remove(d.scossa);
    if (d.fine != 0)
        g_source_remove(d.fine);
    if (d.id_passi != 0)
        g_source_remove(d.id_passi);
    g_strfreev(d.passi);

    chiudi(&d); /* se non e' gia' successo dal pulsante di chiusura */
    for (GSList *l = d.file_invio; l != NULL; l = l->next)
        g_unlink(l->data); /* i file da 40 MB del passo invia: non restano nella cache */
    g_slist_free_full(d.file_invio, g_free);
    g_main_loop_unref(d.ciclo);
    printf("chiusura pulita\n");
    fflush(stdout);
    return d.errori == 0 ? 0 : 1;
}
