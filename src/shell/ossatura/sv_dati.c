/* Servizi veri: stato del telefono, notifiche, app e icone, pannello fisico
 * (protocollo 2.9), appunti (4.11).
 *
 * PANNELLO. Regola del committente del 23 nov 2026 (PIANO.md, in fondo; vale al
 * posto della specifica 4.7): finche' YASDE e' collegata, chiudere l'ultima
 * finestra NON riaccende il pannello. Lo riaccendono solo l'utente (sblocco sul
 * telefono, interruttore «pannello fisico» del popup), una chiamata, la fine
 * della chiamata, e la chiusura o la caduta di YASDE (ripristino lato
 * telefono e guardiano: non sono qui). Qui il pannello si spegne solo per la
 * regola del telefono «in mano», per l'interruttore e, dal 24 nov 2026, appena il
 * collegamento e' riuscito (un solo tentativo, se il telefono non e' «in
 * mano»). Tutto il lavoro sta sui pool; i
 * risultati tornano con le ph_servizi_pubblica_*. Nessun titolo di notifica
 * e nessun testo degli appunti finisce in un log. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sv_interno.h"

#if PH_OSSATURA_SERVIZI_VERI

#define MSG_PANNELLO_IMPOSTA 0x60
#define MSG_PANNELLO_STATO 0x61
#define MSG_PANNELLO_EVENTO 0x6e

/* ---- Stato del telefono -------------------------------------------------------- */
static void tel_pubblica_copia(Sv *sv)
{
    PhSrvTelefono t;
    g_mutex_lock(&sv->tm);
    t = sv->tel;
    g_mutex_unlock(&sv->tm);
    ph_servizi_pubblica_telefono(sv->serv, &t);
}

/* Evento 0x7e (thread del componente: tornare subito). */
void sv_ev_stato(const PhDatiStato *stato, void *utente)
{
    Sv *sv = utente;
    g_mutex_lock(&sv->tm);
    ph_ossatura_telefono_da_stato(stato, &sv->tel);
    g_mutex_unlock(&sv->tm);
    tel_pubblica_copia(sv);
}

/* Evento 0x6e (thread del componente: tornare subito). */
static void ev_pannello(uint8_t tipo, const uint8_t *corpo, size_t n, void *utente)
{
    Sv *sv = utente;
    if (tipo != MSG_PANNELLO_EVENTO)
        return;
    g_mutex_lock(&sv->tm);
    bool ok = ph_ossatura_pannello_da_evento(corpo, n, &sv->tel);
    g_mutex_unlock(&sv->tm);
    if (ok)
        tel_pubblica_copia(sv);
}

/* Chiede lo stato del pannello (PANNELLO_STATO). Con la sessione in uso. */
static void pannello_rileggi(Sv *sv, SvSessione *s)
{
    PhBuffer risp = PH_BUFFER_VUOTO;
    PhErrore e = {{0}};
    if (ph_componente_richiesta(s->c, MSG_PANNELLO_STATO, NULL, 0, &risp, &e) == 0) {
        g_mutex_lock(&sv->tm);
        bool ok = ph_ossatura_pannello_da_stato(risp.dati, risp.lunghezza, &sv->tel);
        g_mutex_unlock(&sv->tm);
        if (ok)
            tel_pubblica_copia(sv);
    }
    ph_buffer_libera(&risp);
}

/* ---- Pannello fisico ------------------------------------------------------------- */
/* Manda PANNELLO_IMPOSTA; torna true se il telefono ha detto OK. */
static bool pannello_imposta(Sv *sv, SvSessione *s, bool acceso)
{
    uint8_t corpo = acceso ? 1 : 0;
    PhErrore e = {{0}};
    if (ph_componente_richiesta(s->c, MSG_PANNELLO_IMPOSTA, &corpo, 1, NULL, &e) != 0)
        return false;
    g_mutex_lock(&sv->tm);
    sv->tel.pannello_noto = true;
    sv->tel.pannello_acceso = acceso;
    sv->tel.in_mano = false; /* acceso o spento dal PC: non e' in mano */
    g_mutex_unlock(&sv->tm);
    tel_pubblica_copia(sv);
    return true;
}

static void pannello_lavoro(Sv *sv, void *dati)
{
    bool acceso = GPOINTER_TO_INT(dati) != 0;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL)
        return;
    if (!pannello_imposta(sv, s, acceso))
        pannello_rileggi(sv, s); /* rifiutato (per esempio una chiamata): si riallinea */
    sv_sessione_lascia(s);
}

void sv_pannello(void *b, bool acceso)
{
    Sv *sv = b;
    sv_lavoro(sv, sv->p_stato, pannello_lavoro, GINT_TO_POINTER(acceso ? 1 : 0));
}

/* Cause dello spegnimento; `dati` di spegni_lavoro e' una di queste. */
static const char CAUSA_IN_MANO[] = "primo input dal PC con il telefono in mano";
static const char CAUSA_COLLEGAMENTO[] = "collegamento riuscito";

