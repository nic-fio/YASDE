/* Widget aggiunti il 23 nov (calendario del mese, sveglia, batteria, docs/protocollo.md
 * 2.6: 0x7b-0x7d) e controlli rapidi (2.10: 0x80-0x85, evento 0x8e).
 * Ogni risposta si legge solo con PhLettore e si controlla per intero; nessun
 * messaggio d'errore di questo file contiene titoli di eventi o pacchetti. Ogni
 * funzione pubblica prende il contatore d'uso (chiusura in parallelo). */
#include <stdlib.h>
#include <string.h>

#include "dati/interno.h"

#define PH_GIORNO_MS 86400000ull

/* Scrive un u64 nel corpo (ph_msg_* non ha u64). */
static int msg_u64(PhBuffer *b, uint64_t v)
{
    if (ph_msg_u32(b, (uint32_t)(v >> 32)) != 0)
        return -1;
    return ph_msg_u32(b, (uint32_t)v);
}

/* Corpo di una richiesta con un solo u8 (o niente se n_campi == 0). */
static int manda_u8(PhDati *d, uint8_t tipo, uint8_t valore, PhErrore *e)
{
    PhBuffer m = PH_BUFFER_VUOTO;
    if (ph_msg_inizia(&m, 0) != 0 || ph_msg_u8(&m, valore) != 0) {
        ph_buffer_libera(&m);
        return ph_errore(e, "memoria finita");
    }
    PhBuffer r = PH_BUFFER_VUOTO;
    int q = ph_dati_chiedi(d, tipo, &m, &r, e);
    ph_buffer_libera(&m);
    ph_buffer_libera(&r); /* l'OK dei comandi non ha corpo che serva */
    return q;
}

/* ---- Calendario per intervallo (0x7b) ---------------------------------------------*/
void ph_dati_calendario_intervallo_libera(PhDatiCalendarioIntervallo *c)
{
    if (c == NULL)
        return;
    ph_dati_calendario_libera(c->eventi, c->numero);
    free(c->app_calendario);
    memset(c, 0, sizeof *c);
}

