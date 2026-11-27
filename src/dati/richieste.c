/* Richieste e risposte 0x70-0x75 (docs/protocollo.md 2.6) e ascolto degli
 * eventi 0x7e-0x7f. Il corpo di ogni risposta si legge solo con PhLettore e si
 * controlla per intero: conteggio dentro il tetto e dentro i byte disponibili,
 * nessun byte avanzato. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dati/interno.h"

_Thread_local int ph_dati_in_avviso;

char *ph_dati_per_schermo(const char *testo)
{
    if (testo == NULL)
        return NULL;
    size_t n = strlen(testo);
    return ph_dati_pulisci((const uint8_t *)testo, n, n * 3, PH_PUL_RIGA);
}

/* ---- Costruzione del corpo delle richieste ---------------------------------
 * ph_msg_* scrive un messaggio intero (con testa da 5 byte): qui serve solo il
 * corpo, quindi si salta la testa. */
static int corpo_stringa_u16(PhBuffer *b, const char *s, uint16_t v, bool con_u16)
{
    if (ph_msg_inizia(b, 0) != 0 || ph_msg_stringa(b, s) != 0)
        return -1;
    if (con_u16 && ph_msg_u16(b, v) != 0)
        return -1;
    return 0;
}

int ph_dati_chiedi(PhDati *d, uint8_t tipo, const PhBuffer *msg, PhBuffer *risposta, PhErrore *e)
{
    const uint8_t *corpo = msg != NULL ? msg->dati + 5 : NULL;
    size_t n = msg != NULL ? msg->lunghezza - 5 : 0;
    return ph_componente_richiesta(d->c, tipo, corpo, n, risposta, e);
}

/* ---- Eventi ----------------------------------------------------------------*/
static int stato_leggi(const uint8_t *p, size_t n, PhDatiStato *s)
{
    if (p == NULL || n != 5)
        return -1;
    if (p[0] > 100 || p[1] > 1 || p[2] > 2 || p[3] > 4 || p[4] > 1)
        return -1;
    s->batteria = p[0];
    s->in_carica = p[1] != 0;
    s->rete = (PhDatiTipoRete)p[2];
    s->segnale = p[3];
    s->bloccato = p[4] != 0;
    return 0;
}

/* Dal thread interno del componente: niente richieste da qui. */
static void ascolto(uint8_t tipo, const uint8_t *corpo, size_t n, void *utente)
{
    PhDati *d = utente;
    if (atomic_load(&d->chiuso))
        return;
    if (tipo == PH_DT_STATO_CAMBIATO) {
        PhDatiStato s;
        if (d->avvisi.stato != NULL && stato_leggi(corpo, n, &s) == 0) {
            ph_dati_in_avviso++;
            d->avvisi.stato(&s, d->avvisi.utente);
            ph_dati_in_avviso--;
        }
    } else if (tipo == PH_DT_NOTIFICHE_CAMBIATE) {
        if (d->avvisi.notifiche != NULL) {
            ph_dati_in_avviso++;
            d->avvisi.notifiche(d->avvisi.utente);
            ph_dati_in_avviso--;
        }
    }
}

