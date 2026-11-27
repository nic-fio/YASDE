/* Solo per il demo (compilato in demo-desktop, mai nel prodotto): prova a schermo del widget Note
 * rapide, con i widget veri sul telefono finto. Una sequenza sola, ogni attesa a condizione con
 * scadenza (mai una pausa fissa per «dare tempo»):
 *   1. due widget Note mostrano il testo del profilo (lo stesso buffer), il trascinamento non parte
 *      dal campo ma dal titolo, la rotella non e' bloccata; mostrare il testo non chiede salvataggi;
 *   2. una battuta: subito nel profilo e subito UN salvataggio chiesto (ph_dsk_salva);
 *   3. il secondo widget mostra quanto scritto nel primo;
 *   4. il limite: incollando troppo si inserisce solo la parte che ci sta (a confine di carattere),
 *      i caratteri di controllo non entrano, «\r\n» diventa «\n»; un inserimento rifiutato non
 *      chiede salvataggi;
 *   5. un riallineamento al profilo (altro telefono) cambia il campo ma non chiede salvataggi;
 *   6. senza profilo il campo non e' modificabile e rifiuta il testo, senza salvataggi; col profilo
 *      di nuovo presente si scrive;
 *   7. widget tolto: il testo resta nel profilo;
 *   8. un altro telefono (profilo nuovo): il campo mostra il suo testo, nessun testo del vecchio.
 * Il testo e' un dato dell'utente: i messaggi di questa prova non lo riportano mai.
 *   note-attendi   se all'uscita la prova non e' arrivata in fondo, il processo esce con errore
 *   note-avvia     parte la sequenza (aspetta il profilo del telefono finto, con scadenza) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "interno.h"
#include "logica_note.h"
#include "shell/finto/finto.h"
#include "shell/servizi_backend.h"

void ph_demo_note(PhShell *sh, const char *cosa);
PhFinto *ph_demo_finto(void); /* demo.c */

static bool sequenza_finita;

static void esci_senza_prova(void)
{
    if (!sequenza_finita) {
        fprintf(stderr, "note: la prova non e' arrivata in fondo\n");
        _exit(1);
    }
}

typedef struct {
    PhShell *sh;
    GtkWidget *radice;
    PhDesktop *d;
    int fase;
    gint64 scadenza; /* della fase in corso */
    unsigned base;           /* d->salvataggi prima del passo */
    GtkWidget *vista[2];     /* il campo del widget a destra 16 (0) e di quello a destra 300 (1) */
} Prova;

#define SCADENZA_US (6 * G_USEC_PER_SEC)
#define FALLISCI(...) g_error("note: " __VA_ARGS__)
#define VERIFICA(cond, msg)                                                                  \
    do {                                                                                     \
        if (!(cond))                                                                         \
            FALLISCI("%s", msg);                                                             \
    } while (0)
/* Se non e' ancora cosi' si riprova al prossimo fotogramma, fino alla scadenza della fase. */
#define ATTENDI(v, cond, msg)                                                                \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            if (g_get_monotonic_time() > (v)->scadenza)                                      \
                FALLISCI("scaduto: %s", msg);                                                \
            return G_SOURCE_CONTINUE;                                                        \
        }                                                                                    \
    } while (0)

static void raccogli(GtkWidget *radice, const char *classe, GtkWidget **out, int max, int *n)
{
    for (GtkWidget *c = gtk_widget_get_first_child(radice); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (gtk_widget_has_css_class(c, classe) && *n < max)
            out[(*n)++] = c;
        raccogli(c, classe, out, max, n);
    }
}

static PhDskWidget *widget_di(GtkWidget *vista)
{
    GtkWidget *s = gtk_widget_get_parent(vista);
    while (s != NULL && !gtk_widget_has_css_class(s, "ph-desktop-widget"))
        s = gtk_widget_get_parent(s);
    return s != NULL ? g_object_get_data(G_OBJECT(s), "ph-desktop-widget") : NULL;
}

/* Trova i campi (uno per widget Note) e li ordina per posto: [0] = destra 16, [1] = destra 300. */
static bool trova_viste(Prova *v, int attesi)
{
    GtkWidget *t[8];
    int n = 0;
    raccogli(v->radice, "ph-desktop-note-testo", t, 8, &n);
    if (n != attesi)
        return false;
    if (n == 0)
        return true;
    memset(v->vista, 0, sizeof v->vista);
    for (int i = 0; i < n && i < 2; i++) {
        PhDskWidget *w = widget_di(t[i]);
        if (w == NULL)
            return false;
        int k = (attesi == 1 || w->destra == 16) ? 0 : 1;
        v->vista[k] = t[i];
    }
    return v->vista[0] != NULL && (attesi == 1 || v->vista[1] != NULL);
}

