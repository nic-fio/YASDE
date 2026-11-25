/* Ricerca di file sul canale "cerca" (docs/protocollo.md 2.7). Un thread per
 * ricerca: apre il canale, manda CERCA, legge TROVATO/CERCA_FINE e consegna.
 * Il canale lo chiude ph_dati_cerca_annulla dopo il join (o il thread stesso
 * se l'annullamento e' arrivato da un suo avviso), cosi' nessuno usa un canale
 * mentre un altro lo chiude. */
#include <stdlib.h>
#include <string.h>

#include "dati/interno.h"

#define PH_DT_CERCA_FETTA_MS 20
#define PH_DT_CERCA_MAX_CORPO (2u + PH_DATI_PERCORSO_MAX + 1u)

/* Ricerca del thread corrente (NULL fuori dai thread di ricerca): permette di
 * riconoscere un annullamento fatto da un proprio avviso senza leggere dati
 * scritti da altri thread. */
static _Thread_local PhDatiRicerca *ricerca_corrente;

struct PhDatiRicerca {
    PhDati *d;
    PhBuffer richiesta;     /* messaggio CERCA gia' pronto */
    bool solo_cartelle;
    size_t massimo;         /* risultati consegnabili (mai 0) */
    PhDatiTrovato trovato;
    PhDatiCercaFine fine;
    void *utente;
    atomic_bool stop;
    PhCanale *canale;       /* scritto dal thread, letto da annulla dopo il join */
    pthread_t thread;
    bool autolibera;        /* scritto solo dal thread stesso (annulla da un suo avviso) */
};

/* Chiude il canale e libera tutto. La conta delle ricerche si scala per ultima:
 * dopo, ph_dati_chiudi puo' liberare d. */
static void libera_ricerca(PhDatiRicerca *r)
{
    PhDati *d = r->d;
    if (r->canale != NULL)
        ph_canale_chiudi(r->canale);
    ph_buffer_libera(&r->richiesta);
    free(r);
    pthread_mutex_lock(&d->m_ric);
    atomic_fetch_sub(&d->ricerche, 1);
    pthread_cond_broadcast(&d->cv_ric);
    pthread_mutex_unlock(&d->m_ric);
}

static void avviso_trovato(PhDatiRicerca *r, const char *percorso, bool cartella)
{
    if (atomic_load(&r->stop))
        return;
    ph_dati_in_avviso++;
    r->trovato(percorso, cartella, r->utente);
    ph_dati_in_avviso--;
}

/* Avviso finale, una sola volta, mai dopo lo stop. */
static void finale(PhDatiRicerca *r, PhDatiCercaMotivo motivo)
{
    if (atomic_load(&r->stop))
        return;
    ph_dati_in_avviso++;
    r->fine(motivo, r->utente);
    ph_dati_in_avviso--;
}

static void esegui(PhDatiRicerca *r)
{
    PhErrore e;
    PhBuffer corpo = PH_BUFFER_VUOTO;
    size_t consegnati = 0;

    if (atomic_load(&r->stop))
        return;
    r->canale = ph_componente_canale(r->d->c, "cerca", &e);
    if (r->canale == NULL) {
        finale(r, PH_DATI_CERCA_ERRORE);
        return;
    }
    if (atomic_load(&r->stop))
        return;
    if (ph_canale_scrivi(r->canale, r->richiesta.dati, r->richiesta.lunghezza, &e) != 0) {
        finale(r, PH_DATI_CERCA_ERRORE);
        return;
    }

    for (;;) {
        uint8_t tipo = 0;
        bool scartato = false;
        int k = ph_dati_leggi_messaggio(r->canale, &r->stop, PH_DT_CERCA_FETTA_MS,
                                        PH_DT_CERCA_MAX_CORPO, &tipo, &corpo, &scartato, &e);
        if (k == -2)
            break;
        if (k != 1) {
            /* Canale chiuso o caduto prima di CERCA_FINE: non e' "finita". */
            finale(r, PH_DATI_CERCA_ERRORE);
            break;
        }
        if (tipo == PH_DT_TROVATO) {
            /* Un corpo oltre il tetto di un percorso non si legge: e' un
             * percorso impossibile, si salta come ogni percorso non valido. */
            if (scartato)
                continue;
            PhLettore l = {corpo.dati, corpo.lunghezza, 0, false};
            const uint8_t *p;
            size_t n;
            ph_dati_leggi_str(&l, &p, &n);
            uint8_t cart = ph_leggi_u8(&l);
            if (l.rotto || l.posizione != l.lunghezza || cart > 1) {
                finale(r, PH_DATI_CERCA_ERRORE);
                break;
            }
            /* Un percorso si valida, non si modifica: o e' UTF-8 valido senza
             * NUL e arriva identico, o quel solo risultato si salta. */
            if (n == 0 || n > PH_DATI_PERCORSO_MAX || !ph_dati_utf8_valido(p, n))
                continue;
            if (r->solo_cartelle && cart == 0)
                continue; /* il telefono non dovrebbe mandarli: si scartano */
            char *percorso = malloc(n + 1);
            if (percorso == NULL) {
                finale(r, PH_DATI_CERCA_ERRORE);
                break;
            }
            memcpy(percorso, p, n);
            percorso[n] = '\0';
            avviso_trovato(r, percorso, cart != 0);
            free(percorso);
            if (++consegnati >= r->massimo) {
                /* Massimo raggiunto: non si aspetta il telefono. */
                finale(r, PH_DATI_CERCA_MASSIMO);
                break;
            }
        } else if (tipo == PH_DT_CERCA_FINE) {
            if (scartato || corpo.lunghezza != 1 || corpo.dati[0] > 2) {
                finale(r, PH_DATI_CERCA_ERRORE);
            } else {
                finale(r, (PhDatiCercaMotivo)corpo.dati[0]);
            }
            break;
        }
        /* Altri tipi: ignorati (il corpo e' gia' stato consumato). */
    }
    ph_buffer_libera(&corpo);
}