PhDati *ph_dati_apri(PhComponente *c, const PhDatiAvvisi *avvisi, PhErrore *e)
{
    if (c == NULL) {
        ph_errore(e, "dati: componente mancante");
        return NULL;
    }
    PhDati *d = calloc(1, sizeof *d);
    if (d == NULL) {
        ph_errore(e, "dati: memoria finita");
        return NULL;
    }
    d->c = c;
    if (avvisi != NULL)
        d->avvisi = *avvisi;
    atomic_init(&d->chiuso, false);
    atomic_init(&d->app_stop, false);
    atomic_init(&d->app_caduto, false);
    atomic_init(&d->ricerche, 0);
    atomic_init(&d->da_annullare, 0);
    d->in_uso = 0;
    d->chiudendo = false;
    pthread_mutex_init(&d->m_ric, NULL);
    pthread_mutex_init(&d->m_ctrl, NULL);
    pthread_cond_init(&d->cv_ric, NULL);
    if (pthread_mutex_init(&d->m_app, NULL) != 0) {
        pthread_cond_destroy(&d->cv_ric);
        pthread_mutex_destroy(&d->m_ric);
        pthread_mutex_destroy(&d->m_ctrl);
        free(d);
        ph_errore(e, "dati: impossibile creare il mutex");
        return NULL;
    }
    if (pthread_cond_init(&d->app_cv, NULL) != 0) {
        pthread_mutex_destroy(&d->m_app);
        pthread_cond_destroy(&d->cv_ric);
        pthread_mutex_destroy(&d->m_ric);
        pthread_mutex_destroy(&d->m_ctrl);
        free(d);
        ph_errore(e, "dati: impossibile creare la variabile di condizione");
        return NULL;
    }
    if (ph_componente_ascolta(c, PH_DT_STATO_CAMBIATO, PH_DT_NOTIFICHE_CAMBIATE, ascolto, d, e) !=
        0) {
        pthread_cond_destroy(&d->app_cv);
        pthread_mutex_destroy(&d->m_app);
        pthread_cond_destroy(&d->cv_ric);
        pthread_mutex_destroy(&d->m_ric);
        pthread_mutex_destroy(&d->m_ctrl);
        free(d);
        return NULL;
    }
    return d;
}

int ph_dati_entra(PhDati *d, PhErrore *e)
{
    if (d == NULL)
        return ph_errore(e, "dati: oggetto mancante");
    int r = 0;
    pthread_mutex_lock(&d->m_ric);
    if (d->chiudendo)
        r = ph_errore(e, "dati: in chiusura");
    else
        d->in_uso++;
    pthread_mutex_unlock(&d->m_ric);
    return r;
}

void ph_dati_esci(PhDati *d)
{
    pthread_mutex_lock(&d->m_ric);
    d->in_uso--;
    pthread_cond_broadcast(&d->cv_ric);
    pthread_mutex_unlock(&d->m_ric);
}

