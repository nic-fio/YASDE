/* Logica pura dell'ossatura (logica.h). */
#include <stdint.h>
#include <string.h>

#include "config/lingua.h"
#include "logica.h"
#include "shell/shell.h"

/* ---- Velo ---------------------------------------------------------------- */
bool ph_velo_contenuto_per(const PhSrvCollegamento *col, PhVeloContenuto *c)
{
    /* Schermo del telefono spento dal sistema: i flussi sono fermi e il velo
     * e' quello del blocco (Decisioni di chi coordina). */
    if (col->schermo_spento && (col->stato == PH_SRV_COLLEGATO || col->stato == PH_SRV_BLOCCATO))
        return ph_velo_contenuto(PH_SRV_BLOCCATO, c);
    return ph_velo_contenuto(col->stato, c);
}

bool ph_velo_contenuto(PhSrvStato stato, PhVeloContenuto *c)
{
    *c = (PhVeloContenuto){0};
    switch (stato) {
    case PH_SRV_BLOCCATO:
        c->lucchetto = true;
        c->titolo = ph_t("Telefono bloccato");
        c->testo = ph_t("Come in DeX, il desktop si blocca con il telefono. Sblocca il telefono per continuare: le finestre tornano dove erano.");
        return true;
    case PH_SRV_PERSO:
        c->titolo = ph_t("Connessione persa");
        c->mostra_motivo = true;
        c->pulsante = true;
        return true;
    case PH_SRV_CERCO:
        c->titolo = ph_t("Cerco il telefono");
        c->mostra_motivo = true;
        return true;
    case PH_SRV_NON_ABBINATO:
        c->titolo = ph_t("Telefono non abbinato");
        c->mostra_motivo = true;
        return true;
    case PH_SRV_COLLEGATO:
    case PH_SRV_CHIUSO:
        break;
    }
    return false;
}

PhVista ph_ossatura_vista(const PhSrvCollegamento *col, bool collegato_in_sessione, bool ha_connessione)
{
    if (ha_connessione && ph_shell_connessione_da_mostrare(col->stato, collegato_in_sessione))
        return PH_VISTA_CONNESSIONE;
    PhVeloContenuto k;
    return ph_velo_contenuto_per(col, &k) ? PH_VISTA_VELO : PH_VISTA_NESSUNA;
}

/* ---- Finestra host ------------------------------------------------------- */
bool ph_host_visibile_da_stato(unsigned stato_toplevel)
{
    return (stato_toplevel & (GDK_TOPLEVEL_STATE_MINIMIZED | GDK_TOPLEVEL_STATE_SUSPENDED)) == 0;
}

/* ---- mkdir ----------------------------------------------------------------- */
int ph_ossatura_comando_mkdir(const char *percorso, char *out, size_t capacita)
{
    static const char testa[] = "mkdir '";
    if (percorso == NULL || *percorso == '\0' || out == NULL || capacita == 0)
        return -1;
    size_t n = 0;
    /* Ogni scrittura controlla di avere posto per il byte e per il
     * terminatore. */
#define METTI(c)                                                                                   \
    do {                                                                                           \
        if (n + 2 > capacita)                                                                      \
            return (out[0] = '\0', -1);                                                            \
        out[n++] = (c);                                                                            \
    } while (0)
    for (size_t i = 0; testa[i] != '\0'; i++)
        METTI(testa[i]);
    for (const char *p = percorso; *p != '\0'; p++) {
        if (*p == '\'') {
            METTI('\'');
            METTI('\\');
            METTI('\'');
            METTI('\'');
        } else {
            METTI(*p);
        }
    }
    METTI('\'');
#undef METTI
    out[n] = '\0';
    return 0;
}

