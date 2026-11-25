/* I tre pulsanti dell'area di sistema della barra (volume, stato del telefono,
 * notifiche): li crea e li aggiorna questo pezzo, li colloca la barra. Mockup:
 * .tb con icone 20x20, per il telefono rete + batteria 22x20 + percentuale,
 * per le notifiche campanella + contatore. */
#include <stdio.h>
#include <string.h>

#include "shell/sistema/interno.h"

typedef struct {
    PhShellPopup tipo;
    GtkWidget *icona_volume;                    /* VOLUME */
    GtkWidget *icona_rete, *icona_batteria, *testo; /* TELEFONO */
    GtkWidget *contatore;                       /* NOTIFICHE */
} Pulsante;

/* Misura unica delle icone dell'area di sistema (il mockup le disegna a 20 px,
 * troppo piccole accanto alle icone da 26 px della barra): le stesse icone del
 * mockup, ingrandite in proporzione (il tracciato e lo spessore scalano con la
 * misura). Le descrizioni vivono per tutto il programma: i widget ne tengono il
 * puntatore. */
#define LATO_ICONA 24

static PhShellIcona icona_volume, icona_volume_muto, icona_wifi, icona_campanella;
static bool icone_pronte;

static PhShellIcona scalata(const PhShellIcona *base)
{
    PhShellIcona i = *base;
    i.larghezza = (base->larghezza * LATO_ICONA + 10) / 20;
    i.altezza = (base->altezza * LATO_ICONA + 10) / 20;
    return i;
}

static void icone_prepara(void)
{
    if (icone_pronte)
        return;
    icona_volume = scalata(&PH_ICONA_VOLUME);
    icona_volume_muto = scalata(&PH_ICONA_VOLUME_MUTO);
    icona_wifi = scalata(&PH_ICONA_WIFI);
    icona_campanella = scalata(&PH_ICONA_CAMPANELLA);
    icone_pronte = true;
}

/* Icona della batteria a 101 livelli. Il widget tiene il puntatore alla
 * descrizione, quindi vive per tutto il programma (tabella statica). */
static PhShellIcona batteria_icone[101];
static char batteria_tratti[101][40];
static bool batteria_pronta[101];

static const PhShellIcona *batteria_icona(int percento)
{
    int p = ph_sistema_batteria_limita(percento);
    if (!batteria_pronta[p]) {
        batteria_icone[p] = scalata(&PH_ICONA_BATTERIA);
        ph_sistema_batteria_pieno(p, batteria_tratti[p], sizeof batteria_tratti[p]);
        batteria_icone[p].pieno = batteria_tratti[p][0] != '\0' ? batteria_tratti[p] : NULL;
        batteria_pronta[p] = true;
    }
    return &batteria_icone[p];
}

static void pulsante_aggiorna(PhSistema *s, GtkWidget *btn)
{
    Pulsante *p = g_object_get_data(G_OBJECT(btn), "ph-sistema-pulsante");
    if (p == NULL)
        return;
    char etichetta[128];
    switch (p->tipo) {
    case PH_POPUP_VOLUME: {
        bool muto = ph_sistema_volume_muto_mostrato(s->volume, s->muto);
        ph_shell_icona_imposta(p->icona_volume, muto ? &icona_volume_muto : &icona_volume);
        ph_sistema_volume_etichetta(s->volume, s->muto, etichetta, sizeof etichetta);
        ph_shell_etichetta(btn, etichetta, NULL);
        break;
    }
    case PH_POPUP_TELEFONO: {
        PhSrvCollegamento c;
        PhSrvTelefono t;
        ph_srv_collegamento(s->srv, &c);
        ph_srv_telefono(s->srv, &t);
        /* Il mockup non ha icone per la rete cellulare e per "nessuna rete":
         * la stessa icona Wi-Fi, attenuata senza rete. */
        bool rete = c.ha_telefono && t.noto && t.rete != PH_SRV_RETE_NESSUNA;
        if (rete)
            gtk_widget_remove_css_class(p->icona_rete, "ph-sistema-senza-rete");
        else
            gtk_widget_add_css_class(p->icona_rete, "ph-sistema-senza-rete");
        /* Stato non noto: niente testo e batteria vuota. */
        bool noto = c.ha_telefono && t.noto;
        ph_shell_icona_imposta(p->icona_batteria, batteria_icona(noto ? t.batteria : 0));
        if (noto) {
            char perc[16];
            snprintf(perc, sizeof perc, "%d", ph_sistema_batteria_limita(t.batteria));
            char testo[32];
            if (ph_lingua_componi(testo, sizeof testo, ph_t("{percento}%"), "percento", perc,
                                  (const char *)NULL) == (size_t)-1)
                g_strlcpy(testo, perc, sizeof testo);
            gtk_label_set_text(GTK_LABEL(p->testo), testo);
        } else {
            gtk_label_set_text(GTK_LABEL(p->testo), "");
        }
        gtk_widget_set_visible(p->testo, noto);
        break;
    }
    case PH_POPUP_NOTIFICHE: {
        char n[32];
        snprintf(n, sizeof n, "%zu", s->visibili);
        gtk_label_set_text(GTK_LABEL(p->contatore), n);
        gtk_widget_set_visible(p->contatore, s->visibili > 0);
        break;
    }
    default:
        break;
    }
    ph_shell_espanso(btn, ph_shell_popup_aperto(s->sh) == p->tipo);
}

