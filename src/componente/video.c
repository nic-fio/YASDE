/* Sessioni video: apertura, lettura dei pacchetti H.264 (docs/protocollo.md, 2.3)
 * e richieste collegate (ridimensiona, fotogramma chiave, chiusura). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "componente/interno.h"

#define TESTA_VIDEO 18
#define PEZZO_MAX (256u * 1024u) /* il buffer cresce a pezzi: una lunghezza falsa non alloca tutto subito */

struct PhVideoFlusso {
    PhComponente *c;
    PhCanale *canale;
    uint32_t id;

    pthread_mutex_t m;
    pthread_cond_t cv;
    int lettori;          /* thread dentro ph_video_leggi */
    bool chiuso;          /* ph_video_chiudi in corso o finita */

    /* Stato della lettura, ripreso a ogni chiamata. Lo tocca solo chi legge. */
    uint8_t testa[TESTA_VIDEO];
    size_t fatti;
    int fase;             /* 0 = intestazione, 1 = dati */
    uint32_t lunghezza;
    PhBuffer buf;
    bool finito;
    bool rotto;
    PhErrore errore;
};

static bool misure_valide(uint16_t l, uint16_t a)
{
    return l >= 64 && l <= 2560 && a >= 64 && a <= 2560 && l % 8 == 0 && a % 8 == 0;
}

static void flusso_libera(PhVideoFlusso *f)
{
    pthread_mutex_destroy(&f->m);
    pthread_cond_destroy(&f->cv);
    ph_buffer_libera(&f->buf);
    free(f);
}

PhVideoFlusso *ph_video_apri(PhComponente *c, const PhVideoRichiesta *r, PhErrore *e)
{
    if (c == NULL || r == NULL) {
        ph_errore(e, "apertura video: componente o richiesta mancanti");
        return NULL;
    }
    if (!misure_valide(r->larghezza, r->altezza)) {
        ph_errore(e, "apertura video: misure %ux%u non valide (multipli di 8, da 64 a 2560)",
                  (unsigned)r->larghezza, (unsigned)r->altezza);
        return NULL;
    }
    const uint32_t id = atomic_fetch_add(&c->prossimo_id, 1);
    const char *pacchetto = r->pacchetto != NULL ? r->pacchetto : "";

    PhBuffer msg = PH_BUFFER_VUOTO;
    if (ph_msg_inizia(&msg, PH_MSG_VIDEO_APRI) || ph_msg_u32(&msg, id) ||
        ph_msg_u16(&msg, r->larghezza) || ph_msg_u16(&msg, r->altezza) ||
        ph_msg_u16(&msg, r->densita) || ph_msg_u32(&msg, r->bit_al_secondo) ||
        ph_msg_u8(&msg, r->fps) || ph_msg_stringa(&msg, pacchetto) || ph_msg_finisci(&msg)) {
        ph_buffer_libera(&msg);
        ph_errore(e, "apertura video: richiesta non costruibile (memoria o nome troppo lungo)");
        return NULL;
    }
    /* Il riferimento si prende prima della richiesta: se il componente viene
     * chiuso da un altro thread la struttura resta valida fino alla fine. */
    ph_componente_riferimento(c);
    int ris = ph_componente_richiedi(c, &msg, NULL, c->timeout_ms, e);
    ph_buffer_libera(&msg);
    if (ris != 0) {
        ph_errore_contesto(e, "apertura della sessione video");
        ph_componente_rilascia(c);
        return NULL;
    }

    PhVideoFlusso *f = calloc(1, sizeof *f);
    if (f == NULL) {
        ph_errore(e, "memoria esaurita aprendo il video");
        goto richiudi;
    }
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_mutex_init(&f->m, NULL);
    pthread_cond_init(&f->cv, &ca);
    pthread_condattr_destroy(&ca);
    f->id = id;

    char riga[40];
    snprintf(riga, sizeof riga, "video:%u\n", (unsigned)id);
    f->canale = ph_canale_apri(c->adb, "localabstract:yasde", c->timeout_ms, e);
    if (f->canale == NULL || ph_canale_scrivi(f->canale, riga, strlen(riga), e) != 0) {
        ph_errore_contesto(e, "apertura del canale video %u", (unsigned)id);
        goto richiudi;
    }
    f->c = c; /* il riferimento preso sopra passa al flusso */
    return f;

richiudi:
    /* La sessione esiste gia' sul telefono: va chiusa, senno' resta schermo virtuale acceso. */
    {
        PhBuffer m2 = PH_BUFFER_VUOTO;
        if (ph_msg_inizia(&m2, PH_MSG_VIDEO_CHIUDI) == 0 && ph_msg_u32(&m2, id) == 0 &&
            ph_msg_u8(&m2, 1) == 0 && ph_msg_finisci(&m2) == 0)
            (void)ph_componente_richiedi(c, &m2, NULL, 2000, NULL);
        ph_buffer_libera(&m2);
    }
    if (f != NULL) {
        if (f->canale != NULL)
            ph_canale_chiudi(f->canale);
        flusso_libera(f);
    }
    ph_componente_rilascia(c);
    return NULL;
}

