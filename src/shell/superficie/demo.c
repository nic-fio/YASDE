/* demo-superficie: tre superfici sopra il telefono finto, in un contenitore
 * libero (senza finestre interne: quelle sono di finestra-app). Tasti:
 *   F9  ridimensiona (in GPU subito, flusso nuovo dopo 300 ms, come 4.5)
 *   F10 pausa / ripresa di tutte
 * Con --dmabuf i fotogrammi finti arrivano in DMA-BUF (via senza copie).
 * Chi scrive il pezzo puo' arricchirlo (misure, trascinamento, ...). */
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#include "shell/finto/finto.h"
#include "shell/superficie.h"

#define N 4 /* la quarta (Banca, schermo protetto) solo con PH_SUP_SEQUENZA=1 */

typedef struct {
    PhShell *sh;
    GtkWidget *libero;
    GtkWidget *sup[N];
    bool grandi, pausa;
    guint assesta, seq[9];
    int n_scatti, n_reg, reg_ev[8], reg2_dopo_chiusura;
    PhSrvEsito scatto_esito[2];
    GdkTexture *scatto_img[2];
    char *scatto_perc[2];
    bool registrando_prima, chiusa2;
    PhFinto *finto;
} Demo;

static const struct {
    const char *app;
    int x, y, w, h;
} POSTI[N] = {
    {"it.finto.note", 40, 40, 400, 240},
    {"it.finto.messaggi", 480, 24, 296, 440},
    {"it.finto.browser", 820, 80, 400, 280},
    {"it.finto.banca", 40, 330, 400, 240},
};

static gboolean assestato(gpointer u)
{
    Demo *d = u;
    d->assesta = 0;
    for (int i = 0; i < N; i++) {
        if (d->sup[i] == NULL)
            continue;
        int w = POSTI[i].w * (d->grandi ? 3 : 2) / 2, h = POSTI[i].h * (d->grandi ? 3 : 2) / 2;
        /* Misura del flusso: qui semplicemente i px logici * 420/160, multipli di 8. */
        ph_superficie_ridimensiona(d->sup[i], (w * 420 / 160 + 4) / 8 * 8, (h * 420 / 160 + 4) / 8 * 8);
    }
    return G_SOURCE_REMOVE;
}

/* Sequenza a tempo per le foto (PH_SUP_SEQUENZA=1, nessun tasto sullo schermo
 * invisibile): dopo 2,5 s la prima superficie chiede una misura di proporzioni
 * diverse (bande a riposo); dopo 5,5 s la connessione cade per 4 s; dopo 9,8 s
 * si chiude la seconda superficie mentre i fotogrammi arrivano. */
/* Verifiche a tempo (PH_SUP_SEQUENZA=1): ogni esito si scrive su stderr come
 * "demo: verifica <nome>: ok" oppure "... FALLITA" (si leggono nel log delle foto). */
static void verifica(const char *nome, bool ok, const char *dettaglio)
{
    fprintf(stderr, "demo: verifica %s: %s%s%s\n", nome, ok ? "ok" : "FALLITA", dettaglio != NULL ? " - " : "",
            dettaglio != NULL ? dettaglio : "");
}

static void demo_scatto(GtkWidget *sup, PhSrvEsito esito, const char *frase, const char *percorso,
                        GdkTexture *img, void *u)
{
    (void)sup;
    Demo *d = u;
    if (d->n_scatti < 2) {
        d->scatto_esito[d->n_scatti] = esito;
        d->scatto_img[d->n_scatti] = img != NULL ? g_object_ref(img) : NULL;
        d->scatto_perc[d->n_scatti] = g_strdup(percorso);
    }
    d->n_scatti++;
    if (frase != NULL)
        fprintf(stderr, "demo: screenshot frase: %s\n", frase);
}

static void demo_reg(GtkWidget *sup, const PhSrvRegDati *dd, void *u)
{
    (void)sup;
    Demo *d = u;
    if (d->chiusa2)
        d->reg2_dopo_chiusura++;
    else if (d->n_reg < 8)
        d->reg_ev[d->n_reg++] = (int)dd->evento;
    if (dd->frase != NULL)
        fprintf(stderr, "demo: registrazione frase: %s\n", dd->frase);
}

static char *cartella_prove(void)
{
    return g_build_filename(g_get_user_cache_dir(), "yasde-sviluppo", "superficie-demo-prove", NULL);
}

/* 3,5 s: due screenshot su file di fila (+ appunti il primo) e una registrazione. */
static gboolean seq_cattura(gpointer u)
{
    Demo *d = u;
    d->seq[3] = 0;
    char *dir = cartella_prove();
    if (d->sup[2] != NULL) {
        ph_superficie_screenshot(d->sup[2], PH_SRV_FOTO_FILE | PH_SRV_FOTO_APPUNTI, dir, "Prova", NULL, demo_scatto, d);
        ph_superficie_screenshot(d->sup[2], PH_SRV_FOTO_FILE, dir, "Prova", NULL, demo_scatto, d);
    }
    if (d->sup[0] != NULL)
        ph_superficie_registra(d->sup[0], dir, "Prova", false, demo_reg, d);
    g_free(dir);
    return G_SOURCE_REMOVE;
}