static char *testo_di(GtkWidget *vista)
{
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(vista));
    GtkTextIter i, f;
    gtk_text_buffer_get_bounds(b, &i, &f);
    return gtk_text_buffer_get_text(b, &i, &f, TRUE);
}

static bool testo_e(GtkWidget *vista, const char *atteso)
{
    char *t = testo_di(vista);
    bool ok = strcmp(t, atteso) == 0;
    g_free(t);
    return ok;
}

/* Come l'utente: inserisce dove sta il cursore, passando per le stesse strade della tastiera. */
static void scrivi(GtkWidget *vista, const char *testo)
{
    gtk_text_buffer_insert_interactive_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(vista)), testo, -1, TRUE);
}

static void svuota(GtkWidget *vista)
{
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(vista));
    GtkTextIter i, f;
    gtk_text_buffer_get_bounds(b, &i, &f);
    gtk_text_buffer_delete_interactive(b, &i, &f, TRUE);
}

/* Dopo un riallineamento non c'e' nulla da annullare, e annullare comunque non cambia ne' il
 * profilo ne' i salvataggi. */
static void controlla_senza_annullamento(Prova *v, GtkWidget *vista, const char *motivo)
{
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(vista));
    PhCfgTelefono *p = ph_dsk_profilo(v->d);
    VERIFICA(p != NULL, motivo);
    char *prima = testo_di(vista);
    char *nota = g_strdup(p->note);
    unsigned salv = v->d->salvataggi;
    VERIFICA(!gtk_text_buffer_get_can_undo(b), motivo);
    gtk_text_buffer_undo(b);
    char *dopo = testo_di(vista);
    VERIFICA(strcmp(prima, dopo) == 0 && strcmp(nota, p->note) == 0 && v->d->salvataggi == salv, motivo);
    g_free(prima);
    g_free(nota);
    g_free(dopo);
}

static PhCfgTelefono *profilo(Prova *v)
{
    return ph_dsk_profilo(v->d);
}

static bool nota_e(Prova *v, const char *atteso)
{
    PhCfgTelefono *p = profilo(v);
    return p != NULL && strcmp(p->note, atteso) == 0;
}

/* La prova della maniglia del trascinamento: il gesto di trascinamento della scheda, emesso a mano,
 * in un punto del campo non deve registrare la partenza; sul titolo si'. */
static GtkGesture *gesto_trascinamento(PhDskWidget *w)
{
    GListModel *m = gtk_widget_observe_controllers(w->scheda);
    GtkGesture *g = NULL;
    for (guint i = 0; i < g_list_model_get_n_items(m) && g == NULL; i++) {
        GObject *c = g_list_model_get_item(m, i);
        if (GTK_IS_GESTURE_DRAG(c))
            g = GTK_GESTURE(c); /* il solo trascinamento della scheda (la maniglia ha il suo, sulla maniglia) */
        g_object_unref(c);
    }
    g_object_unref(m);
    return g;
}

/* La rotella sopra il campo deve scorrere il testo: tra il campo e la scheda non c'e' il controller
 * che ferma la rotella sugli altri widget (widget.c lo battezza «ph-desktop-rotella-ferma»). */
static void controlla_rotella(GtkWidget *vista, GtkWidget *scheda)
{
    for (GtkWidget *a = gtk_widget_get_parent(vista); a != NULL && a != gtk_widget_get_parent(scheda);
         a = gtk_widget_get_parent(a)) {
        GListModel *m = gtk_widget_observe_controllers(a);
        for (guint i = 0; i < g_list_model_get_n_items(m); i++) {
            GObject *c = g_list_model_get_item(m, i);
            if (GTK_IS_EVENT_CONTROLLER_SCROLL(c) &&
                g_strcmp0(gtk_event_controller_get_name(GTK_EVENT_CONTROLLER(c)), "ph-desktop-rotella-ferma") == 0)
                FALLISCI("%s", "la rotella sopra il campo e' bloccata da un controller della scheda");
            g_object_unref(c);
        }
        g_object_unref(m);
    }
}