static int calendario_intervallo_i(PhDati *d, uint64_t da_ms, uint64_t a_ms, uint16_t massimo,
                                   PhDatiCalendarioIntervallo *out, PhErrore *e)
{
    /* L'ampiezza si controlla in millisecondi, con interi senza segno: a prima
     * di da e' un errore, mai un'ampiezza enorme per via del ritorno a capo. */
    if (a_ms < da_ms)
        return ph_errore(e, "calendario: la fine dell'intervallo e' prima dell'inizio");
    if (a_ms - da_ms > (uint64_t)PH_DATI_CALENDARIO_GIORNI_MAX * PH_GIORNO_MS)
        return ph_errore(e, "calendario: intervallo oltre %d giorni", PH_DATI_CALENDARIO_GIORNI_MAX);
    if (massimo < 1 || massimo > PH_DATI_CALENDARIO_INTERVALLO_MAX)
        return ph_errore(e, "calendario: massimo fuori da 1-%d", PH_DATI_CALENDARIO_INTERVALLO_MAX);
    PhBuffer m = PH_BUFFER_VUOTO;
    if (ph_msg_inizia(&m, 0) != 0 || msg_u64(&m, da_ms) != 0 || msg_u64(&m, a_ms) != 0 ||
        ph_msg_u16(&m, massimo) != 0) {
        ph_buffer_libera(&m);
        return ph_errore(e, "memoria finita");
    }
    PhBuffer r = PH_BUFFER_VUOTO;
    int q = ph_dati_chiedi(d, PH_DT_CALENDARIO_INTERVALLO, &m, &r, e);
    ph_buffer_libera(&m);
    if (q != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    PhLettore l = {r.dati, r.lunghezza, 0, false};
    uint8_t troncato = ph_leggi_u8(&l);
    char *app = NULL;
    int k = ph_dati_leggi_pulita(&l, &app, PH_DATI_NOME_MAX, PH_PUL_RIGA);
    size_t n = ph_leggi_u16(&l);
    PhDatiEvento *v = NULL;
    const char *perche = "elenco degli eventi troncato o non valido";
    int esito = -1;
    if (k == -2) {
        perche = "memoria finita";
        goto fine;
    }
    if (k != 0 || l.rotto || troncato > 1)
        goto fine;
    if (n > massimo) {
        perche = "il telefono ha mandato piu' eventi del massimo";
        goto fine;
    }
    if (ph_dati_leggi_eventi(&l, n, &v, &perche) != 0)
        goto fine;
    if (l.posizione != l.lunghezza) {
        perche = "elenco degli eventi con dati in eccesso";
        goto fine;
    }
    esito = 0;
fine:
    ph_buffer_libera(&r);
    if (esito != 0) {
        ph_dati_calendario_libera(v, n);
        free(app);
        return ph_errore(e, "%s", perche);
    }
    out->eventi = v;
    out->numero = n;
    out->troncato = troncato != 0;
    out->app_calendario = app;
    return 0;
}

int ph_dati_calendario_intervallo(PhDati *d, uint64_t da_ms, uint64_t a_ms, uint16_t massimo,
                                  PhDatiCalendarioIntervallo *out, PhErrore *e)
{
    if (out == NULL)
        return ph_errore(e, "calendario: parametri mancanti");
    memset(out, 0, sizeof *out);
    if (ph_dati_entra(d, e) != 0)
        return -1;
    int r = calendario_intervallo_i(d, da_ms, a_ms, massimo, out, e);
    ph_dati_esci(d);
    return r;
}

/* ---- Sveglia (0x7c) ----------------------------------------------------------------*/
void ph_dati_sveglia_libera(PhDatiSveglia *s)
{
    if (s == NULL)
        return;
    free(s->pacchetto);
    memset(s, 0, sizeof *s);
}

static int sveglia_i(PhDati *d, PhDatiSveglia *out, PhErrore *e)
{
    PhBuffer r = PH_BUFFER_VUOTO;
    if (ph_dati_chiedi(d, PH_DT_SVEGLIA, NULL, &r, e) != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    PhLettore l = {r.dati, r.lunghezza, 0, false};
    uint8_t c = ph_leggi_u8(&l);
    uint64_t ora = ph_leggi_u64(&l);
    char *pacchetto = NULL;
    int k = ph_dati_leggi_pulita(&l, &pacchetto, PH_DATI_NOME_MAX, PH_PUL_RIGA);
    /* Una sveglia presente ha un'ora; una assente non ne ha. */
    bool buona = k == 0 && !l.rotto && l.posizione == l.lunghezza && c <= 1 && (c == 1) == (ora != 0);
    ph_buffer_libera(&r);
    if (!buona) {
        free(pacchetto);
        return ph_errore(e, k == -2 ? "memoria finita" : "risposta sulla sveglia non valida");
    }
    out->presente = c == 1;
    out->ora_ms = ora;
    out->pacchetto = pacchetto;
    return 0;
}

int ph_dati_sveglia(PhDati *d, PhDatiSveglia *out, PhErrore *e)
{
    if (out == NULL)
        return ph_errore(e, "sveglia: parametri mancanti");
    memset(out, 0, sizeof *out);
    if (ph_dati_entra(d, e) != 0)
        return -1;
    int r = sveglia_i(d, out, e);
    ph_dati_esci(d);
    return r;
}

/* ---- Batteria (0x7d) ---------------------------------------------------------------*/
static int batteria_i(PhDati *d, PhDatiBatteria *out, PhErrore *e)
{
    PhBuffer r = PH_BUFFER_VUOTO;
    if (ph_dati_chiedi(d, PH_DT_BATTERIA, NULL, &r, e) != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    PhLettore l = {r.dati, r.lunghezza, 0, false};
    uint8_t livello = ph_leggi_u8(&l);
    uint8_t stato = ph_leggi_u8(&l);
    uint8_t alim = ph_leggi_u8(&l);
    int16_t temp = (int16_t)ph_leggi_u16(&l);
    uint8_t salute = ph_leggi_u8(&l);
    uint16_t mv = ph_leggi_u16(&l);
    int32_t carica = (int32_t)ph_leggi_u32(&l);
    int32_t autonomia = (int32_t)ph_leggi_u32(&l);
    bool buona = !l.rotto && l.posizione == l.lunghezza && livello <= 100;
    ph_buffer_libera(&r);
    if (!buona)
        return ph_errore(e, "risposta sulla batteria non valida");
    out->livello = livello;
    out->stato = stato <= PH_DATI_BAT_PIENA ? (PhDatiBatStato)stato : PH_DATI_BAT_SCONOSCIUTO;
    out->alimentazione = alim & 0x0f;
    out->temperatura_nota = temp != 0x7fff;
    out->temperatura_decimi = 0;
    if (out->temperatura_nota)
        out->temperatura_decimi = temp < -500 ? -500 : temp > 1500 ? 1500 : temp;
    out->salute = salute <= 6 ? salute : 0;
    out->tensione_mv = mv;
    out->minuti_carica = carica < 0 ? -1 : carica;
    out->minuti_autonomia = autonomia < 0 ? -1 : autonomia;
    return 0;
}

int ph_dati_batteria(PhDati *d, PhDatiBatteria *out, PhErrore *e)
{
    if (out == NULL)
        return ph_errore(e, "batteria: parametri mancanti");
    memset(out, 0, sizeof *out);
    if (ph_dati_entra(d, e) != 0)
        return -1;
    int r = batteria_i(d, out, e);
    ph_dati_esci(d);
    return r;
}

/* ---- Controlli rapidi (0x80-0x85, 0x8e) ---------------------------------------------*/
/* Valore a tre stati: 0..massimo oppure 0xff = non disponibile. -2 se fuori intervallo. */
static int tre_stati(uint8_t v, uint8_t massimo)
{
    if (v == 0xff)
        return PH_DATI_NON_DISPONIBILE;
    return v <= massimo ? v : -2;
}

/* Corpo di CONTROLLI_STATO e dell'evento 0x8e: cinque byte esatti. */
static int controlli_leggi(const uint8_t *p, size_t n, PhDatiControlli *c)
{
    if (p == NULL || n != 5)
        return -1;
    int s = tre_stati(p[0], 2), nd = tre_stati(p[1], 1), t = tre_stati(p[2], 1);
    if (s == -2 || nd == -2 || t == -2 || p[3] > 1)
        return -1;
    c->suoneria = s;
    c->non_disturbare = nd;
    c->torcia = t;
    c->trova_in_corso = p[3] != 0;
    c->puo_suoneria = (p[4] & 1) != 0; /* i bit sconosciuti si ignorano */
    c->puo_non_disturbare = (p[4] & 2) != 0;
    c->puo_torcia = (p[4] & 4) != 0;
    c->puo_trova = (p[4] & 8) != 0;
    return 0;
}

static int controlli_i(PhDati *d, PhDatiControlli *out, PhErrore *e)
{
    PhBuffer r = PH_BUFFER_VUOTO;
    if (ph_dati_chiedi(d, PH_DT_CONTROLLI_STATO, NULL, &r, e) != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    int k = controlli_leggi(r.dati, r.lunghezza, out);
    ph_buffer_libera(&r);
    if (k != 0)
        return ph_errore(e, "risposta sui controlli non valida");
    return 0;
}

int ph_dati_controlli(PhDati *d, PhDatiControlli *out, PhErrore *e)
{
    if (out == NULL)
        return ph_errore(e, "controlli: parametri mancanti");
    memset(out, 0, sizeof *out);
    if (ph_dati_entra(d, e) != 0)
        return -1;
    int r = controlli_i(d, out, e);
    ph_dati_esci(d);
    return r;
}

/* Dal thread interno del componente: niente richieste da qui. L'avviso si copia
 * sotto m_ric e si chiama senza lock. */
void ph_dati_ascolto_controlli(uint8_t tipo, const uint8_t *corpo, size_t n, void *utente)
{
    PhDati *d = utente;
    PhDatiControlli c;
    if (tipo != PH_DT_CONTROLLI_CAMBIATI || atomic_load(&d->chiuso) ||
        controlli_leggi(corpo, n, &c) != 0)
        return;
    pthread_mutex_lock(&d->m_ric);
    PhDatiAvvisoControlli f = d->ctrl_avviso;
    void *u = d->ctrl_utente;
    pthread_mutex_unlock(&d->m_ric);
    if (f == NULL)
        return;
    ph_dati_in_avviso++;
    f(&c, u);
    ph_dati_in_avviso--;
}

/* Chiamata sotto m_ctrl: registrazione e rimozione non si mescolano. */
static int ascolta_controlli_i(PhDati *d, PhDatiAvvisoControlli avviso, void *utente, PhErrore *e)
{
    pthread_mutex_lock(&d->m_ric);
    bool prima = d->ctrl_registrato;
    PhDatiAvvisoControlli vecchio = d->ctrl_avviso;
    void *vecchio_utente = d->ctrl_utente;
    pthread_mutex_unlock(&d->m_ric);
    if (avviso == NULL) {
        if (!prima)
            return 0;
        /* aspetta la fine di un avviso in corso (lo garantisce il componente) */
        if (ph_componente_ascolta(d->c, PH_DT_CONTROLLI_CAMBIATI, PH_DT_CONTROLLI_CAMBIATI, NULL,
                                  NULL, e) != 0)
            return -1;
        pthread_mutex_lock(&d->m_ric);
        d->ctrl_registrato = false;
        d->ctrl_avviso = NULL;
        d->ctrl_utente = NULL;
        pthread_mutex_unlock(&d->m_ric);
        return 0;
    }
    pthread_mutex_lock(&d->m_ric);
    d->ctrl_avviso = avviso;
    d->ctrl_utente = utente;
    pthread_mutex_unlock(&d->m_ric);
    if (ph_componente_ascolta(d->c, PH_DT_CONTROLLI_CAMBIATI, PH_DT_CONTROLLI_CAMBIATI,
                              ph_dati_ascolto_controlli, d, e) != 0) {
        /* Il componente tiene l'ascolto di prima (o nessuno): l'avviso e il suo dato
         * tornano quelli di prima, cosi' gli eventi non vanno a chi ha avuto -1. */
        pthread_mutex_lock(&d->m_ric);
        d->ctrl_avviso = prima ? vecchio : NULL;
        d->ctrl_utente = prima ? vecchio_utente : NULL;
        pthread_mutex_unlock(&d->m_ric);
        return -1;
    }
    pthread_mutex_lock(&d->m_ric);
    d->ctrl_registrato = true;
    pthread_mutex_unlock(&d->m_ric);
    return 0;
}

int ph_dati_ascolta_controlli(PhDati *d, PhDatiAvvisoControlli avviso, void *utente, PhErrore *e)
{
    /* Da dentro un avviso il componente si bloccherebbe (aspetta l'avviso in
     * corso): si rifiuta, come ph_dati_chiudi. */
    if (ph_dati_in_avviso > 0)
        return ph_errore(e, "controlli: ph_dati_ascolta_controlli chiamata da dentro un avviso");
    if (ph_dati_entra(d, e) != 0)
        return -1;
    pthread_mutex_lock(&d->m_ctrl);
    int r = ascolta_controlli_i(d, avviso, utente, e);
    pthread_mutex_unlock(&d->m_ctrl);
    ph_dati_esci(d);
    return r;
}

/* Comandi: argomenti validati PRIMA di mandare (fuori intervallo: -1, nulla mandato). */
static int comando_u8(PhDati *d, uint8_t tipo, uint8_t valore, PhErrore *e)
{
    if (ph_dati_entra(d, e) != 0)
        return -1;
    int r = manda_u8(d, tipo, valore, e);
    ph_dati_esci(d);
    return r;
}

int ph_dati_suoneria_imposta(PhDati *d, int modo, PhErrore *e)
{
    if (modo < 0 || modo > 2)
        return ph_errore(e, "suoneria: modo fuori da 0-2");
    return comando_u8(d, PH_DT_SUONERIA_IMPOSTA, (uint8_t)modo, e);
}

int ph_dati_non_disturbare_imposta(PhDati *d, bool acceso, PhErrore *e)
{
    return comando_u8(d, PH_DT_NON_DISTURBARE_IMPOSTA, acceso ? 1 : 0, e);
}

int ph_dati_torcia_imposta(PhDati *d, bool accesa, PhErrore *e)
{
    return comando_u8(d, PH_DT_TORCIA_IMPOSTA, accesa ? 1 : 0, e);
}

static int trova_avvia_i(PhDati *d, uint16_t secondi, PhErrore *e)
{
    PhBuffer m = PH_BUFFER_VUOTO;
    if (ph_msg_inizia(&m, 0) != 0 || ph_msg_u16(&m, secondi) != 0) {
        ph_buffer_libera(&m);
        return ph_errore(e, "memoria finita");
    }
    PhBuffer r = PH_BUFFER_VUOTO;
    int q = ph_dati_chiedi(d, PH_DT_TROVA_AVVIA, &m, &r, e);
    ph_buffer_libera(&m);
    ph_buffer_libera(&r);
    return q;
}

int ph_dati_trova_avvia(PhDati *d, uint16_t secondi, PhErrore *e)
{
    if (secondi < 5 || secondi > 120)
        return ph_errore(e, "trova il telefono: durata fuori da 5-120 secondi");
    if (ph_dati_entra(d, e) != 0)
        return -1;
    int r = trova_avvia_i(d, secondi, e);
    ph_dati_esci(d);
    return r;
}

int ph_dati_trova_ferma(PhDati *d, PhErrore *e)
{
    if (ph_dati_entra(d, e) != 0)
        return -1;
    PhBuffer r = PH_BUFFER_VUOTO;
    int q = ph_dati_chiedi(d, PH_DT_TROVA_FERMA, NULL, &r, e);
    ph_buffer_libera(&r);
    ph_dati_esci(d);
    return q;
}

/* ---- Posizione approssimativa (0x90) ---------------------------------------------------
 * Corpo dell'OK: esito u8, lat i32, lon i32 (millesimi di grado, multipli di 5), eta u32, fornitore u8, localita (stringa del
 * protocollo: u16 + n byte, n 0-127), ricerca avviata u8: 17 + n byte esatti. Ogni valore fuori misura
 * e' un errore (messaggio fisso). RISERVATEZZA: coordinate e localita' non compaiono
 * mai in un messaggio d'errore ne' in un log; restano in memoria, in *out. */
#define PH_POS_LOCALITA_MAX 127 /* byte utili: la struttura ne ha 128 con il terminatore */

static int posizione_i(PhDati *d, bool permessa, PhDatiPosizione *out, PhErrore *e)
{
    PhBuffer m = PH_BUFFER_VUOTO;
    if (ph_msg_inizia(&m, 0) != 0 || ph_msg_u8(&m, permessa ? 1 : 0) != 0) {
        ph_buffer_libera(&m);
        return ph_errore(e, "memoria finita");
    }
    /* Il telefono risponde subito: la richiesta dura quanto una richiesta normale. */
    PhBuffer r = PH_BUFFER_VUOTO;
    int q = ph_dati_chiedi(d, PH_DT_POSIZIONE, &m, &r, e);
    ph_buffer_libera(&m);
    if (q != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    PhLettore l = {r.dati, r.lunghezza, 0, false};
    uint8_t esito = ph_leggi_u8(&l);
    int32_t lat = (int32_t)ph_leggi_u32(&l);
    int32_t lon = (int32_t)ph_leggi_u32(&l);
    uint32_t eta = ph_leggi_u32(&l);
    uint8_t fornitore = ph_leggi_u8(&l);
    uint32_t n = ph_leggi_u16(&l);
    const uint8_t *loc = NULL;
    /* La lunghezza dichiarata si confronta con i byte che restano (meno il byte
     * finale) prima di toccare nulla: nessun numero del telefono guida un accesso. */
    if (!l.rotto && n <= PH_POS_LOCALITA_MAX && l.posizione <= l.lunghezza &&
        n + 1u <= l.lunghezza - l.posizione) {
        loc = l.dati + l.posizione;
        l.posizione += n;
    } else {
        l.rotto = true;
    }
    uint8_t ricerca = ph_leggi_u8(&l);
    bool buona = !l.rotto && l.posizione == l.lunghezza && esito <= PH_DATI_POS_SCONOSCIUTA &&
                 lat >= -90000 && lat <= 90000 && lon >= -180000 && lon <= 180000 &&
                 lat % 5 == 0 && lon % 5 == 0 && fornitore <= 3 &&
                 ricerca <= 1 && ph_dati_utf8_valido(loc, n);
    /* Senza posizione nota non ci sono coordinate ne' localita'. */
    if (buona && esito != PH_DATI_POS_NOTA && (lat != 0 || lon != 0 || n != 0))
        buona = false;
    if (!buona) {
        ph_buffer_libera(&r);
        return ph_errore(e, "risposta sulla posizione non valida");
    }
    /* Valida: restano da ripulire i caratteri di controllo (mai piu' lunga di n byte). */
    char *pulita = ph_dati_pulisci(loc, n, PH_POS_LOCALITA_MAX, PH_PUL_RIGA);
    ph_buffer_libera(&r);
    if (pulita == NULL)
        return ph_errore(e, "memoria finita");
    memset(out, 0, sizeof *out);
    out->esito = (PhDatiPosEsito)esito;
    out->lat_millesimi = lat;
    out->lon_millesimi = lon;
    out->eta_nota = eta != 0xffffffffu;
    out->eta_s = out->eta_nota ? eta : 0;
    out->fornitore = fornitore;
    memcpy(out->localita, pulita, strlen(pulita)); /* al massimo 127 byte: il resto e' gia' zero */
    out->ricerca_avviata = ricerca != 0;
    free(pulita);
    return 0;
}

int ph_dati_posizione(PhDati *d, bool richiesta_permessa, PhDatiPosizione *out, PhErrore *e)
{
    if (out == NULL)
        return ph_errore(e, "posizione: parametri mancanti");
    memset(out, 0, sizeof *out);
    out->esito = PH_DATI_POS_SCONOSCIUTA;
    if (ph_dati_entra(d, e) != 0)
        return -1;
    int r = posizione_i(d, richiesta_permessa, out, e);
    ph_dati_esci(d);
    return r;
}

/* ---- Chiamate (0x91), rete (0x92), media (0x93), comando media (0x94) -----------------
 * Protocollo 3. Ogni risposta si legge per intero: campo fuori elenco, stringa non
 * UTF-8 o oltre il massimo, corpo troncato o con coda = errore (messaggio fisso).
 * RISERVATEZZA: nomi, numeri, reti, titoli restano in *out, mai in un messaggio d'errore. */

/* Legge una stringa del protocollo (u16 + byte) nel vettore dst di `cap` byte: al massimo
 * cap-1 byte utili, UTF-8 valido, controlli ripuliti. dst e' gia' azzerato. 0 / -1 / -2 (memoria). */
static int leggi_campo(PhLettore *l, char *dst, size_t cap)
{
    const uint8_t *s = NULL;
    size_t n = 0;
    ph_dati_leggi_str(l, &s, &n);
    if (l->rotto || n > cap - 1 || !ph_dati_utf8_valido(s, n))
        return -1;
    if (n == 0)
        return 0;
    char *pulita = ph_dati_pulisci(s, n, cap - 1, PH_PUL_RIGA);
    if (pulita == NULL)
        return -2;
    memcpy(dst, pulita, strlen(pulita)); /* mai piu' lunga di n byte: il resto e' zero */
    free(pulita);
    return 0;
}

/* Esito del telefono: 0 ok, 1 non disponibile, 2 permesso negato. */
#define PH_DT_ESITO_MAX 2

static int chiamate_i(PhDati *d, uint8_t massimo, PhDatiChiamate *out, PhErrore *e)
{
    PhBuffer m = PH_BUFFER_VUOTO;
    if (ph_msg_inizia(&m, 0) != 0 || ph_msg_u8(&m, massimo) != 0) {
        ph_buffer_libera(&m);
        return ph_errore(e, "memoria finita");
    }
    PhBuffer r = PH_BUFFER_VUOTO;
    int q = ph_dati_chiedi(d, PH_DT_CHIAMATE, &m, &r, e);
    ph_buffer_libera(&m);
    if (q != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    PhLettore l = {r.dati, r.lunghezza, 0, false};
    uint8_t esito = ph_leggi_u8(&l);
    uint8_t n = ph_leggi_u8(&l);
    /* Con esito diverso da 0 la lista e' vuota; n mai oltre il massimo chiesto. */
    bool buona = !l.rotto && esito <= PH_DT_ESITO_MAX && n <= massimo && n <= PH_DATI_MAX_CHIAMATE &&
                 (esito == 0 || n == 0);
    int memoria = 0;
    PhDatiChiamate tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.esito = esito;
    for (size_t i = 0; buona && i < n; i++) {
        PhDatiChiamata *c = &tmp.v[i];
        c->tipo = ph_leggi_u8(&l);
        uint32_t alto = ph_leggi_u32(&l);
        uint32_t basso = ph_leggi_u32(&l);
        c->ora_ms = ((uint64_t)alto << 32) | basso;
        c->durata_s = ph_leggi_u32(&l);
        int a = leggi_campo(&l, c->nome, sizeof c->nome);
        int b = a == 0 ? leggi_campo(&l, c->numero, sizeof c->numero) : a;
        if (b == -2)
            memoria = 1;
        buona = !l.rotto && b == 0 && c->tipo <= 4;
    }
    buona = buona && !l.rotto && l.posizione == l.lunghezza;
    ph_buffer_libera(&r);
    if (memoria)
        return ph_errore(e, "memoria finita");
    if (!buona)
        return ph_errore(e, "risposta sulle chiamate non valida");
    tmp.n = n;
    *out = tmp;
    return 0;
}

int ph_dati_chiamate(PhDati *d, PhDatiChiamate *out, PhErrore *e)
{
    if (out == NULL)
        return ph_errore(e, "chiamate: parametri mancanti");
    memset(out, 0, sizeof *out);
    if (ph_dati_entra(d, e) != 0)
        return -1;
    /* Si chiede sempre il massimo: chi chiama ne mostra quante vuole. */
    int r = chiamate_i(d, PH_DATI_MAX_CHIAMATE, out, e);
    ph_dati_esci(d);
    return r;
}

static int rete_i(PhDati *d, PhDatiRete *out, PhErrore *e)
{
    PhBuffer r = PH_BUFFER_VUOTO;
    if (ph_dati_chiedi(d, PH_DT_RETE, NULL, &r, e) != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    PhLettore l = {r.dati, r.lunghezza, 0, false};
    PhDatiRete t;
    memset(&t, 0, sizeof t);
    t.esito = ph_leggi_u8(&l);
    t.wifi = ph_leggi_u8(&l);
    t.wifi_livello = ph_leggi_u8(&l);
    int a = leggi_campo(&l, t.wifi_nome, sizeof t.wifi_nome);
    t.mobile = a == 0 ? ph_leggi_u8(&l) : 0;
    t.mobile_livello = ph_leggi_u8(&l);
    t.generazione = ph_leggi_u8(&l);
    int b = a == 0 ? leggi_campo(&l, t.operatore, sizeof t.operatore) : a;
    uint8_t dm = ph_leggi_u8(&l);
    uint8_t ae = ph_leggi_u8(&l);
    bool buona = !l.rotto && a == 0 && b == 0 && l.posizione == l.lunghezza &&
                 t.esito <= PH_DT_ESITO_MAX && t.wifi <= 2 && t.wifi_livello <= 4 &&
                 t.mobile <= 2 && t.mobile_livello <= 4 &&
                 (t.generazione == 0 || (t.generazione >= 2 && t.generazione <= 5)) &&
                 dm <= 2 && ae <= 2;
    ph_buffer_libera(&r);
    if (a == -2 || b == -2)
        return ph_errore(e, "memoria finita");
    if (!buona)
        return ph_errore(e, "risposta sulla rete non valida");
    t.dati_mobili = dm;
    t.aereo = ae;
    if (t.esito != 0) {
        /* Senza esito buono il resto e' zero: valori diversi = risposta non valida. */
        PhDatiRete z;
        memset(&z, 0, sizeof z);
        z.esito = t.esito;
        if (memcmp(&z, &t, sizeof z) != 0)
            return ph_errore(e, "risposta sulla rete non valida");
    }
    *out = t;
    return 0;
}

int ph_dati_rete(PhDati *d, PhDatiRete *out, PhErrore *e)
{
    if (out == NULL)
        return ph_errore(e, "rete: parametri mancanti");
    memset(out, 0, sizeof *out);
    if (ph_dati_entra(d, e) != 0)
        return -1;
    int r = rete_i(d, out, e);
    ph_dati_esci(d);
    return r;
}

static int media_i(PhDati *d, PhDatiMedia *out, PhErrore *e)
{
    PhBuffer r = PH_BUFFER_VUOTO;
    if (ph_dati_chiedi(d, PH_DT_MEDIA, NULL, &r, e) != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    PhLettore l = {r.dati, r.lunghezza, 0, false};
    PhDatiMedia t;
    memset(&t, 0, sizeof t);
    t.esito = ph_leggi_u8(&l);
    t.stato = ph_leggi_u8(&l);
    int a = leggi_campo(&l, t.titolo, sizeof t.titolo);
    int b = a == 0 ? leggi_campo(&l, t.artista, sizeof t.artista) : a;
    int c = b == 0 ? leggi_campo(&l, t.pacchetto, sizeof t.pacchetto) : b;
    t.comandi = ph_leggi_u8(&l);
    bool buona = !l.rotto && c == 0 && l.posizione == l.lunghezza &&
                 t.esito <= PH_DT_ESITO_MAX && t.stato <= 2 && t.comandi <= 7;
    ph_buffer_libera(&r);
    if (c == -2)
        return ph_errore(e, "memoria finita");
    if (!buona)
        return ph_errore(e, "risposta sui media non valida");
    if (t.esito != 0 || t.stato == 0) {
        /* Senza esito buono o senza sessione il resto e' zero. */
        PhDatiMedia z;
        memset(&z, 0, sizeof z);
        z.esito = t.esito;
        if (memcmp(&z, &t, sizeof z) != 0)
            return ph_errore(e, "risposta sui media non valida");
    }
    *out = t;
    return 0;
}

int ph_dati_media(PhDati *d, PhDatiMedia *out, PhErrore *e)
{
    if (out == NULL)
        return ph_errore(e, "media: parametri mancanti");
    memset(out, 0, sizeof *out);
    if (ph_dati_entra(d, e) != 0)
        return -1;
    int r = media_i(d, out, e);
    ph_dati_esci(d);
    return r;
}

int ph_dati_media_comando(PhDati *d, uint8_t comando, PhErrore *e)
{
    if (comando < 1 || comando > 3)
        return ph_errore(e, "comando media non valido");
    if (ph_dati_entra(d, e) != 0)
        return -1;
    int r = manda_u8(d, PH_DT_MEDIA_COMANDO, comando, e);
    ph_dati_esci(d);
    return r;
}
