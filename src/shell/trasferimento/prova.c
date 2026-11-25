/* Ganci di prova per il demo (prova.h): compilato SOLO nel demo, non nella
 * libreria di produzione. */
#include "shell/trasferimento/prova.h"

#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell/trasferimento/interno.h"

/* Il pezzo si lascia trovare dalla radice (dato "ph-trasferimento"). */
static Trf *trf_di(PhShell *sh)
{
    return g_object_get_data(G_OBJECT(ph_shell_radice(sh)), "ph-trasferimento");
}

/* ---- Prove del vetro (fase 5) ------------------------------------------------------------------------------ */

static void controlla_vetro(GtkWidget *w, const char *nome)
{
    if (!gtk_widget_has_css_class(w, "ph-vetro-chiaro"))
        g_critical("vetro: %s non e' una superficie chiara (manca ph-vetro-chiaro)", nome);
}

/* Il riquadro di rilascio e' un velo a tutta area: NON e' una superficie di vetro. */
static void controlla_senza_vetro(GtkWidget *w, const char *nome)
{
    if (gtk_widget_has_css_class(w, "ph-vetro-chiaro") || gtk_widget_has_css_class(w, "ph-vetro-scuro"))
        g_critical("vetro: %s non deve essere una superficie di vetro", nome);
}

/* ---- Ganci di prova per il demo (nessuno schermo con mouse e tastiera) ---------------------------------------- */

/* File finti da installare (il telefono finto vuole un file vero e leggibile):
 * si cancellano all'uscita del demo. */
static GPtrArray *apk_creati;
static char *dir_apk;

static void apk_pulisci(void)
{
    if (apk_creati == NULL)
        return;
    for (guint i = 0; i < apk_creati->len; i++)
        g_unlink(g_ptr_array_index(apk_creati, i));
    if (dir_apk != NULL)
        g_rmdir(dir_apk);
    g_ptr_array_free(apk_creati, TRUE);
    g_free(dir_apk);
    apk_creati = NULL;
    dir_apk = NULL;
}

/* Il telefono finto finisce l'installazione in ~2 secondi: dopo, il file non serve piu'. */
static gboolean apk_rimuovi(gpointer p)
{
    g_unlink(p);
    return G_SOURCE_REMOVE;
}

static char *apk_finto(const char *nome)
{
    if (apk_creati == NULL) {
        apk_creati = g_ptr_array_new_with_free_func(g_free);
        dir_apk = g_build_filename(g_get_user_cache_dir(), "yasde-sviluppo", "demo-apk", NULL);
        g_mkdir_with_parents(dir_apk, 0700);
        atexit(apk_pulisci);
    }
    char *percorso = g_build_filename(dir_apk, nome, NULL);
    static const char dati[60000] = {1};
    g_file_set_contents(percorso, dati, sizeof dati, NULL);
    g_ptr_array_add(apk_creati, percorso);
    g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, 6, apk_rimuovi, g_strdup(percorso), g_free);
    return percorso;
}

static gboolean prova_annulla_inst(gpointer dati)
{
    Trf *t = trf_di(dati);
    if (t != NULL && t->inst_prova != 0)
        ph_srv_installazione_annulla(t->srv, t->inst_prova);
    return G_SOURCE_REMOVE;
}

static gboolean prova_scelte(gpointer dati)
{
    Trf *t = trf_di(dati);
    if (t != NULL) {
        ph_nav_prova_scegli(t->nav_ric, "/storage/emulated/0/Download/report.pdf");
        ph_nav_prova_scegli(t->nav_ric, "/storage/emulated/0/Download/notes.txt");
    }
    return G_SOURCE_REMOVE;
}

static gboolean prova_cerca(gpointer dati)
{
    Trf *t = trf_di(dati);
    if (t != NULL)
        ph_nav_prova_cerca(t->nav_invio, "pippo");
    return G_SOURCE_REMOVE;
}

static gboolean prova_cerca_ric(gpointer dati)
{
    Trf *t = trf_di(dati);
    if (t != NULL)
        ph_nav_prova_cerca(t->nav_ric, "pdf");
    return G_SOURCE_REMOVE;
}