uint32_t ph_video_id(const PhVideoFlusso *f)
{
    return f != NULL ? f->id : 0;
}

static int flusso_rotto(PhVideoFlusso *f, PhErrore *e, const char *frase)
{
    f->rotto = true;
    ph_errore(&f->errore, "%s", frase);
    return ph_errore(e, "%s", frase);
}

/* Un passo di lettura con una fetta di tempo: 1 pacchetto pronto, 0 flusso
 * finito, -1 errore, PH_CANALE_TEMPO fetta scaduta (lo stato resta). */
static int video_passo(PhVideoFlusso *f, PhVideoPacchetto *p, int fetta_ms, PhErrore *e)
{
    PhErrore ei;
    ei.testo[0] = '\0';
    if (f->fase == 0) {
        int r = ph_rip_leggi(f->canale, f->testa, &f->fatti, TESTA_VIDEO, fetta_ms, &ei);
        if (r == PH_CANALE_TEMPO)
            return PH_CANALE_TEMPO;
        if (r < 0)
            return flusso_rotto(f, e, ei.testo[0] ? ei.testo : "lettura del video fallita");
        if (r == 0) {
            if (f->fatti == 0) {
                f->finito = true;
                return 0;
            }
            return flusso_rotto(f, e, "il flusso video si e' interrotto a meta' dell'intestazione");
        }
        uint32_t lung = ((uint32_t)f->testa[14] << 24) | ((uint32_t)f->testa[15] << 16) |
                        ((uint32_t)f->testa[16] << 8) | f->testa[17];
        if (f->testa[0] != 1 && f->testa[0] != 2)
            return flusso_rotto(f, e, "pacchetto video di genere sconosciuto");
        if (lung > PH_VIDEO_MAX_PACCHETTO)
            return flusso_rotto(f, e, "pacchetto video dichiarato oltre il limite di 8 MiB");
        f->lunghezza = lung;
        f->fatti = 0;
        ph_buffer_svuota(&f->buf);
        if (ph_buffer_riserva(&f->buf, 0) != 0)
            return flusso_rotto(f, e, "memoria esaurita leggendo il video");
        f->fase = 1;
    }
    while (f->fatti < f->lunghezza) {
        size_t resto = f->lunghezza - f->fatti;
        size_t pezzo = resto < PEZZO_MAX ? resto : PEZZO_MAX;
        if (ph_buffer_riserva(&f->buf, f->fatti + pezzo) != 0)
            return flusso_rotto(f, e, "memoria esaurita leggendo il video");
        ssize_t r = ph_canale_leggi(f->canale, f->buf.dati + f->fatti, pezzo, fetta_ms, &ei);
        if (r > 0) {
            if ((size_t)r > pezzo)
                return flusso_rotto(f, e, "il canale video ha restituito piu' dati del richiesto");
            f->fatti += (size_t)r;
        } else if (r == PH_CANALE_TEMPO) {
            return PH_CANALE_TEMPO;
        } else if (r == PH_CANALE_CHIUSO) {
            return flusso_rotto(f, e, "il flusso video si e' interrotto a meta' di un pacchetto");
        } else {
            return flusso_rotto(f, e, ei.testo[0] ? ei.testo : "lettura del video fallita");
        }
    }
    f->buf.lunghezza = f->lunghezza;
    f->buf.dati[f->lunghezza] = '\0';
    p->configurazione = f->testa[0] == 1;
    p->chiave = (f->testa[1] & 1) != 0;
    p->larghezza = (uint16_t)((f->testa[2] << 8) | f->testa[3]);
    p->altezza = (uint16_t)((f->testa[4] << 8) | f->testa[5]);
    uint64_t t = 0;
    for (int i = 0; i < 8; i++)
        t = (t << 8) | f->testa[6 + i];
    p->tempo_us = t;
    p->dati = f->buf.dati;
    p->lunghezza = f->lunghezza;
    f->fase = 0;
    f->fatti = 0;
    return 1;
}

