/* demo-sistema: il solo pezzo sistema sopra il telefono finto. La barra non c'e':
 * qui ne basta una minima (una riga in basso a destra con i tre pulsanti che
 * crea questo pezzo). Il gestore delle finestre e' finto: scrive su stderr
 * l'app che si apre e tiene lo stato dello specchio. Con --comandi si apre la
 * finestrella del telefono finto (notifiche nuove, aggiornate, illeggibili,
 * batteria, caduta); F5-F8 fanno lo stesso da tastiera. */
#include <stdio.h>
#include <stdlib.h>

#include "shell/finto/finto.h"
#include "shell/pezzi.h"

typedef struct {
    PhShell *sh;
    PhFinto *finto;
    GtkWidget *btn[3]; /* volume, telefono, notifiche */
    bool specchio;
    int passo;
} Demo;

/* Sequenza a tempo per guardare il pezzo senza mouse (PH_SISTEMA_SEQUENZA=1,
 * ogni 2 s): pannello notifiche, notifica nuova (toast), stato del telefono,
 * volume (a meta' e muto). Per le foto: strumenti/foto-demo.sh sistema "3 5 7 9 11". */
static gboolean sequenza(gpointer dati)
{
    Demo *d = dati;
    PhShell *sh = d->sh;
    switch (d->passo++) {
    case 0: gtk_widget_activate(d->btn[2]); break;
    case 1: ph_shell_popup_chiudi(sh); ph_finto_notifica_nuova(d->finto); break;
    case 2: gtk_widget_activate(d->btn[1]); break;
    case 3: gtk_widget_activate(d->btn[0]); break;
    case 4: ph_srv_imposta_muto(ph_shell_servizi(sh), true); break;
    default: return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void finto_apri_app(void *io, const char *pacchetto)
{
    (void)io;
    fprintf(stderr, "demo-sistema: aperta l'app \"%s\"\n", pacchetto);
}

static size_t finto_elenco(void *io, PhShellFinestra *out, size_t max)
{
    (void)io;
    (void)out;
    (void)max;
    return 0;
}

static void finto_clic_barra(void *io, unsigned id)
{
    (void)io;
    (void)id;
}

static void finto_specchio(void *io, bool mostra)
{
    Demo *d = io;
    d->specchio = mostra;
    fprintf(stderr, "demo-sistema: specchio %s\n", mostra ? "aperto" : "chiuso");
    ph_shell_emetti(d->sh, PH_SHELL_EV_FINESTRE);
}

static bool finto_specchio_aperto(void *io) { return ((Demo *)io)->specchio; }

/* Prova a schermo: ogni superficie del pezzo (i tre popup e il toast) ha la classe del
 * vetro chiaro quando e' costruita; i pulsanti nella barra non sono superfici. Si
 * cerca nell'albero della radice; se manca qualcosa il demo esce con errore. */
static void conta_superfici(GtkWidget *w, int *popup, int *toast, int *chiari, int *scuri)
{
    if (gtk_widget_has_css_class(w, "ph-pannello"))
        (*popup)++;
    if (gtk_widget_has_css_class(w, "ph-sistema-toast"))
        (*toast)++;
    if (gtk_widget_has_css_class(w, "ph-vetro-chiaro"))
        (*chiari)++;
    if (gtk_widget_has_css_class(w, "ph-vetro-scuro"))
        (*scuri)++;
    if (gtk_widget_has_css_class(w, "ph-pannello") && !gtk_widget_has_css_class(w, "ph-vetro-chiaro")) {
        fprintf(stderr, "demo-sistema: un popup senza la classe ph-vetro-chiaro\n");
        exit(1);
    }
    if (gtk_widget_has_css_class(w, "ph-sistema-toast") && !gtk_widget_has_css_class(w, "ph-vetro-chiaro")) {
        fprintf(stderr, "demo-sistema: il toast senza la classe ph-vetro-chiaro\n");
        exit(1);
    }
    for (GtkWidget *f = gtk_widget_get_first_child(w); f != NULL; f = gtk_widget_get_next_sibling(f))
        conta_superfici(f, popup, toast, chiari, scuri);
}

static void controlla_vetro(PhShell *sh)
{
    int popup = 0, toast = 0, chiari = 0, scuri = 0;
    conta_superfici(ph_shell_radice(sh), &popup, &toast, &chiari, &scuri);
    if (popup != 3 || toast != 1 || chiari != 4 || scuri != 0) {
        fprintf(stderr, "demo-sistema: superfici di vetro sbagliate (popup %d, toast %d, chiari %d, scuri %d)\n",
                popup, toast, chiari, scuri);
        exit(1);
    }
}

static void installa(PhShell *sh, PhFinto *f, void *u)
{
    Demo *d = u;
    d->sh = sh;
    d->finto = f;
    PhShellGestoreFinestre g = {.io = d,
                                .apri_app = finto_apri_app,
                                .elenco = finto_elenco,
                                .clic_barra = finto_clic_barra,
                                .specchio = finto_specchio,
                                .specchio_aperto = finto_specchio_aperto};
    ph_shell_registra_finestre(sh, &g);

    ph_sistema_installa(sh);

    GtkWidget *barra = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(barra, GTK_ALIGN_END);
    gtk_widget_set_valign(barra, GTK_ALIGN_END);
    gtk_widget_set_margin_end(barra, 8);
    gtk_widget_set_size_request(barra, -1, 48);
    static const PhShellPopup tipi[] = {PH_POPUP_VOLUME, PH_POPUP_TELEFONO, PH_POPUP_NOTIFICHE};
    for (size_t i = 0; i < G_N_ELEMENTS(tipi); i++) {
        GtkWidget *b = ph_shell_sistema_pulsante(sh, tipi[i]);
        d->btn[i] = b;
        if (b != NULL)
            gtk_box_append(GTK_BOX(barra), b);
    }
    if (g_getenv("PH_SISTEMA_SEQUENZA") != NULL && d->btn[0] != NULL && d->btn[1] != NULL &&
        d->btn[2] != NULL)
        g_timeout_add_seconds(2, sequenza, d);
    ph_shell_strato_aggiungi(sh, PH_STRATO_BARRA, barra);
    controlla_vetro(sh);
}

int main(int argc, char **argv)
{
    Demo d = {0};
    return ph_finto_demo(argc, argv, "YASDE 2.2 - demo sistema", installa, &d);
}