/* ---- Macchina del flusso ---------------------------------------------------- */
PhFlPasso ph_fl_passo(PhFlStato stato, PhFlEvento ev, bool gia_aperto)
{
    PhFlPasso p = {.stato = stato};
    if (stato == PH_FL_MORTO)
        return p; /* non riparte piu' */
    switch (ev) {
    case PH_FL_EV_SESSIONE_PERSA:
        if (stato != PH_FL_PERSO) {
            p.stato = PH_FL_PERSO;
            p.ha_avviso = true;
            p.avviso = PH_SRV_FL_PERSO;
        }
        break;
    case PH_FL_EV_SESSIONE_NUOVA:
        if (stato == PH_FL_PERSO) {
            p.stato = PH_FL_APRENDO;
            p.riapri = true;
        }
        break;
    case PH_FL_EV_APERTO:
        if (stato == PH_FL_APRENDO) {
            p.stato = PH_FL_ATTIVO;
            p.ha_avviso = true;
            p.avviso = gia_aperto ? PH_SRV_FL_RIPRESO : PH_SRV_FL_APERTO;
        }
        break;
    case PH_FL_EV_APERTO_SENZA_SESSIONE:
        if (stato == PH_FL_APRENDO) {
            p.stato = PH_FL_PERSO;
            p.ha_avviso = true;
            p.avviso = PH_SRV_FL_PERSO;
        }
        break;
    case PH_FL_EV_APERTO_FALLITO:
        if (stato == PH_FL_APRENDO) {
            p.stato = PH_FL_MORTO;
            p.ha_avviso = true;
            p.avviso = PH_SRV_FL_ERRORE;
        }
        break;
    case PH_FL_EV_CODIFICATORE:
        p.stato = PH_FL_MORTO;
        p.ha_avviso = true;
        p.avviso = PH_SRV_FL_ERRORE;
        break;
    }
    return p;
}

void ph_ossatura_misura_specchio(int schermo_w, int schermo_h, int *w, int *h)
{
    /* Tetto dello specchio: 1920 px per lato (specifica 4.7). */
    enum { TETTO = 1920 };
    int64_t sw = schermo_w > 0 ? schermo_w : 1080;
    int64_t sh = schermo_h > 0 ? schermo_h : 2340;
    int64_t max = sw > sh ? sw : sh;
    /* Se un lato supera il tetto si riduce tutto in proporzione (interi: niente
     * arrotondamenti dei decimali che farebbero perdere un multiplo di 8). */
    if (max > TETTO) {
        sw = sw * TETTO / max;
        sh = sh * TETTO / max;
    }
    int ow = (int)sw / 8 * 8;
    int oh = (int)sh / 8 * 8;
    if (ow < 64)
        ow = 64;
    if (oh < 64)
        oh = 64;
    *w = ow;
    *h = oh;
}

PhSrvPannelloEsito ph_ossatura_esito_pannello(const char *frase)
{
    static const struct {
        const char *inizio;
        PhSrvPannelloEsito esito;
    } note[] = {
        {"chiamata in corso", PH_SRV_PAN_CHIAMATA},
        {"telefono bloccato", PH_SRV_PAN_BLOCCATO},
        {"guardiano assente", PH_SRV_PAN_RIPRISTINO_INCERTO},
        {"percorso del jar non utilizzabile", PH_SRV_PAN_RIPRISTINO_INCERTO},
        {"API di Android non disponibile", PH_SRV_PAN_NON_DISPONIBILE},
        {"ne' SurfaceControl", PH_SRV_PAN_NON_DISPONIBILE},
        {"nessun display fisico", PH_SRV_PAN_NON_DISPONIBILE},
    };
    for (size_t i = 0; frase != NULL && i < sizeof note / sizeof note[0]; i++)
        if (strstr(frase, note[i].inizio) != NULL)
            return note[i].esito;
    return PH_SRV_PAN_ERRORE;
}

void ph_ossatura_scala_testo(double scala, char *out, size_t cap)
{
    if (cap == 0)
        return;
    char tmp[G_ASCII_DTOSTR_BUF_SIZE];
    /* g_ascii_formatd non guarda la lingua del PC: sempre il punto. */
    g_ascii_formatd(tmp, sizeof tmp, "%.2f", scala);
    g_strlcpy(out, tmp, cap);
}

#if PH_OSSATURA_SERVIZI_VERI
/* ---- Conversioni dai pezzi sotto --------------------------------------------- */
PhSrvEsito ph_ossatura_esito_file(PhFileEsito e)
{
    switch (e) {
    case PH_FILE_RIUSCITO:
        return PH_SRV_ESITO_OK;
    case PH_FILE_NON_TROVATO:
        return PH_SRV_ESITO_NON_TROVATO;
    case PH_FILE_NON_CONSENTITO:
        return PH_SRV_ESITO_NON_CONSENTITO;
    case PH_FILE_ERRORE:
    case PH_FILE_ANNULLATO:
    case PH_FILE_ESISTE:
        break;
    }
    return PH_SRV_ESITO_ERRORE;
}