int ph_video_leggi(PhVideoFlusso *f, PhVideoPacchetto *p, int timeout_ms, PhErrore *e)
{
    if (f == NULL || p == NULL)
        return ph_errore(e, "lettura video: flusso o pacchetto mancanti");
    pthread_mutex_lock(&f->m);
    if (f->chiuso) {
        pthread_mutex_unlock(&f->m);
        return ph_errore(e, "il flusso video e' chiuso");
    }
    f->lettori++;
    pthread_mutex_unlock(&f->m);

    int ris;
    const int64_t scadenza = timeout_ms < 0 ? -1 : ph_ora_ms() + timeout_ms;
    if (f->rotto) {
        ris = ph_errore(e, "%s", f->errore.testo);
    } else if (f->finito) {
        ris = 0;
    } else {
        for (;;) {
            bool chiuso;
            pthread_mutex_lock(&f->m);
            chiuso = f->chiuso;
            pthread_mutex_unlock(&f->m);
            if (chiuso || atomic_load(&f->c->chiuso)) {
                ris = ph_errore(e, "il flusso video e' stato chiuso durante la lettura");
                break;
            }
            int resta = ph_resta_ms(scadenza);
            int fetta = (resta < 0 || resta > PH_FETTA_MS) ? PH_FETTA_MS : resta;
            ris = video_passo(f, p, fetta, e);
            if (ris != PH_CANALE_TEMPO)
                break;
            if (scadenza >= 0 && ph_resta_ms(scadenza) == 0)
                break;
        }
    }

    pthread_mutex_lock(&f->m);
    f->lettori--;
    pthread_cond_broadcast(&f->cv);
    pthread_mutex_unlock(&f->m);
    return ris;
}

/* Chi sta dentro una richiesta (come chi sta dentro ph_video_leggi) e' contato
 * in `lettori`: ph_video_chiudi aspetta che tornino a zero prima di mandare
 * VIDEO_CHIUDI e di liberare il flusso. Un flusso gia' in chiusura rifiuta. */
static bool flusso_entra(PhVideoFlusso *f, const char *nome, PhErrore *e)
{
    pthread_mutex_lock(&f->m);
    if (f->chiuso) {
        pthread_mutex_unlock(&f->m);
        ph_errore(e, "%s: il flusso video e' chiuso", nome);
        return false;
    }
    f->lettori++;
    pthread_mutex_unlock(&f->m);
    return true;
}

static void flusso_esce(PhVideoFlusso *f)
{
    pthread_mutex_lock(&f->m);
    f->lettori--;
    pthread_cond_broadcast(&f->cv);
    pthread_mutex_unlock(&f->m);
}

/* Richiesta su un flusso: costruita da chi chiama, qui solo l'invio. */
static int richiesta_flusso(PhVideoFlusso *f, PhBuffer *msg, PhBuffer *corpo, PhErrore *e)
{
    int ris = ph_componente_richiedi(f->c, msg, corpo, f->c->timeout_ms, e);
    ph_buffer_libera(msg);
    return ris;
}

static int ridimensiona_interno(PhVideoFlusso *f, uint16_t larghezza, uint16_t altezza,
                          uint16_t densita, uint16_t *larghezza_vera, uint16_t *altezza_vera,
                          PhErrore *e)
{
    if (f == NULL)
        return ph_errore(e, "ridimensionamento: flusso mancante");
    if (!misure_valide(larghezza, altezza))
        return ph_errore(e, "ridimensionamento: misure %ux%u non valide (multipli di 8, da 64 a 2560)",
                         (unsigned)larghezza, (unsigned)altezza);
    PhBuffer msg = PH_BUFFER_VUOTO, corpo = PH_BUFFER_VUOTO;
    int ris = -1;
    if (ph_msg_inizia(&msg, PH_MSG_VIDEO_RIDIMENSIONA) || ph_msg_u32(&msg, f->id) ||
        ph_msg_u16(&msg, larghezza) || ph_msg_u16(&msg, altezza) || ph_msg_u16(&msg, densita) ||
        ph_msg_finisci(&msg)) {
        ph_buffer_libera(&msg);
        ph_errore(e, "ridimensionamento: memoria esaurita");
        goto fine;
    }
    if (richiesta_flusso(f, &msg, &corpo, e) != 0) {
        ph_errore_contesto(e, "ridimensionamento del video %u", (unsigned)f->id);
        goto fine;
    }
    PhLettore l = {corpo.dati, corpo.lunghezza, 0, false};
    uint16_t lv = ph_leggi_u16(&l), av = ph_leggi_u16(&l);
    if (l.rotto) {
        ph_errore(e, "la risposta al ridimensionamento e' troncata");
        goto fine;
    }
    if (larghezza_vera != NULL)
        *larghezza_vera = lv;
    if (altezza_vera != NULL)
        *altezza_vera = av;
    ris = 0;
fine:
    ph_buffer_libera(&corpo);
    return ris;
}

