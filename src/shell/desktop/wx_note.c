/* Widget Note rapide: un campo di testo a piu' righe dentro il vetro, che si scrive direttamente.
 * Il testo e' UNO per telefono (PhCfgTelefono.note) e sta in un solo GtkTextBuffer del desktop,
 * condiviso da tutti i widget Note: scrivere in uno aggiorna gli altri senza cicli e senza
 * muovere il cursore di chi scrive (e' lo stesso buffer, non c'e' nulla da riallineare).
 *
 * A ogni modifica dell'utente il testo va in p->note e SUBITO si chiede ph_dsk_salva: il rinvio
 * (al massimo una scrittura al secondo mentre si scrive) lo fa il servizio, che salva anche il
 * profilo vecchio prima di sostituirlo con quello di un altro telefono e alla chiusura. Il widget
 * quindi non ha timer, ne' salvataggi sul fuoco perso o alla rimozione. Un riallineamento del
 * campo al profilo (nuovo widget, altro telefono) NON e' una modifica e non chiede salvataggi.
 * Senza profilo il campo non e' modificabile e non accetta testo (le battute sparirebbero).
 *
 * Riservatezza: il testo e' un dato dell'utente. Niente di questo file lo scrive in un log o in un
 * messaggio d'errore (nemmeno la lunghezza).
 *
 * Il trascinamento del widget NON parte dal campo (li' il mouse seleziona): lo nega widget.c con
 * ph_dsk_note_su_campo. Il clic destro sul campo apre un menu della shell (non il popover del
 * testo di GTK, che sta fuori dal sistema di popup) con Taglia, Copia, Incolla, Seleziona tutto e
 * Rimuovi widget; Incolla passa dal buffer, quindi dal filtro e dal limite. */
#include <string.h>

#include "interno.h"
#include "logica_note.h"

typedef struct {
    PhDskWidget *w;
    GtkWidget *campo; /* la finestra che scorre */
    GtkWidget *vista; /* il campo di testo */
    GtkEventController *tasti;
    GtkGesture *destro;
} Note;

/* ---- Il buffer condiviso -------------------------------------------------------------------- */
static bool g_profilo_assente_prova = false;

/* Solo per le prove del demo: finge che il profilo non ci sia (NULL = come prima). */
void ph_dsk_note_profilo_assente_prova(bool assente)
{
    g_profilo_assente_prova = assente;
}

static PhCfgTelefono *profilo_note(PhDesktop *d)
{
    return g_profilo_assente_prova ? NULL : ph_dsk_profilo(d);
}

static char *buffer_testo(GtkTextBuffer *b)
{
    GtkTextIter i, f;
    gtk_text_buffer_get_bounds(b, &i, &f);
    return gtk_text_buffer_get_text(b, &i, &f, TRUE);
}

/* Prima che il testo entri: si tiene solo cio' che e' ammesso e che ci sta (ph_dsk_note_filtra).
 * Se qualcosa cambia, l'inserimento originale si ferma e si inserisce la parte buona. Il testo
 * filtrato passa di qui una seconda volta e ne esce uguale: la ricorsione finisce li'. */
static void buffer_inserisci(GtkTextBuffer *b, GtkTextIter *pos, char *testo, int len, gpointer dati)
{
    PhDesktop *d = dati;
    if (!d->note_mio && profilo_note(d) == NULL) {
        g_signal_stop_emission_by_name(b, "insert-text"); /* senza profilo le battute sparirebbero */
        return;
    }
    size_t n = len < 0 ? strlen(testo) : (size_t)len;
    char *attuale = buffer_testo(b);
    size_t posto = ph_dsk_note_posto(strlen(attuale));
    g_free(attuale);
    char *buono = g_malloc(n + 1);
    size_t k = ph_dsk_note_filtra(testo, n, posto, buono, n + 1);
    if (k == n && memcmp(buono, testo, n) == 0) {
        g_free(buono);
        return; /* niente da cambiare: l'inserimento prosegue */
    }
    g_signal_stop_emission_by_name(b, "insert-text");
    if (k > 0)
        gtk_text_buffer_insert(b, pos, buono, (int)k);
    g_free(buono);
}