void ph_trasferimento_prova(PhShell *sh, int passo)
{
    Trf *t = trf_di(sh);
    PhErrore e = {{0}};
    if (t == NULL)
        return;
    switch (passo) {
    case 0: /* pannello di invio con due file in coda */
        controlla_vetro(t->p_invio, "pannello di invio");
        controlla_vetro(t->p_ric, "pannello di ricezione");
        controlla_senza_vetro(t->riquadro, "riquadro di rilascio");
        ph_trf_apri_pannello(t, PH_SRV_INVIO, false);
        ph_srv_tr_accoda(t->srv, PH_SRV_INVIO, "/etc/os-release", t->dest_invio, &e);
        ph_srv_tr_accoda(t->srv, PH_SRV_INVIO, "/etc/hosts", t->dest_invio, &e);
        break;
    case 1: /* selettore sul telefono, in una cartella non consentita */
        t->scelta_invio = true;
        ph_nav_apri(t->nav_invio, "/data");
        ph_trf_aggiorna_invio(t);
        break;
    case 2: /* ricerca di una cartella */
        ph_nav_apri(t->nav_invio, t->dest_invio);
        g_timeout_add(350, prova_cerca, sh); /* dopo l'elenco: cambiare cartella chiude la ricerca */
        break;
    case 3: /* scarica, con due file scelti */
        ph_trf_apri_pannello(t, PH_SRV_RICEZIONE, false);
        ph_nav_apri(t->nav_ric, "/storage/emulated/0/Download");
        g_timeout_add(600, prova_scelte, sh);
        break;
    case 4: /* selettore di cartelle del PC */
        t->scelta_pc = true;
        ph_nav_apri(t->nav_pc, t->dest_ric);
        ph_trf_aggiorna_ricezione(t);
        break;
    case 5: /* invio con 20 file in coda: si vedono quelli da fare e "Annulla tutti" */
        t->scelta_invio = false;
        ph_trf_aggiorna_invio(t);
        ph_trf_apri_pannello(t, PH_SRV_INVIO, false);
        for (int i = 0; i < 20; i++)
            ph_srv_tr_accoda(t->srv, PH_SRV_INVIO, "/etc/os-release", t->dest_invio, &e);
        break;
    case 6: /* installazione di un APK in corso */
        t->scelta_invio = false;
        ph_trf_aggiorna_invio(t);
        ph_trf_apri_pannello(t, PH_SRV_INVIO, false);
        ph_trf_installa_apk(t, apk_finto("prova-app.apk"));
        break;
    case 7: /* APK rifiutato dal telefono */
        ph_trf_installa_apk(t, apk_finto("prova-rifiutata.apk"));
        break;
    case 8: /* scarica: ricerca con la dimensione dei file */
        ph_trf_apri_pannello(t, PH_SRV_RICEZIONE, false);
        g_timeout_add(350, prova_cerca_ric, sh);
        break;
    case 9: /* Scarica chiuso e riaperto due volte: deve ripartire dalla cartella dov'era */
        for (int volta = 0; volta < 2; volta++) {
            ph_nav_apri(t->nav_ric, "/storage/emulated/0/Download");
            ph_shell_popup_chiudi(sh);
            ph_trf_apri_pannello(t, PH_SRV_RICEZIONE, false);
            if (strcmp(ph_nav_percorso(t->nav_ric), "/storage/emulated/0/Download") != 0)
                g_critical("riapertura di Scarica: percorso %s invece di /storage/emulated/0/Download",
                           ph_nav_percorso(t->nav_ric));
        }
        break;
    case 10: /* installazione annullata dopo mezzo secondo */
        ph_trf_apri_pannello(t, PH_SRV_INVIO, false);
        t->inst_prova = ph_trf_installa_apk(t, apk_finto("prova-annullata.apk"));
        g_timeout_add(500, prova_annulla_inst, sh);
        break;
    case 11: /* riga ferma nello stato "In installazione" (copia finita): senza "x" */
        ph_trf_elenco_inst_nuova(t->el_invio, 9999, 1, "stato-prova.apk");
        ph_trf_elenco_inst_avanza(t->el_invio, 9999, 100, 100);
        break;
    default:
        break;
    }
}