PhSrvStato ph_ossatura_stato_da_collegamento(PhColStato s)
{
    switch (s) {
    case PH_COL_CERCO: return PH_SRV_CERCO;
    case PH_COL_COLLEGATO: return PH_SRV_COLLEGATO;
    case PH_COL_BLOCCATO: return PH_SRV_BLOCCATO;
    case PH_COL_PERSO: return PH_SRV_PERSO;
    case PH_COL_CHIUSO: return PH_SRV_CHIUSO;
    case PH_COL_NON_ABBINATO: return PH_SRV_NON_ABBINATO;
    }
    return PH_SRV_PERSO;
}

void ph_ossatura_collegamento_da_avviso(const PhColAvviso *a, PhSrvCollegamento *c)
{
    memset(c, 0, sizeof *c);
    c->stato = ph_ossatura_stato_da_collegamento(a->stato);
    /* Il motivo e' gia' una frase per l'utente (collegamento.h). */
    g_strlcpy(c->motivo, a->motivo, sizeof c->motivo);
    c->ha_telefono = a->ha_telefono;
    /* Per la schermata della prima connessione (nessun nome o indirizzo: solo numeri). */
    c->telefoni_trovati = a->telefoni_trovati;
    c->scelta_arbitraria = a->scelta_arbitraria;
    if (a->ha_telefono) {
        g_strlcpy(c->nome, a->telefono.nome, sizeof c->nome);
        c->api = a->telefono.info.api;
        c->schermo_w = a->telefono.info.larghezza;
        c->schermo_h = a->telefono.info.altezza;
        c->densita = a->telefono.info.densita;
    }
}

void ph_ossatura_telefono_da_stato(const PhDatiStato *s, PhSrvTelefono *t)
{
    t->noto = true;
    t->batteria = s->batteria > 100 ? 100 : s->batteria;
    t->in_carica = s->in_carica;
    switch (s->rete) {
    case PH_DATI_RETE_WIFI: t->rete = PH_SRV_RETE_WIFI; break;
    case PH_DATI_RETE_CELLULARE: t->rete = PH_SRV_RETE_CELLULARE; break;
    default: t->rete = PH_SRV_RETE_NESSUNA; break;
    }
    t->segnale = s->segnale > 4 ? 4 : s->segnale;
}

bool ph_ossatura_pannello_da_evento(const unsigned char *corpo, size_t n, PhSrvTelefono *t)
{
    if (corpo == NULL || n < 2)
        return false;
    bool acceso = corpo[0] != 0;
    unsigned motivo = corpo[1];
    t->pannello_noto = true;
    t->pannello_acceso = acceso;
    /* Docs/protocollo.md 2.9: il telefono e' "in mano" da quando l'utente lo
     * tocca o sblocca, o per una chiamata (motivi 1-3); un accensione chiesta
     * dal PC (4) o il ripristino alla chiusura (5) non lo sono. Il motivo 6
     * («PC assente» da 30 minuti: il telefono ha riacceso il pannello e il
     * pannello RESTA ACCESO finche' il PC non manda PANNELLO_IMPOSTA 0) conta
     * come «in mano»: al primo input dal PC il pannello si rispegne, come dopo
     * uno sblocco. Senza questo, dopo un'assenza e la ripresa il pannello
     * resterebbe acceso per tutta la sessione. Motivi sconosciuti (> 6): no. */
    t->in_mano = acceso && ((motivo >= 1 && motivo <= 3) || motivo == 6);
    return true;
}

bool ph_ossatura_pannello_da_stato(const unsigned char *corpo, size_t n, PhSrvTelefono *t)
{
    if (corpo == NULL || n < 2)
        return false;
    t->pannello_noto = true;
    t->pannello_acceso = corpo[0] != 0;
    t->in_mano = corpo[1] != 0;
    return true;
}
#endif
