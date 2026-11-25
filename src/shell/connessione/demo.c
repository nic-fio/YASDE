/* demo-connessione: la schermata della prima connessione da sola, sopra il
 * telefono finto. Per vedere gli stati: --stato non-abbinato (predefinito
 * qui), cerca-noto, cerca-nuovo, collegato (opzione di ph_finto_demo).
 * Codici finti: 123456 giusto, 000000 schermata del codice non aperta, altro
 * sbagliato. La finestrella dei comandi (--comandi) ha «Telefono visibile in
 * rete». Come l'ossatura, il demo mostra la schermata solo quando
 * ph_shell_connessione_da_mostrare lo dice.
 *
 * --mostra S: sequenze automatiche (non sono di ph_finto_demo):
 *   errore | in-corso | riuscito | indirizzo   per le foto (dopo 1 s, 2,5 s per
 *                                               in-corso, scrive nel campo come
 *                                               l'utente e preme Invio);
 *   chiudi-in-corso   preme Invio e subito dopo, nello stesso giro del ciclo,
 *                     chiude la finestra mentre l'abbinamento e' in corso (prova
 *                     di chiusura: va sotto AddressSanitizer, nessun esito deve
 *                     arrivare dopo la distruzione).
 * --verifica: prova con i widget veri (digitazione e incolla, doppio Invio,
 *   campo vuoto dopo ogni esito e dopo il nascondimento, ritorno a
 *   NON_ABBINATO); il codice d'uscita e' 0 solo se tutto torna. Si usa
 *   insieme a --prova MS (che da' 77 = saltata se non c'e' uno schermo).
 * Codici e indirizzo qui sono sempre finti e non vengono mai scritti. */
#include <string.h>

#include "config/lingua.h"
#include "connessione.h"
#include "shell/finto/finto.h"
#include "shell/servizi_backend.h"

#define PASSI_MAX 16
#define ATTESA_TICK 400 /* tick da 20 ms: scadenza di 8 s per passo */

typedef struct Demo Demo;
typedef struct {
    const char *nome;
    void (*azione)(Demo *);     /* eseguita una volta all'ingresso nel passo */
    bool (*pronto)(Demo *);     /* NULL = subito; altrimenti si attende (con scadenza) */
    void (*controlli)(Demo *);  /* dopo che il passo e' pronto */
} Passo;

struct Demo {
    GtkWidget *schermata;
    PhServizi *servizi;
    guint id_ascolto;
    bool collegato_in_sessione;
    const char *mostra; /* sequenza a tempo per le foto o la prova di chiusura */
    bool verifica;
    int errori;
    /* verifica */
    guint id_tick;
    int passo, tick;
    bool entrato;
    bool visto_riprova;   /* la frase «Riprova tra un momento.» (esito RIFIUTATA) e' mai comparsa */
    char frase_prec[256]; /* ultima frase d'errore vista (solo frasi, mai codici) */
};

/* Cerca nell'albero il primo widget con quella classe CSS. */
static GtkWidget *trova(GtkWidget *w, const char *classe)
{
    if (gtk_widget_has_css_class(w, classe))
        return w;
    for (GtkWidget *f = gtk_widget_get_first_child(w); f != NULL; f = gtk_widget_get_next_sibling(f)) {
        GtkWidget *r = trova(f, classe);
        if (r != NULL)
            return r;
    }
    return NULL;
}

static GtkWidget *w_codice(Demo *d) { return trova(d->schermata, "ph-connessione-codice"); }
static GtkWidget *w_errore(Demo *d) { return trova(d->schermata, "ph-connessione-errore"); }
static GtkWidget *w_abbina(Demo *d) { return trova(d->schermata, "ph-accento"); }
static GtkWidget *w_cerca(Demo *d) { return trova(d->schermata, "ph-ghost"); }

static void finestra_chiudi(Demo *d)
{
    GtkRoot *r = gtk_widget_get_root(d->schermata);
    if (r != NULL)
        gtk_window_close(GTK_WINDOW(r));
}

/* ---- Sequenze per le foto e prova di chiusura --------------------------- */

/* Come farebbe l'utente: scrive nel campo e preme Invio. Codici finti. */
static gboolean sequenza(gpointer p)
{
    Demo *d = p;
    GtkWidget *codice = w_codice(d);
    if (codice == NULL)
        return G_SOURCE_REMOVE;
    if (strcmp(d->mostra, "indirizzo") == 0) {
        GtkWidget *esp = trova(d->schermata, "ph-connessione-espansore");
        GtkWidget *ind = trova(d->schermata, "ph-connessione-indirizzo");
        gtk_expander_set_expanded(GTK_EXPANDER(esp), TRUE);
        gtk_editable_set_text(GTK_EDITABLE(ind), "192.0.2.20:37099");
        gtk_editable_set_text(GTK_EDITABLE(codice), "12345");
        return G_SOURCE_REMOVE;
    }
    gtk_editable_set_text(GTK_EDITABLE(codice), strcmp(d->mostra, "errore") == 0 ? "999999" : "123456");
    g_signal_emit_by_name(codice, "activate");
    return G_SOURCE_REMOVE;
}

