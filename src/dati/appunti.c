/* Appunti condivisi sul canale "appunti" (docs/protocollo.md 2.8). Il testo
 * porta una lunghezza u32 propria. Un thread legge i testi dal telefono; le
 * scritture le fanno i thread di chi chiama, una alla volta. */
#include <stdlib.h>
#include <string.h>

#include "dati/interno.h"

#define PH_DT_APPUNTI_MAX_CORPO (4u + PH_DATI_APPUNTI_MAX)
/* Il thread vive per tutta la sessione: fette lunghe, poche attese a vuoto. */
#define PH_DT_APPUNTI_FETTA_MS 150

/* Il PhDati di cui il thread corrente e' il lettore degli appunti (NULL fuori):
 * riconosce una chiusura chiesta dal proprio avviso senza leggere dati altrui. */
static _Thread_local PhDati *appunti_corrente;

static void avvisa(PhDati *d, const char *testo, size_t n)
{
    ph_dati_in_avviso++;
    d->app_avviso(testo, n, d->app_utente);
    ph_dati_in_avviso--;
}

static void *lavoro(void *arg)
{
    PhDati *d = arg;
    appunti_corrente = d;
    PhErrore e;
    PhBuffer corpo = PH_BUFFER_VUOTO;
    PhCanale *canale = d->app_canale; /* fissato prima della creazione del thread */
    int fine;
    for (;;) {
        uint8_t tipo = 0;
        bool scartato = false;
        fine = ph_dati_leggi_messaggio(canale, &d->app_stop, PH_DT_APPUNTI_FETTA_MS,
                                       PH_DT_APPUNTI_MAX_CORPO, &tipo, &corpo, &scartato, &e);
        if (fine != 1)
            break; /* chiuso, caduto, stop o messaggio oltre il tetto del protocollo */
        if (tipo != PH_DT_APPUNTI_TESTO || scartato)
            continue; /* testo oltre 200.000 byte e altri tipi: ignorati */
        PhLettore l = {corpo.dati, corpo.lunghezza, 0, false};
        size_t n = ph_leggi_u32(&l);
        /* La lunghezza interna deve coincidere con i byte presenti. */
        if (l.rotto || n == 0 || n > PH_DATI_APPUNTI_MAX || n != l.lunghezza - l.posizione)
            continue;
        char *testo = ph_dati_pulisci(l.dati + l.posizione, n, n * 3, PH_PUL_APPUNTI);
        if (testo == NULL)
            continue;
        size_t lung = strlen(testo);
        /* La sostituzione puo' allungare: oltre il limite il messaggio si scarta. */
        if (lung <= PH_DATI_APPUNTI_MAX && !atomic_load(&d->app_stop))
            avvisa(d, testo, lung);
        free(testo);
    }
    ph_buffer_libera(&corpo);
    if (fine != -2 && !atomic_load(&d->app_stop)) {
        /* Il lettore e' finito da solo: lo si dice una volta (testo NULL). */
        atomic_store(&d->app_caduto, true);
        avvisa(d, NULL, 0);
    }
    /* Chiusura chiesta da un nostro avviso: nessuno aspetta, il canale lo chiudiamo qui. */
    pthread_mutex_lock(&d->m_app);
    if (d->app_distaccato) {
        ph_canale_chiudi(d->app_canale);
        d->app_canale = NULL;
        pthread_cond_broadcast(&d->app_cv);
    }
    pthread_mutex_unlock(&d->m_app);
    return NULL;
}

static int apri_i(PhDati *d, PhDatiAvvisoAppunti avviso, void *utente, PhErrore *e)
{
    if (d == NULL || avviso == NULL)
        return ph_errore(e, "appunti: parametri mancanti");
    pthread_mutex_lock(&d->m_app);
    if (d->app_canale != NULL) {
        pthread_mutex_unlock(&d->m_app);
        return ph_errore(e, "appunti: canale gia' aperto");
    }
    PhCanale *c = ph_componente_canale(d->c, "appunti", e);
    if (c == NULL) {
        pthread_mutex_unlock(&d->m_app);
        return -1;
    }
    d->app_canale = c;
    d->app_avviso = avviso;
    d->app_utente = utente;
    d->app_distaccato = false;
    atomic_store(&d->app_stop, false);
    atomic_store(&d->app_caduto, false);
    if (pthread_create(&d->app_thread, NULL, lavoro, d) != 0) {
        d->app_canale = NULL;
        pthread_mutex_unlock(&d->m_app);
        ph_canale_chiudi(c);
        return ph_errore(e, "appunti: impossibile avviare il thread");
    }
    pthread_mutex_unlock(&d->m_app);
    return 0;
}