/* Dopo ogni modifica del testo: nel profilo e subito la richiesta di salvataggio (il servizio
 * rinvia e raggruppa). */
static void buffer_cambiato(GtkTextBuffer *b, gpointer dati)
{
    PhDesktop *d = dati;
    if (d->note_mio || !d->vivo)
        return; /* l'ha messo il programma stesso (allineamento al profilo) */
    PhCfgTelefono *p = profilo_note(d);
    if (p == NULL)
        return;
    char *t = buffer_testo(b);
    g_strlcpy(p->note, t, sizeof p->note); /* il filtro garantisce che ci sta */
    g_free(t);
    ph_dsk_salva(d);
}

/* Il buffer mostra il testo del profilo (se e' gia' cosi' non si tocca niente). */
static void buffer_allinea(PhDesktop *d)
{
    PhCfgTelefono *p = profilo_note(d);
    if (d->note_testo == NULL || p == NULL)
        return;
    char sicuro[PH_CFG_LUN_NOTE];
    /* Il profilo e' gia' valido (config lo garantisce); si filtra lo stesso: costa niente. */
    ph_dsk_note_filtra(p->note, strnlen(p->note, sizeof p->note), PH_DSK_NOTE_MAX, sicuro, sizeof sicuro);
    char *ora = buffer_testo(d->note_testo);
    bool uguale = strcmp(ora, sicuro) == 0;
    g_free(ora);
    if (uguale)
        return;
    /* Azione irreversibile: il testo del profilo non entra nella cronologia di annullamento (e la
     * cronologia si azzera), altrimenti Ctrl+Z riporterebbe il testo di un altro telefono e lo
     * salverebbe in questo profilo. L'annullamento delle battute dell'utente resta.
     * Alcune versioni di GTK azzerano gia' la cronologia con set_text: li' la prova a schermo
     * passa anche senza queste due righe, che restano come protezione per le altre. */
    d->note_mio = true;
    gtk_text_buffer_begin_irreversible_action(d->note_testo);
    gtk_text_buffer_set_text(d->note_testo, sicuro, -1);
    gtk_text_buffer_end_irreversible_action(d->note_testo);
    d->note_mio = false;
}

static void buffer_assicura(PhDesktop *d)
{
    if (!d->vivo)
        return; /* dopo la chiusura non nasce un buffer con gestori su `d` che nessuno scollega */
    if (d->note_testo == NULL) {
        d->note_testo = gtk_text_buffer_new(NULL);
        g_signal_connect(d->note_testo, "insert-text", G_CALLBACK(buffer_inserisci), d);
        g_signal_connect(d->note_testo, "changed", G_CALLBACK(buffer_cambiato), d);
    }
    buffer_allinea(d);
}

/* Chiusura del desktop (dal "destroy" della radice): il buffer si stacca da noi; i campi ancora
 * vivi tengono il loro riferimento. */
void ph_dsk_note_chiudi(PhDesktop *d)
{
    if (d->note_menu_vista != NULL)
        g_object_remove_weak_pointer(G_OBJECT(d->note_menu_vista), (gpointer *)&d->note_menu_vista);
    d->note_menu_vista = NULL;
    if (d->note_testo != NULL) {
        g_signal_handlers_disconnect_by_data(d->note_testo, d);
        g_clear_object(&d->note_testo);
    }
}

/* ---- Gesti del campo ------------------------------------------------------------------------- */
static void apri_menu(Note *n, double x, double y)
{
    double rx, ry;
    ph_dsk_in_radice(n->w->d, n->vista, x, y, &rx, &ry);
    ph_dsk_menu_note(n->w->d, n->w->id, n->vista, rx, ry);
}