static void controlla_trascinamento(Prova *v)
{
    PhDskWidget *w = widget_di(v->vista[0]);
    VERIFICA(w != NULL && w->scheda != NULL, "widget del campo non trovato");
    controlla_rotella(v->vista[0], w->scheda);
    GtkWidget *campo = gtk_widget_get_ancestor(v->vista[0], GTK_TYPE_SCROLLED_WINDOW);
    graphene_rect_t r;
    VERIFICA(campo != NULL && gtk_widget_compute_bounds(campo, w->scheda, &r), "misura del campo non calcolabile");
    double cx = r.origin.x + r.size.width / 2, cy = r.origin.y + r.size.height / 2;
    VERIFICA(ph_dsk_note_su_campo(w, cx, cy), "il centro del campo non risulta sul campo");
    /* Il titolo sta sopra il campo, dentro la scheda (imbottitura 16): un punto li' non e' sul campo. */
    VERIFICA(!ph_dsk_note_su_campo(w, r.origin.x + r.size.width / 2, r.origin.y / 2),
             "un punto sopra il campo risulta sul campo");
    GtkGesture *g = gesto_trascinamento(w);
    VERIFICA(g != NULL, "gesto di trascinamento non trovato");
    const int SENTINELLA = -12345;
    w->destra0 = SENTINELLA;
    g_signal_emit_by_name(g, "drag-begin", cx, cy);
    VERIFICA(w->destra0 == SENTINELLA, "il trascinamento e' partito dal campo di testo");
    g_signal_emit_by_name(g, "drag-begin", r.origin.x + r.size.width / 2, r.origin.y / 2);
    VERIFICA(w->destra0 == w->destra, "il trascinamento non parte dal titolo");
}