/* Costruisce il messaggio e lo scrive sul canale. id_sessione == NULL: imposta. */
static int invia_i(PhDati *d, uint8_t tipo, const uint32_t *id_sessione, const char *testo,
                   PhErrore *e)
{
    if (d == NULL || testo == NULL)
        return ph_errore(e, "appunti: parametri mancanti");
    size_t n = strlen(testo);
    if (n == 0)
        return ph_errore(e, "appunti: testo vuoto, non inviato");
    if (n > PH_DATI_APPUNTI_MAX)
        return ph_errore(e, "appunti: testo oltre %d byte, non inviato", PH_DATI_APPUNTI_MAX);
    PhBuffer m = PH_BUFFER_VUOTO;
    if (ph_msg_inizia(&m, tipo) != 0 || (id_sessione != NULL && ph_msg_u32(&m, *id_sessione) != 0) ||
        ph_msg_u32(&m, (uint32_t)n) != 0 || ph_buffer_aggiungi(&m, testo, n) != 0 ||
        ph_msg_finisci(&m) != 0) {
        ph_buffer_libera(&m);
        return ph_errore(e, "appunti: memoria finita");
    }
    int r;
    pthread_mutex_lock(&d->m_app);
    if (d->app_canale == NULL || atomic_load(&d->app_stop))
        r = ph_errore(e, "appunti: canale non aperto");
    else if (atomic_load(&d->app_caduto))
        r = ph_errore(e, "canale appunti caduto");
    else
        r = ph_canale_scrivi(d->app_canale, m.dati, m.lunghezza, e);
    pthread_mutex_unlock(&d->m_app);
    ph_buffer_libera(&m);
    return r;
}

int ph_dati_appunti_apri(PhDati *d, PhDatiAvvisoAppunti avviso, void *utente, PhErrore *e)
{
    if (ph_dati_entra(d, e) != 0)
        return -1;
    int r = apri_i(d, avviso, utente, e);
    ph_dati_esci(d);
    return r;
}

/* Prende il contatore d'uso: ph_dati_chiudi aspetta le scritture in corso. */
static int invia(PhDati *d, uint8_t tipo, const uint32_t *id_sessione, const char *testo,
                 PhErrore *e)
{
    if (ph_dati_entra(d, e) != 0)
        return -1;
    int r = invia_i(d, tipo, id_sessione, testo, e);
    ph_dati_esci(d);
    return r;
}

int ph_dati_appunti_imposta(PhDati *d, const char *testo, PhErrore *e)
{
    return invia(d, PH_DT_APPUNTI_IMPOSTA, NULL, testo, e);
}

int ph_dati_appunti_incolla(PhDati *d, uint32_t id_sessione, const char *testo, PhErrore *e)
{
    return invia(d, PH_DT_APPUNTI_INCOLLA, &id_sessione, testo, e);
}

void ph_dati_appunti_chiudi_i(PhDati *d)
{
    if (d == NULL)
        return;
    pthread_mutex_lock(&d->m_app);
    if (d->app_canale == NULL) {
        pthread_mutex_unlock(&d->m_app);
        return;
    }
    /* Da ora le scritture nuove falliscono; quella in corso tiene il mutex. */
    bool primo = !atomic_exchange(&d->app_stop, true);
    if (appunti_corrente == d) {
        /* Dal nostro stesso avviso: non ci si puo' aspettare. Il thread, uscendo,
         * chiude il canale. */
        if (primo) {
            d->app_distaccato = true;
            pthread_detach(pthread_self());
        }
        pthread_mutex_unlock(&d->m_app);
        return;
    }
    if (primo) {
        pthread_mutex_unlock(&d->m_app);
        pthread_join(d->app_thread, NULL);
        pthread_mutex_lock(&d->m_app);
        ph_canale_chiudi(d->app_canale);
        d->app_canale = NULL;
        pthread_cond_broadcast(&d->app_cv);
        pthread_mutex_unlock(&d->m_app);
        return;
    }
    /* Un altro l'ha gia' fermata (o l'ha chiusa dal proprio avviso): si aspetta che finisca. */
    while (d->app_canale != NULL)
        pthread_cond_wait(&d->app_cv, &d->m_app);
    pthread_mutex_unlock(&d->m_app);
}

/* Pubblica: prende il contatore d'uso, cosi' ph_dati_chiudi la aspetta. Se la
 * chiusura generale e' gia' partita non fa nulla (se ne occupa lei). */
void ph_dati_appunti_chiudi(PhDati *d)
{
    if (d == NULL || ph_dati_entra(d, NULL) != 0)
        return;
    ph_dati_appunti_chiudi_i(d);
    ph_dati_esci(d);
}
