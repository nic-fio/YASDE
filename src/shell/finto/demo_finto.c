/* demo-finto: il telefono finto da solo. Mostra lo stato che pubblica
 * (collegamento, batteria, notifiche, trasferimenti) e un flusso video
 * sintetico in una superficie (la superficie finta). Serve a controllare il
 * finto stesso sul tablet, anche con --dmabuf. */
#include <stdio.h>
#include <string.h>

#include "finto.h"
#include "shell/superficie.h"

typedef struct {
    PhShell *sh;
    PhFinto *f;
    PhServizi *s;
    bool prova;
    GtkWidget *stato;
    GtkWidget *sup;
    guint ascolti[PH_SRV_EV_NUMERO], conta_ascolti;
    guint eventi_dopo; /* eventi arrivati dopo che l'etichetta e' stata tolta (prova) */
    bool etichetta_tolta;
} Demo;

static const char *nome_stato(PhSrvStato s)
{
    switch (s) {
    case PH_SRV_CERCO: return "CERCO";
    case PH_SRV_COLLEGATO: return "COLLEGATO";
    case PH_SRV_BLOCCATO: return "BLOCCATO";
    case PH_SRV_PERSO: return "PERSO";
    case PH_SRV_CHIUSO: return "CHIUSO";
    case PH_SRV_NON_ABBINATO: return "NON_ABBINATO";
    }
    return "?";
}

static void aggiorna(PhServizi *s, PhSrvEvento ev, const char *arg, void *u)
{
    (void)ev;
    (void)arg;
    Demo *d = u;
    if (d->etichetta_tolta)
        d->eventi_dopo++;
    if (d->stato == NULL)
        return;
    PhSrvCollegamento c;
    PhSrvTelefono t;
    PhSrvVolume v;
    size_t na, nn, nt;
    ph_srv_collegamento(s, &c);
    ph_srv_telefono(s, &t);
    ph_srv_volume(s, &v);
    ph_srv_app(s, &na);
    ph_srv_notifiche(s, &nn, NULL);
    ph_srv_trasferimenti(s, &nt);
    char *testo = g_strdup_printf("%s - %s\nbatteria %d%%, pannello %s, volume %d%s\n%zu app, %zu notifiche, "
                                  "%zu trasferimenti",
                                  nome_stato(c.stato), c.motivo, t.batteria, t.pannello_acceso ? "acceso" : "spento",
                                  v.volume, v.muto ? " (muto)" : "", na, nn, nt);
    gtk_label_set_text(GTK_LABEL(d->stato), testo);
    g_free(testo);
}

/* L'etichetta sparisce con la shell (o prima, in prova): smettere di ascoltare per lei,
 * altrimenti un avviso in coda dopo la distruzione scrive in un oggetto finito. */
static void etichetta_distrutta(GtkWidget *w, gpointer p)
{
    (void)w;
    Demo *d = p;
    d->stato = NULL;
    d->etichetta_tolta = true;
}

/* In prova: toglie l'etichetta mentre il telefono finto ha ancora avvisi da mandare. */
static gboolean togli_etichetta(gpointer p)
{
    Demo *d = p;
    if (d->stato != NULL)
        gtk_widget_unparent(d->stato); /* senza altri riferimenti: distrutta subito */
    ph_finto_notifica_nuova(d->f);
    return G_SOURCE_REMOVE;
}

static void installa(PhShell *sh, PhFinto *f, void *u)
{
    Demo *d = u;
    d->f = f;
    d->sh = sh;
    PhServizi *s = ph_shell_servizi(sh);
    d->s = s;
    if (d->prova)
        g_timeout_add(900, togli_etichetta, d); /* prima della fine della prova (1500 ms) */
    d->stato = gtk_label_new("");
    gtk_widget_set_halign(d->stato, GTK_ALIGN_START);
    gtk_widget_set_valign(d->stato, GTK_ALIGN_START);
    gtk_widget_set_margin_start(d->stato, 16);
    gtk_widget_set_margin_top(d->stato, 16);
    ph_shell_strato_aggiungi(sh, PH_STRATO_BARRA, d->stato);
    g_signal_connect(d->stato, "destroy", G_CALLBACK(etichetta_distrutta), d);
    for (int ev = 0; ev < PH_SRV_EV_NUMERO; ev++)
        d->ascolti[d->conta_ascolti++] = ph_srv_ascolta(s, (PhSrvEvento)ev, aggiorna, d);

    PhSrvFlussoRichiesta r = {"it.finto.note", 640, 400, 0, 0};
    PhErrore e = {{0}};
    d->sup = ph_superficie_nuova(sh, &r, NULL, NULL, &e);
    if (d->sup == NULL) {
        fprintf(stderr, "demo-finto: %s\n", e.testo);
        return;
    }
    GtkWidget *libero = ph_shell_libero_nuovo();
    ph_shell_libero_metti(libero, d->sup, 200, 140, 640, 400);
    ph_shell_strato_aggiungi(sh, PH_STRATO_FINESTRE, libero);
}

int main(int argc, char **argv)
{
    Demo d = {0};
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--prova") == 0)
            d.prova = true;
    int r = ph_finto_demo(argc, argv, "YASDE 2.2 - telefono finto", installa, &d);
    /* In prova l'etichetta e' stata tolta a ciclo acceso e un avviso e' arrivato DOPO: se non
     * e' arrivato, la prova non ha provato nulla. */
    if (r == 0 && d.prova && d.eventi_dopo == 0) {
        fprintf(stderr, "demo-finto: nessun avviso dopo la rimozione dell'etichetta\n");
        return 1;
    }
    return r;
}