void ph_sistema_pulsanti_aggiorna(PhSistema *s)
{
    if (s->pulsanti == NULL)
        return;
    /* Copia: l'aggiornamento non cambia l'elenco, ma e' piu' sicuro. */
    for (guint i = 0; i < s->pulsanti->len; i++)
        pulsante_aggiorna(s, g_ptr_array_index(s->pulsanti, i));
}

static void clic_volume(GtkButton *b, gpointer dati)
{
    (void)b;
    ph_sistema_volume_alterna(dati);
}

static void clic_telefono(GtkButton *b, gpointer dati)
{
    (void)b;
    ph_sistema_telefono_alterna(dati);
}

static void clic_notifiche(GtkButton *b, gpointer dati)
{
    (void)b;
    ph_sistema_notifiche_alterna(dati);
}

static void pulsante_morto(gpointer dati, GObject *morto)
{
    PhSistema *s = dati;
    g_ptr_array_remove(s->pulsanti, morto);
}

GtkWidget *ph_sistema_pulsante(void *io, PhShellPopup tipo)
{
    PhSistema *s = io;
    if (tipo != PH_POPUP_VOLUME && tipo != PH_POPUP_TELEFONO && tipo != PH_POPUP_NOTIFICHE)
        return NULL;

    icone_prepara();
    Pulsante *p = g_new0(Pulsante, 1);
    p->tipo = tipo;
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "ph-tb");
    GtkWidget *riga = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6); /* gap del mockup */
    gtk_widget_set_halign(riga, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(riga, GTK_ALIGN_CENTER);
    gtk_button_set_child(GTK_BUTTON(btn), riga);
    g_object_set_data_full(G_OBJECT(btn), "ph-sistema-pulsante", p, g_free);

    switch (tipo) {
    case PH_POPUP_VOLUME:
        p->icona_volume = ph_shell_icona_nuova(&icona_volume);
        gtk_box_append(GTK_BOX(riga), p->icona_volume);
        g_signal_connect(btn, "clicked", G_CALLBACK(clic_volume), s);
        break;
    case PH_POPUP_TELEFONO:
        p->icona_rete = ph_shell_icona_nuova(&icona_wifi);
        p->icona_batteria = ph_shell_icona_nuova(batteria_icona(0));
        p->testo = gtk_label_new("");
        gtk_widget_add_css_class(p->testo, "ph-sistema-percentuale");
        gtk_box_append(GTK_BOX(riga), p->icona_rete);
        gtk_box_append(GTK_BOX(riga), p->icona_batteria);
        gtk_box_append(GTK_BOX(riga), p->testo);
        ph_shell_etichetta(btn, ph_t("Stato del telefono"), NULL);
        g_signal_connect(btn, "clicked", G_CALLBACK(clic_telefono), s);
        break;
    default: /* PH_POPUP_NOTIFICHE */
        gtk_box_append(GTK_BOX(riga), ph_shell_icona_nuova(&icona_campanella));
        p->contatore = gtk_label_new("");
        gtk_widget_add_css_class(p->contatore, "ph-sistema-contatore");
        gtk_widget_set_halign(p->contatore, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(p->contatore, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(riga), p->contatore);
        ph_shell_etichetta(btn, ph_t("Notifiche"), NULL);
        g_signal_connect(btn, "clicked", G_CALLBACK(clic_notifiche), s);
        break;
    }

    g_ptr_array_add(s->pulsanti, btn);
    g_object_weak_ref(G_OBJECT(btn), pulsante_morto, s);
    pulsante_aggiorna(s, btn);
    return btn;
}

void ph_sistema_pulsanti_inizia(PhSistema *s) { (void)s; }

void ph_sistema_pulsanti_ferma(PhSistema *s)
{
    /* I pulsanti ancora vivi non devono avvisare uno stato che sparisce. */
    for (guint i = 0; i < s->pulsanti->len; i++)
        g_object_weak_unref(G_OBJECT(g_ptr_array_index(s->pulsanti, i)), pulsante_morto, s);
    g_ptr_array_set_size(s->pulsanti, 0);
}