static int chiave_interno(PhVideoFlusso *f, PhErrore *e)
{
    if (f == NULL)
        return ph_errore(e, "richiesta del fotogramma chiave: flusso mancante");
    PhBuffer msg = PH_BUFFER_VUOTO;
    if (ph_msg_inizia(&msg, PH_MSG_VIDEO_CHIAVE) || ph_msg_u32(&msg, f->id) ||
        ph_msg_finisci(&msg)) {
        ph_buffer_libera(&msg);
        return ph_errore(e, "richiesta del fotogramma chiave: memoria esaurita");
    }
    int ris = richiesta_flusso(f, &msg, NULL, e);
    if (ris != 0)
        ph_errore_contesto(e, "richiesta del fotogramma chiave per il video %u", (unsigned)f->id);
    return ris;
}

int ph_video_ridimensiona(PhVideoFlusso *f, uint16_t larghezza, uint16_t altezza,
                          uint16_t densita, uint16_t *larghezza_vera, uint16_t *altezza_vera,
                          PhErrore *e)
{
    if (f == NULL)
        return ph_errore(e, "ridimensionamento: flusso mancante");
    if (!flusso_entra(f, "ridimensionamento", e))
        return -1;
    int ris = ridimensiona_interno(f, larghezza, altezza, densita, larghezza_vera, altezza_vera, e);
    flusso_esce(f);
    return ris;
}

int ph_video_chiave(PhVideoFlusso *f, PhErrore *e)
{
    if (f == NULL)
        return ph_errore(e, "richiesta del fotogramma chiave: flusso mancante");
    if (!flusso_entra(f, "richiesta del fotogramma chiave", e))
        return -1;
    int ris = chiave_interno(f, e);
    flusso_esce(f);
    return ris;
}

static int pausa_o_ripresa_interno(PhVideoFlusso *f, uint8_t tipo, const char *nome, PhErrore *e);

static int pausa_o_ripresa(PhVideoFlusso *f, uint8_t tipo, const char *nome, PhErrore *e)
{
    if (f == NULL)
        return ph_errore(e, "%s: flusso mancante", nome);
    if (!flusso_entra(f, nome, e))
        return -1;
    int ris = pausa_o_ripresa_interno(f, tipo, nome, e);
    flusso_esce(f);
    return ris;
}

/* Pausa e ripresa: stessa richiesta con il solo id. Un flusso in chiusura
 * (ph_video_chiudi in corso) rifiuta; il componente morto lo dice richiedi. */
static int pausa_o_ripresa_interno(PhVideoFlusso *f, uint8_t tipo, const char *nome, PhErrore *e)
{
    if (f == NULL)
        return ph_errore(e, "%s: flusso mancante", nome);
    PhBuffer msg = PH_BUFFER_VUOTO;
    if (ph_msg_inizia(&msg, tipo) || ph_msg_u32(&msg, f->id) || ph_msg_finisci(&msg)) {
        ph_buffer_libera(&msg);
        return ph_errore(e, "%s: memoria esaurita", nome);
    }
    int ris = richiesta_flusso(f, &msg, NULL, e);
    if (ris != 0)
        ph_errore_contesto(e, "%s del video %u", nome, (unsigned)f->id);
    return ris;
}

int ph_video_pausa(PhVideoFlusso *f, PhErrore *e)
{
    return pausa_o_ripresa(f, PH_MSG_VIDEO_PAUSA, "pausa", e);
}

int ph_video_riprendi(PhVideoFlusso *f, PhErrore *e)
{
    return pausa_o_ripresa(f, PH_MSG_VIDEO_RIPRENDI, "ripresa", e);
}

void ph_video_chiudi(PhVideoFlusso *f, bool togli_task)
{
    if (f == NULL)
        return;
    /* Prima si ferma chi legge (esce entro una fetta), poi si chiude il canale:
     * cosi' non si libera mai un canale su cui un altro thread sta leggendo. */
    pthread_mutex_lock(&f->m);
    f->chiuso = true;
    while (f->lettori > 0)
        pthread_cond_wait(&f->cv, &f->m);
    pthread_mutex_unlock(&f->m);

    if (!atomic_load(&f->c->morto)) {
        PhBuffer msg = PH_BUFFER_VUOTO;
        if (ph_msg_inizia(&msg, PH_MSG_VIDEO_CHIUDI) == 0 && ph_msg_u32(&msg, f->id) == 0 &&
            ph_msg_u8(&msg, togli_task ? 1 : 0) == 0 && ph_msg_finisci(&msg) == 0)
            (void)ph_componente_richiedi(f->c, &msg, NULL, 3000, NULL);
        ph_buffer_libera(&msg);
    }
    ph_canale_chiudi(f->canale);
    ph_componente_rilascia(f->c);
    flusso_libera(f);
}