/* 4,5 s: la registrazione risulta in corso, la si ferma. */
static gboolean seq_ferma(gpointer u)
{
    Demo *d = u;
    d->seq[4] = 0;
    if (d->sup[0] != NULL) {
        d->registrando_prima = ph_superficie_registrando(d->sup[0]);
        ph_superficie_registra_ferma(d->sup[0]);
    }
    return G_SOURCE_REMOVE;
}

/* 5,5 s: esiti di screenshot e registrazione; poi nuova registrazione sulla terza. */
static gboolean seq_verifica1(gpointer u)
{
    Demo *d = u;
    d->seq[5] = 0;
    verifica("screenshot: due risposte OK", d->n_scatti == 2 && d->scatto_esito[0] == PH_SRV_ESITO_OK &&
             d->scatto_esito[1] == PH_SRV_ESITO_OK, NULL);
    bool file = d->scatto_perc[0] != NULL && d->scatto_perc[1] != NULL &&
                g_file_test(d->scatto_perc[0], G_FILE_TEST_IS_REGULAR) &&
                g_file_test(d->scatto_perc[1], G_FILE_TEST_IS_REGULAR);
    verifica("screenshot: due file esistenti", file, NULL);
    verifica("screenshot: due nomi diversi (nessuna sovrascrittura)",
             file && strcmp(d->scatto_perc[0], d->scatto_perc[1]) != 0, NULL);
    /* L'ordine di arrivo non e' quello di richiesta (la decodifica del PNG e' su un thread):
     * una sola risposta ha l'immagine per gli appunti, quella che l'aveva chiesta. */
    verifica("screenshot: immagine per gli appunti (una sola, di 1048 px o piu')",
             (d->scatto_img[0] != NULL) != (d->scatto_img[1] != NULL) &&
             gdk_texture_get_width(d->scatto_img[0] != NULL ? d->scatto_img[0] : d->scatto_img[1]) >= 64, NULL);
    for (int i = 0; i < 2; i++) {
        if (d->scatto_perc[i] != NULL)
            g_unlink(d->scatto_perc[i]);
        g_free(d->scatto_perc[i]);
        g_clear_object(&d->scatto_img[i]);
    }
    char *dir = cartella_prove();
    g_rmdir(dir);
    verifica("registrazione: INIZIATA, in corso, FINITA",
             d->registrando_prima && d->n_reg == 2 && d->reg_ev[0] == PH_SRV_REG_INIZIATA &&
             d->reg_ev[1] == PH_SRV_REG_FINITA && d->sup[0] != NULL && !ph_superficie_registrando(d->sup[0]),
             NULL);
    d->n_reg = 0;
    if (d->sup[2] != NULL)
        ph_superficie_registra(d->sup[2], dir, "Prova", false, demo_reg, d);
    g_free(dir);
    return G_SOURCE_REMOVE;
}

/* 6,5 s: si chiude la superficie CON la registrazione in corso. */
static gboolean seq_chiudi_registrando(gpointer u)
{
    Demo *d = u;
    d->seq[6] = 0;
    if (d->sup[2] == NULL)
        return G_SOURCE_REMOVE;
    verifica("chiusura con registrazione: era in corso", ph_superficie_registrando(d->sup[2]) && d->n_reg == 1, NULL);
    GtkWidget *w = d->sup[2];
    d->sup[2] = NULL;
    d->chiusa2 = true;
    ph_superficie_chiudi(w, false);
    ph_shell_libero_togli(d->libero, w);
    return G_SOURCE_REMOVE;
}

/* 8,5 s: dopo la chiusura nessun avviso. */
static gboolean seq_verifica2(gpointer u)
{
    Demo *d = u;
    d->seq[7] = 0;
    verifica("chiusura con registrazione: nessun avviso dopo la chiusura", d->chiusa2 && d->reg2_dopo_chiusura == 0,
             NULL);
    return G_SOURCE_REMOVE;
}

/* 1,5 s: la terza superficie a 1:1 (misura del flusso = misura del widget, misura esatta uguale):
 * a scala 1 deve vedersi nitida, campionamento esatto. */
static gboolean seq_esatta(gpointer u)
{
    Demo *d = u;
    d->seq[8] = 0;
    if (d->sup[2] != NULL) {
        ph_superficie_ridimensiona(d->sup[2], POSTI[2].w, POSTI[2].h);
        ph_superficie_misura_esatta(d->sup[2], POSTI[2].w, POSTI[2].h);
    }
    return G_SOURCE_REMOVE;
}