static void *lavoro(void *arg)
{
    PhDatiRicerca *r = arg;
    ricerca_corrente = r;
    esegui(r);
    /* Annullata da un suo avviso: nessuno l'aspetta, la libera il thread. */
    if (r->autolibera)
        libera_ricerca(r);
    return NULL;
}

static PhDatiRicerca *avvia_i(PhDati *d, const PhDatiCerca *q, PhDatiTrovato trovato,
                             PhDatiCercaFine fine, void *utente, PhErrore *e)
{
    if (d == NULL || q == NULL || trovato == NULL || fine == NULL) {
        ph_errore(e, "ricerca: parametri mancanti");
        return NULL;
    }
    if (q->cartella == NULL || q->cartella[0] == '\0' ||
        strlen(q->cartella) > PH_DATI_PERCORSO_MAX) {
        ph_errore(e, "ricerca: cartella di partenza non valida");
        return NULL;
    }
    if (q->testo == NULL || q->testo[0] == '\0' || strlen(q->testo) > PH_DATI_CERCA_TESTO_MAX) {
        ph_errore(e, "ricerca: testo cercato non valido");
        return NULL;
    }
    if (q->massimo > PH_DATI_CERCA_MAX) {
        ph_errore(e, "ricerca: massimo oltre %d", PH_DATI_CERCA_MAX);
        return NULL;
    }
    PhDatiRicerca *r = calloc(1, sizeof *r);
    if (r == NULL) {
        ph_errore(e, "ricerca: memoria finita");
        return NULL;
    }
    r->d = d;
    r->solo_cartelle = q->solo_cartelle;
    r->massimo = q->massimo != 0 ? q->massimo : PH_DATI_CERCA_MAX;
    r->trovato = trovato;
    r->fine = fine;
    r->utente = utente;
    atomic_init(&r->stop, false);
    if (ph_msg_inizia(&r->richiesta, PH_DT_CERCA) != 0 || ph_msg_stringa(&r->richiesta, q->cartella) != 0 ||
        ph_msg_stringa(&r->richiesta, q->testo) != 0 || ph_msg_u8(&r->richiesta, q->solo_cartelle ? 1 : 0) != 0 ||
        ph_msg_u16(&r->richiesta, q->massimo) != 0 || ph_msg_finisci(&r->richiesta) != 0) {
        ph_buffer_libera(&r->richiesta);
        free(r);
        ph_errore(e, "ricerca: memoria finita");
        return NULL;
    }
    atomic_fetch_add(&d->ricerche, 1);
    atomic_fetch_add(&d->da_annullare, 1);
    if (pthread_create(&r->thread, NULL, lavoro, r) != 0) {
        atomic_fetch_sub(&d->ricerche, 1);
        atomic_fetch_sub(&d->da_annullare, 1);
        ph_buffer_libera(&r->richiesta);
        free(r);
        ph_errore(e, "ricerca: impossibile avviare il thread");
        return NULL;
    }
    return r;
}

PhDatiRicerca *ph_dati_cerca_avvia(PhDati *d, const PhDatiCerca *q, PhDatiTrovato trovato,
                                   PhDatiCercaFine fine, void *utente, PhErrore *e)
{
    if (ph_dati_entra(d, e) != 0)
        return NULL;
    PhDatiRicerca *r = avvia_i(d, q, trovato, fine, utente, e);
    ph_dati_esci(d);
    return r;
}

void ph_dati_cerca_annulla(PhDatiRicerca *r)
{
    if (r == NULL)
        return;
    /* Seconda chiamata dallo stesso avviso: gia' fatto tutto (e non si stacca due volte). */
    if (ricerca_corrente == r && r->autolibera)
        return;
    atomic_fetch_sub(&r->d->da_annullare, 1);
    atomic_store(&r->stop, true);
    if (ricerca_corrente == r) {
        /* Da un avviso della ricerca stessa: aspettarsi e' impossibile. Il
         * thread (che e' questo) riprendera' l'esecuzione, vedra' lo stop e
         * liberera' r uscendo; dopo questo ritorno r non si tocca piu'. */
        r->autolibera = true;
        pthread_detach(pthread_self());
        return;
    }
    pthread_join(r->thread, NULL);
    libera_ricerca(r);
}