/* Chiusura in un idle subito dopo l'Invio: nessun tempo fisso, l'abbinamento
 * (600 ms nel telefono finto) e' certamente ancora in corso. */
static gboolean chiudi_ora(gpointer p)
{
    finestra_chiudi(p);
    return G_SOURCE_REMOVE;
}

/* Si aspetta (a scatti, con scadenza) che il campo sia attivo, poi Invio e
 * chiusura nello stesso giro del ciclo. */
static gboolean sequenza_chiudi(gpointer p)
{
    Demo *d = p;
    GtkWidget *codice = w_codice(d);
    if (codice == NULL || !gtk_widget_get_visible(codice) || !gtk_widget_get_sensitive(codice)) {
        if (++d->tick > ATTESA_TICK) {
            fprintf(stderr, "chiudi-in-corso: la schermata del codice non e' comparsa\n");
            d->errori++;
            finestra_chiudi(d);
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }
    gtk_editable_set_text(GTK_EDITABLE(codice), "123456");
    g_signal_emit_by_name(codice, "activate");
    g_idle_add(chiudi_ora, d);
    return G_SOURCE_REMOVE;
}

/* ---- Verifica con i widget veri ------------------------------------------ */

static void controlla(Demo *d, bool ok, const char *cosa)
{
    if (!ok) {
        fprintf(stderr, "verifica: FALLITO passo %d: %s\n", d->passo, cosa);
        d->errori++;
    }
}

/* Scrive come l'utente (digitazione o incolla): tastiera e appunti arrivano al
 * GtkText dentro la GtkEntry (il suo delegate), non alla GtkEntry: il filtro
 * del campo deve stare li', e la prova passa di li'. */
static void scrivi(GtkWidget *campo, const char *testo)
{
    GtkEditable *delegato = gtk_editable_get_delegate(GTK_EDITABLE(campo));
    int pos = (int)strlen(gtk_editable_get_text(GTK_EDITABLE(campo)));
    gtk_editable_insert_text(delegato, testo, -1, &pos);
}

/* Registra ogni cambio della frase d'errore: una seconda richiesta (RIFIUTATA)
 * farebbe comparire «Riprova tra un momento.» anche solo per un istante. */
static void su_frase(GObject *o, GParamSpec *ps, gpointer u)
{
    (void)ps;
    Demo *d = u;
    if (strcmp(gtk_label_get_text(GTK_LABEL(o)), "Riprova tra un momento.") == 0)
        d->visto_riprova = true;
}

static const char *testo_di(GtkWidget *campo) { return gtk_editable_get_text(GTK_EDITABLE(campo)); }

static bool esito_arrivato(Demo *d)
{
    GtkWidget *e = w_errore(d);
    return gtk_widget_get_visible(e) && gtk_widget_get_sensitive(w_codice(d)) &&
           !ph_srv_abbinamento_in_corso(d->servizi);
}

/* 1. digitazione e incolla */
static void a_digita(Demo *d)
{
    GtkWidget *c = w_codice(d);
    scrivi(c, "12a3");
    controlla(d, strcmp(testo_di(c), "123") == 0, "digitando «12a3» il campo deve avere «123»");
    controlla(d, !gtk_widget_get_sensitive(w_abbina(d)), "«Abbina» inattivo con 3 cifre");
    gtk_editable_set_text(GTK_EDITABLE(c), "");
    scrivi(c, "123 456");
    controlla(d, strcmp(testo_di(c), "123456") == 0, "incollando «123 456» il campo deve avere «123456»");
    controlla(d, gtk_widget_get_sensitive(w_abbina(d)), "«Abbina» attivo con 6 cifre");
    scrivi(c, "7");
    controlla(d, strcmp(testo_di(c), "123456") == 0, "al massimo 6 cifre");
    gtk_editable_set_text(GTK_EDITABLE(c), "");
    scrivi(c, "12345");
    scrivi(c, "6789");
    controlla(d, strcmp(testo_di(c), "123456") == 0, "incollando oltre il limite restano 6 cifre");
    gtk_editable_set_text(GTK_EDITABLE(c), "");
    controlla(d, !gtk_widget_get_sensitive(w_abbina(d)), "«Abbina» inattivo a campo vuoto");
    /* Indirizzo manuale non valido: «Abbina» resta inattivo. */
    /* Il campo dell'indirizzo sta nell'albero solo a espansore aperto. */
    gtk_expander_set_expanded(GTK_EXPANDER(trova(d->schermata, "ph-connessione-espansore")), TRUE);
    GtkWidget *ind = trova(d->schermata, "ph-connessione-indirizzo");
    scrivi(c, "123456");
    gtk_editable_set_text(GTK_EDITABLE(ind), "192.0.2.20:0");
    controlla(d, !gtk_widget_get_sensitive(w_abbina(d)), "indirizzo non valido: «Abbina» inattivo");
    gtk_editable_set_text(GTK_EDITABLE(ind), "192.0.2.20:5555");
    controlla(d, gtk_widget_get_sensitive(w_abbina(d)), "indirizzo valido: «Abbina» attivo");
    gtk_editable_set_text(GTK_EDITABLE(ind), "");
    gtk_editable_set_text(GTK_EDITABLE(c), "");
    gtk_expander_set_expanded(GTK_EXPANDER(trova(d->schermata, "ph-connessione-espansore")), FALSE);
}

/* 2. doppio Invio: una sola richiesta (la seconda sarebbe RIFIUTATA e cambierebbe la frase) */
static void a_doppio(Demo *d)
{
    GtkWidget *c = w_codice(d);
    g_signal_connect(w_errore(d), "notify::label", G_CALLBACK(su_frase), d);
    scrivi(c, "999999");
    g_signal_emit_by_name(c, "activate");
    controlla(d, strcmp(testo_di(c), "") == 0, "il campo si svuota subito dopo l'Invio");
    controlla(d, !gtk_widget_get_sensitive(c) && !gtk_widget_get_sensitive(w_abbina(d)),
              "durante il tentativo campo e «Abbina» inattivi");
    /* Secondo Invio con un codice di nuovo nel campo: non deve partire nulla. */
    scrivi(c, "123456");
    g_signal_emit_by_name(c, "activate");
    gtk_editable_set_text(GTK_EDITABLE(c), "");
}
static void c_doppio(Demo *d)
{
    controlla(d, !d->visto_riprova, "doppio Invio: la seconda richiesta non deve partire (mai «Riprova tra un momento.»)");
    const char *frase = gtk_label_get_text(GTK_LABEL(w_errore(d)));
    controlla(d, frase[0] != '\0' && strcmp(frase, "Riprova tra un momento.") != 0,
              "un solo tentativo: la frase e' quella dell'esito, non «Riprova tra un momento.»");
    controlla(d, strcmp(testo_di(w_codice(d)), "") == 0, "campo vuoto dopo l'esito (sbagliato)");
    controlla(d, gtk_widget_has_focus(w_codice(d)) || gtk_widget_get_sensitive(w_codice(d)),
              "campo di nuovo attivo");
    g_strlcpy(d->frase_prec, frase, sizeof d->frase_prec);
}

/* 3. «schermata non aperta» */
static void a_zeri(Demo *d)
{
    GtkWidget *c = w_codice(d);
    scrivi(c, "000000");
    g_signal_emit_by_name(c, "activate");
}
static void c_zeri(Demo *d)
{
    const char *frase = gtk_label_get_text(GTK_LABEL(w_errore(d)));
    controlla(d, strstr(frase, "Apri sul telefono") != NULL, "000000: la frase «Apri sul telefono…»");
    controlla(d, strcmp(testo_di(w_codice(d)), "") == 0, "campo vuoto dopo l'esito (000000)");
}

/* 4. nascondimento: campo vuoto, errore tolto */
static void a_nascondi(Demo *d)
{
    scrivi(w_codice(d), "12");
    gtk_widget_set_visible(d->schermata, FALSE);
    gtk_widget_set_visible(d->schermata, TRUE);
    controlla(d, strcmp(testo_di(w_codice(d)), "") == 0, "campo vuoto dopo il nascondimento");
    controlla(d, !gtk_widget_get_visible(w_errore(d)), "errore tolto dopo il nascondimento");
}

/* 5. nascosta e rimostrata durante il tentativo: inattiva finche' il servizio abbina */
static void a_rimostra(Demo *d)
{
    GtkWidget *c = w_codice(d);
    scrivi(c, "999999");
    g_signal_emit_by_name(c, "activate");
    gtk_widget_set_visible(d->schermata, FALSE);
    gtk_widget_set_visible(d->schermata, TRUE);
    controlla(d, ph_srv_abbinamento_in_corso(d->servizi), "il servizio sta ancora abbinando");
    controlla(d, !gtk_widget_get_sensitive(w_codice(d)) && !gtk_widget_get_sensitive(w_abbina(d)),
              "rimostrata durante l'abbinamento: campo e «Abbina» inattivi");
    controlla(d, strcmp(testo_di(w_codice(d)), "") == 0, "campo vuoto");
}
static bool p_rimostra(Demo *d)
{
    return gtk_widget_get_sensitive(w_codice(d)) && !ph_srv_abbinamento_in_corso(d->servizi);
}
static void c_rimostra(Demo *d)
{
    controlla(d, !gtk_widget_get_visible(w_errore(d)), "l'esito di una richiesta annullata non arriva");
    controlla(d, gtk_widget_get_sensitive(w_codice(d)), "finito l'abbinamento il campo torna attivo");
}

/* 6. codice giusto: la schermata sparisce (come farebbe l'ossatura) */
static void a_giusto(Demo *d)
{
    GtkWidget *c = w_codice(d);
    scrivi(c, "123456");
    g_signal_emit_by_name(c, "activate");
}
static bool p_sparita(Demo *d) { return !gtk_widget_get_visible(d->schermata); }

/* 7. ritorno a NON_ABBINATO: la schermata riappare con il campo vuoto e attivo */
static void a_ritorno(Demo *d)
{
    PhSrvCollegamento c = {0};
    c.stato = PH_SRV_CERCO;
    c.telefoni_noti = 1;
    ph_servizi_pubblica_collegamento(d->servizi, &c);
    c.stato = PH_SRV_NON_ABBINATO;
    c.telefoni_trovati = 1;
    ph_servizi_pubblica_collegamento(d->servizi, &c);
}
static bool p_riappare(Demo *d)
{
    return gtk_widget_get_visible(d->schermata) && gtk_widget_get_visible(w_codice(d));
}
static void c_riappare(Demo *d)
{
    controlla(d, strcmp(testo_di(w_codice(d)), "") == 0, "ritorno a NON_ABBINATO: campo vuoto");
    controlla(d, gtk_widget_get_sensitive(w_codice(d)), "ritorno a NON_ABBINATO: campo attivo");
    controlla(d, !gtk_widget_get_visible(w_errore(d)), "ritorno a NON_ABBINATO: nessun errore");
    controlla(d, w_cerca(d) != NULL && gtk_widget_get_sensitive(w_cerca(d)), "«Cerca di nuovo» attivo");
}

static void a_fine(Demo *d) { finestra_chiudi(d); }

static void a_niente(Demo *d) { (void)d; }
static bool p_avviata(Demo *d)
{
    GtkWidget *c = w_codice(d);
    return c != NULL && gtk_widget_get_visible(c) && gtk_widget_get_sensitive(c);
}

/* La carta e' una superficie di vetro chiaro; la radice (a tutta area) no. */
static void c_avviata(Demo *d)
{
    GtkWidget *carta = trova(d->schermata, "ph-dialogo");
    controlla(d, carta != NULL && gtk_widget_has_css_class(carta, "ph-vetro-chiaro"),
              "la carta ha la classe ph-vetro-chiaro");
    controlla(d, !gtk_widget_has_css_class(d->schermata, "ph-vetro-chiaro") &&
                     !gtk_widget_has_css_class(d->schermata, "ph-vetro-scuro"),
              "la radice a tutta area non e' una superficie di vetro");
}

static const Passo PASSI[] = {
    {"avvio: schermata del codice", a_niente, p_avviata, c_avviata},
    {"digitazione e incolla", a_digita, NULL, NULL},
    {"doppio Invio", a_doppio, esito_arrivato, c_doppio},
    {"codice 000000", a_zeri, esito_arrivato, c_zeri},
    {"nascondimento", a_nascondi, NULL, NULL},
    {"rimostrata durante il tentativo", a_rimostra, p_rimostra, c_rimostra},
    {"codice giusto", a_giusto, p_sparita, NULL},
    {"ritorno a NON_ABBINATO", a_ritorno, p_riappare, c_riappare},
    {"fine", a_fine, NULL, NULL},
};

/* Un passo alla volta: azione, poi attesa della condizione (con scadenza, mai
 * un tempo fisso), poi i controlli. */
static gboolean tick(gpointer p)
{
    Demo *d = p;
    if ((size_t)d->passo >= G_N_ELEMENTS(PASSI)) {
        d->id_tick = 0;
        return G_SOURCE_REMOVE;
    }
    const Passo *ps = &PASSI[d->passo];
    if (!d->entrato) {
        d->entrato = true;
        d->tick = 0;
        ps->azione(d);
    }
    if (ps->pronto != NULL && !ps->pronto(d)) {
        if (++d->tick > ATTESA_TICK) {
            fprintf(stderr, "verifica: FALLITO scadenza nel passo «%s»\n", ps->nome);
            d->errori++;
            d->id_tick = 0;
            finestra_chiudi(d);
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }
    if (ps->controlli != NULL)
        ps->controlli(d);
    d->passo++;
    d->entrato = false;
    return G_SOURCE_CONTINUE;
}

/* ---- Collegamento con la schermata ---------------------------------------- */

static void aggiorna(PhServizi *s, PhSrvEvento ev, const char *arg, void *u)
{
    (void)ev;
    (void)arg;
    Demo *d = u;
    PhSrvCollegamento c;
    ph_srv_collegamento(s, &c);
    if (c.stato == PH_SRV_COLLEGATO || c.stato == PH_SRV_BLOCCATO)
        d->collegato_in_sessione = true;
    gtk_widget_set_visible(d->schermata, ph_shell_connessione_da_mostrare(c.stato, d->collegato_in_sessione));
}

/* Alla distruzione della schermata si smette di ascoltare (regola «Ordine di
 * distruzione» di CONTRATTI.md: solo funzioni *_smetti), e si ferma la verifica. */
static void su_distruggi(GtkWidget *w, gpointer u)
{
    (void)w;
    Demo *d = u;
    if (d->id_ascolto != 0)
        ph_srv_smetti(d->servizi, d->id_ascolto);
    d->id_ascolto = 0;
    if (d->id_tick != 0)
        g_source_remove(d->id_tick);
    d->id_tick = 0;
}

/* La finestra e' di ph_finto_demo, che la distrugge da se' dopo il ciclo: la
 * richiesta di chiusura fa solo uscire dal ciclo (il suo gestore) e qui si
 * impedisce la distruzione di default, che sarebbe una seconda distruzione. */
static gboolean su_chiusura(GtkWindow *w, gpointer u)
{
    (void)w;
    (void)u;
    return TRUE;
}

static void installa(PhShell *sh, PhFinto *f, void *u)
{
    (void)f;
    Demo *d = u;
    d->servizi = ph_shell_servizi(sh);
    d->schermata = ph_connessione_nuova(sh);
    GtkRoot *radice_finestra = gtk_widget_get_root(ph_shell_radice(sh));
    if (radice_finestra != NULL)
        g_signal_connect(radice_finestra, "close-request", G_CALLBACK(su_chiusura), d);
    ph_shell_strato_aggiungi(sh, PH_STRATO_VELO, d->schermata);
    g_signal_connect(d->schermata, "destroy", G_CALLBACK(su_distruggi), d);
    d->id_ascolto = ph_srv_ascolta(d->servizi, PH_SRV_EV_COLLEGAMENTO, aggiorna, d);
    aggiorna(d->servizi, PH_SRV_EV_COLLEGAMENTO, NULL, d);
    if (d->verifica)
        d->id_tick = g_timeout_add(20, tick, d);
    else if (d->mostra != NULL && strcmp(d->mostra, "chiudi-in-corso") == 0)
        g_timeout_add(20, sequenza_chiudi, d);
    else if (d->mostra != NULL)
        g_timeout_add(strcmp(d->mostra, "in-corso") == 0 ? 2500 : 1000, sequenza, d);
}

int main(int argc, char **argv)
{
    /* Come il programma vero: l'app e' solo in inglese. */
    ph_lingua_inizializza(PH_CFG_LINGUA_AUTO);
    /* Senza --stato si parte non abbinati (lo stato che interessa qui). */
    bool ha_stato = false;
    Demo d = {0};
    char **args = g_new0(char *, (gsize)argc + 3);
    int n = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--stato") == 0)
            ha_stato = true;
        if (strcmp(argv[i], "--mostra") == 0 && i + 1 < argc) {
            d.mostra = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--verifica") == 0) {
            d.verifica = true;
            continue;
        }
        args[n++] = argv[i];
    }
    if (!ha_stato) {
        args[n++] = (char *)"--stato";
        args[n++] = (char *)"non-abbinato";
    }
    int r = ph_finto_demo(n, args, "YASDE", installa, &d);
    g_free(args);
    if (r == 0 && d.verifica && d.passo < (int)G_N_ELEMENTS(PASSI) - 1) {
        fprintf(stderr, "verifica: FALLITO: la finestra si e' chiusa prima della fine\n");
        d.errori++;
    }
    return r != 0 ? r : (d.errori > 0 ? 1 : 0);
}