int ph_dati_chiudi(PhDati *d, PhErrore *e)
{
    if (d == NULL)
        return 0;
    if (ph_dati_in_avviso > 0) {
        fprintf(stderr, "dati: ph_dati_chiudi chiamata da dentro un avviso: ignorata\n");
        return ph_errore(e, "dati: chiusura rifiutata, chiamata da dentro un avviso");
    }
    /* Tutto sotto m_ric: da "chiudendo" in poi nessuna ph_dati_cerca_avvia
     * nuova puo' entrare, e quelle gia' entrate si aspettano prima di
     * guardare le ricerche non annullate. */
    pthread_mutex_lock(&d->m_ric);
    if (d->chiudendo) {
        pthread_mutex_unlock(&d->m_ric);
        return ph_errore(e, "dati: chiusura rifiutata, un'altra chiusura e' in corso");
    }
    d->chiudendo = true;
    while (d->in_uso > 0)
        pthread_cond_wait(&d->cv_ric, &d->m_ric);
    if (atomic_load(&d->da_annullare) > 0) {
        d->chiudendo = false; /* rifiutata: tutto torna come prima */
        pthread_mutex_unlock(&d->m_ric);
        fprintf(stderr, "dati: ph_dati_chiudi con ricerche non annullate: annullarle prima "
                        "(ph_dati_cerca_annulla); non chiuso\n");
        return ph_errore(e, "dati: chiusura rifiutata, ci sono ricerche non annullate");
    }
    /* Restano solo ricerche gia' annullate che escono da sole: attesa breve. */
    while (atomic_load(&d->ricerche) > 0)
        pthread_cond_wait(&d->cv_ric, &d->m_ric);
    pthread_mutex_unlock(&d->m_ric);
    /* Si tolgono gli ascolti: prima quello dei controlli, poi 0x7e-0x7f. Il passo
     * dei controlli e' sotto m_ctrl (come ph_dati_ascolta_controlli). Se fallisce
     * il primo, nulla e' stato toccato. Se fallisce il secondo, quello dei
     * controlli (gia' tolto) si rimette; se anche questo fallisce (caso
     * estremo) resta tolto: ctrl_registrato torna false e
     * ph_dati_ascolta_controlli si puo' richiamare. ascolta(NULL) aspetta anche
     * la fine di un avviso in corso. */
    pthread_mutex_lock(&d->m_ctrl);
    pthread_mutex_lock(&d->m_ric);
    bool ctrl = d->ctrl_registrato;
    pthread_mutex_unlock(&d->m_ric);
    bool fallita = ctrl && ph_componente_ascolta(d->c, PH_DT_CONTROLLI_CAMBIATI, PH_DT_CONTROLLI_CAMBIATI,
                                                 NULL, NULL, NULL) != 0;
    if (!fallita &&
        ph_componente_ascolta(d->c, PH_DT_STATO_CAMBIATO, PH_DT_NOTIFICHE_CAMBIATE, NULL, NULL,
                              NULL) != 0) {
        fallita = true;
        if (ctrl && ph_componente_ascolta(d->c, PH_DT_CONTROLLI_CAMBIATI, PH_DT_CONTROLLI_CAMBIATI,
                                          ph_dati_ascolto_controlli, d, NULL) != 0) {
            pthread_mutex_lock(&d->m_ric);
            d->ctrl_registrato = false;
            d->ctrl_avviso = NULL;
            d->ctrl_utente = NULL;
            pthread_mutex_unlock(&d->m_ric);
        }
    }
    pthread_mutex_unlock(&d->m_ctrl);
    if (fallita) {
        /* Rifiutata: le chiamate ripartono. */
        pthread_mutex_lock(&d->m_ric);
        d->chiudendo = false;
        pthread_mutex_unlock(&d->m_ric);
        return ph_errore(e, "dati: impossibile togliere l'ascolto degli eventi");
    }
    /* Da qui non si rifiuta piu': tutto il resto non puo' fallire. */
    atomic_store(&d->chiuso, true);
    ph_dati_appunti_chiudi_i(d);
    pthread_cond_destroy(&d->app_cv);
    pthread_mutex_destroy(&d->m_app);
    pthread_cond_destroy(&d->cv_ric);
    pthread_mutex_destroy(&d->m_ric);
    pthread_mutex_destroy(&d->m_ctrl);
    free(d);
    return 0;
}

/* ---- App ---------------------------------------------------------------------*/
void ph_dati_app_libera(PhDatiApp *app, size_t quante)
{
    if (app == NULL)
        return;
    for (size_t i = 0; i < quante; i++) {
        free(app[i].pacchetto);
        free(app[i].nome);
    }
    free(app);
}

/* Legge una stringa dal lettore e la mette in *out pulita (modo PH_PUL_*). */
int ph_dati_leggi_pulita(PhLettore *l, char **out, size_t massimo, int modo)
{
    const uint8_t *p;
    size_t n;
    ph_dati_leggi_str(l, &p, &n);
    if (l->rotto)
        return -1;
    *out = ph_dati_pulisci(p, n, massimo, modo);
    return *out != NULL ? 0 : -2;
}