static gboolean fotogramma(GtkWidget *radice, GdkFrameClock *clock, gpointer dati)
{
    (void)radice;
    (void)clock;
    Prova *v = dati;
    PhCfgTelefono *p = profilo(v);
    switch (v->fase) {
    case 0: { /* il telefono finto ha pubblicato il suo profilo: due widget Note e un testo iniziale */
        ATTENDI(v, p != NULL && p->n_icone >= 1, "il profilo del telefono finto non e' arrivato");
        memset(p->widget, 0, sizeof p->widget);
        for (int i = 0; i < 2; i++) {
            p->widget[i].tipo = PH_CFG_WIDGET_NOTE;
            p->widget[i].destra = i == 0 ? 16 : 300;
            p->widget[i].alto = 16;
            p->widget[i].larghezza = 250;
            p->widget[i].altezza = 180;
            p->widget[i].dimensione = PH_CFG_WIDGET_MEDIO;
        }
        p->n_widget = 2;
        g_strlcpy(p->note, "iniziale", sizeof p->note);
        ph_dsk_widget_ricostruisci(v->d);
        v->fase = 1;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 1: { /* 1. i due campi mostrano il testo del profilo e stanno disposti */
        ATTENDI(v, trova_viste(v, 2), "due campi di testo");
        ATTENDI(v, gtk_widget_get_width(v->vista[0]) > 0 && gtk_widget_get_width(v->vista[1]) > 0, "campi disposti");
        VERIFICA(testo_e(v->vista[0], "iniziale") && testo_e(v->vista[1], "iniziale"),
                 "i campi non mostrano il testo del profilo");
        VERIFICA(gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->vista[0])) ==
                     gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->vista[1])),
                 "i due widget non condividono il testo");
        VERIFICA(gtk_text_view_get_editable(GTK_TEXT_VIEW(v->vista[0])), "il campo non e' modificabile col profilo presente");
        controlla_senza_annullamento(v, v->vista[0], "dopo il primo allineamento si puo' annullare");
        controlla_trascinamento(v);
        /* 2. una battuta: nel profilo e un salvataggio chiesto, subito */
        v->base = v->d->salvataggi;
        scrivi(v->vista[0], "A");
        VERIFICA(nota_e(v, "inizialeA"), "la battuta non e' andata subito nel profilo");
        VERIFICA(v->d->salvataggi == v->base + 1, "la battuta non ha chiesto il salvataggio");
        /* L'annullamento delle battute dell'utente deve restare (altrimenti i controlli sul
         * riallineamento non provano nulla). */
        VERIFICA(gtk_text_buffer_get_can_undo(gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->vista[0]))),
                 "le battute dell'utente non si possono annullare");
        /* 3. il secondo widget mostra quanto scritto nel primo */
        VERIFICA(testo_e(v->vista[1], "inizialeA"), "il secondo widget non mostra quanto scritto nel primo");
        v->fase = 2;
        return G_SOURCE_CONTINUE;
    }
    case 2: { /* 4. il limite, scrivendo dal secondo widget */
        svuota(v->vista[1]);
        VERIFICA(nota_e(v, "") && testo_e(v->vista[0], ""), "svuotare il campo non ha svuotato il profilo");
        char *grande = g_malloc(3001);
        for (int i = 0; i < 1500; i++) /* 1500 caratteri da due byte: 3000 byte */
            memcpy(grande + 2 * i, "\xc3\xa9", 2);
        grande[3000] = '\0';
        scrivi(v->vista[1], grande);
        g_free(grande);
        VERIFICA(strlen(p->note) == PH_DSK_NOTE_MAX - 1, "incollando troppo non si e' fermato al confine di un carattere");
        VERIFICA(g_utf8_validate(p->note, -1, NULL), "il testo tagliato non e' UTF-8 valido");
        VERIFICA(testo_e(v->vista[1], p->note) && testo_e(v->vista[0], p->note), "il campo e il profilo non coincidono");
        scrivi(v->vista[1], "x"); /* ne entra ancora uno da un byte... */
        VERIFICA(strlen(p->note) == PH_DSK_NOTE_MAX, "un carattere che ci sta non e' entrato");
        v->base = v->d->salvataggi;
        scrivi(v->vista[1], "y"); /* ...e poi piu' niente: rifiutato, nessun salvataggio */
        VERIFICA(strlen(p->note) == PH_DSK_NOTE_MAX && p->note[PH_DSK_NOTE_MAX - 1] == 'x',
                 "oltre il limite e' entrato altro testo");
        VERIFICA(v->d->salvataggi == v->base, "un inserimento rifiutato ha chiesto un salvataggio");
        svuota(v->vista[1]);
        char *enorme = g_malloc(20001);
        memset(enorme, 'a', 20000);
        enorme[20000] = '\0';
        scrivi(v->vista[0], enorme);
        g_free(enorme);
        VERIFICA(strlen(p->note) == PH_DSK_NOTE_MAX, "un incolla enorme non e' stato tagliato al limite");
        svuota(v->vista[0]);
        scrivi(v->vista[0], "a\r\nb\x01" "c\td");
        VERIFICA(nota_e(v, "a\nbc\td"), "i caratteri di controllo non sono stati scartati");
        VERIFICA(testo_e(v->vista[1], "a\nbc\td"), "il secondo widget non mostra il testo filtrato");
        /* 5. riallineamento al profilo (widget ricostruiti): il campo cambia, nessun salvataggio */
        g_strlcpy(p->note, "riallineato", sizeof p->note);
        v->base = v->d->salvataggi;
        ph_dsk_widget_ricostruisci(v->d);
        v->fase = 3;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 3: { /* 5. (seguito) e 6. senza profilo: campo non modificabile, testo rifiutato, nessun salvataggio */
        ATTENDI(v, trova_viste(v, 2), "i campi dopo la ricostruzione");
        VERIFICA(testo_e(v->vista[0], "riallineato") && testo_e(v->vista[1], "riallineato"),
                 "il riallineamento non ha cambiato il campo");
        VERIFICA(nota_e(v, "riallineato"), "il riallineamento ha cambiato il profilo");
        VERIFICA(v->d->salvataggi == v->base, "il riallineamento ha chiesto un salvataggio");
        ph_dsk_note_profilo_assente_prova(true);
        ph_dsk_widget_ricostruisci(v->d);
        v->fase = 7;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 7: { /* 6. (seguito) */
        ATTENDI(v, trova_viste(v, 2), "i campi dopo la ricostruzione");
        VERIFICA(!gtk_text_view_get_editable(GTK_TEXT_VIEW(v->vista[0])), "senza profilo il campo e' modificabile");
        v->base = v->d->salvataggi;
        scrivi(v->vista[0], "perso");
        VERIFICA(!g_str_has_suffix(p->note, "perso") && v->d->salvataggi == v->base &&
                     testo_e(v->vista[0], "riallineato"),
                 "senza profilo una battuta e' stata accettata");
        ph_dsk_note_profilo_assente_prova(false);
        ph_dsk_widget_ricostruisci(v->d);
        v->fase = 4;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 4: { /* col profilo di nuovo presente si scrive */
        ATTENDI(v, trova_viste(v, 2), "i campi dopo la ricostruzione");
        VERIFICA(gtk_text_view_get_editable(GTK_TEXT_VIEW(v->vista[0])), "col profilo di nuovo presente il campo non e' modificabile");
        v->base = v->d->salvataggi;
        scrivi(v->vista[0], "ultima");
        VERIFICA(g_str_has_suffix(p->note, "ultima") && v->d->salvataggi == v->base + 1, "dopo il ritorno del profilo la battuta non e' andata");
        /* 7. widget tolto: il testo resta nel profilo */
        PhDskWidget *w = widget_di(v->vista[0]);
        VERIFICA(w != NULL, "widget da togliere non trovato");
        ph_dsk_widget_rimuovi(v->d, w->id); /* attenzione: w e i campi sono morti */
        p = profilo(v);
        VERIFICA(p != NULL && p->n_widget == 1 && g_str_has_suffix(p->note, "ultima"), "dopo la rimozione il testo non c'e' piu'");
        v->fase = 5;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 5: { /* il widget rimasto mostra il testo del profilo; poi 8. un altro telefono */
        ATTENDI(v, trova_viste(v, 1), "il campo del widget rimasto");
        VERIFICA(testo_e(v->vista[0], p->note), "il widget rimasto non mostra il testo del profilo");
        PhCfgTelefono *n = ph_cfg_telefono_nuovo();
        VERIFICA(n != NULL, "profilo nuovo non creato");
        n->widget[0].tipo = PH_CFG_WIDGET_NOTE;
        n->widget[0].destra = 16;
        n->widget[0].alto = 16;
        n->widget[0].dimensione = PH_CFG_WIDGET_MEDIO;
        n->n_widget = 1;
        g_strlcpy(n->note, "altro", sizeof n->note);
        ph_servizi_pubblica_profilo(ph_finto_servizi(ph_demo_finto()), n);
        v->fase = 6;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 6: { /* il campo del profilo nuovo; poi un telefono SENZA widget Note */
        ATTENDI(v, trova_viste(v, 1) && testo_e(v->vista[0], "altro"), "il campo con il testo del profilo nuovo");
        VERIFICA(nota_e(v, "altro"), "il testo del vecchio telefono e' finito nel profilo nuovo");
        controlla_senza_annullamento(v, v->vista[0], "dopo il cambio profilo si puo' annullare verso il vecchio telefono");
        PhCfgTelefono *n = ph_cfg_telefono_nuovo();
        VERIFICA(n != NULL, "profilo nuovo non creato");
        n->n_widget = 0;
        g_strlcpy(n->note, "senza widget", sizeof n->note);
        ph_servizi_pubblica_profilo(ph_finto_servizi(ph_demo_finto()), n);
        v->fase = 8;
        v->scadenza = g_get_monotonic_time() + SCADENZA_US;
        return G_SOURCE_CONTINUE;
    }
    case 8: { /* nessun widget Note: se ne aggiunge uno e mostra il testo del nuovo telefono */
        ATTENDI(v, trova_viste(v, 0) && p != NULL && strcmp(p->note, "senza widget") == 0 && p->n_widget == 0,
                "il profilo senza widget");
        ph_dsk_widget_aggiungi(v->d, PH_CFG_WIDGET_NOTE);
        VERIFICA(trova_viste(v, 1), "il widget Note aggiunto non c'e'");
        VERIFICA(testo_e(v->vista[0], "senza widget"), "il widget aggiunto non mostra il testo del nuovo telefono");
        controlla_senza_annullamento(v, v->vista[0], "dopo l'aggiunta si puo' annullare verso il vecchio telefono");
        sequenza_finita = true;
        g_message("note: battuta con salvataggio, due widget, limite, riallineamento, profilo assente, rimozione, cambio profilo, senza annullamento");
        return G_SOURCE_REMOVE;
    }
    default:
        return G_SOURCE_REMOVE;
    }
}

static gboolean passo(gpointer dati)
{
    Prova *v = dati;
    return fotogramma(v->radice, NULL, v);
}

void ph_demo_note(PhShell *sh, const char *cosa)
{
    GtkWidget *radice = ph_shell_radice(sh);
    if (strcmp(cosa, "note-attendi") == 0) {
        atexit(esci_senza_prova);
    } else if (strcmp(cosa, "note-avvia") == 0) {
        PhDesktop *d = g_object_get_data(G_OBJECT(radice), "ph-desktop");
        if (d == NULL)
            FALLISCI("desktop non trovato");
        Prova *v = g_new0(Prova, 1); /* vive quanto il processo */
        v->sh = sh;
        v->radice = radice;
        v->d = d;
        v->scadenza = g_get_monotonic_time() + 10 * G_USEC_PER_SEC; /* include l'attesa del profilo */
        /* Un controllo ogni 10 ms e non a ogni fotogramma: su uno schermo invisibile i fotogrammi sono
         * radi, e le battute devono cadere agli istanti voluti. Finisce da sola (G_SOURCE_REMOVE). */
        g_timeout_add(10, passo, v);
    }
}