/* Clic destro: in fase di cattura, cosi' il campo non apre il suo menu del toolkit. */
static void destro_premuto(GtkGestureClick *g, int n_press, double x, double y, gpointer dati)
{
    (void)n_press;
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
    apri_menu(dati, x, y);
}

/* Tasto Menu o Maiusc+F10 con il fuoco nel campo: lo stesso menu, accanto al campo. */
static gboolean tasto_premuto(GtkEventControllerKey *c, guint tasto, guint codice, GdkModifierType stato,
                              gpointer dati)
{
    (void)c;
    (void)codice;
    if (tasto == GDK_KEY_Menu || (tasto == GDK_KEY_F10 && (stato & GDK_SHIFT_MASK))) {
        apri_menu(dati, 16, 16);
        return TRUE;
    }
    return FALSE;
}

bool ph_dsk_note_su_campo(PhDskWidget *w, double x, double y)
{
    Note *n = w->x;
    if (n == NULL || w->scheda == NULL)
        return false;
    GtkWidget *p = gtk_widget_pick(w->scheda, x, y, GTK_PICK_DEFAULT);
    return p != NULL && (p == n->campo || gtk_widget_is_ancestor(p, n->campo));
}

/* ---- Tabella ---------------------------------------------------------------------------------- */
static void note_inizia(PhDskWidget *w)
{
    PhDesktop *d = w->d;
    Note *n = g_new0(Note, 1);
    n->w = w;
    w->x = n;
    buffer_assicura(d);
    if (d->note_testo == NULL) { /* desktop gia' chiuso: nessun campo */
        g_free(n);
        w->x = NULL;
        return;
    }

    n->vista = gtk_text_view_new_with_buffer(d->note_testo);
    gtk_widget_add_css_class(n->vista, "ph-desktop-note-testo");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(n->vista), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(n->vista), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(n->vista), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(n->vista), 9);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(n->vista), 9);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(n->vista), profilo_note(d) != NULL);
    ph_shell_etichetta(n->vista, ph_t("Note rapide"), NULL);

    n->campo = gtk_scrolled_window_new();
    gtk_widget_add_css_class(n->campo, "ph-desktop-note-campo");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(n->campo), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(n->campo), 32);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(n->campo), n->vista);
    gtk_widget_set_hexpand(n->campo, TRUE);
    gtk_widget_set_vexpand(n->campo, TRUE);

    n->destro = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(n->destro), GDK_BUTTON_SECONDARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(n->destro), GTK_PHASE_CAPTURE);
    g_signal_connect(n->destro, "pressed", G_CALLBACK(destro_premuto), n);
    gtk_widget_add_controller(n->vista, GTK_EVENT_CONTROLLER(n->destro));
    n->tasti = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(n->tasti, GTK_PHASE_CAPTURE);
    g_signal_connect(n->tasti, "key-pressed", G_CALLBACK(tasto_premuto), n);
    gtk_widget_add_controller(n->vista, n->tasti);

    gtk_widget_set_vexpand(w->corpo, TRUE);
    gtk_box_append(GTK_BOX(w->corpo), n->campo);
}

static void note_ferma(PhDskWidget *w)
{
    Note *n = w->x;
    /* I gesti restano attaccati al campo, che puo' vivere un poco piu' di noi: da qui in poi non
     * devono piu' chiamarci. */
    g_signal_handlers_disconnect_by_data(n->destro, n);
    g_signal_handlers_disconnect_by_data(n->tasti, n);
    g_free(n);
    w->x = NULL;
}

static void note_aggiorna(PhDskWidget *w, PhDskVista *v)
{
    /* Il campo c'e' gia' (costruito in inizia): qui solo il titolo, da dove si trascina. */
    g_strlcpy(v->titolo, ph_t("Note rapide"), sizeof v->titolo);
    gtk_widget_set_visible(w->corpo, TRUE);
}

const PhDskWxVt PH_DSK_WX_NOTE = {
    .inizia = note_inizia,
    .ferma = note_ferma,
    .aggiorna = note_aggiorna,
};