static gboolean seq_bande(gpointer u)
{
    Demo *d = u;
    d->seq[0] = 0;
    if (d->sup[0] != NULL)
        ph_superficie_ridimensiona(d->sup[0], 160, 480);
    return G_SOURCE_REMOVE;
}

static gboolean seq_perdi(gpointer u)
{
    Demo *d = u;
    d->seq[1] = 0;
    ph_finto_perdi(d->finto, 4);
    return G_SOURCE_REMOVE;
}

static gboolean seq_chiudi(gpointer u)
{
    Demo *d = u;
    d->seq[2] = 0;
    if (d->sup[1] != NULL) {
        GtkWidget *w = d->sup[1];
        d->sup[1] = NULL;
        ph_superficie_chiudi(w, false);
        ph_shell_libero_togli(d->libero, w);
    }
    return G_SOURCE_REMOVE;
}

static gboolean tasto(GtkEventControllerKey *k, guint t, guint c, GdkModifierType m, gpointer u)
{
    (void)k;
    (void)c;
    (void)m;
    Demo *d = u;
    if (t == GDK_KEY_F9) {
        d->grandi = !d->grandi;
        for (int i = 0; i < N; i++)
            if (d->sup[i] != NULL)
                ph_shell_libero_sposta(d->libero, d->sup[i], POSTI[i].x, POSTI[i].y,
                                       POSTI[i].w * (d->grandi ? 3 : 2) / 2, POSTI[i].h * (d->grandi ? 3 : 2) / 2);
        if (d->assesta != 0)
            g_source_remove(d->assesta);
        d->assesta = g_timeout_add(300, assestato, d);
        return TRUE;
    }
    if (t == GDK_KEY_F10) {
        d->pausa = !d->pausa;
        for (int i = 0; i < N; i++)
            if (d->sup[i] != NULL)
                ph_superficie_pausa(d->sup[i], d->pausa);
        return TRUE;
    }
    return FALSE;
}

static void radice_distrutta(GtkWidget *w, gpointer u)
{
    (void)w;
    Demo *d = u;
    if (d->assesta != 0)
        g_source_remove(d->assesta);
    d->assesta = 0;
    for (int i = 0; i < 9; i++)
        if (d->seq[i] != 0)
            g_source_remove(d->seq[i]);
    for (int i = 0; i < N; i++)
        d->sup[i] = NULL;
}

static void installa(PhShell *sh, PhFinto *f, void *u)
{
    Demo *d = u;
    d->sh = sh;
    d->finto = f;
    d->libero = ph_shell_libero_nuovo();
    ph_shell_strato_aggiungi(sh, PH_STRATO_FINESTRE, d->libero);
    const char *sq = g_getenv("PH_SUP_SEQUENZA");
    bool sequenza = sq != NULL && sq[0] == '1';
    for (int i = 0; i < (sequenza ? N : 3); i++) {
        PhSrvFlussoRichiesta r = {POSTI[i].app, (POSTI[i].w * 420 / 160 + 4) / 8 * 8,
                                  (POSTI[i].h * 420 / 160 + 4) / 8 * 8, 0, 0};
        PhErrore e = {{0}};
        d->sup[i] = ph_superficie_nuova(sh, &r, NULL, NULL, &e);
        if (d->sup[i] == NULL) {
            fprintf(stderr, "demo-superficie: %s\n", e.testo);
            continue;
        }
        if (i == 0)
            ph_superficie_primo_piano(d->sup[i], true); /* una sola in primo piano */
        ph_shell_libero_metti(d->libero, d->sup[i], POSTI[i].x, POSTI[i].y, POSTI[i].w, POSTI[i].h);
    }
    if (sequenza) {
        d->seq[0] = g_timeout_add(2500, seq_bande, d);
        d->seq[1] = g_timeout_add(5500, seq_perdi, d);
        d->seq[2] = g_timeout_add(9800, seq_chiudi, d);
        d->seq[3] = g_timeout_add(3500, seq_cattura, d);
        d->seq[4] = g_timeout_add(4500, seq_ferma, d);
        d->seq[5] = g_timeout_add(5500, seq_verifica1, d);
        d->seq[6] = g_timeout_add(6500, seq_chiudi_registrando, d);
        d->seq[7] = g_timeout_add(8500, seq_verifica2, d);
        d->seq[8] = g_timeout_add(1500, seq_esatta, d);
    }
    GtkEventController *k = gtk_event_controller_key_new();
    g_signal_connect(k, "key-pressed", G_CALLBACK(tasto), d);
    gtk_widget_add_controller(ph_shell_radice(sh), k);
    g_signal_connect(ph_shell_radice(sh), "destroy", G_CALLBACK(radice_distrutta), d);
}

int main(int argc, char **argv)
{
    Demo d = {0};
    return ph_finto_demo(argc, argv, "YASDE 2.2 - demo superficie", installa, &d);
}