static void spegni_lavoro(Sv *sv, void *dati)
{
    const char *causa = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s != NULL) {
        if (pannello_imposta(sv, s, false))
            ph_shell_decisione("pannello del telefono spento", causa);
        else if (causa == CAUSA_COLLEGAMENTO) /* telefono bloccato o in chiamata: niente ritentativi,
                                               * allo sblocco la regola «in mano» pensa al resto */
            pannello_rileggi(sv, s);
        else /* per esempio durante una chiamata: ci si riprova fra un po' */
            atomic_store(&sv->pannello_non_prima_us, g_get_monotonic_time() + 2 * G_USEC_PER_SEC);
        sv_sessione_lascia(s);
    }
    atomic_store(&sv->pannello_spegnimento, 0);
}

void sv_pannello_spegni_se_in_mano(Sv *sv)
{
    g_mutex_lock(&sv->tm);
    bool da_spegnere = sv->tel.in_mano && sv->tel.pannello_acceso;
    g_mutex_unlock(&sv->tm);
    if (!da_spegnere || g_get_monotonic_time() < atomic_load(&sv->pannello_non_prima_us))
        return;
    if (atomic_exchange(&sv->pannello_spegnimento, 1) == 0)
        sv_lavoro(sv, sv->p_stato, spegni_lavoro, (void *)CAUSA_IN_MANO);
}

/* ---- Notifiche -------------------------------------------------------------------- */
static void notifiche_lavoro(Sv *sv, void *dati)
{
    (void)dati;
    /* Azzerato PRIMA di leggere: un cambio durante la lettura ne mette in
     * coda un'altra. */
    atomic_store(&sv->notifiche_in_coda, 0);
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL)
        return;
    PhDatiNotifica *nt = NULL;
    size_t n = 0;
    PhErrore e = {{0}};
    int r = ph_dati_notifiche(s->d, &nt, &n, &e);
    bool viva = ph_col_prestito_attivo(s->p);
    /* La prima lettura dopo un collegamento e' la «base» per i toast (servizi.h, (6)). */
    bool prima = viva && atomic_exchange(&s->prima_notifiche, false);
    sv_sessione_lascia(s);
    if (r != 0) {
        /* Errore del telefono: a schermo una frase nostra (tradotta), la sua solo nel
         * log. Con la connessione caduta no: l'elenco si rilegge al ritorno. */
        if (viva) {
            fprintf(stderr, "ossatura: lettura delle notifiche: %s\n", e.testo);
            ph_servizi_pubblica_notifiche_lettura(sv->serv, NULL, 0, ph_t("Non riesco a leggere le notifiche del telefono."),
                                                  prima);
        }
        return;
    }
    PhSrvNotifica *v = n > 0 ? calloc(n, sizeof *v) : NULL;
    if (n > 0 && v == NULL) {
        ph_dati_notifiche_libera(nt, n);
        return;
    }
    for (size_t i = 0; i < n; i++) {
        v[i].chiave = strdup(nt[i].chiave);
        v[i].pacchetto = strdup(nt[i].pacchetto);
        v[i].titolo = strdup(nt[i].titolo);
        v[i].testo = strdup(nt[i].testo);
        v[i].ora_ms = nt[i].ora_ms;
        if (!v[i].chiave || !v[i].pacchetto || !v[i].titolo || !v[i].testo) {
            ph_srv_notifiche_libera(v, n); /* free(NULL) e' lecito */
            ph_dati_notifiche_libera(nt, n);
            return;
        }
    }
    ph_dati_notifiche_libera(nt, n);
    ph_servizi_pubblica_notifiche_lettura(sv->serv, v, n, NULL, prima);
}

/* Evento 0x7f (thread del componente: tornare subito): al massimo una
 * lettura in coda. */
void sv_ev_notifiche(void *utente)
{
    Sv *sv = utente;
    if (atomic_exchange(&sv->notifiche_in_coda, 1) == 0)
        sv_lavoro(sv, sv->p_stato, notifiche_lavoro, NULL);
}

/* ---- App e icone -------------------------------------------------------------------- */
static void app_lavoro(Sv *sv, void *dati)
{
    (void)dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL)
        return;
    PhDatiApp *app = NULL;
    size_t n = 0;
    PhErrore e = {{0}};
    int r = ph_dati_app_elenco(s->d, &app, &n, &e);
    sv_sessione_lascia(s);
    if (r != 0) {
        fprintf(stderr, "YASDE 2.1: elenco delle app non letto\n");
        return;
    }
    PhSrvApp *v = n > 0 ? calloc(n, sizeof *v) : NULL;
    if (n > 0 && v == NULL) {
        ph_dati_app_libera(app, n);
        return;
    }
    for (size_t i = 0; i < n; i++) {
        v[i].pacchetto = strdup(app[i].pacchetto);
        v[i].nome = strdup(app[i].nome);
        v[i].di_sistema = app[i].di_sistema;
        v[i].solo_verticale = app[i].solo_verticale;
        if (!v[i].pacchetto || !v[i].nome) {
            ph_srv_app_libera(v, n);
            ph_dati_app_libera(app, n);
            return;
        }
    }
    ph_dati_app_libera(app, n);
    ph_servizi_pubblica_app(sv->serv, v, n); /* solo elenchi completi: dati.h li garantisce */
}