static int app_elenco_i(PhDati *d, PhDatiApp **app, size_t *quante, PhErrore *e)
{
    *app = NULL;
    *quante = 0;
    PhBuffer r = PH_BUFFER_VUOTO;
    if (ph_dati_chiedi(d, PH_DT_APP_ELENCO, NULL, &r, e) != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    PhLettore l = {r.dati, r.lunghezza, 0, false};
    size_t n = ph_leggi_u16(&l);
    PhDatiApp *v = NULL;
    int esito = -1;
    const char *perche = "elenco delle app troncato o non valido";
    if (l.rotto)
        goto fine;
    /* Ogni app occupa almeno 5 byte: un conteggio piu' grande dei byte
     * presenti e' una bugia e non deve guidare l'allocazione. */
    if (n > PH_DATI_APP_MAX) {
        perche = "troppe app nell'elenco";
        goto fine;
    }
    if (n > (l.lunghezza - l.posizione) / 5)
        goto fine;
    if (n > 0) {
        v = calloc(n, sizeof *v);
        if (v == NULL) {
            perche = "memoria finita";
            goto fine;
        }
    }
    for (size_t i = 0; i < n; i++) {
        int a = ph_dati_leggi_pulita(&l, &v[i].pacchetto, PH_DATI_NOME_MAX, PH_PUL_RIGA);
        int b = a == 0 ? ph_dati_leggi_pulita(&l, &v[i].nome, PH_DATI_NOME_MAX, PH_PUL_RIGA) : a;
        uint8_t segni = ph_leggi_u8(&l);
        if (b == -2) {
            perche = "memoria finita";
            goto fine;
        }
        if (b != 0 || l.rotto)
            goto fine;
        v[i].di_sistema = (segni & 1) != 0;
        v[i].solo_verticale = (segni & 2) != 0;
    }
    if (l.posizione != l.lunghezza) {
        perche = "elenco delle app con dati in eccesso";
        goto fine;
    }
    esito = 0;
fine:
    ph_buffer_libera(&r);
    if (esito != 0) {
        ph_dati_app_libera(v, n);
        return ph_errore(e, "%s", perche);
    }
    *app = v;
    *quante = n;
    return 0;
}

/* ---- Icona e miniatura -----------------------------------------------------------*/
/* Chiede e controlla soltanto che la risposta cominci con la firma attesa. */
static int immagine(PhDati *d, uint8_t tipo, const PhBuffer *msg, const uint8_t *firma,
                    size_t lf, const char *nome, PhBuffer *out, PhErrore *e)
{
    PhBuffer r = PH_BUFFER_VUOTO;
    if (ph_dati_chiedi(d, tipo, msg, &r, e) != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    if (r.dati == NULL || r.lunghezza <= lf || memcmp(r.dati, firma, lf) != 0) {
        ph_buffer_libera(&r);
        return ph_errore(e, "la risposta non e' un'immagine %s", nome);
    }
    ph_buffer_libera(out);
    *out = r;
    return 0;
}

static int app_icona_i(PhDati *d, const char *pacchetto, uint16_t lato, PhBuffer *png, PhErrore *e)
{
    static const uint8_t firma[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (pacchetto == NULL || pacchetto[0] == '\0' || strlen(pacchetto) > PH_DATI_NOME_MAX)
        return ph_errore(e, "nome del pacchetto non valido");
    if (lato < 16 || lato > 512)
        return ph_errore(e, "lato dell'icona fuori da 16-512");
    PhBuffer m = PH_BUFFER_VUOTO;
    if (corpo_stringa_u16(&m, pacchetto, lato, true) != 0) {
        ph_buffer_libera(&m);
        return ph_errore(e, "memoria finita");
    }
    int r = immagine(d, PH_DT_APP_ICONA, &m, firma, sizeof firma, "PNG", png, e);
    ph_buffer_libera(&m);
    return r;
}

static int miniatura_i(PhDati *d, const char *percorso, uint16_t lato, PhBuffer *jpeg, PhErrore *e)
{
    static const uint8_t firma[3] = {0xff, 0xd8, 0xff};
    if (percorso == NULL || percorso[0] == '\0' || strlen(percorso) > PH_DATI_PERCORSO_MAX)
        return ph_errore(e, "percorso non valido");
    if (lato < 32 || lato > 512)
        return ph_errore(e, "lato della miniatura fuori da 32-512");
    PhBuffer m = PH_BUFFER_VUOTO;
    if (corpo_stringa_u16(&m, percorso, lato, true) != 0) {
        ph_buffer_libera(&m);
        return ph_errore(e, "memoria finita");
    }
    int r = immagine(d, PH_DT_MINIATURA, &m, firma, sizeof firma, "JPEG", jpeg, e);
    ph_buffer_libera(&m);
    return r;
}

/* ---- Stato e memoria -------------------------------------------------------------*/
static int stato_i(PhDati *d, PhDatiStato *stato, PhErrore *e)
{
    PhBuffer r = PH_BUFFER_VUOTO;
    if (ph_dati_chiedi(d, PH_DT_STATO, NULL, &r, e) != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    int esito = stato_leggi(r.dati, r.lunghezza, stato);
    ph_buffer_libera(&r);
    if (esito != 0)
        return ph_errore(e, "risposta di stato non valida");
    return 0;
}

static int memoria_i(PhDati *d, PhDatiMemoria *memoria, PhErrore *e)
{
    PhBuffer r = PH_BUFFER_VUOTO;
    if (ph_dati_chiedi(d, PH_DT_MEMORIA, NULL, &r, e) != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    PhLettore l = {r.dati, r.lunghezza, 0, false};
    uint64_t totale = ph_leggi_u64(&l);
    uint64_t libera = ph_leggi_u64(&l);
    bool buona = !l.rotto && l.posizione == l.lunghezza && libera <= totale;
    ph_buffer_libera(&r);
    if (!buona)
        return ph_errore(e, "risposta sulla memoria non valida");
    memoria->totale = totale;
    memoria->libera = libera;
    return 0;
}

/* ---- Notifiche -----------------------------------------------------------------------*/
void ph_dati_notifiche_libera(PhDatiNotifica *notifiche, size_t quante)
{
    if (notifiche == NULL)
        return;
    for (size_t i = 0; i < quante; i++) {
        free(notifiche[i].chiave);
        free(notifiche[i].pacchetto);
        free(notifiche[i].titolo);
        free(notifiche[i].testo);
    }
    free(notifiche);
}

static int notifiche_i(PhDati *d, PhDatiNotifica **notifiche, size_t *quante, PhErrore *e)
{
    *notifiche = NULL;
    *quante = 0;
    PhBuffer r = PH_BUFFER_VUOTO;
    if (ph_dati_chiedi(d, PH_DT_NOTIFICHE, NULL, &r, e) != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    PhLettore l = {r.dati, r.lunghezza, 0, false};
    size_t n = ph_leggi_u16(&l);
    PhDatiNotifica *v = NULL;
    int esito = -1;
    const char *perche = "elenco delle notifiche troncato o non valido";
    if (l.rotto)
        goto fine;
    if (n > PH_DATI_NOTIFICHE_MAX) {
        perche = "troppe notifiche nell'elenco";
        goto fine;
    }
    /* Ogni notifica occupa almeno 4 stringhe vuote + l'ora: 16 byte. */
    if (n > (l.lunghezza - l.posizione) / 16)
        goto fine;
    if (n > 0) {
        v = calloc(n, sizeof *v);
        if (v == NULL) {
            perche = "memoria finita";
            goto fine;
        }
    }
    for (size_t i = 0; i < n; i++) {
        int k = ph_dati_leggi_pulita(&l, &v[i].chiave, PH_DATI_CHIAVE_MAX, PH_PUL_RIGA);
        if (k == 0)
            k = ph_dati_leggi_pulita(&l, &v[i].pacchetto, PH_DATI_NOME_MAX, PH_PUL_RIGA);
        if (k == 0)
            k = ph_dati_leggi_pulita(&l, &v[i].titolo, PH_DATI_NOME_MAX, PH_PUL_RIGA);
        if (k == 0)
            k = ph_dati_leggi_pulita(&l, &v[i].testo, PH_DATI_TESTO_NOTIFICA_MAX, PH_PUL_TESTO);
        v[i].ora_ms = ph_leggi_u64(&l);
        if (k == -2) {
            perche = "memoria finita";
            goto fine;
        }
        if (k != 0 || l.rotto)
            goto fine;
    }
    if (l.posizione != l.lunghezza) {
        perche = "elenco delle notifiche con dati in eccesso";
        goto fine;
    }
    esito = 0;
fine:
    ph_buffer_libera(&r);
    if (esito != 0) {
        ph_dati_notifiche_libera(v, n);
        return ph_errore(e, "%s", perche);
    }
    *notifiche = v;
    *quante = n;
    return 0;
}

/* ---- Memoria per categoria e calendario ---------------------------------------*/
#define PH_NON_LEGGIBILE UINT64_MAX

static void voce(PhDatiVoceMemoria *v, uint64_t sul_filo)
{
    v->leggibile = sul_filo != PH_NON_LEGGIBILE;
    v->byte = v->leggibile ? sul_filo : 0;
}

static int memoria_dettaglio_i(PhDati *d, PhDatiMemoriaDettaglio *dettaglio, PhErrore *e)
{
    PhBuffer r = PH_BUFFER_VUOTO;
    if (ph_dati_chiedi(d, PH_DT_MEMORIA_DETTAGLIO, NULL, &r, e) != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    PhLettore l = {r.dati, r.lunghezza, 0, false};
    uint64_t v[4];
    for (int i = 0; i < 4; i++)
        v[i] = ph_leggi_u64(&l);
    bool buona = !l.rotto && l.posizione == l.lunghezza;
    ph_buffer_libera(&r);
    if (!buona)
        return ph_errore(e, "risposta sulla memoria per categoria non valida");
    voce(&dettaglio->immagini, v[0]);
    voce(&dettaglio->video, v[1]);
    voce(&dettaglio->audio, v[2]);
    voce(&dettaglio->app, v[3]);
    return 0;
}

void ph_dati_calendario_libera(PhDatiEvento *eventi, size_t quanti)
{
    if (eventi == NULL)
        return;
    for (size_t i = 0; i < quanti; i++)
        free(eventi[i].titolo);
    free(eventi);
}

/* Legge n eventi (titolo, inizio, fine, tutto il giorno) dal lettore: n gia'
 * validato contro il massimo chiesto. Il conteggio si controlla anche contro i
 * byte presenti (ogni evento ne occupa almeno 19: titolo vuoto 2 + inizio 8 +
 * fine 8 + flag 1), cosi' un numero falso non guida l'allocazione. A errore
 * *out resta NULL, nulla e' allocato e *perche dice il motivo (mai un titolo). */
int ph_dati_leggi_eventi(PhLettore *l, size_t n, PhDatiEvento **out, const char **perche)
{
    *out = NULL;
    *perche = "elenco degli eventi troncato o non valido";
    if (l->rotto || l->posizione > l->lunghezza || n > (l->lunghezza - l->posizione) / 19)
        return -1;
    if (n == 0)
        return 0;
    PhDatiEvento *v = calloc(n, sizeof *v);
    if (v == NULL) {
        *perche = "memoria finita";
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        int k = ph_dati_leggi_pulita(l, &v[i].titolo, PH_DATI_NOME_MAX, PH_PUL_RIGA);
        v[i].inizio_ms = ph_leggi_u64(l);
        v[i].fine_ms = ph_leggi_u64(l);
        uint8_t giorno = ph_leggi_u8(l);
        if (k == -2)
            *perche = "memoria finita";
        if (k != 0 || l->rotto || giorno > 1 || v[i].fine_ms < v[i].inizio_ms) {
            ph_dati_calendario_libera(v, n);
            return -1;
        }
        v[i].tutto_il_giorno = giorno != 0;
    }
    *out = v;
    return 0;
}

/* Nessun messaggio d'errore di questa funzione contiene il contenuto degli eventi. */
static int calendario_i(PhDati *d, uint8_t massimo, PhDatiEvento **eventi, size_t *quanti,
                        PhErrore *e)
{
    if (eventi == NULL || quanti == NULL)
        return ph_errore(e, "calendario: parametri mancanti");
    if (massimo < 1 || massimo > PH_DATI_CALENDARIO_MAX)
        return ph_errore(e, "calendario: massimo fuori da 1-%d", PH_DATI_CALENDARIO_MAX);
    PhBuffer m = PH_BUFFER_VUOTO;
    if (ph_msg_inizia(&m, 0) != 0 || ph_msg_u8(&m, massimo) != 0) {
        ph_buffer_libera(&m);
        return ph_errore(e, "memoria finita");
    }
    PhBuffer r = PH_BUFFER_VUOTO;
    int q = ph_dati_chiedi(d, PH_DT_CALENDARIO, &m, &r, e);
    ph_buffer_libera(&m);
    if (q != 0) {
        ph_buffer_libera(&r);
        return -1;
    }
    PhLettore l = {r.dati, r.lunghezza, 0, false};
    size_t n = ph_leggi_u16(&l);
    PhDatiEvento *v = NULL;
    int esito = -1;
    const char *perche = "elenco degli eventi troncato o non valido";
    if (l.rotto)
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
        return ph_errore(e, "%s", perche);
    }
    *eventi = v;
    *quanti = n;
    return 0;
}

/* ---- Funzioni pubbliche: tengono il contatore d'uso (chiusura in parallelo) ---*/
int ph_dati_app_elenco(PhDati *d, PhDatiApp **app, size_t *quante, PhErrore *e)
{
    if (ph_dati_entra(d, e) != 0) {
        *app = NULL;
        *quante = 0;
        return -1;
    }
    int r = app_elenco_i(d, app, quante, e);
    ph_dati_esci(d);
    return r;
}

int ph_dati_app_icona(PhDati *d, const char *pacchetto, uint16_t lato, PhBuffer *png, PhErrore *e)
{
    if (ph_dati_entra(d, e) != 0) {
        
        return -1;
    }
    int r = app_icona_i(d, pacchetto, lato, png, e);
    ph_dati_esci(d);
    return r;
}

int ph_dati_miniatura(PhDati *d, const char *percorso, uint16_t lato, PhBuffer *jpeg, PhErrore *e)
{
    if (ph_dati_entra(d, e) != 0) {
        
        return -1;
    }
    int r = miniatura_i(d, percorso, lato, jpeg, e);
    ph_dati_esci(d);
    return r;
}

int ph_dati_stato(PhDati *d, PhDatiStato *stato, PhErrore *e)
{
    if (ph_dati_entra(d, e) != 0) {
        
        return -1;
    }
    int r = stato_i(d, stato, e);
    ph_dati_esci(d);
    return r;
}

int ph_dati_memoria(PhDati *d, PhDatiMemoria *memoria, PhErrore *e)
{
    if (ph_dati_entra(d, e) != 0) {
        
        return -1;
    }
    int r = memoria_i(d, memoria, e);
    ph_dati_esci(d);
    return r;
}

int ph_dati_notifiche(PhDati *d, PhDatiNotifica **notifiche, size_t *quante, PhErrore *e)
{
    if (ph_dati_entra(d, e) != 0) {
        *notifiche = NULL;
        *quante = 0;
        return -1;
    }
    int r = notifiche_i(d, notifiche, quante, e);
    ph_dati_esci(d);
    return r;
}

int ph_dati_memoria_dettaglio(PhDati *d, PhDatiMemoriaDettaglio *dettaglio, PhErrore *e)
{
    if (ph_dati_entra(d, e) != 0)
        return -1;
    int r = memoria_dettaglio_i(d, dettaglio, e);
    ph_dati_esci(d);
    return r;
}

int ph_dati_calendario(PhDati *d, uint8_t massimo, PhDatiEvento **eventi, size_t *quanti,
                       PhErrore *e)
{
    if (eventi != NULL)
        *eventi = NULL;
    if (quanti != NULL)
        *quanti = 0;
    if (ph_dati_entra(d, e) != 0)
        return -1;
    int r = calendario_i(d, massimo, eventi, quanti, e);
    ph_dati_esci(d);
    return r;
}