typedef struct {
    char *pacchetto;
    int lato;
} IconaJob;

static void icona_lavoro(Sv *sv, void *dati)
{
    IconaJob *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s != NULL) {
        PhBuffer png = PH_BUFFER_VUOTO;
        PhErrore e = {{0}};
        int lato = j->lato < 16 ? 16 : (j->lato > 512 ? 512 : j->lato);
        int r = ph_dati_app_icona(s->d, j->pacchetto, (uint16_t)lato, &png, &e);
        bool viva = ph_col_prestito_attivo(s->p);
        sv_sessione_lascia(s);
        if (r == 0)
            ph_servizi_pubblica_icona(sv->serv, j->pacchetto, png.dati, png.lunghezza);
        else if (viva) /* l'icona non c'e': si mostra il segnaposto */
            ph_servizi_pubblica_icona(sv->serv, j->pacchetto, NULL, 0);
        ph_buffer_libera(&png);
    }
    free(j->pacchetto);
    free(j);
}

void sv_icona_chiedi(void *b, const char *pacchetto, int lato)
{
    Sv *sv = b;
    IconaJob *j = calloc(1, sizeof *j);
    if (j == NULL)
        return;
    j->pacchetto = strdup(pacchetto);
    j->lato = lato;
    if (j->pacchetto == NULL) {
        free(j);
        return;
    }
    sv_lavoro(sv, sv->p_icone, icona_lavoro, j);
}

/* ---- Appunti ---------------------------------------------------------------------------- */
static void appunti_avviso(const char *testo, size_t n, void *utente)
{
    Sv *sv = utente;
    if (testo != NULL && n > 0) /* testo NULL = il lettore e' finito */
        ph_servizi_pubblica_appunti(sv->serv, testo);
}

static void appunti_lavoro(Sv *sv, void *dati)
{
    char *testo = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s != NULL) {
        PhErrore e = {{0}};
        (void)ph_dati_appunti_imposta(s->d, testo, &e);
        sv_sessione_lascia(s);
    }
    free(testo);
}

void sv_appunti_imposta(void *b, const char *testo)
{
    Sv *sv = b;
    char *copia = strdup(testo);
    if (copia != NULL)
        sv_lavoro(sv, sv->p_lavori, appunti_lavoro, copia);
}

/* ---- Sessione ----------------------------------------------------------------------------- */
void sv_dati_sessione_nuova(Sv *sv, SvSessione *s)
{
    PhErrore e = {{0}};
    PhDatiStato ds;
    if (ph_dati_stato(s->d, &ds, &e) == 0)
        sv_ev_stato(&ds, sv);
    if (ph_componente_ascolta(s->c, MSG_PANNELLO_EVENTO, MSG_PANNELLO_EVENTO, ev_pannello, sv, &e) == 0) {
        s->ascolto_pannello = true;
        pannello_rileggi(sv, s);
        /* Pannello acceso e telefono non in mano: lo si spegne, ma in coda (puo'
         * durare secondi e non deve ritardare l'avvio). Con «in mano» ci pensa la
         * regola al primo input. */
        g_mutex_lock(&sv->tm);
        bool spegni = sv->tel.pannello_noto && sv->tel.pannello_acceso && !sv->tel.in_mano;
        g_mutex_unlock(&sv->tm);
        if (spegni && atomic_exchange(&sv->pannello_spegnimento, 1) == 0)
            sv_lavoro(sv, sv->p_stato, spegni_lavoro, (void *)CAUSA_COLLEGAMENTO);
    }
    if (ph_dati_appunti_apri(s->d, appunti_avviso, sv, &e) == 0)
        s->appunti_aperti = true;
    sv_lavoro(sv, sv->p_stato, app_lavoro, NULL);
    sv_ev_notifiche(sv);
    sv_controlli_sessione_nuova(sv, s);
}

void sv_dati_teardown(Sv *sv, SvSessione *s)
{
    sv_controlli_teardown(sv, s);
    PhErrore e = {{0}};
    if (s->ascolto_pannello) {
        (void)ph_componente_ascolta(s->c, MSG_PANNELLO_EVENTO, MSG_PANNELLO_EVENTO, NULL, NULL, &e);
        s->ascolto_pannello = false;
    }
    if (s->appunti_aperti) {
        ph_dati_appunti_chiudi(s->d);
        s->appunti_aperti = false;
    }
}

#endif
